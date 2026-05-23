#pragma once

#include "Collider2D.hpp"
#include "../physics/World.hpp"

#include <glm/glm.hpp>

namespace Bokken
{
    namespace GameObject
    {

        /**
         * Axis-aligned (or rotated) box collider.
         *
         * `size` is the full width/height in pixels; `offset` translates
         * the shape relative to the body's centre, and `angle` rotates it
         * locally (degrees). For a typical sprite-aligned hitbox you only
         * need to set `size`.
         *
         * Internally builds a b2Polygon via b2MakeOffsetBox.
        */
        class BoxCollider2D : public Collider2D
        {
        public:
            glm::vec2 size{32.0f, 32.0f};
            glm::vec2 offset{0.0f, 0.0f};
            float     angle = 0.0f;       // degrees

        protected:
            b2ShapeId createShape(b2BodyId body, const b2ShapeDef &def) override;
        };

    }
}
