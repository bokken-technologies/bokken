#pragma once

#include <filesystem>
#include <string>

namespace Bokken
{
    namespace CLI
    {
        /**
         * Resolved layout of a Bokken project on disk, derived from the
         * project root and the conventional template directory names. Shared
         * by the build pipeline (build, watch) so the path conventions live
         * in exactly one place.
        */
        struct ProjectLayout
        {
            std::filesystem::path root;
            std::filesystem::path sourceDirectory;      // src/
            std::filesystem::path typesDirectory;       // types/
            std::filesystem::path assetsDirectory;      // assets/
            std::filesystem::path transpiledDirectory;  // build/<platform>/transpiled
            std::filesystem::path scriptsOutput;        // bin/assets/scripts (prefix)
            std::filesystem::path tsconfig;             // tsconfig.json
            std::string platform;                       // linux / darwin / windows

            // Directory compiled .script bytecode lands in before packing.
            std::filesystem::path bytecodeDirectory() const;

            // build/<platform> and build/<platform>/bin.
            std::filesystem::path buildDirectory() const;
            std::filesystem::path binDirectory() const;
        };

        /** Platform folder name matching the template Makefile's
         *  `uname -s | tr A-Z a-z` convention. */
        std::string detectPlatform();

        /** Build a ProjectLayout from a project root. */
        ProjectLayout resolveLayout(const std::filesystem::path &root);

        /** Transpile TypeScript (src/ + types/) into the transpiled output
         *  directory via `npx tsc`. Returns true on success. */
        bool runTypeScript(const ProjectLayout &layout);

        /** Compile transpiled JS to bytecode incrementally, then pack it into
         *  the scripts assetpack. Returns true on success. When force is
         *  true the incremental cache is ignored. */
        bool compileAndPackScripts(const ProjectLayout &layout, bool force);

        /** Repack every asset category under assets/ incrementally. Returns
         *  true on success. */
        bool repackAssets(const ProjectLayout &layout, bool force);

        /** Run the native CMake build for the project. Returns true on a
         *  successful build. configureIfNeeded runs cmake configure first
         *  when the build directory has no CMake cache yet. */
        bool nativeBuild(const ProjectLayout &layout, bool configureIfNeeded);

        /** Locate the built game executable under bin/. When binaryName is
         *  given (the project's APP_NAME, passed by the Makefile via
         *  --bin), that exact file is used; otherwise the bin directory is
         *  scanned for the lone executable, skipping bokken-cli. Returns an
         *  empty path if none is found. */
        std::filesystem::path resolveGameExecutable(
            const ProjectLayout &layout, const std::string &binaryName = {});

        /** Determine the game's executable base name by reading the CMake
         *  project(<name> ...) declaration in the project's CMakeLists.txt.
         *  This is authoritative: CMake names the output binary after the
         *  project. Returns an empty string if CMakeLists.txt can't be read or
         *  no project() name is found. */
        std::string resolveAppName(const ProjectLayout &layout);
    }
}
