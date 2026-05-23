#pragma once

#include "../Node.hpp"
#include "../../AssetPack.hpp"
#include "../../renderer/SpriteBatcher.hpp"
#include "../../renderer/GlyphCache.hpp"
#include "../../renderer/Texture2D.hpp"
#include "../SimpleStyleSheet.hpp"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <string>
#include <memory>
#include <map>
#include <vector>
#include <functional>

namespace Bokken
{
    namespace Renderer
    {
        class SpriteBatcher;
        class GlyphCache;
    }

    namespace Canvas
    {
        namespace Components
        {
            /**
             * Text component.
             *
             * Typography controls:
             *   - textAlign:    left / center / right horizontal placement
             *                   inside the Label's content box.
             *   - lineHeight:   multiplier on the font's natural line
             *                   height; controls vertical line stride.
             *   - letterSpacing: pixels of extra advance between glyphs.
             *   - wordWrap:     when true and the Label has a fixed width,
             *                   text breaks on whitespace into multiple
             *                   lines that flow within the content box.
             *
             * Word wrap is computed on demand in both computeNode and
             * draw using the same logic, rather than cached on an
             * instance. The per-line break decisions are deterministic
             * from style+text+width, so recomputing is safe. For very
             * long text this could be cached, but typical UI labels are
             * short and the wrap pass is O(N).
            */
            class Label
            {
            public:
                Label(const std::string &text) : m_text(text) {}
                void setStyle(const SimpleStyleSheet &s) { m_style = s; }

                std::shared_ptr<Node> toNode();

                static void computeNode(std::shared_ptr<Node> node, AssetPack *assets);
                static void layoutNode(std::shared_ptr<Node> node);

                static void draw(Renderer::SpriteBatcher &batcher,
                                 std::shared_ptr<Node> node,
                                 AssetPack *assets,
                                 int layer);

                static TTF_Font *get_font(const std::string &p, float s, AssetPack *a);
                static void clear_font_cache();

                /* Wired by the Renderer module so draw() can find the
                 * engine GlyphCache without a per-call argument. */
                static inline Renderer::GlyphCache *s_glyphCache = nullptr;

                /* Word-break a string into lines that fit within
                 * `maximumWidth`. Greedy algorithm: pack as many words as
                 * fit, break on whitespace, hard-break inside a word
                 * only if it alone exceeds maximumWidth. Made public so
                 * TextInput can reuse it for its own caret logic later. */
                static std::vector<std::string> wrapLines(const std::string &text,
                                                           TTF_Font *font,
                                                           float maximumWidth,
                                                           float letterSpacing);

            private:
                std::string m_text;
                SimpleStyleSheet m_style;
                static inline std::map<std::string, TTF_Font *> s_measureFonts;
            };
        }
    }
}
