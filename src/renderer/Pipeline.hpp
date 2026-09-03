#pragma once

#include "Stage.hpp"
#include "RenderTarget.hpp"
#include "FrameContext.hpp"
#include "Texture2D.hpp"
#include "lighting/Frame.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>

namespace Bokken
{
    namespace Renderer
    {

        class SpriteBatcher;

        /**
         * Ordered list of stages. Owns a pair of HDR ping-pong targets and
         * rotates them between stages so each stage reads the previous
         * stage's output as a texture.
         *
         * The Pipeline does NOT own the SpriteBatcher — that's per-frame
         * shared state passed via FrameContext.
         *
         * The last stage's output is what eventually lands on the default
         * framebuffer. By convention the final stage is "composite" — it
         * reads the chain's output and blits to the screen with whatever
         * tone-mapping / gamma is wanted.
         *
         * JS-facing operations (add / remove / move / setEnabled) are
         * exposed as plain methods; the bokken/renderer scripting module
         * just forwards to them.
         *
         * Auxiliary targets
         *
         * Beyond the ping-pong pair the Pipeline manages a named registry
         * of auxiliary RenderTargets used by stages that need to outlive
         * a single rotation step. The classic case is deferred lighting:
         * a Normal buffer written by the sprite stage is read several
         * stages later by the lighting stage, long after the ping-pong
         * has rotated past it. Aux targets are allocated lazily on first
         * request (requestAuxTarget) and resized in lockstep with the
         * ping-pong pair so a window resize triggers a single coherent
         * reallocation pass.
         *
         * Stages that need an aux target call requestAuxTarget(name, fmt)
         * during their setup() and cache the returned pointer. The pointer
         * remains valid for the Pipeline's lifetime — aux targets are not
         * removed once created.
        */
        class Pipeline
        {
        public:
            Pipeline() = default;

            /** Allocate ping-pong targets. Call after a GL context exists. */
            bool init(int width, int height);

            /** Recreate ping-pong targets at a new size. Cheap if unchanged. */
            bool resize(int width, int height);

            /** Render a frame. Walks stages in order, rotating targets,
             *  finally leaves the last stage's output bound for whatever
             *  composite step the caller wants. */
            void render(SpriteBatcher *batcher, SpriteBatcher *uiBatcher, float dt);

            void addStage(std::unique_ptr<Stage> stage);
            bool removeStage(const std::string &name);
            bool moveStage(const std::string &name, int newIndex);
            Stage *findStage(const std::string &name);
            const std::vector<std::unique_ptr<Stage>> &stages() const { return m_stages; }

            // The final output target after render(). Caller (Renderer)
            // composites this into the default framebuffer.
            const RenderTarget *finalOutput() const { return m_lastOutput; }

            int width() const { return m_width; }
            int height() const { return m_height; }

            /**
             * Request an auxiliary render target by name. The first call with
             * a given name allocates the target at the pipeline's current
             * size; subsequent calls return the existing target regardless
             * of the passed format (the format the first caller asked for
             * wins).
             *
             * Returns nullptr only if the pipeline has not been init()'d yet
             * or if the underlying RenderTarget allocation fails. The
             * returned pointer remains valid for the Pipeline's lifetime,
             * across resize() calls — the underlying GL texture is
             * recreated in place when the pipeline resizes.
             *
             * Aux targets default to LINEAR / CLAMP_TO_EDGE filtering. Stages
             * needing different filter / wrap configure them after request.
            */
            RenderTarget *requestAuxTarget(const std::string &name,
                                           TextureFormat format = TextureFormat::RGBA16F);

            /**
             * Look up a previously-requested aux target. Returns nullptr if
             * no target with this name exists. Stages that share a target
             * (lighting reads what the sprite stage wrote) use this to
             * locate buffers without re-allocating.
            */
            RenderTarget *findAuxTarget(const std::string &name);
            const RenderTarget *findAuxTarget(const std::string &name) const;

            /**
             * Shared per-frame lighting state used by the ShadowmapPass
             * and the LightingPass. Owned by the pipeline because both
             * stages need it and neither owns the other. The actual
             * gather happens via Frame::gatherIfNeeded(frameId);
             * stages pass currentFrameId() so the gather is idempotent
             * across stage invocations within a single frame.
             *
             * Lazily initialised on first access — pipelines that don't
             * use lighting pay zero VRAM / setup cost.
            */
            Lighting::Frame &lighting();

            /**
             * Monotonically increasing frame counter. Incremented at the
             * top of every render() call. Stages pass this to
             * Frame::gatherIfNeeded so the gather happens
             * exactly once per frame regardless of stage order.
            */
            uint64_t currentFrameId() const { return m_frameId; }

        private:
            std::vector<std::unique_ptr<Stage>> m_stages;

            // Two HDR scratch targets, rotated between stages.
            RenderTarget m_targetA;
            RenderTarget m_targetB;
            const RenderTarget *m_lastOutput = nullptr;

            int m_width = 0;
            int m_height = 0;

            // Named auxiliary targets. Stored as unique_ptrs so pointers
            // returned to stages remain stable across map rehashes. A
            // small companion vector records each target's requested
            // format so resize() can recreate them faithfully.
            struct AuxEntry
            {
                std::unique_ptr<RenderTarget> target;
                TextureFormat format;
            };
            std::unordered_map<std::string, AuxEntry> m_auxTargets;

            // Shared lighting state. Held by unique_ptr so init can be
            // lazy — pipelines that never touch lighting() pay zero
            // GPU allocation cost. Initialised on first lighting()
            // call after init().
            std::unique_ptr<Lighting::Frame> m_lighting;

            // Frame counter; advanced once per render() call. Exposed
            // via currentFrameId() so stages can drive
            // Frame::gatherIfNeeded.
            uint64_t m_frameId = 0;
        };

    }
}