#version 450

// Cluster struct
struct ClusterData
{
    vec4 posScale;                 // xyz = position scale, w = trash
    vec4 posTranslation;           // xyz = position translation, w = trash
    vec4 uvScaleTranslation;       // xy = UV Scale, zw = UV offset
};

// Cluster-level uniforms array (bound once, indexed per draw)
layout(std430, set = 1, binding = 0) buffer ClusterBuffer
{
    ClusterData cluster[];  // Unsized array of structs
};

// Push constant for cluster index (updated per draw)
layout(set = 2, binding = 0) uniform Mesh
{
    mat4 L2w;           // Local space -> camera-centered small world
    mat4 w2C;           // Small world -> clip space (projection * view)
} mesh;

// Push constant for cluster index (updated per draw)
layout(push_constant) uniform PushConstants
{
    uint clusterIndex;  // Index into cluster array
} push;

// Geom shader output
layout(location = 0) out vec4 vwWorldPosition;
layout(location = 1) out vec4 vClipPosition;

// Input attributes
layout(location = 0) in ivec4 in_PosExtra;      // R16G16B16_SINT + R16_UINT (extra_bits)


void main()
{
    ClusterData selectedCluster = cluster[push.clusterIndex];

    // Decode compressed position from int16 to [-1,1]
    const vec3 norm_pos = (vec3(in_PosExtra.xyz) + 32768.0) / 32767.5 - 1.0;
    const vec4 local_pos = vec4(norm_pos * selectedCluster.posScale.xyz + selectedCluster.posTranslation.xyz, 1.0);

    // Set the final position
    vwWorldPosition = mesh.L2w * local_pos;
    vClipPosition   = mesh.w2C * vwWorldPosition;
    gl_Position     = vClipPosition;
}

