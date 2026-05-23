#include "Physics.hpp"

namespace Bokken
{
    namespace Scripting
    {
        namespace Modules
        {
            namespace
            {
                // Wrap a b2ShapeId in a JS opaque object. The payload is
                // a heap-allocated copy of the id so the JS finalizer can
                // free it without touching the underlying b2Shape.
                JSValue wrapShape(JSContext *ctx, b2ShapeId shape)
                {
                    JSValue obj = JS_NewObjectClass(ctx, Physics::s_shape_class_id);
                    if (JS_IsException(obj))
                        return obj;
                    auto *payload = new b2ShapeId(shape);
                    JS_SetOpaque(obj, payload);
                    return obj;
                }

                // Wrap a b2JointId in a JS opaque object. Unlike shapes,
                // joints have user-facing destroy semantics — we keep a
                // small struct in the payload that records whether the
                // joint has been explicitly destroyed so subsequent
                // method calls can early-out gracefully.
                struct JointPayload
                {
                    b2JointId id;
                    bool      destroyed;
                };

                JSValue wrapJoint(JSContext *ctx, b2JointId joint)
                {
                    JSValue obj = JS_NewObjectClass(ctx, Physics::s_joint_class_id);
                    if (JS_IsException(obj))
                        return obj;
                    auto *payload = new JointPayload{joint, false};
                    JS_SetOpaque(obj, payload);
                    return obj;
                }

                // Recover a b2ShapeId pointer from a JS shape handle, or
                // null if the JS value is not a shape handle.
                b2ShapeId *unwrapShape(JSValueConst v)
                {
                    return static_cast<b2ShapeId *>(JS_GetOpaque(v, Physics::s_shape_class_id));
                }

                JointPayload *unwrapJoint(JSValueConst v)
                {
                    return static_cast<JointPayload *>(JS_GetOpaque(v, Physics::s_joint_class_id));
                }

                // Recover a Rigidbody2D* from a JS rigidbody wrapper. Goes
                // through Modules::GameObject's class id since that's
                // where the JS Rigidbody2D objects are constructed.
                Bokken::GameObject::Rigidbody2D *unwrapRigidbody(JSValueConst v)
                {
                    return static_cast<Bokken::GameObject::Rigidbody2D *>(
                        JS_GetOpaque(v, GameObject::s_rigidbody2d_class_id));
                }

                // Read a numeric property off a JS object, leaving `out` unchanged
                // when the property is absent or not a number.
                bool readNumber(JSContext *ctx, JSValueConst obj, const char *name, float &out)
                {
                    if (!JS_IsObject(obj))
                        return false;
                    JSValue v = JS_GetPropertyStr(ctx, obj, name);
                    bool got = false;
                    if (JS_IsNumber(v))
                    {
                        double d = 0;
                        if (JS_ToFloat64(ctx, &d, v) == 0)
                        {
                            out = static_cast<float>(d);
                            got = true;
                        }
                    }
                    JS_FreeValue(ctx, v);
                    return got;
                }

                bool readBool(JSContext *ctx, JSValueConst obj, const char *name, bool &out)
                {
                    if (!JS_IsObject(obj))
                        return false;
                    JSValue v = JS_GetPropertyStr(ctx, obj, name);
                    bool got = false;
                    if (JS_IsBool(v))
                    {
                        out = (JS_ToBool(ctx, v) != 0);
                        got = true;
                    }
                    JS_FreeValue(ctx, v);
                    return got;
                }

                // Read a {x, y} pair into a glm::vec2. Returns false if
                // the property is missing or malformed.
                bool readVec2(JSContext *ctx, JSValueConst obj, const char *name, glm::vec2 &out)
                {
                    if (!JS_IsObject(obj))
                        return false;
                    JSValue v = JS_GetPropertyStr(ctx, obj, name);
                    if (!JS_IsObject(v))
                    {
                        JS_FreeValue(ctx, v);
                        return false;
                    }
                    glm::vec2 r{0.0f};
                    bool ok = readNumber(ctx, v, "x", r.x) && readNumber(ctx, v, "y", r.y);
                    JS_FreeValue(ctx, v);
                    if (ok)
                        out = r;
                    return ok;
                }

                // Build a {x, y} JS object.
                JSValue makeVec2(JSContext *ctx, const glm::vec2 &v)
                {
                    JSValue obj = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, v.x));
                    JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, v.y));
                    return obj;
                }

                // Build a Hit object.
                JSValue makeHit(JSContext *ctx, const Bokken::Physics::World::RaycastHit &h)
                {
                    JSValue obj = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, obj, "shape", wrapShape(ctx, h.shape));
                    JS_SetPropertyStr(ctx, obj, "point", makeVec2(ctx, h.point));
                    JS_SetPropertyStr(ctx, obj, "normal", makeVec2(ctx, h.normal));
                    JS_SetPropertyStr(ctx, obj, "fraction", JS_NewFloat64(ctx, h.fraction));
                    return obj;
                }

                // Decode the optional mask argument off the end of an
                // argv array. Defaults to all-bits-set when missing.
                uint64_t maskFromArg(JSContext *ctx, JSValueConst v)
                {
                    if (JS_IsUndefined(v) || JS_IsNull(v))
                        return ~0ull;
                    int64_t m = 0;
                    if (JS_ToInt64(ctx, &m, v) < 0)
                        return ~0ull;
                    return static_cast<uint64_t>(m);
                }

                // Function-list entries for the Joint prototype.
                const JSCFunctionListEntry s_jointFuncs[] = {
                    JS_CFUNC_DEF("destroy", 0, Physics::js_joint_destroy),
                    JS_CFUNC_DEF("isValid", 0, Physics::js_joint_is_valid),
                    JS_CGETSET_DEF("type", Physics::js_joint_get_type, nullptr),
                    JS_CFUNC_DEF("setMotorSpeed", 1, Physics::js_joint_set_motor_speed),
                    JS_CFUNC_DEF("setMaxMotorTorque", 1, Physics::js_joint_set_max_motor_torque),
                    JS_CFUNC_DEF("setMaxMotorForce", 1, Physics::js_joint_set_max_motor_force),
                    JS_CFUNC_DEF("enableMotor", 1, Physics::js_joint_enable_motor),
                    JS_CFUNC_DEF("enableLimit", 1, Physics::js_joint_enable_limit),
                    JS_CFUNC_DEF("setLimits", 2, Physics::js_joint_set_limits),
                    JS_CFUNC_DEF("setTarget", 2, Physics::js_joint_set_target),
                };
            } // namespace

            // World tunables.

            JSValue Physics::js_set_gravity(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 2)
                    return JS_UNDEFINED;
                double x, y;
                if (JS_ToFloat64(ctx, &x, argv[0]) < 0 ||
                    JS_ToFloat64(ctx, &y, argv[1]) < 0)
                    return JS_EXCEPTION;
                Bokken::Physics::World::get().setGravity({static_cast<float>(x), static_cast<float>(y)});
                return JS_UNDEFINED;
            }

            JSValue Physics::js_get_gravity(JSContext *ctx, JSValueConst, int, JSValueConst *)
            {
                glm::vec2 g = Bokken::Physics::World::get().gravity();
                return makeVec2(ctx, g);
            }

            JSValue Physics::js_set_meter(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 1)
                    return JS_UNDEFINED;
                double v;
                if (JS_ToFloat64(ctx, &v, argv[0]) < 0)
                    return JS_EXCEPTION;
                Bokken::Physics::World::get().setMeter(static_cast<float>(v));
                return JS_UNDEFINED;
            }

            JSValue Physics::js_get_meter(JSContext *ctx, JSValueConst, int, JSValueConst *)
            {
                return JS_NewFloat64(ctx, Bokken::Physics::World::get().meter());
            }

            JSValue Physics::js_set_substeps(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 1)
                    return JS_UNDEFINED;
                int32_t n;
                if (JS_ToInt32(ctx, &n, argv[0]) < 0)
                    return JS_EXCEPTION;
                Bokken::Physics::World::get().setSubSteps(n);
                return JS_UNDEFINED;
            }

            JSValue Physics::js_get_substeps(JSContext *ctx, JSValueConst, int, JSValueConst *)
            {
                return JS_NewInt32(ctx, Bokken::Physics::World::get().subSteps());
            }

            // Queries.

            // JS: physics.raycast(ox, oy, dx, dy, maxDist, mask?)
            JSValue Physics::js_raycast(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 5)
                    return JS_UNDEFINED;
                double ox, oy, dx, dy, maxD;
                if (JS_ToFloat64(ctx, &ox, argv[0]) < 0 ||
                    JS_ToFloat64(ctx, &oy, argv[1]) < 0 ||
                    JS_ToFloat64(ctx, &dx, argv[2]) < 0 ||
                    JS_ToFloat64(ctx, &dy, argv[3]) < 0 ||
                    JS_ToFloat64(ctx, &maxD, argv[4]) < 0)
                    return JS_EXCEPTION;

                uint64_t mask = (argc >= 6) ? maskFromArg(ctx, argv[5]) : ~0ull;

                auto hits = Bokken::Physics::World::get().raycast(
                    {static_cast<float>(ox), static_cast<float>(oy)},
                    {static_cast<float>(dx), static_cast<float>(dy)},
                    static_cast<float>(maxD),
                    mask);

                JSValue arr = JS_NewArray(ctx);
                for (size_t i = 0; i < hits.size(); ++i)
                    JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i), makeHit(ctx, hits[i]));
                return arr;
            }

            JSValue Physics::js_raycast_nearest(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 5)
                    return JS_NULL;
                double ox, oy, dx, dy, maxD;
                if (JS_ToFloat64(ctx, &ox, argv[0]) < 0 ||
                    JS_ToFloat64(ctx, &oy, argv[1]) < 0 ||
                    JS_ToFloat64(ctx, &dx, argv[2]) < 0 ||
                    JS_ToFloat64(ctx, &dy, argv[3]) < 0 ||
                    JS_ToFloat64(ctx, &maxD, argv[4]) < 0)
                    return JS_EXCEPTION;

                uint64_t mask = (argc >= 6) ? maskFromArg(ctx, argv[5]) : ~0ull;

                auto hit = Bokken::Physics::World::get().raycastNearest(
                    {static_cast<float>(ox), static_cast<float>(oy)},
                    {static_cast<float>(dx), static_cast<float>(dy)},
                    static_cast<float>(maxD),
                    mask);

                if (!hit.has_value())
                    return JS_NULL;
                return makeHit(ctx, hit.value());
            }

            // JS: physics.overlapAABB(lx, ly, ux, uy, mask?)
            JSValue Physics::js_overlap_aabb(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 4)
                    return JS_UNDEFINED;
                double lx, ly, ux, uy;
                if (JS_ToFloat64(ctx, &lx, argv[0]) < 0 ||
                    JS_ToFloat64(ctx, &ly, argv[1]) < 0 ||
                    JS_ToFloat64(ctx, &ux, argv[2]) < 0 ||
                    JS_ToFloat64(ctx, &uy, argv[3]) < 0)
                    return JS_EXCEPTION;

                uint64_t mask = (argc >= 5) ? maskFromArg(ctx, argv[4]) : ~0ull;

                auto shapes = Bokken::Physics::World::get().overlapAABB(
                    {static_cast<float>(lx), static_cast<float>(ly)},
                    {static_cast<float>(ux), static_cast<float>(uy)},
                    mask);

                JSValue arr = JS_NewArray(ctx);
                for (size_t i = 0; i < shapes.size(); ++i)
                    JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i), wrapShape(ctx, shapes[i]));
                return arr;
            }

            JSValue Physics::js_overlap_circle(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 3)
                    return JS_UNDEFINED;
                double cx, cy, r;
                if (JS_ToFloat64(ctx, &cx, argv[0]) < 0 ||
                    JS_ToFloat64(ctx, &cy, argv[1]) < 0 ||
                    JS_ToFloat64(ctx, &r, argv[2]) < 0)
                    return JS_EXCEPTION;

                uint64_t mask = (argc >= 4) ? maskFromArg(ctx, argv[3]) : ~0ull;

                auto shapes = Bokken::Physics::World::get().overlapCircle(
                    {static_cast<float>(cx), static_cast<float>(cy)},
                    static_cast<float>(r),
                    mask);

                JSValue arr = JS_NewArray(ctx);
                for (size_t i = 0; i < shapes.size(); ++i)
                    JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i), wrapShape(ctx, shapes[i]));
                return arr;
            }

            // JS: physics.circleCast(cx, cy, r, tx, ty, mask?)
            JSValue Physics::js_circle_cast(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 5)
                    return JS_NULL;
                double cx, cy, r, tx, ty;
                if (JS_ToFloat64(ctx, &cx, argv[0]) < 0 ||
                    JS_ToFloat64(ctx, &cy, argv[1]) < 0 ||
                    JS_ToFloat64(ctx, &r, argv[2]) < 0 ||
                    JS_ToFloat64(ctx, &tx, argv[3]) < 0 ||
                    JS_ToFloat64(ctx, &ty, argv[4]) < 0)
                    return JS_EXCEPTION;

                uint64_t mask = (argc >= 6) ? maskFromArg(ctx, argv[5]) : ~0ull;

                auto hit = Bokken::Physics::World::get().circleCast(
                    {static_cast<float>(cx), static_cast<float>(cy)},
                    static_cast<float>(r),
                    {static_cast<float>(tx), static_cast<float>(ty)},
                    mask);

                if (!hit.has_value())
                    return JS_NULL;
                return makeHit(ctx, hit.value());
            }

            JSValue Physics::js_distance(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 2)
                    return JS_NewFloat64(ctx, -1.0);
                auto *a = unwrapShape(argv[0]);
                auto *b = unwrapShape(argv[1]);
                if (!a || !b)
                    return JS_NewFloat64(ctx, -1.0);
                return JS_NewFloat64(ctx, Bokken::Physics::World::get().distance(*a, *b));
            }

            // Joint factories. Each one parses a (rbA, rbB, params?)
            // argument list, fills in a Physics::Joints::*Params struct,
            // and wraps the returned b2JointId in a JS handle.

            JSValue Physics::js_joint_distance(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 2)
                    return JS_NULL;
                auto *a = unwrapRigidbody(argv[0]);
                auto *b = unwrapRigidbody(argv[1]);
                if (!a || !b)
                    return JS_NULL;

                Bokken::Physics::Joints::DistanceParameters p;
                if (argc >= 3 && JS_IsObject(argv[2]))
                {
                    readVec2(ctx, argv[2], "anchorA", p.anchorA);
                    readVec2(ctx, argv[2], "anchorB", p.anchorB);
                    readNumber(ctx, argv[2], "length", p.length);
                    readNumber(ctx, argv[2], "minimumLength", p.minimumLength);
                    readNumber(ctx, argv[2], "maximumLength", p.maximumLength);
                    readBool(ctx, argv[2], "collideConnected", p.collideConnected);
                    readNumber(ctx, argv[2], "hertz", p.hertz);
                    readNumber(ctx, argv[2], "dampingRatio", p.dampingRatio);
                }

                b2JointId id = Bokken::Physics::Joints::createDistance(a, b, p);
                if (B2_IS_NULL(id))
                    return JS_NULL;
                return wrapJoint(ctx, id);
            }

            JSValue Physics::js_joint_revolute(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 2)
                    return JS_NULL;
                auto *a = unwrapRigidbody(argv[0]);
                auto *b = unwrapRigidbody(argv[1]);
                if (!a || !b)
                    return JS_NULL;

                Bokken::Physics::Joints::RevoluteParameters p;
                if (argc >= 3 && JS_IsObject(argv[2]))
                {
                    readVec2(ctx, argv[2], "anchor", p.anchor);
                    readBool(ctx, argv[2], "collideConnected", p.collideConnected);
                    readBool(ctx, argv[2], "enableLimit", p.enableLimit);
                    readNumber(ctx, argv[2], "lowerAngle", p.lowerAngle);
                    readNumber(ctx, argv[2], "upperAngle", p.upperAngle);
                    readBool(ctx, argv[2], "enableMotor", p.enableMotor);
                    readNumber(ctx, argv[2], "motorSpeed", p.motorSpeed);
                    readNumber(ctx, argv[2], "maximumMotorTorque", p.maximumMotorTorque);
                    readNumber(ctx, argv[2], "referenceAngle", p.referenceAngle);
                }

                b2JointId id = Bokken::Physics::Joints::createRevolute(a, b, p);
                if (B2_IS_NULL(id))
                    return JS_NULL;
                return wrapJoint(ctx, id);
            }

            JSValue Physics::js_joint_prismatic(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 2)
                    return JS_NULL;
                auto *a = unwrapRigidbody(argv[0]);
                auto *b = unwrapRigidbody(argv[1]);
                if (!a || !b)
                    return JS_NULL;

                Bokken::Physics::Joints::PrismaticParameters p;
                if (argc >= 3 && JS_IsObject(argv[2]))
                {
                    readVec2(ctx, argv[2], "anchor", p.anchor);
                    readVec2(ctx, argv[2], "axis", p.axis);
                    readBool(ctx, argv[2], "collideConnected", p.collideConnected);
                    readBool(ctx, argv[2], "enableLimit", p.enableLimit);
                    readNumber(ctx, argv[2], "lowerTranslation", p.lowerTranslation);
                    readNumber(ctx, argv[2], "upperTranslation", p.upperTranslation);
                    readBool(ctx, argv[2], "enableMotor", p.enableMotor);
                    readNumber(ctx, argv[2], "motorSpeed", p.motorSpeed);
                    readNumber(ctx, argv[2], "maximumMotorForce", p.maximumMotorForce);
                    readNumber(ctx, argv[2], "referenceAngle", p.referenceAngle);
                }

                b2JointId id = Bokken::Physics::Joints::createPrismatic(a, b, p);
                if (B2_IS_NULL(id))
                    return JS_NULL;
                return wrapJoint(ctx, id);
            }

            JSValue Physics::js_joint_weld(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 2)
                    return JS_NULL;
                auto *a = unwrapRigidbody(argv[0]);
                auto *b = unwrapRigidbody(argv[1]);
                if (!a || !b)
                    return JS_NULL;

                Bokken::Physics::Joints::WeldParameters p;
                if (argc >= 3 && JS_IsObject(argv[2]))
                {
                    readVec2(ctx, argv[2], "anchor", p.anchor);
                    readBool(ctx, argv[2], "collideConnected", p.collideConnected);
                    readNumber(ctx, argv[2], "linearHertz", p.linearHertz);
                    readNumber(ctx, argv[2], "linearDampingRatio", p.linearDampingRatio);
                    readNumber(ctx, argv[2], "angularHertz", p.angularHertz);
                    readNumber(ctx, argv[2], "angularDampingRatio", p.angularDampingRatio);
                }

                b2JointId id = Bokken::Physics::Joints::createWeld(a, b, p);
                if (B2_IS_NULL(id))
                    return JS_NULL;
                return wrapJoint(ctx, id);
            }

            // JS: physics.joints.mouse(rb, params?) — single-body joint.
            JSValue Physics::js_joint_mouse(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 1)
                    return JS_NULL;
                auto *target = unwrapRigidbody(argv[0]);
                if (!target)
                    return JS_NULL;

                Bokken::Physics::Joints::MouseParameters p;
                if (argc >= 2 && JS_IsObject(argv[1]))
                {
                    readVec2(ctx, argv[1], "target", p.target);
                    readNumber(ctx, argv[1], "maximumForce", p.maximumForce);
                    readNumber(ctx, argv[1], "hertz", p.hertz);
                    readNumber(ctx, argv[1], "dampingRatio", p.dampingRatio);
                    readBool(ctx, argv[1], "collideConnected", p.collideConnected);
                }

                b2JointId id = Bokken::Physics::Joints::createMouse(target, p);
                if (B2_IS_NULL(id))
                    return JS_NULL;
                return wrapJoint(ctx, id);
            }

            JSValue Physics::js_joint_motor(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 2)
                    return JS_NULL;
                auto *a = unwrapRigidbody(argv[0]);
                auto *b = unwrapRigidbody(argv[1]);
                if (!a || !b)
                    return JS_NULL;

                Bokken::Physics::Joints::MotorParameters p;
                if (argc >= 3 && JS_IsObject(argv[2]))
                {
                    readVec2(ctx, argv[2], "linearOffset", p.linearOffset);
                    readNumber(ctx, argv[2], "angularOffset", p.angularOffset);
                    readNumber(ctx, argv[2], "maximumForce", p.maximumForce);
                    readNumber(ctx, argv[2], "maximumTorque", p.maximumTorque);
                    readNumber(ctx, argv[2], "correctionFactor", p.correctionFactor);
                    readBool(ctx, argv[2], "collideConnected", p.collideConnected);
                }

                b2JointId id = Bokken::Physics::Joints::createMotor(a, b, p);
                if (B2_IS_NULL(id))
                    return JS_NULL;
                return wrapJoint(ctx, id);
            }

            JSValue Physics::js_joint_wheel(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 2)
                    return JS_NULL;
                auto *a = unwrapRigidbody(argv[0]);
                auto *b = unwrapRigidbody(argv[1]);
                if (!a || !b)
                    return JS_NULL;

                Bokken::Physics::Joints::WheelParameters p;
                if (argc >= 3 && JS_IsObject(argv[2]))
                {
                    readVec2(ctx, argv[2], "anchor", p.anchor);
                    readVec2(ctx, argv[2], "axis", p.axis);
                    readBool(ctx, argv[2], "collideConnected", p.collideConnected);
                    readBool(ctx, argv[2], "enableLimit", p.enableLimit);
                    readNumber(ctx, argv[2], "lowerTranslation", p.lowerTranslation);
                    readNumber(ctx, argv[2], "upperTranslation", p.upperTranslation);
                    readBool(ctx, argv[2], "enableMotor", p.enableMotor);
                    readNumber(ctx, argv[2], "motorSpeed", p.motorSpeed);
                    readNumber(ctx, argv[2], "maximumMotorTorque", p.maximumMotorTorque);
                    readNumber(ctx, argv[2], "hertz", p.hertz);
                    readNumber(ctx, argv[2], "dampingRatio", p.dampingRatio);
                }

                b2JointId id = Bokken::Physics::Joints::createWheel(a, b, p);
                if (B2_IS_NULL(id))
                    return JS_NULL;
                return wrapJoint(ctx, id);
            }

            JSValue Physics::js_joint_filter(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 2)
                    return JS_NULL;
                auto *a = unwrapRigidbody(argv[0]);
                auto *b = unwrapRigidbody(argv[1]);
                if (!a || !b)
                    return JS_NULL;

                Bokken::Physics::Joints::FilterParameters p;
                b2JointId id = Bokken::Physics::Joints::createFilter(a, b, p);
                if (B2_IS_NULL(id))
                    return JS_NULL;
                return wrapJoint(ctx, id);
            }

            // Joint instance methods.

            JSValue Physics::js_joint_destroy(JSContext *, JSValueConst this_val, int, JSValueConst *)
            {
                auto *p = unwrapJoint(this_val);
                if (!p || p->destroyed)
                    return JS_UNDEFINED;
                Bokken::Physics::Joints::destroy(p->id);
                p->destroyed = true;
                p->id = b2_nullJointId;
                return JS_UNDEFINED;
            }

            JSValue Physics::js_joint_is_valid(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *p = unwrapJoint(this_val);
                if (!p || p->destroyed)
                    return JS_FALSE;
                return JS_NewBool(ctx, b2Joint_IsValid(p->id));
            }

            // Returns a stable string for the joint type so JS can switch
            // on it without including Box2D's enum values.
            JSValue Physics::js_joint_get_type(JSContext *ctx, JSValueConst this_val)
            {
                auto *p = unwrapJoint(this_val);
                if (!p || p->destroyed || !b2Joint_IsValid(p->id))
                    return JS_NewString(ctx, "invalid");

                int t = Bokken::Physics::Joints::type(p->id);
                const char *name = "unknown";
                switch (t)
                {
                case b2_distanceJoint:  name = "distance"; break;
                case b2_revoluteJoint:  name = "revolute"; break;
                case b2_prismaticJoint: name = "prismatic"; break;
                case b2_weldJoint:      name = "weld"; break;
                case b2_mouseJoint:     name = "mouse"; break;
                case b2_motorJoint:     name = "motor"; break;
                case b2_wheelJoint:     name = "wheel"; break;
                case b2_filterJoint:    name = "filter"; break;
                default: break;
                }
                return JS_NewString(ctx, name);
            }

            // Motor / limit setters dispatch by joint type. Each accepts
            // its argument in pixels / degrees / boolean as appropriate
            // and converts at the boundary. Calling on the wrong joint
            // type is a silent no-op — gameplay code that holds a typed
            // joint reference shouldn't pay for a runtime error here.

            JSValue Physics::js_joint_set_motor_speed(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *p = unwrapJoint(this_val);
                if (!p || p->destroyed || argc < 1)
                    return JS_UNDEFINED;
                if (!b2Joint_IsValid(p->id))
                    return JS_UNDEFINED;
                double v;
                if (JS_ToFloat64(ctx, &v, argv[0]) < 0)
                    return JS_EXCEPTION;
                float fv = static_cast<float>(v);

                int t = Bokken::Physics::Joints::type(p->id);
                constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
                switch (t)
                {
                case b2_revoluteJoint:
                    b2RevoluteJoint_SetMotorSpeed(p->id, fv * DEG_TO_RAD);
                    break;
                case b2_prismaticJoint:
                    b2PrismaticJoint_SetMotorSpeed(p->id, Bokken::Physics::World::get().pxToM(fv));
                    break;
                case b2_wheelJoint:
                    b2WheelJoint_SetMotorSpeed(p->id, fv * DEG_TO_RAD);
                    break;
                default:
                    break;
                }
                return JS_UNDEFINED;
            }

            JSValue Physics::js_joint_set_max_motor_torque(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *p = unwrapJoint(this_val);
                if (!p || p->destroyed || argc < 1)
                    return JS_UNDEFINED;
                if (!b2Joint_IsValid(p->id))
                    return JS_UNDEFINED;
                double v;
                if (JS_ToFloat64(ctx, &v, argv[0]) < 0)
                    return JS_EXCEPTION;
                float fv = static_cast<float>(v);

                int t = Bokken::Physics::Joints::type(p->id);
                switch (t)
                {
                case b2_revoluteJoint:
                    b2RevoluteJoint_SetMaxMotorTorque(p->id, fv);
                    break;
                case b2_wheelJoint:
                    b2WheelJoint_SetMaxMotorTorque(p->id, fv);
                    break;
                default:
                    break;
                }
                return JS_UNDEFINED;
            }

            JSValue Physics::js_joint_set_max_motor_force(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *p = unwrapJoint(this_val);
                if (!p || p->destroyed || argc < 1)
                    return JS_UNDEFINED;
                if (!b2Joint_IsValid(p->id))
                    return JS_UNDEFINED;
                double v;
                if (JS_ToFloat64(ctx, &v, argv[0]) < 0)
                    return JS_EXCEPTION;
                float fv = static_cast<float>(v);

                int t = Bokken::Physics::Joints::type(p->id);
                if (t == b2_prismaticJoint)
                    b2PrismaticJoint_SetMaxMotorForce(p->id, fv);
                else if (t == b2_mouseJoint)
                    b2MouseJoint_SetMaxForce(p->id, fv);
                else if (t == b2_motorJoint)
                    b2MotorJoint_SetMaxForce(p->id, fv);
                return JS_UNDEFINED;
            }

            JSValue Physics::js_joint_enable_motor(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *p = unwrapJoint(this_val);
                if (!p || p->destroyed || argc < 1)
                    return JS_UNDEFINED;
                if (!b2Joint_IsValid(p->id))
                    return JS_UNDEFINED;
                bool on = JS_ToBool(ctx, argv[0]);

                int t = Bokken::Physics::Joints::type(p->id);
                switch (t)
                {
                case b2_revoluteJoint:  b2RevoluteJoint_EnableMotor(p->id, on); break;
                case b2_prismaticJoint: b2PrismaticJoint_EnableMotor(p->id, on); break;
                case b2_wheelJoint:     b2WheelJoint_EnableMotor(p->id, on); break;
                default: break;
                }
                return JS_UNDEFINED;
            }

            JSValue Physics::js_joint_enable_limit(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *p = unwrapJoint(this_val);
                if (!p || p->destroyed || argc < 1)
                    return JS_UNDEFINED;
                if (!b2Joint_IsValid(p->id))
                    return JS_UNDEFINED;
                bool on = JS_ToBool(ctx, argv[0]);

                int t = Bokken::Physics::Joints::type(p->id);
                switch (t)
                {
                case b2_revoluteJoint:  b2RevoluteJoint_EnableLimit(p->id, on); break;
                case b2_prismaticJoint: b2PrismaticJoint_EnableLimit(p->id, on); break;
                case b2_wheelJoint:     b2WheelJoint_EnableLimit(p->id, on); break;
                default: break;
                }
                return JS_UNDEFINED;
            }

            JSValue Physics::js_joint_set_limits(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *p = unwrapJoint(this_val);
                if (!p || p->destroyed || argc < 2)
                    return JS_UNDEFINED;
                if (!b2Joint_IsValid(p->id))
                    return JS_UNDEFINED;
                double lo, hi;
                if (JS_ToFloat64(ctx, &lo, argv[0]) < 0 ||
                    JS_ToFloat64(ctx, &hi, argv[1]) < 0)
                    return JS_EXCEPTION;
                float flo = static_cast<float>(lo);
                float fhi = static_cast<float>(hi);

                int t = Bokken::Physics::Joints::type(p->id);
                constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
                switch (t)
                {
                case b2_revoluteJoint:
                    b2RevoluteJoint_SetLimits(p->id, flo * DEG_TO_RAD, fhi * DEG_TO_RAD);
                    break;
                case b2_prismaticJoint:
                {
                    auto &world = Bokken::Physics::World::get();
                    b2PrismaticJoint_SetLimits(p->id, world.pxToM(flo), world.pxToM(fhi));
                    break;
                }
                case b2_wheelJoint:
                {
                    auto &world = Bokken::Physics::World::get();
                    b2WheelJoint_SetLimits(p->id, world.pxToM(flo), world.pxToM(fhi));
                    break;
                }
                case b2_distanceJoint:
                {
                    auto &world = Bokken::Physics::World::get();
                    b2DistanceJoint_SetLengthRange(p->id, world.pxToM(flo), world.pxToM(fhi));
                    break;
                }
                default:
                    break;
                }
                return JS_UNDEFINED;
            }

            JSValue Physics::js_joint_set_target(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *p = unwrapJoint(this_val);
                if (!p || p->destroyed || argc < 2)
                    return JS_UNDEFINED;
                if (!b2Joint_IsValid(p->id))
                    return JS_UNDEFINED;
                double x, y;
                if (JS_ToFloat64(ctx, &x, argv[0]) < 0 ||
                    JS_ToFloat64(ctx, &y, argv[1]) < 0)
                    return JS_EXCEPTION;

                int t = Bokken::Physics::Joints::type(p->id);
                if (t == b2_mouseJoint)
                {
                    auto &world = Bokken::Physics::World::get();
                    b2Vec2 target = world.pxToB2({static_cast<float>(x), static_cast<float>(y)});
                    b2MouseJoint_SetTarget(p->id, target);
                }
                return JS_UNDEFINED;
            }

            // Finalizers — called by the JS GC. Joints intentionally do
            // NOT destroy the b2Joint here: gameplay code commonly stores
            // joints in arrays without strong references, and if the GC
            // unrooted such a joint we'd silently break the constraint.
            // Explicit destroy() is the contract.

            void Physics::joint_finalizer(JSRuntime *, JSValue val)
            {
                auto *p = static_cast<JointPayload *>(JS_GetOpaque(val, s_joint_class_id));
                if (!p)
                    return;
                delete p;
            }

            void Physics::shape_finalizer(JSRuntime *, JSValue val)
            {
                auto *p = static_cast<b2ShapeId *>(JS_GetOpaque(val, s_shape_class_id));
                if (!p)
                    return;
                delete p;
            }

            int Physics::declare(JSContext *ctx, JSModuleDef *m)
            {
                return JS_AddModuleExport(ctx, m, "default");
            }

            int Physics::init(JSContext *ctx, JSModuleDef *m)
            {
                JSRuntime *rt = JS_GetRuntime(ctx);

                // Register opaque classes for joint and shape handles.
                JS_NewClassID(rt, &s_joint_class_id);
                JS_NewClassID(rt, &s_shape_class_id);

                static JSClassDef jointClass = {"Joint", .finalizer = joint_finalizer};
                static JSClassDef shapeClass = {"ShapeId", .finalizer = shape_finalizer};
                JS_NewClass(rt, s_joint_class_id, &jointClass);
                JS_NewClass(rt, s_shape_class_id, &shapeClass);

                // Joint prototype with method table.
                JSValue jointProto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, jointProto, s_jointFuncs,
                                           sizeof(s_jointFuncs) / sizeof(s_jointFuncs[0]));
                JS_SetClassProto(ctx, s_joint_class_id, jointProto);

                // Default-export object.
                JSValue def = JS_NewObject(ctx);

                JS_SetPropertyStr(ctx, def, "setGravity",
                                  JS_NewCFunction(ctx, &Physics::js_set_gravity, "setGravity", 2));
                JS_SetPropertyStr(ctx, def, "getGravity",
                                  JS_NewCFunction(ctx, &Physics::js_get_gravity, "getGravity", 0));
                JS_SetPropertyStr(ctx, def, "setMeter",
                                  JS_NewCFunction(ctx, &Physics::js_set_meter, "setMeter", 1));
                JS_SetPropertyStr(ctx, def, "getMeter",
                                  JS_NewCFunction(ctx, &Physics::js_get_meter, "getMeter", 0));
                JS_SetPropertyStr(ctx, def, "setSubSteps",
                                  JS_NewCFunction(ctx, &Physics::js_set_substeps, "setSubSteps", 1));
                JS_SetPropertyStr(ctx, def, "getSubSteps",
                                  JS_NewCFunction(ctx, &Physics::js_get_substeps, "getSubSteps", 0));

                JS_SetPropertyStr(ctx, def, "raycast",
                                  JS_NewCFunction(ctx, &Physics::js_raycast, "raycast", 6));
                JS_SetPropertyStr(ctx, def, "raycastNearest",
                                  JS_NewCFunction(ctx, &Physics::js_raycast_nearest, "raycastNearest", 6));
                JS_SetPropertyStr(ctx, def, "overlapAABB",
                                  JS_NewCFunction(ctx, &Physics::js_overlap_aabb, "overlapAABB", 5));
                JS_SetPropertyStr(ctx, def, "overlapCircle",
                                  JS_NewCFunction(ctx, &Physics::js_overlap_circle, "overlapCircle", 4));
                JS_SetPropertyStr(ctx, def, "circleCast",
                                  JS_NewCFunction(ctx, &Physics::js_circle_cast, "circleCast", 6));
                JS_SetPropertyStr(ctx, def, "distance",
                                  JS_NewCFunction(ctx, &Physics::js_distance, "distance", 2));

                // Joint factories under physics.joints.*
                JSValue joints = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, joints, "distance",
                                  JS_NewCFunction(ctx, &Physics::js_joint_distance, "distance", 3));
                JS_SetPropertyStr(ctx, joints, "revolute",
                                  JS_NewCFunction(ctx, &Physics::js_joint_revolute, "revolute", 3));
                JS_SetPropertyStr(ctx, joints, "prismatic",
                                  JS_NewCFunction(ctx, &Physics::js_joint_prismatic, "prismatic", 3));
                JS_SetPropertyStr(ctx, joints, "weld",
                                  JS_NewCFunction(ctx, &Physics::js_joint_weld, "weld", 3));
                JS_SetPropertyStr(ctx, joints, "mouse",
                                  JS_NewCFunction(ctx, &Physics::js_joint_mouse, "mouse", 2));
                JS_SetPropertyStr(ctx, joints, "motor",
                                  JS_NewCFunction(ctx, &Physics::js_joint_motor, "motor", 3));
                JS_SetPropertyStr(ctx, joints, "wheel",
                                  JS_NewCFunction(ctx, &Physics::js_joint_wheel, "wheel", 3));
                JS_SetPropertyStr(ctx, joints, "filter",
                                  JS_NewCFunction(ctx, &Physics::js_joint_filter, "filter", 2));
                JS_SetPropertyStr(ctx, def, "joints", joints);

                JS_SetModuleExport(ctx, m, "default", def);
                return 0;
            }

        }
    }
}

