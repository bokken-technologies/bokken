#include "Camera2D.hpp"
#include "Transform2D.hpp"
#include "Base.hpp"
#include "../scripting/modules/Renderer.hpp"
#include "../renderer/Base.hpp"

namespace Bokken
{
    namespace GameObject
    {
        glm::vec2 Camera2D::screenToWorldPoint(float x, float y) const
        {
            auto *renderer = Bokken::Scripting::Modules::Renderer::renderer();
            if (!renderer)
                return {x, y};

            // Screen (logical px) → render (framebuffer px).
            // Mouse coords arrive in logical pixels; the renderer's
            // window↔render math is in physical pixels, so scale up
            // by dpiScale on the way in to invert the composite blit
            // correctly on HighDPI displays.
            const float dpi = renderer->dpiScale();
            float rx = 0.0f, ry = 0.0f;
            renderer->windowToRender(x * dpi, y * dpi, rx, ry);

            // Render → world. Inverse of the composition applied in
            // GameObject::present():
            //   screenCX = hw + (wt.x - cameraX) * zoom
            //   screenCY = hh - (wt.y - cameraY) * zoom
            const float hw = renderer->renderWidth() * 0.5f;
            const float hh = renderer->renderHeight() * 0.5f;

            float camX = 0.0f, camY = 0.0f;
            if (auto *t = gameObject->getComponent<Transform2D>())
            {
                camX = t->position.x;
                camY = t->position.y;
            }

            // Y is flipped — present() builds screenCY with `hh - ...`
            // so the inverse subtracts and negates.
            return {
                (rx - hw) / zoom + camX,
                -(ry - hh) / zoom + camY,
            };
        }

        glm::vec2 Camera2D::worldToScreenPoint(float x, float y) const
        {
            auto *renderer = Bokken::Scripting::Modules::Renderer::renderer();
            if (!renderer)
                return {x, y};

            const float hw = renderer->renderWidth() * 0.5f;
            const float hh = renderer->renderHeight() * 0.5f;

            float camX = 0.0f, camY = 0.0f;
            if (auto *t = gameObject->getComponent<Transform2D>())
            {
                camX = t->position.x;
                camY = t->position.y;
            }

            // World → render. Same composition as present().
            float rx = hw + (x - camX) * zoom;
            float ry = hh - (y - camY) * zoom;

            // Render → screen (physical px) → screen (logical px).
            float pwx = 0.0f, pwy = 0.0f;
            renderer->renderToWindow(rx, ry, pwx, pwy);

            const float dpi = renderer->dpiScale();
            return {
                (dpi > 0.0f) ? pwx / dpi : pwx,
                (dpi > 0.0f) ? pwy / dpi : pwy,
            };
        }
    }
}