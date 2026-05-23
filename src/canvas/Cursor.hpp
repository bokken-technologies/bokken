#pragma once

#include <cstdint>

namespace Bokken
{
    namespace Canvas
    {
        /**
         * Cursor shape requested when this node is hovered.
         *
         * Mapped to SDL_SystemCursor in the Canvas module. The default
         * (Default) means "don't request anything — let whatever the
         * window/app chose stand".
        */
        enum class Cursor : uint8_t
        {
            Default,
            Pointer,    // hand — for clickables
            Text,       // I-beam — for text inputs
            Move,       // 4-way arrows — for draggables
            NotAllowed, // crossed circle — for disabled controls
            Wait,       // hourglass/spinner
            ResizeNS,   // vertical resize
            ResizeEW,   // horizontal resize
            Crosshair
        };
    }
}
