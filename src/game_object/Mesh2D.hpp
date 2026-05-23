#pragma once

#include "Component.hpp"
#include "Shape2D.hpp"
#include <glm/glm.hpp>

namespace Bokken
{
    namespace GameObject
    {
        // Visual representation of a 2D game object.
        // Separated from Transform2D so invisible objects don't carry render state.
        class Mesh2D : public Component
        {
        public:
            Shape2D shape = Shape2D::Empty;

            // Tint colour (RGBA, premultiplied alpha)
            glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};

            bool flipX = false;
            bool flipY = false;
        };
    }
}
