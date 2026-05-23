#pragma once

#include "../Shader.hpp"

#include "glad/gl.h"
#include <SDL3/SDL.h>

#include <string>
#include <unordered_map>
#include <memory>

namespace Bokken
{
    namespace Renderer
    {

        /**
         * Helper for post-process stages. Owns a fullscreen triangle VAO
         * and runs a fragment shader over it sampling some input texture.
         *
         * Shaders can be used in two ways:
         *
         *   1. Pre-compiled: call compile() during setup() with a name and
         *      fragment source. Then call bind() by name in execute(). This
         *      is the preferred path — shaders are compiled exactly once and
         *      never touched again.
         *
         *   2. Lazy path: call beginPass() with raw fragment source.
         *      The shader is compiled on first use and cached by pointer
         *      address. This works but the pre-compiled path avoids
         *      any per-frame string comparison overhead.
         *
         * All compiled shaders share the same built-in fullscreen triangle
         * vertex shader.
         *
         * Why a fullscreen *triangle* instead of two triangles for a quad:
         * a single triangle that overshoots the viewport saves the GPU's
         * triangle setup pipeline a tiny bit and (more practically) avoids
         * a hairline seam down the middle of the screen on some drivers.
         * Standard trick, costs nothing.
        */
        class FullscreenPass
        {
        public:
            ~FullscreenPass();

            bool init();

            /**
             * Pre-compile a named shader. Call during setup(). Returns false
             * if compilation fails.
            */
            bool compile(const char *name, const char *fragmentSrc);

            /**
             * Bind a pre-compiled shader by name. Returns a reference to
             * the shader for setting uniforms. Call draw() after setting
             * uniforms.
            */
            Shader &bind(const char *name);

            /**
             * Lazy path: compile-on-first-use by fragment source pointer.
             * Cached by the raw pointer address so the same const char*
             * literal never recompiles. Prefer compile()/bind() for new code.
            */
            Shader &beginPass(const char *fragmentSrc, const char *debugName);

            /** Draw the fullscreen triangle. */
            void draw();

        private:
            GLuint m_vao = 0;

            // Pre-compiled shaders keyed by name.
            std::unordered_map<std::string, Shader> m_shaders;

            // Lazy cache keyed by source pointer address. This avoids
            // string comparison entirely — if the caller passes the same
            // const char* literal, the pointer is identical across frames.
            std::unordered_map<const void *, Shader> m_ptrCache;

            // Currently bound shader (for bind() return).
            Shader *m_active = nullptr;

            // Fallback empty shader for error paths.
            Shader m_fallback;
        };

    }
}