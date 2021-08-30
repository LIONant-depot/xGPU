#include "xGPU.h"
#include "xcore.h"
#include "../../tools/xgpu_imgui_breach.h"
#include "../../tools/xgpu_xcore_bitmap_helpers.h"
#include "../../dependencies/xcore/dependencies/properties/src/Examples/ImGuiExample/ImGuiPropertyInspector.h"
#include "../../dependencies/xbmp_tools/src/xbmp_tools.h"
#include <format>

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
constexpr auto g_VertShaderSPV = std::array
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
constexpr auto g_FragShaderSPV = std::array
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

//------------------------------------------------------------------------------------------------
static
void DebugMessage(std::string_view View)
{
    printf("%s\n", View.data());
}

//------------------------------------------------------------------------------------------------

struct vert_2d
{
    float           m_X, m_Y;
    float           m_U, m_V;
    std::uint32_t   m_Color;
};

//------------------------------------------------------------------------------------------------

struct bitmap_inspector
{
    void Load( const char* pFileName, xgpu::device& Device, xgpu::pipeline& Pipeline ) noexcept
    {
        //
        // Get the file name
        //
        m_FileName = pFileName;
        m_FileName = m_FileName.substr(m_FileName.find_last_of("/\\") + 1);

        //
        // Load file
        //
        if( auto Err = xbmp::tools::loader::LoadDSS( m_Bitmap, pFileName ); Err )
        {
            DebugMessage( xbmp::tools::getErrorMsg(Err) );
            std::exit(xbmp::tools::getErrorInt(Err) );
        }

        //
        // Create Texture
        //
        xgpu::texture Texture;
        if( auto Err = xgpu::tools::bitmap::Create(Texture, Device, m_Bitmap ); Err )
        {
            DebugMessage(xgpu::getErrorMsg(Err));
            std::exit(xgpu::getErrorInt(Err));
        }

        //
        // Create Pipeline Instance
        //
        {
            auto  Bindings  = std::array{ xgpu::pipeline_instance::sampler_binding{ Texture } };
            auto  Setup     = xgpu::pipeline_instance::setup
            { .m_PipeLine           = Pipeline
            , .m_SamplersBindings   = Bindings
            };

            if (auto Err = Device.Create(m_Instance, Setup); Err)
            {
                DebugMessage(xgpu::getErrorMsg(Err));
                std::exit(xgpu::getErrorInt(Err));
            }
        }
    }

    xgpu::pipeline_instance m_Instance;
    std::string             m_FileName;
    xcore::bitmap           m_Bitmap;
};

property_begin_name(bitmap_inspector, "Bitmap Info" )
{
    property_var(m_FileName)
        .Flags(property::flags::SHOW_READONLY)
,   property_var_fnbegin("Format", std::string )
    {
        if (isRead)
        {
            switch( Self.m_Bitmap.getFormat() )
            {
                case xcore::bitmap::format::R4G4B4A4:   InOut = "R4G4B4A4"; break;
                case xcore::bitmap::format::R5G6B5:     InOut = "R5G6B5"; break;
                case xcore::bitmap::format::B5G5R5A1:   InOut = "B5G5R5A1"; break;
                case xcore::bitmap::format::R8G8B8:     InOut = "R8G8B8"; break;
                case xcore::bitmap::format::R8G8B8U8:   InOut = "R8G8B8U8"; break;
                case xcore::bitmap::format::R8G8B8A8:   InOut = "R8G8B8A8"; break;
                case xcore::bitmap::format::B8G8R8A8:   InOut = "B8G8R8A8"; break;
                case xcore::bitmap::format::B8G8R8U8:   InOut = "B8G8R8U8"; break;
                case xcore::bitmap::format::U8R8G8B8:   InOut = "U8R8G8B8"; break;

                case xcore::bitmap::format::BC1_4RGB:   InOut = "BC1_4RGB / DXT1"; break;
                case xcore::bitmap::format::BC1_4RGBA1: InOut = "BC1_4RGBA1 / DXT1"; break;
                case xcore::bitmap::format::BC2_8RGBA:  InOut = "BC2_8RGBA / DXT3"; break;
                case xcore::bitmap::format::BC3_8RGBA:  InOut = "BC3_8RGBA / DXT5"; break;

                default: InOut = "Unexpected format"; break;
            }
        }
    } property_var_fnend().Flags(property::flags::SHOW_READONLY)
,   property_var_fnbegin("HasAlphaChannel", bool )
    {
        if (isRead) InOut = Self.m_Bitmap.hasAlphaChannel();
    } property_var_fnend().Flags(property::flags::SHOW_READONLY)
,   property_var_fnbegin("HasAlphaInfo", bool )
    {
        if (isRead) InOut = Self.m_Bitmap.hasAlphaInfo();
    } property_var_fnend().Flags(property::flags::SHOW_READONLY)
,   property_var_fnbegin("Width", int )
    {
        if (isRead) InOut = Self.m_Bitmap.getWidth();
    } property_var_fnend().Flags(property::flags::SHOW_READONLY)
,   property_var_fnbegin("Height", int)
    {
        if (isRead) InOut = Self.m_Bitmap.getHeight();
    } property_var_fnend().Flags(property::flags::SHOW_READONLY)
,   property_var_fnbegin("SRGB", bool)
    {
        if (isRead) InOut = !Self.m_Bitmap.isLinearSpace();
    } property_var_fnend().Flags(property::flags::SHOW_READONLY)
,   property_var_fnbegin("nFrames", int)
    {
        if (isRead) InOut = Self.m_Bitmap.getFrameCount();
    } property_var_fnend().Flags(property::flags::SHOW_READONLY)
,   property_var_fnbegin("FrameSizeBytes", int)
    {
        if (isRead) InOut = static_cast<int>(Self.m_Bitmap.getFrameSize());
    } property_var_fnend().Flags(property::flags::SHOW_READONLY)
,   property_var_fnbegin("isCubemap", bool )
    {
        if (isRead) InOut = Self.m_Bitmap.isCubemap();
    } property_var_fnend().Flags(property::flags::SHOW_READONLY)
,   property_var_fnbegin("nFaces", int)
    {
        if (isRead) InOut = Self.m_Bitmap.getFaceCount();
    } property_var_fnend().Flags(property::flags::SHOW_READONLY)
,   property_var_fnbegin("FaceSizeBytes", int)
    {
        if (isRead) InOut = static_cast<int>(Self.m_Bitmap.getFaceSize());
    } property_var_fnend().Flags(property::flags::SHOW_READONLY)
,   property_var_fnbegin("nMips", int)
    {
        if (isRead) InOut = Self.m_Bitmap.getMipCount();
    } property_var_fnend().Flags(property::flags::SHOW_READONLY)
,   property_list_fnbegin( "Mip", std::string )
    {   // Self     - is our class
        // isRead   - Will give ask us what to do
        // InOut    - is the argument
        // Index    - is the index used to access the entry, is 64 bits so we may want to cast down a bit to avoid warnings
        if( isRead ) InOut = std::format( "{}x{}", std::max( 1u, Self.m_Bitmap.getWidth()>>Index), std::max(1u, Self.m_Bitmap.getHeight() >> Index) );
    } property_list_fnenum()
    {   // Self         - is our class
        // Cmd          - Will give ask us what to do
        // InOut        - is the argument
        // MemoryBlock  - block of memory that can be use to store iterators
        switch ( Cmd )
        {
            case property::lists_cmd::READ_COUNT:   InOut = Self.m_Bitmap.getMipCount(); break;
            case property::lists_cmd::WRITE_COUNT:  break;
            case property::lists_cmd::READ_FIRST:   InOut = 0; break;
            case property::lists_cmd::READ_NEXT:    if ( ++InOut == Self.m_Bitmap.getMipCount() ) InOut = property::lists_iterator_ends_v; break;
            default: assert( false );
        }
    } property_list_fnend().Flags(property::flags::SHOW_READONLY)
,   property_var_fnbegin("TotalSize", int )
    {
        if (isRead) InOut = static_cast<int>(Self.m_Bitmap.getDataSize());
    } property_var_fnend().Flags(property::flags::SHOW_READONLY)

} property_end()

//------------------------------------------------------------------------------------------------

int E05_Example()
{
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = false, .m_pLogErrorFunc = DebugMessage, .m_pLogWarning = DebugMessage }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err)
        return xgpu::getErrorInt(Err);

    xgpu::window MainWindow;
    if (auto Err = Device.Create(MainWindow, {}); Err)
        return xgpu::getErrorInt(Err);

    //
    // Define a material
    //
    xgpu::pipeline Pipeline;
    {
        xgpu::vertex_descriptor VertexDescriptor;
        {
            auto Attributes = std::array
            {
                xgpu::vertex_descriptor::attribute
                {
                    .m_Offset = offsetof(vert_2d, m_X)
                ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT2
                }
            ,   xgpu::vertex_descriptor::attribute
                {
                    .m_Offset = offsetof(vert_2d, m_U)
                ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT2
                }
            ,   xgpu::vertex_descriptor::attribute
                {
                    .m_Offset = offsetof(vert_2d, m_Color)
                ,   .m_Format = xgpu::vertex_descriptor::format::UINT8_RGBA_NORMALIZED
                }
            };
            auto Setup = xgpu::vertex_descriptor::setup
            {
                .m_VertexSize = sizeof(vert_2d)
            ,   .m_Attributes = Attributes
            };

            if (auto Err = Device.Create(VertexDescriptor, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader MyFragmentShader;
        {
            xgpu::shader::setup Setup
            { .m_Type = xgpu::shader::type::FRAGMENT
            , .m_Sharer = xgpu::shader::setup::raw_data{ g_FragShaderSPV }
            };
            if (auto Err = Device.Create(MyFragmentShader, Setup ); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader MyVertexShader;
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

            if (auto Err = Device.Create(MyVertexShader, Setup ); Err)
                return xgpu::getErrorInt(Err);
        }

        auto Shaders    = std::array<const xgpu::shader*, 2>{ &MyFragmentShader, &MyVertexShader };
        auto Samplers   = std::array{ xgpu::pipeline::sampler{} };
        auto Setup      = xgpu::pipeline::setup
        {
            .m_VertexDescriptor = VertexDescriptor
        ,   .m_Shaders          = Shaders
        ,   .m_Samplers         = Samplers
        ,   .m_DepthStencil     = { .m_bDepthTestEnable = false }
        };

        if (auto Err = Device.Create(Pipeline, Setup ); Err)
            return xgpu::getErrorInt(Err);
    }

    xgpu::buffer VertexBuffer;
    {
        if (auto Err = Device.Create(VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(vert_2d), .m_EntryCount = 4 }); Err)
            return xgpu::getErrorInt(Err);

        VertexBuffer.MemoryMap( 0, 4, [&](void* pData)
        {
            auto pVertex   = static_cast<vert_2d*>(pData);
            pVertex[0]  = { -100.5f, -100.5f,  0.0f, 0.0f, 0xffffffff };
            pVertex[1]  = {  100.5f, -100.5f,  1.0f, 0.0f, 0xffffffff };
            pVertex[2]  = {  100.5f,  100.5f,  1.0f, 1.0f, 0xffffffff };
            pVertex[3]  = { -100.5f,  100.5f,  0.0f, 1.0f, 0xffffffff };
        });
    }

    xgpu::buffer IndexBuffer;
    {
        if (auto Err = Device.Create(IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = 6 }); Err)
            return xgpu::getErrorInt(Err);

        IndexBuffer.MemoryMap( 0, 6, [&](void* pData)
        {
            auto            pIndex      = static_cast<std::uint32_t*>(pData);
            constexpr auto  StaticIndex = std::array
            {
                2u,  1u,  0u,      3u,  2u,  0u,    // front
            };
            static_assert(StaticIndex.size() == 6);
            for( auto i : StaticIndex )
            {
                *pIndex = i;
                pIndex++;
            }
        });
    }


    //
    // Setup ImGui
    //
    xgpu::tools::imgui::CreateInstance(Instance, Device, MainWindow);

    //
    // Load bitmaps
    //
    bitmap_inspector BitmapInspector;
    constexpr auto TextureList = std::array
    { "../../Src/Tutorials/E05_Textures/Alita-FullColor.dds"
    , "../../Src/Tutorials/E05_Textures/Alita-FullColor-Mipmaps.dds"
    , "../../Src/Tutorials/E05_Textures/Alita-FullColorNoAlpha-Mipmaps.dds"
    , "../../Src/Tutorials/E05_Textures/Alita-DXT1-Mipmaps.dds"
    , "../../Src/Tutorials/E05_Textures/Alita-DXT1-NoAlpha-Mipmaps.dds"
    , "../../Src/Tutorials/E05_Textures/Alita-DXT3.dds"
    , "../../Src/Tutorials/E05_Textures/Alita-DXT5.dds"
    };

    BitmapInspector.Load(TextureList[6], Device, Pipeline );

    //
    // Create the inspector window
    //
    property::inspector Inspector("Property");
    Inspector.clear();
    Inspector.AppendEntity();
    Inspector.AppendEntityComponent(property::getTable(BitmapInspector), &BitmapInspector );

    xgpu::mouse Mouse;
    {
        Instance.Create(Mouse, {});
    }

    float           MouseScale   =1;
    xcore::vector2  MouseTranslate(0,0);

    //
    // Main loop
    //
    while (Instance.ProcessInputEvents())
    {
        if (xgpu::tools::imgui::isMinimize( true ))
            continue;

        //
        // Handle Input
        //
        if (Mouse.isPressed(xgpu::mouse::digital::BTN_RIGHT))
        {
            if( Mouse.isPressed(xgpu::mouse::digital::BTN_LEFT) )
            {
                auto MousePos = Mouse.getValue(xgpu::mouse::analog::POS_REL);
                MouseScale -= 8.0f * (MousePos[1] * (2.0f / MainWindow.getHeight()));
            }
            else
            {
                auto MousePos = Mouse.getValue(xgpu::mouse::analog::POS_REL);
                MouseTranslate.m_X += MousePos[0] * (2.0f / MainWindow.getWidth());
                MouseTranslate.m_Y += MousePos[1] * (2.0f / MainWindow.getHeight());
            }
        }

        MouseScale += 0.5f * Mouse.getValue(xgpu::mouse::analog::WHEEL_REL)[0];
        if(MouseScale < 0.01f ) MouseScale = 0.01f;

        //
        // Render the image
        //
        {
            auto CmdBuffer = MainWindow.getCmdBuffer();
            CmdBuffer.setPipelineInstance(BitmapInspector.m_Instance);
            CmdBuffer.setBuffer(VertexBuffer);
            CmdBuffer.setBuffer(IndexBuffer);

            // Set the push contants
            auto Scale = std::array
            { (MouseScale * 2.0f) / MainWindow.getWidth()
            , (MouseScale * 2.0f) / MainWindow.getHeight()
            };

            CmdBuffer.setConstants(xgpu::shader::type::VERTEX, 0,     Scale.data(), static_cast<std::uint32_t>(sizeof(Scale)));
            CmdBuffer.setConstants(xgpu::shader::type::VERTEX, 2 * 4, &MouseTranslate, static_cast<std::uint32_t>(sizeof(MouseTranslate)));

            CmdBuffer.Draw( 6 );
        }

        //
        // IMGUI demo
        //
        //static bool show_demo_window = true;
        //ImGui::ShowDemoWindow(&show_demo_window);

        //
        // Render the Inspector
        //
        Inspector.Show([]{});

        //
        // Render
        //
        xgpu::tools::imgui::Render();

        //
        // Pageflip the windows
        //
        MainWindow.PageFlip();
    }

    return 0;
}

