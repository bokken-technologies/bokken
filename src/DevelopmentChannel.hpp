#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

#include <SDL3/SDL.h>

namespace Bokken
{

    /**
     * Development-only control channel for live reload.
     *
     * Listens on a localhost TCP port for newline-delimited JSON messages
     * from bokken-cli (the watcher) telling the running game to hot-swap
     * scripts or assets after an incremental rebuild. Each message is
     * parsed on a background thread and delivered to the main thread as an
     * SDL user event, so all reload work happens inside the normal event
     * pump with no locking around the renderer or the script runtime.
     *
     * This is compiled and started only in development builds. A shipping
     * build never opens a socket. The listener accepts connections one at a
     * time (the single CLI client) and tolerates the client connecting,
     * disconnecting, and reconnecting across the session as the watcher is
     * stopped and restarted.
     *
     * The SDL user event carries a Request value in its `code` field via a
     * registered event type, so the loop can dispatch without allocating.
    */
    class DevelopmentChannel
    {
    public:
        /** The kind of reload the watcher requested. Delivered in the SDL
         *  user event's `code` field. */
        enum class Request : int32_t
        {
            ReloadScripts = 1,
            ReloadAssets = 2
        };

        DevelopmentChannel() = default;
        ~DevelopmentChannel();

        DevelopmentChannel(const DevelopmentChannel &) = delete;
        DevelopmentChannel &operator=(const DevelopmentChannel &) = delete;

        /** Register the SDL user event type and begin listening on the
         *  given localhost port. Returns false if the event type or the
         *  socket could not be set up; a failure is non-fatal and simply
         *  means live reload is unavailable this session. */
        bool start(int port);

        /** Stop listening and join the background thread. Safe to call more
         *  than once and from the destructor. */
        void stop();

        /** The SDL event type registered for reload requests, or 0 if the
         *  channel never started. The loop compares incoming
         *  SDL_EVENT_USER events against this. */
        uint32_t eventType() const { return m_eventType; }

    private:
        void listenLoop(int port);

        std::thread m_thread;
        std::atomic<bool> m_running{false};
        uint32_t m_eventType = 0;

        // Listening socket file descriptor (or INVALID_SOCKET on Windows).
        // Stored so stop() can unblock accept() by closing it.
        long long m_listenSocket = -1;
    };

} // namespace Bokken
