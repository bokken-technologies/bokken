#pragma once

#include "Base.hpp"
#include "../Signal.hpp"

#include <algorithm>
#include <cmath>

namespace Bokken
{
    namespace Audio
    {
        namespace Effects
        {
            class Compressor final : public Base
            {
            public:
                float threshold = -12.0f; // dBFS
                float ratio = 4.0f;
                float attack = 0.003f;
                float release = 0.25f;

                explicit Compressor(float threshold = -12.0f, float ratio = 4.0f,
                                    float attack = 0.003f, float release = 0.25f)
                    : threshold(threshold), ratio(ratio), attack(attack), release(release) {}

                void process(Signal &signal) override
                {
                    const float dt = 1.0f / static_cast<float>(signal.sampleRate);
                    const float attackCoef = std::exp(-dt / attack);
                    const float releaseCoef = std::exp(-dt / release);
                    const float threshLin = std::pow(10.0f, threshold / 20.0f);

                    for (uint32_t f = 0; f < signal.frameCount; f++)
                    {
                        // Peak detection across channels
                        float peak = 0.0f;
                        for (uint32_t c = 0; c < signal.channels; c++)
                            peak = std::max(peak, std::abs(signal.data[f * signal.channels + c]));

                        // Envelope follower
                        if (peak > m_envelope)
                            m_envelope = attackCoef * m_envelope + (1.0f - attackCoef) * peak;
                        else
                            m_envelope = releaseCoef * m_envelope + (1.0f - releaseCoef) * peak;

                        // Gain reduction
                        float gain = 1.0f;
                        if (m_envelope > threshLin)
                            gain = threshLin + (m_envelope - threshLin) / ratio;

                        const float gr = (m_envelope > 0.0f) ? (gain / m_envelope) : 1.0f;
                        for (uint32_t c = 0; c < signal.channels; c++)
                            signal.data[f * signal.channels + c] *= gr;
                    }
                }

            private:
                float m_envelope = 0.0f;
            };

        }
    }
}