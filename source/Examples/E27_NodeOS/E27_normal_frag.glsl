#version 450

#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inNormal;
layout (location = 0) out vec4 outFragColor;

// No lighting/textures - this is a live in-node mesh preview for the Node OS example, not a real
// material. Coloring by the (flat, per-face) normal is enough to read as "a real 3D cube", cheaply.
void main()
{
    outFragColor = vec4(abs(normalize(inNormal)), 1.0);
}
