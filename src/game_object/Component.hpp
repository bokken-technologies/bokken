#pragma once

namespace Bokken
{
    namespace GameObject
    {
        class Base;
    }

    namespace GameObject
    {
        class Component
        {
        public:
            virtual ~Component() = default;

            // Back-pointer set by GameObject::addComponent.
            Base *gameObject = nullptr;

            bool enabled = true;

            virtual void onAttach() {}
            virtual void update(float deltaTime) {}
            virtual void fixedUpdate(float deltaTime) {}
            virtual void onDestroy() {}

            /**
             * Returns true when this component has finished its work and
             * has no ongoing state. Used by the destroyWhenIdle system to
             * auto-clean GameObjects whose components have all gone quiet.
             *
             * Defaults to false — components that don't override this are
             * considered permanently active, which prevents accidental
             * destruction of objects with simple components like Transform2D
             * or Mesh2D.
             *
             * Components with a natural "done" state (particle emitters,
             * distortion shockwaves, non-looping animations) override this
             * to return true when they've finished.
            */
            virtual bool isIdle() const { return false; }
        };
    }
}