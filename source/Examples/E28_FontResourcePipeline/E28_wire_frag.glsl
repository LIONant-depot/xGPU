#version 450

#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

// See E28_wire_vert.glsl's own comment - dedicated pipeline for the glyph-bounds debug overlay,
// kept separate from the real MSDF/SDF/BITMAP fragment shader. Fixed opaque red - a debug overlay
// doesn't need to be configurable, and red reads clearly against white/gray glyph fill either way.
layout (location = 0) out vec4 outFragColor;

void main()
{
    outFragColor = vec4(1.0, 0.0, 0.0, 1.0);
}
