#version 450

#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;

layout (std140, push_constant) uniform PushConsts
{
    mat4 L2C;
} pushConsts;

layout (location = 0) out vec3 outNormal;

void main()
{
    outNormal   = inNormal;
    gl_Position = pushConsts.L2C * vec4(inPos.xyz, 1.0);
}
