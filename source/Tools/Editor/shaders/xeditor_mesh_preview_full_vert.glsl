#version 450

// The Material/Material Instance editor's own preview (a primitive shape, not real geometry) needs to draw
// with the material's actual compiled fragment shader. A material built on mb_standard_pbr.frag /
// mb_material_pbr.frag (the common case - e.g. "Default PBR Material") expects the full varying interface
// mb_varying_definition_full.glsl's VaryingFull declares (position/shadow-position/vertex-color/tangent/
// normal/UV at locations 0-5) plus its lighting_uniforms UBO at set 2 binding 1; this shader provides the
// first half. VaryingFull is redeclared here rather than #included - glslc is invoked with no -I flags in
// this project, so a cross-directory #include (mb_varying_definition_full.glsl lives under
// xgeom_static.plugin/source/runtime/) would not resolve. Cross-stage interface matching in SPIR-V is by
// location/component/type, not struct name, so a separately-declared but identically-shaped struct matches
// the fragment shader's own "in VaryingFull In;" exactly.

layout(location = 0) in vec3 in_Pos;
layout(location = 1) in vec2 in_UV;
layout(location = 2) in vec4 in_Color;
layout(location = 3) in vec3 in_Normal;
layout(location = 4) in vec4 in_Tangent;    // xyz = tangent, w = binormal sign

layout(push_constant) uniform Push
{
    mat4 L2W;
    mat4 W2C;
} push;

struct VaryingFull
{
    vec4 wSpacePosition;
    vec4 ShadowPosition;
    vec4 VertColor;
    vec4 Tangent;
    vec3 Normal;
    vec2 UV;
};

layout(location = 0) out VaryingFull Out;

void main()
{
    const vec4 wPos    = push.L2W * vec4(in_Pos, 1.0);
    Out.wSpacePosition = wPos;
    gl_Position        = push.W2C * wPos;

    const mat3 RotMat = mat3(push.L2W);
    Out.Normal        = normalize(RotMat * in_Normal);
    Out.Tangent.xyz   = normalize(RotMat * in_Tangent.xyz);
    Out.Tangent.w     = in_Tangent.w;
    Out.VertColor     = in_Color;
    Out.UV            = in_UV;

    // No shadow map in this preview - sample dead-center of whatever texture is bound there instead of
    // leaving it undefined (avoids a divide-by-zero in ShadowPCF(ShadowPosition / ShadowPosition.w)).
    Out.ShadowPosition = vec4(0.5, 0.5, 0.5, 1.0);
}
