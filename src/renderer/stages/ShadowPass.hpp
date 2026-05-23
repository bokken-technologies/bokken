#pragma once

#include "../Stage.hpp"
#include "../Shader.hpp"

#include "glad/gl.h"

namespace Bokken
{
    namespace Renderer
    {

        /**
         * Rasterizes shadow occluder segments into the per-light 1D
         * shadowmap atlas owned by the pipeline's LightingFrame.
         *
         * Position in the pipeline
         *
         * Install this stage BEFORE LightingPass and AFTER SpriteStage
         * in the pipeline ordering, e.g.:
         *
         *   pipeline.addStage("sprite",     "scene");
         *   pipeline.addStage("shadowmap",  "shadowmap");
         *   pipeline.addStage("lighting",   "lighting");
         *   pipeline.addStage("composite",  "composite");
         *
         * The shadowmap pass does not read inputTarget or write to
         * outputTarget — it writes only to the LightingFrame's shadow
         * atlas, which the LightingPass samples a stage later. The
         * pipeline's normal ping-pong rotation continues unaffected;
         * this stage is effectively "transparent" to the pipeline's
         * input/output flow but lives in the stage list so the
         * pipeline drives its execute() each frame.
         *
         * Algorithm
         *
         * Triggers the pipeline's LightingFrame to gather (idempotent —
         * does nothing if LightingPass already gathered this frame).
         * Then issues a single instanced draw of
         * (shadowCount * segmentCount) instances. Each instance reads
         * one (light, segment) pair from the two data textures and
         * rasterizes the segment's angular range at that light's
         * atlas row, writing the per-fragment ray-segment-intersection
         * distance with GL_MIN blending so the nearest occluder wins.
         *
         * For lights with castsShadows=false, no atlas row is assigned
         * and they contribute no instances. The lighting shader's
         * shadow sample short-circuits to "fully lit" for those lights
         * via the LIGHT_NO_SLOT sentinel in their shadowSlot field.
         *
         * Known limitations
         *
         *  - Segments that span the +X axis as seen from a light may
         *    lose a sliver of their shadow contribution at the wrap
         *    line. Rare in practice; can be addressed by doubling
         *    instance count to render both halves of wrapping
         *    segments if it ever becomes visible.
         *
         *  - No PCF: the lighting shader's shadow sample is a single
         *    NEAREST lookup. Step 10 widens to a 5-tap kernel.
        */
        class ShadowPass : public Stage
        {
        public:
            explicit ShadowPass(std::string name = "shadow")
                : Stage(std::move(name), Kind::Side) {}

            ~ShadowPass();

            bool setup() override;
            void execute(const FrameContext &ctx) override;

        private:
            Shader m_shader;

            // Empty VAO. The instanced draw uses gl_VertexID +
            // gl_InstanceID and reads everything from textures, so
            // there's no vertex buffer. GL 3.3 Core still requires a
            // bound VAO for any draw — this is the smallest possible
            // one.
            GLuint m_emptyVAO = 0;
        };

    }
}