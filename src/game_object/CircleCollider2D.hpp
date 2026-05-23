#pragma once

#include "Collider2D.hpp"
#include "../physics/World.hpp"

#include <glm/glm.hpp>

namespace Bokken
{
    namespace GameObject
    {

        /**
         * Circle collider.
         *
         * `radius` is in pixels; `offset` translates the centre relative
         * to the body. Circle-vs-anything contacts are the cheapest in
         * Box2D, so prefer this over a box hitbox where the geometry is
         * roughly circular.
        */
        class CircleCollider2D : public Collider2D
        {
        public:
            float     radius = 16.0f;
            glm::vec2 offset{0.0f, 0.0f};

        protected:
            b2ShapeId createShape(b2BodyId body, const b2ShapeDef &def) override;
        };

    }
}
