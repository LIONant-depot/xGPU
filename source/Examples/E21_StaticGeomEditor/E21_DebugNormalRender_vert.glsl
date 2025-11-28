#version 450

// Fast oct-encoded normal/tangent decode (12-bit N, 12/11-bit T)
vec3 oct_decode(vec2 e)
{
    e = e * 2.0 - 1.0;                          // [0,1] -> [-1,1]
    vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0.0)
    {
        v.xy = (1.0 - abs(v.yx)) * sign(v.xy);
    }
    return normalize(v);
}

// Mesh-level uniforms (updated once per mesh / per frame)
layout(set = 2, binding = 0) uniform MeshUniforms
{
    mat4 L2C;          // Small world -> clip space (projection * view)
    vec4 ScaleFactor;
    vec4 Color;
} mesh;

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
layout(push_constant) uniform PushConstants
{
    uint    clusterIndex;  // Index into cluster array
} push;

// Vertex inputs
layout(location = 0) in ivec4 in_PosExtra;      // xyz = compressed pos (R16G16B16_SINT), w = extra 16 bits
layout(location = 1) in uvec2 in_UV;            // compressed UV (R16G16_UNORM)
layout(location = 2) in uvec4 in_OctNormTan;    // high 8 bits of oct normal/tangent (R8G8B8A8_UINT)

// Fragment shader inputs
layout(location = 0) out vec4 out_A;
layout(location = 1) out vec4 out_B;
layout(location = 2) out vec4 out_Color;

void main()
{
    // Select cluster by push constant index
    ClusterData selectedCluster = cluster[push.clusterIndex];

    // Decode compressed position from int16 to [-1,1]
    const vec3 norm_pos  = (vec3(in_PosExtra.xyz) + 32768.0) / 32767.5 - 1.0;
    const vec4 local_pos = vec4(norm_pos * selectedCluster.posScale.xyz + selectedCluster.posTranslation.xyz, 1.0);

    // Extract extra_bits
    const uint extra_bits = uint(in_PosExtra.w);

    // Binormal sign: bit 15 of extra_bits (0:+1, 1:-1)
    const uint sign_bit = (extra_bits >> 15u) & 1u;
    const float binormal_sign = (sign_bit == 0u) ? 1.0 : -1.0;

    // Remaining 15 bits (0-14) for low-precision components
    // N_x_low: 4 bits (bits 0-3)
    // N_y_low: 4 bits (bits 4-7)
    // T_x_low: 4 bits (bits 8-11)
    // T_y_low: 3 bits (bits 12-14)
    const uvec4 lows = (uvec4(extra_bits) >> uvec4(0u, 4u, 8u, 12u)) & uvec4(0xFu, 0xFu, 0xFu, 7u);

    // Combine high 8 bits (from vertex attr) + low bits
    const uvec4 combined = (in_OctNormTan << uvec4(4u, 4u, 4u, 3u)) | lows;

    // Decode oct-encoded normal (12 bit) and tangent (12/11 bit)
    const vec2 enc_normal  = vec2(combined.xy) / 4095.0;
    const vec2 enc_tangent = vec2(combined.z / 4095.0, combined.w / 2047.0);

    const vec3 Normal      = oct_decode(enc_normal);
    const vec3 Tangent     = oct_decode(enc_tangent);
    const vec3 Binormal    = cross(Normal, Tangent) * binormal_sign;

    // Transform to all needed spaces
   // out_A  = mesh.L2C * local_pos;
   // out_B =  mesh.L2C * vec4(local_pos.xyz + Normal * mesh.ScaleFactor.www, 1.0);


    float uMaxDepth = mesh.ScaleFactor.w;

    // Try to keep the normal the same size in the screen
    vec3 Axis = Tangent  * mesh.ScaleFactor.xxx
              + Binormal * mesh.ScaleFactor.yyy
              + Normal   * mesh.ScaleFactor.zzz;

    out_A = mesh.L2C * local_pos;
    vec4 dir_clip = mesh.L2C * vec4(Axis, 0.0);
    float depth_scale = min(out_A.w, uMaxDepth);
    out_B = out_A + normalize(dir_clip) * ( 30 * depth_scale * 0.001);

    out_Color = mesh.Color;

    // Render the normal always the same size...
    //vec4 normal_dir_clip = mesh.L2C * vec4(Normal, 0.0);  // Direction in clip space
    //out_B = out_A + normalize(normal_dir_clip) * (mesh.ScaleFactor.w*1000 * out_A.w * 0.001);


    //out_B  = (mesh.L2C * vec4((local_pos.xyz + world_normal.xyz   * mesh.ScaleFactor.www), 1));// * mesh.ScaleFactor.xxxx;   // Choose witch vector to show...
    //out_B += (mesh.L2C * vec4((local_pos.xyz + Tangent.xyz  * mesh.ScaleFactor.www), 1)) * mesh.ScaleFactor.yyyy;
    //out_B += (mesh.L2C * vec4((local_pos.xyz + Binormal.xyz * mesh.ScaleFactor.www), 1)) * mesh.ScaleFactor.zzzz;

    gl_Position = out_A;
}

