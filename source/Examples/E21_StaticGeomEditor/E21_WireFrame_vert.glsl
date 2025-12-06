#version 450

#include "..\..\..\plugins\xgeom_static.plugin\source\runtime\xgeom_static_mb_input_position.vert"

layout(set = 2, binding = 0) uniform Mesh
{
    mat4 L2w;           // Local space -> camera-centered small world
    mat4 w2C;           // Small world -> clip space (projection * view)
} mesh;

// Geom shader output
layout(location = 0) out vec4 vwWorldPosition;
layout(location = 1) out vec4 vClipPosition;

void main()
{
    // Set the final position
    vwWorldPosition = mesh.L2w * getVertexLocalPosition().Value;
    vClipPosition   = mesh.w2C * vwWorldPosition;
    gl_Position     = vClipPosition;
}

