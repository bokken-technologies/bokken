#include "GameProcess.hpp"

#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace Bokken
{
    namespace CLI
    {
        void GameProcess::configure(const fs::path &executable,
                                    const fs::path &workingDirectory)
        {
            m_executable = executable;
            m_workingDirectory = workingDirectory;
        }

#if defined(_WIN32)

        GameProcess::~GameProcess()
        {
            terminate();
        }

        bool GameProcess::launch()
        {
            if (m_processHandle)
                return true;
            if (!fs::exists(m_executable))
            {
                std::cerr << "[watch] game executable not found: "
                          << m_executable << "\n";
                return false;
            }

            STARTUPINFOW startupInfo;
            ZeroMemory(&startupInfo, sizeof(startupInfo));
            startupInfo.cb = sizeof(startupInfo);

            PROCESS_INFORMATION processInfo;
            ZeroMemory(&processInfo, sizeof(processInfo));

            std::wstring commandLine = m_executable.wstring();
            const std::wstring workingDir = m_workingDirectory.wstring();

            // CreateProcessW may modify the command-line buffer, so pass a
            // mutable copy.
            std::wstring mutableCommand = commandLine;

            const BOOL ok = CreateProcessW(
                nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, 0,
                nullptr, workingDir.empty() ? nullptr : workingDir.c_str(),
                &startupInfo, &processInfo);

            if (!ok)
            {
                std::cerr << "[watch] failed to launch game\n";
                return false;
            }

            CloseHandle(processInfo.hThread);
            m_processHandle = processInfo.hProcess;
            return true;
        }

        void GameProcess::terminate()
        {
            if (!m_processHandle)
                return;
            HANDLE handle = static_cast<HANDLE>(m_processHandle);
            TerminateProcess(handle, 0);
            WaitForSingleObject(handle, INFINITE);
            CloseHandle(handle);
            m_processHandle = nullptr;
        }

        bool GameProcess::isRunning()
        {
            if (!m_processHandle)
                return false;
            HANDLE handle = static_cast<HANDLE>(m_processHandle);
            DWORD code = 0;
            if (GetExitCodeProcess(handle, &code) && code == STILL_ACTIVE)
                return true;
            // Process exited on its own; reap the handle.
            CloseHandle(handle);
            m_processHandle = nullptr;
            return false;
        }

        int GameProcess::wait()
        {
            if (!m_processHandle)
                return 0;
            HANDLE handle = static_cast<HANDLE>(m_processHandle);
            WaitForSingleObject(handle, INFINITE);
            DWORD code = 0;
            GetExitCodeProcess(handle, &code);
            CloseHandle(handle);
            m_processHandle = nullptr;
            return static_cast<int>(code);
        }

#else

        GameProcess::~GameProcess()
        {
            terminate();
        }

        bool GameProcess::launch()
        {
            if (m_pid > 0)
                return true;
            if (!fs::exists(m_executable))
            {
                std::cerr << "[watch] game executable not found: "
                          << m_executable << "\n";
                return false;
            }

            const pid_t pid = fork();
            if (pid < 0)
            {
                std::cerr << "[watch] fork failed\n";
                return false;
            }

            // Resolve to an absolute path before the child changes directory,
            // otherwise a relative executable path would not be found after
            // chdir into the working directory.
            std::error_code ec;
            const fs::path absoluteExecutable = fs::absolute(m_executable, ec);
            const std::string executablePath =
                ec ? m_executable.string() : absoluteExecutable.string();

            if (pid == 0)
            {
                // Child: switch to the working directory and exec the game.
                if (!m_workingDirectory.empty())
                {
                    if (chdir(m_workingDirectory.c_str()) != 0)
                        _exit(127);
                }
                execl(executablePath.c_str(), executablePath.c_str(),
                      static_cast<char *>(nullptr));
                // execl only returns on failure.
                _exit(127);
            }

            m_pid = pid;
            return true;
        }

        void GameProcess::terminate()
        {
            if (m_pid <= 0)
                return;
            kill(m_pid, SIGTERM);

            // Give the game a moment to exit cleanly, then force it.
            for (int i = 0; i < 50; i++)
            {
                int status = 0;
                pid_t result;
                do
                {
                    result = waitpid(m_pid, &status, WNOHANG);
                } while (result < 0 && errno == EINTR);

                // Only stop waiting when the child has actually terminated.
                // A stopped/continued status (result == m_pid but
                // !WIFEXITED && !WIFSIGNALED) does NOT mean it's gone, so we
                // keep waiting and ultimately SIGKILL. A hard waitpid error
                // (ECHILD) means there's nothing left to reap.
                if (result < 0)
                {
                    m_pid = -1;
                    return;
                }
                if (result == m_pid &&
                    (WIFEXITED(status) || WIFSIGNALED(status)))
                {
                    m_pid = -1;
                    return;
                }
                usleep(20 * 1000);
            }
            kill(m_pid, SIGKILL);
            waitpid(m_pid, nullptr, 0);
            m_pid = -1;
        }

        bool GameProcess::isRunning()
        {
            if (m_pid <= 0)
                return false;

            int status = 0;
            pid_t result;
            // Retry across EINTR so a signal delivered to the watcher
            // doesn't make us misjudge the child as gone.
            do
            {
                result = waitpid(m_pid, &status, WNOHANG);
            } while (result < 0 && errno == EINTR);

            if (result == 0)
            {
                // No state change reported: the child is alive and running.
                return true;
            }

            if (result < 0)
            {
                // waitpid failed. ECHILD means "not our child / already
                // reaped" — but on the platforms and launch paths we use
                // that should not happen for a live game, and reporting it
                // as dead here is exactly what caused spurious relaunches
                // while the window was still up. Treat any waitpid error
                // conservatively as "still running" and let a real
                // SDL_EVENT_QUIT / exit drive teardown instead. We do NOT
                // clear m_pid, so a later genuine exit can still be seen.
                return true;
            }

            // result == m_pid: a state change was reported. Only an actual
            // termination means the process is gone. A merely stopped or
            // continued child (SIGSTOP/SIGCONT, which the macOS window
            // server can trigger) is still alive — previously this branch
            // treated ANY non-zero result as death, the core false-negative.
            if (WIFEXITED(status) || WIFSIGNALED(status))
            {
                m_pid = -1;
                return false;
            }

            // Stopped or continued — still our process, still alive.
            return true;
        }

        int GameProcess::wait()
        {
            if (m_pid <= 0)
                return 0;

            for (;;)
            {
                int status = 0;
                const pid_t result = waitpid(m_pid, &status, 0);
                if (result < 0)
                {
                    if (errno == EINTR)
                        continue;  // interrupted by a signal — keep waiting
                    // ECHILD or other error: nothing left to wait on.
                    m_pid = -1;
                    return 0;
                }
                if (WIFEXITED(status))
                {
                    m_pid = -1;
                    return WEXITSTATUS(status);
                }
                if (WIFSIGNALED(status))
                {
                    m_pid = -1;
                    // Convention: report a signal death as a negative code so
                    // callers can distinguish it from a clean non-zero exit.
                    return -WTERMSIG(status);
                }
                // Stopped/continued (job control): keep waiting for real exit.
            }
        }

#endif

        bool GameProcess::restart()
        {
            terminate();
            return launch();
        }

    } // namespace CLI
} // namespace Bokken
