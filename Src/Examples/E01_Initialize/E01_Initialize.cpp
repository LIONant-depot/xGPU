#include "xGPU.h"
#include <iostream>

//------------------------------------------------------------------------------------------------
static
void DebugMessage(std::string_view View)
{
    printf("%s\n", View.data());
}

//------------------------------------------------------------------------------------------------

struct draw_vert
{
    float           m_X, m_Y, m_Z;
    float           m_U, m_V;
    std::uint32_t   m_Color;
};

//------------------------------------------------------------------------------------------------

int E01_Example()
{
    xgpu::instance Instance;
    if( auto Err = xgpu::CreateInstance( Instance
                                       , { .m_bDebugMode       = true
                                         , .m_bEnableRenderDoc = true
                                         , .m_pLogErrorFunc    = DebugMessage
                                         , .m_pLogWarning      = DebugMessage 
                                         }
                                       ); Err ) return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if( auto Err = Instance.Create( Device ); Err )
        return xgpu::getErrorInt(Err);

    std::array<xgpu::window,2> lWindow;
    for( int i=0; i<static_cast<int>(lWindow.size()); ++i )
        if( auto Err = Device.Create( lWindow[i], {} ); Err )
            return xgpu::getErrorInt(Err);

    xgpu::vertex_descriptor VertexDescriptor;
    {
        auto Attributes = std::array
        {
            xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(draw_vert, m_X )
            ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D
            }
        ,   xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(draw_vert, m_U)
            ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
            }
        ,   xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(draw_vert, m_Color)
            ,   .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED
            }
        };
        auto Setup = xgpu::vertex_descriptor::setup
        {
            .m_VertexSize = sizeof(draw_vert)
        ,   .m_Attributes = Attributes
        };

        if (auto Err = Device.Create(VertexDescriptor, Setup); Err)
            return xgpu::getErrorInt(Err);
    }

    //
    // Define a material
    //
    xgpu::pipeline PipeLine;
    {
        xgpu::shader MyFragmentShader;
        {
            auto RawData = xgpu::shader::setup::raw_data
            { std::array
                {
                    #include "x64\draw_frag.h"
                }
            };

            if (auto Err = Device.Create(MyFragmentShader, { .m_Type = xgpu::shader::type::bit::FRAGMENT, .m_Sharer = RawData }); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader MyVertexShader;
        {
            auto UniformConstans = std::array
            { static_cast<int>(sizeof(float) * 4 * 4)   // LocalToClip
            };
            auto RawData = xgpu::shader::setup::raw_data
            { std::array
                {
                    #include "x64\draw_vert.h"
                }
            };
            xgpu::shader::setup Setup
            {
                .m_Type   = xgpu::shader::type::bit::VERTEX
            ,   .m_Sharer = RawData
            };

            if (auto Err = Device.Create(MyVertexShader, Setup ); Err)
                return xgpu::getErrorInt(Err);
        }

        auto Shaders    = std::array<const xgpu::shader*, 2>{ &MyFragmentShader, &MyVertexShader };
        auto Samplers   = std::array{ xgpu::pipeline::sampler{} };
        auto Setup      = xgpu::pipeline::setup
        {
            .m_VertexDescriptor  = VertexDescriptor
        ,   .m_Shaders           = Shaders
        ,   .m_PushConstantsSize = sizeof(float)*4*4
        ,   .m_Samplers          = Samplers
        };

        if (auto Err = Device.Create(PipeLine, Setup ); Err)
            return xgpu::getErrorInt(Err);
    }

    //
    // Setup the material instance
    //
    std::array<xgpu::pipeline_instance,2> PipeLineInstance;
    for( int i=0; i< PipeLineInstance.size(); ++i )
    {
        xgpu::texture Texture;
        {
            constexpr auto          size_v = 32;
            xgpu::texture::setup    Setup;
            const auto              Mips   = std::array{ xgpu::texture::setup::mip{ size_v * size_v * sizeof(std::uint32_t) }};

            Setup.m_Height   = size_v;
            Setup.m_Width    = size_v;
            Setup.m_MipChain = Mips;

            auto TextureData = std::make_unique< std::array<std::uint32_t, size_v* size_v> >();
            if( i == 0) std::fill(TextureData->begin(), TextureData->end(), 0xffffffffu);
            else std::fill(TextureData->begin(), TextureData->end(), 0xffffu);

            Setup.m_Data = { reinterpret_cast<const std::byte*>(TextureData->data()), Mips[0].m_Size };

            if (auto Err = Device.Create(Texture, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        auto  Bindings = std::array                     { xgpu::pipeline_instance::sampler_binding{ Texture } };
        auto  Setup    = xgpu::pipeline_instance::setup
        {   .m_PipeLine         = PipeLine
        ,   .m_SamplersBindings = Bindings
        };

        if( auto Err = Device.Create( PipeLineInstance[i], Setup ); Err )
            return xgpu::getErrorInt(Err);
    }

    xgpu::buffer VertexBuffer;
    {
        if (auto Err = Device.Create(VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(draw_vert), .m_EntryCount = 24 }); Err)
            return xgpu::getErrorInt(Err);

        (void)VertexBuffer.MemoryMap( 0, 24, [&](void* pData)
        {
            auto pVertex   = static_cast<draw_vert*>(pData);
            pVertex[0]  = { -0.5f, -0.5f,  0.5f, 0.0f, 1.0f, 0xffffffff };
            pVertex[1]  = {  0.5f, -0.5f,  0.5f, 1.0f, 1.0f, 0xffffffff };
            pVertex[2]  = {  0.5f,  0.5f,  0.5f, 1.0f, 0.0f, 0xffffffff };
            pVertex[3]  = { -0.5f,  0.5f,  0.5f, 0.0f, 0.0f, 0xffffffff };

            // Back face
            pVertex[4]  = { -0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0xffffffff };
            pVertex[5]  = { -0.5f,  0.5f, -0.5f, 1.0f, 1.0f, 0xffffffff };
            pVertex[6]  = {  0.5f,  0.5f, -0.5f, 1.0f, 0.0f, 0xffffffff };
            pVertex[7]  = {  0.5f, -0.5f, -0.5f, 0.0f, 0.0f, 0xffffffff };

            // Top face
            pVertex[8]  = { -0.5f,  0.5f, -0.5f, 0.0f, 1.0f, 0xffffffff };
            pVertex[9]  = { -0.5f,  0.5f,  0.5f, 1.0f, 1.0f, 0xffffffff };
            pVertex[10] = {  0.5f,  0.5f,  0.5f, 1.0f, 0.0f, 0xffffffff };
            pVertex[11] = {  0.5f,  0.5f, -0.5f, 0.0f, 0.0f, 0xffffffff };

            // Bottom face
            pVertex[12] = { -0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0xffffffff };
            pVertex[13] = {  0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 0xffffffff };
            pVertex[14] = {  0.5f, -0.5f,  0.5f, 1.0f, 0.0f, 0xffffffff };
            pVertex[15] = { -0.5f, -0.5f,  0.5f, 0.0f, 0.0f, 0xffffffff };

            // Right face
            pVertex[16] = {  0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0xffffffff };
            pVertex[17] = {  0.5f,  0.5f, -0.5f, 1.0f, 1.0f, 0xffffffff };
            pVertex[18] = {  0.5f,  0.5f,  0.5f, 1.0f, 0.0f, 0xffffffff };
            pVertex[19] = {  0.5f, -0.5f,  0.5f, 0.0f, 0.0f, 0xffffffff };

            // Left face
            pVertex[20] = { -0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0xffffffff };
            pVertex[21] = { -0.5f, -0.5f,  0.5f, 1.0f, 1.0f, 0xffffffff };
            pVertex[22] = { -0.5f,  0.5f,  0.5f, 1.0f, 0.0f, 0xffffffff };
            pVertex[23] = { -0.5f,  0.5f, -0.5f, 0.0f, 0.0f, 0xffffffff };
        });
    }

    xgpu::buffer IndexBuffer;
    {
        if (auto Err = Device.Create(IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = 6 * 6 }); Err)
            return xgpu::getErrorInt(Err);

        (void)IndexBuffer.MemoryMap( 0, 6 * 6, [&](void* pData)
        {
            auto            pIndex      = static_cast<std::uint32_t*>(pData);
            constexpr auto  StaticIndex = std::array
            {
                0u,  1u,  2u,      0u,  2u,  3u,    // front
                4u,  5u,  6u,      4u,  6u,  7u,    // back
                8u,  9u,  10u,     8u,  10u, 11u,   // top
                12u, 13u, 14u,     12u, 14u, 15u,   // bottom
                16u, 17u, 18u,     16u, 18u, 19u,   // right
                20u, 21u, 22u,     20u, 22u, 23u    // left
            };
            static_assert(StaticIndex.size() == 6*6);
            for( auto i : StaticIndex )
            {
                *pIndex = i;
                pIndex++;
            }
        });
    }

    //
    // Basic loop
    //
    while( Instance.ProcessInputEvents() )
    {
        //
        // Run Logic...
        //

        //
        // Render
        //
        for( auto& W : lWindow )
        {
            // Windows can not display when they are minimize
            if( W.BeginRendering() ) continue;

            {
                auto CmdBuffer = W.getCmdBuffer();

                auto FinalMatrix = std::array
                { -0.90439f, -1.15167f,  0.79665f,  0.79506f
                ,  0.00000f, -2.07017f, -0.51553f, -0.51450f
                ,  2.23845f, -0.46530f,  0.32187f,  0.32122f
                ,  0.00000f,  0.00000f,  5.64243f,  5.83095f
                };

                CmdBuffer.setPipelineInstance(PipeLineInstance[0]);
                CmdBuffer.setBuffer(VertexBuffer);
                CmdBuffer.setBuffer(IndexBuffer);
                CmdBuffer.setPushConstants( FinalMatrix.data(), sizeof(FinalMatrix) );
                CmdBuffer.Draw( IndexBuffer.getEntryCount() );

                FinalMatrix[ 1 + 4*3 ] = 3.1f; 
                CmdBuffer.setPipelineInstance(PipeLineInstance[1]);
                CmdBuffer.setPushConstants( FinalMatrix.data(), sizeof(FinalMatrix) );
                CmdBuffer.Draw( IndexBuffer.getEntryCount() );
            }

            W.PageFlip();
        }
    }

    return 0;
}

