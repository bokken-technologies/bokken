#pragma once

#include "Configuration.hpp"
#include "AssetPack.hpp"
#include "DevelopmentChannel.hpp"
#include "./physics/World.hpp"
#include "./renderer/Base.hpp"
#include "./scripting/modules/Canvas.hpp"
#include "./scripting/Engine.hpp"
#include "./scripting/modules/GameObject.hpp"
#include "./scripting/modules/Input.hpp"
#include "./scripting/modules/Network.hpp"
#include "./scripting/modules/Renderer.hpp"
#include "./scripting/modules/Window.hpp"
#include "./game_object/Animation2D.hpp"
#include "./game_object/Distortion2D.hpp"
#include "./canvas/components/Label.hpp"
#include "./renderer/stages/SpriteStage.hpp"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <memory>
#include <string>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <charconv>

namespace Bokken
{

    /**
     * Drives the main game loop using SDL3.
     *
     * Rendering architecture:
     *   - Window is created with SDL_WINDOW_OPENGL (mandatory).
     *   - There is no SDL_Renderer; our Renderer owns the GL context,
     *     SpriteBatcher, GlyphCache, and Pipeline.
     *   - tick() calls renderer.beginFrame()/endFrame() rather than
     *     SDL_RenderClear / SDL_RenderPresent.
     *
     * Caller usage:
     *   Loop loop;
     *   loop.init(config, "development", 60, &assets);
     *   loop.loadBytecode(data, len, "index.script");
     *   loop.run();
    */
    class Loop
    {
    public:
        Loop() = default;
        ~Loop() { shutdown(); }

        Loop(const Loop &) = delete;
        Loop &operator=(const Loop &) = delete;

        bool init(const ProjectConfiguration &config,
                  const std::string &environment = "development",
                  int fixedHz = 60,
                  AssetPack *assets = nullptr);

        bool loadBytecode(const uint8_t *data, size_t len, const std::string &name);

        Scripting::Engine &scriptingEngine() { return Scripting::Engine::Instance(); }

        /**
         * Register the built-in scripting modules on the scripting engine.
         *
         * Called once after init() during startup, and again after the
         * engine is re-initialised during a live script reload. Keeping
         * the registration in one place guarantees the reload path installs
         * exactly the same modules, in the same order, as cold start —
         * which is the single most important invariant for reload
         * correctness. Uses the loop's own window, renderer, and asset pack,
         * so it has no dependency on the caller's locals.
        */
        void installModules();

        /**
         * Enable the development-only live-reload control channel on the
         * given localhost port. Must be called after init(). The channel
         * listens for reload requests from bokken-cli and delivers them to
         * the main loop as SDL user events. Has no effect in builds where
         * the channel cannot open a socket (live reload is then simply
         * unavailable). Safe to skip entirely for shipping builds.
        */
        void enableDevelopmentReload(int port);

        /** Remember the entry script's virtual path so a live reload can
         *  re-read the freshly packed bytecode from the asset pack. */
        void setEntryScriptPath(const std::string &path) { m_entryScriptPath = path; }

        void run();
        void requestQuit() { m_quit = true; }

        SDL_Window *window() const { return m_window; }
        Renderer::Base *renderer() { return m_renderer.get(); }

    private:
        SDL_Window *m_window = nullptr;
        std::unique_ptr<Renderer::Base> m_renderer;
        AssetPack *m_assets = nullptr;

        bool m_quit = false;
        bool m_initialised = false;

        // Timing.
        double m_fixedStep = 0.02;
        double m_fixedAccum = 0.0;
        Uint64 m_lastTick = 0;

        static constexpr double k_maxDeltaTime = 0.25;

        // Cached clear color from configuration (linear sRGB-ish, 0..1).
        float m_clearR = 0.075f, m_clearG = 0.090f, m_clearB = 0.105f;

        // Configuration the scripting engine was initialised with, kept so
        // a live reload can re-init the engine with identical settings.
        ScriptingEngine m_scriptingConfiguration;

        // Entry script virtual path (e.g. "/scripts/index.script"), used by
        // reloadScripts() to re-read freshly packed bytecode.
        std::string m_entryScriptPath;

        // Live-reload control channel. Only started when
        // enableDevelopmentReload() is called; otherwise null and the loop
        // behaves exactly as a shipping build.
        std::unique_ptr<DevelopmentChannel> m_developmentChannel;

        void processEvents();
        void tick();
        void shutdown();

        // Live-reload handlers, run on the main thread from processEvents().
        // reloadAssets() remounts changed packs and refreshes the texture
        // cache without disturbing the script world; reloadScripts()
        // rebuilds the JS world from the freshly packed scripts archive
        // while keeping the window and GL context alive.
        void reloadAssets();
        void reloadScripts();

        static bool parseClearColor(const std::string &hex,
                                    uint8_t &r, uint8_t &g, uint8_t &b);
    };

} // namespace Bokken