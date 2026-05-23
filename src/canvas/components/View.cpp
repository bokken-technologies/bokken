#include "View.hpp"
#include "../Drawing.hpp"

#include <cmath>

namespace Bokken
{
    namespace Canvas
    {
        namespace Components
        {
            std::shared_ptr<Node> View::toNode()
            {
                /* No onCompute/onLayout callbacks: the Canvas::Layout
                 * engine handles all flex measurement and arrangement
                 * for plain Views. Components that need bespoke layout
                 * (Label, ScrollView) install their own. */
                auto node = std::make_shared<Node>("View");
                node->style = m_style;
                return node;
            }

            namespace
            {
                /* Pick whichever color the user wants in this state. The
                 * "0 alpha == not set" rule lets a hoverColor of
                 * 0x00000000 mean "leave the base alone" rather than
                 * "go fully transparent". Same convention CSS-in-JS
                 * libraries adopt; see notes on hoverColor in the SSS. */
                uint32_t pickBackground(const SimpleStyleSheet &s, bool hovered, bool active)
                {
                    if (active && (s.activeBackgroundColor & 0xFFu))
                        return s.activeBackgroundColor;
                    if (hovered && (s.hoverBackgroundColor & 0xFFu))
                        return s.hoverBackgroundColor;
                    return s.backgroundColor;
                }
            }

            void View::draw(Renderer::SpriteBatcher &batcher,
                            std::shared_ptr<Node> node, int layer)
            {
                if (!node) return;

                const auto &s = node->style;
                float x = node->layout.x;
                float y = node->layout.y;
                float w = node->layout.w;
                float h = node->layout.h;
                if (w <= 0.0f || h <= 0.0f) return;

                /* Compose transform onto the node rect
                 *
                 * Order matches CSS transform-origin:center:
                 *   1. translate
                 *   2. scale around center
                 *   3. rotation around center is handled per-quad via
                 *      drawRotatedRect — but only if rotation != 0 AND
                 *      the silhouette is a plain rect (no corners). For
                 *      rounded boxes we'd need to rotate every emitted
                 *      sub-quad's local frame, which is expensive and
                 *      visually rare; we leave that as a TODO and only
                 *      apply rotation to the simple-rect path. */
                x += s.translateX;
                y += s.translateY;

                /* hover/active animation scale composes with the SSS
                 * scaleX/scaleY transform. Both are applied around the
                 * box center so they don't move the node off its layout
                 * spot. */
                const float scale = node->visualScale;
                const float sx = scale * s.scaleX;
                const float sy = scale * s.scaleY;
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

                /* Resolve corners and per-side borders */
                const Drawing::Corners corners = Drawing::resolveCorners(s, w, h);

                const float borderWidths[4] = {
                    resolveBorderWidth(s.borderTopWidth,    s.borderWidth),
                    resolveBorderWidth(s.borderRightWidth,  s.borderWidth),
                    resolveBorderWidth(s.borderBottomWidth, s.borderWidth),
                    resolveBorderWidth(s.borderLeftWidth,   s.borderWidth),
                };
                const uint32_t borderColors[4] = {
                    Drawing::applyTint(resolveBorderColor(s.borderTopColor,    s.borderColor), opacity),
                    Drawing::applyTint(resolveBorderColor(s.borderRightColor,  s.borderColor), opacity),
                    Drawing::applyTint(resolveBorderColor(s.borderBottomColor, s.borderColor), opacity),
                    Drawing::applyTint(resolveBorderColor(s.borderLeftColor,   s.borderColor), opacity),
                };
                const uint32_t fallbackBorder = Drawing::applyTint(s.borderColor, opacity);

                /* Drop shadow (layer-1) */
                if ((s.shadowColor & 0xFFu) > 0u &&
                    (s.shadowBlur > 0.0f || s.shadowOffsetX != 0.0f || s.shadowOffsetY != 0.0f))
                {
                    const uint32_t shadowRgba = Drawing::applyTint(s.shadowColor, opacity);
                    Drawing::dropShadow(batcher, x, y, w, h, corners,
                                        s.shadowOffsetX, s.shadowOffsetY,
                                        s.shadowBlur, shadowRgba, layer);
                }

                /* Background fill / gradient / image */
                const uint32_t bgRaw = pickBackground(s, node->isHovered, node->isActive);
                const uint32_t bg = Drawing::applyTint(bgRaw, opacity);

                const bool hasGradient = (s.gradientStart & 0xFFu) > 0u
                                       || (s.gradientEnd   & 0xFFu) > 0u;
                if (hasGradient)
                {
                    Drawing::fillRoundedGradient(batcher, x, y, w, h, corners,
                                                  Drawing::applyTint(s.gradientStart, opacity),
                                                  Drawing::applyTint(s.gradientEnd,   opacity),
                                                  s.gradientAngle, layer);
                }
                else if ((bg & 0xFFu) > 0u)
                {
                    /* Special-case: rotated solid-color rect with no
                     * corners can use drawRotatedRect for crisp results
                     * with no extra geometry. Anything else goes through
                     * fillRoundedRect (which falls through to drawRect
                     * when there are no corners). */
                    if (s.rotation != 0.0f && !corners.any())
                    {
                        const float cx = x + w * 0.5f;
                        const float cy = y + h * 0.5f;
                        batcher.drawRotatedRect(cx, cy, w, h, s.rotation, bg, layer);
                    }
                    else
                    {
                        Drawing::fillRoundedRect(batcher, x, y, w, h, corners, bg, layer);
                    }
                }

                /* Border stroke */
                const bool anyBorder =
                    (borderWidths[0] > 0.0f && (borderColors[0] & 0xFFu)) ||
                    (borderWidths[1] > 0.0f && (borderColors[1] & 0xFFu)) ||
                    (borderWidths[2] > 0.0f && (borderColors[2] & 0xFFu)) ||
                    (borderWidths[3] > 0.0f && (borderColors[3] & 0xFFu));
                if (anyBorder)
                {
                    Drawing::strokeRoundedBorder(batcher, x, y, w, h, corners,
                                                  borderWidths, borderColors,
                                                  fallbackBorder, layer + 1);
                }
            }
        }
    }
}
