#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Bokken
{
    namespace CLI
    {
        class Manifest;

        /** One file destined for an archive: its real path on disk and the
         *  path it should occupy inside the archive (forward-slashed). */
        struct PackEntry
        {
            std::string realPath;
            std::string internalPath;
        };

        /** One archive to (re)build: its output file, the entries it should
         *  contain, and whether any input changed since the last pack. A
         *  clean archive is skipped entirely. */
        struct PackArchive
        {
            std::string output;
            std::vector<PackEntry> entries;
            bool dirty = false;
        };

        /** The set of archives a pack pass should consider. */
        struct PackPlan
        {
            std::vector<PackArchive> archives;
        };

        /**
         * Decide which archives need rebuilding by comparing their inputs
         * against the manifest.
         *
         * An archive is marked dirty when any of its inputs changed, was
         * added, or was removed since the last successful pack. Clean
         * archives are still listed (so callers can report them) but carry
         * dirty=false and are skipped by the executor.
        */
        PackPlan planPack(const std::vector<PackArchive> &archives,
                          Manifest &manifest);

        /**
         * Execute a pack plan: rebuild every dirty archive via miniz and
         * update the manifest in place (the caller saves it). Returns the
         * number of archives that failed to write.
        */
        int runPackPlan(const PackPlan &plan, Manifest &manifest, bool verbose);

        /**
         * Pack a directory tree into one or more .assetpack archives.
         *
         * argv[0] is the subcommand name ("pack") and is ignored. The
         * remaining args are:
         *   <input-dir>       required — directory tree to pack
         *   <output-prefix>   required — output path stem; suffix
         *                                ".assetpack" is appended
         *   --force           optional — ignore the manifest, repack all
         *
         * If <output-prefix> contains the substring "scripts", the
         * directory is packed into a single archive with a "scripts/"
         * prefix on every entry inside. Otherwise, when an assets.bokken
         * descriptor is present at the input root it drives packing by
         * logical group and glob (folder-independent); failing that, the
         * packer looks for quality-tier subdirectories
         * (low/middle/high/ultra) and emits one archive per tier, falling
         * back to a single archive if no tiers are present.
         *
         * Returns 0 on success, 1 on argument errors (missing args,
         * non-existent input).
        */
        int runPack(int argc, char *argv[]);
    }
}
