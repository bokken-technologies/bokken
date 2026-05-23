#include "Project.hpp"
#include "Compile.hpp"
#include "Pack.hpp"
#include "Manifest.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    constexpr const char *kReset = "\033[0m";
    constexpr const char *kCyan = "\033[36m";
}

namespace Bokken
{
    namespace CLI
    {
        std::string detectPlatform()
        {
#if defined(_WIN32)
            return "windows";
#elif defined(__APPLE__)
            return "darwin";
#else
            return "linux";
#endif
        }

        fs::path ProjectLayout::bytecodeDirectory() const
        {
            // Bytecode staging lives beside the build tree, NOT under bin/.
            // Keeping it out of bin/ ensures the folder-driven asset packer
            // never sweeps raw .script files into an archive, and the name
            // matches the CMake pipeline's COMPILED_SCRIPTS_DIR so the two
            // build paths stage bytecode in the same place.
            return root / "build" / platform / "compiled";
        }

        fs::path ProjectLayout::buildDirectory() const
        {
            return root / "build" / platform;
        }

        fs::path ProjectLayout::binDirectory() const
        {
            return root / "build" / platform / "bin";
        }

        ProjectLayout resolveLayout(const fs::path &root)
        {
            ProjectLayout layout;
            layout.root = root;
            layout.platform = detectPlatform();
            layout.sourceDirectory = root / "src";
            layout.typesDirectory = root / "types";
            layout.assetsDirectory = root / "assets";
            layout.transpiledDirectory =
                root / "build" / layout.platform / "transpiled";
            layout.scriptsOutput =
                root / "build" / layout.platform / "bin" / "assets" / "scripts";
            layout.tsconfig = root / "tsconfig.json";
            return layout;
        }

        bool runTypeScript(const ProjectLayout &layout)
        {
            std::error_code ec;
            fs::create_directories(layout.transpiledDirectory, ec);

            // Use TypeScript's own incremental mode: --incremental persists a
            // .tsbuildinfo so tsc recompiles only files whose inputs changed,
            // matching the incrementality of the bytecode and pack stages.
            // The build-info file lives in the build tree, not the source.
            const fs::path buildInfo =
                layout.buildDirectory() / "tsconfig.tsbuildinfo";
            std::error_code infoEc;
            fs::create_directories(buildInfo.parent_path(), infoEc);

            const std::string command =
                "npx tsc --project \"" + layout.tsconfig.string() +
                "\" --outDir \"" + layout.transpiledDirectory.string() +
                "\" --incremental --tsBuildInfoFile \"" + buildInfo.string() + "\"";
            std::cout << kCyan << "  [tsc] " << kReset
                      << "transpiling TypeScript...\n";
            return std::system(command.c_str()) == 0;
        }

        bool compileAndPackScripts(const ProjectLayout &layout, bool force)
        {
            const fs::path bytecodeDir = layout.bytecodeDirectory();

            Manifest manifest;
            const fs::path manifestPath = bytecodeDir / ".compile-cache.json";
            if (!force)
                manifest.load(manifestPath);

            const CompilePlan plan =
                planCompile(layout.transpiledDirectory, bytecodeDir, manifest);
            const int failures = runCompilePlan(plan, manifest, false);
            manifest.save(manifestPath);
            if (failures != 0)
                return false;

            // Pack the bytecode into the scripts assetpack via the public
            // pack entry so the "scripts/" prefix convention is applied in
            // exactly one place.
            std::vector<std::string> args = {
                "pack", bytecodeDir.string(), layout.scriptsOutput.string()};
            if (force)
                args.push_back("--force");
            std::vector<char *> argv;
            for (std::string &arg : args)
                argv.push_back(arg.data());
            return runPack(static_cast<int>(argv.size()), argv.data()) == 0;
        }

        bool repackAssets(const ProjectLayout &layout, bool force)
        {
            if (!fs::exists(layout.assetsDirectory))
                return true;

            const fs::path outputBase = layout.binDirectory() / "assets";
            std::error_code ec;
            fs::create_directories(outputBase, ec);

            bool ok = true;
            for (const auto &entry :
                 fs::directory_iterator(layout.assetsDirectory, ec))
            {
                if (ec || !entry.is_directory())
                    continue;
                const std::string category = entry.path().filename().string();
                const std::string outputPrefix = (outputBase / category).string();

                std::vector<std::string> args = {
                    "pack", entry.path().string(), outputPrefix};
                if (force)
                    args.push_back("--force");
                std::vector<char *> argv;
                for (std::string &arg : args)
                    argv.push_back(arg.data());
                ok = (runPack(static_cast<int>(argv.size()), argv.data()) == 0) && ok;
            }
            return ok;
        }

        bool nativeBuild(const ProjectLayout &layout, bool configureIfNeeded)
        {
            const fs::path buildDir = layout.buildDirectory();
            const fs::path cmakeCache = buildDir / "CMakeCache.txt";

            if (configureIfNeeded && !fs::exists(cmakeCache))
            {
                std::error_code ec;
                fs::create_directories(buildDir, ec);
                std::string configure =
                    "cmake -S \"" + layout.root.string() + "\" -B \"" +
                    buildDir.string() + "\" -DCMAKE_BUILD_TYPE=Debug " +
                    "-DSCRIPTS_DIR=\"" + layout.transpiledDirectory.string() + "\"";

                // Honour BOKKEN_ENGINE_TAG from the environment so the CLI
                // build path can pin the engine revision exactly like the
                // template Makefile's `make build BOKKEN_ENGINE_TAG=...`.
                if (const char *tag = std::getenv("BOKKEN_ENGINE_TAG");
                    tag != nullptr && tag[0] != '\0')
                {
                    configure +=
                        " -DBOKKEN_ENGINE_TAG=\"" + std::string(tag) + "\"";
                }

                std::cout << kCyan << "  [cmake] " << kReset << "configuring...\n";
                if (std::system(configure.c_str()) != 0)
                    return false;
            }

            const std::string build =
                "cmake --build \"" + buildDir.string() + "\"";
            std::cout << kCyan << "  [cmake] " << kReset << "building...\n";
            return std::system(build.c_str()) == 0;
        }

        fs::path resolveGameExecutable(const ProjectLayout &layout,
                                       const std::string &binaryName)
        {
            const fs::path binDir = layout.binDirectory();
            std::error_code ec;
            if (!fs::is_directory(binDir, ec))
                return {};

            // Preferred and AUTHORITATIVE: when the caller names the binary
            // (via --bin, or derived from the CMake project() name), launch
            // exactly that file. If it isn't present we return empty so the
            // caller can fail with a clear message — we deliberately do NOT
            // fall through to the scan, because the build tree is full of
            // stray executables (bokken-cli, and the vendored QuickJS tooling:
            // lre-test, qjsc, qjs_exe, run-test262, api-test, …) and scanning
            // could launch one of those by mistake. A confident wrong launch
            // is worse than an honest failure.
            if (!binaryName.empty())
            {
#if defined(_WIN32)
                const fs::path candidate = binDir / (binaryName + ".exe");
#else
                const fs::path candidate = binDir / binaryName;
#endif
                if (fs::is_regular_file(candidate, ec))
                    return candidate;
                return {};
            }

            // Fallback scan: only reached when NO name was given at all. Skips
            // data/library artifacts and the known stray executables that the
            // engine's vendored builds drop into the tree. This still cannot
            // safely disambiguate two real game executables — pass --bin (or
            // rely on the CMake project name) for a deterministic launch.
            static const char *kNonGameStems[] = {
                "bokken-cli", "lre-test", "qjsc", "qjs_exe", "qjs",
                "run-test262", "api-test", "function_source",
            };
            for (const auto &entry : fs::directory_iterator(binDir, ec))
            {
                if (ec || !entry.is_regular_file())
                    continue;
                const fs::path path = entry.path();
                const std::string stem = path.stem().string();
                bool skip = false;
                for (const char *s : kNonGameStems)
                    if (stem == s) { skip = true; break; }
                if (skip)
                    continue;
                const std::string extension = path.extension().string();
                if (extension == ".assetpack" || extension == ".so" ||
                    extension == ".dylib" || extension == ".dll" ||
                    extension == ".json")
                    continue;
#if defined(_WIN32)
                if (extension == ".exe")
                    return path;
#else
                if (extension.empty())
                    return path;
#endif
            }
            return {};
        }

        std::string resolveAppName(const ProjectLayout &layout)
        {
            const fs::path cmakeLists = layout.root / "CMakeLists.txt";
            std::ifstream in(cmakeLists);
            if (!in)
                return {};

            // Match `project(<name> ...)`. The name is the first token after
            // the opening paren; it may be quoted. We deliberately match the
            // FIRST project() call, which is the executable's name — the
            // template wraps it in an if(APPLE)/else() with the same name in
            // both branches, so first-match is correct either way. Comment
            // lines (starting with #) are skipped so a commented-out project()
            // can't fool us.
            static const std::regex projectRe(
                R"RE(^\s*project\s*\(\s*"?([A-Za-z0-9_.\-]+)"?)RE",
                std::regex::icase);

            std::string line;
            while (std::getline(in, line))
            {
                const size_t firstNonSpace = line.find_first_not_of(" \t");
                if (firstNonSpace != std::string::npos &&
                    line[firstNonSpace] == '#')
                    continue;

                std::smatch m;
                if (std::regex_search(line, m, projectRe))
                    return m[1].str();
            }
            return {};
        }
    }
}