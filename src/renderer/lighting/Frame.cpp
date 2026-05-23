#include "Frame.hpp"
#include "../../game_object/Light2D.hpp"
#include "../../game_object/ShadowCaster2D.hpp"
#include "../../game_object/Camera2D.hpp"
#include "../../game_object/Transform2D.hpp"
#include "../../game_object/Base.hpp"

namespace Bokken
{
    namespace Renderer
    {
        namespace Lighting
        {

            bool Frame::init()
            {
                if (!m_lightBuffer.init())
                    return false;
                if (!m_shadowBuffer.init())
                    return false;
                if (!m_shadowAtlas.create(static_cast<int>(SHADOW_ATLAS_WIDTH),
                                          static_cast<int>(MAX_SHADOW_SLOTS),
                                          TextureFormat::R16F,
                                          /* withDepth = */ false))
                {
                    return false;
                }
                if (!m_tileGrid.init())
                    return false;
                if (!m_cookieAtlas.init())
                    return false;
                return true;
            }

            void Frame::gatherIfNeeded(uint64_t frameId,
                                                int viewportW, int viewportH)
            {
                if (frameId == m_lastGatheredFrame)
                    return;
                m_lastGatheredFrame = frameId;

                // The shadow atlas is stale from the previous frame
                // until the ShadowmapPass runs. The lighting pass
                // checks this flag to decide whether to sample.
                m_shadowAtlasRendered = false;

                // Resolve the active camera and build the world →
                // render-pixel transform once for this frame. Every
                // light snapshot and every shadow caster emit uses
                // the same transform, so the lighting subsystem
                // operates in a single coherent coordinate space:
                // render-target pixels, with the camera at the centre.
                //
                // No active camera ⇒ defaults to origin + 64 px/unit
                // (the same fallback GameObject::present uses), so
                // scenes without an explicit Camera2D still light up
                // sensibly.
                WorldToScreen w2s;
                w2s.halfW = viewportW * 0.5f;
                w2s.halfH = viewportH * 0.5f;
                w2s.pixelsPerUnit = 64.0f;
                for (auto &go : GameObject::Base::s_objects)
                {
                    auto *cam = go->getComponent<GameObject::Camera2D>();
                    if (!cam || !cam->isActive)
                        continue;
                    if (auto *ct = go->getComponent<GameObject::Transform2D>())
                    {
                        w2s.cameraX = ct->position.x;
                        w2s.cameraY = ct->position.y;
                    }
                    w2s.pixelsPerUnit = cam->zoom;
                    break;
                }

                // Snapshot every enabled Light2D into m_lights, in
                // render-target pixel space (Light2D::snapshot
                // applies w2s to position and range). Shadow slot
                // assignment happens inline so the per-light snap
                // call only needs to be made once.
                m_lights.clear();
                m_lights.reserve(GameObject::Light2D::s_all.size());

                uint32_t nextShadowSlot = 0;

                for (GameObject::Light2D *light : GameObject::Light2D::s_all)
                {
                    if (!light || !light->enabled)
                        continue;

                    Light snap;
                    light->snapshot(snap, w2s);

                    // Assign a shadow atlas slot if (a) the light
                    // requested shadows and (b) we still have atlas
                    // rows available. The snapshot() call already
                    // left snap.shadowSlot = LIGHT_NO_SLOT, so lights
                    // that don't get a slot here render unshadowed
                    // without further action.
                    if (light->castsShadows && nextShadowSlot < MAX_SHADOW_SLOTS)
                    {
                        snap.shadowSlot = nextShadowSlot;
                        ++nextShadowSlot;
                    }

                    // Resolve cookie slot, loading the cookie image
                    // into the atlas on first encounter. Lights with
                    // no cookiePath (the typical case) keep
                    // cookieSlot at LIGHT_NO_SLOT — the shader's
                    // cookieSlot test short-circuits.
                    //
                    // The HAS_COOKIE flag is what the shader actually
                    // checks; we update it here based on whether the
                    // atlas resolution actually succeeded. A failed
                    // resolve (typo'd path, atlas full) clears the
                    // flag so the shader treats the light as
                    // cookie-less rather than sampling slot 0
                    // erroneously.
                    if (!light->cookiePath.empty())
                    {
                        const uint32_t slot = m_cookieAtlas.resolveSlot(light->cookiePath);
                        snap.cookieSlot = slot;
                        if (slot == CookieAtlas::NO_SLOT)
                            snap.flags &= ~LIGHT_FLAG_HAS_COOKIE;
                        else
                            snap.flags |= LIGHT_FLAG_HAS_COOKIE;
                    }
                    else
                    {
                        // Defensive: the snapshot() path already
                        // clears HAS_COOKIE when cookiePath is empty,
                        // but make sure cookieSlot is the sentinel.
                        snap.cookieSlot = CookieAtlas::NO_SLOT;
                    }

                    m_lights.push_back(snap);
                }

                m_shadowCount = nextShadowSlot;

                // Walk casters and emit segments — also in render-
                // pixel space, so segment endpoints can be compared
                // directly against the (now-pixelised) light
                // positions in the shadow rasterizer's ray-segment
                // intersection test.
                m_segments.clear();
                for (GameObject::ShadowCaster2D *caster : GameObject::ShadowCaster2D::s_all)
                {
                    if (!caster || !caster->enabled)
                        continue;
                    caster->emit(m_segments, w2s);
                }

                // Upload both. Both buffers truncate to their own
                // hard caps; the warnings fire once globally on
                // first overflow.
                m_lightBuffer.upload(m_lights);
                m_shadowBuffer.upload(m_segments);

                // Build the per-tile light index lists for forward+
                // culling. Walking the (lights × tiles-they-touch)
                // cross product is fast — typical scenes are sub-
                // millisecond. The grid uses the same flat m_lights
                // vector that we just uploaded, so the indices it
                // stores match the indices the lighting shader uses
                // when sampling the light data texture.
                m_tileGrid.bin(viewportW, viewportH, m_lights);
            }

        }
    }
}