#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>

namespace Bokken
{
    namespace Audio
    {
        /**
         * A decoded audio asset held entirely in memory as interleaved float32 PCM.
         *
         * Created via Audio::load(path), which reads bytes through the asset pack
         * (PhysFS) and decodes them with dr_wav. The decoded buffer is shared via
         * shared_ptr so a single Sound can be referenced by many AudioSources
         * without copying.
         *
         * Sound objects are immutable after load — playback parameters (volume,
         * pitch, looping, spatialisation) live on the AudioSource, not here.
        */
        class Sound
        {
        public:
            // Decode a WAV file from the asset pack. Returns nullptr on failure.
            // Always called from the game thread, never from the audio callback.
            static std::shared_ptr<Sound> loadFromPack(const std::string &virtualPath);

            // Decode WAV bytes already in memory (used by loadFromPack and tests).
            static std::shared_ptr<Sound> decodeWavBytes(const uint8_t *bytes, size_t len);

            uint32_t channels() const { return m_channels; }
            uint32_t sampleRate() const { return m_sampleRate; }
            uint64_t frameCount() const { return m_frameCount; }
            float duration() const { return static_cast<float>(m_frameCount) / static_cast<float>(m_sampleRate); }
            const std::string &path() const { return m_path; }

            // Raw access for the audio thread — read-only, lifetime tied to shared_ptr.
            const float *data() const { return m_pcm.data(); }
            size_t sampleCount() const { return m_pcm.size(); }

        private:
            Sound() = default;

            std::vector<float> m_pcm; // interleaved float32, length = frameCount * channels
            uint32_t m_channels = 0;
            uint32_t m_sampleRate = 0;
            uint64_t m_frameCount = 0;
            std::string m_path;
        };
    }
}
