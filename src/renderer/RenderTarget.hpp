#pragma once

#include "Texture2D.hpp"

#include <SDL3/SDL.h>

#include <utility>

namespace Bokken
{
    namespace Renderer
    {

        /**
         * Off-screen render target. One owned color texture + optional depth,
         * plus the ability to bind borrowed textures as additional color
         * attachments for MRT passes.
         *
         * The primary color attachment (slot 0) is created and owned by the
         * RenderTarget itself via create(); this is the only attachment used
         * by single-output stages (the existing ping-pong post-effects).
         *
         * Multi-output stages (SpriteStage writing albedo + normals +
         * emissive in one MRT pass) bind borrowed textures into slots 1..3
         * via attachAuxColor() each frame, just before submitting their
         * draws. The RenderTarget does not take ownership of those
         * textures — the borrower retains the lifetime — and the
         * attachment is not remembered across resize(), so multi-output
         * stages must re-attach every frame. The per-frame cost is one
         * glFramebufferTexture2D call per attachment, which is negligible.
         *
         * Used by the Pipeline so each Stage can read the previous stage's
         * output as a texture and write to its own. Most stages use HDR
         * (RGBA16F) so bloom and tone-mapping can preserve highlights.
        */
        class RenderTarget
        {
        public:
            static constexpr int MAX_AUX_ATTACHMENTS = 3;

            RenderTarget() = default;
            ~RenderTarget();
            RenderTarget(const RenderTarget &) = delete;
            RenderTarget &operator=(const RenderTarget &) = delete;
            RenderTarget(RenderTarget &&) noexcept;
            RenderTarget &operator=(RenderTarget &&) noexcept;

            bool create(int width, int height,
                        TextureFormat colorFormat = TextureFormat::RGBA16F,
                        bool withDepth = false);

            /** Recreate at a new size. Cheap if size is unchanged. */
            bool resize(int width, int height);

            void bind() const;
            static void bindDefault();

            /**
             * Bind a borrowed texture as auxiliary color attachment at
             * GL_COLOR_ATTACHMENT(slot+1). Slot must be in
             * [0, MAX_AUX_ATTACHMENTS).
             *
             * The attached texture must be the same dimensions as this
             * RenderTarget's primary color attachment; mismatched sizes
             * produce an incomplete FBO and the next draw will fail
             * silently in the driver. The texture remains owned by the
             * caller.
             *
             * The attachment is NOT remembered across resize() — after a
             * resize, callers must re-attach. Multi-output stages should
             * call attachAuxColor every frame as part of their execute()
             * to stay correct under window-resize and aux-target
             * reallocation.
             *
             * Returns false if the slot is out of range or the texture is
             * invalid; otherwise true (FBO completeness is checked by the
             * caller via the next bind() and subsequent draw — the FBO
             * may be temporarily incomplete between attaches).
            */
            bool attachAuxColor(int slot, const Texture2D &texture);

            /** Unbind an auxiliary attachment. After this call the slot's
             *  framebuffer attachment is zero. The default draw-buffer set
             *  (slot 0 only) is not restored — callers using setDrawBuffers
             *  to enable multiple slots must reset it explicitly. */
            void detachAuxColor(int slot);

            /**
             * Set the active draw buffer set for subsequent draws into
             * this target. `count` is the number of consecutive color
             * attachments to enable starting from GL_COLOR_ATTACHMENT0;
             * count=1 is the single-output default, count=N enables
             * attachments 0..N-1. Counts beyond MAX_AUX_ATTACHMENTS + 1
             * are clamped; count <= 0 is treated as 1.
             *
             * Must be called after bind() while this target's FBO is
             * current.
            */
            void setDrawBuffers(int count) const;

            Texture2D &color() { return m_color; }
            const Texture2D &color() const { return m_color; }

            bool isValid() const { return m_fbo != 0; }
            int width() const { return m_width; }
            int height() const { return m_height; }
            GLuint fbo() const { return m_fbo; }

        private:
            GLuint m_fbo = 0;
            GLuint m_depthRBO = 0;
            Texture2D m_color;
            int m_width = 0;
            int m_height = 0;
            TextureFormat m_colorFormat = TextureFormat::RGBA16F;
            bool m_hasDepth = false;
        };

    }
}