#include "Animation2D.hpp"

namespace Bokken
{
    namespace GameObject
    {

        bool Animation2D::addClipFromGrid(const std::string &clipName,
                                          int frameW, int frameH,
                                          int count,
                                          int offsetX, int offsetY,
                                          int paddingX, int paddingY,
                                          float framesPerSecond,
                                          AnimationLoop loop,
                                          const std::string &source)
        {
            if (!s_textureCache || !s_assets)
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "[Animation2D] addClipFromGrid: TextureCache or AssetPack not wired");
                return false;
            }

            if (!gameObject)
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "[Animation2D] addClipFromGrid: no owning GameObject");
                return false;
            }

            // Resolve which texture to slice: explicit path takes priority,
            // otherwise fall back to the sibling Sprite2D's path.
            std::string resolvedPath = source;
            if (resolvedPath.empty())
            {
                auto *sprite = gameObject->getComponent<Sprite2D>();
                if (sprite)
                    resolvedPath = sprite->source;
            }

            if (resolvedPath.empty())
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "[Animation2D] addClipFromGrid: no texture path for clip '%s' on '%s'",
                             clipName.c_str(), gameObject->name.c_str());
                return false;
            }

            // Load the texture if it isn't cached yet.
            const Renderer::Texture2D *tex = s_textureCache->load(resolvedPath, s_assets);
            if (!tex)
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "[Animation2D] addClipFromGrid: failed to load '%s'",
                             resolvedPath.c_str());
                return false;
            }

            // Generate a unique prefix so region names from different
            // GameObjects or clips never collide in the shared cache.
            uint32_t id = s_clipCounter++;
            std::string prefix = "_anim_" + std::to_string(id) + "_" + clipName;

            int created = s_textureCache->defineGrid(
                prefix, resolvedPath,
                frameW, frameH, count,
                offsetX, offsetY,
                paddingX, paddingY);

            if (created == 0)
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                            "[Animation2D] addClipFromGrid: defineGrid produced 0 frames for '%s'",
                            clipName.c_str());
                return false;
            }

            // Build the clip from the generated region names.
            AnimationClip clip;
            clip.name = clipName;
            clip.framesPerSecond = framesPerSecond;
            clip.loop = loop;
            clip.source = resolvedPath;
            clip.frames.reserve(created);

            for (int i = 0; i < created; ++i)
                clip.frames.push_back(prefix + "_" + std::to_string(i));

            addClip(clip);
            return true;
        }

        void Animation2D::update(float dt)
        {
            if (!m_playing || !enabled)
                return;

            auto it = m_clips.find(m_activeClip);
            if (it == m_clips.end())
                return;

            const AnimationClip &clip = it->second;
            if (clip.frames.empty() || clip.framesPerSecond <= 0.0f)
                return;

            float frameDuration = 1.0f / clip.framesPerSecond;
            m_timer += dt;

            while (m_timer >= frameDuration)
            {
                m_timer -= frameDuration;

                int count = static_cast<int>(clip.frames.size());

                switch (clip.loop)
                {
                case AnimationLoop::Loop:
                    m_frameIndex = (m_frameIndex + 1) % count;
                    break;

                case AnimationLoop::PingPong:
                    if (m_forward)
                    {
                        m_frameIndex++;
                        if (m_frameIndex >= count - 1)
                        {
                            m_frameIndex = count - 1;
                            m_forward = false;
                        }
                    }
                    else
                    {
                        m_frameIndex--;
                        if (m_frameIndex <= 0)
                        {
                            m_frameIndex = 0;
                            m_forward = true;
                        }
                    }
                    break;

                case AnimationLoop::None:
                    if (m_frameIndex < count - 1)
                        m_frameIndex++;
                    else
                        m_playing = false;
                    break;
                }
            }

            // Push the current frame into the sibling Sprite2D so the
            // renderer picks it up automatically.
            if (gameObject)
            {
                auto *sprite = gameObject->getComponent<Sprite2D>();
                if (sprite)
                {
                    sprite->regionName = clip.frames[m_frameIndex];

                    // If this clip has its own texture path, switch the
                    // Sprite2D to it. This allows different clips to source
                    // from different sprite sheets.
                    if (!clip.source.empty())
                        sprite->source = clip.source;
                }
            }
        }

    }
}