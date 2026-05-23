#include "JSBehaviour.hpp"

namespace Bokken
{
    namespace Scripting
    {
        namespace Modules
        {
            namespace
            {
                // Build a {x, y} JS object for callback args. Mirrors the
                // shape used elsewhere by collider callbacks so user code
                // sees a consistent vec2 interface across all events.
                JSValue makeVec2(JSContext *ctx, float x, float y)
                {
                    JSValue obj = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, x));
                    JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, y));
                    return obj;
                }
            }

            JSBehaviour::JSBehaviour(JSContext *ctx, JSValue instance)
                : m_ctx(ctx), m_instance(JS_DupValue(ctx, instance))
            {
            }

            JSBehaviour::~JSBehaviour()
            {
                // Free every cached hook ref plus the instance itself.
                // Each cacheHook() call dup'd its slot; symmetric free
                // here keeps the QuickJS refcount balanced.
                JS_FreeValue(m_ctx, m_onStart);
                JS_FreeValue(m_ctx, m_onUpdate);
                JS_FreeValue(m_ctx, m_onFixedUpdate);
                JS_FreeValue(m_ctx, m_onDestroy);
                JS_FreeValue(m_ctx, m_onCollisionBegin);
                JS_FreeValue(m_ctx, m_onCollisionEnd);
                JS_FreeValue(m_ctx, m_onCollisionHit);
                JS_FreeValue(m_ctx, m_onSensorBegin);
                JS_FreeValue(m_ctx, m_onSensorEnd);
                JS_FreeValue(m_ctx, m_instance);
            }

            void JSBehaviour::cacheHook(const char *name, JSValue &out)
            {
                JSValue v = JS_GetPropertyStr(m_ctx, m_instance, name);
                if (JS_IsFunction(m_ctx, v))
                {
                    out = v;  // takes ownership of the ref
                }
                else
                {
                    JS_FreeValue(m_ctx, v);
                    out = JS_UNDEFINED;
                }
            }

            void JSBehaviour::invokeHook(JSValue hook, int argc,
                                         JSValueConst *argv, const char *hookName) const
            {
                if (JS_IsUndefined(hook))
                    return;

                JSValue ret = JS_Call(m_ctx, hook, m_instance, argc, argv);
                if (JS_IsException(ret))
                {
                    // Log + clear so a throwing behaviour doesn't bring
                    // down the next tick. The exception value itself
                    // gets freed by JS_GetException.
                    JSValue exc = JS_GetException(m_ctx);
                    const char *str = JS_ToCString(m_ctx, exc);
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                 "[JSBehaviour::%s] uncaught: %s",
                                 hookName, str ? str : "<unknown>");
                    if (str) JS_FreeCString(m_ctx, str);
                    JS_FreeValue(m_ctx, exc);
                }
                JS_FreeValue(m_ctx, ret);
            }

            void JSBehaviour::onAttach()
            {
                // Cache every method handle once. Subsequent ticks skip
                // the property-lookup cost. Methods that don't exist on
                // the user's class stay JS_UNDEFINED — invokeHook
                // short-circuits on those without dispatching.
                cacheHook("onStart",         m_onStart);
                cacheHook("onUpdate",        m_onUpdate);
                cacheHook("onFixedUpdate",   m_onFixedUpdate);
                cacheHook("onDestroy",       m_onDestroy);
                cacheHook("onCollisionBegin",m_onCollisionBegin);
                cacheHook("onCollisionEnd",  m_onCollisionEnd);
                cacheHook("onCollisionHit",  m_onCollisionHit);
                cacheHook("onSensorBegin",   m_onSensorBegin);
                cacheHook("onSensorEnd",     m_onSensorEnd);

                // Wire `this.gameObject` so user code can reach siblings
                // via this.gameObject.getComponent(Transform2D) etc.
                // The GameObject JS wrapper is non-owning — its opaque
                // pointer is just the Base*, no finalizer. That's safe
                // because the JSBehaviour lives on the GameObject;
                // this.gameObject can't outlive us.
                if (this->gameObject)
                {
                    JSValue goJs = JS_NewObjectClass(m_ctx, GameObject::s_class_id);
                    if (!JS_IsException(goJs))
                    {
                        JS_SetOpaque(goJs, this->gameObject);
                        JS_SetPropertyStr(m_ctx, m_instance, "gameObject", goJs);
                        // JS_SetPropertyStr takes ownership — no free.
                    }
                }
            }

            void JSBehaviour::onDestroy()
            {
                // Fire the user's onDestroy first, while the GameObject
                // and component map are still intact. After this returns,
                // the GameObject is on the way out and any access from
                // the JS side becomes UB.
                invokeHook(m_onDestroy, 0, nullptr, "onDestroy");
            }

            void JSBehaviour::update(float deltaTime)
            {
                // Lazy first-tick onStart — see header comment for why.
                if (!m_started)
                {
                    m_started = true;
                    invokeHook(m_onStart, 0, nullptr, "onStart");
                }
                JSValue dt = JS_NewFloat64(m_ctx, deltaTime);
                invokeHook(m_onUpdate, 1, &dt, "onUpdate");
                JS_FreeValue(m_ctx, dt);
            }

            void JSBehaviour::fixedUpdate(float deltaTime)
            {
                JSValue dt = JS_NewFloat64(m_ctx, deltaTime);
                invokeHook(m_onFixedUpdate, 1, &dt, "onFixedUpdate");
                JS_FreeValue(m_ctx, dt);
            }

            JSValue JSBehaviour::wrapOther(Bokken::GameObject::Collider2D *other) const
            {
                if (!other || !other->gameObject)
                    return JS_NULL;
                JSValue obj = JS_NewObjectClass(m_ctx, GameObject::s_class_id);
                if (JS_IsException(obj))
                    return JS_NULL;
                JS_SetOpaque(obj, other->gameObject);
                return obj;
            }

            void JSBehaviour::onCollisionBegin(Bokken::GameObject::Collider2D *other,
                                               const b2Manifold &manifold)
            {
                if (JS_IsUndefined(m_onCollisionBegin)) return;

                auto &world = Bokken::Physics::World::get();
                glm::vec2 pointPx{0.0f}, normalPx{0.0f};
                if (manifold.pointCount > 0)
                {
                    pointPx = world.b2ToPx(manifold.points[0].point);
                    normalPx = {manifold.normal.x, manifold.normal.y};
                }
                JSValue contact = JS_NewObject(m_ctx);
                JS_SetPropertyStr(m_ctx, contact, "point",
                                  makeVec2(m_ctx, pointPx.x, pointPx.y));
                JS_SetPropertyStr(m_ctx, contact, "normal",
                                  makeVec2(m_ctx, normalPx.x, normalPx.y));
                JS_SetPropertyStr(m_ctx, contact, "pointCount",
                                  JS_NewInt32(m_ctx, manifold.pointCount));

                JSValue otherGo = wrapOther(other);
                JSValue args[2] = {otherGo, contact};
                invokeHook(m_onCollisionBegin, 2, args, "onCollisionBegin");
                JS_FreeValue(m_ctx, otherGo);
                JS_FreeValue(m_ctx, contact);
            }

            void JSBehaviour::onCollisionEnd(Bokken::GameObject::Collider2D *other)
            {
                if (JS_IsUndefined(m_onCollisionEnd)) return;
                JSValue otherGo = wrapOther(other);
                invokeHook(m_onCollisionEnd, 1, &otherGo, "onCollisionEnd");
                JS_FreeValue(m_ctx, otherGo);
            }

            void JSBehaviour::onCollisionHit(Bokken::GameObject::Collider2D *other,
                                             const b2ContactHitEvent &event)
            {
                if (JS_IsUndefined(m_onCollisionHit)) return;

                auto &world = Bokken::Physics::World::get();
                glm::vec2 pt = world.b2ToPx(event.point);
                glm::vec2 nrm{event.normal.x, event.normal.y};

                JSValue hit = JS_NewObject(m_ctx);
                JS_SetPropertyStr(m_ctx, hit, "point", makeVec2(m_ctx, pt.x, pt.y));
                JS_SetPropertyStr(m_ctx, hit, "normal", makeVec2(m_ctx, nrm.x, nrm.y));
                JS_SetPropertyStr(m_ctx, hit, "approachSpeed",
                                  JS_NewFloat64(m_ctx, event.approachSpeed));

                JSValue otherGo = wrapOther(other);
                JSValue args[2] = {otherGo, hit};
                invokeHook(m_onCollisionHit, 2, args, "onCollisionHit");
                JS_FreeValue(m_ctx, otherGo);
                JS_FreeValue(m_ctx, hit);
            }

            void JSBehaviour::onSensorBegin(Bokken::GameObject::Collider2D *other)
            {
                if (JS_IsUndefined(m_onSensorBegin)) return;
                JSValue otherGo = wrapOther(other);
                invokeHook(m_onSensorBegin, 1, &otherGo, "onSensorBegin");
                JS_FreeValue(m_ctx, otherGo);
            }

            void JSBehaviour::onSensorEnd(Bokken::GameObject::Collider2D *other)
            {
                if (JS_IsUndefined(m_onSensorEnd)) return;
                JSValue otherGo = wrapOther(other);
                invokeHook(m_onSensorEnd, 1, &otherGo, "onSensorEnd");
                JS_FreeValue(m_ctx, otherGo);
            }
        }
    }
}
