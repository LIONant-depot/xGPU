#include "xGPU.h"
#include "xcore.h"
#include "../../tools/xgpu_view.h"
#include "../../tools/xgpu_view_inline.h"
#include "../../dependencies/xprim_geom/src/xprim_geom.h"
#include "../../dependencies/xbmp_tools/src/xbmp_tools.h"
#include "../../tools/xgpu_xcore_bitmap_helpers.h"

#include <iostream>

namespace e12
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
        xcore::vector2  m_TexCoord;
        xcore::icolor   m_Tangent;
        xcore::icolor   m_Normal;
        xcore::icolor   m_Color;
    };

    //------------------------------------------------------------------------------------------------

    struct push_constants
    {
        xcore::matrix4 m_L2C;
        xcore::vector4 m_LocalSpaceLightPos;    // We store gamma in the w
        xcore::vector3 m_LocalSpaceEyePos;
        xcore::vector4 m_AmbientLightColor;     // Color and Intensity
        xcore::vector4 m_LightColor;            // Color and Intensity
    };
}

//------------------------------------------------------------------------------------------------

int E12_Example()
{
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = true, .m_pLogErrorFunc = e12::DebugMessage, .m_pLogWarning = e12::DebugMessage }); Err)
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
                .m_Offset = offsetof(e12::draw_vert_btn, m_Position)
            ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D
            }
        ,   xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(e12::draw_vert_btn, m_TexCoord)
            ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
            }
        ,   xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(e12::draw_vert_btn, m_Tangent)
            ,   .m_Format = xgpu::vertex_descriptor::format::SINT8_4D_NORMALIZED
            }
        ,   xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(e12::draw_vert_btn, m_Normal)
            ,   .m_Format = xgpu::vertex_descriptor::format::SINT8_4D_NORMALIZED
            }
        ,   xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(e12::draw_vert_btn, m_Color)
            ,   .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED
            }
        };
        auto Setup = xgpu::vertex_descriptor::setup
        {
            .m_VertexSize = sizeof(e12::draw_vert_btn)
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
                    #include "x64\E12_frag_lighting.h"
                }
            };

            xgpu::shader::setup Setup
            {
                .m_Type                 = xgpu::shader::type::bit::FRAGMENT
            ,   .m_Sharer               = RawData
            };

            if (auto Err = Device.Create(MyFragmentShader, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader MyVertexShader;
        {
            auto RawData = xgpu::shader::setup::raw_data
            { std::array
                {
                    #include "x64\E12_vert_lighting.h"
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

        auto Shaders  = std::array<const xgpu::shader*,2>{ &MyVertexShader, &MyFragmentShader };
        auto Samplers = std::array
        { xgpu::pipeline::sampler{}
        , xgpu::pipeline::sampler{}
        , xgpu::pipeline::sampler{}
        , xgpu::pipeline::sampler{}
        , xgpu::pipeline::sampler{}
        };
        auto Setup    = xgpu::pipeline::setup
        {
            .m_VertexDescriptor  = VertexDescriptor
        ,   .m_Shaders           = Shaders
        ,   .m_PushConstantsSize = sizeof(e12::push_constants)
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
        xgpu::texture NormalTexture;
        xgpu::texture DiffuseTexture;
        xgpu::texture AOTexture;
        xgpu::texture GlossinessTexture;
        xgpu::texture RoughnessTexture;
        
        //
        // Load Normal Texture
        //
        {
            xcore::bitmap Bitmap;

            //
            // Load file
            //
            if (auto Err = xbmp::tools::loader::LoadDSS(Bitmap, "../../Assets/StoneWal01_1K/Stone Wall 01_1K_Normal - Compress BC5.dds" ); Err) // "../../Assets/normal_maps-test-BC5.dds");Err ) // 
            {
                e12::DebugMessage(xbmp::tools::getErrorMsg(Err));
                std::exit(xbmp::tools::getErrorInt(Err));
            }
            Bitmap.setColorSpace( xcore::bitmap::color_space::LINEAR );

            //
            // Create Texture
            //
            if (auto Err = xgpu::tools::bitmap::Create(NormalTexture, Device, Bitmap); Err)
            {
                e12::DebugMessage(xgpu::getErrorMsg(Err));
                std::exit(xgpu::getErrorInt(Err));
            }
        }

        //
        // Load Normal Texture
        //
        {
            xcore::bitmap Bitmap;

            //
            // Load file
            //
            if (auto Err = xbmp::tools::loader::LoadDSS(Bitmap, "../../Assets/StoneWal01_1K/Stone Wall 01_1K_Diffuse.dds"); Err)
            {
                e12::DebugMessage(xbmp::tools::getErrorMsg(Err));
                std::exit(xbmp::tools::getErrorInt(Err));
            }
            Bitmap.setColorSpace(xcore::bitmap::color_space::SRGB);

            //
            // Create Texture
            //
            if (auto Err = xgpu::tools::bitmap::Create(DiffuseTexture, Device, Bitmap); Err)
            {
                e12::DebugMessage(xgpu::getErrorMsg(Err));
                std::exit(xgpu::getErrorInt(Err));
            }
        }

        //
        // Load Ambient Occlusion Texture
        //
        {
            xcore::bitmap Bitmap;

            //
            // Load file
            //
            if (auto Err = xbmp::tools::loader::LoadDSS(Bitmap, "../../Assets/StoneWal01_1K/Stone Wall 01_1K_Ambient Occlusion.dds"); Err)
            {
                e12::DebugMessage(xbmp::tools::getErrorMsg(Err));
                std::exit(xbmp::tools::getErrorInt(Err));
            }
            Bitmap.setColorSpace(xcore::bitmap::color_space::LINEAR);

            //
            // Create Texture
            //
            if (auto Err = xgpu::tools::bitmap::Create(AOTexture, Device, Bitmap); Err)
            {
                e12::DebugMessage(xgpu::getErrorMsg(Err));
                std::exit(xgpu::getErrorInt(Err));
            }
        }

        //
        // Load Glossiness Texture
        //
        {
            xcore::bitmap Bitmap;

            //
            // Load file
            //
            if (auto Err = xbmp::tools::loader::LoadDSS(Bitmap, "../../Assets/StoneWal01_1K/Stone Wall 01_1K_Glossiness.dds"); Err)
            {
                e12::DebugMessage(xbmp::tools::getErrorMsg(Err));
                std::exit(xbmp::tools::getErrorInt(Err));
            }
            Bitmap.setColorSpace(xcore::bitmap::color_space::LINEAR);

            //
            // Create Texture
            //
            if (auto Err = xgpu::tools::bitmap::Create(GlossinessTexture, Device, Bitmap); Err)
            {
                e12::DebugMessage(xgpu::getErrorMsg(Err));
                std::exit(xgpu::getErrorInt(Err));
            }
        }

        //
        // Load Roughness Texture
        //
        {
            xcore::bitmap Bitmap;

            //
            // Load file
            //
            if (auto Err = xbmp::tools::loader::LoadDSS(Bitmap, "../../Assets/StoneWal01_1K/Stone Wall 01_1K_Roughness.dds"); Err)
            {
                e12::DebugMessage(xbmp::tools::getErrorMsg(Err));
                std::exit(xbmp::tools::getErrorInt(Err));
            }
            Bitmap.setColorSpace(xcore::bitmap::color_space::LINEAR);

            //
            // Create Texture
            //
            if (auto Err = xgpu::tools::bitmap::Create(RoughnessTexture, Device, Bitmap); Err)
            {
                e12::DebugMessage(xgpu::getErrorMsg(Err));
                std::exit(xgpu::getErrorInt(Err));
            }
        }

        auto Bindings = std::array
        { xgpu::pipeline_instance::sampler_binding{ NormalTexture }
        , xgpu::pipeline_instance::sampler_binding{DiffuseTexture} 
        , xgpu::pipeline_instance::sampler_binding{AOTexture}
        , xgpu::pipeline_instance::sampler_binding{GlossinessTexture}
        , xgpu::pipeline_instance::sampler_binding{RoughnessTexture}
        };
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
    auto Mesh = //xprim_geom::uvsphere::Generate( 30, 30, 2, 1 ); xcore::vector2 UVScale{ 4,4 };
                //xprim_geom::capsule::Generate( 30,30,1,4); xcore::vector2 UVScale{3,3};
                xprim_geom::cube::Generate( 4, 4, 4, 4, xprim_geom::float3{1,1,1} ); xcore::vector2 UVScale{1,1};

    xgpu::buffer VertexBuffer;
    {
        if (auto Err = Device.Create(VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(e12::draw_vert_btn), .m_EntryCount = static_cast<int>(Mesh.m_Vertices.size()) }); Err)
            return xgpu::getErrorInt(Err);

        (void)VertexBuffer.MemoryMap(0, static_cast<int>(Mesh.m_Vertices.size()), [&](void* pData)
            {
                auto pVertex = static_cast<e12::draw_vert_btn*>(pData);
                for( int i=0; i< static_cast<int>(Mesh.m_Vertices.size()); ++i )
                {
                    auto&       V  = pVertex[i];
                    const auto& v  = Mesh.m_Vertices[i];
                    V.m_Position.setup( v.m_Position.m_X, v.m_Position.m_Y, v.m_Position.m_Z );

                    V.m_Normal.m_R = static_cast<std::uint8_t>(static_cast<std::int8_t>(v.m_Normal.m_X < 0 ? std::max(-128, static_cast<int>(v.m_Normal.m_X * 128)) : std::min(127, static_cast<int>(v.m_Normal.m_X * 127))));
                    V.m_Normal.m_G = static_cast<std::uint8_t>(static_cast<std::int8_t>(v.m_Normal.m_Y < 0 ? std::max(-128, static_cast<int>(v.m_Normal.m_Y * 128)) : std::min(127, static_cast<int>(v.m_Normal.m_Y * 127))));
                    V.m_Normal.m_B = static_cast<std::uint8_t>(static_cast<std::int8_t>(v.m_Normal.m_Z < 0 ? std::max(-128, static_cast<int>(v.m_Normal.m_Z * 128)) : std::min(127, static_cast<int>(v.m_Normal.m_Z * 127))));

                    V.m_Tangent.m_R = static_cast<std::uint8_t>(static_cast<std::int8_t>(v.m_Tangent.m_X < 0 ? std::max(-128, static_cast<int>(v.m_Tangent.m_X * 128)) : std::min(127, static_cast<int>(v.m_Tangent.m_X * 127))));
                    V.m_Tangent.m_G = static_cast<std::uint8_t>(static_cast<std::int8_t>(v.m_Tangent.m_Y < 0 ? std::max(-128, static_cast<int>(v.m_Tangent.m_Y * 128)) : std::min(127, static_cast<int>(v.m_Tangent.m_Y * 127))));
                    V.m_Tangent.m_B = static_cast<std::uint8_t>(static_cast<std::int8_t>(v.m_Tangent.m_Z < 0 ? std::max(-128, static_cast<int>(v.m_Tangent.m_Z * 128)) : std::min(127, static_cast<int>(v.m_Tangent.m_Z * 127))));

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
             
                e12::push_constants PushConstants;
                PushConstants.m_L2C                 = W2C * L2W;
                PushConstants.m_LocalSpaceEyePos    = W2L * View.getPosition();
                PushConstants.m_LocalSpaceLightPos  = W2L * LightPosition;
                PushConstants.m_AmbientLightColor.setup( 0.05f, 0.05f, 0.05f, 1.0f );
                PushConstants.m_LightColor.setup( 0.8f, 0.8f, 0.8f, 1.0f );
                PushConstants.m_LocalSpaceEyePos.m_W = 2.2f;  // We store gamma in the w component of the eye

                CmdBuffer.setPipelineInstance(PipeLineInstance);
                CmdBuffer.setBuffer(VertexBuffer);
                CmdBuffer.setBuffer(IndexBuffer);
                CmdBuffer.setPushConstants( PushConstants );
                CmdBuffer.Draw(IndexBuffer.getEntryCount());
            }
        }

        // Swap the buffers
        Window.PageFlip();
    }

    return 0;
}

