#pragma once

#include "Collider2D.hpp"
#include "../physics/World.hpp"

#include <glm/glm.hpp>

namespace Bokken
{
    namespace GameObject
    {

        /**
         * Single-segment edge collider — typically used for one-off
         * line obstacles. For continuous level geometry (a ground
         * outline) prefer ChainCollider2D, which handles the "ghost
         * vertex" problem at segment joins.
         *
         * Endpoints are in body-local pixels. Edges are inherently
         * one-sided in v3; use the `oneSided` flag to set the inside
         * direction for collision filtering.
        */
        class EdgeCollider2D : public Collider2D
        {
        public:
            glm::vec2 pointA{-32.0f, 0.0f};
            glm::vec2 pointB{32.0f, 0.0f};
            bool oneSided = false;

        protected:
            b2ShapeId createShape(b2BodyId body, const b2ShapeDef &def) override;
        };

    }
}
