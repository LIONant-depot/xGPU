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
    mat4 L2C;
    vec3 LocalSpaceLightPos;
} pushConsts;

layout(location = 0) out struct 
{ 
    float VertexLighting; 


    vec2  UV; 
    vec3  LocalSpaceLightPosition;
	vec3  LocalSpaceLightDir;
    vec3  TangentLightDir;
    mat3  BTN;
    vec3  LocalSpacePosition;
} Out;

void main() 
{
    // Compute lighting information
    Out.LocalSpaceLightPosition = pushConsts.LocalSpaceLightPos;

    //-------------------------------------------------------------------------------
    Out.LocalSpaceLightDir      = normalize( pushConsts.LocalSpaceLightPos - inPos );
    Out.VertexLighting          = max( 0, dot( inNormal, Out.LocalSpaceLightDir ));
    //-------------------------------------------------------------------------------

    Out.UV                      = inUV;

    Out.BTN                     = mat3( inTangent, inBinormal, inNormal);
    Out.TangentLightDir         = transpose(Out.BTN) * Out.LocalSpaceLightDir;

    Out.LocalSpacePosition      = inPos;

    gl_Position                 = pushConsts.L2C * vec4(inPos.xyz, 1.0);
}
