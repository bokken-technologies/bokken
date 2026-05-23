#include "EdgeCollider2D.hpp"

namespace Bokken
{
    namespace GameObject
    {

        b2ShapeId EdgeCollider2D::createShape(b2BodyId body, const b2ShapeDef &def)
        {
            auto &world = Bokken::Physics::World::get();

            b2Segment seg;
            seg.point1 = world.pxToB2(pointA);
            seg.point2 = world.pxToB2(pointB);

            // For one-sided edges Box2D uses ghost vertices to compute
            // the interior. With a standalone segment we don't have
            // adjacent edges to derive ghosts from, so the user gets a
            // double-sided segment regardless of the flag — the JS API
            // still exposes the field for forward-compatibility once
            // chain-link queries are wired up.
            (void)oneSided;

            return b2CreateSegmentShape(body, &def, &seg);
        }

    }
}
