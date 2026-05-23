#pragma once

#include "Light.hpp"
#include "../Texture2D.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Bokken
{
    namespace Renderer
    {
        namespace Lighting
        {

            /**
             * The hard cap on simultaneously-uploaded lights. Sized to
             * fit comfortably within GL 3.3's GL_MAX_TEXTURE_SIZE on
             * every conforming driver (minimum guarantee is 1024;
             * actual is 4096+ everywhere) while leaving the texture
             * height reasonable for cache locality.
             *
             * Lights past this cap are dropped by the lighting pass
             * with a single warning at the first overflow; the artist-
             * facing recommendation is to enable tiled forward+
             * culling (Step 11) which makes the cap effectively
             * irrelevant since lights outside any visible tile cost
             * nothing.
            */
            static constexpr uint32_t MAX_UPLOADED_LIGHTS = 256;

            /**
             * Owns the GPU-side light data texture and the upload path.
             *
             * The light data texture is a 2D texture of width 5 and
             * height MAX_UPLOADED_LIGHTS, format RGBA32F. Each row of
             * the texture corresponds to one Light row in the struct;
             * each column corresponds to one light index. The C++
             * Light struct is laid out byte-for-byte to match this
             * texture's natural memory order, so upload is a single
             * glTexSubImage2D with no intermediate copy.
             *
             * Usage:
             *   UploadBuffer ub;
             *   ub.init();
             *   ...
             *   std::vector<Light> lights = collectLightsThisFrame();
             *   ub.upload(lights);
             *   ub.bind(unit);  // unit becomes u_lightTex in the shader
             *   ...draw using lights...
             *   // No explicit unbind — texture units are reset by the
             *   // lighting pass on exit.
             *
             * Threading: single-threaded use only. The upload buffer
             * is owned by the lighting pass and operated entirely on
             * the render thread.
            */
            class UploadBuffer
            {
            public:
                UploadBuffer() = default;
                ~UploadBuffer() = default;

                UploadBuffer(const UploadBuffer &) = delete;
                UploadBuffer &operator=(const UploadBuffer &) = delete;
                UploadBuffer(UploadBuffer &&) noexcept = default;
                UploadBuffer &operator=(UploadBuffer &&) noexcept = default;

                /** Allocate the GPU texture. Call once after the GL context exists. */
                bool init();

                /**
                 * Upload up to MAX_UPLOADED_LIGHTS lights to the GPU.
                 * Lights past the cap are silently truncated; the
                 * caller is expected to have culled to fit. The
                 * returned count is the number actually uploaded —
                 * the same as min(lights.size(), MAX_UPLOADED_LIGHTS).
                 *
                 * Safe to call with zero lights; the texture is left
                 * unchanged and the call is a no-op apart from
                 * recording the zero count.
                */
                uint32_t upload(const std::vector<Light> &lights);

                /** Bind the texture at the given unit for shader access. */
                void bind(int unit) const;

                /** How many lights were actually uploaded last call. */
                uint32_t uploadedCount() const { return m_uploadedCount; }

                /** The underlying texture, exposed for stages that
                 *  want to read the count uniform without re-binding. */
                const Texture2D &texture() const { return m_texture; }

            private:
                Texture2D m_texture;
                uint32_t m_uploadedCount = 0;
            };

        }
    }
}