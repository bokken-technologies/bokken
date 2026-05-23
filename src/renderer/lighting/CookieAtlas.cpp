#include "CookieAtlas.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace Bokken
{
    namespace Renderer
    {
        namespace Lighting
        {

            namespace
            {
                // Per-(path, reason) set used to log loading failures
                // exactly once. A frequently-respawning Light2D with
                // a typo'd cookie path would otherwise spam logs.
                std::unordered_set<std::string> g_warnedFailures;

                void warnOnce(const std::string &path, const char *reason)
                {
                    const std::string key = path + "::" + reason;
                    if (g_warnedFailures.insert(key).second)
                    {
                        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                                    "[CookieAtlas] %s for '%s'",
                                    reason, path.c_str());
                    }
                }
            }

            bool CookieAtlas::init()
            {
                if (!m_texture.create(static_cast<int>(ATLAS_WIDTH),
                                      static_cast<int>(ATLAS_HEIGHT),
                                      TextureFormat::RGBA8,
                                      TextureFilter::Linear,
                                      TextureWrap::Clamp))
                {
                    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                                 "[CookieAtlas] atlas texture allocation failed");
                    return false;
                }

                // Initialise every slot to transparent black. Without
                // this, lights with cookieSlot != NO_SLOT but a never-
                // written atlas slot would sample undefined GPU memory
                // — usually garbage from previous textures, sometimes
                // visible as "bright noise" through the cookie.
                std::vector<uint8_t> zeros(ATLAS_WIDTH * ATLAS_HEIGHT * 4, 0);
                m_texture.upload(0, 0,
                                 static_cast<int>(ATLAS_WIDTH),
                                 static_cast<int>(ATLAS_HEIGHT),
                                 zeros.data());

                m_pathBySlot.assign(MAX_SLOTS, std::string());
                m_nextEvictSlot = 0;
                return true;
            }

            uint32_t CookieAtlas::resolveSlot(const std::string &virtualPath)
            {
                if (!m_texture.isValid())
                    return NO_SLOT;
                if (!m_assets)
                    return NO_SLOT;
                if (virtualPath.empty())
                    return NO_SLOT;

                // Cached?
                auto it = m_slotByPath.find(virtualPath);
                if (it != m_slotByPath.end())
                    return it->second;

                if (!m_assets->exists(virtualPath))
                {
                    warnOnce(virtualPath, "cookie not found");
                    return NO_SLOT;
                }

                // Load the source image. Same IOStream bridge as
                // TextureCache so cookies live in the asset pack
                // alongside sprites — no separate cookie file path.
                SDL_IOStream *io = m_assets->openIOStream(virtualPath);
                if (!io)
                {
                    warnOnce(virtualPath, "openIOStream failed");
                    return NO_SLOT;
                }
                SDL_Surface *raw = IMG_Load_IO(io, true);
                if (!raw)
                {
                    warnOnce(virtualPath, "IMG_Load_IO failed");
                    return NO_SLOT;
                }

                SDL_Surface *rgba = (raw->format == SDL_PIXELFORMAT_RGBA32)
                                        ? raw
                                        : SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
                if (!rgba)
                {
                    warnOnce(virtualPath, "surface conversion failed");
                    SDL_DestroySurface(raw);
                    return NO_SLOT;
                }

                // Allocate a slot. First-fit through the empty
                // entries; if all are occupied, FIFO-evict the
                // oldest.
                uint32_t slot = NO_SLOT;
                for (uint32_t s = 0; s < MAX_SLOTS; ++s)
                {
                    if (m_pathBySlot[s].empty())
                    {
                        slot = s;
                        break;
                    }
                }
                if (slot == NO_SLOT)
                {
                    slot = m_nextEvictSlot;
                    m_nextEvictSlot = (m_nextEvictSlot + 1) % MAX_SLOTS;

                    // Evict the old occupant — remove from the
                    // forward lookup so future resolveSlot calls
                    // with that path will re-upload (taking
                    // whichever slot is free at that point).
                    const std::string &oldPath = m_pathBySlot[slot];
                    if (!oldPath.empty())
                    {
                        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                    "[CookieAtlas] evicting cookie '%s' from slot %u",
                                    oldPath.c_str(), slot);
                        m_slotByPath.erase(oldPath);
                    }
                }

                // Prepare the target surface — exactly SLOT_PX square.
                // We either upload the source verbatim (when it fits)
                // or downscale via SDL_BlitSurfaceScaled.
                SDL_Surface *target = SDL_CreateSurface(
                    static_cast<int>(SLOT_PX),
                    static_cast<int>(SLOT_PX),
                    SDL_PIXELFORMAT_RGBA32);
                if (!target)
                {
                    warnOnce(virtualPath, "target surface allocation failed");
                    if (rgba != raw) SDL_DestroySurface(rgba);
                    SDL_DestroySurface(raw);
                    return NO_SLOT;
                }

                // Clear target to transparent so unfilled padding
                // around small cookies reads as "no contribution".
                std::memset(target->pixels, 0,
                            static_cast<size_t>(target->pitch) * target->h);

                if (rgba->w == static_cast<int>(SLOT_PX)
                 && rgba->h == static_cast<int>(SLOT_PX))
                {
                    // Exact fit — direct blit. Avoids the scale path
                    // entirely so an authored 256×256 cookie sees
                    // pixel-perfect upload with no resampling.
                    SDL_BlitSurface(rgba, nullptr, target, nullptr);
                }
                else if (rgba->w <= static_cast<int>(SLOT_PX)
                      && rgba->h <= static_cast<int>(SLOT_PX))
                {
                    // Small cookie: centre it in its slot. The atlas
                    // sampling shader treats the full slot as the
                    // cookie's UV [0,1] range, so this means the
                    // authored image takes the centre portion and
                    // the surrounding pixels are transparent.
                    SDL_Rect dst {
                        (static_cast<int>(SLOT_PX) - rgba->w) / 2,
                        (static_cast<int>(SLOT_PX) - rgba->h) / 2,
                        rgba->w, rgba->h
                    };
                    SDL_BlitSurface(rgba, nullptr, target, &dst);
                }
                else
                {
                    // Large cookie: scale down to fit. SDL3's
                    // BlitSurfaceScaled does a linear filter on the
                    // way in; for cookies (which are by nature soft
                    // patterns) this is fine.
                    SDL_BlitSurfaceScaled(rgba, nullptr, target, nullptr,
                                          SDL_SCALEMODE_LINEAR);
                }

                // Upload the target into the atlas at the slot's
                // pixel offset. Slot layout is row-major:
                //   slotX = slot % ATLAS_COLS
                //   slotY = slot / ATLAS_COLS
                const int slotX = static_cast<int>(slot % ATLAS_COLS);
                const int slotY = static_cast<int>(slot / ATLAS_COLS);
                const int xpx = slotX * static_cast<int>(SLOT_PX);
                const int ypx = slotY * static_cast<int>(SLOT_PX);

                m_texture.upload(xpx, ypx,
                                 static_cast<int>(SLOT_PX),
                                 static_cast<int>(SLOT_PX),
                                 target->pixels);

                SDL_DestroySurface(target);
                if (rgba != raw) SDL_DestroySurface(rgba);
                SDL_DestroySurface(raw);

                m_slotByPath[virtualPath] = slot;
                m_pathBySlot[slot] = virtualPath;

                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "[CookieAtlas] loaded '%s' into slot %u",
                            virtualPath.c_str(), slot);
                return slot;
            }

            void CookieAtlas::bind(int unit) const
            {
                m_texture.bind(unit);
            }

        }
    }
}