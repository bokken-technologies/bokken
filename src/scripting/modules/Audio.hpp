#pragma once

#include "Base.hpp"
#include "../Engine.hpp"
#include "../../audio/Mixer.hpp"
#include "../../audio/Channel.hpp"
#include "../../audio/Signal.hpp"
#include "../../audio/effects/Base.hpp"
#include "../../audio/effects/Compressor.hpp"
#include "../../audio/effects/Delay.hpp"
#include "../../audio/effects/Distortion.hpp"
#include "../../audio/effects/Gain.hpp"
#include "../../audio/effects/HighPass.hpp"
#include "../../audio/effects/LowPass.hpp"
#include "../../audio/effects/Reverb.hpp"
#include "../../audio/effects/Registry.hpp"

#include <SDL3/SDL.h>

#include <cstring>

namespace Bokken
{
    namespace Scripting
    {
        namespace Modules
        {
            /*
             * Bridges the native Bokken::Audio mixer / channel / effect graph
             * into the JS scripting runtime.
             *
             * Exposes the "bokken/audio" module with:
             *   - default export: a singleton object exposing
             *       createChannel(name, volume?, muted?)
             *       channel(name)
             *       master()
             *   - "Master": the literal string "Master" (channel name constant).
             *   - Channel class — opaque wrapper with volume / muted accessors,
             *     setVolume, setMuted, addEffect, removeEffect, getEffects.
             *   - Effect classes: Gain, HighPass, LowPass, Compressor, Delay,
             *     Distortion, Reverb. Each is constructible with parameter
             *     defaults and exposes bypass() / enable() plus its public fields.
             *
             * Ownership semantics:
             *   Each effect is heap-allocated by its JS constructor. While the
             *   effect is unattached the JS handle owns it (finalizer frees it).
             *   Calling channel.addEffect(fx) transfers ownership to the channel
             *   (the wrapper marks itself "attached" so the finalizer no-ops).
            */
            class Audio : public Base
            {
            public:
                Audio() : Base("bokken/audio") {}

                int declare(JSContext *ctx, JSModuleDef *m) override;
                int init(JSContext *ctx, JSModuleDef *m) override;

                /*
                 * Wrapper that pairs an effect pointer with an "attached" flag.
                 * Stored as the opaque payload of every effect JS handle so the
                 * finalizer can decide whether to free or leave the pointer alone.
                 *
                 * For custom (registered) effects the descriptor pointer is
                 * non-null and gives the magic-int dispatcher access to the
                 * parameter accessors. For built-in effects (Gain, Reverb, etc.)
                 * the descriptor is null because their parameters use hand-
                 * written CGETSET handlers that already know their own layout.
                */
                struct EffectHandle
                {
                    Bokken::Audio::Effects::Base *effect = nullptr;
                    bool attached = false;
                    const Bokken::Audio::Effects::EffectDescriptor *descriptor = nullptr;
                };

            private:
                static inline JSClassID s_channel_class_id = 0;
                static inline JSClassID s_gain_class_id = 0;
                static inline JSClassID s_highpass_class_id = 0;
                static inline JSClassID s_lowpass_class_id = 0;
                static inline JSClassID s_compressor_class_id = 0;
                static inline JSClassID s_delay_class_id = 0;
                static inline JSClassID s_distortion_class_id = 0;
                static inline JSClassID s_reverb_class_id = 0;

                // One shared class id for every user-registered effect.
                // Per-instance dynamic property bindings give each
                // descriptor its own named getters/setters even though
                // they share the same JS class.
                static inline JSClassID s_custom_effect_class_id = 0;

                /* Mixer-level operations (default export). */
                static JSValue js_create_channel(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_get_channel(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_master(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

                /* Custom effect factory + introspection. */
                static JSValue js_create_effect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_list_effects(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

                /* Custom-effect parameter dispatcher. The magic int is
                 * the parameter index in the effect's descriptor.
                 *
                 * Generic-magic signature is required because we install
                 * these via JS_DefinePropertyGetSet, which invokes
                 * getters/setters through the standard function-call
                 * path. The dedicated getter_magic / setter_magic
                 * cprotos abort when called that way — they're for the
                 * CGETSET property-list path only. */
                static JSValue js_custom_effect_get(JSContext *ctx, JSValueConst this_val,
                                                    int argc, JSValueConst *argv, int magic);
                static JSValue js_custom_effect_set(JSContext *ctx, JSValueConst this_val,
                                                    int argc, JSValueConst *argv, int magic);

                /* Channel methods. */
                static JSValue js_channel_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_channel_add_effect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_channel_remove_effect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_channel_get_effects(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_channel_set_volume(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_channel_set_muted(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

                /* Effect constructors. */
                static JSValue js_gain_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);
                static JSValue js_highpass_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);
                static JSValue js_lowpass_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);
                static JSValue js_compressor_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);
                static JSValue js_delay_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);
                static JSValue js_distortion_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);
                static JSValue js_reverb_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);

                /* Shared effect methods (bypass / enable). */
                static JSValue js_effect_bypass(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_effect_enable(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

                /* Generic enabled property getter/setter. */
                static JSValue js_effect_enabled_get(JSContext *ctx, JSValueConst this_val);
                static JSValue js_effect_enabled_set(JSContext *ctx, JSValueConst this_val, JSValueConst val);

                /* Per-effect property getters / setters — keyed via magic. */
                static JSValue js_gain_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_gain_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
                static JSValue js_highpass_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_highpass_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
                static JSValue js_lowpass_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_lowpass_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
                static JSValue js_compressor_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_compressor_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
                static JSValue js_delay_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_delay_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
                static JSValue js_distortion_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_distortion_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
                static JSValue js_reverb_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_reverb_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);

                /* Wrapping helpers. */
                static JSValue wrap_channel(JSContext *ctx, Bokken::Audio::Channel *channel);
                static JSValue wrap_effect(JSContext *ctx, JSClassID classId, Bokken::Audio::Effects::Base *effect);
                static EffectHandle *get_effect_handle(JSValueConst val);
                static Bokken::Audio::Effects::Base *get_effect_for_any(JSValueConst val);

                /* Finalizer for any effect class — frees the underlying DSP node
                 * only when the JS handle still owns it (i.e. it was never
                 * attached to a channel). */
                static void effect_finalizer(JSRuntime *rt, JSValue val);
            };
        }
    }
}