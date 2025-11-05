#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_ARB_gpu_shader_fp64 : enable

// Constants
const float GridScale = 1.0;

// Push constants
layout(push_constant) uniform PushConsts 
{
    mat4 L2W;
    mat4 W2C;
    vec3 WorldSpaceCameraPos;
    float MajorGridDiv;
} pushConsts;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;   //[INPUT_COLOR]

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outCameraPos;

void main() 
{
    vec4 worldPos4 = pushConsts.L2W * vec4(inPosition, 1.0);
    vec3 worldPos = worldPos4.xyz;
    gl_Position = pushConsts.W2C * worldPos4;

    // Assume WorldUV = true
    outUV = worldPos.xz * GridScale;

    // Detect if orthographic
    vec4 projRow3 = vec4(pushConsts.W2C[0][3], pushConsts.W2C[1][3], pushConsts.W2C[2][3], pushConsts.W2C[3][3]);
    float row3xyzLen = length(projRow3.xyz);
    bool isOrtho = (row3xyzLen < 0.0001) && (abs(projRow3.w - 1.0) < 0.0001);

    // Compute py = P[1][1]
    vec3 projRow1xyz = vec3(pushConsts.W2C[0][1], pushConsts.W2C[1][1], pushConsts.W2C[2][1]);
    float py = length(projRow1xyz);
    if (py < 0.0001) py = 0.0001; // Avoid division by zero

    if (isOrtho) 
    {
        outCameraPos = vec3(0.0, 0.0, 1.0 / py);
    }
    else 
    {
        // Compute px = P[0][0]
        vec3 projRow0xyz = vec3(pushConsts.W2C[0][0], pushConsts.W2C[1][0], pushConsts.W2C[2][0]);
        float px = length(projRow0xyz);
        if (px < 0.0001) px = 0.0001;

        // Compute pz length
        vec3 projRow2xyz = vec3(pushConsts.W2C[0][2], pushConsts.W2C[1][2], pushConsts.W2C[2][2]);
        float pzLen = length(projRow2xyz);
        if (pzLen < 0.0001) pzLen = 0.0001;

        // Compute rotation matrix rows
        vec3 rotRow0 = projRow0xyz / px;
        vec3 rotRow1 = projRow1xyz / py;

        // Test positive pz
        vec3 tempRotRow2 = projRow2xyz / pzLen;
        vec3 crossProd = cross(rotRow1, tempRotRow2);
        float det = dot(rotRow0, crossProd);

        // Choose sign for left-handed (det = -1)
        float pzSign = (det < 0.0) ? 1.0 : -1.0;
        float pz = pzSign * pzLen;

        vec3 rotRow2 = projRow2xyz / pz;

        // Rotation matrix (rows)
        mat3 rot = mat3(rotRow0, rotRow1, rotRow2);

        // Compute view position
        dvec3 diff = dvec3(worldPos) - pushConsts.WorldSpaceCameraPos;
        vec3 viewPos = rot * vec3(diff);

        // Adjust
        outCameraPos = viewPos / py;
    }
}


#if 0
#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(location = 0) in vec3 inPos;     //[INPUT_POSITION]
layout(location = 1) in vec2 inUV;      //[INPUT_UVS]
layout(location = 2) in vec4 inColor;   //[INPUT_COLOR]

layout(std140, push_constant) uniform PushConsts
{
    // Vertex shader
    mat4    L2W;
    mat4    W2C;
    vec3    WorldSpaceCameraPos;
    float   MajorGridDiv;

} pushConsts;

#if defined(_AXIS_X)
    #define AXIS_COMPONENTS yz
#elif defined(_AXIS_Z)
    #define AXIS_COMPONENTS xy
#else
    #define AXIS_COMPONENTS xz
#endif

layout(location = 0) out struct 
{ 
    vec4 Color; 
    vec4 UV; 
} Out;

void main() 
{
    gl_Position = pushConsts.W2C * (pushConsts.L2W * vec4(inPos, 1.0));
    float   div                     = max(2.0, floor(pushConsts.MajorGridDiv + 0.5));
    vec3    worldPos                = (pushConsts.L2W * vec4(inPos.xyz, 1.0)).xyz;
    vec3    cameraCenteringOffset   = floor(pushConsts.WorldSpaceCameraPos / div) * div;

    Out.UV.yx = (worldPos - cameraCenteringOffset).AXIS_COMPONENTS;
    Out.UV.wz = worldPos.AXIS_COMPONENTS;
    Out.Color = inColor;
}
#endif
