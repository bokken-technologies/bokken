#pragma once

#include "Component.hpp"
#include "Transform2D.hpp"
#include "Base.hpp"
#include "../physics/World.hpp"

#include <box2d/box2d.h>
#include <glm/glm.hpp>
#include <SDL3/SDL.h>

#include <cmath>

namespace Bokken
{
    namespace GameObject
    {

        /**
         * Rigidbody2D — owner of a Box2D v3 body.
         *
         * The component does not integrate forces itself any more. It
         * creates a b2Body on attach, keeps the sibling Transform2D in sync
         * with the body's pose after every world step, and forwards
         * gameplay-facing impulses / forces / velocity changes to Box2D.
         *
         * Lifecycle:
         *   onAttach  — reads sibling Transform2D (if present) for the
         *               initial pose, creates a b2Body via Physics::World,
         *               and stores `this` as the body's user data so the
         *               world's event dispatcher can route callbacks back
         *               to the right component.
         *   onDestroy — destroys the b2Body, which also destroys every
         *               attached b2Shape (so Collider2D::onDestroy doesn't
         *               need to call b2DestroyShape if the body is going
         *               away first).
         *
         * Sibling components:
         *   Transform2D — required for visual sync. If absent at attach
         *                 time the body is created at world origin and
         *                 onAttach logs a warning. Adding a Transform2D
         *                 later won't relocate the body retroactively.
         *   Collider2D family — every collider component on the same
         *                       GameObject creates its b2Shape against
         *                       this Rigidbody2D's body. A Collider2D
         *                       without a sibling Rigidbody2D falls back
         *                       to a hidden static body it manages itself.
         *
         * Coordinate convention:
         *   The component's public API is in pixels and degrees, matching
         *   the rest of the engine. Internally it converts to metres /
         *   radians at every Box2D boundary using Physics::World::meter().
        */
        class Rigidbody2D : public Component
        {
        public:
            // Mirrors b2BodyType. Repeated as an enum class so JS-facing
            // code and editor tooling don't need to include box2d/box2d.h
            // for the values.
            enum class Type : uint8_t
            {
                Static = 0,
                Kinematic,
                Dynamic,
            };

            // Configuration — read at onAttach() time. Mutating these
            // after the body exists has no effect; use the setter methods
            // below for live updates that actually round-trip into Box2D.
            Type  type           = Type::Dynamic;
            bool  fixedRotation  = false;
            bool  isBullet       = false;        // continuous collision detection
            float linearDamping  = 0.0f;
            float angularDamping = 0.0f;
            float gravityScale   = 1.0f;
            bool  allowSleep     = true;

            void onAttach() override;
            void onDestroy() override;

            // Live API.

            void setType(Type t);
            Type bodyType() const { return type; }

            void setFixedRotation(bool fixed);
            void setBullet(bool bullet);
            void setLinearDamping(float v);
            void setAngularDamping(float v);
            void setGravityScale(float v);
            void setAllowSleep(bool allow);

            // Pose. Setting position teleports the body — physics will
            // not interpolate, contacts may briefly tunnel. Use sparingly.
            glm::vec2 position() const;
            void setPosition(const glm::vec2 &pixels);

            float rotation() const;          // degrees
            void setRotation(float degrees);

            // Velocity (pixels per second, degrees per second).
            glm::vec2 linearVelocity() const;
            void setLinearVelocity(const glm::vec2 &pxPerSec);

            float angularVelocity() const;   // degrees / second
            void setAngularVelocity(float degPerSec);

            // Wake / sleep.
            bool isAwake() const;
            void setAwake(bool awake);

            // Forces and impulses. All vectors and points are in pixel
            // coordinates; conversion to metres happens internally.
            void applyForce(const glm::vec2 &forcePx, const glm::vec2 &worldPointPx, bool wake = true);
            void applyForceToCenter(const glm::vec2 &forcePx, bool wake = true);
            void applyTorque(float torque, bool wake = true);
            void applyLinearImpulse(const glm::vec2 &impulsePx, const glm::vec2 &worldPointPx, bool wake = true);
            void applyLinearImpulseToCenter(const glm::vec2 &impulsePx, bool wake = true);
            void applyAngularImpulse(float impulse, bool wake = true);

            // Mass.
            float mass() const;
            float inertia() const;

            // Internal: called by Physics::World::dispatchEvents() once
            // per body that moved during the last step. The transform is
            // copied to the sibling Transform2D, so the component's
            // public position()/rotation() methods read the *requested*
            // pose between dispatches and the *committed* pose afterwards.
            void syncFromBox2D(const b2Transform &t, bool fellAsleep);

            // Box2D handle. Exposed because Collider2D needs it during
            // its own onAttach to build shapes against the right body,
            // and because the joint factories need it too.
            b2BodyId bodyId() const { return m_body; }

            // Returns true once the body has been created. Useful for
            // Collider2D::onAttach which has no guaranteed component
            // ordering — if the rigidbody hasn't attached yet we fall
            // through to creating a hidden static body.
            bool hasBody() const { return B2_IS_NON_NULL(m_body); }

            // Pure event component — no per-frame work that needs to
            // keep it alive once the body settles.
            bool isIdle() const override { return true; }

        private:
            b2BodyId m_body = b2_nullBodyId;

            // True when the body's pose has been written from JS / native
            // code since the last step. syncFromBox2D() clears this.
            // Currently informational only but reserved for future
            // editor-mode sanity checks.
            bool m_pendingTeleport = false;
        };

    }
}
