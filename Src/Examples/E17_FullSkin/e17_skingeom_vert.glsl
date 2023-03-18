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
} Uniform;

layout(location = 0) out struct _o
{ 
    mat3  BTN;
    vec4  VertColor;
	vec4  WorldSpacePosition;
    vec2  UV; 
} Out;

void main() 
{
    // Skin the matrix
    mat4 L2W = Uniform.L2W[inBones.x] * inWeights.x + Uniform.L2W[inBones.y] * inWeights.y 
             + Uniform.L2W[inBones.z] * inWeights.z + Uniform.L2W[inBones.w] * inWeights.w;

    // Decompress the binormal & transform everything to world space
    mat3 Rot                    = mat3(L2W);
    vec3 Normal                 = normalize(Rot * inNormal.xyz);
    vec3 Tangent                = normalize(Rot * inTangent.xyz);
    vec3 Binormal               = normalize( cross(Tangent.xyz, Normal.xyz) * inNormal.w );

    // Set all the output vars
    Out.BTN                     = mat3(Tangent, Binormal, Normal);
    Out.UV                      = inUV;
    Out.VertColor               = vec4(1);
    Out.WorldSpacePosition      = L2W * vec4(inPos.xyz, 1.0);
    gl_Position                 = Uniform.W2C * Out.WorldSpacePosition;
}
