#pragma once

#include "Base.hpp"
#include "../Engine.hpp"
#include "../../AssetPack.hpp"
#include "../../canvas/Align.hpp"
#include "../../canvas/Justify.hpp"
#include "../../canvas/TextAlign.hpp"
#include "../../canvas/Cursor.hpp"
#include "../../canvas/Timing.hpp"
#include "../../canvas/SimpleStyleSheet.hpp"
#include "../../canvas/Node.hpp"
#include "../../canvas/Layout.hpp"
#include "../../canvas/components/Label.hpp"
#include "../../canvas/components/View.hpp"
#include "../../canvas/components/Image.hpp"
#include "../../canvas/components/Button.hpp"
#include "../../canvas/components/ScrollView.hpp"
#include "../../canvas/components/TextInput.hpp"
#include "../../canvas/Drawing.hpp"
#include "../../renderer/TextureCache.hpp"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <map>
#include <unordered_map>
#include <functional>

namespace Bokken
{
    namespace Renderer
    {
        class SpriteBatcher;
        class TextureCache;
    }

    namespace Scripting
    {
        namespace Modules
        {
            /**
             * The Canvas scripting module.
             *
             * Capabilities:
             *   - Layout::run drives measure + arrange every render and
             *     on window resize.
             *   - drawNode walker honors overflow:Hidden via scissor and
             *     translates ScrollView children by scrollX/Y.
             *   - Focus management: a single focused node, traversed by
             *     Tab / Shift-Tab; SDL text input is started while a
             *     TextInput is focused.
             *   - Wheel events route to the deepest ScrollView under
             *     the cursor.
             *   - Event hooks exposed to JS: onMouseEnter, onMouseLeave,
             *     onChange, onFocus, onBlur, onScroll.
             *   - Component registry for Image, Button, ScrollView, TextInput.
             *
             * Wiring with the renderer
             * Loop wires both the SpriteBatcher and the TextureCache
             * via setBatcher() / setTextureCache(). The TextureCache
             * pointer is forwarded to Components::Image::s_textureCache
             * during init.
            */
            class Canvas : public Base
            {
            public:
                Canvas(SDL_Window *window, AssetPack *assets)
                    : Base("bokken/canvas"), m_assets(assets)
                {
                    s_window = window;
                    s_assets = assets;
                }

                static void setBatcher(Bokken::Renderer::SpriteBatcher *b) { s_batcher = b; }
                static void setTextureCache(Bokken::Renderer::TextureCache *t);

                /* Subscribe to the renderer's render-size-changed
                 * observer so the canvas relayouts when the render
                 * target — NOT the OS window — actually changes
                 * size. Called from Loop::init after the Renderer
                 * scripting module is up. Idempotent. */
                static void attach();

                /* Unsubscribe. Called from Loop::shutdown BEFORE the
                 * renderer is destroyed. Idempotent. */
                static void detach();

                /* Per-module teardown hook, called by Engine::shutdown() on
                 * every reload AND on final quit, while the JSContext is
                 * still alive. Releases everything tied to the JS runtime:
                 * the useState stores, useEffect slots (callback / cleanup /
                 * deps), the retained root element, the interned-atom cache
                 * (atoms are per-runtime — see below), and the node pointers
                 * into the now-defunct JS tree. Does NOT touch GPU/SDL
                 * resources (fonts, cursors) — those follow the renderer /
                 * window lifetime and are released in clear_font_cache() /
                 * shutdown(). Idempotent and safe when nothing is mounted. */
                void destroy(JSContext *ctx) override;

                int declare(JSContext *ctx, JSModuleDef *m) override;
                int init(JSContext *ctx, JSModuleDef *m) override;

                static TTF_Font *get_font(const std::string &path, float size);
                static void clear_font_cache();

                static void update(float deltaTime);
                static void present();

                /* Render batching entry-point.
                 *
                 * setState marks the tree dirty rather than calling
                 * render() synchronously. Once per frame, this flushes
                 * the pending render. Multiple setStates in the same
                 * frame collapse to one tree rebuild.
                 *
                 * Called automatically from Canvas::update each frame.
                 * Called once explicitly from Loop right after onStart
                 * so the warmup frame paints a real tree. Public so
                 * Loop.cpp can call it; safe to invoke any number of
                 * times in a row. */
                static void flush_pending_render();

                /* Atom cache. Public so the anonymous-namespace
                 * StyleParser (and any other module-internal helper)
                 * can use it without needing to be a friend.
                 *
                 * JS_GetPropertyStr interns the C-string as a JSAtom
                 * on every call; for parsing that runs 80+ times per
                 * node × hundreds of nodes per render, that's an
                 * avoidable cost. We cache atoms by C-string POINTER
                 * (string literals have stable addresses) — first call
                 * interns, subsequent calls just hash the pointer.
                 *
                 * The atoms leak by design — they live for the
                 * lifetime of the runtime. */
                static JSAtom atom_for(JSContext *ctx, const char *name);

                static void markLabelsDirty(std::shared_ptr<Bokken::Canvas::Node> node);
                static void forceRelayout();
                static void handleEvent(const SDL_Event &event);

            private:
                friend void drawNode(Bokken::Renderer::SpriteBatcher &batcher,
                                     std::shared_ptr<Bokken::Canvas::Node> node,
                                     int layer);

                /* Native subscription id on Renderer::Base. -1 when
                 * not subscribed. Set by attach(), cleared by detach(). */
                static inline int s_rendererSubId = -1;

                /* Single native handler installed by attach(). Forces
                 * a relayout of the current tree at the new render
                 * dimensions; the body reads renderer()->renderWidth/Height
                 * directly so the (w, h) args are unused. */
                static void onRendererRenderSizeChanged(int w, int h);

                /* Helpers for the render-vs-window coordinate split.
                 *
                 * The canvas lays out in RENDER pixels (the pipeline's
                 * output target — see GameObject::present), not OS
                 * window logical pixels. These keep that conversion
                 * in one place:
                 *
                 *   viewport(w, h)   — current render-target dimensions.
                 *                      Used by Layout::run.
                 *   screen_to_render — convert SDL mouse coords
                 *                      (logical window px) into render
                 *                      space for hit-testing.
                 *
                 * Both fall back to SDL_GetWindowSize / pass-through
                 * when no renderer is wired (tooling, tests). */
                static void viewport(int &w, int &h);
                static void screen_to_render(float &x, float &y);

                /* Tree walks */
                static std::shared_ptr<Bokken::Canvas::Node> find_node_at(
                    std::shared_ptr<Bokken::Canvas::Node> root, float mx, float my);
                static std::shared_ptr<Bokken::Canvas::Node> find_scroll_at(
                    std::shared_ptr<Bokken::Canvas::Node> root, float mx, float my);
                static void update_node_animations(std::shared_ptr<Bokken::Canvas::Node> node, float dt);
                static float apply_easing(Bokken::Canvas::Timing func, float t);
                static void reset_active_states(std::shared_ptr<Bokken::Canvas::Node> node);
                static void collect_focusables(std::shared_ptr<Bokken::Canvas::Node> root,
                                               std::vector<std::shared_ptr<Bokken::Canvas::Node>> &out);
                static void update_caret_blink(std::shared_ptr<Bokken::Canvas::Node> node, float dt);
                static void update_cursor_for_hover();

                /* Focus management */
                static void set_focus(std::shared_ptr<Bokken::Canvas::Node> node);
                static void cycle_focus(int direction /* +1 or -1 */);

                static inline SDL_Window *s_window = nullptr;
                static inline Bokken::Renderer::SpriteBatcher *s_batcher = nullptr;

                AssetPack *m_assets;
                static inline AssetPack *s_assets = nullptr;

                static inline std::map<std::string, TTF_Font *> s_font_cache;

                static inline std::shared_ptr<Bokken::Canvas::Node> s_hovered_node = nullptr;
                static inline std::shared_ptr<Bokken::Canvas::Node> s_pressed_node = nullptr;
                static inline std::shared_ptr<Bokken::Canvas::Node> s_focused_node = nullptr;

                /* Press position recorded on mouse-down. Click delivery
                 * uses this rather than s_pressed_node identity so that
                 * a re-render between mouse-down and mouse-up doesn't
                 * lose the click (the post-render tree has fresh Node
                 * pointers, but the screen position is stable). */
                static inline float s_press_x = -1.0f;
                static inline float s_press_y = -1.0f;
                static inline float s_press_rect_x = 0.0f;
                static inline float s_press_rect_y = 0.0f;
                static inline float s_press_rect_w = 0.0f;
                static inline float s_press_rect_h = 0.0f;
                static inline bool  s_press_active = false;

                /* Render batching state.
                 *
                 * setState batches renders: it sets s_render_dirty
                 * rather than calling render() synchronously, so N
                 * setStates in a frame still produce a single full-tree
                 * rebuild. The render fires once per frame from
                 * Canvas::update.
                 *
                 * s_rendering is a re-entrancy guard. If render() is
                 * somehow re-entered (e.g. an effect calls setState
                 * which calls render() before the outer render
                 * finishes flush_effects), we mark dirty and return
                 * rather than recursing — the next frame picks it up. */
                static inline bool s_render_dirty = false;
                static inline bool s_rendering = false;

                /* SDL system cursors — lazily allocated, indexed by Cursor enum. */
                static inline SDL_Cursor *s_cursors[16] = {nullptr};
                static inline Bokken::Canvas::Cursor s_lastCursor = Bokken::Canvas::Cursor::Default;

                /* QuickJS Module Functions */
                static JSValue create_element(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue render(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue use_state(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue use_effect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

                static void flush_effects(JSContext *ctx);

                /* Reconciliation entry point.
                 *
                 * If `prev` is non-null and the new JSX element has a
                 * matching type, the existing Node is reused (mutated
                 * in-place with new properties). Children are
                 * recursively reconciled by index.
                 *
                 * If `prev` is null or types differ, a fresh Node is
                 * allocated (the original behaviour).
                 *
                 * The `prev` argument lets us reuse 90%+ of nodes
                 * across renders when the JSX shape is stable, which
                 * eliminates the per-render Node allocation storm
                 * AND the per-render property-parse storm (we only
                 * re-parse properties whose JSValue identity changed,
                 * skipping the unchanged ones entirely). */
                static std::shared_ptr<Bokken::Canvas::Node> synchronize_tree(
                    JSContext *ctx, JSValue val,
                    std::shared_ptr<Bokken::Canvas::Node> prev);

                static void parse_simple_style_sheet(JSContext *ctx, JSValue style,
                                                     Bokken::Canvas::SimpleStyleSheet &out);

                /* Backing storage for the atom cache. The atom_for()
                 * accessor is declared in the public section above so
                 * the StyleParser helper (in an anonymous namespace
                 * inside Canvas.cpp) can reach it. */
                static inline std::unordered_map<const char *, JSAtom> s_atoms;

                /* Bumped by destroy() every time the JS runtime is torn
                 * down. Any cache derived from per-runtime handles (atoms,
                 * the style-dispatch table) records the generation it was
                 * built against and rebuilds when it sees a newer one, so
                 * nothing carries stale atoms across a live reload. */
                static inline uint64_t s_runtimeGeneration = 0;

            public:
                static uint64_t runtimeGeneration() { return s_runtimeGeneration; }

            private:

                /* Helper to bind a JS callback to a C++ functional slot.
                 * Returns a lambda that calls the JS function safely
                 * inside the engine context, plus an onDeconstruct
                 * hook to free the JS value when the node dies. */
                static void bind_callback_void(JSContext *ctx, JSValue cb,
                                               std::function<void()> &outSlot,
                                               std::shared_ptr<Bokken::Canvas::Node> &node);
                static void bind_callback_str(JSContext *ctx, JSValue cb,
                                              std::function<void(const std::string &)> &outSlot,
                                              std::shared_ptr<Bokken::Canvas::Node> &node);

                static inline std::shared_ptr<Bokken::Canvas::Node> s_current_tree = nullptr;
                static std::map<void *, std::vector<JSValue>> s_states;

                struct EffectSlot
                {
                    JSValue callback = JS_UNDEFINED;
                    JSValue cleanup = JS_UNDEFINED;
                    std::vector<JSValue> deps;
                    bool hasRun = false;
                };
                static std::map<void *, std::vector<EffectSlot>> s_effects;
                static std::vector<std::pair<void *, int>> s_pendingEffects;

                static void *s_active_comp;
                static int s_hook_idx;
                static int s_effect_idx;
                static JSValue s_root_element;
            };

            void drawNode(Bokken::Renderer::SpriteBatcher &batcher,
                          std::shared_ptr<Bokken::Canvas::Node> node,
                          int layer);
        }
    }
}