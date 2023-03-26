#include "xGPU.h"
#include <iostream>

#include "../E16_Animation/E16_Importer.h"
#include "../../tools/xgpu_view.h"
#include "../../tools/xgpu_view_inline.h"

#include "../../dependencies/xprim_geom/src/xprim_geom.h"

//------------------------------------------------------------------------------------------------
static
void DebugMessage(std::string_view View)
{
    printf("%s\n", View.data());
}

//------------------------------------------------------------------------------------------------

namespace e17
{
    struct skin_render;
}

//------------------------------------------------------------------------------------------------

struct e17::skin_render
{
    struct mesh
    {
        std::string     m_Name;
        std::uint32_t   m_iSubmesh;
        std::uint32_t   m_nSubmeshes;
    };

    struct submesh
    {
        std::uint32_t   m_iVertex;
        std::uint32_t   m_iIndex;
        std::uint32_t   m_nIndices;
        std::uint32_t   m_iMaterialInstance;
    };

    struct push_constants
    {
        xcore::vector4 m_LightColor;
        xcore::vector4 m_AmbientLightColor;
        xcore::vector3 m_WorldSpaceLightPos;
        xcore::vector3 m_WorldSpaceEyePos;
    };

    struct alignas(256) shader_uniform_buffer
    {
        xcore::matrix4                  m_W2C;
        std::array<xcore::matrix4,256>  m_L2W;
    };

    int Initialize(xgpu::device& Device, const e16::skin_geom& SkinGeom)
    {
        auto nVertices  = 0ull;
        auto nIndices   = 0ull;
        auto nSubmeshes = 0ull;

        for (auto& Mesh : SkinGeom.m_Mesh)
        {
            nSubmeshes += Mesh.m_Submeshes.size();
            for (auto& Submesh : Mesh.m_Submeshes)
            {
                nVertices += Submesh.m_Vertices.size();
                nIndices  += Submesh.m_Indices.size();
            }
        }

        if (auto Err = Device.Create(m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(e16::skin_vertex), .m_EntryCount = static_cast<int>(nVertices) }); Err)
            return xgpu::getErrorInt(Err);

        (void)m_VertexBuffer.MemoryMap(0, static_cast<int>(nVertices), [&](void* pData)
        {
            auto pVertex = static_cast<e16::skin_vertex*>(pData);
            for (auto& Mesh    : SkinGeom.m_Mesh)
            for (auto& Submesh : Mesh.m_Submeshes)
            for (auto& Vert    : Submesh.m_Vertices)
            {
                *pVertex = Vert;
                pVertex++;
            }
        });

        if (auto Err = Device.Create(m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = static_cast<int>(nIndices) }); Err)
            return xgpu::getErrorInt(Err);

        (void)m_IndexBuffer.MemoryMap(0, static_cast<int>(nIndices), [&](void* pData)
        {
            auto pIndex = static_cast<std::uint32_t*>(pData);
            for (auto& Mesh    : SkinGeom.m_Mesh)
            for (auto& Submesh : Mesh.m_Submeshes)
            {
                for (auto Index : Submesh.m_Indices)
                {
                    *pIndex = Index;
                    pIndex++;
                }
            }
        });

        xgpu::pipeline PipeLine;
        {
            // Vertex descriptor
            xgpu::vertex_descriptor VertexDescriptor;
            {
                auto Attributes = std::array
                { xgpu::vertex_descriptor::attribute
                  { .m_Offset = offsetof(e16::skin_vertex, m_Position)
                  , .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D
                  }
                , xgpu::vertex_descriptor::attribute
                  { .m_Offset = offsetof(e16::skin_vertex, m_UV)
                  , .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
                  }
                , xgpu::vertex_descriptor::attribute
                  { .m_Offset = offsetof(e16::skin_vertex, m_Normal)
                  , .m_Format = xgpu::vertex_descriptor::format::SINT8_4D_NORMALIZED
                  }
                , xgpu::vertex_descriptor::attribute
                  { .m_Offset = offsetof(e16::skin_vertex, m_Tangent)
                  , .m_Format = xgpu::vertex_descriptor::format::SINT8_4D_NORMALIZED
                  }
                , xgpu::vertex_descriptor::attribute
                  { .m_Offset = offsetof(e16::skin_vertex, m_BoneWeights)
                  , .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED
                  }
                , xgpu::vertex_descriptor::attribute
                  { .m_Offset = offsetof(e16::skin_vertex, m_BoneIndex)
                  , .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_UINT
                  }
                };
                auto Setup = xgpu::vertex_descriptor::setup
                { .m_VertexSize = sizeof(e16::skin_vertex)
                , .m_Attributes = Attributes
                };

                if (auto Err = Device.Create(VertexDescriptor, Setup); Err)
                    return xgpu::getErrorInt(Err);
            }

            xgpu::shader MyFragmentShader;
            {
                auto RawData = xgpu::shader::setup::raw_data
                { std::array
                    {
                        #include "x64\e17_skingeom_frag.h"
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
                        #include "x64\e17_skingeom_vert.h"
                    }
                };

                if (auto Err = Device.Create(MyVertexShader, { .m_Type = xgpu::shader::type::bit::VERTEX, .m_Sharer = RawData }); Err)
                    return xgpu::getErrorInt(Err);
            }

            auto UBuffersUsage = std::array{ xgpu::shader::type{xgpu::shader::type::bit::VERTEX} };
            auto Shaders       = std::array<const xgpu::shader*, 2>{ &MyFragmentShader, &MyVertexShader };
            auto Samplers      = std::array
                                { xgpu::pipeline::sampler{} // [INPUT_TEXTURE_NORMAL]
                                , xgpu::pipeline::sampler{} // [INPUT_TEXTURE_DIFFUSE]
                                , xgpu::pipeline::sampler{} // [INPUT_TEXTURE_AO]
                                , xgpu::pipeline::sampler{} // [INPUT_TEXTURE_GLOSSINESS]
                                , xgpu::pipeline::sampler{} // [INPUT_TEXTURE_ROUGHNESS]
                                };
            auto Setup         = xgpu::pipeline::setup
            { .m_VertexDescriptor   = VertexDescriptor
            , .m_Shaders            = Shaders
            , .m_PushConstantsSize  = sizeof(push_constants)
            , .m_UniformBufferUsage = UBuffersUsage
            , .m_Samplers           = Samplers
            };

            if (auto Err = Device.Create(PipeLine, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        //
        // Create final structures for us
        //
        m_Submeshes.resize( nSubmeshes );
        m_Meshes.resize(SkinGeom.m_Mesh.size());
        {
            auto nVerts   = 0u;
            auto nIndices = 0u;
            for (auto iMesh = 0u, iSubmesh = 0u; iMesh < SkinGeom.m_Mesh.size(); ++iMesh)
            {
                m_Meshes[iMesh].m_Name       = SkinGeom.m_Mesh[iMesh].m_Name;
                m_Meshes[iMesh].m_iSubmesh   = static_cast<int>(iSubmesh);
                m_Meshes[iMesh].m_nSubmeshes = static_cast<int>( SkinGeom.m_Mesh[iMesh].m_Submeshes.size() );

                for (auto& Submesh : SkinGeom.m_Mesh[iMesh].m_Submeshes)
                {
                    m_Submeshes[iSubmesh].m_iIndex   = nIndices;
                    m_Submeshes[iSubmesh].m_nIndices = static_cast<std::uint32_t>(Submesh.m_Indices.size());
                    m_Submeshes[iSubmesh].m_iVertex  = nVerts;
                    m_Submeshes[iSubmesh].m_iMaterialInstance = Submesh.m_iMaterial;

                    iSubmesh ++;
                    nIndices += static_cast<std::uint32_t>(Submesh.m_Indices.size());
                    nVerts   += static_cast<std::uint32_t>(Submesh.m_Vertices.size());
                }
            }
        }

        //
        // Load all the textures
        //
        if( auto Err = m_LoadedTextures.Initialize( Device, SkinGeom.m_FileName, SkinGeom.m_TexturePaths, SkinGeom.m_MaterialInstance); Err )
            return Err;

        //
        // Setup the material instance
        //
        xgpu::buffer UBO;
        if (auto Err = Device.Create(UBO, { .m_Type = xgpu::buffer::type::UNIFORM, .m_Usage = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ, .m_EntryByteSize = sizeof(shader_uniform_buffer), .m_EntryCount = 2 }); Err)
            return xgpu::getErrorInt(Err);

        m_PipeLineInstance.resize( SkinGeom.m_MaterialInstance.size() );
        for (auto i = 0u; i < m_PipeLineInstance.size(); ++i)
        {
            auto iDiffuse       = SkinGeom.m_MaterialInstance[i].m_DiffuseSampler.m_iTexture;
            auto iNormal        = SkinGeom.m_MaterialInstance[i].m_NormalSampler.m_iTexture;
            auto Bindings       = std::array
                                { xgpu::pipeline_instance::sampler_binding{ iNormal  == -1 ? m_LoadedTextures.m_DefaultTexture : m_LoadedTextures.m_Textures[iNormal]  } // [INPUT_TEXTURE_NORMAL]
                                , xgpu::pipeline_instance::sampler_binding{ iDiffuse == -1 ? m_LoadedTextures.m_DefaultTexture : m_LoadedTextures.m_Textures[iDiffuse] } // [INPUT_TEXTURE_DIFFUSE]
                                , xgpu::pipeline_instance::sampler_binding{ iDiffuse == -1 ? m_LoadedTextures.m_DefaultTexture : m_LoadedTextures.m_Textures[iDiffuse] } // [INPUT_TEXTURE_AO]
                                , xgpu::pipeline_instance::sampler_binding{ iDiffuse == -1 ? m_LoadedTextures.m_DefaultTexture : m_LoadedTextures.m_Textures[iDiffuse] } // [INPUT_TEXTURE_GLOSSINESS]
                                , xgpu::pipeline_instance::sampler_binding{ iDiffuse == -1 ? m_LoadedTextures.m_DefaultTexture : m_LoadedTextures.m_Textures[iDiffuse] } // [INPUT_TEXTURE_ROUGHNESS]
                                };
            auto UniformBuffers = std::array{ xgpu::pipeline_instance::uniform_buffer{ UBO } };

            auto  Setup    = xgpu::pipeline_instance::setup
            { .m_PipeLine               = PipeLine
            , .m_UniformBuffersBindings = UniformBuffers
            , .m_SamplersBindings       = Bindings
            };

            if( auto Err = Device.Create(m_PipeLineInstance[i], Setup ); Err )
                return xgpu::getErrorInt(Err);
        }

        return 0;
    }

    template< typename T_CALLBACK>
    void Render( xgpu::cmd_buffer& CmdBuffer, T_CALLBACK&& Callback, std::uint64_t MeshMask = ~0ull )
    {
        if(!MeshMask) return;

        CmdBuffer.setBuffer(m_VertexBuffer);
        CmdBuffer.setBuffer(m_IndexBuffer);

        bool bComputedMatrices = false;
        for (int i = 0; i < m_Meshes.size(); ++i, MeshMask >>= 1 )
        {
            if((1 & MeshMask) == 0) continue;
            auto& Mesh = m_Meshes[i];
            for (auto j = 0u; j < Mesh.m_nSubmeshes; ++j)
            {
                auto& Submesh = m_Submeshes[Mesh.m_iSubmesh + j];
                CmdBuffer.setPipelineInstance(m_PipeLineInstance[Submesh.m_iMaterialInstance]);

                if (bComputedMatrices == false)
                {
                    bComputedMatrices = true;
                    auto& UBO = CmdBuffer.getUniformBufferVMem<shader_uniform_buffer>(xgpu::shader::type::bit::VERTEX);
                    Callback(UBO);
                    CmdBuffer.setPushConstants(m_PushConstants);
                }

                CmdBuffer.Draw( Submesh.m_nIndices, Submesh.m_iIndex, Submesh.m_iVertex );
            }
        }
    }

    push_constants                       m_PushConstants;
    e16::load_textures                   m_LoadedTextures;
    xgpu::buffer                         m_VertexBuffer;
    xgpu::buffer                         m_IndexBuffer;
    std::vector<xgpu::pipeline_instance> m_PipeLineInstance;
    std::vector<submesh>                 m_Submeshes;
    std::vector<mesh>                    m_Meshes;
};

//------------------------------------------------------------------------------------------------

int E17_Example()
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

    xgpu::window Window;
    if (auto Err = Device.Create(Window, {}); Err)
        return xgpu::getErrorInt(Err);

    //
    // Load the animated character
    //
    e16::anim_character AnimCharacter;
    {
        e16::importer Importer;
        if( Importer.Import(AnimCharacter
         //   , "./../../dependencies/Assets/Animated/ImperialWalker/source/AT-AT.fbx"
            , "./../../dependencies/Assets/Animated/walking-while-listening/source/Walking.fbx"
        ) ) exit(1);
    }

    //
    // Initialize the skin render
    //
    e17::skin_render SkinRender;
    SkinRender.Initialize( Device, AnimCharacter.m_SkinGeom );

    //
    // Compute the BBOX of the mesh
    //
    xcore::bbox MeshBBox;
    for( auto& M : AnimCharacter.m_SkinGeom.m_Mesh )
    for (auto& S : M.m_Submeshes)
    for (auto& V : S.m_Vertices)
    {
        MeshBBox.AddVerts( &V.m_Position, 1 );
    }

    //
    // Basic loop
    //
    xgpu::mouse    Mouse;
    xgpu::keyboard Keyboard;
    {
        Instance.Create(Mouse);
        Instance.Create(Keyboard);
    }

    xcore::vector3      FrozenLightPosition;
    xcore::radian3      Angles;
    float               Distance     = 2;
    bool                FollowCamera = true;
    auto                Clock        = std::chrono::system_clock::now();
    xcore::vector3      CameraTarget(0,0,0);
    xgpu::tools::view   View;

    View.setFov(60_xdeg);

    while (Instance.ProcessInputEvents())
    {
        const float DeltaTime = [&]
        {
            auto                         Now            = std::chrono::system_clock::now();
            std::chrono::duration<float> ElapsedSeconds = Now - Clock;
            Clock     = Now;
            return ElapsedSeconds.count();
        }();

        //
        // Input
        //
        if (Mouse.isPressed(xgpu::mouse::digital::BTN_RIGHT))
        {
            auto MousePos = Mouse.getValue(xgpu::mouse::analog::POS_REL);
            Angles.m_Pitch.m_Value -= 0.01f * MousePos[1];
            Angles.m_Yaw.m_Value -= 0.01f * MousePos[0];
        }

        if (Keyboard.wasPressed(xgpu::keyboard::digital::KEY_SPACE))
        {
            FrozenLightPosition = View.getPosition();
            FollowCamera = !FollowCamera;
        }

        if(Mouse.isPressed(xgpu::mouse::digital::BTN_MIDDLE))
        {
            auto MousePos = Mouse.getValue(xgpu::mouse::analog::POS_REL);
            CameraTarget -= View.getWorldYVector() * (0.005f * MousePos[1]);
            CameraTarget -= View.getWorldXVector() * (0.005f * MousePos[0]);
        }

        Distance += Distance * -0.2f * Mouse.getValue(xgpu::mouse::analog::WHEEL_REL)[0];
        if (Distance < 0.5f) 
        {
            CameraTarget += View.getWorldZVector() * (0.5f * (0.5f - Distance));
            Distance = 0.5f;
        }

        // Update the camera
        View.LookAt(Distance, Angles, CameraTarget );

        // Update light position
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
            auto        CmdBuffer     = Window.getCmdBuffer();
            const auto  W2C           = View.getW2C();
            const float MeshScale     = 1.0f/MeshBBox.getRadius();

            AnimCharacter.m_AnimPlayer.Update(DeltaTime);

            SkinRender.m_PushConstants.m_AmbientLightColor.setup(0.1f, 0.1f, 0.1f, 1.0f);
            SkinRender.m_PushConstants.m_LightColor.setup(1.0f, 1.0f, 1.0f, 1.0f);
            SkinRender.m_PushConstants.m_WorldSpaceEyePos   = View.getPosition();
            SkinRender.m_PushConstants.m_WorldSpaceLightPos = LightPosition;

            // set the gamma here
            SkinRender.m_PushConstants.m_WorldSpaceLightPos.m_W = 2.2f;

            // animate the character
            SkinRender.Render( CmdBuffer, [&]( e17::skin_render::shader_uniform_buffer& UBO )
            {
                UBO.m_W2C = W2C;

                xcore::matrix4 L2W(xcore::matrix4::identity());

                L2W.setScale(MeshScale);
                L2W.setTranslation({0,-1,0});
                
                AnimCharacter.m_AnimPlayer.ComputeMatrices(UBO.m_L2W, L2W);
            });
        }

        // Swap the buffers
        Window.PageFlip();
    }

    return 0;
}

