#include "BoxCollider2D.hpp"

namespace Bokken
{
    namespace GameObject
    {

        b2ShapeId BoxCollider2D::createShape(b2BodyId body, const b2ShapeDef &def)
        {
            auto &world = Bokken::Physics::World::get();

            // Box2D's b2MakeBox / b2MakeOffsetBox take half-extents in
            // metres. Bokken stores full extents in pixels, hence the
            // 0.5 * pxToM(...) conversion.
            float halfW = 0.5f * world.pxToM(size.x);
            float halfH = 0.5f * world.pxToM(size.y);
            b2Vec2 center = world.pxToB2(offset);
            b2Rot rot = b2MakeRot(angle * 0.017453292519943295f);

            b2Polygon poly = b2MakeOffsetBox(halfW, halfH, center, rot);
            return b2CreatePolygonShape(body, &def, &poly);
        }

    }
}
