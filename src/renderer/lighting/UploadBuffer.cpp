#include "UploadBuffer.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <vector>

namespace Bokken
{
    namespace Renderer
    {
        namespace Lighting
        {

            bool UploadBuffer::init()
            {
                // Width 5 = one column per row of the Light struct.
                // Height = light cap. Format = RGBA32F.
                //
                // RGBA32F is required because the Light struct mixes
                // float fields (position / intensity / range /
                // cone-cosine) with integer-coded fields (flags,
                // shadowSlot, cookieSlot) stored as float-equivalent
                // values. A half-float format (RGBA16F) has only a
                // 10-bit mantissa: its float-to-half narrowing pass
                // mangles the encoded bits of the integer-coded fields
                // and produces near-random values for them, which
                // corrupts lighting entirely.
                //
                // RGBA32F round-trips every float we write losslessly
                // and supports `floatBitsToUint` in the shader if we
                // ever want bit-pattern integer storage. The texture is
                // nearest-sampled and never filtered, so the "32F is not
                // texture-filterable in GL 3.3 core" rule doesn't bite —
                // texelFetch returns the exact stored value with no
                // filtering pass.
                if (!m_texture.create(5,
                                      static_cast<int>(MAX_UPLOADED_LIGHTS),
                                      TextureFormat::RGBA32F,
                                      TextureFilter::Nearest,
                                      TextureWrap::Clamp))
                {
                    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                                 "[Lighting] failed to allocate light data texture");
                    return false;
                }
                m_uploadedCount = 0;
                return true;
            }

            uint32_t UploadBuffer::upload(const std::vector<Light> &lights)
            {
                if (!m_texture.isValid())
                    return 0;

                const uint32_t requested = static_cast<uint32_t>(lights.size());
                const uint32_t toUpload = std::min(requested, MAX_UPLOADED_LIGHTS);

                if (requested > MAX_UPLOADED_LIGHTS)
                {
                    static bool warnedOnce = false;
                    if (!warnedOnce)
                    {
                        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                                    "[Lighting] %u lights submitted, only %u uploaded; "
                                    "extras dropped. Enable tiled culling or reduce "
                                    "scene light count.",
                                    requested, MAX_UPLOADED_LIGHTS);
                        warnedOnce = true;
                    }
                }

                m_uploadedCount = toUpload;

                if (toUpload == 0)
                    return 0;

                // Pack each Light into 5 vec4 rows of float values.
                // Integer-coded fields (flags, slots) are stored as
                // their float-equivalent values (e.g. flags=4 stored
                // as 4.0f); the shader recovers them with
                // `int(value + 0.5)`. RGBA32F preserves these exactly,
                // so we don't need bit-pattern reinterpretation.
                //
                // No-slot sentinels (LIGHT_NO_SLOT = 0xFFFFFFFF) are
                // translated to -1.0 here; the shader checks
                // `slot < 0.0` rather than equality with a sentinel.
                std::vector<float> packed(
                    static_cast<size_t>(toUpload) * 5 * 4, 0.0f);
                for (uint32_t i = 0; i < toUpload; ++i)
                {
                    const Light &L = lights[i];
                    float *row = &packed[static_cast<size_t>(i) * 5 * 4];

                    // Row 0: position.xy, direction.xy
                    row[0] = L.position.x;
                    row[1] = L.position.y;
                    row[2] = L.direction.x;
                    row[3] = L.direction.y;

                    // Row 1: color.rgb, intensity
                    row[4] = L.color.r;
                    row[5] = L.color.g;
                    row[6] = L.color.b;
                    row[7] = L.intensity;

                    // Row 2: range, falloffExp, innerConeCos, outerConeCos
                    row[8]  = L.range;
                    row[9]  = L.falloffExponent;
                    row[10] = L.innerConeCos;
                    row[11] = L.outerConeCos;

                    // Row 3: flags (as float), shadowSlot, cookieSlot, softness
                    row[12] = static_cast<float>(L.flags);
                    row[13] = (L.shadowSlot == LIGHT_NO_SLOT)
                              ? -1.0f
                              : static_cast<float>(L.shadowSlot);
                    row[14] = (L.cookieSlot == LIGHT_NO_SLOT)
                              ? -1.0f
                              : static_cast<float>(L.cookieSlot);
                    row[15] = L.softness;

                    // Row 4: cookieUVOffset.xy, cookieUVScale.xy
                    row[16] = L.cookieUVOffset.x;
                    row[17] = L.cookieUVOffset.y;
                    row[18] = L.cookieUVScale.x;
                    row[19] = L.cookieUVScale.y;
                }

                m_texture.upload(0, 0, 5, static_cast<int>(toUpload),
                                 packed.data());
                return toUpload;
            }

            void UploadBuffer::bind(int unit) const
            {
                m_texture.bind(unit);
            }

        }
    }
}