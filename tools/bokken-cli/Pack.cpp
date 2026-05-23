#include "Pack.hpp"
#include "Manifest.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "miniz.h"
#include "nlohmann/json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
    using Bokken::CLI::PackArchive;
    using Bokken::CLI::PackEntry;

    // ANSI color codes — short names because they appear inline in status
    // output and longer names would push the format strings off the right
    // margin.
    constexpr const char *kReset = "\033[0m";
    constexpr const char *kBold  = "\033[1m";
    constexpr const char *kGreen = "\033[32m";
    constexpr const char *kCyan  = "\033[36m";
    constexpr const char *kYel   = "\033[33m";
    constexpr const char *kRed   = "\033[31m";

    // Quality tiers looked for when packing assets by folder. The packer
    // emits one archive per tier that exists; the engine mounts the
    // archive matching the active quality at run time.
    const std::vector<std::string> kQualityTiers = {"low", "middle", "high", "ultra"};

    // Normalise a path to forward slashes so archive-internal paths are
    // identical regardless of host platform.
    std::string toForwardSlashes(std::string path)
    {
        std::replace(path.begin(), path.end(), '\\', '/');
        return path;
    }

    // Match a path against a glob supporting '*' (any run within a path
    // segment), '**' (any run across segments), and '?' (single char).
    // Implemented as a small recursive matcher rather than translating to
    // std::regex so the '**' semantics are explicit and correct.
    bool globMatch(const std::string &pattern, const std::string &text,
                   size_t patternIndex = 0, size_t textIndex = 0)
    {
        while (patternIndex < pattern.size())
        {
            const char pc = pattern[patternIndex];

            if (pc == '*')
            {
                const bool doubleStar = (patternIndex + 1 < pattern.size() &&
                                         pattern[patternIndex + 1] == '*');
                if (doubleStar)
                {
                    // '**' matches any run including '/'. Try consuming
                    // zero-or-more characters of text against the rest of
                    // the pattern (skipping the second star, and a trailing
                    // '/' so "ui/**" matches "ui/a/b" and "ui").
                    size_t nextPattern = patternIndex + 2;
                    if (nextPattern < pattern.size() && pattern[nextPattern] == '/')
                        nextPattern++;
                    for (size_t t = textIndex; t <= text.size(); t++)
                    {
                        if (globMatch(pattern, text, nextPattern, t))
                            return true;
                    }
                    return false;
                }

                // Single '*' matches any run within one segment (no '/').
                for (size_t t = textIndex; t <= text.size(); t++)
                {
                    if (globMatch(pattern, text, patternIndex + 1, t))
                        return true;
                    if (t < text.size() && text[t] == '/')
                        break;
                }
                return false;
            }

            if (textIndex >= text.size())
                return false;

            if (pc == '?')
            {
                if (text[textIndex] == '/')
                    return false;
            }
            else if (pc != text[textIndex])
            {
                return false;
            }

            patternIndex++;
            textIndex++;
        }
        return textIndex == text.size();
    }

    // A logical asset group read from assets.bokken.
    struct AssetGroup
    {
        std::string name;
        std::string mount;
        std::vector<std::string> include;
        bool tiers = false;
    };

    // Parse the optional assets.bokken descriptor at the input root. Returns
    // true when the file is present and well-formed (groups populated); a
    // missing file returns false so the caller falls back to folder-driven
    // packing.
    bool loadAssetDescriptor(const fs::path &inputDir, std::vector<AssetGroup> &outGroups)
    {
        const fs::path descriptorPath = inputDir / "assets.bokken";
        std::ifstream file(descriptorPath, std::ios::binary);
        if (!file.is_open())
            return false;

        json document;
        try
        {
            file >> document;
        }
        catch (const std::exception &)
        {
            std::cerr << kRed << "  [!] assets.bokken is not valid JSON; "
                      << "falling back to folder-driven packing." << kReset << "\n";
            return false;
        }

        const auto groups = document.find("groups");
        if (groups == document.end() || !groups->is_array())
            return false;

        for (const auto &group : *groups)
        {
            AssetGroup parsed;
            parsed.name = group.value("name", std::string{});
            parsed.mount = group.value("mount", std::string{});
            parsed.tiers = group.value("tiers", false);
            if (group.contains("include") && group["include"].is_array())
            {
                for (const auto &pattern : group["include"])
                    parsed.include.push_back(pattern.get<std::string>());
            }
            if (!parsed.name.empty() && !parsed.include.empty())
                outGroups.push_back(std::move(parsed));
        }
        return !outGroups.empty();
    }

    // Collect every regular file under a root, returned as paths relative to
    // that root with forward slashes.
    std::vector<std::string> listRelativeFiles(const fs::path &root)
    {
        std::vector<std::string> files;
        std::error_code ec;
        for (const auto &entry : fs::recursive_directory_iterator(root, ec))
        {
            if (ec || !entry.is_regular_file())
                continue;

            // Skip the CLI's own incremental-build cache files; they live
            // alongside compiled output but are tooling state, not assets,
            // and must never end up inside an archive.
            const std::string filename = entry.path().filename().string();
            if (filename == ".compile-cache.json" ||
                filename == ".pack-cache.json")
                continue;

            files.push_back(toForwardSlashes(
                fs::relative(entry.path(), root).string()));
        }
        return files;
    }

    // Build the archive entries for a folder, optionally prefixing each
    // internal path (used for the "scripts/" mount convention).
    PackArchive archiveFromFolder(const fs::path &inputPath,
                                  const std::string &output,
                                  const std::string &internalPrefix)
    {
        PackArchive archive;
        archive.output = output;
        for (const std::string &relative : listRelativeFiles(inputPath))
        {
            PackEntry entry;
            entry.realPath = (inputPath / relative).string();
            entry.internalPath = internalPrefix + relative;
            archive.entries.push_back(std::move(entry));
        }
        return archive;
    }

    // Build archive entries for an assets.bokken group by matching globs
    // across the whole input tree (folder-independent). The internal path
    // keeps the file's path relative to the input root so the alias table
    // and literal lookups both resolve.
    PackArchive archiveFromGroup(const fs::path &inputDir,
                                 const AssetGroup &group,
                                 const std::string &output,
                                 const std::vector<std::string> &allFiles)
    {
        PackArchive archive;
        archive.output = output;
        for (const std::string &relative : allFiles)
        {
            bool matched = false;
            for (const std::string &pattern : group.include)
            {
                if (globMatch(pattern, relative))
                {
                    matched = true;
                    break;
                }
            }
            if (!matched)
                continue;

            PackEntry entry;
            entry.realPath = (inputDir / relative).string();
            entry.internalPath = relative;
            archive.entries.push_back(std::move(entry));
        }
        return archive;
    }
}

namespace
{
    using Bokken::CLI::Manifest;
    using Bokken::CLI::ManifestEntry;
    using Bokken::CLI::PackPlan;

    int64_t modificationTimeNs(const fs::path &path)
    {
        std::error_code ec;
        const auto time = fs::last_write_time(path, ec);
        if (ec)
            return 0;
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   time.time_since_epoch())
            .count();
    }

    uint64_t fileSize(const fs::path &path)
    {
        std::error_code ec;
        const auto size = fs::file_size(path, ec);
        return ec ? 0 : static_cast<uint64_t>(size);
    }

    // The manifest key for an archive's membership record. Storing it under
    // the output path lets a single manifest track many archives, each with
    // the set of inputs that produced it.
    std::string membershipKey(const std::string &output)
    {
        return "archive:" + output;
    }

    // True when any input changed, was added, or was removed relative to the
    // manifest's record of this archive's last build.
    bool archiveIsDirty(const PackArchive &archive, Manifest &manifest)
    {
        const std::string key = membershipKey(archive.output);
        const ManifestEntry *previous = manifest.find(key);

        // No prior record, or the output file is gone: must rebuild.
        if (previous == nullptr || !fs::exists(archive.output))
            return true;

        // Membership change: the previous build's input list (stored as the
        // entry's outputs field, reused here as "inputs that fed this
        // archive") must match the current set exactly.
        std::set<std::string> previousInputs(previous->outputs.begin(),
                                             previous->outputs.end());
        std::set<std::string> currentInputs;
        for (const PackEntry &entry : archive.entries)
            currentInputs.insert(entry.realPath);
        if (previousInputs != currentInputs)
            return true;

        // Content change: a combined mtime/size signature over all inputs is
        // compared against the recorded signature (stored in hash). Cheap and
        // sufficient — a real edit moves mtime or size.
        uint64_t signature = 1469598103934665603ull;
        for (const PackEntry &entry : archive.entries)
        {
            const uint64_t fields[2] = {
                static_cast<uint64_t>(modificationTimeNs(entry.realPath)),
                fileSize(entry.realPath)};
            for (uint64_t field : fields)
            {
                signature ^= field;
                signature *= 1099511628211ull;
            }
        }
        return signature != previous->hash;
    }

    uint64_t archiveSignature(const PackArchive &archive)
    {
        uint64_t signature = 1469598103934665603ull;
        for (const PackEntry &entry : archive.entries)
        {
            const uint64_t fields[2] = {
                static_cast<uint64_t>(modificationTimeNs(entry.realPath)),
                fileSize(entry.realPath)};
            for (uint64_t field : fields)
            {
                signature ^= field;
                signature *= 1099511628211ull;
            }
        }
        return signature;
    }

    // Write one archive to disk via miniz. Returns true on success.
    bool writeArchive(const PackArchive &archive, bool verbose)
    {
        fs::path outFile(archive.output);
        if (outFile.has_parent_path())
        {
            std::error_code ec;
            fs::create_directories(outFile.parent_path(), ec);
        }

        mz_zip_archive zip;
        std::memset(&zip, 0, sizeof(zip));

        std::cout << kCyan << "  [Init] " << kReset << "Creating archive: "
                  << kBold << archive.output << kReset << "\n";

        if (!mz_zip_writer_init_file(&zip, archive.output.c_str(), 0))
        {
            std::cerr << kRed << "  [!] Could not open " << archive.output
                      << " (check permissions/path)" << kReset << "\n";
            return false;
        }

        int fileCount = 0;
        for (const PackEntry &entry : archive.entries)
        {
            if (verbose)
            {
                const double kilobytes = fileSize(entry.realPath) / 1024.0;
                std::cout << kCyan << "    (+) " << kReset << std::left
                          << std::setw(45) << entry.internalPath << kYel << " ["
                          << std::fixed << std::setprecision(1) << kilobytes
                          << " KB]" << kReset << "\n";
            }
            mz_zip_writer_add_file(&zip, entry.internalPath.c_str(),
                                   entry.realPath.c_str(), nullptr, 0,
                                   MZ_BEST_COMPRESSION);
            fileCount++;
        }

        // Write an alias table mapping each file's stem (basename without
        // extension) to its full internal path, so the engine can resolve
        // logical names like "player" regardless of which folder the file
        // lives in. When two files share a stem the first wins; callers that
        // need disambiguation can still use the full virtual path. The table
        // is a JSON object stored as the reserved entry "__aliases.json".
        {
            json aliases = json::object();
            for (const PackEntry &entry : archive.entries)
            {
                const fs::path internal(entry.internalPath);
                const std::string stem = internal.stem().string();
                if (stem.empty() || aliases.contains(stem))
                    continue;
                aliases[stem] = entry.internalPath;
            }
            const std::string aliasText = aliases.dump();
            mz_zip_writer_add_mem(&zip, "__aliases.json", aliasText.data(),
                                  aliasText.size(), MZ_BEST_COMPRESSION);
        }

        mz_zip_writer_finalize_archive(&zip);
        mz_zip_writer_end(&zip);

        std::cout << kGreen << "  [Done] " << kBold << fileCount << kReset
                  << kGreen << " items -> " << archive.output << kReset << "\n";
        return true;
    }
}

Bokken::CLI::PackPlan Bokken::CLI::planPack(
    const std::vector<PackArchive> &archives, Manifest &manifest)
{
    PackPlan plan;
    for (const PackArchive &candidate : archives)
    {
        PackArchive archive = candidate;
        archive.dirty = archiveIsDirty(archive, manifest);
        plan.archives.push_back(std::move(archive));
    }
    return plan;
}

int Bokken::CLI::runPackPlan(const PackPlan &plan, Manifest &manifest,
                             bool verbose)
{
    int failureCount = 0;
    for (const PackArchive &archive : plan.archives)
    {
        if (!archive.dirty)
        {
            std::cout << kCyan << "  [Skip] " << kReset << archive.output
                      << " (up to date)\n";
            continue;
        }

        if (writeArchive(archive, verbose))
        {
            // Record the input membership and a content signature so the
            // next pack can decide whether this archive is dirty. The
            // membership list is stored in `outputs` (reused as "inputs"),
            // the signature in `hash`.
            ManifestEntry record;
            record.hash = archiveSignature(archive);
            for (const PackEntry &entry : archive.entries)
                record.outputs.push_back(entry.realPath);
            manifest.put(membershipKey(archive.output), std::move(record));
        }
        else
        {
            failureCount++;
        }
    }
    return failureCount;
}

int Bokken::CLI::runPack(int argc, char *argv[])
{
    // argv[0] is the subcommand name ("pack"); positional args start at 1.
    std::vector<std::string> positionals;
    bool force = false;
    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--force") == 0)
            force = true;
        else
            positionals.push_back(argv[i]);
    }

    if (positionals.size() < 2)
    {
        std::cerr << kYel << "Usage: bokken-cli pack <input-dir> <output-prefix> "
                     "[--force]" << kReset << "\n";
        return 1;
    }

    const fs::path inputDir = positionals[0];
    const std::string outputPrefix = positionals[1];

    if (!fs::exists(inputDir))
    {
        std::cerr << kRed << "Error: source path " << inputDir << " not found."
                  << kReset << "\n";
        return 1;
    }

    // Collect the archives to consider, in priority order:
    //   1. scripts        — single archive, "scripts/" internal prefix
    //   2. assets.bokken  — descriptor-driven, folder-independent groups
    //   3. quality tiers  — one archive per low/middle/high/ultra subdir
    //   4. single archive — the whole directory
    std::vector<PackArchive> archives;

    const bool isScripts = (outputPrefix.find("scripts") != std::string::npos);
    if (isScripts)
    {
        archives.push_back(
            archiveFromFolder(inputDir, outputPrefix + ".assetpack", "scripts/"));
    }
    else
    {
        std::vector<AssetGroup> groups;
        if (loadAssetDescriptor(inputDir, groups))
        {
            const std::vector<std::string> allFiles = listRelativeFiles(inputDir);
            for (const AssetGroup &group : groups)
            {
                if (group.tiers)
                {
                    // A tiered group emits one archive per tier subdirectory
                    // that exists, matching the group's globs within it.
                    for (const std::string &tier : kQualityTiers)
                    {
                        const fs::path tierPath = inputDir / tier;
                        if (!fs::is_directory(tierPath))
                            continue;
                        const std::string output =
                            outputPrefix + "-" + group.name + "-" + tier + ".assetpack";
                        std::vector<std::string> tierFiles =
                            listRelativeFiles(tierPath);
                        archives.push_back(
                            archiveFromGroup(tierPath, group, output, tierFiles));
                    }
                }
                else
                {
                    const std::string output =
                        outputPrefix + "-" + group.name + ".assetpack";
                    archives.push_back(
                        archiveFromGroup(inputDir, group, output, allFiles));
                }
            }
        }
        else
        {
            bool foundTier = false;
            for (const std::string &tier : kQualityTiers)
            {
                const fs::path tierPath = inputDir / tier;
                if (fs::is_directory(tierPath))
                {
                    archives.push_back(archiveFromFolder(
                        tierPath, outputPrefix + "-" + tier + ".assetpack", ""));
                    foundTier = true;
                }
            }
            if (!foundTier)
            {
                archives.push_back(
                    archiveFromFolder(inputDir, outputPrefix + ".assetpack", ""));
            }
        }
    }

    // The manifest lives beside the first output so a clean removes it.
    const fs::path manifestPath =
        fs::path(outputPrefix).parent_path() / ".pack-cache.json";

    Manifest manifest;
    if (!force)
        manifest.load(manifestPath);

    const PackPlan plan = planPack(archives, manifest);
    const int failureCount = runPackPlan(plan, manifest, true);
    manifest.save(manifestPath);

    return (failureCount > 0) ? 1 : 0;
}
