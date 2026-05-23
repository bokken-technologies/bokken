#pragma once

#include <cstdint>

namespace Bokken
{
    namespace Audio
    {
        /**
         * The raw audio buffer passed through the effect chain.
         * Interleaved float32 samples — frameCount * channels floats total.
         * Effects mutate data in-place and return the same buffer.
        */
        struct Signal
        {
            float *data;
            uint32_t frameCount;
            uint32_t channels;
            uint32_t sampleRate;
        };
    }
}
