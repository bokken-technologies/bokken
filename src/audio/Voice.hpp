#pragma once

#include "Sound.hpp"
#include "Signal.hpp"
#include "ListenerState.hpp"

#include <glm/glm.hpp>
#include <memory>
#include <atomic>
#include <cstdint>

namespace Bokken
{
    namespace Audio
    {
        /**
         * Generational handle for a playing voice.
         *
         * A 64-bit value combining a slot index (low bits) and a generation
         * counter (high bits). When the audio thread reuses a slot the
         * generation increments, so any stale handle from the game thread
         * becomes a safe no-op instead of a use-after-free.
        */
        using VoiceId = uint64_t;
        constexpr VoiceId INVALID_VOICE = 0;

        inline VoiceId makeVoiceId(uint32_t slot, uint32_t generation)
        {
            return (static_cast<uint64_t>(generation) << 32) | static_cast<uint64_t>(slot);
        }
        inline uint32_t voiceSlot(VoiceId id) { return static_cast<uint32_t>(id & 0xFFFFFFFFu); }
        inline uint32_t voiceGeneration(VoiceId id) { return static_cast<uint32_t>(id >> 32); }

        /**
         * Spatial parameters for a 3D voice. When `enabled` is false the
         * voice plays as 2D (no attenuation, no panning by position).
        */
        struct SpatialParams
        {
            bool enabled = false;
            glm::vec3 position{0.0f};
            glm::vec3 velocity{0.0f};
            float minDistance = 1.0f;
            float maxDistance = 50.0f;
            float rolloff = 1.0f;
            bool dopplerEnabled = false;
        };

        /**
         * A playing instance of a Sound, scheduled on a Channel.
         *
         * Lives on the audio thread. The game thread never touches Voice
         * fields directly — it sends commands through the Mixer queue.
        */
        struct Voice
        {
            // Identity
            uint32_t generation = 0; // bumped each time the slot is reused
            bool active = false;

            // Source data — shared_ptr keeps the PCM alive for the voice's lifetime.
            // Refcount manipulation only happens on the game thread when the voice
            // is created/destroyed (commands carry shared_ptr by value).
            std::shared_ptr<Sound> sound;

            // Routing
            uint32_t channelIndex = 0; // index into Mixer's channel array

            // Playback cursor (in frames within the source). Stored as
            // a double rather than an integer because the cursor advances
            // by `pitch` per output frame, and pitch is generally not
            // integer — it captures both intentional pitch-shifts and
            // the source/device sample-rate ratio (e.g. 22050/48000).
            // The loop body decomposes it into integer index + [0,1)
            // fraction for linear interpolation between adjacent frames.
            //
            // 64-bit double has more than 15 decimal digits of mantissa.
            // Even a 24-hour 192kHz playback would only consume ~10
            // significant digits, so cumulative round-off is well below
            // any audibly meaningful threshold.
            double cursor = 0.0;

            // Per-voice params (ramped per buffer to avoid clicks)
            float volume = 1.0f;
            float targetVolume = 1.0f;
            float pitch = 1.0f;
            bool looping = false;
            bool paused = false;

            // Fade envelope (in seconds remaining; 0 means no fade in progress)
            float fadeRemaining = 0.0f;
            float fadeFromVolume = 0.0f;
            float fadeToVolume = 0.0f;

            // Spatial
            SpatialParams spatial;

            void reset()
            {
                active = false;
                sound.reset();
                channelIndex = 0;
                cursor = 0.0;
                volume = 1.0f;
                targetVolume = 1.0f;
                pitch = 1.0f;
                looping = false;
                paused = false;

                // Clear *all* fade state, not just fadeRemaining.
                // The retire condition in renderVoices uses
                // fadeFromVolume and fadeToVolume to detect
                // completed fade-outs — if those carry stale
                // values from the previous voice in this slot,
                // the new voice retires on its very first
                // buffer (you hear a millisecond of audio then
                // silence). Resetting all three fade fields
                // makes the new voice indistinguishable from a
                // freshly-allocated slot.
                fadeRemaining = 0.0f;
                fadeFromVolume = 0.0f;
                fadeToVolume = 0.0f;

                spatial = {};
            }
        };
    }
}
