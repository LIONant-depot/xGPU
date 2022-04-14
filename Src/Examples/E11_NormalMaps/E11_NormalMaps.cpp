#include "xGPU.h"
#include "xcore.h"
#include "../../tools/xgpu_view.h"
#include "../../tools/xgpu_view_inline.h"
#include "../../dependencies/xprim_geom/src/xprim_geom.h"
#include "../../dependencies/xbmp_tools/src/xbmp_tools.h"
#include "../../tools/xgpu_xcore_bitmap_helpers.h"

#include <iostream>

namespace e11
{
    //------------------------------------------------------------------------------------------------
    static
    void DebugMessage(std::string_view View)
    {
        printf("%s\n", View.data());
    }

    //------------------------------------------------------------------------------------------------

    struct draw_vert_btn
    {
        xcore::vector3d m_Position;
        xcore::vector3d m_Binormal;
        xcore::vector3d m_Tangent;
        xcore::vector3d m_Normal;
        xcore::vector2  m_TexCoord;
        xcore::icolor   m_Color;
    };

    //------------------------------------------------------------------------------------------------

    struct push_contants
    {
        xcore::matrix4 m_L2C;
        xcore::vector3 m_LocalSpaceLightPosition;
    };
}

//------------------------------------------------------------------------------------------------

int E11_Example()
{
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = true, .m_pLogErrorFunc = e11::DebugMessage, .m_pLogWarning = e11::DebugMessage }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err)
        return xgpu::getErrorInt(Err);

    xgpu::window Window;
    if (auto Err = Device.Create(Window, {}); Err)
        return xgpu::getErrorInt(Err);

    xgpu::vertex_descriptor VertexDescriptor;
    {
        auto Attributes = std::array
        {
            xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(e11::draw_vert_btn, m_Position)
            ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D
            }
        ,   xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(e11::draw_vert_btn, m_Binormal)
            ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D
            }
        ,   xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(e11::draw_vert_btn, m_Tangent)
            ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D
            }
        ,   xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(e11::draw_vert_btn, m_Normal)
            ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D
            }
        ,   xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(e11::draw_vert_btn, m_TexCoord)
            ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
            }
        ,   xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(e11::draw_vert_btn, m_Color)
            ,   .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED
            }
        };
        auto Setup = xgpu::vertex_descriptor::setup
        {
            .m_VertexSize = sizeof(e11::draw_vert_btn)
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
                    #include "x64\E11_frag_btn.h"
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
                    #include "x64\E11_vert_btn.h"
                }
            };
            xgpu::shader::setup Setup
            {
                .m_Type                 = xgpu::shader::type::bit::VERTEX
            ,   .m_Sharer               = RawData
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
        ,   .m_PushConstantsSize = sizeof(e11::push_contants)
        ,   .m_Samplers          = Samplers
        };

        if (auto Err = Device.Create(PipeLine, Setup); Err)
            return xgpu::getErrorInt(Err);
    }

    //
    // Setup the material instance
    //
    xgpu::pipeline_instance PipeLineInstance;
    {
        xgpu::texture Texture;
        
        //
        // Load texture
        //
        {
            xcore::bitmap Bitmap;

            //
            // Load file
            //
            if (auto Err = xbmp::tools::loader::LoadDSS(Bitmap, //"../../Assets/normal_maps-test-fullcolor.dds"
                                                                 "../../Assets/normal_maps-test-BC5.dds" 
                                                                // "../../Assets/normal_mapping_normal_map.dds"
                                                                //    "../../Assets/normal_map_raisen.dds"
                                                                // "../../Assets/StoneWal01_1K/Stone Wall 01_1K_Normal - Compress BC5.dds"
                                                                // "../../Assets/StoneWal01_1K/Stone Wall 01_1K_Normal - Compress DXT5.dds"
            ); Err)
            {
                e11::DebugMessage(xbmp::tools::getErrorMsg(Err));
                std::exit(xbmp::tools::getErrorInt(Err));
            }
            Bitmap.setColorSpace( xcore::bitmap::color_space::LINEAR );

            //
            // Create Texture
            //
            if (auto Err = xgpu::tools::bitmap::Create(Texture, Device, Bitmap); Err)
            {
                e11::DebugMessage(xgpu::getErrorMsg(Err));
                std::exit(xgpu::getErrorInt(Err));
            }
        }

        auto Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{ Texture } };
        auto Setup    = xgpu::pipeline_instance::setup
        { .m_PipeLine           = PipeLine
        , .m_SamplersBindings   = Bindings
        };

        if (auto Err = Device.Create(PipeLineInstance, Setup); Err)
            return xgpu::getErrorInt(Err);
    }

    //
    // Create mesh
    //
    auto Mesh = //xprim_geom::uvsphere::Generate( 30, 30, 2, 1 );  xcore::vector2 UVScale{ 4,4 };
                //xprim_geom::capsule::Generate( 30,30,1,4); xcore::vector2 UVScale{3,3};
                xprim_geom::cube::Generate( 4, 4, 4, 4, xprim_geom::float3{1,1,1} ); xcore::vector2 UVScale{1,1};



    //
    // Recompute the tangent vectors
    //
    if( false ) for( int index=0, n = (int)Mesh.m_Indices.size(); index<n; index += 3 )
    {
        std::array<xcore::vector3,3> pos;
        std::array<xcore::vector2,3> uv;

        // copy data for the 3 vertices
        for( int i=0; i<3; ++i)
        {
            const auto& Vert = Mesh.m_Vertices[Mesh.m_Indices[index + i]];
            pos[i].setup(Vert.m_Position.m_X, Vert.m_Position.m_Y, Vert.m_Position.m_Z );
            uv[i].setup(Vert.m_Texcoord.m_X, Vert.m_Texcoord.m_Y );
        }

        //
        // Compute the Tangent and Binormal Vectors for the first vertex
        //
        const auto edge1    = pos[1] - pos[0];
        const auto edge2    = pos[2] - pos[0];
        const auto deltaUV1 = uv[1] - uv[0];
        const auto deltaUV2 = uv[2] - uv[0];

        const float f = 1.0f / (deltaUV1.m_X * deltaUV2.m_Y - deltaUV2.m_X * deltaUV1.m_Y);

        const xcore::vector3 Tangent = xcore::vector3
        { f* (deltaUV2.m_Y* edge1.m_X - deltaUV1.m_Y * edge2.m_X)
        , f* (deltaUV2.m_Y* edge1.m_Y - deltaUV1.m_Y * edge2.m_Y)
        , f* (deltaUV2.m_Y* edge1.m_Z - deltaUV1.m_Y * edge2.m_Z)
        }.NormalizeSafe();

        const xcore::vector3 Binormal = xcore::vector3
        { f * (-deltaUV2.m_X * edge1.m_X + deltaUV1.m_X * edge2.m_X)
        , f * (-deltaUV2.m_X * edge1.m_Y + deltaUV1.m_X * edge2.m_Y)
        , f * (-deltaUV2.m_X * edge1.m_Z + deltaUV1.m_X * edge2.m_Z)
        }.NormalizeSafe();

        //
        // Make sure everything is correct
        //
        for (int i = 0; i < 3; ++i)
        {
            auto& Vert = Mesh.m_Vertices[Mesh.m_Indices[index+i]];

            Vert.m_Tangent.m_X = Tangent.m_X;
            Vert.m_Tangent.m_Y = Tangent.m_Y;
            Vert.m_Tangent.m_Z = Tangent.m_Z;

            const xcore::vector3 ONormal { Vert.m_Normal.m_X,   Vert.m_Normal.m_Y, Vert.m_Normal.m_Z  };
            const xcore::vector3 OTangent{ Vert.m_Tangent.m_X, Vert.m_Tangent.m_Y, Vert.m_Tangent.m_Z };
            const xcore::vector3 OBinormal = (xcore::vector3{ OTangent }.Cross(xcore::vector3{ ONormal })).NormalizeSafe();
        }
    }


    xgpu::buffer VertexBuffer;
    {
        if (auto Err = Device.Create(VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(e11::draw_vert_btn), .m_EntryCount = static_cast<int>(Mesh.m_Vertices.size()) }); Err)
            return xgpu::getErrorInt(Err);

        (void)VertexBuffer.MemoryMap(0, static_cast<int>(Mesh.m_Vertices.size()), [&](void* pData)
            {
                auto pVertex = static_cast<e11::draw_vert_btn*>(pData);
                for( int i=0; i< static_cast<int>(Mesh.m_Vertices.size()); ++i )
                {
                    auto&       V  = pVertex[i];
                    const auto& v  = Mesh.m_Vertices[i];
                    V.m_Position.setup( v.m_Position.m_X, v.m_Position.m_Y, v.m_Position.m_Z );
                    V.m_Normal.setup( v.m_Normal.m_X, v.m_Normal.m_Y, v.m_Normal.m_Z );

                    V.m_Tangent.setup(v.m_Tangent.m_X, v.m_Tangent.m_Y, v.m_Tangent.m_Z);
                    V.m_Binormal = (xcore::vector3{ V.m_Tangent }.Cross(xcore::vector3{ V.m_Normal } )).NormalizeSafe();

                    V.m_TexCoord.setup(v.m_Texcoord.m_X* UVScale.m_X, v.m_Texcoord.m_Y* UVScale.m_Y);
                    V.m_Color = xcore::icolor{0xffffffffu};
                }
            });
    }

    xgpu::buffer IndexBuffer;
    {
        if (auto Err = Device.Create(IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = static_cast<int>(Mesh.m_Indices.size()) }); Err)
            return xgpu::getErrorInt(Err);

        (void)IndexBuffer.MemoryMap(0, static_cast<int>(Mesh.m_Indices.size()), [&](void* pData)
            {
                auto            pIndex      = static_cast<std::uint32_t*>(pData);
                for( int i=0; i< static_cast<int>(Mesh.m_Indices.size()); ++i )
                {
                    pIndex[i] = Mesh.m_Indices[i];
                }
            });
    }

    //
    // Basic loop
    //
    xgpu::tools::view View;

    View.setFov( 60_xdeg );

    xgpu::mouse Mouse;
    xgpu::keyboard Keyboard;
    {
        Instance.Create(Mouse, {});
        Instance.Create(Keyboard, {});
    }

    xcore::vector3 FrozenLightPosition;

    xcore::radian3 Angles;
    float          Distance = 2;
    bool           FollowCamera = true;
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

        if( Keyboard.wasPressed( xgpu::keyboard::digital::KEY_SPACE) )
        {
            FrozenLightPosition = View.getPosition();
            FollowCamera = !FollowCamera;
        }

        Distance += Distance * -0.2f * Mouse.getValue(xgpu::mouse::analog::WHEEL_REL)[0];
        if (Distance < 0.5f) Distance = 0.5f;

        // Update the camera
        View.LookAt(Distance, Angles, { 0,0,0 });

        static xcore::radian R{ 0 };
        R += xcore::radian{ 0.001f };

        auto LightPosition = FollowCamera ? View.getPosition() : FrozenLightPosition; 

        //
        // Rendering
        //
        // Windows can not display when they are minimize
        if (Window.BeginRendering()) continue;

        // Update the view with latest window size
        View.setViewport({ 0, 0, Window.getWidth(), Window.getHeight() });

        // Get the command buffer ready to render
        {
            auto        CmdBuffer = Window.getCmdBuffer();
            const auto  W2C = View.getW2C();

            // Render first object (animated mesh)
            {
                xcore::matrix4 L2W;
                L2W.setIdentity();
                
                // Take the light to local space of the object
                auto W2L = L2W;
                W2L.InvertSRT();

                e11::push_contants PushConstans;
                PushConstans.m_L2C                      = W2C * L2W;
                PushConstans.m_LocalSpaceLightPosition  = W2L * LightPosition;

                CmdBuffer.setPipelineInstance(PipeLineInstance);
                CmdBuffer.setBuffer(VertexBuffer);
                CmdBuffer.setBuffer(IndexBuffer);
                CmdBuffer.setPushConstants( PushConstans );
                CmdBuffer.Draw(IndexBuffer.getEntryCount());
            }
        }

        // Swap the buffers
        Window.PageFlip();
    }

    return 0;
}

