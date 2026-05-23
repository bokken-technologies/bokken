#pragma once

#include "Voice.hpp"
#include "Sound.hpp"

#include <glm/glm.hpp>
#include <atomic>
#include <array>
#include <memory>
#include <cstdint>
#include <variant>
#include <string>

namespace Bokken
{
    namespace Audio
    {
        // Command types
        // Each command is a small POD-ish payload. shared_ptr is fine here:
        // it gets copied/moved on the game thread (when the command is built
        // and pushed) and on the audio thread (when consumed and stored on a
        // Voice). The audio thread releases its refcount when the voice
        // finishes — and shared_ptr's atomic ref counting is wait-free for
        // the operations we actually do.

        struct CmdSpawnVoice
        {
            VoiceId expectedId;
            std::shared_ptr<Sound> sound;
            uint32_t channelIndex;
            float volume;
            float pitch;
            bool looping;
            SpatialParams spatial;
        };

        struct CmdStopVoice
        {
            VoiceId id;
            float fadeOutSeconds; // 0.0f for immediate (still ramped over one buffer to dodge clicks)
        };

        struct CmdSetVoiceVolume
        {
            VoiceId id;
            float volume;
            float fadeSeconds; // 0 = snap (still buffer-ramped)
        };

        struct CmdSetVoicePitch
        {
            VoiceId id;
            float pitch;
        };

        struct CmdSetVoicePaused
        {
            VoiceId id;
            bool paused;
        };

        struct CmdSetVoiceSpatial
        {
            VoiceId id;
            SpatialParams spatial;
        };

        struct CmdUpdateListener
        {
            ListenerState state;
        };

        using Command = std::variant<
            CmdSpawnVoice,
            CmdStopVoice,
            CmdSetVoiceVolume,
            CmdSetVoicePitch,
            CmdSetVoicePaused,
            CmdSetVoiceSpatial,
            CmdUpdateListener>;

        // Ring buffer
        // Single producer (game thread) → single consumer (audio thread).
        // Power-of-two capacity for cheap masking.
        template <size_t Capacity>
        class CommandQueue
        {
            static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two");

        public:
            // Producer: returns false if the queue is full (caller should drop or retry).
            bool push(Command cmd)
            {
                const size_t head = m_head.load(std::memory_order_relaxed);
                const size_t next = (head + 1) & (Capacity - 1);
                if (next == m_tail.load(std::memory_order_acquire))
                    return false; // full
                m_buffer[head] = std::move(cmd);
                m_head.store(next, std::memory_order_release);
                return true;
            }

            // Consumer: returns false if empty.
            bool pop(Command &out)
            {
                const size_t tail = m_tail.load(std::memory_order_relaxed);
                if (tail == m_head.load(std::memory_order_acquire))
                    return false;
                out = std::move(m_buffer[tail]);
                m_tail.store((tail + 1) & (Capacity - 1), std::memory_order_release);
                return true;
            }

        private:
            std::array<Command, Capacity> m_buffer{};
            std::atomic<size_t> m_head{0};
            std::atomic<size_t> m_tail{0};
        };
    }
}
