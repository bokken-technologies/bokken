#pragma once

namespace Bokken
{
    namespace CLI
    {
        /**
         * One-shot build: transpile TypeScript, compile scripts to bytecode,
         * pack scripts and assets, and run the native CMake build — the
         * non-watch equivalent of a full `make build`, driven entirely by
         * the CLI.
         *
         * argv[0] is the subcommand name ("build") and is ignored. Args:
         *   --project <dir>   project root (default: current directory)
         *   --no-native       skip the cmake build step (content only)
         *   --force           ignore incremental caches, rebuild everything
         *
         * Returns 0 on success, non-zero if any stage failed.
        */
        int runBuild(int argc, char *argv[]);

        /**
         * Build the project, then launch the game and wait for it to exit —
         * the CLI-native equivalent of `make run`. This is the everyday
         * "see my changes" command: it runs the same pipeline as `build`
         * (TypeScript, bytecode, asset packs, native), then starts the
         * deployed executable from its bin/ directory so the runtime layout
         * (executable + project.bokken + assets/ + engine library) resolves.
         *
         * argv[0] is the subcommand name ("run") and is ignored. Args:
         *   --project <dir>   project root (default: current directory)
         *   --force           ignore incremental caches, rebuild everything
         *   --no-build        skip the build and launch whatever is already
         *                     deployed (fast path when nothing changed)
         *
         * Returns the game's exit code on a clean launch, or non-zero if the
         * build failed or no executable could be found.
        */
        int runRun(int argc, char *argv[]);

        /**
         * (Re)configure the native build without compiling — the CLI-native
         * equivalent of `make setup`. Forces the next build to re-run CMake
         * configure (and TypeScript) by clearing the incremental stamps,
         * which is the right move when CMake state is wedged but you don't
         * want to nuke the whole build tree and trigger a fresh engine clone.
         * The first invocation in a clean tree is also what bootstraps the
         * engine via FetchContent and builds bokken-cli itself.
         *
         * argv[0] is the subcommand name ("setup") and is ignored. Args:
         *   --project <dir>   project root (default: current directory)
         *
         * Returns 0 on a successful configure, non-zero otherwise.
        */
        int runSetup(int argc, char *argv[]);

        /**
         * Remove build artifacts and incremental caches for a project.
         *
         * argv[0] is the subcommand name ("clean") and is ignored. Args:
         *   --project <dir>   project root (default: current directory)
         *   --all             also remove the CMake FetchContent cache
         *                     (forces a fresh engine clone next build)
         *
         * Returns 0 on success.
        */
        int runClean(int argc, char *argv[]);

        /**
         * Check that the toolchain the CLI and engine builds depend on is
         * present, printing each tool's version. Reports node/npx, cmake,
         * and git, plus whether the current directory looks like a Bokken
         * project.
         *
         * argv[0] is the subcommand name ("doctor") and is ignored. Args:
         *   --project <dir>   project root to inspect (default: current dir)
         *
         * Returns 0 when every required tool is present, 1 otherwise.
        */
        int runDoctor(int argc, char *argv[]);
    }
}
