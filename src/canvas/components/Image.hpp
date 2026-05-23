#pragma once

#include "../Node.hpp"
#include "../../AssetPack.hpp"
#include "../../renderer/SpriteBatcher.hpp"
#include "../../renderer/TextureCache.hpp"
#include "../SimpleStyleSheet.hpp"

#include <memory>
#include <string>

namespace Bokken
{
    namespace Renderer
    {
        class SpriteBatcher;
        class TextureCache;
    }

    namespace Canvas
    {
        namespace Components
        {
            /**
             * Image component.
             *
             * Renders a texture loaded from the AssetPack. The asset
             * path goes on `Node::imageSource` (via the `src` JSX prop)
             * — we keep style.backgroundImage for backgrounds on Views.
             *
             * Sizing rules
             * If width/height are explicit, the image is stretched to
             * fill that box. If only one is set, the other is derived
             * from the image's intrinsic aspect ratio. If neither is
             * set, the image renders at its native pixel size.
             *
             * Border radius
             * Drawing::drawImage's documented limitation: rounded raster
             * images need an `overflow:Hidden` parent to clip cleanly.
             * Image itself ignores style.borderRadius for now and emits
             * a square quad — wrap it for rounded thumbnails.
             *
             * Static binding
             * Like Label::s_glyphCache, Image expects a TextureCache to
             * have been wired in by the Renderer module before draw().
             * If it's null we silently skip — same fail-safe pattern.
            */
            class Image
            {
            public:
                explicit Image(const std::string &source) : m_source(source) {}
                void setStyle(const SimpleStyleSheet &s) { m_style = s; }

                std::shared_ptr<Node> toNode();

                static void computeNode(std::shared_ptr<Node> node, AssetPack *assets);
                static void layoutNode(std::shared_ptr<Node> /*node*/) {}

                static void draw(Renderer::SpriteBatcher &batcher,
                                 std::shared_ptr<Node> node,
                                 AssetPack *assets,
                                 int layer);

                /* Wired by the Renderer module. */
                static inline Renderer::TextureCache *s_textureCache = nullptr;

            private:
                std::string m_source;
                SimpleStyleSheet m_style;
            };
        }
    }
}
