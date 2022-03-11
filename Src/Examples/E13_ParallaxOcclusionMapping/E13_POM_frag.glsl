#version 450
#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 0)    uniform     sampler2D   SamplerNormalMap;		// [INPUT_TEXTURE_NORMAL]
layout (binding = 1)    uniform     sampler2D   SamplerDiffuseMap;		// [INPUT_TEXTURE_DIFFUSE]
layout (binding = 2)    uniform     sampler2D   SamplerAOMap;			// [INPUT_TEXTURE_AO]
layout (binding = 3)    uniform     sampler2D   SamplerGlossivessMap;	// [INPUT_TEXTURE_GLOSSINESS]
layout (binding = 4)    uniform     sampler2D   SamplerDepthMap;		// [INPUT_TEXTURE_DEPTH]

layout(location = 0) in struct 
{ 
    vec3 TangentPosition;
    vec3 TangentView;
    vec3 TangentLight;

    vec4  VertColor;
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

float Shininess = 20.9f;


//-------------------------------------------------------------------------------------------

vec2 ParallaxMapping(vec2 texCoords, vec3 viewDir)
{ 
	const float height_scale = 0.1f;

    // number of depth layers
    const float numLayers = 30;

    // calculate the size of each layer
    float layerDepth = 1.0 / numLayers;

    // depth of current layer
    float currentLayerDepth = 0.0;

    // the amount to shift the texture coordinates per layer (from vector P)
    vec2 P = viewDir.xy * height_scale; 
    vec2 deltaTexCoords = P / numLayers;
  
	// get initial values
	vec2  currentTexCoords     = texCoords;
	float currentDepthMapValue = texture(SamplerDepthMap, currentTexCoords).r;
  
	while(currentLayerDepth < currentDepthMapValue)
	{
		// shift texture coordinates along direction of P
		currentTexCoords -= deltaTexCoords;
		// get depthmap value at current texture coordinates
		currentDepthMapValue = texture(SamplerDepthMap, currentTexCoords).r;  
		// get depth of next layer
		currentLayerDepth += layerDepth;  
	}

	return currentTexCoords;
}

//-------------------------------------------------------------------------------------------

void main() 
{
	// Note This is the true Eye to Texel direction 
	const vec3 EyeDirection = normalize( In.TangentPosition - In.TangentView.xyz );

	//
	// get the parallax coordinates
	//
	vec2 texCoords	= ParallaxMapping( In.UV, EyeDirection );

	//
	// get the normal from a compress texture (either DXT5 or 3Dc/BC5)
	//
	vec3 Normal;

	//			For BC5 it used (rg)
	//			For DXT5 it uses (ag)
	Normal.xy	= (texture(SamplerNormalMap, texCoords).rg * 2.0) - 1.0;
	Normal.z	=  sqrt(1.0 - dot(Normal.xy, Normal.xy));

	//
	// Different techniques to do Lighting
	//

	// Note that the real light direction is the negative of this, but the negative is removed to speed up the equations
	vec3 LightDirection = normalize( In.TangentLight.xyz - In.TangentPosition );

	// Compute the diffuse intensity
	float DiffuseI  = max( 0, dot( Normal, LightDirection ));

	// Another way to compute specular "BLINN-PHONG" (https://learnopengl.com/Advanced-Lighting/Advanced-Lighting)
	float SpecularI  = pow( max( 0, dot(Normal, normalize( LightDirection - EyeDirection ))), Shininess);

	// Read the diffuse color
	vec4 DiffuseColor	= texture(SamplerDiffuseMap, texCoords) * In.VertColor;

	// Set the global constribution
	outFragColor.rgb  = pushConsts.AmbientLightColor.rgb * DiffuseColor.rgb * texture(SamplerAOMap, texCoords).rgb;

	// Add the contribution of this light
	outFragColor.rgb += pushConsts.LightColor.rgb * ( SpecularI.rrr *  texture(SamplerGlossivessMap, texCoords).rgb + DiffuseI.rrr * DiffuseColor.rgb );

	// Convert to gamma
	const float Gamma = pushConsts.LocalSpaceEyePos.w;
	outFragColor.a   = DiffuseColor.a;
	outFragColor.rgb = pow( outFragColor.rgb, vec3(1.0f/Gamma) );

//	outFragColor.rgb = texture(SamplerDepthMap, texCoords).rrr;
}


