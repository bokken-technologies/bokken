#pragma once

#include "Pipeline.hpp"
#include "SpriteBatcher.hpp"
#include "GlyphCache.hpp"
#include "TextureCache.hpp"
#include "../AssetPack.hpp"
#include "stages/SpriteStage.hpp"
#include "stages/UserInterfaceStage.hpp"
#include "stages/CompositeStage.hpp"

#include <glad/gl.h>
#include <SDL3/SDL.h>

#include <functional>
#include <memory>
#include <vector>
#include <cmath>

namespace Bokken
{
    namespace Renderer
    {

        /**
         * How the internal render resolution relates to the window size.
         *
         * - FollowWindow: render at the window's physical pixel size.
         *   The default-by-default; matches old engine behaviour. The
         *   scene reveals more or less content as the window grows or
         *   shrinks, and post-process effects (bloom radius, FXAA
         *   texel step, shadow softness) are tuned per-frame to the
         *   current resolution.
         *
         * - Fixed: render into an offscreen target of a fixed size
         *   (typically the size the game was designed at, e.g.
         *   800x600). The final composite letter/pillar-boxes that
         *   target into the window. The visible scene is identical at
         *   every window size; only the bars change. This is the
         *   Stardew/Celeste/Hollow-Knight model and is the default
         *   chosen at startup, sourced from the project's configured
         *   window.width / window.height.
         *
         * - FixedHeight: render at a fixed vertical resolution; the
         *   horizontal resolution tracks the window aspect ratio so
         *   the camera reveals more or less content sideways but
         *   never vertically. Useful for side-scrollers where the
         *   playfield's vertical extent is gameplay-critical.
         */
        enum class RenderSizePolicy
        {
            FollowWindow,
            Fixed,
            FixedHeight,
        };

        /**
         * The renderer.
         *
         * Owns:
         *   - the SDL_GLContext
         *   - the SpriteBatcher (shared, used by every 2D stage)
         *   - the GlyphCache (text rasterization → atlas)
         *   - the Pipeline (ordered stages with ping-pong targets)
         *
         * Lifecycle:
         *   Renderer r;
         *   r.init(window, assets);     // creates GL context + default pipeline
         *   loop:
         *     r.beginFrame();
         *     r.batcher().drawRect(...);
         *     r.endFrame(dt);            // executes pipeline + composites
         *   r.shutdown();
         *
         * The default pipeline configured by init() is just a SpriteStage
         * followed by a CompositeStage. Anything fancier (bloom, color
         * grading, CRT) is added by the user via the bokken/renderer JS API.
         *
         * Render resolution vs. window size
         *
         * The renderer separates the resolution it actually draws at
         * (render size, the size of the pipeline's offscreen targets)
         * from the window's physical pixel size. The two are equal
         * under RenderSizePolicy::FollowWindow but diverge under Fixed
         * / FixedHeight. The final composite blit handles the upscale
         * and (for Fixed) the letterbox. Everything else inside the
         * pipeline — sprite drawing, light tile binning, bloom radius,
         * shadow atlas, FXAA texel step — operates in render space and
         * stays consistent across window sizes.
         *
         * Game code that needs to draw in "screen" coordinates
         * (GameObject::present, Distortion2D, particle emitters)
         * should size itself off renderWidth() / renderHeight(), NOT
         * SDL_GetWindowSizeInPixels(). The two helpers windowToRender
         * / renderToWindow exist for clicks and other window-space
         * inputs that need to map into the scene.
         */
        class Base
        {
        public:
            Base() = default;
            ~Base();

            bool init(SDL_Window *window, class Bokken::AssetPack *assets);
            void shutdown();

            /** Begin a frame. Resizes pipeline targets if needed. */
            void beginFrame();

            /** Execute the pipeline, then composite the final output to screen. */
            void endFrame(float dt);

            SpriteBatcher &batcher() { return m_batcher; }
            SpriteBatcher &uiBatcher() { return m_uiBatcher; }
            GlyphCache &glyphs() { return m_glyphs; }
            TextureCache &textures() { return m_textures; }
            Pipeline &pipeline() { return m_pipeline; }
            SDL_Window *window() const { return m_window; }

            /** Logical (pre-DPI) window size — useful for OS-space UI layout. */
            int logicalWidth() const { return m_logicalW; }
            int logicalHeight() const { return m_logicalH; }
            /** Physical (post-DPI) framebuffer size — the actual window in GL pixels. */
            int physicalWidth() const { return m_physicalW; }
            int physicalHeight() const { return m_physicalH; }
            float dpiScale() const { return m_logicalW > 0 ? (float)m_physicalW / (float)m_logicalW : 1.0f; }

            /**
             * The render resolution. This is what the pipeline draws
             * into, what SpriteBatcher::begin sets up the projection
             * for, and what every "screen-space" computation in the
             * engine should reference. Distinct from the window's
             * physical pixel size whenever the policy is not
             * FollowWindow.
             */
            int renderWidth() const { return m_renderW; }
            int renderHeight() const { return m_renderH; }

            /** Physical FBO/GL-viewport pixel size the pipeline actually
             *  rasterises into. Equal to (renderWidth, renderHeight) under
             *  FollowWindow; scaled up by dpiScale() under Fixed / FixedHeight
             *  so the offscreen target isn't lower-resolution than the display
             *  it gets composited into — this is what keeps text and SDF edges
             *  crisp instead of upscale-blurred. Draw-call coordinates and
             *  Layout still operate in renderWidth()/renderHeight(); this is
             *  purely a GPU-side allocation/rasterisation detail. */
            int targetWidth() const { return m_targetW; }
            int targetHeight() const { return m_targetH; }

            RenderSizePolicy renderSizePolicy() const { return m_policy; }

            /**
             * Set the render-size policy. For Fixed and FixedHeight,
             * (width, height) seed the fixed dimensions. For
             * FollowWindow the dimensions are ignored — render size
             * tracks the window every frame.
             *
             * Safe to call after init(); the pipeline is resized on
             * the next beginFrame().
             *
             * Returns false only on obviously broken input (zero or
             * negative dimensions paired with a fixed policy).
             */
            bool setRenderSize(int width, int height,
                               RenderSizePolicy policy = RenderSizePolicy::Fixed);

            /**
             * Convert a point in the window's physical pixel space
             * (top-left origin, range [0..physicalW] × [0..physicalH])
             * into render space (top-left origin, range [0..renderW]
             * × [0..renderH]). Inverts the letterbox / pillarbox
             * applied at composite time.
             *
             * Points inside the letterbox bars produce coordinates
             * outside [0..renderW/H] — callers can clamp or
             * range-check as needed.
             */
            void windowToRender(float wx, float wy, float &rx, float &ry) const;

            /**
             * Inverse of windowToRender. Useful for placing native
             * overlays (cursors, OS pickers) at locations defined in
             * scene coordinates.
             */
            void renderToWindow(float rx, float ry, float &wx, float &wy) const;

            /**
             * The destination rect (in window physical pixels) used
             * by the final composite blit. Exposed so input code can
             * detect whether a window-space click landed in the
             * letterbox bars (and should be ignored) vs the scene.
             */
            void compositeDstRect(float &x, float &y, float &w, float &h) const;

            /**
             * Re-derive the current render dimensions from the
             * configured policy and the latest known window size.
             * Called automatically every beginFrame; exposed publicly
             * so that resize-dispatch code in the scripting layer can
             * refresh values mid-tick (before beginFrame) and let
             * onResize callbacks observe the new size in the same
             * frame they fire.
             */
            void syncRenderSize() { recomputeRenderSize(); }

            void updateSize();

            using RenderSizeListener = std::function<void(int width, int height)>;

            /**
             * Subscribe to render-size-changed notifications.
             *
             * Fires whenever the render dimensions actually change —
             * not on every window resize. Under
             * RenderSizePolicy::Fixed a window resize never triggers
             * a callback; under FollowWindow it does; under
             * FixedHeight it does when the aspect changes.
             *
             * The callback runs synchronously from inside the
             * renderer at the moment the change is committed —
             * specifically, from the end of beginFrame() once the
             * pipeline has been resized. Handlers should be cheap
             * and must not mutate render size from inside the
             * callback; that would re-enter the dispatch path.
             *
             * @returns An integer id usable with
             *          removeRenderSizeListener() to unregister.
             */
            int addRenderSizeListener(RenderSizeListener cb);

            /** Returns true if a listener with that id was removed. */
            bool removeRenderSizeListener(int id);

        private:
            SDL_Window *m_window = nullptr;
            SDL_GLContext m_glContext = nullptr;

            SpriteBatcher m_batcher;
            SpriteBatcher m_uiBatcher;
            GlyphCache m_glyphs;
            TextureCache m_textures;
            Pipeline m_pipeline;

            int m_logicalW = 0, m_logicalH = 0;
            int m_physicalW = 0, m_physicalH = 0;

            // Render-size state. m_renderW/H are the actual pipeline
            // dimensions used this frame; m_fixedW/H hold the
            // user-configured "design" size used by Fixed and
            // FixedHeight to derive m_renderW/H.
            RenderSizePolicy m_policy = RenderSizePolicy::FollowWindow;
            int m_renderW = 0, m_renderH = 0;
            int m_targetW = 0, m_targetH = 0;
            int m_fixedW = 0, m_fixedH = 0;

            void buildDefaultPipeline();

            // Re-derive m_renderW/H from policy + window size. Called
            // every beginFrame so window resizes propagate without
            // requiring a callback path through the event loop.
            void recomputeRenderSize();

            // Render-size observer state. fireRenderSizeChanged is
            // called from the end of beginFrame() once the pipeline
            // has been resized; it dedupes against the last-fired
            // values so the common case (window stationary under
            // Fixed policy) costs two int compares and returns.
            struct RenderSizeListenerEntry
            {
                int id;
                RenderSizeListener cb;
            };
            std::vector<RenderSizeListenerEntry> m_renderSizeListeners;
            int m_nextRenderSizeListenerId = 1;
            int m_lastFiredRenderW = -1;
            int m_lastFiredRenderH = -1;

            void fireRenderSizeChanged();
        };

    }
}