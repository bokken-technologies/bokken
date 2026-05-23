#pragma once

#include "Base.hpp"
#include "Component.hpp"
#include "Transform2D.hpp"
#include "../renderer/lighting/Light.hpp"
#include "../renderer/lighting/ShadowCasterBuffer.hpp"

#include <glm/glm.hpp>

#include <cmath>
#include <vector>

namespace Bokken
{
    namespace Renderer
    {
        namespace Lighting
        {
            // Forward-declare: the conversion is consumed in emit()
            // but the full definition lives in Light.hpp alongside the
            // Light struct. Including that here would pull in the
            // entire lighting GPU layout for code that just needs to
            // know how to flatten a point. The implementation file
            // pulls in Light.hpp.
            struct WorldToScreen;
        }
    }
    namespace GameObject
    {

        /**
         * An explicit polygonal occluder for the 2D lighting system.
         *
         * Attach alongside a Transform2D to make the owning GameObject
         * cast shadows. The outline is a list of local-space vertices;
         * the renderer reads the sibling Transform2D each frame to
         * project these into world space and emits line segments into
         * the per-frame shadow buffer.
         *
         * Authoring conventions
         *
         *   - Vertices are in local pixel-space relative to the
         *     GameObject's position. (0, 0) is the object's anchor;
         *     positive X is right, positive Y is down (matches screen
         *     coords).
         *   - Order is counterclockwise around the silhouette in
         *     screen space. The renderer uses winding to distinguish
         *     occluder front/back; reversed winding produces inverted
         *     shadows (lit where it should be shadowed, vice versa).
         *   - The outline is implicitly closed — the last vertex
         *     connects back to the first. Don't repeat the first
         *     vertex at the end.
         *   - Outlines need not be convex. Concave polygons cast
         *     correct shadows because the shadow pass treats every
         *     edge as an independent occluder.
         *   - Fewer than 2 vertices is silently a no-op — the caster
         *     contributes no shadow segments.
         *
         * Performance
         *
         * Each frame the renderer walks every active ShadowCaster2D
         * and emits one ShadowSegment per outline edge. With 100
         * casters of 8 vertices each that's 800 segments — a 12.8 KB
         * GPU upload, fully under the MAX_UPLOADED_SEGMENTS cap of
         * 16384. Dense scenes (a thousand pieces of foliage in a
         * forest) want either coarser outlines or — once Step 11
         * lands — tile-binning so only segments near visible
         * shadow-casting lights pay the per-pixel shadow-test cost.
         *
         * @example
         *   // A simple box-shaped occluder.
         *   const wall = new GameObject("Wall")
         *       .addComponent(Transform2D, { positionX: 400, positionY: 300 })
         *       .addComponent(ShadowCaster2D, {
         *           outline: [
         *               { x: -20, y: -40 },
         *               { x:  20, y: -40 },
         *               { x:  20, y:  40 },
         *               { x: -20, y:  40 },
         *           ],
         *       });
        */
        class ShadowCaster2D : public Component
        {
        public:
            // Polygon outline in local-space pixels. CCW around the
            // silhouette, implicitly closed. See class docs for
            // authoring conventions.
            std::vector<glm::vec2> outline;

            // Global switch — false skips the caster's contribution
            // without removing it. Useful for "lights-off" cutscenes
            // or for occluders that conditionally appear (a door that
            // opens / closes).
            bool castsShadow = true;

            // Per-caster softness multiplier applied to the PCF kernel
            // radius in the shadowmap sampling pass (Step 10). At 1.0
            // the caster uses the renderer-wide default; values above
            // give softer shadows from this specific occluder (foliage,
            // fabric), values below give crisper edges (metal, stone).
            // Has no effect until PCF sampling is in place.
            float softness = 1.0f;

            void onAttach() override;
            void onDestroy() override;

            // Pure data — script writes outline + softness, renderer
            // reads them. Always considered active so the destroy-
            // when-idle sweep doesn't accidentally kill a wall.
            bool isIdle() const override { return false; }

            /**
             * Emit shadow segments for this caster into the provided
             * scratch vector, in render-target pixel coordinates.
             * Called by the lighting pass once per frame per active
             * caster.
             *
             * The w2s transform converts the caster's world-space
             * outline (Transform2D position + rotated/scaled local
             * vertices) into the render-pixel space the shadow
             * rasterizer expects. Both endpoints of every emitted
             * segment land in the same coordinate system the lighting
             * shader uses to position lights — without that match,
             * shadows are cast from the right place but fall against
             * walls drawn at the wrong scale.
             *
             * Skips the call entirely when castsShadow is false, when
             * outline has fewer than 2 vertices, or when the owning
             * GameObject has no sibling Transform2D (without a
             * transform there is no world-space placement, only the
             * local polygon). Degenerate edges (zero-length segments
             * after transform — collapsed vertices) are also skipped
             * so the shadow rasterizer never sees them.
            */
            void emit(std::vector<Renderer::Lighting::ShadowSegment> &out,
                      const Renderer::Lighting::WorldToScreen &w2s) const;

            /**
             * Every attached, non-destroyed ShadowCaster2D. The
             * lighting pass walks this list once per frame to build
             * the global shadow segment buffer. Public for the same
             * reasons as Light2D::s_all — read-only by stages and
             * debug overlays, mutation restricted to onAttach /
             * onDestroy.
            */
            static inline std::vector<ShadowCaster2D *> s_all;

        private:
            // O(1) swap-and-pop index in s_all, same pattern as
            // Light2D::m_registryIndex. -1 means not currently
            // registered.
            int m_registryIndex = -1;
        };

    }
}