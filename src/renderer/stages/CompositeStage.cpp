#include "CompositeStage.hpp"

namespace Bokken
{
    namespace Renderer
    {
        namespace
        {
            /* Clean passthrough shader. */
            const char *kFS = R"(#version 330 core
                in vec2 v_uv;
                uniform sampler2D u_input;
                out vec4 FragColor;

                void main() {
                    FragColor = texture(u_input, v_uv);
                }
            )";
        }

        bool CompositeStage::setup()
        {
            return m_pass.init();
        }

        void CompositeStage::execute(const FrameContext &ctx)
        {
            if (!ctx.inputTarget || !ctx.outputTarget)
                return;

            ctx.outputTarget->bind();
            glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);

            ctx.inputTarget->color().bind(0);
            auto &sh = m_pass.beginPass(kFS, "composite");
            sh.setInt("u_input", 0);

            m_pass.draw();
        }
    }
}