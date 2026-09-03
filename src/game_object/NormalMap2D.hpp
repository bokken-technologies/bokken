#pragma once

#include "Component.hpp"
#include "../renderer/TextureCache.hpp"
#include "../AssetPack.hpp"

#include <SDL3/SDL.h>

#include <string>

namespace Bokken
{
    namespace GameObject
    {
        class Sprite2D;

        /**
         * Attaches a tangent-space normal map to the owning GameObject's
         * Sprite2D. Two authoring modes are supported:
         *
         * 1. Authored — set normalMapPath to a virtual path. The image
         *    at that path is loaded as an RGBA8 texture and bound as
         *    the sprite's normal map during the MRT sprite pass. The
         *    image should already be in tangent-space [0, 1] encoding
         *    (the format that Sprite Illuminator, Laigter, and
         *    Substance produce by default).
         *
         * 2. Auto-generated — set autoGenerate = true with
         *    normalMapPath empty. The sibling Sprite2D's source
         *    is read at first resolve() and a Sobel-from-alpha normal
         *    map is generated; subsequent resolves hit the
         *    TextureCache's auto-normal cache and pay nothing.
         *
         * Authored wins when both are set: if normalMapPath is
         * non-empty, autoGenerate is ignored. This preserves the
         * "explicit beats generated" contract — an artist who supplies
         * a hand-tuned normal map never has it overridden by the
         * fallback.
         *
         * The component holds the resolved Texture2D pointer once
         * resolve() has run successfully. Subsequent resolve() calls
         * are no-ops; setting normalMapPath / autoGenerate / strength
         * after resolution does NOT re-resolve — callers wanting to
         * swap normal maps at runtime should call invalidate() to drop
         * the cached pointer first.
         *
         * @example
         *   const player = new GameObject("Player")
         *       .addComponent(Transform2D)
         *       .addComponent(Sprite2D, { source: "/sprites/player.png" })
         *       .addComponent(NormalMap2D, {
         *           normalMapPath: "/sprites/player.normal.png",
         *       });
         *
         *   const torch = new GameObject("Torch")
         *       .addComponent(Sprite2D, { source: "/sprites/torch.png" })
         *       .addComponent(NormalMap2D, { autoGenerate: true });
        */
        class NormalMap2D : public Component
        {
        public:
            // Set once during engine init, same pattern as Animation2D.
            // Both must be non-null for resolve() to succeed; the
            // TextureCache is shared with sprite loads, the AssetPack is
            // the mounted virtual FS.
            static inline Renderer::TextureCache *s_textureCache = nullptr;
            static inline AssetPack *s_assets = nullptr;

            // Authored normal-map path. Takes precedence over autoGenerate.
            std::string normalMapPath;

            // When true and normalMapPath is empty, the component asks
            // TextureCache to Sobel-generate a normal map from the
            // sibling Sprite2D's albedo on first resolve.
            bool autoGenerate = false;

            // Strength multiplier for the Sobel filter. Only consulted
            // when autoGenerate is true. Higher = more pronounced relief
            // at the silhouette; the default is tuned for character-
            // sized sprites (32-128 px).
            float autoStrength = 3.0f;

            /**
             * Returns the resolved normal-map texture, performing the
             * authored load or auto-generation on first call. Returns
             * nullptr if neither mode is configured, the source is
             * missing, or s_textureCache / s_assets aren't set up.
             *
             * Safe to call every frame — subsequent calls return the
             * cached pointer with no work.
            */
            const Renderer::Texture2D *resolve();

            /** Drop the cached pointer so the next resolve() re-runs.
             *  Call after changing normalMapPath / autoGenerate /
             *  autoStrength at runtime if you want the change to take
             *  effect. */
            void invalidate() { m_resolved = nullptr; m_resolveAttempted = false; }

            // Pure data — always idle for the destroy-when-idle system.
            bool isIdle() const override { return true; }

        private:
            const Renderer::Texture2D *m_resolved = nullptr;

            // True after the first resolve() call regardless of success,
            // so failed resolutions don't spam log lines every frame. A
            // successful invalidate() resets both this and m_resolved.
            bool m_resolveAttempted = false;
        };

    }
}