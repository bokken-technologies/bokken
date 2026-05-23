#include "ShadowCasterBuffer.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

namespace Bokken
{
    namespace Renderer
    {
        namespace Lighting
        {

            bool ShadowCasterBuffer::init()
            {
                // Width 1 holds one segment per texel; the y-coord is
                // the segment index. Nearest filter because the shadow
                // pass uses integer-indexed texelFetch.
                //
                // Format: RGBA16F. RGBA32F triggers Apple's GL
                // "GLD_TEXTURE_INDEX_2D is unloadable" warning because
                // 32-bit float textures aren't texture-filterable per
                // the GL 3.3 spec; the driver flags any sampler
                // reading from them, even though our use of texelFetch
                // bypasses filtering entirely. Each ShadowSegment is
                // 4 floats holding screen-space pixel coordinates —
                // half-float precision (10 mantissa bits, exact up to
                // 2048) is more than enough for any practical viewport.
                if (!m_texture.create(1,
                                      static_cast<int>(MAX_UPLOADED_SEGMENTS),
                                      TextureFormat::RGBA16F,
                                      TextureFilter::Nearest,
                                      TextureWrap::Clamp))
                {
                    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                                 "[Lighting] failed to allocate shadow segment texture");
                    return false;
                }
                m_uploadedCount = 0;
                return true;
            }

            uint32_t ShadowCasterBuffer::upload(const std::vector<ShadowSegment> &segments)
            {
                if (!m_texture.isValid())
                    return 0;

                const uint32_t requested = static_cast<uint32_t>(segments.size());
                const uint32_t toUpload = std::min(requested, MAX_UPLOADED_SEGMENTS);

                if (requested > MAX_UPLOADED_SEGMENTS)
                {
                    static bool warnedOnce = false;
                    if (!warnedOnce)
                    {
                        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                                    "[Lighting] %u shadow segments submitted, only %u "
                                    "uploaded; extras dropped. Reduce caster count, "
                                    "lower polygon detail, or enable tile-binning.",
                                    requested, MAX_UPLOADED_SEGMENTS);
                        warnedOnce = true;
                    }
                }

                m_uploadedCount = toUpload;

                if (toUpload == 0)
                    return 0;

                // Each ShadowSegment is exactly one RGBA32F texel — the
                // upload is a direct memcpy through the GL driver with
                // no transpose or repack.
                m_texture.upload(0, 0, 1, static_cast<int>(toUpload),
                                 segments.data());
                return toUpload;
            }

            void ShadowCasterBuffer::bind(int unit) const
            {
                m_texture.bind(unit);
            }

        }
    }
}