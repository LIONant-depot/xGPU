
#include "imgui.h"
#include "xGPU.h"
#include <windows.h>

namespace xgpu::tools::imgui {
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
static auto g_VertShaderSPV = std::array
{
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
};

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
static auto g_FragShaderSPV = std::array
{
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
};

//------------------------------------------------------------------------------------------------------------

#define GETINSTANCE                                                                             \
    ImGuiIO&            io        = ImGui::GetIO();                                             \
    breach_instance&    Instance  = *reinterpret_cast<breach_instance*>(io.UserData);

//------------------------------------------------------------------------------------------------------------

struct imgui_vert
{
    float           m_X, m_Y, m_Z;
    float           m_U, m_V;
    std::uint32_t   m_Color;
};

//------------------------------------------------------------------------------------------------------------

using clock        = std::chrono::high_resolution_clock;

//------------------------------------------------------------------------------------------------------------

struct window_info
{
    xgpu::device                m_Device            {};
    xgpu::window                m_Window            {};
    xgpu::buffer                m_VertexBuffer      {};
    xgpu::buffer                m_IndexBuffer       {};
    xgpu::vertex_descriptor     m_VertexDescritor   {};

    //------------------------------------------------------------------------------------------------------------

    window_info( xgpu::device& D, xgpu::window& Window ) noexcept
    : m_Device{ D }
    , m_Window{ Window }
    {
        auto Attributes = std::array
        {
            xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(imgui_vert, m_X)
            ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT3
            }
        ,   xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(imgui_vert, m_U)
            ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT2
            }
        ,   xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(imgui_vert, m_Color)
            ,   .m_Format = xgpu::vertex_descriptor::format::UINT8_RGBA_NORMALIZED
            }
        };
        auto Setup = xgpu::vertex_descriptor::setup
        {
            .m_VertexSize = sizeof(imgui_vert)
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
            , .m_EntryByteSize  = sizeof(imgui_vert)
            , .m_EntryCount     = 3 * 1000
            };

            if (auto Err = m_Device.Create(m_VertexBuffer, Setup ); Err) return Err;
        }

        //
        // Create Index buffer
        //
        {
            xgpu::buffer::setup Setup =
            { .m_Type           = xgpu::buffer::type::INDEX
            , .m_Usage          = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ
            , .m_EntryByteSize  = sizeof(std::uint32_t)
            , .m_EntryCount     = 3 * 1000
            };

            if (auto Err = m_Device.Create(m_IndexBuffer, Setup ); Err) return Err;
        }

        return nullptr;
    }

    //------------------------------------------------------------------------------------------------------------

    void Render( ImGuiIO& io, ImDrawData* draw_data, xgpu::pipeline_instance& PipelineInstance ) noexcept
    {
        if( draw_data->CmdListsCount == 0 ) return;

        // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
        const int fb_width  = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
        const int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
        if (fb_width <= 0 || fb_height <= 0)
            return;

        // If we have primitives to render
        if (draw_data->TotalVtxCount > 0)
        {
            if (draw_data->TotalVtxCount > m_VertexBuffer.getEntryCount() )
            {
                auto Err = m_VertexBuffer.Resize(draw_data->TotalVtxCount);
                assert(Err == nullptr);
            }

            if (draw_data->TotalIdxCount > m_IndexBuffer.getEntryCount())
            {
                auto Err = m_IndexBuffer.Resize(draw_data->TotalIdxCount);
                assert(Err == nullptr);
            }

            //
            // Copy over the vertices
            //
            m_VertexBuffer.MemoryMap( 0, m_VertexBuffer.getEntryCount(), [&](void* pV)
            {
                m_IndexBuffer.MemoryMap(0, m_IndexBuffer.getEntryCount(), [&](void* pI)
                {
                    auto pVertex = reinterpret_cast<imgui_vert*>(pV);
                    auto pIndex = reinterpret_cast<std::uint32_t*>(pI);

                    for (int n = 0; n < draw_data->CmdListsCount; n++)
                    {
                        const ImDrawList* cmd_list = draw_data->CmdLists[n];
                        std::memcpy(pVertex, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(imgui_vert));
                        std::memcpy(pIndex,  cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(std::uint32_t));
                        pVertex += cmd_list->VtxBuffer.Size;
                        pIndex  += cmd_list->IdxBuffer.Size;
                    }
                });
            });
        }

        //
        // Get ready to render
        //
        auto CmdBuffer = m_Window.getCmdBuffer();
        CmdBuffer.setPipelineInstance( PipelineInstance );
        CmdBuffer.setBuffer(m_VertexBuffer);
        CmdBuffer.setBuffer(m_IndexBuffer);

        // Set the push contants
        {
            auto Scale = std::array
            { 2.0f / draw_data->DisplaySize.x
            , 2.0f / draw_data->DisplaySize.y
            };
            auto Translate = std::array
            { -1.0f - draw_data->DisplayPos.x * Scale[0]
            , -1.0f - draw_data->DisplayPos.y * Scale[1]
            };

            CmdBuffer.setConstants( xgpu::shader::type::VERTEX, 0,   Scale.data(),     static_cast<std::uint32_t>(sizeof(Scale)));
            CmdBuffer.setConstants( xgpu::shader::type::VERTEX, 2*4, Translate.data(), static_cast<std::uint32_t>(sizeof(Translate)));
        }

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
                        // ImGui_ImplVulkan_SetupRenderState(draw_data, pipeline, command_buffer, rb, fb_width, fb_height);
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
            Setup.m_TotalMemory = Mip[0].m_Size;
            Setup.m_MipChain    = Mip;
            Setup.m_pData       = reinterpret_cast<std::byte*>(pixels);

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
                { .m_Type   = xgpu::shader::type::FRAGMENT
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
                    .m_Type                 = xgpu::shader::type::VERTEX
                ,   .m_Sharer               = xgpu::shader::setup::raw_data{g_VertShaderSPV}
                ,   .m_InOrderUniformSizes  = UniformConstans
                };
                if (auto Err = m_Device.Create(ImGuiVertexShader, Setup); Err) return Err;
            }

            auto Shaders  = std::array<const xgpu::shader*, 2>{ &ImGuiFragmentShader, &ImGuiVertexShader };
            auto Samplers = std::array{ xgpu::pipeline::sampler{} };
            auto Setup    = xgpu::pipeline::setup
            {
                .m_VertexDescriptor = m_VertexDescritor
            ,   .m_Shaders          = Shaders
            ,   .m_Samplers         = Samplers
            ,   .m_Blend            = xgpu::pipeline::blend::getAlphaOriginal()
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

            const auto MouseValues = m_Mouse.getValue(xgpu::mouse::analog::POS_ABS);

            ImGuiPlatformIO& PlatformIO = ImGui::GetPlatformIO();
            for (int n = 0; n < PlatformIO.Viewports.Size; n++)
            {
                ImGuiViewport*  pViewport = PlatformIO.Viewports[n];
                auto&           Info    = *reinterpret_cast<window_info*>(pViewport->PlatformHandle);

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
                        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
                        {
                            // Multi-viewport mode: mouse position in OS absolute coordinates (io.MousePos is (0,0) when the mouse is on the upper-left of the primary monitor)
                            auto [WindowX, WindowY] = Info.m_Window.getPosition();
                            io.MousePos = ImVec2((float)WindowX + MouseValues[0], (float)WindowY + MouseValues[1] );
                        }
                        else
                        {
                            // Single viewport mode: mouse position in client window coordinates (io.MousePos is (0,0) when the mouse is on the upper-left corner of the app window)
                            io.MousePos = ImVec2{ MouseValues[0], MouseValues[1] };
                        }
                    }
                }

                /*
                // (Optional) When using multiple viewports: set io.MouseHoveredViewport to the viewport the OS mouse cursor is hovering.
                // Important: this information is not easy to provide and many high-level windowing library won't be able to provide it correctly, because
                // - This is _ignoring_ viewports with the ImGuiViewportFlags_NoInputs flag (pass-through windows).
                // - This is _regardless_ of whether another viewport is focused or being dragged from.
                // If ImGuiBackendFlags_HasMouseHoveredViewport is not set by the backend, imgui will ignore this field and infer the information by relying on the
                // rectangles and last focused time of every viewports it knows about. It will be unaware of other windows that may be sitting between or over your windows.
                // [GLFW] FIXME: This is currently only correct on Win32. See what we do below with the WM_NCHITTEST, missing an equivalent for other systems.
                // See https://github.com/glfw/glfw/issues/1236 if you want to help in making this a GLFW feature.
#if GLFW_HAS_MOUSE_PASSTHROUGH || (GLFW_HAS_WINDOW_HOVERED && defined(_WIN32))
                const bool window_no_input = (viewport->Flags & ImGuiViewportFlags_NoInputs) != 0;
#if GLFW_HAS_MOUSE_PASSTHROUGH
                glfwSetWindowAttrib(window, GLFW_MOUSE_PASSTHROUGH, window_no_input);
#endif
                if (glfwGetWindowAttrib(window, GLFW_HOVERED) && !window_no_input)
                    io.MouseHoveredViewport = viewport->ID;
#endif
*/
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

            // Write into text boxes
            if( bPresses && !io.KeyCtrl )
                io.AddInputCharacter( m_Keyboard.getLatestChar() );
        }

        // Start the frame
        ImGui::NewFrame();

        return nullptr;
    }
};

//------------------------------------------------------------------------------------------------------------

void Render( void ) noexcept
{
    GETINSTANCE;
    ImDrawData* pMainDrawData = ImGui::GetDrawData();
    Instance.Render( io, pMainDrawData, Instance.m_PipelineInstance );
}


//------------------------------------------------------------------------------------------------------------

void NewFrame( void ) noexcept
{
    GETINSTANCE;
    Instance.StartNewFrame( io );
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

xgpu::device::error* CreateInstance(xgpu::instance& Intance, xgpu::device& Device, xgpu::window& MainWindow ) noexcept
{
    //
    // Setup Dear ImGui context
    //
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
    //io.ConfigViewportsNoAutoMerge = true;
    //io.ConfigViewportsNoTaskBarIcon = true;


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
    auto Instance = std::make_unique<breach_instance>(Intance, Device, MainWindow);
    if( auto Err = Instance->InitializePipeline(io); Err ) return Err;
    Instance->m_LastFrameTimer = clock::now();
    io.UserData = Instance.release();

    //
    // Setup backend capabilities flags
    //
    io.BackendRendererName = "xgpu_imgui_breach";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;  // We can create multi-viewports on the Renderer side (optional)

    ImGuiViewport* main_viewport    = ImGui::GetMainViewport();
//    main_viewport->RendererUserData = nullptr;
    main_viewport->PlatformHandle   = io.UserData;

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
        platform_io.Renderer_CreateWindow  = CreateChildWindow;
        platform_io.Renderer_DestroyWindow = DestroyChildWindow;
        platform_io.Renderer_SetWindowSize = SetChildWindowSize;
        platform_io.Renderer_RenderWindow  = RenderChildWindow;
        platform_io.Renderer_SwapBuffers   = ChildSwapBuffers;
    }

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
} //xgpu::tools::imgui
