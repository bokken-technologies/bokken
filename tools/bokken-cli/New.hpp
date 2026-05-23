#pragma once

namespace Bokken
{
    namespace CLI
    {
        /**
         * Scaffold a new project from the Bokken template repo.
         *
         * Two values drive the scaffold:
         *
         *   project name — human-facing. Becomes the README heading,
         *                  the project.bokken `displayTitle`, and any
         *                  other place humans see the title. May contain
         *                  spaces and most printable characters; only
         *                  ASCII control characters are rejected.
         *
         *   slug         — machine-facing. Becomes the new directory's
         *                  name, the CMake `project()` identifier, the
         *                  Makefile `APP_NAME` (i.e. the executable
         *                  name on disk), and the project.bokken
         *                  `internalSlug`. Restricted to
         *                  [A-Za-z_][A-Za-z0-9_-]*. Dashes are
         *                  converted to underscores when used as a
         *                  CMake / Make identifier.
         *
         * Three invocation forms:
         *
         *   bokken-cli new
         *       Fully interactive: prompts for project name, then for
         *       a slug (defaulting to a slugified form of the name).
         *
         *   bokken-cli new --name "<name>"
         *       Provides the project name; slug is derived and the
         *       prompt for it is skipped.
         *
         *   bokken-cli new --name "<name>" --slug "<slug>"
         *       Fully non-interactive — for CI / scripted use. Both
         *       values are validated and the scaffold proceeds without
         *       any user input.
         *
         * Workflow once values are known:
         *   1. Refuse if ./<slug>/ already exists.
         *   2. git clone --depth 1 the template repo into the destination.
         *   3. Remove .git/ so the user starts with a clean tree.
         *   4. Rewrite placeholders in CMakeLists.txt, Makefile,
         *      project.bokken, and README.md.
         *
         * Returns 0 on success, non-zero on any failure (with a
         * human-readable message on stderr).
        */
        int runNew(int argc, char *argv[]);
    }
}