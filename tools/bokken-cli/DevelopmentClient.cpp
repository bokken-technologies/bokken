#include "DevelopmentClient.hpp"

#include <cstring>
#include <string>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Bokken
{
    namespace CLI
    {
        void DevelopmentClient::configure(const std::string &host, int port)
        {
            m_host = host;
            m_port = port;
        }

        bool DevelopmentClient::requestScriptReload()
        {
            // The pack step already produced scripts.assetpack; the engine
            // knows its mount point, so the message only names the action.
            return sendMessage(R"({"type":"reloadScripts"})");
        }

        bool DevelopmentClient::requestAssetReload()
        {
            return sendMessage(R"({"type":"reloadAssets"})");
        }

        bool DevelopmentClient::sendMessage(const std::string &jsonLine)
        {
#if defined(_WIN32)
            // Winsock needs per-process startup. Doing it per send is cheap
            // relative to a build and keeps the client free of global state.
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
                return false;
#endif

            bool ok = false;

#if defined(_WIN32)
            SOCKET handle = socket(AF_INET, SOCK_STREAM, 0);
            const bool valid = handle != INVALID_SOCKET;
#else
            int handle = socket(AF_INET, SOCK_STREAM, 0);
            const bool valid = handle >= 0;
#endif
            if (valid)
            {
                sockaddr_in address;
                std::memset(&address, 0, sizeof(address));
                address.sin_family = AF_INET;
                address.sin_port = htons(static_cast<uint16_t>(m_port));
                inet_pton(AF_INET, m_host.c_str(), &address.sin_addr);

                if (connect(handle, reinterpret_cast<sockaddr *>(&address),
                            sizeof(address)) == 0)
                {
                    const std::string line = jsonLine + "\n";
#if defined(_WIN32)
                    const int sent = send(handle, line.c_str(),
                                          static_cast<int>(line.size()), 0);
#else
                    const ssize_t sent =
                        send(handle, line.c_str(), line.size(), 0);
#endif
                    ok = sent == static_cast<decltype(sent)>(line.size());
                }

#if defined(_WIN32)
                closesocket(handle);
#else
                close(handle);
#endif
            }

#if defined(_WIN32)
            WSACleanup();
#endif
            return ok;
        }

    } // namespace CLI
} // namespace Bokken
