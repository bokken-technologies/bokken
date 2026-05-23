#include "Manifest.hpp"

#include <fstream>
#include <iostream>

#include "nlohmann/json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Bokken
{
    namespace CLI
    {
        bool Manifest::load(const fs::path &path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
            {
                // No manifest yet — treat as empty so the first build
                // processes the whole tree.
                m_entries.clear();
                return true;
            }

            json document;
            try
            {
                file >> document;
            }
            catch (const std::exception &)
            {
                // A corrupt manifest is a real error: returning false lets
                // the caller fall back to a forced full rebuild rather than
                // silently trusting garbage.
                m_entries.clear();
                return false;
            }

            m_entries.clear();
            const auto entries = document.find("entries");
            if (entries == document.end() || !entries->is_object())
                return true;

            for (auto it = entries->begin(); it != entries->end(); ++it)
            {
                const json &value = it.value();
                ManifestEntry entry;
                entry.mtimeNs = value.value("mtimeNs", int64_t{0});
                entry.size = value.value("size", uint64_t{0});
                entry.hash = value.value("hash", uint64_t{0});
                if (value.contains("outputs") && value["outputs"].is_array())
                {
                    for (const auto &output : value["outputs"])
                        entry.outputs.push_back(output.get<std::string>());
                }
                m_entries.emplace(it.key(), std::move(entry));
            }
            return true;
        }

        bool Manifest::save(const fs::path &path) const
        {
            if (path.has_parent_path())
            {
                std::error_code ec;
                fs::create_directories(path.parent_path(), ec);
            }

            json entries = json::object();
            for (const auto &[input, entry] : m_entries)
            {
                json value;
                value["mtimeNs"] = entry.mtimeNs;
                value["size"] = entry.size;
                value["hash"] = entry.hash;
                value["outputs"] = entry.outputs;
                entries[input] = std::move(value);
            }

            json document;
            document["version"] = 1;
            document["entries"] = std::move(entries);

            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
                return false;

            // Pretty-printed so the manifest is diffable when it lands in
            // a build directory the developer happens to inspect.
            file << document.dump(2);
            return file.good();
        }

        bool Manifest::looksUnchanged(const std::string &input,
                                      int64_t mtimeNs, uint64_t size) const
        {
            const auto it = m_entries.find(input);
            if (it == m_entries.end())
                return false;
            return it->second.mtimeNs == mtimeNs && it->second.size == size;
        }

        const ManifestEntry *Manifest::find(const std::string &input) const
        {
            const auto it = m_entries.find(input);
            return it == m_entries.end() ? nullptr : &it->second;
        }

        void Manifest::put(const std::string &input, ManifestEntry entry)
        {
            m_entries[input] = std::move(entry);
        }

        void Manifest::erase(const std::string &input)
        {
            m_entries.erase(input);
        }

        std::vector<std::string> Manifest::stalePaths(
            const std::set<std::string> &seenInputs) const
        {
            std::vector<std::string> stale;
            for (const auto &[input, entry] : m_entries)
            {
                if (seenInputs.find(input) == seenInputs.end())
                    stale.push_back(input);
            }
            return stale;
        }

        uint64_t Manifest::hashFile(const fs::path &path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
                return 0;

            // FNV-1a, 64-bit. Chosen for zero dependencies and because the
            // hash only runs when mtime or size already differ, so its
            // speed is not on any hot path. The offset basis and prime are
            // the standard 64-bit FNV constants.
            constexpr uint64_t k_offsetBasis = 1469598103934665603ull;
            constexpr uint64_t k_prime = 1099511628211ull;

            uint64_t hash = k_offsetBasis;
            char buffer[8192];
            while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0)
            {
                const std::streamsize count = file.gcount();
                for (std::streamsize i = 0; i < count; i++)
                {
                    hash ^= static_cast<uint8_t>(buffer[i]);
                    hash *= k_prime;
                }
            }
            return hash;
        }

    } // namespace CLI
} // namespace Bokken
