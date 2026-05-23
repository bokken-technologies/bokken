#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace Bokken
{
    namespace Renderer
    {
        namespace Lighting
        {

            /**
             * Bit flags packed into Light::flags. Reserved value 0 is
             * a disabled / dead light — the lighting fragment shader
             * short-circuits to "no contribution" when (flags & ENABLED)
             * is zero, so culled or destroyed lights don't need to be
             * compacted out of the upload buffer.
             *
             * Type bits occupy the low two bits (0..3) leaving room for
             * up to four light types. Currently used: 0 Point, 1 Spot,
             * 2 Directional. Type 3 is reserved for future Area lights.
            */
            enum LightFlags : uint32_t
            {
                LIGHT_FLAG_ENABLED       = 1u << 2,
                LIGHT_FLAG_CASTS_SHADOWS = 1u << 3,
                LIGHT_FLAG_HAS_COOKIE    = 1u << 4,

                LIGHT_TYPE_MASK = 0x3u,
                LIGHT_TYPE_POINT       = 0u,
                LIGHT_TYPE_SPOT        = 1u,
                LIGHT_TYPE_DIRECTIONAL = 2u,
            };

            /**
             * Sentinel for "no shadow slot allocated" and "no cookie
             * slot allocated". The lighting shader treats either of
             * these as "skip the corresponding sample" without further
             * branching.
            */
            static constexpr uint32_t LIGHT_NO_SLOT = 0xFFFFFFFFu;

            /**
             * GPU-side light record. Uploaded into an RGBA32F 2D texture
             * by LightUploadBuffer; the lighting fragment shader pulls
             * each light's five rows via texelFetch indexed by light
             * number.
             *
             * Texture-of-lights was chosen over a std140 UBO because the
             * GL 3.3 spec minimum UBO size is 16 KB — at 80 bytes per
             * light that caps at 204 lights, below the artist-target.
             * RGBA32F textures have no analogous minimum (GL_MAX_TEXTURE_SIZE
             * is 1024+ on every conforming driver, giving us 50,000+
             * lights worth of headroom). Lookup is `texelFetch(texture,
             * ivec2(row, lightIndex), 0)` — five fetches per light, no
             * filtering, identical performance to UBO field reads on
             * modern hardware.
             *
             * The layout is five rows of vec4 per light, indexed by
             * texture x-coordinate 0..4. The y-coordinate is the light
             * index. This matches the std140 layout the shader would
             * have used in the UBO path, so swapping back to a UBO in
             * a future driver-min bump would only require renaming the
             * lookup function.
             *
             * Row 0: position.xy, direction.xy        (16 B)
             * Row 1: color.rgb, intensity              (16 B)
             * Row 2: range, falloffExponent, innerConeCos, outerConeCos (16 B)
             * Row 3: flags (uint), shadowSlot (uint), cookieSlot (uint), _pad (16 B)
             * Row 4: cookieUVOffset.xy, cookieUVScale.xy (16 B)
             *
             * The flags/shadowSlot/cookieSlot uints are stored bitwise-
             * identical in float texels via memcpy; the shader does
             * floatBitsToUint() on the fetched RGBA32F components to
             * recover the integer values. This avoids the need for a
             * second integer texture and keeps everything in one
             * coherent upload.
             *
             * Field meanings:
             *   position   — world-space, pixels. Resolved each frame
             *                from the owning GameObject's Transform2D.
             *                For directional lights this is unused but
             *                left as the GameObject's position for
             *                consistency / future use.
             *   direction  — unit vector for spot/directional lights.
             *                Pointing "away from" the light source for
             *                directional (matches the convention used
             *                in N·L: dot(normal, -direction)).
             *   color      — linear RGB in [0, +inf). Values >1 are
             *                expected and used for HDR sources.
             *   intensity  — scalar multiplier on color. Separated from
             *                color so animation envelopes can modulate
             *                brightness without disturbing hue.
             *   range      — radius in pixels at which falloff reaches
             *                zero. Ignored for directional lights.
             *   falloffExp — 1.0 = linear, 2.0 = quadratic. The shader
             *                computes attenuation = pow(saturate(1 -
             *                d/range), falloffExp).
             *   innerCone  — cosine of the inner cone half-angle for
             *                spotlights. Inside the inner cone the
             *                intensity is full. Ignored for non-spots.
             *   outerCone  — cosine of the outer cone half-angle.
             *                Between inner and outer the intensity is
             *                smoothstep-faded.
             *   flags      — see LightFlags.
             *   shadowSlot — index into the shadowmap atlas for this
             *                light, or LIGHT_NO_SLOT if it doesn't
             *                cast shadows or none were available.
             *   cookieSlot — index into the cookie atlas, or
             *                LIGHT_NO_SLOT.
             *   softness   — PCF kernel radius multiplier for this
             *                light's shadow sampling. 1.0 = the
             *                renderer default kernel, larger values
             *                soften the shadow edge, smaller values
             *                sharpen it. Per-light rather than per-
             *                caster because the atlas only stores the
             *                nearest occluder distance — switching to
             *                per-caster softness needs a secondary
             *                atlas channel (deferred).
             *   cookieUV*  — UV transform applied when sampling the
             *                cookie. Scrolling cookies update the
             *                offset each frame; tiling cookies use a
             *                scale > 1.
            */
            struct Light
            {
                glm::vec2 position{0.0f};
                glm::vec2 direction{0.0f, -1.0f};

                glm::vec3 color{1.0f};
                float     intensity = 1.0f;

                float     range = 256.0f;
                float     falloffExponent = 2.0f;
                float     innerConeCos = 1.0f;
                float     outerConeCos = 1.0f;

                uint32_t  flags = LIGHT_FLAG_ENABLED | LIGHT_TYPE_POINT;
                uint32_t  shadowSlot = LIGHT_NO_SLOT;
                uint32_t  cookieSlot = LIGHT_NO_SLOT;
                float     softness = 1.0f;

                glm::vec2 cookieUVOffset{0.0f};
                glm::vec2 cookieUVScale{1.0f};
            };

            // Compile-time size check so the GPU texture layout stays
            // in sync with the C++ struct. If this static_assert fires,
            // either the C++ struct gained or lost a field or alignment
            // shifted — the matching layout in the lighting fragment
            // shader (the five texelFetches per light) must be updated
            // to match before merging.
            static_assert(sizeof(Light) == 80,
                          "Light struct must be exactly 80 bytes (5 vec4 "
                          "rows) to match the light-data texture layout. "
                          "Update the lighting fragment shader's per-row "
                          "texelFetch decoding if you change this.");

            /**
             * World-space → render-target-pixel-space affine transform.
             *
             * The lighting subsystem operates entirely in render-target
             * pixels: TileLightGrid::bin clamps light AABBs against the
             * render viewport, the lighting fragment shader does
             * `delta = L.position - fragPosPx` where fragPosPx is in
             * pixels, the shadow rasterizer treats segment endpoints
             * and the light position as the same flat 2D coordinate
             * system. Anything fed into Light::position,
             * ShadowSegment::a/b, or Light::range must therefore live
             * in that space.
             *
             * Game objects, on the other hand, live in world units —
             * decoupled from any specific render resolution, scaled
             * into pixels per Camera2D::zoom (pixels-per-world-unit)
             * each frame. This struct captures the per-frame transform
             * so per-light / per-caster snapshot code can convert
             * itself without having to re-walk the active camera.
             *
             * Resolved once per frame by Frame::gatherIfNeeded and
             * threaded through to Light2D::snapshot and
             * ShadowCaster2D::emit.
            */
            struct WorldToScreen
            {
                // Active camera's world-space position. Anything at
                // exactly (cameraX, cameraY) maps to the centre of the
                // render target.
                float cameraX = 0.0f;
                float cameraY = 0.0f;

                // Pixels per world unit. Same value the sprite render
                // path multiplies world distances by; using anything
                // else here would let lights drift relative to their
                // attached sprites as the camera zooms.
                float pixelsPerUnit = 64.0f;

                // Half the render target's width / height in pixels.
                // World origin (after camera translation) lands at
                // (halfW, halfH) — the centre of the render target —
                // because the engine's screen-space convention is
                // y-down with origin at the top-left.
                float halfW = 0.0f;
                float halfH = 0.0f;

                // Apply the transform to a world-space point.
                //
                // The sprite render path uses an ortho projection with
                // a Y flip (SpriteBatcher's orthoTopLeft sets out[5] =
                // -2/h), so a sprite written with screenY=0 displays
                // at the TOP of the visible screen. Sprite code reads
                // naturally in top-left-origin pixel coordinates.
                //
                // The lighting shader has no such flip. Its v_uv is
                // generated by a fullscreen triangle in NDC where
                // (-1, -1) → (0, 0) and (1, 1) → (1, 1). NDC y=-1 is
                // the BOTTOM of the framebuffer, so fragPosPx.y=0
                // is sampled at the bottom of the displayed screen,
                // and fragPosPx.y=screenH is sampled at the top.
                //
                // Visually, a sprite at the same numeric pixel-Y as a
                // light gets drawn at opposite ends of the screen.
                // For lights to align with their owning sprites, the
                // mapping has to invert: higher worldY → higher
                // lightY in screen-pixel space (no flip), because
                // higher fragPosPx.y is the top of the screen where
                // the matching sprite was drawn by the sprite path's
                // flipped projection.
                //
                // This means the formula here intentionally diverges
                // from GameObject::present's screenY = halfH -
                // worldY*ppu — because the rendering pipelines down-
                // stream of each handle Y oppositely.
                glm::vec2 apply(glm::vec2 worldPos) const
                {
                    return glm::vec2(
                        halfW + (worldPos.x - cameraX) * pixelsPerUnit,
                        halfH + (worldPos.y - cameraY) * pixelsPerUnit);
                }

                // Scale a world-space scalar (e.g. light range, shadow
                // softness radius) into render pixels. Camera position
                // doesn't enter — only the unit conversion.
                float scaleLength(float worldLen) const
                {
                    return worldLen * pixelsPerUnit;
                }
            };

        }
    }
}