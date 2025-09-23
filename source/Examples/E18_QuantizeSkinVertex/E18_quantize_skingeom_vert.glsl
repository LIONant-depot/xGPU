#version 450

#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec4  inQPos;          //[INPUT_POSITION]
layout (location = 1) in vec4  inWeights;       //[INPUT_WEIGHTS]
layout (location = 2) in uvec4 inBones;         //[INPUT_BONES]

layout (location = 3) in vec4  inQNormTangent;  //[INPUT_TANGENT]
layout (location = 4) in vec2  inQUV;           //[INPUT_UVS]

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

layout (push_constant) uniform _pc
{
	vec4  LightColor;
	vec4  AmbientLightColor;
    vec4  WorldSpaceLightPos;
    vec4  WorldSpaceEyePos;

    vec3  PosDecompressionScale;                // Decompression constants
    vec3  PosDecompressionOffset;
    vec4  UVDecompression;
} PushC;


void main() 
{
    // Skin the matrix
    mat4 L2W = Uniform.L2W[inBones.x] * inWeights.x + Uniform.L2W[inBones.y] * inWeights.y 
             + Uniform.L2W[inBones.z] * inWeights.z + Uniform.L2W[inBones.w] * inWeights.w;

    // Decompress Normal
    // We must also decompress the Binormal sign bit and the Tangent.Z sign bit
    const float AbsQNormalX  = abs(inQPos.w);
    const float BinormalSign = AbsQNormalX > 0.5f ? -1.0f : 1.0f;
    vec3 Normal = vec3( 4 * AbsQNormalX - (2 - BinormalSign), inQNormTangent.x, inQPos.w < 0 ? -1 : 1 );
    Normal.z *= sqrt(1.01f - Normal.x*Normal.x - Normal.y*Normal.y );

    // Decompress Tangent
    vec3 Tangent = vec3( inQNormTangent.y, inQNormTangent.z, inQNormTangent.w );
    Tangent.z *= sqrt(1.01f - Tangent.x*Tangent.x - Tangent.y*Tangent.y );

    // Decompress the binormal & transform everything to world space
    mat3 Rot                    = mat3(L2W);
    vec3 WNormal                = normalize(Rot * Normal);
    vec3 WTangent               = normalize(Rot * Tangent);
    vec3 WBinormal              = normalize( cross(WTangent, WNormal) * BinormalSign );

    // Decompress Position
    vec4 Pos = vec4( PushC.PosDecompressionScale * inQPos.xyz + PushC.PosDecompressionOffset, 1.0f);

    // Decompress UVs
    Out.UV   = PushC.UVDecompression.xy * inQUV + PushC.UVDecompression.zw;

    // Set all the output vars
    Out.BTN                     = mat3(WTangent, WBinormal, WNormal);
    Out.WorldSpacePosition      = L2W * Pos;
    Out.VertColor               = vec4(1);
    gl_Position                 = Uniform.W2C * Out.WorldSpacePosition;
}
