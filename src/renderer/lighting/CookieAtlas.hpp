#pragma once

#include "../Texture2D.hpp"
#include "../../AssetPack.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Bokken
{
    namespace Renderer
    {
        namespace Lighting
        {

            /**
             * Texture atlas for light cookies (gobos / projection
             * masks).
             *
             * A cookie is a 2D image multiplied into a light's
             * contribution. Use cases: a Venetian-blinds pattern
             * streaking the light, a stained-glass projection of
             * colored rays onto a wall, a Bat-Signal logo, a leaf-
             * shadow pattern for forest sun. The cookie is sampled in
             * light-space (relative to the light's world position),
             * so the projection moves when the light moves.
             *
             * Layout
             *
             * One RGBA8 atlas texture sized
             *   ATLAS_COLS × SLOT_PX  wide
             *   ATLAS_ROWS × SLOT_PX  tall
             * with one cookie per fixed-size slot. Slots are
             * allocated in left-to-right top-to-bottom order;
             * eviction is FIFO when the atlas is full.
             *
             * Cookies larger than SLOT_PX are downscaled to fit on
             * upload. Cookies smaller are uploaded centred in their
             * slot with the extra padding filled black (alpha 0) —
             * so the cookie is sampled exactly within its authored
             * footprint and the slot's surrounding pixels read as
             * "no contribution".
             *
             * Cookies are loaded once per path. Subsequent calls to
             * resolveSlot for the same path return the existing
             * slot. The atlas is not reloaded between scenes — a
             * cookie used by one scene stays cached for the next.
             *
             * Limits
             *
             *   MAX_SLOTS = ATLAS_COLS * ATLAS_ROWS = 32 slots.
             *
             *   Scenes with more than 32 unique cookies in flight
             *   will see least-recently-used eviction — old cookies
             *   silently lose their slot, the next render shows them
             *   as missing (LIGHT_NO_SLOT, no cookie). For most 2D
             *   games this cap is well above the actual usage; if it
             *   bites, the fix is to bump ATLAS_ROWS or implement
             *   real LRU tracking.
            */
            class CookieAtlas
            {
            public:
                static constexpr uint32_t SLOT_PX   = 256;
                static constexpr uint32_t ATLAS_COLS = 4;
                static constexpr uint32_t ATLAS_ROWS = 8;
                static constexpr uint32_t MAX_SLOTS = ATLAS_COLS * ATLAS_ROWS;

                static constexpr uint32_t ATLAS_WIDTH  = SLOT_PX * ATLAS_COLS;
                static constexpr uint32_t ATLAS_HEIGHT = SLOT_PX * ATLAS_ROWS;

                /**
                 * Slot index sentinel for "no cookie" — same value as
                 * the LIGHT_NO_SLOT used throughout the lighting code.
                 * Returned by resolveSlot when the asset can't be
                 * loaded or the atlas is full.
                */
                static constexpr uint32_t NO_SLOT = 0xFFFFFFFFu;

                CookieAtlas() = default;
                ~CookieAtlas() = default;

                CookieAtlas(const CookieAtlas &) = delete;
                CookieAtlas &operator=(const CookieAtlas &) = delete;

                /**
                 * Allocate the atlas texture. Call once after the GL
                 * context exists.
                */
                bool init();

                /**
                 * Wire the asset pack for cookie loading. Must be
                 * called once at engine startup before any cookie
                 * resolves. Until then resolveSlot returns NO_SLOT
                 * with no atlas mutation.
                */
                void setAssetPack(AssetPack *assets) { m_assets = assets; }

                /**
                 * Return a slot index for the cookie at this virtual
                 * path. First call uploads the image into a fresh
                 * slot; subsequent calls return the cached slot.
                 *
                 * Returns NO_SLOT if the asset pack isn't wired, the
                 * file is missing, or the atlas is full. In all
                 * failure cases the failure is logged once per
                 * (path, reason) pair to avoid log spam from a
                 * frequently-respawning light with a typo'd cookie
                 * path.
                */
                uint32_t resolveSlot(const std::string &virtualPath);

                /** Bind the atlas texture at the given sampler unit. */
                void bind(int unit) const;

                /** Number of unique cookies currently resident. */
                uint32_t residentCount() const
                    { return static_cast<uint32_t>(m_slotByPath.size()); }

            private:
                Texture2D m_texture;
                AssetPack *m_assets = nullptr;

                // Path → slot index lookup. The slot index is what
                // the lighting shader reads from Light::cookieSlot
                // and what it uses to compute the atlas UV offset.
                std::unordered_map<std::string, uint32_t> m_slotByPath;

                // Reverse lookup so a FIFO eviction can find the
                // path occupying a given slot. Indexed by slot
                // number; empty string means "slot is free".
                std::vector<std::string> m_pathBySlot;

                // FIFO pointer: the next slot to evict when the
                // atlas is full. Wraps around MAX_SLOTS.
                uint32_t m_nextEvictSlot = 0;
            };

        }
    }
}