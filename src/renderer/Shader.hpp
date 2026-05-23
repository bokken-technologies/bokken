#pragma once

#include <glad/gl.h>
#include <SDL3/SDL.h>

#include <string>
#include <vector>
#include <unordered_map>

namespace Bokken
{
    namespace Renderer
    {

        /**
         * GLSL shader program (vertex + fragment).
         *
         * Owns its program id and a uniform-location cache. Construction
         * compiles+links eagerly; use isValid() to check success — on failure
         * the program id is 0 and bind() is a no-op.
         *
         * Lives entirely on the render thread. Not movable across threads.
        */
        class Shader
        {
        public:
            Shader() = default;
            ~Shader();

            Shader(const Shader &) = delete;
            Shader &operator=(const Shader &) = delete;

            Shader(Shader &&) noexcept;
            Shader &operator=(Shader &&) noexcept;

            /** Compile and link from raw GLSL source. Returns false on error;
             *  errors are also logged via SDL_LogError. */
            bool fromSource(const char *vertexSrc, const char *fragmentSrc,
                            const char *debugName = nullptr);

            bool isValid() const { return m_program != 0; }
            GLuint id() const { return m_program; }

            void bind() const;
            static void unbind();

            /** Uniform setters. Each caches the location on first lookup. */
            void setInt(const char *name, int v);
            void setFloat(const char *name, float v);
            void setVec2(const char *name, float x, float y);
            void setVec3(const char *name, float x, float y, float z);
            void setVec4(const char *name, float x, float y, float z, float w);
            void setMat4(const char *name, const float *col_major16);

        private:
            GLuint m_program = 0;
            mutable std::unordered_map<std::string, GLint> m_locCache;

            GLint locOf(const char *name);
            static GLuint compile(GLenum stage, const char *src, const char *debugName);
        };

    }
}
