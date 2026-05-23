#include "BloomStage.hpp"

namespace Bokken
{
    namespace Renderer
    {

        namespace
        {
            const char *kBrightFS = R"(#version 330 core
                in vec2 v_uv;
                uniform sampler2D u_input;
                uniform float u_threshold;
                out vec4 FragColor;
                void main() {
                    vec3 c = texture(u_input, v_uv).rgb;
                    // Luminance using Rec.709 weights — matches sRGB/HDR conventions.
                    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
                    float k = max(l - u_threshold, 0.0) / max(l, 1e-4);
                    FragColor = vec4(c * k, 1.0);
                }
                )";

            const char *kBlurFS = R"(#version 330 core
                in vec2 v_uv;
                uniform sampler2D u_input;
                uniform vec2 u_dir;     // (1/w, 0) for horizontal, (0, 1/h) for vertical
                uniform float u_radius;
                out vec4 FragColor;
                // 9-tap Gaussian. Weights from a sigma~2 kernel, normalised.
                void main() {
                    const float w[5] = float[5](0.227027, 0.194594, 0.121622, 0.054054, 0.016216);
                    vec3 acc = texture(u_input, v_uv).rgb * w[0];
                    for (int i = 1; i < 5; ++i) {
                        vec2 off = u_dir * float(i) * u_radius;
                        acc += texture(u_input, v_uv + off).rgb * w[i];
                        acc += texture(u_input, v_uv - off).rgb * w[i];
                    }
                    FragColor = vec4(acc, 1.0);
                }
                )";

            const char *kCombineFS = R"(#version 330 core
                in vec2 v_uv;
                uniform sampler2D u_scene;
                uniform sampler2D u_bloom;
                uniform float u_intensity;
                out vec4 FragColor;
                void main() {
                    vec3 s = texture(u_scene, v_uv).rgb;
                    vec3 b = texture(u_bloom, v_uv).rgb;
                    FragColor = vec4(s + b * u_intensity, 1.0);
                }
                )";
        }

        bool BloomStage::setup()
        {
            return m_pass.init();
        }

        void BloomStage::onResize(int w, int h)
        {
            // Half-res for performance — bloom doesn't need full detail.
            m_halfW = std::max(1, w / 2);
            m_halfH = std::max(1, h / 2);
            m_bright.create(m_halfW, m_halfH, TextureFormat::RGBA16F, false);
            m_blurH.create(m_halfW, m_halfH, TextureFormat::RGBA16F, false);
        }

        void BloomStage::execute(const FrameContext &ctx)
        {
            if (!ctx.inputTarget || !ctx.outputTarget)
                return;

            if (m_halfW == 0 || m_halfH == 0)
                onResize(ctx.viewportWidth, ctx.viewportHeight);

            // 1. Bright pass into m_bright (half-res)
            m_bright.bind();
            glViewport(0, 0, m_halfW, m_halfH);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            {
                ctx.inputTarget->color().bind(0);
                auto &sh = m_pass.beginPass(kBrightFS, "bloom-bright");
                sh.setInt("u_input", 0);
                sh.setFloat("u_threshold", threshold);
                m_pass.draw();
            }

            // 2a. Horizontal blur: m_bright → m_blurH
            m_blurH.bind();
            glViewport(0, 0, m_halfW, m_halfH);
            {
                m_bright.color().bind(0);
                auto &sh = m_pass.beginPass(kBlurFS, "bloom-blur");
                sh.setInt("u_input", 0);
                sh.setVec2("u_dir", 1.0f / (float)m_halfW, 0.0f);
                sh.setFloat("u_radius", radius);
                m_pass.draw();
            }

            // 2b. Vertical blur: m_blurH → m_bright (reuse)
            m_bright.bind();
            glViewport(0, 0, m_halfW, m_halfH);
            {
                m_blurH.color().bind(0);
                auto &sh = m_pass.beginPass(kBlurFS, "bloom-blur");
                sh.setInt("u_input", 0);
                sh.setVec2("u_dir", 0.0f, 1.0f / (float)m_halfH);
                sh.setFloat("u_radius", radius);
                m_pass.draw();
            }

            // 3. Combine scene + blurred bright into outputTarget (full res)
            ctx.outputTarget->bind();
            glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            {
                ctx.inputTarget->color().bind(0);
                m_bright.color().bind(1);
                auto &sh = m_pass.beginPass(kCombineFS, "bloom-combine");
                sh.setInt("u_scene", 0);
                sh.setInt("u_bloom", 1);
                sh.setFloat("u_intensity", intensity);
                m_pass.draw();
            }
        }

    }
}
