#version 450
#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 0)    uniform     sampler2D   uSamplerColor; // [INPUT_TEXTURE]

layout(location = 0) in struct 
{ 
    float VertexLighting; 
    vec2  UV; 
	vec3  LocalSpaceLightPosition;
	vec3  LocalSpaceLightDir;
    vec3  TangentLightDir;
    mat3  BTN;
	vec3  LocalSpacePosition;
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
} pc;


void main() 
{
    vec4 Color     = texture( uSamplerColor, In.UV )                 *    pc.Mode.z + 
                     textureLod( uSamplerColor, In.UV, pc.MipLevel ) * (1-pc.Mode.z);

    // Decode the normal
    float DisplayNormal = dot(pc.NormalModes.xy, pc.NormalModes.xy);
    vec3 NormalFromBC3 = vec3( Color.ag, 0);
    vec3 NormalFromBC5 = vec3( Color.gr, 0);
    vec3 Normal        = NormalFromBC3.rgb * pc.NormalModes.x + NormalFromBC5.rgb * pc.NormalModes.y;
    Normal.xy = Normal.rg * 2.0 - 1.0;
    Normal.z  = sqrt(1 - min( 1, dot(Normal.xy, Normal.xy)));

    // Chose between a compressed normal and a raw normal
    Normal = (Color.rgb * 2.0 - 1.0) * (1 - DisplayNormal) + Normal.rgb * DisplayNormal; 

    float LightingDiffuse = max( 0, dot( In.BTN*Normal, normalize(In.LocalSpaceLightPosition - In.LocalSpacePosition) ));

    // Convert Normal to color
    Normal = Normal * 0.5 + 0.5;

    // Make sure that display normal is activated in the case where we want lighting but we did not have a compressed normal
    DisplayNormal = max( DisplayNormal, pc.NormalModes.z);

    // Choose between lighting mode or normal mode
    Normal = vec3(LightingDiffuse) * pc.NormalModes.z + Normal * ( 1 - pc.NormalModes.z);

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
}


