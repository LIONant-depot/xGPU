#version 450

#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;        //[INPUT_POSITION]

layout (push_constant) uniform _pc
{
    mat4  L2C;
} pc;


void main() 
{
    gl_Position = pc.L2C * vec4(inPos.xyz, 1.0);
}
