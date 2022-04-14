
#include "imgui.h"
#include "xGPU.h"
#include <windows.h>

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

struct window_info
{
    struct buffers
    {
        xgpu::buffer                m_VertexBuffer{};
        xgpu::buffer                m_IndexBuffer{};
    };

    xgpu::device                m_Device            {};
    xgpu::window                m_Window            {};
    xgpu::vertex_descriptor     m_VertexDescritor   {};
    std::array<buffers, 2>      m_PrimitiveBuffers  {};
    int                         m_iFrame            {0};

    //------------------------------------------------------------------------------------------------------------

    window_info( xgpu::device& D, xgpu::window& Window ) noexcept
    : m_Device{ D }
    , m_Window{ Window }
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

        auto Err = m_Device.Create(m_VertexDescritor, Setup);
        assert(Err == nullptr);

        InitializeBuffers();
    }

    //------------------------------------------------------------------------------------------------------------

    window_info(xgpu::device& D, xgpu::vertex_descriptor& VertexDescritor ) noexcept
    : m_Device{ D }
    , m_VertexDescritor{ VertexDescritor }
    {
        auto Err = m_Device.Create(m_Window, {});
        assert( Err == nullptr );
        InitializeBuffers();
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
                if (auto Err = m_Device.Create( Prim.m_VertexBuffer, Setup ); Err) return Err;
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
                if (auto Err = m_Device.Create( Prim.m_IndexBuffer, Setup ); Err) return Err;
        }

        return nullptr;
    }

    //------------------------------------------------------------------------------------------------------------

    void Render( ImGuiIO& io, ImDrawData* draw_data, xgpu::pipeline_instance& PipelineInstance ) noexcept
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
                assert(Err == nullptr);
            }

            if (draw_data->TotalIdxCount > Prim.m_IndexBuffer.getEntryCount())
            {
                auto Err = Prim.m_IndexBuffer.Resize(draw_data->TotalIdxCount);
                assert(Err == nullptr);
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
        auto UpdateRenderState = [&]
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
        UpdateRenderState();

        // Will project scissor/clipping rectangles into framebuffer space
        ImVec2 clip_off   = draw_data->DisplayPos;          // (0,0) unless using multi-viewports
        ImVec2 clip_scale = draw_data->FramebufferScale;    // (1,1) unless using retina display which are often (2,2)

    // Render command lists
    // (Because we merged all buffers into a single one, we maintain our own offset into them)
        int global_vtx_offset = 0;
        int global_idx_offset = 0;
        for (int n = 0; n < draw_data->CmdListsCount; n++)
        {
            const ImDrawList* cmd_list = draw_data->CmdLists[n];
            for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
            {
                const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
                if (pcmd->UserCallback != NULL)
                {
                    // User callback, registered via ImDrawList::AddCallback()
                    // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
                    if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                    {
                        UpdateRenderState();
                    }
                    else
                    {
                        pcmd->UserCallback(cmd_list, pcmd);
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
    xgpu::pipeline_instance             m_PipelineInstance;
    xgpu::mouse                         m_Mouse;
    xgpu::keyboard                      m_Keyboard;
    clock::time_point                   m_LastFrameTimer;
    
    //------------------------------------------------------------------------------------------------------------

    breach_instance( xgpu::instance& Intance, xgpu::device& D, xgpu::window MainWindow ) noexcept
    : window_info{D, MainWindow }
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
            std::array<xgpu::texture::setup::mip,1> Mip{ height * width * sizeof(std::uint32_t) };
            xgpu::texture::setup                    Setup;

            Setup.m_Height      = height;
            Setup.m_Width       = width;
            Setup.m_MipChain    = Mip;
            Setup.m_Data        = { reinterpret_cast<const std::byte*>(pixels), Mip[0].m_Size };

            if (auto Err = m_Device.Create(Texture, Setup); Err)
                return Err;
        }

        // We could put here the texture id if we wanted 
        io.Fonts->TexID = nullptr;

        return nullptr;
    }

    //------------------------------------------------------------------------------------------------------------

    xgpu::device::error* InitializePipeline( ImGuiIO& io ) noexcept
    {
        // Font Texture, we can keep it local because the pipeline instance will have a reference
        xgpu::texture Texture;
        if( auto Err = CreateFontsTexture( Texture, io ); Err ) return Err;

        //
        // Define the pipeline
        //
        xgpu::pipeline PipeLine;
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
                .m_VertexDescriptor  = m_VertexDescritor
            ,   .m_Shaders           = Shaders
            ,   .m_PushConstantsSize = sizeof(imgui_push_constants)
            ,   .m_Samplers          = Samplers
            ,   .m_Primitive         = { .m_Cull = xgpu::pipeline::primitive::cull::NONE }
            ,   .m_DepthStencil      = { .m_bDepthTestEnable = false }
            ,   .m_Blend             = xgpu::pipeline::blend::getAlphaOriginal()
            };

            if ( auto Err = m_Device.Create(PipeLine, Setup); Err ) return Err;
        }

        //
        // Create Pipeline Instance
        //
        auto Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{ Texture } };
        auto Setup    = xgpu::pipeline_instance::setup
        { .m_PipeLine         = PipeLine
        , .m_SamplersBindings = Bindings
        };

        if (auto Err = m_Device.Create(m_PipelineInstance, Setup); Err) return Err;

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

            io.MouseDown[0] = m_Mouse.isPressed(xgpu::mouse::digital::BTN_LEFT   );
            io.MouseDown[1] = m_Mouse.isPressed(xgpu::mouse::digital::BTN_RIGHT  );
            io.MouseDown[2] = m_Mouse.isPressed(xgpu::mouse::digital::BTN_MIDDLE );

            io.MouseWheel   = m_Mouse.getValue(xgpu::mouse::analog::WHEEL_REL)[0];

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
                        // SetCursorPos(MouseValues[0] - pViewport->Pos.x, MouseValues[1] - pViewport->Pos.y );
                        assert(false);
                    }
                    else
                    {
                        if ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
                        {
                            auto [WindowX, WindowY] = Info.m_Window.getPosition();
                            //printf("Mouse[%d  %d]  Win+Mouse(%d  %d)\n", int(MouseValues[0]), int(MouseValues[1]), int(WindowX + MouseValues[0]), int(WindowY + MouseValues[1]));

                            // Multi-viewport mode: mouse position in OS absolute coordinates (io.MousePos is (0,0) when the mouse is on the upper-left of the primary monitor)
                            io.MousePos = ImVec2( (float)WindowX + MouseValues[0], (float)WindowY + MouseValues[1] );
                        }
                        else
                        {
                            // Single viewport mode: mouse position in client window coordinates (io.MousePos is (0,0) when the mouse is on the upper-left corner of the app window)
                            io.MousePos = ImVec2{ (float)MouseValues[0], (float)MouseValues[1] };
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
            io.KeyCtrl  = m_Keyboard.isPressed(xgpu::keyboard::digital::KEY_LCONTROL ) || m_Keyboard.isPressed(xgpu::keyboard::digital::KEY_RCONTROL );
            io.KeyShift = m_Keyboard.isPressed(xgpu::keyboard::digital::KEY_LSHIFT   ) || m_Keyboard.isPressed(xgpu::keyboard::digital::KEY_RSHIFT   );
            io.KeyAlt   = m_Keyboard.isPressed(xgpu::keyboard::digital::KEY_LALT     ) || m_Keyboard.isPressed(xgpu::keyboard::digital::KEY_RALT     );
            io.KeySuper = m_Keyboard.isPressed(xgpu::keyboard::digital::KEY_LWIN     ) || m_Keyboard.isPressed(xgpu::keyboard::digital::KEY_RWIN     );

            bool bPresses = false;
            for( int i = 1; i < static_cast<int>(xgpu::keyboard::digital::ENUM_COUNT); i++ )
            {
                io.KeysDown[i] = m_Keyboard.isPressed(static_cast<xgpu::keyboard::digital>(i));
                bPresses |= io.KeysDown[i];
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
    constexpr ImGuiWindowFlags      window_flags = ImGuiWindowFlags_MenuBar
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

    ImGui::Begin("Main DockSpace", nullptr, window_flags);
    if (!opt_padding)
        ImGui::PopStyleVar();

    ImGui::PopStyleVar(2);

    static ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
    ImGui::End();
}

//------------------------------------------------------------------------------------------------------------

bool BeginRendering( bool bEnableDocking ) noexcept
{
    GETINSTANCE;
    if (Instance.m_Window.BeginRendering())
        return true;

    Instance.StartNewFrame(io);

    if(bEnableDocking) EnableDocking();

    return false;
}

//------------------------------------------------------------------------------------------------------------

void Render( void ) noexcept
{
    GETINSTANCE;
    ImGui::Render();
    ImDrawData* pMainDrawData = ImGui::GetDrawData();
    Instance.Render( io, pMainDrawData, Instance.m_PipelineInstance );
}

//------------------------------------------------------------------------------------------------------------

static
void CreateChildWindow( ImGuiViewport* pViewport ) noexcept
{
    GETINSTANCE;
    auto& Info = *new window_info( Instance.m_Device, Instance.m_VertexDescritor );
    pViewport->RendererUserData = &Info;
}

//------------------------------------------------------------------------------------------------------------

static
void DestroyChildWindow(ImGuiViewport* pViewport) noexcept
{
    auto pInfo = reinterpret_cast<window_info*>(pViewport->RendererUserData);
    delete pInfo;
    pViewport->RendererUserData = nullptr;
}

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

//------------------------------------------------------------------------------------------------------------

static
ImVec2 GetChildWindowSize(ImGuiViewport* pViewport ) noexcept
{
    auto& Info = *reinterpret_cast<window_info*>(pViewport->RendererUserData);
    return { (float)Info.m_Window.getWidth(), (float)Info.m_Window.getHeight() };
}
//------------------------------------------------------------------------------------------------------------

static
void SetChildWindowPos(ImGuiViewport* pViewport, ImVec2 size) noexcept
{


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
    Info.Render( io, pViewport->DrawData, Instance.m_PipelineInstance );
}

//------------------------------------------------------------------------------------------------------------

static
void ChildSwapBuffers(ImGuiViewport* pViewport, void*) noexcept
{
    auto& Info = *reinterpret_cast<window_info*>(pViewport->RendererUserData);
    Info.m_Window.PageFlip();
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
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
  //  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
    io.BackendRendererName = "xgpu_imgui_breach";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
  //  io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;  // We can create multi-viewports on the Renderer side (optional)
 //   io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;  // We can create multi-viewports on the Platform side (optional)
    //io.ConfigViewportsNoAutoMerge = true;
    //io.ConfigViewportsNoTaskBarIcon = true;

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
        platform_io.Renderer_CreateWindow  = CreateChildWindow;
        platform_io.Renderer_DestroyWindow = DestroyChildWindow;
        platform_io.Renderer_SetWindowSize = SetChildWindowSize;
        platform_io.Renderer_RenderWindow  = RenderChildWindow;
        platform_io.Renderer_SwapBuffers   = ChildSwapBuffers;

        platform_io.Platform_CreateWindow       = CreateChildWindow;
        platform_io.Platform_DestroyWindow      = DestroyChildWindow;
        platform_io.Platform_ShowWindow         = [](ImGuiViewport* pViewport) {};
        platform_io.Platform_SetWindowPos       = SetChildWindowPos;
        platform_io.Platform_GetWindowPos       = GetChildWindowPos;
        platform_io.Platform_SetWindowSize      = SetChildWindowSize;
        platform_io.Platform_GetWindowSize      = GetChildWindowSize;
//        platform_io.Platform_SetWindowFocus     = ImGui_ImplGlfw_SetWindowFocus;
//        platform_io.Platform_GetWindowFocus     = ImGui_ImplGlfw_GetWindowFocus;
//        platform_io.Platform_GetWindowMinimized = ImGui_ImplGlfw_GetWindowMinimized;
        platform_io.Platform_SetWindowTitle     = [](ImGuiViewport* pViewport, const char*){};
        platform_io.Platform_RenderWindow       = RenderChildWindow;
        platform_io.Platform_SwapBuffers        = ChildSwapBuffers;

        platform_io.Monitors.resize(0);
        ImGuiPlatformMonitor monitor;
        monitor.MainPos = monitor.WorkPos = ImVec2((float)0, (float)0);
        monitor.MainSize = monitor.WorkSize = ImVec2((float)4000.0f, (float)4000.0f);
        platform_io.Monitors.push_back(monitor);
    }

    io.KeyMap[ImGuiKey_Tab]         = static_cast<int>( xgpu::keyboard::digital::KEY_TAB       );                         // Keyboard mapping. ImGui will use those indices to peek into the io.KeyDown[] array.
    io.KeyMap[ImGuiKey_LeftArrow]   = static_cast<int>( xgpu::keyboard::digital::KEY_LEFT      );
    io.KeyMap[ImGuiKey_RightArrow]  = static_cast<int>( xgpu::keyboard::digital::KEY_RIGHT     );
    io.KeyMap[ImGuiKey_UpArrow]     = static_cast<int>( xgpu::keyboard::digital::KEY_UP        );
    io.KeyMap[ImGuiKey_DownArrow]   = static_cast<int>( xgpu::keyboard::digital::KEY_DOWN      );
    io.KeyMap[ImGuiKey_PageUp]      = static_cast<int>( xgpu::keyboard::digital::KEY_PAGEUP    );
    io.KeyMap[ImGuiKey_PageDown]    = static_cast<int>( xgpu::keyboard::digital::KEY_PAGEDOWN  );
    io.KeyMap[ImGuiKey_Home]        = static_cast<int>( xgpu::keyboard::digital::KEY_HOME      );
    io.KeyMap[ImGuiKey_End]         = static_cast<int>( xgpu::keyboard::digital::KEY_END       );
    io.KeyMap[ImGuiKey_Delete]      = static_cast<int>( xgpu::keyboard::digital::KEY_DELETE    );
    io.KeyMap[ImGuiKey_Backspace]   = static_cast<int>( xgpu::keyboard::digital::KEY_BACKSPACE );
    io.KeyMap[ImGuiKey_Enter]       = static_cast<int>( xgpu::keyboard::digital::KEY_ENTER     );
    io.KeyMap[ImGuiKey_Escape]      = static_cast<int>( xgpu::keyboard::digital::KEY_ESCAPE    );
    io.KeyMap[ImGuiKey_A]           = static_cast<int>( xgpu::keyboard::digital::KEY_A         );
    io.KeyMap[ImGuiKey_C]           = static_cast<int>( xgpu::keyboard::digital::KEY_C         );
    io.KeyMap[ImGuiKey_V]           = static_cast<int>( xgpu::keyboard::digital::KEY_V         );
    io.KeyMap[ImGuiKey_X]           = static_cast<int>( xgpu::keyboard::digital::KEY_X         );
    io.KeyMap[ImGuiKey_Y]           = static_cast<int>( xgpu::keyboard::digital::KEY_Y         );
    io.KeyMap[ImGuiKey_Z]           = static_cast<int>( xgpu::keyboard::digital::KEY_Z         );
    io.KeyMap[ImGuiKey_Space]       = static_cast<int>( xgpu::keyboard::digital::KEY_SPACE     );

    //
    // Initialize the instance
    //
    auto Instance = std::make_unique<breach_instance>(XGPUInstance, Device, MainWindow);
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
