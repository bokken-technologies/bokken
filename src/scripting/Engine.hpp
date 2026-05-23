#pragma once

#include "./modules/Base.hpp"
#include "../AssetPack.hpp"

#include <quickjs.h>

#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <cstdio>
#include <cstdint>
#include <cstring>

namespace Bokken
{
    namespace Scripting
    {

        /**
         * Owns and manages the QuickJS runtime and context for one game session.
         *
         * Responsibilities:
         *   - Create / configure the JSRuntime and JSContext from ProjectConfiguration values.
         *   - Hold all registered BokkenModules and install them before script evaluation.
         *   - Load compiled bytecode (.script files produced by BokkenPacker).
         *   - Expose callHook() so the EngineLoop can drive onStart / onUpdate / onFixedUpdate.
         *   - Drain the QuickJS job queue (Promise microtasks) after each hook invocation.
         *   - Provide a structured shutdown path that calls destroy() on every module.
        */
        class Engine
        {
        public:
            Engine() = default;
            ~Engine() { shutdown(); }

            static Engine &Instance()
            {
                static Engine instance;
                return instance;
            }

            // Setup (call in this order before the game loop starts)

            /**
             * Initialises the JSRuntime and JSContext.
             * @param maxHeapMb  Maximum heap in megabytes (from Engine::Runtime config).
             * @param stackKb    Stack size in kilobytes.
             * @param gcThreshKb GC trigger threshold in kilobytes.
             * @return true on success.
            */
            bool init(AssetPack *assets, int maxHeapMb = 128, int stackKb = 1024, int gcThreshKb = 512);

            /**
             * Register a native module. Must be called after init() and before loadBytecode().
             * Modules are installed in registration order.
            */
            void addModule(std::unique_ptr<Modules::Base> module);

            /**
             * Load and evaluate a compiled .script bytecode blob.
             * The module's exported onStart/onUpdate/onFixedUpdate functions are cached
             * from the module namespace after evaluation.
             * @param data  Raw bytecode bytes.
             * @param len   Byte count.
             * @param name  Logical name used in error messages (e.g. "scripts/index.script").
             * @return true if the bytecode was loaded and evaluated without error.
            */
            bool loadBytecode(const uint8_t *data, size_t len, const std::string &name);

            // Print and clear any pending JS exception.
            void reportException(const std::string &context);

            // Game-loop interface

            /** Call the JS onStart() export once at the start of the session. */
            void callOnStart();

            /** Call the JS onUpdate(deltaTime) export every frame. */
            void callOnUpdate(double deltaTime);

            /** Call the JS onFixedUpdate(deltaTime) export at the fixed timestep. */
            void callOnFixedUpdate(double deltaTime);

            /**
             * Capture script-defined state across a live reload.
             *
             * If the entry module exports an onHotReloadSave() function,
             * it is called and its return value serialised to a JSON string
             * which the caller holds across the engine teardown. Returns an
             * empty string if no such export exists or it returns nothing —
             * in which case reload simply restarts the script fresh. This is
             * the script author's opt-in channel for "keep this across a
             * reload" (player position, current scene, menu selection).
            */
            std::string saveHotReloadState();

            /**
             * Restore script-defined state after a live reload.
             *
             * If the freshly loaded entry module exports an
             * onHotReloadRestore(state) function and `state` is non-empty,
             * the JSON is parsed back into a value and passed to it. Called
             * after callOnStart() so the script has built its world before
             * the saved state is reapplied. A no-op when either the export
             * or the state is absent.
            */
            void restoreHotReloadState(const std::string &state);

            /**
             * Tick the JS timer queue (setTimeout / setInterval).
             *
             * QuickJS doesn't ship browser/Node timer APIs, so we
             * polyfill them in C++. Callbacks scheduled via the JS
             * setTimeout/setInterval globals are stored in a vector
             * keyed by deadline-in-seconds-from-engine-start; this
             * method advances the clock and fires every callback
             * whose deadline has passed.
             *
             * Call this once per frame from the main loop, before
             * onUpdate, so timers and the per-frame hook see a
             * consistent JS world state.
            */
            void tickTimers(double deltaTime);

            /**
             * Fire pending requestAnimationFrame callbacks.
             *
             * Each registered RAF callback is invoked exactly once
             * with the current high-resolution timestamp in ms (same
             * clock as performance.now()). Callbacks added via
             * requestAnimationFrame from within another RAF callback
             * are deferred to the next frame, matching browser
             * semantics — without this, a recursive RAF loop
             * (`function tick() { rAF(tick); ... }`) would spin in
             * the same tick instead of yielding once per frame.
             *
             * Call once per frame after tickTimers, before onUpdate.
            */
            void fireAnimationFrames();

            // Shutdown

            /** Free all JS values, call destroy() on modules, free context and runtime. */
            void shutdown();

            // Accessors (for modules that need to register additional JS objects)
            JSRuntime *runtime() const { return m_rt; }
            JSContext *context() const { return m_ctx; }

            /** Returns true if the engine has been successfully initialised. */
            bool isReady() const { return m_rt != nullptr && m_ctx != nullptr; }

            /**
             * Drain the QuickJS job queue (Promise microtasks etc).
             *
             * Public so event-handling code in modules (Canvas onClick,
             * Input key handlers) can drain microtasks immediately
             * after firing a JS callback. Without that, microtasks
             * queued by the handler don't fire until the next
             * lifecycle hook drains them — typically next frame —
             * which violates WHATWG (microtasks must drain at the
             * end of the current task before any other macrotask).
             */
            void drainJobQueue();

        private:
            JSRuntime *m_rt = nullptr;
            JSContext *m_ctx = nullptr;

            AssetPack *m_assets = nullptr; // Store the VFS reference for module loading

            std::vector<std::unique_ptr<Modules::Base>> m_modules;

            // Cached references to the JS hook functions.
            JSModuleDef *m_lastModule = nullptr;
            JSValue m_fn_onStart = JS_UNDEFINED;
            JSValue m_fn_onUpdate = JS_UNDEFINED;
            JSValue m_fn_onFixedUpdate = JS_UNDEFINED;

            // Stub module loader used during bytecode evaluation so that imports of
            // "bokken/*" don't error out while we're resolving the top-level module.
            static JSModuleDef *s_module_loader(JSContext *ctx, const char *name, void *opaque);

            // Helper: call a cached JS function with zero or one double argument.
            void callCachedFn(JSValue fn, const std::string &hookName,
                              double *arg = nullptr);

            // Extract the lifecycle hooks from the module namespace after evaluation.
            void extractLifecycle(const std::string &modulePath);

            // Timer polyfill (setTimeout / setInterval)
            //
            // Per-callback record. `deadline` is the absolute clock
            // at which the callback should fire next, measured in
            // seconds since engine start. For setInterval entries
            // `period` > 0 and the deadline rolls forward by `period`
            // after each fire; for setTimeout `period` is 0 and the
            // entry is removed after firing once.
            //
            // The callback JSValue is duplicated when the timer is
            // installed and freed when it's cleared / fires (one-shot)
            // / the engine shuts down. Cleared timers are flagged
            // rather than removed during iteration so we don't
            // invalidate the firing loop.
            struct TimerEntry {
                int      id;
                double   deadline;
                double   period;     // 0 for setTimeout, > 0 for setInterval
                JSValue  callback;
                bool     cleared;
            };
            std::vector<TimerEntry> m_timers;
            int    m_nextTimerId = 1;
            double m_clock       = 0.0;

            // Frame statistics — exposed via the bokken/engine JS
            // module so scripts can read engine wall-clock time
            // ("how long since startup"), the per-frame delta in
            // seconds (last frame's dt, useful for spot-debugging
            // hitches without hooking onUpdate), and a monotonic
            // frame counter (for "every N frames do X" patterns).
            //
            // Updated by tickTimers() — that's where the engine clock
            // is advanced anyway, so we keep all per-frame
            // bookkeeping in one place. fixed-step physics ticks
            // don't bump these; frame counting follows the variable
            // (render) tick.
            double   m_lastFrameDt = 0.0;
            uint64_t m_frameCount  = 0;

        public:
            // Engine stats accessors — used by the bokken/engine module.
            // Public so the C JS-binding statics can read them via
            // Engine::Instance().
            double clockSeconds() const { return m_clock; }
            double lastFrameDt() const  { return m_lastFrameDt; }
            uint64_t frameCount() const { return m_frameCount; }

            // JS-callable C trampolines
            //
            // These statics are the C function pointers we hand to
            // QuickJS for the polyfilled host APIs (timers, RAF,
            // microtask, performance). They're public so the
            // bokken/engine module can reference them. They are
            // ABI-stable C function pointers by design — exposing
            // them isn't a leak of internal state.

            // setTimeout / setInterval — magic 0 for one-shot, 1 for
            // repeating.
            static JSValue js_set_timer(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv, int magic);
            static JSValue js_clear_timer(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv);

            // requestAnimationFrame / cancelAnimationFrame.
            static JSValue js_request_animation_frame(JSContext *ctx, JSValueConst this_val,
                                                      int argc, JSValueConst *argv);
            static JSValue js_cancel_animation_frame(JSContext *ctx, JSValueConst this_val,
                                                     int argc, JSValueConst *argv);

            // queueMicrotask(fn) — chains onto Promise.resolve().then,
            // landing the callback in QuickJS's standard job queue
            // which we drain via drainJobQueue() after every hook.
            static JSValue js_queue_microtask(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv);

            // performance.now() — ms since engine init. Same clock as
            // the timer queue and the RAF timestamp argument.
            static JSValue js_performance_now(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv);

        private:

            int  scheduleTimer(JSValue cb, double delaySeconds, double periodSeconds);
            void clearTimer(int id);

            // requestAnimationFrame polyfill
            //
            // Browser-style RAF: scheduleAnimationFrame(cb) returns an
            // id; the callback fires exactly once on the next frame
            // tick with the current high-resolution timestamp (in ms).
            // cancelAnimationFrame(id) cancels a pending callback.
            //
            // We keep two vectors: m_rafCallbacks for callbacks added
            // this frame (waiting to fire next), and m_rafFiring used
            // as a swap buffer during fire so callbacks scheduled from
            // within an RAF callback run on the FOLLOWING frame, not
            // the same one — matches browser semantics and prevents
            // infinite recursion via `function loop() { rAF(loop) }`.
            struct RafEntry {
                int     id;
                JSValue callback;
                bool    cleared;
            };
            std::vector<RafEntry> m_rafCallbacks;
            int    m_nextRafId = 1;

            int  scheduleAnimationFrame(JSValue cb);
            void cancelAnimationFrame(int id);
        };
    } // namespace Scripting

} // namespace Bokken