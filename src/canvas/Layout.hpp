#pragma once

#include "Node.hpp"
#include "../AssetPack.hpp"

#include <memory>

namespace Bokken
{
    namespace Canvas
    {
        /**
         * The Bokken Canvas layout engine.
         *
         * Two passes. First measure() walks the tree bottom-up and writes
         * each node's intrinsic size into intrinsicW/H. Then arrange()
         * walks top-down and sets layout.x/y/w/h on every node, resolving
         * flex grow/shrink, gap, alignment, wrapping, absolute positioning,
         * min/max constraints, and alignSelf.
         *
         * Why two passes
         * A single conflated pass only works when the sizing modes are
         * "fixed" and "percent of parent". Once flex-shrink,
         * alignSelf:Stretch, min/max constraints, and wrapping are in
         * play, a real intrinsic-size phase must run before the
         * distributive phase — otherwise grow/shrink can't tell what
         * "preferred size" means.
         *
         * The split also lets components override just one phase. View
         * leaves measure() to the engine and only customizes nothing;
         * Label overrides measure() to compute text size; ScrollView
         * overrides arrange() to translate children by its scroll
         * offset.
         *
         * Coordinate convention
         * After arrange(), every node's layout.x/y is in absolute screen
         * coordinates (not parent-relative), which is what the renderer
         * expects. Scroll offsets are baked into child positions during
         * arrange so the renderer doesn't need to know about scroll at
         * all.
        */
        namespace Layout
        {
            /**
             * Measure the subtree rooted at `node` against the given
             * constraints, writing intrinsicW/H on every node. The node's
             * layout.w/h is also seeded with a tentative value (the same
             * as intrinsic, clamped to constraints).
             *
             * `availW` and `availH` are the maximum content-area sizes
             * the parent can offer this node. For the top of the tree
             * pass (window width, window height).
            */
            void measure(std::shared_ptr<Node> node,
                         float availW, float availH,
                         AssetPack *assets);

            /**
             * Arrange the subtree, writing final layout.x/y/w/h on every
             * descendant. The node itself must already have layout.x/y/w/h
             * set by the caller — this is the entry point's responsibility
             * (typically: 0,0,windowW,windowH for the root).
             *
             * `assets` is needed because arrange may re-run measure on
             * subtrees whose parent's final size differed from its
             * measure-time prediction (typical for flex:1 children),
             * and measure needs the asset pack to resolve fonts when
             * computing Label sizes.
            */
            void arrange(std::shared_ptr<Node> node, AssetPack *assets);

            /**
             * Convenience: run measure + arrange against window-size
             * constraints. The Canvas module calls this on every render
             * commit and on window resize.
            */
            void run(std::shared_ptr<Node> root,
                    float windowW, float windowH,
                    AssetPack *assets);
        }
    }
}
