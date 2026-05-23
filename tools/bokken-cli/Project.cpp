#include "Project.hpp"
#include "Compile.hpp"
#include "Pack.hpp"
#include "Manifest.hpp"

#include <cstdlib>
#include <iostream>
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

            // Preferred: launch the executable named by the caller (the
            // Makefile passes APP_NAME via --bin). This is exact, so it
            // never picks up a stray binary sharing the bin directory.
            if (!binaryName.empty())
            {
#if defined(_WIN32)
                const fs::path candidate = binDir / (binaryName + ".exe");
#else
                const fs::path candidate = binDir / binaryName;
#endif
                if (fs::is_regular_file(candidate, ec))
                    return candidate;
                // Named binary not present: fall through to the scan so a
                // mismatched name still has a chance of launching something.
            }

            // Fallback: scan the bin directory for the lone executable. Used
            // when no name was given. Skips data and library artifacts, but
            // cannot disambiguate two executables — pass --bin for that.
            for (const auto &entry : fs::directory_iterator(binDir, ec))
            {
                if (ec || !entry.is_regular_file())
                    continue;
                const fs::path path = entry.path();
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
    }
}
