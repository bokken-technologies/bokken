#pragma once

#include "Texture2D.hpp"
#include "../AssetPack.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>

namespace Bokken
{
    namespace Renderer
    {

        /**
         * Sub-region of a texture. Describes a rectangular area in pixel
         * coordinates plus pre-computed UVs for the SpriteBatcher.
         *
         * If the source is a standalone image (not a sprite sheet), the
         * region covers the full texture and UVs are (0,0)→(1,1).
        */
        struct TextureRegion
        {
            const Texture2D *texture = nullptr;

            // Pixel rectangle within the texture.
            int x = 0, y = 0;
            int w = 0, h = 0;

            // Pre-computed UVs.
            float u0 = 0.0f, v0 = 0.0f;
            float u1 = 1.0f, v1 = 1.0f;

            bool isValid() const { return texture && texture->isValid(); }
        };

        /**
         * Centralized texture manager.
         *
         * Loads images from the AssetPack virtual filesystem via SDL_image,
         * uploads them as GL textures, and caches by virtual path. Duplicate
         * loads return the same Texture2D — no redundant GPU uploads.
         *
         * Also stores named atlas regions: call defineRegion() to carve a
         * named sub-rectangle out of a loaded texture. Animation2D and
         * Sprite2D components reference these by name.
         *
         * Ownership:
         *   TextureCache owns all Texture2D instances. Consumers receive
         *   const pointers valid for the cache's lifetime (which matches
         *   the Renderer, which matches the engine).
         *
         * Not thread-safe — all access from the main/render thread only.
        */
        class TextureCache
        {
        public:
            TextureCache() = default;
            ~TextureCache() = default;

            TextureCache(const TextureCache &) = delete;
            TextureCache &operator=(const TextureCache &) = delete;

            /**
             * Load a texture from the asset pack. Returns nullptr on failure.
             * Subsequent calls with the same path return the cached texture.
             *
             * @param virtualPath  Path inside the VFS (e.g. "/textures/player.png").
             * @param assets       The mounted asset pack to load from.
             * @param filter       Texture filtering mode.
             * @param wrap         Texture wrapping mode.
            */
            const Texture2D *load(const std::string &virtualPath,
                                  AssetPack *assets,
                                  TextureFilter filter = TextureFilter::Linear,
                                  TextureWrap wrap = TextureWrap::Clamp);

            /**
             * Get or generate a tangent-space normal map for an albedo
             * texture in the asset pack. The result is cached under a
             * synthesised key derived from the albedo path, so repeated
             * calls return the same GL texture.
             *
             * The generator runs a Sobel filter on the source image's
             * alpha channel — see NormalGenerator for technique notes.
             * The result is suitable as the second sampler in the
             * SpriteBatcher's MRT sprite shader: sample with the same
             * UVs as the albedo and decode (2 * rgb - 1) in the
             * lighting pass.
             *
             * Returns nullptr if the albedo is missing or generation
             * fails. The strength parameter is passed through to
             * NormalGenerator unchanged.
            */
            const Texture2D *loadOrGenerateNormal(const std::string &albedoVirtualPath,
                                                  AssetPack *assets,
                                                  float strength = 3.0f,
                                                  TextureFilter filter = TextureFilter::Linear,
                                                  TextureWrap wrap = TextureWrap::Clamp);

            /** Get a previously loaded texture. Returns nullptr if not cached. */
            const Texture2D *get(const std::string &virtualPath) const;

            /** Unload a specific texture, freeing its GL resource. */
            void unload(const std::string &virtualPath);

            /** Unload everything. */
            void clear();

            /**
             * Define a named sub-region of a loaded texture.
             *
             * @param name         Unique region name (e.g. "player_idle_0").
             * @param texturePath  Virtual path of the parent texture.
             * @param x, y, w, h  Pixel rectangle within the texture.
            */
            void defineRegion(const std::string &name,
                              const std::string &texturePath,
                              int x, int y, int w, int h);

            /**
             * Define a grid of regions from a sprite sheet. Generates names
             * as "prefix_0", "prefix_1", etc. scanned left-to-right,
             * top-to-bottom.
             *
             * @param prefix       Name prefix for generated regions.
             * @param texturePath  Virtual path of the sprite sheet.
             * @param frameW       Width of each frame in pixels.
             * @param frameH       Height of each frame in pixels.
             * @param count        Total number of frames (0 = fill the sheet).
             * @param offsetX      Pixel offset from left edge of texture.
             * @param offsetY      Pixel offset from top edge of texture.
             * @param paddingX     Horizontal gap between frames.
             * @param paddingY     Vertical gap between frames.
             * @return             Number of regions created.
            */
            int defineGrid(const std::string &prefix,
                           const std::string &texturePath,
                           int frameW, int frameH,
                           int count = 0,
                           int offsetX = 0, int offsetY = 0,
                           int paddingX = 0, int paddingY = 0);

            /** Look up a named region. Returns nullptr if not found. */
            const TextureRegion *region(const std::string &name) const;

            /**
             * Build a full-texture region on the fly. Useful when the caller
             * has a texture path but no named region.
            */
            TextureRegion fullRegion(const std::string &texturePath) const;

        private:
            std::unordered_map<std::string, std::unique_ptr<Texture2D>> m_textures;
            std::unordered_map<std::string, TextureRegion> m_regions;

            // Synthesise the cache key for an auto-generated normal map.
            // Centralised so the suffix convention stays consistent across
            // load and unload paths — unloading the albedo evicts the
            // auto-generated normal too via prefix scan in unload().
            static std::string autoNormalKey(const std::string &albedoPath);
        };

    }
}