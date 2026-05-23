#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Bokken
{
    namespace CLI
    {
        /** How a watched path changed between two debounce windows. */
        enum class ChangeKind
        {
            Created,
            Modified,
            Deleted
        };

        /** A single coalesced change reported by the watcher. */
        struct FileChange
        {
            std::string path;
            ChangeKind kind;
        };

        /**
         * Recursively watches one or more directory roots and reports
         * coalesced batches of changes.
         *
         * Native file-system events are noisy: a single editor save can
         * fire several events (write to a temp file, rename over the
         * original, attribute touch). The watcher collapses these by
         * holding changes in a map keyed by path and flushing the map to
         * the callback only after a quiet debounce window has elapsed, so
         * the consumer sees one batch per logical edit rather than a storm.
         *
         * start() blocks the calling thread until stop() is invoked from
         * another thread, so callers run it on a dedicated watcher thread.
         *
         * The implementation is per-platform: inotify on Linux, FSEvents on
         * macOS, ReadDirectoryChangesW on Windows. The interface and the
         * debounce logic are shared; only the event source differs.
        */
        class Watcher
        {
        public:
            using Callback = std::function<void(const std::vector<FileChange> &)>;

            Watcher();
            ~Watcher();

            Watcher(const Watcher &) = delete;
            Watcher &operator=(const Watcher &) = delete;

            /** Begin watching. Returns false if no root could be watched.
             *  Blocks until stop() is called. */
            bool start(const std::vector<std::filesystem::path> &roots,
                       std::chrono::milliseconds debounce, Callback callback);

            /** Ask start() to return. Safe to call from any thread. */
            void stop();

            // Opaque implementation, defined in Watch.cpp. Declared public
            // because the platform backends (the macOS FSEvents callback,
            // for one) are free functions that need to name the type to
            // reach the shared debounce buffer; the definition stays in the
            // .cpp, so nothing about its layout is exposed here.
            struct Implementation;

        private:
            std::unique_ptr<Implementation> m_implementation;
        };

    } // namespace CLI
} // namespace Bokken
