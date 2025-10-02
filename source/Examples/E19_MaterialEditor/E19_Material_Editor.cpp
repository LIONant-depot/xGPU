#define NOMINMAX
#include "dependencies/xproperty/source/xcore/my_properties.h"
#include "dependencies/xstrtool/source/xstrtool.h"
#include "source/Examples/E05_Textures/E05_BitmapInspector.h"
#include "E19_Node_Core.h"
#include "dependencies/xproperty/source/examples/imgui/xPropertyImGuiInspector.h" 
#include <fstream>
#include "dependencies/xprim_geom/source/xprim_geom.h"
#include "source/tools/xgpu_xcore_bitmap_helpers.h"
#include "source/tools/xgpu_view.h"
#include <regex>
#include <algorithm>
#include <unordered_set>
#include <filesystem>

#define XRESOURCE_PIPELINE_NO_COMPILER
#include "dependencies/xresource_pipeline_v2/source/xresource_pipeline.h"
#include "source/xstrtool.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_Resources.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_PluginMgr.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_AssetMgr.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_AssetBrowser.h"

// Just include the loader here...
#include "Plugins/xmaterial.plugin/source/xmaterial_xgpu_rsc_loader.cpp"


namespace                   ed                  = ax::NodeEditor;
static ed::EditorContext*   g_pEditor           = nullptr;
static int                  g_SelectedTexture   = 0;
constexpr auto              g_2DVertShader      = std::array
{
    #include "imgui_vert.h"
};
constexpr auto              g_2DFragShader      = std::array
{
    #include "draw_frag.h"
};

//-----------------------------------------------------------------------------------

namespace e19
{
    static void Debugger(std::string_view view)
    {
        printf("%s\n", view.data());
    }

    struct draw_vert
    {
        float           m_X, m_Y, m_Z;
        float           m_U, m_V;
        std::uint32_t   m_Color;
    };

    struct vert_2d
    {
        float           m_X, m_Y;
        float           m_U, m_V;
        std::uint32_t   m_Color;
    };

    struct push_const2D
    {
        xmath::fvec2    m_Scale;
        xmath::fvec2    m_Translation;
        xmath::fvec2    m_UVScale;
    };

    struct push_constants
    {
        xmath::fmat4    m_L2C;
    };

    std::string ExtractNodeName(const std::string_view clog)
    {
        std::regex  nodeRegex(R"(N_(\d+))");
        std::smatch match;
        std::string srt(clog);

        if (std::regex_search(srt, match, nodeRegex))
        {
            return match[1].str(); // The node name
        }

        return "";
    }

    struct TextureManager
    {
        xgpu::device*               m_Device            = nullptr;
        std::vector<xgpu::texture>  m_Textures          = {};
        std::vector<xgpu::texture>  m_BgTextures        = {};
        std::vector<std::string>    m_Names             = {};   //for combo
        int                         m_CurrentIndex      = {};
        int                         m_CurrentBGIndex    = {};
        e05::bitmap_inspector       m_BMInspector       = {};

        void Init(xgpu::device& device)
        {
            m_Device = &device;
            m_Names.push_back("None"); //for default bit map
        }

        void loadTexture(const std::wstring_view path)
        {
            m_BMInspector.Load(path);

            xgpu::texture Tex;
            xgpu::texture BgTex;

            if(auto Err = xgpu::tools::bitmap::Create(Tex, *m_Device, *m_BMInspector.m_pBitmap); Err)
            {
                e05::DebugMessage(xgpu::getErrorMsg(Err));
                std::exit(xgpu::getErrorInt(Err));
            }
            if (auto Err = xgpu::tools::bitmap::Create(BgTex, *m_Device, *m_BMInspector.m_pBitmap); Err)
            {
                e05::DebugMessage(xgpu::getErrorMsg(Err));
                std::exit(xgpu::getErrorInt(Err));
            }
            m_Textures.push_back(std::move(Tex));
            m_BgTextures.push_back(std::move(BgTex));

            auto lastslash  = path.find_last_of(L"/\\");
            auto startPoint = (lastslash == std::string::npos) ? 0 : lastslash + 1;

            auto lastDot    = path.find_last_of(L".");
            size_t end      = (lastDot == std::string::npos || lastDot < startPoint) ? path.size() : lastDot;

            m_Names.push_back(xstrtool::To(path.substr(startPoint, end - startPoint)));
        }

        xgpu::texture* GetTextureByIndex(int index)
        {
            if (index <= 0 || index > m_Textures.size())
                return nullptr;

            // because 0 = "None"
            return &m_Textures[index - 1]; 
        }

        xgpu::texture* GetCurrentBGTexture(void)
        {
            // None = default texture
            if (m_CurrentBGIndex == 0) return nullptr;

            // offset by 1 because slot 0 = None
            return &m_BgTextures[m_CurrentBGIndex - 1];    
        }
    };
}

namespace e19
{
    void MeshPreviewStyle(void)
    {
        ImGui::PushStyleColor(ImGuiCol_Text,            IM_COL32(0, 0, 0, 255));           //Text black colour
        ImGui::PushStyleColor(ImGuiCol_FrameBg,         IM_COL32(200, 200, 200, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,  IM_COL32(180, 180, 180, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,   IM_COL32(160, 160, 160, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg,         IM_COL32(100, 100, 100, 255));// dropdown popup bg
        ImGui::PushStyleColor(ImGuiCol_Border,          IM_COL32(0, 0, 0, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f); // enable border
    }
}

void NodeFillColor(xmaterial_compiler::node& n, ImVec2 pos, ImVec2 size, ImU32 color, float rounding = 0, ImDrawFlags flags = ImDrawFlags_None, bool borderonly = false, ImU32 borderColor = IM_COL32(200,200,200,200))
{
    auto drawList = ed::GetNodeBackgroundDrawList(n.m_Guid.m_Value);

    ImVec2 min = pos;
    ImVec2 max = ImVec2(pos.x + size.x, pos.y + size.y);

    drawList->AddRectFilled(min, max, color, rounding, flags);
    if(borderonly)
        drawList->AddRect(min, max, borderColor, rounding, flags);
    
}


struct mesh_manager
{
    struct mesh
    {
        xgpu::buffer m_VertexBuffer {};
        xgpu::buffer m_IndexBuffer  {};
        int          m_IndexCount   {};
    };

    enum class model
    { CUBE
    , SPHERE
    , CAPSULE
    , CYLINDER
    , PLANE2D
    , ENUM_COUNT
    };

    void Init(xgpu::device& Device)
    {
        CreateCube(Device);
        CreateSphere(Device);
        CreateCapsule(Device);
        CreateCylinder(Device);
        Create2DPlane(Device);
    }

    void Rendering(xgpu::cmd_buffer& CmdBuffer, model Model)
    {
        auto& Mesh = m_Meshes[static_cast<int>(Model)];
        CmdBuffer.setBuffer(Mesh.m_VertexBuffer);
        CmdBuffer.setBuffer(Mesh.m_IndexBuffer);
        CmdBuffer.Draw(Mesh.m_IndexCount);
    }

    //CUBE
    void CreateCube(xgpu::device& Device)
    {
        const auto  Primitive   = xprim_geom::cube::Generate(4, 4, 4, 4, xprim_geom::float3{ 0.7f,0.7f,0.7f });
        mesh&       Mesh        = m_Meshes[static_cast<int>(model::CUBE)];

        Mesh.m_IndexCount = static_cast<int>(Primitive.m_Indices.size());

        if (auto Err = Device.Create(Mesh.m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(e19::draw_vert), .m_EntryCount = static_cast<int>(Primitive.m_Vertices.size()) }); Err)
            exit(xgpu::getErrorInt(Err));

        (void)Mesh.m_VertexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Vertices.size()), [&](void* pData)
        {
            auto pVertex = static_cast<e19::draw_vert*>(pData);
            for (int i = 0; i < static_cast<int>(Primitive.m_Vertices.size()); ++i)
            {
                auto&       V = pVertex[i];
                const auto& v = Primitive.m_Vertices[i];
                V.m_X = v.m_Position.m_X;
                V.m_Y = v.m_Position.m_Y;
                V.m_Z = v.m_Position.m_Z;
                V.m_U = v.m_Texcoord.m_X;
                V.m_V = v.m_Texcoord.m_Y;
                V.m_Color = ~0;
            }
        });

        if (auto Err = Device.Create(Mesh.m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = static_cast<int>(Primitive.m_Indices.size()) }); Err)
            exit(xgpu::getErrorInt(Err));

        (void)Mesh.m_IndexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Indices.size()), [&](void* pData)
        {
            auto pIndex = static_cast<std::uint32_t*>(pData);
            for (int i = 0; i < static_cast<int>(Primitive.m_Indices.size()); ++i)
            {
                pIndex[i] = Primitive.m_Indices[i];
            }
        });
    }

    //SPHERE
    void CreateSphere(xgpu::device& Device)
    {
        const auto  Primitive   = xprim_geom::uvsphere::Generate(70, 70, 1, 0.5f);
        mesh&       Mesh        = m_Meshes[static_cast<int>(model::SPHERE)];

        Mesh.m_IndexCount = static_cast<int>(Primitive.m_Indices.size());

        if (auto Err = Device.Create(Mesh.m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(e19::draw_vert), .m_EntryCount = static_cast<int>(Primitive.m_Vertices.size()) }); Err)
            exit(xgpu::getErrorInt(Err));

        (void)Mesh.m_VertexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Vertices.size()), [&](void* pData)
        {
            auto pVertex = static_cast<e19::draw_vert*>(pData);
            for (int i = 0; i < static_cast<int>(Primitive.m_Vertices.size()); ++i)
            {
                auto&       V = pVertex[i];
                const auto& v = Primitive.m_Vertices[i];
                V.m_X = v.m_Position.m_X;
                V.m_Y = v.m_Position.m_Y;
                V.m_Z = v.m_Position.m_Z;
                V.m_U = v.m_Texcoord.m_X;
                V.m_V = v.m_Texcoord.m_Y;
                V.m_Color = ~0;
            }
        });

        if (auto Err = Device.Create(Mesh.m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = static_cast<int>(Primitive.m_Indices.size()) }); Err)
            exit(xgpu::getErrorInt(Err));

        (void)Mesh.m_IndexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Indices.size()), [&](void* pData)
        {
            auto pIndex = static_cast<std::uint32_t*>(pData);
            for (int i = 0; i < static_cast<int>(Primitive.m_Indices.size()); ++i)
            {
                pIndex[i] = Primitive.m_Indices[i];
            }
        });
    }

    //CAPSULE
    void CreateCapsule(xgpu::device& Device)
    {
        const auto  Primitive   = xprim_geom::capsule::Generate(32, 32, 0.4f, 1.2f);
        mesh&       Mesh        = m_Meshes[static_cast<int>(model::CAPSULE)];

        Mesh.m_IndexCount = static_cast<int>(Primitive.m_Indices.size());

        if (auto Err = Device.Create(Mesh.m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(e19::draw_vert), .m_EntryCount = static_cast<int>(Primitive.m_Vertices.size()) }); Err)
            exit(xgpu::getErrorInt(Err));

        (void)Mesh.m_VertexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Vertices.size()), [&](void* pData)
        {
            auto pVertex = static_cast<e19::draw_vert*>(pData);
            for (int i = 0; i < static_cast<int>(Primitive.m_Vertices.size()); ++i)
            {
                auto&       V = pVertex[i];
                const auto& v = Primitive.m_Vertices[i];
                V.m_X = v.m_Position.m_X;
                V.m_Y = v.m_Position.m_Y;
                V.m_Z = v.m_Position.m_Z;
                V.m_U = v.m_Texcoord.m_X;
                V.m_V = v.m_Texcoord.m_Y;
                V.m_Color = ~0;
            }
        });

        if (auto Err = Device.Create(Mesh.m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = static_cast<int>(Primitive.m_Indices.size()) }); Err)
            exit(xgpu::getErrorInt(Err));

        (void)Mesh.m_IndexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Indices.size()), [&](void* pData)
        {
            auto pIndex = static_cast<std::uint32_t*>(pData);
            for (int i = 0; i < static_cast<int>(Primitive.m_Indices.size()); ++i)
            {
                pIndex[i] = Primitive.m_Indices[i];
            }
        });
    }

    //CYLINDER
    void CreateCylinder(xgpu::device& Device)
    {
        const auto  Primitive   = xprim_geom::cylinder::Generate(1, 64, 1.f, 0.3f, 0.3f);;
        mesh&       Mesh        = m_Meshes[static_cast<int>(model::CYLINDER)];

        Mesh.m_IndexCount = static_cast<int>(Primitive.m_Indices.size());

        if (auto Err = Device.Create(Mesh.m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(e19::draw_vert), .m_EntryCount = static_cast<int>(Primitive.m_Vertices.size()) }); Err)
            exit(xgpu::getErrorInt(Err));

        (void)Mesh.m_VertexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Vertices.size()), [&](void* pData)
        {
            auto pVertex = static_cast<e19::draw_vert*>(pData);
            for (int i = 0; i < static_cast<int>(Primitive.m_Vertices.size()); ++i)
            {
                auto&       V = pVertex[i];
                const auto& v = Primitive.m_Vertices[i];
                V.m_X = v.m_Position.m_X;
                V.m_Y = v.m_Position.m_Y;
                V.m_Z = v.m_Position.m_Z;
                V.m_U = v.m_Texcoord.m_X;
                V.m_V = v.m_Texcoord.m_Y;
                V.m_Color = ~0;
            }
        });

        if (auto Err = Device.Create(Mesh.m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = static_cast<int>(Primitive.m_Indices.size()) }); Err)
            exit(xgpu::getErrorInt(Err));

        (void)Mesh.m_IndexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Indices.size()), [&](void* pData)
        {
            auto pIndex = static_cast<std::uint32_t*>(pData);
            for (int i = 0; i < static_cast<int>(Primitive.m_Indices.size()); ++i)
            {
                pIndex[i] = Primitive.m_Indices[i];
            }
        });
    }

    void Create2DPlane(xgpu::device& Device)
    {
        mesh& Mesh = m_Meshes[static_cast<int>(model::PLANE2D)];
        Mesh.m_IndexCount = 6;

        if (auto Err = Device.Create(Mesh.m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(e19::vert_2d), .m_EntryCount = 4 }); Err)
            exit(xgpu::getErrorInt(Err));

        (void)Mesh.m_VertexBuffer.MemoryMap(0, 4, [&](void* pData)
        {
            auto pVertex = static_cast<e19::vert_2d*>(pData);
            pVertex[0] = { -100.0f, -100.0f,  0.0f, 0.0f, 0xffffffff };
            pVertex[1] = {  100.0f, -100.0f,  1.0f, 0.0f, 0xffffffff };
            pVertex[2] = {  100.0f,  100.0f,  1.0f, 1.0f, 0xffffffff };
            pVertex[3] = { -100.0f,  100.0f,  0.0f, 1.0f, 0xffffffff };
        });

        if (auto Err = Device.Create(Mesh.m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = Mesh.m_IndexCount }); Err)
            exit(xgpu::getErrorInt(Err));

        (void)Mesh.m_IndexBuffer.MemoryMap(0, Mesh.m_IndexCount, [&](void* pData)
        {
            auto            pIndex = static_cast<std::uint32_t*>(pData);
            constexpr auto  StaticIndex = std::array
            {
                2u,  1u,  0u,      3u,  2u,  0u,    // front
            };
            static_assert(StaticIndex.size() == 6);
            for (auto i : StaticIndex)
            {
                *pIndex = i;
                pIndex++;
            }
        });
    }

    std::array<mesh, static_cast<int>(mesh_manager::model::ENUM_COUNT)> m_Meshes;
};

ImColor e19::GetIconColor(const xmaterial_compiler::type_guid& type, const xmaterial_compiler::graph& g)
{
    const auto* t = g.GetType(type);
    if (!t) return ImColor(255, 255, 255); // fallback white

    return ToImColor(t->m_Color);
}

void e19::DrawPinCircle(const xmaterial_compiler::type_guid& type, const xmaterial_compiler::pin_guid& pinId, const xmaterial_compiler::graph& g, ImVec2 size)
{
    const bool    connected   = IsPinConnected(pinId, g);
    const ImColor iconColor   = GetIconColor(type, g);
    const auto    cursorPos   = ImGui::GetCursorScreenPos();
    const auto    drawList    = ImGui::GetWindowDrawList();
    const float   radius      = size.x * 0.5f;
    const ImVec2  center      = ImVec2(cursorPos.x + radius, cursorPos.y + radius);

    if (connected) drawList->AddCircleFilled(center, radius, iconColor);
    else           drawList->AddCircle(center, radius, iconColor);

    // advance layout
    ImGui::Dummy(size); 
}

bool e19::IsPinConnected(const xmaterial_compiler::pin_guid& pinId, const xmaterial_compiler::graph& g)
{
    for (auto& [cid, conn] : g.m_Connections)
    {
        if (conn->m_InputPinGuid == pinId || conn->m_OutputPinGuid == pinId)
            return true;
    }
    return false;
}

//draw ui
void DrawGraphUI(xmaterial_compiler::graph& g, ed::NodeId& lastSelectedNode, e19::TextureManager& texturemgr)
{
    ed::Begin("Material Graph Editor");

    // Draw nodes
    for (auto& [id, nPtr] : g.m_InstanceNodes)
    {
        if (nPtr == nullptr) continue;

        auto& n = *nPtr;
        if (n.isCommentNode())
        {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.75f);
            ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(255, 255, 255, 64));
            ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(255, 255, 255, 64));
        }

        ed::BeginNode(n.m_Guid.m_Value);

        // Draw node header
        ImGui::TextUnformatted(n.m_Name.c_str());
        ImGui::Dummy({ 0.f, 5.f }); //adding spacing between the header and content

        if (n.isCommentNode())
        {
            ed::Group(ImVec2(n.m_Params.m_Properties[1].m_Value.get<float>(), n.m_Params.m_Properties[2].m_Value.get<float>()));
        }
        ImGui::BeginGroup();

        // INPUTS pins
        for (auto& ip : n.m_InputPins)
        {
            ed::BeginPin(ip.m_PinGUID.m_Value, ed::PinKind::Input);
            ed::PinPivotAlignment(ImVec2(0.f, 0.5f));
            e19::DrawPinCircle(ip.m_TypeGUID, ip.m_PinGUID, g);

            ImGui::SameLine();
            ImGui::Text("%s",ip.m_Name.c_str());
            
            ed::EndPin();
            ImGui::Dummy({ 0.f, 1.f });
        }

        ImGui::EndGroup();
        if (n.m_Name == "Sampler2D")
            ImGui::SameLine(100);
        else
            ImGui::SameLine(70);
        ImGui::BeginGroup();

        // OUTPUTS
        for (auto& op : n.m_OutputPins)
        {
            ed::BeginPin(op.m_PinGUID.m_Value, ed::PinKind::Output);

            ImGui::TextUnformatted(op.m_Name.c_str());
            if (op.m_Name == "Texture(2D)")
                ImGui::SameLine(78);
            else
                ImGui::SameLine(40);

            ed::PinPivotAlignment(ImVec2(1.f, 0.5f));
            e19::DrawPinCircle(op.m_TypeGUID, op.m_PinGUID, g);

            ed::EndPin();
            ImGui::Dummy({ 0.f, 1.f });

            if (!op.m_SubElements.empty())
            {
                for (auto& sub : op.m_SubElements)
                {
                    ed::BeginPin(sub.m_PinGUID.m_Value, ed::PinKind::Output);
                    ImGui::TextUnformatted(sub.m_Name.c_str());
                    ImGui::SameLine(40);
                    
                    ed::PinPivotAlignment(ImVec2(1.f, 0.5f));
                    e19::DrawPinCircle(sub.m_TypeGUID, sub.m_PinGUID, g);

                    ed::EndPin();
                    ImGui::Dummy({ 0.f, 1.f });
                }
            }
        }
        ImGui::EndGroup();

        ed::EndNode();
        if (n.isCommentNode())
        {
            ed::PopStyleColor(2);
            ImGui::PopStyleVar();
        }
        //for sampler node only with texture selection in the node
        
        auto nodepos = ed::GetNodePosition(id.m_Value);
        n.m_Pos = { nodepos.x, nodepos.y };

        //
        // design for node
        //
        const float nysize = 26.f;
        const auto borderoutlineClr = IM_COL32(100, 100, 100, 255);
        {
            // add color to header
            auto nsize = ed::GetNodeSize(n.m_Guid.m_Value);
            if (n.isFunctionNode())
            {
                NodeFillColor(n, ed::GetNodePosition(n.m_Guid.m_Value), { nsize.x , nysize + 1 },
                    IM_COL32(64, 64, 64, 255), ed::GetStyle().NodeRounding, ImDrawFlags_RoundCornersTop, true , borderoutlineClr);
            }
            else if(n.isInputNode())
            {
                NodeFillColor(n, ed::GetNodePosition(n.m_Guid.m_Value), { nsize.x , nysize + 1 },
                    IM_COL32(22, 128, 22, 255), ed::GetStyle().NodeRounding, ImDrawFlags_RoundCornersTop, true , borderoutlineClr);
            }
            else if (n.isOutputNode())
            {
                NodeFillColor(n, ed::GetNodePosition(n.m_Guid.m_Value), { nsize.x , nysize + 1 },
                    IM_COL32(60, 60, 128, 255), ed::GetStyle().NodeRounding, ImDrawFlags_RoundCornersTop, true , borderoutlineClr);
            }
            else if (n.isCommentNode())
            {
                if (ed::BeginGroupHint(n.m_Guid.m_Value))
                {
                    auto bgAlpha = static_cast<int>(ImGui::GetStyle().Alpha * 255);
                    auto min = ed::GetGroupMin();

                    auto scspX = min.x - 8;
                    auto scspY = min.y - ImGui::GetTextLineHeightWithSpacing() + 8;
                    ImGui::SetCursorScreenPos({ scspX ,scspY });
                    ImGui::BeginGroup();
                    // Draw editable comment (persistent per node)
                    ImGui::PushID(static_cast<int>(n.m_Guid.m_Value)); // unique ID so ImGui doesn’t mix inputs
                    ImGui::SetNextItemWidth(100); // width of comment box

                    auto& props = n.m_Params.m_Properties;
                    auto& commentvalue = props[0].m_Value.get<std::string>();

                    auto& sizex = props[1].m_Value.get<float>();
                    auto& sizey = props[2].m_Value.get<float>();
                    auto commentnodesize = ed::GetNodeSize(n.m_Guid.m_Value);
                    sizex = commentnodesize.x;
                    sizey = commentnodesize.y;

                    char buf[256];
                    strncpy_s(buf, commentvalue.c_str(), sizeof(buf));
                    buf[sizeof(buf) - 1] = '\0'; // safety null-terminator

                    if (ImGui::InputTextMultiline(
                        "##Comment",
                        buf,
                        IM_ARRAYSIZE(buf),
                        ImVec2(100, 50),
                        ImGuiInputTextFlags_AllowTabInput))
                    {
                        commentvalue = buf;
                    }
                    ImGui::PopID();
                    ImGui::EndGroup();
                    
                    // Draw background frame for the comment box
                    auto drawList = ImGui::GetForegroundDrawList();
    
                    auto hintBounds = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
                    auto ImRect_Expanded = [](const ImRect& rect, float x, float y)
                    {
                        ImRect result = rect;
                        result.Min.x -= x;
                        result.Min.y -= y;
                        result.Max.x += x;
                        result.Max.y += y;
                        return result;
                    };
                    auto hintFrameBounds = ImRect_Expanded(hintBounds, 8, 4);

                    drawList->AddRectFilled(
                        hintFrameBounds.GetTL(),
                        hintFrameBounds.GetBR(),
                        IM_COL32(255, 255, 255, 64 * bgAlpha / 255), 4.0f);

                    drawList->AddRect(
                        hintFrameBounds.GetTL(),
                        hintFrameBounds.GetBR(),
                        IM_COL32(255, 255, 255, 128 * bgAlpha / 255), 4.0f);

                }
                ed::EndGroupHint();
            }
        }
        {
            if (!n.isCommentNode()) {
                // add color to content
                if (n.m_OutputPins.empty())
                {
                    auto nposition = ed::GetNodePosition(n.m_Guid.m_Value);
                    auto nsize = ed::GetNodeSize(n.m_Guid.m_Value);
                    nposition.y += nysize;
                    NodeFillColor(n, { nposition.x, nposition.y }, { nsize.x , nsize.y - nysize },
                        IM_COL32(64, 64, 64, 255), ed::GetStyle().NodeRounding, ImDrawFlags_RoundCornersBottom, true, borderoutlineClr);
                }
                else if (n.m_InputPins.empty())
                {
                    auto nposition = ed::GetNodePosition(n.m_Guid.m_Value);
                    auto nsize = ed::GetNodeSize(n.m_Guid.m_Value);
                    nposition.y += nysize;
                    NodeFillColor(n, { nposition.x, nposition.y }, { nsize.x , nsize.y - nysize },
                        IM_COL32(32, 32, 32, 255), ed::GetStyle().NodeRounding, ImDrawFlags_RoundCornersBottom, true, borderoutlineClr);
                }
                else if (n.isFunctionNode())
                {
                    auto nposition = ed::GetNodePosition(n.m_Guid.m_Value);
                    auto nsize = ed::GetNodeSize(n.m_Guid.m_Value);
                    nposition.y += nysize;
                    NodeFillColor(n, { nposition.x, nposition.y }, { (nsize.x / 2) + 1 , nsize.y - nysize },
                        IM_COL32(64, 64, 64, 255), ed::GetStyle().NodeRounding, ImDrawFlags_RoundCornersBottomLeft, true, borderoutlineClr);
                    NodeFillColor(n, { nposition.x + (nsize.x / 2), nposition.y }, { nsize.x / 2 , nsize.y - nysize },
                        IM_COL32(32, 32, 32, 255), ed::GetStyle().NodeRounding, ImDrawFlags_RoundCornersBottomRight, true, borderoutlineClr);
                }
            }
        }

        //draw a red filter rect to show which nodes has err
        if (n.m_HasErrMsg)
        {
            NodeFillColor(n, ed::GetNodePosition(n.m_Guid.m_Value), ed::GetNodeSize(n.m_Guid.m_Value),
                IM_COL32(255, 0, 0, 150), ed::GetStyle().NodeRounding, ImDrawFlags_RoundCornersAll);
        }
        
        //for error msg visualize
        ed::Suspend();
        if (n.m_HasErrMsg && !n.m_ErrMsg.empty() && ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", n.m_ErrMsg.c_str());
        }
        ed::Resume();
        
        //
        //drawing for floating rect for editable props
        //
        float offsetX = 70.f;
        float offsetY = 28.f;
        for (auto& ip : n.m_InputPins)
        {
            if (!e19::IsPinConnected(ip.m_PinGUID, g) && ip.m_ParamIndex >= 0)
            {
                ImVec2 widgetPos = ImVec2(n.m_Pos.m_X - offsetX, n.m_Pos.m_Y + offsetY);
                ImGui::SetCursorScreenPos(widgetPos);
                if (ip.m_ParamIndex != -1)
                {
                    auto* type = g.GetType(ip.m_TypeGUID);
                    assert(type);
                    assert(n.m_Params.m_Properties.empty() == false);

                    if (n.m_Params.m_Properties[ip.m_ParamIndex].m_Value.m_pType->m_GUID == xproperty::settings::var_type<float>::guid_v ||
                        n.m_Params.m_Properties[ip.m_ParamIndex].m_Value.m_pType->m_GUID == xproperty::settings::var_type<int>::guid_v)
                    {
                        auto& prop = n.m_Params.m_Properties[ip.m_ParamIndex];
                        
                        NodeFillColor(n, { widgetPos.x - 23.f ,widgetPos.y + 1.3f }, { 85,18 },
                            IM_COL32(64, 64, 64, 255), ed::GetStyle().NodeRounding, ImDrawFlags_RoundCornersNone, true, borderoutlineClr);
                        
                        float textHeight = ImGui::GetTextLineHeight();
                        float dragHeight = ImGui::GetFrameHeight(); 
                        float offsetY = (dragHeight - textHeight) * 0.5f;
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 16.f);
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offsetY + 2.f);
                        ImGui::Text(std::format("{}",ip.m_Name[0]).c_str());

                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(45);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 2.f);
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 1.6f);
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(32, 32, 32, 200));
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 1.f));

                        //somehow when using multiple same nodes drag float cause issue so the id make it more unique
                        std::string addons = std::to_string(n.m_Guid.m_Value);

                        if (n.m_Params.m_Properties[ip.m_ParamIndex].m_Value.m_pType->m_GUID == xproperty::settings::var_type<float>::guid_v)
                        {
                            float& value = prop.m_Value.get<float>();
                            ImGui::DragFloat(("##" + ip.m_Name + addons).c_str(), &value, 0.01f);
                        }
                        else if (n.m_Params.m_Properties[ip.m_ParamIndex].m_Value.m_pType->m_GUID == xproperty::settings::var_type<int>::guid_v)
                        {
                            int& value = prop.m_Value.get<int>();
                            ImGui::DragInt(("##" + ip.m_Name + addons).c_str(), &value, 0.01f);
                        }
                        else
                        {
                            assert(false);
                        }
                        ImGui::PopStyleVar();
                        ImGui::PopStyleColor();

                        auto* draw_list = ImGui::GetWindowDrawList();

                        //center of the circle
                        ImVec2 center = ImVec2(widgetPos.x + 53, widgetPos.y + 10);
                        float rad = 5.f;

                        draw_list->AddCircleFilled(center, rad, IM_COL32(64, 200, 64, 255));

                        ImVec2 line_end = ImVec2(center.x + 25.0f, center.y); // 25 px to the right
                        draw_list->AddLine(ImVec2(center.x + rad, center.y), line_end, IM_COL32(64, 200, 64, 255), 2.0f);

                    }
                    else if (n.m_Params.m_Properties[ip.m_ParamIndex].m_Value.m_pType->m_GUID == xproperty::settings::var_type<std::string>::guid_v)
                    {
                        NodeFillColor(n, { widgetPos.x - 23.f ,widgetPos.y + 1.3f }, { 85,18 },
                            IM_COL32(64, 64, 64, 255), ed::GetStyle().NodeRounding, ImDrawFlags_RoundCornersNone, true, borderoutlineClr);

                        xproperty::settings::context Context;
                        // texturemgr.m_Names
                        if (n.m_pCustomInput) n.m_pCustomInput(n, g_SelectedTexture, e10::g_LibMgr, Context);

                        auto* draw_list = ImGui::GetWindowDrawList();
                        //center of the circle
                        ImVec2 center = ImVec2(widgetPos.x + 53, widgetPos.y + 10);
                        float rad = 5.f;
                        draw_list->AddCircleFilled(center, rad, IM_COL32(64, 200, 64, 255));
                        //draw connection line
                        ImVec2 line_end = ImVec2(center.x + 25.0f, center.y); // 25 px to the right
                        draw_list->AddLine(ImVec2(center.x + rad, center.y), line_end, IM_COL32(64, 200, 64, 255), 2.0f);
                    }
                }
            }
            offsetY += 21.f;
        }
        
    }

    // Draw existing links
    for (auto& [cid, cPtr] : g.m_Connections)
    {
        const auto& c = *cPtr;
        
        //fill with color for link by type(eg. float is blue etc)
        auto* outNode = g.FindNodeByPin(c.m_OutputPinGuid);
        if (!outNode) continue; //no node found

        int idx, sub;
        bool dummy;
        const auto* outPin = g.FindPinConst(*outNode, c.m_OutputPinGuid, dummy, idx, sub);
        ImColor color = (outPin) ? e19::GetIconColor(outPin->m_TypeGUID, g)
            : ImColor(200, 200, 200);//default color
        
        //for highlighting link with the selected node
        bool highlight = false;
        if (lastSelectedNode.Get() != 0)
        {
            xmaterial_compiler::node* selectedNode = g.m_InstanceNodes[xmaterial_compiler::node_guid{ lastSelectedNode.Get() }].get();
            if (selectedNode &&
                (selectedNode == g.FindNodeByPin(c.m_InputPinGuid) ||
                    selectedNode == g.FindNodeByPin(c.m_OutputPinGuid)))
            {
                highlight = true;
            }
        }
        if (highlight)
        {
            color = ImColor(255, 255, 0); // bright yellow highlight
            ed::Link(cid.m_Value, c.m_OutputPinGuid.m_Value, c.m_InputPinGuid.m_Value, color, 3.0f);
        }
        else
            ed::Link(cid.m_Value, c.m_OutputPinGuid.m_Value, c.m_InputPinGuid.m_Value, color);

    }
    
    // --- Handle creating links ---
    if (ed::BeginCreate())
    {
        ed::PinId a, b;
        if (ed::QueryNewLink(&a, &b))
        {
            // Determine which side is input/output
            xmaterial_compiler::pin_guid A{ a.Get() };
            xmaterial_compiler::pin_guid B{ b.Get() };

            xmaterial_compiler::node* nA = g.FindNodeByPin(A);
            xmaterial_compiler::node* nB = g.FindNodeByPin(B);

            bool aIsInput = false, bIsInput = false; 
            int idx = -1, sub = -1;

            const xmaterial_compiler::pin* pA = nA ? g.FindPinConst(*nA, A, aIsInput, idx, sub) : nullptr;
            const xmaterial_compiler::pin* pB = nB ? g.FindPinConst(*nB, B, bIsInput, idx, sub) : nullptr;

            // Normalize so have (outPin, inPin)
            const xmaterial_compiler::pin* outP = nullptr;
            const xmaterial_compiler::pin* inP = nullptr;
            xmaterial_compiler::pin_guid outId{}, inId{};

            //handle output and input pins
            if (pA && pB)
            {
                if (!aIsInput && bIsInput) 
                { 
                    outP = pA; 
                    inP = pB; 
                    outId = A; 
                    inId = B; 
                }
                else if (!bIsInput && aIsInput) 
                { 
                    outP = pB; 
                    inP = pA; 
                    outId = B; 
                    inId = A; 
                }
            }

            if (outP && inP)
            {
                //check for type compatibility
                const bool typecompatible = e19::IsPinCompatible(*outP, *inP, g);

                //check if input has already been connect by another link
                //if yes replace it act same as unity or unreal
                xmaterial_compiler::connection* existingConn = nullptr;
                xmaterial_compiler::connection_guid existingConnId{};

                for (const auto& [connid, conn] : g.m_Connections)
                {
                    if (conn.get()->m_InputPinGuid == inId)
                    {
                        existingConn = conn.get();
                        existingConnId = connid;
                        break;
                    }
                }

                if (typecompatible)
                {
                    if (ed::AcceptNewItem(ImColor(0, 255, 0), 2.0f))
                    {

                        // if input already connected, remove old link first
                        if (existingConn)
                        {
                            ed::DeleteLink(existingConn->m_Guid.m_Value); // remove visually
                            g.RemoveConnection(existingConnId);           // remove from graph
                        }

                        auto& link = g.createConnection();
                        link.m_OutputPinGuid = outId;
                        link.m_InputPinGuid = inId;
                        if (auto* inNode = g.FindNodeByPin(inId))
                        {
                            int idx = inNode->getInputPinIndex(inId);
                            if (idx >= 0)
                            {
                                inNode->m_InputPins[idx].m_ConnectionGUID = link.m_Guid;
                            }
                        }
                    }
                }
                else
                {
                    // Visual reject for incompatible types
                    ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                }
            }
        }
    }
    ed::EndCreate();

    // --- Handle deleting links ---
    if (ed::BeginDelete())
    {
        ed::LinkId lid;
        while (ed::QueryDeletedLink(&lid))
        {
            if (ed::AcceptDeletedItem())
            {
                xmaterial_compiler::connection_guid cg{ lid.Get() };
                g.RemoveConnection(cg);
            }
        }
    }
    ed::EndDelete();
    

    auto PopUpPos = ImGui::GetMousePos();
    static ed::NodeId nodecontext = 0;
    ed::Suspend();

    if (ImGui::IsWindowHovered())
    {
        if (ed::ShowBackgroundContextMenu())
        {
            ImGui::OpenPopup("CreateNodePopup");
        }
        else if(ed::ShowNodeContextMenu(&nodecontext))
        {
            ImGui::OpenPopup("Node Context Menu");
        }
    }

    ed::Resume();
    
    //delete nodes
    ed::Suspend();
    if (ImGui::BeginPopup("Node Context Menu"))
    {
        xmaterial_compiler::node_guid nodeGuidcontext{ nodecontext.Get() };
        auto it = g.m_InstanceNodes.find(nodeGuidcontext);

        if (it != g.m_InstanceNodes.end())
        {
            auto nodeinfo = it->second.get();

            ImGui::TextUnformatted(nodeinfo->m_Name.c_str());
            ImGui::Separator();

            if (ImGui::MenuItem("Delete Node"))
            {
                //remove all connections linked to this deleting node
                std::vector<xmaterial_compiler::connection_guid> ctoRemove;
                for (auto& [cid, cptr] : g.m_Connections)
                {
                    if (g.FindNodeByPin(cptr->m_InputPinGuid) == nodeinfo ||
                        g.FindNodeByPin(cptr->m_OutputPinGuid) == nodeinfo)

                        ctoRemove.push_back(cid);
                }
                for (auto& cid : ctoRemove)
                    g.RemoveConnection(cid);

                if (lastSelectedNode.Get() == nodeGuidcontext.m_Value)
                {
                    //lastSelectedNode = 0;
                    ed::ClearSelection();
                }

                //remove the node itself
                g.RemoveNode(nodeGuidcontext);
                ImGui::CloseCurrentPopup();
            }
        }
        else
            ImGui::Text("Unknown node: %p", nodecontext.AsPointer());

        ImGui::EndPopup();
    }

    //create nodes
    if (ImGui::BeginPopup("CreateNodePopup"))
    {
        for (auto& [prefabId, prefabPtr] : g.m_PrefabNodes)
        {
            if (ImGui::MenuItem(prefabPtr->m_Name.c_str()))
            {
                auto& n = g.CreateNode(prefabId);
                n.m_Pos = { PopUpPos.x, PopUpPos.y };
                ed::SetNodePosition(n.m_Guid.m_Value, ImVec2(n.m_Pos.m_X, n.m_Pos.m_Y));
            }
        }
        ImGui::EndPopup();
    }
    ed::Resume();
    ed::End();
}

struct selected_descriptor
{
    bool empty() const
    {
        return m_InfoGUID.empty();
    }

    void clear()
    {
        m_InfoGUID.clear();
        m_LibraryGUID.clear();
        m_Log.reset();
        m_DescriptorPath.clear();
        m_ResourcePath.clear();

        m_Log = std::make_shared<e10::compilation::historical_entry::log>(e10::compilation::historical_entry::communication{ .m_Result = e10::compilation::historical_entry::result::SUCCESS });
    }

    void GeneratePaths(const std::wstring& InfoPath)
    {
        m_DescriptorPath = InfoPath;
        m_DescriptorPath = m_DescriptorPath.replace(InfoPath.find(L"info.txt"), std::wstring_view(L"info.txt").length(), L"Descriptor.txt");
        m_ResourcePath   = m_DescriptorPath.substr(0, m_DescriptorPath.rfind(L'\\') - sizeof("desc"));
        m_LogPath        = std::format(L"{}.log", m_ResourcePath);

        //
        // Finish generating the resource path
        //
        size_t pos = m_ResourcePath.rfind(L"Descriptors");
        assert(pos != std::wstring::npos);


        assert(m_DescriptorPath[pos - 1] == '\\');
        if (m_DescriptorPath[pos - 2] == L'e'
            && m_DescriptorPath[pos - 3] == L'h'
            && m_DescriptorPath[pos - 4] == L'c'
            && m_DescriptorPath[pos - 5] == L'a'
            && m_DescriptorPath[pos - 6] == L'C'
            && m_DescriptorPath[pos - 7] == L'\\')
        {
            m_LogPath.replace     (pos, sizeof("Descriptors"), L"Resources\\Logs\\");
            m_ResourcePath.replace(pos, sizeof("Descriptors"), L"Resources\\Platforms\\WINDOWS\\");
            
        }
        else
        {
            m_LogPath.replace     (pos, sizeof("Descriptors"), L"Cache\\Resources\\Logs\\");
            m_ResourcePath.replace(pos, sizeof("Descriptors"), L"Cache\\Resources\\Platforms\\WINDOWS\\");
        }
    }

    xrsc::material_ref                                          m_MaterialRef       = {};
    e10::library::guid                                          m_LibraryGUID       = {};
    xresource::full_guid                                        m_InfoGUID          = {};
    std::shared_ptr<e10::compilation::historical_entry::log>    m_Log               = {};
    std::wstring                                                m_DescriptorPath    = {};
    std::wstring                                                m_ResourcePath      = {};
    std::wstring                                                m_LogPath           = {};
    bool                                                        m_bReload           = {};
    bool                                                        m_bErrors           = {};
};

void PipelineReload(const xmaterial_compiler::graph& g, xgpu::device& Device, xgpu::vertex_descriptor& vd, xgpu::pipeline& material, xgpu::pipeline_instance& materialInst, selected_descriptor& SelcDesc, xresource::mgr& Mgr )
{
    //destroy old pipeline and pipeline instance
    //after compile a few time will cause error hence need to call destroy 
    Device.Destroy(std::move(material));
    Mgr.ReleaseRef(SelcDesc.m_MaterialRef);
    SelcDesc.m_MaterialRef.m_Instance = SelcDesc.m_InfoGUID.m_Instance;

    //
    // Material
    //
    if ( auto pRef = Mgr.getResource(SelcDesc.m_MaterialRef); pRef )
    {
        g.m_shaderDetail.serializeShaderDetails(true, std::format(L"{}/shader_detail.txt", SelcDesc.m_LogPath));

        xgpu::shader VertexShader;
        {
            auto RawData = xgpu::shader::setup::raw_data
            { std::array
                {
                    #include "draw_vert.h"
                }
            };

            xgpu::shader::setup Setup
            { .m_Type = xgpu::shader::type::bit::VERTEX
            , .m_Sharer = RawData
            };
            if (auto Err = Device.Create(VertexShader, Setup); Err)
                exit(xgpu::getErrorInt(Err));
        }

        std::vector<xgpu::pipeline::sampler> SL;
        SL.resize(g.m_shaderDetail.m_Textures.size());

        auto Shaders  = std::array<const xgpu::shader*, 2>{ &pRef->getShader(), &VertexShader};
        auto Samplers = std::array{ xgpu::pipeline::sampler{} };
        auto Setup    = xgpu::pipeline::setup
        { .m_VertexDescriptor  = vd
        , .m_Shaders           = Shaders
        , .m_PushConstantsSize = sizeof(e19::push_constants)
        , .m_Samplers          = SL
        };

        if (auto Err = Device.Create(material, Setup); Err)
            exit(xgpu::getErrorInt(Err));
    }
}

int E19_Example()
{
    //create vulkan instance
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = true, .m_pLogErrorFunc = e19::Debugger }); Err)
        return xgpu::getErrorInt(Err);

    //create device
    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err) //render and swapchain
        return xgpu::getErrorInt(Err);

    //create window
    xgpu::window MainWindow;
    if (auto Err = Device.Create(MainWindow, {}); Err)
        return xgpu::getErrorInt(Err);
    //
    //setup vertex descriptor for mesh
    //
    xgpu::vertex_descriptor VertexDescriptor;
    {
        auto Attributes = std::array
        { xgpu::vertex_descriptor::attribute
          { .m_Offset = offsetof(e19::draw_vert, m_X)
          , .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D
          }
        , xgpu::vertex_descriptor::attribute
          { .m_Offset = offsetof(e19::draw_vert, m_U)
          , .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
          }
        , xgpu::vertex_descriptor::attribute
          { .m_Offset = offsetof(e19::draw_vert, m_Color)
          , .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED
          }
        };
        auto Setup = xgpu::vertex_descriptor::setup
        { .m_VertexSize = sizeof(e19::draw_vert)
        , .m_Attributes = Attributes
        };

        if (auto Err = Device.Create(VertexDescriptor, Setup); Err)
            return xgpu::getErrorInt(Err);
    }
    
    //
    //setup 2D vertex descriptor for background
    //
    xgpu::pipeline Pipeline2D;
    {
        xgpu::vertex_descriptor VertexDescriptor2D;
        {
            auto Attributes = std::array
            {
                xgpu::vertex_descriptor::attribute
                { .m_Offset = offsetof(e19::vert_2d, m_X)
                , .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
                }
            ,   xgpu::vertex_descriptor::attribute
                { .m_Offset = offsetof(e19::vert_2d, m_U)
                , .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
                }
            ,   xgpu::vertex_descriptor::attribute
                { .m_Offset = offsetof(e19::vert_2d, m_Color)
                , .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED
                }
            };
            auto Setup = xgpu::vertex_descriptor::setup
            { .m_VertexSize = sizeof(e19::vert_2d)
            , .m_Attributes = Attributes
            };

            if (auto Err = Device.Create(VertexDescriptor2D, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader FragmentShader2D;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::FRAGMENT
            , .m_Sharer = xgpu::shader::setup::raw_data{ g_2DFragShader }
            };
            if (auto Err = Device.Create(FragmentShader2D, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader VertexShader2D;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::VERTEX
            , .m_Sharer = xgpu::shader::setup::raw_data{g_2DVertShader}
            };

            if (auto Err = Device.Create(VertexShader2D, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        auto Shaders    = std::array<const xgpu::shader*, 2>{ &FragmentShader2D, &VertexShader2D };
        auto Samplers   = std::array{ xgpu::pipeline::sampler{} };
        auto Setup      = xgpu::pipeline::setup
        { .m_VertexDescriptor     = VertexDescriptor2D
        , .m_Shaders              = Shaders
        , .m_PushConstantsSize    = sizeof(e19::push_const2D)
        , .m_Samplers             = Samplers
        , .m_DepthStencil         = {.m_bDepthTestEnable = false }
        };

        if (auto Err = Device.Create(Pipeline2D, Setup); Err)
            return xgpu::getErrorInt(Err);
    }

    //
    // Asset Mgr
    //
    resource_mgr_user_data  ResourceMgrUserData;
    xresource::mgr          ResourceMgr;
    ResourceMgr.Initiallize();

    e10::assert_browser AsserBrowser;
    selected_descriptor SelectedDescriptor;
    auto                CallBackForCompilation = [&](e10::library_mgr& LibMgr, e10::library::guid gLibrary, xresource::full_guid gCompilingEntry, std::shared_ptr<e10::compilation::historical_entry::log>& LogInformation)
    {
        // Filter by our entry...
        if (SelectedDescriptor.m_InfoGUID == gCompilingEntry)
        {
            if (SelectedDescriptor.m_Log.get() != LogInformation.get())
                SelectedDescriptor.m_Log = LogInformation;

            assert(SelectedDescriptor.m_Log);

            //
            // Check if we are done compiling
            //
            e10::compilation::historical_entry::result Results;
            {
                xcontainer::lock::scope lk(*SelectedDescriptor.m_Log);
                Results = SelectedDescriptor.m_Log->get().m_Result;
            }

            // If we are successful we should reload the texture
            if (Results == e10::compilation::historical_entry::result::SUCCESS_WARNINGS || Results == e10::compilation::historical_entry::result::SUCCESS)
            {
                SelectedDescriptor.m_bReload = true;
                SelectedDescriptor.m_bErrors = false;
            }
            else
            {
                SelectedDescriptor.m_bErrors = true;
            }
        }
    };
    e10::g_LibMgr.m_OnCompilationState.Register(CallBackForCompilation);

    //
    // Set the project path
    //
    {
        TCHAR szFileName[MAX_PATH];
        GetModuleFileName(NULL, szFileName, MAX_PATH);

        std::wcout << L"Full path: " << szFileName << L"\n";
        if (auto I = xstrtool::findI(std::wstring{ szFileName }, { L"xGPU" }); I != std::string::npos)
        {
            I += 4; // Skip the xGPU part
            szFileName[I] = 0;
            std::wcout << L"Found xGPU at: " << szFileName << L"\n";

            TCHAR LIONantProject[] = L"\\dependencies\\xresource_pipeline_example.lion_project";
            for (int i = 0; szFileName[I++] = LIONantProject[i]; ++i);

            std::wcout << "Project Path: " << szFileName << "\n";

            //
            // Open the project
            //
            if (auto Err = e10::g_LibMgr.OpenProject(szFileName); Err)
            {
                e19::Debugger(Err.getMessage());
                return 1;
            }

            //
            // Set the path for the resources
            //
            ResourceMgrUserData.m_Device = Device;
            ResourceMgr.setUserData(&ResourceMgrUserData, false);
            ResourceMgr.setRootPath(std::format(L"{}//Cache//Resources//Platforms//Windows", e10::g_LibMgr.m_ProjectPath));
        }
    }

    //
    // Create mesh mgr
    //
    mesh_manager                MeshManager             = {};
    xgpu::pipeline              material                = {};
    xgpu::pipeline_instance     material_instance       = {};
    xgpu::texture               defaulttexture          = {};
    e19::TextureManager         texturemgr              = {};
    xmaterial_compiler::graph   g                       = {};

    MeshManager.Init(Device);
    texturemgr.Init(Device);
    texturemgr.loadTexture(L"SheKnewMe.tga");

    mesh_manager::model CurrentModel = mesh_manager::model::CUBE;

    //
    //Create Background material
    //
    xgpu::texture BgTexture;
    xgpu::pipeline_instance BackGroundMaterialInstance;
    {
        if (auto Err = xgpu::tools::bitmap::Create(BgTexture, Device, xbitmap::getDefaultBitmap()); Err)
        {
            e19::Debugger(xgpu::getErrorMsg(Err));
            std::exit(xgpu::getErrorInt(Err));
        }

        auto Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{BgTexture} };
        auto setup = xgpu::pipeline_instance::setup
        {
            .m_PipeLine = Pipeline2D
        ,	.m_SamplersBindings = Bindings
        };

        if (auto Err = Device.Create(BackGroundMaterialInstance, setup); Err)
        {
            e19::Debugger(xgpu::getErrorMsg(Err));
            std::exit(xgpu::getErrorInt(Err));
        }

    }

    //
    // setup Imgui interface
    //
    xgpu::tools::imgui::CreateInstance(MainWindow);
    ed::NodeId LastSelectedNode = {};

    //Editor node setting
    ed::Config config;
    config.SettingsFile = "";   //disable .json file for ed::editor
    g_pEditor = ed::CreateEditor(&config);

    //graph prefab nodes creation
    
    g.CreateGraph(g);

    xgpu::tools::view   View        = {};
    xmath::radian3      Angles      = {};
    float               Distance    = 2;

    View.setFov(60_xdeg);

    //
    //create input devices
    //
    xgpu::mouse         Mouse;
    xgpu::keyboard      Keyboard;
    Instance.Create(Mouse, {});
    Instance.Create(Keyboard, {});

    //
    // Create the inspector window
    //
    xproperty::inspector Inspector("Property");

    //
    // Main Loop
    //
    while (Instance.ProcessInputEvents())
    {
        if (xgpu::tools::imgui::BeginRendering(true)) continue;
        ed::SetCurrentEditor(g_pEditor);
        ImGui::Begin("Material Graph");
        DrawGraphUI(g, LastSelectedNode, texturemgr);

        ImGui::End();

        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("\xEE\x98\xAB Home\xee\xa5\xb2"))
            {
                if (ImGui::MenuItem("Resource Browser", "Ctrl-Space"))
                {
                    // Handle Open action
                    // Opens a popup window and allows you to select a Texture from there
                    // The texture is then loaded and the material is updated
                    // The material is then updated
                    AsserBrowser.Show(true);
                }

                ImGui::Separator();
                {
                    bool bDisableSave = !e10::g_LibMgr.isReadyToSave() && SelectedDescriptor.m_InfoGUID.empty();
                    if (bDisableSave) ImGui::BeginDisabled();
                    if (ImGui::MenuItem("\xEE\x9D\x8E Save Graph", "Ctrl+S"))
                    {
                        xproperty::settings::context Context;
                        e10::g_LibMgr.Save(Context);

                        if (SelectedDescriptor.m_InfoGUID.empty() == false )
                        {
                            g.serialize(g, false, SelectedDescriptor.m_DescriptorPath);
                        }
                    }
                    if (bDisableSave) ImGui::EndDisabled();
                }

            ImGui::EndMenu();
            }

            ImGui::SameLine(410);

            if (false == SelectedDescriptor.m_InfoGUID.empty() && g.m_InstanceNodes.empty() == false )
            {
                ImGui::Separator();
                xcontainer::lock::scope lk(*SelectedDescriptor.m_Log);
                auto& Log = SelectedDescriptor.m_Log->get();

                const bool Disable = Log.m_Result == e10::compilation::historical_entry::result::COMPILING_WARNINGS
                                  || Log.m_Result == e10::compilation::historical_entry::result::COMPILING;
                if (Disable) ImGui::BeginDisabled();
                if (ImGui::Button("\xEF\x96\xB0 Compile "))
                {
                    g.serialize(g, false, SelectedDescriptor.m_DescriptorPath);

                    for (auto& [id, node] : g.m_InstanceNodes)
                    {
                        auto& n = *node;
                        n.m_HasErrMsg = false;
                        n.m_ErrMsg.clear();
                    }
                }
                if (Disable) ImGui::EndDisabled();

                std::uint32_t Color;
                {
                    switch (Log.m_Result)
                    {
                    case e10::compilation::historical_entry::result::COMPILING_WARNINGS:
                        Color = IM_COL32(255, 255, 0, 255);
                        break;
                    case e10::compilation::historical_entry::result::COMPILING:
                        Color = IM_COL32(0, 255, 0, 255);
                        break;
                    case e10::compilation::historical_entry::result::FAILURE:
                        Color = IM_COL32(255, 170, 140, 255);
                        break;
                    case e10::compilation::historical_entry::result::SUCCESS_WARNINGS:
                        Color = IM_COL32(255, 255, 0, 255);
                        break;
                    case e10::compilation::historical_entry::result::SUCCESS:
                        Color = IM_COL32(255, 255, 255, 255);
                        break;
                    }
                }

                ImGui::PushStyleColor(ImGuiCol_Text, Color);
                if (ImGui::Button("Feedback:\xee\xa5\xb2"))
                {
                    // Get button position and size
                    ImVec2 button_pos = ImGui::GetItemRectMin();
                    ImVec2 button_size = ImGui::GetItemRectSize();

                    // Set popup position just below the button
                    ImGui::SetNextWindowPos(ImVec2(button_pos.x, button_pos.y + button_size.y));
                    ImGui::OpenPopup("Feedback");
                }
                ImGui::PopStyleColor();

                if (ImGui::BeginPopup("Feedback"))
                {
                    ImGui::BeginChild("###Feedback-Child", ImVec2(600, 300));
                    ImGui::PushTextWrapPos(600);

                    if (Log.m_Log.empty() == false)
                    {
                        size_t start = 0;
                        while (start < Log.m_Log.length()) 
                        {
                            const std::string_view view{ Log.m_Log };
                            size_t end = view.find('\n', start);
                            if (end == std::string_view::npos) end = view.size();
                            std::string_view line = view.substr(start, end - start);

                            if (xstrtool::findI( line, "ERROR:") != std::string::npos)
                            {
                                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
                                ImGui::TextUnformatted(std::string(line).c_str());
                                ImGui::PopStyleColor();

                                std::string nodeName = e19::ExtractNodeName(line);
                                if (!nodeName.empty() && nodeName != "Final FragOut")
                                {
                                    for (auto& [id, node] : g.m_InstanceNodes)
                                    {
                                        auto& n = *node;
                                        if (std::to_string(n.m_Guid.m_Value) == nodeName)
                                        {
                                            n.m_HasErrMsg = true;
                                            n.m_ErrMsg    = line;
                                        }
                                    }
                                }
                            }
                            else
                            {
                                ImGui::TextUnformatted(std::string(line).c_str());
                            }

                            start = end + 1;
                        }
                    }
                    ImGui::PopTextWrapPos();
                    ImGui::EndChild();
                    ImGui::EndPopup();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gives information about the compilation process");

                if (Log.m_Log.empty() == false)
                {
                    ImGui::Text("%s", std::string(xstrtool::getLastLine(Log.m_Log)).c_str());
                }
            }

            ImGui::EndMainMenuBar();
        }


        std::array<ed::NodeId, 2> SelectedNodes;
        int nSelected = ed::GetSelectedNodes(SelectedNodes.data(), static_cast<int>(SelectedNodes.size()));
        ed::SetCurrentEditor(nullptr);

        //
        // Re-load material if we have to
        //
        if (SelectedDescriptor.m_bReload)
        {
            SelectedDescriptor.m_bReload = false;
            PipelineReload(g,Device, VertexDescriptor, material, material_instance, SelectedDescriptor, ResourceMgr);
            {
                xgpu::texture* curTex = &defaulttexture;
                std::vector<xgpu::pipeline_instance::sampler_binding> Bindings;
                if (!g.m_shaderDetail.m_Textures.empty())
                {
                    for (int i = 0u; i < g.m_shaderDetail.m_Textures.size(); ++i)
                    {
                        auto& Texture = g.m_shaderDetail.m_Textures[i];

                        //Search for this texture
                        for (int j = 0u; j < texturemgr.m_Names.size(); j++)
                        {
                            if (texturemgr.m_Names[j] == Texture)
                            {
                                curTex = texturemgr.GetTextureByIndex(j);
                            }
                        }
                        //bindings 
                        Bindings.emplace_back(xgpu::pipeline_instance::sampler_binding{ *curTex });
                    }
                }

                Device.Destroy(std::move(material_instance));
                if (auto Err = Device.Create(material_instance,{ .m_PipeLine = material, .m_SamplersBindings = Bindings }); Err)
                {
                    exit(xgpu::getErrorInt(Err));
                }

            }
        }

        //
        // Preview window
        //
        {
            ImGui::Begin("Mesh Preview");

            //
            // render background
            //
            xgpu::tools::imgui::AddCustomRenderCallback([&](const ImVec2& windowPos, const ImVec2& windowSize)
            {
                auto CmdBufferRef = MainWindow.getCmdBuffer();

                {
                    e19::push_const2D pc;
                    pc.m_Scale =
                    { (150 * 2.0f) / windowSize.x
                    , (150 * 2.0f) / windowSize.y
                    };
                    pc.m_Translation.setup(0);
                    pc.m_UVScale = { 100.0f,100.0f };

                    CmdBufferRef.setPipelineInstance(BackGroundMaterialInstance);
                    CmdBufferRef.setPushConstants(pc);
                    MeshManager.Rendering(CmdBufferRef, mesh_manager::model::PLANE2D);
                }
            });

            //
            // mesh selection
            //
            static int selected = 0;
            const char* meshSelection[] = { "Cube", "Sphere","Capsule","Cylinder" };
            e19::MeshPreviewStyle();
            if (ImGui::Button("\xEE\xAF\x92 Meshes"))
            {
                ImVec2 button_pos = ImGui::GetItemRectMin();
                ImVec2 button_size = ImGui::GetItemRectSize();
                ImGui::SetNextWindowPos(ImVec2(button_pos.x, button_pos.y + button_size.y));
                ImGui::OpenPopup("Meshes");
            }

            if (ImGui::BeginPopup("Meshes"))
            {
                for (int n = 0; n < IM_ARRAYSIZE(meshSelection); n++)
                {
                    bool is_selected = (selected == n);
                    if (ImGui::Selectable(meshSelection[n], is_selected))
                    {
                        selected = n;
                        CurrentModel = static_cast<mesh_manager::model>(n);
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndPopup();
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(6);

            //
            // Background texture selection
            //
            e19::MeshPreviewStyle();
            ImGui::SameLine();
            if (ImGui::Button("\xEE\xBC\x9F BackGround"))
            {
                ImVec2 button_pos = ImGui::GetItemRectMin();
                ImVec2 button_size = ImGui::GetItemRectSize();
                ImGui::SetNextWindowPos(ImVec2(button_pos.x, button_pos.y + button_size.y));
                ImGui::OpenPopup("BackGround");
            }

            if (ImGui::BeginPopup("BackGround"))
            {
                for (int i = 0; i < texturemgr.m_Names.size(); ++i)
                {
                    bool selected = (texturemgr.m_CurrentBGIndex == i);
                    if (ImGui::Selectable(texturemgr.m_Names[i].c_str(), selected))
                    {
                        texturemgr.m_CurrentBGIndex = i;

                        // Update binding immediately
                        xgpu::texture* curTex = texturemgr.GetCurrentBGTexture();
                        auto Bindings = (curTex)
                            ? std::array{ xgpu::pipeline_instance::sampler_binding{*curTex} }
                        : std::array{ xgpu::pipeline_instance::sampler_binding{BgTexture} }; // fallback default checkerboard

                        //need to destroy material_instance if not complain
                        Device.Destroy(std::move(BackGroundMaterialInstance));
                        if (auto Err = Device.Create(BackGroundMaterialInstance, { .m_PipeLine = Pipeline2D, .m_SamplersBindings = Bindings }); Err)
                            exit(xgpu::getErrorInt(Err));
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndPopup();
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(6);

            //
            // Camera controls
            //
            if (ImGui::IsWindowHovered())
            {
                if (Mouse.isPressed(xgpu::mouse::digital::BTN_RIGHT))
                {
                    auto MousePos = Mouse.getValue(xgpu::mouse::analog::POS_REL);
                    Angles.m_Pitch.m_Value -= 0.01f * MousePos[1];
                    Angles.m_Yaw.m_Value   -= 0.01f * MousePos[0];
                }

                // zoom
                Distance += Distance * -0.02f * Mouse.getValue(xgpu::mouse::analog::WHEEL_REL)[0];
            }

            //
            // Render mesh
            //
            if (material_instance.m_Private) xgpu::tools::imgui::AddCustomRenderCallback([&](const ImVec2& windowPos, const ImVec2& windowSize)
            {
                auto CmdBufferRef = MainWindow.getCmdBuffer();

                View.setViewport({ static_cast<int>(windowPos.x)
                                 , static_cast<int>(windowPos.y)
                                 , static_cast<int>(windowSize.x)
                                 , static_cast<int>(windowSize.y)
                                });

                // Compute aspect ratio from ImGui widget size
                View.setAspect(windowSize.x / windowSize.y);

                // Update the camera
                View.LookAt(Distance, Angles, { 0,0,0 });

                e19::push_constants pushConst;
                pushConst.m_L2C = (View.getW2C() * xmath::fmat4::fromScale({ 2.f }));

                // Set pipeline and push constants
                CmdBufferRef.setPipelineInstance(material_instance);
                CmdBufferRef.setPushConstants(pushConst);

                // Render the mesh
                MeshManager.Rendering(CmdBufferRef, CurrentModel);
            });

            ImGui::End();
        }
        
        //
        // Render the Inspector when node is selected
        //
        if (nSelected != 1)
        {
            LastSelectedNode = { 0 };
            Inspector.clear();
        }
        else if (LastSelectedNode != SelectedNodes[0])
        {
            LastSelectedNode = SelectedNodes[0];
            Inspector.clear();
            Inspector.AppendEntity();
            
            auto& Entry = *g.m_InstanceNodes[xmaterial_compiler::node_guid{ LastSelectedNode.Get() }];
            
            Inspector.AppendEntityComponent(*xproperty::getObject(Entry), &Entry);
        }
        else if(g.m_InstanceNodes.find(xmaterial_compiler::node_guid{ LastSelectedNode.Get() }) == g.m_InstanceNodes.end())
        {
            Inspector.clear();
        }

        xproperty::settings::context Context;
        Inspector.Show(Context, [&] {});

        //
        // Show a texture selector in IMGUI
        //
        AsserBrowser.Render(e10::g_LibMgr, ResourceMgr);

        if (auto NewAsset = AsserBrowser.getNewAsset(); NewAsset.empty() == false && NewAsset.m_Type.m_Value == 0xC59E01444175409E )
        {
            g.m_PinToNode.clear();
            g.m_Connections.clear();
            g.m_InstanceNodes.clear();

            // Generate the paths
            e10::g_LibMgr.getNodeInfo(SelectedDescriptor.m_LibraryGUID, SelectedDescriptor.m_InfoGUID, [&](e10::library_db::info_node& NodeInfo)
            {
                SelectedDescriptor.GeneratePaths(NodeInfo.m_Path);
            });
        }
        else if (auto SelectedAsset = AsserBrowser.getSelectedAsset(); SelectedAsset.empty() == false && SelectedAsset.m_Type.m_Value == 0xC59E01444175409E)
        {
            ed::SetCurrentEditor(g_pEditor);
            LastSelectedNode = { 0 };
            ed::ClearSelection();

            g.m_PinToNode.clear();
            g.m_Connections.clear();
            g.m_InstanceNodes.clear();

            SelectedDescriptor.clear();
            //SelectedDescriptor.m_pDescriptor = xresource_pipeline::factory_base::Find(std::string_view{ "Material" })->CreateDescriptor();
            SelectedDescriptor.m_LibraryGUID = AsserBrowser.getSelectedLibrary();
            SelectedDescriptor.m_InfoGUID = SelectedAsset;

            // Generate the paths
            e10::g_LibMgr.getNodeInfo(SelectedDescriptor.m_LibraryGUID, SelectedDescriptor.m_InfoGUID, [&](e10::library_db::info_node& NodeInfo)
            {
                SelectedDescriptor.GeneratePaths(NodeInfo.m_Path);
            });

            // If it is a new resource it probably does not have a descriptor yet...
            if (std::filesystem::exists(SelectedDescriptor.m_DescriptorPath))
            {
                g.serialize(g, true, SelectedDescriptor.m_DescriptorPath );

                for (auto& [nid, pNode] : g.m_InstanceNodes)
                {
                    ed::SetNodePosition(nid.m_Value, ImVec2(pNode->m_Pos.m_X, pNode->m_Pos.m_Y));
                    if (pNode->m_Name == "Sampler2D" && !pNode->m_Params.m_Properties.empty())
                    {
                        for (int i = 0; i < texturemgr.m_Names.size(); i++)
                        {
                            if (texturemgr.m_Names[i] == pNode->m_Params.m_Properties[0].m_Value.get<std::string>())
                            {
                                g_SelectedTexture = i;
                            }
                        }
                    }
                }

                // Tell the system if we should be loading the sprv
                SelectedDescriptor.m_bReload = std::filesystem::exists(SelectedDescriptor.m_ResourcePath);
            }

            ed::SetCurrentEditor(nullptr);
        }

        xgpu::tools::imgui::Render();

        MainWindow.PageFlip();

        // Let the resource manager know we have change the frame
        ResourceMgr.OnEndFrameDelegate();
    }
    ed::DestroyEditor(g_pEditor);
    xgpu::tools::imgui::Shutdown();

    return 0;
}