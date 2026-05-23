#pragma once

#include "Component.hpp"
#include <glm/vec2.hpp>

namespace Bokken
{
    namespace GameObject
    {
        // Camera for 2D rendering. Attach alongside a Transform2D to
        // control the viewport. The first Camera2D found with
        // isActive == true is used by the renderer each frame.
        //
        // zoom is pixels-per-world-unit (higher = closer). Default 64
        // means 1 world unit = 64 screen pixels.
        class Camera2D : public Component
        {
        public:
            float zoom     = 64.0f;
            bool  isActive = false;

            // Convert a screen-space point (logical window pixels, as
            // produced by SDL mouse events) into world-space units.
            // Inverse of worldToScreenPoint.
            //
            // Works on inactive cameras too — the conversion is
            // computed "as if" this camera were rendering. Useful
            // for preview / split-screen cameras (Unity has the
            // same behaviour: the camera you call it on is the one
            // whose transform / zoom is used).
            glm::vec2 screenToWorldPoint(float x, float y) const;

            // Convert a world-space point to screen-space (logical
            // window pixels). Suitable for placing native OS
            // overlays — cursor, tooltip — at a scene point.
            glm::vec2 worldToScreenPoint(float x, float y) const;
        };
    }
}