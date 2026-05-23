#pragma once

#include "../Stage.hpp"
#include "../RenderTarget.hpp"
#include "FullscreenPass.hpp"

#include "glad/gl.h"

namespace Bokken
{
    namespace Renderer
    {

        /**
         * Classic 2D bloom.
         *
         *   1. Bright-pass (threshold) extracts pixels above `threshold`
         *      into a half-resolution scratch.
         *   2. Two-pass separable Gaussian blur (horizontal then vertical).
         *   3. Composite: outputTarget = inputTarget + blurredBright * intensity.
         *
         * Uses two persistent half-res scratch targets for the bright extract
         * and the H-blur intermediate. They live for the stage's lifetime
         * and resize with the viewport.
         *
         * For real "AAA" bloom you'd want a multi-tap downsample pyramid
         * (Call-of-Duty-style); this single-mip Gaussian is good enough to
         * make scenes glow without the cost of seven downsamples per frame.
        */
        class BloomStage : public Stage
        {
        public:
            explicit BloomStage(std::string name = "bloom")
                : Stage(std::move(name), Kind::Post) {}

            // Tunables (live-editable).
            float threshold = 0.9f; // luma above this contributes to bloom
            float intensity = 0.6f; // bloom strength when added back
            float radius = 1.0f;    // blur kernel scale

            bool setup() override;
            void onResize(int w, int h) override;
            void execute(const FrameContext &ctx) override;

        private:
            FullscreenPass m_pass;
            RenderTarget m_bright;
            RenderTarget m_blurH;

            int m_halfW = 0, m_halfH = 0;
        };

    }
}
