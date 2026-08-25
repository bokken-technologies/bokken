#pragma once

#include "../Stage.hpp"
#include "FullscreenPass.hpp"

#include "glad/gl.h"

namespace Bokken
{
    namespace Renderer
    {

        /**
         * The final stage. Reads the previous stage's output and writes
         * to its own outputTarget — but the *Renderer* then takes that
         * target and blits to the default framebuffer (the actual window).
         *
         * Could be just a passthrough, but in practice this is where you
         * dither, apply CRT scanlines, or stamp a debug HUD. Default
         * behaviour is a clean copy.
         */
        class CompositeStage : public Stage
        {
        public:
            explicit CompositeStage(std::string name = "composite")
                : Stage(std::move(name), Kind::Post) {}

            bool setup() override;
            void execute(const FrameContext &ctx) override;

        private:
            FullscreenPass m_pass;
        };

    }
}
