#include "Commands.hpp"
#include "Project.hpp"
#include "GameProcess.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace
{
    constexpr const char *kReset = "\033[0m";
    constexpr const char *kBold = "\033[1m";
    constexpr const char *kGreen = "\033[32m";
    constexpr const char *kCyan = "\033[36m";
    constexpr const char *kRed = "\033[31m";
    constexpr const char *kYel = "\033[33m";

    // Parse a --project flag out of argv; default to the current directory.
    fs::path projectRootFrom(int argc, char *argv[])
    {
        for (int i = 1; i < argc; i++)
        {
            if (std::strcmp(argv[i], "--project") == 0 && i + 1 < argc)
                return argv[i + 1];
        }
        return fs::current_path();
    }

    bool hasFlag(int argc, char *argv[], const char *flag)
    {
        for (int i = 1; i < argc; i++)
        {
            if (std::strcmp(argv[i], flag) == 0)
                return true;
        }
        return false;
    }

    // Read the value argument following a flag (e.g. `--bin Massive` yields
    // "Massive"). Returns an empty string if the flag is absent or has no
    // following token.
    std::string flagValue(int argc, char *argv[], const char *flag)
    {
        for (int i = 1; i < argc; i++)
        {
            if (std::strcmp(argv[i], flag) == 0 && i + 1 < argc)
                return argv[i + 1];
        }
        return {};
    }

    // Run a command and capture its first line of stdout (for version
    // probes). Returns false if the command could not be run.
    bool captureFirstLine(const std::string &command, std::string &outLine)
    {
        std::array<char, 512> buffer{};
#if defined(_WIN32)
        FILE *pipe = _popen((command + " 2>nul").c_str(), "r");
#else
        FILE *pipe = popen((command + " 2>/dev/null").c_str(), "r");
#endif
        if (!pipe)
            return false;

        bool gotLine = false;
        if (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe))
        {
            outLine = buffer.data();
            // Trim trailing newline.
            while (!outLine.empty() &&
                   (outLine.back() == '\n' || outLine.back() == '\r'))
                outLine.pop_back();
            gotLine = true;
        }

#if defined(_WIN32)
        _pclose(pipe);
#else
        pclose(pipe);
#endif
        return gotLine;
    }
}

int Bokken::CLI::runBuild(int argc, char *argv[])
{
    const fs::path projectRoot = projectRootFrom(argc, argv);
    const bool noNative = hasFlag(argc, argv, "--no-native");
    const bool force = hasFlag(argc, argv, "--force");

    const ProjectLayout layout = resolveLayout(projectRoot);
    if (!fs::exists(layout.sourceDirectory))
    {
        std::cerr << kRed << "Error: no src/ directory under " << projectRoot
                  << " — is this a Bokken project?" << kReset << "\n";
        return 1;
    }

    std::cout << kBold << "bokken-cli build" << kReset << " — "
              << projectRoot.string() << "\n";

    bool ok = true;

    std::cout << kCyan << "[1/4] TypeScript" << kReset << "\n";
    if (!runTypeScript(layout))
    {
        std::cerr << kRed << "TypeScript transpile failed." << kReset << "\n";
        return 1;
    }

    std::cout << kCyan << "[2/4] Scripts" << kReset << "\n";
    ok = compileAndPackScripts(layout, force) && ok;

    std::cout << kCyan << "[3/4] Assets" << kReset << "\n";
    ok = repackAssets(layout, force) && ok;

    if (!noNative)
    {
        std::cout << kCyan << "[4/4] Native" << kReset << "\n";
        ok = nativeBuild(layout, true) && ok;
    }
    else
    {
        std::cout << kCyan << "[4/4] Native — skipped (--no-native)"
                  << kReset << "\n";
    }

    if (!ok)
    {
        std::cerr << kRed << "Build finished with errors." << kReset << "\n";
        return 1;
    }
    std::cout << kGreen << "Build complete." << kReset << "\n";
    return 0;
}

int Bokken::CLI::runRun(int argc, char *argv[])
{
    const fs::path projectRoot = projectRootFrom(argc, argv);
    const bool force = hasFlag(argc, argv, "--force");
    const bool noBuild = hasFlag(argc, argv, "--no-build");

    const ProjectLayout layout = resolveLayout(projectRoot);
    if (!fs::exists(layout.sourceDirectory))
    {
        std::cerr << kRed << "Error: no src/ directory under " << projectRoot
                  << " — is this a Bokken project?" << kReset << "\n";
        return 1;
    }

    // Build first (unless told not to). Reuse the exact build pipeline so
    // `run` can never launch a stale binary against fresh sources.
    if (!noBuild)
    {
        std::cout << kBold << "bokken-cli run" << kReset << " — building "
                  << projectRoot.string() << "\n";

        bool ok = true;
        std::cout << kCyan << "[1/4] TypeScript" << kReset << "\n";
        if (!runTypeScript(layout))
        {
            std::cerr << kRed << "TypeScript transpile failed." << kReset << "\n";
            return 1;
        }
        std::cout << kCyan << "[2/4] Scripts" << kReset << "\n";
        ok = compileAndPackScripts(layout, force) && ok;
        std::cout << kCyan << "[3/4] Assets" << kReset << "\n";
        ok = repackAssets(layout, force) && ok;
        std::cout << kCyan << "[4/4] Native" << kReset << "\n";
        ok = nativeBuild(layout, true) && ok;

        if (!ok)
        {
            std::cerr << kRed << "Build failed — not launching." << kReset << "\n";
            return 1;
        }
    }

    // Resolve which executable to launch, by name. The build deploys the
    // game AND bokken-cli into the same bin/ directory, so a blind "first
    // executable" scan can pick the wrong one — we must name the target.
    // Priority: an explicit --bin <name>, else the CMake project() name (which
    // is what names the output binary), else an empty name that falls back to
    // the (bokken-cli-excluding) scan in resolveGameExecutable.
    std::string binaryName = flagValue(argc, argv, "--bin");
    if (binaryName.empty())
        binaryName = resolveAppName(layout);

    const fs::path executable = resolveGameExecutable(layout, binaryName);
    if (executable.empty())
    {
        std::cerr << kRed << "Error: ";
        if (!binaryName.empty())
        {
            std::cerr << "executable '" << binaryName << "' not found in "
                      << layout.binDirectory().string() << ".\n"
                      << "       The name comes from your CMake project() "
                         "declaration"
                      << (hasFlag(argc, argv, "--bin") ? " (overridden by --bin)"
                                                       : "")
                      << "; pass --bin <name> if your executable is named "
                         "differently.";
        }
        else
        {
            std::cerr << "no game executable found in "
                      << layout.binDirectory().string()
                      << " — pass --bin <name> to specify which to launch.";
        }
        if (noBuild)
            std::cerr << "\n       (Running with --no-build; drop it to build "
                         "first.)";
        std::cerr << kReset << "\n";
        return 1;
    }

    // Launch from the executable's own directory so the self-contained
    // runtime layout (project.bokken + assets/ + engine library) resolves,
    // then block until the game exits and forward its exit code.
    std::cout << kGreen << "Launching " << executable.filename().string()
              << kReset << "\n";

    GameProcess game;
    game.configure(executable, executable.parent_path());
    if (!game.launch())
    {
        std::cerr << kRed << "Error: failed to launch "
                  << executable.string() << kReset << "\n";
        return 1;
    }

    const int code = game.wait();
    if (code != 0)
        std::cout << kYel << "Game exited with code " << code << kReset << "\n";
    return code;
}

int Bokken::CLI::runSetup(int argc, char *argv[])
{
    const fs::path projectRoot = projectRootFrom(argc, argv);

    const ProjectLayout layout = resolveLayout(projectRoot);
    if (!fs::exists(layout.sourceDirectory))
    {
        std::cerr << kRed << "Error: no src/ directory under " << projectRoot
                  << " — is this a Bokken project?" << kReset << "\n";
        return 1;
    }

    std::cout << kBold << "bokken-cli setup" << kReset << " — "
              << projectRoot.string() << "\n";

    // Clear the incremental stamps so the next build re-runs TypeScript and
    // re-runs CMake configure, without nuking the build tree (which would
    // force a fresh, slow engine re-clone). Mirrors `make setup`.
    const fs::path buildDir = layout.buildDirectory();
    std::error_code ec;
    for (const char *stamp : {".tsc.stamp", ".compile.stamp"})
    {
        const fs::path p = buildDir / stamp;
        if (fs::exists(p))
        {
            fs::remove(p, ec);
            std::cout << "  cleared " << p.filename().string() << "\n";
        }
    }
    const fs::path cache = buildDir / "CMakeCache.txt";
    if (fs::exists(cache))
    {
        fs::remove(cache, ec);
        std::cout << "  cleared CMakeCache.txt (forces reconfigure)\n";
    }

    // Re-run configure now (the heavy first-time step that fetches the
    // engine and builds bokken-cli) so a subsequent `build` / `run` is a
    // straight compile. configureIfNeeded handles the missing-cache case.
    std::cout << kCyan << "Configuring native build..." << kReset << "\n";
    if (!nativeBuild(layout, /*configureIfNeeded=*/true))
    {
        // nativeBuild also builds; if configure succeeded but you only
        // wanted setup, that's still fine — the artifacts are reusable.
        std::cerr << kRed << "Configure/build reported errors." << kReset << "\n";
        return 1;
    }

    std::cout << kGreen << "Setup complete — `bokken-cli run` is ready."
              << kReset << "\n";
    return 0;
}

int Bokken::CLI::runClean(int argc, char *argv[])
{
    const fs::path projectRoot = projectRootFrom(argc, argv);
    const bool all = hasFlag(argc, argv, "--all");

    const ProjectLayout layout = resolveLayout(projectRoot);
    const fs::path buildDir = layout.buildDirectory();

    std::error_code ec;
    if (!fs::exists(buildDir))
    {
        std::cout << kYel << "Nothing to clean (" << buildDir.string()
                  << " does not exist)." << kReset << "\n";
        return 0;
    }

    if (all)
    {
        // Remove the whole build directory, including the FetchContent
        // cache, forcing a fresh engine clone on the next build.
        const auto removed = fs::remove_all(buildDir, ec);
        if (ec)
        {
            std::cerr << kRed << "Error removing " << buildDir.string() << ": "
                      << ec.message() << kReset << "\n";
            return 1;
        }
        std::cout << kGreen << "Removed " << buildDir.string() << " ("
                  << removed << " entries, including FetchContent cache)."
                  << kReset << "\n";
        return 0;
    }

    // Default clean: remove transpiled output, bytecode, packed assets, and
    // the incremental caches — but keep the CMake build tree and the
    // FetchContent cache so the next build doesn't re-clone the engine.
    const std::array<fs::path, 3> targets = {
        layout.transpiledDirectory, layout.bytecodeDirectory(),
        layout.binDirectory() / "assets"};

    for (const fs::path &target : targets)
    {
        if (fs::exists(target))
        {
            fs::remove_all(target, ec);
            std::cout << "  removed " << target.string() << "\n";
        }
    }

    std::cout << kGreen << "Clean complete (kept CMake + FetchContent cache; "
              << "use --all to remove everything)." << kReset << "\n";
    return 0;
}

int Bokken::CLI::runDoctor(int argc, char *argv[])
{
    const fs::path projectRoot = projectRootFrom(argc, argv);

    std::cout << kBold << "bokken-cli doctor" << kReset << "\n";

    struct Tool
    {
        const char *name;
        const char *probe;
        bool required;
    };
    const std::array<Tool, 4> tools = {{
        {"node", "node --version", true},
        {"npx", "npx --version", true},
        {"cmake", "cmake --version", true},
        {"git", "git --version", true},
    }};

    bool allPresent = true;
    for (const Tool &tool : tools)
    {
        std::string line;
        if (captureFirstLine(tool.probe, line) && !line.empty())
        {
            std::cout << kGreen << "  \u2713 " << kReset << tool.name << "  "
                      << line << "\n";
        }
        else
        {
            std::cout << kRed << "  \u2717 " << kReset << tool.name
                      << "  not found";
            if (tool.required)
            {
                std::cout << kRed << "  (required)" << kReset;
                allPresent = false;
            }
            std::cout << "\n";
        }
    }

    // Project sanity: does the target directory look like a Bokken project?
    const ProjectLayout layout = resolveLayout(projectRoot);
    const bool looksLikeProject =
        fs::exists(layout.sourceDirectory) &&
        fs::exists(layout.root / "project.bokken");
    std::cout << (looksLikeProject ? kGreen : kYel)
              << "  " << (looksLikeProject ? "\u2713" : "\u2014") << " " << kReset
              << "project  "
              << (looksLikeProject
                      ? projectRoot.string() + " looks like a Bokken project"
                      : projectRoot.string() +
                            " has no src/ + project.bokken (not a project root?)")
              << "\n";

    if (!allPresent)
    {
        std::cerr << kRed << "Some required tools are missing." << kReset << "\n";
        return 1;
    }
    std::cout << kGreen << "All required tools present." << kReset << "\n";
    return 0;
}