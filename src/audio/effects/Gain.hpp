#pragma once

#include "Base.hpp"
#include "../Signal.hpp"

namespace Bokken
{
    namespace Audio
    {
        namespace Effects
        {
            class Gain final : public Base
            {
            public:
                float gain = 1.0f;

                explicit Gain(float gain = 1.0f) : gain(gain) {}

                void process(Signal &signal) override
                {
                    for (uint32_t i = 0; i < signal.frameCount * signal.channels; i++)
                        signal.data[i] *= gain;
                }
            };
        }
    }
}