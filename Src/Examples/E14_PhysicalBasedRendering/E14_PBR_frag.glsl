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
    vec4  LocalSpaceLightPos;
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

vec3 loadNormal( in const vec2 texCoords )
{
	vec3 Normal;
	// For BC5 it used (rg)
	Normal.xy	= (texture(SamplerNormalMap, texCoords).gr * 2.0) - 1.0;
	
	// Derive the final element and normalize 
	Normal.z =  sqrt(1.0 - dot(Normal.xy, Normal.xy));

	return Normal;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef ASDSA

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
	const float Roughness               = 0.66;//texture( SamplerRoughnessMap, texCoords).r;
	const float Metallic                = texture( SamplerMetalicMap, texCoords).r;
	const vec3  Albedo                  = texture( SamplerAlbedoMap, texCoords).rgb;
	const vec3  SpecularColor           = vec3(1);//texture( SamplerSpecularMap, texCoords).rgb;

	// get the basics
	const vec3  normalDirection  		= loadNormal(texCoords);
	const vec3  lightDirection			= normalize( In.TangentLight.xyz - In.TangentTexelPosition );
	const vec3  viewDirection			= normalize( In.TangentView.xyz  - In.TangentTexelPosition );

	// vector calculations
	const vec3  lightReflectDirection	= reflect( -lightDirection, normalDirection );
	const vec3  viewReflectDirection    = normalize(reflect( -viewDirection, normalDirection ));
	const float NdotL					= max(0.0, dot( normalDirection, lightDirection ));
	const vec3  halfDirection			= normalize(viewDirection+lightDirection); 
	const float NdotH					= max(0.0,dot( normalDirection, halfDirection));
	const float NdotV					= max(0.0,dot( normalDirection, viewDirection));
	const float VdotH					= max(0.0,dot( viewDirection, halfDirection));
	const float LdotH					= max(0.0,dot(lightDirection, halfDirection)); 
	const float LdotV					= max(0.0,dot(lightDirection, viewDirection)); 
	const float RdotV					= max(0.0, dot( lightReflectDirection, viewDirection ));

	// Light calculations
	const float attenuation				= 1;//1/sqr(In.TangentLight.xyz - In.TangentTexelPosition.rgb);
	const vec3  attenColor				= attenuation * pushConsts.LightColor.rgb;

	// diffuse color calculations
	const float roughness				= sqr(Roughness * Roughness);
	const float f0						= F0(NdotL, NdotV, LdotH, roughness);
    const vec3  diffuseColor			= Albedo.rgb * (1.0 - Metallic) * f0;

	// Specular calculations
	const vec3	specColor				= mix( SpecularColor.rgb, Albedo.rgb, Metallic * 0.5 );
	const float Ior						= 1.7; // Range(1,4))

	// Specular BRDF
	vec3  FresnelFunction				= specColor;
	vec3  SpecularDistribution			= specColor;
	float GeometricShadow				= 1;

	// Specular distribution
	if(false)	SpecularDistribution *= BlinnPhongNormalDistribution( NdotH, 1 - Roughness,  max(1,(1 - Roughness) * 40) );
	else		SpecularDistribution *= GGXNormalDistribution(roughness, NdotH);

	if(false)	GeometricShadow		 *= ImplicitGeometricShadowingFunction		(NdotL, NdotV);
	else		GeometricShadow		 *= SchlickBeckmanGeometricShadowingFunction(NdotL, NdotV, roughness);

	if(false)	FresnelFunction		 *= SphericalGaussianFresnelFunction(LdotH, specColor);
	else        FresnelFunction		 *= SchlickIORFresnelFunction(Ior, LdotH);

	// PBR
	const vec3  specularity		= min( diffuseColor*2, (SpecularDistribution * FresnelFunction * GeometricShadow) / (4 * NdotL * NdotV) );
	const vec3  lightingModel	= NdotL * ( diffuseColor.xyz + specularity.xyz );
	const vec3  finalModel     	= lightingModel * attenColor;

	//
	// Set the global constribution
	//
	vec3 Ambient = pushConsts.AmbientLightColor.rgb * Albedo.rgb * texture(SamplerAOMap, texCoords).rgb;

	// Add the contribution of this light
	outFragColor.rgb = Ambient + finalModel.rgb;

	//
	// Tone map from (HDR) to (LDR) before gamma correction
	//
	outFragColor.rgb = outFragColor.rgb / ( outFragColor.rgb + vec3(1, 1, 0.9) );

	//
	// Convert to gamma
	//
	const float Gamma = pushConsts.LocalSpaceEyePos.w;
	outFragColor.a    = 1;//DiffuseColor.a;
	outFragColor.rgb  = pow( outFragColor.rgb, vec3(1.0f/Gamma) );
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#else
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------

vec3 fresnelSchlick( in const float cosTheta, in const vec3 F0, in const float F90 )
{
	const float f = pow( clamp(1.0 - cosTheta, 0.0, 1.0), 5.0 );
    return F0 + (F90 - F0) * f;
}  

//-------------------------------------------------------------------------------------------

float NormalDistributionGGX( in const vec3 Normal, in const vec3 HalfV, in const float Roughness )
{
    const float a      = Roughness*Roughness;
    const float a2     = a*a;
    const float NdotH  = max(dot(Normal, HalfV), 0.0);
    const float NdotH2 = NdotH*NdotH;
	
    const float num   = a2;
    float		denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
	
    return num / denom;
}

//-------------------------------------------------------------------------------------------

float GeometryShadowingSchlickBeckmanGGX( in const float NdotV, in const float Roughness )
{
	return (2 * NdotV)/ (NdotV + sqrt((Roughness*Roughness) + ( 1-Roughness) * (NdotV*NdotV)));

    const float r = (Roughness + 1.0);
    const float k = (r*r) / 8.0;

    const float num   = NdotV;
    const float denom = NdotV * (1.0 - k) + k;
	
    return num / denom;
}

//-------------------------------------------------------------------------------------------

float GeometryShadowingSmith( in const vec3 Normal, in const vec3 ViewDir, in const vec3 LightDir, in const float Roughness )
{
    const float NdotV = max(dot(Normal, ViewDir), 0.0);
    const float NdotL = max(dot(Normal, LightDir), 0.0);
    const float ggx2  = GeometryShadowingSchlickBeckmanGGX(NdotV, Roughness);
    const float ggx1  = GeometryShadowingSchlickBeckmanGGX(NdotL, Roughness);
	
    return ggx1 * ggx2;
}

//-------------------------------------------------------------------------------------------

void main() 
{
	// Note This is the true Eye to Texel direction 
	const vec3 EyeDirection = normalize( In.TangentTexelPosition - In.TangentView.xyz );

	//
	// get the parallax coordinates
	//
	const vec2 texCoords     = parallaxMapping( EyeDirection, In.UV );
	const vec3 Normal        = loadNormal(texCoords);
	const vec3 ViewDirection = -EyeDirection;


	const float Roughness                = 0.5;//max( 0.01, texture( SamplerRoughnessMap, texCoords).r );//*max( 0.01, texture( SamplerRoughnessMap, texCoords).r );
	const float Metalic                  = texture( SamplerMetalicMap, texCoords).r;
	const vec3  Albedo                   = texture( SamplerAlbedoMap, texCoords).rgb;

	//
	// Compute all the light information
	//
	const vec3  LightVector              = In.TangentLight.xyz - In.TangentTexelPosition;
	const float LightDistanceAttenuation = inversesqrt(dot(LightVector,LightVector));
	const vec3  LightDirection           = LightVector * LightDistanceAttenuation;
	const vec3  LightRadiance            = pushConsts.LightColor.rgb * 1;//LightDistanceAttenuation;
	const vec3  HalfV                    = normalize( LightDirection + ViewDirection );

	// The Fresnel-Schlick approximation expects a F0 parameter which is known as the surface reflection at zero incidence or 
	// how much the surface reflects if looking directly at the surface. The F0 varies per material and is tinted on metals 
	// as we find in large material databases. In the PBR metallic workflow we make the simplifying assumption that most dielectric 
	// surfaces look visually correct with a constant F0 of 0.04, while we do specify F0 for metallic surfaces as then given by the albedo value. 

	// To see possible reflectance values check: https://google.github.io/filament/Filament.html#table_commonmatreflectance
	const float Reflectance = 0.2;
	const vec3  F0			= mix( pushConsts.LightColor.rgb * vec3(0.16 * Reflectance*Reflectance), Albedo, Metalic );
	const vec3  Freshnel	= fresnelSchlick( max(dot(HalfV, ViewDirection), 0.0), F0, 1 ); 

	//
	// cook-torrance brdf
	//
	const float NDF			= NormalDistributionGGX( Normal, HalfV, Roughness ); 
	const float GSF			= GeometryShadowingSmith( Normal, ViewDirection, LightDirection, Roughness );
	const vec3  numerator   = NDF * GSF * Freshnel;
	const float denominator = 4.0 * max(dot(Normal, ViewDirection), 0.0) * max(dot(Normal, LightDirection), 0.0) + 0.0001;
	const vec3  specular    = numerator / denominator;

	// add to outgoing radiance Lo
	const float	NdotL    	= max(dot(Normal, LightDirection), 0.0);
	const vec3  Fd          = Albedo / PI;
	const vec3	kD			= (vec3(1.0) - Freshnel) * (1.0 - Metalic);
	const vec3	Lo			= (kD * Fd + specular) * LightRadiance * NdotL;

	// Set the global constribution
	vec3 Ambient = pushConsts.AmbientLightColor.rgb * Albedo.rgb * texture(SamplerAOMap, texCoords).rgb;

	// Add the contribution of this light
	outFragColor.rgb = Ambient + Lo;

	//
	// Tone map from (HDR) to (LDR) before gamma correction
	//
	outFragColor.rgb = outFragColor.rgb / ( outFragColor.rgb + vec3(1.0) );

	//
	// Convert to gamma
	//
	const float Gamma = pushConsts.LocalSpaceEyePos.w;
	outFragColor.a    = 1;//DiffuseColor.a;
	outFragColor.rgb  = pow( outFragColor.rgb, vec3(1.0f/Gamma) );
}
#endif