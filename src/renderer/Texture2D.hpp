#pragma once

#include <glad/gl.h>

#include <cstdint>

namespace Bokken
{
    namespace Renderer
    {

        /**
         * GL 2D texture wrapper.
         *
         * Holds the GL handle plus width/height/format. Construction is lazy —
         * call create*() to allocate. Defaults to LINEAR / CLAMP_TO_EDGE which
         * is the right choice for UI and HD sprites; pixel-art games can flip
         * to NEAREST via setFilter().
         *
         * Texture2D is non-copyable and non-shared. Hold via unique_ptr or by
         * value; do not pass around by raw pointer beyond simple borrowing
         * inside a frame.
        */
        enum class TextureFormat : uint8_t
        {
            RGBA8,   // 4 bytes/pixel  — sprites, glyphs in color
            R8,      // 1 byte/pixel   — grayscale glyph atlas
            RGBA16F, // 8 bytes/pixel  — HDR render targets, light accumulation
            RG16F,   // 4 bytes/pixel  — normal buffer (X,Y; Z reconstructed)
            R16F,    // 2 bytes/pixel  — 1D shadowmap atlas (occluder distance)
            R8UI,    // 1 byte/pixel   — tile-light index lists, light indices 0..255
            R32UI,   // 4 bytes/pixel  — tile light list offset/count textures
            RGBA32F, // 16 bytes/pixel — light data texture (preserves uint
                     // fields via floatBitsToUint reinterpret in shader)
        };

        enum class TextureFilter : uint8_t
        {
            Linear,  // bilinear — default for UI/HD
            Nearest, // pixel-perfect — for pixel art, integer-indexed lookup textures
        };

        enum class TextureWrap : uint8_t
        {
            Clamp,
            Repeat
        };

        class Texture2D
        {
        public:
            Texture2D() = default;
            ~Texture2D();
            Texture2D(const Texture2D &) = delete;
            Texture2D &operator=(const Texture2D &) = delete;
            Texture2D(Texture2D &&) noexcept;
            Texture2D &operator=(Texture2D &&) noexcept;

            /** Allocate empty storage. Pixel data may be uploaded later via upload(). */
            bool create(int width, int height, TextureFormat fmt = TextureFormat::RGBA8,
                        TextureFilter filter = TextureFilter::Linear,
                        TextureWrap wrap = TextureWrap::Clamp);

            /** Replace a sub-region. Format must match the texture's format.
             *  data is tightly packed with no row stride. */
            void upload(int x, int y, int width, int height, const void *data);

            /** Replace the entire image. Reallocates if dimensions changed. */
            bool uploadFull(int width, int height, TextureFormat fmt, const void *data,
                            TextureFilter filter = TextureFilter::Linear,
                            TextureWrap wrap = TextureWrap::Clamp);

            void setFilter(TextureFilter filter);
            void setWrap(TextureWrap wrap);

            void bind(int unit = 0) const;
            static void unbind(int unit = 0);

            bool isValid() const { return m_id != 0; }
            GLuint id() const { return m_id; }
            int width() const { return m_width; }
            int height() const { return m_height; }
            TextureFormat format() const { return m_format; }

        private:
            GLuint m_id = 0;
            int m_width = 0;
            int m_height = 0;
            TextureFormat m_format = TextureFormat::RGBA8;
        };

    }
}