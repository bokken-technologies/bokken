#pragma once

#include "Base.hpp"
#include "../Signal.hpp"

#include <vector>

namespace Bokken
{
    namespace Audio
    {
        namespace Effects
        {
            class LowPass final : public Base
            {
            public:
                float cutoff = 8000.0f;
                float resonance = 0.707f; // Unused in one-pole, reserved for biquad upgrade

                explicit LowPass(float cutoff = 8000.0f, float resonance = 0.707f)
                    : cutoff(cutoff), resonance(resonance) {}

                void process(Signal &signal) override
                {
                    // Recompute coefficient only when cutoff changes
                    const float rc = 1.0f / (2.0f * 3.14159265f * cutoff);
                    const float dt = 1.0f / static_cast<float>(signal.sampleRate);
                    const float a = dt / (rc + dt);

                    if (m_state.size() != signal.channels)
                        m_state.assign(signal.channels, 0.0f);

                    for (uint32_t f = 0; f < signal.frameCount; f++)
                    {
                        for (uint32_t c = 0; c < signal.channels; c++)
                        {
                            float &s = m_state[c];
                            s = s + a * (signal.data[f * signal.channels + c] - s);
                            signal.data[f * signal.channels + c] = s;
                        }
                    }
                }

            private:
                std::vector<float> m_state; // One per channel
            };

        }
    }
}