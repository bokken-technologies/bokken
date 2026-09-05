#pragma once

#include "Base.hpp"

#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>
#include <quickjs.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Bokken
{
    namespace Scripting
    {
        namespace Modules
        {

            /**
             * `bokken/network` — JS-facing sockets, built on SDL3_net.
             *
             * SDL3_net is entirely non-blocking: resolving a hostname,
             * connecting a stream socket, and accepting clients are all
             * asynchronous operations that need to be pumped over time.
             * The TypeScript surface this module implements, however,
             * reads as a synchronous/event-driven API (constructors that
             * "just work", onConnect/onMessage callbacks). To reconcile
             * the two:
             *
             *   - Address::parse() and the initial connect performed by
             *     TCPClient/UDPClient/UDPServer constructors block for up
             *     to a timeout (options.connectTimeout, default below)
             *     using NET_WaitUntilResolved / NET_WaitUntilConnected.
             *     This keeps `new TCPClient(uri)` behaving the way script
             *     authors expect, at the cost of a possible frame hitch
             *     on connect. If that's unacceptable for a given call
             *     site, script code can race a timer against isConnected.
             *
             *   - Everything that happens *after* a socket is live
             *     (reads, accepts, disconnects, reconnects) is pumped by
             *     Network::poll(), which the engine's main loop MUST call
             *     once per frame — the same way Loop steps
             *     Bokken::Physics::World. This header does not wire that
             *     call up itself; nothing in the codebase currently
             *     drives it.
             *
             * Surface (named exports, matching the .d.ts):
             *   new Address(...)        — not constructible from JS; only
             *                              Address.parse() and internal
             *                              APIs (peer/sender addresses)
             *                              produce instances.
             *   Address.parse(uri)      → Address
             *   address.host / .port / .toString() / .equals(other)
             *
             *   new Message()                    — empty, for building
             *                                        outgoing buffers.
             *   new Message(bytes: Uint8Array)    — pre-filled, e.g. to
             *                                        re-parse a received
             *                                        Uint8Array.
             *   (This constructor isn't in the .d.ts, which only lists
             *   instance methods; it's added because building an outgoing
             *   Message is otherwise impossible from script.)
             *
             *   new TCPClient(uri, options?)
             *   new TCPServer(uri, events?)
             *   new UDPClient(uri, events?)
             *   new UDPServer(port, events?)
             *
             * Wire format: Message reads/writes are little-endian and
             * hand-rolled (no dependency on host byte order or on
             * SDL's endian-swap macros), so a Message written on any
             * platform decodes identically on any other.
            */
            class Network : public Base
            {
            public:
                Network() : Base("bokken/network") {}

                int declare(JSContext *ctx, JSModuleDef *m) override;
                int init(JSContext *ctx, JSModuleDef *m) override;
                void destroy(JSContext *ctx) override;

                // Pumps every live socket. Must be called exactly once per
                // frame by the engine's main loop for connects, reads,
                // accepts, reconnects, and disconnects to be noticed and
                // for the corresponding JS callbacks to fire.
                static void poll(JSContext *ctx);

                // Called once at shutdown, after script teardown, to close
                // any sockets still alive and NET_Quit() the library.
                static void shutdown(JSContext *ctx);

                // Class ids for the opaque-object classes below.
                static inline JSClassID s_address_class_id   = 0;
                static inline JSClassID s_message_class_id   = 0;
                static inline JSClassID s_tcpclient_class_id = 0;
                static inline JSClassID s_tcpserver_class_id = 0;
                static inline JSClassID s_udpclient_class_id = 0;
                static inline JSClassID s_udpserver_class_id = 0;

                // Finalizers.
                static void address_finalizer(JSRuntime *rt, JSValue val);
                static void message_finalizer(JSRuntime *rt, JSValue val);
                static void tcpclient_finalizer(JSRuntime *rt, JSValue val);
                static void tcpserver_finalizer(JSRuntime *rt, JSValue val);
                static void udpclient_finalizer(JSRuntime *rt, JSValue val);
                static void udpserver_finalizer(JSRuntime *rt, JSValue val);

                // GC mark functions for classes holding JS callback values.
                static void tcpclient_gc_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func);
                static void tcpserver_gc_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func);
                static void udpclient_gc_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func);
                static void udpserver_gc_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func);

                // Constructors.
                static JSValue js_message_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);
                static JSValue js_tcpclient_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);
                static JSValue js_tcpserver_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);
                static JSValue js_udpclient_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);
                static JSValue js_udpserver_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);

                // Address.
                static JSValue js_address_parse(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_address_get_host(JSContext *ctx, JSValueConst this_val);
                static JSValue js_address_get_port(JSContext *ctx, JSValueConst this_val);
                static JSValue js_address_to_string(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_address_equals(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

                // Message.
                static JSValue js_message_get_byte_length(JSContext *ctx, JSValueConst this_val);
                static JSValue js_message_get_bytes_remaining(JSContext *ctx, JSValueConst this_val);
                static JSValue js_message_peek_byte(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_peek_bytes(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_read_uint8(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_read_int8(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_read_uint16(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_read_int16(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_read_uint32(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_read_int32(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_read_float32(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_read_float64(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_read_string(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_read_bytes(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_read_remaining(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_write_uint8(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_write_int8(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_write_uint16(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_write_int16(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_write_uint32(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_write_int32(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_write_float32(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_write_float64(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_write_string(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_write_bytes(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_seek(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_reset(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_as_bytes(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_message_as_json(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

                // TCPClient.
                static JSValue js_tcpclient_get_is_connected(JSContext *ctx, JSValueConst this_val);
                static JSValue js_tcpclient_get_address(JSContext *ctx, JSValueConst this_val);
                static JSValue js_tcpclient_get_bytes_sent(JSContext *ctx, JSValueConst this_val);
                static JSValue js_tcpclient_get_bytes_received(JSContext *ctx, JSValueConst this_val);
                static JSValue js_tcpclient_send(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_tcpclient_close(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_tcpclient_get_onconnect(JSContext *ctx, JSValueConst this_val);
                static JSValue js_tcpclient_set_onconnect(JSContext *ctx, JSValueConst this_val, JSValueConst v);
                static JSValue js_tcpclient_get_ondisconnect(JSContext *ctx, JSValueConst this_val);
                static JSValue js_tcpclient_set_ondisconnect(JSContext *ctx, JSValueConst this_val, JSValueConst v);
                static JSValue js_tcpclient_get_onerror(JSContext *ctx, JSValueConst this_val);
                static JSValue js_tcpclient_set_onerror(JSContext *ctx, JSValueConst this_val, JSValueConst v);
                static JSValue js_tcpclient_get_onmessage(JSContext *ctx, JSValueConst this_val);
                static JSValue js_tcpclient_set_onmessage(JSContext *ctx, JSValueConst this_val, JSValueConst v);

                // TCPServer.
                static JSValue js_tcpserver_get_is_listening(JSContext *ctx, JSValueConst this_val);
                static JSValue js_tcpserver_get_clients(JSContext *ctx, JSValueConst this_val);
                static JSValue js_tcpserver_broadcast(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_tcpserver_broadcast_except(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_tcpserver_close(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_tcpserver_get_onconnect(JSContext *ctx, JSValueConst this_val);
                static JSValue js_tcpserver_set_onconnect(JSContext *ctx, JSValueConst this_val, JSValueConst v);
                static JSValue js_tcpserver_get_ondisconnect(JSContext *ctx, JSValueConst this_val);
                static JSValue js_tcpserver_set_ondisconnect(JSContext *ctx, JSValueConst this_val, JSValueConst v);
                static JSValue js_tcpserver_get_onerror(JSContext *ctx, JSValueConst this_val);
                static JSValue js_tcpserver_set_onerror(JSContext *ctx, JSValueConst this_val, JSValueConst v);
                static JSValue js_tcpserver_get_onmessage(JSContext *ctx, JSValueConst this_val);
                static JSValue js_tcpserver_set_onmessage(JSContext *ctx, JSValueConst this_val, JSValueConst v);

                // UDPClient.
                static JSValue js_udpclient_get_is_open(JSContext *ctx, JSValueConst this_val);
                static JSValue js_udpclient_get_bytes_sent(JSContext *ctx, JSValueConst this_val);
                static JSValue js_udpclient_get_bytes_received(JSContext *ctx, JSValueConst this_val);
                static JSValue js_udpclient_send(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_udpclient_send_to(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_udpclient_close(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_udpclient_get_onmessage(JSContext *ctx, JSValueConst this_val);
                static JSValue js_udpclient_set_onmessage(JSContext *ctx, JSValueConst this_val, JSValueConst v);
                static JSValue js_udpclient_get_onerror(JSContext *ctx, JSValueConst this_val);
                static JSValue js_udpclient_set_onerror(JSContext *ctx, JSValueConst this_val, JSValueConst v);

                // UDPServer.
                static JSValue js_udpserver_get_is_open(JSContext *ctx, JSValueConst this_val);
                static JSValue js_udpserver_get_bytes_sent(JSContext *ctx, JSValueConst this_val);
                static JSValue js_udpserver_get_bytes_received(JSContext *ctx, JSValueConst this_val);
                static JSValue js_udpserver_send_to(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_udpserver_broadcast_to_known_peers(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_udpserver_close(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_udpserver_get_onmessage(JSContext *ctx, JSValueConst this_val);
                static JSValue js_udpserver_set_onmessage(JSContext *ctx, JSValueConst this_val, JSValueConst v);
                static JSValue js_udpserver_get_onerror(JSContext *ctx, JSValueConst this_val);
                static JSValue js_udpserver_set_onerror(JSContext *ctx, JSValueConst this_val, JSValueConst v);
            };

        }
    }
}