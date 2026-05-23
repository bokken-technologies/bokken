#pragma once

#include "Collider2D.hpp"
#include "../physics/World.hpp"

#include <glm/glm.hpp>

namespace Bokken
{
    namespace GameObject
    {

        /**
         * Capsule collider — a "stadium" shape: two endpoints with a
         * radius. Ideal for character bodies, projectiles with rounded
         * caps, or any "long pill" geometry.
         *
         * Endpoints are in body-local pixels.
        */
        class CapsuleCollider2D : public Collider2D
        {
        public:
            glm::vec2 pointA{-16.0f, 0.0f};
            glm::vec2 pointB{16.0f, 0.0f};
            float     radius = 8.0f;

        protected:
            b2ShapeId createShape(b2BodyId body, const b2ShapeDef &def) override;
        };

    }
}
