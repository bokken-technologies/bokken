#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Bokken
{
    namespace CLI
    {
        class Manifest;

        /** A single source file to compile and the bytecode path it
         *  produces. */
        struct CompileJob
        {
            std::string input;
            std::string output;
        };

        /** The set of work a compile pass should perform: bytecode to
         *  (re)generate, orphaned .script files whose sources were removed
         *  and which should therefore be deleted, and the source root the
         *  QuickJS module loader resolves relative imports against. */
        struct CompilePlan
        {
            std::vector<CompileJob> toCompile;
            std::vector<std::string> toDelete;
            std::string sourceRoot;
        };

        /**
         * Decide what needs compiling by comparing the source tree against
         * the manifest.
         *
         * Every .js under sourceDir maps to a .script under outputDir,
         * preserving relative layout. An input is scheduled when it is new,
         * when its mtime/size differ from the manifest and a content hash
         * confirms a real change, or when its expected output is missing.
         * Sources recorded in the manifest but no longer on disk contribute
         * their outputs to toDelete.
         *
         * When restrictTo is non-empty only those absolute input paths are
         * considered — the fast path the watcher uses when it already knows
         * which files changed.
        */
        CompilePlan planCompile(const std::filesystem::path &sourceDir,
                                const std::filesystem::path &outputDir,
                                Manifest &manifest,
                                const std::vector<std::string> &restrictTo = {});

        /**
         * Execute a compile plan. Compiles each job to QuickJS bytecode,
         * deletes orphaned outputs, and updates the manifest in place (the
         * caller saves it). Returns the number of jobs that failed to
         * compile.
        */
        int runCompilePlan(const CompilePlan &plan, Manifest &manifest, bool verbose);

        /**
         * Compile every .js file under --source into QuickJS bytecode
         * (.script) under --output, preserving directory layout.
         *
         * argv[0] is the subcommand name ("compile") and is ignored; the
         * remaining args are:
         *   --source <dir>   required
         *   --output <dir>   required
         *   --verbose | -v   optional
         *   --force          optional — ignore the manifest, rebuild all
         *
         * Returns 0 if every file compiled, 1 otherwise. The same value
         * the dispatcher returns from main().
        */
        int runCompile(int argc, char *argv[]);
    }
}
