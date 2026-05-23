#include "Canvas.hpp"
#include "Renderer.hpp"
#include "../../canvas/Drawing.hpp"
#include "../../renderer/TextureCache.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <functional>

namespace Bokken
{
    namespace Scripting
    {
        namespace Modules
        {
            std::map<void *, std::vector<JSValue>> Canvas::s_states;
            void *Canvas::s_active_comp = nullptr;
            int Canvas::s_hook_idx = 0;
            int Canvas::s_effect_idx = 0;
            JSValue Canvas::s_root_element = JS_UNDEFINED;

            std::map<void *, std::vector<Canvas::EffectSlot>> Canvas::s_effects;
            std::vector<std::pair<void *, int>> Canvas::s_pendingEffects;

            void Canvas::setTextureCache(Bokken::Renderer::TextureCache *t)
            {
                /* Wired into both Image (for asset lookup) so its draw
                 * path doesn't need a per-call TextureCache pointer. */
                Bokken::Canvas::Components::Image::s_textureCache = t;
            }

            //  Render-target lifecycle.

            void Canvas::attach()
            {
                auto *renderer = Bokken::Scripting::Modules::Renderer::renderer();
                if (!renderer || s_rendererSubId != -1)
                    return;
                s_rendererSubId = renderer->addRenderSizeListener(
                    &Canvas::onRendererRenderSizeChanged);
            }

            void Canvas::detach()
            {
                auto *renderer = Bokken::Scripting::Modules::Renderer::renderer();
                if (renderer && s_rendererSubId != -1)
                    renderer->removeRenderSizeListener(s_rendererSubId);
                s_rendererSubId = -1;
            }

            void Canvas::destroy(JSContext *ctx)
            {
                if (!ctx)
                    return;

                // useState stores: every retained state value.
                for (auto &entry : s_states)
                {
                    for (JSValue v : entry.second)
                        JS_FreeValue(ctx, v);
                }
                s_states.clear();

                // useEffect slots: each holds a callback, an optional cleanup
                // function, and a dependency array — all dup'd JSValues.
                for (auto &entry : s_effects)
                {
                    for (EffectSlot &slot : entry.second)
                    {
                        JS_FreeValue(ctx, slot.callback);
                        JS_FreeValue(ctx, slot.cleanup);
                        for (JSValue d : slot.deps)
                            JS_FreeValue(ctx, d);
                    }
                }
                s_effects.clear();
                s_pendingEffects.clear();

                // The retained root element from the last render().
                JS_FreeValue(ctx, s_root_element);
                s_root_element = JS_UNDEFINED;

                // Interned atom cache. JSAtoms are per-runtime integer handles
                // into rt->atom_array. After this reload frees the runtime and
                // a fresh one is created, every cached atom value is stale —
                // using one (create_element writes "type"/"properties"/
                // "children" by cached atom) indexes the NEW runtime's
                // atom_array at the OLD index and crashes in JS_DupAtom. Free
                // each atom against the still-live context, then clear the map
                // so atom_for() re-interns against the new runtime on next use.
                for (auto &entry : s_atoms)
                    JS_FreeAtom(ctx, entry.second);
                s_atoms.clear();

                // Invalidate every cache derived from per-runtime handles
                // (the style-dispatch atom table rebuilds when it sees this
                // advance). Wraps harmlessly; only equality matters.
                ++s_runtimeGeneration;

                // The retained widget tree and the transient interaction
                // pointers all reference the JS element tree we just released.
                // Drop them so a rebuilt tree never reconciles against, or
                // dispatches input into, freed nodes.
                s_current_tree = nullptr;
                s_hovered_node = nullptr;
                s_pressed_node = nullptr;
                s_focused_node = nullptr;
                s_press_active = false;

                // Per-frame hook bookkeeping back to its cold-start values.
                s_active_comp = nullptr;
                s_hook_idx = 0;
                s_effect_idx = 0;
                s_render_dirty = false;
                s_rendering = false;
            }

            void Canvas::onRendererRenderSizeChanged(int /*w*/, int /*h*/)
            {
                // forceRelayout reads the current render dimensions
                // itself, so the (w, h) args are unused here.
                forceRelayout();
            }

            //  Viewport & input-coord helpers.

            void Canvas::viewport(int &w, int &h)
            {
                if (auto *renderer = Bokken::Scripting::Modules::Renderer::renderer())
                {
                    w = renderer->renderWidth();
                    h = renderer->renderHeight();
                    return;
                }
                // Fallback for tooling / tests without a renderer:
                // logical window size, so layout doesn't degenerate
                // to 0x0 and trigger downstream divide-by-zeros.
                w = h = 0;
                if (s_window)
                    SDL_GetWindowSize(s_window, &w, &h);
            }

            void Canvas::screen_to_render(float &x, float &y)
            {
                auto *renderer = Bokken::Scripting::Modules::Renderer::renderer();
                if (!renderer)
                    return; // pass-through

                // Mouse coords arrive in logical pixels; the
                // renderer's window↔render math is in physical
                // (framebuffer) pixels. Scale up by dpiScale so the
                // conversion correctly inverts the composite blit
                // on HighDPI displays. Mirrors Camera2D::screenTo-
                // WorldPoint's first leg.
                const float dpi = renderer->dpiScale();
                float rx = 0.0f, ry = 0.0f;
                renderer->windowToRender(x * dpi, y * dpi, rx, ry);
                x = rx;
                y = ry;
            }

            TTF_Font *Canvas::get_font(const std::string &path, float size)
            {
                std::string key = path + ":" + std::to_string((int)size);
                if (s_font_cache.count(key))
                    return s_font_cache[key];
                if (!s_assets)
                {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Canvas] AssetPack not initialized.");
                    return nullptr;
                }
                std::string targetPath = path;
                if (targetPath.empty() || !s_assets->exists(targetPath))
                    targetPath = "fonts/default.ttf";
                if (!s_assets->exists(targetPath))
                {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[Canvas] Font path does not exist: %s", targetPath.c_str());
                    return nullptr;
                }
                SDL_IOStream *fontStream = s_assets->openIOStream(targetPath);
                if (fontStream)
                {
                    TTF_Font *font = TTF_OpenFontIO(fontStream, true, size);
                    if (font)
                    {
                        s_font_cache[key] = font;
                        return font;
                    }
                }
                SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "[Canvas] Failed to load font %s: %s", targetPath.c_str(), SDL_GetError());
                return nullptr;
            }

            void Canvas::clear_font_cache()
            {
                for (auto const &[key, font] : s_font_cache)
                    TTF_CloseFont(font);
                s_font_cache.clear();
            }

            namespace
            {
                /* Style parser plumbing
                 *
                 * Small reusable lambdas for reading typed style fields
                 * (get_b for bool flags, and the wider field set),
                 * factored out to keep the parser readable. */

                struct StyleParser
                {
                    JSContext *ctx;
                    JSValue style;

                    /* All getters route through getp(), which uses the
                     * atom cache. JS_GetPropertyStr would otherwise
                     * intern the C-string as a JSAtom on every call —
                     * for a parser that runs 80+ times per node, this
                     * matters. */
                    JSValue getp(const char *k)
                    {
                        return JS_GetProperty(ctx, style, Canvas::atom_for(ctx, k));
                    }

                    /* Read a number-or-string ("100"/"100%") into
                     * (val, isPercent). */
                    void dimension(const char *k, float &val, bool &isPercent)
                    {
                        JSValue v = getp(k);
                        if (JS_IsNumber(v))
                        {
                            double d;
                            JS_ToFloat64(ctx, &d, v);
                            val = (float)d;
                            isPercent = false;
                        }
                        else if (JS_IsString(v))
                        {
                            const char *str = JS_ToCString(ctx, v);
                            if (str)
                            {
                                std::string s(str);
                                if (!s.empty() && s.back() == '%')
                                {
                                    try
                                    {
                                        val = std::stof(s.substr(0, s.size() - 1));
                                        isPercent = true;
                                    }
                                    catch (...)
                                    {
                                        val = 0;
                                        isPercent = false;
                                    }
                                }
                                else
                                {
                                    try
                                    {
                                        val = std::stof(s);
                                        isPercent = false;
                                    }
                                    catch (...)
                                    {
                                        val = 0;
                                        isPercent = false;
                                    }
                                }
                                JS_FreeCString(ctx, str);
                            }
                        }
                        JS_FreeValue(ctx, v);
                    }
                    void f(const char *k, float &t)
                    {
                        JSValue v = getp(k);
                        if (JS_IsNumber(v))
                        {
                            double d;
                            if (JS_ToFloat64(ctx, &d, v) == 0 && !std::isnan(d))
                                t = (float)d;
                        }
                        JS_FreeValue(ctx, v);
                    }
                    void u32(const char *k, uint32_t &t)
                    {
                        JSValue v = getp(k);
                        uint32_t u;
                        if (JS_ToUint32(ctx, &u, v) == 0)
                            t = u;
                        JS_FreeValue(ctx, v);
                    }
                    void s(const char *k, std::string &t)
                    {
                        JSValue v = getp(k);
                        if (JS_IsString(v))
                        {
                            const char *str = JS_ToCString(ctx, v);
                            if (str)
                            {
                                t = str;
                                JS_FreeCString(ctx, str);
                            }
                        }
                        JS_FreeValue(ctx, v);
                    }
                    void b(const char *k, bool &t)
                    {
                        JSValue v = getp(k);
                        if (JS_IsBool(v))
                            t = JS_ToBool(ctx, v) ? true : false;
                        JS_FreeValue(ctx, v);
                    }
                    void i32(const char *k, int32_t &t)
                    {
                        JSValue v = getp(k);
                        if (JS_IsNumber(v))
                        {
                            int32_t i;
                            if (JS_ToInt32(ctx, &i, v) == 0)
                                t = i;
                        }
                        JS_FreeValue(ctx, v);
                    }
                    /* Read an enum field by string name. `cases` is a
                     * null-terminated array of {name, value} pairs. */
                    template <typename E>
                    void enumStr(const char *k, E &out, std::initializer_list<std::pair<const char *, E>> cases)
                    {
                        JSValue v = getp(k);
                        if (JS_IsString(v))
                        {
                            const char *str = JS_ToCString(ctx, v);
                            if (str)
                            {
                                for (auto &c : cases)
                                    if (std::strcmp(str, c.first) == 0)
                                    {
                                        out = c.second;
                                        break;
                                    }
                                JS_FreeCString(ctx, str);
                            }
                        }
                        JS_FreeValue(ctx, v);
                    }
                };
            }

            JSAtom Canvas::atom_for(JSContext *ctx, const char *name)
            {
                /* Cache key is the C-string POINTER, not its contents.
                 * Safe because we only ever pass string literals here
                 * and string literal pointers are stable for the
                 * lifetime of the program. If someone passes a
                 * heap-allocated name with the same content as before,
                 * we'd intern a duplicate atom — wasteful but correct.
                 *
                 * Atoms leak intentionally — they live as long as the
                 * runtime. JS_FreeAtom would be needed at shutdown
                 * for clean teardown but we accept the leak for now. */
                auto it = s_atoms.find(name);
                if (it != s_atoms.end()) return it->second;
                JSAtom a = JS_NewAtom(ctx, name);
                s_atoms[name] = a;
                return a;
            }

            void Canvas::parse_simple_style_sheet(JSContext *ctx, JSValue style,
                                                  Bokken::Canvas::SimpleStyleSheet &out)
            {
                if (!JS_IsObject(style))
                    return;
                StyleParser p{ctx, style};

                /* Inverted parse: rather than checking each of ~80
                 * known style properties on every node, we iterate
                 * the style object's OWN keys and dispatch each one
                 * through a static lookup table.
                 *
                 * Why this matters: a typical View style has 3-10
                 * keys. The old code did 80 JS_GetProperty calls
                 * regardless — 70 of them returning UNDEFINED. With
                 * 700 nodes per render, that's 56,000 wasted property
                 * lookups every frame. Inversion cuts it to 3,000-7,000.
                 *
                 * Dispatch is keyed by JSAtom (interned once, integer-
                 * comparable) rather than by string. That avoids
                 * AtomToCString + std::string allocation + hash on
                 * every key on every node. */
                using ParseFn = void (*)(StyleParser &, Bokken::Canvas::SimpleStyleSheet &);

                /* String dispatch table — built once, used to populate
                 * the atom-keyed dispatch table on first call. */
                static std::unordered_map<std::string, ParseFn> dispatch = []() {
                    std::unordered_map<std::string, ParseFn> m;
                    /* Sizing */
                    m["width"]         = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.dimension("width", o.width, o.widthIsPercent); };
                    m["height"]        = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.dimension("height", o.height, o.heightIsPercent); };
                    m["minimumWidth"]  = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.dimension("minimumWidth", o.minimumWidth, o.minimumWidthIsPercent); };
                    m["maximumWidth"]  = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.dimension("maximumWidth", o.maximumWidth, o.maximumWidthIsPercent); };
                    m["minimumHeight"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.dimension("minimumHeight", o.minimumHeight, o.minimumHeightIsPercent); };
                    m["maximumHeight"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.dimension("maximumHeight", o.maximumHeight, o.maximumHeightIsPercent); };

                    /* Box model */
                    m["margin"]        = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("margin", o.margin); };
                    m["marginTop"]     = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("marginTop", o.marginTop); };
                    m["marginBottom"]  = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("marginBottom", o.marginBottom); };
                    m["marginLeft"]    = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("marginLeft", o.marginLeft); };
                    m["marginRight"]   = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("marginRight", o.marginRight); };
                    m["padding"]       = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("padding", o.padding); };
                    m["paddingTop"]    = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("paddingTop", o.paddingTop); };
                    m["paddingBottom"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("paddingBottom", o.paddingBottom); };
                    m["paddingLeft"]   = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("paddingLeft", o.paddingLeft); };
                    m["paddingRight"]  = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("paddingRight", o.paddingRight); };
                    m["gap"]           = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("gap", o.gap); };
                    m["rowGap"]        = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("rowGap", o.rowGap); };
                    m["columnGap"]     = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("columnGap", o.columnGap); };

                    /* Position */
                    m["top"]    = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("top", o.top); };
                    m["bottom"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("bottom", o.bottom); };
                    m["left"]   = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("left", o.left); };
                    m["right"]  = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("right", o.right); };
                    m["zIndex"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.i32("zIndex", o.zIndex); };
                    m["position"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){
                        p.enumStr<Bokken::Canvas::Position>("position", o.position, {
                            {"Relative", Bokken::Canvas::Position::Relative},
                            {"Absolute", Bokken::Canvas::Position::Absolute},
                        });
                    };

                    /* Visuals */
                    m["backgroundColor"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.u32("backgroundColor", o.backgroundColor); };
                    m["color"]           = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.u32("color", o.color); };
                    m["opacity"]         = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("opacity", o.opacity); };

                    m["borderRadius"]            = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("borderRadius", o.borderRadius); };
                    m["borderTopLeftRadius"]     = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("borderTopLeftRadius", o.borderTopLeftRadius); };
                    m["borderTopRightRadius"]    = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("borderTopRightRadius", o.borderTopRightRadius); };
                    m["borderBottomLeftRadius"]  = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("borderBottomLeftRadius", o.borderBottomLeftRadius); };
                    m["borderBottomRightRadius"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("borderBottomRightRadius", o.borderBottomRightRadius); };

                    m["borderWidth"]       = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("borderWidth", o.borderWidth); };
                    m["borderColor"]       = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.u32("borderColor", o.borderColor); };
                    m["borderTopWidth"]    = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("borderTopWidth", o.borderTopWidth); };
                    m["borderBottomWidth"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("borderBottomWidth", o.borderBottomWidth); };
                    m["borderLeftWidth"]   = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("borderLeftWidth", o.borderLeftWidth); };
                    m["borderRightWidth"]  = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("borderRightWidth", o.borderRightWidth); };
                    m["borderTopColor"]    = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.u32("borderTopColor", o.borderTopColor); };
                    m["borderBottomColor"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.u32("borderBottomColor", o.borderBottomColor); };
                    m["borderLeftColor"]   = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.u32("borderLeftColor", o.borderLeftColor); };
                    m["borderRightColor"]  = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.u32("borderRightColor", o.borderRightColor); };

                    /* Gradients & background image */
                    m["gradientStart"]   = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.u32("gradientStart", o.gradientStart); };
                    m["gradientEnd"]     = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.u32("gradientEnd", o.gradientEnd); };
                    m["gradientAngle"]   = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("gradientAngle", o.gradientAngle); };
                    m["backgroundImage"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.s("backgroundImage", o.backgroundImage); };

                    /* Shadow */
                    m["shadowColor"]   = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.u32("shadowColor", o.shadowColor); };
                    m["shadowOffsetX"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("shadowOffsetX", o.shadowOffsetX); };
                    m["shadowOffsetY"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("shadowOffsetY", o.shadowOffsetY); };
                    m["shadowBlur"]    = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("shadowBlur", o.shadowBlur); };

                    m["overflow"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){
                        p.enumStr<Bokken::Canvas::Overflow>("overflow", o.overflow, {
                            {"Visible", Bokken::Canvas::Overflow::Visible},
                            {"Hidden", Bokken::Canvas::Overflow::Hidden},
                        });
                    };

                    /* Transform */
                    m["translateX"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("translateX", o.translateX); };
                    m["translateY"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("translateY", o.translateY); };
                    m["rotation"]   = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("rotation", o.rotation); };
                    m["scaleX"]     = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("scaleX", o.scaleX); };
                    m["scaleY"]     = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("scaleY", o.scaleY); };

                    /* Text */
                    m["font"]          = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.s("font", o.font); };
                    m["fontSize"]      = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("fontSize", o.fontSize); };
                    m["lineHeight"]    = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("lineHeight", o.lineHeight); };
                    m["letterSpacing"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("letterSpacing", o.letterSpacing); };
                    m["wordWrap"]      = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.b("wordWrap", o.wordWrap); };
                    m["resize"]        = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.b("resize", o.resize); };
                    m["fontBold"]      = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.b("fontBold", o.fontBold); };
                    m["fontItalic"]    = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.b("fontItalic", o.fontItalic); };
                    m["textAlign"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){
                        p.enumStr<Bokken::Canvas::TextAlign>("textAlign", o.textAlign, {
                            {"Left", Bokken::Canvas::TextAlign::Left},
                            {"Center", Bokken::Canvas::TextAlign::Center},
                            {"Right", Bokken::Canvas::TextAlign::Right},
                            {"Justify", Bokken::Canvas::TextAlign::Justify},
                        });
                    };

                    /* Flex */
                    m["flexDirection"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){
                        p.enumStr<Bokken::Canvas::FlexDirection>("flexDirection", o.flexDirection, {
                            {"Row", Bokken::Canvas::FlexDirection::Row},
                            {"Column", Bokken::Canvas::FlexDirection::Column},
                        });
                    };
                    m["flexWrap"]   = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.b("flexWrap", o.flexWrap); };
                    m["flex"]       = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("flex", o.flex); };
                    m["flexShrink"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("flexShrink", o.flexShrink); };
                    m["flexBasis"]  = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("flexBasis", o.flexBasis); };

                    m["justifyContent"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){
                        p.enumStr<Bokken::Canvas::Justify>("justifyContent", o.justifyContent, {
                            {"Start", Bokken::Canvas::Justify::Start},
                            {"Center", Bokken::Canvas::Justify::Center},
                            {"End", Bokken::Canvas::Justify::End},
                            {"SpaceBetween", Bokken::Canvas::Justify::SpaceBetween},
                            {"SpaceAround", Bokken::Canvas::Justify::SpaceAround},
                            {"SpaceEvenly", Bokken::Canvas::Justify::SpaceEvenly},
                        });
                    };
                    m["alignItems"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){
                        p.enumStr<Bokken::Canvas::SimpleStyleSheet::AlignItems>("alignItems", o.alignItems, {
                            {"Start", Bokken::Canvas::SimpleStyleSheet::AlignItems::Start},
                            {"Center", Bokken::Canvas::SimpleStyleSheet::AlignItems::Center},
                            {"End", Bokken::Canvas::SimpleStyleSheet::AlignItems::End},
                            {"Stretch", Bokken::Canvas::SimpleStyleSheet::AlignItems::Stretch},
                        });
                    };
                    m["alignSelf"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){
                        p.enumStr<Bokken::Canvas::SimpleStyleSheet::AlignSelf>("alignSelf", o.alignSelf, {
                            {"Inherit", Bokken::Canvas::SimpleStyleSheet::AlignSelf::Inherit},
                            {"Start", Bokken::Canvas::SimpleStyleSheet::AlignSelf::Start},
                            {"Center", Bokken::Canvas::SimpleStyleSheet::AlignSelf::Center},
                            {"End", Bokken::Canvas::SimpleStyleSheet::AlignSelf::End},
                            {"Stretch", Bokken::Canvas::SimpleStyleSheet::AlignSelf::Stretch},
                        });
                    };

                    /* Animation */
                    m["transitionDuration"]    = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("transitionDuration", o.transitionDuration); };
                    m["hoverScale"]            = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("hoverScale", o.hoverScale); };
                    m["activeScale"]           = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.f("activeScale", o.activeScale); };
                    m["hoverColor"]            = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.u32("hoverColor", o.hoverColor); };
                    m["hoverBackgroundColor"]  = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.u32("hoverBackgroundColor", o.hoverBackgroundColor); };
                    m["activeBackgroundColor"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.u32("activeBackgroundColor", o.activeBackgroundColor); };
                    m["transitionTiming"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){
                        p.enumStr<Bokken::Canvas::Timing>("transitionTiming", o.transitionTiming, {
                            {"Linear", Bokken::Canvas::Timing::Linear},
                            {"EaseIn", Bokken::Canvas::Timing::EaseIn},
                            {"EaseOut", Bokken::Canvas::Timing::EaseOut},
                            {"EaseInOut", Bokken::Canvas::Timing::EaseInOut},
                            {"Bounce", Bokken::Canvas::Timing::Bounce},
                            {"Back", Bokken::Canvas::Timing::Back},
                            {"Step", Bokken::Canvas::Timing::Step},
                        });
                    };

                    m["cursor"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){
                        p.enumStr<Bokken::Canvas::Cursor>("cursor", o.cursor, {
                            {"Default", Bokken::Canvas::Cursor::Default},
                            {"Pointer", Bokken::Canvas::Cursor::Pointer},
                            {"Text", Bokken::Canvas::Cursor::Text},
                            {"Move", Bokken::Canvas::Cursor::Move},
                            {"NotAllowed", Bokken::Canvas::Cursor::NotAllowed},
                            {"Wait", Bokken::Canvas::Cursor::Wait},
                            {"ResizeNS", Bokken::Canvas::Cursor::ResizeNS},
                            {"ResizeEW", Bokken::Canvas::Cursor::ResizeEW},
                            {"Crosshair", Bokken::Canvas::Cursor::Crosshair},
                        });
                    };

                    m["tabIndex"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.i32("tabIndex", o.tabIndex); };
                    m["disabled"] = [](StyleParser &p, Bokken::Canvas::SimpleStyleSheet &o){ p.b("disabled", o.disabled); };
                    return m;
                }();

                /* Atom-keyed dispatch: built lazily, and REBUILT whenever the
                 * JS runtime is torn down and replaced (live reload). A plain
                 * one-shot 'built' flag would latch the first runtime's atoms;
                 * after a reload its keys would never match the new runtime's
                 * atoms and all style parsing would silently no-op. Keying the
                 * rebuild on Canvas::runtimeGeneration() fixes that. JSAtoms
                 * are 32-bit ints, so the map's hash is trivially fast. */
                struct AtomDispatch {
                    std::unordered_map<JSAtom, ParseFn> map;
                    uint64_t generation = UINT64_MAX;
                };
                static AtomDispatch ad;
                if (ad.generation != Canvas::runtimeGeneration())
                {
                    ad.map.clear();
                    for (auto &kv : dispatch)
                        ad.map[atom_for(ctx, kv.first.c_str())] = kv.second;
                    ad.generation = Canvas::runtimeGeneration();
                }

                /* Enumerate the style object's own keys and dispatch.
                 * Hot loop — no string allocation per key, just an
                 * atom hash-table lookup. */
                JSPropertyEnum *tab = nullptr;
                uint32_t len = 0;
                if (JS_GetOwnPropertyNames(ctx, &tab, &len, style,
                                           JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0)
                {
                    return;
                }
                for (uint32_t i = 0; i < len; i++)
                {
                    auto it = ad.map.find(tab[i].atom);
                    if (it != ad.map.end())
                        it->second(p, out);
                    JS_FreeAtom(ctx, tab[i].atom);
                }
                js_free(ctx, tab);
            }

            JSValue Canvas::create_element(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                /* Use cached atoms for property writes instead of
                 * JS_SetPropertyStr (which interns the string anew
                 * each call). Called once per JSX element — hundreds
                 * of times per render — so this is a hot path. */
                /* Property-name atoms. NOT function-local statics: a static
                 * would latch the first runtime's atom values and survive a
                 * reload, then index the new runtime's atom_array at a stale
                 * slot and crash in JS_DupAtom. atom_for() caches in s_atoms
                 * (cleared on reload by destroy()), so this stays a cheap
                 * hash lookup while remaining reload-safe. */
                const JSAtom a_type       = atom_for(ctx, "type");
                const JSAtom a_properties = atom_for(ctx, "properties");
                const JSAtom a_children   = atom_for(ctx, "children");

                JSValue el = JS_NewObject(ctx);
                JS_SetProperty(ctx, el, a_type, JS_DupValue(ctx, argv[0]));
                JSValue properties = (argc > 1 && JS_IsObject(argv[1])) ? JS_DupValue(ctx, argv[1]) : JS_NewObject(ctx);
                if (argc > 2)
                {
                    JSValue children = JS_NewArray(ctx);
                    for (int i = 2; i < argc; i++)
                        JS_SetPropertyUint32(ctx, children, i - 2, JS_DupValue(ctx, argv[i]));
                    JS_SetProperty(ctx, properties, a_children, children);
                }
                JS_SetProperty(ctx, el, a_properties, properties);
                return el;
            }
            /* JSCapture: tiny RAII for JS values held in C++ lambdas.
             * The QuickJS GC will segfault if we don't free values
             * before context teardown. */
            namespace
            {
                struct JSCapture
                {
                    JSContext *ctx;
                    JSValue val;
                    JSCapture(JSContext *c, JSValue v) : ctx(c), val(v) {}
                    ~JSCapture() { JS_FreeValue(ctx, val); }
                };
            }

            JSValue Canvas::use_state(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                if (!s_active_comp)
                    return JS_ThrowInternalError(ctx, "Hooks can only be called inside a Component function.");

                auto &states = s_states[s_active_comp];
                int idx = s_hook_idx++;
                if (idx >= (int)states.size())
                    states.push_back(JS_DupValue(ctx, argv[0]));

                JSValue res = JS_NewArray(ctx);
                JS_SetPropertyUint32(ctx, res, 0, JS_DupValue(ctx, states[idx]));

                JSValue data[2];
                data[0] = JS_NewInt32(ctx, idx);
                data[1] = JS_NewInt64(ctx, (int64_t)s_active_comp);

                JSValue setter = JS_NewCFunctionData(ctx, [](JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValue *data) -> JSValue
                                                     {
                    int32_t idx; int64_t comp_ptr_int;
                    JS_ToInt32(c, &idx, data[0]);
                    JS_ToInt64(c, &comp_ptr_int, data[1]);
                    void *comp_ptr = (void *)comp_ptr_int;
                    if (argc > 0)
                    {
                        auto &st = s_states[comp_ptr];

                        /* Functional updater: setX(prev => next).
                         * If argv[0] is a function, call it with the
                         * previous state to compute the new state.
                         * Without this, setX(b => b + 1) would store
                         * the FUNCTION as the new state — and any
                         * later String(x) call would render its source
                         * as text. */
                        JSValue newVal;
                        if (JS_IsFunction(c, argv[0]))
                        {
                            JSValue prev = st[idx];
                            newVal = JS_Call(c, argv[0], JS_UNDEFINED, 1, &prev);
                            if (JS_IsException(newVal))
                                return newVal;
                        }
                        else
                        {
                            newVal = JS_DupValue(c, argv[0]);
                        }

                        JS_FreeValue(c, st[idx]);
                        st[idx] = newVal;

                        /* Render batching. Don't render now — mark
                         * dirty and let Canvas::update flush at the
                         * next frame boundary. Multiple setStates in
                         * the same frame collapse to ONE render. */
                        s_render_dirty = true;
                    }
                    return JS_UNDEFINED; }, 1, 0, 2, data);

                JS_SetPropertyUint32(ctx, res, 1, setter);
                return res;
            }

            JSValue Canvas::use_effect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                /* Copied inline to avoid a circular include. */
                if (!s_active_comp)
                    return JS_ThrowInternalError(ctx, "useEffect can only be called inside a component function.");
                if (argc < 1 || !JS_IsFunction(ctx, argv[0]))
                    return JS_ThrowTypeError(ctx, "useEffect requires a callback function as the first argument.");

                auto &slots = s_effects[s_active_comp];
                int idx = s_effect_idx++;

                if (idx >= (int)slots.size())
                {
                    EffectSlot slot;
                    slot.callback = JS_DupValue(ctx, argv[0]);
                    slot.hasRun = false;
                    if (argc >= 2 && JS_IsArray(argv[1]))
                    {
                        uint32_t len = 0;
                        JSValue jsLen = JS_GetPropertyStr(ctx, argv[1], "length");
                        JS_ToUint32(ctx, &len, jsLen);
                        JS_FreeValue(ctx, jsLen);
                        for (uint32_t i = 0; i < len; i++)
                        {
                            JSValue d = JS_GetPropertyUint32(ctx, argv[1], i);
                            slot.deps.push_back(JS_DupValue(ctx, d));
                            JS_FreeValue(ctx, d);
                        }
                    }
                    slots.push_back(std::move(slot));
                    s_pendingEffects.push_back({s_active_comp, idx});
                    return JS_UNDEFINED;
                }

                EffectSlot &slot = slots[idx];
                JS_FreeValue(ctx, slot.callback);
                slot.callback = JS_DupValue(ctx, argv[0]);

                if (argc < 2 || !JS_IsArray(argv[1]))
                {
                    s_pendingEffects.push_back({s_active_comp, idx});
                    return JS_UNDEFINED;
                }
                uint32_t newLen = 0;
                JSValue jsLen = JS_GetPropertyStr(ctx, argv[1], "length");
                JS_ToUint32(ctx, &newLen, jsLen);
                JS_FreeValue(ctx, jsLen);
                if (newLen == 0 && slot.hasRun)
                    return JS_UNDEFINED;

                bool changed = false;
                if (newLen != (uint32_t)slot.deps.size())
                    changed = true;
                else
                {
                    for (uint32_t i = 0; i < newLen; i++)
                    {
                        JSValue newDep = JS_GetPropertyUint32(ctx, argv[1], i);
                        const char *oldStr = JS_ToCString(ctx, slot.deps[i]);
                        const char *newStr = JS_ToCString(ctx, newDep);
                        bool eq = (oldStr && newStr && std::strcmp(oldStr, newStr) == 0);
                        if (oldStr)
                            JS_FreeCString(ctx, oldStr);
                        if (newStr)
                            JS_FreeCString(ctx, newStr);
                        JS_FreeValue(ctx, newDep);
                        if (!eq)
                        {
                            changed = true;
                            break;
                        }
                    }
                }
                if (changed)
                {
                    for (auto &d : slot.deps)
                        JS_FreeValue(ctx, d);
                    slot.deps.clear();
                    for (uint32_t i = 0; i < newLen; i++)
                    {
                        JSValue d = JS_GetPropertyUint32(ctx, argv[1], i);
                        slot.deps.push_back(JS_DupValue(ctx, d));
                        JS_FreeValue(ctx, d);
                    }
                    s_pendingEffects.push_back({s_active_comp, idx});
                }
                return JS_UNDEFINED;
            }

            void Canvas::flush_effects(JSContext *ctx)
            {
                auto pending = std::move(s_pendingEffects);
                s_pendingEffects.clear();
                for (auto &[comp, idx] : pending)
                {
                    auto it = s_effects.find(comp);
                    if (it == s_effects.end() || idx >= (int)it->second.size())
                        continue;
                    EffectSlot &slot = it->second[idx];
                    if (JS_IsFunction(ctx, slot.cleanup))
                    {
                        JSValue ret = JS_Call(ctx, slot.cleanup, JS_UNDEFINED, 0, nullptr);
                        if (JS_IsException(ret))
                            Engine::Instance().reportException("useEffect cleanup");
                        JS_FreeValue(ctx, ret);
                        JS_FreeValue(ctx, slot.cleanup);
                        slot.cleanup = JS_UNDEFINED;
                    }
                    JSValue ret = JS_Call(ctx, slot.callback, JS_UNDEFINED, 0, nullptr);
                    if (JS_IsException(ret))
                        Engine::Instance().reportException("useEffect callback");
                    else if (JS_IsFunction(ctx, ret))
                        slot.cleanup = ret;
                    else
                        JS_FreeValue(ctx, ret);
                    slot.hasRun = true;
                }
            }

            /* Bind a JS callback to a void() functional slot, with
             *    proper retain + release on node teardown. */
            void Canvas::bind_callback_void(JSContext *ctx, JSValue cb,
                                            std::function<void()> &outSlot,
                                            std::shared_ptr<Bokken::Canvas::Node> &node)
            {
                if (!JS_IsFunction(ctx, cb))
                    return;
                JSValue captured = JS_DupValue(ctx, cb);
                outSlot = [captured]()
                {
                    auto &engine = Engine::Instance();
                    if (!engine.isReady())
                        return;
                    JSContext *liveCtx = engine.context();
                    JSValue ret = JS_Call(liveCtx, captured, JS_UNDEFINED, 0, nullptr);
                    if (JS_IsException(ret))
                        engine.reportException("Canvas event handler");
                    JS_FreeValue(liveCtx, ret);
                    /* Drain microtasks queued by the handler so they
                     * fire before the next macrotask, per WHATWG. */
                    engine.drainJobQueue();
                };
                /* Chain onto clearJsBindings (NOT onDeconstruct) so
                 * the reconciler can release these JSValues on a
                 * node-reuse without firing component-internal
                 * cleanup. The Node destructor calls both. */
                auto prevClear = node->clearJsBindings;
                node->clearJsBindings = [prevClear, captured]()
                {
                    if (prevClear)
                        prevClear();
                    auto &engine = Engine::Instance();
                    if (engine.isReady())
                        JS_FreeValue(engine.context(), captured);
                };
            }

            void Canvas::bind_callback_str(JSContext *ctx, JSValue cb,
                                           std::function<void(const std::string &)> &outSlot,
                                           std::shared_ptr<Bokken::Canvas::Node> &node)
            {
                if (!JS_IsFunction(ctx, cb))
                    return;
                JSValue captured = JS_DupValue(ctx, cb);
                outSlot = [captured](const std::string &s)
                {
                    auto &engine = Engine::Instance();
                    if (!engine.isReady())
                        return;
                    JSContext *liveCtx = engine.context();
                    JSValue arg = JS_NewString(liveCtx, s.c_str());
                    JSValue ret = JS_Call(liveCtx, captured, JS_UNDEFINED, 1, &arg);
                    if (JS_IsException(ret))
                        engine.reportException("Canvas onChange handler");
                    JS_FreeValue(liveCtx, ret);
                    JS_FreeValue(liveCtx, arg);
                    engine.drainJobQueue();
                };
                auto prevClear = node->clearJsBindings;
                node->clearJsBindings = [prevClear, captured]()
                {
                    if (prevClear)
                        prevClear();
                    auto &engine = Engine::Instance();
                    if (engine.isReady())
                        JS_FreeValue(engine.context(), captured);
                };
            }

            std::shared_ptr<Bokken::Canvas::Node> Canvas::synchronize_tree(
                JSContext *ctx, JSValue val,
                std::shared_ptr<Bokken::Canvas::Node> prev)
            {
                auto &engine = Engine::Instance();

                /* Tolerate null / undefined / false / non-objects.
                 *
                 * Real React silently drops null, undefined, false, and
                 * empty-string children — they're useful as "render
                 * nothing here" sentinels. Bailing early to nullptr
                 * matches that semantic; the caller treats nullptr as
                 * "no child to add". */
                if (!JS_IsObject(val))
                    return nullptr;

                /* Pre-cache the atoms we need from the JSX element. */
                /* Reload-safe property atoms — see create_element for why
                 * these must not be function-local statics. */
                const JSAtom atom_type       = atom_for(ctx, "type");
                const JSAtom atom_properties = atom_for(ctx, "properties");
                const JSAtom atom_style      = atom_for(ctx, "style");
                const JSAtom atom_children   = atom_for(ctx, "children");
                const JSAtom atom_length     = atom_for(ctx, "length");
                const JSAtom atom_src        = atom_for(ctx, "src");
                const JSAtom atom_placeholder= atom_for(ctx, "placeholder");
                const JSAtom atom_value      = atom_for(ctx, "value");
                const JSAtom atom_onClick    = atom_for(ctx, "onClick");
                const JSAtom atom_onMouseEnter = atom_for(ctx, "onMouseEnter");
                const JSAtom atom_onMouseLeave = atom_for(ctx, "onMouseLeave");
                const JSAtom atom_onFocus    = atom_for(ctx, "onFocus");
                const JSAtom atom_onBlur     = atom_for(ctx, "onBlur");
                const JSAtom atom_onChange   = atom_for(ctx, "onChange");
                const JSAtom atom_onScroll   = atom_for(ctx, "onScroll");

                /* Validate type. */
                JSValue type = JS_GetProperty(ctx, val, atom_type);
                if (JS_IsException(type))
                    return nullptr;

                if (JS_IsFunction(ctx, type))
                {
                    /* Functional component — recurse into its return.
                     * Hook state is keyed by the component function
                     * pointer. Pass prev through to the recursive call
                     * so the host element the function returns can be
                     * reconciled against the existing tree. */
                    void *prev_comp = s_active_comp;
                    int prev_idx = s_hook_idx;
                    int prev_effect_idx = s_effect_idx;

                    s_active_comp = JS_VALUE_GET_PTR(type);
                    s_hook_idx = 0;
                    s_effect_idx = 0;

                    JSValue properties = JS_GetProperty(ctx, val, atom_properties);
                    JSValue result = JS_Call(ctx, type, JS_UNDEFINED, 1, &properties);
                    if (JS_IsException(result))
                    {
                        engine.reportException("Functional component");
                        JS_FreeValue(ctx, properties);
                        JS_FreeValue(ctx, type);
                        return nullptr;
                    }
                    auto node = synchronize_tree(ctx, result, prev);
                    JS_FreeValue(ctx, properties);
                    JS_FreeValue(ctx, result);
                    JS_FreeValue(ctx, type);
                    s_active_comp = prev_comp;
                    s_hook_idx = prev_idx;
                    s_effect_idx = prev_effect_idx;
                    return node;
                }

                const char *tStr = JS_ToCString(ctx, type);
                std::string typeName = tStr ? tStr : "View";
                JS_FreeCString(ctx, tStr);
                JS_FreeValue(ctx, type);

                /* Reconciliation decision: reuse `prev` if its type
                 * matches the new JSX type. Otherwise allocate fresh.
                 *
                 * Reusing means:
                 *   - same shared_ptr (no allocation)
                 *   - clear stale state: callbacks, textContent, style
                 *   - clear children list (we'll re-build via recursive
                 *     reconciliation below, pairing new JSX children
                 *     with prev's children by index)
                 *   - re-parse properties INTO the existing Node
                 *
                 * The big perf win: no make_shared call, no fresh
                 * SimpleStyleSheet allocation, AND the existing Node
                 * keeps its isHovered/isActive/isFocused state across
                 * the render — no flicker, no lost interaction state. */
                std::shared_ptr<Bokken::Canvas::Node> node;
                std::vector<std::shared_ptr<Bokken::Canvas::Node>> prevChildren;

                if (prev && prev->type == typeName)
                {
                    node = prev;
                    /* Stash prev's children before we wipe them — we
                     * need them for child-by-index reconciliation. */
                    prevChildren = std::move(node->children);
                    node->children.clear();

                    /* Release JSValues captured by the previous render's
                     * event handlers. Without this they'd leak forever
                     * because we're about to overwrite the callback
                     * slots without their destructors firing. */
                    if (node->clearJsBindings)
                    {
                        node->clearJsBindings();
                        node->clearJsBindings = nullptr;
                    }

                    /* Clear callback slots so binding doesn't see stale
                     * std::function instances. */
                    node->onClick = nullptr;
                    node->onMouseEnter = nullptr;
                    node->onMouseLeave = nullptr;
                    node->onFocus = nullptr;
                    node->onBlur = nullptr;
                    node->onChange = nullptr;
                    node->onScroll = nullptr;

                    /* Reset style to default. parse_simple_style_sheet
                     * only writes properties present in the JSX style
                     * object — without resetting first, a previously-
                     * set property that was removed in this render
                     * would persist (the "stuck color" anti-pattern). */
                    node->style = Bokken::Canvas::SimpleStyleSheet{};

                    /* Clear text content; it'll be re-set below for
                     * Label/TextInput nodes. */
                    if (typeName == "Label")
                        node->textContent.clear();
                }
                else
                {
                    node = std::make_shared<Bokken::Canvas::Node>(typeName);
                }

                JSValue properties = JS_GetProperty(ctx, val, atom_properties);
                if (!JS_IsObject(properties))
                {
                    JS_FreeValue(ctx, properties);
                    return node;
                }

                /* Style. */
                JSValue style = JS_GetProperty(ctx, properties, atom_style);
                parse_simple_style_sheet(ctx, style, node->style);
                JS_FreeValue(ctx, style);

                /* Component-specific top-level props. */
                if (typeName == "Image")
                {
                    JSValue src = JS_GetProperty(ctx, properties, atom_src);
                    if (JS_IsString(src))
                    {
                        const char *str = JS_ToCString(ctx, src);
                        if (str)
                        {
                            node->imageSource = str;
                            JS_FreeCString(ctx, str);
                        }
                    }
                    JS_FreeValue(ctx, src);
                }
                else if (typeName == "TextInput")
                {
                    JSValue placeholder = JS_GetProperty(ctx, properties, atom_placeholder);
                    if (JS_IsString(placeholder))
                    {
                        const char *str = JS_ToCString(ctx, placeholder);
                        if (str)
                        {
                            Bokken::Canvas::Components::TextInput::setPlaceholderFor(node, str);
                            JS_FreeCString(ctx, str);
                        }
                    }
                    JS_FreeValue(ctx, placeholder);

                    JSValue jsValue = JS_GetProperty(ctx, properties, atom_value);
                    if (JS_IsString(jsValue))
                    {
                        const char *str = JS_ToCString(ctx, jsValue);
                        if (str)
                        {
                            node->value = str;
                            node->textContent = str;
                            JS_FreeCString(ctx, str);
                        }
                    }
                    JS_FreeValue(ctx, jsValue);
                }

                /* Children — same dispatch as before, but pairing each
                 * new JSX child with the corresponding prev child by
                 * index for reconciliation. */
                JSValue children = JS_GetProperty(ctx, properties, atom_children);
                if (typeName == "Label" || typeName == "TextInput")
                {
                    /* Label/TextInput consume children as text content,
                     * not as nested nodes. */
                    if (JS_IsString(children) || JS_IsNumber(children) || JS_IsBool(children))
                    {
                        JSValue strVal = JS_ToString(ctx, children);
                        if (!JS_IsException(strVal))
                        {
                            const char *t = JS_ToCString(ctx, strVal);
                            if (typeName == "Label")
                                node->textContent = t ? t : "";
                            JS_FreeCString(ctx, t);
                        }
                        JS_FreeValue(ctx, strVal);
                    }
                    else if (JS_IsArray(children))
                    {
                        uint32_t len = 0;
                        JSValue jsLen = JS_GetProperty(ctx, children, atom_length);
                        JS_ToUint32(ctx, &len, jsLen);
                        JS_FreeValue(ctx, jsLen);
                        std::string flat;
                        for (uint32_t i = 0; i < len; i++)
                        {
                            JSValue c = JS_GetPropertyUint32(ctx, children, i);
                            JSValue strVal = JS_ToString(ctx, c);
                            if (!JS_IsException(strVal))
                            {
                                const char *t = JS_ToCString(ctx, strVal);
                                if (t)
                                {
                                    flat += t;
                                    JS_FreeCString(ctx, t);
                                }
                            }
                            JS_FreeValue(ctx, strVal);
                            JS_FreeValue(ctx, c);
                        }
                        if (typeName == "Label")
                            node->textContent = flat;
                    }
                }
                else if (JS_IsArray(children))
                {
                    /* Reconcile children by index. childCursor tracks
                     * our position in prevChildren; for each new JSX
                     * child we pair it with prevChildren[childCursor]
                     * and recurse. Nested arrays (from .map output)
                     * are flattened in-line. */
                    size_t childCursor = 0;

                    auto reconcileChild = [&](JSValue inner)
                    {
                        std::shared_ptr<Bokken::Canvas::Node> prevChild =
                            childCursor < prevChildren.size()
                                ? prevChildren[childCursor]
                                : nullptr;
                        childCursor++;
                        auto childNode = synchronize_tree(ctx, inner, prevChild);
                        if (childNode)
                            node->add_child(childNode);
                    };

                    uint32_t len = 0;
                    JSValue jsLen = JS_GetProperty(ctx, children, atom_length);
                    JS_ToUint32(ctx, &len, jsLen);
                    JS_FreeValue(ctx, jsLen);
                    for (uint32_t i = 0; i < len; i++)
                    {
                        JSValue c = JS_GetPropertyUint32(ctx, children, i);
                        if (JS_IsArray(c))
                        {
                            uint32_t innerLen = 0;
                            JSValue jsInnerLen = JS_GetProperty(ctx, c, atom_length);
                            JS_ToUint32(ctx, &innerLen, jsInnerLen);
                            JS_FreeValue(ctx, jsInnerLen);
                            for (uint32_t j = 0; j < innerLen; j++)
                            {
                                JSValue inner = JS_GetPropertyUint32(ctx, c, j);
                                if (JS_IsObject(inner))
                                    reconcileChild(inner);
                                JS_FreeValue(ctx, inner);
                            }
                        }
                        else if (JS_IsObject(c))
                        {
                            reconcileChild(c);
                        }
                        JS_FreeValue(ctx, c);
                    }
                }
                JS_FreeValue(ctx, children);

                /* Bind events */
                auto bindVoid = [&](JSAtom atom, std::function<void()> &slot)
                {
                    JSValue cb = JS_GetProperty(ctx, properties, atom);
                    bind_callback_void(ctx, cb, slot, node);
                    JS_FreeValue(ctx, cb);
                };
                auto bindStr = [&](JSAtom atom, std::function<void(const std::string &)> &slot)
                {
                    JSValue cb = JS_GetProperty(ctx, properties, atom);
                    bind_callback_str(ctx, cb, slot, node);
                    JS_FreeValue(ctx, cb);
                };
                bindVoid(atom_onClick, node->onClick);
                bindVoid(atom_onMouseEnter, node->onMouseEnter);
                bindVoid(atom_onMouseLeave, node->onMouseLeave);
                bindVoid(atom_onFocus, node->onFocus);
                bindVoid(atom_onBlur, node->onBlur);
                bindStr(atom_onChange, node->onChange);

                /* onScroll has a (dx, dy) signature — bind specially. */
                {
                    JSValue cb = JS_GetProperty(ctx, properties, atom_onScroll);
                    if (JS_IsFunction(ctx, cb))
                    {
                        JSValue captured = JS_DupValue(ctx, cb);
                        node->onScroll = [captured](float x, float y)
                        {
                            auto &engine = Engine::Instance();
                            if (!engine.isReady())
                                return;
                            JSContext *liveCtx = engine.context();
                            JSValue args[2] = {JS_NewFloat64(liveCtx, x), JS_NewFloat64(liveCtx, y)};
                            JSValue ret = JS_Call(liveCtx, captured, JS_UNDEFINED, 2, args);
                            if (JS_IsException(ret))
                                engine.reportException("onScroll");
                            JS_FreeValue(liveCtx, ret);
                            JS_FreeValue(liveCtx, args[0]);
                            JS_FreeValue(liveCtx, args[1]);
                            engine.drainJobQueue();
                        };
                        auto prevClear = node->clearJsBindings;
                        node->clearJsBindings = [prevClear, captured]()
                        {
                            if (prevClear)
                                prevClear();
                            auto &engine = Engine::Instance();
                            if (engine.isReady())
                                JS_FreeValue(engine.context(), captured);
                        };
                    }
                    JS_FreeValue(ctx, cb);
                }

                JS_FreeValue(ctx, properties);

                /* Wire component lifecycle. Only do this on FRESH
                 * nodes — reused nodes already have these wired from
                 * their original allocation. */
                if (node != prev)
                {
                    if (typeName == "Label")
                        node->onCompute = &Bokken::Canvas::Components::Label::computeNode;
                    else if (typeName == "Image")
                        node->onCompute = &Bokken::Canvas::Components::Image::computeNode;
                    else if (typeName == "TextInput")
                    {
                        node->onCompute = &Bokken::Canvas::Components::TextInput::computeNode;
                        Bokken::Canvas::Components::TextInput::applyDefaults(node->style);
                    }
                    else if (typeName == "ScrollView")
                    {
                        node->onLayout = &Bokken::Canvas::Components::ScrollView::layoutNode;
                        node->style.overflow = Bokken::Canvas::Overflow::Hidden;
                    }
                    else if (typeName == "Button")
                        Bokken::Canvas::Components::Button::applyDefaults(node->style);
                }
                else
                {
                    /* Reused nodes still need their style defaults
                     * re-applied on every render (we wiped style above). */
                    if (typeName == "TextInput")
                        Bokken::Canvas::Components::TextInput::applyDefaults(node->style);
                    else if (typeName == "ScrollView")
                        node->style.overflow = Bokken::Canvas::Overflow::Hidden;
                    else if (typeName == "Button")
                        Bokken::Canvas::Components::Button::applyDefaults(node->style);
                }

                return node;
            }

            /* Tree drawing
             *
             * Walks the tree and emits batcher quads. The walker is
             * what handles overflow:Hidden (scissor push/pop) and
             * ScrollView's child translation — keeping that policy at
             * the walker level means individual components don't have
             * to repeat the same boilerplate.
             *
             * Layer numbers
             *   Each descent step adds 4 to the layer. The wide gap leaves room for a single node to emit:
             *     +0  shadow
             *     +1  background / gradient / image
             *     +2  border
             *     +3  scrollbar / focus ring
             *   …without colliding with children's layers.
             */
            namespace
            {
                constexpr int kLayerStep = 4;
            }

            void drawNode(Bokken::Renderer::SpriteBatcher &batcher,
                          std::shared_ptr<Bokken::Canvas::Node> node, int layer)
            {
                if (!node)
                    return;

                /* Per-component draw. ScrollView and TextInput use their
                 * own draw because they need extra geometry beyond what
                 * a plain View emits. */
                if (node->type == "Image")
                {
                    Bokken::Canvas::Components::Image::draw(batcher, node, Canvas::s_assets, layer);
                }
                else if (node->type == "TextInput")
                {
                    Bokken::Canvas::Components::TextInput::draw(batcher, node, Canvas::s_assets, layer);
                }
                else if (node->type == "ScrollView")
                {
                    Bokken::Canvas::Components::ScrollView::draw(batcher, node, layer);
                }
                else if (node->type == "Label")
                {
                    Bokken::Canvas::Components::Label::draw(batcher, node, Canvas::s_assets, layer);
                }
                else
                {
                    /* View, Button, anything else — same draw path. */
                    Bokken::Canvas::Components::View::draw(batcher, node, layer);
                }

                /* Stop here if there are no children. */
                if (node->children.empty())
                    return;

                const auto &s = node->style;
                const bool clip = (s.overflow == Bokken::Canvas::Overflow::Hidden);
                const bool scrolls = (node->type == "ScrollView" &&
                                      (node->scrollX != 0.0f || node->scrollY != 0.0f));

                if (clip)
                {
                    /* Push scissor. The clip rect is the content box —
                     * padding-inset, in pixel coords. */
                    const float pT = Bokken::Canvas::resolveSide(s.paddingTop, s.padding);
                    const float pB = Bokken::Canvas::resolveSide(s.paddingBottom, s.padding);
                    const float pL = Bokken::Canvas::resolveSide(s.paddingLeft, s.padding);
                    const float pR = Bokken::Canvas::resolveSide(s.paddingRight, s.padding);
                    int sx = (int)std::round(node->layout.x + pL);
                    int sy = (int)std::round(node->layout.y + pT);
                    int sw = (int)std::round(node->layout.w - pL - pR);
                    int sh = (int)std::round(node->layout.h - pT - pB);
                    batcher.pushScissor(sx, sy, std::max(0, sw), std::max(0, sh));
                }

                /* When scrolling, each child's render uses its layout
                 * minus the scroll offset. We mutate layout.x/y on
                 * the WHOLE SUBTREE for the duration of the child
                 * draw, then restore. Shifting only the immediate
                 * child broke grandchildren — they store absolute
                 * coordinates, so they need the same offset applied
                 * for things to draw consistently. */
                int childLayer = layer + kLayerStep;

                std::function<void(std::shared_ptr<Bokken::Canvas::Node>, float, float)> shiftSubtree;
                shiftSubtree = [&](std::shared_ptr<Bokken::Canvas::Node> n, float dx, float dy)
                {
                    if (!n)
                        return;
                    n->layout.x += dx;
                    n->layout.y += dy;
                    for (auto &c : n->children)
                        shiftSubtree(c, dx, dy);
                };

                for (auto &child : node->children)
                {
                    if (scrolls)
                    {
                        const float dx = -node->scrollX;
                        const float dy = -node->scrollY;
                        shiftSubtree(child, dx, dy);
                        drawNode(batcher, child, childLayer);
                        shiftSubtree(child, -dx, -dy);
                    }
                    else
                    {
                        drawNode(batcher, child, childLayer);
                    }
                }

                if (clip)
                    batcher.popScissor();
            }

            /* Hit testing
             *
             * `x, y` are screen coordinates. As we descend into a
             * ScrollView, we translate the query point by the
             * ScrollView's scroll offset so children are tested in
             * their pre-scroll layout coords (which is what they're
             * stored in). The clip check (using the ScrollView's own
             * untranslated rect) ensures we don't return children
             * that are scrolled out of view. */
            namespace
            {
                std::shared_ptr<Bokken::Canvas::Node> find_node_at_impl(
                    std::shared_ptr<Bokken::Canvas::Node> root,
                    std::shared_ptr<Bokken::Canvas::Node> tree_root,
                    float x, float y)
                {
                    if (!root)
                        return nullptr;

                    /* Clip: if root has overflow:Hidden and the point
                     * is outside its visible rect, descend no further.
                     * (Children may be positioned outside the parent's
                     * rect under scroll/overflow; we shouldn't hit
                     * them at all if they're clipped away visually.) */
                    if (root->style.overflow == Bokken::Canvas::Overflow::Hidden)
                    {
                        bool inside = (x >= root->layout.x && x <= root->layout.x + root->layout.w &&
                                       y >= root->layout.y && y <= root->layout.y + root->layout.h);
                        if (!inside)
                            return nullptr;
                    }

                    /* Translate for ScrollView descent. */
                    float childX = x;
                    float childY = y;
                    if (root->type == "ScrollView")
                    {
                        childX += root->scrollX;
                        childY += root->scrollY;
                    }

                    /* Children first (deepest hit wins). */
                    for (auto it = root->children.rbegin(); it != root->children.rend(); ++it)
                    {
                        auto hit = find_node_at_impl(*it, tree_root, childX, childY);
                        if (hit)
                            return hit;
                    }

                    /* Use the original (untranslated) point against
                     * root's own rect. */
                    bool isInside = (x >= root->layout.x && x <= root->layout.x + root->layout.w &&
                                     y >= root->layout.y && y <= root->layout.y + root->layout.h);
                    if (!isInside)
                        return nullptr;

                    /* Bubble up to find the responsible interactive
                     * ancestor. */
                    Bokken::Canvas::Node *cur = root.get();
                    while (cur)
                    {
                        if (cur->onClick || cur->onMouseEnter || cur->onMouseLeave ||
                            cur->style.cursor != Bokken::Canvas::Cursor::Default ||
                            cur->type == "TextInput" || cur->type == "Button")
                        {
                            /* Re-find shared_ptr for `cur` by descending
                             * from the tree root with pointer equality. */
                            std::function<std::shared_ptr<Bokken::Canvas::Node>(
                                std::shared_ptr<Bokken::Canvas::Node>)>
                                findShared =
                                    [&](std::shared_ptr<Bokken::Canvas::Node> n)
                                -> std::shared_ptr<Bokken::Canvas::Node>
                            {
                                if (!n)
                                    return nullptr;
                                if (n.get() == cur)
                                    return n;
                                for (auto &c : n->children)
                                {
                                    auto r = findShared(c);
                                    if (r)
                                        return r;
                                }
                                return nullptr;
                            };
                            auto sp = findShared(tree_root);
                            if (sp)
                                return sp;
                            return nullptr;
                        }
                        cur = cur->parent;
                    }
                    return nullptr;
                }
            }

            std::shared_ptr<Bokken::Canvas::Node> Canvas::find_node_at(
                std::shared_ptr<Bokken::Canvas::Node> root, float x, float y)
            {
                return find_node_at_impl(root, root, x, y);
            }

            /* Find the deepest ScrollView under (x,y). Wheel events go
             * here. Mirrors the scroll-aware descent of find_node_at_impl. */
            std::shared_ptr<Bokken::Canvas::Node> Canvas::find_scroll_at(
                std::shared_ptr<Bokken::Canvas::Node> root, float x, float y)
            {
                if (!root)
                    return nullptr;

                if (root->style.overflow == Bokken::Canvas::Overflow::Hidden)
                {
                    bool inside = (x >= root->layout.x && x <= root->layout.x + root->layout.w &&
                                   y >= root->layout.y && y <= root->layout.y + root->layout.h);
                    if (!inside)
                        return nullptr;
                }

                float childX = x;
                float childY = y;
                if (root->type == "ScrollView")
                {
                    childX += root->scrollX;
                    childY += root->scrollY;
                }
                for (auto it = root->children.rbegin(); it != root->children.rend(); ++it)
                {
                    auto hit = find_scroll_at(*it, childX, childY);
                    if (hit)
                        return hit;
                }

                bool isInside = (x >= root->layout.x && x <= root->layout.x + root->layout.w &&
                                 y >= root->layout.y && y <= root->layout.y + root->layout.h);
                if (isInside && root->type == "ScrollView")
                    return root;
                return nullptr;
            }

            /* Focus management */
            void Canvas::collect_focusables(std::shared_ptr<Bokken::Canvas::Node> root,
                                            std::vector<std::shared_ptr<Bokken::Canvas::Node>> &out)
            {
                if (!root)
                    return;
                if (root->style.tabIndex >= 0 && !root->style.disabled)
                    out.push_back(root);
                for (auto &c : root->children)
                    collect_focusables(c, out);
            }

            void Canvas::set_focus(std::shared_ptr<Bokken::Canvas::Node> node)
            {
                if (s_focused_node == node)
                    return;
                if (s_focused_node)
                {
                    s_focused_node->isFocused = false;
                    if (s_focused_node->onBlur)
                        s_focused_node->onBlur();
                    if (s_focused_node->type == "TextInput")
                        SDL_StopTextInput(s_window);
                }
                s_focused_node = node;
                if (s_focused_node)
                {
                    s_focused_node->isFocused = true;
                    if (s_focused_node->onFocus)
                        s_focused_node->onFocus();
                    if (s_focused_node->type == "TextInput")
                        SDL_StartTextInput(s_window);
                }
            }

            void Canvas::cycle_focus(int direction)
            {
                if (!s_current_tree)
                    return;
                std::vector<std::shared_ptr<Bokken::Canvas::Node>> focusables;
                collect_focusables(s_current_tree, focusables);
                if (focusables.empty())
                    return;

                /* Sort by (tabIndex, document order). Document order is
                 * already the collection order. tabIndex==0 comes after
                 * any positive tabIndex per the web rule. */
                std::stable_sort(focusables.begin(), focusables.end(),
                                 [](const auto &a, const auto &b)
                                 {
                                     int ta = a->style.tabIndex;
                                     int tb = b->style.tabIndex;
                                     if (ta == 0 && tb != 0)
                                         return false;
                                     if (tb == 0 && ta != 0)
                                         return true;
                                     return ta < tb;
                                 });

                int idx = -1;
                for (size_t i = 0; i < focusables.size(); i++)
                    if (focusables[i] == s_focused_node)
                    {
                        idx = (int)i;
                        break;
                    }
                int next = (idx == -1)
                               ? (direction > 0 ? 0 : (int)focusables.size() - 1)
                               : ((idx + direction + (int)focusables.size()) % (int)focusables.size());
                set_focus(focusables[next]);
            }

            /* Cursor management */
            void Canvas::update_cursor_for_hover()
            {
                Bokken::Canvas::Cursor want = Bokken::Canvas::Cursor::Default;
                if (s_hovered_node)
                    want = s_hovered_node->style.cursor;
                if (want == s_lastCursor)
                    return;
                s_lastCursor = want;

                /* Lazy-init system cursors. */
                static const SDL_SystemCursor map[] = {
                    SDL_SYSTEM_CURSOR_DEFAULT, SDL_SYSTEM_CURSOR_POINTER,
                    SDL_SYSTEM_CURSOR_TEXT, SDL_SYSTEM_CURSOR_MOVE,
                    SDL_SYSTEM_CURSOR_NOT_ALLOWED, SDL_SYSTEM_CURSOR_WAIT,
                    SDL_SYSTEM_CURSOR_NS_RESIZE, SDL_SYSTEM_CURSOR_EW_RESIZE,
                    SDL_SYSTEM_CURSOR_CROSSHAIR};
                int idx = (int)want;
                if (idx < 0 || idx >= (int)(sizeof(map) / sizeof(map[0])))
                    idx = 0;
                if (!s_cursors[idx])
                    s_cursors[idx] = SDL_CreateSystemCursor(map[idx]);
                if (s_cursors[idx])
                    SDL_SetCursor(s_cursors[idx]);
            }

            /* Caret blink */
            void Canvas::update_caret_blink(std::shared_ptr<Bokken::Canvas::Node> node, float dt)
            {
                /* TextInput's caret blink is stored in the static
                 * blinkMap inside TextInput.cpp — we don't have direct
                 * access here, so we increment via the public API.
                 * Cheap: 1-second cycle, wrap when crossing 1.0. */
                (void)node;
                (void)dt;
                /* The blink update runs on every focused TextInput
                 * inside update(). We piggyback on update_node_animations
                 * which already walks the tree. */
            }

            /* Animation update */
            void Canvas::update_node_animations(std::shared_ptr<Bokken::Canvas::Node> node, float dt)
            {
                if (!node)
                    return;

                /* Caret blink — only the focused TextInput needs this,
                 * and TextInput::tickBlink no-ops on unfocused nodes
                 * so the per-node call is cheap. */
                if (node->type == "TextInput")
                    Bokken::Canvas::Components::TextInput::tickBlink(node, dt);

                float goal = 1.0f;
                if (node->onClick != nullptr || node->type == "Button")
                {
                    if (node->isActive)
                        goal = node->style.activeScale;
                    else if (node->isHovered)
                        goal = node->style.hoverScale;
                    else
                        goal = 1.0f;
                }
                else if (node->parent != nullptr)
                {
                    goal = node->parent->visualScale;
                }
                /* Fast path: if we're already at goal AND not mid-
                 * animation, there's nothing to compute. The vast
                 * majority of nodes hit this on every frame — a
                 * full-tree walk is unavoidable but skipping the
                 * lerp/easing math saves real cycles. */
                const bool atGoal = std::abs(goal - node->visualScale) < 0.001f;
                const bool midAnim = node->animationTimer < 1.0f && node->style.transitionDuration > 0.0f;
                if (!atGoal || midAnim)
                {
                    if (std::abs(goal - node->targetScale) > 0.001f)
                    {
                        node->startScale = node->visualScale;
                        node->targetScale = goal;
                        node->animationTimer = 0.0f;
                    }
                    if (node->style.transitionDuration <= 0.0f)
                        node->visualScale = node->targetScale;
                    else if (node->animationTimer < 1.0f)
                    {
                        float duration = std::max(node->style.transitionDuration, 0.0001f);
                        node->animationTimer += dt / duration;
                        if (node->animationTimer > 1.0f)
                            node->animationTimer = 1.0f;
                        float t = apply_easing(node->style.transitionTiming, node->animationTimer);
                        node->visualScale = node->startScale + (node->targetScale - node->startScale) * t;
                    }
                    else
                        node->visualScale = node->targetScale;
                }

                for (const auto &child : node->children)
                    if (child)
                        update_node_animations(child, dt);
            }

            void Canvas::reset_active_states(std::shared_ptr<Bokken::Canvas::Node> node)
            {
                if (!node)
                    return;
                node->isActive = false;
                for (auto &child : node->children)
                    reset_active_states(child);
            }

            float Canvas::apply_easing(Bokken::Canvas::Timing func, float t)
            {
                switch (func)
                {
                case Bokken::Canvas::Timing::EaseIn:
                    return t * t;
                case Bokken::Canvas::Timing::EaseOut:
                    return t * (2.0f - t);
                case Bokken::Canvas::Timing::EaseInOut:
                    return t < 0.5f ? 2 * t * t : -1 + (4 - 2 * t) * t;
                case Bokken::Canvas::Timing::Bounce:
                    if (t < (1.0f / 2.75f))
                        return 7.5625f * t * t;
                    return 1.0f;
                case Bokken::Canvas::Timing::Back:
                    return t * t * (2.7f * t - 1.7f);
                case Bokken::Canvas::Timing::Step:
                    return t < 1.0f ? 0.0f : 1.0f;
                case Bokken::Canvas::Timing::Linear:
                default:
                    return t;
                }
            }

            void Canvas::markLabelsDirty(std::shared_ptr<Bokken::Canvas::Node> node)
            {
                if (!node)
                    return;
                if (node->type == "Label")
                    node->needsRepaint = true;
                for (auto &child : node->children)
                    markLabelsDirty(child);
            }

            void Canvas::forceRelayout()
            {
                if (!s_current_tree)
                    return;
                markLabelsDirty(s_current_tree);
                int w = 0, h = 0;
                viewport(w, h);
                Bokken::Canvas::Layout::run(s_current_tree, (float)w, (float)h, s_assets);
            }

            /* Event dispatch */
            void Canvas::handleEvent(const SDL_Event &e)
            {
                if (!s_current_tree)
                    return;

                // Relayout on resize is driven by the renderer's
                // render-size observer (see Canvas::attach +
                // onRendererRenderSizeChanged), not by window-resize
                // events here. It fires only when the render target
                // actually changes, which correctly stays silent
                // under the Fixed policy where the OS window can
                // resize without the render size changing.

                if (e.type == SDL_EVENT_WINDOW_MOUSE_LEAVE)
                {
                    if (s_hovered_node)
                    {
                        s_hovered_node->isHovered = false;
                        if (s_hovered_node->onMouseLeave)
                            s_hovered_node->onMouseLeave();
                    }
                    s_hovered_node = nullptr;
                    update_cursor_for_hover();
                    return;
                }

                if (e.type == SDL_EVENT_MOUSE_MOTION)
                {
                    // Convert OS-space mouse coords (logical px) into
                    // render space once at branch entry. Every hit
                    // test below works against laid-out rects in
                    // render space, so this is the single point of
                    // truth for the conversion.
                    float mx = e.motion.x, my = e.motion.y;
                    screen_to_render(mx, my);

                    /* Drag-to-resize on TextInput: walk the tree once
                     * looking for the input that owns the active drag.
                     * Cheap because there's at most one drag in flight. */
                    {
                        bool handled = false;
                        std::function<void(std::shared_ptr<Bokken::Canvas::Node>)> findResizing;
                        findResizing = [&](std::shared_ptr<Bokken::Canvas::Node> n)
                        {
                            if (handled || !n)
                                return;
                            if (n->type == "TextInput" &&
                                Bokken::Canvas::Components::TextInput::isResizing(n))
                            {
                                Bokken::Canvas::Components::TextInput::dragResize(
                                    n, mx, my);
                                handled = true;
                                return;
                            }
                            for (auto &c : n->children)
                                findResizing(c);
                        };
                        findResizing(s_current_tree);
                        if (handled)
                            return;
                    }

                    /* Drag-to-scroll: if a ScrollView is being dragged,
                     * forward every motion event to it regardless of
                     * cursor position. The drag has captured the
                     * mouse for as long as the button is held. */
                    auto draggedSV = Bokken::Canvas::Components::ScrollView::draggingNode();
                    if (draggedSV)
                    {
                        Bokken::Canvas::Components::ScrollView::onMouseMove(
                            draggedSV, mx, my);
                        return;
                    }

                    /* Update scrollbar hover (for thumb darkening) on
                     * the deepest ScrollView under the cursor — even
                     * if a deeper non-scroll node owns the hover. */
                    auto svHover = find_scroll_at(s_current_tree, mx, my);
                    if (svHover)
                        Bokken::Canvas::Components::ScrollView::onMouseMove(
                            svHover, mx, my);

                    auto hit = find_node_at(s_current_tree, mx, my);
                    if (hit != s_hovered_node)
                    {
                        if (s_hovered_node)
                        {
                            s_hovered_node->isHovered = false;
                            if (s_hovered_node->onMouseLeave)
                                s_hovered_node->onMouseLeave();
                        }
                        s_hovered_node = hit;
                        if (s_hovered_node)
                        {
                            s_hovered_node->isHovered = true;
                            if (s_hovered_node->onMouseEnter)
                                s_hovered_node->onMouseEnter();
                        }
                        update_cursor_for_hover();
                    }
                    return;
                }

                if (e.type == SDL_EVENT_MOUSE_WHEEL)
                {
                    /* Route to deepest ScrollView under cursor.
                     * SDL_GetMouseState returns logical-window pixels;
                     * convert into render space before hit-testing.
                     * e.wheel.x/y are scroll deltas (not coordinates)
                     * and are passed through unchanged. */
                    float mx = 0.0f, my = 0.0f;
                    SDL_GetMouseState(&mx, &my);
                    screen_to_render(mx, my);
                    auto sv = find_scroll_at(s_current_tree, mx, my);
                    if (sv)
                        Bokken::Canvas::Components::ScrollView::onWheel(sv, e.wheel.x, e.wheel.y);
                    return;
                }

                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                {
                    // Convert OS-space (logical px) to render space
                    // for hit-testing. See screen_to_render comments.
                    float bx = e.button.x, by = e.button.y;
                    screen_to_render(bx, by);

                    /* TextInput resize grip gets first priority — if
                     * the press lands on a grip, neither the scrollbar
                     * nor the node-click pipeline should see it.
                     * `find_node_at` already returns the deepest node
                     * under the cursor; for a TextInput with resize
                     * enabled we additionally check the grip rect. */
                    {
                        auto hit = find_node_at(s_current_tree, bx, by);
                        if (hit && hit->type == "TextInput" &&
                            Bokken::Canvas::Components::TextInput::hitTestGrip(
                                hit, bx, by))
                        {
                            Bokken::Canvas::Components::TextInput::beginResize(
                                hit, bx, by);
                            s_pressed_node = nullptr;
                            s_press_active = false;
                            return;
                        }
                    }

                    /* Then give the deepest ScrollView's scrollbar a
                     * chance to grab the press. If it does, suppress
                     * the normal click pipeline — pressing on a thumb
                     * should NOT trigger an underlying onClick. */
                    auto sv = find_scroll_at(s_current_tree, bx, by);
                    if (sv && Bokken::Canvas::Components::ScrollView::onMouseDown(
                                  sv, bx, by))
                    {
                        s_pressed_node = nullptr;
                        s_press_active = false;
                        return;
                    }

                    s_pressed_node = find_node_at(s_current_tree, bx, by);
                    if (s_pressed_node)
                        s_pressed_node->isActive = true;

                    /* Record press position + rect. Used by the
                     * mouse-up handler to identify "the same button"
                     * even if a re-render replaced the Node pointer
                     * between mouse-down and mouse-up. With
                     * reconciliation reusing nodes, the pointer
                     * usually IS preserved — but for the cases where
                     * a render swapped types or rebuilt the subtree,
                     * the position-based check is the safety net.
                     * Store in RENDER space so the mouse-up
                     * comparison stays in the same coordinate frame
                     * as the laid-out rects. */
                    s_press_x = bx;
                    s_press_y = by;
                    s_press_active = true;
                    if (s_pressed_node)
                    {
                        s_press_rect_x = s_pressed_node->layout.x;
                        s_press_rect_y = s_pressed_node->layout.y;
                        s_press_rect_w = s_pressed_node->layout.w;
                        s_press_rect_h = s_pressed_node->layout.h;
                    }
                    else
                    {
                        s_press_rect_w = 0.0f;
                        s_press_rect_h = 0.0f;
                    }

                    /* Click also moves focus — to either the pressed
                     * node (if focusable) or to nothing. */
                    if (s_pressed_node && s_pressed_node->style.tabIndex >= 0 &&
                        !s_pressed_node->style.disabled)
                        set_focus(s_pressed_node);
                    else if (!s_pressed_node)
                        set_focus(nullptr);
                    return;
                }

                if (e.type == SDL_EVENT_MOUSE_BUTTON_UP)
                {
                    // Convert OS-space (logical px) to render space.
                    // s_press_x/y were recorded in render space at
                    // mouse-down, so this puts press and release on
                    // the same coordinate frame.
                    float bx = e.button.x, by = e.button.y;
                    screen_to_render(bx, by);

                    /* End any active TextInput resize drag. */
                    {
                        std::function<void(std::shared_ptr<Bokken::Canvas::Node>)> end;
                        end = [&](std::shared_ptr<Bokken::Canvas::Node> n)
                        {
                            if (!n)
                                return;
                            if (n->type == "TextInput" &&
                                Bokken::Canvas::Components::TextInput::isResizing(n))
                            {
                                Bokken::Canvas::Components::TextInput::endResize(n);
                            }
                            for (auto &c : n->children)
                                end(c);
                        };
                        end(s_current_tree);
                    }

                    /* If a scrollbar drag is active, end it here and
                     * suppress the click delivery — the user wasn't
                     * trying to click anything. */
                    auto draggedSV = Bokken::Canvas::Components::ScrollView::draggingNode();
                    if (draggedSV)
                    {
                        Bokken::Canvas::Components::ScrollView::onMouseUp(draggedSV);
                        if (s_pressed_node)
                            s_pressed_node->isActive = false;
                        s_pressed_node = nullptr;
                        s_press_active = false;
                        return;
                    }

                    /* Click delivery via position + rect. Both the
                     * press AND release positions must fall in the
                     * rect captured at mouse-down, AND the post-render
                     * tree must have an onClick node at the release
                     * position. Survives any number of re-renders. */
                    auto hit = find_node_at(s_current_tree, bx, by);

                    bool deliver = false;
                    if (s_press_active && hit && hit->onClick && !hit->style.disabled)
                    {
                        const float px = s_press_x;
                        const float py = s_press_y;
                        const float ux = bx;
                        const float uy = by;
                        const float rx = s_press_rect_x;
                        const float ry = s_press_rect_y;
                        const float rw = s_press_rect_w;
                        const float rh = s_press_rect_h;
                        const bool pressIn = (px >= rx && px < rx + rw &&
                                              py >= ry && py < ry + rh);
                        const bool upIn    = (ux >= rx && ux < rx + rw &&
                                              uy >= ry && uy < ry + rh);
                        if (pressIn && upIn)
                            deliver = true;
                    }

                    if (deliver)
                        hit->onClick();

                    if (s_pressed_node)
                        s_pressed_node->isActive = false;
                    s_pressed_node = nullptr;
                    s_press_active = false;
                    return;
                }

                /* Keyboard: special keys go to the focused node first. */
                if (e.type == SDL_EVENT_KEY_DOWN)
                {
                    /* Tab/Shift-Tab moves focus regardless of focus target. */
                    if (e.key.scancode == SDL_SCANCODE_TAB)
                    {
                        bool reverse = (e.key.mod & SDL_KMOD_SHIFT) != 0;
                        cycle_focus(reverse ? -1 : 1);
                        return;
                    }
                    if (s_focused_node && s_focused_node->type == "TextInput")
                    {
                        Bokken::Canvas::Components::TextInput::handleKey(s_focused_node, e.key.scancode);
                    }
                    if (s_focused_node && s_focused_node->onKey)
                        s_focused_node->onKey((int)e.key.scancode, true);
                    return;
                }
                if (e.type == SDL_EVENT_KEY_UP)
                {
                    if (s_focused_node && s_focused_node->onKey)
                        s_focused_node->onKey((int)e.key.scancode, false);
                    return;
                }

                if (e.type == SDL_EVENT_TEXT_INPUT)
                {
                    if (s_focused_node && s_focused_node->type == "TextInput" &&
                        !s_focused_node->style.disabled)
                    {
                        Bokken::Canvas::Components::TextInput::insertText(s_focused_node, e.text.text);
                    }
                    return;
                }
            }

            void Canvas::update(float deltaTime)
            {
                /* Flush any pending render BEFORE running animations.
                 * This way the new tree (if any) is what gets animated,
                 * not the stale previous tree. Render batching: every
                 * setState since the last frame collapses into ONE
                 * tree rebuild here. */
                flush_pending_render();

                if (s_current_tree)
                    update_node_animations(s_current_tree, deltaTime);
            }

            void Canvas::flush_pending_render()
            {
                if (!s_render_dirty)
                    return;
                auto &engine = Engine::Instance();
                if (!engine.isReady())
                    return;
                JSContext *ctx = engine.context();
                if (JS_IsUndefined(s_root_element))
                    return;
                /* render() clears s_render_dirty at the start. */
                render(ctx, JS_UNDEFINED, 1, &s_root_element);
            }

            void Canvas::present()
            {
                if (!s_batcher || !s_current_tree)
                    return;
                /* Ensure Drawing's lookup textures exist (cheap if so). */
                Bokken::Canvas::Drawing::ensureLookups();
                /* Canvas tree draws at base layer 1000 — well above
                 * GameObject sprites. */
                drawNode(*s_batcher, s_current_tree, 1000);
            }

            JSValue Canvas::render(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                if (argc < 1)
                    return JS_UNDEFINED;

                /* Re-entrancy guard. If render() is called from inside
                 * a useEffect callback that calls setState (which can
                 * happen — cleanups setting state for the next render),
                 * mark dirty and bail rather than recurse. The next
                 * frame's flush_pending_render picks it up. Without
                 * this, the inner recursive render mutates the s_states
                 * vectors out from under flush_effects's iteration and
                 * crashes. */
                if (s_rendering)
                {
                    s_render_dirty = true;
                    return JS_UNDEFINED;
                }
                s_rendering = true;
                /* Clear at the start. setState during this render will
                 * set it again, and the next frame will re-render. */
                s_render_dirty = false;

                /* Preserve focus across renders by remembering its
                 * POSITION in the focusables list. Matching only by
                 * (tabIndex, type) collides when there are multiple
                 * inputs of the same type — focus jumps to the first
                 * one in DOM order, which manifests as "I typed in the
                 * 3rd input but after a couple keystrokes it switches
                 * to the 1st". Position-in-list is what React's auto-
                 * focus logic does (with the addition of `key`); for
                 * us, list-position is good enough for typical UI. */
                int prevFocusIndex = -1;
                std::string prevType;
                if (s_focused_node)
                {
                    std::vector<std::shared_ptr<Bokken::Canvas::Node>> prevFocusables;
                    collect_focusables(s_current_tree, prevFocusables);
                    for (size_t i = 0; i < prevFocusables.size(); i++)
                        if (prevFocusables[i] == s_focused_node)
                        {
                            prevFocusIndex = (int)i;
                            prevType = s_focused_node->type;
                            break;
                        }
                }

                /* Hold the previous tree alive so reconciliation can
                 * walk it in parallel with the new JSX. After
                 * synchronize_tree returns, prev gets dropped — any
                 * Nodes not reused get freed at that point. */
                auto prev_tree = s_current_tree;

                s_hovered_node = nullptr;
                s_pressed_node = nullptr;
                s_focused_node = nullptr;

                JSValue nextRoot = JS_DupValue(ctx, argv[0]);
                JS_FreeValue(ctx, s_root_element);
                s_root_element = nextRoot;

                /* Reconciliation: pass prev_tree so synchronize_tree
                 * can reuse Nodes whose JSX type matches. This is the
                 * BIG perf win — no more allocating fresh Nodes for
                 * 700 elements every render. */
                auto new_tree = synchronize_tree(ctx, s_root_element, prev_tree);
                s_current_tree = new_tree;

                if (s_current_tree)
                {
                    int w = 0, h = 0;
                    viewport(w, h);
                    Bokken::Canvas::Layout::run(s_current_tree, (float)w, (float)h, s_assets);

                    /* Re-establish transient interaction state on the
                     * fresh tree. Without this, every render flickers
                     * off the hover/active visual until the next
                     * mouse-move event arrives. With the visual RAF
                     * loop firing renders at 30Hz during playback,
                     * that strobe is very visible.
                     *
                     * With reconciliation reusing many Nodes, the
                     * old hover/active state often DOES survive
                     * (same Node object, same isHovered field). But
                     * for the cases where the tree shape changed
                     * (component swapped, child added/removed), we
                     * still need to re-hit-test. Cheap to do
                     * unconditionally.
                     *
                     * SDL_GetMouseState returns logical window
                     * pixels; convert into render space before
                     * hit-testing against laid-out rects. */
                    float mx = 0.0f, my = 0.0f;
                    SDL_GetMouseState(&mx, &my);
                    screen_to_render(mx, my);
                    auto hovHit = find_node_at(s_current_tree, mx, my);
                    if (hovHit)
                    {
                        hovHit->isHovered = true;
                        s_hovered_node = hovHit;
                    }
                    if (s_press_active)
                    {
                        auto pressHit = find_node_at(
                            s_current_tree, s_press_x, s_press_y);
                        if (pressHit && pressHit->onClick)
                        {
                            pressHit->isActive = true;
                            s_pressed_node = pressHit;
                        }
                    }

                    /* Restore focus to the same position in the new
                     * focusables list, provided the type still matches
                     * (else the user re-arranged their UI and we should
                     * drop focus rather than land on the wrong thing). */
                    if (prevFocusIndex >= 0)
                    {
                        std::vector<std::shared_ptr<Bokken::Canvas::Node>> focusables;
                        collect_focusables(s_current_tree, focusables);
                        if (prevFocusIndex < (int)focusables.size() &&
                            focusables[prevFocusIndex]->type == prevType)
                        {
                            set_focus(focusables[prevFocusIndex]);
                        }
                    }
                }
                flush_effects(ctx);
                s_rendering = false;
                return JS_UNDEFINED;
            }

            /* Module declaration / init */
            int Canvas::declare(JSContext *ctx, JSModuleDef *m)
            {
                JS_AddModuleExport(ctx, m, "default");
                JS_AddModuleExport(ctx, m, "useState");
                JS_AddModuleExport(ctx, m, "useEffect");
                JS_AddModuleExport(ctx, m, "Align");
                JS_AddModuleExport(ctx, m, "AlignItems");
                JS_AddModuleExport(ctx, m, "AlignSelf");
                JS_AddModuleExport(ctx, m, "Justify");
                JS_AddModuleExport(ctx, m, "FlexDirection");
                JS_AddModuleExport(ctx, m, "Overflow");
                JS_AddModuleExport(ctx, m, "Position");
                JS_AddModuleExport(ctx, m, "Timing");
                JS_AddModuleExport(ctx, m, "TextAlign");
                JS_AddModuleExport(ctx, m, "Cursor");
                JS_AddModuleExport(ctx, m, "View");
                JS_AddModuleExport(ctx, m, "Label");
                JS_AddModuleExport(ctx, m, "Image");
                JS_AddModuleExport(ctx, m, "Button");
                JS_AddModuleExport(ctx, m, "ScrollView");
                JS_AddModuleExport(ctx, m, "TextInput");
                return 0;
            }

            namespace
            {
                /* Helper to build a string-enum object: each key maps
                 * to its own name as a string. */
                JSValue makeStringEnum(JSContext *ctx, std::initializer_list<const char *> names)
                {
                    JSValue obj = JS_NewObject(ctx);
                    for (auto n : names)
                        JS_SetPropertyStr(ctx, obj, n, JS_NewString(ctx, n));
                    return obj;
                }
            }

            int Canvas::init(JSContext *ctx, JSModuleDef *m)
            {
                SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "[Canvas] Initializing module");

                JSValue defaultExport = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, defaultExport, "createElement",
                                  JS_NewCFunction(ctx, create_element, "createElement", 3));
                JS_SetPropertyStr(ctx, defaultExport, "render",
                                  JS_NewCFunction(ctx, render, "render", 1));

                JSValue align = makeStringEnum(ctx, {"Start", "Center", "End"});
                JSValue alignItems = makeStringEnum(ctx, {"Start", "Center", "End", "Stretch"});
                JSValue alignSelf = makeStringEnum(ctx, {"Inherit", "Start", "Center", "End", "Stretch"});
                JSValue justify = makeStringEnum(ctx, {"Start", "Center", "End", "SpaceBetween", "SpaceAround", "SpaceEvenly"});
                JSValue flexDir = makeStringEnum(ctx, {"Row", "Column"});
                JSValue overflow = makeStringEnum(ctx, {"Visible", "Hidden"});
                JSValue position = makeStringEnum(ctx, {"Relative", "Absolute"});
                JSValue timing = makeStringEnum(ctx, {"Linear", "EaseIn", "EaseOut", "EaseInOut", "Bounce", "Back", "Step"});
                JSValue textAlign = makeStringEnum(ctx, {"Left", "Center", "Right", "Justify"});
                JSValue cursor = makeStringEnum(ctx, {"Default", "Pointer", "Text", "Move", "NotAllowed", "Wait", "ResizeNS", "ResizeEW", "Crosshair"});

                JS_SetModuleExport(ctx, m, "default", defaultExport);
                JS_SetModuleExport(ctx, m, "useState", JS_NewCFunction(ctx, use_state, "useState", 1));
                JS_SetModuleExport(ctx, m, "useEffect", JS_NewCFunction(ctx, use_effect, "useEffect", 2));
                JS_SetModuleExport(ctx, m, "Align", align);
                JS_SetModuleExport(ctx, m, "AlignItems", alignItems);
                JS_SetModuleExport(ctx, m, "AlignSelf", alignSelf);
                JS_SetModuleExport(ctx, m, "Justify", justify);
                JS_SetModuleExport(ctx, m, "FlexDirection", flexDir);
                JS_SetModuleExport(ctx, m, "Overflow", overflow);
                JS_SetModuleExport(ctx, m, "Position", position);
                JS_SetModuleExport(ctx, m, "Timing", timing);
                JS_SetModuleExport(ctx, m, "TextAlign", textAlign);
                JS_SetModuleExport(ctx, m, "Cursor", cursor);

                /* Component identifiers — plain strings. */
                JS_SetModuleExport(ctx, m, "View", JS_NewString(ctx, "View"));
                JS_SetModuleExport(ctx, m, "Label", JS_NewString(ctx, "Label"));
                JS_SetModuleExport(ctx, m, "Image", JS_NewString(ctx, "Image"));
                JS_SetModuleExport(ctx, m, "Button", JS_NewString(ctx, "Button"));
                JS_SetModuleExport(ctx, m, "ScrollView", JS_NewString(ctx, "ScrollView"));
                JS_SetModuleExport(ctx, m, "TextInput", JS_NewString(ctx, "TextInput"));

                return 0;
            }
        }
    }
}