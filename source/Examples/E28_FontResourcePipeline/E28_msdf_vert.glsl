#version 450

#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec2 aPos;    //[INPUT_POSITION] local em-space, pen-relative, Y-up
layout (location = 1) in vec2 aUV;     //[INPUT_UVS] normalized atlas UV

layout (push_constant) uniform PC
{
    vec2  uScale;           // em -> NDC scale
    vec2  uTranslate;       // NDC translate
    float uPixelRange;      // msdfgen distance field range, in atlas pixels
    uint  uColor;           // packed RGBA8 fill color
    uint  uOutline;         // 0/1 - draw outline using uSDF
    uint  uOutlineColor;    // packed RGBA8 outline color
    float uOutlineWidthPx;  // outline thickness, in screen pixels (already em*scale on the CPU side)
    // Fragment-only fields below - unused here, but MUST stay declared in this exact order/size so
    // this block's byte offsets keep matching msdf_push_constants (the C++ struct) and the fragment
    // shader's own longer PC block. A push_constant block's layout is purely sequential declaration
    // order within THAT block - there's no shared master layout keeping stages in sync automatically,
    // so skipping straight to uItalicShear here would silently read the wrong bytes (whatever
    // uOutputType/uFontWeightPx/uBevelWeightPx actually hold) instead of the real shear value.
    uint  uOutputType;
    float uFontWeightPx;
    float uBevelWeightPx;
    float uItalicShear;     // slope (dx per unit y) - the only new field this stage actually reads
} pc;

out gl_PerVertex { vec4 gl_Position; };
layout (location = 0) out vec2 outUV;

void main()
{
    outUV = aUV;

    // Synthetic italic - shear X proportional to Y (em-space, pen-relative) BEFORE the em->NDC
    // transform, so the slope is consistent regardless of zoom/pan. Real vertex-level skew, not a UV
    // trick, so it actually tilts the rendered shape rather than just resampling the texture at an
    // angle.
    vec2 P = aPos;
    P.x += P.y * pc.uItalicShear;

    gl_Position = vec4(P * pc.uScale + pc.uTranslate, 0.0, 1.0);
}
