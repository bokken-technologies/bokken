#pragma once

#include "../Node.hpp"
#include "../../renderer/SpriteBatcher.hpp"
#include "../SimpleStyleSheet.hpp"
#include "Label.hpp"
#include "View.hpp"

#include <memory>
#include <string>

namespace Bokken
{
    namespace Canvas
    {
        namespace Components
        {
            /**
             * TextInput component.
             *
             * Single-line editable text. The Canvas module routes
             * keyboard and SDL_EVENT_TEXT_INPUT events here when this
             * node is focused. We maintain:
             *   - node->value:        the canonical text content (the
             *                         "form value")
             *   - node->textContent:  what we render (currently == value,
             *                         but we keep them split for future
             *                         IME composition support)
             *   - caretIndex:         byte index of the caret within
             *                         value, stored on the node via the
             *                         m_caretByNode static map (see .cpp)
             *
             * Visual treatment
             * Inherits styling from View. Adds:
             *   - placeholder text drawn at half opacity when value is
             *     empty
             *   - blinking caret line when focused
             *   - a focus ring (drawn as a 2px border in style.color
             *     when focused, only if borderWidth was 0; otherwise
             *     we leave the user's border alone)
             *
             * Note on selection
             * Selection (drag to select) is not implemented in this
             * pass. Users can still home/end/backspace/delete, type to
             * insert, and arrow-key the caret. Adding selection means
             * a second per-node mutable state (selStart/selEnd) and a
             * highlight-rect render — straightforward extension.
            */
            class TextInput
            {
            public:
                TextInput() = default;
                void setStyle(const SimpleStyleSheet &s) { m_style = s; }
                void setValue(const std::string &v) { m_initialValue = v; }
                void setPlaceholder(const std::string &p) { m_placeholder = p; }

                std::shared_ptr<Node> toNode();

                /**
                 * Set the same defaults that toNode() applies to a node
                 * created directly in C++. Called by the JSX bridge in
                 * synchronize_tree so JSX-constructed TextInputs are
                 * focusable, get an I-beam cursor, and have readable
                 * padding / minimum size — without this they end up
                 * with tabIndex=-1 (not focusable) and clicking them
                 * does nothing.
                */
                static void applyDefaults(SimpleStyleSheet &s);

                static void computeNode(std::shared_ptr<Node> node, AssetPack *assets);
                static void draw(Renderer::SpriteBatcher &batcher,
                                 std::shared_ptr<Node> node,
                                 AssetPack *assets,
                                 int layer);

                /* Insert UTF-8 text at the caret position. Called by the
                 * Canvas module when SDL_EVENT_TEXT_INPUT fires while
                 * this node is focused. */
                static void insertText(std::shared_ptr<Node> node, const std::string &utf8);

                /* Handle special keys (backspace, delete, arrows, home,
                 * end). Returns true if the key was consumed. */
                static bool handleKey(std::shared_ptr<Node> node, int sdlScancode);

                /* Caret bookkeeping. Index is in BYTES into node->value
                 * (we keep UTF-8 well-formed by always advancing past
                 * full codepoints). */
                static int  getCaret(const std::shared_ptr<Node> &node);
                static void setCaret(std::shared_ptr<Node> node, int byteIndex);

                /* Placeholder lookup — stored on the node via a
                 * static map keyed on node pointer. The JS bridge sets
                 * this when parsing a TextInput element. */
                static void setPlaceholderFor(std::shared_ptr<Node> node, const std::string &p);

                /* Advance the caret blink phase for the focused input.
                 * Called once per frame from Canvas::update with the
                 * frame's dt. Cycle is 1 second; <0.5 shows, >=0.5
                 * hides — caret toggles every half second. */
                static void tickBlink(std::shared_ptr<Node> node, float dt);

                /* Resize-grip hit testing and drag. Canvas's mouse
                 * routing calls these in priority order: hitTestGrip
                 * on a TextInput with resize enabled returns true if
                 * (x, y) lands on the grip; the caller then calls
                 * beginResize / dragResize / endResize during the
                 * drag. The override mutates node->layout.w/h
                 * directly — it survives until the next Layout::run
                 * (i.e. until setState triggers a re-render). */
                static bool hitTestGrip(std::shared_ptr<Node> node, float mx, float my);
                static void beginResize(std::shared_ptr<Node> node, float mx, float my);
                static void dragResize(std::shared_ptr<Node> node, float mx, float my);
                static void endResize(std::shared_ptr<Node> node);
                static bool isResizing(std::shared_ptr<Node> node);

            private:
                SimpleStyleSheet m_style;
                std::string m_initialValue;
                std::string m_placeholder;
            };
        }
    }
}
