#include "PolygonCollider2D.hpp"

namespace Bokken
{
    namespace GameObject
    {

        b2ShapeId PolygonCollider2D::createShape(b2BodyId body, const b2ShapeDef &def)
        {
            auto &world = Bokken::Physics::World::get();

            if (points.empty() || points.size() < 3)
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "[PolygonCollider2D] need at least 3 points, have %zu",
                             points.size());
                return b2_nullShapeId;
            }

            // Build the b2 vertex array in metres.
            constexpr int kMax = B2_MAX_POLYGON_VERTICES;
            b2Vec2 verts[kMax];
            int count = static_cast<int>(points.size() < kMax ? points.size() : kMax);
            for (int i = 0; i < count; ++i)
                verts[i] = world.pxToB2(points[i]);

            // Box2D v3 wants a convex hull as input. We compute it from
            // the user's points, which both validates convexity and
            // tolerates input ordering.
            b2Hull hull = b2ComputeHull(verts, count);
            if (hull.count < 3)
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "[PolygonCollider2D] b2ComputeHull failed — points are degenerate");
                return b2_nullShapeId;
            }

            b2Polygon poly = b2MakePolygon(&hull, 0.0f);
            return b2CreatePolygonShape(body, &def, &poly);
        }

    }
}
