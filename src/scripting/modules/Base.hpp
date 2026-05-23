#pragma once

#include <quickjs.h>
#include <string>
#include <unordered_map>

namespace Bokken
{
    namespace Scripting
    {
        namespace Modules
        {
            /**
             * Abstract base for every native C++ module exposed to the JS scripting layer.
             *
             * Lifecycle:
             *   1. Construct with a module name matching the JS import path (e.g. "bokken/log")
             *   2. Call registerInto(ctx) before any JS that imports this module is evaluated.
             *      QuickJS will then invoke declare() → init() in sequence.
             *   3. Call destroy() during engine shutdown to free any native resources.
             *
             * Implementation note — no JS_SetModuleOpaque/Private:
             *   QuickJS-NG does not expose a user-data slot on JSModuleDef in its public
             *   API. Instead we keep a static per-runtime registry (JSRuntime* → Base*)
             *   keyed by module name, which the static init callback uses to dispatch
             *   to the correct subclass instance.
            */
            class Base
            {
            public:
                explicit Base(std::string name) : m_name(std::move(name)) {}
                virtual ~Base() = default;

                Base(const Base &) = delete;
                Base &operator=(const Base &) = delete;
                Base(Base &&) = default;
                Base &operator=(Base &&) = default;

                const std::string &name() const { return m_name; }

                /**
                 * Phase 1 – Declaration.
                 * Called once immediately after JS_NewCModule.
                 * Use JS_AddModuleExport() here to declare every symbol this module exports.
                */
                virtual int declare(JSContext *ctx, JSModuleDef *m) = 0;

                /**
                 * Phase 2 – Initialisation.
                 * Called by QuickJS when the module is first imported.
                 * Use JS_SetModuleExport() here to populate the declared exports with values.
                 * Return 0 on success, -1 on failure.
                */
                virtual int init(JSContext *ctx, JSModuleDef *m) = 0;

                /**
                 * Phase 3 – Destructor hook.
                 * Called by the Engine during shutdown AND on every live
                 * reload, while the context is still valid (the Engine passes
                 * the live ctx in explicitly because by this point it has
                 * already cleared its own m_ctx member). A module overrides
                 * this to release every JS handle it retains across calls —
                 * cached JSValues, interned JSAtoms, JS-owning scenes — so
                 * nothing survives into JS_FreeRuntime. Native, non-JS
                 * resources tied to the renderer/window/audio device lifetime
                 * are NOT freed here; they follow their own owners.
                */
                virtual void destroy(JSContext * /*ctx*/) {}

                /**
                 * Registers this module into the provided context.
                 * Must be called before any JS code that imports this module is evaluated.
                */
                void registerInto(JSContext *ctx)
                {
                    JSRuntime *rt = JS_GetRuntime(ctx);

                    // Store this instance in the registry before creating the module
                    // so the static callback can find it immediately.
                    s_registry[rt][m_name] = this;

                    JSModuleDef *m = JS_NewCModule(ctx, m_name.c_str(), &Base::s_init_cb);
                    if (!m)
                    {
                        s_registry[rt].erase(m_name);
                        return;
                    }

                    declare(ctx, m);
                }

                /**
                 * Remove all entries for a given runtime from the registry.
                */
                static void unregisterRuntime(JSRuntime *rt)
                {
                    s_registry.erase(rt);
                }

            private:
                std::string m_name;

                // registry[runtime][moduleName] → Base*
                // Using raw pointers is safe here because Base instances are owned by
                // Engine and always outlive the JSRuntime they're registered into.
                using ModuleMap = std::unordered_map<std::string, Base *>;
                using RuntimeMap = std::unordered_map<JSRuntime *, ModuleMap>;
                inline static RuntimeMap s_registry;

                // Static trampoline — QuickJS requires a plain C function pointer.
                // We recover the Base* from the registry using the runtime + module name.
                static int s_init_cb(JSContext *ctx, JSModuleDef *m)
                {
                    JSRuntime *rt = JS_GetRuntime(ctx);

                    auto runtime_it = s_registry.find(rt);
                    if (runtime_it == s_registry.end())
                        return -1;

                    // Retrieve the module name from the def so we can look up the instance.
                    JSAtom nameAtom = JS_GetModuleName(ctx, m);
                    const char *nameCStr = JS_AtomToCString(ctx, nameAtom);
                    JS_FreeAtom(ctx, nameAtom);

                    if (!nameCStr)
                        return -1;

                    std::string name(nameCStr);
                    JS_FreeCString(ctx, nameCStr);

                    auto module_it = runtime_it->second.find(name);
                    if (module_it == runtime_it->second.end())
                        return -1;

                    return module_it->second->init(ctx, m);
                }
            };

        } // namespace Modules
    } // namespace Scripting
} // namespace Bokken