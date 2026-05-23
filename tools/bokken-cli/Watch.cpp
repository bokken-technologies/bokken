#include "Watch.hpp"

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <system_error>

namespace fs = std::filesystem;

namespace Bokken
{
    namespace CLI
    {
        // Shared debounce buffer.
        //
        // Every platform backend funnels raw events into record(), which
        // accumulates them keyed by path so repeated events for one file
        // collapse to a single entry. flushIfQuiet() emits the batch once
        // no new event has arrived for the debounce interval. The kind
        // precedence rule keeps the semantics intuitive: a Created followed
        // by Modified in the same window stays Created (the file is new to
        // the consumer); anything followed by Deleted becomes Deleted.
        struct Watcher::Implementation
        {
            std::atomic<bool> running{false};
            std::chrono::milliseconds debounce{150};
            Callback callback;

            std::mutex mutex;
            std::map<std::string, ChangeKind> pending;
            std::chrono::steady_clock::time_point lastEvent;

            void record(const std::string &path, ChangeKind kind)
            {
                std::lock_guard<std::mutex> lock(mutex);
                const auto it = pending.find(path);
                if (it == pending.end())
                {
                    pending.emplace(path, kind);
                }
                else
                {
                    // Resolve the combined kind. Deletion always wins;
                    // creation is preserved over a later modification.
                    if (kind == ChangeKind::Deleted)
                        it->second = ChangeKind::Deleted;
                    else if (it->second != ChangeKind::Created)
                        it->second = kind;
                }
                lastEvent = std::chrono::steady_clock::now();
            }

            void flushIfQuiet()
            {
                std::vector<FileChange> batch;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (pending.empty())
                        return;
                    const auto now = std::chrono::steady_clock::now();
                    if (now - lastEvent < debounce)
                        return;
                    for (const auto &[path, kind] : pending)
                        batch.push_back(FileChange{path, kind});
                    pending.clear();
                }
                if (!batch.empty() && callback)
                    callback(batch);
            }
        };

        Watcher::Watcher() : m_implementation(std::make_unique<Implementation>()) {}
        Watcher::~Watcher() { stop(); }

        void Watcher::stop()
        {
            if (m_implementation)
                m_implementation->running.store(false);
        }

    } // namespace CLI
} // namespace Bokken

// Platform backends. Each implements Watcher::start. The shared debounce
// buffer above is platform-agnostic; only the event source differs.

#if defined(__linux__)

#include <climits>
#include <cstring>
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <unordered_map>

namespace Bokken
{
    namespace CLI
    {
        namespace
        {
            // Recursively add an inotify watch for a directory and every
            // subdirectory beneath it, recording the descriptor → path map
            // so events (which carry only the descriptor) can be resolved
            // back to a full path.
            void addWatchRecursive(int notifyFd, const fs::path &directory,
                                   std::unordered_map<int, std::string> &watchPaths)
            {
                const uint32_t mask = IN_CLOSE_WRITE | IN_CREATE | IN_DELETE |
                                      IN_MOVED_FROM | IN_MOVED_TO;
                const int wd =
                    inotify_add_watch(notifyFd, directory.c_str(), mask);
                if (wd >= 0)
                    watchPaths[wd] = directory.string();

                std::error_code ec;
                for (const auto &entry :
                     fs::recursive_directory_iterator(directory, ec))
                {
                    if (!ec && entry.is_directory())
                    {
                        const int childWd = inotify_add_watch(
                            notifyFd, entry.path().c_str(), mask);
                        if (childWd >= 0)
                            watchPaths[childWd] = entry.path().string();
                    }
                }
            }
        }

        bool Watcher::start(const std::vector<fs::path> &roots,
                            std::chrono::milliseconds debounce, Callback callback)
        {
            m_implementation->debounce = debounce;
            m_implementation->callback = std::move(callback);

            const int notifyFd = inotify_init1(IN_NONBLOCK);
            if (notifyFd < 0)
                return false;

            std::unordered_map<int, std::string> watchPaths;
            for (const fs::path &root : roots)
            {
                if (fs::is_directory(root))
                    addWatchRecursive(notifyFd, root, watchPaths);
            }
            if (watchPaths.empty())
            {
                close(notifyFd);
                return false;
            }

            m_implementation->running.store(true);

            // Buffer sized for several events plus their variable-length
            // name fields; the kernel packs multiple events per read.
            alignas(inotify_event) char buffer[4096];
            struct pollfd pfd;
            pfd.fd = notifyFd;
            pfd.events = POLLIN;

            while (m_implementation->running.load())
            {
                // Poll with a short timeout so the loop also wakes to flush
                // the debounce buffer even when no new events arrive.
                const int ready = poll(&pfd, 1, 50);
                if (ready > 0 && (pfd.revents & POLLIN))
                {
                    ssize_t length = read(notifyFd, buffer, sizeof(buffer));
                    ssize_t offset = 0;
                    while (offset < length)
                    {
                        auto *event =
                            reinterpret_cast<inotify_event *>(buffer + offset);
                        if (event->len > 0)
                        {
                            const auto base = watchPaths.find(event->wd);
                            if (base != watchPaths.end())
                            {
                                const std::string path =
                                    base->second + "/" + event->name;

                                if (event->mask & (IN_CREATE | IN_MOVED_TO))
                                {
                                    // A newly created subdirectory must be
                                    // watched too, or edits inside it are
                                    // missed.
                                    if (event->mask & IN_ISDIR)
                                        addWatchRecursive(notifyFd, path,
                                                          watchPaths);
                                    m_implementation->record(
                                        path, ChangeKind::Created);
                                }
                                else if (event->mask &
                                         (IN_DELETE | IN_MOVED_FROM))
                                {
                                    m_implementation->record(
                                        path, ChangeKind::Deleted);
                                }
                                else if (event->mask & IN_CLOSE_WRITE)
                                {
                                    m_implementation->record(
                                        path, ChangeKind::Modified);
                                }
                            }
                        }
                        offset += sizeof(inotify_event) + event->len;
                    }
                }

                m_implementation->flushIfQuiet();
            }

            close(notifyFd);
            return true;
        }

    } // namespace CLI
} // namespace Bokken

#elif defined(__APPLE__)

// macOS backend: an FSEventStream over the roots, delivered on a private
// serial dispatch queue. FSEvents is recursive per root, so there is no
// per-subdirectory bookkeeping. The stream callback receives changed paths
// with flag bits that distinguish create / modify / remove, which are
// translated into the shared debounce buffer's record().

#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>

#include <chrono>
#include <thread>

namespace Bokken
{
    namespace CLI
    {
        namespace
        {
            // Passed to the FSEvents callback through the stream context so
            // it can reach the shared debounce buffer.
            struct FSEventsState
            {
                Watcher::Implementation *implementation = nullptr;
            };

            void fsEventsCallback(ConstFSEventStreamRef /*stream*/,
                                  void *clientInfo, size_t numEvents,
                                  void *eventPaths,
                                  const FSEventStreamEventFlags flags[],
                                  const FSEventStreamEventId /*ids*/[])
            {
                auto *state = static_cast<FSEventsState *>(clientInfo);
                auto **paths = static_cast<char **>(eventPaths);

                for (size_t i = 0; i < numEvents; i++)
                {
                    const std::string path = paths[i];
                    const FSEventStreamEventFlags flag = flags[i];

                    // FSEvents coalesces; a single notification can carry
                    // several flag bits. Removal takes precedence, then
                    // creation, then modification — matching the debounce
                    // buffer's own kind-precedence rule.
                    if (flag & kFSEventStreamEventFlagItemRemoved)
                        state->implementation->record(path, ChangeKind::Deleted);
                    else if (flag & (kFSEventStreamEventFlagItemCreated |
                                     kFSEventStreamEventFlagItemRenamed))
                        state->implementation->record(path, ChangeKind::Created);
                    else if (flag & (kFSEventStreamEventFlagItemModified |
                                     kFSEventStreamEventFlagItemInodeMetaMod))
                        state->implementation->record(path, ChangeKind::Modified);
                }
            }
        }

        bool Watcher::start(const std::vector<std::filesystem::path> &roots,
                            std::chrono::milliseconds debounce, Callback callback)
        {
            m_implementation->debounce = debounce;
            m_implementation->callback = std::move(callback);

            if (roots.empty())
                return false;

            // Build the CFArray of paths to watch.
            CFMutableArrayRef pathsToWatch =
                CFArrayCreateMutable(nullptr, static_cast<CFIndex>(roots.size()),
                                     &kCFTypeArrayCallBacks);
            bool anyValid = false;
            for (const fs::path &root : roots)
            {
                if (!fs::exists(root))
                    continue;
                CFStringRef cfPath = CFStringCreateWithCString(
                    nullptr, root.c_str(), kCFStringEncodingUTF8);
                CFArrayAppendValue(pathsToWatch, cfPath);
                CFRelease(cfPath);
                anyValid = true;
            }
            if (!anyValid)
            {
                CFRelease(pathsToWatch);
                return false;
            }

            FSEventsState state;
            state.implementation = m_implementation.get();

            FSEventStreamContext context;
            context.version = 0;
            context.info = &state;
            context.retain = nullptr;
            context.release = nullptr;
            context.copyDescription = nullptr;

            // Latency in seconds: FSEvents buffers events for this long
            // before delivering. Keep it well under the debounce window so
            // the debounce logic, not FSEvents latency, governs batching.
            const CFAbsoluteTime latency = 0.05;

            FSEventStreamRef stream = FSEventStreamCreate(
                nullptr, &fsEventsCallback, &context, pathsToWatch,
                kFSEventStreamEventIdSinceNow, latency,
                kFSEventStreamCreateFlagFileEvents |
                    kFSEventStreamCreateFlagNoDefer);
            CFRelease(pathsToWatch);

            if (!stream)
                return false;

            // Deliver events on a private serial dispatch queue (the modern
            // replacement for FSEventStreamScheduleWithRunLoop, which is
            // deprecated as of macOS 13). The callback then runs on that
            // queue's thread; this thread only flushes the debounce buffer.
            dispatch_queue_t queue =
                dispatch_queue_create("com.bokken.cli.watch", DISPATCH_QUEUE_SERIAL);
            FSEventStreamSetDispatchQueue(stream, queue);

            if (!FSEventStreamStart(stream))
            {
                FSEventStreamInvalidate(stream);
                FSEventStreamRelease(stream);
                dispatch_release(queue);
                return false;
            }

            m_implementation->running.store(true);

            // Sleep in short slices, flushing the debounce buffer and
            // observing the running flag for stop(). Events arrive
            // asynchronously on the dispatch queue and feed record().
            while (m_implementation->running.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
                m_implementation->flushIfQuiet();
            }

            FSEventStreamStop(stream);
            FSEventStreamInvalidate(stream);
            FSEventStreamRelease(stream);
            dispatch_release(queue);
            return true;
        }

    } // namespace CLI
} // namespace Bokken

#elif defined(_WIN32)

// Windows backend: one watch thread per root using ReadDirectoryChangesW
// with bWatchSubtree = TRUE. Each root runs blocking reads in a small
// worker; the FILE_NOTIFY_INFORMATION records are translated into the
// shared debounce buffer. The main start() thread owns the debounce flush
// and the running flag, and signals the workers to exit by closing their
// directory handles (which makes the pending ReadDirectoryChangesW return).

#include <windows.h>

#include <thread>
#include <vector>

namespace Bokken
{
    namespace CLI
    {
        namespace
        {
            std::string narrow(const std::wstring &wide)
            {
                if (wide.empty())
                    return {};
                const int needed = WideCharToMultiByte(
                    CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                    nullptr, 0, nullptr, nullptr);
                std::string out(static_cast<size_t>(needed), '\0');
                WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                    static_cast<int>(wide.size()), out.data(),
                                    needed, nullptr, nullptr);
                return out;
            }

            void watchRoot(const std::wstring &rootPath, const std::string &rootUtf8,
                           Watcher::Implementation *implementation, HANDLE directory)
            {
                // Aligned buffer for FILE_NOTIFY_INFORMATION records.
                std::vector<BYTE> buffer(64 * 1024);
                const DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME |
                                     FILE_NOTIFY_CHANGE_DIR_NAME |
                                     FILE_NOTIFY_CHANGE_LAST_WRITE |
                                     FILE_NOTIFY_CHANGE_SIZE;

                while (implementation->running.load())
                {
                    DWORD bytesReturned = 0;
                    const BOOL ok = ReadDirectoryChangesW(
                        directory, buffer.data(),
                        static_cast<DWORD>(buffer.size()), TRUE, filter,
                        &bytesReturned, nullptr, nullptr);

                    if (!ok || bytesReturned == 0)
                    {
                        // The handle was closed by stop(), or an error
                        // occurred; either way the worker exits.
                        break;
                    }

                    BYTE *cursor = buffer.data();
                    for (;;)
                    {
                        auto *info =
                            reinterpret_cast<FILE_NOTIFY_INFORMATION *>(cursor);

                        const std::wstring relativeWide(
                            info->FileName,
                            info->FileNameLength / sizeof(WCHAR));
                        std::string relative = narrow(relativeWide);
                        std::replace(relative.begin(), relative.end(), '\\', '/');
                        const std::string fullPath = rootUtf8 + "/" + relative;

                        switch (info->Action)
                        {
                        case FILE_ACTION_ADDED:
                        case FILE_ACTION_RENAMED_NEW_NAME:
                            implementation->record(fullPath, ChangeKind::Created);
                            break;
                        case FILE_ACTION_REMOVED:
                        case FILE_ACTION_RENAMED_OLD_NAME:
                            implementation->record(fullPath, ChangeKind::Deleted);
                            break;
                        case FILE_ACTION_MODIFIED:
                            implementation->record(fullPath, ChangeKind::Modified);
                            break;
                        default:
                            break;
                        }

                        if (info->NextEntryOffset == 0)
                            break;
                        cursor += info->NextEntryOffset;
                    }
                }
            }
        }

        bool Watcher::start(const std::vector<std::filesystem::path> &roots,
                            std::chrono::milliseconds debounce, Callback callback)
        {
            m_implementation->debounce = debounce;
            m_implementation->callback = std::move(callback);

            std::vector<HANDLE> directories;
            std::vector<std::thread> workers;

            for (const fs::path &root : roots)
            {
                if (!fs::is_directory(root))
                    continue;

                const std::wstring widePath = root.wstring();
                HANDLE directory = CreateFileW(
                    widePath.c_str(), FILE_LIST_DIRECTORY,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS, nullptr);

                if (directory == INVALID_HANDLE_VALUE)
                    continue;

                directories.push_back(directory);
            }

            if (directories.empty())
                return false;

            m_implementation->running.store(true);

            // One worker thread per watched root.
            size_t index = 0;
            for (const fs::path &root : roots)
            {
                if (!fs::is_directory(root) || index >= directories.size())
                    continue;
                HANDLE directory = directories[index++];
                const std::wstring widePath = root.wstring();
                const std::string rootUtf8 = root.generic_string();
                workers.emplace_back(watchRoot, widePath, rootUtf8,
                                     m_implementation.get(), directory);
            }

            // Main pump: flush the debounce buffer until stop() is called.
            while (m_implementation->running.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
                m_implementation->flushIfQuiet();
            }

            // Closing the directory handles unblocks the workers'
            // ReadDirectoryChangesW so they can exit.
            for (HANDLE directory : directories)
                CloseHandle(directory);
            for (std::thread &worker : workers)
            {
                if (worker.joinable())
                    worker.join();
            }
            return true;
        }

    } // namespace CLI
} // namespace Bokken

#else

namespace Bokken
{
    namespace CLI
    {
        bool Watcher::start(const std::vector<std::filesystem::path> &,
                            std::chrono::milliseconds, Callback)
        {
            return false;
        }
    } // namespace CLI
} // namespace Bokken

#endif
