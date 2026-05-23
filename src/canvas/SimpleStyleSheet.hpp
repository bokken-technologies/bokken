#pragma once

#include "Align.hpp"
#include "Justify.hpp"
#include "Timing.hpp"
#include "FlexDirection.hpp"
#include "Position.hpp"
#include "Overflow.hpp"
#include "TextAlign.hpp"
#include "Cursor.hpp"
#include <string>
#include <cstdint>
#include <cmath>
#include <limits>

namespace Bokken
{
    namespace Canvas
    {
        /**
         * Native representation of the Simple Style Sheet (SSS).
         *
         * The surface is roughly a CSS-flexbox-flavoured subset that
         * actually ships rather than a full CSS clone that doesn't. The
         * goal is "if you can describe a Discord/Linear/Figma-tier UI
         * panel, you should be able to write it here".
         *
         * NaN sentinel convention
         * Per-side fields (paddingTop, top, …) default to NaN to mean
         * "user did not set this". 0 means "user set it to 0" — those
         * are different. resolveSide() and isSet() are the only correct
         * ways to read these. A field that has no sensible "unset" value
         * (e.g. backgroundColor where 0x00000000 == fully transparent ==
         * fine) uses a normal default.
         *
         * Supported properties
         * - Layout: rowGap / columnGap (and gap shorthand), flexShrink,
         *   flexBasis, alignSelf, minimumWidth/maximumWidth/minimumHeight/maximumHeight,
         *   justifyContent including SpaceBetween/SpaceAround/SpaceEvenly.
         * - Borders: per-corner radius, per-side widths, per-side colors.
         * - Background: linear gradient (two-stop) and image-url support.
         * - Effects: drop shadow (offset + blur + color), opacity
         *   propagated to children during draw, transform
         *   (translateX/Y, rotate, scale).
         * - Text: textAlign, lineHeight, letterSpacing, basic word-wrap.
         * - Interaction: cursor hint, hoverColor, hoverBackgroundColor,
         *   tabIndex (for focus traversal), disabled.
         *
         * Intentionally out of scope
         * Grid layout, multi-stop gradients, conic gradients, blur
         * filters, mask images, custom pseudo-classes beyond hover/active.
         * The first three would be worth their own pass; the rest are
         * shader work that isn't needed yet.
        */
        struct SimpleStyleSheet
        {
            /* Layout & Flexbox */

            /* Direction of child flow */
            FlexDirection flexDirection = FlexDirection::Column;

            /* Wrap children onto new lines when the main axis overflows.
             * Off by default to match the old behaviour. */
            bool flexWrap = false;

            /* Growth factor relative to siblings when extra main-axis
             * space exists. */
            float flex = 0.0f;

            /* Shrink factor when children overflow the main axis.
             * Defaults to 1 to match CSS — items shrink proportionally
             * to fit when the sum of children's preferred sizes exceeds
             * the container. Set to 0 to make a child refuse to shrink
             * (pinning a sidebar at its declared width, etc.). */
            float flexShrink = 1.0f;

            /* Preferred main-axis size before grow/shrink. NaN means
             * "use whatever width/height resolves to". */
            float flexBasis = std::numeric_limits<float>::quiet_NaN();

            /* Per-axis gap between children. The gap shorthand sets both;
             * the per-axis fields override it (NaN = "use gap"). */
            float gap       = 0.0f;
            float rowGap    = std::numeric_limits<float>::quiet_NaN();
            float columnGap = std::numeric_limits<float>::quiet_NaN();

            /* Padding — base shorthand + per-side overrides (NaN = use base). */
            float padding       = 0.0f;
            float paddingTop    = std::numeric_limits<float>::quiet_NaN();
            float paddingBottom = std::numeric_limits<float>::quiet_NaN();
            float paddingLeft   = std::numeric_limits<float>::quiet_NaN();
            float paddingRight  = std::numeric_limits<float>::quiet_NaN();

            /* Margin — same scheme. */
            float margin       = 0.0f;
            float marginTop    = std::numeric_limits<float>::quiet_NaN();
            float marginBottom = std::numeric_limits<float>::quiet_NaN();
            float marginLeft   = std::numeric_limits<float>::quiet_NaN();
            float marginRight  = std::numeric_limits<float>::quiet_NaN();

            /* Positioning */

            Position position = Position::Relative;

            /* Coords for absolute positioning. NaN = not pinned. */
            float top    = std::numeric_limits<float>::quiet_NaN();
            float bottom = std::numeric_limits<float>::quiet_NaN();
            float left   = std::numeric_limits<float>::quiet_NaN();
            float right  = std::numeric_limits<float>::quiet_NaN();

            /* Stacking order. Within the same parent, children with the
             * higher zIndex draw on top. Default 0; ties go to tree order. */
            int32_t zIndex = 0;

            /* Sizing */

            float width  = 0.0f;
            float height = 0.0f;
            bool widthIsPercent  = false;
            bool heightIsPercent = false;

            /* Min/max constraints. 0 (or NaN for max) means "no constraint".
             * Percentage flag mirrors the width/height flag pattern. */
            float minimumWidth        = 0.0f;
            float maximumWidth        = std::numeric_limits<float>::quiet_NaN();
            float minimumHeight       = 0.0f;
            float maximumHeight       = std::numeric_limits<float>::quiet_NaN();
            bool  minimumWidthIsPercent  = false;
            bool  maximumWidthIsPercent  = false;
            bool  minimumHeightIsPercent = false;
            bool  maximumHeightIsPercent = false;

            /* Cross-axis self-alignment */

            /* Per-child override of the parent's alignItems. Inherit means
             * "fall back to parent". */
            enum class AlignSelf : uint8_t { Inherit, Start, Center, End, Stretch };
            AlignSelf alignSelf = AlignSelf::Inherit;

            /* Fonts & text */

            std::string font = "fonts/default.ttf";
            float fontSize = 16.0f;

            /* Multiplier applied to the font's natural line height. 1.0 is
             * the metric line height; web "line-height: 1.4" maps cleanly. */
            float lineHeight = 1.0f;

            /* Pixels of extra advance between glyphs. Negative tightens. */
            float letterSpacing = 0.0f;

            /* Wrap text on word boundaries when the Label has a fixed width.
             * Off by default to match the old single-line behaviour.
             *
             * For TextInput this also enables multi-line editing — the
             * input grows vertically, lines are rendered with the same
             * wrap algorithm Label uses, and the caret can move
             * across lines via Up/Down arrows. */
            bool wordWrap = false;

            /* TextInput: allow the user to drag a corner grip to
             * resize the input. The grip renders in the bottom-right
             * when this is set; without it (the default) the input
             * stays at its layout size. */
            bool resize = false;

            TextAlign textAlign = TextAlign::Left;

            /* Render text in bold/italic if the underlying font has a
             * matching face wired up by AssetPack. We pass a hint flag
             * down to the glyph cache; if no synthesis is available the
             * regular face is used. */
            bool fontBold   = false;
            bool fontItalic = false;

            /* Visual styling */

            /* Background color in 0xRRGGBBAA. */
            uint32_t backgroundColor = 0x00000000;

            /* Linear-gradient background. When gradientEnd has any alpha
             * we emit a gradient instead of a flat fill. Angle is in
             * degrees, clockwise from "top of element". */
            uint32_t gradientStart = 0x00000000;
            uint32_t gradientEnd   = 0x00000000;
            float    gradientAngle = 0.0f;

            /* Optional image used as the background. Empty = no image.
             * Sampled stretched to the element bounds. The drawing code
             * looks the path up via AssetPack — same as everything else. */
            std::string backgroundImage;

            /* Foreground / text color. */
            uint32_t color = 0xFFFFFFFF;

            /* Transparency level [0..1], multiplicative with color alpha.
             * Propagates through the View draw path and into children. */
            float opacity = 1.0f;

            /* Corner rounding. borderRadius is the shorthand; per-corner
             * fields override (NaN = use shorthand). Renderer caps each
             * corner radius to min(width, height)/2. */
            float borderRadius            = 0.0f;
            float borderTopLeftRadius     = std::numeric_limits<float>::quiet_NaN();
            float borderTopRightRadius    = std::numeric_limits<float>::quiet_NaN();
            float borderBottomLeftRadius  = std::numeric_limits<float>::quiet_NaN();
            float borderBottomRightRadius = std::numeric_limits<float>::quiet_NaN();

            /* Border thickness and color — shorthand and per-side overrides. */
            float    borderWidth       = 0.0f;
            uint32_t borderColor       = 0x00000000;
            float    borderTopWidth    = std::numeric_limits<float>::quiet_NaN();
            float    borderBottomWidth = std::numeric_limits<float>::quiet_NaN();
            float    borderLeftWidth   = std::numeric_limits<float>::quiet_NaN();
            float    borderRightWidth  = std::numeric_limits<float>::quiet_NaN();
            uint32_t borderTopColor    = 0x00000000;  // 0 alpha = use borderColor
            uint32_t borderBottomColor = 0x00000000;
            uint32_t borderLeftColor   = 0x00000000;
            uint32_t borderRightColor  = 0x00000000;

            /* Drop shadow (single layer, like CSS box-shadow with no inset).
             * shadowColor alpha == 0 means "no shadow". */
            uint32_t shadowColor   = 0x00000000;
            float    shadowOffsetX = 0.0f;
            float    shadowOffsetY = 0.0f;
            float    shadowBlur    = 0.0f;  // pixels of soft falloff

            /* Clipping behaviour for children. Hidden activates a scissor
             * during the subtree's draw — the corner-radius is honoured
             * approximately (rectangular scissor) which is correct for
             * almost all UI cases and dramatically cheaper than a stencil
             * pass. */
            Overflow overflow = Overflow::Visible;

            /* Transform */

            /* Applied in the order: translate → rotate (around center) →
             * scale (around center). Composed onto layout at draw time
             * without affecting hit-testing geometry — mirroring CSS
             * transform-style behaviour. Cumulative with hover/active
             * scale animations. */
            float translateX = 0.0f;
            float translateY = 0.0f;
            float rotation   = 0.0f;  // radians
            float scaleX     = 1.0f;
            float scaleY     = 1.0f;

            /* Alignment */

            /* Distribution along the main axis. */
            Justify justifyContent = Justify::Start;

            /* Alignment along the cross axis. Stretch is implemented by
             * having the layout pass write the cross-axis size on the
             * child if alignItems == Stretch and the child didn't set
             * its own cross-axis size. */
            enum class AlignItems : uint8_t { Start, Center, End, Stretch };
            AlignItems alignItems = AlignItems::Center;

            /* Animation */

            float transitionDuration = 0.0f;
            Timing transitionTiming = Timing::Linear;

            /* Hover/active visual feedback. The hoverColor and
             * hoverBackgroundColor fields are pseudo-classes — they're
             * substituted at draw time when the node is hovered; setting
             * the alpha to 0 means "leave as-is" rather than "go fully
             * transparent on hover". */
            float    hoverScale          = 1.0f;
            float    activeScale         = 0.95f;
            uint32_t hoverColor          = 0x00000000;
            uint32_t hoverBackgroundColor = 0x00000000;
            uint32_t activeBackgroundColor = 0x00000000;

            /* Interaction */

            Cursor cursor = Cursor::Default;

            /* Tab order for keyboard focus traversal. -1 (default) means
             * "not focusable via Tab"; 0 means "focusable, default order";
             * positive integers are explicit ordering. */
            int tabIndex = -1;

            /* Disabled state — skips event dispatch and renders at half
             * opacity by convention. UI components should respect this
             * (Button does; ScrollView ignores it; TextInput stops
             * accepting input). */
            bool disabled = false;
        };

        /**
         * Resolve a per-side override against its shorthand base.
         * Returns `side` if the user set it (non-NaN), else `base`.
        */
        inline float resolveSide(float side, float base)
        {
            return std::isnan(side) ? base : side;
        }

        /** Has this NaN-sentinel field been set by the user? */
        inline bool isSet(float v)
        {
            return !std::isnan(v);
        }

        /**
         * Resolve a corner radius. Per-corner overrides shorthand;
         * the result is then clamped to fit within the rect — this is
         * what stops "borderRadius: 999" from glitching on small boxes.
        */
        inline float resolveCorner(float corner, float base, float w, float h)
        {
            float r = std::isnan(corner) ? base : corner;
            float lim = std::min(w, h) * 0.5f;
            if (r < 0.0f) r = 0.0f;
            if (r > lim)  r = lim;
            return r;
        }

        /**
         * Resolve a per-side border. Width falls back to shorthand;
         * color falls back to shorthand only when the per-side alpha is
         * zero (matching what users intuitively expect when they leave
         * borderTopColor unset).
        */
        inline float resolveBorderWidth(float side, float base)
        {
            return std::isnan(side) ? base : side;
        }
        inline uint32_t resolveBorderColor(uint32_t side, uint32_t base)
        {
            return ((side & 0xFFu) == 0u) ? base : side;
        }

        /**
         * Resolve gap on a given axis. For Column flex direction the
         * "main axis gap" is rowGap; for Row it's columnGap. Mirrors
         * the CSS naming surprise.
        */
        inline float resolveMainGap(const SimpleStyleSheet &s)
        {
            if (s.flexDirection == FlexDirection::Column)
                return std::isnan(s.rowGap) ? s.gap : s.rowGap;
            return std::isnan(s.columnGap) ? s.gap : s.columnGap;
        }
        inline float resolveCrossGap(const SimpleStyleSheet &s)
        {
            if (s.flexDirection == FlexDirection::Column)
                return std::isnan(s.columnGap) ? s.gap : s.columnGap;
            return std::isnan(s.rowGap) ? s.gap : s.rowGap;
        }

        /**
         * Resolve a min/max constraint to absolute pixels. Returns NaN
         * if the constraint is "none" (max only — min defaults to 0).
         * `parentMain` is the size of the relevant axis on the parent's
         * content box, used to interpret percentages.
        */
        inline float resolveSizeConstraint(float v, bool isPercent, float parentMain)
        {
            if (std::isnan(v)) return std::numeric_limits<float>::quiet_NaN();
            if (v <= 0.0f && !isPercent) return std::numeric_limits<float>::quiet_NaN();
            return isPercent ? parentMain * (v / 100.0f) : v;
        }
    }
}
