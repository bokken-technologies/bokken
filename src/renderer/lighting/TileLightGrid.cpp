#include "TileLightGrid.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

namespace Bokken
{
    namespace Renderer
    {
        namespace Lighting
        {

            namespace
            {
                // 4K width  / 16 = 240 tiles.
                // 4K height / 16 = 135 tiles.
                // Round up generously so we never reallocate during a
                // window resize.
                constexpr int kMaxTileCountX = 256;
                constexpr int kMaxTileCountY = 144;
                constexpr int kMaxTiles = kMaxTileCountX * kMaxTileCountY;

                // Circle-vs-AABB overlap test in 2D. Returns true if
                // the circle at (cx, cy) with radius r overlaps the
                // axis-aligned rectangle [(x0, y0), (x1, y1)].
                bool circleOverlapsRect(float cx, float cy, float r,
                                        float x0, float y0,
                                        float x1, float y1)
                {
                    const float clampedX = std::max(x0, std::min(cx, x1));
                    const float clampedY = std::max(y0, std::min(cy, y1));
                    const float dx = cx - clampedX;
                    const float dy = cy - clampedY;
                    return (dx * dx + dy * dy) <= (r * r);
                }
            }

            bool TileLightGrid::init()
            {
                // Texture is laid out as a packed 2D atlas of tiles.
                // Each tile occupies BYTES_PER_TILE contiguous bytes
                // along X; TILES_PER_TEXTURE_ROW tiles fit in each
                // row before wrapping to the next row.
                //
                // For the maximum tile count (kMaxTiles = 36864) and
                // TILES_PER_TEXTURE_ROW = 32, the texture is:
                //   width  = 32 * 33 = 1056
                //   height = ceil(36864 / 32) = 1152
                // Both well under any conforming driver's
                // GL_MAX_TEXTURE_SIZE (16384 on macOS 3.3 Core).
                const int tilesPerRow =
                    static_cast<int>(TileLightGrid::TILES_PER_TEXTURE_ROW);
                m_textureWidth =
                    tilesPerRow * static_cast<int>(BYTES_PER_TILE);
                m_textureHeight =
                    (kMaxTiles + tilesPerRow - 1) / tilesPerRow;

                m_scratch.assign(
                    static_cast<size_t>(m_textureWidth) *
                    static_cast<size_t>(m_textureHeight), 0);

                // R8 normalised storage. The shader recovers the
                // original byte values with `int(value * 255 + 0.5)`.
                // We use a normalized float format instead of R8UI
                // to keep the lighting shader's samplers homogeneous:
                // Apple's GL driver mis-validates sampler unit
                // assignments when a single program contains both
                // `usampler2D` and `sampler2D`.
                if (!m_texture.create(m_textureWidth,
                                      m_textureHeight,
                                      TextureFormat::R8,
                                      TextureFilter::Nearest,
                                      TextureWrap::Clamp))
                {
                    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                                 "[Lighting] tile light grid texture "
                                 "allocation failed (%dx%d R8)",
                                 m_textureWidth, m_textureHeight);
                    return false;
                }
                return true;
            }

            void TileLightGrid::bin(int viewportW, int viewportH,
                                    const std::vector<Light> &lights)
            {
                if (!m_texture.isValid())
                    return;
                if (viewportW <= 0 || viewportH <= 0)
                    return;

                m_tileCountX = (viewportW + static_cast<int>(TILE_SIZE) - 1)
                               / static_cast<int>(TILE_SIZE);
                m_tileCountY = (viewportH + static_cast<int>(TILE_SIZE) - 1)
                               / static_cast<int>(TILE_SIZE);

                // Clamp defensively in case viewport math produces
                // something past the allocated max — shouldn't happen
                // unless someone enlarges the kMax constants without
                // re-init'ing.
                m_tileCountX = std::min(m_tileCountX, kMaxTileCountX);
                m_tileCountY = std::min(m_tileCountY, kMaxTileCountY);

                const int tilesPerRow =
                    static_cast<int>(TILES_PER_TEXTURE_ROW);
                const int bytesPerTile = static_cast<int>(BYTES_PER_TILE);

                // Helper: byte offset in m_scratch for (tileIdx, byteOffset).
                // Layout matches the shader's recovery formula exactly.
                const auto byteAt = [&](int tileIdx, int byteOffset) -> size_t {
                    const int row = tileIdx / tilesPerRow;
                    const int col = (tileIdx % tilesPerRow) * bytesPerTile
                                  + byteOffset;
                    return static_cast<size_t>(row) *
                           static_cast<size_t>(m_textureWidth) +
                           static_cast<size_t>(col);
                };

                // Clear the visible region. Walk the visible tile
                // rectangle and zero each tile's slot. Tiles outside
                // the visible rect are not sampled by the shader
                // (its tile-coord clamp prevents that), so their
                // stale contents don't matter.
                for (int ty = 0; ty < m_tileCountY; ++ty)
                {
                    for (int tx = 0; tx < m_tileCountX; ++tx)
                    {
                        const int tileIdx = ty * m_tileCountX + tx;
                        for (int b = 0; b < bytesPerTile; ++b)
                            m_scratch[byteAt(tileIdx, b)] = 0;
                    }
                }

                const auto appendToTile = [&](int tx, int ty, uint8_t lightIdx) {
                    const int tileIdx = ty * m_tileCountX + tx;
                    uint8_t &count = m_scratch[byteAt(tileIdx, 0)];
                    if (count >= MAX_PER_TILE)
                    {
                        if (!m_warnedOverflow)
                        {
                            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                                        "[Lighting] tile light cap exceeded "
                                        "(%u); late lights dropped. Reduce "
                                        "scene light density or raise "
                                        "MAX_PER_TILE.", MAX_PER_TILE);
                            m_warnedOverflow = true;
                        }
                        return;
                    }
                    m_scratch[byteAt(tileIdx, 1 + count)] = lightIdx;
                    ++count;
                };

                const uint32_t lightCount =
                    static_cast<uint32_t>(std::min<size_t>(lights.size(), 255));

                for (uint32_t i = 0; i < lightCount; ++i)
                {
                    const Light &L = lights[i];

                    if ((L.flags & LIGHT_FLAG_ENABLED) == 0u)
                        continue;

                    const uint32_t type = L.flags & LIGHT_TYPE_MASK;

                    // Directional lights cover everything.
                    if (type == LIGHT_TYPE_DIRECTIONAL)
                    {
                        for (int ty = 0; ty < m_tileCountY; ++ty)
                            for (int tx = 0; tx < m_tileCountX; ++tx)
                                appendToTile(tx, ty, static_cast<uint8_t>(i));
                        continue;
                    }

                    // Point / spot: bounding circle vs each tile in
                    // the light's pixel-space AABB.
                    const float lx = L.position.x;
                    const float ly = L.position.y;
                    const float r  = L.range;

                    if (r <= 0.0f) continue;

                    const float minPx = std::max(0.0f, lx - r);
                    const float minPy = std::max(0.0f, ly - r);
                    const float maxPx = std::min(static_cast<float>(viewportW), lx + r);
                    const float maxPy = std::min(static_cast<float>(viewportH), ly + r);

                    if (minPx >= maxPx || minPy >= maxPy)
                        continue;

                    const int tx0 = static_cast<int>(minPx) / static_cast<int>(TILE_SIZE);
                    const int ty0 = static_cast<int>(minPy) / static_cast<int>(TILE_SIZE);
                    const int tx1 = std::min(m_tileCountX - 1,
                                             static_cast<int>(maxPx) / static_cast<int>(TILE_SIZE));
                    const int ty1 = std::min(m_tileCountY - 1,
                                             static_cast<int>(maxPy) / static_cast<int>(TILE_SIZE));

                    for (int ty = ty0; ty <= ty1; ++ty)
                    {
                        const float tileY0 = static_cast<float>(ty * static_cast<int>(TILE_SIZE));
                        const float tileY1 = tileY0 + static_cast<float>(TILE_SIZE);

                        for (int tx = tx0; tx <= tx1; ++tx)
                        {
                            const float tileX0 = static_cast<float>(tx * static_cast<int>(TILE_SIZE));
                            const float tileX1 = tileX0 + static_cast<float>(TILE_SIZE);

                            if (circleOverlapsRect(lx, ly, r,
                                                   tileX0, tileY0,
                                                   tileX1, tileY1))
                            {
                                appendToTile(tx, ty, static_cast<uint8_t>(i));
                            }
                        }
                    }
                }

                // Upload the whole packed region that the visible
                // tile rect could touch. Worst case visible tile rect
                // is m_tileCountX × m_tileCountY tiles; in the packed
                // layout these may span up to
                //   ceil(m_tileCountX * m_tileCountY / tilesPerRow)
                // texture rows. Upload that many rows starting at 0.
                const int visibleTiles = m_tileCountX * m_tileCountY;
                const int uploadRows =
                    (visibleTiles + tilesPerRow - 1) / tilesPerRow;

                m_texture.upload(0, 0,
                                 m_textureWidth,
                                 uploadRows,
                                 m_scratch.data());
            }

            void TileLightGrid::bind(int unit) const
            {
                m_texture.bind(unit);
            }

        }
    }
}