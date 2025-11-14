// Fragment Shader
#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_ARB_gpu_shader_fp64 : enable

// Constants
const float GridScale       = 1.0;
const float GridBias        = 1.0;
const vec4  BaseColor       = vec4(0.61, 0.63, 0.7, 1.0);
const vec4  LineColor       = vec4(0.31, 0.33, 0.4, 1.0);
const float LineWidth       = 1.00;
const float MajorLineWidth  = 2.5;
const float fade_power      = 0.7f;

const float ArrowSpacing = 10.0;
const float ArrowStemLength = 4.0;
const float ArrowHeadLength = 1.0;
const float ArrowStemWidth = 0.5;
const float ArrowHeadWidth = 1.5;
const vec4 XAxisColor = vec4(1.0, 0.0, 0.0, 1.0);
const vec4 ZAxisColor = vec4(0.0, 0.0, 1.0, 1.0);

layout(binding = 0) uniform sampler2D SamplerShadowMap;        // [INPUT_TEXTURE_SHADOW]

// Push constants
layout(push_constant) uniform PushConsts {
    mat4 L2W;
    mat4 W2C;
    mat4 ShadowL2C;
    vec3 WorldSpaceCameraPos;
    float MajorGridDiv;
} pushConsts;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inCameraPos;
layout(location = 2) in vec4 inShadowPos;

layout(location = 0) out vec4 out_Color;

// SDF functions from Inigo Quilez
float sdOrientedBox(in vec2 p, in vec2 a, in vec2 b, float th) {
    float l = length(b - a);
    vec2 d = (b - a) / l;
    vec2 q = p - (a + b) * 0.5;
    q = mat2(d.x, -d.y, d.y, d.x) * q;
    q = abs(q) - vec2(l, th) * 0.5;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}

float sdTriangleIsosceles(in vec2 p, in vec2 q) {
    p.x = abs(p.x);
    vec2 a = p - q * clamp(dot(p, q) / dot(q, q), 0.0, 1.0);
    vec2 b = p - q * vec2(clamp(p.x / q.x, 0.0, 1.0), 1.0);
    float s = -sign(q.y);
    vec2 d = min(vec2(dot(a, a), s * (p.x * q.y - p.y * q.x)),
        vec2(dot(b, b), s * (p.y - q.y)));
    return -sqrt(d.x) * sign(d.y);
}

float sdArrow(in vec2 p) {
    // Stem
    float d1 = sdOrientedBox(p, vec2(ArrowStemLength, 0.0), vec2(0.0, 0.0), ArrowStemWidth);

    // Head (rotated to point +x)
    mat2 rot = mat2(0.0, 1.0, -1.0, 0.0); // 90 deg cw for p, to rotate shape ccw
    vec2 pr = rot * p;
    float d2 = sdTriangleIsosceles(pr + vec2(0, ArrowHeadLength), vec2(ArrowHeadWidth / 2.0, ArrowHeadLength));

    return min(d1, d2);
}

vec4 Grid() 
{
    // Compute projection properties
    vec4 projRow3 = vec4(pushConsts.W2C[0][3], pushConsts.W2C[1][3], pushConsts.W2C[2][3], pushConsts.W2C[3][3]);
    float row3xyzLen = length(projRow3.xyz);
    bool isOrtho = (row3xyzLen < 0.0001) && (abs(projRow3.w - 1.0) < 0.0001);

    vec3 projRow0xyz = vec3(pushConsts.W2C[0][0], pushConsts.W2C[1][0], pushConsts.W2C[2][0]);
    float px = length(projRow0xyz);
    if (px < 0.0001) px = 0.0001;
    vec3 rotRow0 = projRow0xyz / px;

    vec3 projRow1xyz = vec3(pushConsts.W2C[0][1], pushConsts.W2C[1][1], pushConsts.W2C[2][1]);
    float py = length(projRow1xyz);
    if (py < 0.0001) py = 0.0001;
    vec3 rotRow1 = projRow1xyz / py;

    vec3 projRow2xyz = vec3(pushConsts.W2C[0][2], pushConsts.W2C[1][2], pushConsts.W2C[2][2]);
    float pzLen = length(projRow2xyz);
    if (pzLen < 0.0001) pzLen = 0.0001;

    vec3 tempRotRow2 = projRow2xyz / pzLen;
    vec3 crossProd = cross(rotRow1, tempRotRow2);
    float det = dot(rotRow0, crossProd);

    float pzSign = (det < 0.0) ? 1.0 : -1.0;
    float pz = pzSign * pzLen;
    vec3 rotRow2 = projRow2xyz / pz;

    // Compute viewPos
    vec3 viewPos;
    if (isOrtho) {
        viewPos = vec3(0.0, 0.0, 1.0);
    }
    else {
        viewPos = inCameraPos * py;
    }

    // Compute diff = transpose(rot) * viewPos
    vec3 diff = vec3(
        dot(rotRow0, viewPos),
        dot(rotRow1, viewPos),
        dot(rotRow2, viewPos)
    );

    float diffLen = max(length(diff), 0.0001);
    float cos_theta = abs(diff.y / diffLen);

    // Grid computation
    float gridDiv = max(round(pushConsts.MajorGridDiv), 2.0);

    float logLength = (0.5 * log(dot(inCameraPos, inCameraPos))) / log(gridDiv) - GridBias;

    float logA = floor(logLength);
    float logB = logA + 1.0;
    float blendFactor = fract(logLength);

    float uvScaleA = pow(gridDiv, logA);
    float uvScaleB = pow(gridDiv, logB);

    vec2 UVA = inUV / uvScaleA;
    vec2 UVB = inUV / uvScaleB;

    float logC = logA + 2.0;
    float uvScaleC = pow(gridDiv, logC);
    vec2 UVC = inUV / uvScaleC;

    vec2 gridUVA = 1.0 - abs(fract(UVA) * 2.0 - 1.0);
    vec2 gridUVB = 1.0 - abs(fract(UVB) * 2.0 - 1.0);
    vec2 gridUVC = 1.0 - abs(fract(UVC) * 2.0 - 1.0);

    float lineWidthA = LineWidth * (1.0 - blendFactor);
    float lineWidthB = mix(MajorLineWidth, LineWidth, blendFactor);
    float lineWidthC = MajorLineWidth * blendFactor;

    float lineFadeA = clamp(lineWidthA, 0.0, 1.0);
    float lineFadeB = clamp(lineWidthB, 0.0, 1.0);
    float lineFadeC = clamp(lineWidthC, 0.0, 1.0);

    vec2 ddxUV = dFdx(inUV);
    vec2 ddyUV = dFdy(inUV);
    float lenX = length(vec2(ddxUV.x, ddyUV.x));
    float lenY = length(vec2(ddxUV.y, ddyUV.y));

    float lineAA_xA = lenX / uvScaleA;
    float lineAA_yA = lenY / uvScaleA;
    float lineAA_xB = lenX / uvScaleB;
    float lineAA_yB = lenY / uvScaleB;
    float lineAA_xC = lenX / uvScaleC;
    float lineAA_yC = lenY / uvScaleC;

    float grid2A_x = 1.0 - smoothstep((lineWidthA - 1.5) * lineAA_xA, (lineWidthA + 1.5) * lineAA_xA, gridUVA.x);
    float grid2A_y = 1.0 - smoothstep((lineWidthA - 1.5) * lineAA_yA, (lineWidthA + 1.5) * lineAA_yA, gridUVA.y);

    float grid2B_x = 1.0 - smoothstep((lineWidthB - 1.5) * lineAA_xB, (lineWidthB + 1.5) * lineAA_xB, gridUVB.x);
    float grid2B_y = 1.0 - smoothstep((lineWidthB - 1.5) * lineAA_yB, (lineWidthB + 1.5) * lineAA_yB, gridUVB.y);

    float grid2C_x = 1.0 - smoothstep((lineWidthC - 1.5) * lineAA_xC, (lineWidthC + 1.5) * lineAA_xC, gridUVC.x);
    float grid2C_y = 1.0 - smoothstep((lineWidthC - 1.5) * lineAA_yC, (lineWidthC + 1.5) * lineAA_yC, gridUVC.y);

    float gridA = clamp(grid2A_x + grid2A_y, 0.0, 1.0) * lineFadeA;
    float gridB = clamp(grid2B_x + grid2B_y, 0.0, 1.0) * lineFadeB;
    float gridC = clamp(grid2C_x + grid2C_y, 0.0, 1.0) * lineFadeC;

    float grid = clamp(gridA + max(gridB, gridC), 0.0, 1.0);

    // Axis lines
    float lowX = (lineWidthB - 1.5) * lenX;
    float highX = (lineWidthB + 1.5) * lenX;
    float distX = abs(inUV.x);
    float axis_z_alpha = 1.0 - smoothstep(lowX, highX, distX); // z-axis at x=0

    float lowY = (lineWidthB - 1.5) * lenY;
    float highY = (lineWidthB + 1.5) * lenY;
    float distY = abs(inUV.y);
    float axis_x_alpha = 1.0 - smoothstep(lowY, highY, distY); // x-axis at y=0

    // Arrows
    float arrow_x = 0.0;
    float arrow_z = 0.0;

    float aa = 0.5 * (lenX + lenY);

    // X axis arrows and lines
    mat2 rot_neg_x = mat2(-1.0, 0.0, 0.0, -1.0); // 180°
    float cell_x = round(inUV.x / ArrowSpacing);
    if (cell_x >= 0.0)
    {
        vec2 arrow_center = vec2(cell_x * ArrowSpacing, 0.0);
        vec2 local_p = inUV - arrow_center - vec2(ArrowStemLength, 0.0);
        float d = sdArrow(rot_neg_x * local_p);
        arrow_x = max(arrow_x, smoothstep(-aa, aa, -d));
    }
    if (cell_x <= 0.0)
    {
        vec2 arrow_center = vec2(cell_x * ArrowSpacing, 0.0);
        vec2 local_p = inUV - arrow_center;
        float d = sdOrientedBox(local_p, vec2(0.0, 0.0), vec2(-ArrowStemLength, 0.0), ArrowStemWidth);
        arrow_x = max(arrow_x, smoothstep(-aa, aa, -d));
    }

    // Z axis arrows and lines
    mat2 rot_y = mat2(0.0, 1.0, -1.0, 0.0); // cw 90
    float cell_y = round(inUV.y / ArrowSpacing);
    if (cell_y >= 0.0)
    {
        vec2 arrow_center = vec2(0.0, cell_y * ArrowSpacing);
        vec2 local_p = inUV - arrow_center - vec2(0.0, ArrowStemLength);
        float d = sdArrow(rot_y * local_p);
        arrow_z = max(arrow_z, smoothstep(-aa, aa, -d));
    }
    if (cell_y <= 0.0)
    {
        vec2 arrow_center = vec2(0.0, cell_y * ArrowSpacing) - vec2(0.0, ArrowStemLength);;
        vec2 local_p = inUV - arrow_center;
        float d = sdOrientedBox(rot_y * local_p, vec2(0.0, 0.0), vec2(-ArrowStemLength, 0.0), ArrowStemWidth);
        arrow_z = max(arrow_z, smoothstep(-aa, aa, -d));
    }

    // Combine
    cos_theta = pow(cos_theta, fade_power);
    vec4 oColor = vec4(0);
    oColor = mix(BaseColor, LineColor, grid * cos_theta * LineColor.a);
    oColor = mix(oColor, XAxisColor, axis_x_alpha * cos_theta * XAxisColor.a);
    oColor = mix(oColor, ZAxisColor, axis_z_alpha * cos_theta * ZAxisColor.a);
    oColor = mix(oColor, XAxisColor, arrow_x * cos_theta * XAxisColor.a);
    oColor = mix(oColor, ZAxisColor, arrow_z * cos_theta * ZAxisColor.a);

    return oColor;
}

//----------------------------------------------------------------------------------------

int isqr(int a) { return a * a; }

//----------------------------------------------------------------------------------------

float SampleShadowTexture(in const vec4 Coord, in const vec2 off)
{
    float dist = texture(SamplerShadowMap, Coord.xy + off).r;
    return (Coord.w > 0.0 && dist < Coord.z) ? 0.0f : 1.0f;
}

//----------------------------------------------------------------------------------------

float ShadowPCF(in const vec4 UVProjection)
{
    float Shadow = 1.0;
    if (UVProjection.z > -1.0 && UVProjection.z < 1.0)
    {
        const float scale = 1.5;
        const vec2  TexelSize = scale / textureSize(SamplerShadowMap, 0);
        const int	SampleRange = 1;
        const int	SampleTotal = isqr(1 + 2 * SampleRange);

        float		ShadowAcc = 0;
        for (int x = -SampleRange; x <= SampleRange; x++)
        {
            for (int y = -SampleRange; y <= SampleRange; y++)
            {
                ShadowAcc += SampleShadowTexture(UVProjection, vec2(TexelSize.x * x, TexelSize.y * y));
            }
        }

        Shadow = ShadowAcc / SampleTotal;
    }

    return Shadow;
}

void main()
{
    const vec4  DiffuseColor    = Grid();
    const float Shadow          = ShadowPCF(inShadowPos / inShadowPos.w);
    const vec3  FinalColor      = (DiffuseColor.xyz - DiffuseColor.xyz * 0.5) * Shadow + DiffuseColor.xyz * 0.5;


    out_Color = vec4(FinalColor.xyz, 1.0f);
}



/*
// Fragment Shader
#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_ARB_gpu_shader_fp64 : enable

// Constants
const float GridScale       = 1.0;
const float GridBias        = 0.8;
const vec4  BaseColor       = vec4(0.5, 0.5, 0.5, 1.0);
const vec4  LineColor       = vec4(0.3, 0.3, 0.3, 1.0);
const float LineWidth       = 2.00;
const float MajorLineWidth  = 4.0;
const float fade_power      = 0.5f;

const float ArrowSpacing    = 10.0;
const float ArrowStemLength = 4.0;
const float ArrowHeadLength = 2.0;
const float ArrowStemWidth  = 0.5;
const float ArrowHeadWidth  = 1.5;
const vec4 XAxisColor       = vec4(1.0, 0.0, 0.0, 1.0);
const vec4 ZAxisColor       = vec4(0.0, 0.0, 1.0, 1.0);

// Push constants
layout(push_constant) uniform PushConsts {
    mat4 L2W;
    mat4 W2C;
    vec3 WorldSpaceCameraPos;
    float MajorGridDiv;
} pushConsts;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inCameraPos;
layout(location = 0) out vec4 outColor;

// SDF functions from Inigo Quilez
float sdOrientedBox(in vec2 p, in vec2 a, in vec2 b, float th) {
    float l = length(b - a);
    vec2 d = (b - a) / l;
    vec2 q = p - (a + b) * 0.5;
    q = mat2(d.x, -d.y, d.y, d.x) * q;
    q = abs(q) - vec2(l, th) * 0.5;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}

float sdTriangleIsosceles(in vec2 p, in vec2 q) {
    p.x = abs(p.x);
    vec2 a = p - q * clamp(dot(p, q) / dot(q, q), 0.0, 1.0);
    vec2 b = p - q * vec2(clamp(p.x / q.x, 0.0, 1.0), 1.0);
    float s = -sign(q.y);
    vec2 d = min(vec2(dot(a, a), s * (p.x * q.y - p.y * q.x)),
        vec2(dot(b, b), s * (p.y - q.y)));
    return -sqrt(d.x) * sign(d.y);
}

float sdArrow(in vec2 p) {
    // Stem
    float d1 = sdOrientedBox(p, vec2(ArrowStemLength, 0.0), vec2(0.0, 0.0), ArrowStemWidth);

    // Head (rotated to point +x)
    mat2 rot = mat2(0.0, 1.0, -1.0, 0.0); // 90 deg cw for p, to rotate shape ccw
    vec2 pr = rot * p;
    float d2 = sdTriangleIsosceles(pr + vec2(0,ArrowHeadLength), vec2(ArrowHeadWidth / 2.0, ArrowHeadLength));

    return min(d1, d2);
}

void main() {
    // Compute projection properties
    vec4 projRow3 = vec4(pushConsts.W2C[0][3], pushConsts.W2C[1][3], pushConsts.W2C[2][3], pushConsts.W2C[3][3]);
    float row3xyzLen = length(projRow3.xyz);
    bool isOrtho = (row3xyzLen < 0.0001) && (abs(projRow3.w - 1.0) < 0.0001);

    vec3 projRow0xyz = vec3(pushConsts.W2C[0][0], pushConsts.W2C[1][0], pushConsts.W2C[2][0]);
    float px = length(projRow0xyz);
    if (px < 0.0001) px = 0.0001;
    vec3 rotRow0 = projRow0xyz / px;

    vec3 projRow1xyz = vec3(pushConsts.W2C[0][1], pushConsts.W2C[1][1], pushConsts.W2C[2][1]);
    float py = length(projRow1xyz);
    if (py < 0.0001) py = 0.0001;
    vec3 rotRow1 = projRow1xyz / py;

    vec3 projRow2xyz = vec3(pushConsts.W2C[0][2], pushConsts.W2C[1][2], pushConsts.W2C[2][2]);
    float pzLen = length(projRow2xyz);
    if (pzLen < 0.0001) pzLen = 0.0001;

    vec3 tempRotRow2 = projRow2xyz / pzLen;
    vec3 crossProd = cross(rotRow1, tempRotRow2);
    float det = dot(rotRow0, crossProd);

    float pzSign = (det < 0.0) ? 1.0 : -1.0;
    float pz = pzSign * pzLen;
    vec3 rotRow2 = projRow2xyz / pz;

    // Compute viewPos
    vec3 viewPos;
    if (isOrtho) {
        viewPos = vec3(0.0, 0.0, 1.0);
    }
    else {
        viewPos = inCameraPos * py;
    }

    // Compute diff = transpose(rot) * viewPos
    vec3 diff = vec3(
        dot(rotRow0, viewPos),
        dot(rotRow1, viewPos),
        dot(rotRow2, viewPos)
    );

    float diffLen = max(length(diff), 0.0001);
    float cos_theta = abs(diff.y / diffLen);

    // Grid computation
    float gridDiv = max(round(pushConsts.MajorGridDiv), 2.0);

    float logLength = (0.5 * log(dot(inCameraPos, inCameraPos))) / log(gridDiv) - GridBias;

    float logA = floor(logLength);
    float logB = logA + 1.0;
    float blendFactor = fract(logLength);

    float uvScaleA = pow(gridDiv, logA);
    float uvScaleB = pow(gridDiv, logB);

    vec2 UVA = inUV / uvScaleA;
    vec2 UVB = inUV / uvScaleB;

    float logC = logA + 2.0;
    float uvScaleC = pow(gridDiv, logC);
    vec2 UVC = inUV / uvScaleC;

    vec2 gridUVA = 1.0 - abs(fract(UVA) * 2.0 - 1.0);
    vec2 gridUVB = 1.0 - abs(fract(UVB) * 2.0 - 1.0);
    vec2 gridUVC = 1.0 - abs(fract(UVC) * 2.0 - 1.0);

    float lineWidthA = LineWidth * (1.0 - blendFactor);
    float lineWidthB = mix(MajorLineWidth, LineWidth, blendFactor);
    float lineWidthC = MajorLineWidth * blendFactor;

    float lineFadeA = clamp(lineWidthA, 0.0, 1.0);
    float lineFadeB = clamp(lineWidthB, 0.0, 1.0);
    float lineFadeC = clamp(lineWidthC, 0.0, 1.0);

    vec2 ddxUV = dFdx(inUV);
    vec2 ddyUV = dFdy(inUV);
    float lenX = length(vec2(ddxUV.x, ddyUV.x));
    float lenY = length(vec2(ddxUV.y, ddyUV.y));

    float lineAA_xA = lenX / uvScaleA;
    float lineAA_yA = lenY / uvScaleA;
    float lineAA_xB = lenX / uvScaleB;
    float lineAA_yB = lenY / uvScaleB;
    float lineAA_xC = lenX / uvScaleC;
    float lineAA_yC = lenY / uvScaleC;

    float grid2A_x = 1.0 - smoothstep((lineWidthA - 1.5) * lineAA_xA, (lineWidthA + 1.5) * lineAA_xA, gridUVA.x);
    float grid2A_y = 1.0 - smoothstep((lineWidthA - 1.5) * lineAA_yA, (lineWidthA + 1.5) * lineAA_yA, gridUVA.y);

    float grid2B_x = 1.0 - smoothstep((lineWidthB - 1.5) * lineAA_xB, (lineWidthB + 1.5) * lineAA_xB, gridUVB.x);
    float grid2B_y = 1.0 - smoothstep((lineWidthB - 1.5) * lineAA_yB, (lineWidthB + 1.5) * lineAA_yB, gridUVB.y);

    float grid2C_x = 1.0 - smoothstep((lineWidthC - 1.5) * lineAA_xC, (lineWidthC + 1.5) * lineAA_xC, gridUVC.x);
    float grid2C_y = 1.0 - smoothstep((lineWidthC - 1.5) * lineAA_yC, (lineWidthC + 1.5) * lineAA_yC, gridUVC.y);

    float gridA = clamp(grid2A_x + grid2A_y, 0.0, 1.0) * lineFadeA;
    float gridB = clamp(grid2B_x + grid2B_y, 0.0, 1.0) * lineFadeB;
    float gridC = clamp(grid2C_x + grid2C_y, 0.0, 1.0) * lineFadeC;

    float grid = clamp(gridA + max(gridB, gridC), 0.0, 1.0);

    // Axis lines
    float lowX = (lineWidthB - 1.5) * lenX;
    float highX = (lineWidthB + 1.5) * lenX;
    float distX = abs(inUV.x);
    float axis_z_alpha = 1.0 - smoothstep(lowX, highX, distX); // z-axis at x=0

    float lowY = (lineWidthB - 1.5) * lenY;
    float highY = (lineWidthB + 1.5) * lenY;
    float distY = abs(inUV.y);
    float axis_x_alpha = 1.0 - smoothstep(lowY, highY, distY); // x-axis at y=0

    // Arrows
    float arrow_x = 0.0;
    float arrow_z = 0.0;

    float aa = 0.5 * (lenX + lenY);

    // X axis arrows and lines
    mat2 rot_neg_x = mat2(-1.0, 0.0, 0.0, -1.0); // 180°
    float cell_x = round(inUV.x / ArrowSpacing );
    if (cell_x >= 0)
    {
        vec2 arrow_center   = vec2(cell_x * ArrowSpacing, 0.0);
        vec2 local_p        = inUV - arrow_center;
        float d = sdArrow(rot_neg_x * local_p);
        arrow_x = max(arrow_x, smoothstep(-aa, aa, -d));
    }
    else if (cell_x <= -1.0) 
    {
        vec2 arrow_center = vec2(cell_x * ArrowSpacing, 0.0);
        vec2 local_p = inUV - arrow_center;
        float d = sdOrientedBox(rot_neg_x * local_p, vec2(-ArrowStemLength, 0.0), vec2(0.0, 0.0), ArrowStemWidth);
        arrow_x = max(arrow_x, smoothstep(-aa, aa, -d));
    }

    // Z axis arrows and lines
    mat2 rot_y = mat2(0.0, 1.0, -1.0, 0.0); // cw 90
    float cell_y = round(inUV.y / ArrowSpacing);
    if (cell_y >= 0) 
    {
        vec2 arrow_center = vec2(0.0, cell_y * ArrowSpacing);
        vec2 local_p = inUV - arrow_center;
        float d = sdArrow(rot_y * local_p);
        arrow_z = max(arrow_z, smoothstep(-aa, aa, -d));
    }
    else if (cell_y <= -1.0) 
    {
        vec2 arrow_center = vec2(0.0, cell_y * ArrowSpacing);
        vec2 local_p = inUV - arrow_center;
        float d = sdOrientedBox(rot_y * local_p, vec2(-ArrowStemLength, 0.0), vec2(0.0, 0.0), ArrowStemWidth);
        arrow_z = max(arrow_z, smoothstep(-aa, aa, -d));
    }

    // Combine
    cos_theta = pow(cos_theta, fade_power);
    outColor = mix(BaseColor, LineColor, grid * cos_theta * LineColor.a);
    outColor = mix(outColor, XAxisColor, axis_x_alpha * cos_theta * XAxisColor.a);
    outColor = mix(outColor, ZAxisColor, axis_z_alpha * cos_theta * ZAxisColor.a);
    outColor = mix(outColor, XAxisColor, arrow_x * cos_theta * XAxisColor.a);
    outColor = mix(outColor, ZAxisColor, arrow_z * cos_theta * ZAxisColor.a);
}
*/


#if 0
// Fragment Shader
#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_ARB_gpu_shader_fp64 : enable

// Constants
const float GridScale = 1.0;
const float GridBias = 0.8;
const vec4 BaseColor = vec4(0.5, 0.5, 0.5, 1.0);
const vec4 LineColor = vec4(0.3, 0.3, 0.3, 1.0);
const float LineWidth = 2.00;
const float MajorLineWidth = 4.0;
const float fade_power  = 0.5f;

// Push constants
layout(push_constant) uniform PushConsts {
    mat4 L2W;
    mat4 W2C;
    vec3 WorldSpaceCameraPos;
    float MajorGridDiv;
} pushConsts;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inCameraPos;
layout(location = 0) out vec4 outColor;

void main() {
    // Compute projection properties
    vec4 projRow3 = vec4(pushConsts.W2C[0][3], pushConsts.W2C[1][3], pushConsts.W2C[2][3], pushConsts.W2C[3][3]);
    float row3xyzLen = length(projRow3.xyz);
    bool isOrtho = (row3xyzLen < 0.0001) && (abs(projRow3.w - 1.0) < 0.0001);

    vec3 projRow0xyz = vec3(pushConsts.W2C[0][0], pushConsts.W2C[1][0], pushConsts.W2C[2][0]);
    float px = length(projRow0xyz);
    if (px < 0.0001) px = 0.0001;
    vec3 rotRow0 = projRow0xyz / px;

    vec3 projRow1xyz = vec3(pushConsts.W2C[0][1], pushConsts.W2C[1][1], pushConsts.W2C[2][1]);
    float py = length(projRow1xyz);
    if (py < 0.0001) py = 0.0001;
    vec3 rotRow1 = projRow1xyz / py;

    vec3 projRow2xyz = vec3(pushConsts.W2C[0][2], pushConsts.W2C[1][2], pushConsts.W2C[2][2]);
    float pzLen = length(projRow2xyz);
    if (pzLen < 0.0001) pzLen = 0.0001;

    vec3 tempRotRow2 = projRow2xyz / pzLen;
    vec3 crossProd = cross(rotRow1, tempRotRow2);
    float det = dot(rotRow0, crossProd);

    float pzSign = (det < 0.0) ? 1.0 : -1.0;
    float pz = pzSign * pzLen;
    vec3 rotRow2 = projRow2xyz / pz;

    // Compute viewPos
    vec3 viewPos;
    if (isOrtho) {
        viewPos = vec3(0.0, 0.0, 1.0);
    }
    else {
        viewPos = inCameraPos * py;
    }

    // Compute diff = transpose(rot) * viewPos
    vec3 diff = vec3(
        dot(rotRow0, viewPos),
        dot(rotRow1, viewPos),
        dot(rotRow2, viewPos)
    );

    float diffLen = max(length(diff), 0.0001);
    float cos_theta = abs(diff.y / diffLen);

    // Grid computation
    float gridDiv = max(round(pushConsts.MajorGridDiv), 2.0);

    float logLength = (0.5 * log(dot(inCameraPos, inCameraPos))) / log(gridDiv) - GridBias;

    float logA = floor(logLength);
    float logB = logA + 1.0;
    float blendFactor = fract(logLength);

    float uvScaleA = pow(gridDiv, logA);
    float uvScaleB = pow(gridDiv, logB);

    vec2 UVA = inUV / uvScaleA;
    vec2 UVB = inUV / uvScaleB;

    float logC = logA + 2.0;
    float uvScaleC = pow(gridDiv, logC);
    vec2 UVC = inUV / uvScaleC;

    vec2 gridUVA = 1.0 - abs(fract(UVA) * 2.0 - 1.0);
    vec2 gridUVB = 1.0 - abs(fract(UVB) * 2.0 - 1.0);
    vec2 gridUVC = 1.0 - abs(fract(UVC) * 2.0 - 1.0);

    float lineWidthA = LineWidth * (1.0 - blendFactor);
    float lineWidthB = mix(MajorLineWidth, LineWidth, blendFactor);
    float lineWidthC = MajorLineWidth * blendFactor;

    float lineFadeA = clamp(lineWidthA, 0.0, 1.0);
    float lineFadeB = clamp(lineWidthB, 0.0, 1.0);
    float lineFadeC = clamp(lineWidthC, 0.0, 1.0);

    vec2 ddxUV = dFdx(inUV);
    vec2 ddyUV = dFdy(inUV);
    float lenX = length(vec2(ddxUV.x, ddyUV.x));
    float lenY = length(vec2(ddxUV.y, ddyUV.y));

    float lineAA_xA = lenX / uvScaleA;
    float lineAA_yA = lenY / uvScaleA;
    float lineAA_xB = lenX / uvScaleB;
    float lineAA_yB = lenY / uvScaleB;
    float lineAA_xC = lenX / uvScaleC;
    float lineAA_yC = lenY / uvScaleC;

    float grid2A_x = 1.0 - smoothstep((lineWidthA - 1.5) * lineAA_xA, (lineWidthA + 1.5) * lineAA_xA, gridUVA.x);
    float grid2A_y = 1.0 - smoothstep((lineWidthA - 1.5) * lineAA_yA, (lineWidthA + 1.5) * lineAA_yA, gridUVA.y);
    vec2 grid2A = vec2(grid2A_x, grid2A_y);

    float grid2B_x = 1.0 - smoothstep((lineWidthB - 1.5) * lineAA_xB, (lineWidthB + 1.5) * lineAA_xB, gridUVB.x);
    float grid2B_y = 1.0 - smoothstep((lineWidthB - 1.5) * lineAA_yB, (lineWidthB + 1.5) * lineAA_yB, gridUVB.y);
    vec2 grid2B = vec2(grid2B_x, grid2B_y);

    float grid2C_x = 1.0 - smoothstep((lineWidthC - 1.5) * lineAA_xC, (lineWidthC + 1.5) * lineAA_xC, gridUVC.x);
    float grid2C_y = 1.0 - smoothstep((lineWidthC - 1.5) * lineAA_yC, (lineWidthC + 1.5) * lineAA_yC, gridUVC.y);
    vec2 grid2C = vec2(grid2C_x, grid2C_y);

    float gridA = clamp(grid2A.x + grid2A.y, 0.0, 1.0) * lineFadeA;
    float gridB = clamp(grid2B.x + grid2B.y, 0.0, 1.0) * lineFadeB;
    float gridC = clamp(grid2C.x + grid2C.y, 0.0, 1.0) * lineFadeC;

    float grid = clamp(gridA + max(gridB, gridC), 0.0, 1.0) * pow(cos_theta, fade_power );

    outColor = mix(BaseColor, LineColor, grid * LineColor.a);
}

#endif


#if 0

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
const vec4    MajorLineColor = vec4(0.7f, 0.7f, 0.7f, 1.0f);
const vec4    MinorLineColor = vec4(0.6f, 0.6f, 0.6f, 1.0f);   // White                    // Color for minor grid lines
const vec4    BaseColor = vec4(0.4f, 0.4f, 0.4f, 1.0f);   // Black                    // Base background color
const vec4    XAxisColor = vec4(1.0f, 0.0f, 0.0f, 1.0f);   // Red                      // Color for the X-axis line
const vec4    XAxisDashColor = vec4(0.5f, 0.0f, 0.0f, 1.0f);   // Dark red                 // Color for the dashed part of the X-axis
const vec4    YAxisColor = vec4(0.0f, 1.0f, 0.0f, 1.0f);   // Green                    // Color for the Y-axis line
const vec4    YAxisDashColor = vec4(0.0f, 0.5f, 0.0f, 1.0f);   // Dark green               // Color for the dashed part of the Y-axis
const vec4    ZAxisColor = vec4(0.0f, 0.0f, 1.0f, 1.0f);   // Blue                     // Color for the Z-axis line
const vec4    ZAxisDashColor = vec4(0.0f, 0.0f, 0.5f, 1.0f);   // Dark blue                // Color for the dashed part of the Z-axis
const vec4    CenterColor = vec4(1.0f, 1.0f, 1.0f, 1.0f);   // White                    // Color for the axis center (origin)
const float   AxisLineWidth = 0.05f;                           // Slightly thicker axes    // Width of the axis lines (range 0-1)
const float   MajorLineWidth = 0.01f;                           // Medium major lines       // Width of the major grid lines (range 0-1)
const float   MinorLineWidth = 0.01f;                           // Thin minor lines         // Width of the minor grid lines (range 0-1)
const float   AxisDashScale = 1.33f;                           // Balanced dash pattern    // Scale factor for axis dashing

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

float log10_custom(float x) {
    return log(x) / log(10.0);
}


float saturate(float x) {
    return clamp(x, 0.0, 1.0);
}

vec2 saturate(vec2 x) {
    return clamp(x, 0.0, 1.0);
}

vec3 saturate(vec3 x) {
    return clamp(x, 0.0, 1.0);
}

vec4 saturate(vec4 x) {
    return clamp(x, 0.0, 1.0);
}


vec4 compute_grid(float minorCell, vec2 uvDeriv, vec2 axisLines2) {
    float majorCell = minorCell * 10.0;
    vec2 pos = In.UV.xy;
    vec2 majorDeriv = uvDeriv / majorCell;
    float majorLineWidth = MajorLineWidth;
    vec2 majorDrawWidth = max(vec2(majorLineWidth), majorDeriv);
    vec2 majorLineAA = majorDeriv * 1.5;
    vec2 majorGridUV = 1.0 - abs(fract(pos / majorCell) * 2.0 - 1.0);
    vec2 majorAxisOffset = (1.0 - clamp(abs(In.UV.zw / majorCell * 2.0), 0.0, 1.0)) * 2.0;
    majorGridUV += majorAxisOffset;
    vec2 majorGrid2 = smoothstep(majorDrawWidth + majorLineAA, majorDrawWidth - majorLineAA, majorGridUV);
    majorGrid2 *= saturate(majorLineWidth / majorDrawWidth);
    majorGrid2 = clamp(majorGrid2 - axisLines2, 0.0, 1.0);
    majorGrid2 = mix(majorGrid2, vec2(majorLineWidth), saturate(majorDeriv * 0.5 - 1.0));

    float minorLineWidth = min(MinorLineWidth, MajorLineWidth);
    bool minorInvertLine = minorLineWidth > 0.5;
    float minorTargetWidth = minorInvertLine ? 1.0 - minorLineWidth : minorLineWidth;
    vec2 minorDeriv = uvDeriv / minorCell;
    vec2 minorDrawWidth = max(vec2(minorTargetWidth), minorDeriv);
    vec2 minorLineAA = minorDeriv * 1.5;
    vec2 minorGridUV = abs(fract(pos / minorCell) * 2.0 - 1.0);
    minorGridUV = minorInvertLine ? minorGridUV : 1.0 - minorGridUV;
    vec2 minorMajorOffset = (1.0 - clamp((1.0 - abs(fract(In.UV.zw / majorCell) * 2.0 - 1.0)) * 10.0, 0.0, 1.0)) * 2.0;
    minorGridUV += minorMajorOffset;
    vec2 minorGrid2 = smoothstep(minorDrawWidth + minorLineAA, minorDrawWidth - minorLineAA, minorGridUV);
    minorGrid2 *= saturate(minorTargetWidth / minorDrawWidth);
    minorGrid2 = clamp(minorGrid2 - axisLines2, 0.0, 1.0);
    minorGrid2 = mix(minorGrid2, vec2(minorTargetWidth), saturate(minorDeriv * 0.5 - 1.0));
    minorGrid2 = minorInvertLine ? 1.0 - minorGrid2 : minorGrid2;
    minorGrid2 = all(greaterThan(abs(In.UV.zw), vec2(minorCell * 0.5))) ? minorGrid2 : vec2(0.0);

    float minorGrid = mix(minorGrid2.x, 1.0, minorGrid2.y);
    float majorGrid = mix(majorGrid2.x, 1.0, majorGrid2.y);

    vec4 col = mix(BaseColor, MinorLineColor, minorGrid * MinorLineColor.a);
    col = mix(col, MajorLineColor, majorGrid * MajorLineColor.a);
    return col;
}

void main()
{
    // Compute partial derivatives of UV for anti-aliasing and line width adjustment
    vec4    uvDDXY = vec4(dFdx(In.UV.xy), dFdy(In.UV.xy));         // Derivatives in X and Y directions
    vec2    uvDeriv = vec2(length(uvDDXY.xz), length(uvDDXY.yw));   // Magnitude of derivatives for each axis

    // Calculate effective axis line width, ensuring it's at least as wide as major lines
    float   axisLineWidth = max(MajorLineWidth, AxisLineWidth);

    // Determine draw width for axis lines, clamped to pixel size for anti-aliasing
    vec2    axisDrawWidth = max(vec2(axisLineWidth), uvDeriv);

    // Anti-aliasing range for axis lines
    vec2    axisLineAA = uvDeriv * 1.5;

    // Compute smoothstep for axis lines (doubled for symmetry around zero)
    vec2    axisLines2 = smoothstep(axisDrawWidth + axisLineAA, axisDrawWidth - axisLineAA, abs(In.UV.zw * 2.0));

    // Scale axis lines intensity based on line width relative to draw width
    axisLines2 *= clamp(axisLineWidth / axisDrawWidth, 0.0, 1.0);

    // Dynamic scale for infinite zoom snapping to power of 10
    float px = 0.5 * (uvDeriv.x + uvDeriv.y);
    float logCell = log10_custom(px) + log10_custom(20.0);
    float floorLog = floor(logCell);
    float fracLog = logCell - floorLog;
    float minorCell1 = pow(10.0, floorLog);
    float minorCell2 = pow(10.0, floorLog + 1.0);
    float fade = smoothstep(0.1, 0.9, fracLog);

    vec4 col1 = compute_grid(minorCell1, uvDeriv, axisLines2);
    vec4 col2 = compute_grid(minorCell2, uvDeriv, axisLines2);
    vec4 col = mix(col1, col2, fade);

    // Compute UV for axis dashes (sawtooth with offset)
    vec2    axisDashUV = abs(fract((In.UV.zw + axisLineWidth * 0.5) * AxisDashScale) * 2.0 - 1.0) - 0.5;

    // Derivatives for dash anti-aliasing
    vec2    axisDashDeriv = uvDeriv * AxisDashScale * 1.5;

    // Smoothstep for dashes
    vec2    axisDash = smoothstep(-axisDashDeriv, axisDashDeriv, axisDashUV);

    // Solid line for positive side, dashed for negative
    axisDash = mix(vec2(1.0), axisDash, vec2(lessThan(In.UV.zw, vec2(0.0))));

    // Gamma correction for colors if in gamma color space
#if defined(COLORSPACE_GAMMA)
    // Convert to linear
    vec4    xAxisColor = vec4(pow(XAxisColor.rgb, vec3(2.2)), XAxisColor.a);
    vec4    yAxisColor = vec4(pow(YAxisColor.rgb, vec3(2.2)), YAxisColor.a);
    vec4    zAxisColor = vec4(pow(ZAxisColor.rgb, vec3(2.2)), ZAxisColor.a);
    vec4    xAxisDashColor = vec4(pow(XAxisDashColor.rgb, vec3(2.2)), XAxisDashColor.a);
    vec4    yAxisDashColor = vec4(pow(YAxisDashColor.rgb, vec3(2.2)), YAxisDashColor.a);
    vec4    zAxisDashColor = vec4(pow(ZAxisDashColor.rgb, vec3(2.2)), ZAxisDashColor.a);
    vec4    centerColor = vec4(pow(CenterColor.rgb, vec3(2.2)), CenterColor.a);
    vec4    majorLineColor = vec4(pow(MajorLineColor.rgb, vec3(2.2)), MajorLineColor.a);
    vec4    minorLineColor = vec4(pow(MinorLineColor.rgb, vec3(2.2)), MinorLineColor.a);
    vec4    baseColor = vec4(pow(BaseColor.rgb, vec3(2.2)), BaseColor.a);
#else
    // Use colors as-is in linear space
    vec4    xAxisColor = XAxisColor;
    vec4    yAxisColor = YAxisColor;
    vec4    zAxisColor = ZAxisColor;
    vec4    xAxisDashColor = XAxisDashColor;
    vec4    yAxisDashColor = YAxisDashColor;
    vec4    zAxisDashColor = ZAxisDashColor;
    vec4    centerColor = CenterColor;
    vec4    majorLineColor = MajorLineColor;
    vec4    minorLineColor = MinorLineColor;
    vec4    baseColor = BaseColor;
#endif

    // Select axis colors based on plane define
#if defined(AXIS_YZ)
    vec4    aAxisColor = yAxisColor;       // A axis: Y
    vec4    bAxisColor = zAxisColor;       // B axis: Z
    vec4    aAxisDashColor = yAxisDashColor;
    vec4    bAxisDashColor = zAxisDashColor;
#elif defined(AXIS_XY)
    vec4    aAxisColor = xAxisColor;       // A axis: X
    vec4    bAxisColor = yAxisColor;       // B axis: Y
    vec4    aAxisDashColor = xAxisDashColor;
    vec4    bAxisDashColor = yAxisDashColor;
#else   // define (AXIS_XZ)
    vec4    aAxisColor = xAxisColor;       // A axis: X
    vec4    bAxisColor = zAxisColor;       // B axis: Z
    vec4    aAxisDashColor = xAxisDashColor;
    vec4    bAxisDashColor = zAxisDashColor;
#endif

    // Apply dashing to A axis color
    aAxisColor = mix(aAxisDashColor, aAxisColor, axisDash.y);

    // Apply dashing to B axis color
    bAxisColor = mix(bAxisDashColor, bAxisColor, axisDash.x);

    // Apply center color to A axis at origin
    aAxisColor = mix(aAxisColor, centerColor, axisLines2.y);

    // Combine axis lines: overlay A over B with intensity
    vec4    axisLines = mix(bAxisColor * axisLines2.y, aAxisColor, axisLines2.x);

    // Overlay axis lines with alpha blending
    col = col * (1.0 - axisLines.a) + axisLines;

    // Convert back to gamma if needed
#if defined(COLORSPACE_GAMMA)
    col = vec4(pow(col.rgb, vec3(1.0 / 2.2)), col.a);
#endif

    // Multiply by input color (e.g., vertex color or tint)
    OutColor = col * In.Color;
    //OutColor.w = 0.75f;
}

#endif