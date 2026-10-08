#version 450
#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 0)    uniform     samplerCube   uSamplerColor; // [INPUT_TEXTURE]

layout(location = 0) in struct 
{ 
    float VertexLighting; 
    vec3  UV; 
    vec3  LocalSpaceLightPosition;
    vec3  LocalSpaceLightDir;
    vec3  TangentLightDir;
    mat3  BTN;
	vec3  LocalSpacePosition;
} In;

layout (location = 0)   out         vec4        outFragColor;

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

void main() 
{
    vec4 Color     = clamp( texture( uSamplerColor, In.UV.xyz ), 0, 1)                 *    UnpackFlags4(4u).w + 
                     clamp( textureLod( uSamplerColor, In.UV.xyz, pc.LightPosMip.w ), 0, 1) * (1-UnpackFlags4(4u).w);

    // Decode the normal
    float DisplayNormal = dot(UnpackFlags4(8u).xy, UnpackFlags4(8u).xy);
    vec3 NormalFromBC3 = vec3( Color.ag, 0);
    vec3 NormalFromBC5 = vec3( Color.gr, 0);
    vec3 Normal        = NormalFromBC3.rgb * UnpackFlags4(8u).x + NormalFromBC5.rgb * UnpackFlags4(8u).y;
    Normal.xy = Normal.rg * 2.0 - 1.0;
    Normal.z  = sqrt(max( 0, 1 - dot(Normal.xy, Normal.xy)));

    // Chose between a compressed normal and a raw normal
    Normal = (Color.rgb * 2.0 - 1.0) * (1 - DisplayNormal) + Normal.rgb * DisplayNormal; 

    float LightingDiffuse = max( 0, dot( In.BTN*Normal, normalize(In.LocalSpaceLightPosition - In.LocalSpacePosition) ));

    // Convert Normal to color
    Normal = Normal * 0.5 + 0.5;

    // Make sure that display normal is activated in the case where we want lighting but we did not have a compressed normal
    DisplayNormal = max( DisplayNormal, UnpackFlags4(8u).z);

    // Choose between lighting mode or normal mode
    Normal = vec3(LightingDiffuse) * UnpackFlags4(8u).z + Normal * ( 1 - UnpackFlags4(8u).z);

    Color = (Color * (1 - DisplayNormal)) + vec4( (Normal.rgb * DisplayNormal), DisplayNormal);

    // Apply tint color
    Color = Color * pc.TintColor;

    vec4 NewColor = vec4( dot( Color, UnpackFlags4(0u) ).rrr, 1);
    vec4 NoAlpha  = vec4( Color.rgb, 1);

    // Output color
    outFragColor = Color    * UnpackFlags4(4u).x 
                 + NewColor * UnpackFlags4(4u).y 
                 + NoAlpha  * UnpackFlags4(4u).z;

    // We must convert to gamma every time...
    outFragColor.rgb = pow( outFragColor.rgb, vec3(1/pc.ToGamma) );
}


