#pragma once

#include "Component.hpp"
#include "Base.hpp"
#include "Transform2D.hpp"

#include <box2d/box2d.h>

namespace Bokken
{
    namespace GameObject
    {
        class Collider2D;

        /**
         * Abstract base for native game scripts.
         *
         * JS scripts query their own transform via
         * gameObject->getComponent<Transform2D>(). The lifecycle hooks
         * onStart / onUpdate / onFixedUpdate are also driven from the
         * scripting Engine; native subclasses can override them too.
         *
         * Collision and sensor hooks were added with the physics layer.
         * They default to no-ops so existing native Behaviours don't need
         * to know about them. The static dispatch helpers walk every
         * Behaviour-derived component on a GameObject and invoke the
         * matching virtual — Collider2D::onContactBegin/End/Hit and the
         * sensor analogues call into these.
        */
        class Behaviour : public Component
        {
        public:
            void onAttach() override;

            virtual void onStart() {}
            virtual void onUpdate(float deltaTime) {}
            virtual void onFixedUpdate(float deltaTime) {}

            // Physics callbacks.
            virtual void onCollisionBegin(Collider2D *other, const b2Manifold &manifold) { (void)other; (void)manifold; }
            virtual void onCollisionEnd(Collider2D *other) { (void)other; }
            virtual void onCollisionHit(Collider2D *other, const b2ContactHitEvent &event) { (void)other; (void)event; }
            virtual void onSensorBegin(Collider2D *other) { (void)other; }
            virtual void onSensorEnd(Collider2D *other) { (void)other; }

            // Dispatch helpers used by Collider2D. They walk every
            // Behaviour on the GameObject and forward the event. Defined
            // out-of-line because they need Base::s_objects' iteration
            // to be cheap, and because the static walk is sensitive to
            // the GameObject component-storage layout (which is private
            // to Base).
            static void dispatchCollisionBegin(Base *go, Collider2D *other, const b2Manifold &manifold);
            static void dispatchCollisionEnd(Base *go, Collider2D *other);
            static void dispatchCollisionHit(Base *go, Collider2D *other, const b2ContactHitEvent &event);
            static void dispatchSensorBegin(Base *go, Collider2D *other);
            static void dispatchSensorEnd(Base *go, Collider2D *other);
        };
    }
}
