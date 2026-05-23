/**
 * Bokken.cpp
 *
 * Implementation of the public entry point declared in include/Bokken.hpp.
 *
 * Keeping the body here (rather than inline in the header) means the
 * public surface is a single function declaration. Consumers don't need
 * to provide the transitive include paths the body touches (SDL, GLM,
 * glad, PhysFS, QuickJS, nlohmann/json, ...): their build files collapse
 * to one include directory plus one library link.
 */

#include "../include/Bokken.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <string>

#include <nlohmann/json.hpp>

#include "Configuration.hpp"
#include "Loop.hpp"
#include "AssetPack.hpp"

// Built-in scripting modules.
#include "scripting/modules/Audio.hpp"
#include "scripting/modules/Canvas.hpp"
#include "scripting/modules/EngineModule.hpp"
#include "scripting/modules/GameObject.hpp"
#include "scripting/modules/Input.hpp"
#include "scripting/modules/Log.hpp"
#include "scripting/modules/Physics.hpp"
#include "scripting/modules/Window.hpp"
#include "scripting/modules/Renderer.hpp"

// Compile-time defaults — see the doc-comment in Bokken.hpp for the
// override convention. These #ifndef guards stay in case someone passes
// -DBOKKEN_FIXED_HZ=120 (or similar) to the engine's CMake configure.
#ifndef BOKKEN_PROJECT_PATH
#define BOKKEN_PROJECT_PATH "project.bokken"
#endif
#ifndef BOKKEN_SCRIPT_PACK
#define BOKKEN_SCRIPT_PACK "assets/scripts.assetpack"
#endif
#ifndef BOKKEN_ENVIRONMENT
#define BOKKEN_ENVIRONMENT "development"
#endif
#ifndef BOKKEN_FIXED_HZ
#define BOKKEN_FIXED_HZ 60
#endif
#ifndef BOKKEN_ENTRY_SCRIPT
#define BOKKEN_ENTRY_SCRIPT "/scripts/index.script"
#endif

namespace Bokken
{
    // TU-local helper: static here since no other translation unit
    // needs it.
    static std::string readTextFile(const std::string &path)
    {
        std::ifstream f(path);
        if (!f)
            return {};
        return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    }

    int entryPoint(int /*argc*/, char *argv[])
    {
        // PhysFS needs the real executable path so it can resolve relative paths.
        PHYSFS_init(argv[0]);

        // 1. Load and parse project configuration (plain JSON, not packed)
        std::string configText = readTextFile(BOKKEN_PROJECT_PATH);
        if (configText.empty())
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] Could not read project file: %s\n", BOKKEN_PROJECT_PATH);
            PHYSFS_deinit();
            return EXIT_FAILURE;
        }

        ProjectConfiguration configuration;
        try
        {
            nlohmann::json j = nlohmann::json::parse(configText);
            configuration = j.get<ProjectConfiguration>();
        }
        catch (const std::exception &ex)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] Failed to parse project configuration: %s\n", ex.what());
            PHYSFS_deinit();
            return EXIT_FAILURE;
        }

        // 2. Mount asset packs via PhysFS.
        //
        //    PhysFS was already initialised above with argv[0]. The AssetPack
        //    constructor calls PHYSFS_init again, which is a harmless no-op
        //    once PhysFS is already up (PhysFS does NOT reference-count
        //    init/deinit — a single deinit tears everything down). AssetPack's
        //    destructor is therefore the ONE place PHYSFS_deinit happens on
        //    the success path, and because `assets` is declared before `loop`
        //    it runs after ~Loop, so the renderer's font streams (PhysFS-backed
        //    SDL_IOStreams) are closed while PhysFS is still alive.
        AssetPack assets;
        if (!assets.mount(BOKKEN_SCRIPT_PACK, "/"))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] Failed to mount scripts asset pack: %s\n", BOKKEN_SCRIPT_PACK);
            return EXIT_FAILURE;
        }

        // Optional asset packs. mountOptional() succeeds when the pack
        // file is genuinely absent (project hasn't shipped that asset
        // category yet) and only fails when a present pack file is
        // corrupt or unparseable. This lets a fresh project run without
        // every category populated, while still surfacing real
        // packaging errors loudly.
        //
        // Fonts is the only optional-looking category that's actually
        // mandatory: Canvas / Label initialise their glyph caches at
        // engine startup and cannot recover if no fonts are available.
        // Everything else (audio, sprites, textures, models, scenes)
        // can be missing without preventing the game from running.
        if (!assets.mountOptional("assets/audio.assetpack", "/audio"))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] Failed to mount audio asset pack\n");
            return EXIT_FAILURE;
        }

        // Open the SDL audio device and resume playback. Without this
        // call the mixer's voice/channel bookkeeping all works (you
        // can play() and the voice slots fill up) but no samples ever
        // leave the program — SDL_OpenAudioDeviceStream is what binds
        // the mixer's audioCallback to the OS output.
        if (!Bokken::Audio::Mixer::get().start())
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] Audio mixer failed to start — game will run silent\n");
            // Not fatal: a game without audio is degraded but
            // playable. The mixer's play() calls will still succeed
            // (they're independent of the device); they just produce
            // no output.
        }
        if (!assets.mount("assets/fonts.assetpack", "/fonts"))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] Failed to mount fonts asset pack\n");
            return EXIT_FAILURE;
        }
        if (!assets.mountOptional("assets/models.assetpack", "/models"))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] Failed to mount models asset pack\n");
            return EXIT_FAILURE;
        }
        if (!assets.mountOptional("assets/scenes.assetpack", "/scenes"))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] Failed to mount scenes asset pack\n");
            return EXIT_FAILURE;
        }
        if (!assets.mountOptional("assets/sprites.assetpack", "/sprites"))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] Failed to mount sprites asset pack\n");
            return EXIT_FAILURE;
        }
        if (!assets.mountOptional("assets/textures.assetpack", "/textures"))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] Failed to mount textures asset pack\n");
            return EXIT_FAILURE;
        }

        // 3. Initialise the engine loop.
        Loop loop;
        if (!loop.init(configuration, BOKKEN_ENVIRONMENT, BOKKEN_FIXED_HZ, &assets))
            return EXIT_FAILURE;

        // Register the built-in scripting modules. installModules() is the
        // single source of truth for the module set and registration order,
        // shared with the live-reload path so the two cannot drift.
        loop.installModules();

        // Remember the entry script's virtual path so a live reload can
        // re-read freshly packed bytecode, and open the development control
        // channel in development environments so bokken-cli can drive hot
        // reload. Production builds skip the channel entirely.
        loop.setEntryScriptPath(BOKKEN_ENTRY_SCRIPT);
        if (std::string(BOKKEN_ENVIRONMENT) != "production")
            loop.enableDevelopmentReload(7878);

        // 4. Entry script.
        if (!assets.exists(BOKKEN_ENTRY_SCRIPT))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] Entry script not found in pack: %s\n", BOKKEN_ENTRY_SCRIPT);
            return EXIT_FAILURE;
        }

        SDL_IOStream *scriptIO = assets.openIOStream(BOKKEN_ENTRY_SCRIPT);
        if (!scriptIO)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] Failed to open entry script stream\n");
            return EXIT_FAILURE;
        }

        size_t scriptLen = 0;
        void *scriptRaw = SDL_LoadFile_IO(scriptIO, &scriptLen, true);

        if (!scriptRaw || scriptLen == 0)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] SDL_LoadFile_IO failed: %s\n", SDL_GetError());
            return EXIT_FAILURE;
        }

        std::vector<uint8_t> scriptData(
            static_cast<uint8_t *>(scriptRaw),
            static_cast<uint8_t *>(scriptRaw) + scriptLen);

        SDL_free(scriptRaw);

        if (!loop.loadBytecode(scriptData.data(), scriptData.size(), BOKKEN_ENTRY_SCRIPT))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[Bokken] Failed to load script bytecode.\n");
            return EXIT_FAILURE;
        }

        loop.run();

        // Tear down audio before the file system so the mixer can drain the
        // device cleanly and any voices currently holding shared_ptr
        // references into the asset pack release them before the packs are
        // unmounted.
        Bokken::Audio::Mixer::get().stop();

        // NOTE: do NOT call PHYSFS_deinit() here.
        //
        // PhysFS does not reference-count init/deinit — a single deinit tears
        // the whole subsystem down no matter how many times init was called.
        // `loop` and `assets` are both still alive at this point and are
        // destroyed at scope exit, in reverse declaration order: ~Loop first
        // (its renderer's GlyphCache closes TTF fonts that are backed by
        // PhysFS-mounted SDL_IOStreams — i.e. it calls PHYSFS_close), then
        // ~AssetPack (which unmounts every pack and deinits PhysFS exactly
        // once). Deiniting here would pull PhysFS out from under those font
        // streams and crash in PHYSFS_close during ~GlyphCache.
        //
        // The AssetPack destructor is the single owner of PHYSFS_deinit, and
        // because `assets` is declared before `loop` it is guaranteed to run
        // last. Nothing else should deinit PhysFS.
        return EXIT_SUCCESS;
    }

} // namespace Bokken