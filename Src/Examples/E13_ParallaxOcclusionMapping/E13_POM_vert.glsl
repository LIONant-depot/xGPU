#version 450

#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;        //[INPUT_POSITION]
layout (location = 1) in vec3 inBinormal;   //[INPUT_BINORMAL]
layout (location = 2) in vec3 inTangent;    //[INPUT_TANGENT]
layout (location = 3) in vec3 inNormal;     //[INPUT_NORMAL]
layout (location = 4) in vec2 inUV;         //[INPUT_UVS]
layout (location = 5) in vec4 inColor;      //[INPUT_COLOR]

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
    vec3 TangentPosition;
    vec3 TangentView;
    vec3 TangentLight;

    vec4  VertColor;
    vec2  UV; 

} Out;

void main() 
{
    mat3 BTN                    = mat3(inTangent, inBinormal, inNormal);
    Out.TangentPosition         = transpose(BTN) * inPos.xyz;
    Out.TangentView             = transpose(BTN) * pushConsts.LocalSpaceEyePos.xyz;
    Out.TangentLight            = transpose(BTN) * pushConsts.LocalSpaceLightPos.xyz;

	const float Gamma           = pushConsts.LocalSpaceEyePos.w;
    Out.VertColor               = pow( inColor, Gamma.rrrr );    // SRGB to RGB
    Out.UV                      = inUV;

    gl_Position                 = pushConsts.L2C * vec4(inPos.xyz, 1.0);
}
