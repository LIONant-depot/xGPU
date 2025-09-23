#version 450

layout (set = 0, binding = 0) uniform sampler2D SamplerShadowMap;        // [INPUT_TEXTURE_SHADOW]
layout (set = 0, binding = 1) uniform sampler2D SamplerAlbedoMap;        // [INPUT_TEXTURE_ALBEDO]

layout(location = 0) in struct 
{ 
    vec4  ShadowmapPos;
    vec4  VertColor;
    vec2  UV; 
} i;

layout (push_constant) uniform _pc
{
	int  m_DebugMode;
} pc;

layout (location = 0) out vec4 outFragColor;

//----------------------------------------------------------------------------------------

int isqr( int a ) { return a*a; }

//----------------------------------------------------------------------------------------

float SampleShadowTexture( in const vec4 Coord, in const vec2 off )
{
	float dist = texture( SamplerShadowMap, Coord.xy + off ).r;
	return ( Coord.w > 0.0 && dist < Coord.z ) ? 0.0f : 1.0f;
}

//----------------------------------------------------------------------------------------

float ShadowNoPCF( in const vec4 UVProjection )
{
	float Shadow = 1.0;	
	if ( UVProjection.z > -1.0 && UVProjection.z < 1.0 ) 
	{
		Shadow = SampleShadowTexture( UVProjection, vec2(0) );
	}
	return Shadow;
}

//----------------------------------------------------------------------------------------

float ShadowPCF( in const vec4 UVProjection )
{
	float Shadow = 1.0;
	if ( UVProjection.z > -1.0 && UVProjection.z < 1.0 ) 
	{
		const float scale			= 1.5;
		const vec2  TexelSize 		= scale / textureSize(SamplerShadowMap, 0);
		const int	SampleRange		= 1;
		const int	SampleTotal		= isqr(1 + 2 * SampleRange);
		
		float		ShadowAcc = 0;
		for (int x = -SampleRange; x <= SampleRange; x++)
		{
			for (int y = -SampleRange; y <= SampleRange; y++)
			{
				ShadowAcc += SampleShadowTexture( UVProjection, vec2(TexelSize.x*x, TexelSize.y*y));
			}
		}

		Shadow = ShadowAcc / SampleTotal;
	}

	return Shadow;
}

//----------------------------------------------------------------------------------------

void main()
{
    const vec3	AmbientLight = vec3(0.1);
    const float Shadow		 = (true) ? ShadowPCF(i.ShadowmapPos / i.ShadowmapPos.w) : ShadowNoPCF(i.ShadowmapPos / i.ShadowmapPos.w);
	const vec3  FinalColor   = (AmbientLight + Shadow * i.VertColor.rgb) * texture( SamplerAlbedoMap, i.UV.xy ).rgb;

	//
	// Convert to gamma + Debug rendering
	//
	vec2 uv = (i.ShadowmapPos.xy / i.ShadowmapPos.w);
	if( pc.m_DebugMode == 0)
	{
		// Normal light rendering with shadows 
		outFragColor.rgb  = pow( FinalColor.rgb, vec3(1.0f/2.2f) );
	}
	else if( pc.m_DebugMode == 1)
	{
		// If the texel is outside the shadow texture then just render normally
		if (uv.x < 0 || uv.y < 0 || uv.x > 1 || uv.y > 1 || i.ShadowmapPos.w < 0)
		{
			// Normal light rendering with shadows 
			outFragColor.rgb  = pow( FinalColor.rgb, vec3(1.0f/2.2f) );
		}
		else
		{
			// Debug Mode
			outFragColor.rgb  = (texture( SamplerShadowMap, uv ).rrr + pow( FinalColor.rgb, vec3(1.0f/2.2f) ))*0.5f;
		}
	}
	else
	{
		// Render the shadow texture only
		if (uv.x < 0 || uv.y < 0 || uv.x > 1 || uv.y > 1 || i.ShadowmapPos.w < 0)
		{
			discard;
		}
		else
		{
			// Debug Mode
			outFragColor.rgb  = texture( SamplerShadowMap, uv ).rrr;
		}
	}

	outFragColor.a    = i.VertColor.a;
}
