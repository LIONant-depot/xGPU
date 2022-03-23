#version 450
#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

//
// The BRDF lighting equation
//
//                   F(V,H)*G(L,V,H)D(H) 
// Lighting(L,V,H) = --------------------
//                     4(NdotL)(NdotV)
//
//------------------------------------------
// N = normal           | F = Fresnel 
// V = View direction   | G = Geometry Shadowing Function (GSF)
// H = Half vector      | D = Normal Distribution Function (NDF)
//-----------------------------------------
//
// Fresnel
//-----------------------------------
// The Fresnel effect is named after the French physicist Augustin-Jean Fresnel who first described it. This effect states that the strength of reflections 
// on a surface is dependent on the viewpoint. The amount of reflection increases on surfaces viewed at a grazing angle. In order to include the Fresnel 
// effect into our shader we need to use it in several places. Firstly we need to account for the diffuse retro-reflection, and then we need to account 
// for the BRDF Fresnel effect.
//
// In order to calculate our Fresnel appropriately, we need to account for the normal incidence and grazing angles. We will use roughness below to calculate 
// the diffuse Fresnel reflection incidence that we can pass into our Fresnel function. To calculate this we use a version of the Schlick approximation of Fresnel. 
//
// The Normal Distribution Function (NDF)
//---------------------------------
// The Normal Distribution Function is one of the three key elements that make up our BRDF shader. 
// The NDF statistically describes the distribution of microfacet normals on a surface. For our uses, 
// the NDF serves as a weighted function to scale the brightness of reflections (specularity). 
// It is important to think of the NDF as a fundemental geometric property of the surface. 
//
// Geometric Shadowing Function (GSF)
//-----------------------------------
// The Geometric Shadowing Function is used to describe the attenuation of the light due to the self-shadowing behavior of microfacets. 
// This approximation models the probability that at a given point the microfacets are occluded by each other, or that light bounces on multiple microfacets. 
// During these probabilities, light loses energy before reaching the viewpoint. In order to accurately generate a GSF, one must sample roughness to determine the microfacet distribution. 
// There are several functions that do not include roughness, and while they still produce reliable results, they do not fit as many use cases as the functions that do sample roughness.
//
// The Geometric Shadowing Function is essential for BRDF energy conservation. Without the GSF, the BRDF can reflect more light energy than it receives. 
// A key part of the microfacet BRDF equation relates to the ratio between the active surface area (the area covered by surface regions that reflect light energy from L to V)
// and the total surface area of the microfaceted surface. If shadowing and masking are not accounted for, then the active area may exceed the total area, 
// which can lead to the BRDF not conserving energy, in some cases by drastic amounts.
//
// References 
// https://www.jordanstevenstechart.com/physically-based-rendering
// https://learnopengl.com/PBR/Lighting
// https://google.github.io/filament/Filament.html#table_commonmatreflectance 

layout (binding = 0)    uniform     sampler2D   SamplerNormalMap;		// [INPUT_TEXTURE_NORMAL]
layout (binding = 1)    uniform     sampler2D   SamplerAlbedoMap;		// [INPUT_TEXTURE_ALBEDO]
layout (binding = 2)    uniform     sampler2D   SamplerAOMap;			// [INPUT_TEXTURE_AO]
layout (binding = 3)    uniform     sampler2D   SamplerSpecularMap;		// [INPUT_TEXTURE_SPECULAR]
layout (binding = 4)    uniform     sampler2D   SamplerRoughnessMap;	// [INPUT_TEXTURE_ROUGHNESS]
layout (binding = 5)    uniform     sampler2D   SamplerDepthMap;		// [INPUT_TEXTURE_DEPTH]
layout (binding = 6)    uniform     sampler2D   SamplerMetalicMap;		// [INPUT_TEXTURE_METALIC]

layout(location = 0) in struct 
{ 
    vec3 TangentTexelPosition;
    vec3 TangentView;
    vec3 TangentLight;

    vec4  VertColor;
    vec2  UV; 
} In;

layout (push_constant) uniform PushConsts 
{
    mat4  L2C;
    vec4  LocalSpaceLightPos;			// LightSpaceLightPos.xyz, w = starting fall off distance in local space units
    vec4  LocalSpaceEyePos;
	vec4  AmbientLightColor;
	vec4  LightColor;
} pushConsts;

layout (location = 0)   out         vec4        outFragColor;

const float PI = 3.14159265359;

//--------------------------------------------------------------------------------------------

vec2 parallaxMapping( in const vec3 V, in const vec2 T )
{
	const float parallaxScale = 0.1f;

	//
	// Try to minimize the iterations
	//
	const float minLayers = 10;
	const float maxLayers = 15;
	float numLayers = mix(maxLayers, minLayers, abs(dot(vec3(0, 0, 1), V)));

	//
	// Steep parallax mapping 
    //

	// height of each layer
	float layerHeight = 1.0 / numLayers;
	// current depth of the layer
	float curLayerHeight = 0;
	// shift of texture coordinates for each layer
	vec2 dtex = parallaxScale * V.xy / numLayers;

	// current texture coordinates
	vec2 currentTextureCoords = T;

	// depth from heightmap
	float heightFromTexture = texture(SamplerDepthMap, currentTextureCoords).r;

	// while point is above the surface
	while(heightFromTexture > curLayerHeight)
	{
		// to the next layer
		curLayerHeight += layerHeight;
		// shift of texture coordinates
		currentTextureCoords -= dtex;
		// new depth from heightmap
		heightFromTexture = texture(SamplerDepthMap, currentTextureCoords).r;
	}

	//
	// Parallax Occlusion Mapping
	//

	// previous texture coordinates
	vec2 prevTCoords = currentTextureCoords + dtex;

	// heights for linear interpolation
	float nextH = heightFromTexture - curLayerHeight;
	float prevH = texture(SamplerDepthMap, prevTCoords).r - curLayerHeight + layerHeight;

	// proportions for linear interpolation
	float weight = nextH / (nextH - prevH);

	// interpolation of texture coordinates
	vec2 finalTexCoords = prevTCoords * weight + currentTextureCoords * (1.0-weight);

	// return result
	return finalTexCoords;
}

//-------------------------------------------------------------------------------------------

vec3 loadNormal( in const vec2 texCoords, in const vec3 viewDirection )
{
	vec3 Normal;
	// For BC5 it used (rg)
	Normal.xy	= (texture(SamplerNormalMap, texCoords).gr * 2.0) - 1.0;
	
	// Derive the final element and normalize 
	Normal.z =  sqrt(1.0 - dot(Normal.xy, Normal.xy));

	// Check if the normal is looking at the eye make it smaller so it wont saturate too much
	float shiftAmount = dot(Normal, viewDirection );
	Normal = shiftAmount < 0.0f ? Normal + viewDirection * (-shiftAmount + 0.0001 /*1e-5f*/) : Normal;
	return Normal;
}

//-------------------------------------------------------------------------------------------
// helper functions
//-------------------------------------------------------------------------------------------
float MixFunction( float i, float j, float x ) 
{
	 return  j * x + i * (1.0 - x);
} 

vec2 MixFunction( vec2 i, vec2 j, float x )
{
	 return  j.xy * x + i.xy * (1.0f - x);
}   

vec3 MixFunction( vec3 i, vec3 j, float x )
{
	 return  j * x + i * (1.0f - x);
}   

vec4 MixFunction( vec4 i, vec4 j, float x )
{
	 return j.xyzw * x + i.xyzw * (1.0f - x);
} 

float sqr( float x )
{
	return x*x; 
}

float sqr( vec3 x )
{
	return dot(x,x); 
}

//-------------------------------------------------------------------------------------------

float SchlickFresnel(float i)
{
    const float x  = clamp(1.0-i, 0.0, 1.0);
    const float x2 = x*x;
    return x2*x2*x;
}

//-------------------------------------------------------------------------------------------

vec3 FresnelLerp( vec3 x, vec3 y, float d )
{
	const float t = SchlickFresnel(d);	
	return mix(x, y, t);
}

//-------------------------------------------------------------------------------------------
//normal incidence reflection calculation
float F0( float NdotL, float NdotV, float LdotH, float roughness )
{
	// Diffuse fresnel
    const float  FresnelLight		= SchlickFresnel(NdotL); 
    const float  FresnelView		= SchlickFresnel(NdotV);
    const float  FresnelDiffuse90	= 0.5 + 2.0 * LdotH*LdotH * roughness;
    return MixFunction(1, FresnelDiffuse90, FresnelLight) * MixFunction(1, FresnelDiffuse90, FresnelView);
}

//--------------------------------------------------------------------------------------------

float SchlickIORFresnelFunction(float ior,float LdotH)
{
    float f0 = pow( (ior-1)/(ior+1), 2);
    return f0 +  (1 - f0) * SchlickFresnel(LdotH);
}

//--------------------------------------------------------------------------------------------

vec3 SphericalGaussianFresnelFunction( float LdotH, vec3 SpecularColor )
{	
	const float power = ((-5.55473 * LdotH) - 6.98316) * LdotH;
	return SpecularColor + (1 - SpecularColor) * pow(2,power);
}

//--------------------------------------------------------------------------------------------

float GGXNormalDistribution(float roughness, float NdotH)
{
    const float roughnessSqr	= roughness*roughness;
    const float NdotHSqr		= NdotH*NdotH;
    const float TanNdotHSqr		= (1-NdotHSqr) / NdotHSqr;
    return (1.0/PI) * sqr(roughness / (NdotHSqr * (roughnessSqr + TanNdotHSqr)));
}

//--------------------------------------------------------------------------------------------

float BlinnPhongNormalDistribution(float NdotH, float specularpower, float speculargloss)
{
    const float Distribution = pow(NdotH,speculargloss) * specularpower;
    return Distribution * (2+specularpower) / (2*PI);
}

//--------------------------------------------------------------------------------------------

float ImplicitGeometricShadowingFunction (float NdotL, float NdotV)
{
	float Gs =  (NdotL*NdotV);
	return Gs;
}

//--------------------------------------------------------------------------------------------

float SchlickBeckmanGeometricShadowingFunction (float NdotL, float NdotV,float roughness)
{
    const float roughnessSqr = roughness*roughness;
    const float k			 = roughnessSqr * 0.797884560802865;
    const float SmithL		 = (NdotL)/ (NdotL * (1- k) + k);
    const float SmithV		 = (NdotV)/ (NdotV * (1- k) + k);
	const float Gs			 =  (SmithL * SmithV);
	return Gs;
}

//--------------------------------------------------------------------------------------------

void main() 
{
	// Note This is the true Eye to Texel direction 
	const vec3 EyeDirection = normalize( In.TangentTexelPosition - In.TangentView.xyz );

	//
	// Texture calculations
	//
	const vec2  texCoords				= parallaxMapping( EyeDirection, In.UV );
	const float Roughness               = 0.6;//texture( SamplerRoughnessMap, texCoords).r;
	const float Metallic                = texture( SamplerMetalicMap, texCoords).r;
	const vec3  Albedo                  = texture( SamplerAlbedoMap, texCoords).rgb;
	const vec3  SpecularColor           = vec3(1);//texture( SamplerSpecularMap, texCoords).rgb;

	// get the basics
	const vec3  lightDirection			= normalize( In.TangentLight.xyz - In.TangentTexelPosition );
	const vec3  viewDirection			= normalize( In.TangentView.xyz  - In.TangentTexelPosition );
	const vec3  normalDirection  		= loadNormal(texCoords, viewDirection);

	// vector calculations
	const vec3  lightReflectDirection	= reflect( -lightDirection, normalDirection );
	const vec3  viewReflectDirection    = normalize(reflect( -viewDirection, normalDirection ));
	const float NdotL					= max(0.0,dot( normalDirection, lightDirection ));
	const vec3  halfDirection			= normalize(viewDirection+lightDirection); 
	const float NdotH					= max(0.0,dot( normalDirection, halfDirection));
	const float NdotV					= max(0.0,dot( normalDirection, viewDirection));
	const float VdotH					= max(0.0,dot( viewDirection, halfDirection));
	const float LdotH					= max(0.0,dot(lightDirection, halfDirection)); 
	const float LdotV					= max(0.0,dot(lightDirection, viewDirection)); 
	const float RdotV					= max(0.0,dot( lightReflectDirection, viewDirection ));

	// Light calculations
	const float attenuation				= min( 1, pushConsts.LocalSpaceLightPos.w / sqr(In.TangentLight.xyz - In.TangentTexelPosition.rgb));
	const vec3  attenColor				= attenuation * pushConsts.LightColor.rgb;// * 4.3;

	// diffuse color calculations
	const float roughness				= sqr( Roughness * Roughness );
	const float f0						= F0(NdotL, NdotV, LdotH, roughness);
    const vec3  diffuseColor			= Albedo.rgb * (1.0 - Metallic) * f0;

	// Specular calculations
	const vec3	specColor				= mix( SpecularColor.rgb, Albedo.rgb, Metallic * 0.5 );
	const float Ior						= 3.2; // Range(1,4))

	// Specular BRDF
	vec3  FresnelFunction				= specColor;
	vec3  SpecularDistribution			= specColor;
	float GeometricShadow				= 1;

	// Specular distribution
	if(false)	SpecularDistribution   *= BlinnPhongNormalDistribution( NdotH, 1 - Roughness,  max(1,(1 - Roughness) * 40) );
	else		SpecularDistribution   *= GGXNormalDistribution(roughness, NdotH);

	if(false)	GeometricShadow		   *= ImplicitGeometricShadowingFunction		(NdotL, NdotV);
	else		GeometricShadow		   *= SchlickBeckmanGeometricShadowingFunction(NdotL, NdotV, roughness);

	if(false)	FresnelFunction		   *= SphericalGaussianFresnelFunction(LdotH, specColor);
	else        FresnelFunction		   *= SchlickIORFresnelFunction(Ior, LdotH);

	// For indirect specularity
	/*
	const vec3 indirectSpecular     = vec3(1);
	const float grazingTerm         = clamp( roughness + Metallic, 0, 1 );
	const vec3  IndirectSpecularity =  indirectSpecular * FresnelLerp( specColor.rgb, grazingTerm.rrr, NdotV) * max(0.15,Metallic) * (1-roughness*roughness* roughness);
	*/

	// PBR
	const vec3  specularity			  = min( vec3(31), (SpecularDistribution * FresnelFunction * GeometricShadow) / (4 * NdotL * NdotV) );
	const vec3  lightingModel		  = NdotL * ( diffuseColor.xyz + specularity.xyz /*+ IndirectSpecularity.xyz*/ );
	const vec3  finalModel     		  = pow( lightingModel * attenColor, vec3(1.2));

	//
	// Set the global constribution
	//
	const vec3  Ambient			      = pushConsts.AmbientLightColor.rgb * Albedo.rgb * texture(SamplerAOMap, texCoords).rgb;

	outFragColor.rgb = Ambient + finalModel;
	
	//
	// Tone map from (HDR) to (LDR) before gamma correction
	// Has a blue tint 
	//
	outFragColor.rgb = outFragColor.rgb / ( outFragColor.rgb + vec3(1.0, 1.0, 0.9) );

	//
	// Convert to gamma
	//
	const float Gamma = pushConsts.LocalSpaceEyePos.w;
	outFragColor.a    = 1;//DiffuseColor.a;
	outFragColor.rgb  = pow( outFragColor.rgb, vec3(1.0f/Gamma) );
}
