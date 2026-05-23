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
            class Delay final : public Base
            {
            public:
                float time = 0.3f;
                float feedback = 0.4f;
                float wet = 0.35f;

                explicit Delay(float time = 0.3f, float feedback = 0.4f, float wet = 0.35f)
                    : time(time), feedback(feedback), wet(wet) {}

                void process(Signal &signal) override
                {
                    const uint32_t delaySamples =
                        static_cast<uint32_t>(time * static_cast<float>(signal.sampleRate));

                    // Resize ring buffers if channel count or delay time changed
                    if (m_ring.size() != signal.channels || m_ring[0].size() != delaySamples)
                    {
                        m_ring.assign(signal.channels, std::vector<float>(delaySamples, 0.0f));
                        m_head.assign(signal.channels, 0u);
                    }

                    for (uint32_t f = 0; f < signal.frameCount; f++)
                    {
                        for (uint32_t c = 0; c < signal.channels; c++)
                        {
                            const uint32_t i = f * signal.channels + c;
                            const float dry = signal.data[i];
                            const float delayed = m_ring[c][m_head[c]];

                            m_ring[c][m_head[c]] = dry + delayed * feedback;
                            m_head[c] = (m_head[c] + 1) % delaySamples;

                            signal.data[i] = dry + delayed * wet;
                        }
                    }
                }

            private:
                std::vector<std::vector<float>> m_ring;
                std::vector<uint32_t> m_head;
            };

        }
    }
}