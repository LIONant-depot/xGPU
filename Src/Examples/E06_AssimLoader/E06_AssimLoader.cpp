#include "xcore.h"
#include "xGPU.h"
#include "../../tools/xgpu_view.h"
#include "../../tools/xgpu_view_inline.h"
#include "../../tools/Import3D/import3d.h"

#include "E06_LoadTextureHelper.h"

namespace e06
{
    //------------------------------------------------------------------------------------------------
    static
    void DebugMessage(std::string_view View)
    {
        printf("%s\n", View.data());
    }

    //------------------------------------------------------------------------------------------------

    struct push_contants
    {
        xcore::matrix4 m_L2C;
    };
}

//------------------------------------------------------------------------------------------------

struct static_geom
{
    struct vertex
    {
        xcore::vector3 m_Position;
        xcore::vector2 m_Texcoord;
        xcore::icolor  m_Color;
    };

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

    xgpu::buffer                            m_VertexBuffer;
    xgpu::buffer                            m_IndexBuffer;
    std::vector<xgpu::pipeline_instance>    m_PipeLineInstance;
    e06::load_textures                      m_LoadedTextures;
    std::vector<submesh>                    m_Submeshes;
    std::vector<mesh>                       m_Meshes;

    bool Initialize( xgpu::device& Device, import3d::geom& Geom )
    {
        auto nVertices  = 0ull;
        auto nIndices   = 0ull;
        auto nSubmeshes = 0ull;

        for (auto& Mesh : Geom.m_Mesh)
        {
            nSubmeshes += Mesh.m_Submeshes.size();
            for (auto& Submesh : Mesh.m_Submeshes)
            {
                nVertices += Submesh.m_Vertices.size();
                nIndices  += Submesh.m_Indices.size();
            }
        }

        if (auto Err = Device.Create(m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(vertex), .m_EntryCount = static_cast<int>(nVertices) }); Err)
            return xgpu::getErrorInt(Err);

        (void)m_VertexBuffer.MemoryMap(0, static_cast<int>(nVertices), [&](void* pData)
        {
            auto pVertex = static_cast<vertex*>(pData);
            for (auto& Mesh     : Geom.m_Mesh)
            for (auto& Submesh  : Mesh.m_Submeshes)
            for (auto& Vert     : Submesh.m_Vertices)
            {
                pVertex->m_Position = Vert.m_Position;
                pVertex->m_Texcoord = Vert.m_UV;
                //pVertex->m_Color    = Vert.m;
                pVertex->m_Color = xcore::icolor(0xffffffff);
                pVertex++;
            }
        });

        if (auto Err = Device.Create(m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = static_cast<int>(nIndices) }); Err)
            return xgpu::getErrorInt(Err);

        (void)m_IndexBuffer.MemoryMap(0, static_cast<int>(nIndices), [&](void* pData)
        {
            auto pIndex = static_cast<std::uint32_t*>(pData);
            for (auto& Mesh     : Geom.m_Mesh)
            for (auto& Submesh  : Mesh.m_Submeshes)
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
                  { .m_Offset = offsetof(vertex, m_Position)
                  , .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D
                  }
                , xgpu::vertex_descriptor::attribute
                  { .m_Offset = offsetof(vertex, m_Texcoord)
                  , .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
                  }
                , xgpu::vertex_descriptor::attribute
                  { .m_Offset = offsetof(vertex, m_Color)
                  , .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED
                  }
                };
                auto Setup = xgpu::vertex_descriptor::setup
                { .m_VertexSize = sizeof(vertex)
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
                        #include "x64\draw_vert.h"
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

            auto Shaders  = std::array<const xgpu::shader*, 2>{ &MyFragmentShader, &MyVertexShader };
            auto Samplers = std::array{ xgpu::pipeline::sampler{} };
            auto Setup    = xgpu::pipeline::setup
            { .m_VertexDescriptor   = VertexDescriptor
            , .m_Shaders            = Shaders
            , .m_PushConstantsSize  = sizeof(xcore::matrix4)
            , .m_Samplers           = Samplers
            };

            if (auto Err = Device.Create(PipeLine, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        //
        // Create final structures for us
        //
        m_Submeshes.resize( nSubmeshes );
        m_Meshes.resize(Geom.m_Mesh.size());
        {
            auto nVerts   = 0u;
            auto nIndices = 0u;
            for (auto iMesh = 0u, iSubmesh = 0u; iMesh < Geom.m_Mesh.size(); ++iMesh)
            {
                m_Meshes[iMesh].m_Name       = Geom.m_Mesh[iMesh].m_Name;
                m_Meshes[iMesh].m_iSubmesh   = static_cast<int>(iSubmesh);
                m_Meshes[iMesh].m_nSubmeshes = static_cast<int>(Geom.m_Mesh[iMesh].m_Submeshes.size() );

                for (auto& Submesh : Geom.m_Mesh[iMesh].m_Submeshes)
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
        if (auto Err = m_LoadedTextures.Initialize(Device, Geom.m_FileName, Geom.m_TexturePaths, Geom.m_MaterialInstance ); Err)
            return Err;

        //
        // Setup the material instance
        //

        m_PipeLineInstance.resize(Geom.m_MaterialInstance.size() );
        for (auto i = 0u; i < m_PipeLineInstance.size(); ++i)
        {
            auto iTexture       = Geom.m_MaterialInstance[i].m_DiffuseSampler.m_iTexture;
            auto Bindings       = std::array{ xgpu::pipeline_instance::sampler_binding{ iTexture == -1 ? m_LoadedTextures.m_DefaultTexture : m_LoadedTextures.m_Textures[iTexture] } };

            auto  Setup    = xgpu::pipeline_instance::setup
            { .m_PipeLine               = PipeLine
            , .m_SamplersBindings       = Bindings
            };

            if( auto Err = Device.Create(m_PipeLineInstance[i], Setup ); Err )
                return xgpu::getErrorInt(Err);
        }

        return 0;
    }

    //------------------------------------------------------------------------------------------------

    void Render(xgpu::cmd_buffer& CmdBuffer, const xcore::matrix4& L2C, std::uint64_t MeshMask = ~0ull)
    {
        CmdBuffer.setBuffer(m_VertexBuffer);
        CmdBuffer.setBuffer(m_IndexBuffer);

        for (int i = 0; i < m_Meshes.size(); ++i, MeshMask >>= 1)
        {
            if ((1 & MeshMask) == 0) continue;
            auto& Mesh = m_Meshes[i];

            for (auto j = 0u; j < Mesh.m_nSubmeshes; ++j)
            {
                auto& Submesh = m_Submeshes[Mesh.m_iSubmesh + j];
                CmdBuffer.setPipelineInstance(m_PipeLineInstance[Submesh.m_iMaterialInstance]);
                CmdBuffer.setPushConstants(L2C);
                CmdBuffer.Draw(Submesh.m_nIndices, Submesh.m_iIndex, Submesh.m_iVertex);
            }
        }
    }
};


//------------------------------------------------------------------------------------------------

int E06_Example()
{
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = true, .m_pLogErrorFunc = e06::DebugMessage, .m_pLogWarning = e06::DebugMessage }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err)
        return xgpu::getErrorInt(Err);

    xgpu::window MainWindow;
    if (auto Err = Device.Create(MainWindow, {}); Err)
        return xgpu::getErrorInt(Err);

    //
    // Load the Mesh
    //
    static_geom StaticGeom;
    {
        import3d::importer Importer;
        import3d::geom     StaticRawGeom;

        if (auto Err = Importer.Import( "../../Assets/demon-skull-textured/source/Skull_textured.fbx", &StaticRawGeom, nullptr, nullptr ); Err)
        {
            assert(false);
            return -1;
        }

        if( StaticGeom.Initialize( Device, StaticRawGeom ) )
        {
            assert(false);
            return -1;
        }
    }

    //
    // Main loop
    //
    xgpu::tools::view View;

    xgpu::mouse Mouse;
    {
        Instance.Create(Mouse, {});
    }

    xcore::radian3 Angles
    { xcore::radian{ -0.230000168f }
    , xcore::radian{ -1.40999949f  }
    , xcore::radian{ 0.0f }
    };
    float          Distance = 122;
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
        if (MainWindow.BeginRendering())
            continue;

        // Update the view with latest window size
        View.setViewport({ 0, 0, MainWindow.getWidth(), MainWindow.getHeight() });


        // Render the static geom
        {
            auto CmdBuffer = MainWindow.getCmdBuffer();
            xcore::matrix4 L2W, W2C;
            L2W.setIdentity();
            L2W.setScale({ 0.4f });

            W2C = View.getW2C() * L2W;
            StaticGeom.Render(CmdBuffer, W2C);
        }

        //
        // Pageflip the windows
        //
        MainWindow.PageFlip();
    }

    return 0;
}