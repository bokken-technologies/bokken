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
         * Chain collider — a sequence of segments that share ghost
         * vertices at joins, eliminating the "interior corner" snag
         * problem for level boundaries.
         *
         * `points` is the chain in body-local pixels. When `loop` is
         * true the chain closes back to the first point; otherwise it's
         * an open polyline. Static bodies only — Box2D v3 disallows
         * chain shapes on dynamic bodies.
         *
         * Internally allocates a b2ChainId rather than a b2ShapeId, so
         * Collider2D's m_shape stays null and we manage the chain handle
         * separately. Queries that look up the collider by b2ShapeId
         * will still work because each chain segment has its own shape
         * id with the same userData (set by Box2D on chain creation).
        */
        class ChainCollider2D : public Collider2D
        {
        public:
            std::vector<glm::vec2> points;
            bool loop = false;

            // Chains override base destruction because they own a
            // b2ChainId rather than a b2ShapeId.
            void onDestroy() override;

            b2ChainId chainId() const { return m_chain; }

        protected:
            b2ShapeId createShape(b2BodyId body, const b2ShapeDef &def) override;

        private:
            b2ChainId m_chain = b2_nullChainId;
        };

    }
}
