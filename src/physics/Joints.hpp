#pragma once

#include "../game_object/Rigidbody2D.hpp"
#include "World.hpp"

#include <box2d/box2d.h>
#include <glm/glm.hpp>
#include <SDL3/SDL.h>

#include <cmath>

namespace Bokken
{
    namespace Physics
    {

        /**
         * Wrappers for Box2D v3's joint types, exposed as factory
         * functions that take Rigidbody2D component pointers (instead of
         * raw b2BodyIds) so JS scripts can stay entirely component-side.
         *
         * Each factory returns a b2JointId. Callers are responsible for
         * destroying joints they create — Box2D will also auto-destroy
         * any joint whose owning bodies are destroyed, so there's no
         * leak risk for joints between two GameObjects whose Rigidbody2D
         * components get destroyed.
         *
         * Box2D v3 joint set vs. Love2D's older v2.x set:
         *   Distance, Revolute, Prismatic, Weld, Mouse, Motor, Wheel,
         *   Filter, NullJoint — present in v3.
         *   Pulley, Gear, Friction, Rope — removed/renamed in v3.
         *     Friction → use linear/angular damping on the body.
         *     Rope     → DistanceJoint with min/max length limits (built in).
         *     Pulley/Gear → not in v3.
        */
        namespace Joints
        {
            // Common parameter blocks. We mirror the b2*JointDef fields
            // most useful from gameplay code, keep them in pixels/degrees,
            // and convert at the API boundary. Anything not exposed here
            // can be tweaked post-creation via b2*Joint_Set* functions
            // through the JS module's `joint.set(...)` helpers.

            struct DistanceParameters
            {
                glm::vec2 anchorA{0.0f};   // pixels, body-local
                glm::vec2 anchorB{0.0f};
                float length = -1.0f;      // pixels; <0 means auto from current pose
                float minimumLength = 0.0f;    // pixels (0 disables lower limit)
                float maximumLength = -1.0f;   // pixels (<0 means equal to length, i.e. rigid)
                bool  collideConnected = false;
                float hertz = 0.0f;        // soft constraint frequency, 0 = rigid
                float dampingRatio = 0.0f;
            };

            struct RevoluteParameters
            {
                glm::vec2 anchor{0.0f};    // pixels, world space
                bool collideConnected = false;
                bool enableLimit = false;
                float lowerAngle = 0.0f;   // degrees
                float upperAngle = 0.0f;
                bool  enableMotor = false;
                float motorSpeed = 0.0f;   // degrees / second
                float maximumMotorTorque = 0.0f;
                float referenceAngle = 0.0f; // degrees
            };

            struct PrismaticParameters
            {
                glm::vec2 anchor{0.0f};    // pixels, world space
                glm::vec2 axis{1.0f, 0.0f}; // unit-ish; will be normalised
                bool collideConnected = false;
                bool enableLimit = false;
                float lowerTranslation = 0.0f; // pixels
                float upperTranslation = 0.0f;
                bool  enableMotor = false;
                float motorSpeed = 0.0f;       // pixels / second
                float maximumMotorForce = 0.0f;
                float referenceAngle = 0.0f;   // degrees
            };

            struct WeldParameters
            {
                glm::vec2 anchor{0.0f};      // world pixels
                bool collideConnected = false;
                float linearHertz = 0.0f;    // soft constraint, 0 = rigid
                float linearDampingRatio = 0.0f;
                float angularHertz = 0.0f;
                float angularDampingRatio = 0.0f;
            };

            struct MouseParameters
            {
                glm::vec2 target{0.0f};     // world pixels
                float maximumForce = 1000.0f;
                float hertz = 5.0f;
                float dampingRatio = 0.7f;
                bool  collideConnected = false;
            };

            struct MotorParameters
            {
                glm::vec2 linearOffset{0.0f}; // pixels
                float angularOffset = 0.0f;   // degrees
                float maximumForce = 1.0f;
                float maximumTorque = 1.0f;
                float correctionFactor = 0.3f;
                bool  collideConnected = false;
            };

            struct WheelParameters
            {
                glm::vec2 anchor{0.0f};       // world pixels
                glm::vec2 axis{0.0f, 1.0f};   // unit-ish
                bool collideConnected = false;
                bool enableLimit = false;
                float lowerTranslation = 0.0f; // pixels
                float upperTranslation = 0.0f;
                bool  enableMotor = false;
                float motorSpeed = 0.0f;       // degrees / second
                float maximumMotorTorque = 0.0f;
                float hertz = 1.0f;            // suspension spring
                float dampingRatio = 0.7f;
            };

            // Filter joint — disables collision between two bodies but
            // imposes no constraint. Useful for ragdolls, gears that
            // must not self-intersect, etc.
            struct FilterParameters
            {
                // No fields; the existence of the joint is the contract.
            };

            // Factory functions. Return b2_nullJointId on failure (e.g.
            // either body has no bodyId yet).
            b2JointId createDistance(Bokken::GameObject::Rigidbody2D *a,
                                     Bokken::GameObject::Rigidbody2D *b,
                                     const DistanceParameters &p);

            b2JointId createRevolute(Bokken::GameObject::Rigidbody2D *a,
                                     Bokken::GameObject::Rigidbody2D *b,
                                     const RevoluteParameters &p);

            b2JointId createPrismatic(Bokken::GameObject::Rigidbody2D *a,
                                      Bokken::GameObject::Rigidbody2D *b,
                                      const PrismaticParameters &p);

            b2JointId createWeld(Bokken::GameObject::Rigidbody2D *a,
                                 Bokken::GameObject::Rigidbody2D *b,
                                 const WeldParameters &p);

            b2JointId createMouse(Bokken::GameObject::Rigidbody2D *target,
                                  const MouseParameters &p);

            b2JointId createMotor(Bokken::GameObject::Rigidbody2D *a,
                                  Bokken::GameObject::Rigidbody2D *b,
                                  const MotorParameters &p);

            b2JointId createWheel(Bokken::GameObject::Rigidbody2D *a,
                                  Bokken::GameObject::Rigidbody2D *b,
                                  const WheelParameters &p);

            b2JointId createFilter(Bokken::GameObject::Rigidbody2D *a,
                                   Bokken::GameObject::Rigidbody2D *b,
                                   const FilterParameters &p);

            // Destroy any joint kind. Safe to call on a null id.
            void destroy(b2JointId joint);

            // Returns the joint's b2JointType (kept as a plain int so JS
            // bindings can pass it through without including Box2D).
            int  type(b2JointId joint);
        }

    }
}
