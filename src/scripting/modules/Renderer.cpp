#include "Renderer.hpp"

namespace Bokken
{
    namespace Scripting
    {
        namespace Modules
        {
            namespace
            {
                // Read a numeric property off a JS object, leaving `out` unchanged
                // if the prop is missing/non-numeric. Returns true if it wrote.
                bool readNumber(JSContext *ctx, JSValueConst obj, const char *name, float &out)
                {
                    if (!JS_IsObject(obj))
                        return false;
                    JSValue v = JS_GetPropertyStr(ctx, obj, name);
                    bool got = false;
                    if (JS_IsNumber(v))
                    {
                        double d = 0;
                        if (JS_ToFloat64(ctx, &d, v) == 0)
                        {
                            out = (float)d;
                            got = true;
                        }
                    }
                    JS_FreeValue(ctx, v);
                    return got;
                }

                bool readBool(JSContext *ctx, JSValueConst obj, const char *name, bool &out)
                {
                    if (!JS_IsObject(obj))
                        return false;
                    JSValue v = JS_GetPropertyStr(ctx, obj, name);
                    bool got = false;
                    if (JS_IsBool(v))
                    {
                        out = (JS_ToBool(ctx, v) != 0);
                        got = true;
                    }
                    JS_FreeValue(ctx, v);
                    return got;
                }

                // Read a packed 0xRRGGBBAA integer property and decode
                // it into the R/G/B components of `out`. The alpha byte
                // is consumed and discarded — matches the convention
                // used by Mesh2D.color (which has alpha) for pipeline
                // tunables that only need RGB. Numeric out-of-range
                // values are masked to the low 32 bits by JS_ToUint32.
                bool readPackedColor(JSContext *ctx, JSValueConst obj, const char *name, glm::vec3 &out)
                {
                    if (!JS_IsObject(obj))
                        return false;
                    JSValue v = JS_GetPropertyStr(ctx, obj, name);
                    bool got = false;
                    if (JS_IsNumber(v))
                    {
                        uint32_t c = 0;
                        if (JS_ToUint32(ctx, &c, v) == 0)
                        {
                            out.r = ((c >> 24) & 0xFFu) / 255.0f;
                            out.g = ((c >> 16) & 0xFFu) / 255.0f;
                            out.b = ((c >> 8) & 0xFFu) / 255.0f;
                            got = true;
                        }
                    }
                    JS_FreeValue(ctx, v);
                    return got;
                }

                // Apply property bag to a known stage by walking its tunables.
                // Unknown properties are silently ignored — stage shape is the
                // contract the JS user reads from the docs.
                void applyProps(JSContext *ctx, Bokken::Renderer::Stage *st, JSValueConst props)
                {
                    if (!st || !JS_IsObject(props))
                        return;

                    bool en = st->enabled;
                    if (readBool(ctx, props, "enabled", en))
                        st->enabled = en;

                    // Stage-specific fields. We use dynamic_cast — these are only
                    // a handful of types and the JS-facing layer is the one place
                    // where dispatch by concrete type is acceptable.
                    if (auto *bs = dynamic_cast<Bokken::Renderer::BloomStage *>(st))
                    {
                        readNumber(ctx, props, "threshold", bs->threshold);
                        readNumber(ctx, props, "intensity", bs->intensity);
                        readNumber(ctx, props, "radius", bs->radius);
                    }
                    if (auto *cg = dynamic_cast<Bokken::Renderer::ColorGradeStage *>(st))
                    {
                        readNumber(ctx, props, "exposure", cg->exposure);
                        readNumber(ctx, props, "saturation", cg->saturation);
                        readNumber(ctx, props, "gamma", cg->gamma);
                    }
                    if (auto *ss = dynamic_cast<Bokken::Renderer::SpriteStage *>(st))
                    {
                        readNumber(ctx, props, "clearR", ss->clearR);
                        readNumber(ctx, props, "clearG", ss->clearG);
                        readNumber(ctx, props, "clearB", ss->clearB);
                        readNumber(ctx, props, "clearA", ss->clearA);
                    }
                    if (auto *ds = dynamic_cast<Bokken::Renderer::DistortionStage *>(st))
                    {
                        readNumber(ctx, props, "intensity", ds->intensity);
                        readNumber(ctx, props, "heatHazeSpeed", ds->heatHazeSpeed);
                        readNumber(ctx, props, "heatHazeFrequency", ds->heatHazeFrequency);
                        readNumber(ctx, props, "heatHazeAmplitude", ds->heatHazeAmplitude);

                        bool haze = ds->heatHaze;
                        if (readBool(ctx, props, "heatHaze", haze))
                            ds->heatHaze = haze;
                    }
                    if (auto *lp = dynamic_cast<Bokken::Renderer::LightingPass *>(st))
                    {
                        readNumber(ctx, props, "intensityScale", lp->intensityScale);
                        readNumber(ctx, props, "wrapAmount", lp->wrapAmount);
                        // Two authoring forms for ambient. The packed
                        // `ambient: 0xRRGGBBAA` form matches Mesh2D.color
                        // and Light2D.color, so a single conventional
                        // colour pattern works everywhere. The flat
                        // `ambientR/G/B` form is kept for backward
                        // compatibility and for HDR authoring where
                        // values >1 are needed (overbright skies,
                        // cave glows) — packed bytes can't represent
                        // HDR. When both are present, the packed form
                        // is applied first so flat overrides win.
                        readPackedColor(ctx, props, "ambient", lp->ambient);
                        readNumber(ctx, props, "ambientR", lp->ambient.r);
                        readNumber(ctx, props, "ambientG", lp->ambient.g);
                        readNumber(ctx, props, "ambientB", lp->ambient.b);
                    }
                    // ShadowPass has no script-tunable fields in
                    // this iteration. Adding the dynamic_cast branch
                    // here would just no-op; leave it out until there
                    // are knobs worth exposing.
                }

                // Build a stage of a given kind. Returns nullptr if kind unknown.
                std::unique_ptr<Bokken::Renderer::Stage> makeStage(const std::string &kind, const std::string &name)
                {
                    if (kind == "sprite")
                        return std::make_unique<Bokken::Renderer::SpriteStage>(name);
                    if (kind == "bloom")
                        return std::make_unique<Bokken::Renderer::BloomStage>(name);
                    if (kind == "color-grade")
                        return std::make_unique<Bokken::Renderer::ColorGradeStage>(name);
                    if (kind == "distortion")
                        return std::make_unique<Bokken::Renderer::DistortionStage>(name);
                    if (kind == "lighting")
                        return std::make_unique<Bokken::Renderer::LightingPass>(name);
                    if (kind == "shadow")
                        return std::make_unique<Bokken::Renderer::ShadowPass>(name);
                    if (kind == "composite")
                        return std::make_unique<Bokken::Renderer::CompositeStage>(name);
                    return nullptr;
                }
            } // namespace

            //  JS-facing functions

            JSValue Renderer::js_pipeline_add_stage(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (!s_renderer)
                    return JS_FALSE;
                if (argc < 2)
                {
                    return JS_ThrowTypeError(ctx, "pipeline.addStage(kind, name, props?) requires (string, string)");
                }
                const char *kindC = JS_ToCString(ctx, argv[0]);
                const char *nameC = JS_ToCString(ctx, argv[1]);
                if (!kindC || !nameC)
                {
                    if (kindC)
                        JS_FreeCString(ctx, kindC);
                    if (nameC)
                        JS_FreeCString(ctx, nameC);
                    return JS_FALSE;
                }
                std::string kind = kindC, name = nameC;
                JS_FreeCString(ctx, kindC);
                JS_FreeCString(ctx, nameC);

                auto stage = makeStage(kind, name);
                if (!stage)
                {
                    return JS_ThrowTypeError(ctx, "Unknown stage kind '%s'", kind.c_str());
                }
                // Apply props before handing off — addStage runs setup() and resize().
                if (argc >= 3)
                    applyProps(ctx, stage.get(), argv[2]);

                s_renderer->pipeline().addStage(std::move(stage));
                return JS_TRUE;
            }

            JSValue Renderer::js_pipeline_remove_stage(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (!s_renderer || argc < 1)
                    return JS_FALSE;
                const char *nameC = JS_ToCString(ctx, argv[0]);
                if (!nameC)
                    return JS_FALSE;
                bool ok = s_renderer->pipeline().removeStage(nameC);
                JS_FreeCString(ctx, nameC);
                return ok ? JS_TRUE : JS_FALSE;
            }

            JSValue Renderer::js_pipeline_move_stage(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (!s_renderer || argc < 2)
                    return JS_FALSE;
                const char *nameC = JS_ToCString(ctx, argv[0]);
                if (!nameC)
                    return JS_FALSE;
                int32_t idx = 0;
                JS_ToInt32(ctx, &idx, argv[1]);
                bool ok = s_renderer->pipeline().moveStage(nameC, idx);
                JS_FreeCString(ctx, nameC);
                return ok ? JS_TRUE : JS_FALSE;
            }

            JSValue Renderer::js_pipeline_set_enabled(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (!s_renderer || argc < 2)
                    return JS_FALSE;
                const char *nameC = JS_ToCString(ctx, argv[0]);
                if (!nameC)
                    return JS_FALSE;
                Bokken::Renderer::Stage *st = s_renderer->pipeline().findStage(nameC);
                JS_FreeCString(ctx, nameC);
                if (!st)
                    return JS_FALSE;
                st->enabled = (JS_ToBool(ctx, argv[1]) != 0);
                return JS_TRUE;
            }

            JSValue Renderer::js_pipeline_configure(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (!s_renderer || argc < 2)
                    return JS_FALSE;
                const char *nameC = JS_ToCString(ctx, argv[0]);
                if (!nameC)
                    return JS_FALSE;
                Bokken::Renderer::Stage *st = s_renderer->pipeline().findStage(nameC);
                JS_FreeCString(ctx, nameC);
                if (!st)
                    return JS_FALSE;
                applyProps(ctx, st, argv[1]);
                return JS_TRUE;
            }

            JSValue Renderer::js_pipeline_list(JSContext *ctx, JSValueConst, int /*argc*/, JSValueConst * /*argv*/)
            {
                if (!s_renderer)
                    return JS_NewArray(ctx);
                const auto &stages = s_renderer->pipeline().stages();
                JSValue arr = JS_NewArray(ctx);
                for (size_t i = 0; i < stages.size(); ++i)
                {
                    JS_SetPropertyUint32(ctx, arr, (uint32_t)i,
                                         JS_NewString(ctx, stages[i]->name().c_str()));
                }
                return arr;
            }

            // JS: Renderer.loadTexture(path, filter?)
            //     filter: "linear" (default) or "nearest"
            //     Returns true on success.
            JSValue Renderer::js_load_texture(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (!s_renderer || !s_assets || argc < 1)
                    return JS_FALSE;

                const char *path = JS_ToCString(ctx, argv[0]);
                if (!path)
                    return JS_FALSE;

                Bokken::Renderer::TextureFilter filter = Bokken::Renderer::TextureFilter::Linear;
                if (argc >= 2)
                {
                    const char *filterStr = JS_ToCString(ctx, argv[1]);
                    if (filterStr)
                    {
                        if (strcmp(filterStr, "nearest") == 0)
                            filter = Bokken::Renderer::TextureFilter::Nearest;
                        JS_FreeCString(ctx, filterStr);
                    }
                }

                const Bokken::Renderer::Texture2D *tex =
                    s_renderer->textures().load(path, s_assets, filter);
                JS_FreeCString(ctx, path);
                return tex ? JS_TRUE : JS_FALSE;
            }

            // JS: Renderer.defineRegion(name, texturePath, x, y, w, h)
            JSValue Renderer::js_define_region(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (!s_renderer || argc < 6)
                    return JS_FALSE;

                const char *name = JS_ToCString(ctx, argv[0]);
                const char *texPath = JS_ToCString(ctx, argv[1]);
                if (!name || !texPath)
                {
                    if (name)
                        JS_FreeCString(ctx, name);
                    if (texPath)
                        JS_FreeCString(ctx, texPath);
                    return JS_FALSE;
                }

                int32_t x, y, w, h;
                JS_ToInt32(ctx, &x, argv[2]);
                JS_ToInt32(ctx, &y, argv[3]);
                JS_ToInt32(ctx, &w, argv[4]);
                JS_ToInt32(ctx, &h, argv[5]);

                s_renderer->textures().defineRegion(name, texPath, x, y, w, h);
                JS_FreeCString(ctx, name);
                JS_FreeCString(ctx, texPath);
                return JS_TRUE;
            }

            // JS: Renderer.defineGrid(prefix, texturePath, frameW, frameH, props?)
            //     props: { count?, offsetX?, offsetY?, paddingX?, paddingY? }
            //     Returns the number of regions created.
            JSValue Renderer::js_define_grid(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (!s_renderer || argc < 4)
                    return JS_NewInt32(ctx, 0);

                const char *prefix = JS_ToCString(ctx, argv[0]);
                const char *texPath = JS_ToCString(ctx, argv[1]);
                if (!prefix || !texPath)
                {
                    if (prefix)
                        JS_FreeCString(ctx, prefix);
                    if (texPath)
                        JS_FreeCString(ctx, texPath);
                    return JS_NewInt32(ctx, 0);
                }

                int32_t frameW, frameH;
                JS_ToInt32(ctx, &frameW, argv[2]);
                JS_ToInt32(ctx, &frameH, argv[3]);

                int count = 0, offX = 0, offY = 0, padX = 0, padY = 0;

                if (argc >= 5 && JS_IsObject(argv[4]))
                {
                    JSValue v;
                    int32_t tmp;

                    v = JS_GetPropertyStr(ctx, argv[4], "count");
                    if (JS_IsNumber(v))
                    {
                        JS_ToInt32(ctx, &tmp, v);
                        count = tmp;
                    }
                    JS_FreeValue(ctx, v);

                    v = JS_GetPropertyStr(ctx, argv[4], "offsetX");
                    if (JS_IsNumber(v))
                    {
                        JS_ToInt32(ctx, &tmp, v);
                        offX = tmp;
                    }
                    JS_FreeValue(ctx, v);

                    v = JS_GetPropertyStr(ctx, argv[4], "offsetY");
                    if (JS_IsNumber(v))
                    {
                        JS_ToInt32(ctx, &tmp, v);
                        offY = tmp;
                    }
                    JS_FreeValue(ctx, v);

                    v = JS_GetPropertyStr(ctx, argv[4], "paddingX");
                    if (JS_IsNumber(v))
                    {
                        JS_ToInt32(ctx, &tmp, v);
                        padX = tmp;
                    }
                    JS_FreeValue(ctx, v);

                    v = JS_GetPropertyStr(ctx, argv[4], "paddingY");
                    if (JS_IsNumber(v))
                    {
                        JS_ToInt32(ctx, &tmp, v);
                        padY = tmp;
                    }
                    JS_FreeValue(ctx, v);
                }

                int created = s_renderer->textures().defineGrid(
                    prefix, texPath, frameW, frameH, count, offX, offY, padX, padY);
                JS_FreeCString(ctx, prefix);
                JS_FreeCString(ctx, texPath);
                return JS_NewInt32(ctx, created);
            }

            // JS: Renderer.addShockwave(x, y, props?)
            //     x, y: normalised screen coordinates (0..1).
            //     props: { speed?, thickness?, amplitude?, maxRadius? }
            //
            // Finds the first DistortionStage in the pipeline and adds a
            // shockwave to it. If no distortion stage exists, does nothing.
            JSValue Renderer::js_add_shockwave(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (!s_renderer || argc < 2)
                    return JS_FALSE;

                double x = 0, y = 0;
                JS_ToFloat64(ctx, &x, argv[0]);
                JS_ToFloat64(ctx, &y, argv[1]);

                float speed = 0.8f, thickness = 0.1f, amplitude = 0.06f, maxRadius = 1.5f;

                if (argc >= 3 && JS_IsObject(argv[2]))
                {
                    auto readF = [&](const char *prop, float &out)
                    {
                        JSValue v = JS_GetPropertyStr(ctx, argv[2], prop);
                        if (JS_IsNumber(v))
                        {
                            double d = 0;
                            JS_ToFloat64(ctx, &d, v);
                            out = static_cast<float>(d);
                        }
                        JS_FreeValue(ctx, v);
                    };

                    readF("speed", speed);
                    readF("thickness", thickness);
                    readF("amplitude", amplitude);
                    readF("maxRadius", maxRadius);
                }

                // Find the distortion stage in the pipeline.
                for (auto &stage : s_renderer->pipeline().stages())
                {
                    auto *ds = dynamic_cast<Bokken::Renderer::DistortionStage *>(stage.get());
                    if (ds)
                    {
                        ds->addShockwave(static_cast<float>(x), static_cast<float>(y),
                                         speed, thickness, amplitude, maxRadius);
                        return JS_TRUE;
                    }
                }

                return JS_FALSE;
            }

            // JS: Renderer.clearShockwaves()
            JSValue Renderer::js_clear_shockwaves(JSContext *ctx, JSValueConst, int, JSValueConst *)
            {
                if (!s_renderer)
                    return JS_UNDEFINED;

                for (auto &stage : s_renderer->pipeline().stages())
                {
                    auto *ds = dynamic_cast<Bokken::Renderer::DistortionStage *>(stage.get());
                    if (ds)
                    {
                        ds->clearShockwaves();
                        break;
                    }
                }

                return JS_UNDEFINED;
            }

            //  Render-target lifecycle.

            void Renderer::detach()
            {
                if (s_renderer && s_rendererSubId != -1)
                    s_renderer->removeRenderSizeListener(s_rendererSubId);
                s_rendererSubId = -1;

                // Release JS callback bookkeeping. detach() runs from
                // Loop::shutdown AFTER ScriptingEngine::shutdown, by
                // which point the JSContexts these callbacks reference
                // have already been torn down — so we can't (and
                // mustn't) call JS_FreeValue here. Just clear the
                // vector.
                s_listeners.clear();
                s_nextListenerId = 1;
            }

            void Renderer::destroy(JSContext * /*ctx*/)
            {
                // Each listener stored the context it was registered from, so
                // we free against l.ctx rather than the passed-in one (they
                // are the same runtime here, but using l.ctx keeps this honest
                // and self-contained).
                for (Listener &l : s_listeners)
                    JS_FreeValue(l.ctx, l.fn);
                s_listeners.clear();
                s_nextListenerId = 1;
            }

            void Renderer::onRendererRenderSizeChanged(int w, int h)
            {
                if (s_listeners.empty())
                    return;

                // Snapshot before iterating so a handler that calls
                // offResize() during dispatch doesn't invalidate our
                // iterator. JS callbacks are notorious for mutating
                // the structure they were invoked from.
                auto snapshot = s_listeners;
                for (const auto &L : snapshot)
                {
                    JSValue arg = JS_NewObject(L.ctx);
                    JS_SetPropertyStr(L.ctx, arg, "width", JS_NewInt32(L.ctx, w));
                    JS_SetPropertyStr(L.ctx, arg, "height", JS_NewInt32(L.ctx, h));
                    JSValueConst argv[1] = {arg};
                    JSValue ret = JS_Call(L.ctx, L.fn, JS_UNDEFINED, 1, argv);
                    if (JS_IsException(ret))
                    {
                        // Swallow the exception — a buggy listener
                        // shouldn't kill the whole frame. Print it
                        // so the script author sees what happened.
                        JSValue ex = JS_GetException(L.ctx);
                        const char *msg = JS_ToCString(L.ctx, ex);
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                     "[Renderer.onResize] handler threw: %s",
                                     msg ? msg : "<unknown>");
                        if (msg)
                            JS_FreeCString(L.ctx, msg);
                        JS_FreeValue(L.ctx, ex);
                    }
                    JS_FreeValue(L.ctx, ret);
                    JS_FreeValue(L.ctx, arg);
                }
            }

            JSValue Renderer::js_get_render_size(JSContext *ctx, JSValueConst /*this_val*/,
                                                 int /*argc*/, JSValueConst * /*argv*/)
            {
                int w = 0, h = 0;
                if (s_renderer)
                {
                    w = s_renderer->renderWidth();
                    h = s_renderer->renderHeight();
                }
                JSValue obj = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, obj, "width", JS_NewInt32(ctx, w));
                JS_SetPropertyStr(ctx, obj, "height", JS_NewInt32(ctx, h));
                return obj;
            }

            JSValue Renderer::js_get_render_mode(JSContext *ctx, JSValueConst /*this_val*/,
                                                 int /*argc*/, JSValueConst * /*argv*/)
            {
                const char *name = "follow";
                if (s_renderer)
                {
                    switch (s_renderer->renderSizePolicy())
                    {
                    case Bokken::Renderer::RenderSizePolicy::FollowWindow:
                        name = "follow";
                        break;
                    case Bokken::Renderer::RenderSizePolicy::Fixed:
                        name = "fixed";
                        break;
                    case Bokken::Renderer::RenderSizePolicy::FixedHeight:
                        name = "fixedHeight";
                        break;
                    }
                }
                return JS_NewString(ctx, name);
            }

            // setRenderSize(width, height, mode?)
            //   mode is one of "follow", "fixed", "fixedHeight"
            //   (default "fixed"). width/height are ignored for
            //   "follow" — under that policy the render size always
            //   tracks the window.
            JSValue Renderer::js_set_render_size(JSContext *ctx, JSValueConst /*this_val*/,
                                                 int argc, JSValueConst *argv)
            {
                if (!s_renderer)
                    return JS_FALSE;

                int32_t w = 0, h = 0;
                if (argc >= 1)
                    JS_ToInt32(ctx, &w, argv[0]);
                if (argc >= 2)
                    JS_ToInt32(ctx, &h, argv[1]);

                Bokken::Renderer::RenderSizePolicy policy =
                    Bokken::Renderer::RenderSizePolicy::Fixed;
                if (argc >= 3 && JS_IsString(argv[2]))
                {
                    const char *mode = JS_ToCString(ctx, argv[2]);
                    if (mode)
                    {
                        if (std::strcmp(mode, "follow") == 0)
                            policy = Bokken::Renderer::RenderSizePolicy::FollowWindow;
                        else if (std::strcmp(mode, "fixedHeight") == 0)
                            policy = Bokken::Renderer::RenderSizePolicy::FixedHeight;
                        else
                            policy = Bokken::Renderer::RenderSizePolicy::Fixed;
                        JS_FreeCString(ctx, mode);
                    }
                }

                // setRenderSize on the renderer stages the new policy;
                // the actual commit happens in the next beginFrame(),
                // and that's where the render-size-changed observer
                // fires. We don't redispatch here.
                const bool ok = s_renderer->setRenderSize(w, h, policy);
                return JS_NewBool(ctx, ok);
            }

            JSValue Renderer::js_on_resize(JSContext *ctx, JSValueConst /*this_val*/,
                                           int argc, JSValueConst *argv)
            {
                if (argc < 1 || !JS_IsFunction(ctx, argv[0]))
                    return JS_ThrowTypeError(ctx,
                                             "Renderer.onResize(callback): callback must be a function");

                const int id = s_nextListenerId++;
                s_listeners.push_back(Listener{
                    id, ctx, JS_DupValue(ctx, argv[0])});
                return JS_NewInt32(ctx, id);
            }

            JSValue Renderer::js_off_resize(JSContext *ctx, JSValueConst /*this_val*/,
                                            int argc, JSValueConst *argv)
            {
                if (argc < 1)
                    return JS_FALSE;
                int32_t id = 0;
                if (JS_ToInt32(ctx, &id, argv[0]) < 0)
                    return JS_FALSE;

                for (auto it = s_listeners.begin(); it != s_listeners.end(); ++it)
                {
                    if (it->id == id)
                    {
                        JS_FreeValue(it->ctx, it->fn);
                        s_listeners.erase(it);
                        return JS_TRUE;
                    }
                }
                return JS_FALSE;
            }

            int Renderer::declare(JSContext *ctx, JSModuleDef *m)
            {
                return JS_AddModuleExport(ctx, m, "default");
            }

            int Renderer::init(JSContext *ctx, JSModuleDef *m)
            {
                JSValue def = JS_NewObject(ctx);
                JSValue pipe = JS_NewObject(ctx);

                JS_SetPropertyStr(ctx, pipe, "addStage",
                                  JS_NewCFunction(ctx, &Renderer::js_pipeline_add_stage, "addStage", 3));
                JS_SetPropertyStr(ctx, pipe, "removeStage",
                                  JS_NewCFunction(ctx, &Renderer::js_pipeline_remove_stage, "removeStage", 1));
                JS_SetPropertyStr(ctx, pipe, "moveStage",
                                  JS_NewCFunction(ctx, &Renderer::js_pipeline_move_stage, "moveStage", 2));
                JS_SetPropertyStr(ctx, pipe, "setEnabled",
                                  JS_NewCFunction(ctx, &Renderer::js_pipeline_set_enabled, "setEnabled", 2));
                JS_SetPropertyStr(ctx, pipe, "configure",
                                  JS_NewCFunction(ctx, &Renderer::js_pipeline_configure, "configure", 2));
                JS_SetPropertyStr(ctx, pipe, "list",
                                  JS_NewCFunction(ctx, &Renderer::js_pipeline_list, "list", 0));

                JS_SetPropertyStr(ctx, def, "pipeline", pipe);

                // Texture management functions.
                JS_SetPropertyStr(ctx, def, "loadTexture",
                                  JS_NewCFunction(ctx, &Renderer::js_load_texture, "loadTexture", 2));
                JS_SetPropertyStr(ctx, def, "defineRegion",
                                  JS_NewCFunction(ctx, &Renderer::js_define_region, "defineRegion", 6));
                JS_SetPropertyStr(ctx, def, "defineGrid",
                                  JS_NewCFunction(ctx, &Renderer::js_define_grid, "defineGrid", 5));

                // Distortion functions.
                JS_SetPropertyStr(ctx, def, "addShockwave",
                                  JS_NewCFunction(ctx, &Renderer::js_add_shockwave, "addShockwave", 3));
                JS_SetPropertyStr(ctx, def, "clearShockwaves",
                                  JS_NewCFunction(ctx, &Renderer::js_clear_shockwaves, "clearShockwaves", 0));

                // Render target. Moved here from `bokken/window`
                // because they describe the pipeline output, not the
                // OS window.
                JS_SetPropertyStr(ctx, def, "getRenderSize",
                                  JS_NewCFunction(ctx, &Renderer::js_get_render_size, "getRenderSize", 0));
                JS_SetPropertyStr(ctx, def, "getRenderMode",
                                  JS_NewCFunction(ctx, &Renderer::js_get_render_mode, "getRenderMode", 0));
                JS_SetPropertyStr(ctx, def, "setRenderSize",
                                  JS_NewCFunction(ctx, &Renderer::js_set_render_size, "setRenderSize", 3));
                JS_SetPropertyStr(ctx, def, "onResize",
                                  JS_NewCFunction(ctx, &Renderer::js_on_resize, "onResize", 1));
                JS_SetPropertyStr(ctx, def, "offResize",
                                  JS_NewCFunction(ctx, &Renderer::js_off_resize, "offResize", 1));

                JS_SetModuleExport(ctx, m, "default", def);
                return 0;
            }

        }
    }
}