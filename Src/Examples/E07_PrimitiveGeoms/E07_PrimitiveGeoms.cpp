#include "xGPU.h"
#include "xcore.h"
#include "../../tools/xgpu_view.h"
#include "../../tools/xgpu_view_inline.h"
#include "../../dependencies/xprim_geom/src/xprim_geom.h"
#include "../../tools/xgpu_xcore_bitmap_helpers.h"

namespace e07
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

    struct push_contants
    {
        xcore::matrix4 m_L2C;
    };
}

//------------------------------------------------------------------------------------------------

int E07_Example()
{
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = true, .m_pLogErrorFunc = e07::DebugMessage, .m_pLogWarning = e07::DebugMessage }); Err)
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
                .m_Offset = offsetof(e07::draw_vert, m_X)
            ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D
            }
        ,   xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(e07::draw_vert, m_U)
            ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
            }
        ,   xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(e07::draw_vert, m_Color)
            ,   .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED
            }
        };
        auto Setup = xgpu::vertex_descriptor::setup
        {
            .m_VertexSize = sizeof(e07::draw_vert)
        ,   .m_Attributes = Attributes
        };

        if (auto Err = Device.Create(VertexDescriptor, Setup); Err)
            return xgpu::getErrorInt(Err);
    }

    //
    // Define a material
    //
    std::array<xgpu::pipeline,2> PipeLine;
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

            if (auto Err = Device.Create(MyVertexShader, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        auto Shaders  = std::array<const xgpu::shader*, 2>{ &MyFragmentShader, &MyVertexShader };
        auto Samplers = std::array{ xgpu::pipeline::sampler{} };

        //
        // Regular Texture mesh
        //
        {
            auto Setup    = xgpu::pipeline::setup
            {   .m_VertexDescriptor  = VertexDescriptor
            ,   .m_Shaders           = Shaders
            ,   .m_PushConstantsSize = sizeof(e07::push_contants)
            ,   .m_Samplers          = Samplers
            };

            if (auto Err = Device.Create(PipeLine[0], Setup); Err)
                return xgpu::getErrorInt(Err);
        }


        //
        // Wire Frame mesh
        //
        {
            auto Setup    = xgpu::pipeline::setup
            {   .m_VertexDescriptor  = VertexDescriptor
            ,   .m_Shaders           = Shaders
            ,   .m_PushConstantsSize = sizeof(e07::push_contants)
            ,   .m_Samplers          = Samplers
            ,   .m_Primitive
                {   .m_LineWidth = 5.0f
                ,   .m_Raster   = xgpu::pipeline::primitive::raster::WIRELINE
                }
                
            };

            if (auto Err = Device.Create(PipeLine[1], Setup); Err)
                return xgpu::getErrorInt(Err);
        }

    }

    //
    // Setup the material instance
    //
    std::array<xgpu::pipeline_instance, 2> PipeLineInstance;
    {
        xgpu::texture Texture;
        if( auto Err = xgpu::tools::bitmap::Create( Texture, Device, xcore::bitmap::getDefaultBitmap() ); Err )
            return xgpu::getErrorInt(Err);

        auto Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{ Texture } };

        //
        // Solid
        //
        {
            auto Setup = xgpu::pipeline_instance::setup
            {   .m_PipeLine         = PipeLine[0]
            ,   .m_SamplersBindings = Bindings
            };

            if (auto Err = Device.Create(PipeLineInstance[0], Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        //
        // Wire
        //
        {
            auto Setup = xgpu::pipeline_instance::setup
            {   .m_PipeLine         = PipeLine[1]
            ,   .m_SamplersBindings = Bindings
            };
            if (auto Err = Device.Create(PipeLineInstance[1], Setup); Err)
                return xgpu::getErrorInt(Err);
        }
    }

    //
    // Generate the meshes
    //
    xgpu::buffer VertexBuffer;
    xgpu::buffer IndexBuffer;
    {
        auto Mesh = xprim_geom::capsule::Generate( 15, 15, 1.5f, 5);

        //
        // Copy verts
        //
        if (auto Err = Device.Create(VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(e07::draw_vert), .m_EntryCount = static_cast<int>(Mesh.m_Vertices.size()) }); Err)
            return xgpu::getErrorInt(Err);

        (void)VertexBuffer.MemoryMap(0, static_cast<int>(Mesh.m_Vertices.size()), [&](void* pData)
        {
            auto pVertex = static_cast<e07::draw_vert*>(pData);
            for( int i = 0; const auto& V : Mesh.m_Vertices )
            {
                auto& DV = pVertex[i++];
                DV.m_X = V.m_Position.m_X;
                DV.m_Y = V.m_Position.m_Y;
                DV.m_Z = V.m_Position.m_Z;
                DV.m_U = V.m_Texcoord.m_X;
                DV.m_V = V.m_Texcoord.m_Y;
                DV.m_Color = ~0;

                /*
                float f = V.m_Normal.m_Z;
                xcore::icolor c;
                c.setupFromRGB(f,f,f);
                DV.m_Color = c.m_Value;
                */
            }
        });

        //
        // Copy Indices
        //
        if (auto Err = Device.Create(IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = static_cast<int>(Mesh.m_Indices.size()) }); Err)
            return xgpu::getErrorInt(Err);

        (void)IndexBuffer.MemoryMap(0, static_cast<int>(Mesh.m_Indices.size()), [&](void* pData)
        {
            auto pIndex = static_cast<std::uint32_t*>(pData);
            for( int i=0; const auto& I : Mesh.m_Indices )
            {
                pIndex[i++] = I;
            }
        });
    }

    //
    // Basic loop
    //
    xgpu::tools::view View;

    xgpu::mouse Mouse;
    {
        Instance.Create(Mouse, {});
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
            Angles.m_Yaw.m_Value -= 0.01f * MousePos[0];
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
                const auto  W2C = View.getW2C();

                // Render first object (animated mesh)
                {
                    e07::push_contants PushConstants;
                    xcore::matrix4 L2W;
                    L2W.setIdentity();
                    PushConstants.m_L2C = (W2C * L2W);

                    CmdBuffer.setPipelineInstance(PipeLineInstance[1]);
                    CmdBuffer.setBuffer(VertexBuffer);
                    CmdBuffer.setBuffer(IndexBuffer);
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

