#pragma once

#include "../Node.hpp"
#include "../../renderer/SpriteBatcher.hpp"
#include "View.hpp"

#include <memory>

namespace Bokken
{
    namespace Canvas
    {
        namespace Components
        {
            /**
             * Button component.
             *
             * Functionally a View with default interaction semantics:
             *   - Cursor::Pointer on hover
             *   - hoverScale = 1.02, activeScale = 0.96 (subtle bump,
             *     overridable by the user's style)
             *   - tabIndex = 0 (focusable by default)
             *   - transitionDuration = 0.12s with EaseOut
             *
             * The reason this is its own component rather than just
             * "View with onClick" is so the JS bridge can detect Button
             * specifically and apply these defaults at parse time —
             * users can still override every property explicitly. Visual
             * rendering goes straight through View::draw; we just set
             * up the node and let the same draw path handle it.
            */
            class Button
            {
            public:
                Button() = default;
                void setStyle(const SimpleStyleSheet &s) { m_style = s; }

                std::shared_ptr<Node> toNode();

                /* Reuses View's drawing — Button is purely a semantic
                 * wrapper at the node level. */
                static void draw(Renderer::SpriteBatcher &batcher,
                                 std::shared_ptr<Node> node, int layer)
                {
                    View::draw(batcher, node, layer);
                }

                /* Apply Button's style defaults onto an existing SSS,
                 * filling in only fields the user didn't set. Used by
                 * the JS bridge after parsing a Button's style prop. */
                static void applyDefaults(SimpleStyleSheet &s);

            private:
                SimpleStyleSheet m_style;
            };
        }
    }
}
