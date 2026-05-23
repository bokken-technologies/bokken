#pragma once

#include "Component.hpp"

namespace Bokken
{
    namespace GameObject
    {
        /**
         * Marks a GameObject as the "ear" of the audio scene.
         *
         * Each fixed update, the listener pushes a snapshot of its
         * sibling Transform2D's position and rotation into the audio
         * mixer. AudioSource2D voices use this snapshot to compute
         * distance attenuation, panning, and Doppler.
         *
         * Only one listener is allowed at a time. Attaching a second
         * AudioListener2D logs a warning and the second component
         * becomes dormant — it stays attached but contributes nothing
         * — until the first is destroyed. There is no automatic
         * promotion of the dormant listener; if you want a different
         * GameObject to be the listener, destroy the existing
         * AudioListener2D first and attach a new one.
         *
         * Typical placement is on the camera GameObject so audio
         * follows the player's view, but you can attach it to the
         * player itself for a first-person-perspective game, or to
         * a stationary scene marker for a fixed-camera game.
         *
         * @example
         *   const cam = new GameObject("Camera")
         *       .addComponent(Transform2D)
         *       .addComponent(Camera2D, { zoom: 32, isActive: true })
         *       .addComponent(AudioListener2D);
        */
        class AudioListener2D : public Component
        {
        public:
            // Master gain applied to everything the listener "hears".
            // Multiplied with channel and voice volumes at mix time.
            // Useful for global audio fades (cutscene start, etc.) that
            // affect everything regardless of which channel it's on.
            float gain = 1.0f;

            //  Lifecycle
            void onAttach() override;
            void fixedUpdate(float deltaTime) override;
            void onDestroy() override;

        private:
            // The single active listener. Set by onAttach when the slot
            // is free, cleared by onDestroy. Subsequent attaches while
            // this is non-null mark themselves dormant and skip the push.
            static inline AudioListener2D *s_active = nullptr;

            // True when this component lost the singleton race and is
            // not currently driving the mixer.
            bool m_dormant = false;

            // For Doppler — listener velocity estimated from successive
            // position samples, same approach as AudioSource2D.
            float m_lastPositionX = 0.0f;
            float m_lastPositionY = 0.0f;
            bool  m_hasLastPosition = false;
        };
    }
}
