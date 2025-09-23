#version 450

#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;        //[INPUT_POSITION]
layout (location = 1) in vec2 inUV;         //[INPUT_UVS]
layout (location = 2) in vec3 inNormal;     //[INPUT_NORMAL]

layout (set = 1, binding = 0) uniform _u
{
    mat4  L2C;
    mat4  ShadowmapL2C;
    vec3  LocalSpaceLightPos;
} u;

layout(location = 0) out struct 
{ 
    vec4  ShadowmapPos;
    vec4  VertColor;
    vec2  UV; 
} o;

void main() 
{
    const vec3  LightColor            = vec3(1);

    o.UV                = inUV;
    o.VertColor.rgb     = LightColor * max( 0, dot( inNormal.xyz, normalize(u.LocalSpaceLightPos - inPos).xyz ));
    o.VertColor.a       = 1;
    o.ShadowmapPos      = u.ShadowmapL2C * vec4(inPos.xyz, 1.0);
    gl_Position         = u.L2C * vec4(inPos.xyz, 1.0);
}
