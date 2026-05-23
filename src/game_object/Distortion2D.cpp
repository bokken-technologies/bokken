#include "Distortion2D.hpp"

namespace Bokken
{
    namespace GameObject
    {

        void Distortion2D::onAttach()
        {
            if (autoStart)
                trigger();
        }

        void Distortion2D::trigger()
        {
            if (!s_pipeline || !gameObject)
                return;

            // Find the DistortionStage in the pipeline. This is a linear
            // walk over a handful of stages — not a hot path since trigger()
            // is called on events (explosions, impacts), not every frame.
            Renderer::DistortionStage *ds = nullptr;
            for (auto &stage : s_pipeline->stages())
            {
                ds = dynamic_cast<Renderer::DistortionStage *>(stage.get());
                if (ds)
                    break;
            }

            if (!ds)
                return;

            auto *t = gameObject->getComponent<Transform2D>();
            if (!t)
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                            "[Distortion2D] no Transform2D on '%s'",
                            gameObject->name.c_str());
                return;
            }

            // Find the active camera for the world → screen conversion.
            float cameraX = 0.0f;
            float cameraY = 0.0f;
            float pixelsPerUnit = 64.0f;

            for (auto &go : Base::s_objects)
            {
                auto *cam = go->getComponent<Camera2D>();
                if (!cam || !cam->isActive)
                    continue;

                auto *ct = go->getComponent<Transform2D>();
                if (ct)
                {
                    cameraX = ct->position.x;
                    cameraY = ct->position.y;
                }
                pixelsPerUnit = cam->zoom;
                break;
            }

            // Compute world position by walking the parent chain, same
            // transform accumulation as the present() render pass.
            float worldX = t->position.x;
            float worldY = t->position.y;

            auto *parent = gameObject->getParent();
            while (parent)
            {
                auto *pt = parent->getComponent<Transform2D>();
                if (!pt)
                    break;

                float pRad = pt->rotation * (3.14159265f / 180.0f);
                float cosR = std::cos(pRad);
                float sinR = std::sin(pRad);

                float lx = worldX * pt->scale.x;
                float ly = worldY * pt->scale.y;
                float rx = lx * cosR - ly * sinR;
                float ry = lx * sinR + ly * cosR;

                worldX = pt->position.x + rx;
                worldY = pt->position.y + ry;

                parent = parent->getParent();
            }

            // Get the render-target size for the world → normalised
            // conversion. Uses the renderer's render size — the same
            // dimensions present() draws sprites against and the same
            // dimensions the DistortionStage's fullscreen pass writes
            // into — so a shockwave triggered at a sprite's world
            // position lands centred on that sprite regardless of the
            // current window size or render-size policy.
            int renderW = 1280, renderH = 720;
            if (s_renderer)
            {
                renderW = s_renderer->renderWidth();
                renderH = s_renderer->renderHeight();
            }

            if (renderW <= 0 || renderH <= 0)
                return;

            float hw = renderW * 0.5f;
            float hh = renderH * 0.5f;

            // World → screen pixels.
            float screenPxX = hw + (worldX - cameraX) * pixelsPerUnit;
            float screenPxY = hh - (worldY - cameraY) * pixelsPerUnit;

            // Screen pixels → normalised [0, 1].
            // The fullscreen triangle's UV origin is bottom-left in GL
            // (v_uv.y=0 at the bottom), but screen pixel Y grows downward
            // (screenPxY=0 at the top). Flip Y so they match.
            float nx = screenPxX / static_cast<float>(renderW);
            float ny = 1.0f - (screenPxY / static_cast<float>(renderH));

            ds->addShockwave(nx, ny, speed, thickness, amplitude, maximumRadius);
        }

    }
}