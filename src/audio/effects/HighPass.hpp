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
            class HighPass final : public Base
            {
            public:
                float cutoff = 800.0f;
                float resonance = 0.707f;

                explicit HighPass(float cutoff = 800.0f, float resonance = 0.707f)
                    : cutoff(cutoff), resonance(resonance) {}

                void process(Signal &signal) override
                {
                    const float rc = 1.0f / (2.0f * 3.14159265f * cutoff);
                    const float dt = 1.0f / static_cast<float>(signal.sampleRate);
                    const float a = rc / (rc + dt);

                    if (m_state.size() != signal.channels)
                    {
                        m_state.assign(signal.channels, 0.0f);
                        m_prev.assign(signal.channels, 0.0f);
                    }

                    for (uint32_t f = 0; f < signal.frameCount; f++)
                    {
                        for (uint32_t c = 0; c < signal.channels; c++)
                        {
                            const uint32_t i = f * signal.channels + c;
                            float &s = m_state[c];
                            s = a * (s + signal.data[i] - m_prev[c]);
                            m_prev[c] = signal.data[i];
                            signal.data[i] = s;
                        }
                    }
                }

            private:
                std::vector<float> m_state;
                std::vector<float> m_prev;
            };
        }
    }
}