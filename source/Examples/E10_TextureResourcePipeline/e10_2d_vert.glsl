#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec3 aUV;

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

out gl_PerVertex { vec4 gl_Position; };
layout(location = 0) out struct 
{ 
    vec3  UV; 
} Out;

void main()
{
    Out.UV      = vec3(aUV.xy * pc.uvScale, 0);
    gl_Position = vec4(aPos * pc.uScale + pc.uTranslate, 0, 1);
}