#version 450
#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 0)    uniform     sampler2D   SamplerNormalMap;	// [INPUT_TEXTURE_NORMAL]
layout (binding = 1)    uniform     sampler2D   SamplerDiffuseMap;	// [INPUT_TEXTURE_DIFFUSE]

layout(location = 0) in struct 
{ 
    mat3  BTN;
	vec3  LocalSpacePosition;
    vec2  UV; 
} In;

layout (push_constant) uniform PushConsts 
{
    mat4 L2C;
    vec4 LocalSpaceLightPos;
    vec4 LocalSpaceEyePos;
} pushConsts;


layout (location = 0)   out         vec4        outFragColor;

vec3  GlobalLightColor = { 0.0f, 0.0f, 0.0f };
float Shininess        = 20.9f;

void main() 
{
	vec3 Normal			= In.BTN * normalize( texture(SamplerNormalMap, In.UV).rgb * 2.0 - 1.0);
	vec4 DiffuseColor	= texture(SamplerDiffuseMap, In.UV);

	//
	// Different techniques to do Lighting
	//

	// Note that the real light direction is the negative of this, but the negative is removed to speed up the equations
	vec3 LightDirection = normalize( pushConsts.LocalSpaceLightPos.xyz - In.LocalSpacePosition );

	// Compute the diffuse intensity
	float DiffuseI  = max( 0, dot( Normal, LightDirection ));

	// Note This is the true Eye to Texel direction 
	vec3 EyeDirection = normalize( In.LocalSpacePosition - pushConsts.LocalSpaceEyePos.xyz );

	// The old way to Compute Specular "PHONG"
	// Reflection == I - 2.0 * dot(N, I) * N // Where N = Normal, I = LightDirectioh
	float SpecularI1   = pow( max(0, dot( reflect(LightDirection, Normal), EyeDirection )), Shininess );

	// Another way to compute specular "BLINN-PHONG" (https://learnopengl.com/Advanced-Lighting/Advanced-Lighting)
	float SpecularI2   = pow( max( 0, dot(Normal, normalize( LightDirection - EyeDirection ))), Shininess);

	//SpecularI1   = max( 0,dot( In.LocalSpaceEyePos, In.LocalSpaceLightPosition ));

	// Build the final pixel color
	outFragColor.rgb = 0.5*SpecularI2.rrr + 0.5*DiffuseI.rrr * DiffuseColor.rgb + GlobalLightColor;

	// Convert to gamma
	const float gamma = 2.2f;
	outFragColor.a   = 1;
	outFragColor.rgb = pow( outFragColor.rgb, vec3(1.0f/gamma) );
}


