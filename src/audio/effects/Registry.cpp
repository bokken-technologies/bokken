#include "Registry.hpp"

#include <SDL3/SDL.h>

namespace Bokken
{
    namespace Audio
    {
        namespace Effects
        {
            namespace
            {
                // The registry itself. A function-local static so it's
                // initialised on first use — no static-init-order
                // dance needed even if registerEffect() is called from
                // global constructors.
                std::unordered_map<std::string, EffectDescriptor> &registry()
                {
                    static std::unordered_map<std::string, EffectDescriptor> r;
                    return r;
                }
            }

            EffectDescriptor *registerEffectImpl(
                const std::string &name,
                std::function<std::unique_ptr<Base>()> factory)
            {
                auto &reg = registry();
                auto it = reg.find(name);
                if (it != reg.end())
                {
                    SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                                "[Effects::registerEffect] re-registering '%s' "
                                "(previous descriptor discarded)",
                                name.c_str());
                    // Wipe the existing descriptor's fields so the
                    // builder rebuilds it cleanly. We can't replace
                    // the EffectDescriptor* because callers from
                    // earlier registration calls may still hold one,
                    // but in practice the builder pattern means we
                    // return a fresh pointer per call anyway. Just
                    // reset the contents.
                    it->second.factory = std::move(factory);
                    it->second.params.clear();
                    return &it->second;
                }

                EffectDescriptor desc;
                desc.name = name;
                desc.factory = std::move(factory);
                auto [insIt, inserted] = reg.emplace(name, std::move(desc));
                return &insIt->second;
            }

            const EffectDescriptor *findEffect(const std::string &name)
            {
                auto &reg = registry();
                auto it = reg.find(name);
                return (it == reg.end()) ? nullptr : &it->second;
            }

            std::vector<std::string> registeredEffects()
            {
                auto &reg = registry();
                std::vector<std::string> names;
                names.reserve(reg.size());
                for (const auto &kv : reg)
                    names.push_back(kv.first);
                return names;
            }
        }
    }
}
