#include "SpriteStage.hpp"
#include "../Pipeline.hpp"
#include "../RenderTarget.hpp"

#include <SDL3/SDL.h>

namespace Bokken
{
    namespace Renderer
    {

        bool SpriteStage::setup()
        {
            return true;
        }

        void SpriteStage::execute(const FrameContext &ctx)
        {
            SDL_LogDebug(SDL_LOG_CATEGORY_RENDER,
                         "[SpriteStage::execute] viewport=%dx%d "
                         "output=%p batcher=%p lightingEnabled=%d",
                         ctx.viewportWidth, ctx.viewportHeight,
                         (const void*)ctx.outputTarget, (const void*)ctx.batcher,
                         (int)lightingEnabled);

            if (!ctx.outputTarget || !ctx.batcher)
            {
                SDL_LogDebug(SDL_LOG_CATEGORY_RENDER,
                             "[SpriteStage::execute] missing output/batcher, skipping");
                return;
            }

            ctx.outputTarget->bind();
            glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);

            if (lightingEnabled && ctx.pipeline)
            {
                if (!m_normalsAux)
                {
                    m_normalsAux = ctx.pipeline->requestAuxTarget(
                        "normals", TextureFormat::RG16F);
                    SDL_LogDebug(SDL_LOG_CATEGORY_RENDER,
                                 "[SpriteStage::execute] requested aux 'normals' -> %p",
                                 (const void*)m_normalsAux);
                }
                if (!m_emissiveAux)
                {
                    m_emissiveAux = ctx.pipeline->requestAuxTarget(
                        "emissive", TextureFormat::RGBA8);
                    SDL_LogDebug(SDL_LOG_CATEGORY_RENDER,
                                 "[SpriteStage::execute] requested aux 'emissive' -> %p",
                                 (const void*)m_emissiveAux);
                }
            }

            const bool mrt = lightingEnabled && m_normalsAux && m_emissiveAux;
            SDL_LogDebug(SDL_LOG_CATEGORY_RENDER,
                         "[SpriteStage::execute] mrt=%d", (int)mrt);

            if (mrt)
            {
                ctx.outputTarget->attachAuxColor(0, m_normalsAux->color());
                ctx.outputTarget->attachAuxColor(1, m_emissiveAux->color());
                ctx.outputTarget->setDrawBuffers(3);

                const float albedoClear[4]   = {clearR, clearG, clearB, clearA};
                const float normalClear[4]   = {0.5f, 0.5f, 0.0f, 1.0f};
                const float emissiveClear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                glClearBufferfv(GL_COLOR, 0, albedoClear);
                glClearBufferfv(GL_COLOR, 1, normalClear);
                glClearBufferfv(GL_COLOR, 2, emissiveClear);
            }
            else
            {
                ctx.outputTarget->setDrawBuffers(1);
                glClearColor(clearR, clearG, clearB, clearA);
                glClear(GL_COLOR_BUFFER_BIT);
            }

            ctx.batcher->flush();

            if (mrt)
            {
                ctx.outputTarget->detachAuxColor(0);
                ctx.outputTarget->detachAuxColor(1);
                ctx.outputTarget->setDrawBuffers(1);
            }
        }

    }
}