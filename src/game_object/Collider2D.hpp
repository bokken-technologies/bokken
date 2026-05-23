#pragma once

#include "Component.hpp"
#include "Base.hpp"
#include "Rigidbody2D.hpp"
#include "Behaviour.hpp"
#include "../physics/World.hpp"

#include <box2d/box2d.h>
#include <glm/glm.hpp>
#include <SDL3/SDL.h>

#include <cstdint>
#include <functional>

namespace Bokken
{
    namespace GameObject
    {

        /**
         * Collider2D — base class for every shape component.
         *
         * Concrete subclasses (BoxCollider2D, CircleCollider2D, ...)
         * override createShape() to call the matching b2CreateXxxShape
         * function and return the resulting handle.
         *
         * Sibling-component lookup:
         *   onAttach() looks for a sibling Rigidbody2D on the same
         *   GameObject. If one exists and already has a body, we attach
         *   our shape directly to it. If one exists but has not yet
         *   created its body (component ordering edge case), we still
         *   piggy-back on its body — Rigidbody2D::onAttach runs synchronously
         *   on addComponent so the only way this could be reversed is if
         *   the user calls addComponent(Collider) before addComponent(RB),
         *   in which case the collider creates its own hidden static body
         *   and the user gets a static obstacle. That matches Love2D's
         *   "shape without a body becomes a static fixture" behaviour.
         *
         * Material:
         *   density, friction, restitution and tangentSpeed map directly
         *   to b2ShapeDef and b2SurfaceMaterial. Mutating these fields
         *   after onAttach has no effect; use the corresponding setters
         *   below for live changes that round-trip into Box2D.
         *
         * Filtering:
         *   categoryBits / maskBits / groupIndex map to b2Filter. Bokken
         *   widens these to 64 bits in its public API but Box2D currently
         *   uses 64-bit category/mask too in v3, so the conversion is
         *   one-to-one.
        */
        class Collider2D : public Component
        {
        public:
            // Material — read at onAttach() time.
            float density       = 1.0f;
            float friction      = 0.3f;
            float restitution   = 0.0f;
            float tangentSpeed  = 0.0f;     // v3 conveyor-belt support
            bool  isSensor      = false;    // trigger-only

            // Filter bits.
            uint64_t categoryBits = 0x0001ull;
            uint64_t maskBits     = 0xFFFFFFFFFFFFFFFFull;
            int32_t  groupIndex   = 0;

            // Scripting callbacks. The JS module (`bokken/physics`) sets
            // these to closures that translate from native event data to
            // a JS event object and invoke a user-provided handler. Set
            // any field to nullptr (or leave at default-constructed) to
            // skip dispatch into JS for that event.
            //
            // These are stored on the collider rather than on the
            // GameObject because Box2D events are shape-keyed, not
            // body-keyed: a player with two hitboxes can route events
            // to different scripts per hitbox.
            std::function<void(Collider2D *other, const b2Manifold &)> jsOnCollisionBegin;
            std::function<void(Collider2D *other)> jsOnCollisionEnd;
            std::function<void(Collider2D *other, const b2ContactHitEvent &)> jsOnCollisionHit;
            std::function<void(Collider2D *other)> jsOnSensorBegin;
            std::function<void(Collider2D *other)> jsOnSensorEnd;

            // Forwarded contact / sensor callbacks. These are invoked
            // from Physics::World::dispatchEvents() and are NOT virtual
            // for the user's component classes — instead, they walk the
            // sibling components on the GameObject looking for any that
            // implement the matching Behaviour2D-style hook (defined in
            // Behaviour.hpp). Keeping the bridge non-virtual here lets
            // user code subclass freely without having to remember the
            // exact signature.
            void onContactBegin(Collider2D *other, const b2Manifold &manifold);
            void onContactEnd(Collider2D *other);
            void onContactHit(Collider2D *other, const b2ContactHitEvent &event);
            void onSensorBegin(Collider2D *other);
            void onSensorEnd(Collider2D *other);

            void onAttach() override;
            void onDestroy() override;

            // Live API.

            void setDensity(float d);
            void setFriction(float f);
            void setRestitution(float r);
            void setTangentSpeed(float v);
            void setSensor(bool sensor);
            void setFilter(uint64_t categoryBits, uint64_t maskBits, int32_t groupIndex);

            // Expose handles so the JS layer and queries can identify shapes.
            b2ShapeId shapeId() const { return m_shape; }

            // True if this collider created its own hidden static body
            // (i.e. there was no sibling Rigidbody2D at attach time).
            bool ownsBody() const { return m_ownsBody; }

            // Idle by default — colliders don't run per-frame work.
            bool isIdle() const override { return true; }

        protected:
            // Subclass hook — fill in `def` (already populated with the
            // shared material/filter fields) and call the appropriate
            // b2CreateXxxShape, returning its id.
            virtual b2ShapeId createShape(b2BodyId body, const b2ShapeDef &def) = 0;

            b2ShapeId m_shape   = b2_nullShapeId;
            b2BodyId  m_body    = b2_nullBodyId;
            bool      m_ownsBody = false;

            // Helper used by every subclass to build the b2ShapeDef from
            // the shared material/filter fields.
            b2ShapeDef makeShapeDef() const;
        };

    }
}
