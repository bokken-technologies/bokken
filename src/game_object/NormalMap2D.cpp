#include "NormalMap2D.hpp"
#include "Sprite2D.hpp"
#include "Base.hpp"

namespace Bokken
{
    namespace GameObject
    {

        const Renderer::Texture2D *NormalMap2D::resolve()
        {
            if (m_resolved)
                return m_resolved;
            if (m_resolveAttempted)
                return nullptr;

            // Mark up front so a failed lookup doesn't spam logs across
            // every subsequent frame. invalidate() clears this flag for
            // callers that genuinely want to retry after fixing the
            // underlying issue.
            m_resolveAttempted = true;

            if (!s_textureCache || !s_assets)
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                            "[NormalMap2D] s_textureCache or s_assets unset");
                return nullptr;
            }

            // Authored normal map always wins — even if autoGenerate is
            // also set, the explicit path is honoured. This is the
            // "artist-authored beats engine-generated" contract.
            if (!normalMapPath.empty())
            {
                m_resolved = s_textureCache->load(normalMapPath, s_assets);
                if (!m_resolved)
                {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                                "[NormalMap2D] failed to load authored normal '%s'",
                                normalMapPath.c_str());
                }
                return m_resolved;
            }

            if (!autoGenerate)
                return nullptr;

            // Fall back to Sobel-generation from the sibling Sprite2D's
            // texturePath. The sibling lookup is via the GameObject's
            // type-keyed component map; if no Sprite2D is present we
            // have nothing to derive from.
            if (!gameObject)
                return nullptr;
            Sprite2D *sprite = gameObject->getComponent<Sprite2D>();
            if (!sprite || sprite->texturePath.empty())
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                            "[NormalMap2D] autoGenerate requested but no sibling "
                            "Sprite2D with texturePath on '%s'",
                            gameObject->name.c_str());
                return nullptr;
            }

            m_resolved = s_textureCache->loadOrGenerateNormal(
                sprite->texturePath, s_assets, autoStrength);
            return m_resolved;
        }

    }
}