#pragma once

#include "FrameContext.hpp"
#include <string>

namespace Bokken
{
    namespace Renderer
    {

        /**
         * A single stage in the rendering pipeline.
         *
         * Stages compose the frame: a typical 2D pipeline looks like
         *
         *   "scene" → "bloom" → "color-grade" → "composite"
         *
         * Each stage:
         *   - has a unique name (used by the JS API to insert / replace)
         *   - has a `kind`: "scene" stages produce content, "post" stages
         *     transform the previous output
         *   - reads ctx.inputTarget, writes ctx.outputTarget
         *
         * The Pipeline owns its stages and rotates render targets between
         * them so each stage's output becomes the next's input.
         *
         * Implement setup() once at construction (compile shaders, allocate
         * persistent GL resources). execute() runs every frame. resize() is
         * called when the viewport size changes.
        */
        class Stage
        {
        public:
            enum class Kind
            {
                Scene, // produces content (clears + draws into outputTarget)
                Post,  // transforms inputTarget → outputTarget
                Side,  // writes to its own target (e.g. shadow atlas);
                       // does not participate in the input/output ping-pong
                       // chain, so Pipeline skips the rotation for these.
            };

            explicit Stage(std::string name, Kind kind)
                : m_name(std::move(name)), m_kind(kind) {}
            virtual ~Stage() = default;

            const std::string &name() const { return m_name; }
            Kind kind() const { return m_kind; }

            bool enabled = true;

            /** One-time GL resource setup. Return false on failure to abort
             *  pipeline init. Called with a current GL context. */
            virtual bool setup() { return true; }

            /** Per-frame work. Bind ctx.outputTarget at the top, do GL work,
             *  leave it bound on exit (Pipeline doesn't care). */
            virtual void execute(const FrameContext &ctx) = 0;

            /** Called when the framebuffer size changes. Default does nothing. */
            virtual void onResize(int /*w*/, int /*h*/) {}

        private:
            std::string m_name;
            Kind m_kind;
        };

    }
}