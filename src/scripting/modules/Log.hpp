#pragma once

#include "Base.hpp"

#include <SDL3/SDL_log.h>
#include <cstdio>
#include <string>

namespace Bokken
{
    namespace Scripting
    {
        namespace Modules
        {
            /**
             * `bokken/log` — variadic logging API.
             *
             * Default export with four severities, each taking any
             * number of arguments of any type:
             *
             *   import Log from "bokken/log";
             *
             *   Log.debug("loading", path);
             *   Log.info("player at", x, y);
             *   Log.warning("retrying", attempt, "of", max);
             *   Log.error("failed:", err);
             *
             * Argument coercion mirrors what console.log does in the
             * browser:
             *
             *   - Strings, numbers, booleans, null, undefined  →
             *     toString().
             *   - Plain objects and arrays  →  JSON.stringify(arg).
             *     This is what makes `Log.info({score: 42})` print
             *     `{"score":42}` instead of the useless
             *     `[object Object]` you'd get from raw toString.
             *   - Errors  →  `name: message` (the toString
             *     representation, which IS useful for Error).
             *
             * Arguments are joined with single spaces, then routed to
             * SDL_Log at the appropriate severity (Debug, Info, Warn,
             * Error). The `[js]` prefix marks the line as coming from
             * script in mixed-source logs.
            */
            class Log : public Base
            {
            public:
                Log() : Base("bokken/log") {}

                int declare(JSContext *ctx, JSModuleDef *m) override
                {
                    return JS_AddModuleExport(ctx, m, "default");
                }

                int init(JSContext *ctx, JSModuleDef *m) override
                {
                    JSValue defaultExport = JS_NewObject(ctx);

                    /* Single C trampoline used for all four severities
                     * via the magic int. Variadic by accepting any
                     * argc; the per-arg coercion lives in
                     * stringify_arg below. */
                    auto log_fn = [](JSContext *ctx, JSValueConst /*this_val*/,
                                     int argc, JSValueConst *argv,
                                     int magic) -> JSValue
                    {
                        std::string line;
                        for (int i = 0; i < argc; i++)
                        {
                            if (i > 0) line += ' ';
                            line += stringify_arg(ctx, argv[i]);
                        }

                        switch (magic)
                        {
                        case 0:
                            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                                         "[Log] %s", line.c_str());
                            break;
                        case 1:
                            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                        "[Log] %s", line.c_str());
                            break;
                        case 2:
                            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                        "[Log] %s", line.c_str());
                            break;
                        case 3:
                            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                         "[Log] %s", line.c_str());
                            break;
                        }

                        return JS_UNDEFINED;
                    };

                    /* The min-arg field is 0, not 1, so calling
                     * Log.info() with no arguments doesn't throw and
                     * prints an empty line — same as console.log()
                     * with no args. */
                    JS_SetPropertyStr(ctx, defaultExport, "debug",
                        JS_NewCFunctionMagic(ctx, log_fn, "debug", 0,
                                             JS_CFUNC_generic_magic, 0));
                    JS_SetPropertyStr(ctx, defaultExport, "info",
                        JS_NewCFunctionMagic(ctx, log_fn, "info", 0,
                                             JS_CFUNC_generic_magic, 1));
                    JS_SetPropertyStr(ctx, defaultExport, "warning",
                        JS_NewCFunctionMagic(ctx, log_fn, "warning", 0,
                                             JS_CFUNC_generic_magic, 2));
                    JS_SetPropertyStr(ctx, defaultExport, "error",
                        JS_NewCFunctionMagic(ctx, log_fn, "error", 0,
                                             JS_CFUNC_generic_magic, 3));

                    JS_SetModuleExport(ctx, m, "default", defaultExport);
                    return 0;
                }

            private:
                /* Coerce a JSValue to a printable std::string with the
                 * same rules console.log uses: primitives go through
                 * toString, plain objects/arrays go through
                 * JSON.stringify so they print their structure rather
                 * than the useless "[object Object]".
                 *
                 * If JSON.stringify throws (cyclic reference, BigInt,
                 * etc.) we fall back to toString — better to print
                 * "[object Object]" than to lose the whole log line.
                 *
                 * Functions and symbols use their toString directly
                 * (functions print their declaration, symbols print
                 * "Symbol(desc)") — matches browser behaviour. */
                static std::string stringify_arg(JSContext *ctx, JSValueConst v)
                {
                    /* Fast path for strings and primitives — JS_ToCString
                     * already handles them perfectly. */
                    if (!JS_IsObject(v))
                    {
                        const char *s = JS_ToCString(ctx, v);
                        if (!s) return "";
                        std::string out(s);
                        JS_FreeCString(ctx, s);
                        return out;
                    }

                    /* Object/array — try JSON.stringify first.
                     * JS_JSONStringify(ctx, val, replacer, indent) with
                     * undefined replacer and undefined indent (0 spaces)
                     * gives the compact form, which is what we want for
                     * a log line. */
                    JSValue json = JS_JSONStringify(ctx, v, JS_UNDEFINED, JS_UNDEFINED);
                    if (!JS_IsException(json) && !JS_IsUndefined(json))
                    {
                        const char *s = JS_ToCString(ctx, json);
                        std::string out;
                        if (s) { out = s; JS_FreeCString(ctx, s); }
                        JS_FreeValue(ctx, json);
                        if (!out.empty()) return out;
                    }
                    else if (JS_IsException(json))
                    {
                        /* Swallow the exception — we don't want a
                         * malformed log call to throw out of the
                         * logger. The user gets the toString fallback. */
                        JSValue ex = JS_GetException(ctx);
                        JS_FreeValue(ctx, ex);
                        JS_FreeValue(ctx, json);
                    }
                    else
                    {
                        JS_FreeValue(ctx, json);
                    }

                    /* Fallback: plain toString. For Errors this gives
                     * "TypeError: foo", which is more useful than
                     * JSON.stringify's typical "{}" for Error objects
                     * (Error.prototype is non-enumerable). */
                    const char *s = JS_ToCString(ctx, v);
                    if (!s) return "";
                    std::string out(s);
                    JS_FreeCString(ctx, s);
                    return out;
                }
            };

        } // namespace Modules
    } // namespace Scripting
} // namespace Bokken