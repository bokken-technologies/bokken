#pragma once

#include "./effects/Base.hpp"
#include "./effects/Gain.hpp"
#include "Signal.hpp"

#include <string>
#include <vector>
#include <memory>
#include <algorithm>

namespace Bokken
{
    namespace Audio
    {
        /**
         * A single audio bus.
         *
         * Owns:
         *   - A scratch buffer that voices render into and effects mutate.
         *   - An ordered chain of effects, with an implicit Gain at index 0.
         *   - A list of "sends" — additional channels this bus feeds a copy of
         *     its post-effect output into (for shared reverb busses, etc).
         *
         * Signal flow per buffer:
         *
         *   1. clear scratch
         *   2. each voice on this channel adds its samples into scratch
         *   3. effects mutate scratch in order
         *   4. for each send, mix scratch into the target channel's scratch
         *      (post-fader sends mix the post-effect signal, pre-fader the pre)
         *   5. Mixer sums scratch into the master channel's scratch
        */
        class Channel
        {
        public:
            const std::string name;

            struct Send
            {
                uint32_t targetIndex; // index into Mixer's channel array
                float amount;         // 0..1 mix coefficient
                bool postFader;       // true = mix post-effects; false = mix pre-effects
            };

            explicit Channel(std::string n, float volume = 1.0f, bool muted = false)
                : name(std::move(n))
            {
                m_gain = new Effects::Gain(muted ? 0.0f : volume);
                m_chain.emplace_back(m_gain);
                m_volume = volume;
                m_muted = muted;
            }

            float volume() const { return m_volume; }
            bool muted() const { return m_muted; }

            Channel &setVolume(float v)
            {
                m_volume = v;
                m_gain->gain = m_muted ? 0.0f : v;
                return *this;
            }

            Channel &setMuted(bool m)
            {
                m_muted = m;
                m_gain->gain = m ? 0.0f : m_volume;
                return *this;
            }

            Channel &addEffect(Effects::Base *effect)
            {
                m_chain.emplace_back(effect);
                return *this;
            }

            Channel &removeEffect(Effects::Base *effect)
            {
                // Index 0 is the implicit gain — never remove it.
                m_chain.erase(
                    std::remove_if(m_chain.begin() + 1, m_chain.end(),
                                   [effect](const auto &e)
                                   { return e.get() == effect; }),
                    m_chain.end());
                return *this;
            }

            std::vector<Effects::Base *> getEffects() const
            {
                std::vector<Effects::Base *> out;
                out.reserve(m_chain.size() - 1);
                for (size_t i = 1; i < m_chain.size(); i++)
                    out.push_back(m_chain[i].get());
                return out;
            }

            //  Sends
            void addSend(uint32_t targetIndex, float amount, bool postFader = true)
            {
                m_sends.push_back({targetIndex, amount, postFader});
            }
            void clearSends() { m_sends.clear(); }
            const std::vector<Send> &sends() const { return m_sends; }

            //  Audio-thread-only
            // Resize the scratch buffer to match the device buffer size.
            // Called once at engine init (not on the audio thread).
            void resizeScratch(uint32_t frameCount, uint32_t channels)
            {
                m_scratch.assign(frameCount * channels, 0.0f);
                m_preFx.assign(frameCount * channels, 0.0f);
            }

            // Zero the scratch buffer for the next render pass.
            void clearScratch()
            {
                std::fill(m_scratch.begin(), m_scratch.end(), 0.0f);
            }

            float *scratch() { return m_scratch.data(); }
            float *preFx() { return m_preFx.data(); }
            size_t scratchSize() const { return m_scratch.size(); }

            // Run effects over our own scratch buffer.
            // Caller must have populated m_scratch with summed voice output first.
            void processEffects(uint32_t frameCount, uint32_t channels, uint32_t sampleRate)
            {
                // Snapshot pre-effect state for any pre-fader sends.
                if (!m_sends.empty())
                {
                    bool needsPre = false;
                    for (auto &s : m_sends)
                        if (!s.postFader) { needsPre = true; break; }
                    if (needsPre)
                        std::copy(m_scratch.begin(), m_scratch.end(), m_preFx.begin());
                }

                Signal sig{m_scratch.data(), frameCount, channels, sampleRate};
                for (auto &effect : m_chain)
                    if (effect->enabled)
                        effect->process(sig);
            }

        private:
            float m_volume = 1.0f;
            bool m_muted = false;

            Effects::Gain *m_gain = nullptr;
            std::vector<std::unique_ptr<Effects::Base>> m_chain;
            std::vector<Send> m_sends;

            // Audio-thread scratch — interleaved, sized to frameCount * channels.
            std::vector<float> m_scratch;
            std::vector<float> m_preFx; // pre-effect snapshot for pre-fader sends
        };
    }
}
