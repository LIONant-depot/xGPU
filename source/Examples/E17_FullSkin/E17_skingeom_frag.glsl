#version 450
#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 0)    uniform     sampler2D   SamplerNormalMap;		// [INPUT_TEXTURE_NORMAL]
layout (binding = 1)    uniform     sampler2D   SamplerDiffuseMap;		// [INPUT_TEXTURE_DIFFUSE]
layout (binding = 2)    uniform     sampler2D   SamplerAOMap;			// [INPUT_TEXTURE_AO]
layout (binding = 3)    uniform     sampler2D   SamplerGlossivessMap;	// [INPUT_TEXTURE_GLOSSINESS]
layout (binding = 4)    uniform     sampler2D   SamplerRoughnessMap;	// [INPUT_TEXTURE_ROUGHNESS]

layout(location = 0) in struct 
{ 
    mat3  BTN;
    vec4  VertColor;
    vec4  WorldSpacePosition;
    vec2  UV; 
} In;

layout (push_constant) uniform PushConsts 
{
	vec4  LightColor;
	vec4  AmbientLightColor;
    vec4  WorldSpaceLightPos;
    vec4  WorldSpaceEyePos;
} pushConsts;

layout (location = 0)   out         vec4        outFragColor;

void main() 
{
	//
	// get the normal from a compress texture BC5
	//
	vec3 Normal;
	
	Normal	= (texture(SamplerNormalMap, In.UV).xyz * 2.0) - 1.0;
	
	// Transform the normal to world space 
	Normal = normalize(In.BTN * Normal);

	//
	// do Lighting
	//

	// Note that the real light direction is the negative of this, but the negative is removed to speed up the equations
	vec3 LightDirection = normalize( pushConsts.WorldSpaceLightPos.xyz - In.WorldSpacePosition.xyz );

	// Compute the diffuse intensity
	float DiffuseI  = max( 0, dot( Normal, LightDirection ));

	// Note This is the true Eye to Texel direction 
	vec3 EyeDirection = normalize( In.WorldSpacePosition.xyz - pushConsts.WorldSpaceEyePos.xyz );

	// Determine the power for the specular based on how rough something is
	const float Shininess = mix( 1, 100, 1 - texture( SamplerRoughnessMap, In.UV).r );

	// The old way to Compute Specular "PHONG"
	float SpecularI1  = pow( max(0, dot( reflect(LightDirection, Normal), EyeDirection )), Shininess );

	// Read the diffuse color
	vec4 DiffuseColor	= texture(SamplerDiffuseMap, In.UV) * In.VertColor;

	// Set the global constribution
	outFragColor.rgb  = pushConsts.AmbientLightColor.rgb * DiffuseColor.rgb * texture(SamplerAOMap, In.UV).rgb;

	// Add the contribution of this light
	outFragColor.rgb += pushConsts.LightColor.rgb * ( SpecularI1.rrr * texture(SamplerGlossivessMap, In.UV).rgb + DiffuseI.rrr * DiffuseColor.rgb );

	// Convert to gamma
	const float Gamma = pushConsts.WorldSpaceEyePos.w;
	outFragColor.a   = DiffuseColor.a;
	outFragColor.rgb = pow( outFragColor.rgb, vec3(1.0f/Gamma) );
}


