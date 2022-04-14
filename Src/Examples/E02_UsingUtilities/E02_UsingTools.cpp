#include "xGPU.h"
#include "xcore.h"
#include "../../tools/xgpu_view.h"
#include "../../tools/xgpu_view_inline.h"
#include <iostream>

//------------------------------------------------------------------------------------------------
namespace e02
{
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

    struct push_constants
    {
        xcore::matrix4 m_L2C;
    };
}

//------------------------------------------------------------------------------------------------

int E02_Example()
{
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = true, .m_pLogErrorFunc = e02::DebugMessage, .m_pLogWarning = e02::DebugMessage }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err)
        return xgpu::getErrorInt(Err);

    std::array<xgpu::window, 1> lWindow;
    for (int i = 0; i < static_cast<int>(lWindow.size()); ++i)
        if (auto Err = Device.Create(lWindow[i], {}); Err)
            return xgpu::getErrorInt(Err);

    xgpu::vertex_descriptor VertexDescriptor;
    {
        auto Attributes = std::array
        {
            xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(e02::draw_vert, m_X)
            ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D
            }
        ,   xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(e02::draw_vert, m_U)
            ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
            }
        ,   xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(e02::draw_vert, m_Color)
            ,   .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED
            }
        };
        auto Setup = xgpu::vertex_descriptor::setup
        {
            .m_VertexSize = sizeof(e02::draw_vert)
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
                    #include "x64/draw_frag.h"
                }
            };

            if (auto Err = Device.Create(MyFragmentShader, { .m_Type = xgpu::shader::type::bit::FRAGMENT, .m_Sharer = RawData }); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader MyVertexShader;
        {
            auto RawData = xgpu::shader::setup::raw_data
            { std::array
                {
                    #include "x64/draw_vert.h"
                }
            };
            xgpu::shader::setup Setup
            {
                .m_Type = xgpu::shader::type::bit::VERTEX
            ,   .m_Sharer = RawData
            };

            if (auto Err = Device.Create(MyVertexShader, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        auto Shaders  = std::array<const xgpu::shader*, 2>{ &MyFragmentShader, & MyVertexShader };
        auto Samplers = std::array{ xgpu::pipeline::sampler{} };
        auto Setup    = xgpu::pipeline::setup
        {
            .m_VertexDescriptor  = VertexDescriptor
        ,   .m_Shaders           = Shaders
        ,   .m_PushConstantsSize = sizeof(e02::push_constants)
        ,   .m_Samplers          = Samplers
        };

        if (auto Err = Device.Create(PipeLine, Setup); Err)
            return xgpu::getErrorInt(Err);
    }

    //
    // Setup the material instance
    //
    std::array<xgpu::pipeline_instance, 2> PipeLineInstance;
    for (int i = 0; i < PipeLineInstance.size(); ++i)
    {
        xgpu::texture Texture;
        {
            constexpr auto          size_v = 32;
            xgpu::texture::setup    Setup;
            auto                    Mips = std::array{ xgpu::texture::setup::mip{ size_v * size_v * sizeof(std::uint32_t) } };

            Setup.m_Height = size_v;
            Setup.m_Width = size_v;
            Setup.m_MipChain = Mips;

            auto TextureData = std::make_unique< std::array<std::uint32_t, size_v* size_v> >();
            if (i == 0) std::fill(TextureData->begin(), TextureData->end(), 0xffffffffu);
            else std::fill(TextureData->begin(), TextureData->end(), 0xffffu);

            Setup.m_Data = { reinterpret_cast<std::byte*>(TextureData->data()), Mips[0].m_Size };

            if (auto Err = Device.Create(Texture, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        auto Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{ Texture } };
        auto Setup    = xgpu::pipeline_instance::setup
        { .m_PipeLine         = PipeLine
        , .m_SamplersBindings = Bindings
        };

        if (auto Err = Device.Create(PipeLineInstance[i], Setup); Err)
            return xgpu::getErrorInt(Err);
    }

    xgpu::buffer VertexBuffer;
    {
        if (auto Err = Device.Create(VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(e02::draw_vert), .m_EntryCount = 24 }); Err)
            return xgpu::getErrorInt(Err);

        (void)VertexBuffer.MemoryMap(0, 24, [&](void* pData)
            {
                auto pVertex = static_cast<e02::draw_vert*>(pData);
                pVertex[0]  = { -0.5f, -0.5f,  0.5f, 0.0f, 1.0f, xcore::endian::Convert(0x0000ffffu) };
                pVertex[1]  = {  0.5f, -0.5f,  0.5f, 1.0f, 1.0f, xcore::endian::Convert(0x0000ffffu) };
                pVertex[2]  = {  0.5f,  0.5f,  0.5f, 1.0f, 0.0f, xcore::endian::Convert(0x0000ffffu) };
                pVertex[3]  = { -0.5f,  0.5f,  0.5f, 0.0f, 0.0f, xcore::endian::Convert(0x0000ffffu) };

                // Back face
                pVertex[4]  = { -0.5f, -0.5f, -0.5f, 0.0f, 1.0f, xcore::endian::Convert(0x000066ffu) };
                pVertex[5]  = { -0.5f,  0.5f, -0.5f, 1.0f, 1.0f, xcore::endian::Convert(0x000066ffu) };
                pVertex[6]  = {  0.5f,  0.5f, -0.5f, 1.0f, 0.0f, xcore::endian::Convert(0x000066ffu) };
                pVertex[7]  = {  0.5f, -0.5f, -0.5f, 0.0f, 0.0f, xcore::endian::Convert(0x000066ffu) };

                // Top face
                pVertex[8]  = { -0.5f,  0.5f, -0.5f, 0.0f, 1.0f, xcore::endian::Convert(0x00ff00ffu) };
                pVertex[9]  = { -0.5f,  0.5f,  0.5f, 1.0f, 1.0f, xcore::endian::Convert(0x00ff00ffu) };
                pVertex[10] = {  0.5f,  0.5f,  0.5f, 1.0f, 0.0f, xcore::endian::Convert(0x00ff00ffu) };
                pVertex[11] = {  0.5f,  0.5f, -0.5f, 0.0f, 0.0f, xcore::endian::Convert(0x00ff00ffu) };

                // Bottom face
                pVertex[12] = { -0.5f, -0.5f, -0.5f, 0.0f, 1.0f, xcore::endian::Convert(0x006600ffu) };
                pVertex[13] = {  0.5f, -0.5f, -0.5f, 1.0f, 1.0f, xcore::endian::Convert(0x006600ffu) };
                pVertex[14] = {  0.5f, -0.5f,  0.5f, 1.0f, 0.0f, xcore::endian::Convert(0x006600ffu) };
                pVertex[15] = { -0.5f, -0.5f,  0.5f, 0.0f, 0.0f, xcore::endian::Convert(0x006600ffu) };

                // Right face
                pVertex[16] = { 0.5f, -0.5f, -0.5f, 0.0f, 1.0f, xcore::endian::Convert(0xff0000ffu) };
                pVertex[17] = { 0.5f,  0.5f, -0.5f, 1.0f, 1.0f, xcore::endian::Convert(0xff0000ffu) };
                pVertex[18] = { 0.5f,  0.5f,  0.5f, 1.0f, 0.0f, xcore::endian::Convert(0xff0000ffu) };
                pVertex[19] = { 0.5f, -0.5f,  0.5f, 0.0f, 0.0f, xcore::endian::Convert(0xff0000ffu) };

                // Left face
                pVertex[20] = { -0.5f, -0.5f, -0.5f, 0.0f, 1.0f, xcore::endian::Convert(0x660000ffu) };
                pVertex[21] = { -0.5f, -0.5f,  0.5f, 1.0f, 1.0f, xcore::endian::Convert(0x660000ffu) };
                pVertex[22] = { -0.5f,  0.5f,  0.5f, 1.0f, 0.0f, xcore::endian::Convert(0x660000ffu) };
                pVertex[23] = { -0.5f,  0.5f, -0.5f, 0.0f, 0.0f, xcore::endian::Convert(0x660000ffu) };
            });
    }

    xgpu::buffer IndexBuffer;
    {
        if (auto Err = Device.Create(IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = 6 * 6 }); Err)
            return xgpu::getErrorInt(Err);

        (void)IndexBuffer.MemoryMap(0, 6 * 6, [&](void* pData)
            {
                auto            pIndex = static_cast<std::uint32_t*>(pData);
                constexpr auto  StaticIndex = std::array
                {
                    0u,  1u,  2u,      0u,  2u,  3u,    // front
                    4u,  5u,  6u,      4u,  6u,  7u,    // back
                    8u,  9u,  10u,     8u,  10u, 11u,   // top
                    12u, 13u, 14u,     12u, 14u, 15u,   // bottom
                    16u, 17u, 18u,     16u, 18u, 19u,   // right
                    20u, 21u, 22u,     20u, 22u, 23u    // left
                };
                static_assert(StaticIndex.size() == 6 * 6);
                for (auto i : StaticIndex)
                {
                    *pIndex = i;
                    pIndex++;
                }
            });
    }

    //
    // Basic loop
    //
    xgpu::tools::view View;

    xgpu::mouse Mouse;
    {
        Instance.Create( Mouse, {} );
    }

    xcore::radian3 Angles;
    float          Distance = 2;
    while (Instance.ProcessInputEvents())
    {
        //
        // Input
        //
        if (Mouse.isPressed(xgpu::mouse::digital::BTN_RIGHT))
        {
            auto MousePos = Mouse.getValue(xgpu::mouse::analog::POS_REL);
            Angles.m_Pitch.m_Value -= 0.01f * MousePos[1];
            Angles.m_Yaw.m_Value   -= 0.01f * MousePos[0];
        }
        Distance += -1.0f * Mouse.getValue(xgpu::mouse::analog::WHEEL_REL)[0];
        if (Distance < 2) Distance = 2;

        // Update the camera
        View.LookAt(Distance, Angles, { 0,0,0 });

        //
        // Rendering
        //
        for (auto& W : lWindow)
        {
            // Windows can not display when they are minimize
            if (W.BeginRendering()) continue;

            // Update the view with latest window size
            View.setViewport({ 0, 0, W.getWidth(), W.getHeight() });

            // Get the command buffer ready to render
            {
                auto        CmdBuffer = W.getCmdBuffer();
                const auto  W2C       = View.getW2C();

                // Render first object (animated mesh)
                {
                    static xcore::radian R{ 0 };
                    R += xcore::radian{ 0.001f };

                    xcore::matrix4 L2W;
                    L2W.setIdentity();
                    L2W.RotateY(R);

                    CmdBuffer.setPipelineInstance(PipeLineInstance[0]);
                    CmdBuffer.setBuffer(VertexBuffer);
                    CmdBuffer.setBuffer(IndexBuffer);
                    CmdBuffer.setPushConstants(e02::push_constants{ .m_L2C = W2C * L2W } );
                    CmdBuffer.Draw(IndexBuffer.getEntryCount());
                }

                // Render second object 
                {
                    xcore::matrix4 L2W;
                    L2W.setIdentity();
                    L2W.Translate({0,-0.5f, -1.1f });

                    e02::push_constants PushConstants;
                    PushConstants.m_L2C = W2C * L2W;

                    CmdBuffer.setPipelineInstance(PipeLineInstance[1]);
                    CmdBuffer.setPushConstants( PushConstants );
                    CmdBuffer.Draw(IndexBuffer.getEntryCount());
                }
            }

            // Swap the buffers
            W.PageFlip();
        }
    }

    return 0;
}

