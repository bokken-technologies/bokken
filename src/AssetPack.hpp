#pragma once

#include <SDL3/SDL.h>

#include <physfs.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <filesystem>

namespace Bokken
{
    /**
     * AssetPack
     *
     * Mounts a .assetpack file (zip archive produced by BokkenPacker) into the
     * PhysFS virtual filesystem and provides SDL_IOStream-compatible access to
     * every file inside it.
     *
     * Layout inside an .assetpack (as written by BokkenPacker):
     *
     *   scripts/index.script       ← QuickJS bytecode (entry point)
     *   scripts/components/MyComponent.script
     *   textures/low/...
     *   textures/high/...
     *   sounds/...
     *   fonts/...
     *
     * Usage:
     *   AssetPack pack;
     *   pack.mount("data/scripts.assetpack", "/scripts");
     *   pack.mount("data/textures_high.assetpack", "/textures");
     *
     *   // Read bytecode into a byte vector:
     *   auto bc = pack.readBytes("/scripts/scripts/index.script");
     *
     *   // Or wrap as SDL_IOStream for SDL_image / SDL_mixer etc.:
     *   SDL_IOStream* io = pack.openIOStream("/textures/player.png");
     *   // ... use io ...
     *   SDL_CloseIO(io);
     *
     *   pack.unmountAll(); // or just let the destructor handle it
    */
    class AssetPack
    {
    public:
        AssetPack();
        ~AssetPack();

        // Non-copyable, movable.
        AssetPack(const AssetPack &) = delete;
        AssetPack &operator=(const AssetPack &) = delete;
        AssetPack(AssetPack &&) = default;

        /**
         * Mount a .assetpack archive into the virtual FS at the given mount point.
         * Multiple packs can be mounted at the same or different mount points.
         * If two packs have a file at the same virtual path, the last-mounted one wins
         * (PHYSFS_APPEND behaviour).
         *
         * @param packPath   Real filesystem path to the .assetpack file.
         * @param mountPoint Virtual path prefix (e.g. "/scripts", "/textures").
         *                   Use "/" to mount at the root.
         * @return true on success.
        */
        bool mount(const std::string &packPath,
                   const std::string &mountPoint = "/");

        /**
         * Optional-mount variant. Returns true when the pack is missing
         * entirely from the filesystem — the assumption being that the
         * project simply doesn't ship that asset category yet (no audio,
         * no models, etc.). A pack file that exists but fails to mount
         * (corrupt / wrong format / permission denied) still returns
         * false because that signals a real build problem.
         *
         * Use this for asset categories the project may legitimately
         * omit. Use mount() (no optional flag) for packs the engine
         * cannot run without — scripts, fonts, anything mandatory.
        */
        bool mountOptional(const std::string &packPath,
                           const std::string &mountPoint = "/");

        /**
         * Unmount a previously mounted pack.
         * @param packPath The same real path used in mount().
        */
        void unmount(const std::string &packPath);

        /** Unmount every pack that was mounted through this AssetPack instance. */
        void unmountAll();

        /**
         * Unmount and re-mount every currently mounted pack at its original
         * mount point. Used by live reload: after the build tool rewrites
         * one or more .assetpack files on disk, remounting makes PhysFS
         * re-read their archive directories so the new contents become
         * visible. Mounts are reproduced in their original order so
         * last-mounted-wins precedence is preserved.
        */
        void remountAll();

        /**
         * Check whether a virtual path exists in any mounted pack.
        */
        bool exists(const std::string &virtualPath) const;

        /**
         * Resolve a logical asset name to a virtual path.
         *
         * Packs written by bokken-cli embed a "__aliases.json" table mapping
         * each file's stem (basename without extension) to its full virtual
         * path, so a script can ask for "player" without knowing which
         * folder or mount the file lives under. resolve() consults those
         * tables across all mounted packs and returns the mapped virtual
         * path. If the name is not an alias it is returned unchanged, so a
         * caller may always pass either a logical name or a literal virtual
         * path. The alias tables are loaded once on first use and cached.
        */
        std::string resolve(const std::string &name) const;

        /**
         * Read the entire contents of a virtual file into a byte vector.
         * Returns an empty vector on failure and logs the error.
        */
        std::vector<uint8_t> readBytes(const std::string &virtualPath) const;

        /**
         * Open a virtual file as an SDL_IOStream.
         * The caller is responsible for calling SDL_CloseIO() when done.
         * Returns nullptr on failure.
         *
         * This is the correct way to feed packed assets into SDL_image, SDL_mixer,
         * SDL_ttf, or any SDL3 subsystem that accepts SDL_IOStream*.
        */
        SDL_IOStream *openIOStream(const std::string &virtualPath) const;

        /**
         * Enumerate all files under a virtual directory.
         * Useful for batch-loading scenes, fonts, etc.
         *
         * @param virtualDir  Virtual directory path (e.g. "/script").
         * @param callback    Called for each entry with the full virtual path.
         * @param recursive   If true, recurse into subdirectories.
        */
        void enumerate(const std::string &virtualDir,
                       const std::function<void(const std::string &)> &callback,
                       bool recursive = false) const;

    private:
        // Track every (real path, mount point) pair so unmountAll() and
        // remountAll() can reproduce the exact mount layout. Storing the
        // mount point alongside the path is what lets a live reload remount
        // a pack at the same virtual location after its file changed on disk.
        struct MountedPack
        {
            std::string realPath;
            std::string mountPoint;
        };
        std::vector<MountedPack> m_mounted;

        // Lazily-loaded alias table aggregated from every mounted pack's
        // "__aliases.json". Maps logical name -> virtual path. Mutable so
        // resolve() can populate it on first use from a const method, and
        // invalidated whenever the mount set changes.
        mutable std::unordered_map<std::string, std::string> m_aliases;
        mutable bool m_aliasesLoaded = false;

        // Internal: create an SDL_IOStream backed by a PHYSFS_File.
        static SDL_IOStream *wrapPhysFSFile(PHYSFS_File *file,
                                            const std::string &virtualPath);

        // SDL_IOStream callback implementations for PhysFS.
        static Sint64 io_size(void *userdata);
        static Sint64 io_seek(void *userdata, Sint64 offset, SDL_IOWhence whence);
        static size_t io_read(void *userdata, void *ptr, size_t size, SDL_IOStatus *status);
        static size_t io_write(void *userdata, const void *ptr, size_t size, SDL_IOStatus *status);
        static bool io_close(void *userdata);
    };

} // namespace Bokken
