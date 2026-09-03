#pragma once

#include "../Stage.hpp"
#include "FullscreenPass.hpp"

namespace Bokken
{
    namespace Renderer
    {
        class UserInterfaceStage : public Stage
        {
        public:
            explicit UserInterfaceStage(std::string name = "userInterface")
                : Stage(std::move(name), Kind::Scene) {}

            bool setup() override;
            void execute(const FrameContext &ctx) override;

        private:
            FullscreenPass m_pass;
        };
    }
}