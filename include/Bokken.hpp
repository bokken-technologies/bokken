#pragma once

/**
 * Bokken — public engine entry point.
 *
 * The engine reads a handful of paths and tunables at build time. They're
 * defined as preprocessor macros consumed by Bokken.cpp inside the engine
 * library, so changing them means rebuilding the engine, not the consumer.
 *
 *   BOKKEN_PROJECT_PATH   - path to project.bokken    ("project.bokken")
 *   BOKKEN_SCRIPT_PACK    - script asset pack         ("assets/scripts.assetpack")
 *   BOKKEN_ENVIRONMENT    - environment string        ("development")
 *   BOKKEN_FIXED_HZ       - fixed-step rate           (60)
 *   BOKKEN_ENTRY_SCRIPT   - entry script in pack      ("/scripts/index.script")
 */

#include "./audio/effects/Base.hpp"
#include "./audio/effects/Registry.hpp"
#include "./audio/Signal.hpp"

namespace Bokken
{
    /**
     * Bootstrap the engine, mount asset packs, load the entry script,
     * and run the main loop until the user quits.
     *
     * @param argc forwarded from main(); currently unused but reserved.
     * @param argv forwarded from main(); argv[0] is used by PhysFS for
     *             path resolution.
     * @return EXIT_SUCCESS or EXIT_FAILURE.
    */
    int entryPoint(int argc, char *argv[]);

} // namespace Bokken
