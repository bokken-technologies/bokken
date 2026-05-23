#include "LightingPass.hpp"
#include "../Pipeline.hpp"
#include "../RenderTarget.hpp"
#include "../lighting/Frame.hpp"

#include <SDL3/SDL.h>

#include <cstdint>

namespace Bokken
{
    namespace Renderer
    {

        namespace
        {
            /* Lighting fragment shader.
             *
             * For each pixel:
             *   1. Sample albedo, normal, emissive from the deferred buffers.
             *   2. Decode normal from [0,1] back to [-1,+1] tangent-space.
             *   3. Reconstruct the .z component (we only store XY in RG16F).
             *   4. Look up the pixel's tile in u_tileGrid; iterate only
             *      the lights whose indices were binned into that tile.
             *      For each light, fetch its five data rows from
             *      u_lightTex and accumulate contribution.
             *   5. Final pixel = albedo * (ambient + accumulated) + emissive.
             *
             * Tile grid texture layout
             *
             * u_tileGrid is a packed 2D atlas: u_tilesPerRow tiles per
             * texture row, each tile occupying u_tileBytesPer bytes along
             * X. Per-tile texel coords are recovered as:
             *   row = tileIdx / u_tilesPerRow
             *   col = (tileIdx % u_tilesPerRow) * u_tileBytesPer + byteOffset
             *
             * This layout keeps the texture's width and height both well
             * under macOS's GL_MAX_TEXTURE_SIZE = 16384, where the older
             * one-tile-per-row layout would overflow at 4K resolution.
             *
             * Shadow sampling
             *
             * Each shadow-casting light has a `shadowSlot` in [0, MAX_SHADOW_SLOTS)
             * which indexes a row of the shadowmap atlas (u_shadowAtlas).
             * The atlas's U axis is the angular dimension around the light;
             * the stored value is occluder distance. The PCF kernel is a
             * 5-tap horizontal blur in atlas-U with per-light softness.
            */
            const char *kLightingFS = R"(#version 330 core
                in vec2 v_uv;

                uniform sampler2D u_albedo;
                uniform sampler2D u_normal;
                uniform sampler2D u_emissive;
                uniform sampler2D u_lightTex;
                uniform sampler2D u_shadowAtlas;
                uniform sampler2D u_tileGrid;
                uniform sampler2D u_cookieAtlas;

                uniform vec2   u_screenSize;
                uniform vec3   u_ambient;
                uniform float  u_intensityScale;
                uniform float  u_wrapAmount;
                uniform float  u_shadowAtlasHeight;
                uniform int    u_shadowAtlasAvailable;
                uniform int    u_tileCountX;
                uniform int    u_tileCountY;
                uniform int    u_tileSize;
                uniform int    u_tileBytesPer;
                uniform int    u_tilesPerRow;
                uniform int    u_cookieAtlasCols;
                uniform int    u_cookieAtlasRows;

                out vec4 FragColor;

                const uint LIGHT_FLAG_ENABLED       = uint(1) << 2;
                const uint LIGHT_FLAG_HAS_COOKIE    = uint(1) << 4;
                const uint LIGHT_TYPE_MASK          = uint(0x3);
                const uint LIGHT_TYPE_POINT         = uint(0);
                const uint LIGHT_TYPE_SPOT          = uint(1);
                const uint LIGHT_TYPE_DIRECTIONAL   = uint(2);

                struct Light {
                    vec2 position;
                    vec2 direction;
                    vec3 color;
                    float intensity;
                    float range;
                    float falloffExp;
                    float innerConeCos;
                    float outerConeCos;
                    uint flags;
                    float shadowSlot;
                    float cookieSlot;
                    float softness;
                    vec2 cookieUVOffset;
                    vec2 cookieUVScale;
                };

                Light fetchLight(int idx) {
                    vec4 row0 = texelFetch(u_lightTex, ivec2(0, idx), 0);
                    vec4 row1 = texelFetch(u_lightTex, ivec2(1, idx), 0);
                    vec4 row2 = texelFetch(u_lightTex, ivec2(2, idx), 0);
                    vec4 row3 = texelFetch(u_lightTex, ivec2(3, idx), 0);
                    vec4 row4 = texelFetch(u_lightTex, ivec2(4, idx), 0);

                    Light L;
                    L.position     = row0.xy;
                    L.direction    = row0.zw;
                    L.color        = row1.rgb;
                    L.intensity    = row1.a;
                    L.range        = row2.x;
                    L.falloffExp   = row2.y;
                    L.innerConeCos = row2.z;
                    L.outerConeCos = row2.w;
                    L.flags        = uint(row3.x + 0.5);
                    L.shadowSlot   = row3.y;
                    L.cookieSlot   = row3.z;
                    L.softness     = row3.w;
                    L.cookieUVOffset = row4.xy;
                    L.cookieUVScale  = row4.zw;
                    return L;
                }

                vec3 decodeNormal(vec2 rg) {
                    vec2 xy = rg * 2.0 - 1.0;
                    float zSq = max(0.0, 1.0 - dot(xy, xy));
                    return vec3(xy, sqrt(zSq));
                }

                // Convert a (tileIdx, byteOffset) pair to a tile-grid
                // texel coordinate. Layout: u_tilesPerRow tiles per row,
                // each tile occupying u_tileBytesPer bytes along X.
                ivec2 tileTexel(int tileIdx, int byteOffset) {
                    int row = tileIdx / u_tilesPerRow;
                    int col = (tileIdx - row * u_tilesPerRow) * u_tileBytesPer
                            + byteOffset;
                    return ivec2(col, row);
                }

                float shadowTap(float u, float v, float fragDist) {
                    float uw = mod(u, 1.0);
                    if (uw < 0.0) uw += 1.0;
                    float occluderDist = texture(u_shadowAtlas, vec2(uw, v)).r;
                    if (occluderDist <= 0.0) return 1.0;
                    const float SHADOW_BIAS = 0.5;
                    return (fragDist <= occluderDist + SHADOW_BIAS) ? 1.0 : 0.0;
                }

                float sampleShadow(Light L, vec2 lightToFrag) {
                    if (u_shadowAtlasAvailable == 0) return 1.0;
                    if (L.shadowSlot < 0.0) return 1.0;
                    float angle = atan(lightToFrag.y, lightToFrag.x);
                    const float TWO_PI = 6.28318530717958647692;
                    if (angle < 0.0) angle += TWO_PI;
                    float u = angle / TWO_PI;
                    float v = (L.shadowSlot + 0.5) / u_shadowAtlasHeight;
                    float fragDist = length(lightToFrag);
                    const float BASE_PCF_RADIUS_TEXELS = 1.5;
                    float atlasW = float(textureSize(u_shadowAtlas, 0).x);
                    float delta = (BASE_PCF_RADIUS_TEXELS * L.softness) / atlasW;
                    float acc = 0.0;
                    acc += shadowTap(u - 2.0 * delta, v, fragDist);
                    acc += shadowTap(u -       delta, v, fragDist);
                    acc += shadowTap(u,                v, fragDist);
                    acc += shadowTap(u +       delta, v, fragDist);
                    acc += shadowTap(u + 2.0 * delta, v, fragDist);
                    return acc * 0.2;
                }

                vec4 sampleCookie(Light L, vec2 fragPosPx) {
                    if ((L.flags & LIGHT_FLAG_HAS_COOKIE) == uint(0)) return vec4(1.0);
                    if (L.cookieSlot < 0.0) return vec4(1.0);
                    uint type = L.flags & LIGHT_TYPE_MASK;
                    if (type == LIGHT_TYPE_DIRECTIONAL) return vec4(1.0);
                    vec2 cookieUV = (fragPosPx - L.position) / L.range * 0.5 + 0.5;
                    cookieUV = cookieUV * L.cookieUVScale + L.cookieUVOffset;
                    cookieUV = fract(cookieUV);
                    int slot = int(L.cookieSlot + 0.5);
                    vec2 slotXY = vec2(slot % u_cookieAtlasCols,
                                       slot / u_cookieAtlasCols);
                    vec2 atlasUV = (slotXY + cookieUV)
                                 / vec2(u_cookieAtlasCols, u_cookieAtlasRows);
                    return texture(u_cookieAtlas, atlasUV);
                }

                vec3 evalLight(Light L, vec2 fragPosPx, vec3 N) {
                    uint type = L.flags & LIGHT_TYPE_MASK;
                    vec3 toLight;
                    float attenuation;
                    vec2 lightToFrag;
                    if (type == LIGHT_TYPE_DIRECTIONAL) {
                        toLight = vec3(-L.direction, 0.0);
                        attenuation = 1.0;
                        lightToFrag = vec2(0.0);
                    } else {
                        vec2 delta = L.position - fragPosPx;
                        float dist = length(delta);
                        if (dist >= L.range) return vec3(0.0);
                        vec2 dir2 = (dist > 1e-4) ? (delta / dist) : vec2(0.0, 1.0);
                        toLight = vec3(dir2, 0.0);
                        float t = 1.0 - clamp(dist / L.range, 0.0, 1.0);
                        attenuation = pow(t, L.falloffExp);
                        lightToFrag = -delta;
                    }
                    if (type == LIGHT_TYPE_SPOT) {
                        float c = dot(toLight.xy, -L.direction);
                        float cone = smoothstep(L.outerConeCos,
                                                L.innerConeCos,
                                                c);
                        attenuation *= cone;
                    }
                    float NdotL = max(dot(N, toLight), 0.0);
                    NdotL = mix(u_wrapAmount, 1.0, NdotL);
                    float shadow = 1.0;
                    if (type != LIGHT_TYPE_DIRECTIONAL) {
                        shadow = sampleShadow(L, lightToFrag);
                    }
                    vec4 cookie = sampleCookie(L, fragPosPx);
                    vec3 cookieTint = cookie.rgb * cookie.a;
                    return L.color * cookieTint * L.intensity * attenuation * NdotL * shadow;
                }

                void main() {
                    vec4 albedoSample = texture(u_albedo, v_uv);
                    vec3 albedo = albedoSample.rgb;
                    float alpha = albedoSample.a;
                    vec3 N = decodeNormal(texture(u_normal, v_uv).rg);
                    vec3 emissive = texture(u_emissive, v_uv).rgb;
                    vec2 fragPosPx = v_uv * u_screenSize;

                    ivec2 tile = clamp(
                        ivec2(fragPosPx / float(u_tileSize)),
                        ivec2(0),
                        ivec2(u_tileCountX - 1, u_tileCountY - 1));
                    int tileIdx = tile.y * u_tileCountX + tile.x;

                    int tileCount = int(
                        texelFetch(u_tileGrid, tileTexel(tileIdx, 0), 0).r
                        * 255.0 + 0.5);

                    vec3 accum = vec3(0.0);
                    for (int slot = 0; slot < tileCount; ++slot) {
                        int lightIdx = int(
                            texelFetch(u_tileGrid, tileTexel(tileIdx, 1 + slot), 0).r
                            * 255.0 + 0.5);
                        Light L = fetchLight(lightIdx);
                        if ((L.flags & LIGHT_FLAG_ENABLED) == uint(0))
                            continue;
                        accum += evalLight(L, fragPosPx, N);
                    }
                    accum *= u_intensityScale;
                    vec3 lit = albedo * (u_ambient + accum) + emissive;
                    FragColor = vec4(lit, alpha);
                }
                )";
        }

        bool LightingPass::setup()
        {
            if (!m_pass.init())
                return false;
            if (!m_pass.compile("lighting", kLightingFS))
                return false;
            return true;
        }

        void LightingPass::execute(const FrameContext &ctx)
        {
            if (!ctx.inputTarget || !ctx.outputTarget || !ctx.pipeline)
                return;

            Lighting::Frame &lf = ctx.pipeline->lighting();
            lf.gatherIfNeeded(ctx.pipeline->currentFrameId(),
                              ctx.viewportWidth, ctx.viewportHeight);

            const RenderTarget *normalsAux = ctx.pipeline->findAuxTarget("normals");
            const RenderTarget *emissiveAux = ctx.pipeline->findAuxTarget("emissive");
            if (!normalsAux || !emissiveAux)
            {
                static const char *kPassthroughFS = R"(#version 330 core
                    in vec2 v_uv;
                    uniform sampler2D u_input;
                    out vec4 FragColor;
                    void main() { FragColor = texture(u_input, v_uv); }
                    )";

                ctx.outputTarget->bind();
                glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);
                ctx.inputTarget->color().bind(0);
                auto &sh = m_pass.beginPass(kPassthroughFS, "lighting-passthrough");
                sh.setInt("u_input", 0);
                m_pass.draw();
                return;
            }

            const bool shadowAtlasUsable = lf.shadowAtlas().isValid()
                                        && lf.shadowAtlasRendered();

            ctx.outputTarget->bind();
            glViewport(0, 0, ctx.viewportWidth, ctx.viewportHeight);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);

            constexpr int UNIT_ALBEDO       = 0;
            constexpr int UNIT_NORMAL       = 1;
            constexpr int UNIT_EMISSIVE     = 2;
            constexpr int UNIT_LIGHTTEX     = 3;
            constexpr int UNIT_SHADOWATLAS  = 4;
            constexpr int UNIT_TILEGRID     = 5;
            constexpr int UNIT_COOKIEATLAS  = 6;

            ctx.inputTarget->color().bind(UNIT_ALBEDO);
            normalsAux->color().bind(UNIT_NORMAL);
            emissiveAux->color().bind(UNIT_EMISSIVE);
            lf.lightBuffer().bind(UNIT_LIGHTTEX);
            if (shadowAtlasUsable)
                lf.shadowAtlas().color().bind(UNIT_SHADOWATLAS);
            lf.tileGrid().bind(UNIT_TILEGRID);
            lf.cookieAtlas().bind(UNIT_COOKIEATLAS);

            auto &sh = m_pass.bind("lighting");
            sh.setInt("u_albedo",       UNIT_ALBEDO);
            sh.setInt("u_normal",       UNIT_NORMAL);
            sh.setInt("u_emissive",     UNIT_EMISSIVE);
            sh.setInt("u_lightTex",     UNIT_LIGHTTEX);
            sh.setInt("u_shadowAtlas",  UNIT_SHADOWATLAS);
            sh.setInt("u_tileGrid",     UNIT_TILEGRID);
            sh.setInt("u_cookieAtlas",  UNIT_COOKIEATLAS);

            sh.setVec2("u_screenSize",
                       static_cast<float>(ctx.viewportWidth),
                       static_cast<float>(ctx.viewportHeight));
            sh.setVec3("u_ambient", ambient.r, ambient.g, ambient.b);
            sh.setFloat("u_intensityScale", intensityScale);
            sh.setFloat("u_wrapAmount", wrapAmount);
            sh.setFloat("u_shadowAtlasHeight",
                        static_cast<float>(Lighting::Frame::MAX_SHADOW_SLOTS));
            sh.setInt("u_shadowAtlasAvailable", shadowAtlasUsable ? 1 : 0);
            sh.setInt("u_tileCountX", lf.tileGrid().tileCountX());
            sh.setInt("u_tileCountY", lf.tileGrid().tileCountY());
            sh.setInt("u_tileSize",
                      static_cast<int>(Lighting::TileLightGrid::TILE_SIZE));
            sh.setInt("u_tileBytesPer",
                      static_cast<int>(Lighting::TileLightGrid::BYTES_PER_TILE));
            sh.setInt("u_tilesPerRow",
                      static_cast<int>(Lighting::TileLightGrid::TILES_PER_TEXTURE_ROW));
            sh.setInt("u_cookieAtlasCols",
                      static_cast<int>(Lighting::CookieAtlas::ATLAS_COLS));
            sh.setInt("u_cookieAtlasRows",
                      static_cast<int>(Lighting::CookieAtlas::ATLAS_ROWS));

            m_pass.draw();

            // Unbind extra units so downstream stages aren't surprised.
            glActiveTexture(GL_TEXTURE0 + UNIT_COOKIEATLAS);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0 + UNIT_TILEGRID);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0 + UNIT_SHADOWATLAS);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0 + UNIT_LIGHTTEX);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0 + UNIT_EMISSIVE);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0 + UNIT_NORMAL);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0 + UNIT_ALBEDO);
        }

    }
}