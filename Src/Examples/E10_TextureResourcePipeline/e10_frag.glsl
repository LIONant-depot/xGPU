#version 450
#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 0)    uniform     sampler2D   uSamplerColor; // [INPUT_TEXTURE]

layout(location = 0) in struct { vec2 UV; } In;

layout (location = 0)   out         vec4        outFragColor;

layout(push_constant) uniform uPushConstant 
{ 
   float MipLevel;
   float ToGamma;
   vec2  uScale; 
   vec2  uTranslate; 
   vec2  uvScale; 
   vec4  TintColor;
   vec4  ColorMask; 
   vec4  Mode;
} pc;


void main() 
{
    vec4 Color     = texture( uSamplerColor, In.UV )                 *    pc.Mode.z + 
                     textureLod( uSamplerColor, In.UV, pc.MipLevel ) * (1-pc.Mode.z);

    Color = Color * pc.TintColor;

    vec4 NewColor = vec4( dot( Color, pc.ColorMask ).rrr, 1);
    vec4 NoAlpha  = vec4( Color.rgb, 1);

    // Output color
    outFragColor = Color    * pc.Mode.x 
                 + NewColor * pc.Mode.y 
                 + NoAlpha  * pc.Mode.z;

    // Output color in linear space 0.454545f
    outFragColor.rgb = pow(outFragColor.rgb, vec3(pc.ToGamma)); 
}


