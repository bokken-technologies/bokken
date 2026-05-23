#pragma once

#include <box2d/box2d.h>
#include <glm/glm.hpp>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace Bokken
{
    namespace Physics
    {

        /**
         * Single owner of the Box2D v3 world for a Bokken session.
         *
         * Box2D v3 is a C library — everything is opaque ids (b2WorldId,
         * b2BodyId, b2ShapeId, b2JointId). This class hides the world id
         * behind a singleton accessor and provides Bokken-flavoured
         * helpers for the things scripts and components actually do:
         * tweak gravity, step the world, run queries.
         *
         * Lifecycle:
         *   World::get().init();      // creates world from ProjectConfig defaults
         *   ...
         *   World::get().step(dt);    // call from Loop::tick fixedUpdate phase
         *   World::get().dispatchEvents();  // after step, before next variable update
         *   ...
         *   World::get().shutdown();
         *
         * Threading:
         *   Single-threaded use only. Box2D v3 has internal task threading
         *   but the public API calls here all happen on the game thread,
         *   matching how every other Bokken subsystem works.
         *
         * Coordinate convention:
         *   Bokken stores positions in pixels (Transform2D::position is in
         *   pixels for sprite/UI rendering). Box2D wants metres. The meter
         *   factor (`meter()`) converts: pixels = metres * meter, so a body
         *   at world-space (300, 100) with meter=30 lives at b2 (10, 3.33).
         *   Collider components and Rigidbody2D do this conversion at the
         *   API boundary — gameplay code never sees metres.
        */
        class World
        {
        public:
            static constexpr float DEFAULT_GRAVITY_X = 0.0f;
            static constexpr float DEFAULT_GRAVITY_Y = -9.81f;
            static constexpr float DEFAULT_METER = 30.0f;
            static constexpr int   DEFAULT_SUB_STEPS = 4;

            static World &get()
            {
                static World instance;
                return instance;
            }

            // Lifecycle.
            // Creates the b2World with default-ish gravity. Idempotent: a
            // second call with an existing world is a no-op so JS scripts
            // can defensively re-init without leaking handles.
            bool init();
            void shutdown();
            bool isReady() const { return B2_IS_NON_NULL(m_world); }

            // Frame integration.
            // step() advances the simulation by dt seconds using the
            // configured sub-step count. Bokken's Loop calls this from
            // fixedUpdate so the timestep is constant — Box2D requires
            // a fixed dt for stable contact solving.
            void step(float dt);

            // dispatchEvents() walks Box2D's contact / sensor / body event
            // arrays from the most recent step() and routes them to JS
            // through the Rigidbody2D / Collider2D component callbacks.
            // Must be called after step() and before the world is touched
            // again — Box2D's event arrays are invalidated by the next step.
            void dispatchEvents();

            // Tunables.
            void setGravity(const glm::vec2 &g);
            glm::vec2 gravity() const;

            // Pixels-per-metre. Setting this rebuilds nothing; it only
            // affects future conversions, so do it at startup before
            // creating colliders. Defaults to DEFAULT_METER.
            void setMeter(float pixelsPerMetre);
            float meter() const { return m_meter; }

            // Sub-step count for the constraint solver (>=1, typically 4).
            void setSubSteps(int n);
            int subSteps() const { return m_subSteps; }

            // Conversion helpers — public because Collider2D and
            // Rigidbody2D need them to translate between pixels and metres
            // at every API boundary.
            inline glm::vec2 pxToM(const glm::vec2 &p) const { return p / m_meter; }
            inline glm::vec2 mToPx(const glm::vec2 &p) const { return p * m_meter; }
            inline float pxToM(float v) const { return v / m_meter; }
            inline float mToPx(float v) const { return v * m_meter; }

            inline b2Vec2 pxToB2(const glm::vec2 &p) const { return {p.x / m_meter, p.y / m_meter}; }
            inline glm::vec2 b2ToPx(const b2Vec2 &p) const { return {p.x * m_meter, p.y * m_meter}; }

            // Queries.

            struct RaycastHit
            {
                b2ShapeId shape{};
                glm::vec2 point{0.0f}; // pixels
                glm::vec2 normal{0.0f};
                float fraction = 0.0f;
            };

            // Cast a ray from `origin` along `direction` for `maximumDistance`
            // pixels. `direction` does not need to be normalised — the
            // function normalises it internally. Returns every hit along
            // the ray, sorted by fraction (nearest first).
            std::vector<RaycastHit> raycast(const glm::vec2 &origin,
                                            const glm::vec2 &direction,
                                            float maximumDistance,
                                            uint64_t maskBits = ~0ull);

            // Returns the nearest hit, or std::nullopt if the ray hits nothing.
            std::optional<RaycastHit> raycastNearest(const glm::vec2 &origin,
                                                     const glm::vec2 &direction,
                                                     float maximumDistance,
                                                     uint64_t maskBits = ~0ull);

            // Returns every shape whose AABB overlaps the box defined by
            // (lower, upper) in world pixels.
            std::vector<b2ShapeId> overlapAABB(const glm::vec2 &lower,
                                               const glm::vec2 &upper,
                                               uint64_t maskBits = ~0ull);

            // Returns every shape overlapping a circle in world pixels.
            std::vector<b2ShapeId> overlapCircle(const glm::vec2 &center,
                                                 float radius,
                                                 uint64_t maskBits = ~0ull);

            // Cast a circle along a translation vector. Returns the first
            // shape hit, or std::nullopt if nothing was struck. Uses
            // Box2D v3's b2World_CastCircle which is more robust than
            // doing the same with a tiny radius raycast.
            std::optional<RaycastHit> circleCast(const glm::vec2 &center,
                                                 float radius,
                                                 const glm::vec2 &translation,
                                                 uint64_t maskBits = ~0ull);

            // Distance between the two closest points of two shapes.
            // Returns negative on error, or 0 if the shapes overlap.
            // Mirrors love.physics.getDistance.
            float distance(b2ShapeId a, b2ShapeId b) const;

            b2WorldId worldId() const { return m_world; }

        private:
            World() = default;
            ~World() { shutdown(); }
            World(const World &) = delete;
            World &operator=(const World &) = delete;

            b2WorldId m_world = b2_nullWorldId;
            float m_meter = DEFAULT_METER;
            int m_subSteps = DEFAULT_SUB_STEPS;
        };

    }
}
