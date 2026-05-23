#pragma once

#include "GameObject.hpp"
#include "../../game_object/Behaviour.hpp"
#include "../../game_object/Collider2D.hpp"
#include "../../physics/World.hpp"

#include <SDL3/SDL.h>
#include <quickjs.h>

namespace Bokken
{
    namespace Scripting
    {
        namespace Modules
        {
            /**
             * Bridges the engine's native Behaviour component lifecycle
             * to a user-defined JavaScript class.
             *
             * Usage from C++ (called by GameObject's addComponent dispatch
             * when the className doesn't match a built-in component):
             *
             *   1. JS_CallConstructor on the user's class to get a fresh
             *      instance JSValue.
             *   2. new JSBehaviour(ctx, instance) — the adapter dups its
             *      own ref to the instance.
             *   3. Set the instance's `gameObject` property so user code
             *      can reach siblings via this.gameObject.getComponent(...).
             *   4. Hand it to GameObject::Base::addBehaviour().
             *   5. Call onAttach() — that's a no-op at the C++ level
             *      but flips the started-flag tracking so onStart fires
             *      exactly once on the next update tick.
             *
             * Lifecycle forwarding:
             *   - onStart() runs once, lazily, on the first update()
             *     call. The frame at which onStart runs gets onUpdate
             *     immediately afterwards. That matches the Unity
             *     convention people will recognise.
             *   - onUpdate(deltaTime) and onFixedUpdate(deltaTime) call
             *     the matching JS methods if they exist; methods are
             *     looked up once at attach time and cached.
             *   - Physics events (onCollisionBegin, onSensorEnd, etc.)
             *     are dispatched through Behaviour's static dispatch
             *     helpers; we override the virtual hooks here and
             *     forward them with structured argument objects.
             *
             * Exception handling:
             *   Every JS_Call is followed by an exception check that
             *   logs and clears. A throwing user behaviour does not
             *   take down the engine — the next tick fires as usual.
            */
            class JSBehaviour final : public Bokken::GameObject::Behaviour
            {
            public:
                JSBehaviour(JSContext *ctx, JSValue instance);
                ~JSBehaviour() override;

                JSBehaviour(const JSBehaviour &) = delete;
                JSBehaviour &operator=(const JSBehaviour &) = delete;

                /** Get the underlying JS instance — used by addComponent
                 *  to apply the optional config object after wrapping. */
                JSValue instance() const { return m_instance; }

                // Component lifecycle.
                void onAttach() override;
                void onDestroy() override;
                void update(float deltaTime) override;
                void fixedUpdate(float deltaTime) override;

                // Physics callbacks — forwarded into JS.
                void onCollisionBegin(Bokken::GameObject::Collider2D *other,
                                      const b2Manifold &manifold) override;
                void onCollisionEnd(Bokken::GameObject::Collider2D *other) override;
                void onCollisionHit(Bokken::GameObject::Collider2D *other,
                                    const b2ContactHitEvent &event) override;
                void onSensorBegin(Bokken::GameObject::Collider2D *other) override;
                void onSensorEnd(Bokken::GameObject::Collider2D *other) override;

            private:
                // Look up a method by name on the instance and cache it.
                // Cached values are dup'd; freed in the destructor.
                void cacheHook(const char *name, JSValue &out);

                // Invoke a cached hook with zero or more args. Swallows
                // exceptions after logging — see class comment.
                void invokeHook(JSValue hook, int argc, JSValueConst *argv,
                                const char *hookName) const;

                // Build a wrapper for the *other* GameObject in a physics
                // event. Returns JS_NULL for a null collider.
                JSValue wrapOther(Bokken::GameObject::Collider2D *other) const;

                JSContext *m_ctx;
                JSValue m_instance = JS_UNDEFINED;

                // Cached method handles. Looked up once at attach so we
                // don't pay a string-keyed property lookup on every tick.
                JSValue m_onStart        = JS_UNDEFINED;
                JSValue m_onUpdate       = JS_UNDEFINED;
                JSValue m_onFixedUpdate  = JS_UNDEFINED;
                JSValue m_onDestroy      = JS_UNDEFINED;
                JSValue m_onCollisionBegin = JS_UNDEFINED;
                JSValue m_onCollisionEnd   = JS_UNDEFINED;
                JSValue m_onCollisionHit   = JS_UNDEFINED;
                JSValue m_onSensorBegin    = JS_UNDEFINED;
                JSValue m_onSensorEnd      = JS_UNDEFINED;

                // True after the first update() tick has fired onStart.
                // We use a flag rather than calling onStart from onAttach
                // because GameObjects are often constructed fully (with
                // all their components) before the first frame; users
                // typically expect onStart to land *after* every sibling
                // is in place.
                bool m_started = false;
            };
        }
    }
}
