
#include "imgui.h"
#include "../xGPU.h"
#include <windows.h>
#include <chrono>
#include <unordered_map>
#include <memory>

#include "imgui_internal.h"

namespace xgpu::tools::imgui {

#define IMGUI_GAMMA_CORRECTION
constexpr auto g_VertShaderSPV = std::array
{
#ifdef IMGUI_GAMMA_CORRECTION
    // #version 450 core
// layout(location = 0) in vec2 aPos;
// layout(location = 1) in vec2 aUV;
// layout(location = 2) in vec4 aColor;
// layout(push_constant) uniform uPushConstant { vec2 uScale; vec2 uTranslate; } pc;
// 
// out gl_PerVertex { vec4 gl_Position; };
// layout(location = 0) out struct { vec4 Color; vec2 UV; } Out;
// 
// void main()
// {
//     Out.Color.rgb = pow(aColor.rgb, vec3(2.2f));
//     Out.Color.a = aColor.a;
//     Out.UV = aUV;
//     gl_Position = vec4(aPos * pc.uScale + pc.uTranslate, 0, 1);
// }
0x07230203,0x00010000,0x000d000a,0x0000003c,
0x00000000,0x00020011,0x00000001,0x0006000b,
0x00000001,0x4c534c47,0x6474732e,0x3035342e,
0x00000000,0x0003000e,0x00000000,0x00000001,
0x000a000f,0x00000000,0x00000004,0x6e69616d,
0x00000000,0x0000000b,0x0000000f,0x00000023,
0x00000029,0x0000002a,0x00030003,0x00000002,
0x000001c2,0x000a0004,0x475f4c47,0x4c474f4f,
0x70635f45,0x74735f70,0x5f656c79,0x656e696c,
0x7269645f,0x69746365,0x00006576,0x00080004,
0x475f4c47,0x4c474f4f,0x6e695f45,0x64756c63,
0x69645f65,0x74636572,0x00657669,0x00040005,
0x00000004,0x6e69616d,0x00000000,0x00030005,
0x00000009,0x00000000,0x00050006,0x00000009,
0x00000000,0x6f6c6f43,0x00000072,0x00040006,
0x00000009,0x00000001,0x00005655,0x00030005,
0x0000000b,0x0074754f,0x00040005,0x0000000f,
0x6c6f4361,0x0000726f,0x00030005,0x00000023,
0x00565561,0x00060005,0x00000027,0x505f6c67,
0x65567265,0x78657472,0x00000000,0x00060006,
0x00000027,0x00000000,0x505f6c67,0x7469736f,
0x006e6f69,0x00030005,0x00000029,0x00000000,
0x00040005,0x0000002a,0x736f5061,0x00000000,
0x00060005,0x0000002c,0x73755075,0x6e6f4368,
0x6e617473,0x00000074,0x00050006,0x0000002c,
0x00000000,0x61635375,0x0000656c,0x00060006,
0x0000002c,0x00000001,0x61725475,0x616c736e,
0x00006574,0x00030005,0x0000002e,0x00006370,
0x00040047,0x0000000b,0x0000001e,0x00000000,
0x00040047,0x0000000f,0x0000001e,0x00000002,
0x00040047,0x00000023,0x0000001e,0x00000001,
0x00050048,0x00000027,0x00000000,0x0000000b,
0x00000000,0x00030047,0x00000027,0x00000002,
0x00040047,0x0000002a,0x0000001e,0x00000000,
0x00050048,0x0000002c,0x00000000,0x00000023,
0x00000000,0x00050048,0x0000002c,0x00000001,
0x00000023,0x00000008,0x00030047,0x0000002c,
0x00000002,0x00020013,0x00000002,0x00030021,
0x00000003,0x00000002,0x00030016,0x00000006,
0x00000020,0x00040017,0x00000007,0x00000006,
0x00000004,0x00040017,0x00000008,0x00000006,
0x00000002,0x0004001e,0x00000009,0x00000007,
0x00000008,0x00040020,0x0000000a,0x00000003,
0x00000009,0x0004003b,0x0000000a,0x0000000b,
0x00000003,0x00040015,0x0000000c,0x00000020,
0x00000001,0x0004002b,0x0000000c,0x0000000d,
0x00000000,0x00040020,0x0000000e,0x00000001,
0x00000007,0x0004003b,0x0000000e,0x0000000f,
0x00000001,0x00040017,0x00000010,0x00000006,
0x00000003,0x0004002b,0x00000006,0x00000013,
0x400ccccd,0x0006002c,0x00000010,0x00000014,
0x00000013,0x00000013,0x00000013,0x00040020,
0x00000016,0x00000003,0x00000007,0x00040015,
0x0000001a,0x00000020,0x00000000,0x0004002b,
0x0000001a,0x0000001b,0x00000003,0x00040020,
0x0000001c,0x00000001,0x00000006,0x00040020,
0x0000001f,0x00000003,0x00000006,0x0004002b,
0x0000000c,0x00000021,0x00000001,0x00040020,
0x00000022,0x00000001,0x00000008,0x0004003b,
0x00000022,0x00000023,0x00000001,0x00040020,
0x00000025,0x00000003,0x00000008,0x0003001e,
0x00000027,0x00000007,0x00040020,0x00000028,
0x00000003,0x00000027,0x0004003b,0x00000028,
0x00000029,0x00000003,0x0004003b,0x00000022,
0x0000002a,0x00000001,0x0004001e,0x0000002c,
0x00000008,0x00000008,0x00040020,0x0000002d,
0x00000009,0x0000002c,0x0004003b,0x0000002d,
0x0000002e,0x00000009,0x00040020,0x0000002f,
0x00000009,0x00000008,0x0004002b,0x00000006,
0x00000036,0x00000000,0x0004002b,0x00000006,
0x00000037,0x3f800000,0x00050036,0x00000002,
0x00000004,0x00000000,0x00000003,0x000200f8,
0x00000005,0x0004003d,0x00000007,0x00000011,
0x0000000f,0x0008004f,0x00000010,0x00000012,
0x00000011,0x00000011,0x00000000,0x00000001,
0x00000002,0x0007000c,0x00000010,0x00000015,
0x00000001,0x0000001a,0x00000012,0x00000014,
0x00050041,0x00000016,0x00000017,0x0000000b,
0x0000000d,0x0004003d,0x00000007,0x00000018,
0x00000017,0x0009004f,0x00000007,0x00000019,
0x00000018,0x00000015,0x00000004,0x00000005,
0x00000006,0x00000003,0x0003003e,0x00000017,
0x00000019,0x00050041,0x0000001c,0x0000001d,
0x0000000f,0x0000001b,0x0004003d,0x00000006,
0x0000001e,0x0000001d,0x00060041,0x0000001f,
0x00000020,0x0000000b,0x0000000d,0x0000001b,
0x0003003e,0x00000020,0x0000001e,0x0004003d,
0x00000008,0x00000024,0x00000023,0x00050041,
0x00000025,0x00000026,0x0000000b,0x00000021,
0x0003003e,0x00000026,0x00000024,0x0004003d,
0x00000008,0x0000002b,0x0000002a,0x00050041,
0x0000002f,0x00000030,0x0000002e,0x0000000d,
0x0004003d,0x00000008,0x00000031,0x00000030,
0x00050085,0x00000008,0x00000032,0x0000002b,
0x00000031,0x00050041,0x0000002f,0x00000033,
0x0000002e,0x00000021,0x0004003d,0x00000008,
0x00000034,0x00000033,0x00050081,0x00000008,
0x00000035,0x00000032,0x00000034,0x00050051,
0x00000006,0x00000038,0x00000035,0x00000000,
0x00050051,0x00000006,0x00000039,0x00000035,
0x00000001,0x00070050,0x00000007,0x0000003a,
0x00000038,0x00000039,0x00000036,0x00000037,
0x00050041,0x00000016,0x0000003b,0x00000029,
0x0000000d,0x0003003e,0x0000003b,0x0000003a,
0x000100fd,0x00010038
#else
// glsl_shader.vert, compiled with:
// # glslangValidator -V -x -o glsl_shader.vert.u32 glsl_shader.vert
//
// #version 450 core
// layout(location = 0) in vec2 aPos;
// layout(location = 1) in vec2 aUV;
// layout(location = 2) in vec4 aColor;
// layout(push_constant) uniform uPushConstant { vec2 uScale; vec2 uTranslate; } pc;
//
// out gl_PerVertex { vec4 gl_Position; };
// layout(location = 0) out struct { vec4 Color; vec2 UV; } Out;
//
// void main()
// {
//     Out.Color = aColor;
//     Out.UV = aUV;
//     gl_Position = vec4(aPos * pc.uScale + pc.uTranslate, 0, 1);
// }
    0x07230203,0x00010000,0x00080001,0x0000002e,0x00000000,0x00020011,0x00000001,0x0006000b,
    0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,
    0x000a000f,0x00000000,0x00000004,0x6e69616d,0x00000000,0x0000000b,0x0000000f,0x00000015,
    0x0000001b,0x0000001c,0x00030003,0x00000002,0x000001c2,0x00040005,0x00000004,0x6e69616d,
    0x00000000,0x00030005,0x00000009,0x00000000,0x00050006,0x00000009,0x00000000,0x6f6c6f43,
    0x00000072,0x00040006,0x00000009,0x00000001,0x00005655,0x00030005,0x0000000b,0x0074754f,
    0x00040005,0x0000000f,0x6c6f4361,0x0000726f,0x00030005,0x00000015,0x00565561,0x00060005,
    0x00000019,0x505f6c67,0x65567265,0x78657472,0x00000000,0x00060006,0x00000019,0x00000000,
    0x505f6c67,0x7469736f,0x006e6f69,0x00030005,0x0000001b,0x00000000,0x00040005,0x0000001c,
    0x736f5061,0x00000000,0x00060005,0x0000001e,0x73755075,0x6e6f4368,0x6e617473,0x00000074,
    0x00050006,0x0000001e,0x00000000,0x61635375,0x0000656c,0x00060006,0x0000001e,0x00000001,
    0x61725475,0x616c736e,0x00006574,0x00030005,0x00000020,0x00006370,0x00040047,0x0000000b,
    0x0000001e,0x00000000,0x00040047,0x0000000f,0x0000001e,0x00000002,0x00040047,0x00000015,
    0x0000001e,0x00000001,0x00050048,0x00000019,0x00000000,0x0000000b,0x00000000,0x00030047,
    0x00000019,0x00000002,0x00040047,0x0000001c,0x0000001e,0x00000000,0x00050048,0x0000001e,
    0x00000000,0x00000023,0x00000000,0x00050048,0x0000001e,0x00000001,0x00000023,0x00000008,
    0x00030047,0x0000001e,0x00000002,0x00020013,0x00000002,0x00030021,0x00000003,0x00000002,
    0x00030016,0x00000006,0x00000020,0x00040017,0x00000007,0x00000006,0x00000004,0x00040017,
    0x00000008,0x00000006,0x00000002,0x0004001e,0x00000009,0x00000007,0x00000008,0x00040020,
    0x0000000a,0x00000003,0x00000009,0x0004003b,0x0000000a,0x0000000b,0x00000003,0x00040015,
    0x0000000c,0x00000020,0x00000001,0x0004002b,0x0000000c,0x0000000d,0x00000000,0x00040020,
    0x0000000e,0x00000001,0x00000007,0x0004003b,0x0000000e,0x0000000f,0x00000001,0x00040020,
    0x00000011,0x00000003,0x00000007,0x0004002b,0x0000000c,0x00000013,0x00000001,0x00040020,
    0x00000014,0x00000001,0x00000008,0x0004003b,0x00000014,0x00000015,0x00000001,0x00040020,
    0x00000017,0x00000003,0x00000008,0x0003001e,0x00000019,0x00000007,0x00040020,0x0000001a,
    0x00000003,0x00000019,0x0004003b,0x0000001a,0x0000001b,0x00000003,0x0004003b,0x00000014,
    0x0000001c,0x00000001,0x0004001e,0x0000001e,0x00000008,0x00000008,0x00040020,0x0000001f,
    0x00000009,0x0000001e,0x0004003b,0x0000001f,0x00000020,0x00000009,0x00040020,0x00000021,
    0x00000009,0x00000008,0x0004002b,0x00000006,0x00000028,0x00000000,0x0004002b,0x00000006,
    0x00000029,0x3f800000,0x00050036,0x00000002,0x00000004,0x00000000,0x00000003,0x000200f8,
    0x00000005,0x0004003d,0x00000007,0x00000010,0x0000000f,0x00050041,0x00000011,0x00000012,
    0x0000000b,0x0000000d,0x0003003e,0x00000012,0x00000010,0x0004003d,0x00000008,0x00000016,
    0x00000015,0x00050041,0x00000017,0x00000018,0x0000000b,0x00000013,0x0003003e,0x00000018,
    0x00000016,0x0004003d,0x00000008,0x0000001d,0x0000001c,0x00050041,0x00000021,0x00000022,
    0x00000020,0x0000000d,0x0004003d,0x00000008,0x00000023,0x00000022,0x00050085,0x00000008,
    0x00000024,0x0000001d,0x00000023,0x00050041,0x00000021,0x00000025,0x00000020,0x00000013,
    0x0004003d,0x00000008,0x00000026,0x00000025,0x00050081,0x00000008,0x00000027,0x00000024,
    0x00000026,0x00050051,0x00000006,0x0000002a,0x00000027,0x00000000,0x00050051,0x00000006,
    0x0000002b,0x00000027,0x00000001,0x00070050,0x00000007,0x0000002c,0x0000002a,0x0000002b,
    0x00000028,0x00000029,0x00050041,0x00000011,0x0000002d,0x0000001b,0x0000000d,0x0003003e,
    0x0000002d,0x0000002c,0x000100fd,0x00010038
#endif
};

constexpr auto g_FragShaderSPV = std::array
{
#ifdef IMGUI_GAMMA_CORRECTION
// #version 450
// #extension GL_ARB_separate_shader_objects : enable
// #extension GL_ARB_shading_language_420pack : enable
// 
// layout(binding = 0)    uniform     sampler2D   uSamplerColor;
// layout(location = 0)   in struct { vec4 Color; vec2 UV; } In;
// layout(location = 0)   out         vec4        outFragColor;
// 
// void main()
// {
//     vec4 Color = In.Color * texture(uSamplerColor, In.UV);
// 
//     // Convert to gamma
//     const float gamma = 2.2f;
//     Color.rgb = pow(Color.rgb, vec3(1.0 / gamma));
// 
//     // Output color
//     outFragColor = Color;
// }
0x07230203,0x00010000,0x000d000a,0x00000029,
0x00000000,0x00020011,0x00000001,0x0006000b,
0x00000001,0x4c534c47,0x6474732e,0x3035342e,
0x00000000,0x0003000e,0x00000000,0x00000001,
0x0007000f,0x00000004,0x00000004,0x6e69616d,
0x00000000,0x0000000d,0x00000027,0x00030010,
0x00000004,0x00000007,0x00030003,0x00000002,
0x000001c2,0x00090004,0x415f4c47,0x735f4252,
0x72617065,0x5f657461,0x64616873,0x6f5f7265,
0x63656a62,0x00007374,0x00090004,0x415f4c47,
0x735f4252,0x69646168,0x6c5f676e,0x75676e61,
0x5f656761,0x70303234,0x006b6361,0x000a0004,
0x475f4c47,0x4c474f4f,0x70635f45,0x74735f70,
0x5f656c79,0x656e696c,0x7269645f,0x69746365,
0x00006576,0x00080004,0x475f4c47,0x4c474f4f,
0x6e695f45,0x64756c63,0x69645f65,0x74636572,
0x00657669,0x00040005,0x00000004,0x6e69616d,
0x00000000,0x00040005,0x00000009,0x6f6c6f43,
0x00000072,0x00030005,0x0000000b,0x00000000,
0x00050006,0x0000000b,0x00000000,0x6f6c6f43,
0x00000072,0x00040006,0x0000000b,0x00000001,
0x00005655,0x00030005,0x0000000d,0x00006e49,
0x00060005,0x00000016,0x6d615375,0x72656c70,
0x6f6c6f43,0x00000072,0x00060005,0x00000027,
0x4674756f,0x43676172,0x726f6c6f,0x00000000,
0x00040047,0x0000000d,0x0000001e,0x00000000,
0x00040047,0x00000016,0x00000022,0x00000000,
0x00040047,0x00000016,0x00000021,0x00000000,
0x00040047,0x00000027,0x0000001e,0x00000000,
0x00020013,0x00000002,0x00030021,0x00000003,
0x00000002,0x00030016,0x00000006,0x00000020,
0x00040017,0x00000007,0x00000006,0x00000004,
0x00040020,0x00000008,0x00000007,0x00000007,
0x00040017,0x0000000a,0x00000006,0x00000002,
0x0004001e,0x0000000b,0x00000007,0x0000000a,
0x00040020,0x0000000c,0x00000001,0x0000000b,
0x0004003b,0x0000000c,0x0000000d,0x00000001,
0x00040015,0x0000000e,0x00000020,0x00000001,
0x0004002b,0x0000000e,0x0000000f,0x00000000,
0x00040020,0x00000010,0x00000001,0x00000007,
0x00090019,0x00000013,0x00000006,0x00000001,
0x00000000,0x00000000,0x00000000,0x00000001,
0x00000000,0x0003001b,0x00000014,0x00000013,
0x00040020,0x00000015,0x00000000,0x00000014,
0x0004003b,0x00000015,0x00000016,0x00000000,
0x0004002b,0x0000000e,0x00000018,0x00000001,
0x00040020,0x00000019,0x00000001,0x0000000a,
0x00040017,0x0000001e,0x00000006,0x00000003,
0x0004002b,0x00000006,0x00000021,0x3ee8ba2f,
0x0006002c,0x0000001e,0x00000022,0x00000021,
0x00000021,0x00000021,0x00040020,0x00000026,
0x00000003,0x00000007,0x0004003b,0x00000026,
0x00000027,0x00000003,0x00050036,0x00000002,
0x00000004,0x00000000,0x00000003,0x000200f8,
0x00000005,0x0004003b,0x00000008,0x00000009,
0x00000007,0x00050041,0x00000010,0x00000011,
0x0000000d,0x0000000f,0x0004003d,0x00000007,
0x00000012,0x00000011,0x0004003d,0x00000014,
0x00000017,0x00000016,0x00050041,0x00000019,
0x0000001a,0x0000000d,0x00000018,0x0004003d,
0x0000000a,0x0000001b,0x0000001a,0x00050057,
0x00000007,0x0000001c,0x00000017,0x0000001b,
0x00050085,0x00000007,0x0000001d,0x00000012,
0x0000001c,0x0003003e,0x00000009,0x0000001d,
0x0004003d,0x00000007,0x0000001f,0x00000009,
0x0008004f,0x0000001e,0x00000020,0x0000001f,
0x0000001f,0x00000000,0x00000001,0x00000002,
0x0007000c,0x0000001e,0x00000023,0x00000001,
0x0000001a,0x00000020,0x00000022,0x0004003d,
0x00000007,0x00000024,0x00000009,0x0009004f,
0x00000007,0x00000025,0x00000024,0x00000023,
0x00000004,0x00000005,0x00000006,0x00000003,
0x0003003e,0x00000009,0x00000025,0x0004003d,
0x00000007,0x00000028,0x00000009,0x0003003e,
0x00000027,0x00000028,0x000100fd,0x00010038
#else
    // glsl_shader.frag, compiled with:
    // # glslangValidator -V -x -o glsl_shader.frag.u32 glsl_shader.frag
    //
    // #version 450 core
    // layout(location = 0) out vec4 fColor;
    // layout(set=0, binding=0) uniform sampler2D sTexture;
    // layout(location = 0) in struct { vec4 Color; vec2 UV; } In;
    // void main()
    // {
    //     fColor = In.Color * texture(sTexture, In.UV.st);
    // }
    0x07230203,0x00010000,0x00080001,0x0000001e,0x00000000,0x00020011,0x00000001,0x0006000b,
    0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,
    0x0007000f,0x00000004,0x00000004,0x6e69616d,0x00000000,0x00000009,0x0000000d,0x00030010,
    0x00000004,0x00000007,0x00030003,0x00000002,0x000001c2,0x00040005,0x00000004,0x6e69616d,
    0x00000000,0x00040005,0x00000009,0x6c6f4366,0x0000726f,0x00030005,0x0000000b,0x00000000,
    0x00050006,0x0000000b,0x00000000,0x6f6c6f43,0x00000072,0x00040006,0x0000000b,0x00000001,
    0x00005655,0x00030005,0x0000000d,0x00006e49,0x00050005,0x00000016,0x78655473,0x65727574,
    0x00000000,0x00040047,0x00000009,0x0000001e,0x00000000,0x00040047,0x0000000d,0x0000001e,
    0x00000000,0x00040047,0x00000016,0x00000022,0x00000000,0x00040047,0x00000016,0x00000021,
    0x00000000,0x00020013,0x00000002,0x00030021,0x00000003,0x00000002,0x00030016,0x00000006,
    0x00000020,0x00040017,0x00000007,0x00000006,0x00000004,0x00040020,0x00000008,0x00000003,
    0x00000007,0x0004003b,0x00000008,0x00000009,0x00000003,0x00040017,0x0000000a,0x00000006,
    0x00000002,0x0004001e,0x0000000b,0x00000007,0x0000000a,0x00040020,0x0000000c,0x00000001,
    0x0000000b,0x0004003b,0x0000000c,0x0000000d,0x00000001,0x00040015,0x0000000e,0x00000020,
    0x00000001,0x0004002b,0x0000000e,0x0000000f,0x00000000,0x00040020,0x00000010,0x00000001,
    0x00000007,0x00090019,0x00000013,0x00000006,0x00000001,0x00000000,0x00000000,0x00000000,
    0x00000001,0x00000000,0x0003001b,0x00000014,0x00000013,0x00040020,0x00000015,0x00000000,
    0x00000014,0x0004003b,0x00000015,0x00000016,0x00000000,0x0004002b,0x0000000e,0x00000018,
    0x00000001,0x00040020,0x00000019,0x00000001,0x0000000a,0x00050036,0x00000002,0x00000004,
    0x00000000,0x00000003,0x000200f8,0x00000005,0x00050041,0x00000010,0x00000011,0x0000000d,
    0x0000000f,0x0004003d,0x00000007,0x00000012,0x00000011,0x0004003d,0x00000014,0x00000017,
    0x00000016,0x00050041,0x00000019,0x0000001a,0x0000000d,0x00000018,0x0004003d,0x0000000a,
    0x0000001b,0x0000001a,0x00050057,0x00000007,0x0000001c,0x00000017,0x0000001b,0x00050085,
    0x00000007,0x0000001d,0x00000012,0x0000001c,0x0003003e,0x00000009,0x0000001d,0x000100fd,
    0x00010038
#endif
};

struct imgui_push_constants
{
    std::array<float, 2> m_Scale;
    std::array<float, 2> m_Translate;
};

//------------------------------------------------------------------------------------------------------------

#define GETINSTANCE                                                                             \
    ImGuiIO&            io        = ImGui::GetIO();                                             \
    breach_instance&    Instance  = *reinterpret_cast<breach_instance*>(io.UserData);

//------------------------------------------------------------------------------------------------------------

using clock        = std::chrono::high_resolution_clock;

//------------------------------------------------------------------------------------------------------------

struct share_resources
{
    inline static std::shared_ptr<share_resources> s_SharePointer;

    //-------------------------------------------------------------------------------------------

    static std::shared_ptr<share_resources>& getOrCreate()
    {
        if ( s_SharePointer.get() == nullptr )
        {
            s_SharePointer = std::make_shared<share_resources>();
            s_SharePointer->CreateKeyMapping();
        }

        return s_SharePointer;
    }

    //-------------------------------------------------------------------------------------------

    void CreateKeyMapping()
    {
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_ESCAPE)]     = ImGuiKey_Escape;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_1)]          = ImGuiKey_1;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_2)]          = ImGuiKey_2;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_3)]          = ImGuiKey_3;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_4)]          = ImGuiKey_4;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_5)]          = ImGuiKey_5;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_6)]          = ImGuiKey_6;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_7)]          = ImGuiKey_7;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_8)]          = ImGuiKey_8;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_9)]          = ImGuiKey_9;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_0)]          = ImGuiKey_0;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_MINUS)]      = ImGuiKey_Minus;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_EQUALS)]     = ImGuiKey_Equal;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_BACKSPACE)]  = ImGuiKey_Backspace;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_TAB)]        = ImGuiKey_Tab;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_Q)]          = ImGuiKey_Q;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_W)]          = ImGuiKey_W;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_E)]          = ImGuiKey_E;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_R)]          = ImGuiKey_R;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_T)]          = ImGuiKey_T;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_Y)]          = ImGuiKey_Y;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_U)]          = ImGuiKey_U;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_I)]          = ImGuiKey_I;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_O)]          = ImGuiKey_O;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_P)]          = ImGuiKey_P;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_LBRACKET)]   = ImGuiKey_LeftBracket;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_RBRACKET)]   = ImGuiKey_RightBracket;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_RETURN)]     = ImGuiKey_Enter;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_LCONTROL)]   = ImGuiKey_LeftCtrl;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_A)]          = ImGuiKey_A;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_S)]          = ImGuiKey_S;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_D)]          = ImGuiKey_D;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_F)]          = ImGuiKey_F;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_G)]          = ImGuiKey_G;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_H)]          = ImGuiKey_H;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_J)]          = ImGuiKey_J;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_K)]          = ImGuiKey_K;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_L)]          = ImGuiKey_L;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_SEMICOLON)]  = ImGuiKey_Semicolon;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_APOSTROPHE)] = ImGuiKey_Apostrophe;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_GRAVE)]      = ImGuiKey_GraveAccent;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_LSHIFT)]     = ImGuiKey_LeftShift;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_BACKSLASH)]  = ImGuiKey_Backslash;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_Z)]          = ImGuiKey_Z;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_X)]          = ImGuiKey_X;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_C)]          = ImGuiKey_C;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_V)]          = ImGuiKey_V;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_B)]          = ImGuiKey_B;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_N)]          = ImGuiKey_N;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_M)]          = ImGuiKey_M;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_COMMA)]      = ImGuiKey_Comma;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_PERIOD)]     = ImGuiKey_Period;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_SLASH)]      = ImGuiKey_Slash;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_RSHIFT)]     = ImGuiKey_RightShift;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_MULTIPLY)]   = ImGuiKey_KeypadMultiply;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_LALT)]       = ImGuiKey_LeftAlt;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_SPACE)]      = ImGuiKey_Space;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_CAPSLOCK)]   = ImGuiKey_CapsLock;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_F1)]         = ImGuiKey_F1;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_F2)]         = ImGuiKey_F2;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_F3)]         = ImGuiKey_F3;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_F4)]         = ImGuiKey_F4;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_F5)]         = ImGuiKey_F5;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_F6)]         = ImGuiKey_F6;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_F7)]         = ImGuiKey_F7;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_F8)]         = ImGuiKey_F8;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_F9)]         = ImGuiKey_F9;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_F10)]        = ImGuiKey_F10;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_NUMLOCK)]    = ImGuiKey_NumLock;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_SCROLLLOCK)] = ImGuiKey_ScrollLock;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_NUMPAD7)]    = ImGuiKey_Keypad7;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_NUMPAD8)]    = ImGuiKey_Keypad8;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_NUMPAD9)]    = ImGuiKey_Keypad9;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_SUBTRACT)]   = ImGuiKey_KeypadSubtract;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_NUMPAD4)]    = ImGuiKey_Keypad4;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_NUMPAD5)]    = ImGuiKey_Keypad5;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_NUMPAD6)]    = ImGuiKey_Keypad6;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_ADD)]        = ImGuiKey_KeypadAdd;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_NUMPAD1)]    = ImGuiKey_Keypad1;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_NUMPAD2)]    = ImGuiKey_Keypad2;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_NUMPAD3)]    = ImGuiKey_Keypad3;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_NUMPAD0)]    = ImGuiKey_Keypad0;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_DECIMAL)]    = ImGuiKey_KeypadDecimal;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_OEM_102)]    = ImGuiKey_Oem102;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_F11)]        = ImGuiKey_F11;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_F12)]        = ImGuiKey_F12;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_F13)]        = ImGuiKey_F13;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_F14)]        = ImGuiKey_F14;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_F15)]        = ImGuiKey_F15;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_NUMPADENTER)]= ImGuiKey_KeypadEnter;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_RCONTROL)]   = ImGuiKey_RightCtrl;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_DIVIDE)]     = ImGuiKey_KeypadDivide;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_RALT)]       = ImGuiKey_RightAlt;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_PAUSE)]      = ImGuiKey_Pause;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_HOME)]       = ImGuiKey_Home;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_UP)]         = ImGuiKey_UpArrow;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_PAGEUP)]     = ImGuiKey_PageUp;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_LEFT)]       = ImGuiKey_LeftArrow;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_RIGHT)]      = ImGuiKey_RightArrow;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_END)]        = ImGuiKey_End;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_DOWN)]       = ImGuiKey_DownArrow;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_PAGEDOWN)]   = ImGuiKey_PageDown;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_INSERT)]     = ImGuiKey_Insert;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_DELETE)]     = ImGuiKey_Delete;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_LWIN)]       = ImGuiKey_LeftSuper;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_RWIN)]       = ImGuiKey_RightSuper;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_APPS)]       = ImGuiKey_Menu;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_WEBBACK)]    = ImGuiKey_AppBack;
        m_xGPUToImGuiKey[static_cast<int>(xgpu::keyboard::digital::KEY_WEBFORWARD)] = ImGuiKey_AppForward;
    }

    //-------------------------------------------------------------------------------------------

    static void Destroy(std::shared_ptr<share_resources>& Ref )
    {
        Ref.reset();
        if (s_SharePointer.use_count() == 1)
        {
            s_SharePointer.reset();
        }
    }

    //-------------------------------------------------------------------------------------------

    void setTexture(cmd_buffer& CmdBuffer, xgpu::texture& Texture )
    {
        if ( auto E = m_TextureToPipeline.find( Texture.m_Private.get() ); E != m_TextureToPipeline.end())
        {
            CmdBuffer.setPipelineInstance( E->second );
        }
        else
        {
            auto Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{ Texture } };
            auto Setup    = xgpu::pipeline_instance::setup
            { .m_PipeLine         = m_PipeLine
            , .m_SamplersBindings = Bindings
            };

            xgpu::pipeline_instance PipelineInstance;
            if (auto Err = m_Device.Create(PipelineInstance, Setup); Err)
            {
                assert(false);
            }

            m_TextureToPipeline.insert( {Texture.m_Private.get(), PipelineInstance} );

            CmdBuffer.setPipelineInstance(PipelineInstance);
        }
    }

    //-------------------------------------------------------------------------------------------

    xgpu::device::error* CreteDefaultPipeline(xgpu::texture& Texture )
    {
        //
        // Define the vertex descriptors
        //
        {
            auto Attributes = std::array
            {
                xgpu::vertex_descriptor::attribute
                {
                    .m_Offset = offsetof(ImDrawVert, pos)
                ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
                }
            ,   xgpu::vertex_descriptor::attribute
                {
                    .m_Offset = offsetof(ImDrawVert, uv)
                ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
                }
            ,   xgpu::vertex_descriptor::attribute
                {
                    .m_Offset = offsetof(ImDrawVert, col)
                ,   .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED
                }
            };
            auto Setup = xgpu::vertex_descriptor::setup
            {
                .m_VertexSize = sizeof(ImDrawVert)
            ,   .m_Attributes = Attributes
            };

            if ( auto Err = m_Device.Create(m_VertexDescriptor, Setup); Err )
                return Err;
        }

        //
        // Define the pipeline
        //
        {
            xgpu::shader ImGuiFragmentShader;
            {
                xgpu::shader::setup Setup
                { .m_Type   = xgpu::shader::type::bit::FRAGMENT
                , .m_Sharer = xgpu::shader::setup::raw_data{ g_FragShaderSPV }
                };
                if (auto Err = m_Device.Create(ImGuiFragmentShader, Setup ); Err) return Err;
            }
            xgpu::shader ImGuiVertexShader;
            {
                auto UniformConstans = std::array
                { static_cast<int>(sizeof(float) * 2)   // Scale
                , static_cast<int>(sizeof(float) * 2)   // Translation
                };
                xgpu::shader::setup Setup
                {
                    .m_Type                 = xgpu::shader::type::bit::VERTEX
                ,   .m_Sharer               = xgpu::shader::setup::raw_data{g_VertShaderSPV}
                };
                if (auto Err = m_Device.Create(ImGuiVertexShader, Setup); Err) return Err;
            }

            auto Shaders  = std::array<const xgpu::shader*, 2>{ &ImGuiFragmentShader, &ImGuiVertexShader };
            auto Samplers = std::array{ xgpu::pipeline::sampler{} };
            auto Setup    = xgpu::pipeline::setup
            {
                .m_VertexDescriptor  = m_VertexDescriptor
            ,   .m_Shaders           = Shaders
            ,   .m_PushConstantsSize = sizeof(imgui_push_constants)
            ,   .m_Samplers          = Samplers
            ,   .m_Primitive         = { .m_Cull             = xgpu::pipeline::primitive::cull::NONE }
            ,   .m_DepthStencil      = { .m_bDepthTestEnable = false }
            ,   .m_Blend             = xgpu::pipeline::blend::getAlphaOriginal()
            };

            if ( auto Err = m_Device.Create(m_PipeLine, Setup); Err ) return Err;
        }

        //
        // Create Pipeline Instance
        //
        {
            auto Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{ Texture } };
            auto Setup    = xgpu::pipeline_instance::setup
            { .m_PipeLine         = m_PipeLine
            , .m_SamplersBindings = Bindings
            };

            if (auto Err = m_Device.Create(m_DefaultPipelineInstance, Setup); Err) return Err;
        }

        return nullptr;
    }
    
    std::unordered_map<void*,xgpu::pipeline_instance> m_TextureToPipeline       = {};
    xgpu::pipeline                                    m_PipeLine                = {};
    xgpu::pipeline_instance                           m_DefaultPipelineInstance = {};
    xgpu::device                                      m_Device                  = {};
    xgpu::vertex_descriptor                           m_VertexDescriptor        = {};
    std::unordered_map<int, ImGuiKey>                 m_xGPUToImGuiKey          = {};
    std::array<bool, static_cast<int>(xgpu::keyboard::digital::ENUM_COUNT)> m_LastsKeyState         = {};
    std::array<bool,3>                                                      m_LastMouseButtonStates = {};
};

//============================================================================================

struct window_info
{
    struct buffers
    {
        xgpu::buffer                m_VertexBuffer{};
        xgpu::buffer                m_IndexBuffer{};
    };

    std::shared_ptr<share_resources>    m_Shared            {};
    xgpu::window                        m_Window            {};
    std::array<buffers, 2>              m_PrimitiveBuffers  {};
    int                                 m_iFrame            {0};

    //------------------------------------------------------------------------------------------------------------

    window_info( xgpu::window& Window ) noexcept
    : m_Shared{ share_resources::getOrCreate() }
    , m_Window{ Window }
    {
        InitializeBuffers();
    }

    //------------------------------------------------------------------------------------------------------------

    window_info( void ) noexcept
    : m_Shared{ share_resources::getOrCreate() }
    {
        auto Err = m_Shared->m_Device.Create(m_Window, {});
        assert( Err == nullptr );
        InitializeBuffers();
    }

    //------------------------------------------------------------------------------------------------------------

    ~window_info()
    {
        // Let the system deals with references...
        share_resources::Destroy(m_Shared);
    }

    //------------------------------------------------------------------------------------------------------------

    void setMousePosition( int x, int y ) noexcept
    {
        m_Window.setMousePosition(x,y);
    }

    //------------------------------------------------------------------------------------------------------------

    xgpu::device::error* InitializeBuffers( void ) noexcept
    {
        //
        // Create Vertex buffer
        //
        {
            xgpu::buffer::setup Setup =
            { .m_Type           = xgpu::buffer::type::VERTEX
            , .m_Usage          = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ
            , .m_EntryByteSize  = sizeof(ImDrawVert)
            , .m_EntryCount     = 3 * 1000
            };

            for( auto& Prim : m_PrimitiveBuffers )
                if (auto Err = m_Shared->m_Device.Create( Prim.m_VertexBuffer, Setup ); Err) return Err;
        }

        //
        // Create Index buffer
        //
        {
            xgpu::buffer::setup Setup =
            { .m_Type           = xgpu::buffer::type::INDEX
            , .m_Usage          = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ
            , .m_EntryByteSize  = sizeof(ImDrawIdx)
            , .m_EntryCount     = 3 * 1000
            };

            for (auto& Prim : m_PrimitiveBuffers)
                if (auto Err = m_Shared->m_Device.Create( Prim.m_IndexBuffer, Setup ); Err) return Err;
        }

        return nullptr;
    }

    //------------------------------------------------------------------------------------------------------------

    void Render( ImGuiIO& io, ImDrawData* draw_data ) noexcept
    {
        //
        // Make sure we are fully sync up before touching any dependent resources
        // In this case the (vertex and index buffer)
        //
        auto CmdBuffer = m_Window.getCmdBuffer();

        if( draw_data->CmdListsCount == 0 ) 
            return;

        // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
        const int fb_width  = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
        const int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
        if (fb_width <= 0 || fb_height <= 0)
            return;

        // Get the active primitives
        auto& Prim = m_PrimitiveBuffers[m_iFrame];

        // Get ready for the next frame
        m_iFrame = (m_iFrame + 1) & 1;

        //
        // If we have primitives to render
        //
        if (draw_data->TotalVtxCount > 0)
        {
            if (draw_data->TotalVtxCount > Prim.m_VertexBuffer.getEntryCount() )
            {
                auto Err = Prim.m_VertexBuffer.Resize(draw_data->TotalVtxCount);
                assert(Err == nullptr && "Vertex buffer resize failed");
            }

            if (draw_data->TotalIdxCount > Prim.m_IndexBuffer.getEntryCount())
            {
                auto Err = Prim.m_IndexBuffer.Resize(draw_data->TotalIdxCount);
                assert(Err == nullptr && "Index buffer resize failed");
            }

            //
            // Copy over the vertices
            //
            (void)Prim.m_VertexBuffer.MemoryMap( 0, Prim.m_VertexBuffer.getEntryCount(), [&](void* pV)
            {
                (void)Prim.m_IndexBuffer.MemoryMap(0, Prim.m_IndexBuffer.getEntryCount(), [&](void* pI)
                {
                    auto pVertex = reinterpret_cast<ImDrawVert*>(pV);
                    auto pIndex  = reinterpret_cast<ImDrawIdx*>(pI);

                    for (int n = 0; n < draw_data->CmdListsCount; n++)
                    {
                        const ImDrawList* cmd_list = draw_data->CmdLists[n];
                        if (cmd_list->VtxBuffer.Data == nullptr) continue;
                        assert(cmd_list->VtxBuffer.Data != nullptr && "Invalid vertex buffer data");
                        assert(cmd_list->IdxBuffer.Data != nullptr && "Invalid index buffer data");
                        std::memcpy(pVertex, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
                        std::memcpy(pIndex,  cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
                        pVertex += cmd_list->VtxBuffer.Size;
                        pIndex  += cmd_list->IdxBuffer.Size;
                    }
                });
            });
        }

        //
        // Get ready to render
        //
        auto UpdateRenderState = [&](xgpu::pipeline_instance& PipelineInstance)
        {
            CmdBuffer.setPipelineInstance(PipelineInstance);
            CmdBuffer.setBuffer(Prim.m_VertexBuffer);
            CmdBuffer.setBuffer(Prim.m_IndexBuffer);
            
            imgui_push_constants PushConstants;

            PushConstants.m_Scale[0] = 2.0f / draw_data->DisplaySize.x;
            PushConstants.m_Scale[1] = 2.0f / draw_data->DisplaySize.y;

            PushConstants.m_Translate[0] = -1.0f - draw_data->DisplayPos.x * PushConstants.m_Scale[0];
            PushConstants.m_Translate[1] = -1.0f - draw_data->DisplayPos.y * PushConstants.m_Scale[1];

            CmdBuffer.setPushConstants( &PushConstants, sizeof(imgui_push_constants) );
        };

        // Set it!
        UpdateRenderState(share_resources::getOrCreate()->m_DefaultPipelineInstance);

        // Will project scissor/clipping rectangles into framebuffer space
        ImVec2 clip_off   = draw_data->DisplayPos;          // (0,0) unless using multi-viewports
        ImVec2 clip_scale = draw_data->FramebufferScale;    // (1,1) unless using retina display which are often (2,2)

    // Render command lists
    // (Because we merged all buffers into a single one, we maintain our own offset into them)
        int global_vtx_offset = 0;
        int global_idx_offset = 0;
        xgpu::texture* pLastTextureUsed = nullptr;
        for (int n = 0; n < draw_data->CmdListsCount; n++)
        {
            const ImDrawList* cmd_list = draw_data->CmdLists[n];
            for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
            {
                const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
                if (pcmd->UserCallback != nullptr)
                {
                    // User callback, registered via ImDrawList::AddCallback()
                    // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
                    if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                    {
                        UpdateRenderState(share_resources::getOrCreate()->m_DefaultPipelineInstance);
                    }
                    else
                    {
                        // Project scissor/clipping rectangles into framebuffer space
                        ImVec4 clip_rect;
                        clip_rect.x = (pcmd->ClipRect.x - clip_off.x) * clip_scale.x;
                        clip_rect.y = (pcmd->ClipRect.y - clip_off.y) * clip_scale.y;
                        clip_rect.z = (pcmd->ClipRect.z - clip_off.x) * clip_scale.x;
                        clip_rect.w = (pcmd->ClipRect.w - clip_off.y) * clip_scale.y;

                        if (clip_rect.x < fb_width && clip_rect.y < fb_height && clip_rect.z >= 0.0f && clip_rect.w >= 0.0f)
                        {
                            // Better than nothing... but to do it right the user should set this by himself...
                            CmdBuffer.setViewport
                            ( clip_rect.x
                            , clip_rect.y
                            , clip_rect.z - clip_rect.x
                            , clip_rect.w - clip_rect.y
                            );

                            // Negative offsets are illegal for vkCmdSetScissor
                            if (clip_rect.x < 0.0f)
                                clip_rect.x = 0.0f;
                            if (clip_rect.y < 0.0f)
                                clip_rect.y = 0.0f;

                            // Apply scissor/clipping rectangle
                            CmdBuffer.setScissor
                            ( static_cast<int32_t>(clip_rect.x)
                            , static_cast<int32_t>(clip_rect.y)
                            , static_cast<int32_t>(clip_rect.z - clip_rect.x)
                            , static_cast<int32_t>(clip_rect.w - clip_rect.y)
                            );
                        }

                        pcmd->UserCallback(cmd_list, pcmd);

                        // Restore full window view port
                        CmdBuffer.setViewport
                        ( 0
                        , 0
                        , static_cast<float>(m_Window.getWidth())
                        , static_cast<float>(m_Window.getHeight())
                        );
                    }
                }
                else
                {
                    // Project scissor/clipping rectangles into framebuffer space
                    ImVec4 clip_rect;
                    clip_rect.x = (pcmd->ClipRect.x - clip_off.x) * clip_scale.x;
                    clip_rect.y = (pcmd->ClipRect.y - clip_off.y) * clip_scale.y;
                    clip_rect.z = (pcmd->ClipRect.z - clip_off.x) * clip_scale.x;
                    clip_rect.w = (pcmd->ClipRect.w - clip_off.y) * clip_scale.y;

                    if (clip_rect.x < fb_width && clip_rect.y < fb_height && clip_rect.z >= 0.0f && clip_rect.w >= 0.0f)
                    {
                        // Negative offsets are illegal for vkCmdSetScissor
                        if (clip_rect.x < 0.0f)
                            clip_rect.x = 0.0f;
                        if (clip_rect.y < 0.0f)
                            clip_rect.y = 0.0f;

                        // Apply scissor/clipping rectangle
                        CmdBuffer.setScissor
                        ( static_cast<int32_t>(clip_rect.x)
                        , static_cast<int32_t>(clip_rect.y)
                        , static_cast<int32_t>(clip_rect.z - clip_rect.x)
                        , static_cast<int32_t>(clip_rect.w - clip_rect.y)
                        );


                        //
                        // Bind the texture here
                        //
                        {
                            auto texture_handle = reinterpret_cast<xgpu::texture*>(pcmd->TexRef._TexID);

                         //   CmdBuffer.setTexture(*texture_handle); // You may need to adapt this to your API
                            if (pLastTextureUsed != texture_handle)
                            {
                                pLastTextureUsed = texture_handle;
                                if (texture_handle)
                                {
                                    assert(texture_handle->m_Private != nullptr && "Invalid texture pointer");
                                    share_resources::getOrCreate()->setTexture(CmdBuffer, *texture_handle);
                                }
                                else
                                {
                                    CmdBuffer.setPipelineInstance(share_resources::getOrCreate()->m_DefaultPipelineInstance);
                                }
                            }
                            
                        }

                        assert(size_t(pcmd->IdxOffset + global_idx_offset + pcmd->ElemCount) <= Prim.m_IndexBuffer.getEntryCount() && "Index offset out of bounds");
                        assert(size_t(pcmd->VtxOffset + global_vtx_offset) < Prim.m_VertexBuffer.getEntryCount() && "Vertex offset out of bounds");

                        // Draw
                        CmdBuffer.Draw
                        ( static_cast<int32_t>(pcmd->ElemCount)
                        , static_cast<int32_t>(pcmd->IdxOffset + global_idx_offset)
                        , static_cast<int32_t>(pcmd->VtxOffset + global_vtx_offset)
                        );
                    }
                }
            }
            global_idx_offset += cmd_list->IdxBuffer.Size;
            global_vtx_offset += cmd_list->VtxBuffer.Size;
        }
    }
};

//------------------------------------------------------------------------------------------------------------

struct breach_instance : window_info
{
    xgpu::mouse                         m_Mouse;
    xgpu::keyboard                      m_Keyboard;
    clock::time_point                   m_LastFrameTimer;
    
    //------------------------------------------------------------------------------------------------------------

    breach_instance( xgpu::instance& Intance, xgpu::window MainWindow ) noexcept
    : window_info{MainWindow }
    {
        Intance.Create( m_Mouse,    {} );
        Intance.Create( m_Keyboard, {} );
    }

    //------------------------------------------------------------------------------------------------------------

    xgpu::device::error* CreateFontsTexture(xgpu::texture& Texture, ImGuiIO& io ) noexcept
    {
        // Build texture atlas
        unsigned char*  pixels;
        int             width, height;

        // Load as RGBA 32-bits (75% of the memory is wasted, but default font is so small) because it is more likely to be compatible with user's existing shaders. 
        // If your ImTextureId represent a higher-level concept than just a GL texture id, consider calling GetTexDataAsAlpha8() instead to save on GPU memory.
        io.Fonts->GetTexDataAsRGBA32( &pixels, &width, &height );   

        // Create the texture in xgpu
        {
            auto Mip  = std::array{ xgpu::texture::setup::mip_size{ height * width * sizeof(std::uint32_t) } };

            xgpu::texture::setup    Setup;

            Setup.m_Height      = height;
            Setup.m_Width       = width;
            Setup.m_MipSizes    = Mip;
            Setup.m_AllFacesData = std::span{ reinterpret_cast<const std::byte*>(pixels), Mip[0].m_SizeInBytes * sizeof(char) };

            if (auto Err = m_Shared->m_Device.Create(Texture, Setup); Err)
                return Err;
        }

        // We could put here the texture id if we wanted 
        //io.Fonts->TexID = nullptr;
        io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(&Texture));

        return nullptr;
    }

    //------------------------------------------------------------------------------------------------------------

    xgpu::device::error* InitializePipeline( ImGuiIO& io ) noexcept
    {
        // Font Texture, we can keep it local because the pipeline instance will have a reference
        xgpu::texture Texture;
        if( auto Err = CreateFontsTexture( Texture, io ); Err ) return Err;


        //
        // Create the default pipeline
        //
        if ( auto Err = share_resources::getOrCreate()->CreteDefaultPipeline(Texture); Err )
            return Err;

        return nullptr;
    }

    //------------------------------------------------------------------------------------------------------------

    xgpu::device::error* StartNewFrame( ImGuiIO& io ) noexcept
    {
        //
        // Setup display size (every frame to accommodate for window resizing)
        // TODO: Note that display size is the actual drawable pixels       
        // glfwGetFramebufferSize(g_Window, &display_w, &display_h);
        const int display_w = m_Window.getWidth();
        const int display_h = m_Window.getHeight();

        io.DisplaySize              = ImVec2((float)display_w, (float)display_h);

        //if (w > 0 && h > 0)
        //    io.DisplayFramebufferScale = ImVec2((float)display_w / w, (float)display_h / h);

        //glfwGetWindowSize(g_Window, &w, &h);
        // TODO: Should be the size of the entire window...
        float w = io.DisplaySize.x;
        float h = io.DisplaySize.y;

        io.DisplayFramebufferScale  = ImVec2(w > 0 ? ((float)display_w / w) : 0, h > 0 ? ((float)display_h / h) : 0);

        //
        // Setup time step
        //
        {
            
            auto T1          = std::chrono::high_resolution_clock::now();
            io.DeltaTime     = std::chrono::duration<float>(T1 - m_LastFrameTimer).count();
            m_LastFrameTimer = T1;
        }

        //
        // Update the mouse
        //
        {
            io.MouseHoveredViewport = 0;

            std::array<bool, 3> MouseButtonStates
            { m_Mouse.isPressed(xgpu::mouse::digital::BTN_LEFT)
            , m_Mouse.isPressed(xgpu::mouse::digital::BTN_RIGHT)
            , m_Mouse.isPressed(xgpu::mouse::digital::BTN_MIDDLE)
            };

            for( auto& j : MouseButtonStates )
            {
                auto Index = static_cast<int>( &j - MouseButtonStates.data() );
                if ( j != share_resources::s_SharePointer->m_LastMouseButtonStates[Index] )
                {
                    io.AddMouseButtonEvent(Index, j);
                    share_resources::s_SharePointer->m_LastMouseButtonStates[Index] = j;
                }
            }

            if ( auto Wheel = m_Mouse.getValue(xgpu::mouse::analog::WHEEL_REL)[0]*2.8f; Wheel != 0.0f )
            {
                io.AddMouseWheelEvent(0.0f, Wheel);
            }

            // Windows get mouse pos
            //POINT MousePos;
            //GetCursorPos( &MousePos);
            //const auto MouseValues = m_Mouse.getValue(xgpu::mouse::analog::POS_ABS);

            // CURSORINFO CurInfo;
            // CurInfo.cbSize = sizeof(CURSORINFO);
            // GetCursorInfo( &CurInfo);

            const auto MouseValues = m_Mouse.getValue(xgpu::mouse::analog::POS_ABS);

            ImGuiPlatformIO& PlatformIO = ImGui::GetPlatformIO();
            for (int n = 0; n < PlatformIO.Viewports.Size; n++)
            {
                ImGuiViewport*  pViewport = PlatformIO.Viewports[n];
                auto&           Info    = *reinterpret_cast<window_info*>(pViewport->RendererUserData);

#ifdef __EMSCRIPTEN__
                const bool focused = true;
                IM_ASSERT(platform_io.Viewports.Size == 1);
#else
                const bool bFocused = Info.m_Window.isFocused();
#endif
                if(bFocused)
                {
                    if (io.WantSetMousePos)
                    {
                        Info.m_Window.setMousePosition(static_cast<int>(io.MousePos.x - pViewport->Pos.x), static_cast<int>(io.MousePos.y - pViewport->Pos.y));
                    }
                    else
                    {
                        if ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
                        {
                            auto [WindowX, WindowY] = Info.m_Window.getPosition();
                            //printf("Mouse[%d  %d]  Win+Mouse(%d  %d)\n", int(MouseValues[0]), int(MouseValues[1]), int(WindowX + MouseValues[0]), int(WindowY + MouseValues[1]));

                            // Multi-viewport mode: mouse position in OS absolute coordinates (io.MousePos is (0,0) when the mouse is on the upper-left of the primary monitor)
                            //io.MousePos = ImVec2( (float)WindowX + MouseValues[0], (float)WindowY + MouseValues[1] );
                            io.AddMousePosEvent(WindowX + MouseValues[0], WindowY + MouseValues[1]);
                        }
                        else
                        {
                            // Single viewport mode: mouse position in client window coordinates (io.MousePos is (0,0) when the mouse is on the upper-left corner of the app window)
                            //io.MousePos = ImVec2{ (float)MouseValues[0], (float)MouseValues[1] };
                            io.AddMousePosEvent(MouseValues[0], MouseValues[1]);
                        }
                    }
                }
            }
        }

        //
        // Update keys
        //
        {
            // Modifiers are not reliable across systems
            const bool KeyCtrl  = m_Keyboard.isPressed(xgpu::keyboard::digital::KEY_LCONTROL ) || m_Keyboard.isPressed(xgpu::keyboard::digital::KEY_RCONTROL );
            const bool KeyShift = m_Keyboard.isPressed(xgpu::keyboard::digital::KEY_LSHIFT   ) || m_Keyboard.isPressed(xgpu::keyboard::digital::KEY_RSHIFT   );
            const bool KeyAlt   = m_Keyboard.isPressed(xgpu::keyboard::digital::KEY_LALT     ) || m_Keyboard.isPressed(xgpu::keyboard::digital::KEY_RALT     );
            const bool KeySuper = m_Keyboard.isPressed(xgpu::keyboard::digital::KEY_LWIN     ) || m_Keyboard.isPressed(xgpu::keyboard::digital::KEY_RWIN     );
            io.AddKeyEvent(ImGuiKey_ModCtrl,    KeyCtrl);
            io.AddKeyEvent(ImGuiKey_ModShift,   KeyShift);
            io.AddKeyEvent(ImGuiKey_ModAlt,     KeyAlt);
            io.AddKeyEvent(ImGuiKey_ModSuper,   KeySuper);

            bool bPresses = false;
            for( int i = 1; i < static_cast<int>(xgpu::keyboard::digital::ENUM_COUNT); i++ )
            {
                const bool j = m_Keyboard.isPressed(static_cast<xgpu::keyboard::digital>(i));
                if( j != share_resources::s_SharePointer->m_LastsKeyState[i] )
                {
                    auto it = share_resources::s_SharePointer->m_xGPUToImGuiKey.find(i);
                    if (it != share_resources::s_SharePointer->m_xGPUToImGuiKey.end())
                    {
                        assert(it->second >= 0 && it->second < ImGuiKey_COUNT && "Invalid ImGuiKey value");
                        io.AddKeyEvent(static_cast<ImGuiKey>(it->second), j);
                        share_resources::s_SharePointer->m_LastsKeyState[i] = j;
                    }
                    bPresses |= j;
                }
            }

            //
            // Write into text boxes
            //
            {
                constexpr float initial_extra_wait_time_v   = -0.5f;
                constexpr float repeat_rate_v               =  0.08f;
                static int      LastChar                    =  0;
                static float    Sleep                       =  initial_extra_wait_time_v;
                if( bPresses && !io.KeyCtrl )
                {
                    if( LastChar == m_Keyboard.getLatestChar() )
                    {
                        Sleep += io.DeltaTime;
                        if(Sleep < repeat_rate_v) 
                        {
                            //io.ClearInputCharacters();
                        }
                        else
                        {
                            Sleep = 0;
                            io.AddInputCharacter(LastChar);
                        }
                    }
                    else
                    {
                        Sleep    = initial_extra_wait_time_v;
                        LastChar = m_Keyboard.getLatestChar();
                        io.AddInputCharacter(LastChar);
                    }
                }
                else
                {
                    LastChar = 0;
                }
            }                
        }

        // Focus events (check if window focus changed)
        static bool was_focused = false;
        bool is_focused = m_Window.isFocused(); // Assuming xGPU provides a focus check
        if (is_focused != was_focused)
        {
            io.AddFocusEvent(is_focused);
            printf("Window Focus: %s\n", is_focused ? "Gained" : "Lost");
            was_focused = is_focused;
        }

        // Start the frame
        ImGui::NewFrame();

        return nullptr;
    }
};

//------------------------------------------------------------------------------------------------------------

void EnableDocking()
{
    //
    // Enable docking
    //
    constexpr bool                  opt_padding = false;
    constexpr ImGuiDockNodeFlags    dockspace_flags = ImGuiDockNodeFlags_AutoHideTabBar
                                                    | ImGuiDockNodeFlags_PassthruCentralNode;
    constexpr ImGuiWindowFlags      window_flags    = 0//ImGuiWindowFlags_MenuBar
                                                    | ImGuiWindowFlags_NoDocking
                                                    | ImGuiWindowFlags_NoBackground
                                                    | ImGuiWindowFlags_NoTitleBar
                                                    | ImGuiWindowFlags_NoCollapse
                                                    | ImGuiWindowFlags_NoResize
                                                    | ImGuiWindowFlags_NoMove
                                                    | ImGuiWindowFlags_NoBringToFrontOnFocus
                                                    | ImGuiWindowFlags_NoNavFocus;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (!opt_padding)
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    bool open = true;
    ImGui::Begin("Main DockSpace", &open, window_flags);
    ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
    ImGui::End();

    if (!opt_padding)
        ImGui::PopStyleVar();
    ImGui::PopStyleVar(2);
}

//------------------------------------------------------------------------------------------------------------

bool BeginRendering( bool bEnableDocking ) noexcept
{
    GETINSTANCE;
    if (Instance.m_Window.BeginRendering())
        return true;

    if (auto Err = Instance.StartNewFrame(io))
    {
        printf("StartNewFrame failed: %s\n", Err );
        return true;
    }

    if(bEnableDocking) EnableDocking();

    return false;
}

//------------------------------------------------------------------------------------------------------------

void Render( void ) noexcept
{
    GETINSTANCE;
    ImGui::Render();
    ImDrawData* pMainDrawData = ImGui::GetDrawData();
    Instance.Render( io, pMainDrawData );

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
    ImGui::EndFrame();
}

//------------------------------------------------------------------------------------------------------------

static
void CreateChildWindow( ImGuiViewport* pViewport ) noexcept
{
/*
    GETINSTANCE;
    auto& Info = *new window_info();
    pViewport->RendererUserData = &Info;
*/
    GETINSTANCE;
    auto pInfo = new window_info();
    xgpu::window::setup Setup;
    Setup.m_Width   = static_cast<int>(pViewport->Size.x);
    Setup.m_Height  = static_cast<int>(pViewport->Size.y);
    Setup.m_X       = static_cast<int>(pViewport->Pos.x);
    Setup.m_Y       = static_cast<int>(pViewport->Pos.y);
    if (auto Err = Instance.m_Shared->m_Device.Create(pInfo->m_Window, Setup))
    {
        printf("Failed to create child window: %s\n", Err );
        delete pInfo;
        return;
    }
    pViewport->RendererUserData = pInfo;
    pViewport->PlatformHandle = reinterpret_cast<void*>(&pInfo->m_Window);
}

//------------------------------------------------------------------------------------------------------------

static
void DestroyChildWindow(ImGuiViewport* pViewport) noexcept
{
    /*
    auto pInfo = reinterpret_cast<window_info*>(pViewport->RendererUserData);
    delete pInfo;
    pViewport->RendererUserData = nullptr;
    */
    auto pInfo = reinterpret_cast<window_info*>(pViewport->RendererUserData);
    if (pInfo)
    {
        // Ensure xgpu::window is properly destroyed if needed
        delete pInfo;
    }
    pViewport->RendererUserData = nullptr;
    pViewport->PlatformHandle = nullptr;
}

/*
//------------------------------------------------------------------------------------------------------------

static
void SetChildWindowSize(ImGuiViewport* pViewport, ImVec2 size ) noexcept
{
    auto& Info = *reinterpret_cast<window_info*>(pViewport->RendererUserData);

    // This better send a WM_SIZE message!
    SetWindowPos
    ( reinterpret_cast<HWND>(Info.m_Window.getSystemWindowHandle())
    , HWND_TOPMOST
    , -1
    , -1 
    , static_cast<int>(size.x)
    , static_cast<int>(size.y)
    , SWP_NOMOVE | SWP_NOZORDER
    );
}
*/

//------------------------------------------------------------------------------------------------------------

static
ImVec2 GetChildWindowSize(ImGuiViewport* pViewport ) noexcept
{
    auto& Info = *reinterpret_cast<window_info*>(pViewport->RendererUserData);
    return { (float)Info.m_Window.getWidth(), (float)Info.m_Window.getHeight() };
}

//------------------------------------------------------------------------------------------------------------

static
void SetChildWindowPos(ImGuiViewport* pViewport, ImVec2 pos) noexcept
{
    auto& Info = *reinterpret_cast<window_info*>(pViewport->RendererUserData);
    Info.m_Window.setPosition(static_cast<int>(pos.x), static_cast<int>(pos.y));
}

//------------------------------------------------------------------------------------------------------------

static
void SetChildWindowSize(ImGuiViewport* pViewport, ImVec2 size) noexcept
{
    auto& Info = *reinterpret_cast<window_info*>(pViewport->RendererUserData);
    Info.m_Window.setSize(static_cast<int>(size.x), static_cast<int>(size.y));
}

//------------------------------------------------------------------------------------------------------------

static
ImVec2 GetChildWindowPos(ImGuiViewport* pViewport)
{
    auto& Info = *reinterpret_cast<window_info*>(pViewport->RendererUserData);
    auto [X,Y] = Info.m_Window.getPosition();
    return ImVec2((float)X, (float)Y);
}

//------------------------------------------------------------------------------------------------------------

static
void RenderChildWindow(ImGuiViewport* pViewport, void*) noexcept
{
    GETINSTANCE;
    auto& Info = *reinterpret_cast<window_info*>(pViewport->RendererUserData);
    Info.Render( io, pViewport->DrawData );
}

//------------------------------------------------------------------------------------------------------------

static
void ChildSwapBuffers(ImGuiViewport* pViewport, void*) noexcept
{
    auto& Info = *reinterpret_cast<window_info*>(pViewport->RendererUserData);
    Info.m_Window.PageFlip();
}

//------------------------------------------------------------------------------------------------------------

ImFont*& getFont(int Index) noexcept
{
    return ImGui::GetIO().Fonts->Fonts[Index];
}

//------------------------------------------------------------------------------------------------------------

xgpu::device::error* CreateInstance( xgpu::window& MainWindow ) noexcept
{
    xgpu::instance  XGPUInstance;
    xgpu::device    Device;

    MainWindow.getDevice(Device);
    Device.getInstance(XGPUInstance);

    //
    // Setup Dear ImGui context
    //
    if( nullptr == ImGui::GetCurrentContext() )
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
    }

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags = 0;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
    io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
    io.BackendRendererName = "xgpu_imgui_breach";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.

    io.ConfigDebugIniSettings = false; // Debug INI settings
    //io.IniFilename = nullptr; // Prevent loading/saving .ini files
    //ImGui::GetCurrentContext()->SettingsIniData.clear();
    //ImGui::ClearIniSettings();
    //printf("SettingsIniData after clear: %s\n", ImGui::GetCurrentContext()->SettingsIniData.c_str());

  //  io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;  // We can create multi-viewports on the Renderer side (optional)
 //   io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;  // We can create multi-viewports on the Platform side (optional)
    //io.ConfigViewportsNoAutoMerge = true;
    //io.ConfigViewportsNoTaskBarIcon = true;

    
    {
        // Load a font that supports Unicode symbols (e.g., Segoe UI Emoji)
        //ImFontConfig config;
        //config.MergeMode = true;
        //io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/consola.ttf", 12.0f, nullptr, glyph_ranges1);
        //io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/SEGOEICONS.TTF", 12.0f, nullptr, glyph_ranges2);
        //io.Fonts->Build();

        static const ImWchar glyph_ranges1[] = 
        {
            0x0020, 0xE000, 0
        };

        static const ImWchar glyph_ranges2[] =
        {
            0xE000, 0xF8CC, 0
        };

        ImGuiIO& io = ImGui::GetIO();
        ImFontAtlas* atlas = io.Fonts;
        atlas->Clear();

        // Load the first font (Consolas)
        ImFontConfig config1;
        config1.PixelSnapH = true;
        ImFont* font1 = atlas->AddFontFromFileTTF("C:/Windows/Fonts/consola.ttf", 12.0f, &config1, glyph_ranges1);
        assert(font1 != nullptr && "Failed to load consola.ttf");

        // Load and merge the second font (Segoe Icons)
        ImFontConfig config2;
        config2.MergeMode = true;   // Merge into the previous font
        config2.PixelSnapH = true;  // Align horizontally
        config2.GlyphOffset.y = 3.0f; // Move icons down
        ImFont* font2 = atlas->AddFontFromFileTTF("C:/Windows/Fonts/SEGOEICONS.TTF", 12.0f, &config2, glyph_ranges2);
        assert(font2 != nullptr && "Failed to load SEGOEICONS.TTF");


        ImFont* font3 = atlas->AddFontFromFileTTF("C:/Windows/Fonts/consolab.ttf", 12.0f, &config1, glyph_ranges1);
        ImFont* font4 = atlas->AddFontFromFileTTF("C:/Windows/Fonts/SEGOEICONS.TTF", 12.0f, &config2, glyph_ranges2);

        ImFont* font5 = atlas->AddFontFromFileTTF("C:/Windows/Fonts/SEGOEICONS.TTF", 64.0f, &config1, glyph_ranges2);

        // Build the atlas
        bool success = atlas->Build();
        assert(success && "Failed to build font atlas");

        // Optional: Store font1 or font2 in io.FontDefault if you want it as the default
        io.FontDefault = font1;
    }

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
        platform_io.Renderer_CreateWindow       = CreateChildWindow;
        platform_io.Renderer_DestroyWindow      = DestroyChildWindow;
        platform_io.Renderer_SetWindowSize      = SetChildWindowSize;
        platform_io.Renderer_RenderWindow       = RenderChildWindow;
        platform_io.Renderer_SwapBuffers        = ChildSwapBuffers;

        platform_io.Platform_CreateWindow       = CreateChildWindow;
        platform_io.Platform_DestroyWindow      = DestroyChildWindow;
        platform_io.Platform_ShowWindow         = [](ImGuiViewport* pViewport) {};
        platform_io.Platform_SetWindowPos       = SetChildWindowPos;
        platform_io.Platform_GetWindowPos       = GetChildWindowPos;
        platform_io.Platform_SetWindowSize      = SetChildWindowSize;
        platform_io.Platform_GetWindowSize      = GetChildWindowSize;
        platform_io.Platform_SetWindowTitle     = [](ImGuiViewport* pViewport, const char*){};
        platform_io.Platform_RenderWindow       = RenderChildWindow;
        platform_io.Platform_SwapBuffers        = ChildSwapBuffers;


        platform_io.Platform_SetWindowFocus = [](ImGuiViewport* pViewport)
        {
            auto& Info = *reinterpret_cast<window_info*>(pViewport->RendererUserData);
            Info.m_Window.setFocus();
        };

        platform_io.Platform_GetWindowFocus = [](ImGuiViewport* pViewport)
        {
            auto& Info = *reinterpret_cast<window_info*>(pViewport->RendererUserData);
            return Info.m_Window.isFocused();
        };

        platform_io.Platform_GetWindowMinimized = [](ImGuiViewport* pViewport)
        {
            auto& Info = *reinterpret_cast<window_info*>(pViewport->RendererUserData);
            return Info.m_Window.isMinimized();
        };


        platform_io.Monitors.resize(0);
        ImGuiPlatformMonitor monitor;
        monitor.MainPos = monitor.WorkPos = ImVec2((float)0, (float)0);
        monitor.MainSize = monitor.WorkSize = ImVec2((float)4000.0f, (float)4000.0f);
        platform_io.Monitors.push_back(monitor);
    }

    //
    // Initialize the instance
    //
    share_resources::getOrCreate()->m_Device = Device;
    auto Instance = std::make_unique<breach_instance>(XGPUInstance, MainWindow);
    if( auto Err = Instance->InitializePipeline(io); Err ) return Err;
    Instance->m_LastFrameTimer = clock::now();
    io.UserData = Instance.release();

    //
    // Setup backend capabilities flags
    //

    ImGuiViewport* main_viewport    = ImGui::GetMainViewport();
    main_viewport->RendererUserData = io.UserData;
    main_viewport->PlatformHandle   = io.UserData;

    //
    // Setup default style
    //
    ImGui::StyleColorsDark();

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    ImGuiStyle& style = ImGui::GetStyle();
    if( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
    {
        style.WindowRounding                = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w   = 1.0f;
    }

    return nullptr;
}

//------------------------------------------------------------------------------------------------------------

void Shutdown( void ) noexcept
{
    GETINSTANCE
    std::unique_ptr<breach_instance>{ reinterpret_cast<breach_instance*>(&Instance) };
    io.UserData = nullptr;
}

//------------------------------------------------------------------------------------------------------------

bool isInputSleeping(void) noexcept
{
    GETINSTANCE

    //
    // Check mouse
    //
    for (int i = 0; i < (int)xgpu::mouse::digital::ENUM_COUNT; ++i )
    {
        if (Instance.m_Mouse.wasPressed((xgpu::mouse::digital)i)) return false;
    }

    //
    // Check keyboard
    //
    for( int i=0; i < (int)xgpu::keyboard::digital::ENUM_COUNT; ++i )
    {
        if( Instance.m_Keyboard.wasPressed( (xgpu::keyboard::digital)i ) ) return false;
    }

    return true;
}


//------------------------------------------------------------------------------------------------------------
} //END (xgpu::tools::imgui)
