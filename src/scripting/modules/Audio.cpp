#include "Audio.hpp"

namespace Bokken
{
    namespace Scripting
    {
        namespace Modules
        {
            /* Property magic numbers — keep one enum per effect so the
             * getter/setter pair can dispatch in a single switch. */

            enum ChannelProperties
            {
                CH_Volume = 0,
                CH_Muted,
                CH_Name
            };

            enum GainProperties
            {
                GN_Gain = 0
            };

            enum HighPassProperties
            {
                HP_Cutoff = 0,
                HP_Resonance
            };

            enum LowPassProperties
            {
                LP_Cutoff = 0,
                LP_Resonance
            };

            enum CompressorProperties
            {
                CP_Threshold = 0,
                CP_Ratio,
                CP_Attack,
                CP_Release
            };

            enum DelayProperties
            {
                DL_Time = 0,
                DL_Feedback,
                DL_Wet
            };

            enum DistortionProperties
            {
                DS_Drive = 0,
                DS_Tone
            };

            enum ReverbProperties
            {
                RV_Decay = 0,
                RV_Wet
            };

            /* Read a JS value as a float64 with a fallback default. */
            static float read_float(JSContext *ctx, JSValueConst val, float fallback)
            {
                if (JS_IsUndefined(val) || JS_IsNull(val))
                    return fallback;
                double d = static_cast<double>(fallback);
                if (JS_ToFloat64(ctx, &d, val) < 0)
                    return fallback;
                return static_cast<float>(d);
            }

            /* Module declaration — registers exports & native classes with QuickJS. */
            int Audio::declare(JSContext *ctx, JSModuleDef *m)
            {
                JSRuntime *rt = JS_GetRuntime(ctx);

                JS_NewClassID(rt, &s_channel_class_id);
                JS_NewClassID(rt, &s_gain_class_id);
                JS_NewClassID(rt, &s_highpass_class_id);
                JS_NewClassID(rt, &s_lowpass_class_id);
                JS_NewClassID(rt, &s_compressor_class_id);
                JS_NewClassID(rt, &s_delay_class_id);
                JS_NewClassID(rt, &s_distortion_class_id);
                JS_NewClassID(rt, &s_reverb_class_id);
                JS_NewClassID(rt, &s_custom_effect_class_id);

                /* Channel handle is non-owning (Mixer owns the channels), so no
                 * finalizer is needed. */
                static JSClassDef channelClass = {"Channel", .finalizer = nullptr};

                /* Effect handles all share the same finalizer. */
                static JSClassDef gainClass = {"Gain", .finalizer = effect_finalizer};
                static JSClassDef highpassClass = {"HighPass", .finalizer = effect_finalizer};
                static JSClassDef lowpassClass = {"LowPass", .finalizer = effect_finalizer};
                static JSClassDef compressorClass = {"Compressor", .finalizer = effect_finalizer};
                static JSClassDef delayClass = {"Delay", .finalizer = effect_finalizer};
                static JSClassDef distortionClass = {"Distortion", .finalizer = effect_finalizer};
                static JSClassDef reverbClass = {"Reverb", .finalizer = effect_finalizer};

                // Single class shared by every registered custom effect.
                // Distinct instances differ by their per-object property
                // bindings, not by class id.
                static JSClassDef customEffectClass = {"Effect", .finalizer = effect_finalizer};

                JS_NewClass(rt, s_channel_class_id, &channelClass);
                JS_NewClass(rt, s_gain_class_id, &gainClass);
                JS_NewClass(rt, s_highpass_class_id, &highpassClass);
                JS_NewClass(rt, s_lowpass_class_id, &lowpassClass);
                JS_NewClass(rt, s_compressor_class_id, &compressorClass);
                JS_NewClass(rt, s_delay_class_id, &delayClass);
                JS_NewClass(rt, s_distortion_class_id, &distortionClass);
                JS_NewClass(rt, s_reverb_class_id, &reverbClass);
                JS_NewClass(rt, s_custom_effect_class_id, &customEffectClass);

                JS_AddModuleExport(ctx, m, "default");
                JS_AddModuleExport(ctx, m, "Master");
                JS_AddModuleExport(ctx, m, "Channel");
                JS_AddModuleExport(ctx, m, "Gain");
                JS_AddModuleExport(ctx, m, "HighPass");
                JS_AddModuleExport(ctx, m, "LowPass");
                JS_AddModuleExport(ctx, m, "Compressor");
                JS_AddModuleExport(ctx, m, "Delay");
                JS_AddModuleExport(ctx, m, "Distortion");
                JS_AddModuleExport(ctx, m, "Reverb");

                return 0;
            }

            /* Module initialisation — populates the declared exports with values. */
            int Audio::init(JSContext *ctx, JSModuleDef *m)
            {
                /* Channel prototype. */
                JSValue channelProto = JS_NewObject(ctx);
                static const JSCFunctionListEntry channelFuncs[] = {
                    JS_CGETSET_MAGIC_DEF("volume", js_channel_get, nullptr, CH_Volume),
                    JS_CGETSET_MAGIC_DEF("muted", js_channel_get, nullptr, CH_Muted),
                    JS_CGETSET_MAGIC_DEF("name", js_channel_get, nullptr, CH_Name),
                    JS_CFUNC_DEF("setVolume", 1, js_channel_set_volume),
                    JS_CFUNC_DEF("setMuted", 1, js_channel_set_muted),
                    JS_CFUNC_DEF("addEffect", 1, js_channel_add_effect),
                    JS_CFUNC_DEF("removeEffect", 1, js_channel_remove_effect),
                    JS_CFUNC_DEF("getEffects", 0, js_channel_get_effects),
                };
                JS_SetPropertyFunctionList(ctx, channelProto, channelFuncs,
                                           sizeof(channelFuncs) / sizeof(JSCFunctionListEntry));
                JS_SetClassProto(ctx, s_channel_class_id, channelProto);

                /* Gain prototype. */
                JSValue gainProto = JS_NewObject(ctx);
                static const JSCFunctionListEntry gainFuncs[] = {
                    JS_CGETSET_MAGIC_DEF("gain", js_gain_get, js_gain_set, GN_Gain),
                    JS_CGETSET_DEF("enabled", js_effect_enabled_get, js_effect_enabled_set),
                    JS_CFUNC_DEF("bypass", 0, js_effect_bypass),
                    JS_CFUNC_DEF("enable", 0, js_effect_enable),
                };
                JS_SetPropertyFunctionList(ctx, gainProto, gainFuncs,
                                           sizeof(gainFuncs) / sizeof(JSCFunctionListEntry));
                JSValue gainCtor = JS_NewCFunction2(ctx, js_gain_ctor, "Gain", 1, JS_CFUNC_constructor, 0);
                JS_SetConstructor(ctx, gainCtor, gainProto);
                JS_SetClassProto(ctx, s_gain_class_id, gainProto);

                /* HighPass prototype. */
                JSValue highpassProto = JS_NewObject(ctx);
                static const JSCFunctionListEntry highpassFuncs[] = {
                    JS_CGETSET_MAGIC_DEF("cutoff", js_highpass_get, js_highpass_set, HP_Cutoff),
                    JS_CGETSET_MAGIC_DEF("resonance", js_highpass_get, js_highpass_set, HP_Resonance),
                    JS_CGETSET_DEF("enabled", js_effect_enabled_get, js_effect_enabled_set),
                    JS_CFUNC_DEF("bypass", 0, js_effect_bypass),
                    JS_CFUNC_DEF("enable", 0, js_effect_enable),
                };
                JS_SetPropertyFunctionList(ctx, highpassProto, highpassFuncs,
                                           sizeof(highpassFuncs) / sizeof(JSCFunctionListEntry));
                JSValue highpassCtor = JS_NewCFunction2(ctx, js_highpass_ctor, "HighPass", 2, JS_CFUNC_constructor, 0);
                JS_SetConstructor(ctx, highpassCtor, highpassProto);
                JS_SetClassProto(ctx, s_highpass_class_id, highpassProto);

                /* LowPass prototype. */
                JSValue lowpassProto = JS_NewObject(ctx);
                static const JSCFunctionListEntry lowpassFuncs[] = {
                    JS_CGETSET_MAGIC_DEF("cutoff", js_lowpass_get, js_lowpass_set, LP_Cutoff),
                    JS_CGETSET_MAGIC_DEF("resonance", js_lowpass_get, js_lowpass_set, LP_Resonance),
                    JS_CGETSET_DEF("enabled", js_effect_enabled_get, js_effect_enabled_set),
                    JS_CFUNC_DEF("bypass", 0, js_effect_bypass),
                    JS_CFUNC_DEF("enable", 0, js_effect_enable),
                };
                JS_SetPropertyFunctionList(ctx, lowpassProto, lowpassFuncs,
                                           sizeof(lowpassFuncs) / sizeof(JSCFunctionListEntry));
                JSValue lowpassCtor = JS_NewCFunction2(ctx, js_lowpass_ctor, "LowPass", 2, JS_CFUNC_constructor, 0);
                JS_SetConstructor(ctx, lowpassCtor, lowpassProto);
                JS_SetClassProto(ctx, s_lowpass_class_id, lowpassProto);

                /* Compressor prototype. */
                JSValue compressorProto = JS_NewObject(ctx);
                static const JSCFunctionListEntry compressorFuncs[] = {
                    JS_CGETSET_MAGIC_DEF("threshold", js_compressor_get, js_compressor_set, CP_Threshold),
                    JS_CGETSET_MAGIC_DEF("ratio", js_compressor_get, js_compressor_set, CP_Ratio),
                    JS_CGETSET_MAGIC_DEF("attack", js_compressor_get, js_compressor_set, CP_Attack),
                    JS_CGETSET_MAGIC_DEF("release", js_compressor_get, js_compressor_set, CP_Release),
                    JS_CGETSET_DEF("enabled", js_effect_enabled_get, js_effect_enabled_set),
                    JS_CFUNC_DEF("bypass", 0, js_effect_bypass),
                    JS_CFUNC_DEF("enable", 0, js_effect_enable),
                };
                JS_SetPropertyFunctionList(ctx, compressorProto, compressorFuncs,
                                           sizeof(compressorFuncs) / sizeof(JSCFunctionListEntry));
                JSValue compressorCtor = JS_NewCFunction2(ctx, js_compressor_ctor, "Compressor", 4, JS_CFUNC_constructor, 0);
                JS_SetConstructor(ctx, compressorCtor, compressorProto);
                JS_SetClassProto(ctx, s_compressor_class_id, compressorProto);

                /* Delay prototype. */
                JSValue delayProto = JS_NewObject(ctx);
                static const JSCFunctionListEntry delayFuncs[] = {
                    JS_CGETSET_MAGIC_DEF("time", js_delay_get, js_delay_set, DL_Time),
                    JS_CGETSET_MAGIC_DEF("feedback", js_delay_get, js_delay_set, DL_Feedback),
                    JS_CGETSET_MAGIC_DEF("wet", js_delay_get, js_delay_set, DL_Wet),
                    JS_CGETSET_DEF("enabled", js_effect_enabled_get, js_effect_enabled_set),
                    JS_CFUNC_DEF("bypass", 0, js_effect_bypass),
                    JS_CFUNC_DEF("enable", 0, js_effect_enable),
                };
                JS_SetPropertyFunctionList(ctx, delayProto, delayFuncs,
                                           sizeof(delayFuncs) / sizeof(JSCFunctionListEntry));
                JSValue delayCtor = JS_NewCFunction2(ctx, js_delay_ctor, "Delay", 3, JS_CFUNC_constructor, 0);
                JS_SetConstructor(ctx, delayCtor, delayProto);
                JS_SetClassProto(ctx, s_delay_class_id, delayProto);

                /* Distortion prototype. */
                JSValue distortionProto = JS_NewObject(ctx);
                static const JSCFunctionListEntry distortionFuncs[] = {
                    JS_CGETSET_MAGIC_DEF("drive", js_distortion_get, js_distortion_set, DS_Drive),
                    JS_CGETSET_MAGIC_DEF("tone", js_distortion_get, js_distortion_set, DS_Tone),
                    JS_CGETSET_DEF("enabled", js_effect_enabled_get, js_effect_enabled_set),
                    JS_CFUNC_DEF("bypass", 0, js_effect_bypass),
                    JS_CFUNC_DEF("enable", 0, js_effect_enable),
                };
                JS_SetPropertyFunctionList(ctx, distortionProto, distortionFuncs,
                                           sizeof(distortionFuncs) / sizeof(JSCFunctionListEntry));
                JSValue distortionCtor = JS_NewCFunction2(ctx, js_distortion_ctor, "Distortion", 2, JS_CFUNC_constructor, 0);
                JS_SetConstructor(ctx, distortionCtor, distortionProto);
                JS_SetClassProto(ctx, s_distortion_class_id, distortionProto);

                /* Reverb prototype. */
                JSValue reverbProto = JS_NewObject(ctx);
                static const JSCFunctionListEntry reverbFuncs[] = {
                    JS_CGETSET_MAGIC_DEF("decay", js_reverb_get, js_reverb_set, RV_Decay),
                    JS_CGETSET_MAGIC_DEF("wet", js_reverb_get, js_reverb_set, RV_Wet),
                    JS_CGETSET_DEF("enabled", js_effect_enabled_get, js_effect_enabled_set),
                    JS_CFUNC_DEF("bypass", 0, js_effect_bypass),
                    JS_CFUNC_DEF("enable", 0, js_effect_enable),
                };
                JS_SetPropertyFunctionList(ctx, reverbProto, reverbFuncs,
                                           sizeof(reverbFuncs) / sizeof(JSCFunctionListEntry));
                JSValue reverbCtor = JS_NewCFunction2(ctx, js_reverb_ctor, "Reverb", 2, JS_CFUNC_constructor, 0);
                JS_SetConstructor(ctx, reverbCtor, reverbProto);
                JS_SetClassProto(ctx, s_reverb_class_id, reverbProto);

                /* Custom effect prototype. Bypass/enable/enabled are
                 * shared across every registered effect; the parameter
                 * getters/setters live on each instance because the
                 * names differ per descriptor. */
                JSValue customEffectProto = JS_NewObject(ctx);
                static const JSCFunctionListEntry customEffectFuncs[] = {
                    JS_CGETSET_DEF("enabled", js_effect_enabled_get, js_effect_enabled_set),
                    JS_CFUNC_DEF("bypass", 0, js_effect_bypass),
                    JS_CFUNC_DEF("enable", 0, js_effect_enable),
                };
                JS_SetPropertyFunctionList(ctx, customEffectProto, customEffectFuncs,
                                           sizeof(customEffectFuncs) / sizeof(JSCFunctionListEntry));
                JS_SetClassProto(ctx, s_custom_effect_class_id, customEffectProto);

                /* Default export — mixer interface. */
                JSValue defaultExport = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, defaultExport, "createChannel",
                                  JS_NewCFunction(ctx, js_create_channel, "createChannel", 3));
                JS_SetPropertyStr(ctx, defaultExport, "channel",
                                  JS_NewCFunction(ctx, js_get_channel, "channel", 1));
                JS_SetPropertyStr(ctx, defaultExport, "master",
                                  JS_NewCFunction(ctx, js_master, "master", 0));
                JS_SetPropertyStr(ctx, defaultExport, "createEffect",
                                  JS_NewCFunction(ctx, js_create_effect, "createEffect", 2));
                JS_SetPropertyStr(ctx, defaultExport, "listEffects",
                                  JS_NewCFunction(ctx, js_list_effects, "listEffects", 0));

                JS_SetModuleExport(ctx, m, "default", defaultExport);
                JS_SetModuleExport(ctx, m, "Master", JS_NewString(ctx, "Master"));
                JS_SetModuleExport(ctx, m, "Channel", JS_DupValue(ctx, channelProto));
                JS_SetModuleExport(ctx, m, "Gain", gainCtor);
                JS_SetModuleExport(ctx, m, "HighPass", highpassCtor);
                JS_SetModuleExport(ctx, m, "LowPass", lowpassCtor);
                JS_SetModuleExport(ctx, m, "Compressor", compressorCtor);
                JS_SetModuleExport(ctx, m, "Delay", delayCtor);
                JS_SetModuleExport(ctx, m, "Distortion", distortionCtor);
                JS_SetModuleExport(ctx, m, "Reverb", reverbCtor);

                return 0;
            }

            /* JS: audio.createChannel(name, volume?, muted?) */
            JSValue Audio::js_create_channel(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 1)
                    return JS_UNDEFINED;

                const char *name = JS_ToCString(ctx, argv[0]);
                if (!name)
                    return JS_EXCEPTION;

                float volume = 1.0f;
                bool muted = false;
                if (argc >= 2)
                    volume = read_float(ctx, argv[1], 1.0f);
                if (argc >= 3)
                    muted = JS_ToBool(ctx, argv[2]);

                Bokken::Audio::Channel *ch =
                    Bokken::Audio::Mixer::get().createChannel(name, volume, muted);
                JS_FreeCString(ctx, name);

                return wrap_channel(ctx, ch);
            }

            /* JS: audio.channel(name) */
            JSValue Audio::js_get_channel(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 1)
                    return JS_UNDEFINED;

                const char *name = JS_ToCString(ctx, argv[0]);
                if (!name)
                    return JS_EXCEPTION;

                Bokken::Audio::Channel *ch = Bokken::Audio::Mixer::get().channel(name);
                JS_FreeCString(ctx, name);

                return ch ? wrap_channel(ctx, ch) : JS_NULL;
            }

            /* JS: audio.master() */
            JSValue Audio::js_master(JSContext *ctx, JSValueConst, int, JSValueConst *)
            {
                return wrap_channel(ctx, Bokken::Audio::Mixer::get().master());
            }

            /* Channel property getter — handles volume / muted / name. */
            JSValue Audio::js_channel_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *ch = static_cast<Bokken::Audio::Channel *>(JS_GetOpaque(this_val, s_channel_class_id));
                if (!ch)
                    return JS_UNDEFINED;

                switch (magic)
                {
                case CH_Volume:
                    return JS_NewFloat64(ctx, ch->volume());
                case CH_Muted:
                    return JS_NewBool(ctx, ch->muted());
                case CH_Name:
                    return JS_NewString(ctx, ch->name.c_str());
                }
                return JS_UNDEFINED;
            }

            /* JS: channel.setVolume(v) — also accepts via the volume property. */
            JSValue Audio::js_channel_set_volume(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *ch = static_cast<Bokken::Audio::Channel *>(JS_GetOpaque(this_val, s_channel_class_id));
                if (!ch || argc < 1)
                    return JS_UNDEFINED;
                ch->setVolume(read_float(ctx, argv[0], ch->volume()));
                return JS_DupValue(ctx, this_val);
            }

            /* JS: channel.setMuted(bool) */
            JSValue Audio::js_channel_set_muted(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *ch = static_cast<Bokken::Audio::Channel *>(JS_GetOpaque(this_val, s_channel_class_id));
                if (!ch || argc < 1)
                    return JS_UNDEFINED;
                ch->setMuted(JS_ToBool(ctx, argv[0]));
                return JS_DupValue(ctx, this_val);
            }

            /* JS: channel.addEffect(effect) — transfers ownership to the channel. */
            JSValue Audio::js_channel_add_effect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *ch = static_cast<Bokken::Audio::Channel *>(JS_GetOpaque(this_val, s_channel_class_id));
                if (!ch || argc < 1)
                    return JS_UNDEFINED;

                EffectHandle *handle = get_effect_handle(argv[0]);
                if (!handle || !handle->effect)
                {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[Audio] addEffect: argument is not a valid effect.");
                    return JS_UNDEFINED;
                }
                if (handle->attached)
                {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[Audio] addEffect: effect is already attached to a channel.");
                    return JS_UNDEFINED;
                }

                ch->addEffect(handle->effect);
                handle->attached = true;
                return JS_DupValue(ctx, this_val);
            }

            /* JS: channel.removeEffect(effect) — detaches and frees the effect. */
            JSValue Audio::js_channel_remove_effect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *ch = static_cast<Bokken::Audio::Channel *>(JS_GetOpaque(this_val, s_channel_class_id));
                if (!ch || argc < 1)
                    return JS_UNDEFINED;

                EffectHandle *handle = get_effect_handle(argv[0]);
                if (!handle || !handle->effect)
                    return JS_UNDEFINED;

                ch->removeEffect(handle->effect);

                /* Channel::removeEffect erased the unique_ptr that owned the
                 * effect, so the raw pointer is now dangling. Mark the handle
                 * detached and clear it so neither side touches it again. */
                handle->effect = nullptr;
                handle->attached = false;
                return JS_DupValue(ctx, this_val);
            }

            /* JS: channel.getEffects() — returns an array of attached effects.
             *
             * Note: the returned values are non-owning views into channel-owned
             * effects. They share the same EffectHandle marker pattern as user-
             * created effects, but with `attached=true` so the finalizer no-ops. */
            JSValue Audio::js_channel_get_effects(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *ch = static_cast<Bokken::Audio::Channel *>(JS_GetOpaque(this_val, s_channel_class_id));
                if (!ch)
                    return JS_UNDEFINED;

                /* The Channel API only returns generic Effects::Base*, so we
                 * don't know the concrete subclass at runtime without RTTI.
                 * Use dynamic_cast to recover the type for each entry. */
                auto effects = ch->getEffects();
                JSValue arr = JS_NewArray(ctx);
                for (size_t i = 0; i < effects.size(); ++i)
                {
                    JSClassID classId = 0;
                    if (dynamic_cast<Bokken::Audio::Effects::Gain *>(effects[i]))
                        classId = s_gain_class_id;
                    else if (dynamic_cast<Bokken::Audio::Effects::HighPass *>(effects[i]))
                        classId = s_highpass_class_id;
                    else if (dynamic_cast<Bokken::Audio::Effects::LowPass *>(effects[i]))
                        classId = s_lowpass_class_id;
                    else if (dynamic_cast<Bokken::Audio::Effects::Compressor *>(effects[i]))
                        classId = s_compressor_class_id;
                    else if (dynamic_cast<Bokken::Audio::Effects::Delay *>(effects[i]))
                        classId = s_delay_class_id;
                    else if (dynamic_cast<Bokken::Audio::Effects::Distortion *>(effects[i]))
                        classId = s_distortion_class_id;
                    else if (dynamic_cast<Bokken::Audio::Effects::Reverb *>(effects[i]))
                        classId = s_reverb_class_id;

                    if (classId == 0)
                        continue;

                    JSValue obj = JS_NewObjectClass(ctx, classId);
                    auto *handle = new EffectHandle{effects[i], true};
                    JS_SetOpaque(obj, handle);
                    JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i), obj);
                }
                return arr;
            }

            /* JS: new Gain(gain?) */
            JSValue Audio::js_gain_ctor(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                float gain = 1.0f;
                if (argc >= 1)
                    gain = read_float(ctx, argv[0], 1.0f);
                return wrap_effect(ctx, s_gain_class_id, new Bokken::Audio::Effects::Gain(gain));
            }

            /* JS: new HighPass(cutoff?, resonance?) */
            JSValue Audio::js_highpass_ctor(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                float cutoff = 800.0f, resonance = 0.707f;
                if (argc >= 1)
                    cutoff = read_float(ctx, argv[0], cutoff);
                if (argc >= 2)
                    resonance = read_float(ctx, argv[1], resonance);
                return wrap_effect(ctx, s_highpass_class_id, new Bokken::Audio::Effects::HighPass(cutoff, resonance));
            }

            /* JS: new LowPass(cutoff?, resonance?) */
            JSValue Audio::js_lowpass_ctor(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                float cutoff = 8000.0f, resonance = 0.707f;
                if (argc >= 1)
                    cutoff = read_float(ctx, argv[0], cutoff);
                if (argc >= 2)
                    resonance = read_float(ctx, argv[1], resonance);
                return wrap_effect(ctx, s_lowpass_class_id, new Bokken::Audio::Effects::LowPass(cutoff, resonance));
            }

            /* JS: new Compressor(threshold?, ratio?, attack?, release?) */
            JSValue Audio::js_compressor_ctor(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                float threshold = -12.0f, ratio = 4.0f, attack = 0.003f, release = 0.25f;
                if (argc >= 1)
                    threshold = read_float(ctx, argv[0], threshold);
                if (argc >= 2)
                    ratio = read_float(ctx, argv[1], ratio);
                if (argc >= 3)
                    attack = read_float(ctx, argv[2], attack);
                if (argc >= 4)
                    release = read_float(ctx, argv[3], release);
                return wrap_effect(ctx, s_compressor_class_id,
                                   new Bokken::Audio::Effects::Compressor(threshold, ratio, attack, release));
            }

            /* JS: new Delay(time?, feedback?, wet?) */
            JSValue Audio::js_delay_ctor(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                float time = 0.3f, feedback = 0.4f, wet = 0.35f;
                if (argc >= 1)
                    time = read_float(ctx, argv[0], time);
                if (argc >= 2)
                    feedback = read_float(ctx, argv[1], feedback);
                if (argc >= 3)
                    wet = read_float(ctx, argv[2], wet);
                return wrap_effect(ctx, s_delay_class_id, new Bokken::Audio::Effects::Delay(time, feedback, wet));
            }

            /* JS: new Distortion(drive?, tone?) */
            JSValue Audio::js_distortion_ctor(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                float drive = 0.5f, tone = 0.5f;
                if (argc >= 1)
                    drive = read_float(ctx, argv[0], drive);
                if (argc >= 2)
                    tone = read_float(ctx, argv[1], tone);
                return wrap_effect(ctx, s_distortion_class_id, new Bokken::Audio::Effects::Distortion(drive, tone));
            }

            /* JS: new Reverb(decay?, wet?) */
            JSValue Audio::js_reverb_ctor(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                float decay = 1.5f, wet = 0.3f;
                if (argc >= 1)
                    decay = read_float(ctx, argv[0], decay);
                if (argc >= 2)
                    wet = read_float(ctx, argv[1], wet);
                return wrap_effect(ctx, s_reverb_class_id, new Bokken::Audio::Effects::Reverb(decay, wet));
            }

            /* JS: effect.bypass() — sets enabled=false and returns the effect. */
            JSValue Audio::js_effect_bypass(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *fx = get_effect_for_any(this_val);
                if (fx)
                    fx->bypass();
                return JS_DupValue(ctx, this_val);
            }

            /* JS: effect.enable() — sets enabled=true and returns the effect. */
            JSValue Audio::js_effect_enable(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *fx = get_effect_for_any(this_val);
                if (fx)
                    fx->enable();
                return JS_DupValue(ctx, this_val);
            }

            /* Generic enabled property getter/setter — works for all effects. */
            JSValue Audio::js_effect_enabled_get(JSContext *ctx, JSValueConst this_val)
            {
                auto *fx = get_effect_for_any(this_val);
                return fx ? JS_NewBool(ctx, fx->enabled) : JS_UNDEFINED;
            }

            JSValue Audio::js_effect_enabled_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
            {
                auto *fx = get_effect_for_any(this_val);
                if (fx)
                    fx->enabled = JS_ToBool(ctx, val);
                return JS_UNDEFINED;
            }

            /* Per-effect property accessors. Each pair handles a small fixed set
             * of fields via the magic value, keeping the binding tables compact. */

            JSValue Audio::js_gain_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *handle = static_cast<EffectHandle *>(JS_GetOpaque(this_val, s_gain_class_id));
                if (!handle || !handle->effect)
                    return JS_UNDEFINED;
                auto *fx = static_cast<Bokken::Audio::Effects::Gain *>(handle->effect);
                if (magic == GN_Gain)
                    return JS_NewFloat64(ctx, fx->gain);
                return JS_UNDEFINED;
            }

            JSValue Audio::js_gain_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *handle = static_cast<EffectHandle *>(JS_GetOpaque(this_val, s_gain_class_id));
                if (!handle || !handle->effect)
                    return JS_UNDEFINED;
                auto *fx = static_cast<Bokken::Audio::Effects::Gain *>(handle->effect);
                if (magic == GN_Gain)
                    fx->gain = read_float(ctx, val, fx->gain);
                return JS_UNDEFINED;
            }

            JSValue Audio::js_highpass_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *handle = static_cast<EffectHandle *>(JS_GetOpaque(this_val, s_highpass_class_id));
                if (!handle || !handle->effect)
                    return JS_UNDEFINED;
                auto *fx = static_cast<Bokken::Audio::Effects::HighPass *>(handle->effect);
                switch (magic)
                {
                case HP_Cutoff:
                    return JS_NewFloat64(ctx, fx->cutoff);
                case HP_Resonance:
                    return JS_NewFloat64(ctx, fx->resonance);
                }
                return JS_UNDEFINED;
            }

            JSValue Audio::js_highpass_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *handle = static_cast<EffectHandle *>(JS_GetOpaque(this_val, s_highpass_class_id));
                if (!handle || !handle->effect)
                    return JS_UNDEFINED;
                auto *fx = static_cast<Bokken::Audio::Effects::HighPass *>(handle->effect);
                switch (magic)
                {
                case HP_Cutoff:
                    fx->cutoff = read_float(ctx, val, fx->cutoff);
                    break;
                case HP_Resonance:
                    fx->resonance = read_float(ctx, val, fx->resonance);
                    break;
                }
                return JS_UNDEFINED;
            }

            JSValue Audio::js_lowpass_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *handle = static_cast<EffectHandle *>(JS_GetOpaque(this_val, s_lowpass_class_id));
                if (!handle || !handle->effect)
                    return JS_UNDEFINED;
                auto *fx = static_cast<Bokken::Audio::Effects::LowPass *>(handle->effect);
                switch (magic)
                {
                case LP_Cutoff:
                    return JS_NewFloat64(ctx, fx->cutoff);
                case LP_Resonance:
                    return JS_NewFloat64(ctx, fx->resonance);
                }
                return JS_UNDEFINED;
            }

            JSValue Audio::js_lowpass_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *handle = static_cast<EffectHandle *>(JS_GetOpaque(this_val, s_lowpass_class_id));
                if (!handle || !handle->effect)
                    return JS_UNDEFINED;
                auto *fx = static_cast<Bokken::Audio::Effects::LowPass *>(handle->effect);
                switch (magic)
                {
                case LP_Cutoff:
                    fx->cutoff = read_float(ctx, val, fx->cutoff);
                    break;
                case LP_Resonance:
                    fx->resonance = read_float(ctx, val, fx->resonance);
                    break;
                }
                return JS_UNDEFINED;
            }

            JSValue Audio::js_compressor_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *handle = static_cast<EffectHandle *>(JS_GetOpaque(this_val, s_compressor_class_id));
                if (!handle || !handle->effect)
                    return JS_UNDEFINED;
                auto *fx = static_cast<Bokken::Audio::Effects::Compressor *>(handle->effect);
                switch (magic)
                {
                case CP_Threshold:
                    return JS_NewFloat64(ctx, fx->threshold);
                case CP_Ratio:
                    return JS_NewFloat64(ctx, fx->ratio);
                case CP_Attack:
                    return JS_NewFloat64(ctx, fx->attack);
                case CP_Release:
                    return JS_NewFloat64(ctx, fx->release);
                }
                return JS_UNDEFINED;
            }

            JSValue Audio::js_compressor_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *handle = static_cast<EffectHandle *>(JS_GetOpaque(this_val, s_compressor_class_id));
                if (!handle || !handle->effect)
                    return JS_UNDEFINED;
                auto *fx = static_cast<Bokken::Audio::Effects::Compressor *>(handle->effect);
                switch (magic)
                {
                case CP_Threshold:
                    fx->threshold = read_float(ctx, val, fx->threshold);
                    break;
                case CP_Ratio:
                    fx->ratio = read_float(ctx, val, fx->ratio);
                    break;
                case CP_Attack:
                    fx->attack = read_float(ctx, val, fx->attack);
                    break;
                case CP_Release:
                    fx->release = read_float(ctx, val, fx->release);
                    break;
                }
                return JS_UNDEFINED;
            }

            JSValue Audio::js_delay_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *handle = static_cast<EffectHandle *>(JS_GetOpaque(this_val, s_delay_class_id));
                if (!handle || !handle->effect)
                    return JS_UNDEFINED;
                auto *fx = static_cast<Bokken::Audio::Effects::Delay *>(handle->effect);
                switch (magic)
                {
                case DL_Time:
                    return JS_NewFloat64(ctx, fx->time);
                case DL_Feedback:
                    return JS_NewFloat64(ctx, fx->feedback);
                case DL_Wet:
                    return JS_NewFloat64(ctx, fx->wet);
                }
                return JS_UNDEFINED;
            }

            JSValue Audio::js_delay_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *handle = static_cast<EffectHandle *>(JS_GetOpaque(this_val, s_delay_class_id));
                if (!handle || !handle->effect)
                    return JS_UNDEFINED;
                auto *fx = static_cast<Bokken::Audio::Effects::Delay *>(handle->effect);
                switch (magic)
                {
                case DL_Time:
                    fx->time = read_float(ctx, val, fx->time);
                    break;
                case DL_Feedback:
                    fx->feedback = read_float(ctx, val, fx->feedback);
                    break;
                case DL_Wet:
                    fx->wet = read_float(ctx, val, fx->wet);
                    break;
                }
                return JS_UNDEFINED;
            }

            JSValue Audio::js_distortion_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *handle = static_cast<EffectHandle *>(JS_GetOpaque(this_val, s_distortion_class_id));
                if (!handle || !handle->effect)
                    return JS_UNDEFINED;
                auto *fx = static_cast<Bokken::Audio::Effects::Distortion *>(handle->effect);
                switch (magic)
                {
                case DS_Drive:
                    return JS_NewFloat64(ctx, fx->drive);
                case DS_Tone:
                    return JS_NewFloat64(ctx, fx->tone);
                }
                return JS_UNDEFINED;
            }

            JSValue Audio::js_distortion_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *handle = static_cast<EffectHandle *>(JS_GetOpaque(this_val, s_distortion_class_id));
                if (!handle || !handle->effect)
                    return JS_UNDEFINED;
                auto *fx = static_cast<Bokken::Audio::Effects::Distortion *>(handle->effect);
                switch (magic)
                {
                case DS_Drive:
                    fx->drive = read_float(ctx, val, fx->drive);
                    break;
                case DS_Tone:
                    fx->tone = read_float(ctx, val, fx->tone);
                    break;
                }
                return JS_UNDEFINED;
            }

            JSValue Audio::js_reverb_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *handle = static_cast<EffectHandle *>(JS_GetOpaque(this_val, s_reverb_class_id));
                if (!handle || !handle->effect)
                    return JS_UNDEFINED;
                auto *fx = static_cast<Bokken::Audio::Effects::Reverb *>(handle->effect);
                switch (magic)
                {
                case RV_Decay:
                    return JS_NewFloat64(ctx, fx->decay);
                case RV_Wet:
                    return JS_NewFloat64(ctx, fx->wet);
                }
                return JS_UNDEFINED;
            }

            JSValue Audio::js_reverb_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *handle = static_cast<EffectHandle *>(JS_GetOpaque(this_val, s_reverb_class_id));
                if (!handle || !handle->effect)
                    return JS_UNDEFINED;
                auto *fx = static_cast<Bokken::Audio::Effects::Reverb *>(handle->effect);
                switch (magic)
                {
                case RV_Decay:
                    fx->decay = read_float(ctx, val, fx->decay);
                    break;
                case RV_Wet:
                    fx->wet = read_float(ctx, val, fx->wet);
                    break;
                }
                return JS_UNDEFINED;
            }

            /* Wrap a non-owning Channel pointer in a JS handle. */
            JSValue Audio::wrap_channel(JSContext *ctx, Bokken::Audio::Channel *channel)
            {
                JSValue obj = JS_NewObjectClass(ctx, s_channel_class_id);
                if (JS_IsException(obj))
                    return obj;
                JS_SetOpaque(obj, channel);
                return obj;
            }

            /* Wrap a freshly-allocated effect in a JS handle that owns it.
             * Ownership transfers to the channel on addEffect(). */
            JSValue Audio::wrap_effect(JSContext *ctx, JSClassID classId, Bokken::Audio::Effects::Base *effect)
            {
                JSValue obj = JS_NewObjectClass(ctx, classId);
                if (JS_IsException(obj))
                {
                    delete effect;
                    return obj;
                }
                auto *handle = new EffectHandle{effect, false};
                JS_SetOpaque(obj, handle);
                return obj;
            }

            /* Recover the EffectHandle from any effect class id. */
            Audio::EffectHandle *Audio::get_effect_handle(JSValueConst val)
            {
                /* Walk every known effect class id — at most one will match. */
                static const JSClassID ids[] = {
                    s_gain_class_id, s_highpass_class_id, s_lowpass_class_id,
                    s_compressor_class_id, s_delay_class_id, s_distortion_class_id,
                    s_reverb_class_id, s_custom_effect_class_id};
                for (JSClassID id : ids)
                {
                    void *p = JS_GetOpaque(val, id);
                    if (p)
                        return static_cast<EffectHandle *>(p);
                }
                return nullptr;
            }

            /* Convenience — recover the underlying Effects::Base pointer. */
            Bokken::Audio::Effects::Base *Audio::get_effect_for_any(JSValueConst val)
            {
                EffectHandle *handle = get_effect_handle(val);
                return handle ? handle->effect : nullptr;
            }

            /* Effect finalizer — frees the handle, and the effect itself only
             * when the handle still owns it (not yet attached to a channel). */
            void Audio::effect_finalizer(JSRuntime *, JSValue val)
            {
                /* Try every effect class id to recover the opaque payload. The
                 * runtime invokes us with the correct one, but JS_GetOpaque
                 * needs the matching id, so a small loop is fine. */
                static const JSClassID ids[] = {
                    s_gain_class_id, s_highpass_class_id, s_lowpass_class_id,
                    s_compressor_class_id, s_delay_class_id, s_distortion_class_id,
                    s_reverb_class_id, s_custom_effect_class_id};
                for (JSClassID id : ids)
                {
                    void *p = JS_GetOpaque(val, id);
                    if (!p)
                        continue;
                    auto *handle = static_cast<EffectHandle *>(p);
                    if (!handle->attached && handle->effect)
                        delete handle->effect;
                    delete handle;
                    return;
                }
            }

            /* JS: audio.listEffects() → string[]
             *
             * Returns the names of every C++-registered custom effect.
             * Useful for tooling, debug overlays, and "what's available"
             * introspection from scripts. */
            JSValue Audio::js_list_effects(JSContext *ctx, JSValueConst, int, JSValueConst *)
            {
                auto names = Bokken::Audio::Effects::registeredEffects();
                JSValue arr = JS_NewArray(ctx);
                for (size_t i = 0; i < names.size(); i++)
                {
                    JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i),
                                         JS_NewString(ctx, names[i].c_str()));
                }
                return arr;
            }

            /* JS: audio.createEffect(name, initialParams?) → Effect | null
             *
             * Looks up the registered descriptor by name, asks the factory
             * for a fresh instance, wraps it in an EffectHandle owned by
             * the JS handle, and installs per-parameter getter/setter
             * pairs as own properties so `effect.<paramName>` reads and
             * writes the underlying std::atomic<float>.
             *
             * Returns JS_NULL when the name is unknown so callers can
             * branch with `if (effect) { ... }` rather than catching an
             * exception. */
            JSValue Audio::js_create_effect(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 1)
                    return JS_NULL;

                const char *name = JS_ToCString(ctx, argv[0]);
                if (!name)
                    return JS_EXCEPTION;

                const Bokken::Audio::Effects::EffectDescriptor *desc =
                    Bokken::Audio::Effects::findEffect(name);
                if (!desc)
                {
                    SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
                                "[audio.createEffect] no registered effect named '%s'",
                                name);
                    JS_FreeCString(ctx, name);
                    return JS_NULL;
                }
                JS_FreeCString(ctx, name);

                auto effect = desc->factory();
                if (!effect)
                    return JS_NULL;

                JSValue obj = JS_NewObjectClass(ctx, s_custom_effect_class_id);
                if (JS_IsException(obj))
                    return obj;

                /* Hand ownership of the C++ effect to the handle. The
                 * release() here is paired with delete in the finalizer
                 * (when still owned) or the channel taking it over via
                 * addEffect(). */
                auto *handle = new EffectHandle{effect.release(), false, desc};
                JS_SetOpaque(obj, handle);

                /* Per-parameter getter/setter installation. Each pair
                 * is a generic-magic JS function whose magic int is
                 * the parameter index — the dispatcher pulls the param
                 * descriptor from handle->descriptor and goes through
                 * its accessor to read/write the underlying atomic.
                 *
                 * Important: getters/setters installed via
                 * JS_DefinePropertyGetSet are invoked through the
                 * normal function-call path, so we use the
                 * generic_magic cproto. The dedicated getter_magic /
                 * setter_magic cprotos exist only for the CGETSET
                 * static-table path and abort if called any other way. */
                for (size_t i = 0; i < desc->params.size(); i++)
                {
                    const auto &param = desc->params[i];
                    JSAtom atom = JS_NewAtom(ctx, param.name.c_str());

                    JSValue getter = JS_NewCFunctionMagic(
                        ctx, js_custom_effect_get,
                        param.name.c_str(), 0,
                        JS_CFUNC_generic_magic, static_cast<int>(i));
                    JSValue setter = JS_NewCFunctionMagic(
                        ctx, js_custom_effect_set,
                        param.name.c_str(), 1,
                        JS_CFUNC_generic_magic, static_cast<int>(i));

                    JS_DefinePropertyGetSet(ctx, obj, atom, getter, setter,
                                            JS_PROP_C_W_E);
                    JS_FreeAtom(ctx, atom);
                }

                /* Apply initial parameter values from the optional
                 * config object. Any unknown keys are silently ignored
                 * — matches how addComponent(config) behaves elsewhere
                 * in the engine. */
                if (argc >= 2 && JS_IsObject(argv[1]))
                {
                    for (size_t i = 0; i < desc->params.size(); i++)
                    {
                        const auto &param = desc->params[i];
                        JSValue v = JS_GetPropertyStr(ctx, argv[1], param.name.c_str());
                        if (!JS_IsUndefined(v) && !JS_IsNull(v))
                        {
                            double d = 0.0;
                            if (JS_ToFloat64(ctx, &d, v) == 0)
                            {
                                param.accessor(handle->effect)
                                    .store(static_cast<float>(d),
                                           std::memory_order_relaxed);
                            }
                        }
                        JS_FreeValue(ctx, v);
                    }
                }
                else
                {
                    /* No config object — apply each param's default so
                     * the JS-visible value reads back consistently with
                     * what process() will see on the audio thread. The
                     * effect's C++ constructor may have set its own
                     * defaults too; we override here to keep the
                     * descriptor as the single source of truth. */
                    for (const auto &param : desc->params)
                    {
                        param.accessor(handle->effect)
                            .store(param.defaultValue,
                                   std::memory_order_relaxed);
                    }
                }

                return obj;
            }

            /* Custom-effect parameter getter — invoked via the standard
             * function-call path by JS_DefinePropertyGetSet, so it has
             * to use the generic_magic signature (ctx, this, argc, argv,
             * magic). The getter/setter cprotos abort when called this
             * way; only the CGETSET property-list path can use them.
             *
             * Magic int = parameter index in the descriptor's params. */
            JSValue Audio::js_custom_effect_get(JSContext *ctx, JSValueConst this_val,
                                                int /*argc*/, JSValueConst * /*argv*/, int magic)
            {
                auto *handle = static_cast<EffectHandle *>(
                    JS_GetOpaque(this_val, s_custom_effect_class_id));
                if (!handle || !handle->descriptor || !handle->effect)
                    return JS_UNDEFINED;
                if (magic < 0 ||
                    static_cast<size_t>(magic) >= handle->descriptor->params.size())
                    return JS_UNDEFINED;

                const auto &param = handle->descriptor->params[magic];
                float v = param.accessor(handle->effect)
                              .load(std::memory_order_relaxed);
                return JS_NewFloat64(ctx, v);
            }

            /* Custom-effect parameter setter — generic_magic signature.
             * argv[0] is the value being assigned. */
            JSValue Audio::js_custom_effect_set(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv, int magic)
            {
                auto *handle = static_cast<EffectHandle *>(
                    JS_GetOpaque(this_val, s_custom_effect_class_id));
                if (!handle || !handle->descriptor || !handle->effect)
                    return JS_UNDEFINED;
                if (magic < 0 ||
                    static_cast<size_t>(magic) >= handle->descriptor->params.size())
                    return JS_UNDEFINED;
                if (argc < 1)
                    return JS_UNDEFINED;

                double d = 0.0;
                if (JS_ToFloat64(ctx, &d, argv[0]) < 0)
                    return JS_EXCEPTION;

                const auto &param = handle->descriptor->params[magic];
                param.accessor(handle->effect)
                    .store(static_cast<float>(d), std::memory_order_relaxed);
                return JS_UNDEFINED;
            }
        }
    }
}