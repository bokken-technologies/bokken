#pragma once

#include "Component.hpp"
#include "Base.hpp"
#include "Transform2D.hpp"
#include "Camera2D.hpp"
#include "../renderer/Pipeline.hpp"
#include "../renderer/Base.hpp"
#include "../renderer/stages/DistortionStage.hpp"

#include <SDL3/SDL.h>

#include <cmath>

namespace Bokken
{
    namespace GameObject
    {

        /**
         * Attaches screen-space distortion effects to a game object.
         *
         * Reads the world position from the sibling Transform2D, converts
         * it to normalised screen coordinates using the active camera, and
         * pushes effects into the DistortionStage in the render pipeline.
         *
         * The DistortionStage is found lazily by walking the pipeline at
         * trigger time — no early wiring needed. If no distortion stage
         * exists in the pipeline the component silently does nothing.
         *
         * Supported effects:
         *
         *   Shockwave — an expanding ring distortion. Call trigger() or
         *   set autoStart to fire one immediately on attach. The wave
         *   expands until it exceeds maximumRadius, then it's removed
         *   automatically. Call trigger() again for another wave.
         *
         * Requires a Transform2D on the same GameObject.
         *
         * Usage from JS:
         *   const boom = new GameObject("Boom");
         *   boom.addComponent(Transform2D, { positionX: 3, positionY: 1 });
         *   boom.addComponent(Distortion2D, { autoStart: true, amplitude: 0.08 });
         *
         *   // Or trigger manually:
         *   const fx = boom.getComponent(Distortion2D);
         *   fx.trigger();
        */
        class Distortion2D : public Component
        {
        public:
            // Set once during engine init. The renderer pointer is
            // used both to locate the DistortionStage (via its
            // pipeline) and to read render-space dimensions for the
            // world → normalised conversion at trigger time, so
            // shockwaves originate at the right place under any
            // render-size policy.
            static inline Renderer::Pipeline *s_pipeline = nullptr;
            static inline Renderer::Base *s_renderer = nullptr;

            // Shockwave parameters.
            float speed = 0.8f;
            float thickness = 0.1f;
            float amplitude = 0.06f;
            float maximumRadius = 1.5f;

            // If true, a shockwave is triggered automatically when the
            // component is first attached. Convenient for one-shot effects
            // like explosions where the GameObject is created at the impact
            // point and destroyed shortly after.
            bool autoStart = false;

            /**
             * Fire a shockwave from the current world position.
             *
             * Reads the sibling Transform2D's position, converts to screen
             * space via the active Camera2D, and pushes a Shockwave into
             * the DistortionStage. Safe to call multiple times — each call
             * creates an independent wave.
            */
            void trigger();

            void onAttach() override;

            // Always idle — the shockwave is pushed into the DistortionStage
            // on trigger() and lives there independently. The component
            // itself has no ongoing state after firing.
            bool isIdle() const override { return true; }
        };

    }
}