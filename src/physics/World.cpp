#include "World.hpp"

#include "../game_object/Base.hpp"
#include "../game_object/Collider2D.hpp"
#include "../game_object/Rigidbody2D.hpp"

namespace Bokken
{
    namespace Physics
    {

        bool World::init()
        {
            if (B2_IS_NON_NULL(m_world))
                return true;

            b2WorldDef def = b2DefaultWorldDef();
            def.gravity = {DEFAULT_GRAVITY_X, DEFAULT_GRAVITY_Y};
            // We want events available after step(); these flags are on
            // by default in v3 but set them explicitly so a future Box2D
            // default change doesn't silently break us.
            def.enableSleep = true;
            def.enableContinuous = true;

            m_world = b2CreateWorld(&def);
            if (B2_IS_NULL(m_world))
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "[Physics::World] b2CreateWorld returned null id");
                return false;
            }

            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "[Physics::World] world created (meter=%.2f px, gravity=(%.2f, %.2f) m/s^2)",
                        m_meter, DEFAULT_GRAVITY_X, DEFAULT_GRAVITY_Y);
            return true;
        }

        void World::shutdown()
        {
            if (B2_IS_NULL(m_world))
                return;

            b2DestroyWorld(m_world);
            m_world = b2_nullWorldId;
        }

        void World::step(float dt)
        {
            if (B2_IS_NULL(m_world) || dt <= 0.0f)
                return;

            b2World_Step(m_world, dt, m_subSteps);
        }

        // Forward declarations of dispatch helpers defined later in this TU.
        // They live here (rather than in Rigidbody2D / Collider2D) because
        // event traversal is a world-level concern.
        static void dispatchContactEvents(b2WorldId world);
        static void dispatchSensorEvents(b2WorldId world);
        static void dispatchBodyMoveEvents(b2WorldId world);

        void World::dispatchEvents()
        {
            if (B2_IS_NULL(m_world))
                return;

            // Order matters: body move events first so Transform2D is in
            // sync with the post-step pose before any collision callbacks
            // fire and let user code read positions.
            dispatchBodyMoveEvents(m_world);
            dispatchContactEvents(m_world);
            dispatchSensorEvents(m_world);
        }

        void World::setGravity(const glm::vec2 &g)
        {
            if (B2_IS_NULL(m_world))
                return;
            b2World_SetGravity(m_world, {g.x, g.y});
        }

        glm::vec2 World::gravity() const
        {
            if (B2_IS_NULL(m_world))
                return {DEFAULT_GRAVITY_X, DEFAULT_GRAVITY_Y};
            b2Vec2 g = b2World_GetGravity(m_world);
            return {g.x, g.y};
        }

        void World::setMeter(float pixelsPerMetre)
        {
            // Guard against zero or negative values which would brick all
            // future conversions.
            if (pixelsPerMetre > 0.0f)
                m_meter = pixelsPerMetre;
        }

        void World::setSubSteps(int n)
        {
            if (n >= 1)
                m_subSteps = n;
        }

        // Raycast filter context — passed through Box2D's callback as user
        // data so the lambda can stay non-capturing (Box2D wants a plain
        // function pointer).
        struct RaycastContext
        {
            std::vector<World::RaycastHit> hits;
            uint64_t maskBits;
            float meter;
        };

        static float raycastReport(b2ShapeId shape, b2Vec2 point, b2Vec2 normal,
                                   float fraction, void *ctx)
        {
            auto *rc = static_cast<RaycastContext *>(ctx);

            // Mask filter. Box2D v3 has b2QueryFilter passed at call time
            // but we still allow per-call masking via Bokken's own bits in
            // case the user wants finer control than the b2Filter on shapes.
            b2Filter f = b2Shape_GetFilter(shape);
            if ((static_cast<uint64_t>(f.categoryBits) & rc->maskBits) == 0ull)
                return -1.0f; // ignore this shape, keep going

            World::RaycastHit hit;
            hit.shape = shape;
            hit.point = {point.x * rc->meter, point.y * rc->meter};
            hit.normal = {normal.x, normal.y};
            hit.fraction = fraction;
            rc->hits.push_back(hit);

            return 1.0f; // continue, collect every hit
        }

        std::vector<World::RaycastHit> World::raycast(const glm::vec2 &origin,
                                                      const glm::vec2 &direction,
                                                      float maximumDistance,
                                                      uint64_t maskBits)
        {
            RaycastContext ctx{};
            ctx.maskBits = maskBits;
            ctx.meter = m_meter;

            if (B2_IS_NULL(m_world) || maximumDistance <= 0.0f)
                return ctx.hits;

            float len = std::sqrt(direction.x * direction.x + direction.y * direction.y);
            if (len < 1e-6f)
                return ctx.hits;

            glm::vec2 dirN = direction / len;
            glm::vec2 transM = pxToM(dirN * maximumDistance);

            b2Vec2 b2Origin = pxToB2(origin);
            b2Vec2 b2Trans = {transM.x, transM.y};

            b2QueryFilter filter = b2DefaultQueryFilter();
            // Bokken's wider mask is applied per-hit in the callback, but
            // we keep the b2QueryFilter open so b2 doesn't reject candidates
            // before we see them.

            b2World_CastRay(m_world, b2Origin, b2Trans, filter, raycastReport, &ctx);

            std::sort(ctx.hits.begin(), ctx.hits.end(),
                      [](const RaycastHit &a, const RaycastHit &b)
                      { return a.fraction < b.fraction; });

            return ctx.hits;
        }

        std::optional<World::RaycastHit> World::raycastNearest(const glm::vec2 &origin,
                                                               const glm::vec2 &direction,
                                                               float maximumDistance,
                                                               uint64_t maskBits)
        {
            auto hits = raycast(origin, direction, maximumDistance, maskBits);
            if (hits.empty())
                return std::nullopt;
            return hits.front();
        }

        struct OverlapContext
        {
            std::vector<b2ShapeId> shapes;
            uint64_t maskBits;
        };

        static bool overlapReport(b2ShapeId shape, void *ctx)
        {
            auto *oc = static_cast<OverlapContext *>(ctx);
            b2Filter f = b2Shape_GetFilter(shape);
            if ((static_cast<uint64_t>(f.categoryBits) & oc->maskBits) == 0ull)
                return true; // skip this one, keep going
            oc->shapes.push_back(shape);
            return true;
        }

        std::vector<b2ShapeId> World::overlapAABB(const glm::vec2 &lower,
                                                  const glm::vec2 &upper,
                                                  uint64_t maskBits)
        {
            OverlapContext ctx{};
            ctx.maskBits = maskBits;

            if (B2_IS_NULL(m_world))
                return ctx.shapes;

            b2AABB box;
            box.lowerBound = pxToB2(lower);
            box.upperBound = pxToB2(upper);

            b2QueryFilter filter = b2DefaultQueryFilter();
            b2World_OverlapAABB(m_world, box, filter, overlapReport, &ctx);
            return ctx.shapes;
        }

        std::vector<b2ShapeId> World::overlapCircle(const glm::vec2 &center,
                                                    float radius,
                                                    uint64_t maskBits)
        {
            OverlapContext ctx{};
            ctx.maskBits = maskBits;

            if (B2_IS_NULL(m_world) || radius <= 0.0f)
                return ctx.shapes;

            b2Circle circle;
            circle.center = {0.0f, 0.0f};
            circle.radius = pxToM(radius);

            b2ShapeProxy proxy = b2MakeProxy(&circle.center, 1, circle.radius);
            b2Transform tr;
            tr.p = pxToB2(center);
            tr.q = b2MakeRot(0.0f);

            // Box2D v3 takes a proxy + transform; older drafts used
            // b2World_OverlapCircle directly. The proxy form lets us share
            // code with shape casts later.
            b2QueryFilter filter = b2DefaultQueryFilter();
            (void)tr;
            b2World_OverlapShape(m_world, &proxy, filter, overlapReport, &ctx);

            return ctx.shapes;
        }

        std::optional<World::RaycastHit> World::circleCast(const glm::vec2 &center,
                                                           float radius,
                                                           const glm::vec2 &translation,
                                                           uint64_t maskBits)
        {
            if (B2_IS_NULL(m_world))
                return std::nullopt;

            b2Circle circle;
            circle.center = {0.0f, 0.0f};
            circle.radius = pxToM(radius);

            b2ShapeProxy proxy = b2MakeProxy(&circle.center, 1, circle.radius);
            b2Vec2 b2Trans = pxToB2(translation);

            RaycastContext rc{};
            rc.maskBits = maskBits;
            rc.meter = m_meter;

            b2QueryFilter filter = b2DefaultQueryFilter();
            b2World_CastShape(m_world, &proxy, b2Trans, filter, raycastReport, &rc);

            if (rc.hits.empty())
                return std::nullopt;

            std::sort(rc.hits.begin(), rc.hits.end(),
                      [](const RaycastHit &a, const RaycastHit &b)
                      { return a.fraction < b.fraction; });

            // CastShape's "point" is in the moving shape's frame; offset
            // the reported point by the centre so callers see a world hit.
            World::RaycastHit hit = rc.hits.front();
            hit.point += center;
            return hit;
        }

        float World::distance(b2ShapeId a, b2ShapeId b) const
        {
            if (B2_IS_NULL(m_world) || B2_IS_NULL(a) || B2_IS_NULL(b))
                return -1.0f;

            // v3.1.x doesn't have a convenience b2MakeShapeDistanceProxy
            // helper, so we hand-roll the per-shape-type proxy build by
            // pulling the underlying primitive (circle / polygon /
            // capsule / segment) and feeding its points to b2MakeProxy.
            // shapeToProxy returns false for shape types we can't
            // currently handle (most notably b2_chainSegmentShape, which
            // is effectively a segment plus neighbour info — the simple
            // segment fallback in that case is good enough for the
            // distance query the JS surface exposes).
            auto shapeToProxy = [](b2ShapeId id, b2ShapeProxy &out) -> bool
            {
                b2ShapeType t = b2Shape_GetType(id);
                switch (t)
                {
                case b2_circleShape:
                {
                    b2Circle c = b2Shape_GetCircle(id);
                    out = b2MakeProxy(&c.center, 1, c.radius);
                    return true;
                }
                case b2_capsuleShape:
                {
                    b2Capsule c = b2Shape_GetCapsule(id);
                    b2Vec2 pts[2] = {c.center1, c.center2};
                    out = b2MakeProxy(pts, 2, c.radius);
                    return true;
                }
                case b2_polygonShape:
                {
                    b2Polygon p = b2Shape_GetPolygon(id);
                    out = b2MakeProxy(p.vertices, p.count, p.radius);
                    return true;
                }
                case b2_segmentShape:
                {
                    b2Segment s = b2Shape_GetSegment(id);
                    b2Vec2 pts[2] = {s.point1, s.point2};
                    out = b2MakeProxy(pts, 2, 0.0f);
                    return true;
                }
                case b2_chainSegmentShape:
                {
                    b2ChainSegment cs = b2Shape_GetChainSegment(id);
                    b2Vec2 pts[2] = {cs.segment.point1, cs.segment.point2};
                    out = b2MakeProxy(pts, 2, 0.0f);
                    return true;
                }
                default:
                    return false;
                }
            };

            b2DistanceInput input{};
            if (!shapeToProxy(a, input.proxyA))
                return -1.0f;
            if (!shapeToProxy(b, input.proxyB))
                return -1.0f;
            input.transformA = b2Body_GetTransform(b2Shape_GetBody(a));
            input.transformB = b2Body_GetTransform(b2Shape_GetBody(b));
            input.useRadii = true;

            b2SimplexCache cache{};
            b2DistanceOutput out = b2ShapeDistance(&input, &cache, nullptr, 0);

            return out.distance * m_meter;
        }

        // Dispatch helpers.

        // Resolves a Bokken Rigidbody2D pointer from a b2BodyId. The
        // Rigidbody2D component stores itself as the body's user data when
        // it creates the body, so this is a single Box2D call plus a cast.
        static Bokken::GameObject::Rigidbody2D *bodyToComponent(b2BodyId body)
        {
            if (B2_IS_NULL(body))
                return nullptr;
            void *userData = b2Body_GetUserData(body);
            return static_cast<Bokken::GameObject::Rigidbody2D *>(userData);
        }

        static Bokken::GameObject::Collider2D *shapeToComponent(b2ShapeId shape)
        {
            // Defensive lookup. Box2D v3 event arrays can contain end-touch
            // events for shapes that were destroyed *between* the step that
            // emitted the event and the dispatch that consumes it — for
            // example, a JS sensor-begin handler that calls
            // GameObject.destroy() on a pickup will mark the body for
            // destruction; the next world step then notices the body is
            // gone and emits an end-touch event with a now-stale shape id.
            // The Box2D 3.1 docs are explicit about this and tell us to
            // use b2Shape_IsValid to filter such events. Without this
            // guard, b2Shape_GetUserData asserts on the recycled-slot
            // generation mismatch.
            if (B2_IS_NULL(shape) || !b2Shape_IsValid(shape))
                return nullptr;
            void *userData = b2Shape_GetUserData(shape);
            return static_cast<Bokken::GameObject::Collider2D *>(userData);
        }

        static void dispatchBodyMoveEvents(b2WorldId world)
        {
            b2BodyEvents events = b2World_GetBodyEvents(world);
            for (int i = 0; i < events.moveCount; ++i)
            {
                const b2BodyMoveEvent &e = events.moveEvents[i];
                auto *rb = static_cast<Bokken::GameObject::Rigidbody2D *>(e.userData);
                if (!rb)
                    continue;
                rb->syncFromBox2D(e.transform, e.fellAsleep);
            }
        }

        static void dispatchContactEvents(b2WorldId world)
        {
            b2ContactEvents events = b2World_GetContactEvents(world);

            for (int i = 0; i < events.beginCount; ++i)
            {
                const b2ContactBeginTouchEvent &e = events.beginEvents[i];
                auto *colA = shapeToComponent(e.shapeIdA);
                auto *colB = shapeToComponent(e.shapeIdB);
                if (colA)
                    colA->onContactBegin(colB, e.manifold);
                if (colB)
                    colB->onContactBegin(colA, e.manifold);
            }

            for (int i = 0; i < events.endCount; ++i)
            {
                const b2ContactEndTouchEvent &e = events.endEvents[i];
                auto *colA = shapeToComponent(e.shapeIdA);
                auto *colB = shapeToComponent(e.shapeIdB);
                if (colA)
                    colA->onContactEnd(colB);
                if (colB)
                    colB->onContactEnd(colA);
            }

            for (int i = 0; i < events.hitCount; ++i)
            {
                const b2ContactHitEvent &e = events.hitEvents[i];
                auto *colA = shapeToComponent(e.shapeIdA);
                auto *colB = shapeToComponent(e.shapeIdB);
                if (colA)
                    colA->onContactHit(colB, e);
                if (colB)
                    colB->onContactHit(colA, e);
            }
        }

        static void dispatchSensorEvents(b2WorldId world)
        {
            b2SensorEvents events = b2World_GetSensorEvents(world);

            for (int i = 0; i < events.beginCount; ++i)
            {
                const b2SensorBeginTouchEvent &e = events.beginEvents[i];
                auto *sensor = shapeToComponent(e.sensorShapeId);
                auto *visitor = shapeToComponent(e.visitorShapeId);
                if (sensor)
                    sensor->onSensorBegin(visitor);
            }

            for (int i = 0; i < events.endCount; ++i)
            {
                const b2SensorEndTouchEvent &e = events.endEvents[i];
                auto *sensor = shapeToComponent(e.sensorShapeId);
                auto *visitor = shapeToComponent(e.visitorShapeId);
                if (sensor)
                    sensor->onSensorEnd(visitor);
            }
        }

    }
}