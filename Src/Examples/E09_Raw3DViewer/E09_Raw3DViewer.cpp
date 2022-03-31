
#include "xGPU.h"
#include "xcore.h"

#include "../../tools/xgpu_view.h"
#include "../../tools/xgpu_view_inline.h"
#include "../../tools/xgpu_xcore_bitmap_helpers.h"
#include "../../dependencies/xgeom_compiler/dependencies/xraw3D/src/xraw3d.h"
#include "../../tools/xgpu_imgui_breach.h"
#include "../../tools/WindowsFileDialog/FileBrowser.h"
#include "../../dependencies/xgeom_compiler/dependencies/xraw3D/dependencies/xcore/dependencies/properties/src/Examples/ImGuiExample/ImGuiPropertyInspector.h"
#include "../../dependencies/xgeom_compiler/dependencies/meshoptimizer/src/meshoptimizer.h"
#include "../../dependencies/xgeom_compiler/src_runtime/xgeom.h"

//------------------------------------------------------------------------------------------------

struct push_constants
{
    xcore::matrix4 m_L2C;
};

//------------------------------------------------------------------------------------------------
static
void DebugMessage(std::string_view View)
{
    printf("%s\n", View.data());
}

//------------------------------------------------------------------------------------------------

struct runtime_geom
{
    auto getVertexVectorFormat( xgeom::stream_info& StreamInfo ) const noexcept
    {
        static_assert((int)xgpu::vertex_descriptor::format::FLOAT_1D             == (int)xgeom::stream_info::format::FLOAT_1D);
        static_assert((int)xgpu::vertex_descriptor::format::FLOAT_2D             == (int)xgeom::stream_info::format::FLOAT_2D);
        static_assert((int)xgpu::vertex_descriptor::format::FLOAT_3D             == (int)xgeom::stream_info::format::FLOAT_3D);
        static_assert((int)xgpu::vertex_descriptor::format::FLOAT_4D             == (int)xgeom::stream_info::format::FLOAT_4D);
        static_assert((int)xgpu::vertex_descriptor::format::UINT8_1D_NORMALIZED  == (int)xgeom::stream_info::format::UINT8_1D_NORMALIZED);
        static_assert((int)xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED  == (int)xgeom::stream_info::format::UINT8_4D_NORMALIZED);
        static_assert((int)xgpu::vertex_descriptor::format::UINT8_1D             == (int)xgeom::stream_info::format::UINT8_1D);
        static_assert((int)xgpu::vertex_descriptor::format::UINT16_1D            == (int)xgeom::stream_info::format::UINT16_1D);
        static_assert((int)xgpu::vertex_descriptor::format::UINT32_1D            == (int)xgeom::stream_info::format::UINT32_1D);
        static_assert((int)xgpu::vertex_descriptor::format::SINT8_3D_NORMALIZED  == (int)xgeom::stream_info::format::SINT8_3D_NORMALIZED);
        static_assert((int)xgpu::vertex_descriptor::format::SINT8_4D_NORMALIZED  == (int)xgeom::stream_info::format::SINT8_4D_NORMALIZED);
        static_assert((int)xgpu::vertex_descriptor::format::ENUM_COUNT           == (int)xgeom::stream_info::format::ENUM_COUNT);

        return xgpu::vertex_descriptor::format(StreamInfo.m_Format);
    }

    xgpu::device::error* InitializeRuntime( xgpu::device& Device )
    {
        //
        // Deal first with the index
        //
        {
            assert( m_pGeom->m_StreamInfo[0].m_iStream == 0 );
            assert(m_pGeom->m_StreamInfo[0].m_ElementsType.m_bIndex );

            auto& StreamInfo = m_pGeom->m_StreamInfo[0];

            xgpu::buffer::setup Setup
            { .m_Type           = xgpu::buffer::type::INDEX
            , .m_Usage          = xgpu::buffer::setup::usage::GPU_READ
            , .m_EntryByteSize  = int(StreamInfo.getVectorElementSize())
            , .m_EntryCount     = int(m_pGeom->m_nIndices)
            , .m_pData          = m_pGeom->getStreamData(m_pGeom->m_StreamInfo[0].m_iStream)
            };
            if( auto Err = Device.Create(m_Buffers[0], Setup); Err ) return Err;
        }

        //
        // Vertex attributes
        //
        {
            std::array<xgpu::vertex_descriptor::attribute, xgeom::max_stream_count_v> Attributes {};
            int nAttributes = 0;
            for (int i = 1; i < m_pGeom->m_nStreamInfos; ++i)
            {
                auto&       StreamInfo  = m_pGeom->m_StreamInfo[i];
                const auto  ElementSize = StreamInfo.getVectorElementSize();

                for( int j=0; j<StreamInfo.m_VectorCount; ++j )
                {
                    auto& Attr       = Attributes[nAttributes++];

                    Attr.m_Format  = getVertexVectorFormat(StreamInfo);
                    Attr.m_Offset  = StreamInfo.m_Offset + j * ElementSize;
                    Attr.m_iStream = StreamInfo.m_iStream - 1;  // Convert to zero base stream since it only cares about vertices
                }
            }

            //
            // Create vertex descriptor
            // TODO: May be we should factor out the vertex descriptors???
            const xgpu::vertex_descriptor::setup Setup
            { .m_bUseStreaming  = m_pGeom->m_CompactedVertexSize == 0
            , .m_Topology       = xgpu::vertex_descriptor::topology::TRIANGLE_LIST
            , .m_VertexSize     = m_pGeom->m_CompactedVertexSize
            , .m_Attributes     = std::span{ Attributes.data(), static_cast<std::size_t>(nAttributes) }
            };

            if (auto Err = Device.Create(m_VertexDescriptor, Setup); Err) return Err;
        }

        //
        // Create Vertex Buffers
        //
        for (int i = 1; i < m_pGeom->m_nStreamInfos; ++i)
        {
            auto& StreamInfo = m_pGeom->m_StreamInfo[i];
            if (StreamInfo.m_Offset != 0) continue;

            const xgpu::buffer::setup BufferSetup
            { .m_Type           = xgpu::buffer::type::VERTEX
            , .m_Usage          = xgpu::buffer::setup::usage::GPU_READ
            , .m_EntryByteSize  = m_pGeom->getVertexSize(i)
            , .m_EntryCount     = int(m_pGeom->m_nVertices)
            , .m_pData          = m_pGeom->getStreamData(StreamInfo.m_iStream)
            };

            if( auto Err = Device.Create(m_Buffers[i], BufferSetup ); Err ) return Err;
        }

        return nullptr;
    }


    bool Load( xgpu::device& Device, xcore::string::view<const wchar_t> FileName ) noexcept
    {
        xcore::serializer::stream Steam;

        if (auto Err = Steam.Load(FileName, m_pGeom); Err)
        {
            DebugMessage(Err.getCode().m_pString);
            return true;
        }

        if( auto Err = InitializeRuntime(Device); Err )
        {
            DebugMessage( xgpu::getErrorMsg(Err) );
            return true;
        }

        return false;
    }

    xgeom*                                                      m_pGeom = nullptr;
    xgpu::vertex_descriptor                                     m_VertexDescriptor;
    std::array<xgpu::buffer, xgeom::max_stream_count_v>         m_Buffers;
};

//------------------------------------------------------------------------------------------------

struct runtime_material
{
    xgpu::pipeline m_Pipeline;

    xgpu::device::error* Initialize(xgpu::device& Device, xgpu::vertex_descriptor& VertexDescriptor)
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
                return Err;
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
                return Err;
        }

        auto Shaders = std::array<const xgpu::shader*, 2>{ &MyFragmentShader, & MyVertexShader };
        auto Samplers = std::array{ xgpu::pipeline::sampler{} };
        auto Setup = xgpu::pipeline::setup
        {
            .m_VertexDescriptor     = VertexDescriptor
        ,   .m_Shaders              = Shaders
        ,   .m_PushConstantsSize    = sizeof(push_constants)
        ,   .m_Samplers             = Samplers
        };

        if (auto Err = Device.Create(m_Pipeline, Setup); Err)
            return Err;


        return nullptr;
    }
};

//------------------------------------------------------------------------------------------------

struct runtime_texture
{
    xgpu::texture m_Texture;

    xgpu::device::error* Initialize(xgpu::device& Device)
    {
        if (auto Err = xgpu::tools::bitmap::Create(m_Texture, Device, xcore::bitmap::getDefaultBitmap()); Err)
            return Err;

        return nullptr;
    }
};

//------------------------------------------------------------------------------------------------

struct runtime_material_instance
{
    runtime_material*               m_pMaterial;
    xgpu::pipeline_instance         m_PipelineInstance;
    std::vector<runtime_texture*>   m_Textures;

    xgpu::device::error* Initialize(runtime_material& Material, xgpu::device& Device)
    {
        auto Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{ m_Textures[0]->m_Texture } };
        auto Setup = xgpu::pipeline_instance::setup
        { .m_PipeLine = Material.m_Pipeline
        , .m_SamplersBindings = Bindings
        };

        if (auto Err = Device.Create(m_PipelineInstance, Setup); Err)
            return Err;

        return nullptr;
    }

    void Activate(xgpu::cmd_buffer& CmdBuffer, const xcore::matrix4& L2C)
    {
        CmdBuffer.setPipelineInstance(m_PipelineInstance);

        push_constants PushConstants;
        PushConstants.m_L2C = L2C;
        CmdBuffer.setConstants( 0, &PushConstants,  sizeof(push_constants) );
    }
};

//------------------------------------------------------------------------------------------------

struct runtime_geom_instance
{
    runtime_geom*                           m_pRuntimeGeom      = nullptr;
    std::vector<runtime_material_instance*> m_MaterialInstances;
    std::uint64_t                           m_MeshMask          = ~0ull;
    std::uint64_t                           m_BufferMask        = ~0ull;

    void Render(xgpu::cmd_buffer& CmdBuffer, const xcore::matrix4& L2C)
    {
        for (int i = 0, end_i = m_pRuntimeGeom->m_pGeom->m_nMeshes; i < end_i; ++i)
        {
            if (!((m_MeshMask >> i) & 1)) continue;

            auto& Mesh = m_pRuntimeGeom->m_pGeom->m_pMesh[i];

            //for (int l = 0, end_l = Mesh.m_nLODs; l < end_l; ++l)
            int l = 0;
            {
                auto& LOD = m_pRuntimeGeom->m_pGeom->m_pLOD[Mesh.m_iLOD + l];
                if (l != 0) break;

                for (int s = 0, end_s = LOD.m_nSubmesh; s < end_s; ++s)
                {
                    auto& SubMesh = m_pRuntimeGeom->m_pGeom->m_pSubMesh[LOD.m_iSubmesh + s];

                    m_MaterialInstances[SubMesh.m_iMaterial]->Activate(CmdBuffer, L2C);

                    CmdBuffer.setStreamingBuffers({ m_pRuntimeGeom->m_Buffers.data(), m_pRuntimeGeom->m_pGeom->m_nStreams });

                    CmdBuffer.Draw(SubMesh.m_nIndices, SubMesh.m_iIndex);
                }
            }
        }
    }
};

//------------------------------------------------------------------------------------------------

int E09_Example()
{
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = true, .m_pLogErrorFunc = DebugMessage, .m_pLogWarning = DebugMessage }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err)
        return xgpu::getErrorInt(Err);

    xgpu::window MainWindow;
    if (auto Err = Device.Create(MainWindow, {}); Err)
        return xgpu::getErrorInt(Err);

    runtime_geom                RuntimeGeom;
    runtime_texture             DefaultTexture;
    runtime_material            DefaultMaterial;
    runtime_material_instance   DefaultMaterialInstance;
    runtime_geom_instance       GeomInstance;

    if( auto Err = RuntimeGeom.Load( Device, xcore::string::view{ L"spider.xgeom"} ); Err )
        return -1;

    if (auto Err = DefaultTexture.Initialize(Device); Err)
        return xgpu::getErrorInt(Err);

    if (auto Err = DefaultMaterial.Initialize( Device, RuntimeGeom.m_VertexDescriptor ); Err)
        return xgpu::getErrorInt(Err);

    DefaultMaterialInstance.m_Textures.emplace_back(&DefaultTexture);
    if (auto Err = DefaultMaterialInstance.Initialize(DefaultMaterial, Device); Err)
        return xgpu::getErrorInt(Err);

    GeomInstance.m_pRuntimeGeom = &RuntimeGeom;
    for (int i = 0; i < 16; i++)
        GeomInstance.m_MaterialInstances.push_back(&DefaultMaterialInstance);

    //
    // Setup ImGui
    //
    xgpu::tools::imgui::CreateInstance(MainWindow);

    //
    // Create the inspector window
    //
  //  raw3d_inspector Inspector("Property", Device);

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
        if (xgpu::tools::imgui::BeginRendering())
            continue;

        // Update the view with latest window size
        View.setViewport({ 0, 0, MainWindow.getWidth(), MainWindow.getHeight() });

        {
            auto CmdBuffer = MainWindow.getCmdBuffer();
            GeomInstance.Render(CmdBuffer, View.getW2C());
        }

        //
        // Render the Inspector
        //
//        Inspector.Show();


        //
        // Render ImGUI
        //
        xgpu::tools::imgui::Render();

        //
        // Pageflip the windows
        //
        MainWindow.PageFlip();
    }

    return 0;
}



#if 0
//------------------------------------------------------------------------------------------------
static
void DebugMessage(std::string_view View)
{
    printf("%s\n", View.data());
}

//------------------------------------------------------------------------------------------------

struct draw_vert
{
    xcore::vector3d  m_Position;
    xcore::vector2   m_Texcoord;
    xcore::icolor    m_Color;
};

//------------------------------------------------------------------------------------------------

struct runtime_geom : xgeom
{
    auto getVertexVectorFormat( xgeom::stream_info& StreamInfo ) const noexcept
    {
        static_assert((int)xgpu::vertex_descriptor::format::FLOAT_1D             == (int)xgeom::stream_info::format::FLOAT_1D);
        static_assert((int)xgpu::vertex_descriptor::format::FLOAT_2D             == (int)xgeom::stream_info::format::FLOAT_2D);
        static_assert((int)xgpu::vertex_descriptor::format::FLOAT_3D             == (int)xgeom::stream_info::format::FLOAT_3D);
        static_assert((int)xgpu::vertex_descriptor::format::FLOAT_4D             == (int)xgeom::stream_info::format::FLOAT_4D);
        static_assert((int)xgpu::vertex_descriptor::format::UINT8_1D_NORMALIZED  == (int)xgeom::stream_info::format::UINT8_1D_NORMALIZED);
        static_assert((int)xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED  == (int)xgeom::stream_info::format::UINT8_4D_NORMALIZED);
        static_assert((int)xgpu::vertex_descriptor::format::UINT8_1D             == (int)xgeom::stream_info::format::UINT8_1D);
        static_assert((int)xgpu::vertex_descriptor::format::UINT16_1D            == (int)xgeom::stream_info::format::UINT16_1D);
        static_assert((int)xgpu::vertex_descriptor::format::UINT32_1D            == (int)xgeom::stream_info::format::UINT32_1D);
        static_assert((int)xgpu::vertex_descriptor::format::SINT8_3D_NORMALIZED  == (int)xgeom::stream_info::format::SINT8_3D_NORMALIZED);
        static_assert((int)xgpu::vertex_descriptor::format::ENUM_COUNT           == (int)xgeom::stream_info::format::ENUM_COUNT);

        return xgpu::vertex_descriptor::format(StreamInfo.m_Format);
    }

    xgpu::device::error* InitializeRuntime( xgpu::device& Device )
    {
        //
        // Deal first with the index
        //
        {
            assert( m_StreamInfo[0].m_iStream == 0 );
            assert( m_StreamInfo[0].m_ElementsType.m_bIndex );

            auto& StreamInfo = m_StreamInfo[0];

            xgpu::buffer::setup Setup
            { .m_Type           = xgpu::buffer::type::INDEX
            , .m_Usage          = xgpu::buffer::setup::usage::GPU_READ
            , .m_EntryByteSize  = int(StreamInfo.getVectorElementSize())
            , .m_EntryCount     = int(xgeom::m_nIndices)
            , .m_pData          = xgeom::getStreamData(m_StreamInfo[0].m_iStream)
            };
            if( auto Err = Device.Create(m_Buffers[0], Setup); Err ) return Err;
        }

        //
        // Vertex attributes
        //
        {
            std::array<xgpu::vertex_descriptor::attribute, xgeom::max_stream_count_v> Attributes {};
            int nAttributes = 0;
            for (int i = 1; i < xgeom::m_nStreamInfos; ++i)
            {
                auto&       StreamInfo  = m_StreamInfo[i];
                const auto  ElementSize = StreamInfo.getVectorElementSize();

                for( int j=0; j<StreamInfo.m_VectorCount; ++j )
                {
                    auto& Attr       = Attributes[nAttributes++];

                    Attr.m_Format  = getVertexVectorFormat(StreamInfo);
                    Attr.m_Offset  = StreamInfo.m_Offset + j * ElementSize;
                    Attr.m_iStream = StreamInfo.m_iStream - 1;  // Convert to zero base stream since it only cares about vertices
                }
            }

            //
            // Create vertex descriptor
            // TODO: May be we should factor out the vertex descriptors???
            const xgpu::vertex_descriptor::setup Setup
            { .m_bUseStreaming  = xgeom::m_CompactedVertexSize == 0
            , .m_Topology       = xgpu::vertex_descriptor::topology::TRIANGLE_LIST
            , .m_VertexSize     = xgeom::m_CompactedVertexSize
            , .m_Attributes     = std::span{ Attributes.data(), static_cast<std::size_t>(nAttributes) }
            };

            if (auto Err = Device.Create(m_VertexDescriptor, Setup); Err) return Err;
        }

        //
        // Create Vertex Buffers
        //
        for (int i = 1; i < xgeom::m_nStreamInfos; ++i)
        {
            auto& StreamInfo = m_StreamInfo[i];
            if (StreamInfo.m_Offset != 0) continue;

            const xgpu::buffer::setup BufferSetup
            { .m_Type           = xgpu::buffer::type::VERTEX
            , .m_Usage          = xgpu::buffer::setup::usage::GPU_READ
            , .m_EntryByteSize  = xgeom::getVertexSize(i)
            , .m_EntryCount     = int(xgeom::m_nVertices)
            , .m_pData          = xgeom::getStreamData(StreamInfo.m_iStream) //xgeom::m_Stream[StreamInfo.m_iStream]
            };

            if( auto Err = Device.Create(m_Buffers[i], BufferSetup ); Err ) return Err;
        }

        return nullptr;
    }

    xgpu::vertex_descriptor                                         m_VertexDescriptor;
    std::array<xgpu::buffer, xgeom::max_stream_count_v>              m_Buffers;
};

//------------------------------------------------------------------------------------------------

struct runtime_material
{
    xgpu::pipeline m_Pipeline;

    xgpu::device::error* Initialize( xgpu::device& Device, xgpu::vertex_descriptor& VertexDescriptor )
    {
        xgpu::shader MyFragmentShader;
        {
            auto RawData = xgpu::shader::setup::raw_data
            { std::array
                {
                    #include "draw_frag.h"
                }
            };

            if (auto Err = Device.Create(MyFragmentShader, { .m_Type = xgpu::shader::type::FRAGMENT, .m_Sharer = RawData }); Err)
                return Err;
        }

        xgpu::shader MyVertexShader;
        {
            auto UniformConstans = std::array
            { static_cast<int>(sizeof(float) * 4 * 4)   // LocalToClip
            };
            auto RawData = xgpu::shader::setup::raw_data
            { std::array
                {
                    #include "draw_vert.h"
                }
            };
            xgpu::shader::setup Setup
            {
                .m_Type                 = xgpu::shader::type::VERTEX
            ,   .m_Sharer               = RawData
            ,   .m_InOrderUniformSizes  = UniformConstans
            };

            if (auto Err = Device.Create(MyVertexShader, Setup); Err)
                return Err;
        }

        auto Shaders  = std::array<const xgpu::shader*, 2>{ &MyFragmentShader, & MyVertexShader };
        auto Samplers = std::array{ xgpu::pipeline::sampler{} };
        auto Setup    = xgpu::pipeline::setup
        {
            .m_VertexDescriptor = VertexDescriptor
        ,   .m_Shaders          = Shaders
        ,   .m_Samplers         = Samplers
        };

        if (auto Err = Device.Create(m_Pipeline, Setup); Err)
            return Err;


        return nullptr;
    }
};

//------------------------------------------------------------------------------------------------

struct runtime_texture
{
    xgpu::texture m_Texture;

    xgpu::device::error* Initialize( xgpu::device& Device )
    {
        if (auto Err = xgpu::tools::bitmap::Create(m_Texture, Device, xcore::bitmap::getDefaultBitmap()); Err)
            return Err;

        return nullptr;
    }
};

//------------------------------------------------------------------------------------------------

struct runtime_material_instance
{
    runtime_material*               m_pMaterial;
    xgpu::pipeline_instance         m_PipelineInstance;
    std::vector<runtime_texture*>   m_Textures;

    xgpu::device::error* Initialize( runtime_material& Material, xgpu::device& Device )
    {
        auto Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{ m_Textures[0]->m_Texture } };
        auto Setup    = xgpu::pipeline_instance::setup
        { .m_PipeLine = Material.m_Pipeline
        , .m_SamplersBindings = Bindings
        };

        if (auto Err = Device.Create(m_PipelineInstance, Setup); Err)
            return Err;

        return nullptr;
    }

    void Activate(xgpu::cmd_buffer& CmdBuffer, const xcore::matrix4& L2C)
    {
        CmdBuffer.setPipelineInstance(m_PipelineInstance);
        CmdBuffer.setConstants(xgpu::shader::type::VERTEX, 0, &L2C, static_cast<std::uint32_t>(sizeof(xcore::matrix4)));
    }
};

//------------------------------------------------------------------------------------------------

struct runtime_geom_instance
{
    runtime_geom*                           m_pGeom                 = nullptr;
    std::vector<runtime_material_instance*> m_MaterialInstances;
    std::uint64_t                           m_MeshMask              = ~0ull;
    std::uint64_t                           m_BufferMask            = ~0ull;

    void Render(xgpu::cmd_buffer& CmdBuffer, const xcore::matrix4& L2C )
    {
        for (int i = 0, end_i = m_pGeom->m_nMeshes; i < end_i; ++i)
        {
            if (!((m_MeshMask >> i) & 1)) continue;

            auto& Mesh = m_pGeom->m_pMesh[i];

            //for (int l = 0, end_l = Mesh.m_nLODs; l < end_l; ++l)
            int l = 0;
            {
                auto& LOD = m_pGeom->m_pLOD[Mesh.m_iLOD + l];
                if (l != 0) break;

                for (int s = 0, end_s = LOD.m_nSubmesh; s < end_s; ++s)
                {
                    auto& SubMesh = m_pGeom->m_pSubMesh[LOD.m_iSubmesh + s];

                    m_MaterialInstances[SubMesh.m_iMaterial]->Activate(CmdBuffer, L2C);

                    CmdBuffer.setStreamingBuffers({ m_pGeom->m_Buffers.data(), m_pGeom->m_nStreams });


                    CmdBuffer.Draw(SubMesh.m_nIndices, SubMesh.m_iIndex);
                }
            }
        }
    }
};

//
// Setup default material and texture
//
runtime_texture             DefaultTexture;
runtime_material            DefaultMaterial;
runtime_material_instance   DefaultMaterialInstance;
runtime_geom_instance       GeomInstance;

//------------------------------------------------------------------------------------------------

struct compiler
{
    struct compiler_options
    {
        bool                    m_bMergeMeshes          = true;
        string_t                m_RenameMesh            = "Master Mesh";
        string_t                m_UseSkeletonFile;                          // Use a different skeleton from the one found in the mesh data
        bool                    m_GenerateLODs          = false;
        float                   m_LODReduction          = 0.7f;
        int                     m_MaxLODs               = 5;
        bool                    m_bForceAddColorIfNone  = true;

        bool                    m_UseElementStreams     = false;
        bool                    m_SeparatePosition      = false;

        bool                    m_bRemoveColor          = false;
        std::array<bool,4>      m_bRemoveUVs            {};
        bool                    m_bRemoveBTN            = true;
        bool                    m_bRemoveBones          = true;

        bool                    m_bCompressPosition     = false;
        bool                    m_bCompressBTN          = true;
        std::array<bool, 4>     m_bCompressUV           {};
        bool                    m_bCompressWeights      = true;
    };

    struct vertex
    { 
        struct weight
        {
            std::int32_t    m_iBone;
            float           m_Weight;
        };

        xcore::vector3d                 m_Position;
        std::array<xcore::vector2, 4>   m_UVs;
        xcore::icolor                   m_Color;
        xcore::vector3d                 m_Normal;
        xcore::vector3d                 m_Tangent;
        xcore::vector3d                 m_Binormal;
        std::array<weight, 4>           m_Weights;
    };

    struct lod
    {
        float                           m_ScreenArea;
        std::vector<std::uint32_t>      m_Indices;
    };

    struct sub_mesh
    {
        std::vector<vertex>             m_Vertex;
        std::vector<std::uint32_t>      m_Indices;
        std::vector<lod>                m_LODs;
        std::uint32_t                   m_iMaterial;
        int                             m_nWeights      { 0 };
        int                             m_nUVs          { 0 };
        bool                            m_bHasColor     { false };
        bool                            m_bHasNormal    { false };
        bool                            m_bHasBTN       { false };
    };

    struct mesh
    {
        std::string                     m_Name;
        std::vector<sub_mesh>           m_SubMesh;
    };

    compiler()
    {
        m_FinalGeom.Initialize();
    }

    void Load( std::filesystem::path Path )
    {
        try
        {
            xraw3d::assimp::ImportAll(m_RawAnim, m_RawGeom, Path.string().c_str());
        }
        catch (std::runtime_error Error)
        {
            printf("%s", Error.what());
        }
    }

    void ConvertToCompilerMesh( const compiler_options& CompilerOption )
    {
        for( auto& Mesh : m_RawGeom.m_Mesh )
        {
            auto& NewMesh = m_CompilerMesh.emplace_back();
            NewMesh.m_Name = Mesh.m_Name;
        }

        std::vector<std::int32_t> GeomToCompilerVertMesh( m_RawGeom.m_Facet.size() * 3          );
        std::vector<std::int32_t> MaterialToSubmesh     ( m_RawGeom.m_MaterialInstance.size()   );

        int MinVert = 0;
        int MaxVert = int(GeomToCompilerVertMesh.size()-1);
        int CurMaterial = -1;
        int LastMesh    = -1;

        for( auto& Face : m_RawGeom.m_Facet )
        {
            auto& Mesh = m_CompilerMesh[Face.m_iMesh];

            // Make sure all faces are shorted by the mesh
            xassert(Face.m_iMesh >= LastMesh);
            LastMesh = Face.m_iMesh;

            // are we dealing with a new mesh? if so we need to reset the remap of the submesh
            if( Mesh.m_SubMesh.size() == 0 )
            {
                for (auto& R : MaterialToSubmesh) R = -1;
            }

            // are we dealiong with a new submesh is so we need to reset the verts
            if( MaterialToSubmesh[Face.m_iMaterialInstance] == -1 )
            {
                MaterialToSubmesh[Face.m_iMaterialInstance] = int(Mesh.m_SubMesh.size());
                auto& Submesh = Mesh.m_SubMesh.emplace_back();

                Submesh.m_iMaterial = Face.m_iMaterialInstance;

                for (int i = MinVert; i <= MaxVert; ++i)
                    GeomToCompilerVertMesh[i] = -1;

                MaxVert = 0;
                MinVert = int(GeomToCompilerVertMesh.size()-1);
                CurMaterial = Face.m_iMaterialInstance;
            }
            else
            {
                // Make sure that faces are shorted by materials
                xassert(CurMaterial >= Face.m_iMaterialInstance );
            }

            auto& SubMesh = Mesh.m_SubMesh[MaterialToSubmesh[Face.m_iMaterialInstance]];

            for( int i=0; i<3; ++i )
            {
                if( GeomToCompilerVertMesh[Face.m_iVertex[i]] == -1 )
                {
                    GeomToCompilerVertMesh[Face.m_iVertex[i]] = int(SubMesh.m_Vertex.size());
                    auto& CompilerVert = SubMesh.m_Vertex.emplace_back();
                    auto& RawVert      = m_RawGeom.m_Vertex[Face.m_iVertex[i]];

                    CompilerVert.m_Binormal = RawVert.m_BTN[0].m_Binormal;
                    CompilerVert.m_Tangent  = RawVert.m_BTN[0].m_Tangent;
                    CompilerVert.m_Normal   = RawVert.m_BTN[0].m_Normal;
                    CompilerVert.m_Color    = RawVert.m_Color[0];               // This could be n in the future...
                    CompilerVert.m_Position = RawVert.m_Position;
                    
                    if ( RawVert.m_nTangents ) SubMesh.m_bHasBTN    = true;
                    if ( RawVert.m_nNormals  ) SubMesh.m_bHasNormal = true;
                    if ( RawVert.m_nColors   ) SubMesh.m_bHasColor  = true;

                    if( SubMesh.m_Indices.size() && SubMesh.m_nUVs != 0 && RawVert.m_nUVs < SubMesh.m_nUVs )
                    {
                        printf("WARNING: Found a vertex with an inconsistent set of uvs (Expecting %d, found %d) MeshName: %s \n"
                        , SubMesh.m_nUVs
                        , RawVert.m_nUVs
                        , Mesh.m_Name.data()
                        );
                    }
                    else
                    {
                        SubMesh.m_nUVs = RawVert.m_nUVs;

                        for (int j = 0; j < RawVert.m_nUVs; ++j)
                            CompilerVert.m_UVs[j] = RawVert.m_UV[j];
                    }

                    for( int j = 0; j < RawVert.m_nWeights; ++j )
                    {
                        CompilerVert.m_Weights[j].m_iBone  = RawVert.m_Weight[j].m_iBone;
                        CompilerVert.m_Weights[j].m_Weight = RawVert.m_Weight[j].m_Weight;
                    }

                    SubMesh.m_nWeights = std::max(SubMesh.m_nWeights , RawVert.m_nWeights );
                }

                xassert( GeomToCompilerVertMesh[Face.m_iVertex[i]] >= 0 );
                xassert( GeomToCompilerVertMesh[Face.m_iVertex[i]] < m_RawGeom.m_Vertex.size() );
                SubMesh.m_Indices.push_back(GeomToCompilerVertMesh[Face.m_iVertex[i]]);

                MinVert = std::min( MinVert, (int)Face.m_iVertex[i] );
                MaxVert = std::max( MaxVert, (int)Face.m_iVertex[i] );
            }
        }
    }

    void GenenateLODs( const compiler_options& CompilerOption )
    {
        if( CompilerOption.m_GenerateLODs == false ) return;

        for (auto& M : m_CompilerMesh)
        {
            for (auto& S : M.m_SubMesh)
            {
                for ( size_t i = 1; i < CompilerOption.m_MaxLODs; ++i )
                {
                    const float       threshold               = std::powf(CompilerOption.m_LODReduction, float(i));
                    const std::size_t target_index_count      = std::size_t(S.m_Indices.size() * threshold) / 3 * 3;
                    const float       target_error            = 1e-2f;
                    const auto&       Source                  = (S.m_LODs.size())? S.m_LODs.back().m_Indices : S.m_Indices;

                    if( Source.size() < target_index_count )
                        break;

                    auto& NewLod = S.m_LODs.emplace_back();

                    NewLod.m_Indices.resize(Source.size());
                    NewLod.m_Indices.resize( meshopt_simplify( NewLod.m_Indices.data(), Source.data(), Source.size(), &S.m_Vertex[0].m_Position.m_X, S.m_Vertex.size(), sizeof(vertex), target_index_count, target_error));
                }
            }
        }
    }

    void optimizeFacesAndVerts( const compiler_options& CompilerOption )
    {
        for( auto& M : m_CompilerMesh)
        {
            const size_t kCacheSize = 16;
            for( auto& S : M.m_SubMesh )
            {
                meshopt_optimizeVertexCache ( S.m_Indices.data(), S.m_Indices.data(), S.m_Indices.size(), S.m_Vertex.size() ); 
                meshopt_optimizeOverdraw    ( S.m_Indices.data(), S.m_Indices.data(), S.m_Indices.size(), &S.m_Vertex[0].m_Position.m_X, S.m_Vertex.size(), sizeof(vertex), 1.0f );

                for( auto& L : S.m_LODs )
                {
                    meshopt_optimizeVertexCache ( L.m_Indices.data(), L.m_Indices.data(), L.m_Indices.size(), S.m_Vertex.size() );
                    meshopt_optimizeOverdraw    ( L.m_Indices.data(), L.m_Indices.data(), L.m_Indices.size(), &S.m_Vertex[0].m_Position.m_X, S.m_Vertex.size(), sizeof(vertex), 1.0f );
                }
            }
        }
    }

    void GenerateFinalMesh(const compiler_options& CompilerOption)
    {
        std::vector<std::uint32_t>  Indices32;
        std::vector<vertex>         FinalVertex;
        std::vector<xgeom::mesh>    FinalMeshes;
        std::vector<xgeom::submesh> FinalSubmeshes;
        std::vector<xgeom::lod>     FinalLod;
        int                         UVDimensionCount     = 0;
        int                         WeightDimensionCount = 0;
        int                         ColorDimensionCount  = 0;

        FinalMeshes.resize(m_CompilerMesh.size() );

        for( int iMesh = 0; iMesh < FinalMeshes.size(); ++iMesh )
        {
            auto& FinalMesh = FinalMeshes[iMesh];
            auto& CompMesh  = m_CompilerMesh[iMesh];

            strcpy_s( FinalMesh.m_Name.data(), FinalMesh.m_Name.size(), CompMesh.m_Name.c_str() );

            FinalMesh.m_iLOD  = std::uint16_t( FinalLod.size() );
            FinalMesh.m_nLODs = 0;

            auto& FinalLOD = FinalLod.emplace_back();

            FinalLOD.m_iSubmesh = std::uint16_t(FinalSubmeshes.size());
            FinalLOD.m_nSubmesh = std::uint16_t(CompMesh.m_SubMesh.size());

            //
            // Gather LOD 0
            //
            std::vector<int> SubmeshStartIndex;
            for( auto& S : CompMesh.m_SubMesh )
            {
                const auto iBaseVertex = FinalVertex.size();
                SubmeshStartIndex.push_back((int)iBaseVertex);
                auto& FinalSubmesh = FinalSubmeshes.emplace_back();

                FinalSubmesh.m_iMaterial = S.m_iMaterial;
                FinalSubmesh.m_iIndex    = std::uint32_t(Indices32.size());
                FinalSubmesh.m_nIndices  = std::uint32_t(S.m_Indices.size());

                UVDimensionCount        = std::max( S.m_nUVs, UVDimensionCount );
                WeightDimensionCount    = std::max( S.m_nWeights, WeightDimensionCount );
                ColorDimensionCount     = std::max( S.m_bHasColor?1:0, ColorDimensionCount );

                for( const auto& Index : S.m_Indices )
                {
                    Indices32.push_back( std::uint32_t(Index + iBaseVertex) );
                }

                for( const auto& Verts : S.m_Vertex )
                {
                    FinalMesh.m_BBox.AddVerts( &Verts.m_Position, 1 );
                    m_FinalGeom.m_BBox.AddVerts( &Verts.m_Position, 1 );
                    FinalVertex.push_back(Verts);
                }
            }

            //
            // Gather LOD 1... to n
            //
            do
            {
                auto& LOD = FinalLod.emplace_back();
                LOD.m_iSubmesh = std::uint16_t(FinalSubmeshes.size());
                LOD.m_nSubmesh = 0;

                int iLocalSubmesh = -1;
                for( auto& S : CompMesh.m_SubMesh )
                {
                    iLocalSubmesh++;
                    if( FinalMesh.m_nLODs >= S.m_LODs.size() ) continue;

                    const auto iBaseVertex = SubmeshStartIndex[iLocalSubmesh];
                    LOD.m_nSubmesh++;

                    auto& FinalSubmesh = FinalSubmeshes.emplace_back();

                    FinalSubmesh.m_iMaterial = S.m_iMaterial;
                    FinalSubmesh.m_iIndex    = std::uint32_t(Indices32.size());
                    FinalSubmesh.m_nIndices  = std::uint32_t(S.m_Indices.size());

                    for (const auto& Index : S.m_Indices)
                    {
                        Indices32.push_back(std::uint32_t(Index + iBaseVertex));
                    }
                }

                if( LOD.m_nSubmesh == 0 )
                {
                    FinalLod.erase(FinalLod.end()-1);
                    break;
                }
                else
                {
                    FinalMesh.m_nLODs++;
                }

            } while(true);
        }

        //-----------------------------------------------------------------------------------
        // Final Optimization Step. Optimize vertex location and remap the indices.
        //-----------------------------------------------------------------------------------

        // vertex fetch optimization should go last as it depends on the final index order
        // note that the order of LODs above affects vertex fetch results
        FinalVertex.resize( meshopt_optimizeVertexFetch( FinalVertex.data(), Indices32.data(), Indices32.size(), FinalVertex.data(), FinalVertex.size(), sizeof(FinalVertex[0]) ) );

        const int kCacheSize = 16;
        m_VertCacheStats = meshopt_analyzeVertexCache(Indices32.data(), Indices32.size(), FinalVertex.size(), kCacheSize, 0, 0);
        m_VertFetchStats = meshopt_analyzeVertexFetch(Indices32.data(), Indices32.size(), FinalVertex.size(), sizeof(FinalVertex[0]));
        m_OverdrawStats  = meshopt_analyzeOverdraw   (Indices32.data(), Indices32.size(), &FinalVertex[0].m_Position.m_X, FinalVertex.size(), sizeof(FinalVertex[0]) );

        m_VertCacheNVidiaStats  = meshopt_analyzeVertexCache(Indices32.data(), Indices32.size(), FinalVertex.size(), 32, 32, 32);
        m_VertCacheAMDStats     = meshopt_analyzeVertexCache(Indices32.data(), Indices32.size(), FinalVertex.size(), 14, 64, 128);
        m_VertCacheIntelStats   = meshopt_analyzeVertexCache(Indices32.data(), Indices32.size(), FinalVertex.size(), 128, 0, 0);

        // TODO: Translate this information into a scoring system that goes from 100% to 0% (or something that makes more sense to casual users)
        printf("INFO: ACMR %f ATVR %f (NV %f AMD %f Intel %f) Overfetch %f Overdraw %f\n"
        , m_VertCacheStats.acmr         // transformed vertices / triangle count; best case 0.5, worst case 3.0, optimum depends on topology
        , m_VertCacheStats.atvr         // transformed vertices / vertex count; best case 1.0, worst case 6.0, optimum is 1.0 (each vertex is transformed once)
        , m_VertCacheNVidiaStats.atvr   // transformed vertices / vertex count; best case 1.0, worst case 6.0, optimum is 1.0 (each vertex is transformed once)
        , m_VertCacheAMDStats.atvr      // transformed vertices / vertex count; best case 1.0, worst case 6.0, optimum is 1.0 (each vertex is transformed once)
        , m_VertCacheIntelStats.atvr    // transformed vertices / vertex count; best case 1.0, worst case 6.0, optimum is 1.0 (each vertex is transformed once)
        , m_VertFetchStats.overfetch    // fetched bytes / vertex buffer size; best case 1.0 (each byte is fetched once)
        , m_OverdrawStats.overdraw      // fetched bytes / vertex buffer size; best case 1.0 (each byte is fetched once)
        );

        //-----------------------------------------------------------------------------------
        // Create Stream Infos
        //-----------------------------------------------------------------------------------
        std::size_t MaxVertAligment = 1u;

        m_FinalGeom.m_nStreamInfos        = 0;
        m_FinalGeom.m_nStreams            = 0;
        m_FinalGeom.m_StreamTypes.m_Value = 0;

        //
        // Deal in indices
        //
        {
            auto& Stream = m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos];
            Stream.m_ElementsType.m_Value       = 0;

            Stream.m_VectorCount                = 1;
            Stream.m_Format                     = Indices32.size() > 0xffffu ? xgeom::stream_info::format::UINT32_1D : xgeom::stream_info::format::UINT16_1D;
            Stream.m_ElementsType.m_bIndex      = true;
            Stream.m_Offset                     = 0;
            Stream.m_iStream                    = m_FinalGeom.m_nStreams;

            m_FinalGeom.m_StreamTypes.m_bIndex  = true;
            m_FinalGeom.m_nStreams++;
            m_FinalGeom.m_nStreamInfos++;
        }


        //
        // Deal with Position
        //
        {
            auto& Stream = m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos];
            Stream.m_ElementsType.m_Value       = 0;

            Stream.m_VectorCount                = 1;
            Stream.m_Format                     = xgeom::stream_info::format::FLOAT_3D;
            Stream.m_ElementsType.m_bPosition   = true;
            Stream.m_Offset                     = 0;
            Stream.m_iStream                    = m_FinalGeom.m_nStreams;

            m_FinalGeom.m_StreamTypes.m_bPosition = true;
            m_FinalGeom.m_nStreamInfos++;

            if( CompilerOption.m_UseElementStreams || CompilerOption.m_SeparatePosition )
            {
                m_FinalGeom.m_nStreams++;
            }
        }

        //
        // Deal with UVs
        //
        if( UVDimensionCount 
         && (  CompilerOption.m_bRemoveUVs[0] == false
            || CompilerOption.m_bRemoveUVs[1] == false
            || CompilerOption.m_bRemoveUVs[2] == false
            || CompilerOption.m_bRemoveUVs[3] == false ) )
        {
            auto& Stream = m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos];

            // Make sure the base offset is set
            Stream.m_Offset = m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos - 1].m_iStream != m_FinalGeom.m_nStreams
                ? 0
                : m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos - 1].m_Offset 
                    + m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos - 1].getSize();

            Stream.m_ElementsType.m_Value       = 0;

            Stream.m_VectorCount     = [&] { int k = 0; for (int i = 0; i < UVDimensionCount; i++) if (CompilerOption.m_bRemoveUVs[i] == false) k++; return k; }();
            Stream.m_Format          = xgeom::stream_info::format::FLOAT_2D;

            // Do we still have UVs?
            if( Stream.m_VectorCount )
            {
                Stream.m_ElementsType.m_bUVs        = true;
                Stream.m_iStream                    = m_FinalGeom.m_nStreams;

                Stream.m_Offset = xcore::bits::Align(Stream.m_Offset, alignof(float));
                MaxVertAligment = std::max(MaxVertAligment, alignof(float));

                m_FinalGeom.m_nStreamInfos++;
                m_FinalGeom.m_StreamTypes.m_bUVs = true;

                if (CompilerOption.m_UseElementStreams) m_FinalGeom.m_nStreams++;
            }
        }

        //
        // Deal with Color
        //
        if( CompilerOption.m_bRemoveColor  == false && ColorDimensionCount )
        {
            auto& Stream = m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos];

            // Make sure the base offset is set
            Stream.m_Offset = m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos - 1].m_iStream != m_FinalGeom.m_nStreams
                ? 0
                : m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos - 1].m_Offset 
                    + m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos - 1].getSize();

            Stream.m_ElementsType.m_Value       = 0;

            Stream.m_VectorCount                = 1;
            Stream.m_Format                     = xgeom::stream_info::format::UINT8_4D_NORMALIZED;
            Stream.m_ElementsType.m_bColor      = true;
            Stream.m_iStream                    = m_FinalGeom.m_nStreams;

            Stream.m_Offset = xcore::bits::Align(Stream.m_Offset, alignof(xcore::icolor));
            MaxVertAligment = std::max(MaxVertAligment, alignof(xcore::icolor));

            m_FinalGeom.m_nStreamInfos++;
            m_FinalGeom.m_StreamTypes.m_bColor = true;

            if (CompilerOption.m_UseElementStreams) m_FinalGeom.m_nStreams++;
        }

        //
        // Deal with Bone Weights
        // 
        if( WeightDimensionCount && CompilerOption.m_bRemoveBones == false )
        {
            auto& Stream = m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos];

            // Make sure the base offset is set
            Stream.m_Offset = m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos - 1].m_iStream != m_FinalGeom.m_nStreams
                ? 0
                : m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos - 1].m_Offset
                + m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos - 1].getSize();

            if( CompilerOption.m_bCompressWeights )
            {
                Stream.m_ElementsType.m_Value       = 0;

                Stream.m_VectorCount                = std::uint8_t(WeightDimensionCount);
                Stream.m_Format                     = xgeom::stream_info::format::UINT8_1D_NORMALIZED;
                Stream.m_ElementsType.m_bBoneWeights= true;
                Stream.m_iStream                    = m_FinalGeom.m_nStreams;

                Stream.m_Offset = xcore::bits::Align(Stream.m_Offset, alignof(std::uint8_t));
                MaxVertAligment = std::max(MaxVertAligment, alignof(std::uint8_t));
            }
            else
            {
                Stream.m_ElementsType.m_Value       = 0;

                Stream.m_VectorCount                = std::uint8_t(WeightDimensionCount);
                Stream.m_Format                     = xgeom::stream_info::format::FLOAT_1D;
                Stream.m_ElementsType.m_bBoneWeights= true; 
                Stream.m_iStream                    = m_FinalGeom.m_nStreams;

                Stream.m_Offset = xcore::bits::Align(Stream.m_Offset, alignof(float));
                MaxVertAligment = std::max(MaxVertAligment, alignof(float));
            }

            m_FinalGeom.m_nStreamInfos++;
            m_FinalGeom.m_StreamTypes.m_bBoneWeights = true;

            if (CompilerOption.m_UseElementStreams) m_FinalGeom.m_nStreams++;
        }

        //
        // Deal with Bone Indices
        // 
        if( WeightDimensionCount && CompilerOption.m_bRemoveBones == false )
        {
            auto& Stream = m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos];

            // Make sure the base offset is set
            Stream.m_Offset = m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos - 1].m_iStream != m_FinalGeom.m_nStreams
                ? 0
                : m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos - 1].m_Offset
                + m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos - 1].getSize();

            if( CompilerOption.m_bCompressWeights || (m_RawGeom.m_Bone.size() < 0xff) )
            {
                Stream.m_ElementsType.m_Value       = 0;

                Stream.m_VectorCount                = std::uint8_t(WeightDimensionCount);
                Stream.m_Format                     = xgeom::stream_info::format::UINT8_1D;
                Stream.m_ElementsType.m_bBoneIndices= true;
                Stream.m_iStream                    = m_FinalGeom.m_nStreams;

                Stream.m_Offset = xcore::bits::Align(Stream.m_Offset, alignof(std::uint8_t));
                MaxVertAligment = std::max(MaxVertAligment, alignof(std::uint8_t));
            }
            else
            {
                Stream.m_ElementsType.m_Value       = 0;

                Stream.m_VectorCount                = std::uint8_t(WeightDimensionCount);
                Stream.m_Format                     = xgeom::stream_info::format::UINT16_1D;
                Stream.m_ElementsType.m_bBoneIndices= true;
                Stream.m_iStream                    = m_FinalGeom.m_nStreams;

                Stream.m_Offset = xcore::bits::Align(Stream.m_Offset, alignof(std::uint16_t));
                MaxVertAligment = std::max(MaxVertAligment, alignof(std::uint16_t));
            }

            m_FinalGeom.m_nStreamInfos++;
            m_FinalGeom.m_StreamTypes.m_bBoneIndices = true;

            if (CompilerOption.m_UseElementStreams) m_FinalGeom.m_nStreams++;
        }

        //
        // Deal with normals
        //
        if( CompilerOption.m_bRemoveBTN == false )
        {
            auto& Stream = m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos];

            // If the position has a different stream then our offset should be zero
            // Other wise we are dealing with a single vertex structure. In that case
            // we should set our offset propertly.
            Stream.m_Offset = m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos - 1].m_iStream != m_FinalGeom.m_nStreams 
                ? 0
                : m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos - 1].m_Offset 
                    + m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos - 1].getSize();

            if( CompilerOption.m_bCompressBTN == false )
            {
                Stream.m_ElementsType.m_Value       = 0;

                Stream.m_VectorCount                = 3;
                Stream.m_Format                     = xgeom::stream_info::format::FLOAT_3D;
                Stream.m_ElementsType.m_bBTNs       = true;
                Stream.m_iStream                    = m_FinalGeom.m_nStreams;

                Stream.m_Offset = xcore::bits::Align(Stream.m_Offset, alignof(xcore::vector3d));
                MaxVertAligment = std::max( MaxVertAligment, alignof(xcore::vector3d) );
            }
            else
            {
                Stream.m_ElementsType.m_Value       = 0;

                Stream.m_VectorCount                = 3;
                Stream.m_Format                     = xgeom::stream_info::format::SINT8_3D_NORMALIZED;
                Stream.m_ElementsType.m_bBTNs       = true;
                Stream.m_iStream                    = m_FinalGeom.m_nStreams;

                Stream.m_Offset = xcore::bits::Align(Stream.m_Offset, alignof(std::int8_t));
                MaxVertAligment = std::max(MaxVertAligment, alignof(std::int8_t));
            }

            m_FinalGeom.m_nStreamInfos++;
            m_FinalGeom.m_StreamTypes.m_bBTNs = true;

            if (CompilerOption.m_UseElementStreams) m_FinalGeom.m_nStreams++;
        }

        //
        // Fill up the rest of the info
        //
        m_FinalGeom.m_nMaterials          = std::uint16_t(m_RawGeom.m_MaterialInstance.size());
        m_FinalGeom.m_nMeshes             = std::uint16_t(FinalMeshes.size());
        m_FinalGeom.m_nSubMeshs           = std::uint16_t(FinalSubmeshes.size());
        m_FinalGeom.m_nIndices            = std::uint16_t(Indices32.size());
        m_FinalGeom.m_nVertices           = std::uint32_t(FinalVertex.size());
        m_FinalGeom.m_CompactedVertexSize = CompilerOption.m_UseElementStreams
                                            ? std::uint8_t(0)
                                            : std::uint8_t( xcore::bits::Align((std::uint32_t)m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos - 1].m_Offset
                                                                                            + m_FinalGeom.m_StreamInfo[m_FinalGeom.m_nStreamInfos - 1].getSize()
                                                                                            , (int)MaxVertAligment ) );
        m_FinalGeom.m_nBones        = 0;
        m_FinalGeom.m_nDisplayLists = 0;

        // Allocate the big vertex structure if we have to
        constexpr auto max_aligment_v = 16;
        using max_align_byte = std::byte alignas(max_aligment_v);
        if( false == CompilerOption.m_UseElementStreams )
        {
            m_FinalGeom.m_nStreams++;
            xassert( max_aligment_v > MaxVertAligment );
            m_FinalGeom.m_Stream[m_FinalGeom.m_nStreams-1] = new max_align_byte[m_FinalGeom.m_nVertices * m_FinalGeom.m_CompactedVertexSize];
        }

        //-----------------------------------------------------------------------------------
        // Create the streams and copy data
        //-----------------------------------------------------------------------------------
        for( int i=0; i<m_FinalGeom.m_nStreamInfos; ++i )
        {
            const auto& StreamInfo = m_FinalGeom.m_StreamInfo[i];
            std::size_t Stride     = m_FinalGeom.m_CompactedVertexSize;

            // If we are dealing with individual streams or we want to separate from the big vertex
            // then we have to allocate memory
            if( i != 0 && CompilerOption.m_UseElementStreams )
            {
                m_FinalGeom.m_Stream[StreamInfo.m_iStream] = new max_align_byte[m_FinalGeom.m_nVertices * StreamInfo.getSize()];

                // Customize the stride
                Stride = StreamInfo.getSize();
            }

            switch( StreamInfo.m_ElementsType.m_Value )
            {
            //
            // Indices
            //
            case xgeom::stream_info::element_def::index_mask_v: 
            {
                if (StreamInfo.m_Format == xgeom::stream_info::format::UINT32_1D)
                {
                    auto pIndexData = new std::uint32_t[Indices32.size()];
                    m_FinalGeom.m_Stream[StreamInfo.m_iStream] = reinterpret_cast<std::byte*>(pIndexData);

                    for (auto i = 0u; i < Indices32.size(); ++i)
                    {
                        pIndexData[i] = Indices32[i];
                    }
                }
                else
                {
                    auto pIndexData = new std::uint16_t[Indices32.size()];
                    m_FinalGeom.m_Stream[StreamInfo.m_iStream] = reinterpret_cast<std::byte*>(pIndexData);

                    for (auto i = 0u; i < Indices32.size(); ++i)
                    {
                        pIndexData[i] = std::uint16_t(Indices32[i]);
                    }
                }
                break;
            }
            //
            // Positions
            //
            case xgeom::stream_info::element_def::position_mask_v:
            {
                xassert(StreamInfo.getVectorElementSize() == 4);

                // If we are dealing with individual streams or we want to separate from the big vertex
                // then we have to allocate memory
                if( false == CompilerOption.m_UseElementStreams && CompilerOption.m_SeparatePosition )
                {
                    m_FinalGeom.m_Stream[StreamInfo.m_iStream] = reinterpret_cast<std::byte*>(new xcore::vector3d[m_FinalGeom.m_nVertices]);

                    // Customize the stride
                    Stride = StreamInfo.getSize();
                }

                std::byte*  pVertex = m_FinalGeom.m_Stream[StreamInfo.m_iStream] + StreamInfo.m_Offset;

                for( auto i=0u; i< FinalVertex.size(); ++i )
                {
                    std::memcpy(pVertex, &FinalVertex[i].m_Position, sizeof(xcore::vector3d) );
                    pVertex += Stride;
                }

                break;
            }

            //
            // BTN
            //
            case xgeom::stream_info::element_def::btn_mask_v:
            {
                std::int8_t* pVertex = reinterpret_cast<std::int8_t*>( m_FinalGeom.m_Stream[StreamInfo.m_iStream] + StreamInfo.m_Offset );

                if( CompilerOption.m_bCompressBTN == false )
                {
                    xassert(StreamInfo.getVectorElementSize() == 4);
                    for( auto i=0u; i< FinalVertex.size(); ++i )
                    {
                        std::memcpy(&pVertex[0 * sizeof(xcore::vector3d)], &FinalVertex[i].m_Binormal, sizeof(xcore::vector3d));
                        std::memcpy(&pVertex[1 * sizeof(xcore::vector3d)], &FinalVertex[i].m_Tangent,  sizeof(xcore::vector3d));
                        std::memcpy(&pVertex[2 * sizeof(xcore::vector3d)], &FinalVertex[i].m_Normal,   sizeof(xcore::vector3d));
                        pVertex += Stride;
                    }
                }
                else
                {
                    xassert(StreamInfo.getVectorElementSize() == 1);
                    Stride -= 3*3;
                    for( auto i=0u; i< FinalVertex.size(); ++i )
                    {
                        *pVertex = std::int8_t(FinalVertex[i].m_Binormal.m_X * (0xff >> 1)); pVertex++;
                        *pVertex = std::int8_t(FinalVertex[i].m_Binormal.m_Y * (0xff >> 1)); pVertex++;
                        *pVertex = std::int8_t(FinalVertex[i].m_Binormal.m_Z * (0xff >> 1)); pVertex++;

                        *pVertex = std::int8_t(FinalVertex[i].m_Tangent.m_X * (0xff >> 1)); pVertex++;
                        *pVertex = std::int8_t(FinalVertex[i].m_Tangent.m_Y * (0xff >> 1)); pVertex++;
                        *pVertex = std::int8_t(FinalVertex[i].m_Tangent.m_Z * (0xff >> 1)); pVertex++;

                        *pVertex = std::int8_t(FinalVertex[i].m_Normal.m_X * (0xff >> 1)); pVertex++;
                        *pVertex = std::int8_t(FinalVertex[i].m_Normal.m_Y * (0xff >> 1)); pVertex++;
                        *pVertex = std::int8_t(FinalVertex[i].m_Normal.m_Z * (0xff >> 1)); pVertex++;

                        pVertex += Stride;
                    }
                }
        
                break;
            }

            //
            // UVs
            //
            case xgeom::stream_info::element_def::uv_mask_v:
            {
                xassert(StreamInfo.getVectorElementSize() == 4);
                auto* pVertex = m_FinalGeom.m_Stream[StreamInfo.m_iStream] + StreamInfo.m_Offset;

                for( auto i = 0u; i < FinalVertex.size(); ++i )
                {
                    for( int j = 0, k = 0; j < UVDimensionCount; ++j )
                    {
                        if (CompilerOption.m_bRemoveUVs[j]) continue;
                        std::memcpy(&pVertex[k*sizeof(xcore::vector2)], &FinalVertex[i].m_UVs[j], sizeof(xcore::vector2));
                        k++;
                    }

                    pVertex += Stride;
                }

                break;
            }

            //
            // Colors
            //
            case xgeom::stream_info::element_def::color_mask_v:
            {
                xassert(StreamInfo.getVectorElementSize() == 1);
                auto* pVertex = m_FinalGeom.m_Stream[StreamInfo.m_iStream] + StreamInfo.m_Offset;

                for( auto i = 0u; i < FinalVertex.size(); ++i )
                {
                    (*(xcore::icolor*)pVertex) = FinalVertex[i].m_Color;
                    pVertex += Stride;
                }

                break;
            }

            //
            // Bone Weights
            //
            case xgeom::stream_info::element_def::bone_weight_mask_v:
            {
                auto* pVertex = reinterpret_cast<std::uint8_t*>(m_FinalGeom.m_Stream[StreamInfo.m_iStream] + StreamInfo.m_Offset);

                if( StreamInfo.getVectorElementSize() == 1 )
                {
                    for (auto i = 0u; i < FinalVertex.size(); ++i)
                    {
                        for (int j = 0; j < StreamInfo.m_VectorCount; ++j)
                        {
                            pVertex[j] = std::uint8_t(FinalVertex[i].m_Weights[j].m_Weight * 0xff);
                        }

                        pVertex += Stride;
                    }
                }
                else
                {
                    xassert(StreamInfo.getVectorElementSize() == 4);
                    for( auto i = 0u; i < FinalVertex.size(); ++i )
                    {
                        for (int j = 0; j < StreamInfo.m_VectorCount; ++j)
                        {
                            ((float*)pVertex)[j] = FinalVertex[i].m_Weights[j].m_Weight;
                        }

                        pVertex += Stride;
                    }
                }

                break;
            } 

            //
            // Bone Indices
            //
            case xgeom::stream_info::element_def::bone_index_mask_v:
            {
                auto* pVertex = reinterpret_cast<std::uint8_t*>(m_FinalGeom.m_Stream[StreamInfo.m_iStream] + StreamInfo.m_Offset);

                if( StreamInfo.getVectorElementSize() == 1 )
                {
                    for (auto i = 0u; i < FinalVertex.size(); ++i)
                    {
                        for (int j = 0; j < StreamInfo.m_VectorCount; ++j)
                        {
                            *pVertex = std::uint8_t(FinalVertex[i].m_Weights[j].m_iBone);
                        }

                        pVertex += Stride;
                    }
                }
                else
                {
                    xassert(StreamInfo.getVectorElementSize() == 2);
                    for( auto i = 0u; i < FinalVertex.size(); ++i )
                    {
                        for (int j = 0; j < StreamInfo.m_VectorCount; ++j)
                        {
                            ((std::uint16_t*)pVertex)[j] = FinalVertex[i].m_Weights[j].m_iBone;
                        }

                        pVertex += Stride;
                    }
                }

                break;
            } 

            } // end case
        }// end for loop

        auto Transfer = []< typename T >( std::vector<T>& V )
        {
            auto Own = std::make_unique<T[]>(V.size());
            for( auto i=0u; i<V.size(); ++i ) Own[i] = V[i];
            return Own.release();
        };

        //
        // Set the rest of the pointers
        //
        m_FinalGeom.m_pLOD      = Transfer(FinalLod);
        m_FinalGeom.m_pMesh     = Transfer(FinalMeshes);
        m_FinalGeom.m_pSubMesh  = Transfer(FinalSubmeshes);
        m_FinalGeom.m_pBone     = nullptr;
        m_FinalGeom.m_pDList    = nullptr;
    }

    void Compile( const compiler_options& CompilerOption )
    {
        try
        {
            if (CompilerOption.m_bForceAddColorIfNone) m_RawGeom.ForceAddColorIfNone();
            if (CompilerOption.m_bMergeMeshes) m_RawGeom.CollapseMeshes(CompilerOption.m_RenameMesh.c_str());
            m_RawGeom.CleanMesh();
            m_RawGeom.SortFacetsByMeshMaterialBone();

            //
            // Convert to mesh
            //
            m_FinalGeom.Reset();
            ConvertToCompilerMesh(CompilerOption);
            GenenateLODs(CompilerOption);
            optimizeFacesAndVerts(CompilerOption);
            GenerateFinalMesh(CompilerOption);
        }
        catch (std::runtime_error Error)
        {
            printf("%s", Error.what());
        }
    }

    meshopt_VertexCacheStatistics   m_VertCacheAMDStats;
    meshopt_VertexCacheStatistics   m_VertCacheNVidiaStats;
    meshopt_VertexCacheStatistics   m_VertCacheIntelStats;
    meshopt_VertexCacheStatistics   m_VertCacheStats;
    meshopt_VertexFetchStatistics   m_VertFetchStats;
    meshopt_OverdrawStatistics      m_OverdrawStats;
    runtime_geom                    m_FinalGeom;
    std::vector<mesh>               m_CompilerMesh;
    xraw3d::anim                    m_RawAnim;
    xraw3d::geom                    m_RawGeom;
};

//------------------------------------------------------------------------------------------------

struct raw3d_inspector
{
    property::inspector m_Inspector;

    raw3d_inspector( const char* pName, xgpu::device& Device ) 
        : m_Inspector   ( pName )
        , m_Device      ( Device )
    {
        m_Inspector.AppendEntity();
        m_Inspector.AppendEntityComponent(property::getTable(*this), this );
    }

    void Show()
    {
        ImGui::SetNextWindowPos({10,10});
        m_Inspector.Show( []{}
              /*, ImGuiWindowFlags_NoBackground
                        | ImGuiWindowFlags_NoTitleBar 
                        | ImGuiWindowFlags_NoMove */
                        );
    }

    void Load()
    {
        auto Path = gbed_filebrowser::BrowseFile( std::filesystem::current_path(), true, L".fbx" );

        m_GeomCompiler.Load(Path);

        m_Meshes.clear();
        for( auto& E : m_GeomCompiler.m_RawGeom.m_Mesh )
        {
            mesh_info Info;
            xcore::string::Copy( Info.m_Name, E.m_Name );
            Info.m_nFaces = 0;
            Info.m_nBones = E.m_nBones;
            m_Meshes.push_back(Info);
        }

        for( auto& F : m_GeomCompiler.m_RawGeom.m_Facet )
        {
            m_Meshes[ F.m_iMesh ].m_nFaces++;
            
            bool bFoundMaterial = false;
            for( auto& Mi : m_Meshes[F.m_iMesh].m_Materials )
            {
                if( Mi == F.m_iMaterialInstance ) 
                {
                    bFoundMaterial = true;
                    break;
                }
            }
            
            if(!bFoundMaterial) m_Meshes[F.m_iMesh].m_Materials.push_back(F.m_iMaterialInstance);
        }
    }

    void Compile()
    {
        m_GeomCompiler.Compile(m_CompilerOptions);
        m_GeomCompiler.m_FinalGeom.InitializeRuntime(m_Device);

        if (auto Err = DefaultTexture.Initialize(m_Device); Err)
            return;

        if (auto Err = DefaultMaterial.Initialize( m_Device, m_GeomCompiler.m_FinalGeom.m_VertexDescriptor ); Err)
            return;

        DefaultMaterialInstance.m_Textures.emplace_back(&DefaultTexture);
        if (auto Err = DefaultMaterialInstance.Initialize(DefaultMaterial, m_Device); Err)
            return;

        GeomInstance.m_pGeom = &m_GeomCompiler.m_FinalGeom;
        for (int i = 0; i < 16; i++)
            GeomInstance.m_MaterialInstances.push_back(&DefaultMaterialInstance);

    }

    struct mesh_info
    {
        xraw3d::string      m_Name;
        int                 m_nFaces;
        int                 m_nBones;
        std::vector<int>    m_Materials;
    };

    xgpu::device                m_Device;
    compiler::compiler_options  m_CompilerOptions;
    std::vector<mesh_info>      m_Meshes;
    compiler                    m_GeomCompiler;
};

property_begin_name(raw3d_inspector::mesh_info, "Mesh Info")
{
    property_var_fnbegin("Name", string_t)
    {
        if (isRead) InOut = Self.m_Name;
        else
        {
            // Some one press the button
            xcore::string::Copy( Self.m_Name, InOut );
        }
    } property_var_fnend()
,   property_var(m_nFaces)
,   property_var(m_nBones)
,   property_var(m_Materials)
} property_end();

property_begin_name(compiler::compiler_options, "Compiler Options")
{
    property_var(m_bMergeMeshes)
,   property_var(m_RenameMesh)
    .DynamicFlags([](const std::byte& Bytes) noexcept->property::flags::type
    {
        auto& Self = reinterpret_cast<const t_self&>(Bytes);
        if (Self.m_bMergeMeshes == false ) return property::flags::DISABLE;
        return {};
    })
,   property_var(m_GenerateLODs)
,   property_var(m_LODReduction)
    .DynamicFlags([](const std::byte& Bytes) noexcept->property::flags::type
    {
        auto& Self = reinterpret_cast<const t_self&>(Bytes);
        if (Self.m_GenerateLODs == false ) return property::flags::DISABLE;
        return {};
    })
,   property_var(m_MaxLODs)
    .DynamicFlags([](const std::byte& Bytes) noexcept->property::flags::type
    {
        auto& Self = reinterpret_cast<const t_self&>(Bytes);
        if (Self.m_GenerateLODs == false ) return property::flags::DISABLE;
        return {};
    })
,   property_var(m_UseElementStreams)
,   property_var(m_bForceAddColorIfNone)
,   property_var(m_SeparatePosition)
    .DynamicFlags([](const std::byte& Bytes) noexcept->property::flags::type
    {
        auto& Self = reinterpret_cast<const t_self&>(Bytes);
        if (Self.m_UseElementStreams) return property::flags::DISABLE;
        return {};
    })
,   property_var(m_bRemoveColor)
,   property_var(m_bRemoveUVs)
,   property_var(m_bRemoveBTN)
,   property_var(m_bRemoveBones)
,   property_var(m_bCompressPosition)
,   property_var(m_bCompressBTN)
    .DynamicFlags([](const std::byte& Bytes) noexcept->property::flags::type
    {
        auto& Self = reinterpret_cast<const t_self&>(Bytes);
        if (Self.m_bRemoveBTN) return property::flags::DISABLE;
        return {};
    })
,   property_var(m_bCompressUV)
    .DynamicFlags([](const std::byte& Bytes) noexcept->property::flags::type
    {
        auto& Self = reinterpret_cast<const t_self&>(Bytes);
        if (Self.m_bRemoveUVs[0] == false 
        &&  Self.m_bRemoveUVs[1] == false
        &&  Self.m_bRemoveUVs[2] == false
        &&  Self.m_bRemoveUVs[3] == false ) return property::flags::DISABLE;
        return {};
    })
,   property_var(m_bCompressWeights)
    .DynamicFlags([](const std::byte& Bytes) noexcept->property::flags::type
    {
        auto& Self = reinterpret_cast<const t_self&>(Bytes);
        if (Self.m_bRemoveBones) return property::flags::DISABLE;
        return {};
    })

} property_end();

property_begin_name(raw3d_inspector, "Raw 3D Info")
{
    property_var_fnbegin("Load Mesh", string_t )
    {
        if( isRead ) InOut = "Browse";
        else
        {
            // Some one press the button
            Self.Load();
        }
    } property_var_fnend()
        .EDStyle(property::edstyle<string_t>::Button())
,   property_var_fnbegin("Compile", string_t )
    {
        if( isRead ) InOut = "Run Compilation";
        else
        {
            // Some one press the button
            Self.Compile();
        }
    } property_var_fnend()
        .EDStyle(property::edstyle<string_t>::Button())
        .DynamicFlags([](const std::byte& Bytes) noexcept->property::flags::type
        {
            auto& Self = reinterpret_cast<const t_self&>(Bytes);
            if( Self.m_Meshes.size() == 0 ) return property::flags::DISABLE;
            return {};
        })
,   property_var(m_CompilerOptions)
,   property_var(m_Meshes)
        .Flags(property::flags::SHOW_READONLY)
} property_end();

//------------------------------------------------------------------------------------------------

int E09_Example()
{
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = true, .m_pLogErrorFunc = DebugMessage, .m_pLogWarning = DebugMessage }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err)
        return xgpu::getErrorInt(Err);

    xgpu::window MainWindow;
    if (auto Err = Device.Create(MainWindow, {}); Err)
        return xgpu::getErrorInt(Err);

    //
    // Setup ImGui
    //
    xgpu::tools::imgui::CreateInstance(Instance, Device, MainWindow);

    //
    // Create the inspector window
    //
    raw3d_inspector Inspector("Property", Device);

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
        if (xgpu::tools::imgui::BeginRendering())
            continue;

        // Update the view with latest window size
        View.setViewport({ 0, 0, MainWindow.getWidth(), MainWindow.getHeight() });

        // If we are ready to render lets do so...
        if( Inspector.m_GeomCompiler.m_FinalGeom.m_Buffers[0].m_Private.get() )
        {
            auto CmdBuffer = MainWindow.getCmdBuffer();
            GeomInstance.Render(CmdBuffer, View.getW2C());
        }

        //
        // Render the Inspector
        //
        Inspector.Show();

        //
        // Render ImGUI
        //
        xgpu::tools::imgui::Render();

        //
        // Pageflip the windows
        //
        MainWindow.PageFlip();
    }

    return 0;
}

#endif