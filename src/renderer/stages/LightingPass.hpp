#pragma once

#include "../Stage.hpp"
#include "FullscreenPass.hpp"

#include "glad/gl.h"
#include <glm/glm.hpp>

namespace Bokken
{
    namespace Renderer
    {

        /**
         * Accumulates per-light contributions into a lit HDR image.
         *
         * Inputs:
         *   - ctx.inputTarget          — albedo (RGBA16F) from SpriteStage
         *   - aux target "normals"     — tangent-space normals (RG16F)
         *   - aux target "emissive"    — self-emitted color (RGBA8)
         *   - aux target "shadowAtlas" — 1D shadowmap per shadow-casting
         *                                light (R16F, from ShadowmapPass)
         *                                — optional, lighting falls back
         *                                to "no shadows" when absent
         *   - Pipeline::lighting()     — shared lights + segments state
         *
         * Output:
         *   - ctx.outputTarget         — albedo * (ambient + light) + emissive
         *
         * Algorithm
         *
         * The CPU-side per-frame work (snapshot every Light2D, walk every
         * ShadowCaster2D, assign shadow slots, upload to GPU) lives in
         * the pipeline's shared LightingFrame. The lighting pass calls
         * Pipeline::lighting().gatherIfNeeded() at the top of execute(),
         * which is idempotent within a frame — when the ShadowmapPass
         * also runs and gathers first, this call is a no-op.
         *
         * The lighting fragment shader then samples albedo, normal,
         * emissive, the light data texture, and (optionally) the shadow
         * atlas, iterating u_lightCount lights and accumulating
         * per-light contributions including PCF-sampled shadow
         * attenuation for lights with a valid shadowSlot.
         *
         * What's NOT here yet (lands in later steps):
         *   - Tiled culling: every pixel iterates every uploaded light.
         *     At 256 lights and 1080p this is ~530M evaluations per
         *     frame, fine on desktop GPUs, borderline on Steam Deck.
         *     Tiling lands in Step 11.
         *   - Cookie sampling: cookieSlot is read from the GPU struct
         *     but the cookie atlas isn't created yet (Step 12).
         *
         * Tunables
         *
         * `ambient` is a constant added before light accumulation —
         * scenes with no lights still render the albedo at ambient
         * brightness, which is almost always wanted for development.
         * Set to (0,0,0) for a hard "lights are everything" look.
         *
         * `intensityScale` is a global multiplier on every light's
         * intensity, useful for "darken everything" cutscene fades or
         * for tone-mapping the whole lighting budget down without
         * touching individual lights.
        */
        class LightingPass : public Stage
        {
        public:
            explicit LightingPass(std::string name = "lighting")
                : Stage(std::move(name), Kind::Post) {}

            // Global ambient term. Added to the accumulated per-light
            // sum before the albedo multiply, so albedo*ambient is the
            // "no light reaches this pixel" contribution. Values can
            // exceed 1.0 (HDR) for overbright scenes.
            glm::vec3 ambient{0.05f, 0.05f, 0.06f};

            // Global multiplier applied to every light's intensity.
            // Scripts use this for cutscene fades / global dimming
            // without having to walk every Light2D and modulate
            // individually.
            float intensityScale = 1.0f;

            // Wrap-lighting amount, in [0, 1]. 0.0 = pure Lambertian
            // N·L (very contrasty on 2D sprites that lack authored
            // normal maps — flat sprites become invisible when no
            // light faces them). 0.5 = half-cosine wrap, the
            // "atmospheric 2D" default favoured by games like
            // Children of Morta and Ori. 1.0 = omnidirectional, only
            // distance and cone shape the lighting falloff and the
            // normal map's effect collapses to nothing.
            float wrapAmount = 0.5f;

            bool setup() override;
            void execute(const FrameContext &ctx) override;

        private:
            FullscreenPass m_pass;
        };

    }
}