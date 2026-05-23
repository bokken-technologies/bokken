#pragma once

#include "Base.hpp"

namespace Bokken
{
    namespace Scripting
    {
        namespace Modules
        {
            /**
             * `bokken/engine` — engine surface module.
             *
             * The C++ class is named EngineModule rather than Engine
             * because the runtime class Bokken::Scripting::Engine
             * already exists in the parent namespace. Inside
             * Bokken::Scripting::Modules, an unqualified `Engine`
             * resolves to the module class itself, so referencing the
             * runtime engine's static C trampolines would require
             * verbose ::Bokken::Scripting::Engine:: qualification on
             * every line. The JS-visible name (the import path) is
             * "bokken/engine" regardless of the C++ class name —
             * users write `import Engine from "bokken/engine"`.
             *
             * The default export holds everything: timing primitives
             * (which are NOT installed as JS globals), engine stats,
             * and engine-level utilities.
             *
             * Usage:
             *
             *   import Engine from "bokken/engine";
             *
             *   const id = Engine.setTimeout(() => { ... }, 100);
             *   Engine.clearTimeout(id);
             *   const handle = Engine.setInterval(() => { ... }, 1000);
             *   Engine.clearInterval(handle);
             *   Engine.requestAnimationFrame(t => { ... });
             *   Engine.cancelAnimationFrame(rafId);
             *   Engine.queueMicrotask(() => { ... });
             *
             *   Engine.version       // "1.0.0-alpha"
             *   Engine.frameCount    // monotonic frame counter (live)
             *   Engine.frameTime     // last frame's dt in seconds (live)
             *   Engine.elapsed       // total seconds since init (live)
             *   Engine.now()         // ms since init
            */
            class EngineModule : public Base
            {
            public:
                EngineModule() : Base("bokken/engine") {}

                int declare(JSContext *ctx, JSModuleDef *m) override;
                int init(JSContext *ctx, JSModuleDef *m) override;

            private:
                static JSValue js_engine_now(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv);

                /* Live property getters. JS_DefinePropertyGetSet calls
                 * these via the standard function-call path so they
                 * need the full 4-arg signature even though argc/argv
                 * are ignored. */
                static JSValue js_get_frame_count(JSContext *ctx, JSValueConst this_val,
                                                  int argc, JSValueConst *argv);
                static JSValue js_get_frame_time(JSContext *ctx, JSValueConst this_val,
                                                 int argc, JSValueConst *argv);
                static JSValue js_get_elapsed(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv);
            };
        }
    }
}