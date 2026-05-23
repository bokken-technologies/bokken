#pragma once

#include "Component.hpp"
#include "Base.hpp"
#include "Transform2D.hpp"
#include "../audio/Mixer.hpp"
#include "../audio/SoundCache.hpp"
#include "../audio/Voice.hpp"

#include <SDL3/SDL.h>

#include <string>

namespace Bokken
{
    namespace GameObject
    {
        /**
         * Plays a sound at the owning GameObject's position.
         *
         * Reads the sibling Transform2D each fixed update and pushes
         * the position to the audio thread via the Mixer's voice
         * spatial parameters. Without a sibling Transform2D the
         * source still works but plays at world origin — fine for
         * non-spatial UI sounds (set spatial = false in that case).
         *
         * The component owns at most one "current" voice — the one
         * started by play()/autoPlay or by setting clip on an active
         * source. Calling play() while another voice is playing
         * stops the previous voice first and starts fresh; this
         * matches how most game engines handle "play this clip" on
         * a source that's already playing.
         *
         * For overlapping sounds (footsteps, gunshots, hits) use
         * playOneShot — that spawns a one-off voice that plays to
         * completion and is forgotten. The current voice is unaffected.
         *
         * @example
         *   const enemy = new GameObject("Enemy")
         *       .addComponent(Transform2D, { positionX: 5, positionY: 3 })
         *       .addComponent(AudioSource2D, {
         *           clip: "/audio/orc-grunt.wav",
         *           autoPlay: true,
         *           loop: true,
         *           channel: "sfx",
         *           volume: 0.8,
         *           spatial: true,
         *           minimumDistance: 2,
         *           maximumDistance: 30,
         *       });
        */
        class AudioSource2D : public Component
        {
        public:
            // Configuration
            // Path to the clip in the asset pack VFS. Setting this when a
            // voice is active stops the current voice and clears the cache
            // entry; the new clip is decoded lazily on the next play().
            std::string clip;

            // Bus name. Defaults to the master bus. Channels are created
            // via audio.createChannel() from JS or from C++ at startup.
            std::string channel = "Master";

            // Linear volume in [0, 1]. Multiplied with the channel and
            // master volumes at mix time.
            float volume = 1.0f;

            // Pitch multiplier. 1.0 = normal speed, 2.0 = octave up,
            // 0.5 = octave down. Affects both pitch and playback speed.
            float pitch = 1.0f;

            // Loop the current voice when it reaches the end of the clip.
            // One-shots (playOneShot) ignore this flag.
            bool loop = false;

            // If true, play() is called automatically on attach.
            bool autoPlay = false;

            // Spatialisation
            // When true, the voice is positional: distance attenuation,
            // panning, and Doppler all apply. When false, the voice plays
            // 2D at full volume regardless of the listener.
            bool spatial = true;

            // Distance below which the voice plays at full volume.
            float minimumDistance = 1.0f;

            // Distance beyond which the voice is fully attenuated to
            // silence. The attenuation curve between min and max is
            // controlled by `rolloff`.
            float maximumDistance = 50.0f;

            // Rolloff steepness. 1.0 = inverse distance, smaller values
            // give a gentler falloff, larger values sharper.
            float rolloff = 1.0f;

            // Doppler effect — pitch shifts based on relative motion of
            // source and listener. Off by default; enable for fast-moving
            // sources (vehicles, projectiles) where the effect is audible.
            bool doppler = false;

            // Methods
            /**
             * Start (or restart) the current clip. If a voice is already
             * playing, it is stopped with a short fade and a new voice
             * begins at the start of the clip.
            */
            void play();

            /**
             * Stop the current voice with an optional fade-out. After
             * the fade, the voice frees its slot in the pool. Stopping
             * an already-stopped source is a no-op.
            */
            void stop(float fadeOutSeconds = 0.005f);

            /** Pause/unpause the current voice. */
            void pause();
            void resume();

            /**
             * Spawn a one-shot voice independent of the current voice.
             * Plays to completion (or loops if loop is set) at the
             * GameObject's current position with the source's volume
             * and pitch. Does not affect the source's primary voice.
             *
             * The optional clipOverride lets a single source play
             * different sounds without changing its `clip` field —
             * useful for footstep variants or impact sounds.
            */
            void playOneShot(const std::string &clipOverride = "");

            /**
             * Spawn a one-shot at a specific world position rather
             * than the source's own position. Lets a single "world
             * FX manager" GameObject trigger sounds anywhere.
            */
            void playOneShotAt(float positionX, float positionY,
                               const std::string &clipOverride = "");

            /** True if the source's current voice is alive in the pool. */
            bool isPlaying() const;

            // Lifecycle
            void onAttach() override;
            void fixedUpdate(float deltaTime) override;
            void onDestroy() override;

        private:
            // The voice handle for the source's primary voice. Set by
            // play() and cleared when the voice ends naturally or is
            // stopped explicitly.
            Bokken::Audio::VoiceId m_currentVoice = Bokken::Audio::INVALID_VOICE;

            // Cached previous position for Doppler velocity estimation.
            // Updated each fixed step alongside the spatial param push.
            float m_lastPositionX = 0.0f;
            float m_lastPositionY = 0.0f;
            bool  m_hasLastPosition = false;

            // Build a SpatialParams snapshot from the current component
            // state and the sibling Transform2D (if any). Used by both
            // play() and the per-step position update.
            Bokken::Audio::SpatialParams buildSpatial(float overridePosX = 0.0f,
                                                      float overridePosY = 0.0f,
                                                      bool useOverride = false) const;
        };
    }
}
