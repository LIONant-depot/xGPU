#version 450
//#extension GL_KHR_vulkan_glsl : require
#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 0)    uniform     sampler2D   uSamplerColor;

layout (location = 0)   in          vec4        inColor;
layout (location = 1)   in          vec2        inUV;

layout (location = 0)   out         vec4        outFragColor;

void main() 
{
    outFragColor = inColor * texture( uSamplerColor, inUV );
}


