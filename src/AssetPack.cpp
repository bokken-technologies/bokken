#include "AssetPack.hpp"

#include "nlohmann/json.hpp"

#include <algorithm>

namespace Bokken
{

    // Construction / destruction
    AssetPack::AssetPack()
    {
        // PhysFS is reference-counted — safe to call init multiple times.
        if (!PHYSFS_isInit())
        {
            if (!PHYSFS_init(nullptr))
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[AssetPack] PHYSFS_init failed: %s\n",
                        PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
            }
        }
    }

    AssetPack::~AssetPack()
    {
        unmountAll();
        // Only deinit if we were the ones who initialised.
        // In practice a single AssetPack lives for the engine's lifetime,
        // so this is safe. If you ever stack multiple AssetPack instances,
        // wrap PhysFS init/deinit in a separate singleton guard.
        if (PHYSFS_isInit())
        {
            PHYSFS_deinit();
        }
    }

    // Mount / unmount

    bool AssetPack::mount(const std::string &packPath, const std::string &mountPoint)
    {
        std::string finalPath = packPath;

        // We check the REAL filesystem here, not the PhysFS VFS
        if (!std::filesystem::exists(finalPath))
        {
            const char *baseDir = PHYSFS_getBaseDir();
            if (baseDir)
            {
                std::string combined = std::string(baseDir) + packPath;
                if (std::filesystem::exists(combined))
                {
                    finalPath = combined;
                }
            }
        }

        if (!PHYSFS_mount(finalPath.c_str(), mountPoint.c_str(), 1))
        {
            PHYSFS_ErrorCode err = PHYSFS_getLastErrorCode();
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[AssetPack] Failed to mount:\n"
                            "  Requested: %s\n"
                            "  Resolved:  %s\n"
                            "  Error:     %s\n",
                    packPath.c_str(), finalPath.c_str(), PHYSFS_getErrorByCode(err));
            return false;
        }

        m_mounted.push_back(MountedPack{finalPath, mountPoint});
        m_aliasesLoaded = false;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[AssetPack] Successfully mounted '%s' at '%s'\n",
                finalPath.c_str(), mountPoint.c_str());

        return true;
    }

    bool AssetPack::mountOptional(const std::string &packPath, const std::string &mountPoint)
    {
        // Resolve the same way mount() does — relative to cwd first,
        // then to PhysFS's base directory. We need the resolution before
        // we can decide whether the pack is missing vs. broken.
        std::string finalPath = packPath;
        bool exists = std::filesystem::exists(finalPath);
        if (!exists)
        {
            const char *baseDir = PHYSFS_getBaseDir();
            if (baseDir)
            {
                std::string combined = std::string(baseDir) + packPath;
                if (std::filesystem::exists(combined))
                {
                    finalPath = combined;
                    exists = true;
                }
            }
        }

        if (!exists)
        {
            // Missing optional pack. Log at info level, not warn — for
            // a project that hasn't shipped audio yet, the absence of
            // audio.assetpack is expected, not noteworthy.
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "[AssetPack] Optional pack not present, skipping: %s\n",
                        packPath.c_str());
            return true;
        }

        // File exists — if PhysFS can't read it, that's a real failure
        // (corrupt zip, wrong format, permissions). Surface it.
        return mount(finalPath, mountPoint);
    }

    void AssetPack::unmount(const std::string &packPath)
    {
        if (!PHYSFS_isInit())
            return;
        PHYSFS_unmount(packPath.c_str());
        m_mounted.erase(
            std::remove_if(m_mounted.begin(), m_mounted.end(),
                           [&](const MountedPack &m) { return m.realPath == packPath; }),
            m_mounted.end());
        m_aliasesLoaded = false;
    }

    void AssetPack::unmountAll()
    {
        if (!PHYSFS_isInit())
            return;
        // Iterate in reverse so the last-mounted is unmounted first.
        for (auto it = m_mounted.rbegin(); it != m_mounted.rend(); ++it)
        {
            PHYSFS_unmount(it->realPath.c_str());
        }
        m_mounted.clear();
        m_aliasesLoaded = false;
    }

    void AssetPack::remountAll()
    {
        if (!PHYSFS_isInit())
            return;

        // Snapshot the current layout, unmount everything, then mount each
        // pack again at its original mount point and in the original order.
        // Mounting via PHYSFS_mount re-reads the archive directory, so any
        // pack whose file changed on disk exposes its new contents.
        const std::vector<MountedPack> layout = m_mounted;

        for (auto it = m_mounted.rbegin(); it != m_mounted.rend(); ++it)
            PHYSFS_unmount(it->realPath.c_str());
        m_mounted.clear();

        for (const MountedPack &pack : layout)
            mount(pack.realPath, pack.mountPoint);
    }

    // Query
    bool AssetPack::exists(const std::string &virtualPath) const
    {
        return PHYSFS_isInit() && PHYSFS_exists(virtualPath.c_str());
    }

    std::string AssetPack::resolve(const std::string &name) const
    {
        // Load and aggregate every mounted pack's alias table once. The
        // alias file lives at the mount point root as "__aliases.json"; a
        // pack mounted at "/textures" therefore exposes it at
        // "/textures/__aliases.json". Last-mounted wins on key collision,
        // matching PhysFS's own file-precedence rule.
        if (!m_aliasesLoaded)
        {
            m_aliases.clear();
            for (const MountedPack &pack : m_mounted)
            {
                std::string aliasPath = pack.mountPoint;
                if (aliasPath.empty() || aliasPath.back() != '/')
                    aliasPath += '/';
                aliasPath += "__aliases.json";

                if (!PHYSFS_isInit() || !PHYSFS_exists(aliasPath.c_str()))
                    continue;

                const std::vector<uint8_t> bytes = readBytes(aliasPath);
                if (bytes.empty())
                    continue;

                try
                {
                    const std::string text(bytes.begin(), bytes.end());
                    const nlohmann::json table = nlohmann::json::parse(text);
                    if (table.is_object())
                    {
                        for (auto it = table.begin(); it != table.end(); ++it)
                        {
                            // Store the alias' target prefixed by the mount
                            // point so the resolved value is a full virtual
                            // path the read APIs accept directly.
                            std::string target = it.value().get<std::string>();
                            std::string full = pack.mountPoint;
                            if (full.empty() || full.back() != '/')
                                full += '/';
                            full += target;
                            m_aliases[it.key()] = full;
                        }
                    }
                }
                catch (const std::exception &)
                {
                    // A malformed alias table is non-fatal: that pack simply
                    // contributes no aliases.
                }
            }
            m_aliasesLoaded = true;
        }

        const auto it = m_aliases.find(name);
        return it == m_aliases.end() ? name : it->second;
    }

    // Read helpers
    std::vector<uint8_t> AssetPack::readBytes(const std::string &virtualPath) const
    {
        if (!PHYSFS_isInit())
            return {};

        PHYSFS_File *f = PHYSFS_openRead(virtualPath.c_str());
        if (!f)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[AssetPack] readBytes: cannot open '%s': %s\n",
                    virtualPath.c_str(),
                    PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
            return {};
        }

        PHYSFS_sint64 fileLen = PHYSFS_fileLength(f);
        if (fileLen < 0)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[AssetPack] readBytes: unknown length for '%s'\n",
                    virtualPath.c_str());
            PHYSFS_close(f);
            return {};
        }

        std::vector<uint8_t> buf(static_cast<size_t>(fileLen));
        PHYSFS_sint64 read = PHYSFS_readBytes(f, buf.data(), static_cast<PHYSFS_uint64>(fileLen));
        PHYSFS_close(f);

        if (read != fileLen)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[AssetPack] readBytes: short read on '%s' (%lld of %lld)\n",
                    virtualPath.c_str(), (long long)read, (long long)fileLen);
            return {};
        }

        return buf;
    }

    // SDL_IOStream bridge
    // Internal userdata struct passed to every SDL_IOStream callback.
    struct PhysFSIOData
    {
        PHYSFS_File *file = nullptr;
        std::string virtualPath; // for error messages
    };

    Sint64 AssetPack::io_size(void *userdata)
    {
        auto *d = static_cast<PhysFSIOData *>(userdata);
        return PHYSFS_fileLength(d->file);
    }

    Sint64 AssetPack::io_seek(void *userdata, Sint64 offset, SDL_IOWhence whence)
    {
        auto *d = static_cast<PhysFSIOData *>(userdata);

        PHYSFS_sint64 target = 0;
        switch (whence)
        {
        case SDL_IO_SEEK_SET:
            target = offset;
            break;
        case SDL_IO_SEEK_CUR:
            target = static_cast<PHYSFS_sint64>(PHYSFS_tell(d->file)) + offset;
            break;
        case SDL_IO_SEEK_END:
            target = PHYSFS_fileLength(d->file) + offset;
            break;
        default:
            return -1;
        }

        if (!PHYSFS_seek(d->file, static_cast<PHYSFS_uint64>(target)))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[AssetPack] io_seek failed on '%s': %s\n",
                    d->virtualPath.c_str(),
                    PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
            return -1;
        }
        return target;
    }

    size_t AssetPack::io_read(void *userdata, void *ptr, size_t size,
                              SDL_IOStatus *status)
    {
        auto *d = static_cast<PhysFSIOData *>(userdata);
        PHYSFS_sint64 n = PHYSFS_readBytes(d->file, ptr, static_cast<PHYSFS_uint64>(size));

        if (n < 0)
        {
            if (status)
                *status = SDL_IO_STATUS_ERROR;
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[AssetPack] io_read error on '%s': %s\n",
                    d->virtualPath.c_str(),
                    PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
            return 0;
        }
        if (static_cast<size_t>(n) < size)
        {
            if (status)
                *status = SDL_IO_STATUS_EOF;
        }
        else
        {
            if (status)
                *status = SDL_IO_STATUS_READY;
        }
        return static_cast<size_t>(n);
    }

    size_t AssetPack::io_write(void * /*userdata*/, const void * /*ptr*/,
                               size_t /*size*/, SDL_IOStatus *status)
    {
        // .assetpack files are read-only.
        if (status)
            *status = SDL_IO_STATUS_ERROR;
        return 0;
    }

    bool AssetPack::io_close(void *userdata)
    {
        auto *d = static_cast<PhysFSIOData *>(userdata);
        bool ok = (PHYSFS_close(d->file) != 0);
        delete d;
        return ok;
    }

    SDL_IOStream *AssetPack::wrapPhysFSFile(PHYSFS_File *file,
                                            const std::string &virtualPath)
    {
        auto *data = new PhysFSIOData{file, virtualPath};
        SDL_IOStreamInterface iface{};
        SDL_INIT_INTERFACE(&iface);
        iface.size = &AssetPack::io_size;
        iface.seek = &AssetPack::io_seek;
        iface.read = &AssetPack::io_read;
        iface.write = &AssetPack::io_write;
        iface.close = &AssetPack::io_close;

        SDL_IOStream *io = SDL_OpenIO(&iface, data);
        if (!io)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[AssetPack] SDL_OpenIO failed for '%s': %s\n",
                    virtualPath.c_str(), SDL_GetError());
            PHYSFS_close(file);
            delete data;
            return nullptr;
        }
        return io;
    }

    SDL_IOStream *AssetPack::openIOStream(const std::string &virtualPath) const
    {
        if (!PHYSFS_isInit())
            return nullptr;

        PHYSFS_File *f = PHYSFS_openRead(virtualPath.c_str());
        if (!f)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[AssetPack] openIOStream: cannot open '%s': %s\n",
                    virtualPath.c_str(),
                    PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
            return nullptr;
        }

        return wrapPhysFSFile(f, virtualPath);
    }

    // Enumeration
    void AssetPack::enumerate(const std::string &virtualDir,
                              const std::function<void(const std::string &)> &callback,
                              bool recursive) const
    {
        if (!PHYSFS_isInit())
            return;

        char **files = PHYSFS_enumerateFiles(virtualDir.c_str());
        if (!files)
            return;

        for (char **i = files; *i != nullptr; ++i)
        {
            // Build full virtual path.
            std::string fullPath = virtualDir;
            if (fullPath.back() != '/')
                fullPath += '/';
            fullPath += *i;

            PHYSFS_Stat stat{};
            if (PHYSFS_stat(fullPath.c_str(), &stat))
            {
                if (stat.filetype == PHYSFS_FILETYPE_DIRECTORY)
                {
                    if (recursive)
                    {
                        enumerate(fullPath, callback, true);
                    }
                }
                else
                {
                    callback(fullPath);
                }
            }
        }

        PHYSFS_freeList(files);
    }

} // namespace Bokken
