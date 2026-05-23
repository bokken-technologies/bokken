#pragma once

#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace Bokken
{
    namespace CLI
    {
        /**
         * One tracked input file and the outputs it produced.
         *
         * The triple (mtimeNs, size, hash) lets the planner answer "did
         * this input change?" cheaply: the modification time and size are
         * compared first, and the content hash is consulted only when one
         * of those differs, so an editor that rewrites a file without
         * changing its contents does not trigger a rebuild.
        */
        struct ManifestEntry
        {
            int64_t mtimeNs = 0;
            uint64_t size = 0;
            uint64_t hash = 0;
            std::vector<std::string> outputs;
        };

        /**
         * Per-project incremental build manifest.
         *
         * Stored as JSON under the project's build directory (so a clean
         * removes it). A missing or unreadable file loads as an empty
         * manifest, which causes the first build to process everything.
         *
         * The manifest maps each absolute input path to the metadata of
         * the last successful build of that input. Planners read it to
         * skip unchanged inputs and to find outputs orphaned by deleted
         * sources; executors update it in place and the caller saves it
         * once the build step succeeds.
        */
        class Manifest
        {
        public:
            /** Load from disk. Returns false only on a present-but-corrupt
             *  file; a missing file loads as empty and returns true. */
            bool load(const std::filesystem::path &path);

            /** Write to disk, creating parent directories as needed. */
            bool save(const std::filesystem::path &path) const;

            /** True when mtime and size both match the recorded entry, so
             *  the caller can skip the content hash. False when the input
             *  is untracked or either field differs. */
            bool looksUnchanged(const std::string &input,
                                int64_t mtimeNs, uint64_t size) const;

            /** Recorded entry for an input, or nullptr if untracked. */
            const ManifestEntry *find(const std::string &input) const;

            /** Insert or replace the entry for an input. */
            void put(const std::string &input, ManifestEntry entry);

            /** Remove an input and forget its outputs. */
            void erase(const std::string &input);

            /** Inputs recorded in the manifest that are absent from
             *  seenInputs — i.e. sources removed since the last build.
             *  Their outputs should be deleted so stale artifacts do not
             *  ship. */
            std::vector<std::string> stalePaths(
                const std::set<std::string> &seenInputs) const;

            /** Compute the FNV-1a hash of a file's bytes. Returns 0 if the
             *  file cannot be read. Public so planners can confirm a
             *  suspected change without duplicating the algorithm. */
            static uint64_t hashFile(const std::filesystem::path &path);

        private:
            std::unordered_map<std::string, ManifestEntry> m_entries;
        };

    } // namespace CLI
} // namespace Bokken
