#include "GlyphCache.hpp"

namespace Bokken
{
    namespace Renderer
    {
        static constexpr int kInitialAtlasSize = 2048;
        static constexpr int kMaxAtlasSize = 8192;

        GlyphCache::~GlyphCache()
        {
            for (auto &kv : m_fonts)
            {
                if (kv.second.font)
                    TTF_CloseFont(kv.second.font);
            }
        }

        std::string GlyphCache::fontKey(const std::string &path, float size) const
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "@%d", (int)(size + 0.5f));
            return path + buf;
        }

        bool GlyphCache::init()
        {
            /* Atlas storage: LINEAR.
             *
             * Glyphs are rasterised at kSupersample × the requested
             * size and stored in the atlas. The atlas texture itself
             * MUST be created with LINEAR filtering — filtering is a
             * texture parameter fixed at creation time (GL_TEXTURE_
             * MIN/MAG_FILTER), not something a shader can override
             * per-sample. Because the source data is supersampled,
             * a LINEAR-filtered sample at draw time averages multiple
             * high-res texels into each display pixel, which is what
             * gives us free SSAA anti-aliasing on text.
             *
             * (NEAREST would defeat the whole point: it'd pick a single
             * texel per destination pixel instead of averaging, so the
             * supersampled data buys nothing and glyphs look blocky.)
             *
             * Under hover-scale animation (~1.05×) the source still
             * has more resolution than the destination, so glyphs
             * stay crisp instead of pixelating. At larger scales
             * (1.5×+) you start to outrun the supersample budget;
             * bumping kSupersample to 3 trades atlas memory for
             * crispness at higher zoom.
             *
             * We bumped the initial atlas size to 2048x2048
             * because each glyph now occupies (kSupersample)² more
             * area; the doubled atlas absorbs the extra without
             * triggering an early grow + re-rasterise. */
            if (!m_atlas.create(kInitialAtlasSize, kInitialAtlasSize,
                                TextureFormat::R8,
                                TextureFilter::Linear, TextureWrap::Clamp))
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "[GlyphCache] atlas creation failed");
                return false;
            }
            m_atlasW = kInitialAtlasSize;
            m_atlasH = kInitialAtlasSize;

            std::vector<uint8_t> zeros(m_atlasW * m_atlasH, 0);
            m_atlas.upload(0, 0, m_atlasW, m_atlasH, zeros.data());

            m_penX = m_penY = m_rowHeight = 0;
            return true;
        }

        TTF_Font *GlyphCache::getFont(const std::string &path, float size, AssetPack *assets)
        {
            const std::string key = fontKey(path, size);
            auto it = m_fonts.find(key);
            if (it != m_fonts.end())
                return it->second.font;

            if (!assets)
                return nullptr;

            SDL_IOStream *io = assets->openIOStream(path);
            if (!io)
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "[GlyphCache] font not in asset pack: %s", path.c_str());
                return nullptr;
            }
            /* Open at supersample × the requested size. The atlas
             * stores oversized glyph bitmaps and the public Glyph
             * struct downscales metrics back to 1× — so the rest of
             * the engine sees normal pixel sizes and doesn't need to
             * know we're cheating. */
            const int rasterPx = (int)(size * kSupersample + 0.5f);
            TTF_Font *font = TTF_OpenFontIO(io, true, rasterPx);
            if (!font)
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "[GlyphCache] TTF_OpenFontIO failed: %s", SDL_GetError());
                return nullptr;
            }

            /* Quality knobs:
             *
             * - TTF_HINTING_LIGHT keeps the type's intended curves
             *   while still snapping to pixel grid for crispness.
             *   Default on macOS / Chrome.
             *
             * - Kerning improves visual spacing between letter pairs
             *   like "AV", "To", "Wa". On by default in modern fonts. */
            TTF_SetFontHinting(font, TTF_HINTING_LIGHT);
            TTF_SetFontKerning(font, true);

            FontEntry entry;
            entry.font = font;
            m_fonts.emplace(key, std::move(entry));
            return font;
        }

        TTF_Font *GlyphCache::FontHandle::font() const
        {
            return entry ? entry->font : nullptr;
        }

        GlyphCache::FontHandle GlyphCache::getFontHandle(
            const std::string &fontPath, float size, AssetPack *assets)
        {
            const std::string key = fontKey(fontPath, size);
            auto it = m_fonts.find(key);
            if (it == m_fonts.end())
            {
                if (!getFont(fontPath, size, assets))
                    return FontHandle{nullptr};
                it = m_fonts.find(key);
                if (it == m_fonts.end())
                    return FontHandle{nullptr};
            }
            return FontHandle{&it->second};
        }

        const GlyphCache::Glyph *GlyphCache::getGlyphFast(
            FontHandle h, uint32_t codepoint)
        {
            if (!h.valid()) return nullptr;
            FontEntry &fe = *h.entry;
            auto it = fe.glyphs.find(codepoint);
            if (it != fe.glyphs.end()) return &it->second;
            Glyph g{};
            if (!rasterizeInto(fe.font, codepoint, g))
                return nullptr;
            auto ins = fe.glyphs.emplace(codepoint, g);
            return &ins.first->second;
        }

        bool GlyphCache::growAtlasIfNeeded(int needW, int needH)
        {
            if (m_penX + needW <= m_atlasW && m_penY + needH <= m_atlasH)
                return true;

            if (m_penY + m_rowHeight + needH <= m_atlasH)
            {
                m_penX = 0;
                m_penY += m_rowHeight;
                m_rowHeight = 0;
                return true;
            }

            int newW = m_atlasW;
            int newH = m_atlasH;
            while ((newW < kMaxAtlasSize || newH < kMaxAtlasSize) &&
                   (m_penY + m_rowHeight + needH > newH))
            {
                if (newW < kMaxAtlasSize)
                    newW *= 2;
                if (newH < kMaxAtlasSize)
                    newH *= 2;
            }
            if (newW > kMaxAtlasSize || newH > kMaxAtlasSize)
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "[GlyphCache] atlas full");
                return false;
            }

            Texture2D fresh;
            if (!fresh.create(newW, newH, TextureFormat::R8,
                              TextureFilter::Linear, TextureWrap::Clamp))
                return false;
            std::vector<uint8_t> zeros(newW * newH, 0);
            fresh.upload(0, 0, newW, newH, zeros.data());

            m_atlas = std::move(fresh);
            m_atlasW = newW;
            m_atlasH = newH;
            m_penX = m_penY = m_rowHeight = 0;
            for (auto &kv : m_fonts)
                kv.second.glyphs.clear();
            return true;
        }

        bool GlyphCache::rasterizeInto(TTF_Font *font, uint32_t codepoint, Glyph &outGlyph)
        {
            if (!font)
                return false;

            int minx, maxx, miny, maxy, advance;
            TTF_GetGlyphMetrics(font, codepoint, &minx, &maxx, &miny, &maxy, &advance);

            SDL_Color white = {255, 255, 255, 255};
            SDL_Surface *raw = TTF_RenderGlyph_Blended(font, codepoint, white);
            if (!raw)
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                            "[GlyphCache] failed to rasterize U+%04X: %s",
                            codepoint, SDL_GetError());
                return false;
            }

            const int surfW = raw->w;
            const int surfH = raw->h;
            if (surfW == 0 || surfH == 0)
            {
                SDL_DestroySurface(raw);
                /* Empty glyph (e.g. space). Convert advance back to 1×. */
                outGlyph = {0, 0, 0, 0, 0, 0,
                            (advance + kSupersample / 2) / kSupersample,
                            0, 0};
                return true;
            }

            // Convert to RGBA32 for reliable byte layout (R=0, G=1, B=2, A=3).
            SDL_Surface *surf = (raw->format == SDL_PIXELFORMAT_RGBA32)
                                    ? raw
                                    : SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
            if (!surf)
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                            "[GlyphCache] SDL_ConvertSurface failed for U+%04X: %s",
                            codepoint, SDL_GetError());
                SDL_DestroySurface(raw);
                return false;
            }

            // Tight-crop the glyph bitmap to actual ink pixels.
            //
            // SDL_ttf's TTF_RenderGlyph_Blended produces a surface
            // covering the full line height with the glyph positioned
            // internally. We crop to the visible ink box and rely on
            // per-glyph bearingX/bearingY for positioning — this is
            // how stb_truetype-based renderers, CryEngine, and id Tech
            // all do it.

            const uint8_t *src = static_cast<const uint8_t *>(surf->pixels);
            const int pitch = surf->pitch;

            int cropTop = surfH, cropBottom = -1;
            int cropLeft = surfW, cropRight = -1;

            for (int y = 0; y < surfH; ++y)
            {
                const uint8_t *row = src + y * pitch;
                for (int x = 0; x < surfW; ++x)
                {
                    if (row[x * 4 + 3] > 0)
                    {
                        if (y < cropTop)    cropTop = y;
                        if (y > cropBottom) cropBottom = y;
                        if (x < cropLeft)   cropLeft = x;
                        if (x > cropRight)  cropRight = x;
                    }
                }
            }

            if (cropBottom < cropTop || cropRight < cropLeft)
            {
                if (surf != raw)
                    SDL_DestroySurface(surf);
                SDL_DestroySurface(raw);
                outGlyph = {0, 0, 0, 0, 0, 0,
                            (advance + kSupersample / 2) / kSupersample,
                            0, 0};
                return true;
            }

            const int cropW = cropRight - cropLeft + 1;
            const int cropH = cropBottom - cropTop + 1;

            if (!growAtlasIfNeeded(cropW + 1, cropH + 1))
            {
                if (surf != raw)
                    SDL_DestroySurface(surf);
                SDL_DestroySurface(raw);
                return false;
            }

            std::vector<uint8_t> r8(static_cast<size_t>(cropW) * cropH);
            for (int y = 0; y < cropH; ++y)
            {
                const uint8_t *row = src + (cropTop + y) * pitch;
                for (int x = 0; x < cropW; ++x)
                {
                    r8[y * cropW + x] = row[(cropLeft + x) * 4 + 3];
                }
            }

            m_atlas.upload(m_penX, m_penY, cropW, cropH, r8.data());

            /* UVs index the supersampled atlas — cropW × cropH atlas
             * pixels. The Glyph's `width`/`height` below are 1× display
             * pixels, so the destination quad is smaller than the
             * source region, producing the SSAA-on-text effect when
             * the GPU samples with LINEAR filtering in the fragment shader. */
            outGlyph.u0 = m_penX;
            outGlyph.v0 = m_penY;
            outGlyph.u1 = m_penX + cropW;
            outGlyph.v1 = m_penY + cropH;

            /* Convert all metrics from supersample → 1× display pixels.
             * Symmetric rounding so we don't bias glyphs toward floor.
             *
             *   bearingX = minx (font's left side bearing)
             *   bearingY = maxy (top side bearing — distance from
             *                    baseline up to ink top)
             *   width/height: the cropped ink box, in 1× pixels.
             *   advance: pen advance after this glyph. */
            const int half = kSupersample / 2;
            outGlyph.width    = (cropW    + half) / kSupersample;
            outGlyph.height   = (cropH    + half) / kSupersample;
            outGlyph.bearingX = (minx     + (minx >= 0 ? half : -half)) / kSupersample;
            outGlyph.bearingY = (maxy     + (maxy >= 0 ? half : -half)) / kSupersample;
            outGlyph.advance  = (advance  + half) / kSupersample;

            /* Floors of the cropped box can go to zero on tiny glyphs
             * (e.g. period at small sizes); clamp so the destination
             * quad stays at least one pixel — otherwise the linear
             * downsample collapses ink to nothing. */
            if (outGlyph.width  < 1) outGlyph.width  = 1;
            if (outGlyph.height < 1) outGlyph.height = 1;

            m_penX += cropW + 1;
            if (cropH + 1 > m_rowHeight)
                m_rowHeight = cropH + 1;

            if (surf != raw)
                SDL_DestroySurface(surf);
            SDL_DestroySurface(raw);
            return true;
        }

        const GlyphCache::Glyph *GlyphCache::getGlyph(const std::string &fontPath, float size,
                                                      AssetPack *assets, uint32_t codepoint)
        {
            const std::string key = fontKey(fontPath, size);
            auto fIt = m_fonts.find(key);
            if (fIt == m_fonts.end())
            {
                if (!getFont(fontPath, size, assets))
                    return nullptr;
                fIt = m_fonts.find(key);
                if (fIt == m_fonts.end())
                    return nullptr;
            }

            auto gIt = fIt->second.glyphs.find(codepoint);
            if (gIt != fIt->second.glyphs.end())
                return &gIt->second;

            Glyph g{};
            if (!rasterizeInto(fIt->second.font, codepoint, g))
                return nullptr;
            auto [insIt, _] = fIt->second.glyphs.emplace(codepoint, g);
            return &insIt->second;
        }

    }
}