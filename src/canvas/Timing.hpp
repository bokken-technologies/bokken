#pragma once

#include <cstdint>

namespace Bokken
{
    namespace Canvas
    {
        enum class Timing : uint8_t
        {
            Linear,
            EaseIn,
            EaseOut,
            EaseInOut,
            Bounce,
            Back,
            Step
        };
    }
}