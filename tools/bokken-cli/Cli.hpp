#pragma once

#include <string>
#include <vector>

namespace Bokken
{
    namespace CLI
    {
        /**
         * A single CLI subcommand: how it is invoked and what it does.
         *
         * One Command entry is the single source of truth for that command —
         * the dispatcher, the top-level help listing, the per-command
         * `<cmd> --help` page, and the "did you mean?" suggestions all read
         * from the same record, so they can never drift out of sync.
        */
        struct Command
        {
            // Invocation name, e.g. "build". Lowercase, no spaces.
            const char *name;

            // One-line summary shown in the grouped `bokken-cli help`
            // listing. Keep it short — a single clause, no trailing period.
            const char *summary;

            // Usage / argument detail shown by `bokken-cli <name> --help`.
            // May span multiple lines; each line is indented when printed.
            const char *help;

            // Logical group for the help listing (e.g. "Project lifecycle",
            // "Build pipeline", "Diagnostics"). Commands are listed under
            // their group in registration order.
            const char *group;

            // The handler. argv[0] is the subcommand name; the handler parses
            // the rest. Returns the process exit code.
            int (*handler)(int argc, char *argv[]);
        };

        /** The registry of every command bokken-cli exposes, in display
         *  order. Defined once in Cli.cpp. */
        const std::vector<Command> &commands();

        /** Look up a command by exact name, or nullptr if unknown. */
        const Command *findCommand(const std::string &name);

        /** Closest registered command names to `input` by edit distance,
         *  for "did you mean?" hints. Returns at most `max` suggestions,
         *  nearest first, and only those within a small distance threshold. */
        std::vector<std::string> suggestCommands(const std::string &input,
                                                 size_t max = 2);

        /** Render the top-level help: tagline, usage line, and every command
         *  grouped with its one-line summary. Written to `out`. */
        void printHelp(std::ostream &out);

        /** Render the detailed help page for one command (its usage block).
         *  Written to `out`. */
        void printCommandHelp(std::ostream &out, const Command &command);

        /** Print the version banner (name + version + engine compatibility
         *  note) to `out`. */
        void printVersion(std::ostream &out);

        /** The bokken-cli semantic version string, e.g. "1.0.0". */
        const char *version();

        /** Parse argv, dispatch to the matching command, and return its exit
         *  code. Handles --help/-h/help, --version/-V, unknown-command
         *  suggestions, and the no-args case. This is the whole body of
         *  main(). */
        int dispatch(int argc, char *argv[]);
    }
}
