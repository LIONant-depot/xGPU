#version 450

#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

// Same transform as draw_vert.glsl, but declares the SAME push_constant block as
// E23_WedgeFill_frag.glsl (mat4 L2C + float Boost) - xGPU requires every stage in a pipeline to
// declare an identical push-constant layout, and draw_vert.glsl's shorter { mat4 L2C; } block
// doesn't match, so it can't be reused here (same reason E23_Pick_vert.glsl exists).

layout (location = 0) in vec3 inPos;    //[INPUT_POSITION]
layout (location = 1) in vec2 inUV;     //[INPUT_UVS]
layout (location = 2) in vec4 inColor;  //[INPUT_COLOR]

layout (std140, push_constant) uniform PushConsts
{
    mat4  L2C;
    float Boost; // unused here - present only so it lands at the offset E23_WedgeFill_frag.glsl expects
} pushConsts;

layout(location = 0) out struct { vec4 Color; vec2 UV; } Out;

void main()
{
    Out.Color.rgb   = pow(inColor.rgb, vec3(2.2f));
    Out.Color.a     = inColor.a;
    Out.UV          = inUV;
    gl_Position     = pushConsts.L2C * vec4(inPos.xyz, 1.0);
}
