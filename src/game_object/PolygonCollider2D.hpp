#pragma once

#include "Collider2D.hpp"
#include "../physics/World.hpp"

#include <glm/glm.hpp>
#include <SDL3/SDL.h>

#include <vector>

namespace Bokken
{
    namespace GameObject
    {

        /**
         * Convex polygon collider. Up to b2_maxPolygonVertices points
         * (8 by default in Box2D v3). Points are in body-local pixels
         * and must form a convex shape — Box2D will compute the convex
         * hull but rejects non-convex inputs.
         *
         * For non-convex shapes, use multiple PolygonCollider2D
         * components on the same GameObject (each one gets its own
         * b2Shape attached to the shared body) or use ChainCollider2D
         * for boundaries.
        */
        class PolygonCollider2D : public Collider2D
        {
        public:
            std::vector<glm::vec2> points;

        protected:
            b2ShapeId createShape(b2BodyId body, const b2ShapeDef &def) override;
        };

    }
}
