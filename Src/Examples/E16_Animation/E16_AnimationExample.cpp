#include "xGPU.h"
#include <iostream>

#include "E16_Importer.h"
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

struct draw_vert
{
    xcore::vector3d m_Position;
    xcore::vector2  m_TexCoord;
    xcore::icolor   m_Color;
};

//------------------------------------------------------------------------------------------------

namespace e16
{
    struct skin_render;
    struct debug_bone;
}

//------------------------------------------------------------------------------------------------

struct e16::debug_bone
{
    int Initialize( xgpu::device& Device )
    {
        auto Mesh = xprim_geom::cube::Generate( 1, 1, 1, 1, xprim_geom::float3{ 1,1,1 } );

        if (auto Err = Device.Create(m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(draw_vert), .m_EntryCount = static_cast<int>(Mesh.m_Vertices.size()) }); Err)
            return xgpu::getErrorInt(Err);

        (void)m_VertexBuffer.MemoryMap(0, static_cast<int>(Mesh.m_Vertices.size()), [&](void* pData)
        {
            auto pVertex = static_cast<draw_vert*>(pData);
            for (int i = 0; i < static_cast<int>(Mesh.m_Vertices.size()); ++i)
            {
                const auto& v     = Mesh.m_Vertices[i];
                auto&       V     = pVertex[i];
                const auto  ScaleXZ = v.m_Position.m_Y < 0 ? 1.5f : v.m_Position.m_Y > 0 ? 0.2f : 1.0f;
                const auto  ScaleY  = v.m_Position.m_Y > 0 ? 3.5f : 1.0f;

                V.m_Position.setup(v.m_Position.m_X* ScaleXZ, v.m_Position.m_Y* ScaleY, v.m_Position.m_Z * ScaleXZ );
                V.m_TexCoord.setup(v.m_Texcoord.m_X, v.m_Texcoord.m_Y);
                V.m_Color = xcore::icolor{ 0xffffffffu };
            }
        });

        if (auto Err = Device.Create(m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = static_cast<int>(Mesh.m_Indices.size()) }); Err)
            return xgpu::getErrorInt(Err);

        (void)m_IndexBuffer.MemoryMap(0, static_cast<int>(Mesh.m_Indices.size()), [&](void* pData)
        {
            auto pIndex = static_cast<std::uint32_t*>(pData);
            for (int i = 0; i < static_cast<int>(Mesh.m_Indices.size()); ++i)
            {
                pIndex[i] = Mesh.m_Indices[i];
            }
        });

        // Vertex descriptor
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
                .m_Offset = offsetof(draw_vert, m_Color)
            ,   .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED
            }
        };
        auto Setup = xgpu::vertex_descriptor::setup
        {
            .m_VertexSize = sizeof(draw_vert)
        ,   .m_Attributes = Attributes
        };

        xgpu::vertex_descriptor VertexDescriptor;
        if (auto Err = Device.Create(VertexDescriptor, Setup); Err)
            return xgpu::getErrorInt(Err);

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
        {
            xgpu::texture Texture;
            if (auto Err = xgpu::tools::bitmap::Create(Texture, Device, xcore::bitmap::getDefaultBitmap()); Err)
                return xgpu::getErrorInt(Err);

            auto  Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{ Texture } };
            auto  Setup    = xgpu::pipeline_instance::setup
            {   .m_PipeLine         = PipeLine
            ,   .m_SamplersBindings = Bindings
            };

            if( auto Err = Device.Create(m_PipeLineInstance, Setup ); Err )
                return xgpu::getErrorInt(Err);
        }

        return 0;
    }
    void Render(xgpu::cmd_buffer& CmdBuffer, const xcore::matrix4& L2C )
    {
        CmdBuffer.setPipelineInstance(m_PipeLineInstance);
        CmdBuffer.setBuffer(m_VertexBuffer);
        CmdBuffer.setBuffer(m_IndexBuffer);
        CmdBuffer.setPushConstants(L2C);
        CmdBuffer.Draw(m_IndexBuffer.getEntryCount());
    }

    xgpu::buffer            m_VertexBuffer;
    xgpu::buffer            m_IndexBuffer;
    xgpu::pipeline_instance m_PipeLineInstance;
};

//------------------------------------------------------------------------------------------------

struct e16::skin_render
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

    struct alignas(256) shader_uniform_buffer
    {
        xcore::matrix4                  m_W2C;
        std::array<xcore::matrix4,256>  m_L2W;
        xcore::vector4                  m_LightPosWorld;
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
            for (auto& Mesh : SkinGeom.m_Mesh)
            for (auto& Submesh : Mesh.m_Submeshes)
            for (auto& Vert : Submesh.m_Vertices)
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
            for (auto& Mesh : SkinGeom.m_Mesh)
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
                        #include "x64\e16_skingeom_vert.h"
                    }
                };
                xgpu::shader::setup Setup
                { .m_Type   = xgpu::shader::type::bit::VERTEX
                , .m_Sharer = RawData
                };

                if (auto Err = Device.Create(MyVertexShader, Setup); Err)
                    return xgpu::getErrorInt(Err);
            }

            auto UBuffersUsage = std::array{ xgpu::shader::type{xgpu::shader::type::bit::VERTEX} };
            auto Shaders  = std::array<const xgpu::shader*, 2>{ &MyFragmentShader, &MyVertexShader };
            auto Samplers = std::array{ xgpu::pipeline::sampler{} };
            auto Setup    = xgpu::pipeline::setup
            { .m_VertexDescriptor   = VertexDescriptor
            , .m_Shaders            = Shaders
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
        if (auto Err = m_LoadedTextures.Initialize(Device, SkinGeom.m_FileName, SkinGeom.m_TexturePaths, SkinGeom.m_MaterialInstance ); Err)
            return Err;


        //
        // Setup the material instance
        //
        xgpu::buffer UBO;
        if (auto Err = Device.Create(UBO, { .m_Type = xgpu::buffer::type::UNIFORM, .m_Usage = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ, .m_EntryByteSize = sizeof(shader_uniform_buffer), .m_EntryCount = 10 }); Err)
            return xgpu::getErrorInt(Err);

        m_PipeLineInstance.resize( SkinGeom.m_MaterialInstance.size() );
        for (auto i = 0u; i < m_PipeLineInstance.size(); ++i)
        {
            auto iTexture       = SkinGeom.m_MaterialInstance[i].m_DiffuseSampler.m_iTexture;
            auto Bindings       = std::array{ xgpu::pipeline_instance::sampler_binding{ iTexture == -1 ? m_LoadedTextures.m_DefaultTexture : m_LoadedTextures.m_Textures[iTexture] } };
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
                }
                CmdBuffer.Draw( Submesh.m_nIndices, Submesh.m_iIndex, Submesh.m_iVertex );
            }
        }
    }

    xgpu::buffer                         m_VertexBuffer;
    xgpu::buffer                         m_IndexBuffer;
    std::vector<xgpu::pipeline_instance> m_PipeLineInstance;
    std::vector<submesh>                 m_Submeshes;
    std::vector<mesh>                    m_Meshes;
    e16::load_textures                   m_LoadedTextures;
};

//------------------------------------------------------------------------------------------------

int E16_Example()
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
    // Initialize a bone
    // 
    e16::debug_bone DebugBone;
    if( auto err = DebugBone.Initialize(Device); err )
        return err;

    //
    // Load the animated character
    //
    e16::anim_character AnimCharacter;
    {
        e16::importer Importer;
        if( Importer.Import(AnimCharacter
        // , "./../../dependencies/Assets/Animated/ImperialWalker/source/AT-AT.fbx"
         , "./../../dependencies/Assets/Animated/catwalk/scene.gltf"
        // , "./../../dependencies/Assets/Animated/supersoldier/source/Idle.fbx"
        // , "./../../dependencies/Assets/Animated/Sonic/source/chr_classicsonic.fbx"
        // , "./../../dependencies/Assets/Animated/Starwars/source/Catwalk Walk Forward.fbx" 
        // , "./../../dependencies/Assets/Animated/walking-while-listening/source/Walking.fbx"
        // , "./../../dependencies/xgeom_compiler/dependencies/xraw3D/dependencies/assimp/test/models/FBX/huesitos.fbx"
        ) ) exit(1);
    }

    //
    // Initialize the skin render
    //
    e16::skin_render SkinRender;
    SkinRender.Initialize( Device, AnimCharacter.m_SkinGeom );

    //
    // Compute the BBOX of the mesh
    //
    xcore::bbox MeshBBox;
    for( auto& M : AnimCharacter.m_SkinGeom.m_Mesh )
    for (auto& S : M.m_Submeshes)
    for (auto& V : S.m_Vertices)
    {
        auto p = AnimCharacter.m_Skeleton.m_Bones[V.m_BoneIndex.m_A].m_NeutalPose * V.m_Position;
        MeshBBox.AddVerts( &p, 1 );
    }

    //
    // Mesh Scale
    //
    const float MeshScale = 1.5f / MeshBBox.getSize().m_Y;

    //
    // Get Average Bone Length
    //
    std::vector<xcore::matrix4> FinalL2W(AnimCharacter.m_Skeleton.m_Bones.size());
    const float ScaleBones = [&]
    {
        float TotalL=0;
        AnimCharacter.m_AnimPlayer.ComputeMatrices(FinalL2W, xcore::matrix4::identity());
        for( auto i=0u; i<AnimCharacter.m_Skeleton.m_Bones.size(); ++i )
        {
            if (AnimCharacter.m_Skeleton.m_Bones[i].m_iParent != -1)
            {
                auto M1 = AnimCharacter.m_Skeleton.m_Bones[i].m_InvBind;
                auto M2 = AnimCharacter.m_Skeleton.m_Bones[AnimCharacter.m_Skeleton.m_Bones[i].m_iParent].m_InvBind;

                M1.FullInvert();
                M2.FullInvert();

                M1 = FinalL2W[i] * M1;
                M2 = FinalL2W[AnimCharacter.m_Skeleton.m_Bones[i].m_iParent]* M2;

                M1.Scale(MeshScale);
                M2.Scale(MeshScale);

                auto V = M1.getTranslation() - M2.getTranslation();
                M1.FullInvert();
                TotalL +=  ( M1 * V ).getLength();
            }
        }

        float a = (TotalL / AnimCharacter.m_Skeleton.m_Bones.size());
        
        return a * 0.05f;
    }();

    //
    // Basic loop
    //
    xgpu::tools::view View;

    View.setFov(60_xdeg);

    xgpu::mouse Mouse;
    xgpu::keyboard Keyboard;
    {
        Instance.Create(Mouse);
        Instance.Create(Keyboard);
    }

    xcore::radian3 Angles;
    float          Distance     = 2;
    auto           Clock        = std::chrono::system_clock::now();
    xcore::vector3 CameraTarget(0,0,0);
    while (Instance.ProcessInputEvents())
    {
        float DeltaTime;
        {
            auto                         Now            = std::chrono::system_clock::now();
            std::chrono::duration<float> ElapsedSeconds = Now - Clock;
            DeltaTime = ElapsedSeconds.count();
            Clock     = Now;
        }        

        //
        // Input
        //
        if (Mouse.isPressed(xgpu::mouse::digital::BTN_RIGHT))
        {
            auto MousePos = Mouse.getValue(xgpu::mouse::analog::POS_REL);
            Angles.m_Pitch.m_Value -= 0.01f * MousePos[1];
            Angles.m_Yaw.m_Value -= 0.01f * MousePos[0];
        }

        if (Mouse.isPressed(xgpu::mouse::digital::BTN_MIDDLE))
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
        View.LookAt(Distance, Angles, CameraTarget);

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

            AnimCharacter.m_AnimPlayer.Update(DeltaTime);
            AnimCharacter.m_AnimPlayer.ComputeMatrices( FinalL2W, xcore::matrix4::identity() );

            // Render the skeleton animated pose 
            {
                int i = 0;
                for (const auto& m : FinalL2W)
                {
                    auto M = AnimCharacter.m_Skeleton.m_Bones[i++].m_InvBind;
                    M.FullInvert();             // The debug bones are already in local space we need them to go to bone space so we need to have the inverse matrix
                    M.PreScale(ScaleBones);
                    M = m * M;
                    M.Scale(MeshScale);
                    M.Translate({ 0,-1.0f, -2 });

                    DebugBone.Render(CmdBuffer, W2C * M);
                }
            }

            // Render bind skeleton
            for( const auto& B : AnimCharacter.m_Skeleton.m_Bones )
            {
                auto M = B.m_InvBind;
                M.FullInvert();             // The debug bones are already in local space we need them to go to bone space so we need to have the inverse matrix
                M.PreScale(ScaleBones);
                M.Scale(MeshScale);
                M.Translate( {-2,-1.0f,-2} );

                DebugBone.Render(CmdBuffer, W2C * M );
            }

            // Render the skeleton neutral pose 
            for (const auto& B : AnimCharacter.m_Skeleton.m_Bones)
            {
                auto BInv = B.m_InvBind;
                auto M    = B.m_NeutalPose * BInv.FullInvert();
                M.PreScale(ScaleBones);
                M.Scale(MeshScale);
                M.Translate({ 2,-1.0f, -2 });

                DebugBone.Render(CmdBuffer, W2C * M);
            }

            // Check the skin neutral pose
            SkinRender.Render( CmdBuffer, [&]( e16::skin_render::shader_uniform_buffer& UBO )
            {
                UBO.m_W2C = W2C;
                UBO.m_W2C.PreTranslate({2,-1,0});
                UBO.m_W2C.PreScale(MeshScale);
                for( auto i=0u;i< FinalL2W.size(); ++i )
                {
                    UBO.m_L2W[i] = AnimCharacter.m_Skeleton.m_Bones[i].m_NeutalPose;
                }
            });

            // Check the skin bind pose
            SkinRender.Render( CmdBuffer, [&]( e16::skin_render::shader_uniform_buffer& UBO )
            {
                UBO.m_W2C = W2C;
                UBO.m_W2C.PreTranslate({-2,-1,0});
                UBO.m_W2C.PreScale(MeshScale);
                for( auto i=0u;i< FinalL2W.size(); ++i ) UBO.m_L2W[i] = xcore::matrix4::identity();
            });

            // animate the character
            SkinRender.Render( CmdBuffer, [&]( e16::skin_render::shader_uniform_buffer& UBO )
            {
                UBO.m_W2C = W2C;
                UBO.m_W2C.PreTranslate({0,-1,0});
                UBO.m_W2C.PreScale(MeshScale);
                for( auto i=0u;i< FinalL2W.size(); ++i ) UBO.m_L2W[i] = FinalL2W[i];
            });
        }

        // Swap the buffers
        Window.PageFlip();
    }

    return 0;
}

