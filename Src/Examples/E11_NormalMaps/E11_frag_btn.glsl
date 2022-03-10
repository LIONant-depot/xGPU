#version 450
#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 0)    uniform     sampler2D   uSamplerColor; // [INPUT_TEXTURE]

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
	vec3 normal = texture(uSamplerColor, In.UV).rgb;

	normal.y    = 1 - normal.y;	// Convert D3D normal map to Vulkan/OpenGL format
    normal      = normalize(normal * 2.0 - 1.0);   

	//
	// Different techniques to do Lighting
	//

	// Vertex method
	float I1 = In.VertexLighting;

	// The standard method
	float I2 = max( 0, dot( In.BTN*normal, normalize(In.LocalSpaceLightDir) ));

	// Fast method, minimal instructions
	float I3 = max( 0, dot( normal,        normalize(In.TangentLightDir) ));

	// Useful for large polygons and higher acuracy 
	float I4 = max( 0, dot( In.BTN*normal, normalize(In.LocalSpaceLightPosition - In.LocalSpacePosition) ));

	// Convert to gamma
	const float gamma = 2.2f;
	outFragColor.rgb = I4.rrr;		// Current technique in used
	outFragColor.a   = 1;
	outFragColor.rgb = pow( outFragColor.rgb, vec3(1.0f/gamma) );
}


