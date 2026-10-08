#version 450

#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;        //[INPUT_POSITION]
layout (location = 1) in vec3 inBinormal;   //[INPUT_BINORMAL]
layout (location = 2) in vec3 inTangent;    //[INPUT_TANGENT]
layout (location = 3) in vec3 inNormal;     //[INPUT_NORMAL]
layout (location = 4) in vec2 inUV;         //[INPUT_UVS]

layout(push_constant) uniform uPushConstant 
{ 
   // 128 bytes: the most every Vulkan device must accept (maxPushConstantsSize; WSLg's Dozen driver allows exactly 128).
   // Must match push_contants::gpu on the C++ side (xtexture_editor_preview.h, xtexture_thumbnail.h, E10_TextureResourcePipeline.cpp).
   mat4  L2C;
   vec4  TintColor;
   vec4  ScaleTranslate;     // xy: uScale, zw: uTranslate
   vec4  LightPosMip;        // xyz: LocalSpaceLightPos, w: MipLevel
   vec2  uvScale;
   float ToGamma;
   uint  Flags;              // 0/1 switches, 4 bits each: ColorMask.xyzw (bits 0-3), Mode.xyzw (4-7), NormalModes.xyzw (8-11)
} pc;

vec4 UnpackFlags4( uint Shift ) { return vec4( (uvec4(pc.Flags >> Shift) >> uvec4(0u, 1u, 2u, 3u)) & uvec4(1u) ); }

layout(location = 0) out struct 
{ 
    float VertexLighting; 
    vec3  UV; 
    vec3  LocalSpaceLightPosition;
    vec3  LocalSpaceLightDir;
    vec3  TangentLightDir;
    mat3  BTN;
    vec3  LocalSpacePosition;
} Out;

void main() 
{
    // Compute lighting information
    Out.LocalSpaceLightPosition = pc.LightPosMip.xyz;

    //-------------------------------------------------------------------------------
    Out.LocalSpaceLightDir      = normalize( pc.LightPosMip.xyz - inPos );
    Out.VertexLighting          = max( 0, dot( inNormal, Out.LocalSpaceLightDir ));
    //-------------------------------------------------------------------------------

    Out.UV                      = normalize(inPos);

    Out.BTN                     = mat3( inTangent, inBinormal, inNormal);
    Out.TangentLightDir         = transpose(Out.BTN) * Out.LocalSpaceLightDir;

    Out.LocalSpacePosition      = inPos;

    gl_Position                 = pc.L2C * vec4(inPos.xyz, 1.0);
}
