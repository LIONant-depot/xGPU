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
	vec4  AmbientLightColor;
	vec4  LightColor;
    vec4  LocalSpaceEyePos;
} pushConsts;

layout(location = 0) out struct 
{ 
    vec3 TangentTexelPosition;
    vec3 TangentView;
    vec3 TangentLight;

    vec4  VertColor;
    vec2  UV; 

} Out;

void main() 
{
    // Decompress the binormal
    vec3 Binormal               = normalize(cross(inTangent,inNormal));

    // Build the TBN
    mat3 BTN_Inv                = transpose(mat3(inTangent, Binormal, inNormal));

    // Transform to tangent space
    Out.TangentTexelPosition    = BTN_Inv * inPos.xyz;
    Out.TangentView             = BTN_Inv * pushConsts.LocalSpaceEyePos.xyz;
    Out.TangentLight            = BTN_Inv * pushConsts.LocalSpaceLightPos.xyz;

    // Convert to linear
	const float Gamma           = pushConsts.LocalSpaceEyePos.w;
    Out.VertColor               = pow( inColor, Gamma.rrrr );    // SRGB to RGB

    // Send over the UVs
    Out.UV                      = inUV;

    gl_Position                 = pushConsts.L2C * vec4(inPos.xyz, 1.0);
}
