#version 450
#extension GL_ARB_separate_shader_objects  : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 0)    uniform     sampler2D   uSamplerColor; // [INPUT_TEXTURE]

layout(location = 0) in struct { vec4 Color; vec2 UV; } In;

layout (location = 0)   out         vec4        outFragColor;

void main() 
{
    vec4 Color = In.Color * texture( uSamplerColor, In.UV );

	// Convert to gamma
	const float gamma = 2.2f;
	Color.rgb = pow(Color.rgb, vec3(0.454545f));

	// Output color
	outFragColor = Color;
}


