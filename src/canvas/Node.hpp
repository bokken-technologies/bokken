#pragma once

#include "SimpleStyleSheet.hpp"
#include "Rect.hpp"
#include "Align.hpp"
#include "../AssetPack.hpp"
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace Bokken
{
    namespace Canvas
    {
        /**
         * Canvas tree node.
         *
         * A Node does not own an SDL_Texture: text rendering goes
         * through the engine-wide GlyphCache and batcher, so there are
         * no per-Label textures. The `needsRepaint` flag exists because
         * measurement benefits from caching; it does not guard a texture
         * upload.
        */
        class Node : public std::enable_shared_from_this<Node>
        {
        public:
            Node *parent = nullptr;

            std::string type;

            /** Free-form text payload — used by Label, TextInput. */
            std::string textContent;

            /** Editable value (TextInput, etc). Distinct from textContent
             *  because for inputs the visible text and the canonical
             *  "form value" differ during IME composition. */
            std::string value;

            /** Image asset path — used by Image, and as background by View
             *  via SimpleStyleSheet::backgroundImage. */
            std::string imageSource;

            /** Smart pointers to child nodes for recursive layout/draw. */
            std::vector<std::shared_ptr<Node>> children;

            /** Styling. */
            SimpleStyleSheet style;

            /** Invalidation flag — set when text or style changes. */
            bool needsRepaint = true;

            /** Intrinsic measurement cache. Populated during the
             *  measure pass; consumed by the layout pass. */
            float intrinsicW = 0.0f;
            float intrinsicH = 0.0f;

            /** Aliases used by Label.cpp's draw path; they point at the
             *  same numbers as intrinsicW/intrinsicH. */
            float &measuredW = intrinsicW;
            float &measuredH = intrinsicH;

            /** Final layout in screen space (post-padding-aware, post-flex). */
            Rect layout{0, 0, 0, 0};

            /** Hover/active animation state. */
            bool isHovered = false;
            bool isActive  = false;
            bool isFocused = false;

            /** Scroll offset (ScrollView writes to these; layout reads them
             *  to translate child positions during draw). */
            float scrollX = 0.0f;
            float scrollY = 0.0f;
            float scrollMaxX = 0.0f;  // computed by ScrollView::layoutNode
            float scrollMaxY = 0.0f;

            float visualScale = 1.0f;
            float startScale  = 1.0f;
            float targetScale = 1.0f;
            float animationTimer = 0.0f;

            /** Tab-order cache — populated by the focus traversal walk. */
            int focusOrder = -1;

            /* Wrapped-text cache (Label only)
             *
             * Re-wrapping on every frame is expensive: wrapLines()
             * walks the codepoints and calls TTF_GetStringSize per word
             * boundary, which with dozens of wrapping Labels costs
             * several ms/frame of pure measurement work.
             *
             * The cache stores the most recently wrapped line set keyed
             * by (text, contentW, fontSize, fontPath). When the key
             * matches, the cached lines are reused; mutating any input
             * changes the key and forces a rebuild. */
            std::vector<std::string> cachedWrappedLines;
            std::string              cachedWrapText;
            std::string              cachedWrapFont;
            float                    cachedWrapWidth   = -1.0f;
            float                    cachedWrapFontSz  = -1.0f;
            float                    cachedWrapLetterSp = 0.0f;

            /* Shaped-text cache (Label only)
             *
             * Even with the wrap cache and the glyph fast-path, every
             * frame Label::draw still iterates ~N codepoints, looks
             * each one up in the glyph hashmap, accumulates per-pair
             * kerning, and pushes a quad per glyph. For a Code block
             * with hundreds of glyphs that adds up.
             *
             * The shaped cache short-circuits all of that by storing
             * the per-glyph (destination rect, atlas UVs) tuples
             * computed by the FIRST draw, and reusing them on every
             * subsequent draw. Cache key is (text + font + size +
             * contentW + letterSpacing + textAlign + alignItems +
             * lineHeight) — the inputs that determine glyph layout.
             * Color and global scale are NOT part of the key — they
             * apply at emit-time so cache hits survive tint changes
             * and hover-scale animations.
             *
             * Quads are stored relative to (0, 0) origin; on emit we
             * shift by the actual block origin and apply the current
             * scale around the text's centre. This keeps the cache
             * geometry-only and color/scale-dependent state live. */
            struct ShapedQuad {
                float x, y, w, h;     // dest rect at scale=1, origin=(0,0)
                float u0, v0, u1, v1; // atlas UVs (already normalized? no — pixel)
            };
            std::vector<ShapedQuad>  cachedShapedQuads;
            std::string              cachedShapeText;
            std::string              cachedShapeFont;
            float                    cachedShapeWidth   = -1.0f;
            float                    cachedShapeFontSz  = -1.0f;
            float                    cachedShapeLetterSp = 0.0f;
            float                    cachedShapeLineH    = -1.0f;
            int                      cachedShapeAlignH   = -1; // textAlign enum
            int                      cachedShapeAlignV   = -1; // alignItems enum
            float                    cachedShapeContainerW = -1.0f;
            float                    cachedShapeContainerH = -1.0f;

            /* Lifecycle hooks */

            /** Measurement (bottom-up) — sets intrinsic size from content. */
            std::function<void(std::shared_ptr<Node>, AssetPack *)> onCompute = nullptr;

            /** Placement (top-down) — set absolute layout from parent. */
            std::function<void(std::shared_ptr<Node>)> onLayout = nullptr;

            /* Event hooks */

            std::function<void()> onClick = nullptr;
            std::function<void()> onMouseEnter = nullptr;
            std::function<void()> onMouseLeave = nullptr;
            std::function<void()> onFocus = nullptr;
            std::function<void()> onBlur = nullptr;
            std::function<void(const std::string &)> onChange = nullptr;
            std::function<void(int /*scancode*/, bool /*pressed*/)> onKey = nullptr;
            std::function<void(float /*deltaX*/, float /*deltaY*/)> onScroll = nullptr;

            std::function<void()> onDeconstruct = nullptr;

            /* JS-binding cleanup, separate from onDeconstruct.
             *
             * When the Canvas reconciler reuses a Node across renders,
             * it needs to release the JSValues captured by the
             * previous render's event-handler closures (onClick etc).
             * Calling onDeconstruct would also fire component-internal
             * cleanup (ScrollView state teardown, TextInput cleanup)
             * which we don't want on a reuse — only on actual node
             * destruction.
             *
             * Code that captures JSValues into callbacks chains itself
             * onto this field. The reconciler calls it before
             * re-binding handlers, releasing the old captures. Cleared
             * to nullptr after each call so the next render's handlers
             * accumulate fresh.
             *
             * On final node destruction, the destructor invokes both
             * clearJsBindings and onDeconstruct in order. */
            std::function<void()> clearJsBindings = nullptr;

            explicit Node(std::string t) : type(std::move(t)) {}

            virtual ~Node()
            {
                if (clearJsBindings)
                    clearJsBindings();
                if (onDeconstruct)
                    onDeconstruct();
            }

            Node(const Node &) = delete;
            Node &operator=(const Node &) = delete;

            void add_child(std::shared_ptr<Node> child)
            {
                child->parent = this;
                children.push_back(child);
            }

            /** Cumulative visual scale up the parent chain. Used by Label
             *  to decide whether to pixel-snap glyph quads. */
            float getGlobalScale() const
            {
                float scale = visualScale;
                Node *p = parent;
                while (p)
                {
                    scale *= p->visualScale;
                    p = p->parent;
                }
                return scale;
            }

            /** Cumulative opacity up the parent chain. Used at draw time
             *  to fade subtrees uniformly without per-node bookkeeping. */
            float getGlobalOpacity() const
            {
                float a = style.opacity;
                Node *p = parent;
                while (p)
                {
                    a *= p->style.opacity;
                    p = p->parent;
                }
                return std::clamp(a, 0.0f, 1.0f);
            }

            /**
             * Single-method compute path used by the onCompute callback.
             *
             * The primary layout pipeline in Layout.cpp drives
             * measurement and placement directly via Layout::measure()
             * / Layout::arrange(). This method exists for components
             * that supply an onCompute callback (such as Label): when
             * onCompute is set it is invoked here, so callback-driven
             * components compute their size and placement through the
             * same entry point.
             *
             * It computes width and height from the node's style and the
             * supplied bounds, then defers to onCompute for content
             * sizing, matching the arguments Label and View::computeNode
             * expect.
            */
            void compute(float startX, float startY, float maximumWidth, float maximumHeight, AssetPack *assets)
            {
                if (style.widthIsPercent)
                    layout.w = maximumWidth * (style.width / 100.0f);
                else if (style.width > 0)
                    layout.w = style.width;
                else
                    layout.w = maximumWidth;

                if (style.heightIsPercent)
                    layout.h = maximumHeight * (style.height / 100.0f);
                else if (style.height > 0)
                    layout.h = style.height;
                else
                    layout.h = maximumHeight;

                const float pT = resolveSide(style.paddingTop,    style.padding);
                const float pB = resolveSide(style.paddingBottom, style.padding);
                const float pL = resolveSide(style.paddingLeft,   style.padding);
                const float pR = resolveSide(style.paddingRight,  style.padding);

                const float childMaxW = std::max(0.0f, layout.w - pL - pR);
                const float childMaxH = std::max(0.0f, layout.h - pT - pB);

                for (auto &child : children)
                    child->compute(0, 0, childMaxW, childMaxH, assets);

                if (onCompute)
                    onCompute(shared_from_this(), assets);

                layout.x = startX;
                layout.y = startY;
            }

            /** Find a descendant by predicate (DFS, pre-order). Returns
             *  nullptr if no match. Useful for focus traversal and tests. */
            std::shared_ptr<Node> find(const std::function<bool(const Node &)> &pred)
            {
                if (pred(*this))
                    return shared_from_this();
                for (auto &c : children)
                {
                    if (auto found = c->find(pred))
                        return found;
                }
                return nullptr;
            }
        };
    }
}