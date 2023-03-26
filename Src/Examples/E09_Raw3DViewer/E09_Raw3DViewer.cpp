
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

namespace e09
{
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

    struct runtime_geom;
    struct runtime_material;
    struct runtime_texture;
    struct runtime_material_instance;
    struct runtime_geom_instance;
}

//------------------------------------------------------------------------------------------------

struct e09::runtime_geom
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
        static_assert((int)xgpu::vertex_descriptor::format::UINT8_4D_UINT        == (int)xgeom::stream_info::format::UINT8_4D_UINT);
        static_assert((int)xgpu::vertex_descriptor::format::UINT16_4D_NORMALIZED == (int)xgeom::stream_info::format::UINT16_4D_NORMALIZED);
        static_assert((int)xgpu::vertex_descriptor::format::SINT16_4D_NORMALIZED == (int)xgeom::stream_info::format::SINT16_4D_NORMALIZED);
        static_assert((int)xgpu::vertex_descriptor::format::UINT16_3D_NORMALIZED == (int)xgeom::stream_info::format::UINT16_3D_NORMALIZED);
        static_assert((int)xgpu::vertex_descriptor::format::SINT16_3D_NORMALIZED == (int)xgeom::stream_info::format::SINT16_3D_NORMALIZED);
        static_assert((int)xgpu::vertex_descriptor::format::UINT16_2D_NORMALIZED == (int)xgeom::stream_info::format::UINT16_2D_NORMALIZED);
        static_assert((int)xgpu::vertex_descriptor::format::SINT16_2D_NORMALIZED == (int)xgeom::stream_info::format::SINT16_2D_NORMALIZED);
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
            e09::DebugMessage(Err.getCode().m_pString);
            return true;
        }

        if( auto Err = InitializeRuntime(Device); Err )
        {
            e09::DebugMessage( xgpu::getErrorMsg(Err) );
            return true;
        }

        return false;
    }

    xgeom*                                                      m_pGeom = nullptr;
    xgpu::vertex_descriptor                                     m_VertexDescriptor;
    std::array<xgpu::buffer, xgeom::max_stream_count_v>         m_Buffers;
};

//------------------------------------------------------------------------------------------------

struct e09::runtime_material
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
        ,   .m_PushConstantsSize    = sizeof(e09::push_constants)
        ,   .m_Samplers             = Samplers
        };

        if (auto Err = Device.Create(m_Pipeline, Setup); Err)
            return Err;


        return nullptr;
    }
};

//------------------------------------------------------------------------------------------------

struct e09::runtime_texture
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

struct e09::runtime_material_instance
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

        e09::push_constants PushConstants;
        PushConstants.m_L2C = L2C;
        CmdBuffer.setPushConstants( PushConstants );
    }
};

//------------------------------------------------------------------------------------------------

struct e09::runtime_geom_instance
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
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = true, .m_pLogErrorFunc = e09::DebugMessage, .m_pLogWarning = e09::DebugMessage }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err)
        return xgpu::getErrorInt(Err);

    xgpu::window MainWindow;
    if (auto Err = Device.Create(MainWindow, {}); Err)
        return xgpu::getErrorInt(Err);

    e09::runtime_geom                RuntimeGeom;
    e09::runtime_texture             DefaultTexture;
    e09::runtime_material            DefaultMaterial;
    e09::runtime_material_instance   DefaultMaterialInstance;
    e09::runtime_geom_instance       GeomInstance;

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
