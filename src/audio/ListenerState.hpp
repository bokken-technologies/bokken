#pragma once

#include <glm/glm.hpp>
#include <atomic>

namespace Bokken
{
    namespace Audio
    {
        /**
         * Snapshot of the active listener's world state.
         *
         * The game thread writes a new ListenerState every frame from
         * AudioListener::update(). The audio thread reads it during
         * spatial source rendering. Updates are lock-free via a generation
         * counter — readers tolerate seeing a torn write by re-reading.
        */
        struct ListenerState
        {
            glm::vec3 position{0.0f};
            glm::vec3 forward{0.0f, 0.0f, -1.0f};
            glm::vec3 up{0.0f, 1.0f, 0.0f};
            glm::vec3 velocity{0.0f};
            float gain = 1.0f;
        };
    }
}
