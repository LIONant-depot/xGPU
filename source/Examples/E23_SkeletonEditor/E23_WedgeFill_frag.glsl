#version 450
#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

// Same as draw_frag.glsl (texture*color, then gamma-decode back from the vertex shader's linear
// encoding), but with an extra multiply by pushConsts.Boost before the alpha blend.
//
// The wedge fill is deliberately translucent (BuildWedgeFillGeometry's FillAlphaScale) so nested/
// overlapping bones stay visible through each other - but alpha blending means the rendered color
// is Src*Alpha + Background*(1-Alpha): at Alpha ~0.55 over a mid-gray floor, over half of what
// reaches the screen is background, no matter how bright Src's own RGB is. A vertex-color-only fix
// can't undo that (colors are clamped to [0,1] going in) - this shader boosts AFTER the vertex
// stage's own math, in a place that isn't clamped until the final blend, so Src actually reaches
// the screen at closer to its intended brightness instead of reading as diluted/neutral.

layout (binding = 0)    uniform     sampler2D   uSamplerColor; // [INPUT_TEXTURE]

layout(location = 0) in struct { vec4 Color; vec2 UV; } In;

layout (location = 0)   out         vec4        outFragColor;

layout (std140, push_constant) uniform PushConsts
{
    mat4  L2C;    // unused here - present only so Boost lands at the offset the C++ side expects
    float Boost;
} pushConsts;

void main()
{
    vec4 Color = In.Color * texture( uSamplerColor, In.UV );

    // Convert back to gamma space (matches draw_frag.glsl exactly)
    Color.rgb = pow(Color.rgb, vec3(0.454545f));

    // Brighten the RGB only - boosting alpha too would just make the fill MORE opaque instead of
    // brighter, defeating the whole point of it being translucent.
    Color.rgb *= pushConsts.Boost;

    outFragColor = Color;
}
