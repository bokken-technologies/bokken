#include "Layout.hpp"
#include "SimpleStyleSheet.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Bokken
{
    namespace Canvas
    {
        namespace Layout
        {
            namespace
            {
                /* Resolved per-node sizing context — kept off the Node so
                 * the layout pass doesn't pollute it with intermediates. */
                struct Box
                {
                    float pT, pB, pL, pR;   // padding (resolved)
                    float mT, mB, mL, mR;   // margin (resolved)
                    float minW, maxW;       // resolved size constraints (NaN = none on max)
                    float minH, maxH;
                };

                Box resolveBox(const SimpleStyleSheet &s, float parentW, float parentH)
                {
                    Box b;
                    b.pT = resolveSide(s.paddingTop,    s.padding);
                    b.pB = resolveSide(s.paddingBottom, s.padding);
                    b.pL = resolveSide(s.paddingLeft,   s.padding);
                    b.pR = resolveSide(s.paddingRight,  s.padding);
                    b.mT = resolveSide(s.marginTop,     s.margin);
                    b.mB = resolveSide(s.marginBottom,  s.margin);
                    b.mL = resolveSide(s.marginLeft,    s.margin);
                    b.mR = resolveSide(s.marginRight,   s.margin);
                    b.minW = resolveSizeConstraint(s.minimumWidth,  s.minimumWidthIsPercent,  parentW);
                    b.maxW = resolveSizeConstraint(s.maximumWidth,  s.maximumWidthIsPercent,  parentW);
                    b.minH = resolveSizeConstraint(s.minimumHeight, s.minimumHeightIsPercent, parentH);
                    b.maxH = resolveSizeConstraint(s.maximumHeight, s.maximumHeightIsPercent, parentH);
                    if (std::isnan(b.minW)) b.minW = 0.0f;
                    if (std::isnan(b.minH)) b.minH = 0.0f;
                    return b;
                }

                /* Apply min/max clamps to a resolved size. NaN max == no
                 * upper bound. */
                float clampSize(float v, float lo, float hi)
                {
                    if (v < lo) v = lo;
                    if (!std::isnan(hi) && v > hi) v = hi;
                    return v;
                }

                /* Resolve an explicit size declaration (width or height)
                 * against parent main size. Returns NaN if the user
                 * didn't declare a size — caller falls back to intrinsic. */
                float resolveExplicit(float declared, bool isPercent, float parentMain)
                {
                    if (isPercent)            return parentMain * (declared / 100.0f);
                    if (declared > 0.0f)      return declared;
                    return std::numeric_limits<float>::quiet_NaN();
                }
            }

            /*
             * Measure pass.
             *
             * Walks bottom-up. Each node's intrinsicW/H ends up containing
             * the smallest size that fits its content (text for Label,
             * sum-of-children for View) before grow/shrink redistribution.
             *
             * The pass also seeds layout.w/h with the intrinsic value
             * clamped to declared width/height — this gives the arrange
             * pass a sane starting point even for nodes that won't get
             * a flex adjustment.
            */
            void measure(std::shared_ptr<Node> node,
                         float availW, float availH,
                         AssetPack *assets)
            {
                if (!node) return;
                const auto &s = node->style;

                Box box = resolveBox(s, availW, availH);

                /* The "outer" box of this node (margin-included) cannot
                 * exceed available; the content available to children is
                 * the remaining inside padding. We compute child-available
                 * here using a tentative outer size — for nodes with an
                 * explicit width/height we know exactly; for auto nodes
                 * we use availW/availH and let shrink-to-fit happen later. */
                float explicitW = resolveExplicit(s.width,  s.widthIsPercent,  availW);
                float explicitH = resolveExplicit(s.height, s.heightIsPercent, availH);

                float outerW = std::isnan(explicitW) ? availW : explicitW;
                float outerH = std::isnan(explicitH) ? availH : explicitH;

                outerW = clampSize(outerW, box.minW, box.maxW);
                outerH = clampSize(outerH, box.minH, box.maxH);

                float childAvailW = std::max(0.0f, outerW - box.pL - box.pR);
                float childAvailH = std::max(0.0f, outerH - box.pT - box.pB);

                /* Recurse into children first so they can report their
                 * own intrinsic sizes back. */
                for (auto &c : node->children)
                {
                    if (c->style.position == Position::Absolute)
                    {
                        /* Absolute children don't contribute to parent
                         * intrinsic size. They're laid out against the
                         * parent's final box later. We still measure
                         * them so their own subtree is set up. */
                        measure(c, childAvailW, childAvailH, assets);
                        continue;
                    }
                    measure(c, childAvailW, childAvailH, assets);
                }

                /* Run any custom measurement callback (Label uses this
                 * to set intrinsicW/H from text metrics). View doesn't
                 * need a custom callback here — the default sum-of-
                 * children logic below covers it. */
                if (node->onCompute)
                {
                    /* Seed layout.w/h to childAvail so onCompute code
                     * (which reads layout.w/h as "max available") works.
                     * The convention is that onCompute writes
                     * intrinsicW/H *and* layout.w/h to the natural size;
                     * we re-clamp afterwards. */
                    node->layout.w = std::isnan(explicitW) ? childAvailW : explicitW;
                    node->layout.h = std::isnan(explicitH) ? childAvailH : explicitH;
                    node->onCompute(node, assets);

                    /* If the callback didn't write intrinsicW/H, derive
                     * them from the layout the callback set. */
                    if (node->intrinsicW <= 0.0f) node->intrinsicW = node->layout.w;
                    if (node->intrinsicH <= 0.0f) node->intrinsicH = node->layout.h;
                }
                else
                {
                    /* Default measurement: sum of children along main,
                     * max along cross, plus padding. This is View's
                     * intrinsic-sizing rule. flexWrap is honored for the
                     * cross-axis intrinsic — wrapping nodes can be wider
                     * than a single row sum. */
                    const float mainGap  = resolveMainGap(s);
                    const float crossGap = resolveCrossGap(s);

                    float contentMain = 0.0f;
                    float contentCross = 0.0f;

                    int flowChildren = 0;
                    for (auto &c : node->children)
                    {
                        if (c->style.position == Position::Absolute) continue;
                        const auto &cs = c->style;
                        const float cmT = resolveSide(cs.marginTop,    cs.margin);
                        const float cmB = resolveSide(cs.marginBottom, cs.margin);
                        const float cmL = resolveSide(cs.marginLeft,   cs.margin);
                        const float cmR = resolveSide(cs.marginRight,  cs.margin);

                        if (s.flexDirection == FlexDirection::Column)
                        {
                            contentMain  += c->intrinsicH + cmT + cmB;
                            contentCross  = std::max(contentCross,
                                                     c->intrinsicW + cmL + cmR);
                        }
                        else
                        {
                            contentMain  += c->intrinsicW + cmL + cmR;
                            contentCross  = std::max(contentCross,
                                                     c->intrinsicH + cmT + cmB);
                        }
                        flowChildren++;
                    }

                    if (flowChildren > 1)
                        contentMain += mainGap * (flowChildren - 1);

                    if (s.flexDirection == FlexDirection::Column)
                    {
                        node->intrinsicW = contentCross + box.pL + box.pR;
                        node->intrinsicH = contentMain  + box.pT + box.pB;
                    }
                    else
                    {
                        node->intrinsicW = contentMain  + box.pL + box.pR;
                        node->intrinsicH = contentCross + box.pT + box.pB;
                    }
                    /* Suppress unused-warning for crossGap when not wrapping;
                     * it's read by the arrange pass instead. */
                    (void)crossGap;
                }

                /* Apply explicit width/height if set, then clamp. */
                if (!std::isnan(explicitW)) node->intrinsicW = explicitW;
                if (!std::isnan(explicitH)) node->intrinsicH = explicitH;
                node->intrinsicW = clampSize(node->intrinsicW, box.minW, box.maxW);
                node->intrinsicH = clampSize(node->intrinsicH, box.minH, box.maxH);

                /* Seed layout size from intrinsics — arrange may overwrite
                 * via flex grow/shrink later. */
                node->layout.w = node->intrinsicW;
                node->layout.h = node->intrinsicH;
            }

            /*
             * Arrange pass.
             *
             * Walks top-down. The caller has already set this node's
             * layout.x/y/w/h. We now place children inside its content
             * box and recurse.
             *
             * The pass handles:
             *   - flex grow & shrink redistribution
             *   - gap / rowGap / columnGap
             *   - justifyContent including space-* family
             *   - alignItems including Stretch
             *   - alignSelf override per child
             *   - flexWrap with line-by-line cross-axis stacking
             *   - absolute positioning (post-flow placement)
             *   - min/max constraints during distribution
            */
            namespace
            {
                struct ChildInfo
                {
                    std::shared_ptr<Node> node;
                    float mt, mb, ml, mr;
                    float mainSize;     // current main-axis size (mutated during distribute)
                    float crossSize;    // current cross-axis size
                    float baseMain;     // hypothetical main from flexBasis or intrinsic
                    float flexGrow;
                    float flexShrink;
                    float minMain, maxMain;
                    float minCross, maxCross;
                    SimpleStyleSheet::AlignSelf alignSelf;
                };

                struct Line
                {
                    std::vector<ChildInfo *> items;
                    float mainSum   = 0.0f;   // sum of main + main-margins (no gap)
                    float maxCross  = 0.0f;   // tallest cross dimension on the line
                };

                /* Distribute free space along the main axis on a single
                 * line. Handles both grow and shrink with min/max clamps.
                 * Returns the final mainSum after distribution.
                 *
                 * `allowShrink` controls whether negative free space
                 * (overflow) triggers shrinking. ScrollView containers
                 * pass false on the scroll axis: children should keep
                 * their natural sizes and the user scrolls instead of
                 * the layout squeezing them to fit. */
                float distributeFlex(Line &line, float containerMain, float gap, bool allowShrink)
                {
                    /* Initial mainSum already includes margins but no gap. */
                    if (line.items.empty()) return 0.0f;

                    float gapTotal = gap * (line.items.size() - 1);
                    float totalGrow   = 0.0f;
                    float totalShrink = 0.0f;
                    float mainSum     = line.mainSum;

                    for (auto *info : line.items)
                    {
                        totalGrow   += info->flexGrow;
                        totalShrink += info->flexShrink;
                    }

                    float free = containerMain - mainSum - gapTotal;

                    if (free > 0.0f && totalGrow > 0.0f)
                    {
                        /* Grow: add free space proportional to flexGrow,
                         * clamping to maxMain. We loop because clamping
                         * may free up space for other items on the line. */
                        for (int iter = 0; iter < 8 && free > 0.5f; iter++)
                        {
                            float remainingGrow = 0.0f;
                            for (auto *info : line.items)
                                if (info->flexGrow > 0.0f &&
                                    (std::isnan(info->maxMain) || info->mainSize < info->maxMain))
                                    remainingGrow += info->flexGrow;
                            if (remainingGrow <= 0.0f) break;

                            float perUnit = free / remainingGrow;
                            free = 0.0f;
                            for (auto *info : line.items)
                            {
                                if (info->flexGrow <= 0.0f) continue;
                                float add = perUnit * info->flexGrow;
                                float newSize = info->mainSize + add;
                                if (!std::isnan(info->maxMain) && newSize > info->maxMain)
                                {
                                    free += (newSize - info->maxMain);
                                    newSize = info->maxMain;
                                }
                                mainSum += (newSize - info->mainSize);
                                info->mainSize = newSize;
                            }
                        }
                    }
                    else if (free < 0.0f && totalShrink > 0.0f && allowShrink)
                    {
                        /* Shrink: take from items proportional to
                         * flexShrink * baseMain (per CSS), clamping to
                         * minMain. Same iterative re-balance pattern. */
                        for (int iter = 0; iter < 8 && free < -0.5f; iter++)
                        {
                            float weightSum = 0.0f;
                            for (auto *info : line.items)
                                if (info->flexShrink > 0.0f && info->mainSize > info->minMain)
                                    weightSum += info->flexShrink * info->baseMain;
                            if (weightSum <= 0.0f) break;

                            float deficit = -free;
                            free = 0.0f;
                            for (auto *info : line.items)
                            {
                                if (info->flexShrink <= 0.0f) continue;
                                float w = info->flexShrink * info->baseMain;
                                float take = deficit * (w / weightSum);
                                float newSize = info->mainSize - take;
                                if (newSize < info->minMain)
                                {
                                    free -= (info->minMain - newSize);
                                    newSize = info->minMain;
                                }
                                mainSum -= (info->mainSize - newSize);
                                info->mainSize = newSize;
                            }
                        }
                    }

                    line.mainSum = mainSum;
                    return mainSum;
                }

                /* Resolve cross-axis size for a child given alignItems
                 * and alignSelf. If the effective alignment is Stretch
                 * and the child didn't declare a cross-axis size, we
                 * stretch it to fill the line's cross extent. */
                void resolveCrossSize(ChildInfo &info, float lineCross,
                                      SimpleStyleSheet::AlignItems parentAlign,
                                      bool isColumnFlow)
                {
                    auto effective = info.alignSelf;
                    SimpleStyleSheet::AlignItems final;
                    if (effective == SimpleStyleSheet::AlignSelf::Inherit)
                        final = parentAlign;
                    else
                    {
                        switch (effective)
                        {
                        case SimpleStyleSheet::AlignSelf::Start:   final = SimpleStyleSheet::AlignItems::Start;   break;
                        case SimpleStyleSheet::AlignSelf::Center:  final = SimpleStyleSheet::AlignItems::Center;  break;
                        case SimpleStyleSheet::AlignSelf::End:     final = SimpleStyleSheet::AlignItems::End;     break;
                        case SimpleStyleSheet::AlignSelf::Stretch: final = SimpleStyleSheet::AlignItems::Stretch; break;
                        default:                                   final = parentAlign;                            break;
                        }
                    }

                    if (final != SimpleStyleSheet::AlignItems::Stretch) return;

                    /* Only stretch when the child didn't declare a cross
                     * size of its own. In CSS this is the auto-vs-fixed
                     * distinction; we approximate it as "child's declared
                     * cross was 0 and not percent". */
                    const auto &cs = info.node->style;
                    bool childHasFixedCross =
                        isColumnFlow
                            ? (cs.width > 0.0f || cs.widthIsPercent)
                            : (cs.height > 0.0f || cs.heightIsPercent);
                    if (childHasFixedCross) return;

                    float available = lineCross
                        - (isColumnFlow ? (info.ml + info.mr) : (info.mt + info.mb));
                    available = clampSize(available, info.minCross, info.maxCross);
                    info.crossSize = std::max(0.0f, available);
                    if (isColumnFlow) info.node->layout.w = info.crossSize;
                    else              info.node->layout.h = info.crossSize;
                }
            }

            void arrange(std::shared_ptr<Node> node, AssetPack *assets)
            {
                if (!node) return;
                const auto &s = node->style;
                const bool isCol = (s.flexDirection == FlexDirection::Column);

                const Box box = resolveBox(s, node->layout.w, node->layout.h);
                const float mainGap  = resolveMainGap(s);
                const float crossGap = resolveCrossGap(s);

                const float contentX = node->layout.x + box.pL;
                const float contentY = node->layout.y + box.pT;
                const float contentW = std::max(0.0f, node->layout.w - box.pL - box.pR);
                const float contentH = std::max(0.0f, node->layout.h - box.pT - box.pB);
                const float containerMain = isCol ? contentH : contentW;
                const float containerCross = isCol ? contentW : contentH;

                /* Build ChildInfo array for in-flow children */
                std::vector<ChildInfo> infos;
                infos.reserve(node->children.size());

                for (auto &c : node->children)
                {
                    if (c->style.position == Position::Absolute) continue;

                    const auto &cs = c->style;
                    ChildInfo info;
                    info.node = c;
                    info.mt = resolveSide(cs.marginTop,    cs.margin);
                    info.mb = resolveSide(cs.marginBottom, cs.margin);
                    info.ml = resolveSide(cs.marginLeft,   cs.margin);
                    info.mr = resolveSide(cs.marginRight,  cs.margin);
                    info.flexGrow   = std::max(0.0f, cs.flex);
                    info.flexShrink = std::max(0.0f, cs.flexShrink);
                    info.alignSelf  = cs.alignSelf;

                    /* flexBasis takes precedence over intrinsic for the
                     * starting main size. Then we clamp by min/max. */
                    float baseMain = isCol ? c->intrinsicH : c->intrinsicW;
                    if (!std::isnan(cs.flexBasis) && cs.flexBasis >= 0.0f)
                        baseMain = cs.flexBasis;

                    Box cb = resolveBox(cs, contentW, contentH);
                    info.minMain  = isCol ? cb.minH : cb.minW;
                    info.maxMain  = isCol ? cb.maxH : cb.maxW;
                    info.minCross = isCol ? cb.minW : cb.minH;
                    info.maxCross = isCol ? cb.maxW : cb.maxH;

                    info.baseMain = clampSize(baseMain, info.minMain, info.maxMain);
                    info.mainSize = info.baseMain;
                    info.crossSize = isCol ? c->layout.w : c->layout.h;
                    info.crossSize = clampSize(info.crossSize, info.minCross, info.maxCross);

                    infos.push_back(info);
                }

                /* Break into lines (only relevant when wrapping) */
                std::vector<Line> lines;
                if (s.flexWrap)
                {
                    Line cur;
                    for (auto &info : infos)
                    {
                        const float itemMain = info.mainSize +
                            (isCol ? (info.mt + info.mb) : (info.ml + info.mr));
                        const float gapAdd = cur.items.empty() ? 0.0f : mainGap;
                        if (!cur.items.empty() && cur.mainSum + gapAdd + itemMain > containerMain + 0.5f)
                        {
                            lines.push_back(std::move(cur));
                            cur = Line{};
                        }
                        cur.items.push_back(&info);
                        cur.mainSum += itemMain;
                        const float itemCross = info.crossSize +
                            (isCol ? (info.ml + info.mr) : (info.mt + info.mb));
                        cur.maxCross = std::max(cur.maxCross, itemCross);
                    }
                    if (!cur.items.empty()) lines.push_back(std::move(cur));
                }
                else
                {
                    /* Single line — every in-flow child goes on it. */
                    Line single;
                    for (auto &info : infos)
                    {
                        single.items.push_back(&info);
                        const float itemMain = info.mainSize +
                            (isCol ? (info.mt + info.mb) : (info.ml + info.mr));
                        single.mainSum += itemMain;
                        const float itemCross = info.crossSize +
                            (isCol ? (info.ml + info.mr) : (info.mt + info.mb));
                        single.maxCross = std::max(single.maxCross, itemCross);
                    }
                    lines.push_back(std::move(single));
                }

                /* Distribute flex on each line */
                /* ScrollViews never shrink children on the scroll axis —
                 * children stay at their preferred size and the user
                 * scrolls. (Cross-axis still allows stretch via
                 * alignItems handling below.) */
                const bool isScrollView = (node->type == "ScrollView");
                for (auto &line : lines)
                    distributeFlex(line, containerMain, mainGap, !isScrollView);

                /* Resolve cross sizes (alignSelf:Stretch etc) */
                for (auto &line : lines)
                {
                    /* For wrapping we use the line's own maxCross as the
                     * stretch target; for single-line non-wrapping use
                     * the container cross. */
                    float lineCross = s.flexWrap ? line.maxCross : containerCross;
                    for (auto *info : line.items)
                        resolveCrossSize(*info, lineCross, s.alignItems, isCol);

                    /* Re-fold the line maxCross after stretches. */
                    line.maxCross = 0.0f;
                    for (auto *info : line.items)
                    {
                        const float itemCross = info->crossSize +
                            (isCol ? (info->ml + info->mr) : (info->mt + info->mb));
                        line.maxCross = std::max(line.maxCross, itemCross);
                    }
                }

                /* Place each line */
                /* Cross-axis cursor — accumulates per line. */
                float crossCursor = isCol ? contentX : contentY;
                /* If wrapping is off, we still place at the start of the
                 * cross axis; alignItems acts within containerCross. */
                if (!s.flexWrap)
                    crossCursor = isCol ? contentX : contentY;

                for (size_t li = 0; li < lines.size(); li++)
                {
                    Line &line = lines[li];
                    const float lineCross = s.flexWrap ? line.maxCross : containerCross;
                    const float gapTotal  = mainGap * (line.items.empty() ? 0 : line.items.size() - 1);

                    /* justifyContent positioning. */
                    float mainStart  = 0.0f;
                    float mainBetween = mainGap;  // adjusted for space-*
                    {
                        float free = containerMain - line.mainSum - gapTotal;
                        if (free < 0.0f) free = 0.0f;

                        switch (s.justifyContent)
                        {
                        case Justify::Start:
                            mainStart = 0.0f;
                            break;
                        case Justify::Center:
                            mainStart = free * 0.5f;
                            break;
                        case Justify::End:
                            mainStart = free;
                            break;
                        case Justify::SpaceBetween:
                            mainStart = 0.0f;
                            if (line.items.size() > 1)
                                mainBetween += free / (line.items.size() - 1);
                            else
                                mainStart = free * 0.5f;
                            break;
                        case Justify::SpaceAround:
                        {
                            const float per = line.items.empty() ? 0.0f : free / line.items.size();
                            mainStart = per * 0.5f;
                            mainBetween += per;
                            break;
                        }
                        case Justify::SpaceEvenly:
                        {
                            const float per = free / (line.items.size() + 1);
                            mainStart = per;
                            mainBetween += per;
                            break;
                        }
                        }
                    }

                    float mainCursor = (isCol ? contentY : contentX) + mainStart;

                    for (size_t i = 0; i < line.items.size(); i++)
                    {
                        ChildInfo *info = line.items[i];
                        auto &child = *info->node;

                        /* Apply main-axis margin before placing. */
                        const float startMargin = isCol ? info->mt : info->ml;
                        const float endMargin   = isCol ? info->mb : info->mr;
                        mainCursor += startMargin;

                        /* Cross-axis placement on this line. */
                        const auto effectiveAlign = (info->alignSelf == SimpleStyleSheet::AlignSelf::Inherit)
                            ? s.alignItems
                            : (info->alignSelf == SimpleStyleSheet::AlignSelf::Start ? SimpleStyleSheet::AlignItems::Start
                              : info->alignSelf == SimpleStyleSheet::AlignSelf::Center ? SimpleStyleSheet::AlignItems::Center
                              : info->alignSelf == SimpleStyleSheet::AlignSelf::End ? SimpleStyleSheet::AlignItems::End
                              : SimpleStyleSheet::AlignItems::Stretch);

                        const float crossMarginStart = isCol ? info->ml : info->mt;
                        const float crossMarginEnd   = isCol ? info->mr : info->mb;
                        const float crossSlot = lineCross - info->crossSize - crossMarginStart - crossMarginEnd;
                        float crossOffset = 0.0f;
                        switch (effectiveAlign)
                        {
                        case SimpleStyleSheet::AlignItems::Start:   crossOffset = 0.0f;             break;
                        case SimpleStyleSheet::AlignItems::Center:  crossOffset = crossSlot * 0.5f; break;
                        case SimpleStyleSheet::AlignItems::End:     crossOffset = crossSlot;        break;
                        case SimpleStyleSheet::AlignItems::Stretch: crossOffset = 0.0f;             break;
                        }

                        /* Commit final size on the layout rect. */
                        if (isCol)
                        {
                            child.layout.h = info->mainSize;
                            child.layout.w = info->crossSize;
                            child.layout.y = std::round(mainCursor);
                            child.layout.x = std::round(crossCursor + crossMarginStart + crossOffset);
                        }
                        else
                        {
                            child.layout.w = info->mainSize;
                            child.layout.h = info->crossSize;
                            child.layout.x = std::round(mainCursor);
                            child.layout.y = std::round(crossCursor + crossMarginStart + crossOffset);
                        }

                        mainCursor += info->mainSize + endMargin;
                        if (i + 1 < line.items.size())
                            mainCursor += mainBetween;
                    }

                    /* Advance cross cursor by line height (only meaningful
                     * when wrapping). */
                    if (s.flexWrap)
                    {
                        crossCursor += lineCross;
                        if (li + 1 < lines.size()) crossCursor += crossGap;
                    }
                }

                /* Recurse into in-flow children */
                /* arrange() positions the child's own children, so we
                 * call it BEFORE onLayout. That way when a ScrollView's
                 * onLayout runs to compute scrollMax, it sees its
                 * direct children's final positions and sizes — those
                 * children's *internal* layouts are also done by then,
                 * but ScrollView only needs the direct-child bounding
                 * box, which arrange has already finalized.
                 *
                 * Two-pass measure correction: if the child's final
                 * layout.w/h differs from what it was given during the
                 * initial measure cascade (typical for flex: 1 children
                 * whose actual width emerges only from grow distribution
                 * here in arrange), re-run measure on the subtree with
                 * the corrected available size. Otherwise descendants
                 * with `width: "100%"` keep the stale parent size and
                 * overflow visibly when the window is narrower than
                 * the measure-time prediction. */
                for (auto &info : infos)
                {
                    const auto &cs = info.node->style;
                    bool needsRemeasure = false;
                    if (cs.flex > 0.0f)
                    {
                        /* flex: 1 children's final main size came from
                         * the grow distribution above and likely
                         * differs from the cascaded availW. */
                        needsRemeasure = true;
                    }
                    else if ((cs.widthIsPercent || cs.heightIsPercent) &&
                             (info.node->layout.w != info.node->intrinsicW ||
                              info.node->layout.h != info.node->intrinsicH))
                    {
                        /* Percent-sized child whose final size differs
                         * from its intrinsic — descendants need to see
                         * the new cell. */
                        needsRemeasure = true;
                    }
                    if (needsRemeasure)
                    {
                        /* Save arranged size — measure() overwrites
                         * layout.w/h with its (now-recomputed) intrinsic
                         * at the end, but the value we want preserved
                         * here is the post-flex-grow size that arrange
                         * just committed. */
                        const float savedW = info.node->layout.w;
                        const float savedH = info.node->layout.h;
                        const float savedX = info.node->layout.x;
                        const float savedY = info.node->layout.y;
                        measure(info.node, info.node->layout.w, info.node->layout.h, assets);
                        info.node->layout.w = savedW;
                        info.node->layout.h = savedH;
                        info.node->layout.x = savedX;
                        info.node->layout.y = savedY;
                    }
                    arrange(info.node, assets);
                    if (info.node->onLayout)
                        info.node->onLayout(info.node);
                }

                /* Absolute children (post-flow) */
                for (auto &child : node->children)
                {
                    const auto &cs = child->style;
                    if (cs.position != Position::Absolute) continue;

                    const float left   = cs.left;
                    const float right  = cs.right;
                    const float top    = cs.top;
                    const float bottom = cs.bottom;

                    if (isSet(left) && isSet(right))
                    {
                        child->layout.x = std::round(node->layout.x + left);
                        child->layout.w = std::round(node->layout.w - left - right);
                    }
                    else
                    {
                        if (cs.width > 0.0f)
                            child->layout.w = cs.width;
                        else if (cs.widthIsPercent)
                            child->layout.w = node->layout.w * (cs.width / 100.0f);

                        if (isSet(left))
                            child->layout.x = std::round(node->layout.x + left);
                        else if (isSet(right))
                            child->layout.x = std::round(node->layout.x + node->layout.w - child->layout.w - right);
                        else
                            child->layout.x = node->layout.x;
                    }

                    if (isSet(top) && isSet(bottom))
                    {
                        child->layout.y = std::round(node->layout.y + top);
                        child->layout.h = std::round(node->layout.h - top - bottom);
                    }
                    else
                    {
                        if (cs.height > 0.0f)
                            child->layout.h = cs.height;
                        else if (cs.heightIsPercent)
                            child->layout.h = node->layout.h * (cs.height / 100.0f);

                        if (isSet(top))
                            child->layout.y = std::round(node->layout.y + top);
                        else if (isSet(bottom))
                            child->layout.y = std::round(node->layout.y + node->layout.h - child->layout.h - bottom);
                        else
                            child->layout.y = node->layout.y;
                    }

                    arrange(child, assets);
                    if (child->onLayout) child->onLayout(child);
                }
            }

            void run(std::shared_ptr<Node> root,
                     float windowW, float windowH,
                     AssetPack *assets)
            {
                if (!root) return;

                /* Phase 1: measure. */
                measure(root, windowW, windowH, assets);

                /* Phase 2: arrange. The root has a definite outer rect
                 * (the whole window) — we set it before arrange. */
                root->layout.x = 0.0f;
                root->layout.y = 0.0f;
                /* Honor explicit width/height on the root; otherwise it
                 * fills the window. */
                if (root->style.widthIsPercent)
                    root->layout.w = windowW * (root->style.width / 100.0f);
                else if (root->style.width > 0.0f)
                    root->layout.w = root->style.width;
                else
                    root->layout.w = windowW;

                if (root->style.heightIsPercent)
                    root->layout.h = windowH * (root->style.height / 100.0f);
                else if (root->style.height > 0.0f)
                    root->layout.h = root->style.height;
                else
                    root->layout.h = windowH;

                arrange(root, assets);
                if (root->onLayout) root->onLayout(root);
            }
        }
    }
}
