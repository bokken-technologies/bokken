#pragma once

#include "../AssetPack.hpp"
#include "Texture2D.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Bokken
{
    namespace Renderer
    {

        /**
         * Generates fake tangent-space normal maps from an albedo image's
         * alpha channel, using a Sobel filter to derive surface slope.
         *
         * Technique
         *
         * The Sobel operator estimates the gradient of the alpha channel
         * at every pixel: a steep alpha transition (sprite silhouette,
         * cut-out outline) produces a strong lateral gradient, while
         * the interior of the sprite (constant alpha) produces zero
         * gradient. The gradient vector becomes the X/Y of the output
         * normal; Z is derived from the gradient magnitude so steep
         * edges tilt outward while flat interiors stay up.
         *
         * Output encoding matches the SpriteBatcher's normal-map
         * expectation: RGBA8 where R = (Nx*0.5+0.5) and G = (Ny*0.5+0.5),
         * with B/A both 255 (B unused by the lighting shader, A reserved
         * for the future "this pixel has authored normal data" mask).
         *
         * Quality
         *
         * The result is mediocre on sprites that should have interior
         * detail (a face on a portrait gets no nose-shadow) but
         * excellent on the silhouette / rim, which is where atmospheric
         * 2D lighting wants relief the most. Hand-authored normal maps
         * always produce better-looking lighting; this is the zero-
         * artist-work fallback for prototyping and for bulk-converted
         * existing sprites.
         *
         * Performance
         *
         * Pure CPU. ~5 µs per pixel on modern x86; a 256×256 sprite is
         * ~300 ms one-shot at load time. Results are cached by the
         * caller (TextureCache) under a synthesised key so the cost is
         * paid once per unique sprite path per session.
        */
        class NormalGenerator
        {
        public:
            /**
             * Build a tangent-space normal map from the alpha channel of
             * an image in the asset pack. Returns the raw RGBA8 pixel
             * data plus the dimensions; the caller is expected to feed
             * this into a Texture2D::uploadFull.
             *
             * The strength parameter scales the Sobel gradient before
             * Z reconstruction — higher values produce more pronounced
             * relief at the cost of saturation at the silhouette edge.
             * The default (3.0) is tuned for "obvious but not exaggerated"
             * relief on typical 32-128 px characters and props.
             *
             * Returns true on success; outPixels is replaced with the
             * RGBA8 bytes (width * height * 4 bytes), outW / outH carry
             * the dimensions. Returns false if the source image cannot
             * be decoded or has zero pixels.
            */
            static bool generateFromAssetAlpha(const std::string &albedoVirtualPath,
                                               AssetPack *assets,
                                               std::vector<uint8_t> &outPixels,
                                               int &outW,
                                               int &outH,
                                               float strength = 3.0f);

            /**
             * Build a normal map from already-decoded RGBA8 pixel data.
             * Useful when the caller already has the source surface and
             * wants to avoid re-loading from the asset pack — for
             * example, a procedural sprite that was never on disk.
             *
             * `inPixels` must be exactly w*h*4 bytes of RGBA8.
            */
            static bool generateFromRGBA8(const uint8_t *inPixels,
                                          int w, int h,
                                          std::vector<uint8_t> &outPixels,
                                          float strength = 3.0f);
        };

    }
}