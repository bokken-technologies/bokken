#pragma once

#include "Shader.hpp"
#include "Texture2D.hpp"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <vector>
#include <cstdint>
#include <cstring>

namespace Bokken
{
    namespace Renderer
    {

        /**
         * Per-quad blend mode.
         *
         * Alpha is the standard porter-duff over.
         * Additive is src*srcA + dst — for fire, particles, magic.
         * Screen is 1 - (1-src)*(1-dst) — subtler than additive.
        */
        enum class BlendMode : uint8_t
        {
            Alpha,
            Additive,
            Screen,
        };

        /**
         * What the fragment shader should treat the quad as.
         *
         * Textured  — sample u_tex with v_uv and modulate by v_color (RGBA texture).
         * AlphaMask — sample u_tex.r as coverage, tint by v_color (R8 atlas: glyphs, masks).
         * SolidRect — flat-color rect, no texture sampled (white texture bound).
         * RoundedRect — fragment computes a rounded-rect SDF from the quad's
         *               rect bounds + per-corner radii + optional border, and
         *               outputs anti-aliased fill / border / fill+border in one pass.
         *               Replaces the old corner-sprite + edge-rect composition that
         *               left visible seams and pixel halos at small radii.
        */
        enum class ShapeMode : uint8_t
        {
            Textured  = 0,
            AlphaMask = 1,
            SolidRect = 2,
            RoundedRect = 3,
        };

        /**
         * SDF parameters carried per-quad when ShapeMode is RoundedRect.
         *
         * `rectCenter` and `rectHalfSize` describe the rect in screen-space pixels.
         * The fragment shader's per-pixel position is reconstructed from gl_FragCoord
         * and the viewport size so the SDF is evaluated in the same pixel space the
         * UI was laid out in.
         *
         * `radii` is TL, TR, BR, BL — clockwise from top-left, matching how Drawing.hpp's
         * Corners struct is unpacked. Each radius is clamped at runtime to half the
         * shorter dimension so we don't get geometric impossibilities.
         *
         * `borderWidth = 0.0` disables the border path; `borderColor` is then ignored.
         * When the border is enabled the fragment outputs a smooth crossfade between
         * the border ring and the fill interior, with both anti-aliased against the
         * outer rim by a 1-pixel smoothstep band.
        */
        struct RoundedRectParams
        {
            float cx, cy;
            float halfW, halfH;
            float rTL, rTR, rBR, rBL;
            float borderWidth;
            uint32_t borderColor; // RGBA8
        };

        /**
         * Batched 2D quad renderer.
         *
         * Clipping and shapes:
         * - Scissor stack: pushScissor/popScissor wrap a draw range with
         *   a glScissor rect. Each quad pushed while a scissor is active
         *   carries the active rect along; flush() applies/restores the
         *   GL scissor at batch boundaries when the active rect changes.
         *   Used by the Canvas overflow:Hidden path and by ScrollView.
         *
         * - Shape modes: each quad carries a ShapeMode the fragment
         *   shader uses to pick its rendering behaviour. The RoundedRect
         *   mode performs proper SDF anti-aliasing at every radius and
         *   every device-pixel scale, producing clean edges with no
         *   halos on the inside of rounded buttons.
         *
         * Deferred lighting substrate:
         * - Normal map per quad: an optional secondary texture sampled
         *   alongside u_tex and written to GL_COLOR_ATTACHMENT1 in
         *   tangent-space encoding (RG channels, Z reconstructed). Quads
         *   without an authored normal map output a flat up-normal so
         *   downstream lighting still has a sensible value to sample.
         *
         * - Per-quad emissive flag: when set, the quad's color is also
         *   written to GL_COLOR_ATTACHMENT2 (after multiplication by
         *   the sampled texture if any), telling the lighting composite
         *   pass that this surface contributes self-emitted light
         *   independent of any external light source. Used for glowing
         *   FX, magic effects, additive particles.
         *
         * The MRT outputs are only meaningful when the SpriteStage has
         * attached aux targets and called setDrawBuffers(3) before
         * flushing. When only one draw buffer is enabled the fragment
         * shader still computes the secondary outputs but they are
         * discarded by the driver. Cost is a few extra ALU ops per
         * fragment — negligible.
        */
        class SpriteBatcher
        {
        public:
            struct ScissorRect
            {
                int x, y, w, h;          // pixel coords, top-left origin
                bool active = false;     // false = no scissor
            };

            struct Quad
            {
                float x, y;
                float w, h;
                float u0, v0;
                float u1, v1;
                uint32_t rgba;
                const Texture2D *texture;
                int layer;
                float rotation;
                BlendMode blend;
                ScissorRect scissor;
                ShapeMode shape = ShapeMode::Textured;
                RoundedRectParams sdf{}; // only populated when shape == RoundedRect

                // Optional tangent-space normal map sampled with the same
                // UVs as `texture`. Stored as RGB where R/G are X/Y in
                // [0,1] and B is unused (Z is reconstructed in the
                // lighting shader). When null, the normal output is a
                // flat (0.5, 0.5) — decoded as (0,0,1), a sprite facing
                // the camera.
                const Texture2D *normalTexture = nullptr;

                // When true, this quad's color contributes to the
                // emissive output buffer. Surfaces flagged emissive
                // appear lit even with all lights disabled — useful for
                // glow, hot particles, magic effects, screens, neon.
                bool emissive = false;
            };

            SpriteBatcher() = default;
            ~SpriteBatcher();

            bool init();

            void begin(int projWidth, int projHeight, int viewportWidth = 0, int viewportHeight = 0);

            void drawTextured(const Texture2D *tex,
                              float x, float y, float w, float h,
                              float u0, float v0, float u1, float v1,
                              uint32_t rgba = 0xFFFFFFFFu, int layer = 0,
                              BlendMode blend = BlendMode::Alpha,
                              float rotationDeg = 0.0f);

            /**
             * Textured quad with an authored tangent-space normal map and
             * optional emissive flag. The normal texture is sampled with
             * the same UVs as the albedo texture; pass nullptr to fall
             * back to the flat-normal default. The emissive flag toggles
             * whether this quad contributes to the emissive MRT output.
             *
             * This is the lighting-aware variant of drawTextured. Existing
             * call sites of drawTextured keep working unchanged — they
             * default to no normal map and not emissive, which produces
             * the same single-attachment output as before.
            */
            void drawTexturedLit(const Texture2D *tex, const Texture2D *normalTex,
                                 float x, float y, float w, float h,
                                 float u0, float v0, float u1, float v1,
                                 uint32_t rgba = 0xFFFFFFFFu, int layer = 0,
                                 BlendMode blend = BlendMode::Alpha,
                                 bool emissive = false,
                                 float rotationDeg = 0.0f);

            void drawRect(float x, float y, float w, float h,
                          uint32_t rgba, int layer = 0,
                          BlendMode blend = BlendMode::Alpha);

            void drawRotatedRect(float cx, float cy, float w, float h,
                                 float rotationDeg, uint32_t rgba, int layer = 0,
                                 BlendMode blend = BlendMode::Alpha);

            /**
             * Fill a rounded rectangle with mathematically-perfect AA.
             *
             * Submits a single quad whose fragments are shaded by an SDF
             * evaluating the rounded-rect distance — fill alpha is a
             * smoothstep over a 1-pixel band, so the silhouette stays
             * crisp at every radius (4px or 64px) without the corner-
             * sprite halo the old composite path produced.
             *
             * `rTL/rTR/rBR/rBL` are clamped to half the shorter dimension.
            */
            void drawRoundedRect(float x, float y, float w, float h,
                                 float rTL, float rTR, float rBR, float rBL,
                                 uint32_t rgba, int layer = 0,
                                 BlendMode blend = BlendMode::Alpha);

            /**
             * Fill + stroke a rounded rectangle in one quad.
             *
             * The fragment shader produces both the fill (interior) and
             * the border (a ring of width `borderWidth` along the SDF's
             * zero-isoline) in a single pass, anti-aliased identically.
             * If `borderWidth <= 0` this is equivalent to drawRoundedRect.
             *
             * `fillColor` may be transparent (alpha=0) to draw an
             * outline-only rect — the interior won't contribute to the
             * fragment alpha at all.
            */
            void drawRoundedRectWithBorder(float x, float y, float w, float h,
                                           float rTL, float rTR, float rBR, float rBL,
                                           uint32_t fillColor,
                                           float borderWidth,
                                           uint32_t borderColor,
                                           int layer = 0,
                                           BlendMode blend = BlendMode::Alpha);

            /**
             * Push a scissor rect. All subsequent draw calls have their
             * pixels clipped to the intersection of the previously-active
             * rect (if any) and the new one. Pair every push with a pop;
             * the stack is limited to 16 levels.
             *
             * `y` is in top-left pixel space (matching layout). flush()
             * converts to GL's bottom-left scissor convention.
            */
            void pushScissor(int x, int y, int w, int h);
            void popScissor();

            void flush();

            struct Stats
            {
                int quadCount = 0;
                int drawCallCount = 0;
                int textureBindCount = 0;
                int blendSwitchCount = 0;
                int scissorSwitchCount = 0;
                int normalBindCount = 0;      // additional bindings for MRT
            };
            const Stats &lastFrameStats() const { return m_stats; }

        private:
            struct Vertex
            {
                /* Geometry */
                float x, y;
                float u, v;
                float r, g, b, a;
                /* SDF (only meaningful in RoundedRect mode; ignored
                 * otherwise — vertex attribs are cheap and constant
                 * fields don't cost anything in the shader's hot
                 * path because branches on `shape` short-circuit). */
                float rectCx, rectCy;
                float rectHalfW, rectHalfH;
                float radTL, radTR, radBR, radBL;
                float borderR, borderG, borderB, borderA;
                /* borderW + shape are read as a contiguous vec2 by the
                 * vertex shader's a_borderShape attribute. They MUST be
                 * adjacent here — splitting them would silently corrupt
                 * the shape-mode dispatch (shape would read borderR
                 * instead of itself, and every glyph would render as a
                 * black silhouette rather than an alpha-masked glyph).
                 *
                 * The `shape` float carries the ShapeMode in its low
                 * bits and the emissive flag in bit 7 (value 128). This
                 * packing keeps the vertex layout compact while carrying
                 * the MRT emissive signal. The shader unpacks via integer
                 * arithmetic on the rounded float. */
                float borderW;
                float shape;
            };

            std::vector<Quad> m_quads;
            std::vector<Vertex> m_verts;
            std::vector<uint32_t> m_indices;

            Shader m_shader;
            Texture2D m_whiteTex;
            // Default normal texture sampled when a quad has no authored
            // normal map. A 1x1 RGBA8 holding (128, 128, 255, 255) which
            // decodes in the shader to the tangent-space up-vector
            // (0, 0, 1). Created once at init() and bound at texture
            // unit 1 for every batch lacking a custom normal map.
            Texture2D m_defaultNormalTex;
            GLuint m_vao = 0;
            GLuint m_vbo = 0;
            GLuint m_ibo = 0;

            int m_viewportW = 0;
            int m_viewportH = 0;
            int m_projW = 0;
            int m_projH = 0;
            Stats m_stats;

            static constexpr int k_bufferSegments = 3;
            int m_bufferSegment = 0;
            size_t m_vboSegmentSize = 0;
            size_t m_iboSegmentSize = 0;
            size_t m_vboCapacityVerts = 0;
            size_t m_iboCapacityInds = 0;

            /* Scissor stack — push/pop track current state; quads
             * remember the active scissor at submission time. */
            std::vector<ScissorRect> m_scissorStack;

            void issueBatch(const Texture2D *tex, BlendMode blend,
                            size_t firstQuad, size_t count);
            void ensureBufferCapacity(size_t totalVerts, size_t totalIndices);
            void applyBlendMode(BlendMode mode);
            void applyScissor(const ScissorRect &rect);
        };

    }
}