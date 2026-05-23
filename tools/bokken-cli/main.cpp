/**
 * bokken-cli — the unified command-line tool for the Bokken engine.
 *
 * This file is intentionally tiny: it is just the process entry point.
 * Every command, its help text, and the dispatch logic live in the command
 * registry in Cli.cpp, which is the single source of truth so that the
 * dispatcher, the help listing, per-command help, and "did you mean?"
 * suggestions can never drift apart.
 *
 * The full command surface (run `bokken-cli help` for the formatted view):
 *
 *   Project lifecycle
 *     new      Scaffold a new project from the template
 *     setup    Configure the native build (first-time bootstrap)
 *     build    Compile scripts, pack assets, and build the native game
 *     run      Build, then launch the game and wait for it
 *     watch    Live-reload: rebuild and hot-swap on file changes
 *     clean    Remove build artifacts and incremental caches
 *
 *   Build pipeline (normally invoked for you by `build`)
 *     compile  Compile transpiled JS to QuickJS bytecode
 *     pack     Pack a directory tree into .assetpack archives
 *
 *   Diagnostics
 *     doctor   Check the toolchain and project health
 *
 * Typical workflow, entirely make-free after the first build:
 *
 *     bokken-cli new --name "My Game"
 *     cd my-game
 *     bokken-cli run            # first run bootstraps the engine + CLI
 *     bokken-cli watch --run    # live-reload loop while you work
 *
 * Layout note:
 *   This source lives at tools/bokken-cli/ inside the engine repo. The
 *   engine's CMakeLists builds it as a sibling target alongside
 *   BokkenEngine, linked against the same vendored qjs / miniz the engine
 *   consumes.
 */

#include "Cli.hpp"

int main(int argc, char *argv[])
{
    return Bokken::CLI::dispatch(argc, argv);
}
