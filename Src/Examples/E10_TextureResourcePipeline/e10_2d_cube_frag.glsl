#version 450
#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 0)    uniform     samplerCube   uSamplerColor; // [INPUT_TEXTURE]

layout(location = 0) in struct 
{ 
    vec3  UV; 
} In;

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
   vec4  NormalModes;
   mat4  L2C;
   vec3  LocalSpaceLightPos;
   vec4  UVMode;
} pc;


void main() 
{
 //   int faceIndex  = int( pc.UVMode.z );
    vec4 Color     = clamp( texture( uSamplerColor, In.UV.xyz ), 0, 1)                 *    pc.Mode.w + 
                     clamp( textureLod( uSamplerColor, In.UV.xyz, pc.MipLevel ), 0, 1) * (1-pc.Mode.w);

    // Decode the normal
    float DisplayNormal = dot(pc.NormalModes, pc.NormalModes);
    vec3 NormalFromBC3 = vec3( Color.ag, 0);
    vec3 NormalFromBC5 = vec3( Color.gr, 0);
    vec3 Normal        = NormalFromBC3.rgb * pc.NormalModes.x + NormalFromBC5.rgb * pc.NormalModes.y;
    Normal.xy = Normal.rg * 2.0 - 1.0;
    Normal.z  = sqrt(1 - min( 1, dot(Normal.xy, Normal.xy)));

    // Convert Normal to color
    Normal = Normal * 0.5 + 0.5;

    Color = (Color * (1 - DisplayNormal)) + vec4( (Normal.rgb * DisplayNormal), DisplayNormal);

    // Apply tint color
    Color = Color * pc.TintColor;

    vec4 NewColor = vec4( dot( Color, pc.ColorMask ).rrr, 1);
    vec4 NoAlpha  = vec4( Color.rgb, 1);

    // Output color
    outFragColor = Color    * pc.Mode.x 
                 + NewColor * pc.Mode.y 
                 + NoAlpha  * pc.Mode.z;

    // We must convert to gamma every time...
    outFragColor.rgb = pow( outFragColor.rgb, vec3(1/pc.ToGamma) );

    //
    // Gamma correction with color satuation
    //
    /*
    float lum    = dot(outFragColor.rgb, vec3(0.299, 0.587, 0.114));
    if( lum > 0.001f )
    {
        float newLum = pow(lum, (1/pc.ToGamma));
        float t      = 0.2f;
        outFragColor.rgb = pow( outFragColor.rgb, vec3(1/pc.ToGamma)) * (1-t) + t * (outFragColor.rgb / lum * newLum);
    }
    else
    {
        outFragColor.rgb = pow( outFragColor.rgb, vec3(1/pc.ToGamma) );
    }
    */
}


