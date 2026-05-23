#include "ColorGradeStage.hpp"

namespace Bokken
{
    namespace Renderer
    {

        namespace
        {
            // Hable filmic. The constants are the published Uncharted 2 numbers,
            // with W = 11.2 (white point). Output is in [0, 1].
            const char *kFS = R"(#version 330 core
                in vec2 v_uv;
                uniform sampler2D u_input;
                uniform float u_exposure;
                uniform float u_saturation;
                uniform float u_gamma;
                out vec4 FragColor;

                vec3 hable(vec3 x) {
                    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
                    return ((x*(A*x + C*B) + D*E) / (x*(A*x + B) + D*F)) - E/F;
                }

                void main() {
                    vec3 c = texture(u_input, v_uv).rgb * u_exposure;

                    // Filmic tonemap, normalised to white point.
                    vec3 mapped = hable(c);
                    vec3 white = hable(vec3(11.2));
                    mapped /= white;

                    // Saturation pivot around luminance.
                    float luma = dot(mapped, vec3(0.2126, 0.7152, 0.0722));
                    mapped = mix(vec3(luma), mapped, u_saturation);

                    // Gamma — use 1/gamma in the pow to invert the linear→display transform.
                    mapped = pow(max(mapped, 0.0), vec3(1.0 / u_gamma));

                    FragColor = vec4(mapped, 1.0);
                }
                )";
        }

        bool ColorGradeStage::setup() { return m_pass.init(); }

        void ColorGradeStage::execute(const FrameContext &ctx)
        {
            if (!ctx.inputTarget || !ctx.outputTarget)
                return;
            ctx.outputTarget->bind();
            glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);

            ctx.inputTarget->color().bind(0);
            auto &sh = m_pass.beginPass(kFS, "color-grade");
            sh.setInt("u_input", 0);
            sh.setFloat("u_exposure", exposure);
            sh.setFloat("u_saturation", saturation);
            sh.setFloat("u_gamma", gamma);
            m_pass.draw();
        }

    }
}
