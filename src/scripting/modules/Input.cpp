#include "Input.hpp"

namespace Bokken
{
    namespace Scripting
    {
        namespace Modules
        {
            int Input::declare(JSContext *ctx, JSModuleDef *m)
            {
                return JS_AddModuleExport(ctx, m, "default");
            }

            int Input::init(JSContext *ctx, JSModuleDef *m)
            {
                JSValue obj = JS_NewObject(ctx);

                JS_SetPropertyStr(ctx, obj, "isKeyDown",
                    JS_NewCFunction(ctx, js_is_key_down, "isKeyDown", 1));
                JS_SetPropertyStr(ctx, obj, "isKeyPressed",
                    JS_NewCFunction(ctx, js_is_key_pressed, "isKeyPressed", 1));
                JS_SetPropertyStr(ctx, obj, "isKeyReleased",
                    JS_NewCFunction(ctx, js_is_key_released, "isKeyReleased", 1));
                JS_SetPropertyStr(ctx, obj, "getMousePosition",
                    JS_NewCFunction(ctx, js_get_mouse_position, "getMousePosition", 0));
                JS_SetPropertyStr(ctx, obj, "isMouseDown",
                    JS_NewCFunction(ctx, js_is_mouse_down, "isMouseDown", 1));
                JS_SetPropertyStr(ctx, obj, "isMousePressed",
                    JS_NewCFunction(ctx, js_is_mouse_pressed, "isMousePressed", 1));
                JS_SetPropertyStr(ctx, obj, "isMouseReleased",
                    JS_NewCFunction(ctx, js_is_mouse_released, "isMouseReleased", 1));
                JS_SetPropertyStr(ctx, obj, "getScrollDelta",
                    JS_NewCFunction(ctx, js_get_scroll_delta, "getScrollDelta", 0));

                JS_SetModuleExport(ctx, m, "default", obj);
                return 0;
            }

            void Input::handleEvent(const SDL_Event &e)
            {
                switch (e.type)
                {
                case SDL_EVENT_KEY_DOWN:
                    if (!e.key.repeat)
                        s_keysPressed.insert(e.key.scancode);
                    break;

                case SDL_EVENT_KEY_UP:
                    s_keysReleased.insert(e.key.scancode);
                    break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                {
                    int idx = e.button.button - 1; // SDL buttons are 1-based
                    if (idx >= 0 && idx < 3)
                    {
                        s_mouseHeld[idx] = true;
                        s_mousePressed[idx] = true;
                    }
                    break;
                }

                case SDL_EVENT_MOUSE_BUTTON_UP:
                {
                    int idx = e.button.button - 1;
                    if (idx >= 0 && idx < 3)
                    {
                        s_mouseHeld[idx] = false;
                        s_mouseReleased[idx] = true;
                    }
                    break;
                }

                case SDL_EVENT_MOUSE_WHEEL:
                    s_scrollX += e.wheel.x;
                    s_scrollY += e.wheel.y;
                    break;

                default:
                    break;
                }
            }

            void Input::endFrame()
            {
                s_keysPressed.clear();
                s_keysReleased.clear();

                s_mousePressed[0]  = false;
                s_mousePressed[1]  = false;
                s_mousePressed[2]  = false;
                s_mouseReleased[0] = false;
                s_mouseReleased[1] = false;
                s_mouseReleased[2] = false;

                s_scrollX = 0.0f;
                s_scrollY = 0.0f;
            }

            // JS: Input.isKeyDown("ArrowLeft") — true while the key is held.
            JSValue Input::js_is_key_down(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 1) return JS_FALSE;

                const char *name = JS_ToCString(ctx, argv[0]);
                if (!name) return JS_FALSE;

                SDL_Scancode sc = scancode_from_name(name);
                JS_FreeCString(ctx, name);

                const bool *state = SDL_GetKeyboardState(nullptr);
                return JS_NewBool(ctx, state && state[sc]);
            }

            // JS: Input.isKeyPressed("Space") — true only on the frame it went down.
            JSValue Input::js_is_key_pressed(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 1) return JS_FALSE;

                const char *name = JS_ToCString(ctx, argv[0]);
                if (!name) return JS_FALSE;

                SDL_Scancode sc = scancode_from_name(name);
                JS_FreeCString(ctx, name);

                return JS_NewBool(ctx, s_keysPressed.count(sc) > 0);
            }

            // JS: Input.isKeyReleased("KeyW") — true only on the frame it went up.
            JSValue Input::js_is_key_released(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 1) return JS_FALSE;

                const char *name = JS_ToCString(ctx, argv[0]);
                if (!name) return JS_FALSE;

                SDL_Scancode sc = scancode_from_name(name);
                JS_FreeCString(ctx, name);

                return JS_NewBool(ctx, s_keysReleased.count(sc) > 0);
            }

            // JS: Input.getMousePosition() → {x, y}
            JSValue Input::js_get_mouse_position(JSContext *ctx, JSValueConst, int, JSValueConst *)
            {
                float mx = 0.0f, my = 0.0f;
                SDL_GetMouseState(&mx, &my);

                JSValue obj = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, mx));
                JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, my));
                return obj;
            }

            // JS: Input.isMouseDown(0) — 0=left, 1=middle, 2=right.
            JSValue Input::js_is_mouse_down(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 1) return JS_FALSE;

                int idx = 0;
                JS_ToInt32(ctx, &idx, argv[0]);
                if (idx < 0 || idx > 2) return JS_FALSE;

                return JS_NewBool(ctx, s_mouseHeld[idx]);
            }

            // JS: Input.isMousePressed(0) — true on the frame the button went down.
            JSValue Input::js_is_mouse_pressed(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 1) return JS_FALSE;

                int idx = 0;
                JS_ToInt32(ctx, &idx, argv[0]);
                if (idx < 0 || idx > 2) return JS_FALSE;

                return JS_NewBool(ctx, s_mousePressed[idx]);
            }

            // JS: Input.isMouseReleased(0) — true on the frame the button went up.
            JSValue Input::js_is_mouse_released(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 1) return JS_FALSE;

                int idx = 0;
                JS_ToInt32(ctx, &idx, argv[0]);
                if (idx < 0 || idx > 2) return JS_FALSE;

                return JS_NewBool(ctx, s_mouseReleased[idx]);
            }

            // JS: Input.getScrollDelta() → {x, y}
            JSValue Input::js_get_scroll_delta(JSContext *ctx, JSValueConst, int, JSValueConst *)
            {
                JSValue obj = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, s_scrollX));
                JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, s_scrollY));
                return obj;
            }

            // Maps JS key name strings to SDL scancodes.
            // Accepts both web-standard names ("ArrowUp", "KeyW", "Space", "Digit1")
            // and SDL names ("W", "Up", "Space") for convenience.
            SDL_Scancode Input::scancode_from_name(const char *name)
            {
                if (!name || !name[0])
                    return SDL_SCANCODE_UNKNOWN;

                // Web-standard "Key*" prefix → bare letter for SDL.
                if (strncmp(name, "Key", 3) == 0 && name[3] >= 'A' && name[3] <= 'Z' && name[4] == '\0')
                    return SDL_GetScancodeFromName(&name[3]);

                // Web-standard "Digit*" prefix → bare digit for SDL.
                if (strncmp(name, "Digit", 5) == 0 && name[5] >= '0' && name[5] <= '9' && name[6] == '\0')
                    return SDL_GetScancodeFromName(&name[5]);

                // Web-standard arrow names.
                if (strcmp(name, "ArrowUp") == 0)    return SDL_SCANCODE_UP;
                if (strcmp(name, "ArrowDown") == 0)  return SDL_SCANCODE_DOWN;
                if (strcmp(name, "ArrowLeft") == 0)  return SDL_SCANCODE_LEFT;
                if (strcmp(name, "ArrowRight") == 0) return SDL_SCANCODE_RIGHT;

                // Common web-standard names that differ from SDL.
                if (strcmp(name, "ShiftLeft") == 0)    return SDL_SCANCODE_LSHIFT;
                if (strcmp(name, "ShiftRight") == 0)   return SDL_SCANCODE_RSHIFT;
                if (strcmp(name, "ControlLeft") == 0)   return SDL_SCANCODE_LCTRL;
                if (strcmp(name, "ControlRight") == 0)  return SDL_SCANCODE_RCTRL;
                if (strcmp(name, "AltLeft") == 0)       return SDL_SCANCODE_LALT;
                if (strcmp(name, "AltRight") == 0)      return SDL_SCANCODE_RALT;
                if (strcmp(name, "MetaLeft") == 0)      return SDL_SCANCODE_LGUI;
                if (strcmp(name, "MetaRight") == 0)     return SDL_SCANCODE_RGUI;
                if (strcmp(name, "Enter") == 0)         return SDL_SCANCODE_RETURN;
                if (strcmp(name, "Backspace") == 0)     return SDL_SCANCODE_BACKSPACE;
                if (strcmp(name, "Escape") == 0)        return SDL_SCANCODE_ESCAPE;

                // Fallback: let SDL try to resolve it directly.
                // This handles "Space", "Tab", "F1"–"F12", letter keys, etc.
                SDL_Scancode sc = SDL_GetScancodeFromName(name);
                return sc;
            }
        }
    }
}
