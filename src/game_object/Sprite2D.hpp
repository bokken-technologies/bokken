#pragma once

#include "Component.hpp"
#include "../renderer/SpriteBatcher.hpp"

#include <glm/glm.hpp>

#include <string>

namespace Bokken
{
    namespace GameObject
    {
        /**
         * Visual representation using a texture or texture atlas region.
         *
         * Unlike Mesh2D (which draws solid-color primitives), Sprite2D
         * draws a textured quad sourced from the TextureCache. The region
         * can reference the full texture or a named sub-rectangle defined
         * via TextureCache::defineRegion() / defineGrid().
         *
         * When both Sprite2D and Mesh2D exist on the same GameObject,
         * Sprite2D takes priority for rendering.
         *
         * The source is resolved lazily at render time against the
         * TextureCache — no GL resources are held by this component.
        */
        class Sprite2D : public Component
        {
        public:
            // Path to the texture in the asset pack VFS.
            // If regionName is empty, the full texture is drawn.
            std::string source;

            // Named region within the texture (from TextureCache::defineRegion).
            // Empty string means "use the full texture".
            std::string regionName;

            // Tint colour multiplied with the texture sample.
            glm::vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};

            // Overall opacity (multiplied into tint.a at draw time).
            float opacity = 1.0f;

            bool flipX = false;
            bool flipY = false;

            // Pixel dimensions to draw at. If both are zero, the source
            // region's pixel size is used (1:1 mapping at the camera's
            // current zoom). Set one to zero and the other non-zero to
            // preserve aspect ratio.
            float overrideWidth = 0.0f;
            float overrideHeight = 0.0f;

            // Anchor point for positioning, in normalised [0,1] coordinates.
            // (0.5, 0.5) = center (default), (0, 0) = top-left.
            float anchorX = 0.5f;
            float anchorY = 0.5f;

            // Blend mode for this sprite. Alpha is standard transparency,
            // Additive for glowing/emissive sprites, Screen for soft glows.
            Bokken::Renderer::BlendMode blendMode = Bokken::Renderer::BlendMode::Alpha;
        };
    }
}