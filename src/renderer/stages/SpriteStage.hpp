#pragma once

#include "../Stage.hpp"

#include "glad/gl.h"

namespace Bokken
{
    namespace Renderer
    {

        /**
         * Draws all queued 2D content into its output target.
         *
         * This is the default scene stage for 2D games. It expects the
         * SpriteBatcher to have been populated *before* the pipeline runs
         * (i.e. user code calls `renderer.batcher().drawRect(...)` between
         * beginFrame() and endFrame()).
         *
         * Clears the output target to a configurable clear color, then
         * flushes the batcher.
         *
         * Deferred-lighting MRT
         *
         * When lighting is in use, the SpriteStage writes three render
         * targets in a single pass via MRT:
         *
         *   attachment 0 — albedo, the output target's owned color (RGBA16F)
         *   attachment 1 — tangent-space normals (RG16F aux target "normals")
         *   attachment 2 — color emissive (RGBA8 aux target "emissive"; the
         *                  RGB channels hold the emitted color, alpha is
         *                  the sprite's alpha so emissive at the silhouette
         *                  edge fades like the rest of the sprite)
         *
         * The stage requests these aux targets from the Pipeline on setup,
         * re-attaches them to the output target each frame (Pipeline
         * resize may have replaced the underlying GL texture), and sets
         * glDrawBuffers(3). The shader unconditionally writes all three
         * outputs; when lighting is disabled and only attachment 0 is
         * bound, the extra outputs are discarded by the driver. Set
         * `lightingEnabled = false` to skip the attach+drawbuffers work
         * entirely on a frame-by-frame basis.
        */
        class SpriteStage : public Stage
        {
        public:
            explicit SpriteStage(std::string name = "scene")
                : Stage(std::move(name), Kind::Scene) {}

            // Clear color — applied to the output target (albedo) before drawing.
            // Use 0 alpha if you want the next stage to see "no scene drawn here".
            float clearR = 0.075f, clearG = 0.090f, clearB = 0.105f, clearA = 1.0f;

            // When true, the stage writes albedo + normals + emissive as MRT.
            // When false, only albedo is written and aux targets are not
            // touched — useful for cheap fallback when the lighting pass
            // is disabled or the platform doesn't support MRT.
            bool lightingEnabled = true;

            bool setup() override;
            void execute(const FrameContext &ctx) override;

        private:
            // Aux target pointers cached from the Pipeline at setup time.
            // The Pipeline owns these and they remain valid for its
            // lifetime; SpriteStage just borrows them every frame to
            // attach as MRT slots on the output target.
            class RenderTarget *m_normalsAux = nullptr;
            class RenderTarget *m_emissiveAux = nullptr;
        };

    }
}