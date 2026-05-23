#pragma once

#include "Texture2D.hpp"
#include "../AssetPack.hpp"

#include <glad/gl.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <unordered_map>
#include <string>
#include <cstdint>
#include <memory>
#include <cstring>
#include <vector>

namespace Bokken
{
    namespace Renderer
    {

        /**
         * Glyph atlas backed by SDL_ttf, with supersampled rasterisation
         * for crisp text under animation / scaling.
         *
         * For each (font path, font size) we maintain:
         *   - an open TTF_Font, opened at SUPERSAMPLE × the requested
         *     pixel size (default supersample = 2). The atlas stores
         *     oversized glyph bitmaps; the public Glyph struct exposes
         *     metrics already divided back down to the requested size,
         *     so callers see normal 1× values.
         *   - an R8 GL atlas texture, sampled with LINEAR filtering.
         *     Linear is correct here precisely because we ARE
         *     downsampling at draw time — the GPU averages 4 supersample
         *     texels into each display pixel, giving 4× SSAA on text
         *     for free. Under hover-scale animation (~1.05×) the source
         *     resolution still beats the destination so glyphs stay
         *     sharp instead of pixelating.
         *   - a per-codepoint glyph table with UVs and metrics.
         *
         * The supersample factor is a build-time constant
         * `kSupersample`; bumping it to 3 trades atlas memory for
         * crispness at higher animation scales. 2× is the sweet spot
         * for typical UI hover-bounces.
         *
         * Glyphs are rasterised lazily on first use. The atlas grows
         * in a "shelf" pattern: rows of fixed height, glyphs packed
         * left-to-right. When a row fills, we advance to the next row.
         * When the atlas fills, we double its size and re-rasterise
         * everything (rare in practice — at 2× supersample a typical
         * UI workload stays under ~2 MB).
         *
         * This is the bridge between SDL_ttf's classic SDL_Surface
         * output and our GL pipeline. Replacing SDL_ttf with FreeType
         * or stb_truetype is a swap-in change here later — the public
         * API doesn't depend on it.
        */
        class GlyphCache
        {
        public:
            /* Supersample factor for glyph rasterisation. The atlas
             * stores glyphs at this multiple of the requested font
             * size; the Glyph struct's metrics are pre-divided so
             * callers see normal 1× values. */
            static constexpr int kSupersample = 2;

            struct Glyph
            {
                /* Position within the atlas, in atlas pixels (i.e.
                 * supersample-scaled). Width/height of the atlas
                 * region equals (u1 - u0, v1 - v0). */
                int u0, v0, u1, v1;
                /* Metrics in 1× display pixels (already divided by
                 * supersample). The renderer uses these directly for
                 * the destination quad — callers don't need to know
                 * about the supersample factor. */
                int bearingX, bearingY;
                int advance;
                int width, height;
            };

            /* FontEntry is forward-declared (full definition below)
             * so FontHandle can hold a pointer to it. */
            struct FontEntry;

            GlyphCache() = default;
            ~GlyphCache();

            bool init();

            /**
             * Get (and cache) a font handle.
             *
             * `path` is virtual — resolved against the asset pack.
             * `size` is in pixels (already DPI-scaled by the caller).
             * Internally we open the font at `size * kSupersample`.
             * Returns nullptr on fail.
            */
            TTF_Font *getFont(const std::string &path, float size, AssetPack *assets);

            /**
             * Get the glyph entry for a (font, size, codepoint),
             * rasterising into the atlas on first miss. Returns
             * nullptr if the font is unavailable or the glyph can't
             * be rendered.
            */
            const Glyph *getGlyph(const std::string &fontPath, float size, AssetPack *assets,
                                  uint32_t codepoint);

            /**
             * Opaque handle that bundles the TTF_Font* with the cache
             * slot for that (path, size). Use the two-step lookup
             *   FontHandle h = getFontHandle(path, size, assets);
             *   for (cp : codepoints) {
             *       const Glyph *g = getGlyphFast(h, cp);
             *       …
             *   }
             * to avoid the per-glyph std::string allocation that the
             * (fontPath, size) → key path otherwise pays. With ~800
             * glyphs per frame on a Code block that allocation
             * dominated CPU on the welcome page; the fast-path drops
             * it to a single hashmap lookup per codepoint.
            */
            struct FontHandle {
                FontEntry *entry = nullptr;
                TTF_Font *font() const;
                bool valid() const { return entry != nullptr; }
            };
            FontHandle getFontHandle(const std::string &fontPath, float size, AssetPack *assets);
            const Glyph *getGlyphFast(FontHandle h, uint32_t codepoint);

            /* FontEntry is logically internal but lives in public so
             * FontHandle (which embeds a pointer to it) is usable
             * cross-translation-unit. Treat as opaque from outside. */
            struct FontEntry
            {
                TTF_Font *font = nullptr;
                std::unordered_map<uint32_t, Glyph> glyphs;
            };

            /** The atlas texture — bound by SpriteBatcher when drawing glyphs. */
            const Texture2D *atlas() const { return &m_atlas; }
            Texture2D *atlas() { return &m_atlas; }

        private:
            Texture2D m_atlas;
            int m_atlasW = 0;
            int m_atlasH = 0;
            // Shelf-allocator state.
            int m_penX = 0;
            int m_penY = 0;
            int m_rowHeight = 0;

            std::unordered_map<std::string, FontEntry> m_fonts;

            std::string fontKey(const std::string &path, float size) const;
            bool growAtlasIfNeeded(int needW, int needH);
            bool rasterizeInto(TTF_Font *font, uint32_t codepoint, Glyph &outGlyph);
        };

    }
}