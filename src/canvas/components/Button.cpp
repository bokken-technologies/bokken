#include "Button.hpp"

namespace Bokken
{
    namespace Canvas
    {
        namespace Components
        {
            void Button::applyDefaults(SimpleStyleSheet &s)
            {
                /* Only fill in fields the user didn't override. The
                 * heuristic for "didn't override" is "still at the SSS
                 * default" — which is fine for these specific fields
                 * because they don't have a meaningful zero default
                 * for Button (a hoverScale of 1.0 means "no bump",
                 * which a Button by convention does want). */
                if (s.hoverScale  == 1.0f)  s.hoverScale  = 1.02f;
                if (s.activeScale == 0.95f) s.activeScale = 0.96f;
                if (s.cursor      == Cursor::Default) s.cursor = Cursor::Pointer;
                if (s.tabIndex    == -1)    s.tabIndex    = 0;
                if (s.transitionDuration == 0.0f)
                {
                    s.transitionDuration = 0.12f;
                    s.transitionTiming   = Timing::EaseOut;
                }
                /* A reasonable default padding/cornering for a button
                 * with no styling at all — enough that an empty Button
                 * isn't invisible. Users typically override these. */
                if (s.padding == 0.0f && s.paddingLeft != s.paddingLeft /*NaN check*/)
                {
                    s.padding = 8.0f;
                }
                if (s.borderRadius == 0.0f)
                    s.borderRadius = 4.0f;
            }

            std::shared_ptr<Node> Button::toNode()
            {
                auto node = std::make_shared<Node>("Button");
                node->style = m_style;
                applyDefaults(node->style);
                return node;
            }
        }
    }
}
