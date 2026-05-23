#pragma once

#include "Component.hpp"
#include "Transform2D.hpp"
#include "Base.hpp"
#include "../renderer/lighting/Light.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Bokken
{
    namespace GameObject
    {

        /**
         * Light type, exposed at the component level. Maps 1:1 onto
         * the GPU-side LIGHT_TYPE_* constants but stays in its own
         * enum so script bindings can use a strongly-typed API rather
         * than raw integers.
        */
        enum class LightType : uint8_t
        {
            Point       = 0,
            Spot        = 1,
            Directional = 2,
        };

        /**
         * Animation envelope applied to a light's intensity each frame.
         * The envelope multiplies the static `intensity` field so the
         * authored intensity is the peak (or mean, for Pulse) brightness
         * the artist sees in the editor.
         *
         * Envelope evaluation lives in Light2D::update; envelope-specific
         * math (noise, sine, square wave) is wired in a later step but
         * the field shape is locked here so the GPU upload path doesn't
         * churn.
        */
        enum class LightEnvelope : uint8_t
        {
            Constant, // no modulation
            Flicker,  // 1D Perlin / value noise, candle / torch
            Pulse,    // sinusoidal, magical orbs / heartbeat
            Strobe,   // square wave, alarms / emergency lights
            Custom,   // script writes intensityModulator each frame
        };

        /**
         * A scene light. Attach alongside a Transform2D to control the
         * light's world-space position; the renderer pulls position
         * from Transform2D each frame.
         *
         * Lights register themselves in a global list on attach and
         * deregister on destroy. The lighting pass walks this list once
         * per frame, packs each enabled light into the GPU upload
         * buffer, and renders accumulated contributions.
         *
         * Point lights radiate uniformly in all directions, falling off
         * with distance from `range`. Spot lights cone-restrict using
         * `direction`, `innerConeAngle`, and `outerConeAngle`.
         * Directional lights apply uniformly across the visible scene
         * regardless of position — useful for sunlight / moonlight /
         * global ambient direction.
         *
         * @example
         *   const torch = new GameObject("Torch")
         *       .addComponent(Transform2D, { positionX: 200, positionY: 400 })
         *       .addComponent(Light2D, {
         *           type: LightType.Point,
         *           color: { r: 1.0, g: 0.6, b: 0.2 },
         *           intensity: 2.5,
         *           range: 180,
         *           falloff: 2.0,
         *           castsShadows: true,
         *           envelope: LightEnvelope.Flicker,
         *           envelopeAmplitude: 0.15,
         *           envelopeFrequency: 4.0,
         *       });
        */
        class Light2D : public Component
        {
        public:
            LightType type = LightType::Point;

            // Color is linear RGB. Values >1 are valid and feed the HDR
            // accumulation buffer.
            glm::vec3 color{1.0f, 1.0f, 1.0f};

            // Scalar multiplier on color. Animation envelopes modulate
            // this each frame without changing the authored value;
            // scripts that want to fade a light should write
            // `light.intensity = newValue` directly.
            float intensity = 1.0f;

            // Range in pixels at which falloff reaches zero. Ignored
            // for directional lights.
            float range = 256.0f;

            // Falloff exponent. 1.0 = linear, 2.0 = quadratic (physically
            // accurate for inverse-square law). Higher values produce
            // a sharper falloff edge.
            float falloff = 2.0f;

            // Spot cone half-angles in degrees. Inside the inner cone
            // intensity is full; between inner and outer the intensity
            // smoothsteps to zero. Outer must be >= inner.
            float innerConeAngle = 25.0f;
            float outerConeAngle = 35.0f;

            // Spot / directional direction, expressed as a 2D angle in
            // degrees (0 = +X, 90 = +Y). For directional lights this is
            // the direction light is travelling, not where the light is
            // "looking" — same convention as glm::vec light direction
            // and matching the dot(normal, -direction) lighting math.
            float directionDegrees = 90.0f;

            // Whether this light writes to the per-light shadowmap atlas
            // and is sampled with PCF in the lighting pass. Shadow slots
            // are allocated each frame by the ShadowmapPass; lights
            // beyond the slot cap render unshadowed for that frame.
            bool castsShadows = false;

            // PCF kernel radius multiplier applied to this light's
            // shadow sampling. 1.0 = the renderer's default kernel
            // size; larger values soften the shadow edge (good for
            // diffuse sources like overcast sun or a candle through
            // frosted glass), smaller values sharpen it (good for
            // a focused flashlight or laser).
            //
            // Per-light rather than per-caster because the shadow
            // atlas stores only the nearest occluder distance with no
            // record of which ShadowCaster2D produced it. The
            // ShadowCaster2D::softness field is currently inert and
            // documented as "reserved for future per-caster softness
            // via a secondary atlas channel". For Step 10 the
            // per-light softness covers the common case: an artist
            // tunes the softness on each light source to match the
            // scene's intended look.
            //
            // Range: nominally [0, ~5]. Values near 0 produce a
            // single-tap effectively-hard shadow; values above 5
            // smear the shadow into the surrounding lit area, useful
            // for "moonlight through fog" effects but quickly past
            // physical plausibility.
            float shadowSoftness = 1.0f;

            // Animation envelope.
            LightEnvelope envelope = LightEnvelope::Constant;
            float envelopeAmplitude = 0.0f; // 0..1 fraction of intensity
            float envelopeFrequency = 1.0f; // Hz
            float envelopePhase = 0.0f;     // seconds offset

            // The envelope writes here each frame; the lighting pass
            // reads it. Custom envelopes are driven by scripts writing
            // this field directly. Resets to 1.0 at every onAttach so
            // a freshly-created light starts at full authored intensity.
            float intensityModulator = 1.0f;

            // Optional cookie / gobo texture sampled by the lighting
            // shader to mask the light's contribution. Empty string =
            // no cookie. Setting / changing this resolves a cookie
            // atlas slot on next frame; cookie atlas management is
            // owned by the lighting pass and is opaque to this component.
            std::string cookiePath;

            // UV transform applied to the cookie sample. Scrolling
            // cookies update offset each frame from a script; tiling
            // cookies use scale > 1.
            glm::vec2 cookieUVOffset{0.0f};
            glm::vec2 cookieUVScale{1.0f};

            void onAttach() override;
            void update(float dt) override;
            void onDestroy() override;

            // Pure data + script-controlled — always considered active
            // by the destroy-when-idle system. Lights are explicit;
            // killing them is an explicit GameObject::destroy call.
            bool isIdle() const override { return false; }

            /**
             * Snapshot this component into the GPU-side Light struct.
             * Resolves world position from the sibling Transform2D
             * (zero-vector if absent), applies the current intensity
             * modulator, and packs flags. Called by the lighting pass
             * once per enabled light per frame.
             *
             * The renderer-owned shadowSlot/cookieSlot are NOT written
             * here — they are assigned by the lighting / shadow passes
             * which know which slots are free this frame. This method
             * leaves both as LIGHT_NO_SLOT.
             *
             * The world→screen transform converts the light's world-
             * space position and range into the render-target pixel
             * coordinates the lighting subsystem operates in. Without
             * this conversion the shader's `delta = L.position -
             * fragPosPx` math would mix units (world × pixel) and
             * lights would land in nonsense locations as soon as
             * pixelsPerUnit ≠ 1 or the camera moves off origin.
            */
            void snapshot(Renderer::Lighting::Light &out,
                          const Renderer::Lighting::WorldToScreen &w2s) const;

            /**
             * Reset the envelope's internal phase clock to zero. Useful
             * when an artist wants the envelope to start fresh at a
             * specific moment — entering a room, beginning a cutscene,
             * triggering an alarm. Without this, the envelope's phase
             * is continuous from onAttach and may be mid-cycle at
             * dramatic moments.
             *
             * Has no effect on Constant or Custom envelopes — they
             * don't read the internal clock. Their respective fields
             * (intensityModulator for Custom) remain whatever they
             * were.
            */
            void resetEnvelope() { m_time = 0.0f; }

            /**
             * The active list. Every attached, non-destroyed Light2D
             * is in here. Lighting code walks this list once per frame
             * to build the GPU upload buffer.
             *
             * Exposed publicly so the lighting pass and editor /
             * debug overlays can read without indirection. Mutation
             * is restricted to onAttach / onDestroy.
            */
            static inline std::vector<Light2D *> s_all;

        private:
            // Position in s_all, kept up to date so onDestroy can do an
            // O(1) swap-and-pop instead of a linear search. -1 means
            // "not currently registered" (after destroy, before second
            // attach if reused).
            int m_registryIndex = -1;

            // Seconds elapsed since onAttach (or the last resetEnvelope).
            // Drives the Flicker / Pulse / Strobe envelope evaluation
            // each update(). Each light keeps its own clock so multiple
            // lights with the same envelope settings stay phase-
            // independent — without the per-component clock, a room
            // full of torches would all flicker in lockstep.
            float m_time = 0.0f;
        };

    }
}