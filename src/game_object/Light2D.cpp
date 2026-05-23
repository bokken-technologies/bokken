#include "Light2D.hpp"

namespace Bokken
{
    namespace GameObject
    {

        namespace
        {
            // Pack the LightType + flags into the 32-bit field the GPU
            // reads. Light type occupies the low two bits; the upper
            // bits carry the boolean toggles. Kept TU-local because
            // nothing outside the snapshot path needs this.
            uint32_t packFlags(LightType type, bool enabled,
                               bool castsShadows, bool hasCookie)
            {
                using namespace Renderer::Lighting;
                uint32_t flags = static_cast<uint32_t>(type) & LIGHT_TYPE_MASK;
                if (enabled)        flags |= LIGHT_FLAG_ENABLED;
                if (castsShadows)   flags |= LIGHT_FLAG_CASTS_SHADOWS;
                if (hasCookie)      flags |= LIGHT_FLAG_HAS_COOKIE;
                return flags;
            }

            // Cheap 1D hash → float in [0, 1]. Used as the random oracle
            // for valueNoise1D below. Mixing constants are the standard
            // "PCG/wang-hash" style — they avoid the visible periodicity
            // that simpler `(x * large_prime) & 0xFFFF` hashes produce
            // at small inputs, which would manifest as repeating
            // flicker patterns.
            float hash1D(int x)
            {
                uint32_t u = static_cast<uint32_t>(x);
                u = (u ^ 61u) ^ (u >> 16);
                u = u + (u << 3);
                u = u ^ (u >> 4);
                u = u * 0x27d4eb2du;
                u = u ^ (u >> 15);
                return static_cast<float>(u & 0x00FFFFFFu) / 16777215.0f;
            }

            // Smoothstep cubic. About as cheap as a multiply-add can
            // get; GLSL-equivalent expression is 3t^2 - 2t^3. Used to
            // soften the linear-interpolation seams between random
            // samples in valueNoise1D so the noise reads as "soft
            // random fluctuation" rather than "zigzag".
            float smoothstep01(float t)
            {
                t = std::clamp(t, 0.0f, 1.0f);
                return t * t * (3.0f - 2.0f * t);
            }

            // 1D value noise. Samples random values at integer x,
            // smoothly interpolates between them. Output is in [0, 1].
            // For the flicker envelope, callers feed (time * frequency)
            // — a higher frequency walks more samples per second and
            // produces faster, jitterier flicker.
            //
            // This is intentionally simpler than Perlin noise: it has
            // none of the rotational properties Perlin offers (we only
            // need one dimension) and is cheap enough that 256 lights
            // ticking it every frame is invisible in the profiler.
            float valueNoise1D(float x)
            {
                const int xi = static_cast<int>(std::floor(x));
                const float xf = x - static_cast<float>(xi);
                const float a = hash1D(xi);
                const float b = hash1D(xi + 1);
                return a + (b - a) * smoothstep01(xf);
            }
        }

        void Light2D::onAttach()
        {
            // Fresh attach starts the envelope at its authored peak so
            // a torch lit at intensity 2.5 appears as 2.5 on the very
            // first frame, before update() has a chance to modulate.
            intensityModulator = 1.0f;

            m_registryIndex = static_cast<int>(s_all.size());
            s_all.push_back(this);
        }

        void Light2D::update(float dt)
        {
            // Advance the per-component clock. Used by every envelope
            // except Constant (which ignores time) and Custom (which is
            // entirely script-driven).
            m_time += dt;

            // The amplitude knob is clamped to [0, 1]. Above 1.0 the
            // modulator would go negative on the deep side of the swing
            // and produce nonsense in the lighting math; below 0 there
            // is no swing at all. The artist-facing field stays free —
            // we clamp at evaluation, not at assignment, so a script
            // tweening amplitude past the bounds reads the un-clamped
            // value back from the field.
            const float amp = std::clamp(envelopeAmplitude, 0.0f, 1.0f);
            const float t = m_time + envelopePhase;
            const float twoPi = 6.28318530717958647692f;

            switch (envelope)
            {
            case LightEnvelope::Constant:
                intensityModulator = 1.0f;
                break;

            case LightEnvelope::Flicker:
            {
                // 1D value noise modulated to [1 - amp, 1]. The noise
                // sample is in [0, 1], so we map it into the swing band
                // with intensityModulator = 1 - amp * (1 - noise) =
                // (1 - amp) + amp * noise. A `noise = 1` sample reads
                // as peak (no dim), `noise = 0` reads as deepest dim.
                // This keeps the artist-authored intensity as the
                // ceiling — flicker only ever subtracts brightness,
                // never adds.
                const float n = valueNoise1D(t * envelopeFrequency);
                intensityModulator = (1.0f - amp) + amp * n;
                break;
            }

            case LightEnvelope::Pulse:
            {
                // Sinusoidal swing. Same "peak = authored intensity"
                // convention as Flicker: the modulator is bounded
                // above at 1.0 and dips by amp at the trough. A
                // breath / heartbeat pulse with amp=0.3 reads as
                // "always at least 70% of authored brightness, peaking
                // at 100% once per period". The 0.5*(1 + sin) maps
                // sin's [-1, 1] to [0, 1] before the swing remap.
                const float s = 0.5f * (1.0f + std::sin(twoPi * envelopeFrequency * t));
                intensityModulator = (1.0f - amp) + amp * s;
                break;
            }

            case LightEnvelope::Strobe:
            {
                // Square wave with a 5%-of-period smoothstep edge so the
                // transitions read as "snappy" rather than aliasing into
                // a flicker at low frame rates or when the period falls
                // between two consecutive frames. Without the smoothed
                // edge, two adjacent frames can land on opposite halves
                // of a fast strobe and the displayed brightness becomes
                // dependent on frame timing.
                //
                // Strobe spends half its period at peak and half at the
                // dim trough. Using cos here (centred at +1 at t=0)
                // means a fresh light starts ON, which is the more
                // useful default for "alarm fires at t=0".
                const float c = std::cos(twoPi * envelopeFrequency * t);
                const float edge = 0.05f;
                const float square = smoothstep01((c + edge) / (2.0f * edge));
                intensityModulator = (1.0f - amp) + amp * square;
                break;
            }

            case LightEnvelope::Custom:
                // Script-managed; leave intensityModulator untouched.
                // The script is expected to write a value in roughly
                // [0, 1] each frame; values outside that range work
                // (the lighting math is linear in the modulator) but
                // peak intensity goes past the artist-authored ceiling.
                break;
            }
        }

        void Light2D::onDestroy()
        {
            if (m_registryIndex < 0)
                return;
            // O(1) swap-and-pop. The component swapped into our slot
            // updates its registry index so subsequent destroys
            // continue to work in constant time.
            const size_t idx = static_cast<size_t>(m_registryIndex);
            const size_t last = s_all.size() - 1;
            if (idx != last)
            {
                s_all[idx] = s_all[last];
                s_all[idx]->m_registryIndex = static_cast<int>(idx);
            }
            s_all.pop_back();
            m_registryIndex = -1;
        }

        void Light2D::snapshot(Renderer::Lighting::Light &out,
                               const Renderer::Lighting::WorldToScreen &w2s) const
        {
            // World-space position from the sibling Transform2D. Lights
            // attached to GameObjects without a Transform2D snap to
            // origin — usable for global / directional lights that
            // don't care about position anyway.
            glm::vec2 worldPos{0.0f};
            if (gameObject)
            {
                if (auto *t = gameObject->getComponent<Transform2D>())
                    worldPos = t->position;
            }
            // Convert into the render-target pixel coordinate system
            // the lighting subsystem operates in. The shader and
            // TileLightGrid both compare this to fragPosPx (pixels);
            // emitting world units here makes the comparison
            // dimensionally meaningless.
            out.position = w2s.apply(worldPos);

            // Direction expressed as a 2D unit vector. The component
            // stores it as a degrees angle for ergonomic authoring
            // (artists think in angles, not vectors); we convert here
            // each frame so the GPU side stays trig-free.
            const float rad = directionDegrees * 3.14159265358979323846f / 180.0f;
            out.direction = glm::vec2(std::cos(rad), std::sin(rad));

            out.color = color;
            out.intensity = intensity * intensityModulator;

            // Range is a world-space distance in user-facing units;
            // bring it into pixel space too so the shader's
            // `dist >= L.range` test agrees with the now-pixelised
            // position.
            out.range = w2s.scaleLength(range);
            out.falloffExponent = falloff;
            // Convert cone half-angles from degrees to cosines for the
            // shader's smoothstep. The shader compares dot(toLight,
            // lightDir) against these, so storing cosines here saves
            // a per-pixel acos / cos pair in every spot light.
            const float innerRad = innerConeAngle * 3.14159265358979323846f / 180.0f;
            const float outerRad = outerConeAngle * 3.14159265358979323846f / 180.0f;
            out.innerConeCos = std::cos(innerRad);
            out.outerConeCos = std::cos(outerRad);

            const bool hasCookie = !cookiePath.empty();
            out.flags = packFlags(type, enabled, castsShadows, hasCookie);

            // Slot fields are filled in by the lighting / shadow passes
            // which know per-frame slot availability. Default-init both
            // to "no slot" so a half-populated upload is well-defined.
            out.shadowSlot = Renderer::Lighting::LIGHT_NO_SLOT;
            out.cookieSlot = Renderer::Lighting::LIGHT_NO_SLOT;
            out.softness = shadowSoftness;

            out.cookieUVOffset = cookieUVOffset;
            out.cookieUVScale = cookieUVScale;
        }

    }
}