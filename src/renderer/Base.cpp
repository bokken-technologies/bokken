#include "Base.hpp"

namespace Bokken
{
    namespace Renderer
    {

        Base::~Base() { shutdown(); }

        bool Base::init(SDL_Window *window, AssetPack *assets)
        {
            if (!window)
                return false;
            m_window = window;
            (void)assets; // not used directly here; passed to GlyphCache on demand

            // Request a 3.3 Core context. macOS requires Core profile to get
            // anything > 2.1, so we ask for it explicitly.
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
            SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
            SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

            m_glContext = SDL_GL_CreateContext(window);
            if (!m_glContext)
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "[Renderer] SDL_GL_CreateContext failed: %s", SDL_GetError());
                return false;
            }
            SDL_GL_MakeCurrent(window, m_glContext);

            /* Vsync: try adaptive (-1, falls back to standard if not
             * supported, but doesn't tear at low framerates) then
             * standard (1). On macOS adaptive often returns false but
             * standard works. Log the result so a CPU-pinned-at-100%
             * report can be diagnosed at a glance — without vsync this
             * loop will spin as fast as the GPU can present frames.
             *
             * If both attempts fail, the loop will run uncapped — the
             * SDL_Delay(0) at the end of tick() yields but doesn't
             * sleep, so CPU usage will saturate. A future fix would
             * be a manual frame cap (sleep until target frame time)
             * but that adds latency; vsync is preferred. */
            int vsyncMode = -1;
            bool vsyncOK = SDL_GL_SetSwapInterval(-1);
            if (!vsyncOK)
            {
                vsyncMode = 1;
                vsyncOK = SDL_GL_SetSwapInterval(1);
            }
            if (vsyncOK)
            {
                SDL_LogInfo(SDL_LOG_CATEGORY_RENDER,
                            "[Renderer] vsync enabled (mode=%d)", vsyncMode);
            }
            else
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                            "[Renderer] vsync NOT enabled: %s — frame loop will run "
                            "uncapped and CPU usage will be high",
                            SDL_GetError());
            }

            // glad's loader takes a callback with signature
            //   void* (const char *)
            // SDL3's SDL_GL_GetProcAddress returns an SDL_FunctionPointer
            // (a typed function-pointer alias), not void*. They're
            // ABI-compatible — SDL_FunctionPointer is just a typed
            // function pointer — but the C++ type system needs the
            // explicit reinterpret_cast.
            const int version = gladLoadGL(
                reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress));

            if (version == 0)
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "[GL] gladLoadGL failed — no GL context current?");
                return false;
            }

            const int major = GLAD_VERSION_MAJOR(version);
            const int minor = GLAD_VERSION_MINOR(version);
            if (major < 3 || (major == 3 && minor < 3))
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "[GL] need GL 3.3 core, got %d.%d", major, minor);
                return false;
            }

            // Establish current size before we build resources sized to it.
            updateSize();

            // Seed the render-size state. The default policy is
            // FollowWindow: the render size tracks the window. Callers
            // that want a stable design resolution flip this with
            // setRenderSize() after init() (which Loop does using the
            // project's configured window dimensions).
            m_policy = RenderSizePolicy::FollowWindow;
            m_fixedW = m_physicalW;
            m_fixedH = m_physicalH;
            m_renderW = m_physicalW;
            m_renderH = m_physicalH;

            if (!m_batcher.init())
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "[Renderer] SpriteBatcher init failed");
                return false;
            }
            if (!m_glyphs.init())
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "[Renderer] GlyphCache init failed");
                return false;
            }

            // Default pipeline: scene → composite. JS code can add bloom etc.
            buildDefaultPipeline();

            if (!m_pipeline.init(m_renderW, m_renderH))
            {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "[Renderer] Pipeline init failed");
                return false;
            }

            return true;
        }

        void Base::shutdown()
        {
            if (m_glContext)
            {
                SDL_GL_DestroyContext(m_glContext);
                m_glContext = nullptr;
            }
        }

        void Base::buildDefaultPipeline()
        {
            m_pipeline.addStage(std::make_unique<SpriteStage>("scene"));
            m_pipeline.addStage(std::make_unique<UserInterfaceStage>("userInterface"));
            m_pipeline.addStage(std::make_unique<CompositeStage>("composite"));
        }

        void Base::updateSize()
        {
            if (!m_window)
                return;
            int lw, lh, pw, ph;
            SDL_GetWindowSize(m_window, &lw, &lh);
            SDL_GetWindowSizeInPixels(m_window, &pw, &ph);
            m_logicalW = lw;
            m_logicalH = lh;
            m_physicalW = pw;
            m_physicalH = ph;
        }

        void Base::recomputeRenderSize()
        {
            // Default to current values so a degenerate window (e.g.
            // minimised to 0 px) leaves the previous frame's render
            // size in place rather than collapsing the pipeline.
            int rw = m_renderW > 0 ? m_renderW : 1;
            int rh = m_renderH > 0 ? m_renderH : 1;

            switch (m_policy)
            {
            case RenderSizePolicy::FollowWindow:
                if (m_physicalW > 0 && m_physicalH > 0)
                {
                    rw = m_physicalW;
                    rh = m_physicalH;
                }
                break;

            case RenderSizePolicy::Fixed:
                rw = m_fixedW > 0 ? m_fixedW : 1;
                rh = m_fixedH > 0 ? m_fixedH : 1;
                break;

            case RenderSizePolicy::FixedHeight:
                // Vertical resolution is locked; horizontal tracks the
                // window's aspect ratio. The camera reveals more or
                // less content sideways but the pixel-per-vertical-unit
                // density never changes — useful for side-scrollers
                // where the playfield's vertical extent is gameplay-
                // critical.
                if (m_physicalW > 0 && m_physicalH > 0 && m_fixedH > 0)
                {
                    rh = m_fixedH;
                    // Round to even to avoid sub-pixel shimmer on
                    // bilinear-filtered composites and on aux targets
                    // that sample neighbour pixels (bloom downsample,
                    // FXAA).
                    int derived = static_cast<int>(
                        std::lround((double)m_fixedH * (double)m_physicalW / (double)m_physicalH));
                    if (derived < 1)
                        derived = 1;
                    if ((derived & 1) != 0)
                        derived += 1;
                    rw = derived;
                }
                break;
            }

            m_renderW = rw;
            m_renderH = rh;

            if (m_policy == RenderSizePolicy::FollowWindow)
            {
                m_targetW = m_renderW;
                m_targetH = m_renderH;
            }
            else
            {
                const float scale = dpiScale();
                m_targetW = std::max(1, (int)std::lround(m_renderW * scale));
                m_targetH = std::max(1, (int)std::lround(m_renderH * scale));
            }
        }

        bool Base::setRenderSize(int width, int height, RenderSizePolicy policy)
        {
            if (policy != RenderSizePolicy::FollowWindow)
            {
                if (width <= 0 || height <= 0)
                {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                                "[Renderer] setRenderSize: invalid dimensions "
                                "%dx%d for non-FollowWindow policy; ignoring",
                                width, height);
                    return false;
                }
                m_fixedW = width;
                m_fixedH = height;
            }
            m_policy = policy;
            // The actual pipeline resize is deferred to the next
            // beginFrame() so callers that touch this mid-frame don't
            // tear down GL state while the pipeline is mid-render.
            // The render-size-changed observer chain is fired from
            // the same place for the same reason.
            return true;
        }

        int Base::addRenderSizeListener(RenderSizeListener cb)
        {
            const int id = m_nextRenderSizeListenerId++;
            m_renderSizeListeners.push_back({id, std::move(cb)});
            return id;
        }

        bool Base::removeRenderSizeListener(int id)
        {
            for (auto it = m_renderSizeListeners.begin();
                 it != m_renderSizeListeners.end(); ++it)
            {
                if (it->id == id)
                {
                    m_renderSizeListeners.erase(it);
                    return true;
                }
            }
            return false;
        }

        void Base::fireRenderSizeChanged()
        {
            // Keyed on actual change vs the last value we dispatched.
            // beginFrame calls this every frame; almost every call is
            // a no-op (Fixed policy with no setRenderSize between
            // frames). Two int compares is the cost.
            if (m_renderW == m_lastFiredRenderW &&
                m_renderH == m_lastFiredRenderH)
            {
                return;
            }
            m_lastFiredRenderW = m_renderW;
            m_lastFiredRenderH = m_renderH;

            // Snapshot before iterating: a listener that calls
            // removeRenderSizeListener() during dispatch must not
            // invalidate our iterator. JS callbacks routed through
            // the Window scripting module are notorious for mutating
            // the structure they were invoked from.
            auto snapshot = m_renderSizeListeners;
            for (const auto &L : snapshot)
            {
                L.cb(m_renderW, m_renderH);
            }
        }

        void Base::compositeDstRect(float &x, float &y, float &w, float &h) const
        {
            // Fit-and-letterbox: the largest aspect-preserving rect
            // that fits in the window. Equal sizes (aspect match) make
            // dstW == physicalW and dstH == physicalH, so the math
            // degenerates to a 1:1 blit and no bars appear.
            if (m_physicalW <= 0 || m_physicalH <= 0 ||
                m_renderW <= 0 || m_renderH <= 0)
            {
                x = 0.0f;
                y = 0.0f;
                w = 0.0f;
                h = 0.0f;
                return;
            }

            const float renderAspect = (float)m_renderW / (float)m_renderH;
            const float windowAspect = (float)m_physicalW / (float)m_physicalH;
            float dstW, dstH;
            if (windowAspect > renderAspect)
            {
                // Window is wider than render aspect — pillarbox.
                dstH = (float)m_physicalH;
                dstW = dstH * renderAspect;
            }
            else
            {
                // Window is taller than render aspect — letterbox.
                dstW = (float)m_physicalW;
                dstH = dstW / renderAspect;
            }
            x = ((float)m_physicalW - dstW) * 0.5f;
            y = ((float)m_physicalH - dstH) * 0.5f;
            w = dstW;
            h = dstH;
        }

        void Base::windowToRender(float wx, float wy, float &rx, float &ry) const
        {
            float dx, dy, dw, dh;
            compositeDstRect(dx, dy, dw, dh);
            if (dw <= 0.0f || dh <= 0.0f)
            {
                rx = 0.0f;
                ry = 0.0f;
                return;
            }
            rx = (wx - dx) * ((float)m_renderW / dw);
            ry = (wy - dy) * ((float)m_renderH / dh);
        }

        void Base::renderToWindow(float rx, float ry, float &wx, float &wy) const
        {
            float dx, dy, dw, dh;
            compositeDstRect(dx, dy, dw, dh);
            if (m_renderW <= 0 || m_renderH <= 0)
            {
                wx = 0.0f;
                wy = 0.0f;
                return;
            }
            wx = dx + rx * (dw / (float)m_renderW);
            wy = dy + ry * (dh / (float)m_renderH);
        }

        void Base::beginFrame()
        {
            // Refresh window dimensions first — recomputeRenderSize
            // consults them for FollowWindow / FixedHeight policies.
            updateSize();
            recomputeRenderSize();

            // Pipeline is the single source of truth for "what size
            // are we drawing at". A no-op if dimensions are unchanged.
            if (m_targetW != m_pipeline.width() || m_targetH != m_pipeline.height())
            {
                m_pipeline.resize(m_targetW, m_targetH);
            }

            // The batcher's viewport and projection are bound to
            // render space; sprite stage draws using these. The final
            // composite blit re-bases the batcher to window space.
            m_batcher.begin(m_renderW, m_renderH, m_targetW, m_targetH);

            // Notify observers of any render-size change. Fires only
            // when the dimensions actually changed since the last
            // dispatch — see fireRenderSizeChanged for the dedup. Done
            // after the pipeline resize so handlers that query
            // dependent state (e.g. pipeline.width()) see the new
            // value. Done before any scene-stage draws so handlers
            // that reposition objects in response to the resize have
            // their effects visible on the current frame.
            fireRenderSizeChanged();
        }

        void Base::endFrame(float dt)
        {
            // Run pipeline. Each stage writes into the rotating ping-pong
            // targets; final output is in pipeline.finalOutput().
            m_pipeline.render(&m_batcher, dt);

            // Composite to the default framebuffer (the actual window).
            const RenderTarget *final = m_pipeline.finalOutput();

            // Always rebind the window before swapping so the swap
            // affects the correct surface. Without this, an empty
            // pipeline (final == nullptr) would leave whatever
            // framebuffer the last stage bound active.
            RenderTarget::bindDefault();
            glViewport(0, 0, m_physicalW, m_physicalH);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);

            // Letterbox bars: clear the entire window to black before
            // drawing the scene rect on top. Skipped when render
            // aspect matches window aspect (no bars to draw) as a
            // tiny perf nicety — the upcoming opaque blit would
            // overwrite the clear anyway.
            float dstX, dstY, dstW, dstH;
            compositeDstRect(dstX, dstY, dstW, dstH);
            const bool hasBars =
                (dstX > 0.5f) || (dstY > 0.5f) ||
                (dstX + dstW < (float)m_physicalW - 0.5f) ||
                (dstY + dstH < (float)m_physicalH - 0.5f);
            if (hasBars)
            {
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
            }

            if (final && dstW > 0.0f && dstH > 0.0f)
            {
                // Filter pick for the upscale:
                //   - Pure 1:1 (FollowWindow at native aspect, or
                //     Fixed with the window exactly the render size):
                //     either filter is identical; pick Nearest as
                //     a safe default to avoid an accidental smear if
                //     fractional rounding put dst 0.5 px off.
                //   - Integer-multiple upscale (dst is an exact NxN
                //     enlargement of the render target): Nearest is
                //     the right call — keeps pixel art crisp.
                //   - Otherwise: Linear, since nearest-neighbour at a
                //     non-integer scale produces visible row/column
                //     duplication artefacts.
                auto *colorTex = const_cast<Texture2D *>(&final->color());
                const float scaleX = dstW / (float)m_targetW;
                const float scaleY = dstH / (float)m_targetH;
                auto nearInt = [](float s) -> bool
                {
                    const float r = std::round(s);
                    return r >= 1.0f && std::abs(s - r) < 1e-3f;
                };
                const bool isIntegerScale =
                    nearInt(scaleX) && nearInt(scaleY) &&
                    std::abs(scaleX - scaleY) < 1e-3f;
                colorTex->setFilter(isIntegerScale
                                        ? TextureFilter::Nearest
                                        : TextureFilter::Linear);

                // Composite blit is the one place we deliberately run
                // the batcher in window space — the rest of the frame
                // runs in render space.
                m_batcher.begin(m_physicalW, m_physicalH);
                m_batcher.drawTextured(colorTex,
                                       dstX, dstY,
                                       dstW, dstH,
                                       0.0f, 1.0f, 1.0f, 0.0f,
                                       0xFFFFFFFFu, 0);
                m_batcher.flush();

                // Restore Linear so other stages that consume this
                // target in subsequent frames keep their filtering.
                colorTex->setFilter(TextureFilter::Linear);
            }

            SDL_GL_SwapWindow(m_window);
        }

    }
}