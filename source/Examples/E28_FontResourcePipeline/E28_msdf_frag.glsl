#version 450
#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

// [INPUT_TEXTURE] The ONE texture this font compiled - what it holds depends on uOutputType:
//   MTSDF (0): RGB = median-reconstructible MSDF, A = true single-channel SDF (for outline).
//   SDF   (1): R alone IS the true distance - no median, no separate outline channel needed.
//   BITMAP(2): ordinary pre-rasterized glyph, A = antialiasing coverage - not a distance field.
layout (binding = 0) uniform sampler2D uAtlas;
// Vestigial second binding, always bound to the SAME texture as uAtlas by the CPU side (see
// text_renderer::Draw's own comment) - kept only so the pipeline's sampler count doesn't change;
// unused by this shader now that MTSDF's true SDF lives in uAtlas's own alpha channel instead of a
// separate texture.
layout (binding = 1) uniform sampler2D uSDF;

layout (push_constant) uniform PC
{
    vec2  uScale;
    vec2  uTranslate;
    float uPixelRange;
    uint  uColor;
    uint  uOutline;
    uint  uOutlineColor;
    float uOutlineWidthPx;
    uint  uOutputType; // mirrors xfont_rsc::output_type: 0=MTSDF, 1=SDF, 2=BITMAP
    float uFontWeightPx; // shifts the fill threshold - positive = bolder, negative = thinner
    float uBevelWeightPx; // 0 = off; else how far the pseudo-lit edge band reaches inward, in screen px
    float uGlowRadiusPx; // 0 = off; else how far the soft halo extends outward, in screen px
    float uGlowIntensity; // 0-1 opacity multiplier for the glow
} pc;

// Fixed light direction for the bevel effect - matches the reference shader's own default
// (https://www.shadertoy.com/view/ldfcDr) rather than adding a property for something this minor.
const vec2 kBevelLightDir = normalize(vec2(-1.0, -0.5));

// Fixed glow color - same "not worth a property for one shader constant" precedent as
// kBevelLightDir above and the shadow's own hardcoded translucent black on the CPU side.
const vec3 kGlowColor = vec3(0.3, 0.8, 1.0);

layout (location = 0) in  vec2 inUV;
layout (location = 0) out vec4 outFragColor;

const uint OUTPUT_TYPE_MTSDF  = 0u;
const uint OUTPUT_TYPE_SDF    = 1u;
const uint OUTPUT_TYPE_BITMAP = 2u;

float median3(float r, float g, float b)
{
    return max(min(r, g), min(max(r, g), b));
}

vec4 unpackColor(uint c)
{
    return vec4( float((c      ) & 0xFFu) / 255.0
               , float((c >> 8 ) & 0xFFu) / 255.0
               , float((c >> 16) & 0xFFu) / 255.0
               , float((c >> 24) & 0xFFu) / 255.0 );
}

// Screen-space-correct AA width, in screen pixels - the standard msdf-atlas-gen shader formula:
// converts the field's own pixel-range (measured in atlas texels at bake time) into screen pixels
// using how fast the UV is changing on screen right now (fwidth), so it stays correct under any
// scale/rotation rather than baking in a fixed on-screen glyph size assumption. MTSDF/SDF only -
// BITMAP isn't a distance field, so this formula doesn't apply to it at all.
float screenPxRange(sampler2D Atlas)
{
    vec2 unitRange     = vec2(pc.uPixelRange) / vec2(textureSize(Atlas, 0));
    vec2 screenTexSize = vec2(1.0) / fwidth(inUV);
    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

void main()
{
    vec4 texel = texture(uAtlas, inUV);

    // BITMAP: an ordinary pre-rasterized image, not a distance field - alpha IS the coverage
    // directly, no pxRange/fwidth scaling (that formula assumes resolution-independent distance
    // data; a fixed-size raster has no such thing - sharpness comes from picking the closest baked
    // size at layout time instead, see xfont_rsc_runtime.h's own FindClosestSizeGroup). No outline
    // support in this mode either - there's no distance field to compute one from.
    if (pc.uOutputType == OUTPUT_TYPE_BITMAP)
    {
        vec4 fillColor = unpackColor(pc.uColor);
        vec4 result    = vec4(fillColor.rgb, fillColor.a * texel.a);
        if (result.a < 0.02) discard;
        outFragColor = result;
        return;
    }

    // MTSDF needs the median-of-3 reconstruction to be robust to per-channel noise/compression
    // error; SDF's single channel already IS the exact distance, no reconstruction needed (and
    // median3(r,r,r) isn't safe to use as a shortcut here - the other two channels of an
    // INTENSITY-usage texture aren't guaranteed to hold anything meaningful).
    float sd = (pc.uOutputType == OUTPUT_TYPE_MTSDF) ? median3(texel.r, texel.g, texel.b) : texel.r;

    float pxRange      = screenPxRange(uAtlas);
    // uFontWeightPx shifts the threshold directly (0 by default, a no-op) - the same trick as
    // outline width below, just applied to the FILL edge instead of an outer ring: growing/shrinking
    // the "inside" region a fixed number of screen pixels is exactly what synthetic bold/thin does,
    // with no separate bold glyph data needed.
    float fillDistPx   = pxRange * (sd - 0.5) + pc.uFontWeightPx;
    float fillCoverage = clamp(fillDistPx + 0.5, 0.0, 1.0);

    // Premultiplied alpha internally, from here through the outline composite below - un-premultiplied
    // for the actual output at the very end (the pipeline's blend state expects straight alpha, see
    // there). Was straight-alpha-with-unconditional-rgb before (fillColor.rgb stored even where
    // coverage was 0), which broke the outline: "src + dst*(1-srcA)" is only a correct "over" operator
    // for PREMULTIPLIED colors - applied to straight alpha, the outline ring's black rgb got added on
    // top of the fill's white rgb weighted by a near-zero factor and vanished, so the outline came out
    // reporting the right alpha but the fill's own color (white) instead of its own (black).
    vec4  fillColor = unpackColor(pc.uColor);
    float fillAlpha = fillColor.a * fillCoverage;
    vec4  result    = vec4(fillColor.rgb * fillAlpha, fillAlpha);

    if (pc.uBevelWeightPx > 0.0)
    {
        // Screen-space gradient of the raw sd value approximates the shape's surface normal, same
        // idea as deriving a normal from a heightfield's own gradient - works for ANY msdfgen-style
        // SDF/MTSDF regardless of what its own channels encode (unlike a hack that treats one
        // specific tool's own G/B channels directly as a normal, which doesn't generalize here).
        // Gradient naturally goes to ~0 away from edges (msdfgen's stored field flattens out beyond
        // its own pixel range), so this self-confines to a soft band near the edge without needing
        // a second distance threshold to carve one out explicitly.
        vec2  Grad    = vec2(dFdx(sd), dFdy(sd));
        float GradLen = length(Grad);
        if (GradLen > 0.0001)
        {
            float NdotL = dot(Grad / GradLen, kBevelLightDir);
            float Shade = clamp(NdotL, -1.0, 1.0) * clamp(pc.uBevelWeightPx / max(pxRange, 1.0), 0.0, 1.0);
            result.rgb *= (1.0 + Shade * 0.5);
        }
    }

    if (pc.uOutline != 0u)
    {
        // The true (non-median) SDF is more numerically stable at longer range than a median-
        // reconstructed MSDF - see xfont_rsc_descriptor.h's own note on why MTSDF keeps a true SDF
        // in its alpha channel at all. SDF mode's own R channel already IS that true distance, so
        // there's nothing extra to sample - reuse sd directly.
        float trueSD         = (pc.uOutputType == OUTPUT_TYPE_MTSDF) ? texel.a : sd;
        float outlineDistPx  = pxRange * (trueSD - 0.5);
        float outlineCover   = clamp(outlineDistPx + 0.5 + pc.uOutlineWidthPx, 0.0, 1.0) - fillCoverage;

        vec4  outlineColor = unpackColor(pc.uOutlineColor);
        float outlineAlpha = outlineColor.a * clamp(outlineCover, 0.0, 1.0);
        vec4  outlineSrc   = vec4(outlineColor.rgb * outlineAlpha, outlineAlpha); // premultiplied

        // Composite fill (top) over outline (bottom) - standard premultiplied "over" operator.
        result = result + outlineSrc * (1.0 - result.a);
    }

    if (pc.uGlowRadiusPx > 0.0)
    {
        // Same trueSD reasoning as the outline block above (SDF's own R IS the true distance;
        // MTSDF keeps its true SDF in alpha since the median-reconstructed RGB isn't numerically
        // stable at longer range). distOutsidePx is 0 inside the glyph, growing positive outward -
        // the mirror image of fillCoverage's own "inside" measurement.
        float trueSD        = (pc.uOutputType == OUTPUT_TYPE_MTSDF) ? texel.a : sd;
        float distOutsidePx = max(0.0, -(pxRange * (trueSD - 0.5)));

        // Same DIRECT, linear-clamp style as fillCoverage/outlineCover above - not an exp() curve
        // with an arbitrary steepness constant that has no predictable relationship to uGlowRadiusPx
        // at all (tried twice: too tight, then not tight enough, because the "reach" the user actually
        // sees was never a clean function of the property value). fillCoverage proves this pattern
        // reads correctly right up to a hard clamp with no visible seam, PROVIDED the distance you
        // feed it never exceeds what the SDF can represent (see the cap below) - Bold/Outline never
        // hit that ceiling because their own widths stay small; Glow's whole point is to reach much
        // further, so it has to actively guard against asking for more than exists.
        //
        // The SDF texture only encodes distance out to ~pxRange screen px before saturating (an
        // 8-bit UNORM texel can't go below 0, so msdfgen's own bake-time encoding clamps there) -
        // beyond that, distOutsidePx stops growing and reads a CONSTANT rather than a genuinely
        // increasing value. Clamping uGlowRadiusPx itself to that same ceiling means the linear ramp
        // below reaches EXACTLY 0 at-or-before the point where the data would otherwise plateau -
        // clamp(), unlike exp(), actually HITS zero rather than approaching it, so there's no residual
        // sliver of constant brightness left sitting at the saturation boundary either way. This is
        // still a hard, honest ceiling tied to the font's own baked PixelRange - reaching further than
        // this needs more real distance data (a bigger PixelRange baked into the font), not another
        // shader tweak.
        float EffectiveRadiusPx = min(pc.uGlowRadiusPx, pxRange);
        float glowFalloff = clamp(1.0 - distOutsidePx / max(EffectiveRadiusPx, 0.0001), 0.0, 1.0);

        float glowAlpha = pc.uGlowIntensity * glowFalloff;
        vec4  glowSrc   = vec4(kGlowColor * glowAlpha, glowAlpha); // premultiplied

        // Composite everything so far (fill [+ bevel] [+ outline]) over the glow - it's the
        // backmost layer, showing through wherever what's on top of it isn't fully opaque yet
        // (the anti-aliased fill/outline edges themselves, and everywhere beyond the outline ring).
        result = result + glowSrc * (1.0 - result.a);
    }

    if (result.a < 0.02) discard;
    // Un-premultiply - the pipeline's blend state (getAlphaOriginal) expects straight alpha.
    outFragColor = vec4(result.a > 0.0001 ? result.rgb / result.a : result.rgb, result.a);
}
