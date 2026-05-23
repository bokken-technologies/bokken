#pragma once

#include "../Node.hpp"
#include "../../renderer/SpriteBatcher.hpp"
#include "../SimpleStyleSheet.hpp"

#include <memory>

namespace Bokken
{
    namespace Renderer { class SpriteBatcher; }

    namespace Canvas
    {
        namespace Components
        {
            /**
             * View component.
             *
             * The drawing path goes through Canvas::Drawing for
             * rounded fills, gradients, borders and shadows. The
             * measurement and layout responsibilities have moved to
             * Canvas::Layout — View no longer carries onCompute /
             * onLayout callbacks for its own internal flex math.
             *
             * The static `draw()` is still where the per-node visual
             * decisions happen. Parents push the scissor for their
             * subtree before recursing into children when overflow
             * is Hidden — that's done by the Canvas::drawNode walker,
             * not here, so the same View::draw is correct whether the
             * scissor is active or not.
            */
            class View
            {
            public:
                View() = default;
                void setStyle(const SimpleStyleSheet &style) { m_style = style; }
                std::shared_ptr<Node> toNode();

                /**
                 * Emit batcher quads for this node's shadow, background,
                 * border and any transform. `layer` is the base layer for
                 * this node — children get layer+2.
                */
                static void draw(Renderer::SpriteBatcher &batcher,
                                 std::shared_ptr<Node> node,
                                 int layer);

            private:
                SimpleStyleSheet m_style;
            };
        }
    }
}
