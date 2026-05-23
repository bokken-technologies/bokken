#include "Cli.hpp"

#include "Commands.hpp"
#include "Compile.hpp"
#include "Pack.hpp"
#include "WatchCommand.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>  // isatty
#endif

#define BOKKEN_CLI_VERSION "0.1.0"

namespace
{
    // ANSI styling. Disabled automatically when output is not a TTY or when
    // NO_COLOR / BOKKEN_NO_COLOR is set, so piped output and CI logs stay
    // clean. Resolved once on first use.
    bool colorEnabled()
    {
        static const bool enabled = []
        {
            if (std::getenv("NO_COLOR") || std::getenv("BOKKEN_NO_COLOR"))
                return false;
#if defined(_WIN32)
            // Modern Windows terminals honour ANSI; older ones don't, but we
            // keep it simple and let NO_COLOR opt out.
            return true;
#else
            return ::isatty(1) != 0;
#endif
        }();
        return enabled;
    }

    const char *c(const char *code) { return colorEnabled() ? code : ""; }

    constexpr const char *kReset = "\033[0m";
    constexpr const char *kBold = "\033[1m";
    constexpr const char *kDim = "\033[2m";
    constexpr const char *kCyan = "\033[36m";
    constexpr const char *kYel = "\033[33m";
    constexpr const char *kRed = "\033[31m";

    // Case-insensitive Levenshtein distance, capped — only used for short
    // command names, so the simple O(n*m) table is fine.
    size_t editDistance(const std::string &a, const std::string &b)
    {
        const size_t n = a.size(), m = b.size();
        std::vector<size_t> prev(m + 1), cur(m + 1);
        for (size_t j = 0; j <= m; j++)
            prev[j] = j;
        for (size_t i = 1; i <= n; i++)
        {
            cur[0] = i;
            for (size_t j = 1; j <= m; j++)
            {
                const char ca = static_cast<char>(std::tolower(a[i - 1]));
                const char cb = static_cast<char>(std::tolower(b[j - 1]));
                const size_t cost = (ca == cb) ? 0 : 1;
                cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
            }
            std::swap(prev, cur);
        }
        return prev[m];
    }
}

namespace Bokken
{
    namespace CLI
    {
        const char *version() { return BOKKEN_CLI_VERSION; }

        const std::vector<Command> &commands()
        {
            // Single source of truth. Order here is the order shown in help,
            // within each group. Groups cluster the everyday lifecycle
            // commands first, then the lower-level pipeline pieces, then
            // diagnostics.
            static const std::vector<Command> table = {
                {"setup",
                 "Configure the native build (first-time bootstrap)",
                 "bokken-cli setup [--project <dir>]\n"
                 "\n"
                 "Clears the incremental stamps and re-runs CMake configure. The\n"
                 "first run in a clean tree fetches the engine via FetchContent and\n"
                 "builds bokken-cli itself. Run this when CMake state is wedged but\n"
                 "you don't want to wipe the build tree (and re-clone the engine).",
                 "Project lifecycle", &runSetup},

                {"build",
                 "Compile scripts, pack assets, and build the native game",
                 "bokken-cli build [--project <dir>] [--force] [--no-native]\n"
                 "\n"
                 "Runs the full pipeline: TypeScript -> QuickJS bytecode -> asset\n"
                 "packs -> native CMake build. Incremental by default; --force\n"
                 "ignores the caches. --no-native does content only (skips cmake).\n"
                 "  --project <dir>   project root (default: current directory)\n"
                 "  --force           rebuild everything, ignore caches\n"
                 "  --no-native       skip the native build (scripts + assets only)",
                 "Project lifecycle", &runBuild},

                {"run",
                 "Build, then launch the game and wait for it",
                 "bokken-cli run [--project <dir>] [--force] [--no-build]\n"
                 "\n"
                 "The everyday command: builds (same pipeline as `build`), then\n"
                 "launches the deployed executable from its bin/ directory and\n"
                 "forwards its exit code.\n"
                 "  --project <dir>   project root (default: current directory)\n"
                 "  --force           rebuild everything before launching\n"
                 "  --no-build        launch what's already deployed (no rebuild)",
                 "Project lifecycle", &runRun},

                {"watch",
                 "Live-reload: rebuild and hot-swap on file changes",
                 "bokken-cli watch [--project <dir>] [--run] [--bin <name>]\n"
                 "                 [--no-assets] [--port <n>]\n"
                 "\n"
                 "Watches src/, types/, assets/ and tsconfig.json; on each change\n"
                 "rebuilds only what's affected and signals the running game to\n"
                 "hot-swap scripts or assets in place.\n"
                 "  --run             launch the game and keep it live across reloads\n"
                 "  --bin <name>      exact executable to (re)launch under --run\n"
                 "  --no-assets       watch scripts only, skip asset repacking\n"
                 "  --port <n>        dev-channel port for live reload (default 7878)",
                 "Project lifecycle", &runWatch},

                {"clean",
                 "Remove build artifacts and incremental caches",
                 "bokken-cli clean [--project <dir>] [--all]\n"
                 "\n"
                 "Removes transpiled output, bytecode, packed assets, and the\n"
                 "incremental caches, keeping the CMake + FetchContent cache so the\n"
                 "next build doesn't re-clone the engine.\n"
                 "  --all   also remove the FetchContent cache (fresh engine clone)",
                 "Project lifecycle", &runClean},

                {"compile",
                 "Compile transpiled JS to QuickJS bytecode",
                 "bokken-cli compile --source <dir> --output <dir>\n"
                 "                   [--verbose] [--force]\n"
                 "\n"
                 "Low-level pipeline step. Compiles every .js under <source> into\n"
                 ".script bytecode under <output>, preserving layout. Normally\n"
                 "invoked for you by `build`; exposed for tooling and debugging.\n"
                 "  --verbose | -v    list each file as it compiles\n"
                 "  --force           ignore the manifest, recompile everything",
                 "Build pipeline", &runCompile},

                {"pack",
                 "Pack a directory tree into .assetpack archives",
                 "bokken-cli pack <input-dir> <output-prefix> [--force]\n"
                 "\n"
                 "Low-level pipeline step. Packs a tree into one or more .assetpack\n"
                 "files (quality tiers / asset groups handled automatically; a\n"
                 "\"scripts\" prefix packs with a scripts/ mount). Normally invoked\n"
                 "for you by `build`.\n"
                 "  --force   ignore the manifest, repack everything",
                 "Build pipeline", &runPack},

                {"doctor",
                 "Check the toolchain and project health",
                 "bokken-cli doctor [--project <dir>]\n"
                 "\n"
                 "Verifies node/npx, cmake, and git are present (with versions),\n"
                 "and whether the target directory looks like a Bokken project.\n"
                 "Exit code is non-zero if a required tool is missing.",
                 "Diagnostics", &runDoctor},
            };
            return table;
        }

        const Command *findCommand(const std::string &name)
        {
            for (const Command &cmd : commands())
                if (name == cmd.name)
                    return &cmd;
            return nullptr;
        }

        std::vector<std::string> suggestCommands(const std::string &input,
                                                 size_t max)
        {
            // Score every command by edit distance; keep the closest few
            // within a tolerance that scales a little with input length so
            // "biuld" -> "build" but random noise suggests nothing.
            std::vector<std::pair<size_t, std::string>> scored;
            for (const Command &cmd : commands())
                scored.emplace_back(editDistance(input, cmd.name), cmd.name);
            std::sort(scored.begin(), scored.end());

            const size_t threshold = std::max<size_t>(2, input.size() / 2);
            std::vector<std::string> out;
            for (const auto &[dist, name] : scored)
            {
                if (dist == 0 || dist > threshold)
                    continue;
                out.push_back(name);
                if (out.size() >= max)
                    break;
            }
            return out;
        }

        void printVersion(std::ostream &out)
        {
            out << c(kBold) << "bokken-cli" << c(kReset) << " " << version()
                << "\n"
                << c(kDim)
                << "The unified command-line tool for the Bokken engine."
                << c(kReset) << "\n";
        }

        void printHelp(std::ostream &out)
        {
            out << c(kBold) << "bokken-cli" << c(kReset) << " "
                << c(kDim) << version() << c(kReset)
                << " — build, run, and ship Bokken games.\n\n";

            out << c(kBold) << "USAGE" << c(kReset) << "\n"
                << "  bokken-cli <command> [options]\n"
                << "  bokken-cli <command> --help        " << c(kDim)
                << "details for one command" << c(kReset) << "\n\n";

            // Group commands in first-seen group order, preserving the
            // registration order within each group.
            std::vector<std::string> groupOrder;
            for (const Command &cmd : commands())
            {
                if (std::find(groupOrder.begin(), groupOrder.end(), cmd.group) ==
                    groupOrder.end())
                    groupOrder.push_back(cmd.group);
            }

            // Column width for aligned summaries.
            size_t widest = 0;
            for (const Command &cmd : commands())
                widest = std::max(widest, std::strlen(cmd.name));
            const size_t col = widest + 4;

            for (const std::string &group : groupOrder)
            {
                out << c(kBold) << group << c(kReset) << "\n";
                for (const Command &cmd : commands())
                {
                    if (group != cmd.group)
                        continue;
                    std::string pad(col - std::strlen(cmd.name), ' ');
                    out << "  " << c(kCyan) << cmd.name << c(kReset) << pad
                        << cmd.summary << "\n";
                }
                out << "\n";
            }

            out << c(kDim)
                << "Run `bokken-cli doctor` to check your toolchain"
                << c(kReset) << "\n";
        }

        void printCommandHelp(std::ostream &out, const Command &command)
        {
            out << c(kBold) << command.name << c(kReset) << " — "
                << command.summary << "\n\n";
            // Indent each line of the help block by two spaces.
            const std::string help = command.help;
            size_t start = 0;
            while (start <= help.size())
            {
                const size_t nl = help.find('\n', start);
                const std::string line =
                    help.substr(start, nl == std::string::npos
                                           ? std::string::npos
                                           : nl - start);
                if (line.empty())
                    out << "\n";
                else
                    out << "  " << line << "\n";
                if (nl == std::string::npos)
                    break;
                start = nl + 1;
            }
        }

        int dispatch(int argc, char *argv[])
        {
            if (argc < 2)
            {
                printHelp(std::cerr);
                return 1;
            }

            const std::string first = argv[1];

            if (first == "--help" || first == "-h" || first == "help")
            {
                // `help <command>` shows that command's page.
                if (argc >= 3)
                {
                    if (const Command *cmd = findCommand(argv[2]))
                    {
                        printCommandHelp(std::cout, *cmd);
                        return 0;
                    }
                    std::cerr << c(kRed) << "Unknown command '" << argv[2]
                              << "'." << c(kReset) << "\n";
                    printHelp(std::cerr);
                    return 1;
                }
                printHelp(std::cout);
                return 0;
            }

            if (first == "--version" || first == "-V" || first == "version")
            {
                printVersion(std::cout);
                return 0;
            }

            const Command *cmd = findCommand(first);
            if (!cmd)
            {
                std::cerr << c(kRed) << "bokken-cli: unknown command '" << first
                          << "'." << c(kReset) << "\n";
                const std::vector<std::string> hints = suggestCommands(first);
                if (!hints.empty())
                {
                    std::cerr << "\nDid you mean";
                    for (size_t i = 0; i < hints.size(); i++)
                        std::cerr << (i ? " or " : " ") << c(kCyan) << hints[i]
                                  << c(kReset);
                    std::cerr << "?\n";
                }
                std::cerr << "\nRun `" << c(kBold) << "bokken-cli help"
                          << c(kReset) << "` to see all commands.\n";
                return 1;
            }

            // A bare `<command> --help` / `-h` prints that command's page
            // without invoking the handler.
            for (int i = 2; i < argc; i++)
            {
                if (std::strcmp(argv[i], "--help") == 0 ||
                    std::strcmp(argv[i], "-h") == 0)
                {
                    printCommandHelp(std::cout, *cmd);
                    return 0;
                }
            }

            // Hand off: the handler sees argv[0] == the subcommand name,
            // matching the convention every handler is written against.
            return cmd->handler(argc - 1, argv + 1);
        }
    }
}
