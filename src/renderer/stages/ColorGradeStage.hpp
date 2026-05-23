#pragma once

#include "../Stage.hpp"
#include "FullscreenPass.hpp"

#include "glad/gl.h"

namespace Bokken
{
    namespace Renderer
    {

        /**
         * Tone mapping + exposure + saturation + simple lift/gamma/gain grading.
         *
         * This is the single most effective "make my game look professional"
         * stage. Even with default values it pulls HDR highlights into a
         * pleasing range and keeps shadows clean.
         *
         * Shader uses a filmic curve (Hable/Uncharted-2 style) — battle-tested
         * across countless shipped titles and cheap to evaluate.
        */
        class ColorGradeStage : public Stage
        {
        public:
            explicit ColorGradeStage(std::string name = "color-grade")
                : Stage(std::move(name), Kind::Post) {}

            // Tunables
            float exposure = 1.0f;
            float saturation = 1.0f;
            float gamma = 2.2f; // sRGB-ish; lower = brighter midtones

            bool setup() override;
            void execute(const FrameContext &ctx) override;

        private:
            FullscreenPass m_pass;
        };

    }
}
