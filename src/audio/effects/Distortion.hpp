#pragma once

#include "Base.hpp"
#include "../Signal.hpp"

#include <cmath>
#include <vector>

namespace Bokken
{
    namespace Audio
    {
        namespace Effects
        {
            class Distortion final : public Base
            {
            public:
                float drive = 0.5f;
                float tone = 0.5f;

                explicit Distortion(float drive = 0.5f, float tone = 0.5f)
                    : drive(drive), tone(tone) {}

                void process(Signal &signal) override
                {
                    const float gain = 1.0f + drive * 49.0f; // 1x – 50x
                    const float cutoff = 1000.0f + tone * 7000.0f;
                    const float rc = 1.0f / (2.0f * 3.14159265f * cutoff);
                    const float dt = 1.0f / static_cast<float>(signal.sampleRate);
                    const float a = dt / (rc + dt);

                    if (m_lp.size() != signal.channels)
                        m_lp.assign(signal.channels, 0.0f);

                    for (uint32_t f = 0; f < signal.frameCount; f++)
                    {
                        for (uint32_t c = 0; c < signal.channels; c++)
                        {
                            const uint32_t i = f * signal.channels + c;

                            // Drive
                            float s = signal.data[i] * gain;

                            // Soft clip (tanh)
                            s = std::tanh(s);

                            // Tone (post low-pass)
                            m_lp[c] = m_lp[c] + a * (s - m_lp[c]);
                            signal.data[i] = m_lp[c];
                        }
                    }
                }

            private:
                std::vector<float> m_lp;
            };

        }
    }
}