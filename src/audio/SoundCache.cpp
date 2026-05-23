#include "SoundCache.hpp"

namespace Bokken
{
    namespace Audio
    {
        std::shared_ptr<Sound> SoundCache::load(const std::string &virtualPath)
        {
            // Fast path: check existing entry without doing the
            // (potentially expensive) decode under the lock.
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_entries.find(virtualPath);
                if (it != m_entries.end())
                    return it->second;
            }

            // Decode outside the lock — Sound::loadFromPack does
            // PhysFS reads and dr_wav decoding which can take many ms
            // for large clips. Holding the mutex through that would
            // serialise concurrent loads from background scripts,
            // and the SoundCache is meant to be cheap to query.
            auto sound = Sound::loadFromPack(virtualPath);
            if (!sound)
                return nullptr;

            // Insert under the lock. If another thread raced us and
            // already inserted the same path, prefer their entry so
            // identical-path loads always return the same shared_ptr
            // — important for downstream identity checks.
            std::lock_guard<std::mutex> lock(m_mutex);
            auto [it, inserted] = m_entries.try_emplace(virtualPath, sound);
            if (!inserted)
            {
                SDL_LogVerbose(SDL_LOG_CATEGORY_AUDIO,
                               "[SoundCache] race on '%s' — using earlier load",
                               virtualPath.c_str());
            }
            return it->second;
        }

        void SoundCache::evict(const std::string &virtualPath)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_entries.erase(virtualPath);
        }

        void SoundCache::clear()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_entries.clear();
        }
    }
}
