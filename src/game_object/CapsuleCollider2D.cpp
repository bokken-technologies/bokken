#include "CapsuleCollider2D.hpp"

namespace Bokken
{
    namespace GameObject
    {

        b2ShapeId CapsuleCollider2D::createShape(b2BodyId body, const b2ShapeDef &def)
        {
            auto &world = Bokken::Physics::World::get();

            b2Capsule cap;
            cap.center1 = world.pxToB2(pointA);
            cap.center2 = world.pxToB2(pointB);
            cap.radius = world.pxToM(radius);

            return b2CreateCapsuleShape(body, &def, &cap);
        }

    }
}
