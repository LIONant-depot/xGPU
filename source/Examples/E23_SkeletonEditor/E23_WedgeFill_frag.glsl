#version 450
#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

// Same as draw_frag.glsl (texture*color, then gamma-decode back from the vertex shader's linear
// encoding), but with an extra multiply by pushConsts.Boost before the alpha blend, plus a
// self-shadow term (one bone's solid fill darkening another's). The PCF/bounds-check logic below is
// a straight copy of E21_GridShader_frag.glsl's ShadowPCF - small enough not to be worth sharing
// across files, but keep the two in sync if either gets a correctness fix.
//
// The wedge fill is deliberately translucent (BuildWedgeFillGeometry's FillAlphaScale) so nested/
// overlapping bones stay visible through each other - but alpha blending means the rendered color
// is Src*Alpha + Background*(1-Alpha): at Alpha ~0.55 over a mid-gray floor, over half of what
// reaches the screen is background, no matter how bright Src's own RGB is. A vertex-color-only fix
// can't undo that (colors are clamped to [0,1] going in) - this shader boosts AFTER the vertex
// stage's own math, in a place that isn't clamped until the final blend, so Src actually reaches
// the screen at closer to its intended brightness instead of reading as diluted/neutral.

layout (binding = 0)    uniform     sampler2D   uSamplerColor;    // [INPUT_TEXTURE]
layout (binding = 1)    uniform     sampler2D   SamplerShadowMap;

layout(location = 0) in struct { vec4 Color; vec2 UV; vec4 ShadowPos; } In;

layout (location = 0)   out         vec4        outFragColor;

layout (std140, push_constant) uniform PushConsts
{
    mat4  L2C;         // unused here - present only so ShadowL2C/Boost land at the offsets the C++ side expects
    mat4  ShadowL2C;   // unused here - the vertex shader already projected it into In.ShadowPos
    float Boost;
} pushConsts;

int isqr(int a) { return a * a; }

float SampleShadowTexture(in const vec4 Coord, in const vec2 off)
{
    float dist = texture(SamplerShadowMap, Coord.xy + off).r;
    return (dist < Coord.z) ? 0.0 : 1.0;
}

// See E21_GridShader_frag.glsl's own comment for why both the Z range AND the XY [0,1] bounds have
// to be checked - Z alone lets anything outside the light's actual frustum clamp to an edge texel
// and flip in/out of shadow as its own Z keeps varying.
float ShadowPCF(in const vec4 UVProjection)
{
    float Shadow = 1.0;
    if (UVProjection.z > -1.0 && UVProjection.z < 1.0
     && UVProjection.x >  0.0 && UVProjection.x <  1.0
     && UVProjection.y >  0.0 && UVProjection.y <  1.0)
    {
        const float scale = 1.5;
        const vec2  TexelSize = scale / textureSize(SamplerShadowMap, 0);
        const int   SampleRange = 1;
        const int   SampleTotal = isqr(1 + 2 * SampleRange);

        float ShadowAcc = 0;
        for (int x = -SampleRange; x <= SampleRange; x++)
            for (int y = -SampleRange; y <= SampleRange; y++)
                ShadowAcc += SampleShadowTexture(UVProjection, vec2(TexelSize.x * x, TexelSize.y * y));

        Shadow = ShadowAcc / SampleTotal;
    }
    return Shadow;
}

void main()
{
    vec4 Color = In.Color * texture( uSamplerColor, In.UV );

    // Convert back to gamma space (matches draw_frag.glsl exactly)
    Color.rgb = pow(Color.rgb, vec3(0.454545f));

    // Brighten the RGB only - boosting alpha too would just make the fill MORE opaque instead of
    // brighter, defeating the whole point of it being translucent.
    Color.rgb *= pushConsts.Boost;

    // Self-shadowing - w must be checked BEFORE the divide (see E21_GridShader_frag.glsl's main()
    // for why doing it after is a no-op). Less aggressive than the grid's own t (0.5 vs 0.3) so a
    // shadowed bone stays clearly visible/colored instead of reading as nearly black.
    const float Shadow = (In.ShadowPos.w > 0.0) ? ShadowPCF(In.ShadowPos / In.ShadowPos.w) : 1.0;
    const float t = 0.5;
    Color.rgb = (Color.rgb - Color.rgb * t) * Shadow + Color.rgb * t;

    outFragColor = Color;
}
