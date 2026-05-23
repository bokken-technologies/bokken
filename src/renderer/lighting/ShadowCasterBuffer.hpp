#pragma once

#include "../Texture2D.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Bokken
{
    namespace Renderer
    {
        namespace Lighting
        {

            /**
             * A single occluder line segment in world-space pixels.
             *
             * Stored as exactly four floats (one RGBA32F texel) so the
             * GPU side can index segments via texelFetch with no offset
             * arithmetic — `texelFetch(u_segments, ivec2(0, i), 0)` is
             * the entire segment for index i.
             *
             * Winding convention: segments are emitted in
             * counterclockwise order around the source polygon in
             * screen-space (top-left origin). The shadowmap pass uses
             * this to distinguish the inside from the outside of each
             * occluder when computing per-pixel "is this pixel in
             * shadow" tests; reversed winding produces inverted
             * shadows (sprite appears lit by lights it should occlude
             * and vice versa).
            */
            struct ShadowSegment
            {
                glm::vec2 a;
                glm::vec2 b;
            };

            // Compile-time size check so the GPU texture layout stays
            // in sync with the C++ struct. RGBA32F is 16 bytes per
            // texel; one ShadowSegment must fit exactly.
            static_assert(sizeof(ShadowSegment) == 16,
                          "ShadowSegment must be exactly 16 bytes "
                          "(one RGBA32F texel) to upload directly as "
                          "shadow-segment texture data.");

            /**
             * Soft cap on simultaneously-uploaded shadow segments.
             *
             * Chosen to leave plenty of headroom for complex scenes —
             * a 16384-segment buffer at 16 bytes/segment is 256 KB of
             * VRAM, negligible on every shipping target. Scenes that
             * exceed the cap drop late segments with a one-shot
             * warning, same pattern as MAX_UPLOADED_LIGHTS.
             *
             * Tiled forward+ in Step 11 will additionally cull
             * segments per-tile against the tile's spanning lights;
             * after tiling lands, the global cap rarely matters
             * because only segments near visible shadow-casting lights
             * get evaluated.
            */
            static constexpr uint32_t MAX_UPLOADED_SEGMENTS = 16384;

            /**
             * Owns the GPU-side shadow-segment data texture.
             *
             * Storage parallels LightUploadBuffer: a 2D RGBA32F texture
             * of width 1 and height MAX_UPLOADED_SEGMENTS, each texel
             * holding one segment's four floats (ax, ay, bx, by). The
             * upload is a single glTexSubImage2D straight from a
             * vector<ShadowSegment> — no repack, the C++ struct is
             * laid out byte-identically to one texel.
             *
             * The two-axis storage (width=1 here, vs width=5 for the
             * light data) is purely to allow texelFetch addressing by
             * a y-coord that is the segment index. It would be
             * marginally cheaper to use a 1D texture but GL 3.3
             * sampler2D is more universal than samplerBuffer and the
             * cost difference is zero in practice.
             *
             * Threading: single-threaded use only, same constraints
             * as LightUploadBuffer.
            */
            class ShadowCasterBuffer
            {
            public:
                ShadowCasterBuffer() = default;
                ~ShadowCasterBuffer() = default;

                ShadowCasterBuffer(const ShadowCasterBuffer &) = delete;
                ShadowCasterBuffer &operator=(const ShadowCasterBuffer &) = delete;
                ShadowCasterBuffer(ShadowCasterBuffer &&) noexcept = default;
                ShadowCasterBuffer &operator=(ShadowCasterBuffer &&) noexcept = default;

                /** Allocate the GPU texture. Call once after the GL context exists. */
                bool init();

                /**
                 * Upload up to MAX_UPLOADED_SEGMENTS segments. Segments
                 * past the cap are silently truncated with a one-shot
                 * warning. Returns the count actually uploaded.
                 *
                 * Safe with zero segments — the texture is left
                 * unchanged and the call records a zero count without
                 * touching GL state.
                */
                uint32_t upload(const std::vector<ShadowSegment> &segments);

                /** Bind the texture at the given unit for shader access. */
                void bind(int unit) const;

                /** Segments uploaded on the most recent upload() call. */
                uint32_t uploadedCount() const { return m_uploadedCount; }

                /** Underlying texture, exposed for stages that want to
                 *  read the count uniform without re-binding. */
                const Texture2D &texture() const { return m_texture; }

            private:
                Texture2D m_texture;
                uint32_t m_uploadedCount = 0;
            };

        }
    }
}