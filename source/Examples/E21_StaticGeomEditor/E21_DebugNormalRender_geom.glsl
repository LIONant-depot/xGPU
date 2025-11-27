#version 450
#pragma shader_stage(geometry)

layout(triangles) in;
layout(line_strip, max_vertices = 6) out;

layout(location = 0) in vec4 A[3];
layout(location = 1) in vec4 B[3];

layout(location = 0) out vec4 Color;

vec4 ColorA = vec4( 1,      1,   0, 1  );
vec4 ColorB = vec4( 0.7,  0.7,   0, 0.7);

void GenerateLine(int index)
{
    gl_Position = A[index];
    Color       = ColorA;
    EmitVertex();

    gl_Position = B[index];
    Color       = ColorB;
    EmitVertex();
    EndPrimitive();
}

void main()
{
    GenerateLine(0);
    GenerateLine(1);
    GenerateLine(2);
}