#include "Engine.hpp"
#include <algorithm>
#include <SDL3/SDL_log.h>

namespace Bokken
{
    namespace Scripting
    {
        // Static module loader callback for QuickJS-NG. This is registered as a fallback
        // for any import path that doesn't match a registered Modules::Base. It attempts
        // to load modules from the AssetPack, allowing users to import bytecode modules
        // directly from their scripts.
        JSModuleDef *Engine::s_module_loader(JSContext *ctx, const char *name, void *opaque)
        {
            // Cast opaque back to our Scripting Engine
            auto *engine = static_cast<Engine *>(opaque);

            // 1. Check native modules
            if (strncmp(name, "bokken/", 7) == 0)
            {
                return nullptr;
            }

            // Remap the module path to the expected location in the AssetPack
            std::string path = std::string("/scripts/") + name;
            size_t pos = path.rfind(".js");
            if (pos != std::string::npos)
                path.replace(pos, 3, ".script");

            if (engine && engine->m_assets && engine->m_assets->exists(path.c_str()))
            {
                std::vector<uint8_t> bc = engine->m_assets->readBytes(path.c_str());
                if (!bc.empty())
                {
                    JSValue obj = JS_ReadObject(ctx, bc.data(), bc.size(), JS_READ_OBJ_BYTECODE);
                    if (!JS_IsException(obj))
                    {
                        JSModuleDef *mod = (JSModuleDef *)JS_VALUE_GET_PTR(obj);
                        JS_FreeValue(ctx, obj);
                        return mod;
                    }
                    JS_FreeValue(ctx, obj);
                }
            }

            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Bokken] Module not found: %s\n", name);
            return nullptr;
        }

        // Lifecycle
        bool Engine::init(AssetPack *assets, int maxHeapMb, int stackKb, int gcThreshKb)
        {
            m_assets = assets;

            m_rt = JS_NewRuntime();
            if (!m_rt)
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Bokken] Failed to create JSRuntime\n");
                return false;
            }

            // Apply memory / stack limits from the project configuration.
            JS_SetMemoryLimit(m_rt, static_cast<size_t>(maxHeapMb) * 1024 * 1024);
            JS_SetMaxStackSize(m_rt, static_cast<size_t>(stackKb) * 1024);
            JS_SetGCThreshold(m_rt, static_cast<size_t>(gcThreshKb) * 1024);

            m_ctx = JS_NewContext(m_rt);
            if (!m_ctx)
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Bokken] Failed to create JSContext\n");
                JS_FreeRuntime(m_rt);
                m_rt = nullptr;
                return false;
            }

            // Install the stub module loader. It acts as a fallback for any import
            // path that doesn't match a registered Modules::Base.
            JS_SetModuleLoaderFunc(m_rt, nullptr, &Engine::s_module_loader, this);

            return true;
        }

        void Engine::addModule(std::unique_ptr<Modules::Base> module)
        {
            if (!m_ctx)
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Bokken] addModule() called before init()\n");
                return;
            }
            module->registerInto(m_ctx);
            m_modules.push_back(std::move(module));
        }

        bool Engine::loadBytecode(const uint8_t *data, size_t len, const std::string &name)
        {
            if (!m_ctx)
                return false;

            JSValue object = JS_ReadObject(m_ctx, data, len, JS_READ_OBJ_BYTECODE);
            if (JS_IsException(object))
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Bokken] Critical: Failed to parse bytecode for '%s'\n", name.c_str());
                reportException("ReadObject: " + name);
                return false;
            }

            if (JS_ResolveModule(m_ctx, object) != 0)
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Bokken] Critical: Failed to resolve module '%s'\n", name.c_str());
                reportException("ResolveModule: " + name);
                JS_FreeValue(m_ctx, object);
                return false;
            }

            m_lastModule = (JSModuleDef *)JS_VALUE_GET_PTR(object);

            JSValue result = JS_EvalFunction(m_ctx, object);
            if (JS_IsException(result))
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Bokken] Critical: Module Evaluation/Linking failed for '%s'\n", name.c_str());
                reportException("EvalFunction: " + name);
                JS_FreeValue(m_ctx, result);
                return false;
            }
            JS_FreeValue(m_ctx, result);

            extractLifecycle(name);
            return true;
        }

        void Engine::shutdown()
        {
            if (!m_rt)
                return;

            // We move the pointers to local variables and set the class members to null.
            // Now, any other thread or any lambda calling isReady() will get 'false' instantly.
            JSContext *ctx = m_ctx;
            JSRuntime *rt = m_rt;

            m_ctx = nullptr;
            m_rt = nullptr;

            if (ctx)
            {
                // Free cached hook references
                JS_FreeValue(ctx, m_fn_onStart);
                JS_FreeValue(ctx, m_fn_onUpdate);
                JS_FreeValue(ctx, m_fn_onFixedUpdate);

                m_fn_onStart = JS_UNDEFINED;
                m_fn_onUpdate = JS_UNDEFINED;
                m_fn_onFixedUpdate = JS_UNDEFINED;

                // Free any outstanding timer callbacks. setInterval
                // entries that the script never cleared would otherwise
                // leak their JSValue across runtime teardown.
                for (auto &t : m_timers)
                {
                    if (!t.cleared)
                        JS_FreeValue(ctx, t.callback);
                }
                m_timers.clear();

                // Same for any pending requestAnimationFrame callbacks
                // that never got a chance to fire (e.g. a shutdown
                // mid-frame between rAF registration and the next
                // tick).
                for (auto &r : m_rafCallbacks)
                {
                    if (!r.cleared)
                        JS_FreeValue(ctx, r.callback);
                }
                m_rafCallbacks.clear();

                // Let each module release its own retained JS handles while
                // the context is still alive. m_ctx was nulled above, so we
                // hand the live local ctx in explicitly.
                for (auto &mod : m_modules)
                {
                    mod->destroy(ctx);
                }

                m_modules.clear();

                JS_FreeContext(ctx);
            }

            if (rt)
            {
                Modules::Base::unregisterRuntime(rt);
                JS_FreeRuntime(rt);
            }

            // m_lastModule pointed into the runtime we just freed. The
            // hot-reload state hooks (saveHotReloadState / restoreHotReloadState)
            // and extractLifecycle guard only on it being non-null, so leaving
            // a dangling pointer here is a use-after-free the next time the
            // engine is re-initialised and one of those runs before a
            // successful loadBytecode reassigns it (e.g. a reload whose
            // bytecode read or module resolve fails). Clear it so "no module
            // loaded yet" is represented honestly.
            m_lastModule = nullptr;
        }

        // Game-loop hooks
        void Engine::callOnStart()
        {
            callCachedFn(m_fn_onStart, "onStart");
        }

        void Engine::callOnUpdate(double deltaTime)
        {
            callCachedFn(m_fn_onUpdate, "onUpdate", &deltaTime);
        }

        void Engine::callOnFixedUpdate(double deltaTime)
        {
            callCachedFn(m_fn_onFixedUpdate, "onFixedUpdate", &deltaTime);
        }

        std::string Engine::saveHotReloadState()
        {
            if (!m_ctx || !m_lastModule)
                return {};

            JSValue ns = JS_GetModuleNamespace(m_ctx, m_lastModule);
            if (JS_IsException(ns))
                return {};

            std::string saved;
            JSValue hook = JS_GetPropertyStr(m_ctx, ns, "onHotReloadSave");
            if (JS_IsFunction(m_ctx, hook))
            {
                JSValue result = JS_Call(m_ctx, hook, JS_UNDEFINED, 0, nullptr);
                if (JS_IsException(result))
                {
                    reportException("onHotReloadSave");
                }
                else if (!JS_IsUndefined(result) && !JS_IsNull(result))
                {
                    // Serialise the returned value to JSON so it survives
                    // the runtime teardown as a plain C++ string.
                    JSValue json =
                        JS_JSONStringify(m_ctx, result, JS_UNDEFINED, JS_UNDEFINED);
                    if (!JS_IsException(json))
                    {
                        const char *text = JS_ToCString(m_ctx, json);
                        if (text)
                        {
                            saved = text;
                            JS_FreeCString(m_ctx, text);
                        }
                    }
                    JS_FreeValue(m_ctx, json);
                }
                JS_FreeValue(m_ctx, result);
            }
            JS_FreeValue(m_ctx, hook);
            JS_FreeValue(m_ctx, ns);
            return saved;
        }

        void Engine::restoreHotReloadState(const std::string &state)
        {
            if (!m_ctx || !m_lastModule || state.empty())
                return;

            JSValue ns = JS_GetModuleNamespace(m_ctx, m_lastModule);
            if (JS_IsException(ns))
                return;

            JSValue hook = JS_GetPropertyStr(m_ctx, ns, "onHotReloadRestore");
            if (JS_IsFunction(m_ctx, hook))
            {
                JSValue parsed =
                    JS_ParseJSON(m_ctx, state.c_str(), state.size(), "<hot-reload-state>");
                if (JS_IsException(parsed))
                {
                    reportException("onHotReloadRestore (parse)");
                }
                else
                {
                    JSValue result =
                        JS_Call(m_ctx, hook, JS_UNDEFINED, 1, &parsed);
                    if (JS_IsException(result))
                        reportException("onHotReloadRestore");
                    JS_FreeValue(m_ctx, result);
                    drainJobQueue();
                }
                JS_FreeValue(m_ctx, parsed);
            }
            JS_FreeValue(m_ctx, hook);
            JS_FreeValue(m_ctx, ns);
        }

        // Private helpers
        void Engine::callCachedFn(JSValue fn, const std::string &hookName,
                                  double *arg)
        {
            if (!m_ctx || JS_IsUndefined(fn))
                return;

            JSValue result;
            if (arg)
            {
                JSValue jsArg = JS_NewFloat64(m_ctx, *arg);
                result = JS_Call(m_ctx, fn, JS_UNDEFINED, 1, &jsArg);
                JS_FreeValue(m_ctx, jsArg);
            }
            else
            {
                result = JS_Call(m_ctx, fn, JS_UNDEFINED, 0, nullptr);
            }

            if (JS_IsException(result))
            {
                reportException(hookName);
            }
            JS_FreeValue(m_ctx, result);

            // Always drain microtasks after a hook call so that async/await in user
            // scripts progresses without needing an explicit flush call.
            drainJobQueue();
        }

        void Engine::drainJobQueue()
        {
            JSContext *pctx = nullptr;
            int ret;
            while ((ret = JS_ExecutePendingJob(m_rt, &pctx)) > 0)
            {
                // Keep draining.
            }
            if (ret < 0 && pctx)
            {
                reportException("drainJobQueue");
            }
        }

        void Engine::reportException(const std::string &context)
        {
            if (!m_ctx)
                return;

            JSValue exc = JS_GetException(m_ctx);

            // 1. Get the primary message
            JSValue str = JS_ToString(m_ctx, exc);
            const char *msg = JS_ToCString(m_ctx, str);

            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Context: %s\n", context.c_str());
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Message: %s\n", msg ? msg : "<unknown>");

            JS_FreeCString(m_ctx, msg);
            JS_FreeValue(m_ctx, str);

            // 2. Extract deep metadata if it's an Error object
            if (JS_IsError(exc))
            {
                // Get File and Line info
                auto logExtra = [&](const char *prop, const char *label)
                {
                    JSValue val = JS_GetPropertyStr(m_ctx, exc, prop);
                    if (!JS_IsUndefined(val))
                    {
                        const char *cval = JS_ToCString(m_ctx, val);
                        if (cval)
                        {
                            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: %s\n", label, cval);
                            JS_FreeCString(m_ctx, cval);
                        }
                    }
                    JS_FreeValue(m_ctx, val);
                };

                logExtra("fileName", "File");
                logExtra("lineNumber", "Line");

                // Get Stack Trace
                JSValue stack = JS_GetPropertyStr(m_ctx, exc, "stack");
                if (!JS_IsUndefined(stack))
                {
                    const char *stackStr = JS_ToCString(m_ctx, stack);
                    if (stackStr)
                    {
                        std::string s = stackStr;
                        size_t pos = 0;
                        // CORRECTED: Use std::string::npos
                        while ((pos = s.find('\n', pos)) != std::string::npos)
                        {
                            s.replace(pos, 1, "\n    ");
                            pos += 5; // move past the newline and indentation
                        }
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Stack Trace:\n    %s\n", s.c_str());
                        JS_FreeCString(m_ctx, stackStr);
                    }
                }
                JS_FreeValue(m_ctx, stack);
            }

            JS_FreeValue(m_ctx, exc);
        }

        void Engine::extractLifecycle(const std::string &modulePath)
        {
            if (!m_lastModule)
                return;

            JSValue ns = JS_GetModuleNamespace(m_ctx, m_lastModule);
            if (JS_IsException(ns))
                return;

            auto syncHook = [&](const char *prop, JSValue &member)
            {
                JS_FreeValue(m_ctx, member);
                JSValue fn = JS_GetPropertyStr(m_ctx, ns, prop);
                if (JS_IsFunction(m_ctx, fn))
                    member = fn;
                else
                {
                    member = JS_UNDEFINED;
                    JS_FreeValue(m_ctx, fn);
                }
            };

            syncHook("onStart", m_fn_onStart);
            syncHook("onUpdate", m_fn_onUpdate);
            syncHook("onFixedUpdate", m_fn_onFixedUpdate);

            JS_FreeValue(m_ctx, ns);
        }

        /*
         *  TIMING + RAF + MICROTASK POLYFILLS
         *
         *
         * QuickJS deliberately ships only the language core; browser
         * and Node host APIs are the embedder's responsibility. We
         * polyfill the subset users typically reach for:
         *
         *   - setTimeout / setInterval / clearTimeout / clearInterval
         *   - requestAnimationFrame / cancelAnimationFrame
         *   - queueMicrotask
         *   - performance.now() (as Engine.now() in JS)
         *
         * None of these are installed on the JS global. They are
         * exported only through the bokken/engine module — users
         * write `import Engine from "bokken/engine"; Engine.setTimeout(…)`
         * (or destructure the names they want). Keeping host APIs out
         * of the global keeps the module surface honest and avoids
         * shadowing user code that defines names like `setTimeout`
         * for its own purposes.
         *
         * Implementation notes:
         *
         *   - Timers live in a flat vector keyed by absolute deadline.
         *     Firing each frame sweeps the vector for due entries,
         *     calls them, and either rolls the deadline forward (for
         *     setInterval) or marks the entry cleared (setTimeout).
         *
         *   - We don't bother with a heap because typical use has
         *     <50 active timers; linear scan is faster than the heap
         *     bookkeeping at that scale and predictably allocation-
         *     free per tick.
         *
         *   - clearTimeout / clearInterval flag the entry rather than
         *     erase it, so it's safe to clear a timer from inside its
         *     own callback. Cleared entries are dropped at the end of
         *     each tick.
         *
         *   - JSValue callbacks are duplicated on schedule and freed
         *     on either fire-and-forget (setTimeout) or clear /
         *     shutdown (setInterval). We never leak a callback — see
         *     shutdown() below for the bulk free.
         *
         *   - The fired callback is re-entrant safe via a
         *     fire-then-mutate pattern: we copy the to-fire entries
         *     out before calling, so timers added or cleared from
         *     within a callback don't trip the iteration. */

        int Engine::scheduleTimer(JSValue cb, double delaySeconds, double periodSeconds)
        {
            int id = m_nextTimerId++;
            m_timers.push_back(TimerEntry{
                id,
                m_clock + delaySeconds,
                periodSeconds,
                JS_DupValue(m_ctx, cb),
                false,
            });
            return id;
        }

        void Engine::clearTimer(int id)
        {
            for (auto &t : m_timers)
            {
                if (t.id == id && !t.cleared)
                {
                    t.cleared = true;
                    JS_FreeValue(m_ctx, t.callback);
                    t.callback = JS_UNDEFINED;
                    return;
                }
            }
        }

        void Engine::tickTimers(double deltaTime)
        {
            // Frame stats — these live on the variable (render) tick.
            // Done here rather than in fireAnimationFrames() so a build
            // that doesn't use RAF still has live frame stats.
            m_lastFrameDt = deltaTime;
            m_frameCount++;

            if (!m_ctx || m_timers.empty())
            {
                m_clock += deltaTime;
                return;
            }
            m_clock += deltaTime;

            // Snapshot which entries are due BEFORE firing, so callbacks
            // that schedule new timers don't get fired in the same tick
            // (that would risk runaway re-entry for a timer scheduled
            // with delay 0).
            //
            // We collect indices, not pointers, because the vector may
            // reallocate when a callback calls setTimeout itself.
            std::vector<int> toFireIds;
            toFireIds.reserve(m_timers.size());
            for (const auto &t : m_timers)
            {
                if (!t.cleared && t.deadline <= m_clock)
                    toFireIds.push_back(t.id);
            }

            for (int id : toFireIds)
            {
                // Re-look-up by id; the entry might have been cleared
                // by an earlier callback in this same tick.
                auto it = std::find_if(m_timers.begin(), m_timers.end(),
                                       [id](const TimerEntry &e)
                                       { return e.id == id; });
                if (it == m_timers.end() || it->cleared)
                    continue;

                JSValue cb = it->callback; // borrowed; freed below if one-shot
                bool isInterval = it->period > 0.0;

                JSValue result = JS_Call(m_ctx, cb, JS_UNDEFINED, 0, nullptr);
                if (JS_IsException(result))
                    reportException(isInterval ? "setInterval callback"
                                               : "setTimeout callback");
                JS_FreeValue(m_ctx, result);

                // Re-look-up again: the callback may have cleared its
                // own entry, or vector may have reallocated.
                it = std::find_if(m_timers.begin(), m_timers.end(),
                                  [id](const TimerEntry &e)
                                  { return e.id == id; });
                if (it == m_timers.end())
                    continue;

                if (it->cleared)
                    continue;

                if (isInterval)
                {
                    // Roll the deadline forward. If we overshot by more
                    // than one period (frame hitched), skip missed fires
                    // by snapping to the next future deadline rather
                    // than catching up — matches browser behaviour.
                    do
                    {
                        it->deadline += it->period;
                    } while (it->deadline <= m_clock);
                }
                else
                {
                    // setTimeout fires once and is gone.
                    it->cleared = true;
                    JS_FreeValue(m_ctx, it->callback);
                    it->callback = JS_UNDEFINED;
                }
            }

            // Compact away cleared entries so the vector doesn't grow
            // unbounded across long sessions.
            m_timers.erase(std::remove_if(m_timers.begin(), m_timers.end(),
                                          [](const TimerEntry &e)
                                          { return e.cleared; }),
                           m_timers.end());

            // Drain microtasks queued by timer callbacks. Without this,
            // a setTimeout that does `await something` would queue the
            // continuation but it would only run on the next hook fire.
            // Browsers run microtasks immediately after each task, and
            // we want the same behaviour.
            drainJobQueue();
        }

        // JS: setTimeout(callback, delayMs) / setInterval(callback, periodMs)
        // Returns the timer id (number) for use with clearTimeout/clearInterval.
        // magic == 0 → setTimeout, magic == 1 → setInterval.
        JSValue Engine::js_set_timer(JSContext *ctx, JSValueConst /*this_val*/,
                                     int argc, JSValueConst *argv, int magic)
        {
            if (argc < 1 || !JS_IsFunction(ctx, argv[0]))
                return JS_NewInt32(ctx, 0);

            // Delay in ms (default 0). Browsers clamp negative to 0; we do too.
            double delayMs = 0;
            if (argc >= 2)
                JS_ToFloat64(ctx, &delayMs, argv[1]);
            if (delayMs < 0)
                delayMs = 0;
            const double delaySec = delayMs * 0.001;

            const double periodSec = (magic == 1) ? delaySec : 0.0;
            int id = Instance().scheduleTimer(argv[0], delaySec, periodSec);
            return JS_NewInt32(ctx, id);
        }

        // JS: clearTimeout(id) / clearInterval(id) — both call here.
        JSValue Engine::js_clear_timer(JSContext *ctx, JSValueConst /*this_val*/,
                                       int argc, JSValueConst *argv)
        {
            if (argc < 1)
                return JS_UNDEFINED;
            int32_t id = 0;
            JS_ToInt32(ctx, &id, argv[0]);
            if (id > 0)
                Instance().clearTimer(id);
            return JS_UNDEFINED;
        }

        /*
         *  requestAnimationFrame / cancelAnimationFrame
         *
         *
         * The browser semantics that matter:
         *
         *   1. requestAnimationFrame returns an id, and the callback
         *      fires exactly once on the NEXT frame with a hi-res
         *      timestamp. Re-registering inside the callback (the
         *      common `function loop(t) { rAF(loop); … }` pattern)
         *      schedules another fire on the frame AFTER the current
         *      one, never the same frame.
         *
         *   2. cancelAnimationFrame is a no-op if the id has already
         *      fired or never existed.
         *
         *   3. The callback receives one argument: a millisecond
         *      timestamp. Browsers use `performance.now()`-style time
         *      since navigation start; we use time since engine init,
         *      which is the same `m_clock` field the timer subsystem
         *      uses.
         *
         * Implementation: we move the "due now" callbacks into a
         * local vector, swap the live list to empty BEFORE calling
         * any of them, then fire. New rAF registrations during firing
         * land in the now-empty live list and won't be fired this
         * frame. After firing we drain microtasks so async/await
         * inside an RAF callback progresses immediately. */

        int Engine::scheduleAnimationFrame(JSValue cb)
        {
            int id = m_nextRafId++;
            m_rafCallbacks.push_back(RafEntry{
                id,
                JS_DupValue(m_ctx, cb),
                false,
            });
            return id;
        }

        void Engine::cancelAnimationFrame(int id)
        {
            for (auto &r : m_rafCallbacks)
            {
                if (r.id == id && !r.cleared)
                {
                    r.cleared = true;
                    JS_FreeValue(m_ctx, r.callback);
                    r.callback = JS_UNDEFINED;
                    return;
                }
            }
        }

        void Engine::fireAnimationFrames()
        {
            if (!m_ctx || m_rafCallbacks.empty())
                return;

            // Swap out the pending list so callbacks scheduled during
            // firing don't fire this same frame. Any rAF call from
            // inside a fired callback lands in the new (empty) live
            // list and will be picked up next frame.
            std::vector<RafEntry> firing;
            firing.swap(m_rafCallbacks);

            // Browser-compatible argument: ms-since-start. The clock is
            // already in seconds; multiply for ms.
            JSValue tsArg = JS_NewFloat64(m_ctx, m_clock * 1000.0);

            for (auto &r : firing)
            {
                if (r.cleared)
                    continue; // cancelled before fire
                JSValue ret = JS_Call(m_ctx, r.callback, JS_UNDEFINED, 1, &tsArg);
                if (JS_IsException(ret))
                    reportException("requestAnimationFrame callback");
                JS_FreeValue(m_ctx, ret);
                JS_FreeValue(m_ctx, r.callback); // one-shot — always release
                r.callback = JS_UNDEFINED;
            }

            JS_FreeValue(m_ctx, tsArg);

            drainJobQueue();
        }

        // JS: requestAnimationFrame(cb) → id
        JSValue Engine::js_request_animation_frame(JSContext *ctx, JSValueConst /*this_val*/,
                                                   int argc, JSValueConst *argv)
        {
            if (argc < 1 || !JS_IsFunction(ctx, argv[0]))
                return JS_NewInt32(ctx, 0);
            int id = Instance().scheduleAnimationFrame(argv[0]);
            return JS_NewInt32(ctx, id);
        }

        // JS: cancelAnimationFrame(id)
        JSValue Engine::js_cancel_animation_frame(JSContext *ctx, JSValueConst /*this_val*/,
                                                  int argc, JSValueConst *argv)
        {
            if (argc < 1)
                return JS_UNDEFINED;
            int32_t id = 0;
            JS_ToInt32(ctx, &id, argv[0]);
            if (id > 0)
                Instance().cancelAnimationFrame(id);
            return JS_UNDEFINED;
        }

        /*
         *  queueMicrotask(fn)
         *
         *
         * Standard implementation for embedders that want to expose
         * the WHATWG queueMicrotask: chain the user function onto an
         * already-resolved Promise. The .then() schedules the
         * continuation on QuickJS's job queue, which the engine
         * drains after every hook (drainJobQueue), so the timing is
         * correct relative to other microtasks without us having to
         * maintain a separate FIFO.
         *
         * No id, no cancel — matches the WHATWG API. */
        JSValue Engine::js_queue_microtask(JSContext *ctx, JSValueConst /*this_val*/,
                                           int argc, JSValueConst *argv)
        {
            if (argc < 1 || !JS_IsFunction(ctx, argv[0]))
                return JS_UNDEFINED;

            // Build Promise.resolve().then(fn) entirely in C so we
            // don't depend on global Promise being unshadowed.
            JSValue global = JS_GetGlobalObject(ctx);
            JSValue Promise = JS_GetPropertyStr(ctx, global, "Promise");
            if (JS_IsObject(Promise))
            {
                JSValue resolveFn = JS_GetPropertyStr(ctx, Promise, "resolve");
                if (JS_IsFunction(ctx, resolveFn))
                {
                    JSValue resolved = JS_Call(ctx, resolveFn, Promise, 0, nullptr);
                    if (!JS_IsException(resolved))
                    {
                        JSValue thenFn = JS_GetPropertyStr(ctx, resolved, "then");
                        if (JS_IsFunction(ctx, thenFn))
                        {
                            JSValueConst arg = argv[0];
                            JSValue chained = JS_Call(ctx, thenFn, resolved, 1, &arg);
                            JS_FreeValue(ctx, chained);
                        }
                        JS_FreeValue(ctx, thenFn);
                    }
                    JS_FreeValue(ctx, resolved);
                }
                JS_FreeValue(ctx, resolveFn);
            }
            JS_FreeValue(ctx, Promise);
            JS_FreeValue(ctx, global);
            return JS_UNDEFINED;
        }

        /*
         *  performance.now()
         *
         *
         * Returns the engine clock (seconds since init) converted to
         * milliseconds. Same time base as the timer subsystem and as
         * the timestamp passed to requestAnimationFrame callbacks, so
         * deltas computed across these APIs are consistent.
         *
         * Browsers add sub-millisecond precision via the OS hi-res
         * clock; our resolution is bounded by the frame delta-time
         * (typically ~1/60s = ~16ms granularity). For most game-
         * script use (animation timing, profiling, debounce
         * thresholds) that's plenty. */
        JSValue Engine::js_performance_now(JSContext *ctx, JSValueConst /*this_val*/,
                                           int /*argc*/, JSValueConst * /*argv*/)
        {
            return JS_NewFloat64(ctx, Instance().m_clock * 1000.0);
        }
    } // namespace Scripting

} // namespace Bokken