#include "Texture2D.hpp"

#include <SDL3/SDL.h>

namespace Bokken
{
    namespace Renderer
    {

        namespace
        {
            struct GLFormat
            {
                GLenum internalFormat;
                GLenum format;
                GLenum type;
                int bytesPerPixel;
            };

            const char *formatName(TextureFormat f)
            {
                switch (f)
                {
                case TextureFormat::RGBA8:   return "RGBA8";
                case TextureFormat::R8:      return "R8";
                case TextureFormat::RGBA16F: return "RGBA16F";
                case TextureFormat::RG16F:   return "RG16F";
                case TextureFormat::R16F:    return "R16F";
                case TextureFormat::R8UI:    return "R8UI";
                case TextureFormat::R32UI:   return "R32UI";
                case TextureFormat::RGBA32F: return "RGBA32F";
                }
                return "?";
            }

            GLFormat resolveFormat(TextureFormat f)
            {
                switch (f)
                {
                case TextureFormat::RGBA8:
                    return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
                case TextureFormat::R8:
                    return {GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1};
                case TextureFormat::RGBA16F:
                    return {GL_RGBA16F, GL_RGBA, GL_FLOAT, 8};
                case TextureFormat::RG16F:
                    return {GL_RG16F, GL_RG, GL_FLOAT, 4};
                case TextureFormat::R16F:
                    return {GL_R16F, GL_RED, GL_FLOAT, 2};
                case TextureFormat::R8UI:
                    return {GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, 1};
                case TextureFormat::R32UI:
                    return {GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, 4};
                case TextureFormat::RGBA32F:
                    return {GL_RGBA32F, GL_RGBA, GL_FLOAT, 16};
                }
                return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
            }

            GLint resolveFilter(TextureFilter f)
            {
                return f == TextureFilter::Nearest ? GL_NEAREST : GL_LINEAR;
            }
            GLint resolveWrap(TextureWrap w)
            {
                return w == TextureWrap::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
            }

            // Returns the driver's GL_MAX_TEXTURE_SIZE, cached after the
            // first call. Used for upfront dimension validation so we
            // can fail with a meaningful message instead of letting
            // glTexImage2D emit GL_INVALID_VALUE and silently leave the
            // texture in an unloadable state.
            GLint queryMaxTextureSize()
            {
                static GLint cached = 0;
                if (cached == 0)
                {
                    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &cached);
                    if (cached <= 0)
                        cached = 2048; // ultra-conservative fallback
                }
                return cached;
            }
        }

        Texture2D::~Texture2D()
        {
            if (m_id)
                glDeleteTextures(1, &m_id);
        }

        Texture2D::Texture2D(Texture2D &&o) noexcept
            : m_id(o.m_id), m_width(o.m_width), m_height(o.m_height), m_format(o.m_format)
        {
            o.m_id = 0;
            o.m_width = o.m_height = 0;
        }

        Texture2D &Texture2D::operator=(Texture2D &&o) noexcept
        {
            if (this != &o)
            {
                if (m_id)
                    glDeleteTextures(1, &m_id);
                m_id = o.m_id;
                m_width = o.m_width;
                m_height = o.m_height;
                m_format = o.m_format;
                o.m_id = 0;
                o.m_width = o.m_height = 0;
            }
            return *this;
        }

        bool Texture2D::create(int width, int height, TextureFormat fmt,
                               TextureFilter filter, TextureWrap wrap)
        {
            if (width <= 0 || height <= 0)
                return false;

            // Upfront dimension check. Fails *before* glTexImage2D so
            // we can report which call site asked for the bogus size,
            // rather than letting the driver silently leave us with
            // a zero-storage texture handle that produces black
            // sampling and an "unloadable" warning at draw time.
            const GLint maxSize = queryMaxTextureSize();
            if (width > maxSize || height > maxSize)
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "[Texture2D::create] %dx%d (fmt=%s) exceeds "
                             "GL_MAX_TEXTURE_SIZE=%d; allocation refused",
                             width, height, formatName(fmt), maxSize);
                return false;
            }

            if (!m_id)
                glGenTextures(1, &m_id);

            m_width = width;
            m_height = height;
            m_format = fmt;
            const GLFormat gf = resolveFormat(fmt);

            // Drain any pre-existing GL error so we attribute correctly.
            while (glGetError() != GL_NO_ERROR) {}

            glBindTexture(GL_TEXTURE_2D, m_id);
            glPixelStorei(GL_UNPACK_ALIGNMENT, gf.bytesPerPixel == 1 ? 1 : 4);
            glTexImage2D(GL_TEXTURE_2D, 0, gf.internalFormat, width, height, 0,
                         gf.format, gf.type, nullptr);

            const GLenum err = glGetError();
            if (err != GL_NO_ERROR)
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "[Texture2D::create] glTexImage2D failed "
                             "%dx%d fmt=%s err=0x%04X; deleting handle",
                             width, height, formatName(fmt), err);
                glDeleteTextures(1, &m_id);
                m_id = 0;
                m_width = m_height = 0;
                return false;
            }

            const GLint flt = resolveFilter(filter);
            const GLint wr = resolveWrap(wrap);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, flt);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, flt);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            return true;
        }

        void Texture2D::upload(int x, int y, int width, int height, const void *data)
        {
            if (!m_id || !data)
                return;
            const GLFormat gf = resolveFormat(m_format);
            glBindTexture(GL_TEXTURE_2D, m_id);
            glPixelStorei(GL_UNPACK_ALIGNMENT, gf.bytesPerPixel == 1 ? 1 : 4);
            glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, gf.format, gf.type, data);
        }

        bool Texture2D::uploadFull(int width, int height, TextureFormat fmt, const void *data,
                                   TextureFilter filter, TextureWrap wrap)
        {
            if (width <= 0 || height <= 0)
                return false;

            const GLint maxSize = queryMaxTextureSize();
            if (width > maxSize || height > maxSize)
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "[Texture2D::uploadFull] %dx%d (fmt=%s) exceeds "
                             "GL_MAX_TEXTURE_SIZE=%d; allocation refused",
                             width, height, formatName(fmt), maxSize);
                return false;
            }

            if (!m_id)
                glGenTextures(1, &m_id);

            m_width = width;
            m_height = height;
            m_format = fmt;
            const GLFormat gf = resolveFormat(fmt);

            while (glGetError() != GL_NO_ERROR) {}

            glBindTexture(GL_TEXTURE_2D, m_id);
            glPixelStorei(GL_UNPACK_ALIGNMENT, gf.bytesPerPixel == 1 ? 1 : 4);
            glTexImage2D(GL_TEXTURE_2D, 0, gf.internalFormat, width, height, 0,
                         gf.format, gf.type, data);

            const GLenum err = glGetError();
            if (err != GL_NO_ERROR)
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "[Texture2D::uploadFull] glTexImage2D failed "
                             "%dx%d fmt=%s err=0x%04X; deleting handle",
                             width, height, formatName(fmt), err);
                glDeleteTextures(1, &m_id);
                m_id = 0;
                m_width = m_height = 0;
                return false;
            }

            const GLint flt = resolveFilter(filter);
            const GLint wr = resolveWrap(wrap);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, flt);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, flt);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            return true;
        }

        void Texture2D::setFilter(TextureFilter filter)
        {
            if (!m_id)
                return;
            const GLint flt = resolveFilter(filter);
            glBindTexture(GL_TEXTURE_2D, m_id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, flt);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, flt);
        }

        void Texture2D::setWrap(TextureWrap wrap)
        {
            if (!m_id)
                return;
            const GLint wr = resolveWrap(wrap);
            glBindTexture(GL_TEXTURE_2D, m_id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wr);
        }

        void Texture2D::bind(int unit) const
        {
            if (!m_id)
                return;
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, m_id);
        }

        void Texture2D::unbind(int unit)
        {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

    }
}