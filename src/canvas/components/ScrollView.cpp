#include "ScrollView.hpp"
#include "View.hpp"
#include "../Drawing.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace Bokken
{
    namespace Canvas
    {
        namespace Components
        {
            namespace
            {
                /* Drag state for a held scrollbar thumb. Keyed on Node*
                 * so multiple ScrollViews can keep their own state
                 * independently. */
                struct DragState
                {
                    /* 0 = not dragging, 1 = vertical thumb, 2 = horizontal thumb */
                    int axis = 0;
                    /* Pointer Y/X at drag start, in screen coords. */
                    float startMouse = 0.0f;
                    /* scrollX/Y at drag start. */
                    float startScroll = 0.0f;
                };
                std::map<Node *, DragState> s_dragMap;

                /* Currently-dragging ScrollView (weak — Canvas's call
                 * site holds the shared_ptr). */
                std::weak_ptr<Node> s_draggingNode;

                /* Hover tracking for the thumb. 0 = none, 1 = v, 2 = h. */
                std::map<Node *, int> s_hoverThumbMap;

                /* Tunables matching Discord's slim-scrollbar feel. */
                constexpr float kBarThickness  = 8.0f;
                constexpr float kBarGap        = 4.0f;     // gap from edge
                constexpr float kBarMinThumb   = 30.0f;    // min thumb size in px
                constexpr float kBarTrackPad   = 2.0f;     // inset thumb inside track
                constexpr float kLineHeight    = 30.0f;    // wheel "1 line" in px

                /* Compute the layout rectangles for the vertical and
                 * horizontal scrollbars in screen coords. */
                struct BarLayout
                {
                    bool vVisible = false;
                    bool hVisible = false;
                    float vTrackX = 0, vTrackY = 0, vTrackW = 0, vTrackH = 0;
                    float vThumbY = 0, vThumbH = 0;
                    float hTrackX = 0, hTrackY = 0, hTrackW = 0, hTrackH = 0;
                    float hThumbX = 0, hThumbW = 0;
                };

                BarLayout computeBars(const std::shared_ptr<Node> &node)
                {
                    BarLayout bl;
                    if (!node) return bl;
                    const auto &s = node->style;
                    /* Padding is read for content-vs-box deltas used in
                     * the proportion math below — track itself spans the
                     * full box height (corner to corner) for a clean
                     * native-scrollbar feel. */
                    const float pT = resolveSide(s.paddingTop,    s.padding);
                    const float pB = resolveSide(s.paddingBottom, s.padding);
                    const float pL = resolveSide(s.paddingLeft,   s.padding);
                    const float pR = resolveSide(s.paddingRight,  s.padding);

                    const float boxX = node->layout.x;
                    const float boxY = node->layout.y;
                    const float boxW = node->layout.w;
                    const float boxH = node->layout.h;
                    const float contentH = std::max(0.0f, boxH - pT - pB);
                    const float contentW = std::max(0.0f, boxW - pL - pR);

                    /* Inset the track from the corners by a small amount
                     * so the thumb doesn't touch the rounded box edges
                     * — looks cleaner and matches macOS overlay
                     * scrollbars. The track itself still runs corner-
                     * to-corner conceptually for hit-testing track
                     * clicks; only the thumb's travel range is inset. */
                    constexpr float kBarEndPad = 6.0f;

                    if (node->scrollMaxY > 0.5f && contentH > 0.0f)
                    {
                        bl.vVisible = true;
                        bl.vTrackX = boxX + boxW - kBarThickness - kBarGap;
                        bl.vTrackY = boxY + kBarEndPad;
                        bl.vTrackW = kBarThickness;
                        bl.vTrackH = std::max(0.0f, boxH - kBarEndPad * 2.0f);

                        float ratio = contentH / (contentH + node->scrollMaxY);
                        bl.vThumbH = std::max(kBarMinThumb, bl.vTrackH * ratio);
                        bl.vThumbH = std::min(bl.vThumbH, bl.vTrackH);
                        const float scrollableTrack = bl.vTrackH - bl.vThumbH;
                        const float scrollProgress = node->scrollMaxY > 0.0f
                            ? std::clamp(node->scrollY / node->scrollMaxY, 0.0f, 1.0f)
                            : 0.0f;
                        bl.vThumbY = bl.vTrackY + scrollableTrack * scrollProgress;
                    }

                    if (node->scrollMaxX > 0.5f && contentW > 0.0f)
                    {
                        bl.hVisible = true;
                        bl.hTrackX = boxX + kBarEndPad;
                        bl.hTrackY = boxY + boxH - kBarThickness - kBarGap;
                        bl.hTrackW = std::max(0.0f, boxW - kBarEndPad * 2.0f);
                        bl.hTrackH = kBarThickness;

                        float ratio = contentW / (contentW + node->scrollMaxX);
                        bl.hThumbW = std::max(kBarMinThumb, bl.hTrackW * ratio);
                        bl.hThumbW = std::min(bl.hThumbW, bl.hTrackW);
                        const float scrollableTrack = bl.hTrackW - bl.hThumbW;
                        const float scrollProgress = node->scrollMaxX > 0.0f
                            ? std::clamp(node->scrollX / node->scrollMaxX, 0.0f, 1.0f)
                            : 0.0f;
                        bl.hThumbX = bl.hTrackX + scrollableTrack * scrollProgress;
                    }

                    return bl;
                }
            }

            std::shared_ptr<Node> ScrollView::toNode()
            {
                auto node = std::make_shared<Node>("ScrollView");
                node->style = m_style;
                node->style.overflow = Overflow::Hidden;
                node->onLayout = &layoutNode;
                node->onDeconstruct = [n = node.get()]() {
                    s_dragMap.erase(n);
                    s_hoverThumbMap.erase(n);
                };
                return node;
            }

            void ScrollView::layoutNode(std::shared_ptr<Node> node)
            {
                if (!node) return;
                const auto &s = node->style;
                const float pT = resolveSide(s.paddingTop,    s.padding);
                const float pB = resolveSide(s.paddingBottom, s.padding);
                const float pL = resolveSide(s.paddingLeft,   s.padding);
                const float pR = resolveSide(s.paddingRight,  s.padding);

                const float contentW = std::max(0.0f, node->layout.w - pL - pR);
                const float contentH = std::max(0.0f, node->layout.h - pT - pB);

                /* Union bounds of in-flow children. */
                float maxRight  = node->layout.x + pL;
                float maxBottom = node->layout.y + pT;
                for (auto &c : node->children)
                {
                    if (c->style.position == Position::Absolute) continue;
                    maxRight  = std::max(maxRight,  c->layout.x + c->layout.w);
                    maxBottom = std::max(maxBottom, c->layout.y + c->layout.h);
                }

                const float contentRight  = node->layout.x + pL + contentW;
                const float contentBottom = node->layout.y + pT + contentH;
                node->scrollMaxX = std::max(0.0f, maxRight  - contentRight);
                node->scrollMaxY = std::max(0.0f, maxBottom - contentBottom);

                node->scrollX = std::clamp(node->scrollX, 0.0f, node->scrollMaxX);
                node->scrollY = std::clamp(node->scrollY, 0.0f, node->scrollMaxY);
            }

            void ScrollView::onWheel(std::shared_ptr<Node> node, float dx, float dy)
            {
                if (!node) return;
                node->scrollX = std::clamp(node->scrollX + dx * kLineHeight, 0.0f, node->scrollMaxX);
                node->scrollY = std::clamp(node->scrollY - dy * kLineHeight, 0.0f, node->scrollMaxY);
                if (node->onScroll)
                    node->onScroll(node->scrollX, node->scrollY);
            }

            int ScrollView::hitTestThumb(std::shared_ptr<Node> node, float mx, float my)
            {
                if (!node) return 0;
                BarLayout bl = computeBars(node);
                if (bl.vVisible)
                {
                    const float thumbX = bl.vTrackX + kBarTrackPad;
                    const float thumbW = bl.vTrackW - kBarTrackPad * 2.0f;
                    if (mx >= thumbX && mx <= thumbX + thumbW &&
                        my >= bl.vThumbY && my <= bl.vThumbY + bl.vThumbH)
                        return 1;
                }
                if (bl.hVisible)
                {
                    const float thumbY = bl.hTrackY + kBarTrackPad;
                    const float thumbH = bl.hTrackH - kBarTrackPad * 2.0f;
                    if (my >= thumbY && my <= thumbY + thumbH &&
                        mx >= bl.hThumbX && mx <= bl.hThumbX + bl.hThumbW)
                        return 2;
                }
                return 0;
            }

            bool ScrollView::onMouseDown(std::shared_ptr<Node> node, float mx, float my)
            {
                if (!node) return false;
                int axis = hitTestThumb(node, mx, my);
                if (axis == 0)
                {
                    /* Click on empty track area: jump-scroll one page. */
                    BarLayout bl = computeBars(node);
                    if (bl.vVisible &&
                        mx >= bl.vTrackX && mx <= bl.vTrackX + bl.vTrackW &&
                        my >= bl.vTrackY && my <= bl.vTrackY + bl.vTrackH)
                    {
                        const float pageDelta = bl.vTrackH * 0.85f;
                        const float dir = (my < bl.vThumbY) ? -1.0f : 1.0f;
                        node->scrollY = std::clamp(node->scrollY + dir * pageDelta,
                                                    0.0f, node->scrollMaxY);
                        if (node->onScroll) node->onScroll(node->scrollX, node->scrollY);
                        return true;
                    }
                    if (bl.hVisible &&
                        mx >= bl.hTrackX && mx <= bl.hTrackX + bl.hTrackW &&
                        my >= bl.hTrackY && my <= bl.hTrackY + bl.hTrackH)
                    {
                        const float pageDelta = bl.hTrackW * 0.85f;
                        const float dir = (mx < bl.hThumbX) ? -1.0f : 1.0f;
                        node->scrollX = std::clamp(node->scrollX + dir * pageDelta,
                                                    0.0f, node->scrollMaxX);
                        if (node->onScroll) node->onScroll(node->scrollX, node->scrollY);
                        return true;
                    }
                    return false;
                }

                /* Started a thumb drag. */
                DragState d;
                d.axis = axis;
                d.startMouse  = (axis == 1) ? my : mx;
                d.startScroll = (axis == 1) ? node->scrollY : node->scrollX;
                s_dragMap[node.get()] = d;
                s_draggingNode = node;
                return true;
            }

            void ScrollView::onMouseMove(std::shared_ptr<Node> node, float mx, float my)
            {
                if (!node) return;

                /* Track hover regardless of drag, so the thumb can
                 * darken on plain hover. */
                int axis = hitTestThumb(node, mx, my);
                s_hoverThumbMap[node.get()] = axis;

                auto it = s_dragMap.find(node.get());
                if (it == s_dragMap.end() || it->second.axis == 0) return;
                const DragState &d = it->second;

                BarLayout bl = computeBars(node);
                if (d.axis == 1 && bl.vVisible)
                {
                    /* Scroll delta = pointer delta * scrollMax / scrollableTrack.
                     * scrollableTrack is the range the thumb can move. */
                    const float scrollableTrack = std::max(1.0f, bl.vTrackH - bl.vThumbH);
                    const float pixDelta = my - d.startMouse;
                    const float scrollDelta = pixDelta * (node->scrollMaxY / scrollableTrack);
                    node->scrollY = std::clamp(d.startScroll + scrollDelta, 0.0f, node->scrollMaxY);
                    if (node->onScroll) node->onScroll(node->scrollX, node->scrollY);
                }
                else if (d.axis == 2 && bl.hVisible)
                {
                    const float scrollableTrack = std::max(1.0f, bl.hTrackW - bl.hThumbW);
                    const float pixDelta = mx - d.startMouse;
                    const float scrollDelta = pixDelta * (node->scrollMaxX / scrollableTrack);
                    node->scrollX = std::clamp(d.startScroll + scrollDelta, 0.0f, node->scrollMaxX);
                    if (node->onScroll) node->onScroll(node->scrollX, node->scrollY);
                }
            }

            void ScrollView::onMouseUp(std::shared_ptr<Node> node)
            {
                if (!node) return;
                auto it = s_dragMap.find(node.get());
                if (it != s_dragMap.end()) it->second.axis = 0;
                s_draggingNode.reset();
            }

            bool ScrollView::isDragging()
            {
                return !s_draggingNode.expired();
            }
            std::shared_ptr<Node> ScrollView::draggingNode()
            {
                return s_draggingNode.lock();
            }

            void ScrollView::draw(Renderer::SpriteBatcher &batcher,
                                   std::shared_ptr<Node> node, int layer)
            {
                /* Background + border + shadow via View::draw. */
                View::draw(batcher, node, layer);

                if (node->scrollMaxY <= 0.5f && node->scrollMaxX <= 0.5f) return;

                BarLayout bl = computeBars(node);
                const float globalOpacity = node->getGlobalOpacity();

                /* Discord-style colors. The thumb is a dark translucent
                 * pill that floats over content with no track. We pick
                 * the alpha based on (idle / hover / dragging). */
                auto thumbState = [&](int axis) -> uint32_t {
                    int hover = 0;
                    auto hit = s_hoverThumbMap.find(node.get());
                    if (hit != s_hoverThumbMap.end()) hover = hit->second;
                    int drag = 0;
                    auto dit = s_dragMap.find(node.get());
                    if (dit != s_dragMap.end()) drag = dit->second.axis;

                    uint32_t base;
                    if (drag == axis)        base = 0x1A1B1EE6u;  // dragging
                    else if (hover == axis)  base = 0x1A1B1ECCu;  // hovered
                    else                     base = 0x1A1B1E80u;  // idle
                    return Drawing::applyTint(base, globalOpacity);
                };

                if (bl.vVisible)
                {
                    const float thumbX = bl.vTrackX + kBarTrackPad;
                    const float thumbW = bl.vTrackW - kBarTrackPad * 2.0f;
                    Drawing::Corners cs;
                    cs.tl = cs.tr = cs.bl = cs.br = thumbW * 0.5f;
                    /* Layer offset must beat anything the ScrollView's
                     * children draw at. Canvas's drawNode walker bumps
                     * `childLayer` by kLayerStep (=4) when descending,
                     * and grandchildren add another step on top — so a
                     * +100 bump trivially clears any reasonable nesting
                     * depth. Without this the scrollbar sits BEHIND the
                     * ScrollView's own children and you can't see it. */
                    Drawing::fillRoundedRect(batcher,
                                              thumbX, bl.vThumbY,
                                              thumbW, bl.vThumbH,
                                              cs, thumbState(1), layer + 100);
                }
                if (bl.hVisible)
                {
                    const float thumbY = bl.hTrackY + kBarTrackPad;
                    const float thumbH = bl.hTrackH - kBarTrackPad * 2.0f;
                    Drawing::Corners cs;
                    cs.tl = cs.tr = cs.bl = cs.br = thumbH * 0.5f;
                    Drawing::fillRoundedRect(batcher,
                                              bl.hThumbX, thumbY,
                                              bl.hThumbW, thumbH,
                                              cs, thumbState(2), layer + 100);
                }
            }
        }
    }
}
