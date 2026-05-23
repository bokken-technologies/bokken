#include "Shader.hpp"

namespace Bokken
{
    namespace Renderer
    {

        Shader::~Shader()
        {
            if (m_program)
                glDeleteProgram(m_program);
        }

        Shader::Shader(Shader &&other) noexcept
            : m_program(other.m_program), m_locCache(std::move(other.m_locCache))
        {
            other.m_program = 0;
        }

        Shader &Shader::operator=(Shader &&other) noexcept
        {
            if (this != &other)
            {
                if (m_program)
                    glDeleteProgram(m_program);
                m_program = other.m_program;
                m_locCache = std::move(other.m_locCache);
                other.m_program = 0;
            }
            return *this;
        }

        GLuint Shader::compile(GLenum stage, const char *src, const char *debugName)
        {
            GLuint sh = glCreateShader(stage);
            glShaderSource(sh, 1, &src, nullptr);
            glCompileShader(sh);

            GLint ok = 0;
            glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                GLint len = 0;
                glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
                std::vector<char> log(len > 0 ? len : 1, '\0');
                if (len > 0)
                    glGetShaderInfoLog(sh, len, nullptr, log.data());
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "[Shader:%s] %s compile failed:\n%s",
                             debugName ? debugName : "?",
                             stage == GL_VERTEX_SHADER ? "VS" : "FS",
                             log.data());
                glDeleteShader(sh);
                return 0;
            }
            return sh;
        }

        bool Shader::fromSource(const char *vertexSrc, const char *fragmentSrc, const char *debugName)
        {
            GLuint vs = compile(GL_VERTEX_SHADER, vertexSrc, debugName);
            GLuint fs = compile(GL_FRAGMENT_SHADER, fragmentSrc, debugName);
            if (!vs || !fs)
            {
                if (vs)
                    glDeleteShader(vs);
                if (fs)
                    glDeleteShader(fs);
                return false;
            }

            GLuint prog = glCreateProgram();
            glAttachShader(prog, vs);
            glAttachShader(prog, fs);
            glLinkProgram(prog);

            // The shader objects can be deleted after linking — the program
            // retains its own copies.
            glDeleteShader(vs);
            glDeleteShader(fs);

            GLint ok = 0;
            glGetProgramiv(prog, GL_LINK_STATUS, &ok);
            if (!ok)
            {
                GLint len = 0;
                glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
                std::vector<char> log(len > 0 ? len : 1, '\0');
                if (len > 0)
                    glGetProgramInfoLog(prog, len, nullptr, log.data());
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "[Shader:%s] link failed:\n%s",
                             debugName ? debugName : "?", log.data());
                glDeleteProgram(prog);
                return false;
            }

            if (m_program)
                glDeleteProgram(m_program);
            m_program = prog;
            m_locCache.clear();
            return true;
        }

        void Shader::bind() const
        {
            if (m_program)
                glUseProgram(m_program);
        }

        void Shader::unbind()
        {
            glUseProgram(0);
        }

        GLint Shader::locOf(const char *name)
        {
            if (!m_program)
                return -1;
            auto it = m_locCache.find(name);
            if (it != m_locCache.end())
                return it->second;
            GLint loc = glGetUniformLocation(m_program, name);
            m_locCache.emplace(name, loc);
            return loc;
        }

        void Shader::setInt(const char *name, int v)
        {
            GLint l = locOf(name);
            if (l >= 0)
                glUniform1i(l, v);
        }
        void Shader::setFloat(const char *name, float v)
        {
            GLint l = locOf(name);
            if (l >= 0)
                glUniform1f(l, v);
        }
        void Shader::setVec2(const char *name, float x, float y)
        {
            GLint l = locOf(name);
            if (l >= 0)
                glUniform2f(l, x, y);
        }
        void Shader::setVec3(const char *name, float x, float y, float z)
        {
            GLint l = locOf(name);
            if (l >= 0)
                glUniform3f(l, x, y, z);
        }
        void Shader::setVec4(const char *name, float x, float y, float z, float w)
        {
            GLint l = locOf(name);
            if (l >= 0)
                glUniform4f(l, x, y, z, w);
        }
        void Shader::setMat4(const char *name, const float *m)
        {
            GLint l = locOf(name);
            if (l >= 0)
                glUniformMatrix4fv(l, 1, GL_FALSE, m);
        }

    }
}
