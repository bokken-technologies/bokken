#include "AudioSource2D.hpp"

namespace Bokken
{
    namespace GameObject
    {
        Bokken::Audio::SpatialParams AudioSource2D::buildSpatial(float overridePosX,
                                                                  float overridePosY,
                                                                  bool useOverride) const
        {
            Bokken::Audio::SpatialParams sp;
            sp.enabled = spatial;
            sp.minDistance = minimumDistance;
            sp.maxDistance = maximumDistance;
            sp.rolloff = rolloff;
            sp.dopplerEnabled = doppler;

            if (useOverride)
            {
                sp.position = {overridePosX, overridePosY, 0.0f};
                return sp;
            }

            // Pull position from sibling Transform2D when present. A source
            // without a Transform2D is unusual but legal — the source plays
            // at world origin in that case.
            if (gameObject)
            {
                if (auto *t = gameObject->getComponent<Transform2D>())
                {
                    sp.position = {t->position.x, t->position.y, 0.0f};
                }
            }
            return sp;
        }

        void AudioSource2D::play()
        {
            // Cancel the previous voice if it's still alive. A short fade
            // prevents the click that an instant cut would produce when
            // restarting a sustained sound.
            if (m_currentVoice != Bokken::Audio::INVALID_VOICE)
            {
                Bokken::Audio::Mixer::get().stopVoice(m_currentVoice, 0.01f);
                m_currentVoice = Bokken::Audio::INVALID_VOICE;
            }

            if (clip.empty())
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                            "[AudioSource2D] play() with no clip set on '%s'",
                            gameObject ? gameObject->name.c_str() : "<null>");
                return;
            }

            auto sound = Bokken::Audio::SoundCache::get().load(clip);
            if (!sound)
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "[AudioSource2D] SoundCache returned null for '%s'", clip.c_str());
                return;
            }

            const auto sp = buildSpatial();
            m_currentVoice = Bokken::Audio::Mixer::get().play(
                sound, channel, volume, pitch, loop, sp);
        }

        void AudioSource2D::stop(float fadeOutSeconds)
        {
            if (m_currentVoice == Bokken::Audio::INVALID_VOICE)
                return;
            Bokken::Audio::Mixer::get().stopVoice(m_currentVoice, fadeOutSeconds);
            m_currentVoice = Bokken::Audio::INVALID_VOICE;
        }

        void AudioSource2D::pause()
        {
            if (m_currentVoice == Bokken::Audio::INVALID_VOICE)
                return;
            Bokken::Audio::Mixer::get().setVoicePaused(m_currentVoice, true);
        }

        void AudioSource2D::resume()
        {
            if (m_currentVoice == Bokken::Audio::INVALID_VOICE)
                return;
            Bokken::Audio::Mixer::get().setVoicePaused(m_currentVoice, false);
        }

        void AudioSource2D::playOneShot(const std::string &clipOverride)
        {
            const std::string &path = clipOverride.empty() ? clip : clipOverride;
            if (path.empty())
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                            "[AudioSource2D] playOneShot() with no clip on '%s'",
                            gameObject ? gameObject->name.c_str() : "<null>");
                return;
            }

            auto sound = Bokken::Audio::SoundCache::get().load(path);
            if (!sound) return;

            // One-shots use the source's spatial setup but never loop —
            // looping a fire-and-forget would never end and would leak
            // a voice slot.
            const auto sp = buildSpatial();
            Bokken::Audio::Mixer::get().play(sound, channel, volume, pitch, false, sp);
            // Discard the returned voice id — caller asked for fire-and-forget.
        }

        void AudioSource2D::playOneShotAt(float positionX, float positionY,
                                          const std::string &clipOverride)
        {
            const std::string &path = clipOverride.empty() ? clip : clipOverride;
            if (path.empty())
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                            "[AudioSource2D] playOneShotAt() with no clip on '%s'",
                            gameObject ? gameObject->name.c_str() : "<null>");
                return;
            }

            auto sound = Bokken::Audio::SoundCache::get().load(path);
            if (!sound) return;

            const auto sp = buildSpatial(positionX, positionY, /*useOverride=*/true);
            Bokken::Audio::Mixer::get().play(sound, channel, volume, pitch, false, sp);
        }

        bool AudioSource2D::isPlaying() const
        {
            if (m_currentVoice == Bokken::Audio::INVALID_VOICE) return false;
            return Bokken::Audio::Mixer::get().isVoiceActive(m_currentVoice);
        }

        void AudioSource2D::onAttach()
        {
            if (autoPlay)
                play();
        }

        void AudioSource2D::fixedUpdate(float deltaTime)
        {
            if (!enabled) return;
            if (m_currentVoice == Bokken::Audio::INVALID_VOICE) return;

            // The voice may have ended naturally (non-looping clip ran
            // off the end). Don't bother updating spatial in that case
            // — clear our handle so subsequent ticks no-op cheaply.
            if (!Bokken::Audio::Mixer::get().isVoiceActive(m_currentVoice))
            {
                m_currentVoice = Bokken::Audio::INVALID_VOICE;
                m_hasLastPosition = false;
                return;
            }

            // Pull the current position from the Transform2D and push
            // an updated spatial snapshot. Cheap — one queue write per
            // active source per fixed step.
            if (!gameObject) return;
            auto *t = gameObject->getComponent<Transform2D>();
            if (!t) return;

            auto sp = buildSpatial();

            // Velocity estimation for Doppler. We could fold this into
            // Transform2D directly, but keeping it local to AudioSource2D
            // means transforms that don't have a source pay nothing.
            if (doppler && deltaTime > 0.0f && m_hasLastPosition)
            {
                const float invDt = 1.0f / deltaTime;
                sp.velocity = {
                    (t->position.x - m_lastPositionX) * invDt,
                    (t->position.y - m_lastPositionY) * invDt,
                    0.0f,
                };
            }
            m_lastPositionX = t->position.x;
            m_lastPositionY = t->position.y;
            m_hasLastPosition = true;

            Bokken::Audio::Mixer::get().setVoiceSpatial(m_currentVoice, sp);
        }

        void AudioSource2D::onDestroy()
        {
            // Tear down the voice when the GameObject is destroyed.
            // A short fade rather than a hard cut avoids clicks on
            // sustained sounds when the game object disappears suddenly
            // (e.g. enemy with a looping growl is killed).
            if (m_currentVoice != Bokken::Audio::INVALID_VOICE)
            {
                Bokken::Audio::Mixer::get().stopVoice(m_currentVoice, 0.05f);
                m_currentVoice = Bokken::Audio::INVALID_VOICE;
            }
        }
    }
}
