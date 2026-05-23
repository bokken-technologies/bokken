#pragma once

#include "Channel.hpp"
#include "Voice.hpp"
#include "Sound.hpp"
#include "ListenerState.hpp"
#include "CommandQueue.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Bokken
{
    namespace Audio
    {
        /**
         * The Mixer is the audio engine.
         *
         * Owns:
         *   - The SDL audio device + stream
         *   - All channels (Master + user-created busses)
         *   - The voice pool (active playing instances)
         *   - The command queue between game thread and audio thread
         *   - The current listener state
         *
         * Threading model:
         *   - The game thread calls every public method below.
         *   - All mutations are funnelled through the SPSC command queue.
         *   - The audio thread (SDL callback) drains commands at the start of
         *     each render, then renders voices → channels → master → device.
         *   - shared_ptr<Sound> is the only thing crossing thread boundaries
         *     by value; all other state is owned by exactly one thread.
        */
        class Mixer
        {
        public:
            static constexpr uint32_t MAX_VOICES = 256;
            static constexpr uint32_t DEFAULT_SAMPLE_RATE = 48000;
            static constexpr uint32_t DEFAULT_CHANNELS = 2;
            static constexpr uint32_t COMMAND_QUEUE_SIZE = 1024; // power of two

            static Mixer &get()
            {
                static Mixer instance;
                return instance;
            }

            //  Lifecycle
            // Open the SDL audio device and start the callback.
            // Safe to call multiple times — re-opens are no-ops if already running.
            bool start();

            // Stop the callback and close the device. Safe to call multiple times.
            void stop();

            bool isRunning() const { return m_deviceId != 0; }

            //  Channels (game thread)
            Channel *createChannel(const std::string &name, float volume = 1.0f, bool muted = false);
            Channel *channel(const std::string &name);
            Channel *master() { return m_channels[0].get(); }

            // Get the audio-thread index for a channel name. Used internally
            // when building Spawn commands so the audio thread doesn't need
            // to do string lookups in the hot path.
            uint32_t channelIndex(const std::string &name) const;

            //  Voices (game thread)
            // Spawn a voice on the given channel. Returns a generational handle
            // that's valid until the voice finishes or is stopped.
            VoiceId play(const std::shared_ptr<Sound> &sound,
                         const std::string &channelName,
                         float volume = 1.0f,
                         float pitch = 1.0f,
                         bool looping = false,
                         const SpatialParams &spatial = {});

            void stopVoice(VoiceId id, float fadeOutSeconds = 0.005f);
            void setVoiceVolume(VoiceId id, float volume, float fadeSeconds = 0.0f);
            void setVoicePitch(VoiceId id, float pitch);
            void setVoicePaused(VoiceId id, bool paused);
            void setVoiceSpatial(VoiceId id, const SpatialParams &spatial);

            // True if the voice is still allocated. May briefly return true
            // after the voice has finished but before the audio thread has
            // freed the slot — fine for "should I update its position" checks.
            bool isVoiceActive(VoiceId id) const;

            //  Listener (game thread)
            void updateListener(const ListenerState &state);

            //  Audio thread callback
            // Called by SDL on the audio thread. Public for the static trampoline.
            void renderCallback(float *out, uint32_t frameCount);

        private:
            Mixer();
            ~Mixer();
            Mixer(const Mixer &) = delete;
            Mixer &operator=(const Mixer &) = delete;

            // Generate a new VoiceId by reserving the next free slot.
            // Increments the slot's generation. Called from the game thread.
            VoiceId reserveVoiceSlot();

            // Audio-thread internals
            void drainCommands();
            void renderVoices(uint32_t frameCount);
            void renderChannels(uint32_t frameCount);
            void writeMaster(float *out, uint32_t frameCount);

            // SDL trampoline
            static void SDLCALL audioCallback(void *userdata, SDL_AudioStream *stream,
                                              int additionalAmount, int totalAmount);

            //  State
            // Channels: index 0 is always Master. Stored in a vector so the
            // audio thread can index by uint32_t without map lookups.
            std::vector<std::unique_ptr<Channel>> m_channels;
            std::unordered_map<std::string, uint32_t> m_channelLookup; // name → index
            mutable std::mutex m_channelMutex; // protects channel creation only

            // Voice pool — fixed-size, indexed by slot. Audio-thread only.
            std::array<Voice, MAX_VOICES> m_voices{};

            // Generation table — the *next* generation to use when reserving a slot.
            // Game thread bumps on reserve; audio thread does not touch.
            // Atomic so isVoiceActive() can read safely from the game thread.
            std::array<std::atomic<uint32_t>, MAX_VOICES> m_voiceGenerations{};
            std::array<std::atomic<bool>, MAX_VOICES> m_voiceReserved{};

            // The active listener — written by game thread, read by audio thread.
            // Sequence-locked: writers bump m_listenerSeq twice (odd=writing, even=stable),
            // readers retry if seq changed between read attempts.
            ListenerState m_listener;
            std::atomic<uint32_t> m_listenerSeq{0};

            // SPSC command queue
            CommandQueue<COMMAND_QUEUE_SIZE> m_queue;

            // Final mix scratch (interleaved, frameCount * channels)
            std::vector<float> m_masterScratch;

            // Device state
            SDL_AudioDeviceID m_deviceId = 0;
            SDL_AudioStream *m_stream = nullptr;
            uint32_t m_sampleRate = DEFAULT_SAMPLE_RATE;
            uint32_t m_outChannels = DEFAULT_CHANNELS;
            uint32_t m_bufferFrames = 1024; // requested device buffer size
        };
    }
}
