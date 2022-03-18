#version 450
#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 0)    uniform     sampler2D   uSamplerNormal; // [INPUT_TEXTURE_NORMAL]

layout(location = 0) in struct 
{ 
    float VertexLighting; 
    vec2  UV; 
	vec3  LocalSpaceLightPosition;
	vec3  LocalSpaceLightDir;
    vec3  TangentLightDir;
    mat3  BTN;
	vec3  LocalSpacePosition;
} In;

layout (location = 0)   out         vec4        outFragColor;

void main() 
{
	//
	// Read the Normal map texture
	//
	vec3 Normal;
	if( true )
	{
		// Normal map texture that are compress
		if( true )
		{
			// For Normal map textures compress with BC5, it uses (rg)
			Normal.rg	= texture(uSamplerNormal, In.UV).gr;
		}
		else
		{
			// For Normal map textures compress with DXT5, it uses (ag)
			Normal.rg	= texture(uSamplerNormal, In.UV).ag;
		}

		// Decode to -1 to 1 for each read element
		Normal.xy =  Normal.rg * 2.0 - 1.0;

		// Derive the final element since we only have (x,y)
		// This also forces the Normal map to be normalize
		Normal.z =  sqrt(1.0 - dot(Normal.xy, Normal.xy));
	}
	else
	{
		// Uncompress Normal Map textures (FULL 8888) RGB, A is not used
		Normal = texture(uSamplerNormal, In.UV).rgb;

		// Decode to -1 to 1 for each read element
		Normal.xyz =  Normal.rgb * 2.0 - 1.0;

		// We make sure that the Normal has a length of 1
	    Normal = normalize(Normal);
	}

	// Some Normal maps are left handed (D3D for example) Ideally we would not need to do this
	// If is better to precompute this in the texture compiler worse case
	if( true )
	{
		// Convert D3D Normal map to Vulkan/OpenGL format
		// This is the equivalent of doing Normal.g = 1 - Normal.g; before the Normal got comverted to [-1,1]
		Normal.y =  -Normal.y;	
	}

	//
	// Different techniques to do Lighting
	//

	// Vertex method
	float I1 = In.VertexLighting;

	// The standard method
	float I2 = max( 0, dot( In.BTN*Normal, normalize(In.LocalSpaceLightDir) ));

	// Fast method, minimal instructions
	float I3 = max( 0, dot( Normal,        normalize(In.TangentLightDir) ));

	// Useful for large polygons and higher acuracy 
	float I4 = max( 0, dot( In.BTN*Normal, normalize(In.LocalSpaceLightPosition - In.LocalSpacePosition) ));

	// Convert to gamma
	const float gamma = 2.2f;
	outFragColor.rgb = I2.rrr;		// Current technique in used
	outFragColor.a   = 1;
	outFragColor.rgb = pow( outFragColor.rgb, vec3(1.0f/gamma) );
}


