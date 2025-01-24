#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

layout(push_constant) uniform uPushConstant 
{ 
   float MipLevel;
   float    m_Padding;
   vec2  uScale; 
   vec2  uTranslate; 
   vec2  uvScale; 
   vec4  TintColor;
   vec4  ColorMask; 
   vec4  Mode;
} pc;

out gl_PerVertex { vec4 gl_Position; };
layout(location = 0) out struct 
{ 
  vec2 UV; 
} Out;

void main()
{
    Out.UV      = aUV * pc.uvScale;
    gl_Position = vec4(aPos * pc.uScale + pc.uTranslate, 0, 1);
}