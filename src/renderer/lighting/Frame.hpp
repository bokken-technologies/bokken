#pragma once

#include "Light.hpp"
#include "UploadBuffer.hpp"
#include "ShadowCasterBuffer.hpp"
#include "TileLightGrid.hpp"
#include "CookieAtlas.hpp"
#include "../RenderTarget.hpp"

#include <cstdint>
#include <vector>

namespace Bokken
{
    namespace Renderer
    {
        namespace Lighting
        {

            /**
             * Shared per-frame lighting state. Owned by Pipeline so
             * both the ShadowmapPass and the LightingPass can read it
             * without one stage reaching into the other's privates.
             *
             * The frame-data is populated by whichever stage runs
             * first via gatherIfNeeded(frameId), which is idempotent
             * within a single frame (subsequent calls in the same
             * frame are no-ops). Both stages call it at the top of
             * their execute(); the actual walk-and-upload happens
             * exactly once per frame regardless of stage ordering.
             *
             * What gets gathered each frame:
             *   - Every enabled Light2D snapshotted into m_lights.
             *   - Shadow slots assigned in walk order (0, 1, 2, ...)
             *     to lights with castsShadows=true. Lights past
             *     MAX_SHADOW_SLOTS render unshadowed.
             *   - Every enabled ShadowCaster2D's outline transformed
             *     to world space and emitted as segments into
             *     m_segments.
             *   - m_lights uploaded to the GPU light data texture.
             *   - m_segments uploaded to the GPU shadow-segment texture.
             *
             * The two stages then read the uploaded GPU buffers
             * (textures bound at the appropriate sampler units) and
             * the lightCount() / segmentCount() / shadowCount()
             * accessors to know loop bounds for their shaders.
             *
             * Threading: single-threaded only, same as every other
             * Bokken render-thread structure.
            */
            class Frame
            {
            public:
                /**
                 * Maximum number of shadow-casting lights per frame.
                 * Equal to the shadow atlas height — the atlas is
                 * sized SHADOW_ATLAS_WIDTH wide × MAX_SHADOW_SLOTS tall,
                 * one row per shadow-casting light.
                 *
                 * Lights with castsShadows=true past this cap get
                 * shadowSlot = LIGHT_NO_SLOT and render unshadowed
                 * for the frame. The lighting shader checks the
                 * sentinel and skips the shadow sample. No warning
                 * is emitted — exceeding the cap is "scene has more
                 * shadowed lights than the atlas can fit" which is a
                 * design call, not an error.
                */
                static constexpr uint32_t MAX_SHADOW_SLOTS = 256;

                /**
                 * Angular resolution of the shadowmap atlas in texels.
                 * Each row of the atlas covers a full 2*pi sweep around
                 * one light, so the angular step is (2*pi / WIDTH).
                 * 256 texels = ~1.4 degrees per step, which softens
                 * acceptably with PCF in Step 10. Higher resolutions
                 * cost linearly more atlas memory and shadow-pass
                 * fragment work; lower resolutions become visibly
                 * stepped at large light radii.
                */
                static constexpr uint32_t SHADOW_ATLAS_WIDTH = 256;

                Frame() = default;
                ~Frame() = default;

                Frame(const Frame &) = delete;
                Frame &operator=(const Frame &) = delete;

                /**
                 * Allocate the GPU buffers backing this frame data.
                 * Call once after the GL context exists; cheap to call
                 * — internally delegates to UploadBuffer::init
                 * and ShadowCasterBuffer::init.
                */
                bool init();

                /**
                 * Gather + upload the per-frame lighting state if it
                 * hasn't been done this frame. Stages pass their own
                 * monotonically-increasing frameId (typically the
                 * pipeline-level frame counter); the gather runs once
                 * per unique frameId. Calling with the same frameId
                 * twice is a no-op.
                 *
                 * The viewport dimensions are needed for tile binning
                 * (Step 11+). Stages always have them via the
                 * FrameContext they receive — pass them through here
                 * so the binning operates on the actual visible region.
                 *
                 * The frameId convention is: caller increments outside
                 * this class (Pipeline owns the counter and advances
                 * it at the top of each render frame). Frame
                 * itself just compares against m_lastGatheredFrame.
                */
                void gatherIfNeeded(uint64_t frameId,
                                    int viewportW, int viewportH);

                /**
                 * Force re-gather on the next gatherIfNeeded call,
                 * regardless of frame id. Useful for debug toggles
                 * that need to refresh state mid-frame; not part of
                 * the normal render path.
                */
                void invalidate() { m_lastGatheredFrame = UINT64_MAX; }

                /** Lights uploaded this frame. <= MAX_UPLOADED_LIGHTS. */
                uint32_t lightCount() const { return m_lightBuffer.uploadedCount(); }

                /** Shadow segments uploaded this frame. <= MAX_UPLOADED_SEGMENTS. */
                uint32_t segmentCount() const { return m_shadowBuffer.uploadedCount(); }

                /** Shadow slots actually assigned this frame.
                 *  <= MAX_SHADOW_SLOTS and <= number of lights with
                 *  castsShadows=true. */
                uint32_t shadowCount() const { return m_shadowCount; }

                /**
                 * True after the ShadowmapPass has rendered the atlas
                 * for this frame; false at gather time. The lighting
                 * pass uses this to know whether the atlas contents are
                 * fresh and sample-able, or stale from a previous frame
                 * (or never rendered at all, in pipelines without a
                 * ShadowmapPass).
                */
                bool shadowAtlasRendered() const { return m_shadowAtlasRendered; }

                /** Called by ShadowmapPass after a successful atlas render. */
                void markShadowAtlasRendered() { m_shadowAtlasRendered = true; }

                /** Read-only handles to the GPU buffers for stages to bind. */
                const UploadBuffer &lightBuffer() const { return m_lightBuffer; }
                const ShadowCasterBuffer &shadowBuffer() const { return m_shadowBuffer; }

                UploadBuffer &lightBuffer() { return m_lightBuffer; }
                ShadowCasterBuffer &shadowBuffer() { return m_shadowBuffer; }

                /**
                 * The shadow atlas render target. Owned by the lighting
                 * frame because both the ShadowmapPass (which renders
                 * into it) and the LightingPass (which samples it) need
                 * access, and neither stage owns the other.
                 *
                 * Returns a valid RenderTarget after init() if the atlas
                 * allocation succeeded; on allocation failure the
                 * returned RenderTarget's isValid() returns false and
                 * stages should bail out of shadow rendering for the
                 * session.
                */
                RenderTarget &shadowAtlas() { return m_shadowAtlas; }
                const RenderTarget &shadowAtlas() const { return m_shadowAtlas; }

                /**
                 * The tile-light grid used for forward+ culling. The
                 * lighting pass binds this and reads it per-fragment to
                 * iterate only the lights touching the fragment's tile.
                 *
                 * Populated each frame by gatherIfNeeded after the
                 * snapshot. The grid's tileCountX / tileCountY reflect
                 * the most recent viewport.
                */
                TileLightGrid &tileGrid() { return m_tileGrid; }
                const TileLightGrid &tileGrid() const { return m_tileGrid; }

                /**
                 * The cookie atlas. The lighting pass binds this and
                 * reads it per-fragment for lights with a non-empty
                 * cookiePath. The atlas resolves the cookie image
                 * lazily on first use of each unique path.
                 *
                 * Cookie loading requires a wired asset pack — call
                 * setAssetPack at engine startup before the first
                 * frame.
                */
                CookieAtlas &cookieAtlas() { return m_cookieAtlas; }
                const CookieAtlas &cookieAtlas() const { return m_cookieAtlas; }

                /**
                 * Wire the asset pack for cookie loading. Forwards to
                 * CookieAtlas::setAssetPack. The lighting frame
                 * doesn't otherwise need an asset pack — every other
                 * resource it manages is GPU-only.
                */
                void setAssetPack(AssetPack *assets)
                    { m_cookieAtlas.setAssetPack(assets); }

            private:
                UploadBuffer m_lightBuffer;
                ShadowCasterBuffer m_shadowBuffer;
                RenderTarget m_shadowAtlas;
                TileLightGrid m_tileGrid;
                CookieAtlas m_cookieAtlas;

                // Reused frame-to-frame; cleared on every gather.
                std::vector<Light> m_lights;
                std::vector<ShadowSegment> m_segments;

                // Number of shadow slots assigned this frame (0 if no
                // lights have castsShadows=true). Drives the
                // ShadowmapPass's draw — only the first m_shadowCount
                // atlas rows need rasterisation.
                uint32_t m_shadowCount = 0;

                // Set false at every gather, set true by the
                // ShadowmapPass once the atlas has been cleared and
                // rasterised this frame. The lighting pass checks this
                // before sampling the atlas to distinguish "fresh
                // shadow data" from "stale data left over from a
                // previous session or a pipeline that never installed
                // a ShadowmapPass".
                bool m_shadowAtlasRendered = false;

                // Frame id of the most recent gather. Compared in
                // gatherIfNeeded to skip redundant work when both
                // lighting stages call within the same frame. Initial
                // value of UINT64_MAX guarantees the first call
                // (with any sensible frameId starting at 0 or 1)
                // does the gather.
                uint64_t m_lastGatheredFrame = UINT64_MAX;
            };

        }
    }
}