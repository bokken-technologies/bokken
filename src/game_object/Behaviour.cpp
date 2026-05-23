#include "Behaviour.hpp"

namespace Bokken
{
    namespace GameObject
    {
        void Behaviour::onAttach()
        {
        }

        // Dispatch helpers. These walk every component on the GameObject
        // and dynamic_cast each one to Behaviour* so user-defined
        // subclasses (which register themselves under their own type_index
        // in the component map) all get notified.
        //
        // The cost of dynamic_cast per component per event is well below
        // the cost of the contact resolution itself, so we don't pay for
        // a separate registry. If profiling ever shows this matters,
        // Collider2D could cache a Behaviour* sibling pointer at attach
        // time — but that breaks if a Behaviour is added after the
        // collider, so we go with the live walk for correctness.

        void Behaviour::dispatchCollisionBegin(Base *go, Collider2D *other, const b2Manifold &manifold)
        {
            if (!go)
                return;
            go->forEachComponent([&](Component *c)
            {
                if (auto *b = dynamic_cast<Behaviour *>(c))
                    if (b->enabled)
                        b->onCollisionBegin(other, manifold);
            });
        }

        void Behaviour::dispatchCollisionEnd(Base *go, Collider2D *other)
        {
            if (!go)
                return;
            go->forEachComponent([&](Component *c)
            {
                if (auto *b = dynamic_cast<Behaviour *>(c))
                    if (b->enabled)
                        b->onCollisionEnd(other);
            });
        }

        void Behaviour::dispatchCollisionHit(Base *go, Collider2D *other, const b2ContactHitEvent &event)
        {
            if (!go)
                return;
            go->forEachComponent([&](Component *c)
            {
                if (auto *b = dynamic_cast<Behaviour *>(c))
                    if (b->enabled)
                        b->onCollisionHit(other, event);
            });
        }

        void Behaviour::dispatchSensorBegin(Base *go, Collider2D *other)
        {
            if (!go)
                return;
            go->forEachComponent([&](Component *c)
            {
                if (auto *b = dynamic_cast<Behaviour *>(c))
                    if (b->enabled)
                        b->onSensorBegin(other);
            });
        }

        void Behaviour::dispatchSensorEnd(Base *go, Collider2D *other)
        {
            if (!go)
                return;
            go->forEachComponent([&](Component *c)
            {
                if (auto *b = dynamic_cast<Behaviour *>(c))
                    if (b->enabled)
                        b->onSensorEnd(other);
            });
        }
    }
}
