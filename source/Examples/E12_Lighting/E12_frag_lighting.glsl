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
	vec3  LocalSpacePosition;
    vec2  UV; 
} In;

layout (push_constant) uniform PushConsts 
{
    mat4  L2C;
    vec4  LocalSpaceLightPos;
    vec4  LocalSpaceEyePos;
	vec4  AmbientLightColor;
	vec4  LightColor;
} pushConsts;

layout (location = 0)   out         vec4        outFragColor;

void main() 
{
	//
	// get the normal from a compress texture BC5
	//
	vec3 Normal;
	// For BC5 it used (rg)
	Normal.xy	= (texture(SamplerNormalMap, In.UV).gr * 2.0) - 1.0;
	
	// Derive the final element
	Normal.z =  sqrt(1.0 - dot(Normal.xy, Normal.xy));

	// Transform the normal to from tangent space to local space
	Normal = In.BTN * Normal;

	//
	// Different techniques to do Lighting
	//

	// Note that the real light direction is the negative of this, but the negative is removed to speed up the equations
	vec3 LightDirection = normalize( pushConsts.LocalSpaceLightPos.xyz - In.LocalSpacePosition );

	// Compute the diffuse intensity
	float DiffuseI  = max( 0, dot( Normal, LightDirection ));

	// Note This is the true Eye to Texel direction 
	vec3 EyeDirection = normalize( In.LocalSpacePosition - pushConsts.LocalSpaceEyePos.xyz );

	// Determine the power for the specular based on how rough something is
	const float Shininess = mix( 1, 100, 1 - texture( SamplerRoughnessMap, In.UV).r );

	// The old way to Compute Specular "PHONG"
	// Reflection == I - 2.0 * dot(N, I) * N // Where N = Normal, I = LightDirectioh
	float SpecularI1  = pow( max(0, dot( reflect(LightDirection, Normal), EyeDirection )), Shininess );

	// Another way to compute specular "BLINN-PHONG" (https://learnopengl.com/Advanced-Lighting/Advanced-Lighting)
	float SpecularI2  = pow( max( 0, dot(Normal, normalize( LightDirection - EyeDirection ))), Shininess );

	// Read the diffuse color
	vec4 DiffuseColor	= texture(SamplerDiffuseMap, In.UV) * In.VertColor;

	// Set the global constribution
	outFragColor.rgb  = pushConsts.AmbientLightColor.rgb * DiffuseColor.rgb * texture(SamplerAOMap, In.UV).rgb;

	// Add the contribution of this light
	outFragColor.rgb += pushConsts.LightColor.rgb * ( SpecularI2.rrr * texture(SamplerGlossivessMap, In.UV).rgb + DiffuseI.rrr * DiffuseColor.rgb );

	// Convert to gamma
	const float Gamma = pushConsts.LocalSpaceEyePos.w;
	outFragColor.a   = DiffuseColor.a;
	outFragColor.rgb = pow( outFragColor.rgb, vec3(1.0f/Gamma) );
}


