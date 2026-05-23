#include "Collider2D.hpp"

namespace Bokken
{
    namespace GameObject
    {

        b2ShapeDef Collider2D::makeShapeDef() const
        {
            b2ShapeDef def = b2DefaultShapeDef();
            def.density = density;
            // Box2D v3 separated material into b2SurfaceMaterial; the
            // shape def carries one inline.
            def.material.friction = friction;
            def.material.restitution = restitution;
            def.material.tangentSpeed = tangentSpeed;
            def.isSensor = isSensor;

            // Box2D v3's b2Filter uses 64-bit category/mask matching our
            // public surface; pass through unchanged.
            def.filter.categoryBits = categoryBits;
            def.filter.maskBits = maskBits;
            def.filter.groupIndex = groupIndex;

            // We always want contact events on dynamic bodies. The cost
            // is small and not having them is a footgun for users who
            // expect onCollisionEnter to "just work".
            def.enableContactEvents = true;
            def.enableSensorEvents = true;

            // Hit events fire when relative speed exceeds a threshold.
            // They're cheap; enable for everyone.
            def.enableHitEvents = true;

            return def;
        }

        void Collider2D::onAttach()
        {
            auto &world = Bokken::Physics::World::get();
            if (!world.isReady())
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "[Collider2D] Physics::World not initialised; "
                             "shape will not be created on '%s'",
                             gameObject ? gameObject->name.c_str() : "<null>");
                return;
            }

            // Try to find a sibling Rigidbody2D.
            Rigidbody2D *rb = nullptr;
            if (gameObject)
                rb = gameObject->getComponent<Rigidbody2D>();

            if (rb && rb->hasBody())
            {
                m_body = rb->bodyId();
                m_ownsBody = false;
            }
            else
            {
                // No rigidbody — create a hidden static body so this
                // collider still participates in queries / collisions.
                glm::vec2 startPos{0.0f};
                float startRotDeg = 0.0f;
                if (gameObject)
                {
                    if (auto *t = gameObject->getComponent<Transform2D>())
                    {
                        startPos = t->position;
                        startRotDeg = t->rotation;
                    }
                }

                b2BodyDef bd = b2DefaultBodyDef();
                bd.type = b2_staticBody;
                bd.position = world.pxToB2(startPos);
                bd.rotation = b2MakeRot(startRotDeg * 0.017453292519943295f);
                bd.userData = nullptr; // no Rigidbody2D backing this body

                m_body = b2CreateBody(world.worldId(), &bd);
                m_ownsBody = true;
            }

            if (B2_IS_NULL(m_body))
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "[Collider2D] failed to obtain or create body");
                return;
            }

            b2ShapeDef def = makeShapeDef();
            def.userData = this;

            m_shape = createShape(m_body, def);
            if (B2_IS_NULL(m_shape))
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "[Collider2D] createShape failed on '%s'",
                             gameObject ? gameObject->name.c_str() : "<null>");
            }
        }

        void Collider2D::onDestroy()
        {
            // If the body is owned by a sibling Rigidbody2D, the body's
            // own destruction will take this shape with it. We can still
            // call b2DestroyShape on a live shape against a live body,
            // but we have to be careful not to do so after the body is
            // already gone (Box2D will assert).
            if (B2_IS_NON_NULL(m_shape))
            {
                // Update body events so we know whether the body is alive.
                // The cheapest check: B2_IS_NON_NULL on the body id and
                // b2Shape_IsValid which Box2D provides for exactly this
                // ordering edge case.
                if (b2Shape_IsValid(m_shape))
                    b2DestroyShape(m_shape, true);
                m_shape = b2_nullShapeId;
            }

            if (m_ownsBody && B2_IS_NON_NULL(m_body))
            {
                if (b2Body_IsValid(m_body))
                    b2DestroyBody(m_body);
                m_body = b2_nullBodyId;
                m_ownsBody = false;
            }
        }

        void Collider2D::setDensity(float d)
        {
            density = d;
            if (B2_IS_NON_NULL(m_shape))
            {
                b2Shape_SetDensity(m_shape, d, true);
            }
        }

        void Collider2D::setFriction(float f)
        {
            friction = f;
            if (B2_IS_NON_NULL(m_shape))
            {
                b2SurfaceMaterial m = b2Shape_GetSurfaceMaterial(m_shape);
                m.friction = f;
                b2Shape_SetSurfaceMaterial(m_shape, m);
            }
        }

        void Collider2D::setRestitution(float r)
        {
            restitution = r;
            if (B2_IS_NON_NULL(m_shape))
            {
                b2SurfaceMaterial m = b2Shape_GetSurfaceMaterial(m_shape);
                m.restitution = r;
                b2Shape_SetSurfaceMaterial(m_shape, m);
            }
        }

        void Collider2D::setTangentSpeed(float v)
        {
            tangentSpeed = v;
            if (B2_IS_NON_NULL(m_shape))
            {
                b2SurfaceMaterial m = b2Shape_GetSurfaceMaterial(m_shape);
                m.tangentSpeed = v;
                b2Shape_SetSurfaceMaterial(m_shape, m);
            }
        }

        void Collider2D::setSensor(bool sensor)
        {
            // Box2D v3 doesn't allow flipping sensor-ness in place. If
            // we ever need this we'd recreate the shape; for now keep
            // the change in the field and warn so the user doesn't
            // silently get the old behaviour.
            isSensor = sensor;
            if (B2_IS_NON_NULL(m_shape))
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "[Collider2D] setSensor() after attach is not supported in v3; "
                            "destroy and recreate the collider");
            }
        }

        void Collider2D::setFilter(uint64_t cat, uint64_t mask, int32_t group)
        {
            categoryBits = cat;
            maskBits = mask;
            groupIndex = group;
            if (B2_IS_NON_NULL(m_shape))
            {
                b2Filter f;
                f.categoryBits = cat;
                f.maskBits = mask;
                f.groupIndex = group;
                b2Shape_SetFilter(m_shape, f);
            }
        }

        // Forward contact / sensor events to any Behaviour-derived
        // sibling component on either GameObject. The Behaviour base
        // (game_object/Behaviour.hpp) declares the optional hook
        // signatures; subclasses override the ones they care about.
        // We dispatch to every Behaviour on the GameObject because a
        // single object can carry several gameplay scripts (a Health
        // component, an Audio cue trigger, etc.) all interested in
        // the same collision.
        void Collider2D::onContactBegin(Collider2D *other, const b2Manifold &manifold)
        {
            if (jsOnCollisionBegin)
                jsOnCollisionBegin(other, manifold);
            if (!gameObject)
                return;
            Behaviour::dispatchCollisionBegin(gameObject, other, manifold);
        }

        void Collider2D::onContactEnd(Collider2D *other)
        {
            if (jsOnCollisionEnd)
                jsOnCollisionEnd(other);
            if (!gameObject)
                return;
            Behaviour::dispatchCollisionEnd(gameObject, other);
        }

        void Collider2D::onContactHit(Collider2D *other, const b2ContactHitEvent &event)
        {
            if (jsOnCollisionHit)
                jsOnCollisionHit(other, event);
            if (!gameObject)
                return;
            Behaviour::dispatchCollisionHit(gameObject, other, event);
        }

        void Collider2D::onSensorBegin(Collider2D *other)
        {
            if (jsOnSensorBegin)
                jsOnSensorBegin(other);
            if (!gameObject)
                return;
            Behaviour::dispatchSensorBegin(gameObject, other);
        }

        void Collider2D::onSensorEnd(Collider2D *other)
        {
            if (jsOnSensorEnd)
                jsOnSensorEnd(other);
            if (!gameObject)
                return;
            Behaviour::dispatchSensorEnd(gameObject, other);
        }

    }
}
