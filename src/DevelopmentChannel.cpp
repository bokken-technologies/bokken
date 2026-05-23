#include "DevelopmentChannel.hpp"

#include <cstring>
#include <string>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
static constexpr SocketHandle k_invalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
static constexpr SocketHandle k_invalidSocket = -1;
#endif

namespace Bokken
{
    namespace
    {
        void closeSocket(long long handle)
        {
            if (handle < 0)
                return;
#if defined(_WIN32)
            closesocket(static_cast<SocketHandle>(handle));
#else
            close(static_cast<SocketHandle>(handle));
#endif
        }

        // Map a parsed message body to a request code. The protocol is tiny
        // and fixed, so a substring test for the type value is enough and
        // avoids pulling a JSON parser into the engine's hot startup path.
        bool parseRequest(const std::string &message,
                          DevelopmentChannel::Request &outRequest)
        {
            if (message.find("reloadScripts") != std::string::npos)
            {
                outRequest = DevelopmentChannel::Request::ReloadScripts;
                return true;
            }
            if (message.find("reloadAssets") != std::string::npos)
            {
                outRequest = DevelopmentChannel::Request::ReloadAssets;
                return true;
            }
            return false;
        }
    }

    DevelopmentChannel::~DevelopmentChannel()
    {
        stop();
    }

    bool DevelopmentChannel::start(int port)
    {
        m_eventType = SDL_RegisterEvents(1);
        if (m_eventType == 0 || m_eventType == static_cast<uint32_t>(-1))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[DevelopmentChannel] could not register SDL event type; "
                        "live reload disabled");
            m_eventType = 0;
            return false;
        }

#if defined(_WIN32)
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
            return false;
#endif

        SocketHandle listener = socket(AF_INET, SOCK_STREAM, 0);
        if (listener == k_invalidSocket)
            return false;

        int reuse = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char *>(&reuse), sizeof(reuse));

        sockaddr_in address;
        std::memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<uint16_t>(port));
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

        if (bind(listener, reinterpret_cast<sockaddr *>(&address),
                 sizeof(address)) != 0 ||
            listen(listener, 1) != 0)
        {
            closeSocket(static_cast<long long>(listener));
            return false;
        }

        m_listenSocket = static_cast<long long>(listener);
        m_running.store(true);
        m_thread = std::thread(&DevelopmentChannel::listenLoop, this, port);

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "[DevelopmentChannel] live reload listening on 127.0.0.1:%d",
                    port);
        return true;
    }

    void DevelopmentChannel::stop()
    {
        if (!m_running.exchange(false))
        {
            // Already stopped; still ensure any half-open socket is closed.
            if (m_listenSocket >= 0)
            {
                closeSocket(m_listenSocket);
                m_listenSocket = -1;
            }
            return;
        }

        // Closing the listening socket unblocks the accept() in the thread.
        if (m_listenSocket >= 0)
        {
            closeSocket(m_listenSocket);
            m_listenSocket = -1;
        }
        if (m_thread.joinable())
            m_thread.join();

#if defined(_WIN32)
        WSACleanup();
#endif
    }

    void DevelopmentChannel::listenLoop(int /*port*/)
    {
        while (m_running.load())
        {
            const SocketHandle listener =
                static_cast<SocketHandle>(m_listenSocket);
            if (listener == k_invalidSocket)
                break;

            SocketHandle client = accept(listener, nullptr, nullptr);
            if (client == k_invalidSocket)
            {
                // accept() returns an error when stop() closes the listener;
                // the running flag tells us whether that was intentional.
                if (!m_running.load())
                    break;
                continue;
            }

            // Read one batch of newline-delimited messages from this client
            // and dispatch each. The watcher opens a fresh connection per
            // message, so a short read loop until close is sufficient.
            std::string buffer;
            char chunk[512];
            for (;;)
            {
#if defined(_WIN32)
                const int received = recv(client, chunk, sizeof(chunk), 0);
#else
                const ssize_t received = recv(client, chunk, sizeof(chunk), 0);
#endif
                if (received <= 0)
                    break;
                buffer.append(chunk, static_cast<size_t>(received));

                // Dispatch every complete line currently in the buffer.
                size_t newline;
                while ((newline = buffer.find('\n')) != std::string::npos)
                {
                    const std::string line = buffer.substr(0, newline);
                    buffer.erase(0, newline + 1);

                    Request request;
                    if (parseRequest(line, request))
                    {
                        SDL_Event event;
                        SDL_zero(event);
                        event.type = m_eventType;
                        event.user.code = static_cast<int32_t>(request);
                        // Thread-safe: SDL_PushEvent may be called from any
                        // thread, which is exactly why the channel runs here
                        // and the work happens on the main thread.
                        SDL_PushEvent(&event);
                    }
                }
            }

            closeSocket(static_cast<long long>(client));
        }
    }

} // namespace Bokken
