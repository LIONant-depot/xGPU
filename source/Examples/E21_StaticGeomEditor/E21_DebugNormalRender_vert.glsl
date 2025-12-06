#version 450

#include "..\..\..\plugins\xgeom_static.plugin\source\runtime\xgeom_static_mb_input_full.vert"

// Mesh-level uniforms (updated once per mesh / per frame)
layout(set = 2, binding = 0) uniform MeshUniforms
{
    mat4 L2C;          // Small world -> clip space (projection * view)
    vec4 ScaleFactor;
    vec4 Color;
} mesh;

// Fragment shader inputs
layout(location = 0) out vec4 out_A;
layout(location = 1) out vec4 out_B;
layout(location = 2) out vec4 out_Color;

void main()
{
    const mb_full_vertex VertData = getVertexData();

    // Try to keep the normal the same size in the screen
    vec3 Axis = VertData.Tangent  * mesh.ScaleFactor.xxx
              + VertData.Binormal * mesh.ScaleFactor.yyy
              + VertData.Normal   * mesh.ScaleFactor.zzz;

    float uMaxDepth     = mesh.ScaleFactor.w;
    out_A               = mesh.L2C * VertData.LocalPos;
    vec4 dir_clip       = mesh.L2C * vec4(Axis, 0.0);
    float depth_scale   = min(out_A.w, uMaxDepth);
    out_B               = out_A + normalize(dir_clip) * (30 * depth_scale * 0.001);

    out_Color           = mesh.Color;

    gl_Position         = out_A;
}
