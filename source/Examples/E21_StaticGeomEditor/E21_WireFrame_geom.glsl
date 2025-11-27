#version 450
#pragma shader_stage(geometry)

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 0) in vec4 vWorldPosition[3];
layout(location = 1) in vec4 vClipPosition[3];

layout(location = 0) out vec4 gWorldPosition;
layout(location = 1) out vec4 gDist;

const float uWireThickness = 1;

void main() 
{
    vec2 p0 = vClipPosition[0].xy / vClipPosition[0].w;
    vec2 p1 = vClipPosition[1].xy / vClipPosition[1].w;
    vec2 p2 = vClipPosition[2].xy / vClipPosition[2].w;

    vec2 edge0 = p2 - p1;
    vec2 edge1 = p2 - p0;
    vec2 edge2 = p1 - p0;

    float area          = abs(edge1.x * edge2.y - edge1.y * edge2.x);
    float wireThickness = 800.0 - uWireThickness;

    // Vertex 0
    gWorldPosition  = vWorldPosition[0];
    gl_Position     = vClipPosition[0];
    gDist.xyz       = vec3(area / length(edge0), 0.0, 0.0) * vClipPosition[0].w * wireThickness;
    gDist.w         = 1.0 / vClipPosition[0].w;
    EmitVertex();

    // Vertex 1
    gWorldPosition  = vWorldPosition[1];
    gl_Position     = vClipPosition[1];
    gDist.xyz       = vec3(0.0, area / length(edge1), 0.0) * vClipPosition[1].w * wireThickness;
    gDist.w         = 1.0 / vClipPosition[1].w;
    EmitVertex();

    // Vertex 2
    gWorldPosition  = vWorldPosition[2];
    gl_Position     = vClipPosition[2];
    gDist.xyz       = vec3(0.0, 0.0, area / length(edge2)) * vClipPosition[2].w * wireThickness;
    gDist.w         = 1.0 / vClipPosition[2].w;
    EmitVertex();

    EndPrimitive();
}