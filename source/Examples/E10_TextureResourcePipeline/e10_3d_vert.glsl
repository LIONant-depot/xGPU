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
    Out.LocalSpaceLightPosition = pc.LocalSpaceLightPos;

    //-------------------------------------------------------------------------------
    Out.LocalSpaceLightDir      = normalize( pc.LocalSpaceLightPos - inPos );
    Out.VertexLighting          = max( 0, dot( inNormal, Out.LocalSpaceLightDir ));
    //-------------------------------------------------------------------------------

    Out.UV                      = vec3(inUV * pc.uvScale,0);

    Out.BTN                     = mat3( inTangent, inBinormal, inNormal);
    Out.TangentLightDir         = transpose(Out.BTN) * Out.LocalSpaceLightDir;

    Out.LocalSpacePosition      = inPos;

    gl_Position                 = pc.L2C * vec4(inPos.xyz, 1.0);
}
