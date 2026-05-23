#include "EngineModule.hpp"
#include "../Engine.hpp"

namespace Bokken
{
    namespace Scripting
    {
        namespace Modules
        {
            int EngineModule::declare(JSContext *ctx, JSModuleDef *m)
            {
                /* Single export — everything hangs off the default
                 * object. No globals, no named timing exports. Users
                 * write `import Engine from "bokken/engine"` and
                 * reach everything as `Engine.setTimeout(…)`,
                 * `Engine.frameCount`, etc. */
                JS_AddModuleExport(ctx, m, "default");
                return 0;
            }

            int EngineModule::init(JSContext *ctx, JSModuleDef *m)
            {
                JSValue api = JS_NewObject(ctx);

                /* Static metadata */
                JS_SetPropertyStr(ctx, api, "version",
                                  JS_NewString(ctx, "1.0.0-alpha"));

                /* Timing primitives */
                /* setTimeout / setInterval — magic 0 vs 1 picks
                 * one-shot vs repeating. clearTimeout / clearInterval
                 * share an implementation (both clear by id). */
                JS_SetPropertyStr(ctx, api, "setTimeout",
                    JS_NewCFunctionMagic(ctx, &Engine::js_set_timer,
                                         "setTimeout", 2, JS_CFUNC_generic_magic, 0));
                JS_SetPropertyStr(ctx, api, "setInterval",
                    JS_NewCFunctionMagic(ctx, &Engine::js_set_timer,
                                         "setInterval", 2, JS_CFUNC_generic_magic, 1));
                JS_SetPropertyStr(ctx, api, "clearTimeout",
                    JS_NewCFunction(ctx, &Engine::js_clear_timer,
                                    "clearTimeout", 1));
                JS_SetPropertyStr(ctx, api, "clearInterval",
                    JS_NewCFunction(ctx, &Engine::js_clear_timer,
                                    "clearInterval", 1));

                /* RAF — Loop drives this each frame via
                 * Engine::fireAnimationFrames(). */
                JS_SetPropertyStr(ctx, api, "requestAnimationFrame",
                    JS_NewCFunction(ctx, &Engine::js_request_animation_frame,
                                    "requestAnimationFrame", 1));
                JS_SetPropertyStr(ctx, api, "cancelAnimationFrame",
                    JS_NewCFunction(ctx, &Engine::js_cancel_animation_frame,
                                    "cancelAnimationFrame", 1));

                /* queueMicrotask — chains the user fn onto
                 * Promise.resolve().then so it lands in QuickJS's
                 * standard job queue, drained after every hook. */
                JS_SetPropertyStr(ctx, api, "queueMicrotask",
                    JS_NewCFunction(ctx, &Engine::js_queue_microtask,
                                    "queueMicrotask", 1));

                /* `now()` — high-resolution timestamp in ms since
                 * init. Same clock as the timer queue and the
                 * timestamp passed to RAF callbacks, so deltas are
                 * consistent across all three. */
                JS_SetPropertyStr(ctx, api, "now",
                    JS_NewCFunction(ctx, js_engine_now, "now", 0));

                /* Live frame stats (getters) */
                /* frameCount / frameTime / elapsed change every tick;
                 * a static snapshot at module-init would be
                 * permanently stale. JS_DefinePropertyGetSet calls
                 * the getter via the standard function-call path so
                 * the binding must use the generic 4-arg signature. */
                auto defineGetter = [&](const char *name, JSCFunction *getter) {
                    JSAtom atom = JS_NewAtom(ctx, name);
                    JSValue getFn = JS_NewCFunction(ctx, getter, name, 0);
                    JS_DefinePropertyGetSet(ctx, api, atom,
                                            getFn, JS_UNDEFINED,
                                            JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
                    JS_FreeAtom(ctx, atom);
                };
                defineGetter("frameCount", js_get_frame_count);
                defineGetter("frameTime",  js_get_frame_time);
                defineGetter("elapsed",    js_get_elapsed);

                JS_SetModuleExport(ctx, m, "default", api);
                return 0;
            }

            JSValue EngineModule::js_engine_now(JSContext *ctx, JSValueConst /*this_val*/,
                                                int /*argc*/, JSValueConst * /*argv*/)
            {
                return JS_NewFloat64(ctx, Engine::Instance().clockSeconds() * 1000.0);
            }

            JSValue EngineModule::js_get_frame_count(JSContext *ctx, JSValueConst /*this_val*/,
                                                     int /*argc*/, JSValueConst * /*argv*/)
            {
                /* uint64_t counter → JS Number. Safe-integer range is
                 * 2^53-1 which fits any plausible session length. */
                return JS_NewInt64(ctx, (int64_t)Engine::Instance().frameCount());
            }

            JSValue EngineModule::js_get_frame_time(JSContext *ctx, JSValueConst /*this_val*/,
                                                    int /*argc*/, JSValueConst * /*argv*/)
            {
                return JS_NewFloat64(ctx, Engine::Instance().lastFrameDt());
            }

            JSValue EngineModule::js_get_elapsed(JSContext *ctx, JSValueConst /*this_val*/,
                                                 int /*argc*/, JSValueConst * /*argv*/)
            {
                return JS_NewFloat64(ctx, Engine::Instance().clockSeconds());
            }
        }
    }
}