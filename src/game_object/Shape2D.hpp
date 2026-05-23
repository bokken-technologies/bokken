#pragma once

#include <cstdint>

namespace Bokken
{
    namespace GameObject
    {
        // Primitive shapes for 2D rendering. Shape3D will mirror this when 3D lands.
        enum class Shape2D : uint8_t
        {
            Empty,
            Quad,
            Circle,
            Triangle,
            Line
        };
    }
}
