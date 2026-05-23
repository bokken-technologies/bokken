#include "NormalGenerator.hpp"

#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cmath>

namespace Bokken
{
    namespace Renderer
    {

        namespace
        {
            // Sobel kernels are the standard 3x3 weights. The X kernel
            // picks up horizontal gradient (alpha varying along the
            // row), the Y kernel picks up vertical gradient. The same
            // weights are used here as in any image-processing textbook;
            // they're tuned for orthogonal-direction sensitivity and
            // include a mild diagonal weighting via the 2-weight on
            // the cardinal neighbours.
            //
            //   Gx =  -1 0 +1     Gy =  -1 -2 -1
            //         -2 0 +2            0  0  0
            //         -1 0 +1           +1 +2 +1
            //
            // The kernels are applied to the alpha channel of the source
            // image. The result is a per-pixel (gx, gy) gradient vector;
            // dividing by 8 (the kernel weight sum / 2) maps the range
            // back to [-1, +1] for a fully-saturated alpha edge.
            inline float sampleAlpha(const uint8_t *src, int w, int h, int x, int y)
            {
                if (x < 0) x = 0; else if (x >= w) x = w - 1;
                if (y < 0) y = 0; else if (y >= h) y = h - 1;
                return src[(y * w + x) * 4 + 3] / 255.0f;
            }
        }

        bool NormalGenerator::generateFromRGBA8(const uint8_t *inPixels,
                                                int w, int h,
                                                std::vector<uint8_t> &outPixels,
                                                float strength)
        {
            if (!inPixels || w <= 0 || h <= 0)
                return false;

            outPixels.assign(static_cast<size_t>(w) * h * 4, 0);

            for (int y = 0; y < h; ++y)
            {
                for (int x = 0; x < w; ++x)
                {
                    // Sobel 3x3 on alpha. Edge samples are clamped via
                    // sampleAlpha so the result is well-defined at the
                    // image border — no need to special-case the first
                    // and last row / column.
                    const float a00 = sampleAlpha(inPixels, w, h, x - 1, y - 1);
                    const float a10 = sampleAlpha(inPixels, w, h, x,     y - 1);
                    const float a20 = sampleAlpha(inPixels, w, h, x + 1, y - 1);
                    const float a01 = sampleAlpha(inPixels, w, h, x - 1, y);
                    const float a21 = sampleAlpha(inPixels, w, h, x + 1, y);
                    const float a02 = sampleAlpha(inPixels, w, h, x - 1, y + 1);
                    const float a12 = sampleAlpha(inPixels, w, h, x,     y + 1);
                    const float a22 = sampleAlpha(inPixels, w, h, x + 1, y + 1);

                    const float gx = (-a00 - 2.0f * a01 - a02
                                      + a20 + 2.0f * a21 + a22) * 0.125f;
                    const float gy = (-a00 - 2.0f * a10 - a20
                                      + a02 + 2.0f * a12 + a22) * 0.125f;

                    // Treat alpha as a height field: the sprite "rises
                    // out" of the transparent background, so the gradient
                    // of alpha points from background (low) toward
                    // sprite center (high). The surface normal of a
                    // height field is the negated gradient — at the left
                    // edge of a sprite (gx > 0, height rising rightward)
                    // the normal points leftward, away from the bulk.
                    // Without the negation lighting would bake reversed:
                    // a light source on the right would brighten the
                    // sprite's left edge.
                    float nx = -gx * strength;
                    float ny = -gy * strength;

                    // Tangent-space normal: Z is reconstructed as
                    // sqrt(max(0, 1 - nx^2 - ny^2)). When the gradient
                    // saturates above unit length we clamp to a fully
                    // sideways normal (z=0) instead of producing NaN.
                    const float xy2 = nx * nx + ny * ny;
                    float nz;
                    if (xy2 >= 1.0f)
                    {
                        const float inv = 1.0f / std::sqrt(xy2);
                        nx *= inv;
                        ny *= inv;
                        nz = 0.0f;
                    }
                    else
                    {
                        nz = std::sqrt(1.0f - xy2);
                    }

                    // Encode (nx, ny, nz) in [-1, +1] to RGB in [0, 1].
                    // The lighting shader decodes (rgb * 2 - 1) before
                    // use. Alpha is reserved for a future authored-mask
                    // flag and stays 255 for generated normals.
                    const int idx = (y * w + x) * 4;
                    outPixels[idx + 0] = static_cast<uint8_t>(
                        std::clamp((nx * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
                    outPixels[idx + 1] = static_cast<uint8_t>(
                        std::clamp((ny * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
                    outPixels[idx + 2] = static_cast<uint8_t>(
                        std::clamp((nz * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
                    outPixels[idx + 3] = 255;
                }
            }

            return true;
        }

        bool NormalGenerator::generateFromAssetAlpha(const std::string &albedoVirtualPath,
                                                     AssetPack *assets,
                                                     std::vector<uint8_t> &outPixels,
                                                     int &outW,
                                                     int &outH,
                                                     float strength)
        {
            if (!assets)
                return false;
            if (!assets->exists(albedoVirtualPath))
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "[NormalGenerator] source not found: %s",
                             albedoVirtualPath.c_str());
                return false;
            }

            // Re-decode the source image rather than reading back from
            // GL — PNG decode is microseconds and keeps the generator
            // free of GL state assumptions. This means we pay the
            // decode cost twice on first use of a sprite, once during
            // the original TextureCache::load and once here, but the
            // result is cached by the caller so subsequent frames pay
            // nothing.
            SDL_IOStream *io = assets->openIOStream(albedoVirtualPath);
            if (!io)
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "[NormalGenerator] openIOStream failed: %s",
                             albedoVirtualPath.c_str());
                return false;
            }

            SDL_Surface *raw = IMG_Load_IO(io, true);
            if (!raw)
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "[NormalGenerator] IMG_Load_IO failed for '%s': %s",
                             albedoVirtualPath.c_str(), SDL_GetError());
                return false;
            }

            SDL_Surface *rgba = (raw->format == SDL_PIXELFORMAT_RGBA32)
                                    ? raw
                                    : SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
            if (!rgba)
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "[NormalGenerator] surface conversion failed for '%s': %s",
                             albedoVirtualPath.c_str(), SDL_GetError());
                SDL_DestroySurface(raw);
                return false;
            }

            outW = rgba->w;
            outH = rgba->h;
            const bool ok = generateFromRGBA8(
                static_cast<const uint8_t *>(rgba->pixels),
                rgba->w, rgba->h, outPixels, strength);

            if (rgba != raw)
                SDL_DestroySurface(rgba);
            SDL_DestroySurface(raw);

            if (!ok)
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "[NormalGenerator] generation failed for '%s'",
                             albedoVirtualPath.c_str());
                return false;
            }
            return true;
        }

    }
}