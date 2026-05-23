#include "FullscreenPass.hpp"

namespace Bokken
{
    namespace Renderer
    {

        namespace
        {
            // Fullscreen triangle in NDC. gl_VertexID lookup avoids needing
            // any vertex buffer — the GPU computes positions from the index.
            const char *kVS = R"(#version 330 core
                out vec2 v_uv;
                void main() {
                    // Generate a fullscreen triangle from gl_VertexID.
                    //   id=0 -> (-1,-1)  id=1 -> (3,-1)  id=2 -> (-1,3)
                    vec2 pos = vec2((gl_VertexID == 1) ? 3.0 : -1.0,
                                    (gl_VertexID == 2) ? 3.0 : -1.0);
                    v_uv = (pos + 1.0) * 0.5;
                    gl_Position = vec4(pos, 0.0, 1.0);
                }
                )";
        }

        FullscreenPass::~FullscreenPass()
        {
            if (m_vao)
                glDeleteVertexArrays(1, &m_vao);
        }

        bool FullscreenPass::init()
        {
            glGenVertexArrays(1, &m_vao);
            return m_vao != 0;
        }

        bool FullscreenPass::compile(const char *name, const char *fragmentSrc)
        {
            Shader sh;
            if (!sh.fromSource(kVS, fragmentSrc, name))
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "[FullscreenPass] failed to compile shader '%s'", name);
                return false;
            }
            m_shaders[name] = std::move(sh);
            return true;
        }

        Shader &FullscreenPass::bind(const char *name)
        {
            auto it = m_shaders.find(name);
            if (it == m_shaders.end())
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "[FullscreenPass] shader '%s' not compiled", name);
                return m_fallback;
            }
            m_active = &it->second;
            m_active->bind();
            return *m_active;
        }

        Shader &FullscreenPass::beginPass(const char *fragmentSrc, const char *debugName)
        {
            // Cache by the raw pointer address of the source string. Since
            // callers always pass static const char* literals, the address
            // is stable across frames. This avoids both string comparison
            // and recompilation entirely after the first call.
            const void *key = static_cast<const void *>(fragmentSrc);
            auto it = m_ptrCache.find(key);

            if (it == m_ptrCache.end())
            {
                Shader sh;
                if (!sh.fromSource(kVS, fragmentSrc, debugName))
                {
                    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                                 "[FullscreenPass] failed to compile shader '%s'",
                                 debugName ? debugName : "?");
                    return m_fallback;
                }
                auto [inserted, _] = m_ptrCache.emplace(key, std::move(sh));
                m_active = &inserted->second;
            }
            else
            {
                m_active = &it->second;
            }

            m_active->bind();
            return *m_active;
        }

        void FullscreenPass::draw()
        {
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glBindVertexArray(m_vao);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

    }
}