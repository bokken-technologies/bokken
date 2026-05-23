#pragma once

#include "Base.hpp"
#include "../../renderer/Base.hpp"
#include "../../renderer/Pipeline.hpp"
#include "../../renderer/stages/SpriteStage.hpp"
#include "../../renderer/stages/BloomStage.hpp"
#include "../../renderer/stages/ColorGradeStage.hpp"
#include "../../renderer/stages/CompositeStage.hpp"
#include "../../renderer/stages/DistortionStage.hpp"
#include "../../renderer/stages/LightingPass.hpp"
#include "../../renderer/stages/ShadowPass.hpp"

#include <SDL3/SDL.h>

#include <string>
#include <memory>
#include <cstring>
#include <vector>

namespace Bokken
{
    namespace Scripting
    {
        namespace Modules
        {

            /**
             * `bokken/renderer` — JS-facing pipeline + render-target surface.
             *
             * Pipeline configuration
             *
             *   pipeline.addStage(kindString, name, props?)
             *   pipeline.removeStage(name)
             *   pipeline.moveStage(name, index)
             *   pipeline.setEnabled(name, bool)
             *   pipeline.configure(name, props)
             *   pipeline.list()
             *
             * Built-in stage kinds: "sprite", "bloom", "color-grade",
             * "shadowmap", "lighting", "distortion", "composite".
             *
             * Render target
             *
             *   getRenderSize()                  → { width, height }
             *   getRenderMode()                  → "follow" | "fixed" | "fixedHeight"
             *   setRenderSize(width, height, mode)
             *
             *   onResize(cb) → id                fires when the render
             *                                    size (NOT the OS
             *                                    window) changes. The
             *                                    callback receives
             *                                    { width, height }
             *                                    in render pixels.
             *                                    Returns an integer id.
             *   offResize(id) → bool             unregister by id.
             *
             * "follow" makes the render size track the window
             * framebuffer, so the scene reveals more content as the
             * window grows. "fixed" pins the render
             * size at (w, h) and letterboxes; "fixedHeight" pins
             * height only and the camera reveals more or less content
             * sideways. The project's configured windowBase.width /
             * windowBase.height are applied as ("fixed", w, h) at
             * startup, so 800x600 in bokken.json means "design at
             * 800x600, letterbox everything else".
             *
             * Under the Fixed policy a window resize never triggers a
             * render-size change, so onResize callbacks correctly
             * stay silent.
             *
             * Example
             *
             *   import Renderer from "bokken/renderer";
             *   Renderer.pipeline.addStage("lighting", "lighting",
             *       { ambient: { r: 0.03, g: 0.03, b: 0.05 } });
             *   Renderer.pipeline.addStage("bloom", "bloom",
             *       { threshold: 0.7, intensity: 0.5 });
             *   Renderer.pipeline.moveStage("bloom", 2);
             *   Renderer.pipeline.configure("color-grade", { exposure: 1.2 });
             *
             *   const { width, height } = Renderer.getRenderSize();
             *   Renderer.onResize(({ width, height }) => layoutHUD(width, height));
             */
            class Renderer : public Base
            {
            public:
                Renderer(Bokken::Renderer::Base *renderer, Bokken::AssetPack *assets = nullptr)
                    : Base("bokken/renderer")
                {
                    s_renderer = renderer;
                    s_assets = assets;

                    // Subscribe to the renderer's render-size observer
                    // immediately. The constructor runs in Bokken.cpp
                    // after the renderer is up, so the subscription
                    // is safe here. JS handlers registered later via
                    // onResize() are fanned out by
                    // onRendererRenderSizeChanged().
                    if (s_renderer && s_rendererSubId == -1)
                    {
                        s_rendererSubId = s_renderer->addRenderSizeListener(
                            &Renderer::onRendererRenderSizeChanged);
                    }
                }

                int declare(JSContext *ctx, JSModuleDef *m) override;
                int init(JSContext *ctx, JSModuleDef *m) override;

                /* Unsubscribe and drop JS bookkeeping. Called from
                 * Loop::shutdown BEFORE the renderer is destroyed —
                 * removeRenderSizeListener needs a live renderer to
                 * unhook from. Idempotent. */
                static void detach();

                /* Per-module teardown hook, called by Engine::shutdown() on
                 * every reload AND on final quit while the JSContext is still
                 * alive. Frees every retained onResize-callback JSValue and
                 * clears the listener list. Unlike detach() (which touches the
                 * live renderer's observer), this only releases JS handles, so
                 * it is the half that must run before the runtime is freed.
                 * Idempotent. */
                void destroy(JSContext *ctx) override;

                /* Access for other native code that needs the raw
                 * renderer pointer (Camera2D uses this for screen ↔
                 * world conversions). Returns nullptr before the
                 * Renderer module instance is constructed; callers
                 * must null-check. */
                static Bokken::Renderer::Base *renderer() { return s_renderer; }

            private:
                static inline Bokken::Renderer::Base *s_renderer = nullptr;
                static inline Bokken::AssetPack *s_assets = nullptr;

                /* Native subscription id on Renderer::Base. -1 when
                 * not subscribed. Lets detach() unhook cleanly. */
                static inline int s_rendererSubId = -1;

                /* Registered onResize listeners. Each holds the JSContext
                 * the callback was registered from; the JSValue is the
                 * callback itself. Stored in a vector so dispatch is a
                 * forward iterate; the int id is used for removal. */
                struct Listener
                {
                    int id;
                    JSContext *ctx;
                    JSValue fn;
                };
                static inline std::vector<Listener> s_listeners;
                static inline int s_nextListenerId = 1;

                /* Single native handler installed on the renderer by
                 * attach(). Fans out to every registered JS listener. */
                static void onRendererRenderSizeChanged(int w, int h);

                // Pipeline functions.
                static JSValue js_pipeline_add_stage(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_pipeline_remove_stage(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_pipeline_move_stage(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_pipeline_set_enabled(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_pipeline_configure(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_pipeline_list(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

                // Texture functions.
                static JSValue js_load_texture(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_define_region(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_define_grid(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

                // Distortion functions.
                static JSValue js_add_shockwave(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_clear_shockwaves(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

                // Render-target functions. Moved here from the Window
                // module because they describe the pipeline output,
                // not the OS window.
                static JSValue js_get_render_size(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_get_render_mode(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_set_render_size(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_on_resize(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_off_resize(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
            };

        }
    }
}