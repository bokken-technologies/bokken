#pragma once

#include "Base.hpp"

#include <SDL3/SDL.h>

#include <cstring>
#include <unordered_set>

namespace Bokken
{
    namespace Scripting
    {
        namespace Modules
        {
            // Exposes "bokken/input" to JS with polling-based keyboard and mouse state.
            //
            // Keyboard:
            //   isKeyDown(key)     — true while the key is held
            //   isKeyPressed(key)  — true only on the frame the key went down
            //   isKeyReleased(key) — true only on the frame the key went up
            //
            // Mouse:
            //   getMousePosition() — {x, y} in window coordinates
            //   isMouseDown(button) — true while the button is held (0=left, 1=middle, 2=right)
            //   isMousePressed(button)  — true on the frame the button went down
            //   isMouseReleased(button) — true on the frame the button went up
            //   getScrollDelta()   — {x, y} scroll wheel delta this frame
            class Input : public Base
            {
            public:
                Input() : Base("bokken/input") {}

                int declare(JSContext *ctx, JSModuleDef *m) override;
                int init(JSContext *ctx, JSModuleDef *m) override;

                // Called from Loop::processEvents for each SDL event.
                static void handleEvent(const SDL_Event &e);

                // Called at the end of each tick to clear pressed/released edge flags.
                static void endFrame();

            private:
                // Keyboard edge state — scancodes that went down or up this frame.
                static inline std::unordered_set<SDL_Scancode> s_keysPressed;
                static inline std::unordered_set<SDL_Scancode> s_keysReleased;

                // Mouse edge state.
                static inline bool s_mousePressed[3]  = {};
                static inline bool s_mouseReleased[3] = {};
                static inline bool s_mouseHeld[3]     = {};

                // Scroll accumulator for this frame.
                static inline float s_scrollX = 0.0f;
                static inline float s_scrollY = 0.0f;

                // JS bindings.
                static JSValue js_is_key_down(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_is_key_pressed(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_is_key_released(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_get_mouse_position(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_is_mouse_down(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_is_mouse_pressed(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_is_mouse_released(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_get_scroll_delta(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

                // Maps a JS key name string to an SDL_Scancode.
                static SDL_Scancode scancode_from_name(const char *name);
            };
        }
    }
}
