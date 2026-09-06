#include "GameObject.hpp"

namespace Bokken
{
    namespace Scripting
    {
        namespace Modules
        {
            enum Camera2DProperties
            {
                C2_Zoom = 0,
                C2_IsActive,
            };

            enum ParticleEmitter2DProperties
            {
                PE2_Emitting = 0,
                PE2_EmitRate,
                PE2_LifetimeMinimum,
                PE2_LifetimeMaximum,
                PE2_SpeedMinimum,
                PE2_SpeedMaximum,
                PE2_SizeStart,
                PE2_SizeEnd,
                PE2_SizeStartVariance,
                PE2_SizeEase,
                PE2_SpreadAngle,
                PE2_Direction,
                PE2_Gravity,
                PE2_Damping,
                PE2_AngularVelocityMinimum,
                PE2_AngularVelocityMaximum,
                PE2_SpawnOffsetX,
                PE2_SpawnOffsetY,
                PE2_VelocityScaleEmission,
                PE2_VelocityReferenceSpeed,
                PE2_ColorStart,
                PE2_ColorEnd,
                PE2_AlphaEase,
                PE2_ZOrder,
                PE2_MaximumParticles,
                PE2_BlendMode,
            };

            enum Transform2DProperties
            {
                T2_PositionX = 0,
                T2_PositionY,
                T2_Rotation,
                T2_ScaleX,
                T2_ScaleY,
                T2_ZOrder
            };

            enum Rigidbody2DProperties
            {
                RB2_Type = 0,
                RB2_FixedRotation,
                RB2_IsBullet,
                RB2_LinearDamping,
                RB2_AngularDamping,
                RB2_GravityScale,
                RB2_AllowSleep,
                RB2_PositionX,
                RB2_PositionY,
                RB2_Rotation,
                RB2_VelocityX,
                RB2_VelocityY,
                RB2_AngularVelocity,
                RB2_Awake,
                RB2_Mass,
                RB2_Inertia,
            };

            enum Mesh2DProperties
            {
                M2_Shape = 0,
                M2_Color, // packed 0xRRGGBBAA
                M2_FlipX,
                M2_FlipY
            };

            enum Sprite2DProperties
            {
                S2_source = 0,
                S2_RegionName,
                S2_Tint,
                S2_Opacity,
                S2_FlipX,
                S2_FlipY,
                S2_OverrideWidth,
                S2_OverrideHeight,
                S2_AnchorX,
                S2_AnchorY,
                S2_BlendMode,
            };

            enum Animation2DProperties
            {
                A2_IsPlaying = 0,
                A2_ActiveClip,
                A2_FrameIndex,
                A2_CurrentRegion,
            };

            enum Distortion2DProperties
            {
                D2_Speed = 0,
                D2_Thickness,
                D2_Amplitude,
                D2_MaximumRadius,
                D2_AutoStart,
            };

            enum Light2DProperties
            {
                L2_Type = 0,
                L2_ColorR,
                L2_ColorG,
                L2_ColorB,
                L2_Color,
                L2_Intensity,
                L2_Range,
                L2_Falloff,
                L2_InnerConeAngle,
                L2_OuterConeAngle,
                L2_DirectionDegrees,
                L2_CastsShadows,
                L2_ShadowSoftness,
                L2_Envelope,
                L2_EnvelopeAmplitude,
                L2_EnvelopeFrequency,
                L2_EnvelopePhase,
                L2_IntensityModulator,
                L2_CookiePath,
                L2_CookieUVOffsetX,
                L2_CookieUVOffsetY,
                L2_CookieUVScaleX,
                L2_CookieUVScaleY,
            };

            enum ShadowCaster2DProperties
            {
                SC2_CastsShadow = 0,
                SC2_Softness,
                SC2_Outline,
            };

            enum NormalMap2DProperties
            {
                NM2_NormalMapPath = 0,
                NM2_AutoGenerate,
                NM2_AutoStrength,
            };

            enum AudioSource2DProperties
            {
                AS2_Clip = 0,
                AS2_Channel,
                AS2_Volume,
                AS2_Pitch,
                AS2_Loop,
                AS2_AutoPlay,
                AS2_Spatial,
                AS2_MinimumDistance,
                AS2_MaximumDistance,
                AS2_Rolloff,
                AS2_Doppler,
                AS2_IsPlaying,
            };

            enum AudioListener2DProperties
            {
                AL2_Gain = 0,
            };

            // Collider2D base properties — common to every shape kind.
            // Concrete colliders extend this set with their own enums
            // starting at Col2_BaseEnd so magic numbers don't collide.
            enum Collider2DBaseProperties
            {
                Col2_Density = 0,
                Col2_Friction,
                Col2_Restitution,
                Col2_TangentSpeed,
                Col2_IsSensor,
                Col2_CategoryBits,
                Col2_MaskBits,
                Col2_GroupIndex,
                Col2_BaseEnd, // first free index for shape-specific entries
            };

            enum BoxCollider2DProperties
            {
                Bx2_SizeX = Col2_BaseEnd,
                Bx2_SizeY,
                Bx2_OffsetX,
                Bx2_OffsetY,
                Bx2_Angle,
            };

            enum CircleCollider2DProperties
            {
                Cc2_Radius = Col2_BaseEnd,
                Cc2_OffsetX,
                Cc2_OffsetY,
            };

            enum CapsuleCollider2DProperties
            {
                Cp2_PointAX = Col2_BaseEnd,
                Cp2_PointAY,
                Cp2_PointBX,
                Cp2_PointBY,
                Cp2_Radius,
            };

            enum PolygonCollider2DProperties
            {
                Pl2_Points = Col2_BaseEnd,
            };

            enum EdgeCollider2DProperties
            {
                Eg2_PointAX = Col2_BaseEnd,
                Eg2_PointAY,
                Eg2_PointBX,
                Eg2_PointBY,
                Eg2_OneSided,
            };

            enum ChainCollider2DProperties
            {
                Ch2_Points = Col2_BaseEnd,
                Ch2_Loop,
            };

            // Static function lists — must not be stack-allocated.
            // QuickJS-NG may retain interior pointers into these arrays after
            // JS_SetPropertyFunctionList returns, so they need static lifetime.
            static const JSCFunctionListEntry s_goProtoFuncs[] = {
                JS_CFUNC_DEF("addComponent", 2, GameObject::js_add_component),
                JS_CFUNC_DEF("getComponent", 1, GameObject::js_get_component),
                JS_CFUNC_DEF("setParent", 1, GameObject::js_set_parent),
                JS_CFUNC_DEF("getChildren", 0, GameObject::js_get_children),
                JS_CGETSET_DEF("destroyWhenIdle", GameObject::js_get_destroy_when_idle, GameObject::js_set_destroy_when_idle),
            };

            static const JSCFunctionListEntry s_goStaticFuncs[] = {
                JS_CFUNC_DEF("destroy", 1, GameObject::js_destroy),
                JS_CFUNC_DEF("find", 1, GameObject::js_find),
            };

            static const JSCFunctionListEntry s_cam2Funcs[] = {
                JS_CGETSET_MAGIC_DEF("zoom", GameObject::js_camera2d_get, GameObject::js_camera2d_set, C2_Zoom),
                JS_CGETSET_MAGIC_DEF("isActive", GameObject::js_camera2d_get, GameObject::js_camera2d_set, C2_IsActive),
                JS_CFUNC_DEF("screenToWorldPoint", 2, GameObject::js_camera2d_screen_to_world_point),
                JS_CFUNC_DEF("worldToScreenPoint", 2, GameObject::js_camera2d_world_to_screen_point),
            };

            static const JSCFunctionListEntry s_pe2Funcs[] = {
                JS_CGETSET_MAGIC_DEF("emitting", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_Emitting),
                JS_CGETSET_MAGIC_DEF("emitRate", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_EmitRate),
                JS_CGETSET_MAGIC_DEF("lifetimeMinimum", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_LifetimeMinimum),
                JS_CGETSET_MAGIC_DEF("lifetimeMaximum", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_LifetimeMaximum),
                JS_CGETSET_MAGIC_DEF("speedMinimum", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_SpeedMinimum),
                JS_CGETSET_MAGIC_DEF("speedMaximum", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_SpeedMaximum),
                JS_CGETSET_MAGIC_DEF("sizeStart", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_SizeStart),
                JS_CGETSET_MAGIC_DEF("sizeEnd", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_SizeEnd),
                JS_CGETSET_MAGIC_DEF("sizeStartVariance", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_SizeStartVariance),
                JS_CGETSET_MAGIC_DEF("sizeEase", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_SizeEase),
                JS_CGETSET_MAGIC_DEF("spreadAngle", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_SpreadAngle),
                JS_CGETSET_MAGIC_DEF("direction", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_Direction),
                JS_CGETSET_MAGIC_DEF("gravity", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_Gravity),
                JS_CGETSET_MAGIC_DEF("damping", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_Damping),
                JS_CGETSET_MAGIC_DEF("angularVelocityMinimum", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_AngularVelocityMinimum),
                JS_CGETSET_MAGIC_DEF("angularVelocityMaximum", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_AngularVelocityMaximum),
                JS_CGETSET_MAGIC_DEF("spawnOffsetX", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_SpawnOffsetX),
                JS_CGETSET_MAGIC_DEF("spawnOffsetY", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_SpawnOffsetY),
                JS_CGETSET_MAGIC_DEF("velocityScaleEmission", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_VelocityScaleEmission),
                JS_CGETSET_MAGIC_DEF("velocityReferenceSpeed", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_VelocityReferenceSpeed),
                JS_CGETSET_MAGIC_DEF("colorStart", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_ColorStart),
                JS_CGETSET_MAGIC_DEF("colorEnd", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_ColorEnd),
                JS_CGETSET_MAGIC_DEF("alphaEase", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_AlphaEase),
                JS_CGETSET_MAGIC_DEF("zOrder", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_ZOrder),
                JS_CGETSET_MAGIC_DEF("maximumParticles", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_MaximumParticles),
                JS_CGETSET_MAGIC_DEF("blendMode", GameObject::js_particle2d_get, GameObject::js_particle2d_set, PE2_BlendMode),
                JS_CFUNC_DEF("burst", 1, GameObject::js_particle2d_burst),
            };

            static const JSCFunctionListEntry s_t2Funcs[] = {
                JS_CGETSET_MAGIC_DEF("positionX", GameObject::js_transform2d_get, GameObject::js_transform2d_set, T2_PositionX),
                JS_CGETSET_MAGIC_DEF("positionY", GameObject::js_transform2d_get, GameObject::js_transform2d_set, T2_PositionY),
                JS_CGETSET_MAGIC_DEF("rotation", GameObject::js_transform2d_get, GameObject::js_transform2d_set, T2_Rotation),
                JS_CGETSET_MAGIC_DEF("scaleX", GameObject::js_transform2d_get, GameObject::js_transform2d_set, T2_ScaleX),
                JS_CGETSET_MAGIC_DEF("scaleY", GameObject::js_transform2d_get, GameObject::js_transform2d_set, T2_ScaleY),
                JS_CGETSET_MAGIC_DEF("zOrder", GameObject::js_transform2d_get, GameObject::js_transform2d_set, T2_ZOrder),
                JS_CFUNC_DEF("translate", 2, GameObject::js_transform2d_translate),
                JS_CFUNC_DEF("rotate", 1, GameObject::js_transform2d_rotate),
            };

            static const JSCFunctionListEntry s_rb2Funcs[] = {
                JS_CGETSET_MAGIC_DEF("type", GameObject::js_rigidbody2d_get, GameObject::js_rigidbody2d_set, RB2_Type),
                JS_CGETSET_MAGIC_DEF("fixedRotation", GameObject::js_rigidbody2d_get, GameObject::js_rigidbody2d_set, RB2_FixedRotation),
                JS_CGETSET_MAGIC_DEF("isBullet", GameObject::js_rigidbody2d_get, GameObject::js_rigidbody2d_set, RB2_IsBullet),
                JS_CGETSET_MAGIC_DEF("linearDamping", GameObject::js_rigidbody2d_get, GameObject::js_rigidbody2d_set, RB2_LinearDamping),
                JS_CGETSET_MAGIC_DEF("angularDamping", GameObject::js_rigidbody2d_get, GameObject::js_rigidbody2d_set, RB2_AngularDamping),
                JS_CGETSET_MAGIC_DEF("gravityScale", GameObject::js_rigidbody2d_get, GameObject::js_rigidbody2d_set, RB2_GravityScale),
                JS_CGETSET_MAGIC_DEF("allowSleep", GameObject::js_rigidbody2d_get, GameObject::js_rigidbody2d_set, RB2_AllowSleep),
                JS_CGETSET_MAGIC_DEF("positionX", GameObject::js_rigidbody2d_get, GameObject::js_rigidbody2d_set, RB2_PositionX),
                JS_CGETSET_MAGIC_DEF("positionY", GameObject::js_rigidbody2d_get, GameObject::js_rigidbody2d_set, RB2_PositionY),
                JS_CGETSET_MAGIC_DEF("rotation", GameObject::js_rigidbody2d_get, GameObject::js_rigidbody2d_set, RB2_Rotation),
                JS_CGETSET_MAGIC_DEF("velocityX", GameObject::js_rigidbody2d_get, GameObject::js_rigidbody2d_set, RB2_VelocityX),
                JS_CGETSET_MAGIC_DEF("velocityY", GameObject::js_rigidbody2d_get, GameObject::js_rigidbody2d_set, RB2_VelocityY),
                JS_CGETSET_MAGIC_DEF("angularVelocity", GameObject::js_rigidbody2d_get, GameObject::js_rigidbody2d_set, RB2_AngularVelocity),
                JS_CGETSET_MAGIC_DEF("awake", GameObject::js_rigidbody2d_get, GameObject::js_rigidbody2d_set, RB2_Awake),
                JS_CGETSET_MAGIC_DEF("mass", GameObject::js_rigidbody2d_get, nullptr, RB2_Mass),
                JS_CGETSET_MAGIC_DEF("inertia", GameObject::js_rigidbody2d_get, nullptr, RB2_Inertia),
                JS_CFUNC_DEF("applyForce", 4, GameObject::js_rigidbody2d_apply_force),
                JS_CFUNC_DEF("applyForceToCenter", 2, GameObject::js_rigidbody2d_apply_force_to_center),
                JS_CFUNC_DEF("applyTorque", 1, GameObject::js_rigidbody2d_apply_torque),
                JS_CFUNC_DEF("applyImpulse", 4, GameObject::js_rigidbody2d_apply_impulse),
                JS_CFUNC_DEF("applyImpulseToCenter", 2, GameObject::js_rigidbody2d_apply_impulse_to_center),
                JS_CFUNC_DEF("applyAngularImpulse", 1, GameObject::js_rigidbody2d_apply_angular_impulse),
                JS_CFUNC_DEF("setVelocity", 2, GameObject::js_rigidbody2d_set_velocity),
            };

            static const JSCFunctionListEntry s_m2Funcs[] = {
                JS_CGETSET_MAGIC_DEF("shape", GameObject::js_mesh2d_get, GameObject::js_mesh2d_set, M2_Shape),
                JS_CGETSET_MAGIC_DEF("color", GameObject::js_mesh2d_get, GameObject::js_mesh2d_set, M2_Color),
                JS_CGETSET_MAGIC_DEF("flipX", GameObject::js_mesh2d_get, GameObject::js_mesh2d_set, M2_FlipX),
                JS_CGETSET_MAGIC_DEF("flipY", GameObject::js_mesh2d_get, GameObject::js_mesh2d_set, M2_FlipY),
            };

            static const JSCFunctionListEntry s_s2Funcs[] = {
                JS_CGETSET_MAGIC_DEF("source", GameObject::js_sprite2d_get, GameObject::js_sprite2d_set, S2_source),
                JS_CGETSET_MAGIC_DEF("regionName", GameObject::js_sprite2d_get, GameObject::js_sprite2d_set, S2_RegionName),
                JS_CGETSET_MAGIC_DEF("tint", GameObject::js_sprite2d_get, GameObject::js_sprite2d_set, S2_Tint),
                JS_CGETSET_MAGIC_DEF("opacity", GameObject::js_sprite2d_get, GameObject::js_sprite2d_set, S2_Opacity),
                JS_CGETSET_MAGIC_DEF("flipX", GameObject::js_sprite2d_get, GameObject::js_sprite2d_set, S2_FlipX),
                JS_CGETSET_MAGIC_DEF("flipY", GameObject::js_sprite2d_get, GameObject::js_sprite2d_set, S2_FlipY),
                JS_CGETSET_MAGIC_DEF("overrideWidth", GameObject::js_sprite2d_get, GameObject::js_sprite2d_set, S2_OverrideWidth),
                JS_CGETSET_MAGIC_DEF("overrideHeight", GameObject::js_sprite2d_get, GameObject::js_sprite2d_set, S2_OverrideHeight),
                JS_CGETSET_MAGIC_DEF("anchorX", GameObject::js_sprite2d_get, GameObject::js_sprite2d_set, S2_AnchorX),
                JS_CGETSET_MAGIC_DEF("anchorY", GameObject::js_sprite2d_get, GameObject::js_sprite2d_set, S2_AnchorY),
                JS_CGETSET_MAGIC_DEF("blendMode", GameObject::js_sprite2d_get, GameObject::js_sprite2d_set, S2_BlendMode),
            };

            static const JSCFunctionListEntry s_a2Funcs[] = {
                JS_CGETSET_MAGIC_DEF("isPlaying", GameObject::js_animation2d_get, nullptr, A2_IsPlaying),
                JS_CGETSET_MAGIC_DEF("activeClip", GameObject::js_animation2d_get, nullptr, A2_ActiveClip),
                JS_CGETSET_MAGIC_DEF("frameIndex", GameObject::js_animation2d_get, nullptr, A2_FrameIndex),
                JS_CGETSET_MAGIC_DEF("currentRegion", GameObject::js_animation2d_get, nullptr, A2_CurrentRegion),
                JS_CFUNC_DEF("play", 1, GameObject::js_animation2d_play),
                JS_CFUNC_DEF("pause", 0, GameObject::js_animation2d_pause),
                JS_CFUNC_DEF("stop", 0, GameObject::js_animation2d_stop),
                JS_CFUNC_DEF("resume", 0, GameObject::js_animation2d_resume),
                JS_CFUNC_DEF("addClip", 1, GameObject::js_animation2d_add_clip),
            };

            static const JSCFunctionListEntry s_d2Funcs[] = {
                JS_CGETSET_MAGIC_DEF("speed", GameObject::js_distortion2d_get, GameObject::js_distortion2d_set, D2_Speed),
                JS_CGETSET_MAGIC_DEF("thickness", GameObject::js_distortion2d_get, GameObject::js_distortion2d_set, D2_Thickness),
                JS_CGETSET_MAGIC_DEF("amplitude", GameObject::js_distortion2d_get, GameObject::js_distortion2d_set, D2_Amplitude),
                JS_CGETSET_MAGIC_DEF("maximumRadius", GameObject::js_distortion2d_get, GameObject::js_distortion2d_set, D2_MaximumRadius),
                JS_CGETSET_MAGIC_DEF("autoStart", GameObject::js_distortion2d_get, GameObject::js_distortion2d_set, D2_AutoStart),
                JS_CFUNC_DEF("trigger", 0, GameObject::js_distortion2d_trigger),
            };

            static const JSCFunctionListEntry s_l2Funcs[] = {
                JS_CGETSET_MAGIC_DEF("type", GameObject::js_light2d_get, GameObject::js_light2d_set, L2_Type),
                JS_CGETSET_MAGIC_DEF("colorR", GameObject::js_light2d_get, GameObject::js_light2d_set, L2_ColorR),
                JS_CGETSET_MAGIC_DEF("colorG", GameObject::js_light2d_get, GameObject::js_light2d_set, L2_ColorG),
                JS_CGETSET_MAGIC_DEF("colorB", GameObject::js_light2d_get, GameObject::js_light2d_set, L2_ColorB),
                JS_CGETSET_MAGIC_DEF("color", GameObject::js_light2d_get, GameObject::js_light2d_set, L2_Color),
                JS_CGETSET_MAGIC_DEF("intensity", GameObject::js_light2d_get, GameObject::js_light2d_set, L2_Intensity),
                JS_CGETSET_MAGIC_DEF("range", GameObject::js_light2d_get, GameObject::js_light2d_set, L2_Range),
                JS_CGETSET_MAGIC_DEF("falloff", GameObject::js_light2d_get, GameObject::js_light2d_set, L2_Falloff),
                JS_CGETSET_MAGIC_DEF("innerConeAngle", GameObject::js_light2d_get, GameObject::js_light2d_set, L2_InnerConeAngle),
                JS_CGETSET_MAGIC_DEF("outerConeAngle", GameObject::js_light2d_get, GameObject::js_light2d_set, L2_OuterConeAngle),
                JS_CGETSET_MAGIC_DEF("directionDegrees", GameObject::js_light2d_get, GameObject::js_light2d_set, L2_DirectionDegrees),
                JS_CGETSET_MAGIC_DEF("castsShadows", GameObject::js_light2d_get, GameObject::js_light2d_set, L2_CastsShadows),
                JS_CGETSET_MAGIC_DEF("shadowSoftness", GameObject::js_light2d_get, GameObject::js_light2d_set, L2_ShadowSoftness),
                JS_CGETSET_MAGIC_DEF("envelope", GameObject::js_light2d_get, GameObject::js_light2d_set, L2_Envelope),
                JS_CGETSET_MAGIC_DEF("envelopeAmplitude", GameObject::js_light2d_get, GameObject::js_light2d_set, L2_EnvelopeAmplitude),
                JS_CGETSET_MAGIC_DEF("envelopeFrequency", GameObject::js_light2d_get, GameObject::js_light2d_set, L2_EnvelopeFrequency),
                JS_CGETSET_MAGIC_DEF("envelopePhase", GameObject::js_light2d_get, GameObject::js_light2d_set, L2_EnvelopePhase),
                JS_CGETSET_MAGIC_DEF("intensityModulator", GameObject::js_light2d_get, GameObject::js_light2d_set, L2_IntensityModulator),
                JS_CGETSET_MAGIC_DEF("cookiePath", GameObject::js_light2d_get, GameObject::js_light2d_set, L2_CookiePath),
                JS_CGETSET_MAGIC_DEF("cookieUVOffsetX", GameObject::js_light2d_get, GameObject::js_light2d_set, L2_CookieUVOffsetX),
                JS_CGETSET_MAGIC_DEF("cookieUVOffsetY", GameObject::js_light2d_get, GameObject::js_light2d_set, L2_CookieUVOffsetY),
                JS_CGETSET_MAGIC_DEF("cookieUVScaleX", GameObject::js_light2d_get, GameObject::js_light2d_set, L2_CookieUVScaleX),
                JS_CGETSET_MAGIC_DEF("cookieUVScaleY", GameObject::js_light2d_get, GameObject::js_light2d_set, L2_CookieUVScaleY),
                JS_CFUNC_DEF("resetEnvelope", 0, GameObject::js_light2d_reset_envelope),
            };

            static const JSCFunctionListEntry s_sc2Funcs[] = {
                JS_CGETSET_MAGIC_DEF("castsShadow", GameObject::js_shadow_caster2d_get, GameObject::js_shadow_caster2d_set, SC2_CastsShadow),
                JS_CGETSET_MAGIC_DEF("softness", GameObject::js_shadow_caster2d_get, GameObject::js_shadow_caster2d_set, SC2_Softness),
                JS_CGETSET_MAGIC_DEF("outline", GameObject::js_shadow_caster2d_get, GameObject::js_shadow_caster2d_set, SC2_Outline),
            };

            static const JSCFunctionListEntry s_nm2Funcs[] = {
                JS_CGETSET_MAGIC_DEF("normalMapPath", GameObject::js_normal_map2d_get, GameObject::js_normal_map2d_set, NM2_NormalMapPath),
                JS_CGETSET_MAGIC_DEF("autoGenerate", GameObject::js_normal_map2d_get, GameObject::js_normal_map2d_set, NM2_AutoGenerate),
                JS_CGETSET_MAGIC_DEF("autoStrength", GameObject::js_normal_map2d_get, GameObject::js_normal_map2d_set, NM2_AutoStrength),
                JS_CFUNC_DEF("invalidate", 0, GameObject::js_normal_map2d_invalidate),
            };

            static const JSCFunctionListEntry s_as2Funcs[] = {
                JS_CGETSET_MAGIC_DEF("clip", GameObject::js_audio_source2d_get, GameObject::js_audio_source2d_set, AS2_Clip),
                JS_CGETSET_MAGIC_DEF("channel", GameObject::js_audio_source2d_get, GameObject::js_audio_source2d_set, AS2_Channel),
                JS_CGETSET_MAGIC_DEF("volume", GameObject::js_audio_source2d_get, GameObject::js_audio_source2d_set, AS2_Volume),
                JS_CGETSET_MAGIC_DEF("pitch", GameObject::js_audio_source2d_get, GameObject::js_audio_source2d_set, AS2_Pitch),
                JS_CGETSET_MAGIC_DEF("loop", GameObject::js_audio_source2d_get, GameObject::js_audio_source2d_set, AS2_Loop),
                JS_CGETSET_MAGIC_DEF("autoPlay", GameObject::js_audio_source2d_get, GameObject::js_audio_source2d_set, AS2_AutoPlay),
                JS_CGETSET_MAGIC_DEF("spatial", GameObject::js_audio_source2d_get, GameObject::js_audio_source2d_set, AS2_Spatial),
                JS_CGETSET_MAGIC_DEF("minimumDistance", GameObject::js_audio_source2d_get, GameObject::js_audio_source2d_set, AS2_MinimumDistance),
                JS_CGETSET_MAGIC_DEF("maximumDistance", GameObject::js_audio_source2d_get, GameObject::js_audio_source2d_set, AS2_MaximumDistance),
                JS_CGETSET_MAGIC_DEF("rolloff", GameObject::js_audio_source2d_get, GameObject::js_audio_source2d_set, AS2_Rolloff),
                JS_CGETSET_MAGIC_DEF("doppler", GameObject::js_audio_source2d_get, GameObject::js_audio_source2d_set, AS2_Doppler),
                JS_CGETSET_MAGIC_DEF("isPlaying", GameObject::js_audio_source2d_get, nullptr, AS2_IsPlaying),
                JS_CFUNC_DEF("play", 0, GameObject::js_audio_source2d_play),
                JS_CFUNC_DEF("stop", 1, GameObject::js_audio_source2d_stop),
                JS_CFUNC_DEF("pause", 0, GameObject::js_audio_source2d_pause),
                JS_CFUNC_DEF("resume", 0, GameObject::js_audio_source2d_resume),
                JS_CFUNC_DEF("playOneShot", 1, GameObject::js_audio_source2d_play_one_shot),
                JS_CFUNC_DEF("playOneShotAt", 3, GameObject::js_audio_source2d_play_one_shot_at),
            };

            static const JSCFunctionListEntry s_al2Funcs[] = {
                JS_CGETSET_MAGIC_DEF("gain", GameObject::js_audio_listener2d_get, GameObject::js_audio_listener2d_set, AL2_Gain),
            };

// Shared callback property entries — used by every concrete
// collider's prototype. Setting any of these to a function
// installs a JS-side handler; setting to null/undefined clears
// it. These are placed at the start of every collider's
// function list so the property lookup hits them before the
// shape-specific getters.
#define BOKKEN_COLLIDER_BASE_FUNC_ENTRIES(GET, SET)                                                    \
    JS_CGETSET_MAGIC_DEF("density", GET, SET, Col2_Density),                                           \
        JS_CGETSET_MAGIC_DEF("friction", GET, SET, Col2_Friction),                                     \
        JS_CGETSET_MAGIC_DEF("restitution", GET, SET, Col2_Restitution),                               \
        JS_CGETSET_MAGIC_DEF("tangentSpeed", GET, SET, Col2_TangentSpeed),                             \
        JS_CGETSET_MAGIC_DEF("isSensor", GET, SET, Col2_IsSensor),                                     \
        JS_CGETSET_MAGIC_DEF("categoryBits", GET, SET, Col2_CategoryBits),                             \
        JS_CGETSET_MAGIC_DEF("maskBits", GET, SET, Col2_MaskBits),                                     \
        JS_CGETSET_MAGIC_DEF("groupIndex", GET, SET, Col2_GroupIndex),                                 \
        JS_CGETSET_DEF("onCollisionEnter", nullptr, GameObject::js_collider2d_set_on_collision_begin), \
        JS_CGETSET_DEF("onCollisionExit", nullptr, GameObject::js_collider2d_set_on_collision_end),    \
        JS_CGETSET_DEF("onCollisionHit", nullptr, GameObject::js_collider2d_set_on_collision_hit),     \
        JS_CGETSET_DEF("onSensorEnter", nullptr, GameObject::js_collider2d_set_on_sensor_begin),       \
        JS_CGETSET_DEF("onSensorExit", nullptr, GameObject::js_collider2d_set_on_sensor_end)

            static const JSCFunctionListEntry s_box2Funcs[] = {
                BOKKEN_COLLIDER_BASE_FUNC_ENTRIES(GameObject::js_box_collider2d_get, GameObject::js_box_collider2d_set),
                JS_CGETSET_MAGIC_DEF("sizeX", GameObject::js_box_collider2d_get, GameObject::js_box_collider2d_set, Bx2_SizeX),
                JS_CGETSET_MAGIC_DEF("sizeY", GameObject::js_box_collider2d_get, GameObject::js_box_collider2d_set, Bx2_SizeY),
                JS_CGETSET_MAGIC_DEF("offsetX", GameObject::js_box_collider2d_get, GameObject::js_box_collider2d_set, Bx2_OffsetX),
                JS_CGETSET_MAGIC_DEF("offsetY", GameObject::js_box_collider2d_get, GameObject::js_box_collider2d_set, Bx2_OffsetY),
                JS_CGETSET_MAGIC_DEF("angle", GameObject::js_box_collider2d_get, GameObject::js_box_collider2d_set, Bx2_Angle),
            };

            static const JSCFunctionListEntry s_circle2Funcs[] = {
                BOKKEN_COLLIDER_BASE_FUNC_ENTRIES(GameObject::js_circle_collider2d_get, GameObject::js_circle_collider2d_set),
                JS_CGETSET_MAGIC_DEF("radius", GameObject::js_circle_collider2d_get, GameObject::js_circle_collider2d_set, Cc2_Radius),
                JS_CGETSET_MAGIC_DEF("offsetX", GameObject::js_circle_collider2d_get, GameObject::js_circle_collider2d_set, Cc2_OffsetX),
                JS_CGETSET_MAGIC_DEF("offsetY", GameObject::js_circle_collider2d_get, GameObject::js_circle_collider2d_set, Cc2_OffsetY),
            };

            static const JSCFunctionListEntry s_capsule2Funcs[] = {
                BOKKEN_COLLIDER_BASE_FUNC_ENTRIES(GameObject::js_capsule_collider2d_get, GameObject::js_capsule_collider2d_set),
                JS_CGETSET_MAGIC_DEF("pointAX", GameObject::js_capsule_collider2d_get, GameObject::js_capsule_collider2d_set, Cp2_PointAX),
                JS_CGETSET_MAGIC_DEF("pointAY", GameObject::js_capsule_collider2d_get, GameObject::js_capsule_collider2d_set, Cp2_PointAY),
                JS_CGETSET_MAGIC_DEF("pointBX", GameObject::js_capsule_collider2d_get, GameObject::js_capsule_collider2d_set, Cp2_PointBX),
                JS_CGETSET_MAGIC_DEF("pointBY", GameObject::js_capsule_collider2d_get, GameObject::js_capsule_collider2d_set, Cp2_PointBY),
                JS_CGETSET_MAGIC_DEF("radius", GameObject::js_capsule_collider2d_get, GameObject::js_capsule_collider2d_set, Cp2_Radius),
            };

            static const JSCFunctionListEntry s_polygon2Funcs[] = {
                BOKKEN_COLLIDER_BASE_FUNC_ENTRIES(GameObject::js_polygon_collider2d_get, GameObject::js_polygon_collider2d_set),
                JS_CGETSET_MAGIC_DEF("points", GameObject::js_polygon_collider2d_get, GameObject::js_polygon_collider2d_set, Pl2_Points),
            };

            static const JSCFunctionListEntry s_edge2Funcs[] = {
                BOKKEN_COLLIDER_BASE_FUNC_ENTRIES(GameObject::js_edge_collider2d_get, GameObject::js_edge_collider2d_set),
                JS_CGETSET_MAGIC_DEF("pointAX", GameObject::js_edge_collider2d_get, GameObject::js_edge_collider2d_set, Eg2_PointAX),
                JS_CGETSET_MAGIC_DEF("pointAY", GameObject::js_edge_collider2d_get, GameObject::js_edge_collider2d_set, Eg2_PointAY),
                JS_CGETSET_MAGIC_DEF("pointBX", GameObject::js_edge_collider2d_get, GameObject::js_edge_collider2d_set, Eg2_PointBX),
                JS_CGETSET_MAGIC_DEF("pointBY", GameObject::js_edge_collider2d_get, GameObject::js_edge_collider2d_set, Eg2_PointBY),
                JS_CGETSET_MAGIC_DEF("oneSided", GameObject::js_edge_collider2d_get, GameObject::js_edge_collider2d_set, Eg2_OneSided),
            };

            static const JSCFunctionListEntry s_chain2Funcs[] = {
                BOKKEN_COLLIDER_BASE_FUNC_ENTRIES(GameObject::js_chain_collider2d_get, GameObject::js_chain_collider2d_set),
                JS_CGETSET_MAGIC_DEF("points", GameObject::js_chain_collider2d_get, GameObject::js_chain_collider2d_set, Ch2_Points),
                JS_CGETSET_MAGIC_DEF("loop", GameObject::js_chain_collider2d_get, GameObject::js_chain_collider2d_set, Ch2_Loop),
            };

#undef BOKKEN_COLLIDER_BASE_FUNC_ENTRIES

            int GameObject::declare(JSContext *ctx, JSModuleDef *m)
            {
                JSRuntime *rt = JS_GetRuntime(ctx);

                JS_NewClassID(rt, &s_class_id);
                JS_NewClassID(rt, &s_camera2d_class_id);
                JS_NewClassID(rt, &s_particle2d_class_id);
                JS_NewClassID(rt, &s_mesh2d_class_id);
                JS_NewClassID(rt, &s_sprite2d_class_id);
                JS_NewClassID(rt, &s_animation2d_class_id);
                JS_NewClassID(rt, &s_distortion2d_class_id);
                JS_NewClassID(rt, &s_light2d_class_id);
                JS_NewClassID(rt, &s_shadow_caster2d_class_id);
                JS_NewClassID(rt, &s_normal_map2d_class_id);
                JS_NewClassID(rt, &s_rigidbody2d_class_id);
                JS_NewClassID(rt, &s_transform2d_class_id);
                JS_NewClassID(rt, &s_box_collider2d_class_id);
                JS_NewClassID(rt, &s_circle_collider2d_class_id);
                JS_NewClassID(rt, &s_capsule_collider2d_class_id);
                JS_NewClassID(rt, &s_polygon_collider2d_class_id);
                JS_NewClassID(rt, &s_edge_collider2d_class_id);
                JS_NewClassID(rt, &s_chain_collider2d_class_id);
                JS_NewClassID(rt, &s_audio_source2d_class_id);
                JS_NewClassID(rt, &s_audio_listener2d_class_id);

                static JSClassDef goClass = {"GameObject", .finalizer = nullptr};
                static JSClassDef cam2Class = {"Camera2D", .finalizer = nullptr};
                static JSClassDef pe2Class = {"ParticleEmitter2D", .finalizer = nullptr};
                static JSClassDef m2Class = {"Mesh2D", .finalizer = nullptr};
                static JSClassDef s2Class = {"Sprite2D", .finalizer = nullptr};
                static JSClassDef a2Class = {"Animation2D", .finalizer = nullptr};
                static JSClassDef d2Class = {"Distortion2D", .finalizer = nullptr};
                static JSClassDef l2Class = {"Light2D", .finalizer = nullptr};
                static JSClassDef sc2Class = {"ShadowCaster2D", .finalizer = nullptr};
                static JSClassDef nm2Class = {"NormalMap2D", .finalizer = nullptr};
                static JSClassDef rb2Class = {"Rigidbody2D", .finalizer = nullptr};
                static JSClassDef t2Class = {"Transform2D", .finalizer = nullptr};

                // Collider classes carry no finalizer either: the C++
                // component is owned by the GameObject's component map,
                // and the JS handle is just a non-owning view.
                static JSClassDef boxColClass = {"BoxCollider2D", .finalizer = nullptr};
                static JSClassDef circleColClass = {"CircleCollider2D", .finalizer = nullptr};
                static JSClassDef capsuleColClass = {"CapsuleCollider2D", .finalizer = nullptr};
                static JSClassDef polygonColClass = {"PolygonCollider2D", .finalizer = nullptr};
                static JSClassDef edgeColClass = {"EdgeCollider2D", .finalizer = nullptr};
                static JSClassDef chainColClass = {"ChainCollider2D", .finalizer = nullptr};

                static JSClassDef audioSrc2Class = {"AudioSource2D", .finalizer = nullptr};
                static JSClassDef audioLst2Class = {"AudioListener2D", .finalizer = nullptr};

                JS_NewClass(rt, s_class_id, &goClass);
                JS_NewClass(rt, s_camera2d_class_id, &cam2Class);
                JS_NewClass(rt, s_particle2d_class_id, &pe2Class);
                JS_NewClass(rt, s_mesh2d_class_id, &m2Class);
                JS_NewClass(rt, s_sprite2d_class_id, &s2Class);
                JS_NewClass(rt, s_animation2d_class_id, &a2Class);
                JS_NewClass(rt, s_distortion2d_class_id, &d2Class);
                JS_NewClass(rt, s_light2d_class_id, &l2Class);
                JS_NewClass(rt, s_shadow_caster2d_class_id, &sc2Class);
                JS_NewClass(rt, s_normal_map2d_class_id, &nm2Class);
                JS_NewClass(rt, s_rigidbody2d_class_id, &rb2Class);
                JS_NewClass(rt, s_transform2d_class_id, &t2Class);
                JS_NewClass(rt, s_box_collider2d_class_id, &boxColClass);
                JS_NewClass(rt, s_circle_collider2d_class_id, &circleColClass);
                JS_NewClass(rt, s_capsule_collider2d_class_id, &capsuleColClass);
                JS_NewClass(rt, s_polygon_collider2d_class_id, &polygonColClass);
                JS_NewClass(rt, s_edge_collider2d_class_id, &edgeColClass);
                JS_NewClass(rt, s_chain_collider2d_class_id, &chainColClass);
                JS_NewClass(rt, s_audio_source2d_class_id, &audioSrc2Class);
                JS_NewClass(rt, s_audio_listener2d_class_id, &audioLst2Class);

                JS_AddModuleExport(ctx, m, "Camera2D");
                JS_AddModuleExport(ctx, m, "ParticleEmitter2D");
                JS_AddModuleExport(ctx, m, "Mesh2D");
                JS_AddModuleExport(ctx, m, "Sprite2D");
                JS_AddModuleExport(ctx, m, "Animation2D");
                JS_AddModuleExport(ctx, m, "Distortion2D");
                JS_AddModuleExport(ctx, m, "Light2D");
                JS_AddModuleExport(ctx, m, "ShadowCaster2D");
                JS_AddModuleExport(ctx, m, "NormalMap2D");
                JS_AddModuleExport(ctx, m, "LightType");
                JS_AddModuleExport(ctx, m, "LightEnvelope");
                JS_AddModuleExport(ctx, m, "Rigidbody2D");
                JS_AddModuleExport(ctx, m, "Shape2D");
                JS_AddModuleExport(ctx, m, "AnimationLoopMode");
                JS_AddModuleExport(ctx, m, "BlendMode");
                JS_AddModuleExport(ctx, m, "Transform2D");
                JS_AddModuleExport(ctx, m, "BoxCollider2D");
                JS_AddModuleExport(ctx, m, "CircleCollider2D");
                JS_AddModuleExport(ctx, m, "CapsuleCollider2D");
                JS_AddModuleExport(ctx, m, "PolygonCollider2D");
                JS_AddModuleExport(ctx, m, "EdgeCollider2D");
                JS_AddModuleExport(ctx, m, "ChainCollider2D");
                JS_AddModuleExport(ctx, m, "AudioSource2D");
                JS_AddModuleExport(ctx, m, "AudioListener2D");
                JS_AddModuleExport(ctx, m, "Behaviour");

                return JS_AddModuleExport(ctx, m, "GameObject");
            }

            int GameObject::init(JSContext *ctx, JSModuleDef *m)
            {
                // GameObject prototype and constructor.
                JSValue goProto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, goProto, s_goProtoFuncs,
                                           sizeof(s_goProtoFuncs) / sizeof(s_goProtoFuncs[0]));

                JSValue goCtor = JS_NewCFunction2(ctx, GameObject::js_constructor,
                                                  "GameObject", 1, JS_CFUNC_constructor, 0);
                JS_SetPropertyFunctionList(ctx, goCtor, s_goStaticFuncs,
                                           sizeof(s_goStaticFuncs) / sizeof(s_goStaticFuncs[0]));

                JS_SetConstructor(ctx, goCtor, goProto);
                JS_SetClassProto(ctx, s_class_id, goProto);

                // Camera2D prototype.
                JSValue cam2Proto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, cam2Proto, s_cam2Funcs,
                                           sizeof(s_cam2Funcs) / sizeof(s_cam2Funcs[0]));
                JS_SetClassProto(ctx, s_camera2d_class_id, cam2Proto);

                JSValue pe2Proto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, pe2Proto, s_pe2Funcs,
                                           sizeof(s_pe2Funcs) / sizeof(s_pe2Funcs[0]));
                JS_SetClassProto(ctx, s_particle2d_class_id, pe2Proto);

                // Mesh2D prototype.
                JSValue m2Proto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, m2Proto, s_m2Funcs,
                                           sizeof(s_m2Funcs) / sizeof(s_m2Funcs[0]));
                JS_SetClassProto(ctx, s_mesh2d_class_id, m2Proto);

                // Sprite2D prototype.
                JSValue s2Proto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, s2Proto, s_s2Funcs,
                                           sizeof(s_s2Funcs) / sizeof(s_s2Funcs[0]));
                JS_SetClassProto(ctx, s_sprite2d_class_id, s2Proto);

                // Animation2D prototype.
                JSValue a2Proto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, a2Proto, s_a2Funcs,
                                           sizeof(s_a2Funcs) / sizeof(s_a2Funcs[0]));
                JS_SetClassProto(ctx, s_animation2d_class_id, a2Proto);

                // Distortion2D prototype.
                JSValue d2Proto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, d2Proto, s_d2Funcs,
                                           sizeof(s_d2Funcs) / sizeof(s_d2Funcs[0]));
                JS_SetClassProto(ctx, s_distortion2d_class_id, d2Proto);

                // Light2D prototype.
                JSValue l2Proto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, l2Proto, s_l2Funcs,
                                           sizeof(s_l2Funcs) / sizeof(s_l2Funcs[0]));
                JS_SetClassProto(ctx, s_light2d_class_id, l2Proto);

                // ShadowCaster2D prototype.
                JSValue sc2Proto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, sc2Proto, s_sc2Funcs,
                                           sizeof(s_sc2Funcs) / sizeof(s_sc2Funcs[0]));
                JS_SetClassProto(ctx, s_shadow_caster2d_class_id, sc2Proto);

                // NormalMap2D prototype.
                JSValue nm2Proto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, nm2Proto, s_nm2Funcs,
                                           sizeof(s_nm2Funcs) / sizeof(s_nm2Funcs[0]));
                JS_SetClassProto(ctx, s_normal_map2d_class_id, nm2Proto);

                // Rigidbody2D prototype.
                JSValue rb2Proto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, rb2Proto, s_rb2Funcs,
                                           sizeof(s_rb2Funcs) / sizeof(s_rb2Funcs[0]));
                JS_SetClassProto(ctx, s_rigidbody2d_class_id, rb2Proto);

                // Transform2D prototype.
                JSValue t2Proto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, t2Proto, s_t2Funcs,
                                           sizeof(s_t2Funcs) / sizeof(s_t2Funcs[0]));
                JS_SetClassProto(ctx, s_transform2d_class_id, t2Proto);

                // Collider prototypes — one per concrete shape type.
                JSValue boxColProto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, boxColProto, s_box2Funcs,
                                           sizeof(s_box2Funcs) / sizeof(s_box2Funcs[0]));
                JS_SetClassProto(ctx, s_box_collider2d_class_id, boxColProto);

                JSValue circleColProto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, circleColProto, s_circle2Funcs,
                                           sizeof(s_circle2Funcs) / sizeof(s_circle2Funcs[0]));
                JS_SetClassProto(ctx, s_circle_collider2d_class_id, circleColProto);

                JSValue capsuleColProto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, capsuleColProto, s_capsule2Funcs,
                                           sizeof(s_capsule2Funcs) / sizeof(s_capsule2Funcs[0]));
                JS_SetClassProto(ctx, s_capsule_collider2d_class_id, capsuleColProto);

                JSValue polygonColProto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, polygonColProto, s_polygon2Funcs,
                                           sizeof(s_polygon2Funcs) / sizeof(s_polygon2Funcs[0]));
                JS_SetClassProto(ctx, s_polygon_collider2d_class_id, polygonColProto);

                JSValue edgeColProto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, edgeColProto, s_edge2Funcs,
                                           sizeof(s_edge2Funcs) / sizeof(s_edge2Funcs[0]));
                JS_SetClassProto(ctx, s_edge_collider2d_class_id, edgeColProto);

                JSValue chainColProto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, chainColProto, s_chain2Funcs,
                                           sizeof(s_chain2Funcs) / sizeof(s_chain2Funcs[0]));
                JS_SetClassProto(ctx, s_chain_collider2d_class_id, chainColProto);

                // Audio component prototypes.
                JSValue audioSrc2Proto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, audioSrc2Proto, s_as2Funcs,
                                           sizeof(s_as2Funcs) / sizeof(s_as2Funcs[0]));
                JS_SetClassProto(ctx, s_audio_source2d_class_id, audioSrc2Proto);

                JSValue audioLst2Proto = JS_NewObject(ctx);
                JS_SetPropertyFunctionList(ctx, audioLst2Proto, s_al2Funcs,
                                           sizeof(s_al2Funcs) / sizeof(s_al2Funcs[0]));
                JS_SetClassProto(ctx, s_audio_listener2d_class_id, audioLst2Proto);

                // Component tokens.
                JSValue cam2Token = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, cam2Token, "name", JS_NewString(ctx, "Camera2D"));

                JSValue pe2Token = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, pe2Token, "name", JS_NewString(ctx, "ParticleEmitter2D"));

                JSValue m2Token = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, m2Token, "name", JS_NewString(ctx, "Mesh2D"));

                JSValue s2Token = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, s2Token, "name", JS_NewString(ctx, "Sprite2D"));

                JSValue a2Token = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, a2Token, "name", JS_NewString(ctx, "Animation2D"));

                JSValue d2Token = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, d2Token, "name", JS_NewString(ctx, "Distortion2D"));

                JSValue l2Token = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, l2Token, "name", JS_NewString(ctx, "Light2D"));

                JSValue sc2Token = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, sc2Token, "name", JS_NewString(ctx, "ShadowCaster2D"));

                JSValue nm2Token = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, nm2Token, "name", JS_NewString(ctx, "NormalMap2D"));

                // LightType + LightEnvelope enums. Exported as plain
                // objects whose values are the same strings that
                // js_light2d_set parses on assignment. Matches the
                // existing BlendMode / Shape2D string-enum convention.
                JSValue lightType = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, lightType, "Point", JS_NewString(ctx, "Point"));
                JS_SetPropertyStr(ctx, lightType, "Spot", JS_NewString(ctx, "Spot"));
                JS_SetPropertyStr(ctx, lightType, "Directional", JS_NewString(ctx, "Directional"));

                JSValue lightEnvelope = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, lightEnvelope, "Constant", JS_NewString(ctx, "Constant"));
                JS_SetPropertyStr(ctx, lightEnvelope, "Flicker", JS_NewString(ctx, "Flicker"));
                JS_SetPropertyStr(ctx, lightEnvelope, "Pulse", JS_NewString(ctx, "Pulse"));
                JS_SetPropertyStr(ctx, lightEnvelope, "Strobe", JS_NewString(ctx, "Strobe"));
                JS_SetPropertyStr(ctx, lightEnvelope, "Custom", JS_NewString(ctx, "Custom"));

                JSValue rb2Token = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, rb2Token, "name", JS_NewString(ctx, "Rigidbody2D"));

                JSValue t2Token = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, t2Token, "name", JS_NewString(ctx, "Transform2D"));

                // Collider tokens — addComponent dispatches on `name`.
                JSValue boxColToken = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, boxColToken, "name", JS_NewString(ctx, "BoxCollider2D"));

                JSValue circleColToken = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, circleColToken, "name", JS_NewString(ctx, "CircleCollider2D"));

                JSValue capsuleColToken = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, capsuleColToken, "name", JS_NewString(ctx, "CapsuleCollider2D"));

                JSValue polygonColToken = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, polygonColToken, "name", JS_NewString(ctx, "PolygonCollider2D"));

                JSValue edgeColToken = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, edgeColToken, "name", JS_NewString(ctx, "EdgeCollider2D"));

                JSValue chainColToken = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, chainColToken, "name", JS_NewString(ctx, "ChainCollider2D"));

                // Audio tokens — addComponent dispatches on `name`.
                JSValue audioSrc2Token = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, audioSrc2Token, "name", JS_NewString(ctx, "AudioSource2D"));

                JSValue audioLst2Token = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, audioLst2Token, "name", JS_NewString(ctx, "AudioListener2D"));

                // AnimationLoopMode enum.
                JSValue animationLoopMode = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, animationLoopMode, "None", JS_NewString(ctx, "None"));
                JS_SetPropertyStr(ctx, animationLoopMode, "Loop", JS_NewString(ctx, "Loop"));
                JS_SetPropertyStr(ctx, animationLoopMode, "PingPong", JS_NewString(ctx, "PingPong"));

                // Shape2D enum.
                JSValue shape2d = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, shape2d, "Empty", JS_NewString(ctx, "Empty"));
                JS_SetPropertyStr(ctx, shape2d, "Quad", JS_NewString(ctx, "Quad"));
                JS_SetPropertyStr(ctx, shape2d, "Circle", JS_NewString(ctx, "Circle"));
                JS_SetPropertyStr(ctx, shape2d, "Triangle", JS_NewString(ctx, "Triangle"));
                JS_SetPropertyStr(ctx, shape2d, "Line", JS_NewString(ctx, "Line"));

                // BlendMode enum.
                JSValue blendMode = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, blendMode, "Alpha", JS_NewString(ctx, "Alpha"));
                JS_SetPropertyStr(ctx, blendMode, "Additive", JS_NewString(ctx, "Additive"));
                JS_SetPropertyStr(ctx, blendMode, "Screen", JS_NewString(ctx, "Screen"));

                // Behaviour — a do-nothing base class for user scripts.
                // The engine doesn't inspect the inheritance chain at all;
                // it just looks up method names on each instance via
                // JS_GetPropertyStr. This base class exists purely so
                // `class MyBehaviour extends Behaviour { ... }` compiles
                // and runs — the prototype contributes no behaviour of
                // its own. Built via a small eval rather than the
                // QuickJS class C API: ~10 lines vs ~50, and a
                // user-defined constructor in the subclass overrides
                // anything we'd put here anyway.
                JSValue behaviourCls = JS_Eval(ctx,
                                               "(class Behaviour {})",
                                               20, // strlen of the source above
                                               "<bokken/gameObject:Behaviour>",
                                               JS_EVAL_TYPE_GLOBAL);
                if (JS_IsException(behaviourCls))
                {
                    // Should never fire — the source is fixed and trivial.
                    // Log and substitute null so the module still loads.
                    JSValue exc = JS_GetException(ctx);
                    const char *str = JS_ToCString(ctx, exc);
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                 "[bokken/gameObject] failed to build Behaviour class: %s",
                                 str ? str : "<unknown>");
                    if (str)
                        JS_FreeCString(ctx, str);
                    JS_FreeValue(ctx, exc);
                    behaviourCls = JS_NULL;
                }

                JS_SetModuleExport(ctx, m, "GameObject", goCtor);

                JS_SetModuleExport(ctx, m, "Camera2D", cam2Token);
                JS_SetModuleExport(ctx, m, "ParticleEmitter2D", pe2Token);
                JS_SetModuleExport(ctx, m, "Mesh2D", m2Token);
                JS_SetModuleExport(ctx, m, "Sprite2D", s2Token);
                JS_SetModuleExport(ctx, m, "Animation2D", a2Token);
                JS_SetModuleExport(ctx, m, "Distortion2D", d2Token);
                JS_SetModuleExport(ctx, m, "Light2D", l2Token);
                JS_SetModuleExport(ctx, m, "ShadowCaster2D", sc2Token);
                JS_SetModuleExport(ctx, m, "NormalMap2D", nm2Token);
                JS_SetModuleExport(ctx, m, "LightType", lightType);
                JS_SetModuleExport(ctx, m, "LightEnvelope", lightEnvelope);
                JS_SetModuleExport(ctx, m, "Rigidbody2D", rb2Token);
                JS_SetModuleExport(ctx, m, "AnimationLoopMode", animationLoopMode);
                JS_SetModuleExport(ctx, m, "Shape2D", shape2d);
                JS_SetModuleExport(ctx, m, "BlendMode", blendMode);
                JS_SetModuleExport(ctx, m, "Transform2D", t2Token);
                JS_SetModuleExport(ctx, m, "BoxCollider2D", boxColToken);
                JS_SetModuleExport(ctx, m, "CircleCollider2D", circleColToken);
                JS_SetModuleExport(ctx, m, "CapsuleCollider2D", capsuleColToken);
                JS_SetModuleExport(ctx, m, "PolygonCollider2D", polygonColToken);
                JS_SetModuleExport(ctx, m, "EdgeCollider2D", edgeColToken);
                JS_SetModuleExport(ctx, m, "ChainCollider2D", chainColToken);
                JS_SetModuleExport(ctx, m, "AudioSource2D", audioSrc2Token);
                JS_SetModuleExport(ctx, m, "AudioListener2D", audioLst2Token);
                JS_SetModuleExport(ctx, m, "Behaviour", behaviourCls);

                return 0;
            }

            // JS: new GameObject(name?, opts?)
            //     opts: { destroyWhenIdle?: boolean }
            JSValue GameObject::js_constructor(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                JSValue obj = JS_NewObjectClass(ctx, s_class_id);
                if (JS_IsException(obj))
                    return obj;

                std::string name = "Untitled";
                if (argc > 0 && JS_IsString(argv[0]))
                {
                    const char *s = JS_ToCString(ctx, argv[0]);
                    if (s)
                    {
                        name = s;
                        JS_FreeCString(ctx, s);
                    }
                }

                auto go = std::make_unique<Bokken::GameObject::Base>(name);
                Bokken::GameObject::Base *raw = go.get();

                // Apply optional metadata from the second argument.
                if (argc > 1 && JS_IsObject(argv[1]))
                {
                    JSValue v = JS_GetPropertyStr(ctx, argv[1], "destroyWhenIdle");
                    if (JS_IsBool(v))
                        raw->destroyWhenIdle = JS_ToBool(ctx, v);
                    JS_FreeValue(ctx, v);
                }

                Bokken::GameObject::Base::s_objects.push_back(std::move(go));

                JS_SetOpaque(obj, raw);
                return obj;
            }

            // JS: gameObject.addComponent(Token, props?) — returns `this` for chaining.
            JSValue GameObject::js_add_component(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *go = static_cast<Bokken::GameObject::Base *>(JS_GetOpaque(this_val, s_class_id));
                if (!go || argc < 1)
                    return JS_UNDEFINED;

                JSValue nameVal = JS_GetPropertyStr(ctx, argv[0], "name");
                const char *className = JS_ToCString(ctx, nameVal);
                JS_FreeValue(ctx, nameVal);
                if (!className)
                    return JS_UNDEFINED;

                JSValue wrapped = JS_UNDEFINED;

                if (strcmp(className, "Transform2D") == 0)
                {
                    auto &t = go->addComponent<Bokken::GameObject::Transform2D>();
                    wrapped = wrap_transform2d(ctx, &t);
                }
                else if (strcmp(className, "Rigidbody2D") == 0)
                {
                    auto &rb = go->addComponent<Bokken::GameObject::Rigidbody2D>();
                    wrapped = wrap_rigidbody2d(ctx, &rb);
                }
                else if (strcmp(className, "Mesh2D") == 0)
                {
                    auto &mesh = go->addComponent<Bokken::GameObject::Mesh2D>();
                    wrapped = wrap_mesh2d(ctx, &mesh);
                }
                else if (strcmp(className, "Sprite2D") == 0)
                {
                    auto &sprite = go->addComponent<Bokken::GameObject::Sprite2D>();
                    wrapped = wrap_sprite2d(ctx, &sprite);
                }
                else if (strcmp(className, "Animation2D") == 0)
                {
                    auto &anim = go->addComponent<Bokken::GameObject::Animation2D>();
                    wrapped = wrap_animation2d(ctx, &anim);
                }
                else if (strcmp(className, "Distortion2D") == 0)
                {
                    auto &dist = go->addComponent<Bokken::GameObject::Distortion2D>();
                    wrapped = wrap_distortion2d(ctx, &dist);
                }
                else if (strcmp(className, "Light2D") == 0)
                {
                    auto &light = go->addComponent<Bokken::GameObject::Light2D>();
                    wrapped = wrap_light2d(ctx, &light);
                }
                else if (strcmp(className, "ShadowCaster2D") == 0)
                {
                    auto &caster = go->addComponent<Bokken::GameObject::ShadowCaster2D>();
                    wrapped = wrap_shadow_caster2d(ctx, &caster);
                }
                else if (strcmp(className, "NormalMap2D") == 0)
                {
                    auto &nm = go->addComponent<Bokken::GameObject::NormalMap2D>();
                    wrapped = wrap_normal_map2d(ctx, &nm);
                }
                else if (strcmp(className, "Camera2D") == 0)
                {
                    auto &cam = go->addComponent<Bokken::GameObject::Camera2D>();
                    wrapped = wrap_camera2d(ctx, &cam);
                }
                else if (strcmp(className, "ParticleEmitter2D") == 0)
                {
                    auto &em = go->addComponent<Bokken::GameObject::ParticleEmitter2D>();
                    wrapped = wrap_particle2d(ctx, &em);
                }
                else if (strcmp(className, "BoxCollider2D") == 0)
                {
                    auto &col = go->addComponentDeferred<Bokken::GameObject::BoxCollider2D>();
                    wrapped = wrap_box_collider2d(ctx, &col);
                }
                else if (strcmp(className, "CircleCollider2D") == 0)
                {
                    auto &col = go->addComponentDeferred<Bokken::GameObject::CircleCollider2D>();
                    wrapped = wrap_circle_collider2d(ctx, &col);
                }
                else if (strcmp(className, "CapsuleCollider2D") == 0)
                {
                    auto &col = go->addComponentDeferred<Bokken::GameObject::CapsuleCollider2D>();
                    wrapped = wrap_capsule_collider2d(ctx, &col);
                }
                else if (strcmp(className, "PolygonCollider2D") == 0)
                {
                    auto &col = go->addComponentDeferred<Bokken::GameObject::PolygonCollider2D>();
                    wrapped = wrap_polygon_collider2d(ctx, &col);
                }
                else if (strcmp(className, "EdgeCollider2D") == 0)
                {
                    auto &col = go->addComponentDeferred<Bokken::GameObject::EdgeCollider2D>();
                    wrapped = wrap_edge_collider2d(ctx, &col);
                }
                else if (strcmp(className, "ChainCollider2D") == 0)
                {
                    auto &col = go->addComponentDeferred<Bokken::GameObject::ChainCollider2D>();
                    wrapped = wrap_chain_collider2d(ctx, &col);
                }
                else if (strcmp(className, "AudioSource2D") == 0)
                {
                    // Deferred attach so user-supplied props (clip,
                    // volume, autoPlay, etc.) are applied *before*
                    // onAttach runs — autoPlay needs to see the clip.
                    auto &src = go->addComponentDeferred<Bokken::GameObject::AudioSource2D>();
                    wrapped = wrap_audio_source2d(ctx, &src);
                }
                else if (strcmp(className, "AudioListener2D") == 0)
                {
                    // Listener attach is order-dependent (singleton),
                    // but it has no props that need to land before
                    // attach, so the regular non-deferred path is
                    // fine and avoids extra plumbing.
                    auto &lst = go->addComponent<Bokken::GameObject::AudioListener2D>();
                    wrapped = wrap_audio_listener2d(ctx, &lst);
                }
                else
                {
                    // Unknown token — treat as a user-defined Behaviour
                    // class. The argument is expected to be a JS class
                    // constructor (e.g. addComponent(WahWahDemo)). We
                    // instantiate it, wrap the instance in a JSBehaviour
                    // adapter, and attach to the GameObject's parallel
                    // behaviour list.
                    //
                    // wrapped becomes the instance itself (not a
                    // component wrapper) so apply_props() lands user-
                    // supplied config directly on the instance, where
                    // user code can read it from onStart/onUpdate.
                    JSValue instance = JS_CallConstructor(ctx, argv[0], 0, nullptr);
                    if (JS_IsException(instance))
                    {
                        // Constructor threw — log and clear so the
                        // engine keeps running. Subsequent addComponent
                        // calls in the same chain still work.
                        JSValue exc = JS_GetException(ctx);
                        const char *str = JS_ToCString(ctx, exc);
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                     "[addComponent('%s')] constructor threw: %s",
                                     className, str ? str : "<unknown>");
                        if (str)
                            JS_FreeCString(ctx, str);
                        JS_FreeValue(ctx, exc);
                    }
                    else
                    {
                        auto adapter = std::make_unique<JSBehaviour>(ctx, instance);
                        // Attach to the GameObject's behaviour list. The
                        // adapter's destructor frees its dup'd instance
                        // ref; the local 'instance' here is freed at the
                        // bottom of this branch.
                        Bokken::GameObject::Component *raw =
                            go->addBehaviour(std::move(adapter));
                        // onAttach caches method handles and wires
                        // this.gameObject. Apply props *after* attach so
                        // gameObject is in place if the user reads it
                        // from a property setter — though in practice
                        // most props are plain data and don't observe.
                        if (raw)
                            raw->onAttach();
                        // Pass the bare instance back as `wrapped` so
                        // apply_props lands fields directly on it.
                        wrapped = JS_DupValue(ctx, instance);
                        JS_FreeValue(ctx, instance);
                    }
                }

                JS_FreeCString(ctx, className);

                // Apply config object if provided.
                if (!JS_IsUndefined(wrapped) && argc >= 2 && JS_IsObject(argv[1]))
                    apply_props(ctx, wrapped, argv[1]);

                // Deferred attach for colliders — their geometry is baked
                // into Box2D at onAttach time and we need user-supplied
                // size / radius / points etc. to be on the component
                // before we cross into Box2D. unwrap_collider2d returns
                // non-null for any of the seven concrete collider classes.
                if (!JS_IsUndefined(wrapped))
                {
                    if (auto *col = unwrap_collider2d(wrapped))
                        col->onAttach();

                    // AudioSource2D is also deferred so autoPlay can see
                    // the clip path supplied in the props bag.
                    if (auto *src = static_cast<Bokken::GameObject::AudioSource2D *>(
                            JS_GetOpaque(wrapped, s_audio_source2d_class_id)))
                        src->onAttach();
                }

                JS_FreeValue(ctx, wrapped);

                // Return `this` for chaining.
                return JS_DupValue(ctx, this_val);
            }

            // JS: gameObject.getComponent(Token)
            JSValue GameObject::js_get_component(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *go = static_cast<Bokken::GameObject::Base *>(JS_GetOpaque(this_val, s_class_id));
                if (!go || argc < 1)
                    return JS_UNDEFINED;

                JSValue nameVal = JS_GetPropertyStr(ctx, argv[0], "name");
                const char *className = JS_ToCString(ctx, nameVal);
                JS_FreeValue(ctx, nameVal);
                if (!className)
                    return JS_UNDEFINED;

                JSValue result = JS_UNDEFINED;

                if (strcmp(className, "Transform2D") == 0)
                {
                    auto *t = go->getComponent<Bokken::GameObject::Transform2D>();
                    if (t)
                        result = wrap_transform2d(ctx, t);
                }
                else if (strcmp(className, "Rigidbody2D") == 0)
                {
                    auto *rb = go->getComponent<Bokken::GameObject::Rigidbody2D>();
                    if (rb)
                        result = wrap_rigidbody2d(ctx, rb);
                }
                else if (strcmp(className, "Mesh2D") == 0)
                {
                    auto *mesh = go->getComponent<Bokken::GameObject::Mesh2D>();
                    if (mesh)
                        result = wrap_mesh2d(ctx, mesh);
                }
                else if (strcmp(className, "Sprite2D") == 0)
                {
                    auto *sprite = go->getComponent<Bokken::GameObject::Sprite2D>();
                    if (sprite)
                        result = wrap_sprite2d(ctx, sprite);
                }
                else if (strcmp(className, "Animation2D") == 0)
                {
                    auto *anim = go->getComponent<Bokken::GameObject::Animation2D>();
                    if (anim)
                        result = wrap_animation2d(ctx, anim);
                }
                else if (strcmp(className, "Distortion2D") == 0)
                {
                    auto *dist = go->getComponent<Bokken::GameObject::Distortion2D>();
                    if (dist)
                        result = wrap_distortion2d(ctx, dist);
                }
                else if (strcmp(className, "Light2D") == 0)
                {
                    auto *light = go->getComponent<Bokken::GameObject::Light2D>();
                    if (light)
                        result = wrap_light2d(ctx, light);
                }
                else if (strcmp(className, "ShadowCaster2D") == 0)
                {
                    auto *caster = go->getComponent<Bokken::GameObject::ShadowCaster2D>();
                    if (caster)
                        result = wrap_shadow_caster2d(ctx, caster);
                }
                else if (strcmp(className, "NormalMap2D") == 0)
                {
                    auto *nm = go->getComponent<Bokken::GameObject::NormalMap2D>();
                    if (nm)
                        result = wrap_normal_map2d(ctx, nm);
                }
                else if (strcmp(className, "Camera2D") == 0)
                {
                    auto *cam = go->getComponent<Bokken::GameObject::Camera2D>();
                    if (cam)
                        result = wrap_camera2d(ctx, cam);
                }
                else if (strcmp(className, "ParticleEmitter2D") == 0)
                {
                    auto *em = go->getComponent<Bokken::GameObject::ParticleEmitter2D>();
                    if (em)
                        result = wrap_particle2d(ctx, em);
                }
                else if (strcmp(className, "BoxCollider2D") == 0)
                {
                    auto *col = go->getComponent<Bokken::GameObject::BoxCollider2D>();
                    if (col)
                        result = wrap_box_collider2d(ctx, col);
                }
                else if (strcmp(className, "CircleCollider2D") == 0)
                {
                    auto *col = go->getComponent<Bokken::GameObject::CircleCollider2D>();
                    if (col)
                        result = wrap_circle_collider2d(ctx, col);
                }
                else if (strcmp(className, "CapsuleCollider2D") == 0)
                {
                    auto *col = go->getComponent<Bokken::GameObject::CapsuleCollider2D>();
                    if (col)
                        result = wrap_capsule_collider2d(ctx, col);
                }
                else if (strcmp(className, "PolygonCollider2D") == 0)
                {
                    auto *col = go->getComponent<Bokken::GameObject::PolygonCollider2D>();
                    if (col)
                        result = wrap_polygon_collider2d(ctx, col);
                }
                else if (strcmp(className, "EdgeCollider2D") == 0)
                {
                    auto *col = go->getComponent<Bokken::GameObject::EdgeCollider2D>();
                    if (col)
                        result = wrap_edge_collider2d(ctx, col);
                }
                else if (strcmp(className, "ChainCollider2D") == 0)
                {
                    auto *col = go->getComponent<Bokken::GameObject::ChainCollider2D>();
                    if (col)
                        result = wrap_chain_collider2d(ctx, col);
                }
                else if (strcmp(className, "AudioSource2D") == 0)
                {
                    auto *src = go->getComponent<Bokken::GameObject::AudioSource2D>();
                    if (src)
                        result = wrap_audio_source2d(ctx, src);
                }
                else if (strcmp(className, "AudioListener2D") == 0)
                {
                    auto *lst = go->getComponent<Bokken::GameObject::AudioListener2D>();
                    if (lst)
                        result = wrap_audio_listener2d(ctx, lst);
                }
                else
                {
                    // Unknown token — check user-defined Behaviour classes,
                    // mirroring js_add_component's fallback for the write
                    // side. These live in m_jsBehaviours (not m_components,
                    // which is keyed by C++ type and can't distinguish JS
                    // subclasses that all share the JSBehaviour wrapper),
                    // so walk every attached component/behaviour and match
                    // by JS class identity instead.
                    go->forEachComponent([&](Bokken::GameObject::Component *comp)
                    {
                        if (!JS_IsUndefined(result))
                            return; // already found a match

                        auto *jsb = dynamic_cast<Bokken::Scripting::Modules::JSBehaviour *>(comp);
                        if (jsb && JS_IsInstanceOf(ctx, jsb->instance(), argv[0]) > 0)
                            result = JS_DupValue(ctx, jsb->instance());
                    });
                }

                JS_FreeCString(ctx, className);
                return result;
            }

            JSValue GameObject::js_set_parent(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *go = static_cast<Bokken::GameObject::Base *>(JS_GetOpaque(this_val, s_class_id));
                if (!go || argc < 1)
                    return JS_UNDEFINED;

                if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0]))
                    go->setParent(nullptr);
                else
                {
                    auto *parent = static_cast<Bokken::GameObject::Base *>(JS_GetOpaque(argv[0], s_class_id));
                    go->setParent(parent);
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_get_children(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *go = static_cast<Bokken::GameObject::Base *>(JS_GetOpaque(this_val, s_class_id));
                if (!go)
                    return JS_UNDEFINED;

                JSValue arr = JS_NewArray(ctx);
                auto children = go->getChildren();
                for (size_t i = 0; i < children.size(); ++i)
                {
                    JSValue childObj = JS_NewObjectClass(ctx, s_class_id);
                    JS_SetOpaque(childObj, children[i]);
                    JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i), childObj);
                }
                return arr;
            }

            JSValue GameObject::js_destroy(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 1)
                    return JS_UNDEFINED;
                auto *go = static_cast<Bokken::GameObject::Base *>(JS_GetOpaque(argv[0], s_class_id));
                if (go)
                    Bokken::GameObject::Base::destroy(go);
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_find(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv)
            {
                if (argc < 1)
                    return JS_UNDEFINED;
                const char *name = JS_ToCString(ctx, argv[0]);
                if (!name)
                    return JS_UNDEFINED;

                Bokken::GameObject::Base *go = Bokken::GameObject::Base::find(name);
                JS_FreeCString(ctx, name);
                if (!go)
                    return JS_UNDEFINED;

                JSValue obj = JS_NewObjectClass(ctx, s_class_id);
                if (JS_IsException(obj))
                    return obj;
                JS_SetOpaque(obj, go);
                return obj;
            }

            JSValue GameObject::js_get_destroy_when_idle(JSContext *ctx, JSValueConst this_val)
            {
                auto *go = static_cast<Bokken::GameObject::Base *>(
                    JS_GetOpaque(this_val, s_class_id));
                if (!go)
                    return JS_UNDEFINED;
                return JS_NewBool(ctx, go->destroyWhenIdle);
            }

            JSValue GameObject::js_set_destroy_when_idle(JSContext *ctx, JSValueConst this_val, JSValueConst val)
            {
                auto *go = static_cast<Bokken::GameObject::Base *>(
                    JS_GetOpaque(this_val, s_class_id));
                if (!go)
                    return JS_UNDEFINED;
                go->destroyWhenIdle = JS_ToBool(ctx, val);
                return JS_UNDEFINED;
            }

            // Camera2D getters.
            JSValue GameObject::js_camera2d_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *cam = static_cast<Bokken::GameObject::Camera2D *>(
                    JS_GetOpaque(this_val, s_camera2d_class_id));
                if (!cam)
                    return JS_UNDEFINED;

                switch (magic)
                {
                case C2_Zoom:
                    return JS_NewFloat64(ctx, cam->zoom);
                case C2_IsActive:
                    return JS_NewBool(ctx, cam->isActive);
                }
                return JS_UNDEFINED;
            }

            // Camera2D setters only for bools and zoom, since other properties are read-only or derived.
            JSValue GameObject::js_camera2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *cam = static_cast<Bokken::GameObject::Camera2D *>(
                    JS_GetOpaque(this_val, s_camera2d_class_id));
                if (!cam)
                    return JS_UNDEFINED;

                switch (magic)
                {
                case C2_IsActive:
                    cam->isActive = JS_ToBool(ctx, val);
                    return JS_UNDEFINED;
                case C2_Zoom:
                {
                    double d;
                    if (JS_ToFloat64(ctx, &d, val) < 0)
                        return JS_EXCEPTION;
                    cam->zoom = static_cast<float>(d);
                    return JS_UNDEFINED;
                }
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_camera2d_screen_to_world_point(JSContext *ctx, JSValueConst this_val,
                                                                  int argc, JSValueConst *argv)
            {
                auto *cam = static_cast<Bokken::GameObject::Camera2D *>(
                    JS_GetOpaque(this_val, s_camera2d_class_id));
                if (!cam || argc < 2)
                    return JS_UNDEFINED;

                double x, y;
                if (JS_ToFloat64(ctx, &x, argv[0]) < 0 ||
                    JS_ToFloat64(ctx, &y, argv[1]) < 0)
                    return JS_EXCEPTION;

                glm::vec2 w = cam->screenToWorldPoint(static_cast<float>(x),
                                                      static_cast<float>(y));
                return make_vec2(ctx, w);
            }

            JSValue GameObject::js_camera2d_world_to_screen_point(JSContext *ctx, JSValueConst this_val,
                                                                  int argc, JSValueConst *argv)
            {
                auto *cam = static_cast<Bokken::GameObject::Camera2D *>(
                    JS_GetOpaque(this_val, s_camera2d_class_id));
                if (!cam || argc < 2)
                    return JS_UNDEFINED;

                double x, y;
                if (JS_ToFloat64(ctx, &x, argv[0]) < 0 ||
                    JS_ToFloat64(ctx, &y, argv[1]) < 0)
                    return JS_EXCEPTION;

                glm::vec2 w = cam->worldToScreenPoint(static_cast<float>(x),
                                                      static_cast<float>(y));
                return make_vec2(ctx, w);
            }

            // Camera2D getters/setters.
            JSValue GameObject::wrap_camera2d(JSContext *ctx, Bokken::GameObject::Camera2D *cam)
            {
                JSValue obj = JS_NewObjectClass(ctx, s_camera2d_class_id);
                if (JS_IsException(obj))
                    return obj;
                JS_SetOpaque(obj, cam);
                return obj;
            }

            JSValue GameObject::js_particle2d_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *em = static_cast<Bokken::GameObject::ParticleEmitter2D *>(
                    JS_GetOpaque(this_val, s_particle2d_class_id));
                if (!em)
                    return JS_UNDEFINED;

                switch (magic)
                {
                case PE2_Emitting:
                    return JS_NewBool(ctx, em->emitting);
                case PE2_EmitRate:
                    return JS_NewFloat64(ctx, em->emitRate);
                case PE2_LifetimeMinimum:
                    return JS_NewFloat64(ctx, em->lifetimeMinimum);
                case PE2_LifetimeMaximum:
                    return JS_NewFloat64(ctx, em->lifetimeMaximum);
                case PE2_SpeedMinimum:
                    return JS_NewFloat64(ctx, em->speedMinimum);
                case PE2_SpeedMaximum:
                    return JS_NewFloat64(ctx, em->speedMaximum);
                case PE2_SizeStart:
                    return JS_NewFloat64(ctx, em->sizeStart);
                case PE2_SizeEnd:
                    return JS_NewFloat64(ctx, em->sizeEnd);
                case PE2_SizeStartVariance:
                    return JS_NewFloat64(ctx, em->sizeStartVariance);
                case PE2_SizeEase:
                    return JS_NewInt32(ctx, static_cast<int>(em->sizeEase));
                case PE2_SpreadAngle:
                    return JS_NewFloat64(ctx, em->spreadAngle);
                case PE2_Direction:
                    return JS_NewFloat64(ctx, em->direction);
                case PE2_Gravity:
                    return JS_NewFloat64(ctx, em->gravity);
                case PE2_Damping:
                    return JS_NewFloat64(ctx, em->damping);
                case PE2_AngularVelocityMinimum:
                    return JS_NewFloat64(ctx, em->angularVelocityMinimum);
                case PE2_AngularVelocityMaximum:
                    return JS_NewFloat64(ctx, em->angularVelocityMaximum);
                case PE2_SpawnOffsetX:
                    return JS_NewFloat64(ctx, em->spawnOffsetX);
                case PE2_SpawnOffsetY:
                    return JS_NewFloat64(ctx, em->spawnOffsetY);
                case PE2_VelocityScaleEmission:
                    return JS_NewBool(ctx, em->velocityScaleEmission);
                case PE2_VelocityReferenceSpeed:
                    return JS_NewFloat64(ctx, em->velocityReferenceSpeed);
                case PE2_ZOrder:
                    return JS_NewFloat64(ctx, em->zOrder);
                case PE2_MaximumParticles:
                    return JS_NewInt32(ctx, em->maximumParticles);
                case PE2_AlphaEase:
                    return JS_NewInt32(ctx, static_cast<int>(em->alphaEase));
                case PE2_ColorStart:
                {
                    uint32_t packed =
                        (static_cast<uint32_t>(em->colorStart.r * 255) << 24) |
                        (static_cast<uint32_t>(em->colorStart.g * 255) << 16) |
                        (static_cast<uint32_t>(em->colorStart.b * 255) << 8) |
                        static_cast<uint32_t>(em->colorStart.a * 255);
                    return JS_NewUint32(ctx, packed);
                }
                case PE2_ColorEnd:
                {
                    uint32_t packed =
                        (static_cast<uint32_t>(em->colorEnd.r * 255) << 24) |
                        (static_cast<uint32_t>(em->colorEnd.g * 255) << 16) |
                        (static_cast<uint32_t>(em->colorEnd.b * 255) << 8) |
                        static_cast<uint32_t>(em->colorEnd.a * 255);
                    return JS_NewUint32(ctx, packed);
                }
                case PE2_BlendMode:
                {
                    const char *name = "Alpha";
                    if (em->blendMode == Bokken::Renderer::BlendMode::Additive)
                        name = "Additive";
                    else if (em->blendMode == Bokken::Renderer::BlendMode::Screen)
                        name = "Screen";
                    return JS_NewString(ctx, name);
                }
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_particle2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *em = static_cast<Bokken::GameObject::ParticleEmitter2D *>(
                    JS_GetOpaque(this_val, s_particle2d_class_id));
                if (!em)
                    return JS_UNDEFINED;

                // Bool properties.
                switch (magic)
                {
                case PE2_Emitting:
                    em->emitting = JS_ToBool(ctx, val);
                    return JS_UNDEFINED;
                case PE2_VelocityScaleEmission:
                    em->velocityScaleEmission = JS_ToBool(ctx, val);
                    return JS_UNDEFINED;
                case PE2_ColorStart:
                {
                    uint32_t c = 0;
                    JS_ToUint32(ctx, &c, val);
                    em->colorStart.r = ((c >> 24) & 0xFF) / 255.0f;
                    em->colorStart.g = ((c >> 16) & 0xFF) / 255.0f;
                    em->colorStart.b = ((c >> 8) & 0xFF) / 255.0f;
                    em->colorStart.a = ((c >> 0) & 0xFF) / 255.0f;
                    return JS_UNDEFINED;
                }
                case PE2_ColorEnd:
                {
                    uint32_t c = 0;
                    JS_ToUint32(ctx, &c, val);
                    em->colorEnd.r = ((c >> 24) & 0xFF) / 255.0f;
                    em->colorEnd.g = ((c >> 16) & 0xFF) / 255.0f;
                    em->colorEnd.b = ((c >> 8) & 0xFF) / 255.0f;
                    em->colorEnd.a = ((c >> 0) & 0xFF) / 255.0f;
                    return JS_UNDEFINED;
                }
                case PE2_MaximumParticles:
                {
                    int32_t v = 0;
                    JS_ToInt32(ctx, &v, val);
                    em->maximumParticles = v;
                    return JS_UNDEFINED;
                }
                case PE2_SizeEase:
                {
                    int32_t v = 0;
                    JS_ToInt32(ctx, &v, val);
                    em->sizeEase = static_cast<Bokken::GameObject::ParticleEase>(v);
                    return JS_UNDEFINED;
                }
                case PE2_AlphaEase:
                {
                    int32_t v = 0;
                    JS_ToInt32(ctx, &v, val);
                    em->alphaEase = static_cast<Bokken::GameObject::ParticleEase>(v);
                    return JS_UNDEFINED;
                }
                case PE2_BlendMode:
                {
                    const char *str = JS_ToCString(ctx, val);
                    if (str)
                    {
                        if (strcmp(str, "additive") == 0)
                            em->blendMode = Bokken::Renderer::BlendMode::Additive;
                        else if (strcmp(str, "screen") == 0)
                            em->blendMode = Bokken::Renderer::BlendMode::Screen;
                        else
                            em->blendMode = Bokken::Renderer::BlendMode::Alpha;
                        JS_FreeCString(ctx, str);
                    }
                    return JS_UNDEFINED;
                }
                }

                // Float properties.
                double d = 0.0;
                if (JS_ToFloat64(ctx, &d, val) < 0)
                    return JS_EXCEPTION;
                float f = static_cast<float>(d);

                switch (magic)
                {
                case PE2_EmitRate:
                    em->emitRate = f;
                    break;
                case PE2_LifetimeMinimum:
                    em->lifetimeMinimum = f;
                    break;
                case PE2_LifetimeMaximum:
                    em->lifetimeMaximum = f;
                    break;
                case PE2_SpeedMinimum:
                    em->speedMinimum = f;
                    break;
                case PE2_SpeedMaximum:
                    em->speedMaximum = f;
                    break;
                case PE2_SizeStart:
                    em->sizeStart = f;
                    break;
                case PE2_SizeEnd:
                    em->sizeEnd = f;
                    break;
                case PE2_SizeStartVariance:
                    em->sizeStartVariance = f;
                    break;
                case PE2_SpreadAngle:
                    em->spreadAngle = f;
                    break;
                case PE2_Direction:
                    em->direction = f;
                    break;
                case PE2_Gravity:
                    em->gravity = f;
                    break;
                case PE2_Damping:
                    em->damping = f;
                    break;
                case PE2_AngularVelocityMinimum:
                    em->angularVelocityMinimum = f;
                    break;
                case PE2_AngularVelocityMaximum:
                    em->angularVelocityMaximum = f;
                    break;
                case PE2_SpawnOffsetX:
                    em->spawnOffsetX = f;
                    break;
                case PE2_SpawnOffsetY:
                    em->spawnOffsetY = f;
                    break;
                case PE2_VelocityReferenceSpeed:
                    em->velocityReferenceSpeed = f;
                    break;
                case PE2_ZOrder:
                    em->zOrder = f;
                    break;
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_particle2d_burst(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *em = static_cast<Bokken::GameObject::ParticleEmitter2D *>(
                    JS_GetOpaque(this_val, s_particle2d_class_id));
                if (!em || argc < 1)
                    return JS_UNDEFINED;

                int32_t count = 0;
                JS_ToInt32(ctx, &count, argv[0]);
                em->burst(count);
                return JS_UNDEFINED;
            }

            JSValue GameObject::wrap_particle2d(JSContext *ctx, Bokken::GameObject::ParticleEmitter2D *em)
            {
                JSValue obj = JS_NewObjectClass(ctx, s_particle2d_class_id);
                if (JS_IsException(obj))
                    return obj;
                JS_SetOpaque(obj, em);
                return obj;
            }

            // Transform2D getters/setters.
            JSValue GameObject::js_transform2d_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *t = static_cast<Bokken::GameObject::Transform2D *>(
                    JS_GetOpaque(this_val, s_transform2d_class_id));
                if (!t)
                    return JS_UNDEFINED;

                switch (magic)
                {
                case T2_PositionX:
                    return JS_NewFloat64(ctx, t->position.x);
                case T2_PositionY:
                    return JS_NewFloat64(ctx, t->position.y);
                case T2_Rotation:
                    return JS_NewFloat64(ctx, t->rotation);
                case T2_ScaleX:
                    return JS_NewFloat64(ctx, t->scale.x);
                case T2_ScaleY:
                    return JS_NewFloat64(ctx, t->scale.y);
                case T2_ZOrder:
                    return JS_NewFloat64(ctx, t->zOrder);
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_transform2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *t = static_cast<Bokken::GameObject::Transform2D *>(
                    JS_GetOpaque(this_val, s_transform2d_class_id));
                if (!t)
                    return JS_UNDEFINED;

                double d = 0.0;
                if (JS_ToFloat64(ctx, &d, val) < 0)
                    return JS_EXCEPTION;
                float f = static_cast<float>(d);

                switch (magic)
                {
                case T2_PositionX:
                    t->position.x = f;
                    break;
                case T2_PositionY:
                    t->position.y = f;
                    break;
                case T2_Rotation:
                    t->rotation = f;
                    break;
                case T2_ScaleX:
                    t->scale.x = f;
                    break;
                case T2_ScaleY:
                    t->scale.y = f;
                    break;
                case T2_ZOrder:
                    t->zOrder = f;
                    break;
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_transform2d_translate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *t = static_cast<Bokken::GameObject::Transform2D *>(
                    JS_GetOpaque(this_val, s_transform2d_class_id));
                if (!t || argc < 2)
                    return JS_UNDEFINED;

                double x, y;
                if (JS_ToFloat64(ctx, &x, argv[0]) < 0 ||
                    JS_ToFloat64(ctx, &y, argv[1]) < 0)
                    return JS_EXCEPTION;

                t->translate(static_cast<float>(x), static_cast<float>(y));
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_transform2d_rotate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *t = static_cast<Bokken::GameObject::Transform2D *>(
                    JS_GetOpaque(this_val, s_transform2d_class_id));
                if (!t || argc < 1)
                    return JS_UNDEFINED;

                double deg;
                if (JS_ToFloat64(ctx, &deg, argv[0]) < 0)
                    return JS_EXCEPTION;

                t->rotate(static_cast<float>(deg));
                return JS_UNDEFINED;
            }

            // Rigidbody2D getters/setters.
            JSValue GameObject::js_rigidbody2d_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *rb = static_cast<Bokken::GameObject::Rigidbody2D *>(
                    JS_GetOpaque(this_val, s_rigidbody2d_class_id));
                if (!rb)
                    return JS_UNDEFINED;

                switch (magic)
                {
                case RB2_Type:
                {
                    const char *name = "dynamic";
                    if (rb->bodyType() == Bokken::GameObject::Rigidbody2D::Type::Static)
                        name = "static";
                    else if (rb->bodyType() == Bokken::GameObject::Rigidbody2D::Type::Kinematic)
                        name = "kinematic";
                    return JS_NewString(ctx, name);
                }
                case RB2_FixedRotation:
                    return JS_NewBool(ctx, rb->fixedRotation);
                case RB2_IsBullet:
                    return JS_NewBool(ctx, rb->isBullet);
                case RB2_LinearDamping:
                    return JS_NewFloat64(ctx, rb->linearDamping);
                case RB2_AngularDamping:
                    return JS_NewFloat64(ctx, rb->angularDamping);
                case RB2_GravityScale:
                    return JS_NewFloat64(ctx, rb->gravityScale);
                case RB2_AllowSleep:
                    return JS_NewBool(ctx, rb->allowSleep);
                case RB2_PositionX:
                    return JS_NewFloat64(ctx, rb->position().x);
                case RB2_PositionY:
                    return JS_NewFloat64(ctx, rb->position().y);
                case RB2_Rotation:
                    return JS_NewFloat64(ctx, rb->rotation());
                case RB2_VelocityX:
                    return JS_NewFloat64(ctx, rb->linearVelocity().x);
                case RB2_VelocityY:
                    return JS_NewFloat64(ctx, rb->linearVelocity().y);
                case RB2_AngularVelocity:
                    return JS_NewFloat64(ctx, rb->angularVelocity());
                case RB2_Awake:
                    return JS_NewBool(ctx, rb->isAwake());
                case RB2_Mass:
                    return JS_NewFloat64(ctx, rb->mass());
                case RB2_Inertia:
                    return JS_NewFloat64(ctx, rb->inertia());
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_rigidbody2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *rb = static_cast<Bokken::GameObject::Rigidbody2D *>(
                    JS_GetOpaque(this_val, s_rigidbody2d_class_id));
                if (!rb)
                    return JS_UNDEFINED;

                // Bool / string / enum fields up front, before the
                // shared float-coerce path below.
                switch (magic)
                {
                case RB2_Type:
                {
                    const char *str = JS_ToCString(ctx, val);
                    if (str)
                    {
                        if (strcmp(str, "static") == 0)
                            rb->setType(Bokken::GameObject::Rigidbody2D::Type::Static);
                        else if (strcmp(str, "kinematic") == 0)
                            rb->setType(Bokken::GameObject::Rigidbody2D::Type::Kinematic);
                        else
                            rb->setType(Bokken::GameObject::Rigidbody2D::Type::Dynamic);
                        JS_FreeCString(ctx, str);
                    }
                    return JS_UNDEFINED;
                }
                case RB2_FixedRotation:
                    rb->setFixedRotation(JS_ToBool(ctx, val));
                    return JS_UNDEFINED;
                case RB2_IsBullet:
                    rb->setBullet(JS_ToBool(ctx, val));
                    return JS_UNDEFINED;
                case RB2_AllowSleep:
                    rb->setAllowSleep(JS_ToBool(ctx, val));
                    return JS_UNDEFINED;
                case RB2_Awake:
                    rb->setAwake(JS_ToBool(ctx, val));
                    return JS_UNDEFINED;
                }

                double d = 0.0;
                if (JS_ToFloat64(ctx, &d, val) < 0)
                    return JS_EXCEPTION;
                float f = static_cast<float>(d);

                switch (magic)
                {
                case RB2_LinearDamping:
                    rb->setLinearDamping(f);
                    break;
                case RB2_AngularDamping:
                    rb->setAngularDamping(f);
                    break;
                case RB2_GravityScale:
                    rb->setGravityScale(f);
                    break;
                case RB2_PositionX:
                {
                    glm::vec2 p = rb->position();
                    p.x = f;
                    rb->setPosition(p);
                    break;
                }
                case RB2_PositionY:
                {
                    glm::vec2 p = rb->position();
                    p.y = f;
                    rb->setPosition(p);
                    break;
                }
                case RB2_Rotation:
                    rb->setRotation(f);
                    break;
                case RB2_VelocityX:
                {
                    glm::vec2 v = rb->linearVelocity();
                    v.x = f;
                    rb->setLinearVelocity(v);
                    break;
                }
                case RB2_VelocityY:
                {
                    glm::vec2 v = rb->linearVelocity();
                    v.y = f;
                    rb->setLinearVelocity(v);
                    break;
                }
                case RB2_AngularVelocity:
                    rb->setAngularVelocity(f);
                    break;
                }
                return JS_UNDEFINED;
            }

            // JS: rb.applyForce(fx, fy)             — at body centre
            // JS: rb.applyForce(fx, fy, px, py)     — at world point (pixels)
            JSValue GameObject::js_rigidbody2d_apply_force(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *rb = static_cast<Bokken::GameObject::Rigidbody2D *>(
                    JS_GetOpaque(this_val, s_rigidbody2d_class_id));
                if (!rb || argc < 2)
                    return JS_UNDEFINED;

                double fx, fy;
                if (JS_ToFloat64(ctx, &fx, argv[0]) < 0 ||
                    JS_ToFloat64(ctx, &fy, argv[1]) < 0)
                    return JS_EXCEPTION;

                if (argc >= 4)
                {
                    double px, py;
                    if (JS_ToFloat64(ctx, &px, argv[2]) < 0 ||
                        JS_ToFloat64(ctx, &py, argv[3]) < 0)
                        return JS_EXCEPTION;
                    rb->applyForce({static_cast<float>(fx), static_cast<float>(fy)},
                                   {static_cast<float>(px), static_cast<float>(py)});
                }
                else
                {
                    rb->applyForceToCenter({static_cast<float>(fx), static_cast<float>(fy)});
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_rigidbody2d_apply_force_to_center(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *rb = static_cast<Bokken::GameObject::Rigidbody2D *>(
                    JS_GetOpaque(this_val, s_rigidbody2d_class_id));
                if (!rb || argc < 2)
                    return JS_UNDEFINED;

                double fx, fy;
                if (JS_ToFloat64(ctx, &fx, argv[0]) < 0 ||
                    JS_ToFloat64(ctx, &fy, argv[1]) < 0)
                    return JS_EXCEPTION;

                rb->applyForceToCenter({static_cast<float>(fx), static_cast<float>(fy)});
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_rigidbody2d_apply_torque(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *rb = static_cast<Bokken::GameObject::Rigidbody2D *>(
                    JS_GetOpaque(this_val, s_rigidbody2d_class_id));
                if (!rb || argc < 1)
                    return JS_UNDEFINED;

                double t;
                if (JS_ToFloat64(ctx, &t, argv[0]) < 0)
                    return JS_EXCEPTION;

                rb->applyTorque(static_cast<float>(t));
                return JS_UNDEFINED;
            }

            // JS: rb.applyImpulse(jx, jy)            — at body centre
            // JS: rb.applyImpulse(jx, jy, px, py)    — at world point (pixels)
            JSValue GameObject::js_rigidbody2d_apply_impulse(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *rb = static_cast<Bokken::GameObject::Rigidbody2D *>(
                    JS_GetOpaque(this_val, s_rigidbody2d_class_id));
                if (!rb || argc < 2)
                    return JS_UNDEFINED;

                double jx, jy;
                if (JS_ToFloat64(ctx, &jx, argv[0]) < 0 ||
                    JS_ToFloat64(ctx, &jy, argv[1]) < 0)
                    return JS_EXCEPTION;

                if (argc >= 4)
                {
                    double px, py;
                    if (JS_ToFloat64(ctx, &px, argv[2]) < 0 ||
                        JS_ToFloat64(ctx, &py, argv[3]) < 0)
                        return JS_EXCEPTION;
                    rb->applyLinearImpulse({static_cast<float>(jx), static_cast<float>(jy)},
                                           {static_cast<float>(px), static_cast<float>(py)});
                }
                else
                {
                    rb->applyLinearImpulseToCenter({static_cast<float>(jx), static_cast<float>(jy)});
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_rigidbody2d_apply_impulse_to_center(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *rb = static_cast<Bokken::GameObject::Rigidbody2D *>(
                    JS_GetOpaque(this_val, s_rigidbody2d_class_id));
                if (!rb || argc < 2)
                    return JS_UNDEFINED;

                double jx, jy;
                if (JS_ToFloat64(ctx, &jx, argv[0]) < 0 ||
                    JS_ToFloat64(ctx, &jy, argv[1]) < 0)
                    return JS_EXCEPTION;

                rb->applyLinearImpulseToCenter({static_cast<float>(jx), static_cast<float>(jy)});
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_rigidbody2d_apply_angular_impulse(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *rb = static_cast<Bokken::GameObject::Rigidbody2D *>(
                    JS_GetOpaque(this_val, s_rigidbody2d_class_id));
                if (!rb || argc < 1)
                    return JS_UNDEFINED;

                double j;
                if (JS_ToFloat64(ctx, &j, argv[0]) < 0)
                    return JS_EXCEPTION;

                rb->applyAngularImpulse(static_cast<float>(j));
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_rigidbody2d_set_velocity(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *rb = static_cast<Bokken::GameObject::Rigidbody2D *>(
                    JS_GetOpaque(this_val, s_rigidbody2d_class_id));
                if (!rb || argc < 2)
                    return JS_UNDEFINED;

                double x, y;
                if (JS_ToFloat64(ctx, &x, argv[0]) < 0 ||
                    JS_ToFloat64(ctx, &y, argv[1]) < 0)
                    return JS_EXCEPTION;

                rb->setLinearVelocity({static_cast<float>(x), static_cast<float>(y)});
                return JS_UNDEFINED;
            }

            // Mesh2D getters/setters.
            JSValue GameObject::js_mesh2d_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *mesh = static_cast<Bokken::GameObject::Mesh2D *>(
                    JS_GetOpaque(this_val, s_mesh2d_class_id));
                if (!mesh)
                    return JS_UNDEFINED;

                switch (magic)
                {
                case M2_Shape:
                    return JS_NewString(ctx, shape2d_to_string(mesh->shape));
                case M2_Color:
                {
                    uint32_t packed =
                        (static_cast<uint32_t>(mesh->color.r * 255) << 24) |
                        (static_cast<uint32_t>(mesh->color.g * 255) << 16) |
                        (static_cast<uint32_t>(mesh->color.b * 255) << 8) |
                        static_cast<uint32_t>(mesh->color.a * 255);

                    return JS_NewUint32(ctx, packed);
                }
                case M2_FlipX:
                    return JS_NewBool(ctx, mesh->flipX);
                case M2_FlipY:
                    return JS_NewBool(ctx, mesh->flipY);
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_mesh2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *mesh = static_cast<Bokken::GameObject::Mesh2D *>(
                    JS_GetOpaque(this_val, s_mesh2d_class_id));
                if (!mesh)
                    return JS_UNDEFINED;

                switch (magic)
                {
                case M2_Shape:
                {
                    const char *str = JS_ToCString(ctx, val);
                    if (str)
                    {
                        mesh->shape = parse_shape2d(str);
                        JS_FreeCString(ctx, str);
                    }
                    return JS_UNDEFINED;
                }
                case M2_FlipX:
                    mesh->flipX = JS_ToBool(ctx, val);
                    return JS_UNDEFINED;
                case M2_FlipY:
                    mesh->flipY = JS_ToBool(ctx, val);
                    return JS_UNDEFINED;
                }

                double d = 0.0;
                if (JS_ToFloat64(ctx, &d, val) < 0)
                    return JS_EXCEPTION;
                float f = static_cast<float>(d);

                switch (magic)
                {
                case M2_Color:
                {
                    uint32_t c = 0;
                    JS_ToUint32(ctx, &c, val);
                    mesh->color.r = ((c >> 24) & 0xFF) / 255.0f;
                    mesh->color.g = ((c >> 16) & 0xFF) / 255.0f;
                    mesh->color.b = ((c >> 8) & 0xFF) / 255.0f;
                    mesh->color.a = ((c >> 0) & 0xFF) / 255.0f;
                    return JS_UNDEFINED;
                }
                }

                return JS_UNDEFINED;
            }

            // Wrappers.
            JSValue GameObject::wrap_transform2d(JSContext *ctx, Bokken::GameObject::Transform2D *t)
            {
                JSValue obj = JS_NewObjectClass(ctx, s_transform2d_class_id);
                if (JS_IsException(obj))
                    return obj;
                JS_SetOpaque(obj, t);
                return obj;
            }

            JSValue GameObject::wrap_rigidbody2d(JSContext *ctx, Bokken::GameObject::Rigidbody2D *rb)
            {
                JSValue obj = JS_NewObjectClass(ctx, s_rigidbody2d_class_id);
                if (JS_IsException(obj))
                    return obj;
                JS_SetOpaque(obj, rb);
                return obj;
            }

            JSValue GameObject::wrap_mesh2d(JSContext *ctx, Bokken::GameObject::Mesh2D *mesh)
            {
                JSValue obj = JS_NewObjectClass(ctx, s_mesh2d_class_id);
                if (JS_IsException(obj))
                    return obj;
                JS_SetOpaque(obj, mesh);
                return obj;
            }

            JSValue GameObject::wrap_sprite2d(JSContext *ctx, Bokken::GameObject::Sprite2D *sprite)
            {
                JSValue obj = JS_NewObjectClass(ctx, s_sprite2d_class_id);
                if (JS_IsException(obj))
                    return obj;
                JS_SetOpaque(obj, sprite);
                return obj;
            }

            JSValue GameObject::wrap_animation2d(JSContext *ctx, Bokken::GameObject::Animation2D *anim)
            {
                JSValue obj = JS_NewObjectClass(ctx, s_animation2d_class_id);
                if (JS_IsException(obj))
                    return obj;
                JS_SetOpaque(obj, anim);
                return obj;
            }

            JSValue GameObject::wrap_distortion2d(JSContext *ctx, Bokken::GameObject::Distortion2D *dist)
            {
                JSValue obj = JS_NewObjectClass(ctx, s_distortion2d_class_id);
                if (JS_IsException(obj))
                    return obj;
                JS_SetOpaque(obj, dist);
                return obj;
            }

            JSValue GameObject::js_distortion2d_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *dist = static_cast<Bokken::GameObject::Distortion2D *>(
                    JS_GetOpaque(this_val, s_distortion2d_class_id));
                if (!dist)
                    return JS_UNDEFINED;

                switch (magic)
                {
                case D2_Speed:
                    return JS_NewFloat64(ctx, dist->speed);
                case D2_Thickness:
                    return JS_NewFloat64(ctx, dist->thickness);
                case D2_Amplitude:
                    return JS_NewFloat64(ctx, dist->amplitude);
                case D2_MaximumRadius:
                    return JS_NewFloat64(ctx, dist->maximumRadius);
                case D2_AutoStart:
                    return JS_NewBool(ctx, dist->autoStart);
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_distortion2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *dist = static_cast<Bokken::GameObject::Distortion2D *>(
                    JS_GetOpaque(this_val, s_distortion2d_class_id));
                if (!dist)
                    return JS_UNDEFINED;

                if (magic == D2_AutoStart)
                {
                    dist->autoStart = JS_ToBool(ctx, val);
                    return JS_UNDEFINED;
                }

                double d = 0.0;
                if (JS_ToFloat64(ctx, &d, val) < 0)
                    return JS_EXCEPTION;
                float f = static_cast<float>(d);

                switch (magic)
                {
                case D2_Speed:
                    dist->speed = f;
                    break;
                case D2_Thickness:
                    dist->thickness = f;
                    break;
                case D2_Amplitude:
                    dist->amplitude = f;
                    break;
                case D2_MaximumRadius:
                    dist->maximumRadius = f;
                    break;
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_distortion2d_trigger(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *dist = static_cast<Bokken::GameObject::Distortion2D *>(
                    JS_GetOpaque(this_val, s_distortion2d_class_id));
                if (dist)
                    dist->trigger();
                return JS_UNDEFINED;
            }

            //  Light2D

            JSValue GameObject::wrap_light2d(JSContext *ctx, Bokken::GameObject::Light2D *light)
            {
                JSValue obj = JS_NewObjectClass(ctx, s_light2d_class_id);
                if (JS_IsException(obj))
                    return obj;
                JS_SetOpaque(obj, light);
                return obj;
            }

            namespace
            {
                /* String <-> LightType / LightEnvelope conversion.
                 *
                 * The JS API uses string-valued enums (the same idiom
                 * as BlendMode / Shape2D elsewhere in this file).
                 * Conversion is done at the property-set boundary so
                 * the rest of the code stays in the typed enum.
                 *
                 * Unknown strings are silently coerced to Point /
                 * Constant rather than throwing — the lighting code
                 * tolerates them and an artist-side typo surfaces as
                 * "the light looks wrong" rather than a runtime
                 * crash. The trade-off is debatable but consistent
                 * with how BlendMode handles unknowns.
                 */
                Bokken::GameObject::LightType parseLightType(const char *s)
                {
                    if (!s)
                        return Bokken::GameObject::LightType::Point;
                    if (std::strcmp(s, "Spot") == 0)
                        return Bokken::GameObject::LightType::Spot;
                    if (std::strcmp(s, "Directional") == 0)
                        return Bokken::GameObject::LightType::Directional;
                    return Bokken::GameObject::LightType::Point;
                }

                const char *lightTypeToString(Bokken::GameObject::LightType t)
                {
                    switch (t)
                    {
                    case Bokken::GameObject::LightType::Spot:
                        return "Spot";
                    case Bokken::GameObject::LightType::Directional:
                        return "Directional";
                    default:
                        return "Point";
                    }
                }

                Bokken::GameObject::LightEnvelope parseLightEnvelope(const char *s)
                {
                    if (!s)
                        return Bokken::GameObject::LightEnvelope::Constant;
                    if (std::strcmp(s, "Flicker") == 0)
                        return Bokken::GameObject::LightEnvelope::Flicker;
                    if (std::strcmp(s, "Pulse") == 0)
                        return Bokken::GameObject::LightEnvelope::Pulse;
                    if (std::strcmp(s, "Strobe") == 0)
                        return Bokken::GameObject::LightEnvelope::Strobe;
                    if (std::strcmp(s, "Custom") == 0)
                        return Bokken::GameObject::LightEnvelope::Custom;
                    return Bokken::GameObject::LightEnvelope::Constant;
                }

                const char *lightEnvelopeToString(Bokken::GameObject::LightEnvelope e)
                {
                    switch (e)
                    {
                    case Bokken::GameObject::LightEnvelope::Flicker:
                        return "Flicker";
                    case Bokken::GameObject::LightEnvelope::Pulse:
                        return "Pulse";
                    case Bokken::GameObject::LightEnvelope::Strobe:
                        return "Strobe";
                    case Bokken::GameObject::LightEnvelope::Custom:
                        return "Custom";
                    default:
                        return "Constant";
                    }
                }
            }

            JSValue GameObject::js_light2d_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *light = static_cast<Bokken::GameObject::Light2D *>(
                    JS_GetOpaque(this_val, s_light2d_class_id));
                if (!light)
                    return JS_UNDEFINED;

                switch (magic)
                {
                case L2_Type:
                    return JS_NewString(ctx, lightTypeToString(light->type));
                case L2_ColorR:
                    return JS_NewFloat64(ctx, light->color.r);
                case L2_ColorG:
                    return JS_NewFloat64(ctx, light->color.g);
                case L2_ColorB:
                    return JS_NewFloat64(ctx, light->color.b);
                case L2_Color:
                {
                    // Light2D has no alpha channel; serialise with
                    // alpha = 0xFF for symmetry with Mesh2D.color and
                    // so a round-trip through `light.color = light.color`
                    // is a no-op. HDR values >1 are clamped on the way
                    // out — `colorR`/`colorG`/`colorB` remain the
                    // lossless path for HDR authoring.
                    auto toByte = [](float v)
                    {
                        if (v < 0.0f)
                            v = 0.0f;
                        if (v > 1.0f)
                            v = 1.0f;
                        return static_cast<uint32_t>(v * 255.0f);
                    };
                    uint32_t packed =
                        (toByte(light->color.r) << 24) |
                        (toByte(light->color.g) << 16) |
                        (toByte(light->color.b) << 8) |
                        0xFFu;
                    return JS_NewUint32(ctx, packed);
                }
                case L2_Intensity:
                    return JS_NewFloat64(ctx, light->intensity);
                case L2_Range:
                    return JS_NewFloat64(ctx, light->range);
                case L2_Falloff:
                    return JS_NewFloat64(ctx, light->falloff);
                case L2_InnerConeAngle:
                    return JS_NewFloat64(ctx, light->innerConeAngle);
                case L2_OuterConeAngle:
                    return JS_NewFloat64(ctx, light->outerConeAngle);
                case L2_DirectionDegrees:
                    return JS_NewFloat64(ctx, light->directionDegrees);
                case L2_CastsShadows:
                    return JS_NewBool(ctx, light->castsShadows);
                case L2_ShadowSoftness:
                    return JS_NewFloat64(ctx, light->shadowSoftness);
                case L2_Envelope:
                    return JS_NewString(ctx, lightEnvelopeToString(light->envelope));
                case L2_EnvelopeAmplitude:
                    return JS_NewFloat64(ctx, light->envelopeAmplitude);
                case L2_EnvelopeFrequency:
                    return JS_NewFloat64(ctx, light->envelopeFrequency);
                case L2_EnvelopePhase:
                    return JS_NewFloat64(ctx, light->envelopePhase);
                case L2_IntensityModulator:
                    return JS_NewFloat64(ctx, light->intensityModulator);
                case L2_CookiePath:
                    return JS_NewString(ctx, light->cookiePath.c_str());
                case L2_CookieUVOffsetX:
                    return JS_NewFloat64(ctx, light->cookieUVOffset.x);
                case L2_CookieUVOffsetY:
                    return JS_NewFloat64(ctx, light->cookieUVOffset.y);
                case L2_CookieUVScaleX:
                    return JS_NewFloat64(ctx, light->cookieUVScale.x);
                case L2_CookieUVScaleY:
                    return JS_NewFloat64(ctx, light->cookieUVScale.y);
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_light2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *light = static_cast<Bokken::GameObject::Light2D *>(
                    JS_GetOpaque(this_val, s_light2d_class_id));
                if (!light)
                    return JS_UNDEFINED;

                // String-valued enums dispatched first because the
                // numeric coercion path below would lose the string.
                if (magic == L2_Type)
                {
                    const char *s = JS_ToCString(ctx, val);
                    if (s)
                    {
                        light->type = parseLightType(s);
                        JS_FreeCString(ctx, s);
                    }
                    return JS_UNDEFINED;
                }
                if (magic == L2_Envelope)
                {
                    const char *s = JS_ToCString(ctx, val);
                    if (s)
                    {
                        light->envelope = parseLightEnvelope(s);
                        JS_FreeCString(ctx, s);
                    }
                    return JS_UNDEFINED;
                }
                if (magic == L2_CookiePath)
                {
                    const char *s = JS_ToCString(ctx, val);
                    light->cookiePath = s ? std::string(s) : std::string();
                    if (s)
                        JS_FreeCString(ctx, s);
                    return JS_UNDEFINED;
                }
                if (magic == L2_CastsShadows)
                {
                    light->castsShadows = (JS_ToBool(ctx, val) != 0);
                    return JS_UNDEFINED;
                }
                if (magic == L2_Color)
                {
                    // Packed 0xRRGGBBAA — matches Mesh2D.color. The
                    // alpha byte is consumed and discarded since
                    // Light2D has no alpha channel. JS_ToUint32
                    // handles both `0xFF8040FF`-style integer literals
                    // and `Number` values without bit-loss for the
                    // full 32-bit range.
                    uint32_t c = 0;
                    JS_ToUint32(ctx, &c, val);
                    light->color.r = ((c >> 24) & 0xFFu) / 255.0f;
                    light->color.g = ((c >> 16) & 0xFFu) / 255.0f;
                    light->color.b = ((c >> 8) & 0xFFu) / 255.0f;
                    return JS_UNDEFINED;
                }

                double d = 0.0;
                if (JS_ToFloat64(ctx, &d, val) < 0)
                    return JS_EXCEPTION;
                float f = static_cast<float>(d);

                switch (magic)
                {
                case L2_ColorR:
                    light->color.r = f;
                    break;
                case L2_ColorG:
                    light->color.g = f;
                    break;
                case L2_ColorB:
                    light->color.b = f;
                    break;
                case L2_Intensity:
                    light->intensity = f;
                    break;
                case L2_Range:
                    light->range = f;
                    break;
                case L2_Falloff:
                    light->falloff = f;
                    break;
                case L2_InnerConeAngle:
                    light->innerConeAngle = f;
                    break;
                case L2_OuterConeAngle:
                    light->outerConeAngle = f;
                    break;
                case L2_DirectionDegrees:
                    light->directionDegrees = f;
                    break;
                case L2_ShadowSoftness:
                    light->shadowSoftness = f;
                    break;
                case L2_EnvelopeAmplitude:
                    light->envelopeAmplitude = f;
                    break;
                case L2_EnvelopeFrequency:
                    light->envelopeFrequency = f;
                    break;
                case L2_EnvelopePhase:
                    light->envelopePhase = f;
                    break;
                case L2_IntensityModulator:
                    light->intensityModulator = f;
                    break;
                case L2_CookieUVOffsetX:
                    light->cookieUVOffset.x = f;
                    break;
                case L2_CookieUVOffsetY:
                    light->cookieUVOffset.y = f;
                    break;
                case L2_CookieUVScaleX:
                    light->cookieUVScale.x = f;
                    break;
                case L2_CookieUVScaleY:
                    light->cookieUVScale.y = f;
                    break;
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_light2d_reset_envelope(JSContext *, JSValueConst this_val, int, JSValueConst *)
            {
                auto *light = static_cast<Bokken::GameObject::Light2D *>(
                    JS_GetOpaque(this_val, s_light2d_class_id));
                if (light)
                    light->resetEnvelope();
                return JS_UNDEFINED;
            }

            //  ShadowCaster2D

            JSValue GameObject::wrap_shadow_caster2d(JSContext *ctx, Bokken::GameObject::ShadowCaster2D *caster)
            {
                JSValue obj = JS_NewObjectClass(ctx, s_shadow_caster2d_class_id);
                if (JS_IsException(obj))
                    return obj;
                JS_SetOpaque(obj, caster);
                return obj;
            }

            JSValue GameObject::js_shadow_caster2d_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *caster = static_cast<Bokken::GameObject::ShadowCaster2D *>(
                    JS_GetOpaque(this_val, s_shadow_caster2d_class_id));
                if (!caster)
                    return JS_UNDEFINED;

                switch (magic)
                {
                case SC2_CastsShadow:
                    return JS_NewBool(ctx, caster->castsShadow);
                case SC2_Softness:
                    return JS_NewFloat64(ctx, caster->softness);
                case SC2_Outline:
                {
                    // Build a fresh JS array of {x, y} objects from
                    // the C++ outline. The array is a copy; mutating
                    // it doesn't affect the caster — assign a new
                    // array back via the setter to update.
                    JSValue arr = JS_NewArray(ctx);
                    for (size_t i = 0; i < caster->outline.size(); ++i)
                    {
                        JSValue pt = JS_NewObject(ctx);
                        JS_SetPropertyStr(ctx, pt, "x", JS_NewFloat64(ctx, caster->outline[i].x));
                        JS_SetPropertyStr(ctx, pt, "y", JS_NewFloat64(ctx, caster->outline[i].y));
                        JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i), pt);
                    }
                    return arr;
                }
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_shadow_caster2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *caster = static_cast<Bokken::GameObject::ShadowCaster2D *>(
                    JS_GetOpaque(this_val, s_shadow_caster2d_class_id));
                if (!caster)
                    return JS_UNDEFINED;

                if (magic == SC2_CastsShadow)
                {
                    caster->castsShadow = (JS_ToBool(ctx, val) != 0);
                    return JS_UNDEFINED;
                }
                if (magic == SC2_Outline)
                {
                    // The setter accepts a JS array of {x, y}
                    // objects (or anything with .x / .y readable
                    // as numbers). The C++ outline vector is
                    // rebuilt; an invalid entry (missing fields,
                    // non-numeric values) is skipped silently so
                    // partial data doesn't corrupt the buffer.
                    if (!JS_IsArray(val))
                    {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                    "[ShadowCaster2D] outline setter expects an array");
                        return JS_UNDEFINED;
                    }
                    JSValue lenV = JS_GetPropertyStr(ctx, val, "length");
                    uint32_t len = 0;
                    JS_ToUint32(ctx, &len, lenV);
                    JS_FreeValue(ctx, lenV);

                    caster->outline.clear();
                    caster->outline.reserve(len);
                    for (uint32_t i = 0; i < len; ++i)
                    {
                        JSValue pt = JS_GetPropertyUint32(ctx, val, i);
                        if (!JS_IsObject(pt))
                        {
                            JS_FreeValue(ctx, pt);
                            continue;
                        }
                        JSValue jx = JS_GetPropertyStr(ctx, pt, "x");
                        JSValue jy = JS_GetPropertyStr(ctx, pt, "y");
                        double dx = 0, dy = 0;
                        bool okX = JS_ToFloat64(ctx, &dx, jx) == 0;
                        bool okY = JS_ToFloat64(ctx, &dy, jy) == 0;
                        JS_FreeValue(ctx, jx);
                        JS_FreeValue(ctx, jy);
                        JS_FreeValue(ctx, pt);
                        if (okX && okY)
                        {
                            caster->outline.emplace_back(
                                static_cast<float>(dx),
                                static_cast<float>(dy));
                        }
                    }
                    return JS_UNDEFINED;
                }

                double d = 0.0;
                if (JS_ToFloat64(ctx, &d, val) < 0)
                    return JS_EXCEPTION;
                float f = static_cast<float>(d);

                switch (magic)
                {
                case SC2_Softness:
                    caster->softness = f;
                    break;
                }
                return JS_UNDEFINED;
            }

            //  NormalMap2D

            JSValue GameObject::wrap_normal_map2d(JSContext *ctx, Bokken::GameObject::NormalMap2D *nm)
            {
                JSValue obj = JS_NewObjectClass(ctx, s_normal_map2d_class_id);
                if (JS_IsException(obj))
                    return obj;
                JS_SetOpaque(obj, nm);
                return obj;
            }

            JSValue GameObject::js_normal_map2d_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *nm = static_cast<Bokken::GameObject::NormalMap2D *>(
                    JS_GetOpaque(this_val, s_normal_map2d_class_id));
                if (!nm)
                    return JS_UNDEFINED;

                switch (magic)
                {
                case NM2_NormalMapPath:
                    return JS_NewString(ctx, nm->normalMapPath.c_str());
                case NM2_AutoGenerate:
                    return JS_NewBool(ctx, nm->autoGenerate);
                case NM2_AutoStrength:
                    return JS_NewFloat64(ctx, nm->autoStrength);
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_normal_map2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *nm = static_cast<Bokken::GameObject::NormalMap2D *>(
                    JS_GetOpaque(this_val, s_normal_map2d_class_id));
                if (!nm)
                    return JS_UNDEFINED;

                // Any property mutation invalidates the cached
                // resolved texture so the next resolve() picks up
                // the change. Without this, setting normalMapPath
                // at runtime would silently keep the old texture.
                if (magic == NM2_NormalMapPath)
                {
                    const char *s = JS_ToCString(ctx, val);
                    nm->normalMapPath = s ? std::string(s) : std::string();
                    if (s)
                        JS_FreeCString(ctx, s);
                    nm->invalidate();
                    return JS_UNDEFINED;
                }
                if (magic == NM2_AutoGenerate)
                {
                    nm->autoGenerate = (JS_ToBool(ctx, val) != 0);
                    nm->invalidate();
                    return JS_UNDEFINED;
                }
                if (magic == NM2_AutoStrength)
                {
                    double d = 0.0;
                    if (JS_ToFloat64(ctx, &d, val) < 0)
                        return JS_EXCEPTION;
                    nm->autoStrength = static_cast<float>(d);
                    nm->invalidate();
                    return JS_UNDEFINED;
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_normal_map2d_invalidate(JSContext *, JSValueConst this_val, int, JSValueConst *)
            {
                auto *nm = static_cast<Bokken::GameObject::NormalMap2D *>(
                    JS_GetOpaque(this_val, s_normal_map2d_class_id));
                if (nm)
                    nm->invalidate();
                return JS_UNDEFINED;
            }

            //  AudioSource2D

            JSValue GameObject::wrap_audio_source2d(JSContext *ctx, Bokken::GameObject::AudioSource2D *src)
            {
                JSValue obj = JS_NewObjectClass(ctx, s_audio_source2d_class_id);
                if (JS_IsException(obj))
                    return obj;
                JS_SetOpaque(obj, src);
                return obj;
            }

            JSValue GameObject::js_audio_source2d_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *src = static_cast<Bokken::GameObject::AudioSource2D *>(
                    JS_GetOpaque(this_val, s_audio_source2d_class_id));
                if (!src)
                    return JS_UNDEFINED;

                switch (magic)
                {
                case AS2_Clip:
                    return JS_NewString(ctx, src->clip.c_str());
                case AS2_Channel:
                    return JS_NewString(ctx, src->channel.c_str());
                case AS2_Volume:
                    return JS_NewFloat64(ctx, src->volume);
                case AS2_Pitch:
                    return JS_NewFloat64(ctx, src->pitch);
                case AS2_Loop:
                    return JS_NewBool(ctx, src->loop);
                case AS2_AutoPlay:
                    return JS_NewBool(ctx, src->autoPlay);
                case AS2_Spatial:
                    return JS_NewBool(ctx, src->spatial);
                case AS2_MinimumDistance:
                    return JS_NewFloat64(ctx, src->minimumDistance);
                case AS2_MaximumDistance:
                    return JS_NewFloat64(ctx, src->maximumDistance);
                case AS2_Rolloff:
                    return JS_NewFloat64(ctx, src->rolloff);
                case AS2_Doppler:
                    return JS_NewBool(ctx, src->doppler);
                case AS2_IsPlaying:
                    return JS_NewBool(ctx, src->isPlaying());
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_audio_source2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *src = static_cast<Bokken::GameObject::AudioSource2D *>(
                    JS_GetOpaque(this_val, s_audio_source2d_class_id));
                if (!src)
                    return JS_UNDEFINED;

                // String properties handled separately so we can manage
                // the JS_ToCString lifetime cleanly per branch.
                if (magic == AS2_Clip || magic == AS2_Channel)
                {
                    const char *s = JS_ToCString(ctx, val);
                    if (!s)
                        return JS_EXCEPTION;
                    if (magic == AS2_Clip)
                        src->clip = s;
                    else
                        src->channel = s;
                    JS_FreeCString(ctx, s);
                    return JS_UNDEFINED;
                }

                // Boolean properties.
                if (magic == AS2_Loop || magic == AS2_AutoPlay ||
                    magic == AS2_Spatial || magic == AS2_Doppler)
                {
                    bool b = JS_ToBool(ctx, val);
                    switch (magic)
                    {
                    case AS2_Loop:
                        src->loop = b;
                        break;
                    case AS2_AutoPlay:
                        src->autoPlay = b;
                        break;
                    case AS2_Spatial:
                        src->spatial = b;
                        break;
                    case AS2_Doppler:
                        src->doppler = b;
                        break;
                    }
                    return JS_UNDEFINED;
                }

                // isPlaying is read-only — silently ignore writes so
                // an accidental assignment doesn't throw a confusing
                // error.
                if (magic == AS2_IsPlaying)
                    return JS_UNDEFINED;

                double d = 0.0;
                if (JS_ToFloat64(ctx, &d, val) < 0)
                    return JS_EXCEPTION;
                float f = static_cast<float>(d);

                switch (magic)
                {
                case AS2_Volume:
                    src->volume = f;
                    break;
                case AS2_Pitch:
                    src->pitch = f;
                    break;
                case AS2_MinimumDistance:
                    src->minimumDistance = f;
                    break;
                case AS2_MaximumDistance:
                    src->maximumDistance = f;
                    break;
                case AS2_Rolloff:
                    src->rolloff = f;
                    break;
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_audio_source2d_play(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *src = static_cast<Bokken::GameObject::AudioSource2D *>(
                    JS_GetOpaque(this_val, s_audio_source2d_class_id));
                if (src)
                    src->play();
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_audio_source2d_stop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *src = static_cast<Bokken::GameObject::AudioSource2D *>(
                    JS_GetOpaque(this_val, s_audio_source2d_class_id));
                if (!src)
                    return JS_UNDEFINED;

                // Optional fade-out seconds. Default matches the C++
                // default (5 ms) to suppress click on hard cuts.
                float fadeSec = 0.005f;
                if (argc >= 1 && !JS_IsUndefined(argv[0]))
                {
                    double d = 0.0;
                    if (JS_ToFloat64(ctx, &d, argv[0]) == 0)
                        fadeSec = static_cast<float>(d);
                }
                src->stop(fadeSec);
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_audio_source2d_pause(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *src = static_cast<Bokken::GameObject::AudioSource2D *>(
                    JS_GetOpaque(this_val, s_audio_source2d_class_id));
                if (src)
                    src->pause();
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_audio_source2d_resume(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *src = static_cast<Bokken::GameObject::AudioSource2D *>(
                    JS_GetOpaque(this_val, s_audio_source2d_class_id));
                if (src)
                    src->resume();
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_audio_source2d_play_one_shot(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *src = static_cast<Bokken::GameObject::AudioSource2D *>(
                    JS_GetOpaque(this_val, s_audio_source2d_class_id));
                if (!src)
                    return JS_UNDEFINED;

                std::string clipOverride;
                if (argc >= 1 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0]))
                {
                    const char *s = JS_ToCString(ctx, argv[0]);
                    if (s)
                    {
                        clipOverride = s;
                        JS_FreeCString(ctx, s);
                    }
                }
                src->playOneShot(clipOverride);
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_audio_source2d_play_one_shot_at(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *src = static_cast<Bokken::GameObject::AudioSource2D *>(
                    JS_GetOpaque(this_val, s_audio_source2d_class_id));
                if (!src || argc < 2)
                    return JS_UNDEFINED;

                double px = 0.0, py = 0.0;
                if (JS_ToFloat64(ctx, &px, argv[0]) < 0 ||
                    JS_ToFloat64(ctx, &py, argv[1]) < 0)
                    return JS_EXCEPTION;

                std::string clipOverride;
                if (argc >= 3 && !JS_IsUndefined(argv[2]) && !JS_IsNull(argv[2]))
                {
                    const char *s = JS_ToCString(ctx, argv[2]);
                    if (s)
                    {
                        clipOverride = s;
                        JS_FreeCString(ctx, s);
                    }
                }
                src->playOneShotAt(static_cast<float>(px), static_cast<float>(py), clipOverride);
                return JS_UNDEFINED;
            }

            //  AudioListener2D

            JSValue GameObject::wrap_audio_listener2d(JSContext *ctx, Bokken::GameObject::AudioListener2D *lst)
            {
                JSValue obj = JS_NewObjectClass(ctx, s_audio_listener2d_class_id);
                if (JS_IsException(obj))
                    return obj;
                JS_SetOpaque(obj, lst);
                return obj;
            }

            JSValue GameObject::js_audio_listener2d_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *lst = static_cast<Bokken::GameObject::AudioListener2D *>(
                    JS_GetOpaque(this_val, s_audio_listener2d_class_id));
                if (!lst)
                    return JS_UNDEFINED;

                switch (magic)
                {
                case AL2_Gain:
                    return JS_NewFloat64(ctx, lst->gain);
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_audio_listener2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *lst = static_cast<Bokken::GameObject::AudioListener2D *>(
                    JS_GetOpaque(this_val, s_audio_listener2d_class_id));
                if (!lst)
                    return JS_UNDEFINED;

                double d = 0.0;
                if (JS_ToFloat64(ctx, &d, val) < 0)
                    return JS_EXCEPTION;
                float f = static_cast<float>(d);

                switch (magic)
                {
                case AL2_Gain:
                    lst->gain = f;
                    break;
                }
                return JS_UNDEFINED;
            }

            // Sprite2D property getters.
            JSValue GameObject::js_sprite2d_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *sprite = static_cast<Bokken::GameObject::Sprite2D *>(
                    JS_GetOpaque(this_val, s_sprite2d_class_id));
                if (!sprite)
                    return JS_UNDEFINED;

                switch (magic)
                {
                case S2_source:
                    return JS_NewString(ctx, sprite->source.c_str());
                case S2_RegionName:
                    return JS_NewString(ctx, sprite->regionName.c_str());
                case S2_Tint:
                {
                    uint32_t packed =
                        (static_cast<uint32_t>(sprite->tint.r * 255) << 24) |
                        (static_cast<uint32_t>(sprite->tint.g * 255) << 16) |
                        (static_cast<uint32_t>(sprite->tint.b * 255) << 8) |
                        static_cast<uint32_t>(sprite->tint.a * 255);
                    return JS_NewUint32(ctx, packed);
                }
                case S2_Opacity:
                    return JS_NewFloat64(ctx, sprite->opacity);
                case S2_FlipX:
                    return JS_NewBool(ctx, sprite->flipX);
                case S2_FlipY:
                    return JS_NewBool(ctx, sprite->flipY);
                case S2_OverrideWidth:
                    return JS_NewFloat64(ctx, sprite->overrideWidth);
                case S2_OverrideHeight:
                    return JS_NewFloat64(ctx, sprite->overrideHeight);
                case S2_AnchorX:
                    return JS_NewFloat64(ctx, sprite->anchorX);
                case S2_AnchorY:
                    return JS_NewFloat64(ctx, sprite->anchorY);
                case S2_BlendMode:
                {
                    const char *name = "alpha";
                    if (sprite->blendMode == Bokken::Renderer::BlendMode::Additive)
                        name = "additive";
                    else if (sprite->blendMode == Bokken::Renderer::BlendMode::Screen)
                        name = "screen";
                    return JS_NewString(ctx, name);
                }
                }
                return JS_UNDEFINED;
            }

            // Sprite2D property setters.
            JSValue GameObject::js_sprite2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *sprite = static_cast<Bokken::GameObject::Sprite2D *>(
                    JS_GetOpaque(this_val, s_sprite2d_class_id));
                if (!sprite)
                    return JS_UNDEFINED;

                switch (magic)
                {
                case S2_source:
                {
                    const char *str = JS_ToCString(ctx, val);
                    if (str)
                    {
                        sprite->source = str;

                        // Eagerly load the texture into the cache so it's ready
                        // by the time present() runs.
                        if (s_textures && !sprite->source.empty())
                        {
                            auto &engine = Bokken::Scripting::Engine::Instance();
                            // The AssetPack pointer lives on the Engine — we
                            // stored it during init. For now we access it via
                            // the texture cache's own load which requires it.
                            // The cache will no-op if already loaded.
                        }

                        JS_FreeCString(ctx, str);
                    }
                    return JS_UNDEFINED;
                }
                case S2_RegionName:
                {
                    const char *str = JS_ToCString(ctx, val);
                    if (str)
                    {
                        sprite->regionName = str;
                        JS_FreeCString(ctx, str);
                    }
                    return JS_UNDEFINED;
                }
                case S2_FlipX:
                    sprite->flipX = JS_ToBool(ctx, val);
                    return JS_UNDEFINED;
                case S2_FlipY:
                    sprite->flipY = JS_ToBool(ctx, val);
                    return JS_UNDEFINED;
                case S2_Tint:
                {
                    uint32_t c = 0;
                    JS_ToUint32(ctx, &c, val);
                    sprite->tint.r = ((c >> 24) & 0xFF) / 255.0f;
                    sprite->tint.g = ((c >> 16) & 0xFF) / 255.0f;
                    sprite->tint.b = ((c >> 8) & 0xFF) / 255.0f;
                    sprite->tint.a = ((c >> 0) & 0xFF) / 255.0f;
                    return JS_UNDEFINED;
                }
                case S2_BlendMode:
                {
                    const char *str = JS_ToCString(ctx, val);
                    if (str)
                    {
                        if (strcmp(str, "additive") == 0)
                            sprite->blendMode = Bokken::Renderer::BlendMode::Additive;
                        else if (strcmp(str, "screen") == 0)
                            sprite->blendMode = Bokken::Renderer::BlendMode::Screen;
                        else
                            sprite->blendMode = Bokken::Renderer::BlendMode::Alpha;
                        JS_FreeCString(ctx, str);
                    }
                    return JS_UNDEFINED;
                }
                }

                // Float properties.
                double d = 0.0;
                if (JS_ToFloat64(ctx, &d, val) < 0)
                    return JS_EXCEPTION;
                float f = static_cast<float>(d);

                switch (magic)
                {
                case S2_Opacity:
                    sprite->opacity = f;
                    break;
                case S2_OverrideWidth:
                    sprite->overrideWidth = f;
                    break;
                case S2_OverrideHeight:
                    sprite->overrideHeight = f;
                    break;
                case S2_AnchorX:
                    sprite->anchorX = f;
                    break;
                case S2_AnchorY:
                    sprite->anchorY = f;
                    break;
                }
                return JS_UNDEFINED;
            }

            // Animation2D read-only property getters.
            JSValue GameObject::js_animation2d_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *anim = static_cast<Bokken::GameObject::Animation2D *>(
                    JS_GetOpaque(this_val, s_animation2d_class_id));
                if (!anim)
                    return JS_UNDEFINED;

                switch (magic)
                {
                case A2_IsPlaying:
                    return JS_NewBool(ctx, anim->isPlaying());
                case A2_ActiveClip:
                    return JS_NewString(ctx, anim->activeClip().c_str());
                case A2_FrameIndex:
                    return JS_NewInt32(ctx, anim->frameIndex());
                case A2_CurrentRegion:
                    return JS_NewString(ctx, anim->currentRegion().c_str());
                }
                return JS_UNDEFINED;
            }

            // JS: animation.play("run")
            JSValue GameObject::js_animation2d_play(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *anim = static_cast<Bokken::GameObject::Animation2D *>(
                    JS_GetOpaque(this_val, s_animation2d_class_id));
                if (!anim || argc < 1)
                    return JS_UNDEFINED;

                const char *name = JS_ToCString(ctx, argv[0]);
                if (name)
                {
                    anim->play(name);
                    JS_FreeCString(ctx, name);
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_animation2d_pause(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *anim = static_cast<Bokken::GameObject::Animation2D *>(
                    JS_GetOpaque(this_val, s_animation2d_class_id));
                if (anim)
                    anim->pause();
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_animation2d_stop(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *anim = static_cast<Bokken::GameObject::Animation2D *>(
                    JS_GetOpaque(this_val, s_animation2d_class_id));
                if (anim)
                    anim->stop();
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_animation2d_resume(JSContext *ctx, JSValueConst this_val, int, JSValueConst *)
            {
                auto *anim = static_cast<Bokken::GameObject::Animation2D *>(
                    JS_GetOpaque(this_val, s_animation2d_class_id));
                if (anim)
                    anim->resume();
                return JS_UNDEFINED;
            }

            // JS: animation.addClip({ name, frames, framesPerSecond?, loop? })
            //
            // frames can be:
            //   - An array of region name strings (explicit regions).
            //   - An object { frameWidth, frameHeight, count?, offsetX?, offsetY?,
            //     paddingX?, paddingY? } which auto-slices the sibling Sprite2D's
            //     texture into a grid of regions.
            JSValue GameObject::js_animation2d_add_clip(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
            {
                auto *anim = static_cast<Bokken::GameObject::Animation2D *>(
                    JS_GetOpaque(this_val, s_animation2d_class_id));
                if (!anim || argc < 1 || !JS_IsObject(argv[0]))
                    return JS_UNDEFINED;

                JSValue clipObj = argv[0];

                // name (required)
                JSValue nameVal = JS_GetPropertyStr(ctx, clipObj, "name");
                const char *nameStr = JS_ToCString(ctx, nameVal);
                std::string clipName;
                if (nameStr)
                {
                    clipName = nameStr;
                    JS_FreeCString(ctx, nameStr);
                }
                JS_FreeValue(ctx, nameVal);

                if (clipName.empty())
                    return JS_UNDEFINED;

                // framesPerSecond (optional, default 12)
                float framesPerSecond = 12.0f;
                JSValue framesPerSecondVal = JS_GetPropertyStr(ctx, clipObj, "framesPerSecond");
                if (JS_IsNumber(framesPerSecondVal))
                {
                    double d = 0;
                    JS_ToFloat64(ctx, &d, framesPerSecondVal);
                    framesPerSecond = static_cast<float>(d);
                }
                JS_FreeValue(ctx, framesPerSecondVal);

                // loop (optional, default "Loop")
                Bokken::GameObject::AnimationLoop loop = Bokken::GameObject::AnimationLoop::Loop;
                JSValue loopVal = JS_GetPropertyStr(ctx, clipObj, "loop");
                if (JS_IsString(loopVal))
                {
                    const char *loopStr = JS_ToCString(ctx, loopVal);
                    if (loopStr)
                    {
                        if (strcmp(loopStr, "None") == 0)
                            loop = Bokken::GameObject::AnimationLoop::None;
                        else if (strcmp(loopStr, "PingPong") == 0)
                            loop = Bokken::GameObject::AnimationLoop::PingPong;
                        JS_FreeCString(ctx, loopStr);
                    }
                }
                JS_FreeValue(ctx, loopVal);

                // frames — array or object
                JSValue framesVal = JS_GetPropertyStr(ctx, clipObj, "frames");

                if (JS_IsArray(framesVal))
                {
                    // Explicit region name list.
                    Bokken::GameObject::AnimationClip clip;
                    clip.name = clipName;
                    clip.framesPerSecond = framesPerSecond;
                    clip.loop = loop;

                    uint32_t len = 0;
                    JSValue lenVal = JS_GetPropertyStr(ctx, framesVal, "length");
                    JS_ToUint32(ctx, &len, lenVal);
                    JS_FreeValue(ctx, lenVal);

                    for (uint32_t i = 0; i < len; ++i)
                    {
                        JSValue elem = JS_GetPropertyUint32(ctx, framesVal, i);
                        const char *frameStr = JS_ToCString(ctx, elem);
                        if (frameStr)
                        {
                            clip.frames.push_back(frameStr);
                            JS_FreeCString(ctx, frameStr);
                        }
                        JS_FreeValue(ctx, elem);
                    }

                    anim->addClip(clip);
                }
                else if (JS_IsObject(framesVal))
                {
                    // Auto-slice a sprite sheet into frames.
                    int32_t frameW = 0, frameH = 0;
                    int32_t count = 0, offX = 0, offY = 0, padX = 0, padY = 0;

                    auto readInt = [&](const char *prop, int32_t &out)
                    {
                        JSValue v = JS_GetPropertyStr(ctx, framesVal, prop);
                        if (JS_IsNumber(v))
                            JS_ToInt32(ctx, &out, v);
                        JS_FreeValue(ctx, v);
                    };

                    readInt("frameWidth", frameW);
                    readInt("frameHeight", frameH);
                    readInt("count", count);
                    readInt("offsetX", offX);
                    readInt("offsetY", offY);
                    readInt("paddingX", padX);
                    readInt("paddingY", padY);

                    // Optional per-clip texture path. When present, this clip
                    // sources from a different sprite sheet than the Sprite2D's
                    // default. Useful for separate idle/run/jump PNGs.
                    std::string clipsource;
                    JSValue texPathVal = JS_GetPropertyStr(ctx, framesVal, "source");
                    if (JS_IsString(texPathVal))
                    {
                        const char *s = JS_ToCString(ctx, texPathVal);
                        if (s)
                        {
                            clipsource = s;
                            JS_FreeCString(ctx, s);
                        }
                    }
                    JS_FreeValue(ctx, texPathVal);

                    if (frameW <= 0 || frameH <= 0)
                    {
                        JS_FreeValue(ctx, framesVal);
                        return JS_ThrowTypeError(ctx,
                                                 "addClip: frames object requires frameWidth and frameHeight > 0");
                    }

                    anim->addClipFromGrid(clipName, frameW, frameH,
                                          count, offX, offY, padX, padY,
                                          framesPerSecond, loop, clipsource);
                }

                JS_FreeValue(ctx, framesVal);
                return JS_DupValue(ctx, this_val);
            }

            // Copies every own enumerable property from `props` onto `target` by
            // reading the key from props and setting it on target. This triggers
            // the target's setters (CGETSET_MAGIC) so { shape: "Quad" } on a
            // Mesh2D handle calls js_mesh2d_set with M2_Shape automatically.
            void GameObject::apply_props(JSContext *ctx, JSValue target, JSValue props)
            {
                JSPropertyEnum *tab = nullptr;
                uint32_t len = 0;

                if (JS_GetOwnPropertyNames(ctx, &tab, &len, props,
                                           JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0)
                    return;

                for (uint32_t i = 0; i < len; i++)
                {
                    JSValue val = JS_GetProperty(ctx, props, tab[i].atom);
                    JS_SetProperty(ctx, target, tab[i].atom, val);
                    // JS_SetProperty takes ownership of val, don't free it.
                }

                for (uint32_t i = 0; i < len; i++)
                    JS_FreeAtom(ctx, tab[i].atom);

                js_free(ctx, tab);
            }

            // Utility.
            JSValue GameObject::make_vec2(JSContext *ctx, const glm::vec2 &v)
            {
                JSValue obj = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, v.x));
                JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, v.y));
                return obj;
            }

            bool GameObject::read_vec2(JSContext *ctx, JSValueConst val, glm::vec2 &out)
            {
                if (!JS_IsObject(val))
                    return false;

                auto readField = [&](const char *k, float &dst) -> bool
                {
                    JSValue v = JS_GetPropertyStr(ctx, val, k);
                    double d = 0.0;
                    int rc = JS_ToFloat64(ctx, &d, v);
                    JS_FreeValue(ctx, v);
                    if (rc < 0)
                        return false;
                    dst = static_cast<float>(d);
                    return true;
                };

                return readField("x", out.x) && readField("y", out.y);
            }

            Bokken::GameObject::Shape2D GameObject::parse_shape2d(const char *name)
            {
                if (!name)
                    return Bokken::GameObject::Shape2D::Empty;
                if (strcmp(name, "Quad") == 0)
                    return Bokken::GameObject::Shape2D::Quad;
                if (strcmp(name, "Circle") == 0)
                    return Bokken::GameObject::Shape2D::Circle;
                if (strcmp(name, "Triangle") == 0)
                    return Bokken::GameObject::Shape2D::Triangle;
                if (strcmp(name, "Line") == 0)
                    return Bokken::GameObject::Shape2D::Line;
                return Bokken::GameObject::Shape2D::Empty;
            }

            const char *GameObject::shape2d_to_string(Bokken::GameObject::Shape2D shape)
            {
                switch (shape)
                {
                case Bokken::GameObject::Shape2D::Quad:
                    return "Quad";
                case Bokken::GameObject::Shape2D::Circle:
                    return "Circle";
                case Bokken::GameObject::Shape2D::Triangle:
                    return "Triangle";
                case Bokken::GameObject::Shape2D::Line:
                    return "Line";
                case Bokken::GameObject::Shape2D::Empty:
                default:
                    return "Empty";
                }
            }

            void GameObject::destroy(JSContext * /*ctx*/)
            {
                // Clear the scene. Destroying each owned object runs its
                // components' destructors — including ~JSBehaviour, which
                // frees the behaviour's cached lifecycle-hook JSValues and
                // its instance handle — while the JSContext is still alive.
                // The name index holds raw Base* back-pointers into these
                // objects, so it must be cleared in lockstep or it would be
                // left dangling for the next session's find(). Class IDs are
                // process-global and intentionally retained.
                Bokken::GameObject::Base::s_objects.clear();
                Bokken::GameObject::Base::s_nameIndex.clear();
            }

            void GameObject::update(float deltaTime)
            {
                // Snapshot the object list before iterating. User code
                // running in component update() can call new GameObject(),
                // which push_backs into s_objects — if that reallocates
                // the underlying vector, the range-for's cached iterators
                // dangle and the next iteration crashes with a null deref.
                // Copying raw pointers is cheap (they're 8 bytes each)
                // and decouples the iteration from the container's
                // reallocation behaviour.
                //
                // Newly-spawned objects this frame won't be ticked until
                // next frame. That's the standard convention; spawning a
                // GameObject inside update is a "queue for next tick"
                // operation, not an "execute immediately" one.
                std::vector<Bokken::GameObject::Base *> snapshot;
                snapshot.reserve(Bokken::GameObject::Base::s_objects.size());
                for (auto &go : Bokken::GameObject::Base::s_objects)
                    snapshot.push_back(go.get());

                for (auto *go : snapshot)
                    if (go)
                        go->update(deltaTime);

                Bokken::GameObject::Base::sweepIdle();
                Bokken::GameObject::Base::flushDestroyed();
            }

            void GameObject::fixedUpdate(float deltaTime)
            {
                // Same reasoning as update — snapshot before iterating so
                // physics-step-driven spawns don't invalidate the loop.
                std::vector<Bokken::GameObject::Base *> snapshot;
                snapshot.reserve(Bokken::GameObject::Base::s_objects.size());
                for (auto &go : Bokken::GameObject::Base::s_objects)
                    snapshot.push_back(go.get());

                for (auto *go : snapshot)
                    if (go)
                        go->fixedUpdate(deltaTime);
            }

            void GameObject::present()
            {
                if (!s_window || !s_batcher)
                    return;

                // Draw into render space, not window space. Under the
                // Fixed and FixedHeight render policies these differ
                // from the window dimensions; under FollowWindow they
                // match. Either way, this matches what
                // SpriteBatcher::begin was called with up in
                // Renderer::Base::beginFrame, so projection &
                // viewport agree with the coordinates we hand the
                // batcher below.
                int w = 0, h = 0;
                if (s_renderer)
                {
                    w = s_renderer->renderWidth();
                    h = s_renderer->renderHeight();
                }
                else
                {
                    // Defensive fallback for tooling / tests that
                    // never wired a renderer: fall back to the window
                    // pixel size so we don't silently black-out the
                    // scene.
                    SDL_GetWindowSizeInPixels(s_window, &w, &h);
                }
                if (w <= 0 || h <= 0)
                    return;

                float hw = w * 0.5f;
                float hh = h * 0.5f;

                // Find the active camera.
                float cameraX = 0.0f;
                float cameraY = 0.0f;
                float pixelsPerUnit = 64.0f;

                for (auto &go : Bokken::GameObject::Base::s_objects)
                {
                    auto *cam = go->getComponent<Bokken::GameObject::Camera2D>();
                    if (!cam || !cam->isActive)
                        continue;

                    auto *ct = go->getComponent<Bokken::GameObject::Transform2D>();
                    if (ct)
                    {
                        cameraX = ct->position.x;
                        cameraY = ct->position.y;
                    }
                    pixelsPerUnit = cam->zoom;
                    break;
                }

                // Helper: compute world-space transform by walking the parent chain.
                // Accumulates position (with parent rotation applied), rotation, and scale.
                struct WorldTransform
                {
                    float x, y, rotation, scaleX, scaleY, zOrder;
                };

                auto computeWorld = [](Bokken::GameObject::Base *go,
                                       Bokken::GameObject::Transform2D *localT) -> WorldTransform
                {
                    // Collect ancestor chain (child -> ... -> root).
                    struct AncestorT
                    {
                        float px, py, rot, sx, sy, z;
                    };

                    // Start with the object's own local transform.
                    float worldX = localT->position.x;
                    float worldY = localT->position.y;
                    float worldRot = localT->rotation;
                    float worldSX = localT->scale.x;
                    float worldSY = localT->scale.y;
                    float worldZ = localT->zOrder;

                    // Walk up the parent chain and apply each parent's transform.
                    auto *parent = go->getParent();
                    while (parent)
                    {
                        auto *pt = parent->getComponent<Bokken::GameObject::Transform2D>();
                        if (!pt)
                            break;

                        float pRad = pt->rotation * (3.14159265f / 180.0f);
                        float cosR = std::cos(pRad);
                        float sinR = std::sin(pRad);

                        // Scale the local position by parent scale, then rotate, then translate.
                        float lx = worldX * pt->scale.x;
                        float ly = worldY * pt->scale.y;
                        float rx = lx * cosR - ly * sinR;
                        float ry = lx * sinR + ly * cosR;

                        worldX = pt->position.x + rx;
                        worldY = pt->position.y + ry;
                        worldRot = worldRot + pt->rotation;
                        worldSX = worldSX * pt->scale.x;
                        worldSY = worldSY * pt->scale.y;
                        worldZ = worldZ + pt->zOrder;

                        parent = parent->getParent();
                    }

                    return {worldX, worldY, worldRot, worldSX, worldSY, worldZ};
                };

                // Render sprites (textured quads from Sprite2D component).
                // If a GameObject has both Sprite2D and Mesh2D, the sprite
                // takes priority and the mesh is skipped.
                for (auto &go : Bokken::GameObject::Base::s_objects)
                {
                    auto *t = go->getComponent<Bokken::GameObject::Transform2D>();
                    auto *sprite = go->getComponent<Bokken::GameObject::Sprite2D>();
                    if (!t || !sprite || sprite->source.empty() || !s_textures)
                        continue;

                    // Resolve the texture region.
                    const Bokken::Renderer::TextureRegion *reg = nullptr;
                    Bokken::Renderer::TextureRegion fullReg;

                    if (!sprite->regionName.empty())
                        reg = s_textures->region(sprite->regionName);

                    if (!reg)
                    {
                        fullReg = s_textures->fullRegion(sprite->source);
                        if (!fullReg.isValid() && s_assets)
                        {
                            if (s_textures->load(sprite->source, s_assets))
                                fullReg = s_textures->fullRegion(sprite->source);
                        }

                        if (!fullReg.isValid())
                            continue;
                        reg = &fullReg;
                    }

                    WorldTransform wt = computeWorld(go.get(), t);

                    // Determine draw size: use override dimensions or source pixels.
                    float srcW = (sprite->overrideWidth > 0.0f)
                                     ? sprite->overrideWidth
                                     : static_cast<float>(reg->w);
                    float srcH = (sprite->overrideHeight > 0.0f)
                                     ? sprite->overrideHeight
                                     : static_cast<float>(reg->h);

                    // Aspect ratio preservation when only one dimension is set.
                    if (sprite->overrideWidth > 0.0f && sprite->overrideHeight <= 0.0f)
                        srcH = srcW * (static_cast<float>(reg->h) / static_cast<float>(reg->w));
                    else if (sprite->overrideHeight > 0.0f && sprite->overrideWidth <= 0.0f)
                        srcW = srcH * (static_cast<float>(reg->w) / static_cast<float>(reg->h));

                    // Scale by transform scale and camera zoom. srcW/srcH are
                    // in "pixels at 1:1 zoom" — we convert to world units by
                    // dividing by pixelsPerUnit, then back to screen pixels.
                    float sw = (srcW / pixelsPerUnit) * wt.scaleX * pixelsPerUnit;
                    float sh = (srcH / pixelsPerUnit) * wt.scaleY * pixelsPerUnit;

                    float screenCX = hw + (wt.x - cameraX) * pixelsPerUnit;
                    float screenCY = hh - (wt.y - cameraY) * pixelsPerUnit;

                    // Apply anchor offset.
                    float drawX = screenCX - sw * sprite->anchorX;
                    float drawY = screenCY - sh * sprite->anchorY;

                    // Compute tint with opacity.
                    float alpha = sprite->tint.a * sprite->opacity;
                    uint32_t rgba =
                        (static_cast<uint32_t>(sprite->tint.r * 255) << 24) |
                        (static_cast<uint32_t>(sprite->tint.g * 255) << 16) |
                        (static_cast<uint32_t>(sprite->tint.b * 255) << 8) |
                        static_cast<uint32_t>(alpha * 255);

                    // Handle flip by swapping UVs.
                    float u0 = sprite->flipX ? reg->u1 : reg->u0;
                    float u1 = sprite->flipX ? reg->u0 : reg->u1;
                    float v0 = sprite->flipY ? reg->v1 : reg->v0;
                    float v1 = sprite->flipY ? reg->v0 : reg->v1;

                    // drawTextured expects a non-const texture pointer for binding.
                    // The const_cast is safe — bind() only calls glBindTexture.
                    auto *bindTex = const_cast<Bokken::Renderer::Texture2D *>(reg->texture);

                    s_batcher->drawTextured(bindTex,
                                            drawX, drawY, sw, sh,
                                            u0, v0, u1, v1,
                                            rgba, static_cast<int>(wt.zOrder),
                                            sprite->blendMode,
                                            -wt.rotation);
                }

                // Render meshes (solid-color primitives from Mesh2D).
                // Skipped for GameObjects that already have a Sprite2D.
                for (auto &go : Bokken::GameObject::Base::s_objects)
                {
                    auto *t = go->getComponent<Bokken::GameObject::Transform2D>();
                    auto *mesh = go->getComponent<Bokken::GameObject::Mesh2D>();
                    if (!t || !mesh || mesh->shape == Bokken::GameObject::Shape2D::Empty)
                        continue;

                    // Skip if this object has a Sprite2D — it was already rendered above.
                    if (go->hasComponent<Bokken::GameObject::Sprite2D>())
                        continue;

                    WorldTransform wt = computeWorld(go.get(), t);

                    float sw = wt.scaleX * pixelsPerUnit;
                    float sh = wt.scaleY * pixelsPerUnit;

                    // Screen-space center of this object.
                    float screenCX = hw + (wt.x - cameraX) * pixelsPerUnit;
                    float screenCY = hh - (wt.y - cameraY) * pixelsPerUnit;

                    uint32_t rgba =
                        (static_cast<uint32_t>(mesh->color.r * 255) << 24) |
                        (static_cast<uint32_t>(mesh->color.g * 255) << 16) |
                        (static_cast<uint32_t>(mesh->color.b * 255) << 8) |
                        static_cast<uint32_t>(mesh->color.a * 255);

                    if (wt.rotation != 0.0f)
                    {
                        s_batcher->drawRotatedRect(screenCX, screenCY, sw, sh, -wt.rotation, rgba, static_cast<int>(wt.zOrder));
                    }
                    else
                    {
                        // Fast path: axis-aligned.
                        float sx = screenCX - sw * 0.5f;
                        float sy = screenCY - sh * 0.5f;
                        s_batcher->drawRect(sx, sy, sw, sh, rgba, static_cast<int>(wt.zOrder));
                    }
                }

                // Render particles.
                for (auto &go : Bokken::GameObject::Base::s_objects)
                {
                    auto *emitter = go->getComponent<Bokken::GameObject::ParticleEmitter2D>();
                    if (!emitter)
                        continue;

                    emitter->render(s_batcher, cameraX, cameraY, pixelsPerUnit, hw, hh);
                }
            }

            // Collider2D bindings.
            //
            // Layout: every concrete collider's getter/setter calls
            // js_collider2d_get_base / js_collider2d_set_base for any
            // magic value below Col2_BaseEnd, then dispatches its own
            // shape-specific switch for higher magic values.

            // RAII wrapper for a JS callback. Owns one ref on the
            // JSValue and frees it via the captured context when the
            // wrapper is destroyed (either because the std::function
            // is replaced or because the Collider2D goes away). The
            // ctx pointer is stable for the lifetime of the engine —
            // the scripting Engine owns it and shutdown happens after
            // every collider has been destroyed.
            namespace
            {
                struct JsCallback
                {
                    JSContext *ctx = nullptr;
                    JSValue fn = JS_UNDEFINED;

                    JsCallback() = default;
                    JsCallback(JSContext *c, JSValue f) : ctx(c), fn(f) {}

                    JsCallback(const JsCallback &other) : ctx(other.ctx)
                    {
                        fn = ctx ? JS_DupValue(ctx, other.fn) : JS_UNDEFINED;
                    }
                    JsCallback &operator=(const JsCallback &other)
                    {
                        if (this == &other)
                            return *this;
                        if (ctx && !JS_IsUndefined(fn))
                            JS_FreeValue(ctx, fn);
                        ctx = other.ctx;
                        fn = ctx ? JS_DupValue(ctx, other.fn) : JS_UNDEFINED;
                        return *this;
                    }
                    JsCallback(JsCallback &&other) noexcept
                        : ctx(other.ctx), fn(other.fn)
                    {
                        other.ctx = nullptr;
                        other.fn = JS_UNDEFINED;
                    }
                    JsCallback &operator=(JsCallback &&other) noexcept
                    {
                        if (this == &other)
                            return *this;
                        if (ctx && !JS_IsUndefined(fn))
                            JS_FreeValue(ctx, fn);
                        ctx = other.ctx;
                        fn = other.fn;
                        other.ctx = nullptr;
                        other.fn = JS_UNDEFINED;
                        return *this;
                    }
                    ~JsCallback()
                    {
                        if (ctx && !JS_IsUndefined(fn))
                            JS_FreeValue(ctx, fn);
                    }
                };

                // Build a vec2 {x, y} JS object for callback arguments.
                JSValue cbVec2(JSContext *ctx, float x, float y)
                {
                    JSValue obj = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, x));
                    JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, y));
                    return obj;
                }

                // Find the GameObject JS wrapper for a Collider2D's owner,
                // or JS_UNDEFINED if none exists. Used by callback dispatch
                // to give JS code a handle to the *other* collider's owning
                // GameObject without forcing the user to walk back from a
                // collider handle.
                JSValue makeOtherHandle(JSContext *ctx, Bokken::GameObject::Collider2D *col)
                {
                    if (!col)
                        return JS_NULL;
                    JSValue obj = JS_NewObjectClass(ctx, GameObject::s_class_id);
                    if (JS_IsException(obj))
                        return JS_NULL;
                    JS_SetOpaque(obj, col->gameObject);
                    return obj;
                }
            }

            Bokken::GameObject::Collider2D *GameObject::unwrap_collider2d(JSValueConst val)
            {
                static const JSClassID ids[] = {
                    s_box_collider2d_class_id,
                    s_circle_collider2d_class_id,
                    s_capsule_collider2d_class_id,
                    s_polygon_collider2d_class_id,
                    s_edge_collider2d_class_id,
                    s_chain_collider2d_class_id,
                };
                for (JSClassID id : ids)
                {
                    void *p = JS_GetOpaque(val, id);
                    if (p)
                        return static_cast<Bokken::GameObject::Collider2D *>(p);
                }
                return nullptr;
            }

            // Shared base getter — used by every concrete collider's
            // js_<name>_get for magic values below Col2_BaseEnd.
            JSValue GameObject::js_collider2d_get_base(JSContext *ctx, Bokken::GameObject::Collider2D *col, int magic)
            {
                if (!col)
                    return JS_UNDEFINED;
                switch (magic)
                {
                case Col2_Density:
                    return JS_NewFloat64(ctx, col->density);
                case Col2_Friction:
                    return JS_NewFloat64(ctx, col->friction);
                case Col2_Restitution:
                    return JS_NewFloat64(ctx, col->restitution);
                case Col2_TangentSpeed:
                    return JS_NewFloat64(ctx, col->tangentSpeed);
                case Col2_IsSensor:
                    return JS_NewBool(ctx, col->isSensor);
                case Col2_CategoryBits:
                    return JS_NewInt64(ctx, static_cast<int64_t>(col->categoryBits));
                case Col2_MaskBits:
                    return JS_NewInt64(ctx, static_cast<int64_t>(col->maskBits));
                case Col2_GroupIndex:
                    return JS_NewInt32(ctx, col->groupIndex);
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_collider2d_set_base(JSContext *ctx, Bokken::GameObject::Collider2D *col, JSValueConst val, int magic)
            {
                if (!col)
                    return JS_UNDEFINED;

                // isSensor / category-mask-group are bool/int and have to
                // be peeled off before the float-coerce common path.
                switch (magic)
                {
                case Col2_IsSensor:
                    col->setSensor(JS_ToBool(ctx, val));
                    return JS_UNDEFINED;
                case Col2_CategoryBits:
                {
                    int64_t v;
                    if (JS_ToInt64(ctx, &v, val) < 0)
                        return JS_EXCEPTION;
                    col->setFilter(static_cast<uint64_t>(v), col->maskBits, col->groupIndex);
                    return JS_UNDEFINED;
                }
                case Col2_MaskBits:
                {
                    int64_t v;
                    if (JS_ToInt64(ctx, &v, val) < 0)
                        return JS_EXCEPTION;
                    col->setFilter(col->categoryBits, static_cast<uint64_t>(v), col->groupIndex);
                    return JS_UNDEFINED;
                }
                case Col2_GroupIndex:
                {
                    int32_t v;
                    if (JS_ToInt32(ctx, &v, val) < 0)
                        return JS_EXCEPTION;
                    col->setFilter(col->categoryBits, col->maskBits, v);
                    return JS_UNDEFINED;
                }
                }

                double d = 0.0;
                if (JS_ToFloat64(ctx, &d, val) < 0)
                    return JS_EXCEPTION;
                float f = static_cast<float>(d);

                switch (magic)
                {
                case Col2_Density:
                    col->setDensity(f);
                    break;
                case Col2_Friction:
                    col->setFriction(f);
                    break;
                case Col2_Restitution:
                    col->setRestitution(f);
                    break;
                case Col2_TangentSpeed:
                    col->setTangentSpeed(f);
                    break;
                }
                return JS_UNDEFINED;
            }

            // Callback setters. Each one:
            //   1) Recovers the Collider2D* from the JS handle.
            //   2) Builds a JsCallback owning a dup'd ref to the JS fn,
            //      or stores nullptr in the std::function slot when the
            //      caller passes null/undefined to clear.
            //   3) Wraps the JsCallback in a closure with the right
            //      signature for the matching jsOn* slot on the collider.
            // The closure captures by value so the JsCallback's destructor
            // fires when the std::function is replaced or the collider
            // is destroyed.

            JSValue GameObject::js_collider2d_set_on_collision_begin(JSContext *ctx, JSValueConst this_val, JSValueConst val)
            {
                auto *col = unwrap_collider2d(this_val);
                if (!col)
                    return JS_UNDEFINED;
                if (!JS_IsFunction(ctx, val))
                {
                    col->jsOnCollisionBegin = nullptr;
                    return JS_UNDEFINED;
                }
                JsCallback cb(ctx, JS_DupValue(ctx, val));
                col->jsOnCollisionBegin =
                    [cb](Bokken::GameObject::Collider2D *other, const b2Manifold &manifold)
                {
                    if (!cb.ctx || JS_IsUndefined(cb.fn))
                        return;
                    // Build the manifold info object — first contact
                    // point + normal in pixel space, mirroring the
                    // hit object shape returned by raycast queries.
                    auto &world = Bokken::Physics::World::get();
                    glm::vec2 pointPx{0.0f}, normalPx{0.0f};
                    if (manifold.pointCount > 0)
                    {
                        pointPx = world.b2ToPx(manifold.points[0].point);
                        normalPx = {manifold.normal.x, manifold.normal.y};
                    }
                    JSValue contact = JS_NewObject(cb.ctx);
                    JS_SetPropertyStr(cb.ctx, contact, "point", cbVec2(cb.ctx, pointPx.x, pointPx.y));
                    JS_SetPropertyStr(cb.ctx, contact, "normal", cbVec2(cb.ctx, normalPx.x, normalPx.y));
                    JS_SetPropertyStr(cb.ctx, contact, "pointCount", JS_NewInt32(cb.ctx, manifold.pointCount));

                    JSValue otherGo = makeOtherHandle(cb.ctx, other);
                    JSValue args[2] = {otherGo, contact};
                    JSValue ret = JS_Call(cb.ctx, cb.fn, JS_UNDEFINED, 2, args);
                    if (JS_IsException(ret))
                        JS_FreeValue(cb.ctx, JS_GetException(cb.ctx));
                    JS_FreeValue(cb.ctx, ret);
                    JS_FreeValue(cb.ctx, contact);
                    JS_FreeValue(cb.ctx, otherGo);
                };
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_collider2d_set_on_collision_end(JSContext *ctx, JSValueConst this_val, JSValueConst val)
            {
                auto *col = unwrap_collider2d(this_val);
                if (!col)
                    return JS_UNDEFINED;
                if (!JS_IsFunction(ctx, val))
                {
                    col->jsOnCollisionEnd = nullptr;
                    return JS_UNDEFINED;
                }
                JsCallback cb(ctx, JS_DupValue(ctx, val));
                col->jsOnCollisionEnd =
                    [cb](Bokken::GameObject::Collider2D *other)
                {
                    if (!cb.ctx || JS_IsUndefined(cb.fn))
                        return;
                    JSValue otherGo = makeOtherHandle(cb.ctx, other);
                    JSValue args[1] = {otherGo};
                    JSValue ret = JS_Call(cb.ctx, cb.fn, JS_UNDEFINED, 1, args);
                    if (JS_IsException(ret))
                        JS_FreeValue(cb.ctx, JS_GetException(cb.ctx));
                    JS_FreeValue(cb.ctx, ret);
                    JS_FreeValue(cb.ctx, otherGo);
                };
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_collider2d_set_on_collision_hit(JSContext *ctx, JSValueConst this_val, JSValueConst val)
            {
                auto *col = unwrap_collider2d(this_val);
                if (!col)
                    return JS_UNDEFINED;
                if (!JS_IsFunction(ctx, val))
                {
                    col->jsOnCollisionHit = nullptr;
                    return JS_UNDEFINED;
                }
                JsCallback cb(ctx, JS_DupValue(ctx, val));
                col->jsOnCollisionHit =
                    [cb](Bokken::GameObject::Collider2D *other, const b2ContactHitEvent &event)
                {
                    if (!cb.ctx || JS_IsUndefined(cb.fn))
                        return;
                    auto &world = Bokken::Physics::World::get();
                    glm::vec2 pt = world.b2ToPx(event.point);
                    glm::vec2 nrm{event.normal.x, event.normal.y};

                    JSValue hit = JS_NewObject(cb.ctx);
                    JS_SetPropertyStr(cb.ctx, hit, "point", cbVec2(cb.ctx, pt.x, pt.y));
                    JS_SetPropertyStr(cb.ctx, hit, "normal", cbVec2(cb.ctx, nrm.x, nrm.y));
                    JS_SetPropertyStr(cb.ctx, hit, "approachSpeed", JS_NewFloat64(cb.ctx, event.approachSpeed));

                    JSValue otherGo = makeOtherHandle(cb.ctx, other);
                    JSValue args[2] = {otherGo, hit};
                    JSValue ret = JS_Call(cb.ctx, cb.fn, JS_UNDEFINED, 2, args);
                    if (JS_IsException(ret))
                        JS_FreeValue(cb.ctx, JS_GetException(cb.ctx));
                    JS_FreeValue(cb.ctx, ret);
                    JS_FreeValue(cb.ctx, hit);
                    JS_FreeValue(cb.ctx, otherGo);
                };
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_collider2d_set_on_sensor_begin(JSContext *ctx, JSValueConst this_val, JSValueConst val)
            {
                auto *col = unwrap_collider2d(this_val);
                if (!col)
                    return JS_UNDEFINED;
                if (!JS_IsFunction(ctx, val))
                {
                    col->jsOnSensorBegin = nullptr;
                    return JS_UNDEFINED;
                }
                JsCallback cb(ctx, JS_DupValue(ctx, val));
                col->jsOnSensorBegin =
                    [cb](Bokken::GameObject::Collider2D *other)
                {
                    if (!cb.ctx || JS_IsUndefined(cb.fn))
                        return;
                    JSValue otherGo = makeOtherHandle(cb.ctx, other);
                    JSValue args[1] = {otherGo};
                    JSValue ret = JS_Call(cb.ctx, cb.fn, JS_UNDEFINED, 1, args);
                    if (JS_IsException(ret))
                        JS_FreeValue(cb.ctx, JS_GetException(cb.ctx));
                    JS_FreeValue(cb.ctx, ret);
                    JS_FreeValue(cb.ctx, otherGo);
                };
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_collider2d_set_on_sensor_end(JSContext *ctx, JSValueConst this_val, JSValueConst val)
            {
                auto *col = unwrap_collider2d(this_val);
                if (!col)
                    return JS_UNDEFINED;
                if (!JS_IsFunction(ctx, val))
                {
                    col->jsOnSensorEnd = nullptr;
                    return JS_UNDEFINED;
                }
                JsCallback cb(ctx, JS_DupValue(ctx, val));
                col->jsOnSensorEnd =
                    [cb](Bokken::GameObject::Collider2D *other)
                {
                    if (!cb.ctx || JS_IsUndefined(cb.fn))
                        return;
                    JSValue otherGo = makeOtherHandle(cb.ctx, other);
                    JSValue args[1] = {otherGo};
                    JSValue ret = JS_Call(cb.ctx, cb.fn, JS_UNDEFINED, 1, args);
                    if (JS_IsException(ret))
                        JS_FreeValue(cb.ctx, JS_GetException(cb.ctx));
                    JS_FreeValue(cb.ctx, ret);
                    JS_FreeValue(cb.ctx, otherGo);
                };
                return JS_UNDEFINED;
            }

            // Per-shape getters / setters / wrap functions.
            //
            // Each getter delegates to js_collider2d_get_base for magic
            // values below Col2_BaseEnd, then handles its own shape
            // fields. Setters mirror the same dispatch.

            JSValue GameObject::js_box_collider2d_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *col = static_cast<Bokken::GameObject::BoxCollider2D *>(
                    JS_GetOpaque(this_val, s_box_collider2d_class_id));
                if (!col)
                    return JS_UNDEFINED;
                if (magic < Col2_BaseEnd)
                    return js_collider2d_get_base(ctx, col, magic);
                switch (magic)
                {
                case Bx2_SizeX:
                    return JS_NewFloat64(ctx, col->size.x);
                case Bx2_SizeY:
                    return JS_NewFloat64(ctx, col->size.y);
                case Bx2_OffsetX:
                    return JS_NewFloat64(ctx, col->offset.x);
                case Bx2_OffsetY:
                    return JS_NewFloat64(ctx, col->offset.y);
                case Bx2_Angle:
                    return JS_NewFloat64(ctx, col->angle);
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_box_collider2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *col = static_cast<Bokken::GameObject::BoxCollider2D *>(
                    JS_GetOpaque(this_val, s_box_collider2d_class_id));
                if (!col)
                    return JS_UNDEFINED;
                if (magic < Col2_BaseEnd)
                    return js_collider2d_set_base(ctx, col, val, magic);

                double d = 0.0;
                if (JS_ToFloat64(ctx, &d, val) < 0)
                    return JS_EXCEPTION;
                float f = static_cast<float>(d);
                switch (magic)
                {
                case Bx2_SizeX:
                    col->size.x = f;
                    break;
                case Bx2_SizeY:
                    col->size.y = f;
                    break;
                case Bx2_OffsetX:
                    col->offset.x = f;
                    break;
                case Bx2_OffsetY:
                    col->offset.y = f;
                    break;
                case Bx2_Angle:
                    col->angle = f;
                    break;
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::wrap_box_collider2d(JSContext *ctx, Bokken::GameObject::BoxCollider2D *col)
            {
                JSValue obj = JS_NewObjectClass(ctx, s_box_collider2d_class_id);
                if (JS_IsException(obj))
                    return obj;
                JS_SetOpaque(obj, col);
                return obj;
            }

            JSValue GameObject::js_circle_collider2d_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *col = static_cast<Bokken::GameObject::CircleCollider2D *>(
                    JS_GetOpaque(this_val, s_circle_collider2d_class_id));
                if (!col)
                    return JS_UNDEFINED;
                if (magic < Col2_BaseEnd)
                    return js_collider2d_get_base(ctx, col, magic);
                switch (magic)
                {
                case Cc2_Radius:
                    return JS_NewFloat64(ctx, col->radius);
                case Cc2_OffsetX:
                    return JS_NewFloat64(ctx, col->offset.x);
                case Cc2_OffsetY:
                    return JS_NewFloat64(ctx, col->offset.y);
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_circle_collider2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *col = static_cast<Bokken::GameObject::CircleCollider2D *>(
                    JS_GetOpaque(this_val, s_circle_collider2d_class_id));
                if (!col)
                    return JS_UNDEFINED;
                if (magic < Col2_BaseEnd)
                    return js_collider2d_set_base(ctx, col, val, magic);

                double d = 0.0;
                if (JS_ToFloat64(ctx, &d, val) < 0)
                    return JS_EXCEPTION;
                float f = static_cast<float>(d);
                switch (magic)
                {
                case Cc2_Radius:
                    col->radius = f;
                    break;
                case Cc2_OffsetX:
                    col->offset.x = f;
                    break;
                case Cc2_OffsetY:
                    col->offset.y = f;
                    break;
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::wrap_circle_collider2d(JSContext *ctx, Bokken::GameObject::CircleCollider2D *col)
            {
                JSValue obj = JS_NewObjectClass(ctx, s_circle_collider2d_class_id);
                if (JS_IsException(obj))
                    return obj;
                JS_SetOpaque(obj, col);
                return obj;
            }

            JSValue GameObject::js_capsule_collider2d_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *col = static_cast<Bokken::GameObject::CapsuleCollider2D *>(
                    JS_GetOpaque(this_val, s_capsule_collider2d_class_id));
                if (!col)
                    return JS_UNDEFINED;
                if (magic < Col2_BaseEnd)
                    return js_collider2d_get_base(ctx, col, magic);
                switch (magic)
                {
                case Cp2_PointAX:
                    return JS_NewFloat64(ctx, col->pointA.x);
                case Cp2_PointAY:
                    return JS_NewFloat64(ctx, col->pointA.y);
                case Cp2_PointBX:
                    return JS_NewFloat64(ctx, col->pointB.x);
                case Cp2_PointBY:
                    return JS_NewFloat64(ctx, col->pointB.y);
                case Cp2_Radius:
                    return JS_NewFloat64(ctx, col->radius);
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_capsule_collider2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *col = static_cast<Bokken::GameObject::CapsuleCollider2D *>(
                    JS_GetOpaque(this_val, s_capsule_collider2d_class_id));
                if (!col)
                    return JS_UNDEFINED;
                if (magic < Col2_BaseEnd)
                    return js_collider2d_set_base(ctx, col, val, magic);

                double d = 0.0;
                if (JS_ToFloat64(ctx, &d, val) < 0)
                    return JS_EXCEPTION;
                float f = static_cast<float>(d);
                switch (magic)
                {
                case Cp2_PointAX:
                    col->pointA.x = f;
                    break;
                case Cp2_PointAY:
                    col->pointA.y = f;
                    break;
                case Cp2_PointBX:
                    col->pointB.x = f;
                    break;
                case Cp2_PointBY:
                    col->pointB.y = f;
                    break;
                case Cp2_Radius:
                    col->radius = f;
                    break;
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::wrap_capsule_collider2d(JSContext *ctx, Bokken::GameObject::CapsuleCollider2D *col)
            {
                JSValue obj = JS_NewObjectClass(ctx, s_capsule_collider2d_class_id);
                if (JS_IsException(obj))
                    return obj;
                JS_SetOpaque(obj, col);
                return obj;
            }

            // Polygon and chain colliders accept `points` as a JS array of
            // {x, y} objects (or [x, y] tuples). Building one polygon from
            // a list of pairs is the natural JS shape — we don't try to
            // express it as flat numeric magic properties.

            JSValue GameObject::js_polygon_collider2d_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *col = static_cast<Bokken::GameObject::PolygonCollider2D *>(
                    JS_GetOpaque(this_val, s_polygon_collider2d_class_id));
                if (!col)
                    return JS_UNDEFINED;
                if (magic < Col2_BaseEnd)
                    return js_collider2d_get_base(ctx, col, magic);
                if (magic == Pl2_Points)
                {
                    JSValue arr = JS_NewArray(ctx);
                    for (size_t i = 0; i < col->points.size(); ++i)
                        JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i),
                                             cbVec2(ctx, col->points[i].x, col->points[i].y));
                    return arr;
                }
                return JS_UNDEFINED;
            }

            // Helper to read an array of {x, y} into a glm::vec2 vector.
            // Tolerates both {x: ..., y: ...} and [x, y] entries.
            static bool readPointArray(JSContext *ctx, JSValueConst arrVal, std::vector<glm::vec2> &out)
            {
                if (!JS_IsArray(arrVal))
                    return false;
                JSValue lenV = JS_GetPropertyStr(ctx, arrVal, "length");
                int32_t len = 0;
                JS_ToInt32(ctx, &len, lenV);
                JS_FreeValue(ctx, lenV);
                if (len <= 0)
                    return false;

                out.clear();
                out.reserve(len);
                for (int32_t i = 0; i < len; ++i)
                {
                    JSValue entry = JS_GetPropertyUint32(ctx, arrVal, i);
                    glm::vec2 p{0.0f};
                    if (JS_IsArray(entry))
                    {
                        JSValue xv = JS_GetPropertyUint32(ctx, entry, 0);
                        JSValue yv = JS_GetPropertyUint32(ctx, entry, 1);
                        double xd = 0, yd = 0;
                        JS_ToFloat64(ctx, &xd, xv);
                        JS_ToFloat64(ctx, &yd, yv);
                        JS_FreeValue(ctx, xv);
                        JS_FreeValue(ctx, yv);
                        p.x = static_cast<float>(xd);
                        p.y = static_cast<float>(yd);
                    }
                    else if (JS_IsObject(entry))
                    {
                        JSValue xv = JS_GetPropertyStr(ctx, entry, "x");
                        JSValue yv = JS_GetPropertyStr(ctx, entry, "y");
                        double xd = 0, yd = 0;
                        JS_ToFloat64(ctx, &xd, xv);
                        JS_ToFloat64(ctx, &yd, yv);
                        JS_FreeValue(ctx, xv);
                        JS_FreeValue(ctx, yv);
                        p.x = static_cast<float>(xd);
                        p.y = static_cast<float>(yd);
                    }
                    JS_FreeValue(ctx, entry);
                    out.push_back(p);
                }
                return true;
            }

            JSValue GameObject::js_polygon_collider2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *col = static_cast<Bokken::GameObject::PolygonCollider2D *>(
                    JS_GetOpaque(this_val, s_polygon_collider2d_class_id));
                if (!col)
                    return JS_UNDEFINED;
                if (magic < Col2_BaseEnd)
                    return js_collider2d_set_base(ctx, col, val, magic);
                if (magic == Pl2_Points)
                {
                    readPointArray(ctx, val, col->points);
                    // Note: assigning points after onAttach has no effect
                    // on the live shape — Box2D polygons are immutable
                    // post-creation. The deferred-attach path in
                    // addComponent ensures props applied at construction
                    // time work correctly.
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::wrap_polygon_collider2d(JSContext *ctx, Bokken::GameObject::PolygonCollider2D *col)
            {
                JSValue obj = JS_NewObjectClass(ctx, s_polygon_collider2d_class_id);
                if (JS_IsException(obj))
                    return obj;
                JS_SetOpaque(obj, col);
                return obj;
            }

            JSValue GameObject::js_edge_collider2d_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *col = static_cast<Bokken::GameObject::EdgeCollider2D *>(
                    JS_GetOpaque(this_val, s_edge_collider2d_class_id));
                if (!col)
                    return JS_UNDEFINED;
                if (magic < Col2_BaseEnd)
                    return js_collider2d_get_base(ctx, col, magic);
                switch (magic)
                {
                case Eg2_PointAX:
                    return JS_NewFloat64(ctx, col->pointA.x);
                case Eg2_PointAY:
                    return JS_NewFloat64(ctx, col->pointA.y);
                case Eg2_PointBX:
                    return JS_NewFloat64(ctx, col->pointB.x);
                case Eg2_PointBY:
                    return JS_NewFloat64(ctx, col->pointB.y);
                case Eg2_OneSided:
                    return JS_NewBool(ctx, col->oneSided);
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_edge_collider2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *col = static_cast<Bokken::GameObject::EdgeCollider2D *>(
                    JS_GetOpaque(this_val, s_edge_collider2d_class_id));
                if (!col)
                    return JS_UNDEFINED;
                if (magic < Col2_BaseEnd)
                    return js_collider2d_set_base(ctx, col, val, magic);
                if (magic == Eg2_OneSided)
                {
                    col->oneSided = JS_ToBool(ctx, val);
                    return JS_UNDEFINED;
                }

                double d = 0.0;
                if (JS_ToFloat64(ctx, &d, val) < 0)
                    return JS_EXCEPTION;
                float f = static_cast<float>(d);
                switch (magic)
                {
                case Eg2_PointAX:
                    col->pointA.x = f;
                    break;
                case Eg2_PointAY:
                    col->pointA.y = f;
                    break;
                case Eg2_PointBX:
                    col->pointB.x = f;
                    break;
                case Eg2_PointBY:
                    col->pointB.y = f;
                    break;
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::wrap_edge_collider2d(JSContext *ctx, Bokken::GameObject::EdgeCollider2D *col)
            {
                JSValue obj = JS_NewObjectClass(ctx, s_edge_collider2d_class_id);
                if (JS_IsException(obj))
                    return obj;
                JS_SetOpaque(obj, col);
                return obj;
            }

            JSValue GameObject::js_chain_collider2d_get(JSContext *ctx, JSValueConst this_val, int magic)
            {
                auto *col = static_cast<Bokken::GameObject::ChainCollider2D *>(
                    JS_GetOpaque(this_val, s_chain_collider2d_class_id));
                if (!col)
                    return JS_UNDEFINED;
                if (magic < Col2_BaseEnd)
                    return js_collider2d_get_base(ctx, col, magic);
                switch (magic)
                {
                case Ch2_Points:
                {
                    JSValue arr = JS_NewArray(ctx);
                    for (size_t i = 0; i < col->points.size(); ++i)
                        JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i),
                                             cbVec2(ctx, col->points[i].x, col->points[i].y));
                    return arr;
                }
                case Ch2_Loop:
                    return JS_NewBool(ctx, col->loop);
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::js_chain_collider2d_set(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic)
            {
                auto *col = static_cast<Bokken::GameObject::ChainCollider2D *>(
                    JS_GetOpaque(this_val, s_chain_collider2d_class_id));
                if (!col)
                    return JS_UNDEFINED;
                if (magic < Col2_BaseEnd)
                    return js_collider2d_set_base(ctx, col, val, magic);
                switch (magic)
                {
                case Ch2_Points:
                    readPointArray(ctx, val, col->points);
                    break;
                case Ch2_Loop:
                    col->loop = JS_ToBool(ctx, val);
                    break;
                }
                return JS_UNDEFINED;
            }

            JSValue GameObject::wrap_chain_collider2d(JSContext *ctx, Bokken::GameObject::ChainCollider2D *col)
            {
                JSValue obj = JS_NewObjectClass(ctx, s_chain_collider2d_class_id);
                if (JS_IsException(obj))
                    return obj;
                JS_SetOpaque(obj, col);
                return obj;
            }

        }
    }
}