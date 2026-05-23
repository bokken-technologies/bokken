#include "SpriteBatcher.hpp"

namespace Bokken
{
    namespace Renderer
    {

        namespace
        {
            /* GLSL 330 core. Pipeline supports four shape modes selected
             * per-vertex via a_shape, plus a per-vertex emissive flag
             * packed into the high bit of the shape attribute.
             *
             *   0 = Textured  — sample u_tex with v_uv, modulate by v_color.
             *   1 = AlphaMask — sample u_tex.r as coverage, tint by v_color.
             *   2 = SolidRect — flat color, no texture sample.
             *   3 = RoundedRect — fragment evaluates a rounded-rect SDF
             *                     using v_rectCenter, v_rectHalfSize and
             *                     per-corner v_radii, plus an optional
             *                     border ring of width v_borderW colored
             *                     by v_borderRGBA. Both fill and border
             *                     are anti-aliased by the same smoothstep
             *                     band so they share the silhouette.
             *
             * The SDF math is the standard rounded-rect formula:
             *   - Pick which corner the fragment is in (quadrant of rect).
             *   - Use that corner's radius for the local SDF.
             *   - d = length(max(|p - center| - (halfSize - r), 0)) - r
             *   - Anti-alias with smoothstep over a 1-pixel band derived
             *     from fwidth(d), so it stays crisp under any zoom or DPI.
             *
             * MRT outputs:
             *   oAlbedo   (location 0) — same RGBA as previous single-output path
             *   oNormal   (location 1) — tangent-space normal RG in [0,1],
             *                            from u_normalTex sampled at v_uv;
             *                            B/A unused but written as 0,1.
             *   oEmissive (location 2) — albedo when v_emissive==1, else 0.
             *                            The lighting composite stage adds
             *                            this onto the final lit color so
             *                            emissive surfaces appear bright
             *                            independent of any external light.
             *
             * When the SpriteStage has set glDrawBuffers to a single
             * attachment, oNormal/oEmissive writes are discarded by the
             * driver. The shader cost is unchanged either way — a few
             * extra ALU ops per fragment.
            */
            const char *kVS = R"(#version 330 core
                layout(location = 0) in vec2 a_pos;
                layout(location = 1) in vec2 a_uv;
                layout(location = 2) in vec4 a_color;
                layout(location = 3) in vec4 a_rectCenterHalf;
                layout(location = 4) in vec4 a_radii;
                layout(location = 5) in vec4 a_borderRGBA;
                layout(location = 6) in vec2 a_borderShape;
                uniform mat4 u_proj;
                out vec2 v_uv;
                out vec4 v_color;
                out vec2 v_fragPos;
                out vec4 v_rectCenterHalf;
                out vec4 v_radii;
                out vec4 v_borderRGBA;
                out float v_borderW;
                flat out int v_shape;
                flat out int v_emissive;
                void main() {
                    v_uv = a_uv;
                    v_color = a_color;
                    v_fragPos = a_pos;
                    v_rectCenterHalf = a_rectCenterHalf;
                    v_radii = a_radii;
                    v_borderRGBA = a_borderRGBA;
                    v_borderW = a_borderShape.x;
                    int packed = int(a_borderShape.y + 0.5);
                    v_shape    = packed & 0x7F;
                    v_emissive = (packed >> 7) & 0x1;
                    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);
                }
                )";

            const char *kFS = R"(#version 330 core
                in vec2 v_uv;
                in vec4 v_color;
                in vec2 v_fragPos;
                in vec4 v_rectCenterHalf;
                in vec4 v_radii;
                in vec4 v_borderRGBA;
                in float v_borderW;
                flat in int v_shape;
                flat in int v_emissive;
                uniform sampler2D u_tex;
                uniform sampler2D u_normalTex;
                layout(location = 0) out vec4 oAlbedo;
                layout(location = 1) out vec4 oNormal;
                layout(location = 2) out vec4 oEmissive;

                /* Pick the per-corner radius the fragment falls into. The
                 * rect is split into four quadrants by its center; each
                 * quadrant uses the radius of the corner it owns. */
                float pickRadius(vec2 local, vec4 radii)
                {
                    if (local.x < 0.0)
                        return (local.y < 0.0) ? radii.x : radii.w;
                    return (local.y < 0.0) ? radii.y : radii.z;
                }

                /* Standard rounded-rect SDF in 2D. `local` is fragment
                 * position relative to the rect's center. `halfSize` is
                 * the rect's half-extents. `r` is the corner radius for
                 * this fragment's quadrant. */
                float roundedRectSDF(vec2 local, vec2 halfSize, float r)
                {
                    vec2 q = abs(local) - halfSize + vec2(r);
                    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
                }

                void main() {
                    vec4 albedo;
                    if (v_shape == 0) {
                        albedo = texture(u_tex, v_uv) * v_color;
                    } else if (v_shape == 1) {
                        float a = texture(u_tex, v_uv).r;
                        albedo = vec4(v_color.rgb, v_color.a * a);
                    } else if (v_shape == 2) {
                        /* Solid rect, no AA. Axis-aligned rects land on
                         * pixel boundaries cleanly so AA isn't needed,
                         * and adding it via UV-distance smoothstep
                         * thinned every rect by a half-pixel — visible
                         * as faint rim color artifacts where adjacent
                         * geometry of a different color showed through.
                         * Rotated rects DO benefit from AA but they're
                         * rare in UI and we don't want to penalize the
                         * common axis-aligned case for them. */
                        albedo = v_color;
                    } else {
                        vec2 center   = v_rectCenterHalf.xy;
                        vec2 halfSize = v_rectCenterHalf.zw;
                        vec2 local    = v_fragPos - center;
                        float r       = pickRadius(local, v_radii);
                        float d       = roundedRectSDF(local, halfSize, r);
                        float aa = fwidth(d) * 0.5;
                        float fillCov = 1.0 - smoothstep(-aa, aa, d);
                        if (v_borderW > 0.0) {
                            float innerCov = 1.0 - smoothstep(-aa, aa,
                                                              d + v_borderW);
                            float borderCov = fillCov - innerCov;
                            vec4 fill = vec4(v_color.rgb,
                                             v_color.a * innerCov);
                            vec4 brdr = vec4(v_borderRGBA.rgb,
                                             v_borderRGBA.a * borderCov);
                            float outA = brdr.a + fill.a * (1.0 - brdr.a);
                            vec3 outRGB = (brdr.rgb * brdr.a +
                                           fill.rgb * fill.a * (1.0 - brdr.a))
                                          / max(outA, 1e-5);
                            albedo = vec4(outRGB, outA);
                        } else {
                            albedo = vec4(v_color.rgb,
                                          v_color.a * fillCov);
                        }
                    }

                    oAlbedo = albedo;

                    /* Tangent-space normal. The normal texture stores X,Y in
                     * the R,G channels of an unsigned [0,1] format; the
                     * shader passes them through unchanged so the lighting
                     * pass can decode (2*rg-1) and reconstruct Z.
                     *
                     * For non-textured shapes (SolidRect, RoundedRect)
                     * sampling the normal texture is meaningless, but the
                     * default normal texture bound is a 1x1 (0.5, 0.5)
                     * which decodes to a flat up-normal — exactly what UI
                     * surfaces should produce. */
                    vec2 nrg = texture(u_normalTex, v_uv).rg;
                    oNormal = vec4(nrg, 0.0, albedo.a);

                    /* Emissive output: when the per-vertex flag is set,
                     * write the lit-side color (albedo) into the emissive
                     * buffer; otherwise zero. The lighting composite pass
                     * adds emissive onto the final color after diffuse
                     * lighting, so an emissive surface stays bright at
                     * zero light intensity. */
                    oEmissive = (v_emissive == 1)
                              ? vec4(albedo.rgb, albedo.a)
                              : vec4(0.0, 0.0, 0.0, albedo.a);
                }
                )";

            // Build column-major 4x4 ortho. Maps:
            //   x: [0, w]   →  [-1, +1]
            //   y: [0, h]   →  [+1, -1]   (flip — screen-space top-left origin)
            //   z: [-1, 1]  →  [-1, +1]
            void orthoTopLeft(float out[16], float w, float h)
            {
                std::memset(out, 0, sizeof(float) * 16);
                out[0] = 2.0f / w;
                out[5] = -2.0f / h;
                out[10] = -1.0f;
                out[12] = -1.0f;
                out[13] = 1.0f;
                out[15] = 1.0f;
            }

            // Initial per-segment capacity. Grows on demand if a frame
            // pushes more quads than this. 4096 quads = 16384 verts +
            // 24576 indices. The vertex is 24 floats (96 bytes; the
            // emissive flag rides inside the existing `shape` float), so
            // that's roughly 1.5 MB per segment, 4.5 MB total across the
            // triple-buffer.
            static constexpr size_t k_initialQuadCapacity = 4096;

            // Texture units used by the sprite shader. Unit 0 is the
            // albedo texture, unit 1 is the normal texture. They are
            // distinct units so the lighting MRT can rebind the normal
            // independent of the albedo when batching by normal-texture
            // identity.
            static constexpr int k_albedoUnit = 0;
            static constexpr int k_normalUnit = 1;

            // Packed emissive bit position inside the `shape` vertex
            // attribute. ShapeMode values are 0..3, so any bit at or
            // above bit 7 is safe. Using bit 7 specifically keeps the
            // packed value representable as an exact integer float
            // (everything up to 2^24 round-trips losslessly).
            static constexpr int k_emissiveBit = 0x80;
        }

        SpriteBatcher::~SpriteBatcher()
        {
            if (m_vao)
                glDeleteVertexArrays(1, &m_vao);
            if (m_vbo)
                glDeleteBuffers(1, &m_vbo);
            if (m_ibo)
                glDeleteBuffers(1, &m_ibo);
        }

        bool SpriteBatcher::init()
        {
            if (!m_shader.fromSource(kVS, kFS, "sprite"))
                return false;

            // 1x1 white texture for solid-color quads.
            const uint8_t white[4] = {0xFF, 0xFF, 0xFF, 0xFF};
            if (!m_whiteTex.uploadFull(1, 1, TextureFormat::RGBA8, white,
                                       TextureFilter::Nearest, TextureWrap::Clamp))
                return false;

            // 1x1 default-normal texture. RGB (128, 128, 255) decodes in
            // the lighting pass via (2*rg-1) to a tangent-space Z+ vector
            // — i.e. a flat surface facing the camera. Bound at unit 1
            // for any batch whose quads have no authored normal map.
            const uint8_t flatNormal[4] = {0x80, 0x80, 0xFF, 0xFF};
            if (!m_defaultNormalTex.uploadFull(1, 1, TextureFormat::RGBA8, flatNormal,
                                               TextureFilter::Nearest, TextureWrap::Clamp))
                return false;

            glGenVertexArrays(1, &m_vao);
            glGenBuffers(1, &m_vbo);
            glGenBuffers(1, &m_ibo);

            // Allocate the triple-buffered VBO and IBO.
            m_vboCapacityVerts = k_initialQuadCapacity * 4;
            m_iboCapacityInds = k_initialQuadCapacity * 6;
            m_vboSegmentSize = m_vboCapacityVerts * sizeof(Vertex);
            m_iboSegmentSize = m_iboCapacityInds * sizeof(uint32_t);

            glBindVertexArray(m_vao);

            glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(m_vboSegmentSize * k_bufferSegments),
                         nullptr, GL_DYNAMIC_DRAW);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(m_iboSegmentSize * k_bufferSegments),
                         nullptr, GL_DYNAMIC_DRAW);

            // Vertex layout — 7 attributes total:
            //   0: pos    (vec2)
            //   1: uv     (vec2)
            //   2: color  (vec4)
            //   3: rectCenterHalf (vec4) — cx, cy, halfW, halfH
            //   4: radii  (vec4) — TL, TR, BR, BL
            //   5: borderRGBA (vec4)
            //   6: borderShape (vec2) — borderW, shape (shape has emissive packed in bit 7)
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  (const void *)offsetof(Vertex, x));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  (const void *)offsetof(Vertex, u));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  (const void *)offsetof(Vertex, r));
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  (const void *)offsetof(Vertex, rectCx));
            glEnableVertexAttribArray(4);
            glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  (const void *)offsetof(Vertex, radTL));
            glEnableVertexAttribArray(5);
            glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  (const void *)offsetof(Vertex, borderR));
            glEnableVertexAttribArray(6);
            glVertexAttribPointer(6, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  (const void *)offsetof(Vertex, borderW));

            glBindVertexArray(0);

            m_bufferSegment = 0;
            return true;
        }

        void SpriteBatcher::ensureBufferCapacity(size_t totalVerts, size_t totalIndices)
        {
            if (totalVerts <= m_vboCapacityVerts && totalIndices <= m_iboCapacityInds)
                return;

            // Double until we fit.
            while (m_vboCapacityVerts < totalVerts)
                m_vboCapacityVerts *= 2;
            while (m_iboCapacityInds < totalIndices)
                m_iboCapacityInds *= 2;

            m_vboSegmentSize = m_vboCapacityVerts * sizeof(Vertex);
            m_iboSegmentSize = m_iboCapacityInds * sizeof(uint32_t);

            // Reallocate the full triple-buffer. This orphans the old
            // allocation — any in-flight GPU reads from previous frames
            // continue on the old memory, so no sync is needed.
            glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(m_vboSegmentSize * k_bufferSegments),
                         nullptr, GL_DYNAMIC_DRAW);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(m_iboSegmentSize * k_bufferSegments),
                         nullptr, GL_DYNAMIC_DRAW);
        }

        void SpriteBatcher::begin(int viewportW, int viewportH)
        {
            m_viewportW = viewportW;
            m_viewportH = viewportH;
            m_quads.clear();
            m_verts.clear();
            m_indices.clear();
            m_stats = {};
        }

        void SpriteBatcher::drawTextured(const Texture2D *tex,
                                         float x, float y, float w, float h,
                                         float u0, float v0, float u1, float v1,
                                         uint32_t rgba, int layer,
                                         BlendMode blend)
        {
            ScissorRect sc = m_scissorStack.empty()
                ? ScissorRect{0, 0, 0, 0, false}
                : m_scissorStack.back();
            ShapeMode shape = (tex && tex->format() == TextureFormat::R8)
                ? ShapeMode::AlphaMask
                : ShapeMode::Textured;
            Quad q{x, y, w, h, u0, v0, u1, v1, rgba, tex, layer, 0.0f, blend, sc, shape, {},
                   nullptr, false};
            m_quads.push_back(q);
        }

        void SpriteBatcher::drawTexturedLit(const Texture2D *tex, const Texture2D *normalTex,
                                            float x, float y, float w, float h,
                                            float u0, float v0, float u1, float v1,
                                            uint32_t rgba, int layer,
                                            BlendMode blend, bool emissive)
        {
            ScissorRect sc = m_scissorStack.empty()
                ? ScissorRect{0, 0, 0, 0, false}
                : m_scissorStack.back();
            ShapeMode shape = (tex && tex->format() == TextureFormat::R8)
                ? ShapeMode::AlphaMask
                : ShapeMode::Textured;
            Quad q{x, y, w, h, u0, v0, u1, v1, rgba, tex, layer, 0.0f, blend, sc, shape, {},
                   normalTex, emissive};
            m_quads.push_back(q);
        }

        void SpriteBatcher::drawRect(float x, float y, float w, float h,
                                     uint32_t rgba, int layer,
                                     BlendMode blend)
        {
            ScissorRect sc = m_scissorStack.empty()
                ? ScissorRect{0, 0, 0, 0, false}
                : m_scissorStack.back();
            Quad q{x, y, w, h, 0.f, 0.f, 1.f, 1.f, rgba, nullptr, layer, 0.0f, blend, sc,
                   ShapeMode::SolidRect, {}, nullptr, false};
            m_quads.push_back(q);
        }

        void SpriteBatcher::drawRotatedRect(float cx, float cy, float w, float h,
                                            float rotationRad, uint32_t rgba, int layer,
                                            BlendMode blend)
        {
            ScissorRect sc = m_scissorStack.empty()
                ? ScissorRect{0, 0, 0, 0, false}
                : m_scissorStack.back();
            float x = cx - w * 0.5f;
            float y = cy - h * 0.5f;
            Quad q{x, y, w, h, 0.f, 0.f, 1.f, 1.f, rgba, nullptr, layer, rotationRad, blend, sc,
                   ShapeMode::SolidRect, {}, nullptr, false};
            m_quads.push_back(q);
        }

        void SpriteBatcher::drawRoundedRect(float x, float y, float w, float h,
                                            float rTL, float rTR, float rBR, float rBL,
                                            uint32_t rgba, int layer, BlendMode blend)
        {
            drawRoundedRectWithBorder(x, y, w, h, rTL, rTR, rBR, rBL,
                                      rgba, 0.0f, 0u, layer, blend);
        }

        void SpriteBatcher::drawRoundedRectWithBorder(float x, float y, float w, float h,
                                                      float rTL, float rTR, float rBR, float rBL,
                                                      uint32_t fillColor,
                                                      float borderWidth,
                                                      uint32_t borderColor,
                                                      int layer,
                                                      BlendMode blend)
        {
            if (w <= 0.0f || h <= 0.0f) return;

            /* Clamp corner radii to half the shorter side; this keeps the
             * SDF well-defined and matches CSS behaviour where a
             * border-radius larger than the rect collapses to a pill. */
            const float maxR = 0.5f * std::min(w, h);
            rTL = std::clamp(rTL, 0.0f, maxR);
            rTR = std::clamp(rTR, 0.0f, maxR);
            rBR = std::clamp(rBR, 0.0f, maxR);
            rBL = std::clamp(rBL, 0.0f, maxR);
            borderWidth = std::max(0.0f, borderWidth);

            ScissorRect sc = m_scissorStack.empty()
                ? ScissorRect{0, 0, 0, 0, false}
                : m_scissorStack.back();

            RoundedRectParams p;
            p.cx = x + w * 0.5f;
            p.cy = y + h * 0.5f;
            p.halfW = w * 0.5f;
            p.halfH = h * 0.5f;
            p.rTL = rTL; p.rTR = rTR; p.rBR = rBR; p.rBL = rBL;
            p.borderWidth = borderWidth;
            p.borderColor = borderColor;

            /* Inflate the quad slightly so the AA falloff has room. With
             * a 1-pixel smoothstep band we need at least 1px of padding
             * on each side, but we use 2 to be safe across DPI scaling
             * and weird sub-pixel positioning. The SDF stays anchored
             * to the original rect (cx/cy/halfW/halfH unchanged), so
             * the visible silhouette is still at the requested bounds. */
            const float pad = 2.0f;
            float qx = x - pad;
            float qy = y - pad;
            float qw = w + pad * 2.0f;
            float qh = h + pad * 2.0f;

            Quad q{qx, qy, qw, qh, 0.f, 0.f, 1.f, 1.f, fillColor, nullptr, layer, 0.0f, blend, sc,
                   ShapeMode::RoundedRect, p, nullptr, false};
            m_quads.push_back(q);
        }

        void SpriteBatcher::pushScissor(int x, int y, int w, int h)
        {
            /* Intersect with the parent scissor (if any) so nested
             * push/pop produces the geometric intersection — matches
             * how nested overflow:Hidden views compose. */
            if (!m_scissorStack.empty() && m_scissorStack.back().active)
            {
                const auto &p = m_scissorStack.back();
                int x0 = std::max(x, p.x);
                int y0 = std::max(y, p.y);
                int x1 = std::min(x + w, p.x + p.w);
                int y1 = std::min(y + h, p.y + p.h);
                x = x0; y = y0;
                w = std::max(0, x1 - x0);
                h = std::max(0, y1 - y0);
            }
            m_scissorStack.push_back(ScissorRect{x, y, w, h, true});
        }

        void SpriteBatcher::popScissor()
        {
            if (!m_scissorStack.empty()) m_scissorStack.pop_back();
        }

        void SpriteBatcher::applyScissor(const ScissorRect &rect)
        {
            if (!rect.active)
            {
                glDisable(GL_SCISSOR_TEST);
                return;
            }
            glEnable(GL_SCISSOR_TEST);
            /* GL scissor uses bottom-left origin; layout is top-left.
             * Convert: y_gl = viewportH - (y_top + h). */
            int yGL = m_viewportH - (rect.y + rect.h);
            if (yGL < 0) yGL = 0;
            int wClamp = std::max(0, rect.w);
            int hClamp = std::max(0, rect.h);
            glScissor(rect.x, yGL, wClamp, hClamp);
        }

        void SpriteBatcher::applyBlendMode(BlendMode mode)
        {
            switch (mode)
            {
            case BlendMode::Alpha:
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                    GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                break;
            case BlendMode::Additive:
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE,
                                    GL_ONE, GL_ONE);
                break;
            case BlendMode::Screen:
                // GL approximation: src*(1) + dst*(1-src) per channel.
                glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR,
                                    GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                break;
            }
        }

        void SpriteBatcher::issueBatch(const Texture2D *tex, BlendMode blend,
                                       size_t firstQuad, size_t count)
        {
            (void)tex; (void)blend;
            if (count == 0)
                return;

            for (size_t i = 0; i < count; ++i)
            {
                const Quad &q = m_quads[firstQuad + i];
                const float r = ((q.rgba >> 24) & 0xFF) / 255.0f;
                const float g = ((q.rgba >> 16) & 0xFF) / 255.0f;
                const float b = ((q.rgba >> 8) & 0xFF) / 255.0f;
                const float a = ((q.rgba >> 0) & 0xFF) / 255.0f;

                // Four corners (top-left origin, y down).
                float x0 = q.x, y0 = q.y;
                float x1 = q.x + q.w, y1 = q.y;
                float x2 = q.x + q.w, y2 = q.y + q.h;
                float x3 = q.x, y3 = q.y + q.h;

                // Apply rotation around center if nonzero. Rotation only
                // makes sense for plain rects/textures — rounded-rect
                // SDFs would need to rotate the SDF math too, which we
                // don't currently do. Callers requesting rotated
                // rounded-rects fall back to axis-aligned visual.
                if (q.rotation != 0.0f)
                {
                    float cx = q.x + q.w * 0.5f;
                    float cy = q.y + q.h * 0.5f;
                    float cosR = std::cos(q.rotation);
                    float sinR = std::sin(q.rotation);

                    auto rotate = [cx, cy, cosR, sinR](float &px, float &py)
                    {
                        float dx = px - cx;
                        float dy = py - cy;
                        px = cx + dx * cosR - dy * sinR;
                        py = cy + dx * sinR + dy * cosR;
                    };

                    rotate(x0, y0);
                    rotate(x1, y1);
                    rotate(x2, y2);
                    rotate(x3, y3);
                }

                /* SDF parameters — inactive shapes get zeroed but the
                 * shader's branch on v_shape skips them anyway, so
                 * filling these for non-SDF quads is fine. */
                const float br = ((q.sdf.borderColor >> 24) & 0xFF) / 255.0f;
                const float bg = ((q.sdf.borderColor >> 16) & 0xFF) / 255.0f;
                const float bb = ((q.sdf.borderColor >> 8)  & 0xFF) / 255.0f;
                const float ba = ((q.sdf.borderColor >> 0)  & 0xFF) / 255.0f;

                // Pack the emissive flag into the high bit of shape.
                // ShapeMode is 0..3 so bit 7 is safely outside the
                // shape value range. The shader unpacks via integer
                // bitwise ops on the rounded float.
                int packedShape = static_cast<int>(q.shape);
                if (q.emissive)
                    packedShape |= k_emissiveBit;
                const float shapef = static_cast<float>(packedShape);

                Vertex base{};
                base.r = r; base.g = g; base.b = b; base.a = a;
                base.rectCx = q.sdf.cx;
                base.rectCy = q.sdf.cy;
                base.rectHalfW = q.sdf.halfW;
                base.rectHalfH = q.sdf.halfH;
                base.radTL = q.sdf.rTL;
                base.radTR = q.sdf.rTR;
                base.radBR = q.sdf.rBR;
                base.radBL = q.sdf.rBL;
                base.borderW = q.sdf.borderWidth;
                base.borderR = br; base.borderG = bg;
                base.borderB = bb; base.borderA = ba;
                base.shape = shapef;

                const uint32_t baseIdx = static_cast<uint32_t>(m_verts.size());

                Vertex v0 = base; v0.x = x0; v0.y = y0; v0.u = q.u0; v0.v = q.v0;
                Vertex v1 = base; v1.x = x1; v1.y = y1; v1.u = q.u1; v1.v = q.v0;
                Vertex v2 = base; v2.x = x2; v2.y = y2; v2.u = q.u1; v2.v = q.v1;
                Vertex v3 = base; v3.x = x3; v3.y = y3; v3.u = q.u0; v3.v = q.v1;
                m_verts.push_back(v0);
                m_verts.push_back(v1);
                m_verts.push_back(v2);
                m_verts.push_back(v3);

                m_indices.push_back(baseIdx + 0);
                m_indices.push_back(baseIdx + 1);
                m_indices.push_back(baseIdx + 2);
                m_indices.push_back(baseIdx + 0);
                m_indices.push_back(baseIdx + 2);
                m_indices.push_back(baseIdx + 3);
            }

            m_stats.drawCallCount++;
            m_stats.textureBindCount++;
        }

        void SpriteBatcher::flush()
        {
            if (m_quads.empty())
                return;

            /* Stable sort by (layer, scissor, blend, texture, normalTexture,
             * shape). Normal texture is now part of the key so quads using
             * different normal maps batch separately — matching how
             * different albedo textures already break a batch. The shape
             * tag stays in the key so SDF and textured quads never share
             * a draw, since the SDF path doesn't sample u_tex and a
             * different texture binding would force an extra draw call
             * anyway. Stable so quads at the same key keep submission
             * order. */
            std::stable_sort(m_quads.begin(), m_quads.end(),
                             [](const Quad &a, const Quad &b)
                             {
                                 if (a.layer != b.layer)
                                     return a.layer < b.layer;
                                 if (a.scissor.active != b.scissor.active)
                                     return !a.scissor.active;
                                 if (a.scissor.active && b.scissor.active)
                                 {
                                     if (a.scissor.x != b.scissor.x) return a.scissor.x < b.scissor.x;
                                     if (a.scissor.y != b.scissor.y) return a.scissor.y < b.scissor.y;
                                     if (a.scissor.w != b.scissor.w) return a.scissor.w < b.scissor.w;
                                     if (a.scissor.h != b.scissor.h) return a.scissor.h < b.scissor.h;
                                 }
                                 if (a.blend != b.blend)
                                     return a.blend < b.blend;
                                 if (a.texture != b.texture)
                                     return a.texture < b.texture;
                                 if (a.normalTexture != b.normalTexture)
                                     return a.normalTexture < b.normalTexture;
                                 return static_cast<int>(a.shape) < static_cast<int>(b.shape);
                             });

            m_stats.quadCount = static_cast<int>(m_quads.size());

            // Build all vertex/index data for the entire frame.
            m_verts.clear();
            m_indices.clear();
            m_verts.reserve(m_quads.size() * 4);
            m_indices.reserve(m_quads.size() * 6);

            struct BatchRecord
            {
                size_t firstVert;
                size_t firstIndex;
                size_t indexCount;
                const Texture2D *texture;
                const Texture2D *normalTexture;
                BlendMode blend;
                ScissorRect scissor;
            };
            std::vector<BatchRecord> batches;

            auto sameScissor = [](const ScissorRect &a, const ScissorRect &b) {
                if (a.active != b.active) return false;
                if (!a.active) return true;
                return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
            };

            {
                size_t i = 0;
                while (i < m_quads.size())
                {
                    size_t j = i + 1;
                    while (j < m_quads.size()
                           && m_quads[j].layer == m_quads[i].layer
                           && m_quads[j].blend == m_quads[i].blend
                           && m_quads[j].texture == m_quads[i].texture
                           && m_quads[j].normalTexture == m_quads[i].normalTexture
                           && m_quads[j].shape == m_quads[i].shape
                           && sameScissor(m_quads[j].scissor, m_quads[i].scissor))
                    {
                        ++j;
                    }

                    size_t prevVerts = m_verts.size();
                    size_t prevInds = m_indices.size();

                    issueBatch(m_quads[i].texture, m_quads[i].blend, i, j - i);

                    batches.push_back({
                        prevVerts,
                        prevInds,
                        m_indices.size() - prevInds,
                        m_quads[i].texture,
                        m_quads[i].normalTexture,
                        m_quads[i].blend,
                        m_quads[i].scissor,
                    });

                    i = j;
                }
            }

            if (m_verts.empty())
            {
                m_quads.clear();
                return;
            }

            ensureBufferCapacity(m_verts.size(), m_indices.size());

            size_t vboOffset = m_bufferSegment * m_vboSegmentSize;
            size_t iboOffset = m_bufferSegment * m_iboSegmentSize;

            glBindVertexArray(m_vao);

            glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
            glBufferSubData(GL_ARRAY_BUFFER,
                            static_cast<GLintptr>(vboOffset),
                            static_cast<GLsizeiptr>(m_verts.size() * sizeof(Vertex)),
                            m_verts.data());

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER,
                            static_cast<GLintptr>(iboOffset),
                            static_cast<GLsizeiptr>(m_indices.size() * sizeof(uint32_t)),
                            m_indices.data());

            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);

            m_shader.bind();

            float proj[16];
            orthoTopLeft(proj, static_cast<float>(m_viewportW),
                         static_cast<float>(m_viewportH));
            m_shader.setMat4("u_proj", proj);
            m_shader.setInt("u_tex", k_albedoUnit);
            m_shader.setInt("u_normalTex", k_normalUnit);

            BlendMode currentBlend = BlendMode::Alpha;
            applyBlendMode(currentBlend);

            size_t vertByteBase = vboOffset;

            glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  (const void *)(vertByteBase + offsetof(Vertex, x)));
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  (const void *)(vertByteBase + offsetof(Vertex, u)));
            glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  (const void *)(vertByteBase + offsetof(Vertex, r)));
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  (const void *)(vertByteBase + offsetof(Vertex, rectCx)));
            glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  (const void *)(vertByteBase + offsetof(Vertex, radTL)));
            glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  (const void *)(vertByteBase + offsetof(Vertex, borderR)));
            glVertexAttribPointer(6, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  (const void *)(vertByteBase + offsetof(Vertex, borderW)));

            ScissorRect currentScissor{0, 0, 0, 0, false};
            applyScissor(currentScissor);

            const Texture2D *currentNormal = nullptr;

            for (const auto &batch : batches)
            {
                if (batch.blend != currentBlend)
                {
                    applyBlendMode(batch.blend);
                    currentBlend = batch.blend;
                    m_stats.blendSwitchCount++;
                }

                if (!sameScissor(batch.scissor, currentScissor))
                {
                    applyScissor(batch.scissor);
                    currentScissor = batch.scissor;
                    m_stats.scissorSwitchCount++;
                }

                const Texture2D *albedo = batch.texture ? batch.texture : &m_whiteTex;
                albedo->bind(k_albedoUnit);

                // Bind the normal map at unit 1. Quads without an
                // authored normal fall back to the 1x1 default which
                // decodes to a flat up-normal in the lighting pass.
                const Texture2D *normal = batch.normalTexture
                                              ? batch.normalTexture
                                              : &m_defaultNormalTex;
                if (normal != currentNormal)
                {
                    normal->bind(k_normalUnit);
                    currentNormal = normal;
                    m_stats.normalBindCount++;
                }

                size_t indexByteOffset = iboOffset + batch.firstIndex * sizeof(uint32_t);

                glDrawElements(GL_TRIANGLES,
                               static_cast<GLsizei>(batch.indexCount),
                               GL_UNSIGNED_INT,
                               (const void *)indexByteOffset);
            }

            // Restore default blend mode for any non-batcher GL code.
            if (currentBlend != BlendMode::Alpha)
                applyBlendMode(BlendMode::Alpha);
            // Always disable scissor on exit so downstream stages
            // aren't surprised.
            if (currentScissor.active)
                glDisable(GL_SCISSOR_TEST);

            // Unbind the normal texture unit so the rest of the frame
            // sees a clean state at unit 1. Unit 0 stays bound to whatever
            // the last batch used.
            glActiveTexture(GL_TEXTURE0 + k_normalUnit);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0 + k_albedoUnit);

            // Rotate to the next triple-buffer segment.
            m_bufferSegment = (m_bufferSegment + 1) % k_bufferSegments;

            m_quads.clear();
        }

    }
}