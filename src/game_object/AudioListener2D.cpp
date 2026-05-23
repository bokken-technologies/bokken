#include "AudioListener2D.hpp"

#include "Base.hpp"
#include "Transform2D.hpp"
#include "../audio/Mixer.hpp"
#include "../audio/ListenerState.hpp"

#include <SDL3/SDL.h>
#include <cmath>

namespace Bokken
{
    namespace GameObject
    {
        void AudioListener2D::onAttach()
        {
            if (s_active != nullptr && s_active != this)
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                            "[AudioListener2D] another listener is already active on '%s'; "
                            "this listener on '%s' will be dormant until the other is destroyed",
                            s_active->gameObject ? s_active->gameObject->name.c_str() : "<null>",
                            gameObject ? gameObject->name.c_str() : "<null>");
                m_dormant = true;
                return;
            }
            s_active = this;
            m_dormant = false;
        }

        void AudioListener2D::fixedUpdate(float deltaTime)
        {
            if (!enabled || m_dormant) return;
            if (!gameObject) return;

            auto *t = gameObject->getComponent<Transform2D>();
            if (!t) return;

            // Build the listener snapshot. The forward/up vectors are
            // fixed for 2D — listener faces into the screen (-Z) with
            // up along world +Y. Rotation of the GameObject is ignored
            // for now; if you wanted "rotate the soundstage with the
            // camera" semantics this is where you'd derive the basis
            // from t->rotation. The default fixed basis is what most
            // 2D games want: left-of-screen sounds pan left regardless
            // of any camera roll.
            Bokken::Audio::ListenerState ls;
            ls.position = {t->position.x, t->position.y, 0.0f};
            ls.forward  = {0.0f, 0.0f, -1.0f};
            ls.up       = {0.0f, 1.0f,  0.0f};
            ls.gain     = gain;

            if (deltaTime > 0.0f && m_hasLastPosition)
            {
                const float invDt = 1.0f / deltaTime;
                ls.velocity = {
                    (t->position.x - m_lastPositionX) * invDt,
                    (t->position.y - m_lastPositionY) * invDt,
                    0.0f,
                };
            }
            m_lastPositionX = t->position.x;
            m_lastPositionY = t->position.y;
            m_hasLastPosition = true;

            Bokken::Audio::Mixer::get().updateListener(ls);
        }

        void AudioListener2D::onDestroy()
        {
            if (s_active == this)
            {
                s_active = nullptr;
            }
            // Note: we deliberately don't promote any waiting dormant
            // listener. The dormant component would have to walk the
            // GameObject registry to find itself, and the resulting
            // "spooky-action-at-a-distance" promotion is more confusing
            // than just requiring the user to make their listener
            // intent explicit. If you want a swap, destroy the old
            // listener and attach a new one.
        }
    }
}
