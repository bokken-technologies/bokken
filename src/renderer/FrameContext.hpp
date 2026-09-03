#pragma once

#include "GlyphCache.hpp"
#include "RenderTarget.hpp"
#include "SpriteBatcher.hpp"

namespace Bokken
{
    namespace Renderer
    {

        class Pipeline;

        /**
         * Per-frame context passed down the Pipeline.
         *
         * Stages read from `inputTarget` (the previous stage's output) and
         * write to `outputTarget` (their own scratch). The Pipeline rotates
         * these between stages so each stage's output becomes the next
         * stage's input.
         *
         * `viewportWidth/Height` are the framebuffer dimensions in physical
         * pixels (HiDPI-aware). Stages that draw screen-aligned content use
         * these to size their work.
         *
         * `dt` is the variable timestep in seconds; useful for time-based
         * post effects (animated noise, scanline scroll, etc).
         *
         * `batcher` is shared — stages that draw 2D content go through it.
         *
         * `pipeline` is a back-reference to the owning Pipeline. Stages
         * use it to reach auxiliary render targets (Pipeline::findAuxTarget)
         * that outlive a single ping-pong step — the canonical case being
         * the lighting pass reading the normal buffer the sprite stage
         * wrote several rotations earlier.
        */
        struct FrameContext
        {
            const RenderTarget *inputTarget = nullptr; // previous stage's output
            RenderTarget *outputTarget = nullptr;      // where this stage writes
            SpriteBatcher *batcher = nullptr;
            SpriteBatcher *uiBatcher = nullptr;
            int viewportWidth = 0;
            int viewportHeight = 0;
            float dt = 0.0f;
            Pipeline *pipeline = nullptr; // owning pipeline, for aux target lookups
        };

    }
}