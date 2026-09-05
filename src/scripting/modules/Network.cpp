#include "Network.hpp"

namespace Bokken
{
    namespace Scripting
    {
        namespace Modules
        {
            namespace
            {

                // How long Address::parse() and a UDPClient/UDPServer's implicit
                // hostname resolution are allowed to block, in milliseconds.
                // These are the only blocking points in the module; everything
                // else (TCP connect/accept/read) is pumped by Network::poll().
                constexpr Sint32 kDefaultResolveTimeoutMs = 5000;

                // Scratch buffer size for draining a TCP stream socket per poll.
                constexpr int kTcpReadChunkSize = 8192;

                struct AddressPayload
                {
                    NET_Address *addr; // owns one reference
                    uint16_t port;     // 0 if not meaningful (e.g. accepted-client peer)
                };

                struct MessagePayload
                {
                    std::vector<uint8_t> buf;
                    size_t cursor = 0;
                };

                enum class TCPState
                {
                    ResolvingAddress,
                    Connecting,
                    Connected,
                    Reconnecting, // waiting out reconnectDelay before retrying
                    Closed
                };

                struct TCPClientPayload
                {
                    std::string host;            // retained for reconnect attempts
                    NET_Address *addr = nullptr; // ref'd target address
                    uint16_t port = 0;
                    NET_StreamSocket *sock = nullptr;
                    TCPState state = TCPState::ResolvingAddress;

                    bool isServerSide = false;          // accepted by a TCPServer
                    JSValue ownerServer = JS_UNDEFINED; // dup'd, server-side only

                    bool autoReconnect = false;
                    Sint32 reconnectDelayMs = 1000;
                    int maximumReconnectAttempts = -1; // -1 = infinite
                    int reconnectAttempts = 0;
                    Uint64 reconnectAtTicks = 0;

                    Sint32 connectTimeoutMs = 0; // 0 = wait forever
                    Uint64 deadlineTicks = 0;    // valid only if connectTimeoutMs > 0

                    uint64_t bytesSent = 0;
                    uint64_t bytesReceived = 0;

                    JSValue onConnect = JS_UNDEFINED;
                    JSValue onDisconnect = JS_UNDEFINED;
                    JSValue onError = JS_UNDEFINED;
                    JSValue onMessage = JS_UNDEFINED;
                };

                struct TCPServerPayload
                {
                    NET_Server *server = nullptr;
                    bool closed = false;

                    // dup'd TCPClient JS objects for currently-accepted clients.
                    std::vector<JSValue> clients;

                    JSValue onConnect = JS_UNDEFINED;
                    JSValue onDisconnect = JS_UNDEFINED;
                    JSValue onError = JS_UNDEFINED;
                    JSValue onMessage = JS_UNDEFINED;
                };

                struct UDPClientPayload
                {
                    NET_DatagramSocket *sock = nullptr;
                    bool closed = false;

                    NET_Address *defaultAddr = nullptr; // ref'd
                    uint16_t defaultPort = 0;

                    uint64_t bytesSent = 0;
                    uint64_t bytesReceived = 0;

                    JSValue onMessage = JS_UNDEFINED;
                    JSValue onError = JS_UNDEFINED;
                };

                struct KnownPeer
                {
                    NET_Address *addr; // ref'd
                    uint16_t port;
                };

                struct UDPServerPayload
                {
                    NET_DatagramSocket *sock = nullptr;
                    bool closed = false;

                    std::vector<KnownPeer> knownPeers;

                    uint64_t bytesSent = 0;
                    uint64_t bytesReceived = 0;

                    JSValue onMessage = JS_UNDEFINED;
                    JSValue onError = JS_UNDEFINED;
                };

                // Holding a strong (dup'd) reference here is what keeps a socket
                // "running" even if script code doesn't keep its own variable
                // around — matching the fire-and-forget style implied by passing
                // callbacks straight into the constructor. The reference is
                // dropped (and the JSValue freed) when the socket fully closes.

                std::vector<JSValue> &tcpClientRegistry()
                {
                    static std::vector<JSValue> v;
                    return v;
                }
                std::vector<JSValue> &tcpServerRegistry()
                {
                    static std::vector<JSValue> v;
                    return v;
                }
                std::vector<JSValue> &udpClientRegistry()
                {
                    static std::vector<JSValue> v;
                    return v;
                }
                std::vector<JSValue> &udpServerRegistry()
                {
                    static std::vector<JSValue> v;
                    return v;
                }

                void unregister(std::vector<JSValue> &reg, JSContext *ctx, JSValueConst v)
                {
                    for (size_t i = 0; i < reg.size(); ++i)
                    {
                        if (JS_VALUE_GET_PTR(reg[i]) == JS_VALUE_GET_PTR(v))
                        {
                            JS_FreeValue(ctx, reg[i]);
                            reg.erase(reg.begin() + static_cast<long>(i));
                            return;
                        }
                    }
                }

                bool splitHostPort(const std::string &uri, std::string &host, uint16_t &port)
                {
                    auto pos = uri.find_last_of(':');
                    if (pos == std::string::npos || pos == uri.size() - 1)
                        return false;
                    host = uri.substr(0, pos);
                    long p = std::strtol(uri.c_str() + pos + 1, nullptr, 10);
                    if (p <= 0 || p > 65535)
                        return false;
                    port = static_cast<uint16_t>(p);
                    return true;
                }

                // Resolve a hostname and block (bounded) until it settles.
                // Returns true on success, setting *outAddr to a referenced
                // NET_Address the caller now owns (or to nullptr if `host` means
                // "any local address" - empty, "*", or "0.0.0.0"). Returns false
                // with a JS exception already thrown if resolution failed.
                bool resolveBlocking(JSContext *ctx, const std::string &host, Sint32 timeoutMs, NET_Address **outAddr)
                {
                    *outAddr = nullptr;
                    if (host.empty() || host == "*" || host == "0.0.0.0")
                        return true;

                    NET_Address *addr = NET_ResolveHostname(host.c_str());
                    if (!addr)
                    {
                        JS_ThrowTypeError(ctx, "network: failed to resolve '%s': %s", host.c_str(), SDL_GetError());
                        return false;
                    }
                    NET_Status status = NET_WaitUntilResolved(addr, timeoutMs);
                    if (status != NET_SUCCESS)
                    {
                        JS_ThrowTypeError(ctx, "network: could not resolve '%s': %s",
                                          host.c_str(), status == NET_WAITING ? "timed out" : SDL_GetError());
                        NET_UnrefAddress(addr);
                        return false;
                    }
                    *outAddr = addr;
                    return true;
                }

                JSValue wrapAddress(JSContext *ctx, NET_Address *addr, uint16_t port)
                {
                    // addr may be null (e.g. peer address unavailable); caller must
                    // have already ref'd it appropriately - this takes ownership.
                    JSValue obj = JS_NewObjectClass(ctx, Network::s_address_class_id);
                    if (JS_IsException(obj))
                        return obj;
                    auto *p = new AddressPayload{addr, port};
                    JS_SetOpaque(obj, p);
                    return obj;
                }

                AddressPayload *unwrapAddress(JSValueConst v)
                {
                    return static_cast<AddressPayload *>(JS_GetOpaque(v, Network::s_address_class_id));
                }

                JSValue wrapMessage(JSContext *ctx, std::vector<uint8_t> &&bytes)
                {
                    JSValue obj = JS_NewObjectClass(ctx, Network::s_message_class_id);
                    if (JS_IsException(obj))
                        return obj;
                    auto *p = new MessagePayload{std::move(bytes), 0};
                    JS_SetOpaque(obj, p);
                    return obj;
                }

                MessagePayload *unwrapMessage(JSValueConst v)
                {
                    return static_cast<MessagePayload *>(JS_GetOpaque(v, Network::s_message_class_id));
                }

                TCPClientPayload *unwrapTcpClient(JSValueConst v)
                {
                    return static_cast<TCPClientPayload *>(JS_GetOpaque(v, Network::s_tcpclient_class_id));
                }
                TCPServerPayload *unwrapTcpServer(JSValueConst v)
                {
                    return static_cast<TCPServerPayload *>(JS_GetOpaque(v, Network::s_tcpserver_class_id));
                }
                UDPClientPayload *unwrapUdpClient(JSValueConst v)
                {
                    return static_cast<UDPClientPayload *>(JS_GetOpaque(v, Network::s_udpclient_class_id));
                }
                UDPServerPayload *unwrapUdpServer(JSValueConst v)
                {
                    return static_cast<UDPServerPayload *>(JS_GetOpaque(v, Network::s_udpserver_class_id));
                }

                // Reads a Uint8Array/ArrayBuffer-backed JS value into a byte
                // pointer + length. Returns false (and leaves an exception set)
                // if `v` isn't byte data.
                bool readBytesArg(JSContext *ctx, JSValueConst v, uint8_t **out, size_t *len)
                {
                    size_t byteOffset = 0, byteLength = 0, bytesPerElement = 0;
                    JSValue buf = JS_GetTypedArrayBuffer(ctx, v, &byteOffset, &byteLength, &bytesPerElement);
                    if (JS_IsException(buf))
                    {
                        JS_ThrowTypeError(ctx, "network: expected a Uint8Array");
                        return false;
                    }
                    size_t bufSize = 0;
                    uint8_t *data = JS_GetArrayBuffer(ctx, &bufSize, buf);
                    JS_FreeValue(ctx, buf);
                    if (!data)
                        return false;
                    *out = data + byteOffset;
                    *len = byteLength;
                    return true;
                }

                JSValue makeUint8Array(JSContext *ctx, const uint8_t *data, size_t len)
                {
                    JSValue ab = JS_NewArrayBufferCopy(ctx, data, len);
                    if (JS_IsException(ab))
                        return ab;
                    JSValue global = JS_GetGlobalObject(ctx);
                    JSValue ctorFn = JS_GetPropertyStr(ctx, global, "Uint8Array");
                    JSValue args[] = {ab};
                    JSValue arr = JS_CallConstructor(ctx, ctorFn, 1, args);
                    JS_FreeValue(ctx, ctorFn);
                    JS_FreeValue(ctx, global);
                    JS_FreeValue(ctx, ab);
                    return arr;
                }

                // Invokes a possibly-unset JS callback. Any exception it throws is
                // logged and swallowed - a script bug in an onMessage handler
                // shouldn't be able to tear down the whole networking loop.
                void invoke(JSContext *ctx, JSValue fn, int argc, JSValueConst *argv)
                {
                    if (!JS_IsFunction(ctx, fn))
                        return;
                    JSValue ret = JS_Call(ctx, fn, JS_UNDEFINED, argc, argv);
                    if (JS_IsException(ret))
                    {
                        JSValue exc = JS_GetException(ctx);
                        const char *msg = JS_ToCString(ctx, exc);
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "bokken/network callback threw: %s",
                                     msg ? msg : "(unknown)");
                        if (msg)
                            JS_FreeCString(ctx, msg);
                        JS_FreeValue(ctx, exc);
                    }
                    JS_FreeValue(ctx, ret);
                }

                // Reads `name` off `obj` and, if it's a function, dup's it into
                // `slot`. Leaves `slot` untouched otherwise (including if the
                // property is absent) so a default of JS_UNDEFINED is preserved.
                void readCallback(JSContext *ctx, JSValueConst obj, const char *name, JSValue &slot)
                {
                    if (!JS_IsObject(obj))
                        return;
                    JSValue v = JS_GetPropertyStr(ctx, obj, name);
                    if (JS_IsFunction(ctx, v))
                        slot = v; // transfer the reference readCallback just took
                    else
                        JS_FreeValue(ctx, v);
                }

                JSValue makeErrorMessage(JSContext *ctx, const char *text)
                {
                    std::vector<uint8_t> bytes(text, text + std::strlen(text));
                    return wrapMessage(ctx, std::move(bytes));
                }

            } // namespace

            // Message

            // Wire format is little-endian and hand-rolled byte-by-byte so it
            // never depends on host byte order or on any particular endian-swap
            // intrinsics being available.

            JSValue Network::js_message_ctor(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                std::vector<uint8_t> bytes;
                if (argc >= 1 && !JS_IsUndefined(argv[0]))
                {
                    uint8_t *data = nullptr;
                    size_t len = 0;
                    if (!readBytesArg(ctx, argv[0], &data, &len))
                        return JS_EXCEPTION;
                    bytes.assign(data, data + len);
                }
                return wrapMessage(ctx, std::move(bytes));
            }

            JSValue Network::js_message_get_byte_length(JSContext *ctx, JSValueConst this_val)
            {
                auto *m = unwrapMessage(this_val);
                if (!m)
                    return JS_EXCEPTION;
                return JS_NewInt64(ctx, static_cast<int64_t>(m->buf.size()));
            }

            JSValue Network::js_message_get_bytes_remaining(JSContext *ctx, JSValueConst this_val)
            {
                auto *m = unwrapMessage(this_val);
                if (!m)
                    return JS_EXCEPTION;
                return JS_NewInt64(ctx, static_cast<int64_t>(m->buf.size() - m->cursor));
            }

            JSValue Network::js_message_peek_byte(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *m = unwrapMessage(this_val);
                if (!m || argc < 1)
                    return JS_EXCEPTION;
                int64_t offset = 0;
                if (JS_ToInt64(ctx, &offset, argv[0]) < 0)
                    return JS_EXCEPTION;
                if (offset < 0 || static_cast<size_t>(offset) >= m->buf.size())
                    return JS_ThrowRangeError(ctx, "Message.peekByte: offset out of range");
                return JS_NewInt32(ctx, m->buf[static_cast<size_t>(offset)]);
            }

            JSValue Network::js_message_peek_bytes(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *m = unwrapMessage(this_val);
                if (!m || argc < 2)
                    return JS_EXCEPTION;
                int64_t offset = 0, length = 0;
                if (JS_ToInt64(ctx, &offset, argv[0]) < 0 || JS_ToInt64(ctx, &length, argv[1]) < 0)
                    return JS_EXCEPTION;
                if (offset < 0 || length < 0 || static_cast<size_t>(offset + length) > m->buf.size())
                    return JS_ThrowRangeError(ctx, "Message.peekBytes: range out of bounds");
                return makeUint8Array(ctx, m->buf.data() + offset, static_cast<size_t>(length));
            }

            namespace
            {
                bool ensureAvailable(JSContext *ctx, MessagePayload *m, size_t n)
                {
                    if (m->cursor + n > m->buf.size())
                    {
                        JS_ThrowRangeError(ctx, "Message: read past end of buffer");
                        return false;
                    }
                    return true;
                }
            }

            JSValue Network::js_message_read_uint8(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *m = unwrapMessage(this_val);
                if (!m || !ensureAvailable(ctx, m, 1))
                    return JS_EXCEPTION;
                return JS_NewInt32(ctx, m->buf[m->cursor++]);
            }

            JSValue Network::js_message_read_int8(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *m = unwrapMessage(this_val);
                if (!m || !ensureAvailable(ctx, m, 1))
                    return JS_EXCEPTION;
                return JS_NewInt32(ctx, static_cast<int8_t>(m->buf[m->cursor++]));
            }

            JSValue Network::js_message_read_uint16(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *m = unwrapMessage(this_val);
                if (!m || !ensureAvailable(ctx, m, 2))
                    return JS_EXCEPTION;
                uint16_t v = static_cast<uint16_t>(m->buf[m->cursor]) |
                             (static_cast<uint16_t>(m->buf[m->cursor + 1]) << 8);
                m->cursor += 2;
                return JS_NewInt32(ctx, v);
            }

            JSValue Network::js_message_read_int16(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *m = unwrapMessage(this_val);
                if (!m || !ensureAvailable(ctx, m, 2))
                    return JS_EXCEPTION;
                uint16_t u = static_cast<uint16_t>(m->buf[m->cursor]) |
                             (static_cast<uint16_t>(m->buf[m->cursor + 1]) << 8);
                m->cursor += 2;
                return JS_NewInt32(ctx, static_cast<int16_t>(u));
            }

            namespace
            {
                uint32_t readU32LE(const uint8_t *b)
                {
                    return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
                           (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
                }
                uint64_t readU64LE(const uint8_t *b)
                {
                    uint64_t lo = readU32LE(b);
                    uint64_t hi = readU32LE(b + 4);
                    return lo | (hi << 32);
                }
                void writeU32LE(std::vector<uint8_t> &buf, uint32_t v)
                {
                    buf.push_back(static_cast<uint8_t>(v & 0xFF));
                    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
                    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
                    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
                }
                void writeU64LE(std::vector<uint8_t> &buf, uint64_t v)
                {
                    writeU32LE(buf, static_cast<uint32_t>(v & 0xFFFFFFFFu));
                    writeU32LE(buf, static_cast<uint32_t>(v >> 32));
                }
            }

            JSValue Network::js_message_read_uint32(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *m = unwrapMessage(this_val);
                if (!m || !ensureAvailable(ctx, m, 4))
                    return JS_EXCEPTION;
                uint32_t v = readU32LE(&m->buf[m->cursor]);
                m->cursor += 4;
                return JS_NewUint32(ctx, v);
            }

            JSValue Network::js_message_read_int32(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *m = unwrapMessage(this_val);
                if (!m || !ensureAvailable(ctx, m, 4))
                    return JS_EXCEPTION;
                uint32_t v = readU32LE(&m->buf[m->cursor]);
                m->cursor += 4;
                return JS_NewInt32(ctx, static_cast<int32_t>(v));
            }

            JSValue Network::js_message_read_float32(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *m = unwrapMessage(this_val);
                if (!m || !ensureAvailable(ctx, m, 4))
                    return JS_EXCEPTION;
                uint32_t bits = readU32LE(&m->buf[m->cursor]);
                m->cursor += 4;
                float f;
                std::memcpy(&f, &bits, sizeof(f));
                return JS_NewFloat64(ctx, f);
            }

            JSValue Network::js_message_read_float64(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *m = unwrapMessage(this_val);
                if (!m || !ensureAvailable(ctx, m, 8))
                    return JS_EXCEPTION;
                uint64_t bits = readU64LE(&m->buf[m->cursor]);
                m->cursor += 8;
                double d;
                std::memcpy(&d, &bits, sizeof(d));
                return JS_NewFloat64(ctx, d);
            }

            JSValue Network::js_message_read_string(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *m = unwrapMessage(this_val);
                if (!m || argc < 1)
                    return JS_EXCEPTION;
                int64_t length = 0;
                if (JS_ToInt64(ctx, &length, argv[0]) < 0)
                    return JS_EXCEPTION;
                if (length < 0 || !ensureAvailable(ctx, m, static_cast<size_t>(length)))
                    return JS_EXCEPTION;
                JSValue s = JS_NewStringLen(ctx, reinterpret_cast<const char *>(&m->buf[m->cursor]),
                                            static_cast<size_t>(length));
                m->cursor += static_cast<size_t>(length);
                return s;
            }

            JSValue Network::js_message_read_bytes(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *m = unwrapMessage(this_val);
                if (!m || argc < 1)
                    return JS_EXCEPTION;
                int64_t length = 0;
                if (JS_ToInt64(ctx, &length, argv[0]) < 0)
                    return JS_EXCEPTION;
                if (length < 0 || !ensureAvailable(ctx, m, static_cast<size_t>(length)))
                    return JS_EXCEPTION;
                JSValue arr = makeUint8Array(ctx, &m->buf[m->cursor], static_cast<size_t>(length));
                m->cursor += static_cast<size_t>(length);
                return arr;
            }

            JSValue Network::js_message_read_remaining(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *m = unwrapMessage(this_val);
                if (!m)
                    return JS_EXCEPTION;
                size_t n = m->buf.size() - m->cursor;
                JSValue arr = makeUint8Array(ctx, &m->buf[m->cursor], n);
                m->cursor += n;
                return arr;
            }

            // Writes always append to the buffer and return `this`, regardless
            // of where `cursor` currently sits - matching the TS signature
            // (`writeX(...): this`), so building a message is a plain append
            // sequence and reading it back afterwards starts with reset().

            JSValue Network::js_message_write_uint8(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *m = unwrapMessage(this_val);
                if (!m || argc < 1)
                    return JS_EXCEPTION;
                int32_t v;
                if (JS_ToInt32(ctx, &v, argv[0]) < 0)
                    return JS_EXCEPTION;
                m->buf.push_back(static_cast<uint8_t>(v));
                return JS_DupValue(ctx, this_val);
            }

            JSValue Network::js_message_write_int8(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                return js_message_write_uint8(ctx, this_val, argc, argv);
            }

            JSValue Network::js_message_write_uint16(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *m = unwrapMessage(this_val);
                if (!m || argc < 1)
                    return JS_EXCEPTION;
                int32_t v;
                if (JS_ToInt32(ctx, &v, argv[0]) < 0)
                    return JS_EXCEPTION;
                uint16_t u = static_cast<uint16_t>(v);
                m->buf.push_back(static_cast<uint8_t>(u & 0xFF));
                m->buf.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
                return JS_DupValue(ctx, this_val);
            }

            JSValue Network::js_message_write_int16(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                return js_message_write_uint16(ctx, this_val, argc, argv);
            }

            JSValue Network::js_message_write_uint32(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *m = unwrapMessage(this_val);
                if (!m || argc < 1)
                    return JS_EXCEPTION;
                uint32_t v;
                if (JS_ToUint32(ctx, &v, argv[0]) < 0)
                    return JS_EXCEPTION;
                writeU32LE(m->buf, v);
                return JS_DupValue(ctx, this_val);
            }

            JSValue Network::js_message_write_int32(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *m = unwrapMessage(this_val);
                if (!m || argc < 1)
                    return JS_EXCEPTION;
                int32_t v;
                if (JS_ToInt32(ctx, &v, argv[0]) < 0)
                    return JS_EXCEPTION;
                writeU32LE(m->buf, static_cast<uint32_t>(v));
                return JS_DupValue(ctx, this_val);
            }

            JSValue Network::js_message_write_float32(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *m = unwrapMessage(this_val);
                if (!m || argc < 1)
                    return JS_EXCEPTION;
                double d;
                if (JS_ToFloat64(ctx, &d, argv[0]) < 0)
                    return JS_EXCEPTION;
                float f = static_cast<float>(d);
                uint32_t bits;
                std::memcpy(&bits, &f, sizeof(bits));
                writeU32LE(m->buf, bits);
                return JS_DupValue(ctx, this_val);
            }

            JSValue Network::js_message_write_float64(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *m = unwrapMessage(this_val);
                if (!m || argc < 1)
                    return JS_EXCEPTION;
                double d;
                if (JS_ToFloat64(ctx, &d, argv[0]) < 0)
                    return JS_EXCEPTION;
                uint64_t bits;
                std::memcpy(&bits, &d, sizeof(bits));
                writeU64LE(m->buf, bits);
                return JS_DupValue(ctx, this_val);
            }

            JSValue Network::js_message_write_string(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *m = unwrapMessage(this_val);
                if (!m || argc < 1)
                    return JS_EXCEPTION;
                size_t len = 0;
                const char *s = JS_ToCStringLen(ctx, &len, argv[0]);
                if (!s)
                    return JS_EXCEPTION;
                m->buf.insert(m->buf.end(), s, s + len);
                JS_FreeCString(ctx, s);
                return JS_DupValue(ctx, this_val);
            }

            JSValue Network::js_message_write_bytes(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *m = unwrapMessage(this_val);
                if (!m || argc < 1)
                    return JS_EXCEPTION;
                uint8_t *data = nullptr;
                size_t len = 0;
                if (!readBytesArg(ctx, argv[0], &data, &len))
                    return JS_EXCEPTION;
                m->buf.insert(m->buf.end(), data, data + len);
                return JS_DupValue(ctx, this_val);
            }

            JSValue Network::js_message_seek(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *m = unwrapMessage(this_val);
                if (!m || argc < 1)
                    return JS_EXCEPTION;
                int64_t offset = 0;
                if (JS_ToInt64(ctx, &offset, argv[0]) < 0)
                    return JS_EXCEPTION;
                if (offset < 0 || static_cast<size_t>(offset) > m->buf.size())
                    return JS_ThrowRangeError(ctx, "Message.seek: offset out of range");
                m->cursor = static_cast<size_t>(offset);
                return JS_UNDEFINED;
            }

            JSValue Network::js_message_reset(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *m = unwrapMessage(this_val);
                if (!m)
                    return JS_EXCEPTION;
                m->cursor = 0;
                return JS_UNDEFINED;
            }

            JSValue Network::js_message_as_bytes(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *m = unwrapMessage(this_val);
                if (!m)
                    return JS_EXCEPTION;
                return makeUint8Array(ctx, m->buf.data(), m->buf.size());
            }

            JSValue Network::js_message_as_json(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *m = unwrapMessage(this_val);
                if (!m)
                    return JS_EXCEPTION;
                return JS_ParseJSON(ctx, reinterpret_cast<const char *>(m->buf.data()),
                                    m->buf.size(), "<message>");
            }

            void Network::message_finalizer(JSRuntime *, JSValue val)
            {
                auto *m = static_cast<MessagePayload *>(JS_GetOpaque(val, s_message_class_id));
                delete m;
            }

            // Address

            JSValue Network::js_address_parse(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 1)
                    return JS_ThrowTypeError(ctx, "Address.parse: expected a 'host:port' string");
                const char *uriC = JS_ToCString(ctx, argv[0]);
                if (!uriC)
                    return JS_EXCEPTION;
                std::string uri(uriC);
                JS_FreeCString(ctx, uriC);

                std::string host;
                uint16_t port = 0;
                if (!splitHostPort(uri, host, port))
                    return JS_ThrowTypeError(ctx, "Address.parse: expected 'host:port', got '%s'", uri.c_str());

                NET_Address *addr = nullptr;
                if (!resolveBlocking(ctx, host, kDefaultResolveTimeoutMs, &addr))
                    return JS_EXCEPTION; // resolveBlocking already threw
                return wrapAddress(ctx, addr, port);
            }

            JSValue Network::js_address_get_host(JSContext *ctx, JSValueConst this_val)
            {
                auto *a = unwrapAddress(this_val);
                if (!a)
                    return JS_EXCEPTION;
                if (!a->addr)
                    return JS_NewString(ctx, "0.0.0.0");
                const char *s = NET_GetAddressString(a->addr);
                return JS_NewString(ctx, s ? s : "");
            }

            JSValue Network::js_address_get_port(JSContext *ctx, JSValueConst this_val)
            {
                auto *a = unwrapAddress(this_val);
                if (!a)
                    return JS_EXCEPTION;
                return JS_NewInt32(ctx, a->port);
            }

            JSValue Network::js_address_to_string(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *a = unwrapAddress(this_val);
                if (!a)
                    return JS_EXCEPTION;
                const char *host = a->addr ? NET_GetAddressString(a->addr) : "0.0.0.0";
                std::string s = std::string(host ? host : "") + ":" + std::to_string(a->port);
                return JS_NewString(ctx, s.c_str());
            }

            JSValue Network::js_address_equals(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *a = unwrapAddress(this_val);
                if (!a || argc < 1)
                    return JS_ThrowTypeError(ctx, "Address.equals: expected an Address");
                auto *b = unwrapAddress(argv[0]);
                if (!b)
                    return JS_FALSE;
                if (a->port != b->port)
                    return JS_FALSE;
                if (!a->addr || !b->addr)
                    return JS_NewBool(ctx, a->addr == b->addr);
                return JS_NewBool(ctx, NET_CompareAddresses(a->addr, b->addr) == 0);
            }

            void Network::address_finalizer(JSRuntime *, JSValue val)
            {
                auto *a = static_cast<AddressPayload *>(JS_GetOpaque(val, s_address_class_id));
                if (!a)
                    return;
                if (a->addr)
                    NET_UnrefAddress(a->addr);
                delete a;
            }

            // TCPClient

            JSValue Network::js_tcpclient_ctor(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 1)
                    return JS_ThrowTypeError(ctx, "TCPClient: expected a 'host:port' uri");
                const char *uriC = JS_ToCString(ctx, argv[0]);
                if (!uriC)
                    return JS_EXCEPTION;
                std::string uri(uriC);
                JS_FreeCString(ctx, uriC);

                std::string host;
                uint16_t port = 0;
                if (!splitHostPort(uri, host, port))
                    return JS_ThrowTypeError(ctx, "TCPClient: expected 'host:port', got '%s'", uri.c_str());
                if (host.empty())
                    return JS_ThrowTypeError(ctx, "TCPClient: a remote host is required");

                NET_Address *addr = NET_ResolveHostname(host.c_str());
                if (!addr)
                    return JS_ThrowTypeError(ctx, "TCPClient: %s", SDL_GetError());

                auto *p = new TCPClientPayload();
                p->host = host;
                p->addr = addr;
                p->port = port;
                p->state = TCPState::ResolvingAddress;

                JSValueConst options = (argc >= 2) ? argv[1] : JS_UNDEFINED;
                if (JS_IsObject(options))
                {
                    double d;
                    int32_t i;
                    JSValue v;

                    v = JS_GetPropertyStr(ctx, options, "autoReconnect");
                    if (JS_IsBool(v))
                        p->autoReconnect = JS_ToBool(ctx, v) != 0;
                    JS_FreeValue(ctx, v);

                    v = JS_GetPropertyStr(ctx, options, "reconnectDelay");
                    if (JS_IsNumber(v) && JS_ToFloat64(ctx, &d, v) == 0)
                        p->reconnectDelayMs = static_cast<Sint32>(d);
                    JS_FreeValue(ctx, v);

                    v = JS_GetPropertyStr(ctx, options, "maximumReconnectAttempts");
                    if (JS_IsNumber(v) && JS_ToInt32(ctx, &i, v) == 0)
                        p->maximumReconnectAttempts = i;
                    JS_FreeValue(ctx, v);

                    v = JS_GetPropertyStr(ctx, options, "connectTimeout");
                    if (JS_IsNumber(v) && JS_ToFloat64(ctx, &d, v) == 0)
                        p->connectTimeoutMs = static_cast<Sint32>(d);
                    JS_FreeValue(ctx, v);

                    readCallback(ctx, options, "onConnect", p->onConnect);
                    readCallback(ctx, options, "onDisconnect", p->onDisconnect);
                    readCallback(ctx, options, "onError", p->onError);
                    readCallback(ctx, options, "onMessage", p->onMessage);
                }

                if (p->connectTimeoutMs > 0)
                    p->deadlineTicks = SDL_GetTicks() + static_cast<Uint64>(p->connectTimeoutMs);

                JSValue obj = JS_NewObjectClass(ctx, Network::s_tcpclient_class_id);
                if (JS_IsException(obj))
                {
                    NET_UnrefAddress(addr);
                    delete p;
                    return obj;
                }
                JS_SetOpaque(obj, p);

                tcpClientRegistry().push_back(JS_DupValue(ctx, obj));
                return obj;
            }

            JSValue Network::js_tcpclient_get_is_connected(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapTcpClient(this_val);
                if (!p)
                    return JS_EXCEPTION;
                return JS_NewBool(ctx, p->state == TCPState::Connected);
            }

            JSValue Network::js_tcpclient_get_address(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapTcpClient(this_val);
                if (!p)
                    return JS_EXCEPTION;
                if (!p->addr)
                    return JS_NULL;
                return wrapAddress(ctx, NET_RefAddress(p->addr), p->port);
            }

            JSValue Network::js_tcpclient_get_bytes_sent(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapTcpClient(this_val);
                if (!p)
                    return JS_EXCEPTION;
                return JS_NewInt64(ctx, static_cast<int64_t>(p->bytesSent));
            }

            JSValue Network::js_tcpclient_get_bytes_received(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapTcpClient(this_val);
                if (!p)
                    return JS_EXCEPTION;
                return JS_NewInt64(ctx, static_cast<int64_t>(p->bytesReceived));
            }

            JSValue Network::js_tcpclient_send(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *p = unwrapTcpClient(this_val);
                if (!p || argc < 1)
                    return JS_UNDEFINED;
                if (p->state != TCPState::Connected || !p->sock)
                    return JS_UNDEFINED; // silent no-op, see Joint convention
                uint8_t *data = nullptr;
                size_t len = 0;
                if (!readBytesArg(ctx, argv[0], &data, &len))
                    return JS_EXCEPTION;
                if (NET_WriteToStreamSocket(p->sock, data, static_cast<int>(len)))
                    p->bytesSent += len;
                return JS_UNDEFINED;
            }

            namespace
            {
                // Shared by close() and poll()'s failure/reconnect path. Frees
                // the socket/address and, unless the caller says otherwise,
                // drops this client's slot in the poll registry.
                void teardownTcpClient(JSContext *ctx, JSValueConst self, TCPClientPayload *p, bool removeFromRegistry)
                {
                    if (p->sock)
                    {
                        NET_DestroyStreamSocket(p->sock);
                        p->sock = nullptr;
                    }
                    if (p->addr)
                    {
                        NET_UnrefAddress(p->addr);
                        p->addr = nullptr;
                    }
                    p->state = TCPState::Closed;
                    if (!JS_IsUndefined(p->ownerServer))
                    {
                        JS_FreeValue(ctx, p->ownerServer);
                        p->ownerServer = JS_UNDEFINED;
                    }
                    if (removeFromRegistry)
                        unregister(tcpClientRegistry(), ctx, self);
                }
            }

            JSValue Network::js_tcpclient_close(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *p = unwrapTcpClient(this_val);
                if (!p || p->state == TCPState::Closed)
                    return JS_UNDEFINED;
                teardownTcpClient(ctx, this_val, p, !p->isServerSide);
                // Server-accepted clients are also tracked in TCPServer::clients;
                // that list is pruned lazily by poll() when it notices the socket
                // is gone, since we don't have the owning TCPServerPayload here.
                return JS_UNDEFINED;
            }

            JSValue Network::js_tcpclient_get_onconnect(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapTcpClient(this_val);
                return p ? JS_DupValue(ctx, p->onConnect) : JS_EXCEPTION;
            }
            JSValue Network::js_tcpclient_set_onconnect(JSContext *ctx, JSValueConst this_val, JSValueConst v)
            {
                auto *p = unwrapTcpClient(this_val);
                if (!p)
                    return JS_EXCEPTION;
                JS_FreeValue(ctx, p->onConnect);
                p->onConnect = JS_DupValue(ctx, v);
                return JS_UNDEFINED;
            }

            JSValue Network::js_tcpclient_get_ondisconnect(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapTcpClient(this_val);
                return p ? JS_DupValue(ctx, p->onDisconnect) : JS_EXCEPTION;
            }
            JSValue Network::js_tcpclient_set_ondisconnect(JSContext *ctx, JSValueConst this_val, JSValueConst v)
            {
                auto *p = unwrapTcpClient(this_val);
                if (!p)
                    return JS_EXCEPTION;
                JS_FreeValue(ctx, p->onDisconnect);
                p->onDisconnect = JS_DupValue(ctx, v);
                return JS_UNDEFINED;
            }

            JSValue Network::js_tcpclient_get_onerror(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapTcpClient(this_val);
                return p ? JS_DupValue(ctx, p->onError) : JS_EXCEPTION;
            }
            JSValue Network::js_tcpclient_set_onerror(JSContext *ctx, JSValueConst this_val, JSValueConst v)
            {
                auto *p = unwrapTcpClient(this_val);
                if (!p)
                    return JS_EXCEPTION;
                JS_FreeValue(ctx, p->onError);
                p->onError = JS_DupValue(ctx, v);
                return JS_UNDEFINED;
            }

            JSValue Network::js_tcpclient_get_onmessage(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapTcpClient(this_val);
                return p ? JS_DupValue(ctx, p->onMessage) : JS_EXCEPTION;
            }
            JSValue Network::js_tcpclient_set_onmessage(JSContext *ctx, JSValueConst this_val, JSValueConst v)
            {
                auto *p = unwrapTcpClient(this_val);
                if (!p)
                    return JS_EXCEPTION;
                JS_FreeValue(ctx, p->onMessage);
                p->onMessage = JS_DupValue(ctx, v);
                return JS_UNDEFINED;
            }

            void Network::tcpclient_finalizer(JSRuntime *rt, JSValue val)
            {
                auto *p = static_cast<TCPClientPayload *>(JS_GetOpaque(val, s_tcpclient_class_id));
                if (!p)
                    return;
                if (p->sock)
                    NET_DestroyStreamSocket(p->sock);
                if (p->addr)
                    NET_UnrefAddress(p->addr);
                JS_FreeValueRT(rt, p->ownerServer);
                JS_FreeValueRT(rt, p->onConnect);
                JS_FreeValueRT(rt, p->onDisconnect);
                JS_FreeValueRT(rt, p->onError);
                JS_FreeValueRT(rt, p->onMessage);
                delete p;
            }

            void Network::tcpclient_gc_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func)
            {
                auto *p = static_cast<TCPClientPayload *>(JS_GetOpaque(val, s_tcpclient_class_id));
                if (!p)
                    return;
                JS_MarkValue(rt, p->ownerServer, mark_func);
                JS_MarkValue(rt, p->onConnect, mark_func);
                JS_MarkValue(rt, p->onDisconnect, mark_func);
                JS_MarkValue(rt, p->onError, mark_func);
                JS_MarkValue(rt, p->onMessage, mark_func);
            }

            // TCPServer

            JSValue Network::js_tcpserver_ctor(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 1)
                    return JS_ThrowTypeError(ctx, "TCPServer: expected a 'host:port' uri");
                const char *uriC = JS_ToCString(ctx, argv[0]);
                if (!uriC)
                    return JS_EXCEPTION;
                std::string uri(uriC);
                JS_FreeCString(ctx, uriC);

                std::string host;
                uint16_t port = 0;
                if (!splitHostPort(uri, host, port))
                    return JS_ThrowTypeError(ctx, "TCPServer: expected 'host:port' (use '*:port' or ':port' to bind all interfaces), got '%s'", uri.c_str());

                NET_Address *bindAddr = nullptr;
                if (!resolveBlocking(ctx, host, kDefaultResolveTimeoutMs, &bindAddr))
                    return JS_EXCEPTION;

                NET_Server *server = NET_CreateServer(bindAddr, port, 0);
                if (bindAddr)
                    NET_UnrefAddress(bindAddr); // NET_CreateServer takes its own ref internally
                if (!server)
                    return JS_ThrowTypeError(ctx, "TCPServer: %s", SDL_GetError());

                auto *p = new TCPServerPayload();
                p->server = server;

                JSValueConst events = (argc >= 2) ? argv[1] : JS_UNDEFINED;
                readCallback(ctx, events, "onConnect", p->onConnect);
                readCallback(ctx, events, "onDisconnect", p->onDisconnect);
                readCallback(ctx, events, "onError", p->onError);
                readCallback(ctx, events, "onMessage", p->onMessage);

                JSValue obj = JS_NewObjectClass(ctx, Network::s_tcpserver_class_id);
                if (JS_IsException(obj))
                {
                    NET_DestroyServer(server);
                    delete p;
                    return obj;
                }
                JS_SetOpaque(obj, p);

                tcpServerRegistry().push_back(JS_DupValue(ctx, obj));
                return obj;
            }

            JSValue Network::js_tcpserver_get_is_listening(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapTcpServer(this_val);
                if (!p)
                    return JS_EXCEPTION;
                return JS_NewBool(ctx, p->server != nullptr && !p->closed);
            }

            JSValue Network::js_tcpserver_get_clients(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapTcpServer(this_val);
                if (!p)
                    return JS_EXCEPTION;
                JSValue arr = JS_NewArray(ctx);
                for (size_t i = 0; i < p->clients.size(); ++i)
                    JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i), JS_DupValue(ctx, p->clients[i]));
                return arr;
            }

            JSValue Network::js_tcpserver_broadcast(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *p = unwrapTcpServer(this_val);
                if (!p || argc < 1)
                    return JS_UNDEFINED;
                uint8_t *data = nullptr;
                size_t len = 0;
                if (!readBytesArg(ctx, argv[0], &data, &len))
                    return JS_EXCEPTION;
                for (JSValue clientVal : p->clients)
                {
                    auto *c = unwrapTcpClient(clientVal);
                    if (c && c->sock && c->state == TCPState::Connected)
                    {
                        if (NET_WriteToStreamSocket(c->sock, data, static_cast<int>(len)))
                            c->bytesSent += len;
                    }
                }
                return JS_UNDEFINED;
            }

            JSValue Network::js_tcpserver_broadcast_except(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *p = unwrapTcpServer(this_val);
                if (!p || argc < 2)
                    return JS_UNDEFINED;
                uint8_t *data = nullptr;
                size_t len = 0;
                if (!readBytesArg(ctx, argv[0], &data, &len))
                    return JS_EXCEPTION;
                auto *except = unwrapTcpClient(argv[1]);
                for (JSValue clientVal : p->clients)
                {
                    auto *c = unwrapTcpClient(clientVal);
                    if (c && c != except && c->sock && c->state == TCPState::Connected)
                    {
                        if (NET_WriteToStreamSocket(c->sock, data, static_cast<int>(len)))
                            c->bytesSent += len;
                    }
                }
                return JS_UNDEFINED;
            }

            JSValue Network::js_tcpserver_close(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *p = unwrapTcpServer(this_val);
                if (!p || p->closed)
                    return JS_UNDEFINED;
                NET_DestroyServer(p->server);
                p->server = nullptr;
                p->closed = true;
                // Accepted clients are left running (NET_DestroyServer doesn't
                // touch them either); drop this server's own list references.
                for (JSValue c : p->clients)
                    JS_FreeValue(ctx, c);
                p->clients.clear();
                unregister(tcpServerRegistry(), ctx, this_val);
                return JS_UNDEFINED;
            }

            JSValue Network::js_tcpserver_get_onconnect(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapTcpServer(this_val);
                return p ? JS_DupValue(ctx, p->onConnect) : JS_EXCEPTION;
            }
            JSValue Network::js_tcpserver_set_onconnect(JSContext *ctx, JSValueConst this_val, JSValueConst v)
            {
                auto *p = unwrapTcpServer(this_val);
                if (!p)
                    return JS_EXCEPTION;
                JS_FreeValue(ctx, p->onConnect);
                p->onConnect = JS_DupValue(ctx, v);
                return JS_UNDEFINED;
            }

            JSValue Network::js_tcpserver_get_ondisconnect(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapTcpServer(this_val);
                return p ? JS_DupValue(ctx, p->onDisconnect) : JS_EXCEPTION;
            }
            JSValue Network::js_tcpserver_set_ondisconnect(JSContext *ctx, JSValueConst this_val, JSValueConst v)
            {
                auto *p = unwrapTcpServer(this_val);
                if (!p)
                    return JS_EXCEPTION;
                JS_FreeValue(ctx, p->onDisconnect);
                p->onDisconnect = JS_DupValue(ctx, v);
                return JS_UNDEFINED;
            }

            JSValue Network::js_tcpserver_get_onerror(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapTcpServer(this_val);
                return p ? JS_DupValue(ctx, p->onError) : JS_EXCEPTION;
            }
            JSValue Network::js_tcpserver_set_onerror(JSContext *ctx, JSValueConst this_val, JSValueConst v)
            {
                auto *p = unwrapTcpServer(this_val);
                if (!p)
                    return JS_EXCEPTION;
                JS_FreeValue(ctx, p->onError);
                p->onError = JS_DupValue(ctx, v);
                return JS_UNDEFINED;
            }

            JSValue Network::js_tcpserver_get_onmessage(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapTcpServer(this_val);
                return p ? JS_DupValue(ctx, p->onMessage) : JS_EXCEPTION;
            }
            JSValue Network::js_tcpserver_set_onmessage(JSContext *ctx, JSValueConst this_val, JSValueConst v)
            {
                auto *p = unwrapTcpServer(this_val);
                if (!p)
                    return JS_EXCEPTION;
                JS_FreeValue(ctx, p->onMessage);
                p->onMessage = JS_DupValue(ctx, v);
                return JS_UNDEFINED;
            }

            void Network::tcpserver_finalizer(JSRuntime *rt, JSValue val)
            {
                auto *p = static_cast<TCPServerPayload *>(JS_GetOpaque(val, s_tcpserver_class_id));
                if (!p)
                    return;
                if (p->server)
                    NET_DestroyServer(p->server);
                for (JSValue c : p->clients)
                    JS_FreeValueRT(rt, c);
                JS_FreeValueRT(rt, p->onConnect);
                JS_FreeValueRT(rt, p->onDisconnect);
                JS_FreeValueRT(rt, p->onError);
                JS_FreeValueRT(rt, p->onMessage);
                delete p;
            }

            void Network::tcpserver_gc_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func)
            {
                auto *p = static_cast<TCPServerPayload *>(JS_GetOpaque(val, s_tcpserver_class_id));
                if (!p)
                    return;
                for (JSValue c : p->clients)
                    JS_MarkValue(rt, c, mark_func);
                JS_MarkValue(rt, p->onConnect, mark_func);
                JS_MarkValue(rt, p->onDisconnect, mark_func);
                JS_MarkValue(rt, p->onError, mark_func);
                JS_MarkValue(rt, p->onMessage, mark_func);
            }

            // UDPClient

            JSValue Network::js_udpclient_ctor(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 1)
                    return JS_ThrowTypeError(ctx, "UDPClient: expected a 'host:port' uri");
                const char *uriC = JS_ToCString(ctx, argv[0]);
                if (!uriC)
                    return JS_EXCEPTION;
                std::string uri(uriC);
                JS_FreeCString(ctx, uriC);

                std::string host;
                uint16_t port = 0;
                if (!splitHostPort(uri, host, port))
                    return JS_ThrowTypeError(ctx, "UDPClient: expected 'host:port', got '%s'", uri.c_str());
                if (host.empty())
                    return JS_ThrowTypeError(ctx, "UDPClient: a remote host is required");

                NET_Address *defaultAddr = nullptr;
                if (!resolveBlocking(ctx, host, kDefaultResolveTimeoutMs, &defaultAddr))
                    return JS_EXCEPTION;

                NET_DatagramSocket *sock = NET_CreateDatagramSocket(nullptr, 0, 0);
                if (!sock)
                {
                    if (defaultAddr)
                        NET_UnrefAddress(defaultAddr);
                    return JS_ThrowTypeError(ctx, "UDPClient: %s", SDL_GetError());
                }

                auto *p = new UDPClientPayload();
                p->sock = sock;
                p->defaultAddr = defaultAddr;
                p->defaultPort = port;

                JSValueConst events = (argc >= 2) ? argv[1] : JS_UNDEFINED;
                readCallback(ctx, events, "onMessage", p->onMessage);
                readCallback(ctx, events, "onError", p->onError);

                JSValue obj = JS_NewObjectClass(ctx, Network::s_udpclient_class_id);
                if (JS_IsException(obj))
                {
                    NET_DestroyDatagramSocket(sock);
                    if (defaultAddr)
                        NET_UnrefAddress(defaultAddr);
                    delete p;
                    return obj;
                }
                JS_SetOpaque(obj, p);

                udpClientRegistry().push_back(JS_DupValue(ctx, obj));
                return obj;
            }

            JSValue Network::js_udpclient_get_is_open(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapUdpClient(this_val);
                if (!p)
                    return JS_EXCEPTION;
                return JS_NewBool(ctx, p->sock != nullptr && !p->closed);
            }
            JSValue Network::js_udpclient_get_bytes_sent(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapUdpClient(this_val);
                if (!p)
                    return JS_EXCEPTION;
                return JS_NewInt64(ctx, static_cast<int64_t>(p->bytesSent));
            }
            JSValue Network::js_udpclient_get_bytes_received(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapUdpClient(this_val);
                if (!p)
                    return JS_EXCEPTION;
                return JS_NewInt64(ctx, static_cast<int64_t>(p->bytesReceived));
            }

            JSValue Network::js_udpclient_send(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *p = unwrapUdpClient(this_val);
                if (!p || argc < 1 || p->closed || !p->sock)
                    return JS_UNDEFINED;
                if (!p->defaultAddr)
                    return JS_ThrowTypeError(ctx, "UDPClient.send: no default address (use sendTo)");
                uint8_t *data = nullptr;
                size_t len = 0;
                if (!readBytesArg(ctx, argv[0], &data, &len))
                    return JS_EXCEPTION;
                if (NET_SendDatagram(p->sock, p->defaultAddr, p->defaultPort, data, static_cast<int>(len)))
                    p->bytesSent += len;
                return JS_UNDEFINED;
            }

            JSValue Network::js_udpclient_send_to(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *p = unwrapUdpClient(this_val);
                if (!p || argc < 2 || p->closed || !p->sock)
                    return JS_UNDEFINED;
                auto *a = unwrapAddress(argv[0]);
                if (!a || !a->addr)
                    return JS_ThrowTypeError(ctx, "UDPClient.sendTo: invalid Address");
                uint8_t *data = nullptr;
                size_t len = 0;
                if (!readBytesArg(ctx, argv[1], &data, &len))
                    return JS_EXCEPTION;
                if (NET_SendDatagram(p->sock, a->addr, a->port, data, static_cast<int>(len)))
                    p->bytesSent += len;
                return JS_UNDEFINED;
            }

            JSValue Network::js_udpclient_close(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *p = unwrapUdpClient(this_val);
                if (!p || p->closed)
                    return JS_UNDEFINED;
                if (p->sock)
                {
                    NET_DestroyDatagramSocket(p->sock);
                    p->sock = nullptr;
                }
                if (p->defaultAddr)
                {
                    NET_UnrefAddress(p->defaultAddr);
                    p->defaultAddr = nullptr;
                }
                p->closed = true;
                unregister(udpClientRegistry(), ctx, this_val);
                return JS_UNDEFINED;
            }

            JSValue Network::js_udpclient_get_onmessage(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapUdpClient(this_val);
                return p ? JS_DupValue(ctx, p->onMessage) : JS_EXCEPTION;
            }
            JSValue Network::js_udpclient_set_onmessage(JSContext *ctx, JSValueConst this_val, JSValueConst v)
            {
                auto *p = unwrapUdpClient(this_val);
                if (!p)
                    return JS_EXCEPTION;
                JS_FreeValue(ctx, p->onMessage);
                p->onMessage = JS_DupValue(ctx, v);
                return JS_UNDEFINED;
            }
            JSValue Network::js_udpclient_get_onerror(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapUdpClient(this_val);
                return p ? JS_DupValue(ctx, p->onError) : JS_EXCEPTION;
            }
            JSValue Network::js_udpclient_set_onerror(JSContext *ctx, JSValueConst this_val, JSValueConst v)
            {
                auto *p = unwrapUdpClient(this_val);
                if (!p)
                    return JS_EXCEPTION;
                JS_FreeValue(ctx, p->onError);
                p->onError = JS_DupValue(ctx, v);
                return JS_UNDEFINED;
            }

            void Network::udpclient_finalizer(JSRuntime *rt, JSValue val)
            {
                auto *p = static_cast<UDPClientPayload *>(JS_GetOpaque(val, s_udpclient_class_id));
                if (!p)
                    return;
                if (p->sock)
                    NET_DestroyDatagramSocket(p->sock);
                if (p->defaultAddr)
                    NET_UnrefAddress(p->defaultAddr);
                JS_FreeValueRT(rt, p->onMessage);
                JS_FreeValueRT(rt, p->onError);
                delete p;
            }

            void Network::udpclient_gc_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func)
            {
                auto *p = static_cast<UDPClientPayload *>(JS_GetOpaque(val, s_udpclient_class_id));
                if (!p)
                    return;
                JS_MarkValue(rt, p->onMessage, mark_func);
                JS_MarkValue(rt, p->onError, mark_func);
            }

            // UDPServer

            JSValue Network::js_udpserver_ctor(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 1)
                    return JS_ThrowTypeError(ctx, "UDPServer: expected a port number");
                int32_t portI = 0;
                if (JS_ToInt32(ctx, &portI, argv[0]) < 0)
                    return JS_EXCEPTION;
                if (portI <= 0 || portI > 65535)
                    return JS_ThrowRangeError(ctx, "UDPServer: port out of range");

                NET_DatagramSocket *sock = NET_CreateDatagramSocket(nullptr, static_cast<Uint16>(portI), 0);
                if (!sock)
                    return JS_ThrowTypeError(ctx, "UDPServer: %s", SDL_GetError());

                auto *p = new UDPServerPayload();
                p->sock = sock;

                JSValueConst events = (argc >= 2) ? argv[1] : JS_UNDEFINED;
                readCallback(ctx, events, "onMessage", p->onMessage);
                readCallback(ctx, events, "onError", p->onError);

                JSValue obj = JS_NewObjectClass(ctx, Network::s_udpserver_class_id);
                if (JS_IsException(obj))
                {
                    NET_DestroyDatagramSocket(sock);
                    delete p;
                    return obj;
                }
                JS_SetOpaque(obj, p);

                udpServerRegistry().push_back(JS_DupValue(ctx, obj));
                return obj;
            }

            JSValue Network::js_udpserver_get_is_open(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapUdpServer(this_val);
                if (!p)
                    return JS_EXCEPTION;
                return JS_NewBool(ctx, p->sock != nullptr && !p->closed);
            }
            JSValue Network::js_udpserver_get_bytes_sent(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapUdpServer(this_val);
                if (!p)
                    return JS_EXCEPTION;
                return JS_NewInt64(ctx, static_cast<int64_t>(p->bytesSent));
            }
            JSValue Network::js_udpserver_get_bytes_received(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapUdpServer(this_val);
                if (!p)
                    return JS_EXCEPTION;
                return JS_NewInt64(ctx, static_cast<int64_t>(p->bytesReceived));
            }

            JSValue Network::js_udpserver_send_to(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *p = unwrapUdpServer(this_val);
                if (!p || argc < 2 || p->closed || !p->sock)
                    return JS_UNDEFINED;
                auto *a = unwrapAddress(argv[0]);
                if (!a || !a->addr)
                    return JS_ThrowTypeError(ctx, "UDPServer.sendTo: invalid Address");
                uint8_t *data = nullptr;
                size_t len = 0;
                if (!readBytesArg(ctx, argv[1], &data, &len))
                    return JS_EXCEPTION;
                if (NET_SendDatagram(p->sock, a->addr, a->port, data, static_cast<int>(len)))
                    p->bytesSent += len;
                return JS_UNDEFINED;
            }

            JSValue Network::js_udpserver_broadcast_to_known_peers(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *p = unwrapUdpServer(this_val);
                if (!p || argc < 1 || p->closed || !p->sock)
                    return JS_UNDEFINED;
                uint8_t *data = nullptr;
                size_t len = 0;
                if (!readBytesArg(ctx, argv[0], &data, &len))
                    return JS_EXCEPTION;
                for (auto &peer : p->knownPeers)
                {
                    if (NET_SendDatagram(p->sock, peer.addr, peer.port, data, static_cast<int>(len)))
                        p->bytesSent += len;
                }
                return JS_UNDEFINED;
            }

            JSValue Network::js_udpserver_close(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *p = unwrapUdpServer(this_val);
                if (!p || p->closed)
                    return JS_UNDEFINED;
                if (p->sock)
                {
                    NET_DestroyDatagramSocket(p->sock);
                    p->sock = nullptr;
                }
                for (auto &peer : p->knownPeers)
                    NET_UnrefAddress(peer.addr);
                p->knownPeers.clear();
                p->closed = true;
                unregister(udpServerRegistry(), ctx, this_val);
                return JS_UNDEFINED;
            }

            JSValue Network::js_udpserver_get_onmessage(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapUdpServer(this_val);
                return p ? JS_DupValue(ctx, p->onMessage) : JS_EXCEPTION;
            }
            JSValue Network::js_udpserver_set_onmessage(JSContext *ctx, JSValueConst this_val, JSValueConst v)
            {
                auto *p = unwrapUdpServer(this_val);
                if (!p)
                    return JS_EXCEPTION;
                JS_FreeValue(ctx, p->onMessage);
                p->onMessage = JS_DupValue(ctx, v);
                return JS_UNDEFINED;
            }
            JSValue Network::js_udpserver_get_onerror(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapUdpServer(this_val);
                return p ? JS_DupValue(ctx, p->onError) : JS_EXCEPTION;
            }
            JSValue Network::js_udpserver_set_onerror(JSContext *ctx, JSValueConst this_val, JSValueConst v)
            {
                auto *p = unwrapUdpServer(this_val);
                if (!p)
                    return JS_EXCEPTION;
                JS_FreeValue(ctx, p->onError);
                p->onError = JS_DupValue(ctx, v);
                return JS_UNDEFINED;
            }

            void Network::udpserver_finalizer(JSRuntime *rt, JSValue val)
            {
                auto *p = static_cast<UDPServerPayload *>(JS_GetOpaque(val, s_udpserver_class_id));
                if (!p)
                    return;
                if (p->sock)
                    NET_DestroyDatagramSocket(p->sock);
                for (auto &peer : p->knownPeers)
                    NET_UnrefAddress(peer.addr);
                JS_FreeValueRT(rt, p->onMessage);
                JS_FreeValueRT(rt, p->onError);
                delete p;
            }

            void Network::udpserver_gc_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func)
            {
                auto *p = static_cast<UDPServerPayload *>(JS_GetOpaque(val, s_udpserver_class_id));
                if (!p)
                    return;
                JS_MarkValue(rt, p->onMessage, mark_func);
                JS_MarkValue(rt, p->onError, mark_func);
            }

            // poll() — pumps every live socket. Call once per frame.

            namespace
            {
                void failTcpClient(JSContext *ctx, JSValueConst self, TCPClientPayload *p, const char *reason)
                {
                    JSValue reasonMsg = makeErrorMessage(ctx, reason);

                    if (p->isServerSide)
                    {
                        JSValue serverVal = p->ownerServer;
                        auto *srv = unwrapTcpServer(serverVal);
                        if (srv)
                        {
                            JSValueConst errArgs[] = {self};
                            invoke(ctx, srv->onError, 1, errArgs);
                            JSValueConst discArgs[] = {self, reasonMsg};
                            invoke(ctx, srv->onDisconnect, 2, discArgs);
                            // Drop this client from the server's live-clients list.
                            unregister(srv->clients, ctx, self);
                        }
                        JSValueConst clientErrArgs[] = {reasonMsg};
                        invoke(ctx, p->onError, 1, clientErrArgs);
                        invoke(ctx, p->onDisconnect, 1, clientErrArgs);
                        JS_FreeValue(ctx, reasonMsg);
                        teardownTcpClient(ctx, self, p, /*removeFromRegistry*/ true);
                        return;
                    }

                    JSValueConst errArgs[] = {reasonMsg};
                    invoke(ctx, p->onError, 1, errArgs);
                    invoke(ctx, p->onDisconnect, 1, errArgs);
                    JS_FreeValue(ctx, reasonMsg);

                    bool canRetry = p->autoReconnect &&
                                    (p->maximumReconnectAttempts < 0 || p->reconnectAttempts < p->maximumReconnectAttempts);
                    if (canRetry)
                    {
                        if (p->sock)
                        {
                            NET_DestroyStreamSocket(p->sock);
                            p->sock = nullptr;
                        }
                        if (p->addr)
                        {
                            NET_UnrefAddress(p->addr);
                            p->addr = nullptr;
                        }
                        p->state = TCPState::Reconnecting;
                        p->reconnectAttempts++;
                        p->reconnectAtTicks = SDL_GetTicks() + static_cast<Uint64>(p->reconnectDelayMs);
                        return; // stays registered
                    }

                    teardownTcpClient(ctx, self, p, /*removeFromRegistry*/ true);
                }

                // Drains as much as is available on a connected stream socket this
                // frame, firing onMessage once per chunk read.
                void pumpTcpReads(JSContext *ctx, JSValueConst self, TCPClientPayload *p)
                {
                    uint8_t chunk[kTcpReadChunkSize];
                    for (;;)
                    {
                        int n = NET_ReadFromStreamSocket(p->sock, chunk, sizeof(chunk));
                        if (n < 0)
                        {
                            failTcpClient(ctx, self, p, SDL_GetError());
                            return;
                        }
                        if (n == 0)
                            return;
                        p->bytesReceived += static_cast<uint64_t>(n);

                        std::vector<uint8_t> bytes(chunk, chunk + n);
                        JSValue msg = wrapMessage(ctx, std::move(bytes));

                        if (p->isServerSide)
                        {
                            auto *srv = unwrapTcpServer(p->ownerServer);
                            if (srv)
                            {
                                JSValueConst args[] = {self, msg};
                                invoke(ctx, srv->onMessage, 2, args);
                            }
                        }
                        JSValueConst args[] = {msg};
                        invoke(ctx, p->onMessage, 1, args);
                        JS_FreeValue(ctx, msg);

                        if (n < static_cast<int>(sizeof(chunk)))
                            return; // drained for this frame
                    }
                }

                void pollOneTcpClient(JSContext *ctx, JSValueConst self, TCPClientPayload *p)
                {
                    Uint64 now = SDL_GetTicks();

                    switch (p->state)
                    {
                    case TCPState::ResolvingAddress:
                    {
                        NET_Status status = NET_GetAddressStatus(p->addr);
                        if (status == NET_SUCCESS)
                        {
                            p->sock = NET_CreateClient(p->addr, p->port, 0);
                            if (!p->sock)
                            {
                                failTcpClient(ctx, self, p, SDL_GetError());
                                return;
                            }
                            p->state = TCPState::Connecting;
                        }
                        else if (status == NET_FAILURE)
                        {
                            failTcpClient(ctx, self, p, SDL_GetError());
                        }
                        else if (p->connectTimeoutMs > 0 && now >= p->deadlineTicks)
                        {
                            failTcpClient(ctx, self, p, "connection timed out while resolving address");
                        }
                        return;
                    }
                    case TCPState::Connecting:
                    {
                        NET_Status status = NET_GetConnectionStatus(p->sock);
                        if (status == NET_SUCCESS)
                        {
                            p->state = TCPState::Connected;
                            p->reconnectAttempts = 0;
                            invoke(ctx, p->onConnect, 0, nullptr);
                        }
                        else if (status == NET_FAILURE)
                        {
                            failTcpClient(ctx, self, p, SDL_GetError());
                        }
                        else if (p->connectTimeoutMs > 0 && now >= p->deadlineTicks)
                        {
                            failTcpClient(ctx, self, p, "connection timed out while connecting");
                        }
                        return;
                    }
                    case TCPState::Connected:
                        pumpTcpReads(ctx, self, p);
                        return;
                    case TCPState::Reconnecting:
                        if (now >= p->reconnectAtTicks)
                        {
                            // p->addr was released when the previous attempt
                            // failed (see failTcpClient); re-resolve the
                            // originally-requested hostname from scratch.
                            p->addr = NET_ResolveHostname(p->host.c_str());
                            if (!p->addr)
                            {
                                failTcpClient(ctx, self, p, SDL_GetError());
                                return;
                            }
                            if (p->connectTimeoutMs > 0)
                                p->deadlineTicks = now + static_cast<Uint64>(p->connectTimeoutMs);
                            p->state = TCPState::ResolvingAddress;
                        }
                        return;
                    case TCPState::Closed:
                        return;
                    }
                }
            } // namespace

            void Network::poll(JSContext *ctx)
            {
                // UDP clients.
                for (JSValue v : udpClientRegistry())
                {
                    auto *p = unwrapUdpClient(v);
                    if (!p || p->closed || !p->sock)
                        continue;
                    for (;;)
                    {
                        NET_Datagram *dgram = nullptr;
                        if (!NET_ReceiveDatagram(p->sock, &dgram))
                        {
                            JSValue msg = makeErrorMessage(ctx, SDL_GetError());
                            JSValueConst args[] = {msg};
                            invoke(ctx, p->onError, 1, args);
                            JS_FreeValue(ctx, msg);
                            break;
                        }
                        if (!dgram)
                            break;
                        p->bytesReceived += static_cast<uint64_t>(dgram->buflen);
                        std::vector<uint8_t> bytes(dgram->buf, dgram->buf + dgram->buflen);
                        NET_DestroyDatagram(dgram);
                        JSValue msg = wrapMessage(ctx, std::move(bytes));
                        JSValueConst args[] = {msg};
                        invoke(ctx, p->onMessage, 1, args);
                        JS_FreeValue(ctx, msg);
                    }
                }

                // UDP servers.
                for (JSValue v : udpServerRegistry())
                {
                    auto *p = unwrapUdpServer(v);
                    if (!p || p->closed || !p->sock)
                        continue;
                    for (;;)
                    {
                        NET_Datagram *dgram = nullptr;
                        if (!NET_ReceiveDatagram(p->sock, &dgram))
                        {
                            JSValue msg = makeErrorMessage(ctx, SDL_GetError());
                            JSValueConst args[] = {msg};
                            invoke(ctx, p->onError, 1, args);
                            JS_FreeValue(ctx, msg);
                            break;
                        }
                        if (!dgram)
                            break;
                        p->bytesReceived += static_cast<uint64_t>(dgram->buflen);

                        bool known = false;
                        for (auto &peer : p->knownPeers)
                        {
                            if (peer.port == dgram->port && NET_CompareAddresses(peer.addr, dgram->addr) == 0)
                            {
                                known = true;
                                break;
                            }
                        }
                        if (!known)
                            p->knownPeers.push_back(KnownPeer{NET_RefAddress(dgram->addr), dgram->port});

                        std::vector<uint8_t> bytes(dgram->buf, dgram->buf + dgram->buflen);
                        JSValue addrVal = wrapAddress(ctx, NET_RefAddress(dgram->addr), dgram->port);
                        NET_DestroyDatagram(dgram);
                        JSValue msg = wrapMessage(ctx, std::move(bytes));
                        JSValueConst args[] = {msg, addrVal};
                        invoke(ctx, p->onMessage, 2, args);
                        JS_FreeValue(ctx, msg);
                        JS_FreeValue(ctx, addrVal);
                    }
                }

                // TCP servers: accept new connections.
                for (JSValue v : tcpServerRegistry())
                {
                    auto *p = unwrapTcpServer(v);
                    if (!p || p->closed || !p->server)
                        continue;
                    for (;;)
                    {
                        NET_StreamSocket *incoming = nullptr;
                        if (!NET_AcceptClient(p->server, &incoming))
                        {
                            JSValue msg = makeErrorMessage(ctx, SDL_GetError());
                            JSValueConst args[] = {msg};
                            invoke(ctx, p->onError, 1, args);
                            JS_FreeValue(ctx, msg);
                            break;
                        }
                        if (!incoming)
                            break;

                        auto *cp = new TCPClientPayload();
                        cp->sock = incoming;
                        cp->addr = NET_GetStreamSocketAddress(incoming); // already ref'd
                        cp->port = 0;                                    // SDL3_net does not expose the peer's source port
                        cp->state = TCPState::Connected;
                        cp->isServerSide = true;
                        cp->ownerServer = JS_DupValue(ctx, v);

                        JSValue clientObj = JS_NewObjectClass(ctx, Network::s_tcpclient_class_id);
                        if (JS_IsException(clientObj))
                        {
                            NET_DestroyStreamSocket(incoming);
                            if (cp->addr)
                                NET_UnrefAddress(cp->addr);
                            JS_FreeValue(ctx, cp->ownerServer);
                            delete cp;
                            continue;
                        }
                        JS_SetOpaque(clientObj, cp);

                        p->clients.push_back(JS_DupValue(ctx, clientObj));
                        tcpClientRegistry().push_back(JS_DupValue(ctx, clientObj));

                        JSValueConst args[] = {clientObj};
                        invoke(ctx, p->onConnect, 1, args);
                        JS_FreeValue(ctx, clientObj);
                    }
                }

                // TCP clients (both user-created and server-accepted).
                // Iterate over a snapshot since pollOneTcpClient may mutate
                // tcpClientRegistry() (reconnect failures remove entries).
                std::vector<JSValue> snapshot = tcpClientRegistry();
                for (JSValue v : snapshot)
                {
                    auto *p = unwrapTcpClient(v);
                    if (!p)
                        continue;
                    pollOneTcpClient(ctx, v, p);
                }
            }

            void Network::destroy(JSContext *ctx)
            {
                for (JSValue v : tcpClientRegistry())
                {
                    auto *p = unwrapTcpClient(v);
                    if (p)
                        teardownTcpClient(ctx, v, p, false);
                    JS_FreeValue(ctx, v);
                }
                tcpClientRegistry().clear();

                for (JSValue v : tcpServerRegistry())
                {
                    auto *p = unwrapTcpServer(v);
                    if (p)
                    {
                        if (p->server)
                        {
                            NET_DestroyServer(p->server);
                            p->server = nullptr;
                            p->closed = true;
                        }
                        // p->clients holds its own dup'd refs, distinct from the
                        // ones already freed via tcpClientRegistry() above.
                        for (JSValue c : p->clients)
                            JS_FreeValue(ctx, c);
                        p->clients.clear();
                    }
                    JS_FreeValue(ctx, v);
                }
                tcpServerRegistry().clear();

                for (JSValue v : udpClientRegistry())
                {
                    auto *p = unwrapUdpClient(v);
                    if (p && p->sock)
                    {
                        NET_DestroyDatagramSocket(p->sock);
                        p->sock = nullptr;
                        p->closed = true;
                    }
                    JS_FreeValue(ctx, v);
                }
                udpClientRegistry().clear();

                for (JSValue v : udpServerRegistry())
                {
                    auto *p = unwrapUdpServer(v);
                    if (p)
                    {
                        if (p->sock)
                        {
                            NET_DestroyDatagramSocket(p->sock);
                            p->sock = nullptr;
                            p->closed = true;
                        }
                        for (auto &peer : p->knownPeers)
                            NET_UnrefAddress(peer.addr);
                        p->knownPeers.clear();
                    }
                    JS_FreeValue(ctx, v);
                }
                udpServerRegistry().clear();

                NET_Quit();
            }

            // Module wiring

            namespace
            {
                JSValue js_address_ctor_throws(JSContext *ctx, JSValueConst, int, JSValueConst *)
                {
                    return JS_ThrowTypeError(ctx, "Address is not constructible; use Address.parse(uri) instead");
                }

                const JSCFunctionListEntry s_addressFuncs[] = {
                    JS_CGETSET_DEF("host", Network::js_address_get_host, nullptr),
                    JS_CGETSET_DEF("port", Network::js_address_get_port, nullptr),
                    JS_CFUNC_DEF("toString", 0, Network::js_address_to_string),
                    JS_CFUNC_DEF("equals", 1, Network::js_address_equals),
                };

                const JSCFunctionListEntry s_messageFuncs[] = {
                    JS_CGETSET_DEF("byteLength", Network::js_message_get_byte_length, nullptr),
                    JS_CGETSET_DEF("bytesRemaining", Network::js_message_get_bytes_remaining, nullptr),
                    JS_CFUNC_DEF("peekByte", 1, Network::js_message_peek_byte),
                    JS_CFUNC_DEF("peekBytes", 2, Network::js_message_peek_bytes),
                    JS_CFUNC_DEF("readUint8", 0, Network::js_message_read_uint8),
                    JS_CFUNC_DEF("readInt8", 0, Network::js_message_read_int8),
                    JS_CFUNC_DEF("readUint16", 0, Network::js_message_read_uint16),
                    JS_CFUNC_DEF("readInt16", 0, Network::js_message_read_int16),
                    JS_CFUNC_DEF("readUint32", 0, Network::js_message_read_uint32),
                    JS_CFUNC_DEF("readInt32", 0, Network::js_message_read_int32),
                    JS_CFUNC_DEF("readFloat32", 0, Network::js_message_read_float32),
                    JS_CFUNC_DEF("readFloat64", 0, Network::js_message_read_float64),
                    JS_CFUNC_DEF("readString", 1, Network::js_message_read_string),
                    JS_CFUNC_DEF("readBytes", 1, Network::js_message_read_bytes),
                    JS_CFUNC_DEF("readRemaining", 0, Network::js_message_read_remaining),
                    JS_CFUNC_DEF("writeUint8", 1, Network::js_message_write_uint8),
                    JS_CFUNC_DEF("writeInt8", 1, Network::js_message_write_int8),
                    JS_CFUNC_DEF("writeUint16", 1, Network::js_message_write_uint16),
                    JS_CFUNC_DEF("writeInt16", 1, Network::js_message_write_int16),
                    JS_CFUNC_DEF("writeUint32", 1, Network::js_message_write_uint32),
                    JS_CFUNC_DEF("writeInt32", 1, Network::js_message_write_int32),
                    JS_CFUNC_DEF("writeFloat32", 1, Network::js_message_write_float32),
                    JS_CFUNC_DEF("writeFloat64", 1, Network::js_message_write_float64),
                    JS_CFUNC_DEF("writeString", 1, Network::js_message_write_string),
                    JS_CFUNC_DEF("writeBytes", 1, Network::js_message_write_bytes),
                    JS_CFUNC_DEF("seek", 1, Network::js_message_seek),
                    JS_CFUNC_DEF("reset", 0, Network::js_message_reset),
                    JS_CFUNC_DEF("asBytes", 0, Network::js_message_as_bytes),
                    JS_CFUNC_DEF("asJSON", 0, Network::js_message_as_json),
                };

                const JSCFunctionListEntry s_tcpClientFuncs[] = {
                    JS_CGETSET_DEF("isConnected", Network::js_tcpclient_get_is_connected, nullptr),
                    JS_CGETSET_DEF("address", Network::js_tcpclient_get_address, nullptr),
                    JS_CGETSET_DEF("bytesSent", Network::js_tcpclient_get_bytes_sent, nullptr),
                    JS_CGETSET_DEF("bytesReceived", Network::js_tcpclient_get_bytes_received, nullptr),
                    JS_CFUNC_DEF("send", 1, Network::js_tcpclient_send),
                    JS_CFUNC_DEF("close", 0, Network::js_tcpclient_close),
                    JS_CGETSET_DEF("onConnect", Network::js_tcpclient_get_onconnect, Network::js_tcpclient_set_onconnect),
                    JS_CGETSET_DEF("onDisconnect", Network::js_tcpclient_get_ondisconnect, Network::js_tcpclient_set_ondisconnect),
                    JS_CGETSET_DEF("onError", Network::js_tcpclient_get_onerror, Network::js_tcpclient_set_onerror),
                    JS_CGETSET_DEF("onMessage", Network::js_tcpclient_get_onmessage, Network::js_tcpclient_set_onmessage),
                };

                const JSCFunctionListEntry s_tcpServerFuncs[] = {
                    JS_CGETSET_DEF("isListening", Network::js_tcpserver_get_is_listening, nullptr),
                    JS_CGETSET_DEF("clients", Network::js_tcpserver_get_clients, nullptr),
                    JS_CFUNC_DEF("broadcast", 1, Network::js_tcpserver_broadcast),
                    JS_CFUNC_DEF("broadcastExcept", 2, Network::js_tcpserver_broadcast_except),
                    JS_CFUNC_DEF("close", 0, Network::js_tcpserver_close),
                    JS_CGETSET_DEF("onConnect", Network::js_tcpserver_get_onconnect, Network::js_tcpserver_set_onconnect),
                    JS_CGETSET_DEF("onDisconnect", Network::js_tcpserver_get_ondisconnect, Network::js_tcpserver_set_ondisconnect),
                    JS_CGETSET_DEF("onError", Network::js_tcpserver_get_onerror, Network::js_tcpserver_set_onerror),
                    JS_CGETSET_DEF("onMessage", Network::js_tcpserver_get_onmessage, Network::js_tcpserver_set_onmessage),
                };

                const JSCFunctionListEntry s_udpClientFuncs[] = {
                    JS_CGETSET_DEF("isOpen", Network::js_udpclient_get_is_open, nullptr),
                    JS_CGETSET_DEF("bytesSent", Network::js_udpclient_get_bytes_sent, nullptr),
                    JS_CGETSET_DEF("bytesReceived", Network::js_udpclient_get_bytes_received, nullptr),
                    JS_CFUNC_DEF("send", 1, Network::js_udpclient_send),
                    JS_CFUNC_DEF("sendTo", 2, Network::js_udpclient_send_to),
                    JS_CFUNC_DEF("close", 0, Network::js_udpclient_close),
                    JS_CGETSET_DEF("onMessage", Network::js_udpclient_get_onmessage, Network::js_udpclient_set_onmessage),
                    JS_CGETSET_DEF("onError", Network::js_udpclient_get_onerror, Network::js_udpclient_set_onerror),
                };

                const JSCFunctionListEntry s_udpServerFuncs[] = {
                    JS_CGETSET_DEF("isOpen", Network::js_udpserver_get_is_open, nullptr),
                    JS_CGETSET_DEF("bytesSent", Network::js_udpserver_get_bytes_sent, nullptr),
                    JS_CGETSET_DEF("bytesReceived", Network::js_udpserver_get_bytes_received, nullptr),
                    JS_CFUNC_DEF("sendTo", 2, Network::js_udpserver_send_to),
                    JS_CFUNC_DEF("broadcastToKnownPeers", 1, Network::js_udpserver_broadcast_to_known_peers),
                    JS_CFUNC_DEF("close", 0, Network::js_udpserver_close),
                    JS_CGETSET_DEF("onMessage", Network::js_udpserver_get_onmessage, Network::js_udpserver_set_onmessage),
                    JS_CGETSET_DEF("onError", Network::js_udpserver_get_onerror, Network::js_udpserver_set_onerror),
                };

            }

            int Network::declare(JSContext *ctx, JSModuleDef *m)
            {
                JS_AddModuleExport(ctx, m, "Address");
                JS_AddModuleExport(ctx, m, "Message");
                JS_AddModuleExport(ctx, m, "TCPClient");
                JS_AddModuleExport(ctx, m, "TCPServer");
                JS_AddModuleExport(ctx, m, "UDPClient");
                JS_AddModuleExport(ctx, m, "UDPServer");
                return 0;
            }

            int Network::init(JSContext *ctx, JSModuleDef *m)
            {
                NET_Init();

                JSRuntime *rt = JS_GetRuntime(ctx);

                JS_NewClassID(rt, &s_address_class_id);
                JS_NewClassID(rt, &s_message_class_id);
                JS_NewClassID(rt, &s_tcpclient_class_id);
                JS_NewClassID(rt, &s_tcpserver_class_id);
                JS_NewClassID(rt, &s_udpclient_class_id);
                JS_NewClassID(rt, &s_udpserver_class_id);

                static JSClassDef addressClass = {"Address", .finalizer = address_finalizer};
                static JSClassDef messageClass = {"Message", .finalizer = message_finalizer};
                static JSClassDef tcpClientClass = {"TCPClient", .finalizer = tcpclient_finalizer, .gc_mark = tcpclient_gc_mark};
                static JSClassDef tcpServerClass = {"TCPServer", .finalizer = tcpserver_finalizer, .gc_mark = tcpserver_gc_mark};
                static JSClassDef udpClientClass = {"UDPClient", .finalizer = udpclient_finalizer, .gc_mark = udpclient_gc_mark};
                static JSClassDef udpServerClass = {"UDPServer", .finalizer = udpserver_finalizer, .gc_mark = udpserver_gc_mark};

                JS_NewClass(rt, s_address_class_id, &addressClass);
                JS_NewClass(rt, s_message_class_id, &messageClass);
                JS_NewClass(rt, s_tcpclient_class_id, &tcpClientClass);
                JS_NewClass(rt, s_tcpserver_class_id, &tcpServerClass);
                JS_NewClass(rt, s_udpclient_class_id, &udpClientClass);
                JS_NewClass(rt, s_udpserver_class_id, &udpServerClass);

                JSValue addressProto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, addressProto, s_addressFuncs,
                                           sizeof(s_addressFuncs) / sizeof(s_addressFuncs[0]));
                JS_SetClassProto(ctx, s_address_class_id, addressProto);

                JSValue addressCtor = JS_NewCFunction2(ctx, js_address_ctor_throws, "Address", 0, JS_CFUNC_constructor, 0);
                JS_SetConstructor(ctx, addressCtor, addressProto);
                JS_SetPropertyStr(ctx, addressCtor, "parse",
                                  JS_NewCFunction(ctx, &Network::js_address_parse, "parse", 1));
                JS_SetModuleExport(ctx, m, "Address", addressCtor);

                JSValue messageProto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, messageProto, s_messageFuncs,
                                           sizeof(s_messageFuncs) / sizeof(s_messageFuncs[0]));
                JS_SetClassProto(ctx, s_message_class_id, messageProto);

                JSValue messageCtor = JS_NewCFunction2(ctx, &Network::js_message_ctor, "Message", 1, JS_CFUNC_constructor, 0);
                JS_SetConstructor(ctx, messageCtor, messageProto);
                JS_SetModuleExport(ctx, m, "Message", messageCtor);

                JSValue tcpClientProto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, tcpClientProto, s_tcpClientFuncs,
                                           sizeof(s_tcpClientFuncs) / sizeof(s_tcpClientFuncs[0]));
                JS_SetClassProto(ctx, s_tcpclient_class_id, tcpClientProto);

                JSValue tcpClientCtor = JS_NewCFunction2(ctx, &Network::js_tcpclient_ctor, "TCPClient", 2, JS_CFUNC_constructor, 0);
                JS_SetConstructor(ctx, tcpClientCtor, tcpClientProto);
                JS_SetModuleExport(ctx, m, "TCPClient", tcpClientCtor);

                JSValue tcpServerProto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, tcpServerProto, s_tcpServerFuncs,
                                           sizeof(s_tcpServerFuncs) / sizeof(s_tcpServerFuncs[0]));
                JS_SetClassProto(ctx, s_tcpserver_class_id, tcpServerProto);

                JSValue tcpServerCtor = JS_NewCFunction2(ctx, &Network::js_tcpserver_ctor, "TCPServer", 2, JS_CFUNC_constructor, 0);
                JS_SetConstructor(ctx, tcpServerCtor, tcpServerProto);
                JS_SetModuleExport(ctx, m, "TCPServer", tcpServerCtor);

                JSValue udpClientProto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, udpClientProto, s_udpClientFuncs,
                                           sizeof(s_udpClientFuncs) / sizeof(s_udpClientFuncs[0]));
                JS_SetClassProto(ctx, s_udpclient_class_id, udpClientProto);

                JSValue udpClientCtor = JS_NewCFunction2(ctx, &Network::js_udpclient_ctor, "UDPClient", 2, JS_CFUNC_constructor, 0);
                JS_SetConstructor(ctx, udpClientCtor, udpClientProto);
                JS_SetModuleExport(ctx, m, "UDPClient", udpClientCtor);

                JSValue udpServerProto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, udpServerProto, s_udpServerFuncs,
                                           sizeof(s_udpServerFuncs) / sizeof(s_udpServerFuncs[0]));
                JS_SetClassProto(ctx, s_udpserver_class_id, udpServerProto);

                JSValue udpServerCtor = JS_NewCFunction2(ctx, &Network::js_udpserver_ctor, "UDPServer", 2, JS_CFUNC_constructor, 0);
                JS_SetConstructor(ctx, udpServerCtor, udpServerProto);
                JS_SetModuleExport(ctx, m, "UDPServer", udpServerCtor);

                return 0;
            }

        }
    }
}