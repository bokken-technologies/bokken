#include "Compile.hpp"
#include "Manifest.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "quickjs.h"

namespace fs = std::filesystem;

namespace
{
    // Carries the source root through the QuickJS module loader so user
    // imports can be resolved relative to it.
    struct CompilerContext
    {
        fs::path sourceDirectory;
    };

    // Stub native modules.
    //
    // The compiler doesn't link against the engine — it just needs to
    // compile bytecode that *references* the bokken/* modules. At
    // compile time we install empty stubs so QuickJS's module resolver
    // is satisfied; the real implementations are wired in by the engine
    // at run time.
    //
    // Each stub exports a single `default` symbol with no actual content.
    // This is sufficient because the bytecode writer only needs a
    // resolved JSModuleDef, not a fully-populated one.

    int stubInit(JSContext *ctx, JSModuleDef *m)
    {
        JS_SetModuleExport(ctx, m, "default", JS_NewObject(ctx));
        return 0;
    }

    JSModuleDef *makeStub(JSContext *ctx, const char *name)
    {
        JSModuleDef *m = JS_NewCModule(ctx, name, stubInit);
        JS_AddModuleExport(ctx, m, "default");
        return m;
    }

    // The set of bokken/* modules the engine exposes at run time. Kept
    // as a flat list because that's both the smallest representation
    // and the easiest to keep in sync with the engine's module registry.
    constexpr const char *kBokkenModules[] = {
        "bokken/audio",
        "bokken/canvas",
        "bokken/engine",
        "bokken/gameObject",
        "bokken/input",
        "bokken/log",
        "bokken/network",
        "bokken/physics",
        "bokken/renderer",
        "bokken/window",
    };

    JSModuleDef *moduleLoader(JSContext *ctx, const char *moduleName, void *opaque)
    {
        auto *configuration = static_cast<CompilerContext *>(opaque);

        // 1. Resolve native bokken/* modules to stubs.
        for (const char *known : kBokkenModules)
        {
            if (std::strcmp(moduleName, known) == 0)
                return makeStub(ctx, known);
        }

        // 2. Resolve user modules relative to the source root. QuickJS's
        //    bytecode loader passes module specifiers verbatim, so we
        //    accept both extension-less imports (the TS-bundler default)
        //    and explicit ".js" imports.
        fs::path targetPath = configuration->sourceDirectory / moduleName;

        if (!fs::exists(targetPath) && fs::exists(targetPath.string() + ".js"))
        {
            targetPath += ".js";
        }

        std::ifstream file(targetPath);
        if (!file.is_open())
        {
            std::fprintf(stderr, "  [Loader] Could not find user module: %s\n", moduleName);
            return nullptr;
        }

        std::stringstream buf;
        buf << file.rdbuf();
        const std::string code = buf.str();

        // Compile the module. Keeping the original module name as the
        // "filename" preserves clean references in any nested imports
        // and in error messages.
        JSValue func = JS_Eval(ctx, code.c_str(), code.size(), moduleName,
                               JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);

        if (JS_IsException(func))
            return nullptr;

        // Do NOT JS_FreeValue(func): the JSModuleDef has to stay alive
        // until JS_WriteObject serialises this module's bytecode. The
        // runtime owns the def either way — the JSValue is just a
        // typed handle to it.
        return reinterpret_cast<JSModuleDef *>(JS_VALUE_GET_PTR(func));
    }
}

namespace
{
    using Bokken::CLI::CompileJob;
    using Bokken::CLI::CompilePlan;
    using Bokken::CLI::Manifest;
    using Bokken::CLI::ManifestEntry;

    // Modification time of a file in nanoseconds since the filesystem
    // clock epoch. The absolute epoch does not matter — only that the
    // value is stable and comparable against the manifest's record.
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

    // Compile one JavaScript source file to a QuickJS bytecode (.script)
    // file. Returns true on success; on failure the QuickJS exception is
    // printed and the output is left untouched.
    bool compileOne(JSContext *context, const CompileJob &job,
                    const fs::path &sourceDirectory)
    {
        std::ifstream input(job.input, std::ios::binary);
        const std::string source((std::istreambuf_iterator<char>(input)), {});

        // Use the path relative to the source root as the module name so
        // error messages and inter-module bytecode references point at
        // human-readable identifiers.
        const std::string moduleName =
            fs::relative(job.input, sourceDirectory).string();

        JSValue compiled = JS_Eval(context, source.c_str(), source.size(),
                                   moduleName.c_str(),
                                   JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);

        if (JS_IsException(compiled))
        {
            JSValue exception = JS_GetException(context);
            const char *text = JS_ToCString(context, exception);
            std::cerr << "  Error in " << moduleName << ": "
                      << (text ? text : "unknown") << "\n";
            JS_FreeCString(context, text);
            JS_FreeValue(context, exception);
            return false;
        }

        size_t bytecodeLength = 0;
        uint8_t *bytecode =
            JS_WriteObject(context, &bytecodeLength, compiled, JS_WRITE_OBJ_BYTECODE);

        bool ok = false;
        if (bytecode)
        {
            std::error_code ec;
            fs::create_directories(fs::path(job.output).parent_path(), ec);

            std::ofstream output(job.output, std::ios::binary | std::ios::trunc);
            if (output.is_open())
            {
                output.write(reinterpret_cast<const char *>(bytecode),
                             static_cast<std::streamsize>(bytecodeLength));
                ok = output.good();
            }
            js_free(context, bytecode);

            if (ok)
                std::cout << "\u2713 " << moduleName << " (" << bytecodeLength
                          << " bytes)\n";
            else
                std::cerr << "  Error writing " << job.output << "\n";
        }

        JS_FreeValue(context, compiled);
        return ok;
    }

    // Map a source .js path to its .script output path, preserving the
    // layout relative to the source root.
    fs::path outputFor(const fs::path &input, const fs::path &sourceDirectory,
                       const fs::path &outputDirectory)
    {
        fs::path out = outputDirectory / fs::relative(input, sourceDirectory);
        out.replace_extension(".script");
        return out;
    }
}

Bokken::CLI::CompilePlan Bokken::CLI::planCompile(
    const fs::path &sourceDir, const fs::path &outputDir,
    Manifest &manifest, const std::vector<std::string> &restrictTo)
{
    const fs::path sourceDirectory = fs::absolute(sourceDir);
    const fs::path outputDirectory = fs::absolute(outputDir);

    CompilePlan plan;
    plan.sourceRoot = sourceDirectory.string();
    std::set<std::string> seenInputs;

    // Build the candidate input set: either the explicit restrictTo list
    // (the watcher's fast path) or every .js under the source tree.
    std::vector<fs::path> candidates;
    if (!restrictTo.empty())
    {
        for (const std::string &path : restrictTo)
        {
            const fs::path absolute = fs::absolute(path);
            if (absolute.extension() == ".js" && fs::exists(absolute))
                candidates.push_back(absolute);
        }
    }
    else
    {
        std::error_code ec;
        for (const auto &entry :
             fs::recursive_directory_iterator(sourceDirectory, ec))
        {
            if (!ec && entry.is_regular_file() &&
                entry.path().extension() == ".js")
                candidates.push_back(entry.path());
        }
    }

    for (const fs::path &input : candidates)
    {
        const std::string key = input.string();
        seenInputs.insert(key);

        const fs::path output = outputFor(input, sourceDirectory, outputDirectory);

        // Schedule the job when the output is missing, the input is
        // untracked, or its metadata differs and a content hash confirms a
        // real change.
        bool needsCompile = !fs::exists(output);
        if (!needsCompile)
        {
            const int64_t mtime = modificationTimeNs(input);
            const uint64_t size = fileSize(input);
            if (!manifest.looksUnchanged(key, mtime, size))
            {
                const ManifestEntry *previous = manifest.find(key);
                const uint64_t hash = Manifest::hashFile(input);
                needsCompile = (previous == nullptr || previous->hash != hash);
            }
        }

        if (needsCompile)
            plan.toCompile.push_back(CompileJob{key, output.string()});
    }

    // Only prune stale outputs on a full pass — a restricted pass does not
    // know about files outside its list and must not delete their outputs.
    if (restrictTo.empty())
    {
        for (const std::string &stale : manifest.stalePaths(seenInputs))
        {
            const ManifestEntry *entry = manifest.find(stale);
            if (entry)
            {
                for (const std::string &output : entry->outputs)
                    plan.toDelete.push_back(output);
            }
        }
    }

    return plan;
}

int Bokken::CLI::runCompilePlan(const CompilePlan &plan, Manifest &manifest,
                                bool verbose)
{
    // Delete orphaned outputs first so a removed source never ships a
    // stale .script, then forget the input that produced them.
    for (const std::string &output : plan.toDelete)
    {
        std::error_code ec;
        fs::remove(output, ec);
        if (verbose)
            std::cout << "Removing orphaned output: " << output << "\n";
    }

    if (plan.toCompile.empty())
    {
        if (verbose)
            std::cout << "Nothing to compile (all outputs up to date).\n";
        return 0;
    }

    // The module loader resolves user imports relative to the source
    // root, which the planner recorded on the plan.
    const fs::path sourceDirectory = plan.sourceRoot.empty()
        ? fs::path(plan.toCompile.front().input).parent_path()
        : fs::path(plan.sourceRoot);

    CompilerContext context{sourceDirectory};
    JSRuntime *runtime = JS_NewRuntime();
    JSContext *jsContext = JS_NewContext(runtime);
    JS_SetModuleLoaderFunc(runtime, nullptr, moduleLoader, &context);

    int failureCount = 0;
    for (const CompileJob &job : plan.toCompile)
    {
        if (verbose)
            std::cout << "Compiling: "
                      << fs::relative(job.input, sourceDirectory).string() << "\n";

        if (compileOne(jsContext, job, sourceDirectory))
        {
            // Record success so the next build skips this input unless it
            // changes again.
            ManifestEntry entry;
            entry.mtimeNs = modificationTimeNs(job.input);
            entry.size = fileSize(job.input);
            entry.hash = Manifest::hashFile(job.input);
            entry.outputs = {job.output};
            manifest.put(job.input, std::move(entry));
        }
        else
        {
            failureCount++;
        }
    }

    JS_FreeContext(jsContext);
    JS_FreeRuntime(runtime);
    return failureCount;
}

int Bokken::CLI::runCompile(int argc, char *argv[])
{
    std::string sourceDirString;
    std::string outputDirString;
    bool verbose = false;
    bool force = false;

    // argv[0] is the subcommand name ("compile"); start parsing at 1.
    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--source") == 0 && i + 1 < argc)
            sourceDirString = argv[++i];
        else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc)
            outputDirString = argv[++i];
        else if (std::strcmp(argv[i], "--verbose") == 0 ||
                 std::strcmp(argv[i], "-v") == 0)
            verbose = true;
        else if (std::strcmp(argv[i], "--force") == 0)
            force = true;
    }

    if (sourceDirString.empty() || outputDirString.empty())
    {
        std::cerr << "Usage: bokken-cli compile --source <dir> --output <dir> "
                     "[--verbose] [--force]\n";
        return 1;
    }

    const fs::path sourceDirectory = fs::absolute(sourceDirString);
    const fs::path outputDirectory = fs::absolute(outputDirString);

    // The manifest lives beside the output so a clean of the build tree
    // removes it. Naming it after the output keeps separate compile
    // targets from sharing incremental state.
    const fs::path manifestPath = outputDirectory / ".compile-cache.json";

    Manifest manifest;
    if (!force)
        manifest.load(manifestPath);

    const CompilePlan plan =
        planCompile(sourceDirectory, outputDirectory, manifest);

    const int failureCount = runCompilePlan(plan, manifest, verbose);

    manifest.save(manifestPath);

    const int compiledCount =
        static_cast<int>(plan.toCompile.size()) - failureCount;
    std::cout << "\nCompilation complete: " << compiledCount << " succeeded, "
              << failureCount << " failed";
    if (!plan.toDelete.empty())
        std::cout << ", " << plan.toDelete.size() << " removed";
    std::cout << "\n";

    return (failureCount > 0) ? 1 : 0;
}
