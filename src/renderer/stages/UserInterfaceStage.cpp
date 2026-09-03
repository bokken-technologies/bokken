#include "UserInterfaceStage.hpp"

namespace Bokken
{
    namespace Renderer
    {
        namespace
        {
            /* FXAA, conservative tier tuned for UI:
             * Prevents stair-stepping on rotated rects/SDF curves while
             * keeping alpha-masked glyph text sharp. */
            const char *kFS = R"(#version 330 core
                in vec2 v_uv;
                uniform sampler2D u_input;
                uniform vec2 u_invResolution;  // 1.0 / framebufferSize
                out vec4 FragColor;

                float luma(vec3 c) {
                    return dot(c, vec3(0.299, 0.587, 0.114));
                }

                void main() {
                    vec2 px = u_invResolution;
                    vec3 rgbM = texture(u_input, v_uv).rgb;
                    vec3 rgbN = texture(u_input, v_uv + vec2( 0.0, -px.y)).rgb;
                    vec3 rgbS = texture(u_input, v_uv + vec2( 0.0,  px.y)).rgb;
                    vec3 rgbW = texture(u_input, v_uv + vec2(-px.x,  0.0)).rgb;
                    vec3 rgbE = texture(u_input, v_uv + vec2( px.x,  0.0)).rgb;

                    float lumaM = luma(rgbM);
                    float lumaN = luma(rgbN);
                    float lumaS = luma(rgbS);
                    float lumaW = luma(rgbW);
                    float lumaE = luma(rgbE);

                    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
                    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
                    float range   = lumaMax - lumaMin;

                    const float edgeMin = 0.0833;       // 1/12
                    const float edgeRel = 0.25;
                    if (range < max(edgeMin, lumaMax * edgeRel)) {
                        FragColor = vec4(rgbM, 1.0);
                        return;
                    }

                    float lumaH = abs(lumaN + lumaS - 2.0 * lumaM);
                    float lumaV = abs(lumaW + lumaE - 2.0 * lumaM);
                    float dom = max(lumaH, lumaV);
                    float weak = min(lumaH, lumaV);
                    if (weak < dom * 0.4) {
                        FragColor = vec4(rgbM, 1.0);
                        return;
                    }

                    bool horzEdge = lumaH >= lumaV;
                    vec2 dir = horzEdge ? vec2(0.0, px.y) : vec2(px.x, 0.0);

                    vec3 rgbA = (rgbM + texture(u_input, v_uv + dir * 0.5).rgb) * 0.5;
                    vec3 rgbB = rgbA * 0.5 + texture(u_input, v_uv - dir * 0.5).rgb * 0.5;

                    float lumaB = luma(rgbB);
                    vec3 picked = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;

                    FragColor = vec4(mix(rgbM, picked, 0.3), 1.0);
                }
            )";
        }

        bool UserInterfaceStage::setup()
        {
            return m_pass.init();
        }

        void UserInterfaceStage::execute(const FrameContext &ctx)
        {
            if (!ctx.inputTarget || !ctx.outputTarget)
                return;

            ctx.inputTarget->bind();
            glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);

            if (ctx.uiBatcher)
            {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDisable(GL_DEPTH_TEST);

                ctx.uiBatcher->flush();
            }

            ctx.outputTarget->bind();
            glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);

            ctx.inputTarget->color().bind(0);
            auto &sh = m_pass.beginPass(kFS, "userInterface");
            sh.setInt("u_input", 0);

            const float invW = ctx.viewportWidth > 0 ? 1.0f / static_cast<float>(ctx.viewportWidth) : 0.0f;
            const float invH = ctx.viewportHeight > 0 ? 1.0f / static_cast<float>(ctx.viewportHeight) : 0.0f;
            sh.setVec2("u_invResolution", invW, invH);

            m_pass.draw();
        }
    }
}