#version 450

#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

// Dedicated shader pair for the glyph-bounds debug overlay (RenderSettings/Debug/ShowGlyphBounds) -
// deliberately NOT the MSDF shader with a branch: a separate pipeline keeps the production text
// shader free of debug-only logic, and means this pipeline needs no texture samplers at all. Draws
// the SAME real glyph vertex/index data as the normal fill pass (see text_renderer::Draw's own
// comment) through the pipeline's WIRELINE raster mode instead of a second vertex set - so this is
// genuinely the mesh used to render each glyph, diagonal included, not a synthetic bounding box.
// Shares text_renderer's msdf_vert vertex layout (aUV is simply unused here).
layout (location = 0) in vec2 aPos;    // local em-space, pen-relative, Y-up - same space the glyph quads use
layout (location = 1) in vec2 aUV;     // unused

layout (push_constant) uniform PC
{
    vec2  uScale;       // em -> NDC scale (same value text_renderer::Draw used for the real glyphs)
    vec2  uTranslate;   // NDC translate (same value text_renderer::Draw used for the real glyphs)
} pc;

out gl_PerVertex { vec4 gl_Position; };

void main()
{
    gl_Position = vec4(aPos * pc.uScale + pc.uTranslate, 0.0, 1.0);
}
