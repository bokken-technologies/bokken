#pragma once

#include "Base.hpp"
#include "../Signal.hpp"

#include <cstddef>
#include <vector>

namespace Bokken
{
    namespace Audio
    {
        namespace Effects
        {
            class Reverb final : public Base
            {
            public:
                float decay = 1.5f;
                float wet = 0.3f;

                explicit Reverb(float decay = 1.5f, float wet = 0.3f)
                    : decay(decay), wet(wet) {}

                void process(Signal &signal) override
                {
                    if (m_combs.empty())
                        buildBuffers(signal.sampleRate);

                    for (uint32_t f = 0; f < signal.frameCount; f++)
                    {
                        // Mix down to mono for reverb input
                        float mono = 0.0f;
                        for (uint32_t c = 0; c < signal.channels; c++)
                            mono += signal.data[f * signal.channels + c];
                        mono /= static_cast<float>(signal.channels);

                        // Four parallel comb filters
                        float rev = 0.0f;
                        for (auto &comb : m_combs)
                        {
                            float out = comb.buf[comb.head];
                            comb.buf[comb.head] = mono + out * decay * 0.5f;
                            comb.head = (comb.head + 1) % comb.buf.size();
                            rev += out;
                        }
                        rev *= 0.25f;

                        // Two series allpass filters
                        for (auto &ap : m_allpass)
                        {
                            float out = ap.buf[ap.head];
                            ap.buf[ap.head] = rev + out * 0.5f;
                            ap.head = (ap.head + 1) % ap.buf.size();
                            rev = out - rev;
                        }

                        // Write wet/dry mix back to all channels
                        for (uint32_t c = 0; c < signal.channels; c++)
                        {
                            const uint32_t i = f * signal.channels + c;
                            signal.data[i] = signal.data[i] * (1.0f - wet) + rev * wet;
                        }
                    }
                }

            private:
                struct DelayLine
                {
                    std::vector<float> buf;
                    uint32_t head = 0;
                };

                std::vector<DelayLine> m_combs;
                std::vector<DelayLine> m_allpass;

                void buildBuffers(uint32_t sampleRate)
                {
                    // Classic Schroeder comb delays (in ms)
                    for (float ms : {29.7f, 37.1f, 41.1f, 43.7f})
                    {
                        DelayLine d;
                        d.buf.assign(static_cast<size_t>(ms * 0.001f * sampleRate), 0.0f);
                        m_combs.push_back(std::move(d));
                    }
                    // Allpass delays
                    for (float ms : {5.0f, 1.7f})
                    {
                        DelayLine d;
                        d.buf.assign(static_cast<size_t>(ms * 0.001f * sampleRate), 0.0f);
                        m_allpass.push_back(std::move(d));
                    }
                }
            };
        }
    }
}