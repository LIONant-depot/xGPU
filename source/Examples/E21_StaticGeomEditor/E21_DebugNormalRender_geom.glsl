#version 450
#pragma shader_stage(geometry)

layout(triangles) in;
layout(line_strip, max_vertices = 6) out;

layout(location = 0) in vec4 A[3];
layout(location = 1) in vec4 B[3];
layout(location = 2) in vec4 AxisColor[3];

layout(location = 0) out vec4 Color;

void GenerateLine(int index)
{
    gl_Position = A[index];
    Color       = AxisColor[index];
    EmitVertex();

    gl_Position = B[index];
    Color       = AxisColor[index]*0.7;
    EmitVertex();
    EndPrimitive();
}

void main()
{
    GenerateLine(0);
    GenerateLine(1);
    GenerateLine(2);
}