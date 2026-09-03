#pragma once

#include "Component.hpp"
#include "Sprite2D.hpp"
#include "Base.hpp"
#include "../renderer/TextureCache.hpp"
#include "../AssetPack.hpp"

#include <SDL3/SDL.h>

#include <string>
#include <vector>
#include <unordered_map>

namespace Bokken
{
    namespace GameObject
    {

        /**
         * Loop behaviour for an animation clip.
        */
        enum class AnimationLoop : uint8_t
        {
            None,       // Play once, stop on last frame.
            Loop,       // Restart from the first frame after the last.
            PingPong,   // Reverse direction at each end.
        };

        /**
         * A single named animation clip — a sequence of TextureCache region
         * names played back at a fixed frame rate.
         *
         * Frames can be populated in two ways:
         *
         *   1. Explicit region names — the caller defines regions manually
         *      and passes an array of their names.
         *
         *   2. Auto-sliced from a sprite sheet via addClipFromGrid(), which
         *      reads the sibling Sprite2D's source, loads the texture
         *      if necessary, slices a grid from it, and fills the frames
         *      vector automatically.
        */
        struct AnimationClip
        {
            std::string name;
            std::vector<std::string> frames;  // TextureCache region names.
            float framesPerSecond = 12.0f;
            AnimationLoop loop = AnimationLoop::Loop;

            // Optional per-clip texture path. When set, Animation2D writes
            // this into the sibling Sprite2D on every frame switch, allowing
            // different clips to source from different sprite sheets.
            // Empty string means "use whatever the Sprite2D already has".
            std::string source;
        };

        /**
         * Sprite sheet animation controller.
         *
         * Holds a dictionary of named AnimationClips and drives the active
         * clip's frame counter. Each tick, it updates the owning GameObject's
         * Sprite2D::regionName to the current frame's region name.
         *
         * Requires a Sprite2D on the same GameObject — Animation2D sets the
         * region, Sprite2D holds the texture path and rendering state.
         *
         * Named "Animation2D" rather than "SpriteAnimator2D" so the same
         * naming pattern extends to Animation3D when 3D support lands.
        */
        class Animation2D : public Component
        {
        public:
            // Set once during engine init, same pattern as Canvas::s_batcher.
            static inline Renderer::TextureCache *s_textureCache = nullptr;
            static inline AssetPack *s_assets = nullptr;

            /**
             * Add or replace a clip with pre-built region names.
             * The first clip added becomes the default if nothing is playing.
            */
            void addClip(const AnimationClip &clip)
            {
                m_clips[clip.name] = clip;
                if (m_activeClip.empty())
                    m_activeClip = clip.name;
            }

            /**
             * Add a clip by auto-slicing a sprite sheet texture.
             *
             * If source is non-empty, that texture is used. Otherwise
             * the sibling Sprite2D's source is used as a fallback.
             * The texture is loaded into the cache automatically if needed.
             *
             * @param clipName     Unique clip name (e.g. "run", "idle").
             * @param frameW       Width of each frame in pixels.
             * @param frameH       Height of each frame in pixels.
             * @param count        Number of frames to extract (0 = fill the row).
             * @param offsetX      Pixel X offset into the texture.
             * @param offsetY      Pixel Y offset into the texture.
             * @param paddingX     Horizontal gap between frames.
             * @param paddingY     Vertical gap between frames.
             * @param fps          Playback speed.
             * @param loop         Loop mode.
             * @param source  Optional path to a specific sprite sheet.
             *                     Empty string falls back to the Sprite2D's path.
             * @return             true if the clip was created successfully.
            */
            bool addClipFromGrid(const std::string &clipName,
                                 int frameW, int frameH,
                                 int count = 0,
                                 int offsetX = 0, int offsetY = 0,
                                 int paddingX = 0, int paddingY = 0,
                                 float fps = 12.0f,
                                 AnimationLoop loop = AnimationLoop::Loop,
                                 const std::string &source = "");

            /**
             * Start playing a named clip from the beginning.
             * If the clip is already active, restarts it.
            */
            void play(const std::string &clipName)
            {
                auto it = m_clips.find(clipName);
                if (it == m_clips.end())
                    return;

                m_activeClip = clipName;
                m_frameIndex = 0;
                m_timer = 0.0f;
                m_playing = true;
                m_forward = true;
            }

            /** Resume a paused animation without resetting the frame. */
            void resume() { m_playing = true; }

            /** Pause the animation on the current frame. */
            void pause() { m_playing = false; }

            /** Stop and reset to the first frame. */
            void stop()
            {
                m_playing = false;
                m_frameIndex = 0;
                m_timer = 0.0f;
            }

            bool isPlaying() const { return m_playing; }
            const std::string &activeClip() const { return m_activeClip; }
            int frameIndex() const { return m_frameIndex; }

            /**
             * Returns the region name of the current frame, or empty string
             * if no clip is active or the clip has no frames.
            */
            std::string currentRegion() const
            {
                auto it = m_clips.find(m_activeClip);
                if (it == m_clips.end() || it->second.frames.empty())
                    return {};
                int idx = m_frameIndex;
                if (idx < 0 || idx >= static_cast<int>(it->second.frames.size()))
                    idx = 0;
                return it->second.frames[idx];
            }

            void update(float dt) override;

        private:
            std::unordered_map<std::string, AnimationClip> m_clips;
            std::string m_activeClip;

            int m_frameIndex = 0;
            float m_timer = 0.0f;
            bool m_playing = false;
            bool m_forward = true;  // For PingPong mode.

            // Counter for generating unique internal region prefixes so
            // clips from different GameObjects never collide in the cache.
            static inline uint32_t s_clipCounter = 0;
        };

    }
}