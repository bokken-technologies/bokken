#pragma once

#include <filesystem>
#include <string>

namespace Bokken
{
    namespace CLI
    {
        /**
         * Owns the running game process for `bokken-cli watch --run`.
         *
         * Launches the project's built executable, can terminate and
         * relaunch it across rebuilds (the auto-restart reload tier), and
         * reports whether it is currently alive. The lifetime is tied to the
         * watch session: stopping the watcher stops the game.
         *
         * This is the fallback reload path for changes the engine cannot
         * hot-swap in place (native C++ edits). For script and asset edits
         * the development channel performs an in-process reload and the
         * process is left running; restart() is only used when a full
         * relaunch is genuinely required.
        */
        class GameProcess
        {
        public:
            ~GameProcess();

            GameProcess(const GameProcess &) = delete;
            GameProcess &operator=(const GameProcess &) = delete;
            GameProcess() = default;

            /** Point at the executable to run and the working directory to
             *  run it from. Does not launch yet. */
            void configure(const std::filesystem::path &executable,
                           const std::filesystem::path &workingDirectory);

            /** Launch the game if the executable exists. Returns false if it
             *  is missing or could not be started. A no-op (returns true) if
             *  it is already running. */
            bool launch();

            /** Block until the launched game exits, returning its exit code
             *  (the value passed to exit()/returned from main, or the
             *  negated signal number if it was killed by a signal). Returns
             *  0 if nothing is running. Used by `bokken-cli run` to launch
             *  the game in the foreground and forward its exit status. */
            int wait();

            /** Terminate the running game, if any, and wait for it to exit. */
            void terminate();

            /** Terminate and launch again — used for native rebuilds where an
             *  in-process reload is not possible. */
            bool restart();

            /** True while the launched process is still alive. */
            bool isRunning();

        private:
            std::filesystem::path m_executable;
            std::filesystem::path m_workingDirectory;

#if defined(_WIN32)
            // Process handle (void* to keep <windows.h> out of this header).
            void *m_processHandle = nullptr;
#else
            // Child PID, or -1 when nothing is running.
            int m_pid = -1;
#endif
        };

    } // namespace CLI
} // namespace Bokken
