#include "Joints.hpp"

namespace Bokken
{
    namespace Physics
    {
        namespace Joints
        {

            static constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;

            // Validate that two bodies are usable. Logs and returns false
            // when either is null or hasn't created its body yet.
            static bool validate(Bokken::GameObject::Rigidbody2D *a,
                                 Bokken::GameObject::Rigidbody2D *b,
                                 const char *jointName)
            {
                if (!a || !b)
                {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                 "[Physics::Joints::%s] null Rigidbody2D",
                                 jointName);
                    return false;
                }
                if (!a->hasBody() || !b->hasBody())
                {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                 "[Physics::Joints::%s] Rigidbody2D has no body",
                                 jointName);
                    return false;
                }
                return true;
            }

            b2JointId createDistance(Bokken::GameObject::Rigidbody2D *a,
                                     Bokken::GameObject::Rigidbody2D *b,
                                     const DistanceParameters &p)
            {
                if (!validate(a, b, "distance"))
                    return b2_nullJointId;

                auto &world = World::get();
                b2DistanceJointDef def = b2DefaultDistanceJointDef();
                def.bodyIdA = a->bodyId();
                def.bodyIdB = b->bodyId();
                def.localAnchorA = world.pxToB2(p.anchorA);
                def.localAnchorB = world.pxToB2(p.anchorB);

                // Negative length means "use whatever the bodies are
                // currently apart" — Love2D semantics.
                float lenM;
                if (p.length < 0.0f)
                {
                    b2Vec2 wa = b2Body_GetWorldPoint(a->bodyId(), def.localAnchorA);
                    b2Vec2 wb = b2Body_GetWorldPoint(b->bodyId(), def.localAnchorB);
                    float dx = wb.x - wa.x, dy = wb.y - wa.y;
                    lenM = std::sqrt(dx * dx + dy * dy);
                }
                else
                {
                    lenM = world.pxToM(p.length);
                }
                def.length = lenM;
                def.minLength = (p.minimumLength <= 0.0f) ? 0.0f : world.pxToM(p.minimumLength);
                def.maxLength = (p.maximumLength < 0.0f) ? lenM : world.pxToM(p.maximumLength);
                def.collideConnected = p.collideConnected;
                def.hertz = p.hertz;
                def.dampingRatio = p.dampingRatio;

                return b2CreateDistanceJoint(world.worldId(), &def);
            }

            b2JointId createRevolute(Bokken::GameObject::Rigidbody2D *a,
                                     Bokken::GameObject::Rigidbody2D *b,
                                     const RevoluteParameters &p)
            {
                if (!validate(a, b, "revolute"))
                    return b2_nullJointId;

                auto &world = World::get();
                b2Vec2 anchorM = world.pxToB2(p.anchor);

                b2RevoluteJointDef def = b2DefaultRevoluteJointDef();
                def.bodyIdA = a->bodyId();
                def.bodyIdB = b->bodyId();
                def.localAnchorA = b2Body_GetLocalPoint(a->bodyId(), anchorM);
                def.localAnchorB = b2Body_GetLocalPoint(b->bodyId(), anchorM);
                def.referenceAngle = p.referenceAngle * DEG_TO_RAD;
                def.collideConnected = p.collideConnected;
                def.enableLimit = p.enableLimit;
                def.lowerAngle = p.lowerAngle * DEG_TO_RAD;
                def.upperAngle = p.upperAngle * DEG_TO_RAD;
                def.enableMotor = p.enableMotor;
                def.motorSpeed = p.motorSpeed * DEG_TO_RAD;
                def.maxMotorTorque = p.maximumMotorTorque;

                return b2CreateRevoluteJoint(world.worldId(), &def);
            }

            b2JointId createPrismatic(Bokken::GameObject::Rigidbody2D *a,
                                      Bokken::GameObject::Rigidbody2D *b,
                                      const PrismaticParameters &p)
            {
                if (!validate(a, b, "prismatic"))
                    return b2_nullJointId;

                auto &world = World::get();
                b2Vec2 anchorM = world.pxToB2(p.anchor);

                // Normalise axis.
                float axLen = std::sqrt(p.axis.x * p.axis.x + p.axis.y * p.axis.y);
                b2Vec2 axisLocal{1.0f, 0.0f};
                if (axLen > 1e-6f)
                {
                    b2Vec2 axisWorld{p.axis.x / axLen, p.axis.y / axLen};
                    axisLocal = b2Body_GetLocalVector(a->bodyId(), axisWorld);
                }

                b2PrismaticJointDef def = b2DefaultPrismaticJointDef();
                def.bodyIdA = a->bodyId();
                def.bodyIdB = b->bodyId();
                def.localAnchorA = b2Body_GetLocalPoint(a->bodyId(), anchorM);
                def.localAnchorB = b2Body_GetLocalPoint(b->bodyId(), anchorM);
                def.localAxisA = axisLocal;
                def.referenceAngle = p.referenceAngle * DEG_TO_RAD;
                def.collideConnected = p.collideConnected;
                def.enableLimit = p.enableLimit;
                def.lowerTranslation = world.pxToM(p.lowerTranslation);
                def.upperTranslation = world.pxToM(p.upperTranslation);
                def.enableMotor = p.enableMotor;
                def.motorSpeed = world.pxToM(p.motorSpeed);
                def.maxMotorForce = p.maximumMotorForce;

                return b2CreatePrismaticJoint(world.worldId(), &def);
            }

            b2JointId createWeld(Bokken::GameObject::Rigidbody2D *a,
                                 Bokken::GameObject::Rigidbody2D *b,
                                 const WeldParameters &p)
            {
                if (!validate(a, b, "weld"))
                    return b2_nullJointId;

                auto &world = World::get();
                b2Vec2 anchorM = world.pxToB2(p.anchor);

                b2WeldJointDef def = b2DefaultWeldJointDef();
                def.bodyIdA = a->bodyId();
                def.bodyIdB = b->bodyId();
                def.localAnchorA = b2Body_GetLocalPoint(a->bodyId(), anchorM);
                def.localAnchorB = b2Body_GetLocalPoint(b->bodyId(), anchorM);

                // Reference angle = current angle delta so the weld
                // captures the bodies at their current orientation.
                b2Rot rotA = b2Body_GetRotation(a->bodyId());
                b2Rot rotB = b2Body_GetRotation(b->bodyId());
                def.referenceAngle = b2Rot_GetAngle(rotB) - b2Rot_GetAngle(rotA);

                def.collideConnected = p.collideConnected;
                def.linearHertz = p.linearHertz;
                def.linearDampingRatio = p.linearDampingRatio;
                def.angularHertz = p.angularHertz;
                def.angularDampingRatio = p.angularDampingRatio;

                return b2CreateWeldJoint(world.worldId(), &def);
            }

            b2JointId createMouse(Bokken::GameObject::Rigidbody2D *target,
                                  const MouseParameters &p)
            {
                if (!target || !target->hasBody())
                {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                 "[Physics::Joints::mouse] null or unattached target");
                    return b2_nullJointId;
                }

                auto &world = World::get();
                b2MouseJointDef def = b2DefaultMouseJointDef();

                // Box2D's mouse joint requires a "ground" body. We use
                // a small sleeping static body created on demand, shared
                // across all mouse joints — that's the conventional
                // Box2D pattern.
                static b2BodyId s_ground = b2_nullBodyId;
                if (B2_IS_NULL(s_ground))
                {
                    b2BodyDef gd = b2DefaultBodyDef();
                    gd.type = b2_staticBody;
                    s_ground = b2CreateBody(world.worldId(), &gd);
                }

                def.bodyIdA = s_ground;
                def.bodyIdB = target->bodyId();
                def.target = world.pxToB2(p.target);
                def.maxForce = p.maximumForce;
                def.hertz = p.hertz;
                def.dampingRatio = p.dampingRatio;
                def.collideConnected = p.collideConnected;

                return b2CreateMouseJoint(world.worldId(), &def);
            }

            b2JointId createMotor(Bokken::GameObject::Rigidbody2D *a,
                                  Bokken::GameObject::Rigidbody2D *b,
                                  const MotorParameters &p)
            {
                if (!validate(a, b, "motor"))
                    return b2_nullJointId;

                auto &world = World::get();
                b2MotorJointDef def = b2DefaultMotorJointDef();
                def.bodyIdA = a->bodyId();
                def.bodyIdB = b->bodyId();
                def.linearOffset = world.pxToB2(p.linearOffset);
                def.angularOffset = p.angularOffset * DEG_TO_RAD;
                def.maxForce = p.maximumForce;
                def.maxTorque = p.maximumTorque;
                def.correctionFactor = p.correctionFactor;
                def.collideConnected = p.collideConnected;

                return b2CreateMotorJoint(world.worldId(), &def);
            }

            b2JointId createWheel(Bokken::GameObject::Rigidbody2D *a,
                                  Bokken::GameObject::Rigidbody2D *b,
                                  const WheelParameters &p)
            {
                if (!validate(a, b, "wheel"))
                    return b2_nullJointId;

                auto &world = World::get();
                b2Vec2 anchorM = world.pxToB2(p.anchor);

                float axLen = std::sqrt(p.axis.x * p.axis.x + p.axis.y * p.axis.y);
                b2Vec2 axisLocal{0.0f, 1.0f};
                if (axLen > 1e-6f)
                {
                    b2Vec2 axisWorld{p.axis.x / axLen, p.axis.y / axLen};
                    axisLocal = b2Body_GetLocalVector(a->bodyId(), axisWorld);
                }

                b2WheelJointDef def = b2DefaultWheelJointDef();
                def.bodyIdA = a->bodyId();
                def.bodyIdB = b->bodyId();
                def.localAnchorA = b2Body_GetLocalPoint(a->bodyId(), anchorM);
                def.localAnchorB = b2Body_GetLocalPoint(b->bodyId(), anchorM);
                def.localAxisA = axisLocal;
                def.collideConnected = p.collideConnected;
                def.enableLimit = p.enableLimit;
                def.lowerTranslation = world.pxToM(p.lowerTranslation);
                def.upperTranslation = world.pxToM(p.upperTranslation);
                def.enableMotor = p.enableMotor;
                def.motorSpeed = p.motorSpeed * DEG_TO_RAD;
                def.maxMotorTorque = p.maximumMotorTorque;
                def.hertz = p.hertz;
                def.dampingRatio = p.dampingRatio;

                return b2CreateWheelJoint(world.worldId(), &def);
            }

            b2JointId createFilter(Bokken::GameObject::Rigidbody2D *a,
                                   Bokken::GameObject::Rigidbody2D *b,
                                   const FilterParameters &)
            {
                if (!validate(a, b, "filter"))
                    return b2_nullJointId;

                auto &world = World::get();
                b2FilterJointDef def = b2DefaultFilterJointDef();
                def.bodyIdA = a->bodyId();
                def.bodyIdB = b->bodyId();

                return b2CreateFilterJoint(world.worldId(), &def);
            }

            void destroy(b2JointId joint)
            {
                if (B2_IS_NULL(joint))
                    return;
                if (b2Joint_IsValid(joint))
                    b2DestroyJoint(joint);
            }

            int type(b2JointId joint)
            {
                if (B2_IS_NULL(joint) || !b2Joint_IsValid(joint))
                    return -1;
                return static_cast<int>(b2Joint_GetType(joint));
            }

        }
    }
}
