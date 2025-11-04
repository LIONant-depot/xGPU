#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(location = 0) in vec3 inPos;     //[INPUT_POSITION]
layout(location = 1) in vec2 inUV;      //[INPUT_UVS]
layout(location = 2) in vec4 inColor;   //[INPUT_COLOR]

layout(std140, push_constant) uniform PushConsts
{
    // Vertex shader
    mat4    L2W;
    mat4    W2C;
    vec3    WorldSpaceCameraPos;
    float   MajorGridDiv;

} pushConsts;

#if defined(_AXIS_X)
    #define AXIS_COMPONENTS yz
#elif defined(_AXIS_Z)
    #define AXIS_COMPONENTS xy
#else
    #define AXIS_COMPONENTS xz
#endif

layout(location = 0) out struct 
{ 
    vec4 Color; 
    vec4 UV; 
} Out;

void main() 
{
    gl_Position = pushConsts.W2C * (pushConsts.L2W * vec4(inPos, 1.0));
    float   div                     = max(2.0, floor(pushConsts.MajorGridDiv + 0.5));
    vec3    worldPos                = (pushConsts.L2W * vec4(inPos.xyz, 1.0)).xyz;
    vec3    cameraCenteringOffset   = floor(pushConsts.WorldSpaceCameraPos / div) * div;

    Out.UV.yx = (worldPos - cameraCenteringOffset).AXIS_COMPONENTS;
    Out.UV.wz = worldPos.AXIS_COMPONENTS;
    Out.Color = inColor;
}