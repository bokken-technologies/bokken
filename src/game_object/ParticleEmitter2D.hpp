#pragma once

#include "../renderer/SpriteBatcher.hpp"
#include "Component.hpp"
#include "Transform2D.hpp"
#include "Base.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <random>

namespace Bokken
{
    namespace Renderer { class SpriteBatcher; }

    namespace GameObject
    {
        // Easing function selection for size / alpha curves
        enum class ParticleEase : uint8_t
        {
            Linear = 0,
            EaseInQuad,
            EaseOutQuad,
            EaseInOutQuad,
            EaseOutCubic,
            EaseInCubic,
        };

        // A single live particle
        struct Particle
        {
            glm::vec2 position{0.0f};
            glm::vec2 velocity{0.0f};
            float rotation       = 0.0f;   // Current rotation (radians)
            float angularVelocity = 0.0f;  // Radians per second
            float lifetime       = 0.0f;   // Seconds remaining
            float maximumLifetime = 0.0f;  // Initial lifetime (for interpolation)
            float size           = 1.0f;
            float sizeStart      = 1.0f;   // Per-particle (randomized from range)
            float sizeEnd        = 0.0f;
            glm::vec4 colorStart{1.0f};
            glm::vec4 colorEnd{1.0f, 1.0f, 1.0f, 0.0f};
        };

        // CPU-side 2D particle emitter (AAA-grade)
        //
        // Attach to a GameObject with a Transform2D.
        // Spawns particles at the transform's position, simulates them each
        // frame, and submits colored quads to the SpriteBatcher.
        //
        // Modes:
        //   - Continuous: emits `emitRate` particles/sec while `emitting`.
        //   - Burst: call burst(count) for an instant batch.
        //
        // Velocity-aware emission:
        //   When `velocityScaleEmission` is true, the effective emit rate is
        //   scaled by the emitter's frame-to-frame movement speed, so a
        //   stationary emitter produces ZERO particles.  This fixes the
        //   classic "idle dust" bug where particles appear at a standing
        //   player's feet.
        //
        // Particles are world-space — they detach from the emitter once
        // spawned.
        class ParticleEmitter2D : public Component
        {
        public:
            // Emission control
            bool  emitting = false;
            float emitRate = 10.0f;  // Particles per second (base rate)

            // Velocity-aware emission
            //    When enabled, the actual emit rate is:
            //       effectiveRate = emitRate * clamp(speed / velocityRef, 0, 1)
            //    This means NO particles spawn when the emitter is stationary.
            bool  velocityScaleEmission = false;
            float velocityReferenceSpeed = 5.0f; // Speed at which full emitRate is reached

            // Particle lifetime
            float lifetimeMinimum = 0.3f;
            float lifetimeMaximum = 1.0f;

            // Initial speed
            float speedMinimum    = 1.0f;
            float speedMaximum    = 3.0f;

            // Size over lifetime
            float sizeStart       = 0.15f;
            float sizeEnd         = 0.02f;
            float sizeStartVariance = 0.0f;  // ±random added to sizeStart per particle
            ParticleEase sizeEase = ParticleEase::Linear;

            // Direction / spread
            float spreadAngle = 360.0f; // Degrees, full circle by default
            float direction   = 90.0f;  // Degrees, 90 = upward

            // Forces
            float gravity     = 0.0f;   // Applied each second (Y axis)
            float damping     = 0.0f;   // 0 = none, 1 = full stop in ~1s. Per-frame: vel *= (1 - damping * dt)

            // Rotation
            float angularVelocityMinimum = 0.0f;  // Radians/sec
            float angularVelocityMaximum = 0.0f;

            // Spawn shape
            //    Particles spawn randomly within a box centered on the
            //    emitter. (0,0) = point emitter.
            float spawnOffsetX = 0.0f;
            float spawnOffsetY = 0.0f;

            // Colors
            glm::vec4 colorStart{1.0f, 1.0f, 1.0f, 1.0f};
            glm::vec4 colorEnd{1.0f, 1.0f, 1.0f, 0.0f};
            ParticleEase alphaEase = ParticleEase::Linear;

            // Draw layer
            float zOrder = 10.0f;

            // Blend mode for all particles in this emitter. Additive is the
            // classic choice for fire and explosions; Alpha for smoke and dust.
            Bokken::Renderer::BlendMode blendMode = Bokken::Renderer::BlendMode::Alpha;

            // Pool cap
            int maximumParticles = 256;

            // Public API
            void burst(int count);
            void update(float dt) override;

            void render(Bokken::Renderer::SpriteBatcher *batcher,
                        float cameraX, float cameraY, float pixelsPerUnit,
                        float screenHalfW, float screenHalfH);

            // Read-only diagnostics.
            int liveParticleCount() const { return static_cast<int>(m_particles.size()); }

            // Idle when not emitting and all particles have expired.
            bool isIdle() const override { return !emitting && m_particles.empty(); }

        private:
            std::vector<Particle> m_particles;
            float m_emitAccum = 0.0f;

            // Previous-frame position for velocity-aware emission.
            glm::vec2 m_prevPosition{0.0f};
            bool m_hasPrevPosition = false;

            // RNG — one per emitter, avoids global rand() issues.
            std::mt19937 m_rng{std::random_device{}()};

            void spawnParticle(float originX, float originY);
            float randomFloat(float min, float max);
            static float applyEase(float t, ParticleEase ease);
        };
    }
}