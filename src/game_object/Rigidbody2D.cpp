#include "Rigidbody2D.hpp"

namespace Bokken
{
    namespace GameObject
    {

        // Translates the Bokken type enum to b2BodyType. Inline helper
        // because every place we need it has to do the same little switch.
        static inline b2BodyType toB2Type(Rigidbody2D::Type t)
        {
            switch (t)
            {
            case Rigidbody2D::Type::Static:    return b2_staticBody;
            case Rigidbody2D::Type::Kinematic: return b2_kinematicBody;
            case Rigidbody2D::Type::Dynamic:
            default:                           return b2_dynamicBody;
            }
        }

        static constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
        static constexpr float RAD_TO_DEG = 180.0f / 3.14159265358979323846f;

        void Rigidbody2D::onAttach()
        {
            auto &world = Bokken::Physics::World::get();
            if (!world.isReady())
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "[Rigidbody2D] Physics::World not initialised; "
                             "body will not be created on '%s'",
                             gameObject ? gameObject->name.c_str() : "<null>");
                return;
            }

            // Resolve initial pose from sibling Transform2D, if any.
            glm::vec2 startPos{0.0f};
            float startRotDeg = 0.0f;
            if (gameObject)
            {
                if (auto *t = gameObject->getComponent<Transform2D>())
                {
                    startPos = t->position;
                    startRotDeg = t->rotation;
                }
                else
                {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "[Rigidbody2D] no Transform2D on '%s'; body created at origin",
                                gameObject->name.c_str());
                }
            }

            b2BodyDef def = b2DefaultBodyDef();
            def.type = toB2Type(type);
            def.position = world.pxToB2(startPos);
            def.rotation = b2MakeRot(startRotDeg * DEG_TO_RAD);
            def.isBullet = isBullet;
            def.linearDamping = linearDamping;
            def.angularDamping = angularDamping;
            def.gravityScale = gravityScale;
            def.enableSleep = allowSleep;
            def.userData = this;

            // fixedRotation is not exposed on b2BodyDef in the v3.1.x
            // API line we target — the field was renamed/restructured
            // mid-3.x toward a `motionLocks` design that ships in v3.2+.
            // Workaround for users who need rotation locked: set a very
            // high angularDamping (e.g. 1e10) which effectively freezes
            // rotation while remaining valid on every Box2D version.
            // Revisit when we bump the Box2D pin to v3.2+.
            if (fixedRotation)
            {
                def.angularDamping = 1.0e10f;
            }

            m_body = b2CreateBody(world.worldId(), &def);
            if (B2_IS_NULL(m_body))
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "[Rigidbody2D] b2CreateBody failed on '%s'",
                             gameObject ? gameObject->name.c_str() : "<null>");
            }
        }

        void Rigidbody2D::onDestroy()
        {
            if (B2_IS_NULL(m_body))
                return;

            // Box2D destroys all attached shapes and joints with the body.
            // Collider2D::onDestroy guards on B2_IS_NULL before redoing
            // any work, so destruction order between sibling components
            // doesn't matter.
            b2DestroyBody(m_body);
            m_body = b2_nullBodyId;
        }

        void Rigidbody2D::setType(Type t)
        {
            type = t;
            if (B2_IS_NON_NULL(m_body))
                b2Body_SetType(m_body, toB2Type(t));
        }

        void Rigidbody2D::setFixedRotation(bool fixed)
        {
            fixedRotation = fixed;
            if (B2_IS_NON_NULL(m_body))
            {
                // Same workaround as in onAttach — toggle a saturating
                // angularDamping value rather than calling a Box2D API
                // that doesn't exist in v3.1.x. Revisit alongside the
                // motionLocks migration when we bump to v3.2+.
                b2Body_SetAngularDamping(m_body, fixed ? 1.0e10f : angularDamping);
            }
        }

        void Rigidbody2D::setBullet(bool bullet)
        {
            isBullet = bullet;
            if (B2_IS_NON_NULL(m_body))
                b2Body_SetBullet(m_body, bullet);
        }

        void Rigidbody2D::setLinearDamping(float v)
        {
            linearDamping = v;
            if (B2_IS_NON_NULL(m_body))
                b2Body_SetLinearDamping(m_body, v);
        }

        void Rigidbody2D::setAngularDamping(float v)
        {
            angularDamping = v;
            if (B2_IS_NON_NULL(m_body))
                b2Body_SetAngularDamping(m_body, v);
        }

        void Rigidbody2D::setGravityScale(float v)
        {
            gravityScale = v;
            if (B2_IS_NON_NULL(m_body))
                b2Body_SetGravityScale(m_body, v);
        }

        void Rigidbody2D::setAllowSleep(bool allow)
        {
            allowSleep = allow;
            if (B2_IS_NON_NULL(m_body))
                b2Body_EnableSleep(m_body, allow);
        }

        glm::vec2 Rigidbody2D::position() const
        {
            if (B2_IS_NULL(m_body))
                return {0.0f, 0.0f};
            return Bokken::Physics::World::get().b2ToPx(b2Body_GetPosition(m_body));
        }

        void Rigidbody2D::setPosition(const glm::vec2 &pixels)
        {
            if (B2_IS_NULL(m_body))
                return;
            auto &world = Bokken::Physics::World::get();
            b2Rot rot = b2Body_GetRotation(m_body);
            b2Body_SetTransform(m_body, world.pxToB2(pixels), rot);
            m_pendingTeleport = true;

            // Mirror to Transform2D immediately — the next world step will
            // also generate a body move event but for teleports we want
            // the visual sync to happen on the same frame as the call.
            if (gameObject)
                if (auto *t = gameObject->getComponent<Transform2D>())
                    t->position = pixels;
        }

        float Rigidbody2D::rotation() const
        {
            if (B2_IS_NULL(m_body))
                return 0.0f;
            return b2Rot_GetAngle(b2Body_GetRotation(m_body)) * RAD_TO_DEG;
        }

        void Rigidbody2D::setRotation(float degrees)
        {
            if (B2_IS_NULL(m_body))
                return;
            b2Vec2 pos = b2Body_GetPosition(m_body);
            b2Body_SetTransform(m_body, pos, b2MakeRot(degrees * DEG_TO_RAD));
            m_pendingTeleport = true;

            if (gameObject)
                if (auto *t = gameObject->getComponent<Transform2D>())
                    t->rotation = degrees;
        }

        glm::vec2 Rigidbody2D::linearVelocity() const
        {
            if (B2_IS_NULL(m_body))
                return {0.0f, 0.0f};
            return Bokken::Physics::World::get().b2ToPx(b2Body_GetLinearVelocity(m_body));
        }

        void Rigidbody2D::setLinearVelocity(const glm::vec2 &pxPerSec)
        {
            if (B2_IS_NULL(m_body))
                return;
            b2Body_SetLinearVelocity(m_body, Bokken::Physics::World::get().pxToB2(pxPerSec));
        }

        float Rigidbody2D::angularVelocity() const
        {
            if (B2_IS_NULL(m_body))
                return 0.0f;
            return b2Body_GetAngularVelocity(m_body) * RAD_TO_DEG;
        }

        void Rigidbody2D::setAngularVelocity(float degPerSec)
        {
            if (B2_IS_NULL(m_body))
                return;
            b2Body_SetAngularVelocity(m_body, degPerSec * DEG_TO_RAD);
        }

        bool Rigidbody2D::isAwake() const
        {
            if (B2_IS_NULL(m_body))
                return false;
            return b2Body_IsAwake(m_body);
        }

        void Rigidbody2D::setAwake(bool awake)
        {
            if (B2_IS_NON_NULL(m_body))
                b2Body_SetAwake(m_body, awake);
        }

        void Rigidbody2D::applyForce(const glm::vec2 &forcePx,
                                     const glm::vec2 &worldPointPx,
                                     bool wake)
        {
            if (B2_IS_NULL(m_body))
                return;
            auto &world = Bokken::Physics::World::get();
            // Forces in Box2D have units of (kg m / s^2). Bokken stores
            // forces in (kg px / s^2) so users can think in pixels; divide
            // out the metre factor on entry.
            b2Vec2 fM = world.pxToB2(forcePx);
            b2Vec2 pM = world.pxToB2(worldPointPx);
            b2Body_ApplyForce(m_body, fM, pM, wake);
        }

        void Rigidbody2D::applyForceToCenter(const glm::vec2 &forcePx, bool wake)
        {
            if (B2_IS_NULL(m_body))
                return;
            b2Vec2 fM = Bokken::Physics::World::get().pxToB2(forcePx);
            b2Body_ApplyForceToCenter(m_body, fM, wake);
        }

        void Rigidbody2D::applyTorque(float torque, bool wake)
        {
            if (B2_IS_NULL(m_body))
                return;
            b2Body_ApplyTorque(m_body, torque, wake);
        }

        void Rigidbody2D::applyLinearImpulse(const glm::vec2 &impulsePx,
                                             const glm::vec2 &worldPointPx,
                                             bool wake)
        {
            if (B2_IS_NULL(m_body))
                return;
            auto &world = Bokken::Physics::World::get();
            b2Vec2 jM = world.pxToB2(impulsePx);
            b2Vec2 pM = world.pxToB2(worldPointPx);
            b2Body_ApplyLinearImpulse(m_body, jM, pM, wake);
        }

        void Rigidbody2D::applyLinearImpulseToCenter(const glm::vec2 &impulsePx, bool wake)
        {
            if (B2_IS_NULL(m_body))
                return;
            b2Vec2 jM = Bokken::Physics::World::get().pxToB2(impulsePx);
            b2Body_ApplyLinearImpulseToCenter(m_body, jM, wake);
        }

        void Rigidbody2D::applyAngularImpulse(float impulse, bool wake)
        {
            if (B2_IS_NULL(m_body))
                return;
            b2Body_ApplyAngularImpulse(m_body, impulse, wake);
        }

        float Rigidbody2D::mass() const
        {
            if (B2_IS_NULL(m_body))
                return 0.0f;
            return b2Body_GetMass(m_body);
        }

        float Rigidbody2D::inertia() const
        {
            if (B2_IS_NULL(m_body))
                return 0.0f;
            return b2Body_GetRotationalInertia(m_body);
        }

        void Rigidbody2D::syncFromBox2D(const b2Transform &t, bool fellAsleep)
        {
            (void)fellAsleep;
            m_pendingTeleport = false;

            if (!gameObject)
                return;

            auto *transform = gameObject->getComponent<Transform2D>();
            if (!transform)
                return;

            transform->position = Bokken::Physics::World::get().b2ToPx(t.p);
            transform->rotation = b2Rot_GetAngle(t.q) * RAD_TO_DEG;
        }

    }
}
