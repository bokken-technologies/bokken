#pragma once

#include <cstdint>

namespace Bokken
{
    namespace Canvas
    {
        /**
         * Horizontal alignment of glyphs within a Label's content box.
         *
         * Distinct from justifyContent — that one positions a Label inside
         * its parent's flex flow. textAlign positions glyphs inside the
         * Label itself once it has a width (either explicit or stretched
         * by alignItems).
        */
        enum class TextAlign : uint8_t
        {
            Left,
            Center,
            Right,
            Justify   // word-wrap mode only; falls back to Left without wrap
        };
    }
}
