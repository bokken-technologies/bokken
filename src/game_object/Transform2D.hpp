#pragma once

#include "Component.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Bokken
{
    namespace GameObject
    {
        // 2D spatial transform: position (x, y), rotation (z-axis), and scale.
        // Does not carry a mesh — that's the job of Mesh2D.
        class Transform2D : public Component
        {
        public:
            glm::vec2 position{0.0f};
            float     rotation = 0.0f; // Degrees, counter-clockwise
            glm::vec2 scale{1.0f};
            float     zOrder = 0.0f;   // Draw ordering depth

            void translate(float x, float y)
            {
                position += glm::vec2(x, y);
            }

            void rotate(float degrees)
            {
                rotation += degrees;
            }

            // Returns a 4x4 matrix for GL compat. Transform3D will return a full 3D matrix.
            glm::mat4 getMatrix() const
            {
                glm::mat4 m = glm::mat4(1.0f);
                m = glm::translate(m, glm::vec3(position, zOrder));
                m = glm::rotate(m, glm::radians(rotation), {0, 0, 1});
                m = glm::scale(m, glm::vec3(scale, 1.0f));
                return m;
            }

            // Pure data — always idle.
            bool isIdle() const override { return true; }
        };
    }
}