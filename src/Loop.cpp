#include "Loop.hpp"

#include "scripting/modules/Audio.hpp"
#include "scripting/modules/EngineModule.hpp"
#include "scripting/modules/Log.hpp"
#include "scripting/modules/Physics.hpp"
#include "game_object/Base.hpp"

namespace Bokken
{

    bool Loop::init(const ProjectConfiguration &configuration,
                    const std::string &environment,
                    int fixedHz,
                    AssetPack *assets)
    {
        m_assets = assets;

        // Resolve environment overrides.
        const EnvironmentConfiguration *environmentConfiguration = nullptr;
        try
        {
            environmentConfiguration = &configuration.get_environment(environment == "production");
        }
        catch (...)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] Unknown environment '%s', defaulting to development\n",
                         environment.c_str());
            environmentConfiguration = &configuration.get_environment(false);
        }

        const WindowSettings &window = configuration.windowBase;
        const auto &windowOverrides = environmentConfiguration->windowOverrides;
        const auto &scriptingEngineConfiguration = environmentConfiguration->scriptingEngine;

        // SDL3 init — video + events. Audio comes via the audio module.
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] SDL_Init failed: %s\n", SDL_GetError());
            return false;
        }

        if (!TTF_Init())
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TTF_Init failed: %s", SDL_GetError());
            SDL_Quit();
            return false;
        }

        // Window flags. SDL_WINDOW_OPENGL is mandatory now — the GL
        // renderer needs a GL-capable surface to attach to.
        //
        // SDL_WINDOW_HIDDEN: we create the window invisibly and only
        // show it AFTER the first frame has been rendered + presented.
        // Without this, the window appears as a black rectangle the
        // moment SDL_CreateWindow returns and stays black for however
        // long the chain of (renderer init → atlas creation → JS
        // engine startup → React tree construction → first layout →
        // shader compilation on first draw → first SwapWindow) takes
        // — typically 200–500ms on a cold start. That's the half-
        // second of black the user sees. Showing the window only
        // after the first present means the user sees the populated
        // UI appear in one step instead of "black, then UI".
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN;

        if (windowOverrides.isFullscreen)
            flags |= SDL_WINDOW_FULLSCREEN;
        if (windowOverrides.isBorderlessFullscreen)
            flags |= SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS;
        if (windowOverrides.alwaysOnTop)
            flags |= SDL_WINDOW_ALWAYS_ON_TOP;
        if (windowOverrides.transparent)
            flags |= SDL_WINDOW_TRANSPARENT;

        int createW = window.width;
        int createH = window.height;

        if (windowOverrides.useNativeResolution)
        {
            SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();
            const SDL_DisplayMode *desktopMode = SDL_GetDesktopDisplayMode(primaryDisplay);
            if (desktopMode)
            {
                /* SDL_GetDesktopDisplayMode reports the display's mode in
                 * physical pixels (e.g. 2560x1440 on a 2x Retina display).
                 * Divide by the content scale to get back to logical
                 * coordinates before handing this to SDL_CreateWindow —
                 * otherwise HIGH_PIXEL_DENSITY would double-apply the
                 * scale and create a window at 2x the intended on-screen
                 * size. */
                const float contentScale = SDL_GetDisplayContentScale(primaryDisplay);
                const float scale = (contentScale > 0.0f) ? contentScale : 1.0f;

                createW = (int)((float)desktopMode->w / scale + 0.5f);
                createH = (int)((float)desktopMode->h / scale + 0.5f);
            }
        }

        m_window = SDL_CreateWindow(
            configuration.general.displayTitle.c_str(),
            createW, createH,
            flags);

        if (!m_window)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] SDL_CreateWindow failed: %s\n", SDL_GetError());
            TTF_Quit();
            SDL_Quit();
            return false;
        }

        SDL_SetWindowMinimumSize(m_window, 640, 480);

        // Cache clear color in 0..1 floats. SpriteStage will pick this
        // up below.
        uint8_t cr = 19, cg = 23, cb = 27;
        if (parseClearColor(window.clearColor, cr, cg, cb))
        {
            m_clearR = cr / 255.0f;
            m_clearG = cg / 255.0f;
            m_clearB = cb / 255.0f;
        }

        // Create our renderer — this is where the GL context is born.
        m_renderer = std::make_unique<Renderer::Base>();
        if (!m_renderer->init(m_window, m_assets))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] Renderer::init failed");
            SDL_DestroyWindow(m_window);
            TTF_Quit();
            SDL_Quit();
            return false;
        }

        int physicalW = 0, physicalH = 0;
        SDL_GetWindowSizeInPixels(m_window, &physicalW, &physicalH);

        int logicalW = 0, logicalH = 0;
        SDL_GetWindowSize(m_window, &logicalW, &logicalH);

        const float dpiScale = (logicalW > 0) ? (float)physicalW / (float)logicalW : 1.0f;

        if (physicalW > 0 && physicalH > 0)
        {
            m_renderer->setRenderSize(physicalW, physicalH,
                                      Renderer::RenderSizePolicy::Fixed);
        }

        // Apply configured clear color to the default sprite stage.
        if (auto *st = dynamic_cast<Renderer::SpriteStage *>(
                m_renderer->pipeline().findStage("sprite")))
        {
            st->clearR = m_clearR;
            st->clearG = m_clearG;
            st->clearB = m_clearB;
            st->clearA = 1.0f;
        }

        // Wire the renderer into the Canvas pieces that need it.
        Scripting::Modules::GameObject::setBatcher(&m_renderer->batcher());
        Scripting::Modules::GameObject::setTextureCache(&m_renderer->textures());
        Scripting::Modules::GameObject::setRenderer(m_renderer.get());
        Scripting::Modules::Canvas::setBatcher(&m_renderer->uiBatcher());
        Scripting::Modules::Canvas::setTextureCache(&m_renderer->textures());
        Canvas::Components::Label::s_glyphCache = &m_renderer->glyphs();

        // Wire the Window scripting module to the SDL window. The
        // module just needs the SDL_Window pointer for setTitle /
        // getSize / getLogicalSize; render-target sizing and resize
        // callbacks live on the Renderer scripting module now, which
        // subscribes to the renderer's render-size observer from its
        // constructor (registered in Bokken.cpp after init returns).
        Scripting::Modules::Window::attach(m_window);

        // Wire the texture cache and asset pack into Animation2D so
        // addClipFromGrid() can auto-load and slice sprite sheets.
        Bokken::GameObject::Animation2D::s_textureCache = &m_renderer->textures();
        Bokken::GameObject::Animation2D::s_assets = assets;

        // Wire the pipeline into Distortion2D so trigger() can find
        // the DistortionStage lazily at runtime. The renderer pointer
        // gives it access to render-space dimensions for the world →
        // normalised conversion so shockwaves originate at the right
        // place under any render-size policy.
        Bokken::GameObject::Distortion2D::s_pipeline = &m_renderer->pipeline();
        Bokken::GameObject::Distortion2D::s_renderer = m_renderer.get();

        // Physics world. Created before the scripting engine so that
        // any script-side module that touches Physics::World on first
        // import (the Physics module) sees a ready world. Failure here
        // is non-fatal — scripts that don't use physics will still run.
        if (!Bokken::Physics::World::get().init())
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[Bokken] Physics::World::init() failed; physics features disabled\n");
        }

        // Scripting engine.
        //
        // Remember the runtime configuration so a live script reload can
        // re-init the engine with identical heap / stack / GC settings.
        m_scriptingConfiguration = scriptingEngineConfiguration;
        if (!this->scriptingEngine().init(assets,
                                          scriptingEngineConfiguration.runtime.maxHeapSizeMb,
                                          scriptingEngineConfiguration.runtime.stackSizeKb,
                                          scriptingEngineConfiguration.runtime.gcThresholdKb))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] ScriptingEngine::init() failed\n");
            // Renderer::detach / Canvas::detach are idempotent — at
            // this point in init the Renderer scripting module isn't
            // constructed yet (that happens in Bokken.cpp after this
            // returns), so there's nothing to unhook. Call anyway for
            // symmetry with the orderly shutdown path below.
            Scripting::Modules::Canvas::detach();
            Scripting::Modules::Renderer::detach();
            Scripting::Modules::Window::detach();
            m_renderer.reset();
            SDL_DestroyWindow(m_window);
            TTF_Quit();
            SDL_Quit();
            return false;
        }

        // Timing.
        m_fixedStep = (fixedHz > 0) ? (1.0 / fixedHz) : 0.02;
        m_fixedAccum = 0.0;
        m_lastTick = SDL_GetTicksNS();

        m_initialised = true;
        m_quit = false;

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[Bokken] Engine initialised — %s v%s (%s)\n",
                    configuration.general.displayTitle.c_str(),
                    configuration.general.projectVersion.c_str(),
                    environment.c_str());
        return true;
    }

    bool Loop::loadBytecode(const uint8_t *data, size_t len, const std::string &name)
    {
        if (!m_initialised)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] loadBytecode() called before init()\n");
            return false;
        }
        return this->scriptingEngine().loadBytecode(data, len, name);
    }

    void Loop::run()
    {
        auto &engine = this->scriptingEngine();
        if (!m_initialised)
            return;
        if (!engine.isReady())
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] ScriptingEngine is not ready!\n");
            return;
        }

        // Subscribe Canvas to the renderer's render-size observer
        // so the layout reflows when the render target changes size.
        // Has to happen after the Renderer scripting module is
        // constructed (in Bokken.cpp before Loop::run is called),
        // since Canvas::attach reads Renderer::renderer() to find
        // the pointer to subscribe on.
        Scripting::Modules::Canvas::attach();

        engine.callOnStart();

        /* If onStart called Canvas.render(<App/>), that just marked
         * the tree as dirty (render batching defers the actual render
         * to the next frame). Force a flush now so the warmup frame
         * below has a real tree to paint, otherwise the user would
         * see one blank frame between window-show and first real
         * render. */
        Scripting::Modules::Canvas::flush_pending_render();

        /* Warm-up frame: with the window still hidden, run one full
         * render → present cycle. This forces:
         *
         *   - GL shader compilation (sprite SDF, FXAA, glyph paths)
         *     which can be 100–300 ms on macOS the first time the
         *     driver sees them.
         *   - Glyph atlas population for whatever text the initial
         *     screen renders — these get rasterised and uploaded on
         *     first use of each codepoint.
         *   - Layout pass on the freshly-built JS tree.
         *   - SDF rounded-rect quad shaping for the first frame.
         *
         * Doing all of this with the window hidden means the user
         * never sees the half-second black flash between window
         * creation and the first painted UI. After SwapWindow returns
         * the framebuffer is populated; SDL_ShowWindow then reveals
         * it in one step.
         *
         * We deliberately skip the input/event pump and the dt
         * accumulation here — this isn't a "real" game tick, just a
         * pre-paint to fill the framebuffer.
         *
         * Note: beginFrame() will fire the render-size-changed
         * observer for the first time here (initial dispatch — the
         * "lastFired" sentinels are -1, so any positive render size
         * counts as a change). That means any Renderer.onResize
         * handler the script registered during onStart will fire
         * once on the warmup frame with the initial render size.
         * This is fine — it matches the convention "you get told
         * what the size is when you start listening" and avoids
         * scripts having to query getRenderSize() separately. */
        m_renderer->beginFrame();
        Scripting::Modules::GameObject::present();
        Scripting::Modules::Canvas::present();
        m_renderer->endFrame(0.0f);

        SDL_ShowWindow(m_window);

        m_lastTick = SDL_GetTicksNS();

        while (!m_quit)
        {
            processEvents();
            if (m_quit)
                break;
            tick();
        }
    }

    void Loop::processEvents()
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            // Live-reload requests arrive as the development channel's
            // registered user event, pushed from its background thread.
            // Handling them here keeps all reload work on the main thread,
            // so no locking is needed around the renderer or script runtime.
            if (m_developmentChannel &&
                m_developmentChannel->eventType() != 0 &&
                e.type == m_developmentChannel->eventType())
            {
                const auto request =
                    static_cast<DevelopmentChannel::Request>(e.user.code);
                if (request == DevelopmentChannel::Request::ReloadAssets)
                    reloadAssets();
                else if (request == DevelopmentChannel::Request::ReloadScripts)
                    reloadScripts();
                continue;
            }

            Scripting::Modules::Canvas::handleEvent(e);
            Scripting::Modules::Input::handleEvent(e);

            switch (e.type)
            {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                m_quit = true;
                break;

            // Window-size-affecting events. We deliberately do NOT
            // call pipeline.resize() here — beginFrame() is the
            // single source of truth for pipeline sizing and runs
            // recomputeRenderSize() based on the chosen policy.
            // Forcing a resize here would race the per-frame logic
            // and (under Fixed policy) waste a resize since the
            // render dimensions don't actually change with the
            // window.
            //
            // The one thing we still want to do is refresh the
            // renderer's cached window dimensions immediately so any
            // code that reads them between now and the next
            // beginFrame() sees up-to-date values.
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                if (m_renderer)
                    m_renderer->updateSize();
                break;

            default:
                break;
            }
        }
    }

    void Loop::tick()
    {
        auto &engine = this->scriptingEngine();

        // dt
        Uint64 now = SDL_GetTicksNS();
        double dt = static_cast<double>(now - m_lastTick) * 1e-9;
        m_lastTick = now;
        if (dt > k_maxDeltaTime)
            dt = k_maxDeltaTime;

        // Note: Renderer.onResize listeners are no longer dispatched
        // from here. The renderer fires its render-size-changed
        // observer from inside beginFrame() (below) when the
        // dimensions actually change; the Renderer scripting module
        // subscribes to that observer at construction time and fans
        // out to JS callbacks. This means handlers fire AFTER user
        // onUpdate within the same frame instead of before — the
        // tradeoff for removing the polling-and-duplicated-resize-
        // logic path. If a handler needs to influence the same
        // frame's onUpdate it can still query Renderer.getRenderSize()
        // directly from onUpdate.

        // Fixed steps.
        m_fixedAccum += dt;
        while (m_fixedAccum >= m_fixedStep)
        {
            // Gameplay first — JS onFixedUpdate and native components
            // get to apply forces and tweak rigidbody state for this tick.
            engine.callOnFixedUpdate(m_fixedStep);
            Scripting::Modules::GameObject::fixedUpdate((float)m_fixedStep);

            // Then advance the physics world by exactly one fixed step.
            // step() is a no-op when the world wasn't initialised, so the
            // physics-disabled fallback path costs nothing.
            auto &physicsWorld = Bokken::Physics::World::get();
            physicsWorld.step((float)m_fixedStep);

            // Drain Box2D's event arrays into Behaviour callbacks and
            // any JS handlers attached to colliders. Must happen before
            // the next step() call — Box2D invalidates the event arrays
            // when a new step begins.
            physicsWorld.dispatchEvents();

            m_fixedAccum -= m_fixedStep;
        }

        // Variable updates.
        //
        // Tick the JS timer queue first so any setTimeout/setInterval
        // callbacks scheduled by user code fire before onUpdate sees
        // the new state. This matches browser semantics and means the
        // per-frame hook can safely react to state mutated by a timer
        // (e.g. an interval that drives an animation tick).
        engine.tickTimers(dt);

        Bokken::Scripting::Modules::Network::poll(engine.context());

        // Then fire any pending requestAnimationFrame callbacks. RAF
        // is the browser's "do this on the next paint" primitive, so
        // it logically belongs right before the variable update — the
        // animation tick has run, and onUpdate / present can now use
        // the up-to-date state.
        engine.fireAnimationFrames();

        engine.callOnUpdate(dt);
        Scripting::Modules::GameObject::update((float)dt);
        Scripting::Modules::Canvas::update((float)dt);

        // Render. beginFrame() commits any pending render-size change
        // (from a window resize, a setRenderSize call earlier in this
        // tick, or a policy change) and fires Renderer.onResize
        // listeners as a side effect.
        m_renderer->beginFrame();

        // Modules submit draws into m_renderer->batcher() during these calls.
        Scripting::Modules::GameObject::present();
        Scripting::Modules::Canvas::present();

        m_renderer->endFrame((float)dt);

        /* Frame cap fallback. With vsync engaged, SDL_GL_SwapWindow
         * inside endFrame() blocks on the GPU's next refresh, and we
         * idle naturally at ~60fps with low CPU. But if vsync couldn't
         * be enabled (older drivers, virtual machines, compositor
         * weirdness), the loop spins as fast as the GPU can present —
         * pinning a CPU core at 100%.
         *
         * As a safety net we sleep until the previous frame's start +
         * ~16.6ms, which caps us to 60fps and lets the scheduler park
         * the thread. The cost when vsync IS working: a no-op
         * because elapsed time already exceeds the target.
         *
         * We use SDL_DelayNS for precision — on macOS it uses
         * mach_wait_until and sleeps within ~100µs of the request. */
        constexpr Uint64 k_frameTargetNs = 16'666'667ULL; // ~60 Hz
        Uint64 elapsedNs = SDL_GetTicksNS() - now;
        if (elapsedNs + 1'000'000ULL < k_frameTargetNs)
        {
            SDL_DelayNS(k_frameTargetNs - elapsedNs - 1'000'000ULL);
        }
        else
        {
            SDL_Delay(0);
        }

        // Clear transient input state after the frame.
        Scripting::Modules::Input::endFrame();
    }

    void Loop::shutdown()
    {
        if (!m_initialised)
            return;
        auto &engine = this->scriptingEngine();

        // Tear down the JS world. engine.shutdown() frees the engine's own
        // cached handles (hooks, timers, rAF) and then calls destroy() on
        // every module — each releasing its OWN retained JS handles (the
        // GameObject scene, the Canvas hook system + interned atoms, the
        // Renderer's resize callbacks) while the context is still alive —
        // before freeing the context and runtime. This is the SAME path the
        // live reload uses, so quit and reload can't drift: anything that
        // must be released to satisfy QuickJS's gc_obj_list assertion is
        // released by the modules themselves, in one place.
        engine.shutdown();

        // Unhook the Renderer scripting module from the renderer's
        // render-size observer before the renderer is destroyed.
        // detach() calls removeRenderSizeListener on the live
        // renderer; running it after m_renderer.reset() would
        // dereference a dead pointer. Safe to call after
        // engine.shutdown() because detach() doesn't touch the
        // already-torn-down JSContexts — it just clears its own
        // bookkeeping vector.
        //
        // Canvas::detach also unhooks from the same observer (Canvas
        // subscribes for relayout-on-resize), so it must run before
        // m_renderer.reset() too.
        //
        // Window::detach() just drops the SDL_Window pointer and
        // has no ordering requirement against the renderer, but is
        // grouped here for symmetry.
        Scripting::Modules::Canvas::detach();
        Scripting::Modules::Renderer::detach();
        Scripting::Modules::Window::detach();

        // Tear down physics before the renderer / window. The world owns
        // every b2Body / b2Shape / b2Joint; destroying it here invalidates
        // all the handles still held by Rigidbody2D / Collider2D
        // components — those components' onDestroy paths guard with
        // b2*_IsValid so any later cleanup at static teardown is a no-op.
        Bokken::Physics::World::get().shutdown();

        // Drop renderer before the window — it owns the GL context bound to it.
        m_renderer.reset();

        if (m_window)
        {
            SDL_DestroyWindow(m_window);
            m_window = nullptr;
        }

        Scripting::Modules::Canvas::clear_font_cache();
        Canvas::Components::Label::clear_font_cache();

        TTF_Quit();
        SDL_Quit();
        m_initialised = false;

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[Bokken] Engine shutdown complete.\n");
    }

    void Loop::installModules()
    {
        // Registration order and module set MUST match Bokken.cpp's cold
        // start. Both cold start and live reload call this one function so
        // they cannot drift. The window / renderer / asset-pack pointers
        // come from the loop's own members, which a script reload keeps
        // alive across the engine re-init.
        auto &engine = this->scriptingEngine();
        engine.addModule(std::make_unique<Scripting::Modules::Audio>());
        engine.addModule(std::make_unique<Scripting::Modules::Canvas>(m_window, m_assets));
        engine.addModule(std::make_unique<Scripting::Modules::EngineModule>());
        engine.addModule(std::make_unique<Scripting::Modules::GameObject>(m_window, m_assets));
        engine.addModule(std::make_unique<Scripting::Modules::Input>());
        engine.addModule(std::make_unique<Scripting::Modules::Log>());
        engine.addModule(std::make_unique<Scripting::Modules::Network>());
        engine.addModule(std::make_unique<Scripting::Modules::Physics>());
        engine.addModule(std::make_unique<Scripting::Modules::Renderer>(m_renderer.get(), m_assets));
        engine.addModule(std::make_unique<Scripting::Modules::Window>());
    }

    void Loop::enableDevelopmentReload(int port)
    {
        if (!m_initialised)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[Bokken] enableDevelopmentReload() called before init(); ignored\n");
            return;
        }
        m_developmentChannel = std::make_unique<DevelopmentChannel>();
        if (!m_developmentChannel->start(port))
        {
            // Non-fatal: the game runs normally, just without live reload.
            m_developmentChannel.reset();
        }
    }

    void Loop::reloadAssets()
    {
        // Asset-only reload. Remount the packs so PhysFS re-reads the
        // freshly written .assetpack archives, then drop cached GPU uploads
        // so the next draw re-pulls any changed image. The script world is
        // left entirely untouched, so this is cheap and safe.
        if (m_assets)
            m_assets->remountAll();
        if (m_renderer)
            m_renderer->textures().clear();

        // Force the UI to repaint with whatever changed.
        Scripting::Modules::Canvas::flush_pending_render();

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "[Bokken] Live reload: assets refreshed.\n");
    }

    void Loop::reloadScripts()
    {
        if (m_entryScriptPath.empty())
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[Bokken] Live reload: no entry script path set; "
                        "call setEntryScriptPath() to enable script reload\n");
            return;
        }

        auto &engine = this->scriptingEngine();

        // 0. Ask the current script to hand back any state it wants to keep
        //    across the reload (player position, current scene, …). Captured
        //    as a JSON string so it survives the runtime teardown. A script
        //    that exports nothing simply restarts fresh.
        const std::string preservedState = engine.saveHotReloadState();

        // 1. Tear down the JS world. shutdown() frees the engine's own
        //    cached hook values, outstanding timers / animation-frame
        //    callbacks, then calls destroy() on every module — each module
        //    releases its OWN retained JS handles there (the GameObject
        //    scene, the Canvas hook system + interned atoms, the Renderer's
        //    resize callbacks) while the context is still alive — and finally
        //    frees the context and runtime. The renderer, window, and GL
        //    context are NOT touched, so the surviving renderer is reused.
        engine.shutdown();

        // 2. Unhook Canvas from the renderer's render-size observer. The JS
        //    tree state (node pointers, hooks, atoms) was already released by
        //    Canvas::destroy() inside engine.shutdown(); detach() here is only
        //    about the native observer subscription, which attach() re-adds
        //    below against the surviving renderer.
        Scripting::Modules::Canvas::detach();

        // 3. Re-initialise the runtime with the same configuration the cold
        //    start used.
        if (!engine.init(m_assets,
                         m_scriptingConfiguration.runtime.maxHeapSizeMb,
                         m_scriptingConfiguration.runtime.stackSizeKb,
                         m_scriptingConfiguration.runtime.gcThresholdKb))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] Live reload: engine re-init failed; "
                         "the game may be in a bad state\n");
            return;
        }

        // 4. Re-register the modules (identical set / order to cold start)
        //    and re-wire the renderer-backed pointers. Those pointers target
        //    the surviving renderer, so they remain valid; re-setting them
        //    is defensive in case a module reset them on destroy().
        installModules();
        Scripting::Modules::Canvas::setBatcher(&m_renderer->batcher());
        Scripting::Modules::Canvas::setTextureCache(&m_renderer->textures());
        Scripting::Modules::GameObject::setBatcher(&m_renderer->batcher());
        Scripting::Modules::GameObject::setTextureCache(&m_renderer->textures());
        Scripting::Modules::GameObject::setRenderer(m_renderer.get());
        Canvas::Components::Label::s_glyphCache = &m_renderer->glyphs();
        Scripting::Modules::Window::attach(m_window);

        // 5. Remount the packs so PhysFS sees the freshly written
        //    scripts.assetpack, then read the new bytecode and load it.
        if (m_assets)
            m_assets->remountAll();

        if (!m_assets || !m_assets->exists(m_entryScriptPath))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] Live reload: entry script '%s' not found in pack\n",
                         m_entryScriptPath.c_str());
            return;
        }

        SDL_IOStream *scriptIO = m_assets->openIOStream(m_entryScriptPath);
        if (!scriptIO)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] Live reload: failed to open entry script stream\n");
            return;
        }

        size_t scriptLen = 0;
        void *scriptRaw = SDL_LoadFile_IO(scriptIO, &scriptLen, true);
        if (!scriptRaw || scriptLen == 0)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] Live reload: SDL_LoadFile_IO failed: %s\n",
                         SDL_GetError());
            return;
        }

        const bool loaded = engine.loadBytecode(
            static_cast<uint8_t *>(scriptRaw), scriptLen, m_entryScriptPath);
        SDL_free(scriptRaw);

        if (!loaded)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] Live reload: failed to load bytecode; "
                         "keeping the window alive so you can fix and re-save\n");
            return;
        }

        // 6. Re-attach Canvas to the renderer's size observer, run the new
        //    script's onStart, hand back any preserved state, and force a
        //    render so the new tree paints this frame.
        Scripting::Modules::Canvas::attach();
        engine.callOnStart();
        engine.restoreHotReloadState(preservedState);
        Scripting::Modules::Canvas::flush_pending_render();

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "[Bokken] Live reload: scripts reloaded.\n");
    }

    bool Loop::parseClearColor(const std::string &hex,
                               uint8_t &r, uint8_t &g, uint8_t &b)
    {
        if (hex.size() != 7 || hex[0] != '#')
            return false;
        auto hexByte = [&](size_t pos, uint8_t &out) -> bool
        {
            uint8_t val = 0;
            auto [ptr, ec] = std::from_chars(hex.data() + pos,
                                             hex.data() + pos + 2, val, 16);
            if (ec != std::errc())
                return false;
            out = val;
            return true;
        };
        return hexByte(1, r) && hexByte(3, g) && hexByte(5, b);
    }

} // namespace Bokken