#pragma once

#include "../Node.hpp"
#include "../../renderer/SpriteBatcher.hpp"
#include "../SimpleStyleSheet.hpp"

#include <memory>

namespace Bokken
{
    namespace Canvas
    {
        namespace Components
        {
            /**
             * ScrollView component.
             *
             * Scroll mechanics
             * - Layout::arrange skips main-axis shrink for ScrollView
             *   children: they keep their preferred sizes and overflow
             *   the container. ScrollView::layoutNode runs after arrange
             *   completes (so direct children are positioned), measures
             *   the union bounding box, and writes scrollMaxX/Y to the
             *   node.
             * - drawNode pushes a scissor matching the ScrollView's
             *   content rect and translates each child by (-scrollX,
             *   -scrollY) at draw time. The children's stored layout
             *   coordinates are not modified.
             *
             * Scrollbar
             * Always visible when content overflows (Discord-style):
             *   - 8px-wide vertical track on the right edge
             *   - thumb darkens on hover and during drag
             *   - clicking-and-dragging the thumb scrolls proportionally
             *   - the same applies to a horizontal scrollbar at the
             *     bottom when scrollMaxX > 0.
             *
             * The drag-tracking state (which scrollbar is held, the
             * grab offset relative to the thumb) lives in static maps
             * keyed on Node* — same pattern as TextInput's caret. This
             * keeps the Node struct lean and makes scrollbar state
             * free to discard when the tree is replaced.
            */
            class ScrollView
            {
            public:
                ScrollView() = default;
                void setStyle(const SimpleStyleSheet &s) { m_style = s; }
                std::shared_ptr<Node> toNode();

                static void layoutNode(std::shared_ptr<Node> node);

                static void draw(Renderer::SpriteBatcher &batcher,
                                 std::shared_ptr<Node> node, int layer);

                static void onWheel(std::shared_ptr<Node> node, float dx, float dy);

                /* Mouse interaction with the scrollbar. The Canvas module
                 * calls these from its event handler when a press/release
                 * lands on a ScrollView and motion happens while
                 * dragging. They return true if the event was consumed. */
                static bool onMouseDown(std::shared_ptr<Node> node, float mx, float my);
                static void onMouseMove(std::shared_ptr<Node> node, float mx, float my);
                static void onMouseUp(std::shared_ptr<Node> node);

                /* True if any ScrollView is currently being dragged.
                 * Canvas uses this to suppress click delivery while the
                 * mouse is held down on a scrollbar. */
                static bool isDragging();

                /* Get the ScrollView whose thumb is being held, or null.
                 * Canvas routes mouse-move events here. */
                static std::shared_ptr<Node> draggingNode();

                /* Hit-test the scrollbar thumbs to know if a hover
                 * should color them. Returns 0 = none, 1 = vertical,
                 * 2 = horizontal. */
                static int hitTestThumb(std::shared_ptr<Node> node, float mx, float my);

            private:
                SimpleStyleSheet m_style;
            };
        }
    }
}

