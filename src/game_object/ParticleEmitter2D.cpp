#include "ParticleEmitter2D.hpp"

namespace Bokken
{
    namespace GameObject
    {
        // Easing helpers
        float ParticleEmitter2D::applyEase(float t, ParticleEase ease)
        {
            // `t` is always in [0, 1] (0 = spawn, 1 = death).
            switch (ease)
            {
            case ParticleEase::EaseInQuad:
                return t * t;
            case ParticleEase::EaseOutQuad:
                return t * (2.0f - t);
            case ParticleEase::EaseInOutQuad:
                return (t < 0.5f) ? 2.0f * t * t
                                  : -1.0f + (4.0f - 2.0f * t) * t;
            case ParticleEase::EaseOutCubic:
            {
                float u = 1.0f - t;
                return 1.0f - u * u * u;
            }
            case ParticleEase::EaseInCubic:
                return t * t * t;
            case ParticleEase::Linear:
            default:
                return t;
            }
        }

        // RNG
        float ParticleEmitter2D::randomFloat(float min, float max)
        {
            std::uniform_real_distribution<float> dist(min, max);
            return dist(m_rng);
        }

        // Spawn a single particle
        void ParticleEmitter2D::spawnParticle(float originX, float originY)
        {
            if (static_cast<int>(m_particles.size()) >= maximumParticles)
                return;

            Particle p;

            // Randomize spawn position within the spawn box.
            float offX = (spawnOffsetX > 0.0f) ? randomFloat(-spawnOffsetX, spawnOffsetX) : 0.0f;
            float offY = (spawnOffsetY > 0.0f) ? randomFloat(-spawnOffsetY, spawnOffsetY) : 0.0f;
            p.position = glm::vec2(originX + offX, originY + offY);

            // Lifetime.
            p.maximumLifetime = randomFloat(lifetimeMinimum, lifetimeMaximum);
            p.lifetime = p.maximumLifetime;

            // Per-particle size (with variance).
            float ss = sizeStart;
            if (sizeStartVariance > 0.0f)
                ss += randomFloat(-sizeStartVariance, sizeStartVariance);
            if (ss < 0.0f)
                ss = 0.0f;
            p.sizeStart = ss;
            p.sizeEnd   = sizeEnd;
            p.size       = ss;

            // Colors.
            p.colorStart = colorStart;
            p.colorEnd   = colorEnd;

            // Direction with spread.
            float halfSpread = spreadAngle * 0.5f;
            float angle = direction + randomFloat(-halfSpread, halfSpread);
            float rad = angle * (3.14159265f / 180.0f);
            float speed = randomFloat(speedMinimum, speedMaximum);
            p.velocity = glm::vec2(std::cos(rad), std::sin(rad)) * speed;

            // Rotation.
            p.rotation = randomFloat(0.0f, 6.28318530f); // Random initial angle
            if (angularVelocityMinimum != 0.0f || angularVelocityMaximum != 0.0f)
            {
                p.angularVelocity = randomFloat(angularVelocityMinimum, angularVelocityMaximum);
                // Randomly flip rotation direction for visual variety.
                if (randomFloat(0.0f, 1.0f) > 0.5f)
                    p.angularVelocity = -p.angularVelocity;
            }

            m_particles.push_back(p);
        }

        // Burst
        void ParticleEmitter2D::burst(int count)
        {
            if (!gameObject)
                return;

            auto *t = gameObject->getComponent<Transform2D>();
            float ox = t ? t->position.x : 0.0f;
            float oy = t ? t->position.y : 0.0f;

            for (int i = 0; i < count; i++)
                spawnParticle(ox, oy);
        }

        // Update
        void ParticleEmitter2D::update(float dt)
        {
            if (!enabled)
                return;

            // Continuous emission
            if (emitting && gameObject)
            {
                auto *t = gameObject->getComponent<Transform2D>();
                float ox = t ? t->position.x : 0.0f;
                float oy = t ? t->position.y : 0.0f;

                // Compute effective emit rate.
                float effectiveRate = emitRate;

                if (velocityScaleEmission)
                {
                    glm::vec2 currentPos(ox, oy);

                    if (!m_hasPrevPosition)
                    {
                        // First frame — can't compute velocity yet. Seed the
                        // previous position and emit nothing this frame.
                        m_prevPosition = currentPos;
                        m_hasPrevPosition = true;
                        effectiveRate = 0.0f;
                    }
                    else
                    {
                        glm::vec2 delta = currentPos - m_prevPosition;
                        float dist = std::sqrt(delta.x * delta.x + delta.y * delta.y);
                        float speed = (dt > 0.0f) ? dist / dt : 0.0f;

                        // Dead-zone: ignore micro-jitter from physics settling
                        // or floating-point noise in position updates.
                        // Anything below 2% of the reference speed is treated as
                        // stationary — this kills the idle-dust problem for good.
                        constexpr float kDeadZoneFraction = 0.02f;
                        float deadZone = velocityReferenceSpeed * kDeadZoneFraction;
                        if (speed < deadZone)
                            speed = 0.0f;

                        float factor = (velocityReferenceSpeed > 0.0f)
                                           ? std::min(speed / velocityReferenceSpeed, 1.0f)
                                           : 0.0f;
                        effectiveRate *= factor;

                        m_prevPosition = currentPos;
                    }
                }

                m_emitAccum += effectiveRate * dt;
                // Cap so a long pause doesn't release a flood when re-enabled.
                if (m_emitAccum > 5.0f)
                    m_emitAccum = 5.0f;

                while (m_emitAccum >= 1.0f)
                {
                    spawnParticle(ox, oy);
                    m_emitAccum -= 1.0f;
                }
            }
            else
            {
                // Reset accumulator and velocity tracking when not emitting
                // so state doesn't carry over.
                m_emitAccum = 0.0f;
                m_hasPrevPosition = false;
            }

            // Simulate live particles
            for (auto &p : m_particles)
            {
                // Forces.
                p.velocity.y += gravity * dt;

                // Damping (velocity drag).
                if (damping > 0.0f)
                {
                    float factor = std::max(0.0f, 1.0f - damping * dt);
                    p.velocity *= factor;
                }

                p.position += p.velocity * dt;

                // Rotation.
                p.rotation += p.angularVelocity * dt;

                // Lifetime progress.
                p.lifetime -= dt;
                float t = 1.0f - std::max(0.0f, p.lifetime / p.maximumLifetime);

                // Size with easing.
                float easedT = applyEase(t, sizeEase);
                p.size = p.sizeStart + (p.sizeEnd - p.sizeStart) * easedT;
            }

            // Remove dead particles
            m_particles.erase(
                std::remove_if(m_particles.begin(), m_particles.end(),
                               [](const Particle &p)
                               { return p.lifetime <= 0.0f; }),
                m_particles.end());
        }

        // Render
        void ParticleEmitter2D::render(
            Bokken::Renderer::SpriteBatcher *batcher,
            float cameraX, float cameraY, float pixelsPerUnit,
            float screenHalfW, float screenHalfH)
        {
            if (!batcher)
                return;

            int layer = static_cast<int>(zOrder);

            for (const auto &p : m_particles)
            {
                float t = 1.0f - std::max(0.0f, p.lifetime / p.maximumLifetime);

                // Color interpolation with alpha easing for softer fade-out.
                glm::vec4 c = p.colorStart + (p.colorEnd - p.colorStart) * t;
                float alphaT = applyEase(t, alphaEase);
                c.a = p.colorStart.a + (p.colorEnd.a - p.colorStart.a) * alphaT;

                float sx = screenHalfW + (p.position.x - cameraX) * pixelsPerUnit - (p.size * pixelsPerUnit * 0.5f);
                float sy = screenHalfH - (p.position.y - cameraY) * pixelsPerUnit - (p.size * pixelsPerUnit * 0.5f);
                float sw = p.size * pixelsPerUnit;
                float sh = p.size * pixelsPerUnit;

                uint32_t rgba =
                    (static_cast<uint32_t>(std::clamp(c.r, 0.0f, 1.0f) * 255) << 24) |
                    (static_cast<uint32_t>(std::clamp(c.g, 0.0f, 1.0f) * 255) << 16) |
                    (static_cast<uint32_t>(std::clamp(c.b, 0.0f, 1.0f) * 255) << 8) |
                    static_cast<uint32_t>(std::clamp(c.a, 0.0f, 1.0f) * 255);

                batcher->drawRect(sx, sy, sw, sh, rgba, layer, blendMode);
            }
        }
    }
}