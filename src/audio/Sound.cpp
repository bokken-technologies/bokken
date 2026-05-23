#include "Sound.hpp"

#include <SDL3/SDL.h>
#include <physfs.h>
#include <dr_wav.h>

namespace Bokken
{
    namespace Audio
    {
        std::shared_ptr<Sound> Sound::loadFromPack(const std::string &virtualPath)
        {
            // Open through PhysFS so the path resolves against mounted asset packs.
            PHYSFS_File *file = PHYSFS_openRead(virtualPath.c_str());
            if (!file)
            {
                SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
                             "[Audio] Sound::load — PHYSFS_openRead failed for '%s': %s",
                             virtualPath.c_str(), PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
                return nullptr;
            }

            const PHYSFS_sint64 fileLen = PHYSFS_fileLength(file);
            if (fileLen <= 0)
            {
                PHYSFS_close(file);
                SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "[Audio] Sound::load — empty file: %s", virtualPath.c_str());
                return nullptr;
            }

            std::vector<uint8_t> bytes(static_cast<size_t>(fileLen));
            const PHYSFS_sint64 readLen = PHYSFS_readBytes(file, bytes.data(), fileLen);
            PHYSFS_close(file);

            if (readLen != fileLen)
            {
                SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
                             "[Audio] Sound::load — short read on %s (%lld of %lld bytes)",
                             virtualPath.c_str(), (long long)readLen, (long long)fileLen);
                return nullptr;
            }

            auto sound = decodeWavBytes(bytes.data(), bytes.size());
            if (sound)
                sound->m_path = virtualPath;
            return sound;
        }

        std::shared_ptr<Sound> Sound::decodeWavBytes(const uint8_t *bytes, size_t len)
        {
            unsigned int channels = 0;
            unsigned int sampleRate = 0;
            drwav_uint64 totalFrames = 0;

            // dr_wav's one-shot decode-to-float32 entry point.
            // Always returns interleaved samples in [-1, 1].
            float *pcm = drwav_open_memory_and_read_pcm_frames_f32(
                bytes, len, &channels, &sampleRate, &totalFrames, nullptr);

            if (!pcm)
            {
                SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "[Audio] dr_wav failed to decode buffer (%zu bytes)", len);
                return nullptr;
            }

            auto sound = std::shared_ptr<Sound>(new Sound());
            sound->m_channels = channels;
            sound->m_sampleRate = sampleRate;
            sound->m_frameCount = totalFrames;
            sound->m_pcm.assign(pcm, pcm + (totalFrames * channels));
            drwav_free(pcm, nullptr);

            return sound;
        }
    }
}
