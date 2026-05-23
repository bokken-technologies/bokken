#include "CircleCollider2D.hpp"

namespace Bokken
{
    namespace GameObject
    {

        b2ShapeId CircleCollider2D::createShape(b2BodyId body, const b2ShapeDef &def)
        {
            auto &world = Bokken::Physics::World::get();

            b2Circle circle;
            circle.center = world.pxToB2(offset);
            circle.radius = world.pxToM(radius);

            return b2CreateCircleShape(body, &def, &circle);
        }

    }
}
