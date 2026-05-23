#pragma once

#include "Base.hpp"
#include "GameObject.hpp"
#include "../../physics/World.hpp"
#include "../../physics/Joints.hpp"
#include "../../game_object/Rigidbody2D.hpp"

#include <SDL3/SDL.h>
#include <quickjs.h>

#include <cstring>

namespace Bokken
{
    namespace Scripting
    {
        namespace Modules
        {

            /**
             * `bokken/physics` — JS-facing world tunables, query API,
             * and joint factories.
             *
             * The world itself is a process-singleton (Bokken::Physics::World)
             * created by Loop::init before this module is registered, so
             * every method here just forwards to the singleton without
             * any per-module state.
             *
             * Surface (default-export object):
             *   physics.setGravity(x, y)
             *   physics.getGravity()                   → {x, y}
             *   physics.setMeter(pxPerMetre)
             *   physics.getMeter()                     → number
             *   physics.setSubSteps(n)
             *   physics.getSubSteps()                  → number
             *
             *   physics.raycast(ox, oy, dx, dy, maxDist, mask?)         → Hit[]
             *   physics.raycastNearest(ox, oy, dx, dy, maxDist, mask?)  → Hit | null
             *   physics.overlapAABB(lx, ly, ux, uy, mask?)              → ShapeId[]
             *   physics.overlapCircle(cx, cy, r, mask?)                 → ShapeId[]
             *   physics.circleCast(cx, cy, r, tx, ty, mask?)            → Hit | null
             *   physics.distance(shapeA, shapeB)                        → number
             *
             *   physics.joints.distance(rbA, rbB, params?)              → Joint
             *   physics.joints.revolute(rbA, rbB, params?)              → Joint
             *   physics.joints.prismatic(rbA, rbB, params?)             → Joint
             *   physics.joints.weld(rbA, rbB, params?)                  → Joint
             *   physics.joints.mouse(rb, params?)                       → Joint
             *   physics.joints.motor(rbA, rbB, params?)                 → Joint
             *   physics.joints.wheel(rbA, rbB, params?)                 → Joint
             *   physics.joints.filter(rbA, rbB)                         → Joint
             *
             * Joint handle methods:
             *   joint.destroy()
             *   joint.type                       (read-only string)
             *   joint.isValid()
             *   joint.setMotorSpeed(v)           — revolute, prismatic, wheel
             *   joint.setMaxMotorTorque(v)       — revolute, wheel
             *   joint.setMaxMotorForce(v)        — prismatic
             *   joint.enableMotor(bool)          — revolute, prismatic, wheel
             *   joint.enableLimit(bool)          — revolute, prismatic, wheel
             *   joint.setLimits(lower, upper)    — units depend on joint type
             *   joint.setTarget(x, y)            — mouse joint only
             *
             * Hit object shape:
             *   { shape: ShapeId, point: {x, y}, normal: {x, y}, fraction: number }
             *
             * ShapeId is an opaque object — pass it back to physics
             * functions, don't poke at its internals from JS.
            */
            class Physics : public Base
            {
            public:
                Physics() : Base("bokken/physics") {}

                int declare(JSContext *ctx, JSModuleDef *m) override;
                int init(JSContext *ctx, JSModuleDef *m) override;

                // World tunables.
                static JSValue js_set_gravity(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_get_gravity(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_set_meter(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_get_meter(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_set_substeps(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_get_substeps(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

                // Queries.
                static JSValue js_raycast(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_raycast_nearest(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_overlap_aabb(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_overlap_circle(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_circle_cast(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_distance(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

                // Joint factories.
                static JSValue js_joint_distance(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_joint_revolute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_joint_prismatic(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_joint_weld(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_joint_mouse(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_joint_motor(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_joint_wheel(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_joint_filter(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

                // Joint instance methods.
                static JSValue js_joint_destroy(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_joint_is_valid(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_joint_get_type(JSContext *ctx, JSValueConst this_val);
                static JSValue js_joint_set_motor_speed(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_joint_set_max_motor_torque(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_joint_set_max_motor_force(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_joint_enable_motor(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_joint_enable_limit(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_joint_set_limits(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_joint_set_target(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

                // Class ids — joint and shape-id handles use QuickJS classes
                // so we get a finalizer for the heap-allocated payload.
                static inline JSClassID s_joint_class_id = 0;
                static inline JSClassID s_shape_class_id = 0;

                // Finalizer for joint handles. Joints survive JS GC unless
                // explicitly destroyed via joint.destroy() — the finalizer
                // only frees the wrapper, not the underlying b2Joint, so
                // gameplay code can safely drop references and the joint
                // continues to exist until the world or owning bodies go.
                static void joint_finalizer(JSRuntime *rt, JSValue val);

                // Finalizer for shape-id handles. Shape ids are values,
                // not owners — the finalizer only frees the wrapper.
                static void shape_finalizer(JSRuntime *rt, JSValue val);
            };

        }
    }
}
