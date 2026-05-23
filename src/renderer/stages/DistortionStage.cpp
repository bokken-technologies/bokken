#include "DistortionStage.hpp"

namespace Bokken
{
    namespace Renderer
    {

        namespace
        {
            // Pass 1: Render displacement vectors into the RG16F buffer.
            //
            // The shader accumulates displacement from two sources:
            //   - Up to k_maxShockwaves radial shockwaves, each contributing
            //     a ring-shaped displacement band pointing away from the origin.
            //   - An optional scrolling sinusoidal heat haze.
            //
            // Output is in RG: (dx, dy) in normalised UV space. The apply
            // pass scales these by the global intensity uniform.
            const char *kDisplacementFS = R"(#version 330 core
                in vec2 v_uv;
                out vec4 FragColor;

                // Shockwave uniforms. Each wave is packed as:
                //   vec4(originX, originY, radius, thickness)
                //   vec4(amplitude, 0, 0, 0)
                // Using two vec4s per wave for clean alignment.
                const int MAX_WAVES = 16;
                uniform int u_waveCount;
                uniform vec4 u_waveA[MAX_WAVES]; // origin.xy, radius, thickness
                uniform vec4 u_waveB[MAX_WAVES]; // amplitude, unused, unused, unused

                // Heat haze uniforms.
                uniform int u_heatHaze;
                uniform float u_time;
                uniform float u_hazeSpeed;
                uniform float u_hazeFreq;
                uniform float u_hazeAmp;

                // Aspect ratio correction so shockwaves are circular on
                // non-square viewports.
                uniform float u_aspect; // width / height

                void main() {
                    vec2 displacement = vec2(0.0);

                    // Shockwaves: radial ring displacement.
                    for (int i = 0; i < u_waveCount; ++i) {
                        vec2 origin = u_waveA[i].xy;
                        float radius = u_waveA[i].z;
                        float thickness = u_waveA[i].w;
                        float amplitude = u_waveB[i].x;

                        // Aspect-corrected distance from the wave origin.
                        vec2 delta = v_uv - origin;
                        delta.x *= u_aspect;
                        float dist = length(delta);

                        // Smooth ring: 1.0 at the wave front, fading to 0.0
                        // at ±thickness/2 from the front.
                        float halfThick = thickness * 0.5;
                        float band = smoothstep(radius - halfThick, radius, dist)
                                   - smoothstep(radius, radius + halfThick, dist);

                        // Displacement points radially outward from the origin.
                        vec2 dir = dist > 0.001 ? normalize(delta) : vec2(0.0);

                        // Undo the aspect correction on the direction so the
                        // displacement is in UV space.
                        dir.x /= u_aspect;

                        displacement += dir * band * amplitude;
                    }

                    // Heat haze: vertical sinusoidal wobble.
                    if (u_heatHaze == 1) {
                        float phase = v_uv.y * u_hazeFreq + u_time * u_hazeSpeed;
                        displacement.x += sin(phase) * u_hazeAmp;
                        // Smaller vertical component offset by quarter-phase
                        // for a more organic shimmer.
                        displacement.y += sin(phase * 0.7 + 1.57) * u_hazeAmp * 0.3;
                    }

                    FragColor = vec4(displacement, 0.0, 1.0);
                }
                )";

            // Pass 2: Apply displacement to the scene.
            //
            // Samples the displacement buffer at the current UV, scales by
            // the global intensity, and uses the result to offset the UV
            // lookup into the scene texture. Simple, cheap, effective.
            const char *kApplyFS = R"(#version 330 core
                in vec2 v_uv;
                uniform sampler2D u_scene;
                uniform sampler2D u_displacement;
                uniform float u_intensity;
                out vec4 FragColor;
                void main() {
                    vec2 offset = texture(u_displacement, v_uv).rg * u_intensity;
                    vec2 uv = clamp(v_uv + offset, 0.0, 1.0);
                    FragColor = texture(u_scene, uv);
                }
                )";
        }

        void DistortionStage::addShockwave(float screenX, float screenY,
                                           float speed, float thickness,
                                           float amplitude, float maxRadius)
        {
            if (static_cast<int>(m_shockwaves.size()) >= k_maxShockwaves)
            {
                // Drop the oldest wave to make room.
                m_shockwaves.erase(m_shockwaves.begin());
            }

            Shockwave w;
            w.originX = screenX;
            w.originY = screenY;
            w.radius = 0.0f;
            w.maxRadius = maxRadius;
            w.speed = speed;
            w.thickness = thickness;
            w.amplitude = amplitude;
            m_shockwaves.push_back(w);
        }

        bool DistortionStage::setup()
        {
            return m_displacementPass.init() && m_applyPass.init();
        }

        void DistortionStage::onResize(int w, int h)
        {
            m_width = w;
            m_height = h;
            // RG16F gives signed float precision for displacement vectors.
            m_displacementBuffer.create(w, h, TextureFormat::RGBA16F, false);
        }

        void DistortionStage::execute(const FrameContext &ctx)
        {
            if (!ctx.inputTarget || !ctx.outputTarget)
                return;

            // Early out when there's nothing to distort.
            bool hasShockwaves = !m_shockwaves.empty();
            bool hasHaze = heatHaze;
            if (intensity <= 0.0f || (!hasShockwaves && !hasHaze))
            {
                // Passthrough: copy input to output unchanged.
                ctx.outputTarget->bind();
                glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);
                ctx.inputTarget->color().bind(0);
                auto &sh = m_applyPass.beginPass(kApplyFS, "distortion-passthrough");
                sh.setInt("u_scene", 0);
                sh.setInt("u_displacement", 0);
                sh.setFloat("u_intensity", 0.0f);
                m_applyPass.draw();
                return;
            }

            if (m_width == 0 || m_height == 0)
                onResize(ctx.viewportWidth, ctx.viewportHeight);

            // Advance time for heat haze animation.
            m_time += ctx.dt;

            // Advance shockwave radii and prune expired ones.
            for (auto &w : m_shockwaves)
                w.radius += w.speed * ctx.dt;

            m_shockwaves.erase(
                std::remove_if(m_shockwaves.begin(), m_shockwaves.end(),
                               [](const Shockwave &w) { return w.radius > w.maxRadius; }),
                m_shockwaves.end());

            // Pass 1: Render displacement into the buffer.
            m_displacementBuffer.bind();
            glViewport(0, 0, m_width, m_height);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            {
                auto &sh = m_displacementPass.beginPass(kDisplacementFS, "distortion-disp");

                int waveCount = std::min(static_cast<int>(m_shockwaves.size()), k_maxShockwaves);
                sh.setInt("u_waveCount", waveCount);

                for (int i = 0; i < waveCount; ++i)
                {
                    const Shockwave &w = m_shockwaves[i];
                    char bufA[32], bufB[32];
                    std::snprintf(bufA, sizeof(bufA), "u_waveA[%d]", i);
                    std::snprintf(bufB, sizeof(bufB), "u_waveB[%d]", i);
                    sh.setVec4(bufA, w.originX, w.originY, w.radius, w.thickness);
                    sh.setVec4(bufB, w.amplitude, 0.0f, 0.0f, 0.0f);
                }

                sh.setInt("u_heatHaze", hasHaze ? 1 : 0);
                sh.setFloat("u_time", m_time);
                sh.setFloat("u_hazeSpeed", heatHazeSpeed);
                sh.setFloat("u_hazeFreq", heatHazeFrequency);
                sh.setFloat("u_hazeAmp", heatHazeAmplitude);
                sh.setFloat("u_aspect",
                            m_width > 0 ? static_cast<float>(m_width) / static_cast<float>(m_height) : 1.0f);

                m_displacementPass.draw();
            }

            // Pass 2: Apply displacement to the scene.
            ctx.outputTarget->bind();
            glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);
            {
                ctx.inputTarget->color().bind(0);
                m_displacementBuffer.color().bind(1);
                auto &sh = m_applyPass.beginPass(kApplyFS, "distortion-apply");
                sh.setInt("u_scene", 0);
                sh.setInt("u_displacement", 1);
                sh.setFloat("u_intensity", intensity);
                m_applyPass.draw();
            }
        }

    }
}