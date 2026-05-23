#include "WatchCommand.hpp"
#include "Watch.hpp"
#include "Project.hpp"
#include "DevelopmentClient.hpp"
#include "GameProcess.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    constexpr const char *kReset = "\033[0m";
    constexpr const char *kBold = "\033[1m";
    constexpr const char *kGreen = "\033[32m";
    constexpr const char *kCyan = "\033[36m";
    constexpr const char *kRed = "\033[31m";

    using Bokken::CLI::ProjectLayout;

    // Classification of a debounced batch into the build stages it requires.
    struct BuildNeeds
    {
        bool scripts = false;   // .ts/.tsx/.js or tsconfig → tsc + compile + pack
        bool assets = false;    // assets/** → repack assets
        bool native = false;    // a *.cpp / *.hpp / CMakeLists → cmake build
    };

    bool pathIsUnder(const fs::path &path, const fs::path &directory)
    {
        const std::string p = fs::weakly_canonical(path).string();
        const std::string d = fs::weakly_canonical(directory).string();
        return p.rfind(d, 0) == 0;
    }

    BuildNeeds classify(const std::vector<Bokken::CLI::FileChange> &changes,
                        const ProjectLayout &layout)
    {
        BuildNeeds needs;
        for (const Bokken::CLI::FileChange &change : changes)
        {
            const fs::path path(change.path);
            const std::string name = path.filename().string();
            const std::string extension = path.extension().string();

            if (name == "tsconfig.json" ||
                pathIsUnder(path, layout.sourceDirectory) ||
                pathIsUnder(path, layout.typesDirectory))
            {
                if (extension == ".ts" || extension == ".tsx" ||
                    extension == ".js" || name == "tsconfig.json")
                    needs.scripts = true;
            }
            if (pathIsUnder(path, layout.assetsDirectory))
                needs.assets = true;
            if (extension == ".cpp" || extension == ".hpp" ||
                extension == ".h" || extension == ".cc" ||
                name == "CMakeLists.txt")
                needs.native = true;
        }
        return needs;
    }
}

int Bokken::CLI::runWatch(int argc, char *argv[])
{
    fs::path projectRoot = fs::current_path();
    bool watchAssets = true;
    bool runGame = false;
    int port = 7878;
    std::string binaryName;  // --bin: exact executable name to launch under --run

    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--project") == 0 && i + 1 < argc)
            projectRoot = argv[++i];
        else if (std::strcmp(argv[i], "--no-assets") == 0)
            watchAssets = false;
        else if (std::strcmp(argv[i], "--run") == 0)
            runGame = true;
        else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc)
            port = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--bin") == 0 && i + 1 < argc)
            binaryName = argv[++i];
    }

    const ProjectLayout layout = resolveLayout(projectRoot);

    if (!fs::exists(layout.sourceDirectory))
    {
        std::cerr << kRed << "Error: no src/ directory under " << projectRoot
                  << " — is this a Bokken project?" << kReset << "\n";
        return 1;
    }

    std::cout << kBold << "bokken-cli watch" << kReset << " — "
              << projectRoot.string() << "\n";

    // Startup sync: catch anything edited while the watcher was off.
    std::cout << kCyan << "Initial sync..." << kReset << "\n";
    if (runTypeScript(layout))
        compileAndPackScripts(layout, false);
    if (watchAssets)
        repackAssets(layout, false);

    // The development client connects lazily; if the game is not yet running
    // a failed connect is silent and reload signals are skipped until the
    // engine is up and listening.
    DevelopmentClient developmentClient;
    developmentClient.configure("127.0.0.1", port);

    // With --run, the watcher owns the game process: it launches it now and
    // restarts it on native rebuilds. Script and asset edits are delivered
    // in-process via the development channel, so the process keeps running
    // across those.
    GameProcess gameProcess;
    if (runGame)
    {
        const fs::path executable = resolveGameExecutable(layout, binaryName);
        if (executable.empty())
        {
            std::cerr << kRed << "Warning: --run was given but no built game "
                      << "executable was found under "
                      << layout.binDirectory().string()
                      << ". Build the project first (bokken-cli build)."
                      << kReset << "\n";
        }
        else
        {
            gameProcess.configure(executable, executable.parent_path());
            if (gameProcess.launch())
                std::cout << kGreen << "Launched "
                          << executable.filename().string() << kReset << "\n";
        }
    }

    Watcher watcher;
    const std::chrono::milliseconds debounce(150);

    std::vector<fs::path> roots = {layout.sourceDirectory, layout.tsconfig};
    if (fs::exists(layout.typesDirectory))
        roots.push_back(layout.typesDirectory);
    if (watchAssets && fs::exists(layout.assetsDirectory))
        roots.push_back(layout.assetsDirectory);

    std::cout << kGreen << "Watching for changes. Press Ctrl-C to stop."
              << kReset << "\n";

    const bool started = watcher.start(
        roots, debounce,
        [&](const std::vector<FileChange> &changes)
        {
            const BuildNeeds needs = classify(changes, layout);
            if (!needs.scripts && !needs.assets && !needs.native)
                return;

            bool ok = true;
            if (needs.scripts)
            {
                std::cout << kCyan << "\u21bb scripts changed" << kReset << "\n";
                if (runTypeScript(layout))
                    ok = compileAndPackScripts(layout, false) && ok;
                else
                    ok = false;
            }
            if (needs.assets)
            {
                std::cout << kCyan << "\u21bb assets changed" << kReset << "\n";
                ok = repackAssets(layout, false) && ok;
            }

            if (needs.native)
            {
                // A native source changed: the running process cannot
                // hot-swap C++. Rebuild the binary, and under --run relaunch
                // it. Without --run, report that a manual relaunch is needed.
                std::cout << kCyan << "\u21bb native source changed" << kReset << "\n";
                const bool built = nativeBuild(layout, true);
                ok = built && ok;
                if (built && runGame)
                {
                    std::cout << kCyan << "  restarting game..." << kReset << "\n";
                    gameProcess.restart();
                    std::cout << kGreen << "\u2713 rebuilt and relaunched"
                              << kReset << "\n";
                    return;
                }
                if (built && !runGame)
                    std::cout << kCyan << "  rebuilt — relaunch the game to "
                              << "pick up native changes" << kReset << "\n";
            }

            if (!ok)
            {
                std::cerr << kRed << "\u2717 build failed — keeping last good "
                          << "artifacts; not reloading" << kReset << "\n";
                return;
            }

            // Script and asset edits are delivered to the running game
            // in-process over the development channel — the window is NEVER
            // torn down for them. If the game is alive (the common case),
            // send the appropriate hot-swap request and we are done.
            //
            // Only when the game is genuinely gone (it crashed, or the user
            // closed the window) do we relaunch, and a relaunch already
            // loads the freshly packed artifacts off disk, so no reload
            // signal is needed afterwards. Sending one would race the new
            // process's startup before its dev channel is listening.
            if (runGame && !gameProcess.isRunning())
            {
                std::cout << kCyan << "  game not running — relaunching"
                          << kReset << "\n";
                gameProcess.launch();
            }
            else
            {
                if (needs.assets && !needs.scripts)
                    developmentClient.requestAssetReload();
                else if (needs.scripts)
                    developmentClient.requestScriptReload();
            }

            std::cout << kGreen << "\u2713 up to date" << kReset << "\n";
        });

    if (!started)
    {
        std::cerr << kRed << "Error: could not start the file watcher."
                  << kReset << "\n";
        return 1;
    }

    return 0;
}
