#pragma once

#include "Light.hpp"
#include "../Texture2D.hpp"

#include <cstdint>
#include <vector>

namespace Bokken
{
    namespace Renderer
    {
        namespace Lighting
        {

            /**
             * Per-tile light index table for tiled forward+ lighting.
             *
             * Why this exists
             *
             * Without tiling, every pixel iterates every uploaded light
             * (up to MAX_UPLOADED_LIGHTS = 256). At 1920×1080 that's
             * ~530M evaluations per frame even for lights that
             * contribute nothing to most of the screen. Tiled
             * forward+ splits the screen into a grid of tiles, the
             * CPU pre-computes which lights touch which tiles, and
             * the fragment shader iterates only its tile's lights.
             * For typical scenes this drops the per-pixel iteration
             * count from 256 to ~4–12.
             *
             * GPU storage layout
             *
             * Each tile's data occupies BYTES_PER_TILE consecutive
             * bytes (one count byte + MAX_PER_TILE index bytes). The
             * previous implementation laid tiles out one per texture
             * row, so for a 4K display with 16-px tiles the texture
             * was 32400 rows tall — past the macOS GL_MAX_TEXTURE_SIZE
             * cap of 16384, silently failing allocation.
             *
             * The current layout packs TILES_PER_TEXTURE_ROW tiles into
             * each texture row. The texture width is therefore
             * TILES_PER_TEXTURE_ROW × BYTES_PER_TILE and the height is
             * ceil(maxTiles / TILES_PER_TEXTURE_ROW). Worst case:
             *   32 × 33 = 1056 wide
             *   ceil(32400 / 32) = 1013 tall
             * Comfortably within every conforming driver's limits.
             *
             * The shader recovers the per-tile texel coords as
             *   col = (tileIdx % TILES_PER_TEXTURE_ROW) * BYTES_PER_TILE + byteOffset
             *   row = tileIdx / TILES_PER_TEXTURE_ROW
             *
             * Binning algorithm
             *
             * Per (light, tile) test: 2D point-vs-AABB closest-
             * distance compare against the light's bounding circle.
             *
             *   - Point lights / spotlights use range as the bounding
             *     circle radius. Spotlights are over-conservative
             *     here (a narrow cone of radius R doesn't touch every
             *     tile inside R) but circle culling is much cheaper
             *     than cone culling and the over-coverage produces
             *     correct lighting, just slightly more per-tile work
             *     for narrow spots. Refinement deferred.
             *   - Directional lights are added to EVERY tile —
             *     they're infinitely large by definition. Typically
             *     only the sun, so the per-tile overhead is one
             *     extra entry across the whole frame.
             *
             * Threading: single-threaded, same as the rest of the
             * lighting infrastructure.
            */
            class TileLightGrid
            {
            public:
                static constexpr uint32_t TILE_SIZE = 16;
                static constexpr uint32_t MAX_PER_TILE = 32;

                // Bytes per tile in the GPU texture: one count byte
                // plus MAX_PER_TILE index bytes.
                static constexpr uint32_t BYTES_PER_TILE = 1 + MAX_PER_TILE;

                // Tiles packed per texture row. Chosen so the texture
                // width (TILES_PER_TEXTURE_ROW * BYTES_PER_TILE = 1056)
                // and worst-case height (ceil(32400 / 32) = 1013) both
                // fit comfortably under the GL_MAX_TEXTURE_SIZE = 16384
                // minimum guaranteed by macOS GL 3.3 Core. Must be a
                // power of two so the shader's div/mod compile to
                // shifts and masks.
                static constexpr uint32_t TILES_PER_TEXTURE_ROW = 32;

                TileLightGrid() = default;
                ~TileLightGrid() = default;

                TileLightGrid(const TileLightGrid &) = delete;
                TileLightGrid &operator=(const TileLightGrid &) = delete;
                TileLightGrid(TileLightGrid &&) noexcept = default;
                TileLightGrid &operator=(TileLightGrid &&) noexcept = default;

                /**
                 * Allocate at the maximum supported tile count (sized
                 * for 4K with TILE_SIZE = 16 = 240 × 135 = 32400 tiles).
                 * The actual binning region per frame is the visible
                 * tileCountX × tileCountY rectangle; unused tiles
                 * past that point are not touched / sampled.
                 *
                 * Allocating for max-size up front avoids the cost of
                 * recreating the texture on resize() and keeps the
                 * binning code free of "did I grow this frame?"
                 * branches.
                */
                bool init();

                /**
                 * Re-bin the world based on the provided lights and
                 * viewport. Populates the CPU-side buffer and uploads
                 * to the GPU.
                 *
                 * @param viewportW  Visible pixel width.
                 * @param viewportH  Visible pixel height.
                 * @param lights     Flat vector of every light snapshotted
                 *                   this frame. Must match the order/index
                 *                   layout the lighting shader expects from
                 *                   the light data texture (i.e. the same
                 *                   vector that's about to be uploaded to
                 *                   LightUploadBuffer).
                */
                void bin(int viewportW, int viewportH,
                         const std::vector<Light> &lights);

                /** Bind the GPU texture at the given sampler unit. */
                void bind(int unit) const;

                /** Tiles spanned by the most recent bin() call. */
                int tileCountX() const { return m_tileCountX; }
                int tileCountY() const { return m_tileCountY; }

                /** Underlying texture, exposed for shaders that want
                 *  to use textureSize() instead of receiving the tile
                 *  counts via separate uniforms. */
                const Texture2D &texture() const { return m_texture; }

            private:
                Texture2D m_texture;

                // CPU-side scratch holding the same bytes that get
                // uploaded to the texture each frame. Stored in the
                // SAME packed layout as the GPU texture so upload is
                // a single glTexSubImage2D with no rearrangement.
                std::vector<uint8_t> m_scratch;

                int m_tileCountX = 0;
                int m_tileCountY = 0;

                // Texture dimensions, cached for the upload call.
                int m_textureWidth = 0;
                int m_textureHeight = 0;

                // Once-only overflow warning, same pattern as
                // LightUploadBuffer.
                bool m_warnedOverflow = false;
            };

        }
    }
}