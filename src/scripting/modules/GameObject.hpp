#pragma once

#include "Base.hpp"
#include "JSBehaviour.hpp"
#include "../Engine.hpp"
#include "../../AssetPack.hpp"
#include "../../game_object/Base.hpp"
#include "../../game_object/Camera2D.hpp"
#include "../../game_object/Mesh2D.hpp"
#include "../../game_object/ParticleEmitter2D.hpp"
#include "../../game_object/Rigidbody2D.hpp"
#include "../../game_object/Shape2D.hpp"
#include "../../game_object/Sprite2D.hpp"
#include "../../game_object/Animation2D.hpp"
#include "../../game_object/Distortion2D.hpp"
#include "../../game_object/Light2D.hpp"
#include "../../game_object/ShadowCaster2D.hpp"
#include "../../game_object/NormalMap2D.hpp"
#include "../../game_object/BoxCollider2D.hpp"
#include "../../game_object/CircleCollider2D.hpp"
#include "../../game_object/CapsuleCollider2D.hpp"
#include "../../game_object/PolygonCollider2D.hpp"
#include "../../game_object/EdgeCollider2D.hpp"
#include "../../game_object/ChainCollider2D.hpp"
#include "../../game_object/AudioSource2D.hpp"
#include "../../game_object/AudioListener2D.hpp"
#include "../../renderer/SpriteBatcher.hpp"
#include "../../renderer/TextureCache.hpp"
#include "../../renderer/Base.hpp"
#include "../../game_object/Transform2D.hpp"
#include "../../physics/World.hpp"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstring>

namespace Bokken
{
    namespace Scripting
    {
        namespace Modules
        {
            // Bridges the native GameObject layer into JS via "bokken/gameObject".
            //
            // addComponent returns `this` for chaining and accepts an optional
            // config object as a second argument:
            //
            //   player
            //       .addComponent(Transform2D, { positionX: 5 })
            //       .addComponent(Mesh2D, { shape: Shape2D.Quad, colorR: 1.0 })
            //       .addComponent(Rigidbody2D, { type: "dynamic", linearDamping: 0.1 })
            //       .addComponent(BoxCollider2D, { size: { x: 32, y: 32 } });
            //
            // getComponent returns the wrapped component handle.
            class GameObject : public Base
            {
            public:
                GameObject(SDL_Window *window, AssetPack *assets)
                    : Base("bokken/gameObject"), m_window(window), m_assets(assets) {}

                // Wired in by the Renderer module before the first frame.
                static void setBatcher(Bokken::Renderer::SpriteBatcher *b) { s_batcher = b; }
                static void setTextureCache(Bokken::Renderer::TextureCache *tc) { s_textures = tc; }
                static void setRenderer(Bokken::Renderer::Base *r) { s_renderer = r; }

                int declare(JSContext *ctx, JSModuleDef *m) override;
                int init(JSContext *ctx, JSModuleDef *m) override;

                /* Per-module teardown hook, called by Engine::shutdown() on
                 * every reload AND on final quit while the JSContext is still
                 * alive. Clears the entire GameObject scene: each owned object
                 * is destroyed, running ~JSBehaviour, which frees that
                 * behaviour's cached lifecycle-hook JSValues and its instance
                 * handle. Without this the scene's JS handles would survive
                 * into JS_FreeRuntime and abort on a non-empty gc_obj_list.
                 * The persistent JSClassIDs are intentionally kept — they are
                 * process-global in quickjs-ng and re-registered into the new
                 * runtime by init(). Idempotent. */
                void destroy(JSContext *ctx) override;

                static void update(float deltaTime);
                static void fixedUpdate(float deltaTime);
                static void present();

                // QuickJS C callbacks and helpers — public because file-scope
                // static function list arrays need to reference them directly.
                static inline JSClassID s_class_id = 0;

                static inline JSClassID s_camera2d_class_id = 0;
                static inline JSClassID s_mesh2d_class_id = 0;
                static inline JSClassID s_particle2d_class_id = 0;
                static inline JSClassID s_sprite2d_class_id = 0;
                static inline JSClassID s_animation2d_class_id = 0;
                static inline JSClassID s_distortion2d_class_id = 0;
                static inline JSClassID s_light2d_class_id = 0;
                static inline JSClassID s_shadow_caster2d_class_id = 0;
                static inline JSClassID s_normal_map2d_class_id = 0;
                static inline JSClassID s_transform2d_class_id = 0;
                static inline JSClassID s_rigidbody2d_class_id = 0;
                static inline JSClassID s_box_collider2d_class_id = 0;
                static inline JSClassID s_circle_collider2d_class_id = 0;
                static inline JSClassID s_capsule_collider2d_class_id = 0;
                static inline JSClassID s_polygon_collider2d_class_id = 0;
                static inline JSClassID s_edge_collider2d_class_id = 0;
                static inline JSClassID s_chain_collider2d_class_id = 0;
                static inline JSClassID s_audio_source2d_class_id = 0;
                static inline JSClassID s_audio_listener2d_class_id = 0;

                static inline SDL_Window *s_window = nullptr;
                static inline Bokken::Renderer::SpriteBatcher *s_batcher = nullptr;
                static inline Bokken::Renderer::TextureCache *s_textures = nullptr;
                static inline Bokken::Renderer::Base *s_renderer = nullptr;

                static JSValue js_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);
                static JSValue js_add_component(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_get_component(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_set_parent(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_get_children(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_destroy(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_find(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_get_destroy_when_idle(JSContext *ctx, JSValueConst this_val);
                static JSValue js_set_destroy_when_idle(JSContext *ctx, JSValueConst this_val, JSValueConst val);

                static JSValue js_camera2d_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_camera2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
                static JSValue js_camera2d_screen_to_world_point(JSContext *ctx, JSValueConst this_val,
                                                                 int argc, JSValueConst *argv);
                static JSValue js_camera2d_world_to_screen_point(JSContext *ctx, JSValueConst this_val,
                                                                 int argc, JSValueConst *argv);
                static JSValue wrap_camera2d(JSContext *ctx, Bokken::GameObject::Camera2D *cam);

                static JSValue js_particle2d_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_particle2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
                static JSValue js_particle2d_burst(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue wrap_particle2d(JSContext *ctx, Bokken::GameObject::ParticleEmitter2D *em);

                static JSValue js_transform2d_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_transform2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
                static JSValue js_transform2d_translate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_transform2d_rotate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

                static JSValue js_rigidbody2d_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_rigidbody2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
                static JSValue js_rigidbody2d_apply_force(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_rigidbody2d_apply_force_to_center(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_rigidbody2d_apply_torque(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_rigidbody2d_apply_impulse(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_rigidbody2d_apply_impulse_to_center(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_rigidbody2d_apply_angular_impulse(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_rigidbody2d_set_velocity(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

                static JSValue js_mesh2d_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_mesh2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);

                static JSValue js_sprite2d_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_sprite2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
                static JSValue wrap_sprite2d(JSContext *ctx, Bokken::GameObject::Sprite2D *sprite);

                static JSValue js_animation2d_play(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_animation2d_pause(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_animation2d_stop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_animation2d_resume(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_animation2d_add_clip(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_animation2d_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue wrap_animation2d(JSContext *ctx, Bokken::GameObject::Animation2D *anim);

                static JSValue js_distortion2d_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_distortion2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
                static JSValue js_distortion2d_trigger(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue wrap_distortion2d(JSContext *ctx, Bokken::GameObject::Distortion2D *dist);

                // Light2D — runtime light source with type, color,
                // attenuation, optional shadow casting, optional cookie,
                // and per-light animation envelope (flicker / pulse /
                // strobe / custom).
                static JSValue js_light2d_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_light2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
                static JSValue js_light2d_reset_envelope(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue wrap_light2d(JSContext *ctx, Bokken::GameObject::Light2D *light);

                // ShadowCaster2D — polygonal occluder for shadow casting.
                // Outline is exposed as an array of {x, y} objects on
                // the JS side; the C++ outline vector is rebuilt
                // whenever the JS array setter fires.
                static JSValue js_shadow_caster2d_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_shadow_caster2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
                static JSValue wrap_shadow_caster2d(JSContext *ctx, Bokken::GameObject::ShadowCaster2D *caster);

                // NormalMap2D — authored or auto-generated normal map
                // for the sibling Sprite2D.
                static JSValue js_normal_map2d_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_normal_map2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
                static JSValue js_normal_map2d_invalidate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue wrap_normal_map2d(JSContext *ctx, Bokken::GameObject::NormalMap2D *nm);

                // AudioSource2D — sound source attached to a GameObject.
                static JSValue js_audio_source2d_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_audio_source2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
                static JSValue js_audio_source2d_play(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_audio_source2d_stop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_audio_source2d_pause(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_audio_source2d_resume(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_audio_source2d_play_one_shot(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue js_audio_source2d_play_one_shot_at(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
                static JSValue wrap_audio_source2d(JSContext *ctx, Bokken::GameObject::AudioSource2D *src);

                // AudioListener2D — singleton listener marker.
                static JSValue js_audio_listener2d_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_audio_listener2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
                static JSValue wrap_audio_listener2d(JSContext *ctx, Bokken::GameObject::AudioListener2D *lst);

                // Collider2D family — every concrete collider shares the
                // base material / filter / sensor fields, then layers its
                // own shape-specific getters/setters on top. The base
                // helpers below are called from each concrete getter/setter
                // before the shape-specific switch runs.
                static JSValue js_collider2d_get_base(JSContext *ctx, Bokken::GameObject::Collider2D *col, int magic);
                static JSValue js_collider2d_set_base(JSContext *ctx, Bokken::GameObject::Collider2D *col, JSValueConst val, int magic);

                // Recover a Collider2D* from an opaque JS handle, regardless
                // of which concrete class ID was used. Returns nullptr when
                // the value is not any known collider handle.
                static Bokken::GameObject::Collider2D *unwrap_collider2d(JSValueConst val);

                // Shared JS-side callback setters. Each one inspects the
                // wrapped collider, releases any previously-stored JSValue,
                // and installs a new closure that calls `fn(other, contact?)`
                // when the matching event fires. Pass `null` / `undefined`
                // to clear an installed handler.
                static JSValue js_collider2d_set_on_collision_begin(JSContext *ctx, JSValueConst this_val, JSValueConst val);
                static JSValue js_collider2d_set_on_collision_end(JSContext *ctx, JSValueConst this_val, JSValueConst val);
                static JSValue js_collider2d_set_on_collision_hit(JSContext *ctx, JSValueConst this_val, JSValueConst val);
                static JSValue js_collider2d_set_on_sensor_begin(JSContext *ctx, JSValueConst this_val, JSValueConst val);
                static JSValue js_collider2d_set_on_sensor_end(JSContext *ctx, JSValueConst this_val, JSValueConst val);

                // Per-shape getters / setters and wrap functions.
                static JSValue js_box_collider2d_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_box_collider2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
                static JSValue wrap_box_collider2d(JSContext *ctx, Bokken::GameObject::BoxCollider2D *col);

                static JSValue js_circle_collider2d_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_circle_collider2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
                static JSValue wrap_circle_collider2d(JSContext *ctx, Bokken::GameObject::CircleCollider2D *col);

                static JSValue js_capsule_collider2d_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_capsule_collider2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
                static JSValue wrap_capsule_collider2d(JSContext *ctx, Bokken::GameObject::CapsuleCollider2D *col);

                static JSValue js_polygon_collider2d_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_polygon_collider2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
                static JSValue wrap_polygon_collider2d(JSContext *ctx, Bokken::GameObject::PolygonCollider2D *col);

                static JSValue js_edge_collider2d_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_edge_collider2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
                static JSValue wrap_edge_collider2d(JSContext *ctx, Bokken::GameObject::EdgeCollider2D *col);

                static JSValue js_chain_collider2d_get(JSContext *ctx, JSValueConst this_val, int magic);
                static JSValue js_chain_collider2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
                static JSValue wrap_chain_collider2d(JSContext *ctx, Bokken::GameObject::ChainCollider2D *col);

                static JSValue wrap_transform2d(JSContext *ctx, Bokken::GameObject::Transform2D *t);
                static JSValue wrap_rigidbody2d(JSContext *ctx, Bokken::GameObject::Rigidbody2D *rb);
                static JSValue wrap_mesh2d(JSContext *ctx, Bokken::GameObject::Mesh2D *mesh);
                static void apply_props(JSContext *ctx, JSValue target, JSValue props);
                static JSValue make_vec2(JSContext *ctx, const glm::vec2 &v);
                static bool read_vec2(JSContext *ctx, JSValueConst val, glm::vec2 &out);
                static Bokken::GameObject::Shape2D parse_shape2d(const char *name);
                static const char *shape2d_to_string(Bokken::GameObject::Shape2D shape);

            private:
                SDL_Window *m_window;
                AssetPack *m_assets;
            };
        }
    }
}