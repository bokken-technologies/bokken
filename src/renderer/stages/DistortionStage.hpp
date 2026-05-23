#pragma once

#include "../Stage.hpp"
#include "../RenderTarget.hpp"
#include "FullscreenPass.hpp"

#include "glad/gl.h"

#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Bokken
{
    namespace Renderer
    {

        /**
         * Screen-space UV displacement post-process.
         *
         * Reads the previous stage's output and displaces each fragment's
         * UV lookup by a vector sampled from a displacement buffer. This
         * single mechanism handles shockwaves, heat haze, underwater
         * wobble, rain-on-glass, portal distortion — anything that warps
         * the image without changing geometry.
         *
         * The displacement buffer is an RG16F render target where:
         *   R = horizontal UV offset (positive = shift right)
         *   G = vertical UV offset (positive = shift down)
         *   Both in normalised [−1, 1] range, scaled by `intensity`.
         *
         * Built-in effects:
         *
         *   Shockwave — call addShockwave() with a screen-space origin
         *   (normalised 0..1) and the stage draws an expanding ring into
         *   the displacement buffer each frame. Multiple shockwaves can
         *   overlap; they're additive.
         *
         *   Heat haze — enable heatHaze and the stage adds a scrolling
         *   sinusoidal displacement. Good for desert scenes or above
         *   fire emitters.
         *
         * The stage runs at full resolution — distortion at half-res
         * produces visible stairstepping on sharp edges.
         *
         * Pipeline placement: after the scene stage, before bloom and
         * color grading. Distortion on the raw HDR image gives the most
         * natural results.
         *
         * JS example:
         *   Renderer.pipeline.addStage("distortion", "distortion",
         *       { intensity: 0.05 });
         *   Renderer.pipeline.moveStage("distortion", 1);
        */
        class DistortionStage : public Stage
        {
        public:
            explicit DistortionStage(std::string name = "distortion")
                : Stage(std::move(name), Kind::Post) {}

            // Global intensity multiplier for all displacement. 0 disables
            // the effect entirely (early-out, zero cost). Range is
            // unclamped — values above 1.0 give extreme warping.
            float intensity = 0.05f;

            // Heat haze — animated sinusoidal displacement.
            bool heatHaze = false;
            float heatHazeSpeed = 1.5f;      // scroll speed in UV/sec
            float heatHazeFrequency = 40.0f;  // wave count across the screen
            float heatHazeAmplitude = 0.003f; // maximum UV offset per wave

            /**
             * Describes a single radial shockwave expanding from a point.
             *
             * Screen-space origin is in normalised coordinates: (0,0) is
             * top-left, (1,1) is bottom-right. The wave expands outward at
             * `speed` units per second, with the visible distortion band
             * between (radius - thickness/2) and (radius + thickness/2).
             *
             * When radius exceeds maxRadius, the shockwave is removed
             * automatically.
            */
            struct Shockwave
            {
                float originX = 0.5f;     // normalised screen X
                float originY = 0.5f;     // normalised screen Y
                float radius = 0.0f;      // current radius (grows each frame)
                float maxRadius = 1.5f;   // auto-remove threshold
                float speed = 0.8f;       // expansion rate in UV/sec
                float thickness = 0.1f;   // width of the distortion band
                float amplitude = 0.06f;  // peak displacement strength
            };

            /**
             * Spawn a shockwave at the given screen-space position.
             * Multiple shockwaves can be active simultaneously.
            */
            void addShockwave(float screenX, float screenY,
                              float speed = 0.8f,
                              float thickness = 0.1f,
                              float amplitude = 0.06f,
                              float maxRadius = 1.5f);

            /** Remove all active shockwaves. */
            void clearShockwaves() { m_shockwaves.clear(); }

            /** Number of active shockwaves (for debug HUD). */
            int shockwaveCount() const { return static_cast<int>(m_shockwaves.size()); }

            bool setup() override;
            void onResize(int w, int h) override;
            void execute(const FrameContext &ctx) override;

        private:
            FullscreenPass m_displacementPass;
            FullscreenPass m_applyPass;
            RenderTarget m_displacementBuffer;
            int m_width = 0, m_height = 0;

            float m_time = 0.0f;

            std::vector<Shockwave> m_shockwaves;

            // Maximum shockwaves the shader can handle in a single pass.
            // Kept modest — the uniform array size is baked into the GLSL.
            static constexpr int k_maxShockwaves = 16;
        };

    }
}