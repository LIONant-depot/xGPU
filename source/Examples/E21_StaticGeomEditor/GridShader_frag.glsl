#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// Push constants uniform block containing data for both vertex and fragment shaders
layout(push_constant) uniform PushConsts
{
    // Vertex shader parameters
    mat4    L2W;                   // Local to World transformation matrix
    mat4    W2C;                   // World to Clip transformation matrix
    vec3    WorldSpaceCameraPos;   // World space position of the camera
    float   MajorGridDiv;          // Number of major grid divisions (clamped between 2 and 25)
} pushConsts;

// Fragment shader parameters
const vec4    MajorLineColor      = vec4(0.7f, 0.7f, 0.7f, 1.0f);
const vec4    MinorLineColor      = vec4(0.6f, 0.6f, 0.6f, 1.0f);   // White                    // Color for minor grid lines
const vec4    BaseColor           = vec4(0.4f, 0.4f, 0.4f, 1.0f);   // Black                    // Base background color
const vec4    XAxisColor          = vec4(1.0f, 0.0f, 0.0f, 1.0f);   // Red                      // Color for the X-axis line
const vec4    XAxisDashColor      = vec4(0.5f, 0.0f, 0.0f, 1.0f);   // Dark red                 // Color for the dashed part of the X-axis
const vec4    YAxisColor          = vec4(0.0f, 1.0f, 0.0f, 1.0f);   // Green                    // Color for the Y-axis line
const vec4    YAxisDashColor      = vec4(0.0f, 0.5f, 0.0f, 1.0f);   // Dark green               // Color for the dashed part of the Y-axis
const vec4    ZAxisColor          = vec4(0.0f, 0.0f, 1.0f, 1.0f);   // Blue                     // Color for the Z-axis line
const vec4    ZAxisDashColor      = vec4(0.0f, 0.0f, 0.5f, 1.0f);   // Dark blue                // Color for the dashed part of the Z-axis
const vec4    CenterColor         = vec4(1.0f, 1.0f, 1.0f, 1.0f);   // White                    // Color for the axis center (origin)
const float   AxisLineWidth      = 0.10f;                           // Slightly thicker axes    // Width of the axis lines (range 0-1)
const float   MajorLineWidth     = 0.08f;                           // Medium major lines       // Width of the major grid lines (range 0-1)
const float   MinorLineWidth     = 0.02f;                           // Thin minor lines         // Width of the minor grid lines (range 0-1)
const float   AxisDashScale      = 1.33f;                           // Balanced dash pattern    // Scale factor for axis dashing

// Preprocessor defines to select plane axis components based on keyword enum (_AXIS_X, _AXIS_Y default, _AXIS_Z)
#if defined(AXIS_YZ)
    #define AXIS_COMPONENTS yz     // For X-plane: use YZ components
#elif defined(AXIS_XY)
    #define AXIS_COMPONENTS xy     // For Z-plane: use XY components
#else // AXIS_XZ
    #define AXIS_COMPONENTS xz     // For Y-plane (default): use XZ components
#endif

// Input from vertex shader: color and UV coordinates (UV.xy for grid, UV.zw for world axis)
layout(location = 0) in struct 
{ 
    vec4 Color; 
    vec4 UV; 
} In;

// Output: final fragment color
layout(location = 0) out vec4 OutColor;

void main() 
{
    // Compute partial derivatives of UV for anti-aliasing and line width adjustment
    vec4    uvDDXY          = vec4(dFdx(In.UV.xy), dFdy(In.UV.xy));         // Derivatives in X and Y directions
    vec2    uvDeriv         = vec2(length(uvDDXY.xz), length(uvDDXY.yw));   // Magnitude of derivatives for each axis

    // Calculate effective axis line width, ensuring it's at least as wide as major lines
    float   axisLineWidth   = max(MajorLineWidth, AxisLineWidth);

    // Determine draw width for axis lines, clamped to pixel size for anti-aliasing
    vec2    axisDrawWidth   = max(vec2(axisLineWidth), uvDeriv);

    // Anti-aliasing range for axis lines
    vec2    axisLineAA      = uvDeriv * 1.5;

    // Compute smoothstep for axis lines (doubled for symmetry around zero)
    vec2    axisLines2      = smoothstep(axisDrawWidth + axisLineAA, axisDrawWidth - axisLineAA, abs(In.UV.zw * 2.0));

    // Scale axis lines intensity based on line width relative to draw width
    axisLines2 *= clamp(axisLineWidth / axisDrawWidth, 0.0, 1.0);

    // Clamp major grid divisions to nearest integer >= 2
    float   div             = max(2.0, floor(pushConsts.MajorGridDiv + 0.5));

    // Derivatives scaled for major grid
    vec2    majorUVDeriv    = uvDeriv / div;

    // Effective major line width scaled by divisions
    float   majorLineWidth  = MajorLineWidth / div;

    // Draw width for major lines, clamped between derivative and 0.5
    vec2    majorDrawWidth  = clamp(vec2(majorLineWidth), majorUVDeriv, vec2(0.5));

    // Anti-aliasing for major lines
    vec2    majorLineAA     = majorUVDeriv * 1.5;

    // Compute major grid UV (sawtooth wave for lines)
    vec2    majorGridUV     = 1.0 - abs(fract(In.UV.xy / div) * 2.0 - 1.0);

    // Offset to skip center axis for major lines
    vec2    majorAxisOffset = (1.0 - clamp(abs(In.UV.zw / div * 2.0), 0.0, 1.0)) * 2.0;

    // Apply offset to major grid UV
    majorGridUV            += majorAxisOffset;

    // Smoothstep for major grid lines
    vec2 majorGrid2         = smoothstep(majorDrawWidth + majorLineAA, majorDrawWidth - majorLineAA, majorGridUV);

    // Scale major grid intensity
    majorGrid2             *= clamp(majorLineWidth / majorDrawWidth.x, 0.0, 1.0);

    // Subtract axis lines to avoid overlap (hack)
    majorGrid2              = clamp(majorGrid2 - axisLines2, 0.0, 1.0);

    // Fade major lines when derivatives are large (far away)
    majorGrid2              = mix(majorGrid2, vec2(majorLineWidth), clamp(majorUVDeriv * 2.0 - 1.0, 0.0, 1.0));

    // Effective minor line width, clamped to major width
    float   minorLineWidth  = min(MinorLineWidth, MajorLineWidth);

    // Determine if minor lines should be inverted (thick lines)
    bool    minorInvertLine = minorLineWidth > 0.5;

    // Target width for minor lines (inverted if thick)
    float   minorTargetWidth= minorInvertLine ? 1.0 - minorLineWidth : minorLineWidth;

    // Draw width for minor lines
    vec2    minorDrawWidth  = clamp(vec2(minorTargetWidth), uvDeriv, vec2(0.5));

    // Anti-aliasing for minor lines
    vec2    minorLineAA     = uvDeriv * 1.5;

    // Compute minor grid UV (sawtooth)
    vec2    minorGridUV     = abs(fract(In.UV.xy) * 2.0 - 1.0);

    // Invert minor UV if thick lines
    minorGridUV             = minorInvertLine ? minorGridUV : 1.0 - minorGridUV;

    // Offset to skip major lines for minor grid
    vec2 minorMajorOffset   = (1.0 - clamp((1.0 - abs(fract(In.UV.zw / div) * 2.0 - 1.0)) * div, 0.0, 1.0)) * 2.0;

    // Apply offset
    minorGridUV            += minorMajorOffset;

    // Smoothstep for minor grid
    vec2 minorGrid2         = smoothstep(minorDrawWidth + minorLineAA, minorDrawWidth - minorLineAA, minorGridUV);

    // Scale minor grid intensity
    minorGrid2             *= clamp(minorTargetWidth / minorDrawWidth.x, 0.0, 1.0);

    // Subtract axis lines (hack)
    minorGrid2              = clamp(minorGrid2 - axisLines2, 0.0, 1.0);

    // Fade minor lines when far
    minorGrid2              = mix(minorGrid2, vec2(minorTargetWidth), clamp(uvDeriv * 2.0 - 1.0, 0.0, 1.0));

    // Re-invert if thick lines
    minorGrid2              = minorInvertLine ? 1.0 - minorGrid2 : minorGrid2;

    // Hide minor lines near origin (within 0.5 units)
    minorGrid2              = mix(vec2(0.0), minorGrid2, vec2(greaterThan(abs(In.UV.zw), vec2(0.5))));

    // Combine minor grid axes (lerp between x and y components)
    float   minorGrid       = mix(minorGrid2.x, 1.0, minorGrid2.y);

    // Combine major grid axes
    float   majorGrid       = mix(majorGrid2.x, 1.0, majorGrid2.y);

    // Compute UV for axis dashes (sawtooth with offset)
    vec2    axisDashUV      = abs(fract((In.UV.zw + axisLineWidth * 0.5) * AxisDashScale) * 2.0 - 1.0) - 0.5;

    // Derivatives for dash anti-aliasing
    vec2    axisDashDeriv   = uvDeriv * AxisDashScale * 1.5;

    // Smoothstep for dashes
    vec2    axisDash        = smoothstep(-axisDashDeriv, axisDashDeriv, axisDashUV);

    // Solid line for positive side, dashed for negative
    axisDash                = mix(vec2(1.0), axisDash, vec2(lessThan(In.UV.zw, vec2(0.0))));

// Gamma correction for colors if in gamma color space
#if defined(COLORSPACE_GAMMA)
    // Convert to linear
    vec4    xAxisColor      = vec4(pow(XAxisColor.rgb,       vec3(2.2)), XAxisColor.a);  
    vec4    yAxisColor      = vec4(pow(YAxisColor.rgb,       vec3(2.2)), YAxisColor.a);
    vec4    zAxisColor      = vec4(pow(ZAxisColor.rgb,       vec3(2.2)), ZAxisColor.a);
    vec4    xAxisDashColor  = vec4(pow(XAxisDashColor.rgb,   vec3(2.2)), XAxisDashColor.a);
    vec4    yAxisDashColor  = vec4(pow(YAxisDashColor.rgb,   vec3(2.2)), YAxisDashColor.a);
    vec4    zAxisDashColor  = vec4(pow(ZAxisDashColor.rgb,   vec3(2.2)), ZAxisDashColor.a);
    vec4    centerColor     = vec4(pow(CenterColor.rgb,      vec3(2.2)), CenterColor.a);
    vec4    majorLineColor  = vec4(pow(MajorLineColor.rgb,   vec3(2.2)), MajorLineColor.a);
    vec4    minorLineColor  = vec4(pow(MinorLineColor.rgb,   vec3(2.2)), MinorLineColor.a);
    vec4    baseColor       = vec4(pow(BaseColor.rgb,        vec3(2.2)), BaseColor.a);
#else
    // Use colors as-is in linear space
    vec4    xAxisColor      = XAxisColor;
    vec4    yAxisColor      = YAxisColor;
    vec4    zAxisColor      = ZAxisColor;
    vec4    xAxisDashColor  = XAxisDashColor;
    vec4    yAxisDashColor  = YAxisDashColor;
    vec4    zAxisDashColor  = ZAxisDashColor;
    vec4    centerColor     = CenterColor;
    vec4    majorLineColor  = MajorLineColor;
    vec4    minorLineColor  = MinorLineColor;
    vec4    baseColor       = BaseColor;
#endif

// Select axis colors based on plane define
#if defined(AXIS_YZ)
    vec4    aAxisColor      = yAxisColor;       // A axis: Y
    vec4    bAxisColor      = zAxisColor;       // B axis: Z
    vec4    aAxisDashColor  = yAxisDashColor;
    vec4    bAxisDashColor  = zAxisDashColor;
#elif defined(AXIS_XY)
    vec4    aAxisColor      = xAxisColor;       // A axis: X
    vec4    bAxisColor      = yAxisColor;       // B axis: Y
    vec4    aAxisDashColor  = xAxisDashColor;
    vec4    bAxisDashColor  = yAxisDashColor;
#else   // define (AXIS_XZ)
    vec4    aAxisColor      = xAxisColor;       // A axis: X
    vec4    bAxisColor      = zAxisColor;       // B axis: Z
    vec4    aAxisDashColor  = xAxisDashColor;
    vec4    bAxisDashColor  = zAxisDashColor;
#endif

    // Apply dashing to A axis color
    aAxisColor              = mix(aAxisDashColor, aAxisColor, axisDash.y);

    // Apply dashing to B axis color
    bAxisColor              = mix(bAxisDashColor, bAxisColor, axisDash.x);

    // Apply center color to A axis at origin
    aAxisColor              = mix(aAxisColor, centerColor, axisLines2.y);

    // Combine axis lines: overlay A over B with intensity
    vec4    axisLines       = mix(bAxisColor * axisLines2.y, aAxisColor, axisLines2.x);

    // Start with base color, layer minor grid
    vec4    col             = mix(baseColor, minorLineColor, minorGrid * minorLineColor.a);

    // Layer major grid over minor
    col                     = mix(col, majorLineColor, majorGrid * majorLineColor.a);

    // Overlay axis lines with alpha blending
    col                     = col * (1.0 - axisLines.a) + axisLines;

// Convert back to gamma if needed
#if defined(COLORSPACE_GAMMA)
    col                     = vec4(pow(col.rgb, vec3(1.0 / 2.2)), col.a);
#endif

    // Multiply by input color (e.g., vertex color or tint)
    OutColor = col * In.Color;
    //OutColor.w = 0.75f;
}
