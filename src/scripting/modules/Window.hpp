#pragma once

#include "Base.hpp"

#include <SDL3/SDL.h>

namespace Bokken
{
    namespace Scripting
    {
        namespace Modules
        {

            /**
             * `bokken/window` — OS window query / manipulation surface.
             *
             *   import Window from "bokken/window";
             *
             *   Window.setTitle("My Game");
             *   const { width, height } = Window.getSize();         // physical px
             *   const { width, height } = Window.getLogicalSize();  // OS logical px
             *
             * Size convention
             *
             *   getSize()        — physical framebuffer pixels
             *                      (SDL_GetWindowSizeInPixels). The raw
             *                      output surface size. Under a Fixed
             *                      render-size policy this is typically
             *                      larger than the render target; the
             *                      final composite blit letter / pillar-
             *                      boxes the render output into it.
             *
             *   getLogicalSize() — OS-reported logical pixels
             *                      (SDL_GetWindowSize). Matches the
             *                      space mouse events are reported in.
             *
             * Related modules
             *
             *   Render-target sizing, render-mode policy, and resize
             *   callbacks live on `bokken/renderer`. They describe the
             *   pipeline output, not the OS window:
             *
             *     Renderer.getRenderSize()
             *     Renderer.getRenderMode()
             *     Renderer.setRenderSize(w, h, mode)
             *     Renderer.onResize(cb) / Renderer.offResize(id)
             *
             *   Screen ↔ world coordinate conversion lives on Camera2D:
             *
             *     camera.getComponent(Camera2D).screenToWorldPoint(x, y)
             *     camera.getComponent(Camera2D).worldToScreenPoint(x, y)
             */
            class Window : public Base
            {
            public:
                Window() : Base("bokken/window") {}

                /* Wire the SDL window pointer. Called from Loop::init.
                 * Idempotent — calling a second time replaces the
                 * binding (test harnesses, hot reload). */
                static void attach(SDL_Window *win);

                /* Drop the binding. Called from Loop::shutdown.
                 * Idempotent. */
                static void detach();

                int declare(JSContext *ctx, JSModuleDef *m) override;
                int init(JSContext *ctx, JSModuleDef *m) override;

                /* Access for other native code that needs the raw SDL
                 * handle (e.g. input event sourcing). Returns nullptr
                 * before attach() or after detach(). */
                static SDL_Window *window() { return s_window; }

            private:
                static inline SDL_Window *s_window = nullptr;

                static JSValue js_setTitle      (JSContext *, JSValueConst, int, JSValueConst *);
                static JSValue js_getSize       (JSContext *, JSValueConst, int, JSValueConst *);
                static JSValue js_getLogicalSize(JSContext *, JSValueConst, int, JSValueConst *);
            };

        } // namespace Modules
    } // namespace Scripting
} // namespace Bokken