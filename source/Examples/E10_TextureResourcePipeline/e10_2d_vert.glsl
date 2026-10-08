#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec3 aUV;

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

out gl_PerVertex { vec4 gl_Position; };
layout(location = 0) out struct 
{ 
    vec3  UV; 
} Out;

void main()
{
    Out.UV      = vec3(aUV.xy * pc.uvScale, 0);
    gl_Position = vec4(aPos * pc.ScaleTranslate.xy + pc.ScaleTranslate.zw, 0, 1);
}