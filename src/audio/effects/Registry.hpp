#pragma once

#include "Base.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Bokken
{
    namespace Audio
    {
        namespace Effects
        {
            /**
             * Per-parameter descriptor for a registered custom effect.
             *
             * The accessor is a type-erased lambda that hands back a
             * reference to the parameter's std::atomic<float> when given
             * a Base* pointing at an instance of the registered effect
             * type. We use std::function rather than a raw member-pointer
             * because the registry stores effects by their base class —
             * the concrete derived type has already been forgotten by
             * the time JS code reaches in to set a parameter.
            */
            struct ParamDescriptor
            {
                std::string name;
                std::function<std::atomic<float> &(Base *)> accessor;
                float defaultValue = 0.0f;
                float minimumValue = -1e30f;
                float maximumValue =  1e30f;
            };

            /**
             * Top-level descriptor for one registered custom effect
             * type. Lives in the static registry; never copied.
            */
            struct EffectDescriptor
            {
                std::string name;
                std::function<std::unique_ptr<Base>()> factory;
                std::vector<ParamDescriptor> params;
            };

            /**
             * Fluent builder returned from registerEffect(). Holds a
             * pointer to the descriptor that was inserted into the
             * registry and exposes parameter() to add per-field
             * accessors.
             *
             * The builder is templated on the concrete effect type T
             * so member-pointers like &WahWah::rate can be type-checked
             * at compile time. The accessor closure captures the
             * member pointer and returns the atomic<float> reference
             * after a static_cast back to T*.
            */
            template <typename T>
            class EffectBuilder
            {
                static_assert(std::is_base_of_v<Base, T>,
                              "registerEffect<T>: T must derive from Effects::Base");

            public:
                explicit EffectBuilder(EffectDescriptor *desc) : m_desc(desc) {}

                /**
                 * Bind a JS-visible parameter name to a std::atomic<float>
                 * member of the effect. The default value is what the
                 * parameter starts at when the effect is constructed,
                 * subject to whatever the constructor itself wrote first.
                 *
                 * Min/max bounds are advisory — JS scripts can write any
                 * value; the audio thread reads atomically and the
                 * effect's process() is responsible for clamping if
                 * out-of-range values would misbehave. The bounds are
                 * surfaced through introspection helpers (listed via
                 * audio.listEffects() in future tooling) but not
                 * enforced by the engine.
                */
                EffectBuilder &parameter(const std::string &name,
                                         std::atomic<float> T::*member,
                                         float defaultValue,
                                         float minimumValue = -1e30f,
                                         float maximumValue =  1e30f)
                {
                    ParamDescriptor pd;
                    pd.name = name;
                    pd.defaultValue = defaultValue;
                    pd.minimumValue = minimumValue;
                    pd.maximumValue = maximumValue;
                    pd.accessor = [member](Base *b) -> std::atomic<float> & {
                        return static_cast<T *>(b)->*member;
                    };
                    m_desc->params.push_back(std::move(pd));
                    return *this;
                }

            private:
                EffectDescriptor *m_desc;
            };

            /**
             * Register a custom effect type under a JS-visible name.
             *
             * Call this from C++ before Bokken::entryPoint(). After
             * the engine has booted, JS scripts can spawn instances
             * with audio.createEffect("name", initialParams?).
             *
             * Re-registering the same name overwrites the previous
             * descriptor — useful for hot-reload-style workflows in
             * development. A warning is logged when this happens.
             *
             * @example
             *   class WahWah : public Effects::Base {
             *   public:
             *       std::atomic<float> rate{5.0f};
             *       std::atomic<float> depth{0.5f};
             *       void process(Signal &s) override { ... }
             *   };
             *
             *   int main(int argc, char *argv[]) {
             *       Effects::registerEffect<WahWah>("wahwah")
             *           .parameter("rate",  &WahWah::rate,  5.0f, 0.1f, 50.0f)
             *           .parameter("depth", &WahWah::depth, 0.5f, 0.0f, 1.0f);
             *       return Bokken::entryPoint(argc, argv);
             *   }
            */
            template <typename T>
            EffectBuilder<T> registerEffect(const std::string &name);

            /** Look up a registered descriptor by name. Null if absent. */
            const EffectDescriptor *findEffect(const std::string &name);

            /** All registered effect names — for tooling and listEffects(). */
            std::vector<std::string> registeredEffects();

            /**
             * Internal: the concrete registration entry point that the
             * templated registerEffect() funnels into. Public so the
             * template can call it across translation units; not
             * intended for direct use.
            */
            EffectDescriptor *registerEffectImpl(
                const std::string &name,
                std::function<std::unique_ptr<Base>()> factory);

            // Inline definition of the template so client code can
            // instantiate it for any T without a separate
            // template-instantiation translation unit.
            template <typename T>
            EffectBuilder<T> registerEffect(const std::string &name)
            {
                auto factory = []() -> std::unique_ptr<Base> {
                    return std::unique_ptr<Base>(new T());
                };
                EffectDescriptor *desc = registerEffectImpl(name, std::move(factory));
                return EffectBuilder<T>(desc);
            }
        }
    }
}
