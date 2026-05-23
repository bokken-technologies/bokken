#pragma once

#include <string>

namespace Bokken
{
    namespace CLI
    {
        /**
         * Client side of the live-reload control channel.
         *
         * The running engine, when started in development mode, listens on a
         * localhost port for newline-delimited JSON messages telling it to
         * hot-swap scripts or assets. This client connects to that port and
         * sends those messages after a successful incremental build.
         *
         * Connection is lazy and forgiving: if the game is not yet running
         * (nothing is listening) a send simply fails quietly and the build
         * still completes, so the watcher can be started before the game and
         * begin signalling as soon as the engine comes up. Each request
         * reconnects if the previous connection dropped (the game was
         * restarted), so the channel survives the game being closed and
         * reopened during a session.
        */
        class DevelopmentClient
        {
        public:
            /** Set the host and port to reach the engine's development
             *  channel. Does not connect yet. */
            void configure(const std::string &host, int port);

            /** Ask the running game to reload its script world from the
             *  freshly packed scripts archive. Returns false if no game was
             *  reachable. */
            bool requestScriptReload();

            /** Ask the running game to remount changed asset packs without
             *  tearing down the script world. Returns false if no game was
             *  reachable. */
            bool requestAssetReload();

        private:
            // Open a short-lived connection, send one newline-terminated
            // message, and close. Returns false if the connection or send
            // failed (typically because nothing is listening yet).
            bool sendMessage(const std::string &jsonLine);

            std::string m_host = "127.0.0.1";
            int m_port = 7878;
        };

    } // namespace CLI
} // namespace Bokken
