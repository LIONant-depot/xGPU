#version 450

#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3  inPos;        //[INPUT_POSITION]
layout (location = 1) in vec2  inUV;         //[INPUT_UVS]
layout (location = 2) in vec4  inNormal;     //[INPUT_NORMAL]
layout (location = 3) in vec4  inTangent;    //[INPUT_TANGENT]
layout (location = 4) in vec4  inWeights;    //[INPUT_WEIGHTS]
layout (location = 5) in uvec4 inBones;      //[INPUT_BONES]

layout (set = 1, binding = 0) uniform _u
{
    mat4  W2C;
    mat4  L2W[256];
    vec4  LightPosWorldSpace;
} u;

layout(location = 0) out struct 
{ 
    vec4  VertColor;
    vec2  UV; 
} o;

void main() 
{
    mat4 L2W = u.L2W[inBones.x] * inWeights.x + u.L2W[inBones.y] * inWeights.y 
             + u.L2W[inBones.z] * inWeights.z + u.L2W[inBones.w] * inWeights.w;

    o.UV                = inUV;
    o.VertColor         = vec4(1);
    gl_Position         = u.W2C * L2W * vec4(inPos.xyz, 1.0);

    // (used for debugging) Bone influence for each vertex (change the zero to one)
    #if 0
    const int iBone = 12;
         if( inBones.x == iBone ) o.VertColor = vec4(1,1-inWeights.x,1-inWeights.x,1);
    else if( inBones.y == iBone ) o.VertColor = vec4(1,1-inWeights.y,1-inWeights.y,1);
    else if( inBones.z == iBone ) o.VertColor = vec4(1,1-inWeights.z,1-inWeights.z,1);
    else if( inBones.w == iBone ) o.VertColor = vec4(1,1-inWeights.w,1-inWeights.w,1);
    #endif
}
