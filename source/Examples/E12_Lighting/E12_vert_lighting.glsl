#version 450

#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;        //[INPUT_POSITION]
layout (location = 1) in vec2 inUV;         //[INPUT_UVS]
layout (location = 2) in vec3 inTangent;    //[INPUT_TANGENT]
layout (location = 3) in vec3 inNormal;     //[INPUT_NORMAL]
layout (location = 4) in vec4 inColor;      //[INPUT_COLOR]

layout (push_constant) uniform PushConsts 
{
    mat4  L2C;
    vec4  LocalSpaceLightPos;
    vec4  LocalSpaceEyePos;
	vec4  AmbientLightColor;
	vec4  LightColor;
} pushConsts;

layout(location = 0) out struct 
{ 
    mat3  BTN;
    vec4  VertColor;
	vec3  LocalSpacePosition;
    vec2  UV; 
} Out;

void main() 
{
	const float Gamma = pushConsts.LocalSpaceEyePos.w;

    // Decompress the binormal
    vec3 Binormal               = normalize(cross(inTangent, inNormal));

    // Compute lighting information
    Out.BTN                     = mat3(inTangent, Binormal, inNormal);
    Out.LocalSpacePosition      = inPos;
    Out.VertColor               = pow( inColor, Gamma.rrrr );    // SRGB to RGB
    Out.UV                      = inUV;

    gl_Position                 = pushConsts.L2C * vec4(inPos.xyz, 1.0);
}
