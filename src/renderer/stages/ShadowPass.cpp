#include "ShadowPass.hpp"
#include "../Pipeline.hpp"
#include "../RenderTarget.hpp"
#include "../lighting/Frame.hpp"

#include <SDL3/SDL.h>

namespace Bokken
{
    namespace Renderer
    {

        namespace
        {
            /* Vertex shader for shadow segment rasterization.
             *
             * Inputs: empty VAO. Geometry is reconstructed from
             * gl_VertexID (0..3 → quad corner) and gl_InstanceID
             * (decomposed into light index and segment index).
             *
             * Per-instance work:
             *   1. Decompose gl_InstanceID into (lightIdx, segIdx).
             *   2. Fetch the light's position from u_lightTex row 0.
             *   3. Fetch the segment endpoints A, B from u_segTex.
             *   4. Compute atlas U for each endpoint as
             *        u = (atan2(p - light) + 2*pi) mod 2*pi / 2*pi
             *      handling the wrap by shifting B's angle by ±2*pi
             *      if |uA - uB| > 0.5 so the resulting quad has
             *      small extent in U.
             *   5. Compute atlas V (constant per-instance) from the
             *      light's shadow slot.
             *   6. Emit a quad in atlas-pixel-space NDC spanning
             *      [uMin, uMax] × [v - 0.5/H, v + 0.5/H].
             *
             * The two segment endpoints A, B and the light position
             * are passed through as flat varyings so the fragment
             * shader can compute the exact ray-segment intersection
             * distance per pixel (linear UV interpolation of distance
             * would be wrong for slanted segments).
            */
            const char *kShadowVS = R"(#version 330 core
                uniform sampler2D u_lightTex;
                uniform sampler2D u_segTex;
                uniform int       u_segmentCount;
                uniform float     u_atlasHeight;

                flat out vec2 v_lightPos;
                flat out vec2 v_segA;
                flat out vec2 v_segB;
                flat out float v_uMin;
                flat out float v_uMax;
                flat out float v_lightRange;

                const float TWO_PI = 6.28318530717958647692;

                void main() {
                    int lightIdx = gl_InstanceID / u_segmentCount;
                    int segIdx   = gl_InstanceID % u_segmentCount;

                    // Light data: row 0 is (positionXY, directionXY).
                    // We also need the shadowSlot from row 3 (.y) and
                    // the range from row 2 (.x).
                    vec4 lRow0 = texelFetch(u_lightTex, ivec2(0, lightIdx), 0);
                    vec4 lRow2 = texelFetch(u_lightTex, ivec2(2, lightIdx), 0);
                    vec4 lRow3 = texelFetch(u_lightTex, ivec2(3, lightIdx), 0);

                    vec2 lightPos = lRow0.xy;
                    float range = lRow2.x;
                    // Shadow slot is plain float in the RGBA16F light
                    // buffer (no bit-pattern reinterpretation), with
                    // -1.0 as the "no slot allocated" sentinel.
                    float shadowSlot = lRow3.y;

                    // Segment endpoints, one segment per row.
                    vec4 seg = texelFetch(u_segTex, ivec2(0, segIdx), 0);
                    vec2 A = seg.xy;
                    vec2 B = seg.zw;

                    // Compute angular endpoints relative to the light.
                    vec2 dA = A - lightPos;
                    vec2 dB = B - lightPos;
                    float aA = atan(dA.y, dA.x);
                    float aB = atan(dB.y, dB.x);
                    if (aA < 0.0) aA += TWO_PI;
                    if (aB < 0.0) aB += TWO_PI;

                    // Wrap handling: if the angular delta is more than
                    // half a circle, the segment straddles the +X axis.
                    // Shift the smaller endpoint by +2*pi so the strip
                    // spans the short arc. After shifting, the U range
                    // can extend past 1.0 — that portion is clipped by
                    // the rasterizer, accepting a sliver of lost shadow
                    // contribution at the wrap line.
                    if (abs(aA - aB) > 3.14159265358979323846) {
                        if (aA < aB) aA += TWO_PI;
                        else         aB += TWO_PI;
                    }

                    float uA = aA / TWO_PI;
                    float uB = aB / TWO_PI;
                    float uMin = min(uA, uB);
                    float uMax = max(uA, uB);

                    // Atlas-V coord for this light's slot. Centre of
                    // the row at half-texel offset for clean sampling
                    // alignment with the lighting pass.
                    float v = (shadowSlot + 0.5) / u_atlasHeight;

                    // Quad corners, gl_VertexID 0..3 in (x, y):
                    //   0: (uMin, v_top)    1: (uMax, v_top)
                    //   2: (uMin, v_bottom) 3: (uMax, v_bottom)
                    float u = (gl_VertexID == 1 || gl_VertexID == 3) ? uMax : uMin;
                    // Use the FULL row in V to make sure we cover the
                    // single texel cleanly under any rasterizer
                    // rounding. atlasHeight is the rows count; one row
                    // is 1/atlasHeight in V.
                    float halfRow = 0.5 / u_atlasHeight;
                    float vCoord = (gl_VertexID < 2) ? (v - halfRow) : (v + halfRow);

                    // Convert atlas UV to NDC. Atlas U is in [0, 1];
                    // atlas V is in [0, 1]; NDC is [-1, +1] on both
                    // axes. The Y inversion (NDC Y goes up, atlas V
                    // goes down) is intentional: shadow slots are
                    // assigned top-to-bottom and we want slot 0 at
                    // the top of the atlas. With our V mapping and
                    // standard NDC Y, slot 0 lands at NDC y = +1
                    // which is the top of the framebuffer — correct.
                    float ndcX = u * 2.0 - 1.0;
                    float ndcY = 1.0 - vCoord * 2.0;
                    gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);

                    // Disable lights that didn't get a slot. If the
                    // shadowSlot sentinel made it here, emit a degenerate
                    // off-screen vertex so the rasterizer skips this
                    // instance entirely. Same for segments past the
                    // light's range — the fragment shader's intersection
                    // test would handle them via discard but bailing in
                    // the vertex shader saves all four fragment
                    // invocations.
                    if (shadowSlot < 0.0) {
                        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
                    }

                    v_lightPos = lightPos;
                    v_segA = A;
                    v_segB = B;
                    v_uMin = uMin;
                    v_uMax = uMax;
                    v_lightRange = range;
                }
                )";

            /* Fragment shader: per pixel, compute the ray-segment
             * intersection distance and output it. Blend mode GL_MIN
             * keeps the nearest occluder.
             *
             * The ray from the light at fragment-angle theta is
             *   r(t) = lightPos + t * (cos theta, sin theta)
             * The segment is
             *   s(u) = A + u*(B-A) for u in [0, 1]
             * Setting equal and solving for t gives
             *   t = cross(A - lightPos, B - A) / cross(rayDir, B - A)
             * where cross is the 2D scalar cross product (ax*by - ay*bx).
             *
             * The fragment angle is recovered by inverting the vertex
             * shader's mapping: gl_FragCoord.x is in pixel-space of
             * the atlas viewport, so we divide by atlas width and
             * multiply back to radians.
             *
             * Out-of-range (t outside [0, light.range]) produces a
             * very large sentinel that loses the GL_MIN blend to any
             * real occluder elsewhere.
             *
             * The intersection-u check ensures the ray hits within
             * the segment's parametric extent. Without it, the
             * fragment's "this pixel's angle's ray-segment math is
             * defined" would extend infinitely past the segment's
             * endpoints, producing false shadow beyond the segment
             * silhouette.
            */
            const char *kShadowFS = R"(#version 330 core
                flat in vec2 v_lightPos;
                flat in vec2 v_segA;
                flat in vec2 v_segB;
                flat in float v_uMin;
                flat in float v_uMax;
                flat in float v_lightRange;

                uniform float u_atlasWidth;

                out float oDistance;

                const float TWO_PI = 6.28318530717958647692;
                const float SHADOW_FAR = 1.0e9;

                float cross2(vec2 a, vec2 b) {
                    return a.x * b.y - a.y * b.x;
                }

                void main() {
                    // Recover the fragment's atlas-U then convert to angle.
                    float fragU = gl_FragCoord.x / u_atlasWidth;
                    float theta = fragU * TWO_PI;
                    vec2 rayDir = vec2(cos(theta), sin(theta));

                    vec2 BA = v_segB - v_segA;
                    vec2 ALp = v_segA - v_lightPos;

                    float denom = cross2(rayDir, BA);

                    // Parallel ray and segment — no intersection. The
                    // fragment is in the segment's angular span only
                    // because of the conservative quad bounds; output
                    // sentinel so this pixel doesn't shadow anything.
                    if (abs(denom) < 1.0e-6) {
                        oDistance = SHADOW_FAR;
                        return;
                    }

                    // Parametric solution. t is distance along the ray;
                    // u is parametric position along the segment.
                    float t = cross2(ALp, BA) / denom;
                    float u = cross2(ALp, rayDir) / denom;

                    // Reject misses: ray must hit the segment within
                    // its parametric extent, and forward of the light
                    // (t > 0), and within the light's range.
                    if (t < 0.0 || u < 0.0 || u > 1.0 || t > v_lightRange) {
                        oDistance = SHADOW_FAR;
                        return;
                    }

                    oDistance = t;
                }
                )";
        }

        ShadowPass::~ShadowPass()
        {
            if (m_emptyVAO)
                glDeleteVertexArrays(1, &m_emptyVAO);
        }

        bool ShadowPass::setup()
        {
            if (!m_shader.fromSource(kShadowVS, kShadowFS, "shadowmap"))
                return false;
            glGenVertexArrays(1, &m_emptyVAO);
            return m_emptyVAO != 0;
        }

        void ShadowPass::execute(const FrameContext &ctx)
        {
            if (!ctx.pipeline)
                return;

            // Same gather-or-no-op pattern as LightingPass. When the
            // ShadowPass runs first (the typical install order),
            // this is the call that populates the buffers.
            Lighting::Frame &lf = ctx.pipeline->lighting();
            lf.gatherIfNeeded(ctx.pipeline->currentFrameId(),
                              ctx.viewportWidth, ctx.viewportHeight);

            const uint32_t shadowCount = lf.shadowCount();
            const uint32_t segmentCount = lf.segmentCount();

            // Nothing to rasterize — but still mark the atlas as
            // rendered (cleared) so the lighting pass treats it as
            // fresh-empty rather than stale.
            RenderTarget &atlas = lf.shadowAtlas();
            if (!atlas.isValid())
                return;

            atlas.bind();
            glViewport(0, 0,
                       static_cast<int>(Lighting::Frame::SHADOW_ATLAS_WIDTH),
                       static_cast<int>(Lighting::Frame::MAX_SHADOW_SLOTS));

            // Clear to a large sentinel so any real occluder distance
            // wins the subsequent MIN-blend. glClearBufferfv targets
            // the colour attachment directly with a per-channel value;
            // R16F's red channel takes the first slot.
            const float sentinel[4] = {1.0e9f, 0.0f, 0.0f, 0.0f};
            glClearBufferfv(GL_COLOR, 0, sentinel);

            // Mark as rendered before bailing on the empty-scene path
            // so the lighting pass treats this frame's atlas as fresh
            // (cleared to "no shadows" everywhere).
            lf.markShadowAtlasRendered();

            if (shadowCount == 0 || segmentCount == 0)
                return;

            // GL state for the shadow rasterization:
            //   - Depth off (we're writing distance into a colour buffer).
            //   - Blend ON with MIN equation, function GL_ONE / GL_ONE
            //     so the equation operates on the raw source and
            //     destination colours unmodified.
            //   - Disable scissor / culling.
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_CULL_FACE);

            glEnable(GL_BLEND);
            glBlendEquation(GL_MIN);
            glBlendFunc(GL_ONE, GL_ONE);

            constexpr int UNIT_LIGHTTEX = 0;
            constexpr int UNIT_SEGTEX   = 1;
            lf.lightBuffer().bind(UNIT_LIGHTTEX);
            lf.shadowBuffer().bind(UNIT_SEGTEX);

            m_shader.bind();
            m_shader.setInt("u_lightTex",   UNIT_LIGHTTEX);
            m_shader.setInt("u_segTex",     UNIT_SEGTEX);
            m_shader.setInt("u_segmentCount", static_cast<int>(segmentCount));
            m_shader.setFloat("u_atlasHeight",
                static_cast<float>(Lighting::Frame::MAX_SHADOW_SLOTS));
            m_shader.setFloat("u_atlasWidth",
                static_cast<float>(Lighting::Frame::SHADOW_ATLAS_WIDTH));

            glBindVertexArray(m_emptyVAO);

            // One instance per (light, segment) pair, each emitting
            // four vertices via gl_VertexID. The total instance count
            // is the cross product of shadow-casting lights times the
            // global segment count.
            const GLsizei instanceCount =
                static_cast<GLsizei>(shadowCount * segmentCount);
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, instanceCount);

            glBindVertexArray(0);

            // Restore the standard alpha blend equation so post-effect
            // stages downstream don't inherit GL_MIN.
            glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            // Unbind sampler units.
            glActiveTexture(GL_TEXTURE0 + UNIT_SEGTEX);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0 + UNIT_LIGHTTEX);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

    }
}