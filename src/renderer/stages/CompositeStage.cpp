#include "CompositeStage.hpp"

namespace Bokken
{
    namespace Renderer
    {

        namespace
        {
            /* FXAA, conservative tier.
             *
             * Tuned for UI: the previous default-quality settings blurred
             * fine type because the contrast between dark glyph strokes
             * and a light background trips the standard 0.0625 / 0.166
             * thresholds, and the symmetric 4-tap blend pulls glyph
             * edges toward the surrounding background colour.
             *
             * Two changes vs the textbook FXAA preset:
             *
             *   1. Higher edge thresholds — only obvious diagonal jaggies
             *      (rotated rects, SDF curve corners at sharp angles)
             *      register as edges. Glyph anti-aliasing already lives
             *      in the alpha mask coming out of the atlas, so most
             *      glyph edges have a smooth luma ramp rather than a
             *      hard step; the higher threshold lets that ramp pass
             *      through unaltered.
             *
             *   2. Heavily biased blend — even when an edge IS detected
             *      we mix only ~30% of the off-axis sample. Textbook
             *      FXAA goes 50/50, which is great for game scenes but
             *      reads as soft on flat UI.
             *
             * Net result: rotated rects stop stair-stepping, text stays
             * pixel-crisp. The cost is that very subtle diagonal AA
             * isn't fully erased — we trade a small amount of AA quality
             * on already-AA'd primitives for sharp text everywhere. */
            const char *kFS = R"(#version 330 core
                in vec2 v_uv;
                uniform sampler2D u_input;
                uniform vec2 u_invResolution;  // 1.0 / framebufferSize
                out vec4 FragColor;

                /* Rec.601 luma — the FXAA paper's recommendation. */
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

                    /* Higher thresholds than textbook FXAA so glyph
                     * edges (which already have alpha-mask AA) don't
                     * get blurred:
                     *   edgeMin: absolute floor — pixels whose luma
                     *            range is below this are flat enough
                     *            to skip outright.
                     *   edgeRel: relative — proportional to the
                     *            brighter neighbour. Textbook value is
                     *            ~0.166; we go to 0.25 so only strong
                     *            diagonal jumps trigger the blend.
                     *
                     * Together these mean the AA only fires on
                     * geometric edges (rotated rect borders, SDF
                     * curves) where the contrast is much higher than
                     * a typical anti-aliased glyph stroke. */
                    const float edgeMin = 0.0833;       // 1/12
                    const float edgeRel = 0.25;
                    if (range < max(edgeMin, lumaMax * edgeRel)) {
                        FragColor = vec4(rgbM, 1.0);
                        return;
                    }

                    /* Diagonal-only guard. Pure horizontal or pure
                     * vertical edges are very common on axis-aligned
                     * UI (top of a card, side of a button) and they
                     * don't actually need FXAA — they look fine. We
                     * detect "is this edge nearly diagonal?" by
                     * checking the H and V luma gradients; if one
                     * dominates strongly, the edge is axis-aligned
                     * and we skip. This protects single-pixel-tall
                     * dividers and the like from being smudged. */
                    float lumaH = abs(lumaN + lumaS - 2.0 * lumaM);
                    float lumaV = abs(lumaW + lumaE - 2.0 * lumaM);
                    float dom = max(lumaH, lumaV);
                    float weak = min(lumaH, lumaV);
                    if (weak < dom * 0.4) {
                        // Strongly axis-aligned — skip.
                        FragColor = vec4(rgbM, 1.0);
                        return;
                    }

                    /* Diagonal edge — sample along the dominant axis. */
                    bool horzEdge = lumaH >= lumaV;
                    vec2 dir = horzEdge ? vec2(0.0, px.y) : vec2(px.x, 0.0);

                    vec3 rgbA = (rgbM + texture(u_input, v_uv + dir * 0.5).rgb) * 0.5;
                    vec3 rgbB = rgbA * 0.5 + texture(u_input, v_uv - dir * 0.5).rgb * 0.5;

                    float lumaB = luma(rgbB);
                    vec3 picked = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;

                    /* Conservative blend: lean ~70% toward the original
                     * pixel. Textbook FXAA replaces the pixel outright;
                     * for UI that softens too much. The edges still
                     * smooth out visibly because we're blending toward
                     * the edge-walked colour, just less aggressively. */
                    FragColor = vec4(mix(rgbM, picked, 0.3), 1.0);
                }
                )";
        }

        bool CompositeStage::setup() { return m_pass.init(); }

        void CompositeStage::execute(const FrameContext &ctx)
        {
            if (!ctx.inputTarget || !ctx.outputTarget)
                return;
            ctx.outputTarget->bind();
            glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);

            ctx.inputTarget->color().bind(0);
            auto &sh = m_pass.beginPass(kFS, "composite");
            sh.setInt("u_input", 0);
            /* FXAA needs the pixel size in [0..1] UV space so it can
             * step one texel in any direction. */
            const float invW = ctx.viewportWidth  > 0
                ? 1.0f / (float)ctx.viewportWidth  : 0.0f;
            const float invH = ctx.viewportHeight > 0
                ? 1.0f / (float)ctx.viewportHeight : 0.0f;
            sh.setVec2("u_invResolution", invW, invH);
            m_pass.draw();
        }

    }
}