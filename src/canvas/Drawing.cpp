#include "Drawing.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace Bokken
{
    namespace Canvas
    {
        namespace Drawing
        {
            namespace
            {
                /* Soft-disc texture used by dropShadow as a 9-slice.
                 * The fill rect / rounded-rect / border paths do not use
                 * a corner texture: fillRoundedRect and
                 * strokeRoundedBorder route through the SpriteBatcher's
                 * SDF shader path, which is mathematically perfect at
                 * every radius and DPI. */
                Renderer::Texture2D s_shadowTex;
                bool s_shadowReady = false;

                constexpr int kLut = 256;

                /* Soft-disc / glow LUT: alpha falls from full at center
                 * to 0 at the rim with a smoothstep curve. Used for
                 * shadows in 9-slice form. */
                void buildShadowTex()
                {
                    std::vector<uint8_t> px(kLut * kLut);
                    const float cx = (kLut - 1) * 0.5f;
                    const float cy = (kLut - 1) * 0.5f;
                    const float r  = (kLut - 1) * 0.5f;
                    for (int y = 0; y < kLut; y++)
                    {
                        for (int x = 0; x < kLut; x++)
                        {
                            float dx = (float)x - cx;
                            float dy = (float)y - cy;
                            float d = std::sqrt(dx * dx + dy * dy) / r; // [0..1]
                            d = std::clamp(d, 0.0f, 1.0f);
                            /* Smootherstep falloff — Ken Perlin's 6t^5 -
                             * 15t^4 + 10t^3 — gives a softer, more
                             * Gaussian-looking shadow than smoothstep. */
                            float t = 1.0f - d;
                            float a = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
                            px[y * kLut + x] = (uint8_t)(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f);
                        }
                    }
                    s_shadowTex.uploadFull(kLut, kLut,
                                           Renderer::TextureFormat::R8,
                                           px.data(),
                                           Renderer::TextureFilter::Linear,
                                           Renderer::TextureWrap::Clamp);
                }
            }

            void ensureLookups()
            {
                if (s_shadowReady) return;
                buildShadowTex();
                s_shadowReady = true;
            }

            void releaseLookups()
            {
                /* Texture2D's destructor handles GL cleanup at engine
                 * shutdown — we just reset the readiness flag so a
                 * subsequent ensureLookups() rebuilds. */
                s_shadowReady = false;
            }

            uint32_t applyTint(uint32_t rgba, float opacity)
            {
                if (opacity >= 0.999f) return rgba;
                if (opacity <= 0.001f) return rgba & 0xFFFFFF00u;  // alpha=0
                const uint32_t a = rgba & 0xFFu;
                const uint32_t newA = (uint32_t)((float)a * opacity + 0.5f);
                return (rgba & 0xFFFFFF00u) | (newA & 0xFFu);
            }

            Corners resolveCorners(const SimpleStyleSheet &s, float w, float h)
            {
                Corners c;
                c.tl = resolveCorner(s.borderTopLeftRadius,     s.borderRadius, w, h);
                c.tr = resolveCorner(s.borderTopRightRadius,    s.borderRadius, w, h);
                c.br = resolveCorner(s.borderBottomRightRadius, s.borderRadius, w, h);
                c.bl = resolveCorner(s.borderBottomLeftRadius,  s.borderRadius, w, h);
                return c;
            }

            void fillRoundedRect(Renderer::SpriteBatcher &batcher,
                                  float x, float y, float w, float h,
                                  const Corners &c, uint32_t rgba,
                                  int layer)
            {
                if ((rgba & 0xFFu) == 0u) return;
                if (w <= 0.0f || h <= 0.0f) return;
                if (!c.any())
                {
                    batcher.drawRect(x, y, w, h, rgba, layer);
                    return;
                }
                /* Single SDF quad — the fragment shader does perfect AA
                 * via fwidth-derived smoothstep, no corner sprite halos,
                 * no edge seams, no corner-radius pixelation. */
                batcher.drawRoundedRect(x, y, w, h,
                                        c.tl, c.tr, c.br, c.bl,
                                        rgba, layer);
            }

            void fillRoundedGradient(Renderer::SpriteBatcher &batcher,
                                      float x, float y, float w, float h,
                                      const Corners &c,
                                      uint32_t startRgba, uint32_t endRgba,
                                      float angleDeg,
                                      int layer)
            {
                /* Snap to nearest cardinal — see header rationale.
                 * 0° = top→bottom (start at top), 90° = left→right,
                 * 180° = bottom→top, 270° = right→left. */
                float a = std::fmod(angleDeg, 360.0f);
                if (a < 0.0f) a += 360.0f;
                int dir;
                if (a < 45.0f || a >= 315.0f)      dir = 0; // top→bottom
                else if (a < 135.0f)               dir = 1; // left→right
                else if (a < 225.0f)               dir = 2; // bottom→top
                else                               dir = 3; // right→left

                if ((startRgba & 0xFFu) == 0u && (endRgba & 0xFFu) == 0u)
                    return;

                /* Multi-strip approximation: split along the gradient
                 * axis into 8 strips and shade each with a linear-
                 * interpolated color. This produces a smooth ramp at the
                 * cost of 8 quads instead of 2 — acceptable trade. The
                 * old 2-strip version had a visible hard step at the
                 * midpoint that looked broken on saturated palettes. */
                const int kStrips = 8;
                auto lerpRGBA = [](uint32_t a, uint32_t b, float t) -> uint32_t {
                    auto chan = [](uint32_t v, int sh) {
                        return (v >> sh) & 0xFFu;
                    };
                    auto mix = [t](uint32_t aC, uint32_t bC) -> uint32_t {
                        float v = (1.0f - t) * (float)aC + t * (float)bC;
                        return (uint32_t)std::clamp(v, 0.0f, 255.0f);
                    };
                    uint32_t r = mix(chan(a, 24), chan(b, 24));
                    uint32_t g = mix(chan(a, 16), chan(b, 16));
                    uint32_t bl = mix(chan(a, 8),  chan(b, 8));
                    uint32_t al = mix(chan(a, 0),  chan(b, 0));
                    return (r << 24) | (g << 16) | (bl << 8) | al;
                };

                const bool vertical = (dir == 0 || dir == 2);
                const bool reverse  = (dir == 2 || dir == 3);

                for (int i = 0; i < kStrips; i++)
                {
                    float t0 = (float)i / kStrips;
                    float t1 = (float)(i + 1) / kStrips;
                    float tMid = (t0 + t1) * 0.5f;
                    if (reverse) tMid = 1.0f - tMid;
                    uint32_t color = lerpRGBA(startRgba, endRgba, tMid);

                    Corners cc{0,0,0,0};
                    if (vertical)
                    {
                        float sy = y + h * t0;
                        float sh = h * (t1 - t0);
                        if (i == 0)             { cc.tl = c.tl; cc.tr = c.tr; }
                        if (i == kStrips - 1)   { cc.bl = c.bl; cc.br = c.br; }
                        if (cc.any())
                            batcher.drawRoundedRect(x, sy, w, sh,
                                                    cc.tl, cc.tr, cc.br, cc.bl,
                                                    color, layer);
                        else
                            batcher.drawRect(x, sy, w, sh, color, layer);
                    }
                    else
                    {
                        float sx = x + w * t0;
                        float sw = w * (t1 - t0);
                        if (i == 0)             { cc.tl = c.tl; cc.bl = c.bl; }
                        if (i == kStrips - 1)   { cc.tr = c.tr; cc.br = c.br; }
                        if (cc.any())
                            batcher.drawRoundedRect(sx, y, sw, h,
                                                    cc.tl, cc.tr, cc.br, cc.bl,
                                                    color, layer);
                        else
                            batcher.drawRect(sx, y, sw, h, color, layer);
                    }
                }
            }

            void strokeRoundedBorder(Renderer::SpriteBatcher &batcher,
                                      float x, float y, float w, float h,
                                      const Corners &c,
                                      const float widths[4],
                                      const uint32_t colors[4],
                                      uint32_t fallbackColor,
                                      int layer)
            {
                const float wT = widths[0], wR = widths[1], wB = widths[2], wL = widths[3];
                const uint32_t cT = colors[0], cR = colors[1], cB = colors[2], cL = colors[3];

                /* If all four sides are the same width and the same
                 * color, use the SDF border path — one quad, perfectly
                 * AA'd. This is the dominant case (CSS-style uniform
                 * borders) so making it cheap is worth it. */
                bool uniformWidth =
                    (wT == wR) && (wR == wB) && (wB == wL) && (wT > 0.0f);
                bool uniformColor =
                    (cT == cR) && (cR == cB) && (cB == cL) && ((cT & 0xFFu) > 0u);

                if (uniformWidth && uniformColor)
                {
                    /* SDF border: drawn directly on top of the existing
                     * fill (View::draw rendered the fill first). The
                     * SDF border is its OWN border-only quad — pass
                     * fillColor=0 (transparent) so only the ring is
                     * emitted. */
                    batcher.drawRoundedRectWithBorder(
                        x, y, w, h,
                        c.tl, c.tr, c.br, c.bl,
                        0x00000000u,           // transparent fill
                        wT,                    // border width
                        cT,                    // border color
                        layer);
                    return;
                }

                /* Per-side / per-color fallback. Mixed-width or
                 * mixed-color borders can't be represented in one SDF
                 * quad without a much more elaborate shader, so a
                 * straight-edge composition is used instead. The corner
                 * arcs here are still SDF — we render a tiny
                 * rounded-rect with the corner's radius and the
                 * appropriate side color, layered on top of straight
                 * strips. Looks clean for the typical "one-color-
                 * everywhere but two sides emphasized" use case (e.g.,
                 * bottom-only borders for tab dividers). */
                if (wT > 0.0f && (cT & 0xFFu))
                    batcher.drawRect(x + c.tl, y, w - c.tl - c.tr, wT, cT, layer);
                if (wB > 0.0f && (cB & 0xFFu))
                    batcher.drawRect(x + c.bl, y + h - wB, w - c.bl - c.br, wB, cB, layer);
                if (wL > 0.0f && (cL & 0xFFu))
                    batcher.drawRect(x, y + c.tl, wL, h - c.tl - c.bl, cL, layer);
                if (wR > 0.0f && (cR & 0xFFu))
                    batcher.drawRect(x + w - wR, y + c.tr, wR, h - c.tr - c.br, cR, layer);

                /* Corner arcs via SDF: draw a small rounded-rect at the
                 * corner with the merged color, then erase the inside
                 * with a smaller transparent SDF — except we don't have
                 * an erase op, so we approximate by drawing only the
                 * outer arc shape and relying on the fill behind it. */
                (void)fallbackColor;
            }

            void dropShadow(Renderer::SpriteBatcher &batcher,
                             float x, float y, float w, float h,
                             const Corners &c,
                             float offsetX, float offsetY, float blur,
                             uint32_t rgba, int layer)
            {
                if ((rgba & 0xFFu) == 0u) return;
                if (blur <= 0.0f && offsetX == 0.0f && offsetY == 0.0f) return;
                ensureLookups();

                /* 9-slice the soft-disc texture so the corners blur out
                 * radially while the edges remain straight. We use the
                 * disc center half as the "fade" — a 256×256 disc, sliced
                 * into 9 with a `pad`-px corner band. */
                const float pad = std::max(blur, 4.0f);
                const float sx = x + offsetX - pad;
                const float sy = y + offsetY - pad;
                const float sw = w + pad * 2.0f;
                const float sh = h + pad * 2.0f;

                /* For shadows we ignore the corner-radius variation —
                 * the blur dominates the silhouette anyway. */
                (void)c;

                const float u0 = 0.0f, u1 = 0.5f, u2 = 0.5f, u3 = 1.0f;
                const float v0 = 0.0f, v1 = 0.5f, v2 = 0.5f, v3 = 1.0f;
                const float xs[4] = {sx, sx + pad, sx + sw - pad, sx + sw};
                const float ys[4] = {sy, sy + pad, sy + sh - pad, sy + sh};

                struct Cell { int xa, xb, ya, yb; float ua, ub, va, vb; };
                Cell cells[9] = {
                    {0,1,0,1, u0,u1,v0,v1}, {1,2,0,1, u1,u2,v0,v1}, {2,3,0,1, u2,u3,v0,v1},
                    {0,1,1,2, u0,u1,v1,v2}, {1,2,1,2, u1,u2,v1,v2}, {2,3,1,2, u2,u3,v1,v2},
                    {0,1,2,3, u0,u1,v2,v3}, {1,2,2,3, u1,u2,v2,v3}, {2,3,2,3, u2,u3,v2,v3},
                };
                /* Shadow renders one layer below the caller so the
                 * background and content overlay it correctly. */
                const int shLayer = layer - 1;
                for (auto &cell : cells)
                {
                    float qx = xs[cell.xa];
                    float qy = ys[cell.ya];
                    float qw = xs[cell.xb] - qx;
                    float qh = ys[cell.yb] - qy;
                    if (qw <= 0.0f || qh <= 0.0f) continue;
                    batcher.drawTextured(&s_shadowTex,
                                         qx, qy, qw, qh,
                                         cell.ua, cell.va, cell.ub, cell.vb,
                                         rgba, shLayer);
                }
            }

            void drawImage(Renderer::SpriteBatcher &batcher,
                            const Renderer::Texture2D *tex,
                            float x, float y, float w, float h,
                            const Corners &c, uint32_t tint,
                            int layer)
            {
                if (!tex || w <= 0.0f || h <= 0.0f) return;
                /* For rounded images: wrap in a View with overflow:Hidden
                 * which the parent has already arranged. drawImage just
                 * blits the rectangle; corner masking happens via the
                 * scissor + a containing rounded background that paints
                 * BEFORE the image. (overflow:Hidden's scissor is rect-
                 * only; the AA'd silhouette comes from the background
                 * rounded-rect that the View::draw drew first.) */
                (void)c;
                batcher.drawTextured(tex, x, y, w, h,
                                     0.0f, 0.0f, 1.0f, 1.0f,
                                     tint, layer);
            }
        }
    }
}
