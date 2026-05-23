#pragma once

#include "../renderer/SpriteBatcher.hpp"
#include "../renderer/Texture2D.hpp"
#include "SimpleStyleSheet.hpp"

#include <cstdint>

namespace Bokken
{
    namespace Canvas
    {
        /**
         * Higher-level drawing helpers that compose multiple SpriteBatcher
         * quads to produce rounded rectangles, gradients, and soft shadows.
         *
         * These exist because the SpriteBatcher is intentionally a flat
         * "textured quad pusher" — adding a separate rounded-rect shader
         * would split the batch and slow everything down. Instead we
         * decompose:
         *   - Rounded rect → 1 center rect + 4 edge rects + 4 corner
         *     quads sampled from a small alpha lookup texture.
         *   - Linear gradient → tinted UV-aligned strips, currently a
         *     two-strip approximation (good enough for vertical/horizontal
         *     gradients; angle is snapped to nearest cardinal). Extending
         *     this to a real per-vertex-tinted quad would mean breaking
         *     the batcher's "one color per quad" invariant — we'll do
         *     that when someone needs diagonal gradients.
         *   - Drop shadow → 9-slice of an alpha lookup that has a soft
         *     edge baked in. The blur radius scales the slice geometry.
         *
         * The corner / shadow lookup textures are lazy-initialised on
         * first use and shared globally. They live in `LookupTextures`
         * (singleton) — one corner ring atlas (256×256, alpha) and one
         * shadow soft-disc atlas (256×256, alpha).
         *
         * Drawing functions take a `tint` parameter as the global subtree
         * opacity multiplier, applied to the final per-quad alpha. The
         * caller (View::draw) computes this from getGlobalOpacity().
        */
        namespace Drawing
        {
            /**
             * One-time setup of the lookup textures used by rounded-rect
             * and shadow rendering. Safe to call multiple times — it's
             * idempotent. Called automatically on first use; you don't
             * normally need to call this directly.
            */
            void ensureLookups();

            /**
             * Tear down lookup textures. Called from the engine shutdown
             * path (Loop::~Loop) to clean up GL resources.
            */
            void releaseLookups();

            /**
             * Apply a per-subtree opacity multiplier to a packed RGBA
             * color. Multiplies the alpha channel; leaves RGB intact.
             * If the resulting alpha rounds to 0, returns 0 — callers
             * use that as a "skip" signal.
            */
            uint32_t applyTint(uint32_t rgba, float opacity);

            /**
             * Per-corner radii bundle. Returned by resolveCorners() and
             * passed to fillRoundedRect / strokeRoundedBorder. The four
             * fields are in CSS shorthand order: top-left, top-right,
             * bottom-right, bottom-left.
            */
            struct Corners
            {
                float tl, tr, br, bl;
                bool any() const { return tl > 0.0f || tr > 0.0f || br > 0.0f || bl > 0.0f; }
            };
            Corners resolveCorners(const SimpleStyleSheet &s, float w, float h);

            /**
             * Draw a filled rounded rectangle. If all four radii are 0
             * this falls through to a single drawRect. Otherwise it emits
             * a center fill + 4 edge fills + 4 corner sprites, all on
             * the same `layer`.
            */
            void fillRoundedRect(Renderer::SpriteBatcher &batcher,
                                  float x, float y, float w, float h,
                                  const Corners &c, uint32_t rgba,
                                  int layer);

            /**
             * Draw an axis-aligned linear gradient. Currently snaps the
             * angle to nearest cardinal (0°, 90°, 180°, 270°). If
             * `corners.any()` is true, the gradient is intersected with
             * the rounded silhouette by drawing two stacked rounded
             * rects with the appropriate colors and clipping their
             * overlap — see notes in fillRoundedGradient.
            */
            void fillRoundedGradient(Renderer::SpriteBatcher &batcher,
                                      float x, float y, float w, float h,
                                      const Corners &c,
                                      uint32_t startRgba, uint32_t endRgba,
                                      float angleDeg,
                                      int layer);

            /**
             * Stroke a rounded-rect border. `widths` is per-side (top,
             * right, bottom, left); `colors` is per-side (same order).
             * For non-zero corners we emit corner sprites tinted with
             * the adjacent edge color (when both adjacent sides agree;
             * otherwise we fall back to the borderColor shorthand for
             * the corner — same compromise CSS makes for non-uniform
             * borders).
            */
            void strokeRoundedBorder(Renderer::SpriteBatcher &batcher,
                                      float x, float y, float w, float h,
                                      const Corners &c,
                                      const float widths[4],
                                      const uint32_t colors[4],
                                      uint32_t fallbackColor,
                                      int layer);

            /**
             * Draw a soft drop shadow under a rounded-rect silhouette.
             * Offset and blur define the shape; rgba is the shadow
             * color (alpha controls density). Drawn as a 9-slice of
             * the soft-disc lookup, scaled so the disc radius matches
             * the blur. The rendered shadow is placed BELOW its caller
             * by using `layer-1`.
            */
            void dropShadow(Renderer::SpriteBatcher &batcher,
                             float x, float y, float w, float h,
                             const Corners &c,
                             float offsetX, float offsetY, float blur,
                             uint32_t rgba, int layer);

            /**
             * Draw an image quad sampling from a Texture2D, with optional
             * rounded-rect masking. When corners.any() is false this is
             * a single drawTextured call; otherwise we use the same
             * decomposition as fillRoundedRect, sampling the image UV
             * range corresponding to each piece.
            */
            void drawImage(Renderer::SpriteBatcher &batcher,
                            const Renderer::Texture2D *tex,
                            float x, float y, float w, float h,
                            const Corners &c, uint32_t tint,
                            int layer);
        }
    }
}
