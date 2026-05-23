#include "Window.hpp"

namespace Bokken
{
    namespace Scripting
    {
        namespace Modules
        {

            void Window::attach(SDL_Window *win)
            {
                s_window = win;
            }

            void Window::detach()
            {
                s_window = nullptr;
            }

            JSValue Window::js_setTitle(JSContext *ctx, JSValueConst /*this_val*/,
                                        int argc, JSValueConst *argv)
            {
                if (s_window && argc > 0)
                {
                    const char *title = JS_ToCString(ctx, argv[0]);
                    if (title)
                    {
                        SDL_SetWindowTitle(s_window, title);
                        JS_FreeCString(ctx, title);
                    }
                }
                return JS_UNDEFINED;
            }

            JSValue Window::js_getSize(JSContext *ctx, JSValueConst /*this_val*/,
                                       int /*argc*/, JSValueConst * /*argv*/)
            {
                int w = 0, h = 0;

                if (s_window)
                    SDL_GetWindowSizeInPixels(s_window, &w, &h);

                JSValue obj = JS_NewObject(ctx);

                JS_SetPropertyStr(ctx, obj, "width", JS_NewInt32(ctx, w));
                JS_SetPropertyStr(ctx, obj, "height", JS_NewInt32(ctx, h));

                return obj;
            }

            JSValue Window::js_getLogicalSize(JSContext *ctx, JSValueConst /*this_val*/,
                                              int /*argc*/, JSValueConst * /*argv*/)
            {
                int w = 0, h = 0;

                if (s_window)
                    SDL_GetWindowSize(s_window, &w, &h);

                JSValue obj = JS_NewObject(ctx);

                JS_SetPropertyStr(ctx, obj, "width", JS_NewInt32(ctx, w));
                JS_SetPropertyStr(ctx, obj, "height", JS_NewInt32(ctx, h));

                return obj;
            }

            int Window::declare(JSContext *ctx, JSModuleDef *m)
            {
                return JS_AddModuleExport(ctx, m, "default");
            }

            int Window::init(JSContext *ctx, JSModuleDef *m)
            {
                JSValue def = JS_NewObject(ctx);

                JS_SetPropertyStr(ctx, def, "setTitle",
                                  JS_NewCFunction(ctx, &Window::js_setTitle, "setTitle", 1));
                JS_SetPropertyStr(ctx, def, "getSize",
                                  JS_NewCFunction(ctx, &Window::js_getSize, "getSize", 0));
                JS_SetPropertyStr(ctx, def, "getLogicalSize",
                                  JS_NewCFunction(ctx, &Window::js_getLogicalSize, "getLogicalSize", 0));

                JS_SetModuleExport(ctx, m, "default", def);
                return 0;
            }

        } // namespace Modules
    } // namespace Scripting
} // namespace Bokken