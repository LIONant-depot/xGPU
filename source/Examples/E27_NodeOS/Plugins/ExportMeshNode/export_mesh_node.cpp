// Export Mesh node plugin - the graph's first artifact-producing node: it has zero outputs, and its
// entire purpose is a real side effect outside the graph (writing a plain-text .obj file to disk).
// Wiring a Cube's Mesh output into this node and opening the resulting .obj in any real 3D tool is the
// proof this whole system produces something real, not just an internal simulation.
//
// Draws its properties the same way cube_node.cpp does - the real, official xproperty::inspector,
// compiled entirely into this plugin's own binary (see cube_node.cpp's top comment for the full
// reasoning: no xproperty data crosses the DLL boundary, only ImGui's shared context does). Every node
// type using the same official look, rather than mixing it with the plainer ABI-safe
// ixnode_os_reflected_object widget walk, is the point - a plugin author can still choose the lighter
// ABI-safe-only path (leave m_pDrawProperties null) when the extra imgui/inspector compile weight
// genuinely isn't worth it for a trivial property set, but this one opts in for visual consistency.
//
// Must be defined before imgui.h's FIRST inclusion anywhere in this translation unit (imgui_internal.h
// hard-errors if it's defined too late) - core imgui.cpp code (e.g. ImGuiStyle::ScaleAllSizes) uses
// ImVec2*float unconditionally.
#define IMGUI_DEFINE_MATH_OPERATORS

#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include "../../SDK/xnode_os_property_adapter.h"
#include "dependencies/xproperty/source/xcore/my_properties.h"
#include <cstdio>
#include <fstream>

// xPropertyImGuiInspector.cpp must come FIRST: its own <windows.h> include (for its file/folder dialog
// code) needs the full Win32 API surface (OPENFILENAMEW, commdlg.h), but imgui.cpp defines
// WIN32_LEAN_AND_MEAN before ITS OWN <windows.h> include - see cube_node.cpp's identical comment for
// the full explanation of why this ordering matters in a raw-#include unity build.
#include "dependencies/xproperty/source/examples/imgui/xPropertyImGuiInspector.cpp"
#include "dependencies/imgui/imgui.cpp"
#include "dependencies/imgui/imgui_draw.cpp"
#include "dependencies/imgui/imgui_widgets.cpp"
#include "dependencies/imgui/imgui_tables.cpp"

namespace
{
    // Double-null-terminated (description, pattern) pairs, the same Win32 OPENFILENAME filter format
    // xgeom_static_descriptor.h's own mesh_filter_v already uses for this exact file_dialog UI style.
    static constexpr wchar_t s_ExportFilter[] = L"Wavefront OBJ\0*.obj\0All Files\0*.*\0";

    struct export_mesh_properties
    {
        // std::wstring (not std::string) because member_ui<T>::file_dialog only exists for wstring in
        // this engine's property UI - it's what actually gets the "..." browse button and the native
        // Windows file picker, instead of the plain type-a-path text field the default std::string style
        // draws (which also only committed on Enter, unlike the picker's immediate commit on selection).
        std::wstring m_Path = L"D:/LIONant/xGPU/source/Examples/E27_NodeOS/exported_mesh.obj";

        XPROPERTY_DEF
        ( "export_mesh_properties", export_mesh_properties
        , obj_member<"Path", &export_mesh_properties::m_Path, member_ui<std::wstring>::file_dialog<s_ExportFilter>>
        )
    };
}
XPROPERTY_REG(export_mesh_properties)

namespace
{
    //--------------------------------------------------------------------------------------
    // Writes the incoming mesh as a plain Wavefront .obj - positions only (no normals/UVs, matching
    // xnode_os_mesh_data's own scope), one "v" line per vertex and one "f" line per triangle. OBJ face
    // indices are 1-based, unlike xnode_os_mesh_data's 0-based ones.
    //--------------------------------------------------------------------------------------
    void Execute(const void* pProperties, void** Inputs, void** /*Outputs*/) noexcept
    {
        const auto* pProps = static_cast<const export_mesh_properties*>(pProperties);
        const auto* pMesh  = static_cast<const xnode_os_mesh_data*>(Inputs[0]);
        if (!pProps || pProps->m_Path.empty() || !pMesh) return;

        std::ofstream File(pProps->m_Path.c_str()); // MSVC's wchar_t* fstream constructor overload
        if (!File.is_open()) return;

        File << "# Exported by Node OS - " << pMesh->m_VertexCount << " vertices, " << (pMesh->m_IndexCount / 3) << " triangles\n";
        for (unsigned int i = 0; i < pMesh->m_VertexCount; ++i)
            File << "v " << pMesh->m_pPositions[i * 3 + 0] << ' ' << pMesh->m_pPositions[i * 3 + 1] << ' ' << pMesh->m_pPositions[i * 3 + 2] << '\n';
        for (unsigned int i = 0; i + 2 < pMesh->m_IndexCount; i += 3)
            File << "f " << (pMesh->m_pIndices[i + 0] + 1) << ' ' << (pMesh->m_pIndices[i + 1] + 1) << ' ' << (pMesh->m_pIndices[i + 2] + 1) << '\n';
    }

    void* CreateDefaultProperties()
    {
        return new export_mesh_properties();
    }

    void DestroyProperties(void* pProperties) noexcept
    {
        delete static_cast<export_mesh_properties*>(pProperties);
    }

    ixnode_os_reflected_object* GetReflectedObject(void* pProperties) noexcept
    {
        return xnode_os_MakeReflectedObject(static_cast<export_mesh_properties*>(pProperties));
    }

    // Draws with the real, official xproperty::inspector - see cube_node.cpp's DrawProperties for the
    // full reasoning on why this is safe, and why the Inspector must persist across frames rather than
    // being rebuilt every call (a fresh AppendEntityComponent() every frame reallocates the
    // component-list slot that Show() partly seeds each row's ImGui id from, making every widget's id
    // unstable frame to frame - which looks exactly like "clicking does nothing").
    void DrawProperties(void* pProperties) noexcept
    {
        // Same window title regardless of node type - see cube_node.cpp's identical comment on why a
        // per-type title breaks docking.
        static xproperty::inspector s_Inspector("Node Properties");
        static void*                s_pBoundProperties = nullptr;
        if (s_pBoundProperties != pProperties)
        {
            s_Inspector.clear();
            s_Inspector.AppendEntity();
            s_Inspector.AppendEntityComponent(*xproperty::getObjectByType<export_mesh_properties>(), pProperties);
            s_pBoundProperties = pProperties;
        }
        xproperty::settings::context Context;
        ImGui::SetNextWindowPos(ImVec2(1265, 90), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);
        s_Inspector.Show(Context, [] {});
    }

    const xnode_os_port_desc s_Inputs[1] = { { "Mesh", "Mesh" } };

    const xnode_os_node_type_desc s_NodeType =
    { .m_AbiVersion               = XNODE_OS_ABI_VERSION
    , .m_pName                    = "Export Mesh"
    , .m_pCategory                = "Output"
    , .m_InputCount                = 1
    , .m_pInputs                   = s_Inputs
    , .m_OutputCount                = 0
    , .m_pOutputs                   = nullptr
    , .m_pExecute                   = &Execute
    , .m_pFreeOutputs               = nullptr
    , .m_PropertyStructSize         = sizeof(export_mesh_properties)
    , .m_pCreateDefaultProperties   = &CreateDefaultProperties
    , .m_pDestroyProperties         = &DestroyProperties
    , .m_pGetReflectedObject        = &GetReflectedObject
    , .m_pDrawProperties            = &DrawProperties
    };
}

extern "C" XNODE_OS_EXPORT bool NodeOS_OnLoad(ixnode_os_host* pHost)
{
    if (!pHost || pHost->GetAbiVersion() != XNODE_OS_ABI_VERSION) return false;

    // Share the host's real ImGui context/allocator so this plugin's own compiled ImGui:: calls
    // (inside DrawProperties, above) render into the host's actual window instead of an empty,
    // never-rendered private context - Dear ImGui's own documented pattern for exactly this scenario.
    ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(pHost->GetImGuiContext()));
    void *pAllocFunc = nullptr, *pFreeFunc = nullptr, *pUserData = nullptr;
    pHost->GetImGuiAllocatorFunctions(&pAllocFunc, &pFreeFunc, &pUserData);
    ImGui::SetAllocatorFunctions(reinterpret_cast<ImGuiMemAllocFunc>(pAllocFunc), reinterpret_cast<ImGuiMemFreeFunc>(pFreeFunc), pUserData);

    pHost->RegisterNodeType(&s_NodeType);
    return true;
}
