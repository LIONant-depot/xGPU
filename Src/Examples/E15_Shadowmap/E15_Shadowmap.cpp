#include "xGPU.h"
#include "xcore.h"
#include "../../tools/xgpu_view.h"
#include "../../tools/xgpu_view_inline.h"
#include "../../dependencies/xprim_geom/src/xprim_geom.h"
#include "../../dependencies/xbmp_tools/src/xbmp_tools.h"
#include "../../tools/xgpu_xcore_bitmap_helpers.h"

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
    xcore::vector3d m_Position;
    xcore::vector2  m_TexCoord;
    xcore::icolor   m_Normal;
};

struct lighting_uniform_buffer
{
    xcore::matrix4 m_L2C;
    xcore::matrix4 m_ShadowL2CPlus;
    xcore::vector4 m_LocalSpaceLightPos;
};

struct shadow_generation_push_constants
{
    xcore::matrix4 m_L2C;                   // To light space
};

//------------------------------------------------------------------------------------------------

int E15_Example()
{
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = true, .m_pLogErrorFunc = DebugMessage, .m_pLogWarning = DebugMessage }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err)
        return xgpu::getErrorInt(Err);

    xgpu::window Window;
    if (auto Err = Device.Create(Window, {.m_ClearColorR = 0.25f, .m_ClearColorG = 0.25f, .m_ClearColorB = 0.25f }); Err)
        return xgpu::getErrorInt(Err);

    //
    // Generate Shadow map render pass
    //
    xgpu::renderpass RenderPass;
    xgpu::texture    ShadowMapTexture;
    {
        if (auto Err = Device.Create(ShadowMapTexture, { .m_Format = xgpu::texture::format::DEPTH_U16, .m_Type = xgpu::texture::type::LINEAR, .m_Width = 1024, .m_Height = 1024 }); Err)
            return xgpu::getErrorInt(Err);

        std::array<xgpu::renderpass::attachment, 1> Attachments
        { 
            ShadowMapTexture 
        };

        if (auto Err = Device.Create(RenderPass, { .m_Attachments = Attachments }); Err)
            return xgpu::getErrorInt(Err);
    }
    
    //
    // Shadow Generation Pipeline Instance
    //  
    xgpu::pipeline_instance ShadowGenerationPipeLineInstance;
    {
        xgpu::shader FragmentShader;
        {
            auto RawData = xgpu::shader::setup::raw_data
            { std::array {
                #include "x64\E15_ShadowmapGeneration_frag.h" 
            }};

            if (auto Err = Device.Create(FragmentShader, { .m_Type = xgpu::shader::type::bit::FRAGMENT, .m_Sharer = RawData }); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader VertexShader;
        {
            auto RawData = xgpu::shader::setup::raw_data
            { std::array {
                #include "x64\E15_ShadowmapGeneration_vert.h"
            }};

            if (auto Err = Device.Create(VertexShader, { .m_Type = xgpu::shader::type::bit::VERTEX, .m_Sharer = RawData }); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::vertex_descriptor ShadowmapVertexDescriptor;
        {
            auto Attributes = std::array
            {
                xgpu::vertex_descriptor::attribute
                {
                    .m_Offset = offsetof(draw_vert, m_Position)
                ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D
                }
            };
            auto Setup = xgpu::vertex_descriptor::setup
            {
                .m_VertexSize = sizeof(draw_vert)
            ,   .m_Attributes = Attributes
            };

            if (auto Err = Device.Create(ShadowmapVertexDescriptor, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        auto Shaders = std::array<const xgpu::shader*, 2>{ &VertexShader, &FragmentShader };
        auto Setup   = xgpu::pipeline::setup
        { .m_VertexDescriptor  = ShadowmapVertexDescriptor
        , .m_Shaders           = Shaders
        , .m_PushConstantsSize = sizeof(shadow_generation_push_constants)
        , .m_DepthStencil      = { .m_DepthBiasConstantFactor = 1.25        // Depth bias (and slope) are used to avoid shadowing artifacts (always applied)
                                 , .m_DepthBiasSlopeFactor    = 1.75f       // Slope depth bias factor, applied depending on polygon's slope
                                 , .m_bDepthBiasEnable        = true        // Enable the depth bias
                                 }
        };

        xgpu::pipeline ShadowGenerationPipeLine;
        if (auto Err = Device.Create(ShadowGenerationPipeLine, Setup); Err)
            return xgpu::getErrorInt(Err);

        if (auto Err = Device.Create(ShadowGenerationPipeLineInstance, { .m_PipeLine = ShadowGenerationPipeLine } ); Err)
            return xgpu::getErrorInt(Err);
    }

    //
    // Render with Shadow Pipeline
    //
    xgpu::pipeline PipeLine;
    {
        xgpu::shader MyFragmentShader;
        {
            const std::uint32_t RawDataArray[] = {
                #include "x64\E15_ShadowmapLighting_frag.h" 
            };
            auto RawData = xgpu::shader::setup::raw_data
            { .m_Value = { (const int*)RawDataArray, sizeof(RawDataArray) / 4 }
            };

            if (auto Err = Device.Create(MyFragmentShader, { .m_Type = xgpu::shader::type::bit::FRAGMENT, .m_Sharer = RawData } ); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader MyVertexShader;
        {
            auto RawData = xgpu::shader::setup::raw_data
            { std::array {
                #include "x64\E15_ShadowmapLighting_vert.h"
            }};

            if (auto Err = Device.Create(MyVertexShader, { .m_Type = xgpu::shader::type::bit::VERTEX, .m_Sharer = RawData }); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::vertex_descriptor VertexDescriptor;
        {
            auto Attributes = std::array
            {
                xgpu::vertex_descriptor::attribute
                {
                    .m_Offset = offsetof(draw_vert, m_Position)
                ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D
                }
            ,   xgpu::vertex_descriptor::attribute
                {
                    .m_Offset = offsetof(draw_vert, m_TexCoord)
                ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
                }
            ,   xgpu::vertex_descriptor::attribute
                {
                    .m_Offset = offsetof(draw_vert, m_Normal)
                ,   .m_Format = xgpu::vertex_descriptor::format::SINT8_4D_NORMALIZED
                }
            };

            if (auto Err = Device.Create(VertexDescriptor, { .m_VertexSize = sizeof(draw_vert), .m_Attributes = Attributes }); Err)
                return xgpu::getErrorInt(Err);
        }

        auto UBuffersUsage  = std::array{ xgpu::shader::type{xgpu::shader::type::bit::VERTEX} };
        auto Shaders        = std::array<const xgpu::shader*,2>{ &MyVertexShader, &MyFragmentShader };
        auto Samplers       = std::array
        {   xgpu::pipeline::sampler
            { .m_MaxAnisotropy = 1
            , .m_AddressMode   = {xgpu::pipeline::sampler::address_mode::CLAMP,xgpu::pipeline::sampler::address_mode::CLAMP,xgpu::pipeline::sampler::address_mode::CLAMP}
            , .m_MipmapMag     = xgpu::pipeline::sampler::mipmap_sampler::NEAREST
            , .m_MipmapMin     = xgpu::pipeline::sampler::mipmap_sampler::NEAREST
            , .m_MipmapMode    = xgpu::pipeline::sampler::mipmap_mode::LINEAR 
            }
        ,   xgpu::pipeline::sampler{}
        };

        auto Setup = xgpu::pipeline::setup
        { .m_VertexDescriptor   = VertexDescriptor
        , .m_Shaders            = Shaders
        , .m_UniformBufferUsage = UBuffersUsage
        , .m_Samplers           = Samplers
        };

        if (auto Err = Device.Create(PipeLine, Setup); Err)
            return xgpu::getErrorInt(Err);
    }
    
    //
    // Setup the material instance
    //
    xgpu::pipeline_instance PipeLineInstance;
    {
        xgpu::texture DiffuseTexture;
        
        //
        // Load Albedo Texture
        //
        {
            xcore::bitmap Bitmap;

            //
            // Load file
            //
            if (auto Err = xbmp::tools::loader::LoadDSS(Bitmap, "../../Assets/StoneWal01_1K/Stone Wall 01_1K_Diffuse.dds"); Err)
            {
                DebugMessage(xbmp::tools::getErrorMsg(Err));
                std::exit(xbmp::tools::getErrorInt(Err));
            }
            Bitmap.setColorSpace(xcore::bitmap::color_space::SRGB);

            //
            // Create Texture
            //
            if (auto Err = xgpu::tools::bitmap::Create(DiffuseTexture, Device, Bitmap); Err)
            {
                DebugMessage(xgpu::getErrorMsg(Err));
                std::exit(xgpu::getErrorInt(Err));
            }
        }

        //
        // Create the Lighting Uniform Buffer
        //
        xgpu::buffer LightingUBO;
        if (auto Err = Device.Create(LightingUBO, { .m_Type = xgpu::buffer::type::UNIFORM, .m_Usage = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ, .m_EntryByteSize = sizeof(lighting_uniform_buffer), .m_EntryCount = 16 }); Err)
            return xgpu::getErrorInt(Err);

        //
        // Create our pipeline instance
        //
        auto Textures = std::array
        { xgpu::pipeline_instance::sampler_binding{ShadowMapTexture}
        , xgpu::pipeline_instance::sampler_binding{DiffuseTexture} 
        };

        auto UniformBuffers = std::array
        { xgpu::pipeline_instance::uniform_buffer{ LightingUBO }
        };
        auto Setup    = xgpu::pipeline_instance::setup
        { .m_PipeLine               = PipeLine
        , .m_UniformBuffersBindings = UniformBuffers
        , .m_SamplersBindings       = Textures
        };

        if (auto Err = Device.Create(PipeLineInstance, Setup); Err)
            return xgpu::getErrorInt(Err);
    }

    //
    // Create mesh
    //
    auto Mesh = xprim_geom::cube::Generate( 4, 4, 4, 4, xprim_geom::float3{1,1,1} ); xcore::vector2 UVScale{1,1};

    xgpu::buffer VertexBuffer;
    {
        if (auto Err = Device.Create(VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(draw_vert), .m_EntryCount = static_cast<int>(Mesh.m_Vertices.size()) }); Err)
            return xgpu::getErrorInt(Err);

        (void)VertexBuffer.MemoryMap(0, static_cast<int>(Mesh.m_Vertices.size()), [&](void* pData)
        {
            auto pVertex = static_cast<draw_vert*>(pData);
            for( int i=0; i< static_cast<int>(Mesh.m_Vertices.size()); ++i )
            {
                auto&       V  = pVertex[i];
                const auto& v  = Mesh.m_Vertices[i];
                V.m_Position.setup( v.m_Position.m_X, v.m_Position.m_Y, v.m_Position.m_Z );

                V.m_TexCoord.setup(v.m_Texcoord.m_X* UVScale.m_X, v.m_Texcoord.m_Y* UVScale.m_Y);

                V.m_Normal.m_R = static_cast<std::uint8_t>(static_cast<std::int8_t>(v.m_Normal.m_X < 0 ? std::max(-128, static_cast<int>(v.m_Normal.m_X * 128)) : std::min(127, static_cast<int>(v.m_Normal.m_X * 127))));
                V.m_Normal.m_G = static_cast<std::uint8_t>(static_cast<std::int8_t>(v.m_Normal.m_Y < 0 ? std::max(-128, static_cast<int>(v.m_Normal.m_Y * 128)) : std::min(127, static_cast<int>(v.m_Normal.m_Y * 127))));
                V.m_Normal.m_B = static_cast<std::uint8_t>(static_cast<std::int8_t>(v.m_Normal.m_Z < 0 ? std::max(-128, static_cast<int>(v.m_Normal.m_Z * 128)) : std::min(127, static_cast<int>(v.m_Normal.m_Z * 127))));
            }
        });
    }

    xgpu::buffer IndexBuffer;
    {
        if (auto Err = Device.Create(IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = static_cast<int>(Mesh.m_Indices.size()) }); Err)
            return xgpu::getErrorInt(Err);

        (void)IndexBuffer.MemoryMap(0, static_cast<int>(Mesh.m_Indices.size()), [&](void* pData)
        {
            auto pIndex = static_cast<std::uint32_t*>(pData);
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
    xgpu::tools::view LightingView;

    View.setFov( 60_xdeg );

    LightingView.setFov(60_xdeg);
    LightingView.setViewport( {0,0,ShadowMapTexture.getTextureDimensions()[0], ShadowMapTexture.getTextureDimensions()[1] });
    LightingView.setNearZ( 0.01f );
    

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

        // Do some random rendering
        {
            auto        CmdBuffer   = Window.StartRenderPass( RenderPass );

            if(FollowCamera)
            {
                LightingView.LookAt(Distance, Angles, { 0,0,0 });
            }
            const auto  W2C         = LightingView.getW2C();

            // Render first object (animated mesh)
            {
                {
                    xcore::matrix4 L2W;
                    L2W.setIdentity();

                    shadow_generation_push_constants PushConstants;
                    PushConstants.m_L2C = W2C * L2W;

                    CmdBuffer.setPipelineInstance(ShadowGenerationPipeLineInstance);
                    CmdBuffer.setBuffer(VertexBuffer);
                    CmdBuffer.setBuffer(IndexBuffer);
                    CmdBuffer.setConstants(0, &PushConstants, sizeof(PushConstants));
                    CmdBuffer.Draw(IndexBuffer.getEntryCount());
                }

                // Floor
                {
                    xcore::matrix4 L2W;
                    L2W.setIdentity();
                    L2W.setScale({ 100, 0.5f, 100 });
                    L2W.setTranslation({ 0, -1, 0 });

                    shadow_generation_push_constants PushConstants;
                    PushConstants.m_L2C = W2C * L2W;

                    CmdBuffer.setBuffer(VertexBuffer);
                    CmdBuffer.setBuffer(IndexBuffer);
                    CmdBuffer.setConstants(0, &PushConstants, sizeof(PushConstants));
                    CmdBuffer.Draw(IndexBuffer.getEntryCount());
                }
            }
        }

        // Get the command buffer ready to render
        {
            auto        CmdBuffer = Window.getCmdBuffer();
            const auto  W2C       = View.getW2C();

            static const xcore::matrix4 LightBiasMatrix = []
            {
                xcore::matrix4 LightBiasMatrix;
                LightBiasMatrix.setIdentity();
                LightBiasMatrix.setScale({ 0.5f, 0.5f, 1.0f });
                LightBiasMatrix.setTranslation({ 0.5f, 0.5f, 0.0f });
                return LightBiasMatrix;
            }();

            // Render first object (animated mesh)
            {

                {
                    xcore::matrix4 L2W;
                    L2W.setIdentity();

                    xcore::matrix4 W2L = L2W;
                    W2L.InvertSRT();

                    // Shadow Matrix
                    xcore::matrix4 LightMatrixPlus;
                    LightMatrixPlus = LightBiasMatrix * LightingView.getW2C() * L2W;

                    CmdBuffer.setPipelineInstance(PipeLineInstance);

                    auto& LightUniforms = CmdBuffer.getUniformBufferVMem<lighting_uniform_buffer>(xgpu::shader::type::bit::VERTEX);
                    LightUniforms.m_ShadowL2CPlus       = LightMatrixPlus;
                    LightUniforms.m_L2C                 = W2C * L2W;
                    LightUniforms.m_LocalSpaceLightPos  = W2L * LightPosition;


                    CmdBuffer.setBuffer(VertexBuffer);
                    CmdBuffer.setBuffer(IndexBuffer);
                    CmdBuffer.Draw(IndexBuffer.getEntryCount());
                }

                // Render floor
                {
                    xcore::matrix4 L2W;
                    L2W.setIdentity();
                    L2W.setScale({ 100, 0.5f, 100 });
                    L2W.setTranslation({ 0, -1, 0 });

                    xcore::matrix4 W2L = L2W;
                    W2L.InvertSRT();

                    // Shadow Matrix
                    xcore::matrix4 LightMatrixPlus;
                    LightMatrixPlus = LightBiasMatrix * LightingView.getW2C() * L2W;

                    CmdBuffer.setPipelineInstance(PipeLineInstance);

                    auto& LightUniforms = CmdBuffer.getUniformBufferVMem<lighting_uniform_buffer>(xgpu::shader::type::bit::VERTEX);
                    LightUniforms.m_ShadowL2CPlus       = LightMatrixPlus;
                    LightUniforms.m_L2C                 = W2C * L2W;
                    LightUniforms.m_LocalSpaceLightPos  = W2L * LightPosition;

                    CmdBuffer.setBuffer(VertexBuffer);
                    CmdBuffer.setBuffer(IndexBuffer);
                    CmdBuffer.Draw(IndexBuffer.getEntryCount());
                }    

            }
        }

        // Swap the buffers
        Window.PageFlip();
    }

    return 0;
}
