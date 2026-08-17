// Inspect Mesh node plugin - a second, completely independent DLL. It has never seen CubeNode's
// source and does not link against it; the only thing tying them together is the shared ABI
// (xnode_os_plugin_api.h) and the shared "Mesh" data layout (xnode_os_shared_types.h) both happen
// to agree on. That is the actual proof: the host can wire this node's input to any other node's
// "Mesh"-typed output - Cube today, an importer or a modeling-tool node later - without either side
// being recompiled.
//
// No output port at all - the vertex/index/triangle counts live as read-only properties instead of
// a "Report" text output stapled to the canvas. Draws its properties with the real, official
// xproperty::inspector, same pattern (and same reasoning) as cube_node.cpp's own DrawProperties: no
// xproperty data crosses the DLL boundary, only ImGui draw calls do.
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
#include <cstdlib>
#include <cstring>

// xPropertyImGuiInspector.cpp must come FIRST - see cube_node.cpp's identical comment for the full
// windows.h/WIN32_LEAN_AND_MEAN ordering explanation.
#include "dependencies/xproperty/source/examples/imgui/xPropertyImGuiInspector.cpp"
#include "dependencies/imgui/imgui.cpp"
#include "dependencies/imgui/imgui_draw.cpp"
#include "dependencies/imgui/imgui_widgets.cpp"
#include "dependencies/imgui/imgui_tables.cpp"

namespace
{
    // A nested "Mesh" scope so the inspector shows Vertices/Indices/Triangles grouped under it,
    // rather than three flat top-level fields - obj_member_ro marks each read-only (m_bConst), which
    // the real inspector's Flags.m_bShowReadOnly disables the widget for; the ABI-safe fallback
    // drawer (DrawReflectedMembers, for plugins without their own m_pDrawProperties) has no concept
    // of read-only at all, which is exactly why this node needs the real inspector, not that fallback.
    struct mesh_stats
    {
        int m_Vertices  = 0;
        int m_Indices   = 0;
        int m_Triangles = 0;

        XPROPERTY_DEF
        ( "mesh_stats", mesh_stats
        , obj_member_ro<"Vertices",  &mesh_stats::m_Vertices>
        , obj_member_ro<"Indices",   &mesh_stats::m_Indices>
        , obj_member_ro<"Triangles", &mesh_stats::m_Triangles>
        )
    };
}
XPROPERTY_REG(mesh_stats)

namespace
{
    struct inspect_mesh_properties
    {
        mesh_stats m_Mesh;

        XPROPERTY_DEF
        ( "inspect_mesh_properties", inspect_mesh_properties
        , obj_member<"Mesh", &inspect_mesh_properties::m_Mesh>
        )
    };
}
XPROPERTY_REG(inspect_mesh_properties)

namespace
{
    // No outputs - Execute's only job now is to refresh this node's own read-only properties from
    // whatever mesh is currently connected. pProperties arrives as const (Execute's ABI signature
    // never assumes a node mutates its own state), but this plugin owns that block outright (it's
    // its own m_pCreateDefaultProperties allocation), so writing back into it here is safe.
    void Execute(const void* pProperties, void** Inputs, void** /*Outputs*/) noexcept
    {
        auto* pProps = const_cast<inspect_mesh_properties*>(static_cast<const inspect_mesh_properties*>(pProperties));
        if (!pProps) return;
        if (auto* pMesh = static_cast<xnode_os_mesh_data*>(Inputs[0]))
        {
            pProps->m_Mesh.m_Vertices  = static_cast<int>(pMesh->m_VertexCount);
            pProps->m_Mesh.m_Indices   = static_cast<int>(pMesh->m_IndexCount);
            pProps->m_Mesh.m_Triangles = static_cast<int>(pMesh->m_IndexCount / 3);
        }
        else
        {
            pProps->m_Mesh.m_Vertices = pProps->m_Mesh.m_Indices = pProps->m_Mesh.m_Triangles = 0;
        }
    }

    void* CreateDefaultProperties()
    {
        return new inspect_mesh_properties();
    }

    void DestroyProperties(void* pProperties) noexcept
    {
        delete static_cast<inspect_mesh_properties*>(pProperties);
    }

    ixnode_os_reflected_object* GetReflectedObject(void* pProperties) noexcept
    {
        return xnode_os_MakeReflectedObject(static_cast<inspect_mesh_properties*>(pProperties));
    }

    // Persistent across frames (static, rebuilt only when the bound properties pointer changes) -
    // see cube_node.cpp's DrawProperties comment for exactly why a fresh Inspector every frame breaks
    // every widget's ImGui id (this doesn't matter for read-only fields specifically, but it's the
    // established, correct pattern regardless).
    void DrawProperties(void* pProperties) noexcept
    {
        static xproperty::inspector s_Inspector("Node Properties");
        static void*                s_pBoundProperties = nullptr;
        if (s_pBoundProperties != pProperties)
        {
            s_Inspector.clear();
            s_Inspector.AppendEntity();
            s_Inspector.AppendEntityComponent(*xproperty::getObjectByType<inspect_mesh_properties>(), pProperties);
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
    , .m_pName                    = "Inspect Mesh"
    , .m_pCategory                = "Debug"
    , .m_InputCount                = 1
    , .m_pInputs                   = s_Inputs
    , .m_OutputCount                = 0
    , .m_pOutputs                   = nullptr
    , .m_pExecute                   = &Execute
    , .m_pFreeOutputs               = nullptr
    , .m_PropertyStructSize         = sizeof(inspect_mesh_properties)
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
