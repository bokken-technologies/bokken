#pragma once

#include "Sound.hpp"

#include <SDL3/SDL.h>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Bokken
{
    namespace Audio
    {
        /**
         * Engine-internal cache of decoded Sound assets, keyed by virtual
         * path through the asset pack VFS. Mirrors the role of the
         * TextureCache for sprites: scripts reference clips by path,
         * the engine handles loading lazily and shares decoded PCM
         * across every voice that plays it.
         *
         * Threading: all access happens on the game thread. The voice
         * itself holds the shared_ptr<Sound> so the cache freeing its
         * map entry cannot rip the data out from under a playing voice
         * — refcounting on the audio thread is read-only and safe.
        */
        class SoundCache
        {
        public:
            static SoundCache &get()
            {
                static SoundCache instance;
                return instance;
            }

            /**
             * Resolve a path to a Sound, decoding via PhysFS on first use.
             * Returns nullptr on load failure (logged at the call site).
             * Cached entries are returned by shared_ptr copy; the cache
             * keeps its own reference so identical paths re-use one
             * decoded buffer no matter how many sources play it.
            */
            std::shared_ptr<Sound> load(const std::string &virtualPath);

            /**
             * Drop the cache's reference to a clip. Voices still playing
             * the clip keep their own shared_ptr, so the PCM lives until
             * the last voice finishes — there's no rug-pull.
             *
             * Useful for level transitions where a track loaded for the
             * previous level isn't needed anymore.
            */
            void evict(const std::string &virtualPath);

            /** Drop every entry — typically called on engine shutdown. */
            void clear();

        private:
            SoundCache() = default;
            ~SoundCache() = default;
            SoundCache(const SoundCache &) = delete;
            SoundCache &operator=(const SoundCache &) = delete;

            std::mutex m_mutex;
            std::unordered_map<std::string, std::shared_ptr<Sound>> m_entries;
        };
    }
}
