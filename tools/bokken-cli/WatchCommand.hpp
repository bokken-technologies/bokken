#pragma once

namespace Bokken
{
    namespace CLI
    {
        /**
         * Watch a project for changes and rebuild only what changed,
         * keeping the running game up to date for fast iteration.
         *
         * argv[0] is the subcommand name ("watch") and is ignored. The
         * remaining args are:
         *   --project <dir>   project root (default: current directory)
         *   --no-assets       watch scripts only, skip asset repacking
         *   --run             launch the game and keep it running across
         *                     rebuilds (auto-restart on each successful build)
         *   --bin <name>      exact executable name to launch under --run
         *                     (the project's APP_NAME; the Makefile passes
         *                     this). Without it, bin/ is scanned for the
         *                     lone executable.
         *   --port <number>   development-channel port for live reload
         *                     (default: 7878)
         *
         * On startup the watcher performs one incremental sync so edits made
         * while it was not running are caught, then blocks watching src/,
         * types/, assets/, and tsconfig.json. Each debounced batch triggers
         * only the build stages the changed paths imply; a failed build
         * leaves the last good artifacts in place and is not signalled to
         * the running game.
         *
         * Returns 0 on a clean exit (Ctrl-C), non-zero on a fatal setup
         * error (no project found, watcher could not start).
        */
        int runWatch(int argc, char *argv[]);
    }
}
