#pragma once

#include <cstdint>

namespace Bokken
{
    namespace Canvas
    {
        /**
         * justifyContent values.
         *
         * The original Align enum (Start/Center/End) was overloaded for both
         * justifyContent (main-axis distribution) and alignItems (cross-axis
         * placement). It worked, but it couldn't express the space-* family
         * that web flexbox provides — and those are the ones that pay off
         * the most for real UI: even spacing of toolbar buttons, evenly
         * distributed nav items, "push the last child to the far edge"
         * layouts via SpaceBetween.
         *
         * We keep Align as the cross-axis enum (it doesn't need the extras)
         * and introduce Justify here for the main axis. The JS bridge
         * accepts the same string names; "Start"/"Center"/"End" still work
         * for backward compatibility.
        */
        enum class Justify : uint8_t
        {
            Start,
            Center,
            End,
            SpaceBetween,  // first/last flush to edges, equal gaps between
            SpaceAround,   // equal gaps with half-gap padding at each edge
            SpaceEvenly    // equal gaps including outer edges
        };
    }
}
