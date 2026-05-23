#include "Image.hpp"
#include "../Drawing.hpp"

#include <algorithm>
#include <cmath>

namespace Bokken
{
    namespace Canvas
    {
        namespace Components
        {
            std::shared_ptr<Node> Image::toNode()
            {
                auto node = std::make_shared<Node>("Image");
                node->imageSource = m_source;
                node->style = m_style;
                node->onCompute = &computeNode;
                return node;
            }

            void Image::computeNode(std::shared_ptr<Node> node, AssetPack *assets)
            {
                const auto &s = node->style;
                const float pT = resolveSide(s.paddingTop,    s.padding);
                const float pB = resolveSide(s.paddingBottom, s.padding);
                const float pL = resolveSide(s.paddingLeft,   s.padding);
                const float pR = resolveSide(s.paddingRight,  s.padding);

                /* If we don't have a TextureCache wired up yet, fall
                 * back to whatever Layout passed in — this keeps the
                 * tree measurable during the brief gap before the
                 * Renderer module hands us its cache. */
                if (!s_textureCache || node->imageSource.empty() || !assets)
                {
                    if (s.width <= 0.0f && !s.widthIsPercent)
                        node->layout.w = pL + pR;
                    if (s.height <= 0.0f && !s.heightIsPercent)
                        node->layout.h = pT + pB;
                    return;
                }

                const Renderer::Texture2D *tex = s_textureCache->load(node->imageSource, assets);
                if (!tex || !tex->isValid())
                {
                    /* Missing assets — measure as a 1×1 placeholder so
                     * the rest of the layout doesn't collapse. */
                    if (s.width <= 0.0f && !s.widthIsPercent)
                        node->layout.w = 1.0f + pL + pR;
                    if (s.height <= 0.0f && !s.heightIsPercent)
                        node->layout.h = 1.0f + pT + pB;
                    return;
                }

                const float natW = (float)tex->width();
                const float natH = (float)tex->height();

                /* Resolve any explicit dimensions (or percent). One-axis-
                 * fixed fills the other from aspect. Both unset = native. */
                bool wExplicit = (s.width > 0.0f) || s.widthIsPercent;
                bool hExplicit = (s.height > 0.0f) || s.heightIsPercent;

                if (!wExplicit && !hExplicit)
                {
                    node->intrinsicW = natW;
                    node->intrinsicH = natH;
                    node->layout.w = natW + pL + pR;
                    node->layout.h = natH + pT + pB;
                }
                else if (wExplicit && !hExplicit)
                {
                    /* Derive height from aspect, given the resolved
                     * width — for percent widths Layout::measure sets
                     * layout.w before us; for fixed widths it's s.width. */
                    float resolvedW = s.widthIsPercent ? node->layout.w : s.width;
                    if (resolvedW <= 0.0f) resolvedW = node->layout.w;
                    float aspect = natW > 0.0f ? natH / natW : 1.0f;
                    float resolvedH = (resolvedW - pL - pR) * aspect;
                    node->intrinsicW = resolvedW;
                    node->intrinsicH = resolvedH + pT + pB;
                    node->layout.h = resolvedH + pT + pB;
                }
                else if (!wExplicit && hExplicit)
                {
                    float resolvedH = s.heightIsPercent ? node->layout.h : s.height;
                    if (resolvedH <= 0.0f) resolvedH = node->layout.h;
                    float aspect = natH > 0.0f ? natW / natH : 1.0f;
                    float resolvedW = (resolvedH - pT - pB) * aspect;
                    node->intrinsicW = resolvedW + pL + pR;
                    node->intrinsicH = resolvedH;
                    node->layout.w = resolvedW + pL + pR;
                }
                else
                {
                    /* Both explicit — image stretches. */
                    node->intrinsicW = node->layout.w;
                    node->intrinsicH = node->layout.h;
                }
            }

            void Image::draw(Renderer::SpriteBatcher &batcher,
                             std::shared_ptr<Node> node,
                             AssetPack *assets,
                             int layer)
            {
                if (!node || !s_textureCache) return;

                const auto &s = node->style;
                const float pT = resolveSide(s.paddingTop, s.padding);
                const float pB = resolveSide(s.paddingBottom, s.padding);
                const float pL = resolveSide(s.paddingLeft, s.padding);
                const float pR = resolveSide(s.paddingRight, s.padding);

                float x = node->layout.x + pL + s.translateX;
                float y = node->layout.y + pT + s.translateY;
                float w = std::max(0.0f, node->layout.w - pL - pR);
                float h = std::max(0.0f, node->layout.h - pT - pB);
                if (w <= 0.0f || h <= 0.0f) return;

                /* Apply scale around center, including animation scale. */
                const float sx = node->visualScale * s.scaleX;
                const float sy = node->visualScale * s.scaleY;
                if (sx != 1.0f || sy != 1.0f)
                {
                    const float cx = x + w * 0.5f;
                    const float cy = y + h * 0.5f;
                    w *= sx;
                    h *= sy;
                    x = cx - w * 0.5f;
                    y = cy - h * 0.5f;
                }

                const float opacity = node->getGlobalOpacity()
                                    * (s.disabled ? 0.5f : 1.0f);
                if (opacity <= 0.001f) return;

                const Renderer::Texture2D *tex = s_textureCache->load(node->imageSource, assets);
                if (!tex || !tex->isValid()) return;

                const Drawing::Corners corners = Drawing::resolveCorners(s, w, h);
                /* color acts as a multiplicative tint on the image. The
                 * default 0xFFFFFFFF leaves it unchanged. */
                const uint32_t tint = Drawing::applyTint(s.color, opacity);

                Drawing::drawImage(batcher, tex, x, y, w, h, corners, tint, layer);
            }
        }
    }
}
