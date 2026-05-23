#include "Mixer.hpp"

namespace Bokken
{
    namespace Audio
    {
        Mixer::Mixer()
        {
            // Master channel always exists at index 0.
            auto master = std::make_unique<Channel>("Master", 1.0f, false);
            m_channels.push_back(std::move(master));
            m_channelLookup["Master"] = 0;

            for (auto &g : m_voiceGenerations) g.store(1, std::memory_order_relaxed);
            for (auto &r : m_voiceReserved) r.store(false, std::memory_order_relaxed);
        }

        Mixer::~Mixer() { stop(); }

        // Lifecycle
        bool Mixer::start()
        {
            if (m_deviceId != 0)
                return true;

            // Initialise SDL audio if not already.
            if (!SDL_WasInit(SDL_INIT_AUDIO))
            {
                if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
                {
                    SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
                                 "[Audio] SDL_InitSubSystem(AUDIO) failed: %s", SDL_GetError());
                    return false;
                }
            }

            SDL_AudioSpec spec{};
            spec.format = SDL_AUDIO_F32;
            spec.channels = static_cast<int>(m_outChannels);
            spec.freq = static_cast<int>(m_sampleRate);

            // Open default playback device with a stream.
            m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                                 &spec, &Mixer::audioCallback, this);
            if (!m_stream)
            {
                SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
                             "[Audio] SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
                return false;
            }

            m_deviceId = SDL_GetAudioStreamDevice(m_stream);

            // Allocate scratch buffers sized for our nominal buffer.
            // SDL may request more or less per callback; we'll grow dynamically if needed.
            m_masterScratch.assign(m_bufferFrames * m_outChannels, 0.0f);
            for (auto &ch : m_channels)
                ch->resizeScratch(m_bufferFrames, m_outChannels);

            // Resume — devices open paused.
            SDL_ResumeAudioDevice(m_deviceId);

            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "[Audio] Mixer started — %u Hz, %u channels", m_sampleRate, m_outChannels);
            return true;
        }

        void Mixer::stop()
        {
            if (m_stream)
            {
                SDL_DestroyAudioStream(m_stream);
                m_stream = nullptr;
            }
            m_deviceId = 0;
        }

        // Channels
        Channel *Mixer::createChannel(const std::string &name, float volume, bool muted)
        {
            std::lock_guard<std::mutex> lk(m_channelMutex);

            auto it = m_channelLookup.find(name);
            if (it != m_channelLookup.end())
                return m_channels[it->second].get();

            auto ch = std::make_unique<Channel>(name, volume, muted);
            ch->resizeScratch(m_bufferFrames, m_outChannels);

            const uint32_t index = static_cast<uint32_t>(m_channels.size());
            m_channelLookup[name] = index;
            m_channels.push_back(std::move(ch));
            return m_channels.back().get();
        }

        Channel *Mixer::channel(const std::string &name)
        {
            std::lock_guard<std::mutex> lk(m_channelMutex);
            auto it = m_channelLookup.find(name);
            return it != m_channelLookup.end() ? m_channels[it->second].get() : nullptr;
        }

        uint32_t Mixer::channelIndex(const std::string &name) const
        {
            std::lock_guard<std::mutex> lk(m_channelMutex);
            auto it = m_channelLookup.find(name);
            return it != m_channelLookup.end() ? it->second : 0; // 0 = Master fallback
        }

        // Voices
        VoiceId Mixer::reserveVoiceSlot()
        {
            for (uint32_t i = 0; i < MAX_VOICES; i++)
            {
                bool expected = false;
                if (m_voiceReserved[i].compare_exchange_strong(expected, true,
                                                               std::memory_order_acq_rel))
                {
                    const uint32_t gen = m_voiceGenerations[i].load(std::memory_order_relaxed);
                    return makeVoiceId(i, gen);
                }
            }
            return INVALID_VOICE;
        }

        VoiceId Mixer::play(const std::shared_ptr<Sound> &sound,
                            const std::string &channelName,
                            float volume, float pitch, bool looping,
                            const SpatialParams &spatial)
        {
            if (!sound)
                return INVALID_VOICE;

            const VoiceId id = reserveVoiceSlot();
            if (id == INVALID_VOICE)
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                            "[Audio] Voice pool exhausted (max %u). Sound dropped: %s",
                            MAX_VOICES, sound->path().c_str());
                return INVALID_VOICE;
            }

            CmdSpawnVoice spawn{};
            spawn.expectedId = id;
            spawn.sound = sound;
            spawn.channelIndex = channelIndex(channelName);
            spawn.volume = volume;
            spawn.pitch = pitch;
            spawn.looping = looping;
            spawn.spatial = spatial;

            if (!m_queue.push(Command{std::move(spawn)}))
            {
                // Queue full — release the reservation.
                m_voiceReserved[voiceSlot(id)].store(false, std::memory_order_release);
                SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "[Audio] Command queue full; spawn dropped");
                return INVALID_VOICE;
            }

            return id;
        }

        void Mixer::stopVoice(VoiceId id, float fadeOutSeconds)
        {
            if (id == INVALID_VOICE) return;
            m_queue.push(Command{CmdStopVoice{id, fadeOutSeconds}});
        }

        void Mixer::setVoiceVolume(VoiceId id, float volume, float fadeSeconds)
        {
            if (id == INVALID_VOICE) return;
            m_queue.push(Command{CmdSetVoiceVolume{id, volume, fadeSeconds}});
        }

        void Mixer::setVoicePitch(VoiceId id, float pitch)
        {
            if (id == INVALID_VOICE) return;
            m_queue.push(Command{CmdSetVoicePitch{id, pitch}});
        }

        void Mixer::setVoicePaused(VoiceId id, bool paused)
        {
            if (id == INVALID_VOICE) return;
            m_queue.push(Command{CmdSetVoicePaused{id, paused}});
        }

        void Mixer::setVoiceSpatial(VoiceId id, const SpatialParams &spatial)
        {
            if (id == INVALID_VOICE) return;
            m_queue.push(Command{CmdSetVoiceSpatial{id, spatial}});
        }

        bool Mixer::isVoiceActive(VoiceId id) const
        {
            if (id == INVALID_VOICE) return false;
            const uint32_t slot = voiceSlot(id);
            const uint32_t gen = voiceGeneration(id);
            if (slot >= MAX_VOICES) return false;
            // Generation match means our handle is still valid.
            return m_voiceGenerations[slot].load(std::memory_order_acquire) == gen
                && m_voiceReserved[slot].load(std::memory_order_acquire);
        }

        // Listener
        void Mixer::updateListener(const ListenerState &state)
        {
            // Use the command queue so the listener update is ordered with
            // voice spawns (a spawn that uses the new listener won't see stale data).
            m_queue.push(Command{CmdUpdateListener{state}});
        }

        // Audio thread
        void SDLCALL Mixer::audioCallback(void *userdata, SDL_AudioStream *stream,
                                          int additionalAmount, int /*totalAmount*/)
        {
            auto *self = static_cast<Mixer *>(userdata);
            if (additionalAmount <= 0) return;

            // additionalAmount is bytes; convert to frames.
            const uint32_t bytesPerFrame = sizeof(float) * self->m_outChannels;
            const uint32_t frameCount = static_cast<uint32_t>(additionalAmount) / bytesPerFrame;
            if (frameCount == 0) return;

            // Grow scratch if SDL asked for more than our nominal buffer.
            if (self->m_masterScratch.size() < frameCount * self->m_outChannels)
            {
                self->m_masterScratch.assign(frameCount * self->m_outChannels, 0.0f);
                for (auto &ch : self->m_channels)
                    ch->resizeScratch(frameCount, self->m_outChannels);
            }

            self->renderCallback(self->m_masterScratch.data(), frameCount);

            SDL_PutAudioStreamData(stream, self->m_masterScratch.data(),
                                   static_cast<int>(frameCount * bytesPerFrame));
        }

        void Mixer::renderCallback(float *out, uint32_t frameCount)
        {
            // 1. Drain pending commands from the game thread.
            drainCommands();

            // 2. Clear all channel scratch buffers.
            for (auto &ch : m_channels)
                ch->clearScratch();

            // 3. Render each active voice into its channel's scratch.
            renderVoices(frameCount);

            // 4. Run channel effects + sends.
            renderChannels(frameCount);

            // 5. Sum non-master channels into master, run master's effects,
            //    write to output buffer.
            writeMaster(out, frameCount);
        }

        void Mixer::drainCommands()
        {
            Command cmd;
            while (m_queue.pop(cmd))
            {
                std::visit([this](auto &c) {
                    using T = std::decay_t<decltype(c)>;

                    if constexpr (std::is_same_v<T, CmdSpawnVoice>) {
                        const uint32_t slot = voiceSlot(c.expectedId);
                        if (slot >= MAX_VOICES) return;
                        Voice &v = m_voices[slot];
                        v.reset();
                        v.active = true;
                        v.generation = voiceGeneration(c.expectedId);
                        v.sound = std::move(c.sound);
                        v.channelIndex = c.channelIndex < m_channels.size() ? c.channelIndex : 0;
                        v.volume = c.volume;
                        v.targetVolume = c.volume;
                        v.pitch = c.pitch;
                        v.looping = c.looping;
                        v.spatial = c.spatial;
                    }
                    else if constexpr (std::is_same_v<T, CmdStopVoice>) {
                        const uint32_t slot = voiceSlot(c.id);
                        if (slot >= MAX_VOICES) return;
                        Voice &v = m_voices[slot];
                        if (v.generation != voiceGeneration(c.id) || !v.active) return;
                        v.fadeRemaining = std::max(c.fadeOutSeconds, 1.0f / static_cast<float>(m_sampleRate));
                        v.fadeFromVolume = v.volume;
                        v.fadeToVolume = 0.0f;
                        // active stays true until fade completes; renderVoices kills it then.
                    }
                    else if constexpr (std::is_same_v<T, CmdSetVoiceVolume>) {
                        const uint32_t slot = voiceSlot(c.id);
                        if (slot >= MAX_VOICES) return;
                        Voice &v = m_voices[slot];
                        if (v.generation != voiceGeneration(c.id) || !v.active) return;
                        if (c.fadeSeconds > 0.0f) {
                            v.fadeRemaining = c.fadeSeconds;
                            v.fadeFromVolume = v.volume;
                            v.fadeToVolume = c.volume;
                        } else {
                            v.targetVolume = c.volume;
                        }
                    }
                    else if constexpr (std::is_same_v<T, CmdSetVoicePitch>) {
                        const uint32_t slot = voiceSlot(c.id);
                        if (slot >= MAX_VOICES || m_voices[slot].generation != voiceGeneration(c.id)) return;
                        m_voices[slot].pitch = std::max(0.01f, c.pitch);
                    }
                    else if constexpr (std::is_same_v<T, CmdSetVoicePaused>) {
                        const uint32_t slot = voiceSlot(c.id);
                        if (slot >= MAX_VOICES || m_voices[slot].generation != voiceGeneration(c.id)) return;
                        m_voices[slot].paused = c.paused;
                    }
                    else if constexpr (std::is_same_v<T, CmdSetVoiceSpatial>) {
                        const uint32_t slot = voiceSlot(c.id);
                        if (slot >= MAX_VOICES || m_voices[slot].generation != voiceGeneration(c.id)) return;
                        m_voices[slot].spatial = c.spatial;
                    }
                    else if constexpr (std::is_same_v<T, CmdUpdateListener>) {
                        m_listener = c.state;
                    }
                }, cmd);
            }
        }

        // Compute per-channel gains for a spatial voice.
        // Returns {leftGain, rightGain} for stereo output. Mono input is
        // panned; stereo input is attenuated equally on both channels.
        static void computeSpatialGains(const SpatialParams &sp,
                                        const ListenerState &lst,
                                        float &outLeft, float &outRight)
        {
            const glm::vec3 toSource = sp.position - lst.position;
            const float dist = glm::length(toSource);

            // Distance attenuation — inverse-distance with min/max clamping.
            float atten;
            if (dist <= sp.minDistance) {
                atten = 1.0f;
            } else if (dist >= sp.maxDistance) {
                atten = 0.0f;
            } else {
                const float t = (dist - sp.minDistance) / (sp.maxDistance - sp.minDistance);
                atten = std::pow(1.0f - t, sp.rolloff);
            }

            // Pan: project onto listener's right vector (forward × up).
            const glm::vec3 right = glm::normalize(glm::cross(lst.forward, lst.up));
            float pan = 0.0f;
            if (dist > 1e-4f) {
                const glm::vec3 dir = toSource / dist;
                pan = glm::dot(dir, right); // -1 = full left, +1 = full right
            }
            pan = std::max(-1.0f, std::min(1.0f, pan));

            // Equal-power pan
            const float angle = (pan + 1.0f) * 0.25f * 3.14159265f; // [0, π/2]
            outLeft = atten * std::cos(angle);
            outRight = atten * std::sin(angle);
        }

        void Mixer::renderVoices(uint32_t frameCount)
        {
            const float invSr = 1.0f / static_cast<float>(m_sampleRate);
            const float bufferSeconds = static_cast<float>(frameCount) * invSr;

            for (uint32_t slot = 0; slot < MAX_VOICES; slot++)
            {
                Voice &v = m_voices[slot];
                if (!v.active || v.paused || !v.sound) continue;

                Channel &ch = *m_channels[v.channelIndex];
                float *dst = ch.scratch();

                const Sound &snd = *v.sound;
                const uint32_t srcChannels = snd.channels();
                const uint64_t srcFrames = snd.frameCount();
                const float *src = snd.data();

                // Per-buffer volume ramp: target → current
                const float startVol = v.volume;
                float endVol = v.targetVolume;

                // Apply fade if active
                if (v.fadeRemaining > 0.0f)
                {
                    const float t = std::min(bufferSeconds, v.fadeRemaining) / v.fadeRemaining;
                    endVol = v.fadeFromVolume + (v.fadeToVolume - v.fadeFromVolume) * t;
                    v.fadeRemaining -= bufferSeconds;
                    v.targetVolume = endVol;
                }

                // Spatial gains (recomputed per buffer; smooth enough at 1024 frames).
                float spatialL = 1.0f, spatialR = 1.0f;
                if (v.spatial.enabled)
                    computeSpatialGains(v.spatial, m_listener, spatialL, spatialR);

                // Sample-by-sample mix with linear ramp over the buffer.
                const float volStep = (endVol - startVol) / static_cast<float>(frameCount);
                float vol = startVol;

                bool finished = false;
                for (uint32_t f = 0; f < frameCount; f++)
                {
                    if (v.cursor >= static_cast<double>(srcFrames))
                    {
                        if (v.looping) {
                            // Wrap, preserving any fractional overshoot
                            // so a looping voice keeps consistent pitch
                            // across the seam.
                            v.cursor -= static_cast<double>(srcFrames);
                            if (v.cursor < 0.0) v.cursor = 0.0;
                        } else {
                            finished = true;
                            break;
                        }
                    }

                    // Decompose the fractional cursor into integer index
                    // and [0, 1) fraction for linear interpolation. This
                    // is the core of the resampler — for pitch == 1.0
                    // frac is 0 and the result is bit-exact equal to
                    // reading src[i0] directly, so unity-pitch playback
                    // is unaffected by the change.
                    const uint64_t i0 = static_cast<uint64_t>(v.cursor);
                    const uint64_t i1 = std::min(i0 + 1, srcFrames - 1);
                    const float frac = static_cast<float>(v.cursor - static_cast<double>(i0));

                    // Read source frame, interpolating between i0 and i1.
                    // Stereo gets the same treatment per-channel — the
                    // previous code skipped interpolation for stereo,
                    // which made any non-unity pitch sound stepped on
                    // stereo sources.
                    float sl, sr;
                    if (srcChannels == 1) {
                        const float s = src[i0] * (1.0f - frac) + src[i1] * frac;
                        sl = sr = s;
                    } else {
                        const float l0 = src[i0 * srcChannels + 0];
                        const float l1 = src[i1 * srcChannels + 0];
                        const float r0 = src[i0 * srcChannels + 1];
                        const float r1 = src[i1 * srcChannels + 1];
                        sl = l0 * (1.0f - frac) + l1 * frac;
                        sr = r0 * (1.0f - frac) + r1 * frac;
                    }

                    // Apply volume + spatial pan + accumulate
                    const float gL = vol * spatialL;
                    const float gR = vol * spatialR;
                    dst[f * m_outChannels + 0] += sl * gL;
                    if (m_outChannels >= 2)
                        dst[f * m_outChannels + 1] += sr * gR;

                    // Advance the cursor by exactly pitch frames.
                    // Sub-unity pitch (slow playback) and supra-unity
                    // pitch (fast / pitch-up) are both handled the
                    // same way — the fractional part of cursor drives
                    // the interpolation on the next output frame.
                    v.cursor += static_cast<double>(v.pitch);
                    vol += volStep;
                }

                v.volume = endVol;

                // Voice end conditions.
                if (finished || (v.fadeRemaining <= 0.0f && v.fadeToVolume == 0.0f && v.fadeFromVolume > 0.0f))
                {
                    v.active = false;
                    v.sound.reset();
                    // Bump generation so any stale handle stops matching.
                    m_voiceGenerations[slot].fetch_add(1, std::memory_order_acq_rel);
                    m_voiceReserved[slot].store(false, std::memory_order_release);
                }
            }
        }

        void Mixer::renderChannels(uint32_t frameCount)
        {
            // Process every non-master channel: effects, then sends.
            // Master is processed separately in writeMaster() after summing.
            for (uint32_t i = 1; i < m_channels.size(); i++)
            {
                Channel &ch = *m_channels[i];
                ch.processEffects(frameCount, m_outChannels, m_sampleRate);

                // Sends — mix this channel's output into target channels.
                for (const auto &send : ch.sends())
                {
                    if (send.targetIndex >= m_channels.size() || send.targetIndex == i) continue;
                    float *srcBuf = send.postFader ? ch.scratch() : ch.preFx();
                    float *dstBuf = m_channels[send.targetIndex]->scratch();
                    const size_t n = frameCount * m_outChannels;
                    for (size_t s = 0; s < n; s++)
                        dstBuf[s] += srcBuf[s] * send.amount;
                }
            }
        }

        void Mixer::writeMaster(float *out, uint32_t frameCount)
        {
            Channel &masterCh = *m_channels[0];

            // Sum non-master channels into master's scratch.
            // (master's scratch was cleared in renderCallback; voices routed
            //  directly to master have already populated it.)
            const size_t n = frameCount * m_outChannels;
            float *masterBuf = masterCh.scratch();
            for (uint32_t i = 1; i < m_channels.size(); i++)
            {
                const float *src = m_channels[i]->scratch();
                for (size_t s = 0; s < n; s++)
                    masterBuf[s] += src[s];
            }

            // Master effects (gain at index 0, then user effects).
            masterCh.processEffects(frameCount, m_outChannels, m_sampleRate);

            // Copy + clip to output.
            for (size_t s = 0; s < n; s++)
            {
                float v = masterBuf[s];
                if (v > 1.0f) v = 1.0f;
                else if (v < -1.0f) v = -1.0f;
                out[s] = v;
            }
        }
    }
}
