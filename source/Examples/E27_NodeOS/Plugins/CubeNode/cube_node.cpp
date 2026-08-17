// Cube node plugin - a native Node OS node type that lives entirely in its own DLL, compiled
// completely separately from the Node OS host executable. The host never has this file's code
// linked into it; it discovers "Cube" purely by loading CubeNode.dll and calling NodeOS_OnLoad, the
// one exported function at the bottom of this file. This is the proof that a node's behavior doesn't
// require touching (or recompiling) the host at all.
//
// Its dimensions are real, editable xproperty state (cube_properties, below) - written exactly like
// every other reflected descriptor in this engine (XPROPERTY_DEF/XPROPERTY_REG/obj_member), not a
// Node-OS-specific scheme. xnode_os_property_adapter.h turns it into an ABI-safe
// ixnode_os_reflected_object automatically, so the host's Node Properties panel can show and edit
// Width/Height/Depth without ever knowing this plugin's concrete struct exists.
// Must be defined before imgui.h's FIRST inclusion anywhere in this translation unit (imgui_internal.h
// hard-errors if it's defined too late) - core imgui.cpp code (e.g. ImGuiStyle::ScaleAllSizes) uses
// ImVec2*float unconditionally, so this plugin's own compiled copy of imgui.cpp needs it regardless of
// how the host's build happens to get it.
#define IMGUI_DEFINE_MATH_OPERATORS

#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include "../../SDK/xnode_os_property_adapter.h"
#include "dependencies/xproperty/source/xcore/my_properties.h"
#include <cstdlib>
#include <cstring>

// Real, official property UI - compiled entirely into THIS plugin's own binary, the same way
// source/Examples/E04_Properties/E04_Properties.cpp already pulls the real inspector bodies into the
// host executable (a raw #include of the .cpp, this engine's own established pattern for this exact
// file). Because this plugin's own compiled code both builds AND walks its own xproperty::type::object
// (via its own atomic_v<T> singletons), no xproperty data ever crosses the DLL boundary - see
// xnode_os_plugin_api.h's comment on m_pDrawProperties for why an earlier attempt at the host doing
// this instead was fundamentally unsound. The only thing crossing the boundary is ImGui draw calls,
// via Dear ImGui's own documented DLL pattern (imgui.h's "DLL users" comment) - wired up once in
// NodeOS_OnLoad, below.
//
// xPropertyImGuiInspector.cpp must come FIRST: its own <windows.h> include (for its file/folder
// dialog code) needs the full Win32 API surface (OPENFILENAMEW, commdlg.h), but imgui.cpp defines
// WIN32_LEAN_AND_MEAN before ITS OWN <windows.h> include - and since this is all one flattened
// translation unit, whichever file hits <windows.h>'s include guard first decides what's available
// for the rest of the compile. Including the property UI first gets the full header in before
// imgui.cpp's lean flag would otherwise have shut half of it out.
#include "dependencies/xproperty/source/examples/imgui/xPropertyImGuiInspector.cpp"
#include "dependencies/imgui/imgui.cpp"
#include "dependencies/imgui/imgui_draw.cpp"
#include "dependencies/imgui/imgui_widgets.cpp"
#include "dependencies/imgui/imgui_tables.cpp"

namespace
{
    struct cube_properties
    {
        float m_Width  = 1.0f;
        float m_Height = 1.0f;
        float m_Depth  = 1.0f;

        XPROPERTY_DEF
        ( "cube_properties", cube_properties
        , obj_member<"Width",  &cube_properties::m_Width>
        , obj_member<"Height", &cube_properties::m_Height>
        , obj_member<"Depth",  &cube_properties::m_Depth>
        )
    };
}
XPROPERTY_REG(cube_properties)

namespace
{
    //--------------------------------------------------------------------------------------
    // A box built from cube_properties' own Width/Height/Depth - 8 corner vertices, 12 triangles
    // (2 per face, 36 indices). No normals/UVs - this first proof only needs enough real data for a
    // downstream node to report real counts.
    //--------------------------------------------------------------------------------------
    void Execute(const void* pProperties, void** /*Inputs*/, void** Outputs) noexcept
    {
        const auto* pProps = static_cast<const cube_properties*>(pProperties);
        const float X = pProps ? pProps->m_Width  * 0.5f : 0.5f;
        const float Y = pProps ? pProps->m_Height * 0.5f : 0.5f;
        const float Z = pProps ? pProps->m_Depth  * 0.5f : 0.5f;

        const float Positions[8 * 3] =
        { -X,-Y,-Z,   X,-Y,-Z,   X, Y,-Z,  -X, Y,-Z
        , -X,-Y, Z,   X,-Y, Z,   X, Y, Z,  -X, Y, Z
        };
        static const unsigned int s_Indices[36] =
        { 0,1,2, 0,2,3         // back
        , 4,6,5, 4,7,6         // front
        , 4,0,3, 4,3,7         // left
        , 1,5,6, 1,6,2         // right
        , 4,5,1, 4,1,0         // bottom
        , 3,2,6, 3,6,7         // top
        };

        auto* pMesh = static_cast<xnode_os_mesh_data*>(std::malloc(sizeof(xnode_os_mesh_data)));
        pMesh->m_VertexCount = 8;
        pMesh->m_pPositions  = static_cast<float*>(std::malloc(sizeof(Positions)));
        std::memcpy(pMesh->m_pPositions, Positions, sizeof(Positions));
        pMesh->m_IndexCount  = 36;
        pMesh->m_pIndices    = static_cast<unsigned int*>(std::malloc(sizeof(s_Indices)));
        std::memcpy(pMesh->m_pIndices, s_Indices, sizeof(s_Indices));

        Outputs[0] = pMesh;
    }

    void FreeOutputs(void** Outputs, int /*OutputCount*/) noexcept
    {
        if (auto* pMesh = static_cast<xnode_os_mesh_data*>(Outputs[0]))
        {
            std::free(pMesh->m_pPositions);
            std::free(pMesh->m_pIndices);
            std::free(pMesh);
        }
    }

    void* CreateDefaultProperties()
    {
        return new cube_properties();
    }

    void DestroyProperties(void* pProperties) noexcept
    {
        delete static_cast<cube_properties*>(pProperties);
    }

    ixnode_os_reflected_object* GetReflectedObject(void* pProperties) noexcept
    {
        return xnode_os_MakeReflectedObject(static_cast<cube_properties*>(pProperties));
    }

    // Draws with the real, official xproperty::inspector - safe here because it's THIS plugin's own
    // compiled inspector walking THIS plugin's own xproperty::type::object (see this file's top
    // comment). Show() manages its own ImGui window, matching every other xproperty::inspector call
    // site in this engine (e.g. E26_RTCS_Editor.cpp).
    //
    // The Inspector itself MUST persist across frames rather than being rebuilt from scratch on every
    // call: internally, Show() seeds each property row's ImGui ID partly from the address of its
    // component-list slot (&C in xPropertyImGuiInspector.cpp's render loop). A fresh
    // AppendEntity()/AppendEntityComponent() call every frame reallocates that slot every frame, so
    // the address - and therefore every widget's final ImGui ID - changes every frame even though
    // nothing about the property actually changed. ImGui widgets need a STABLE id across frames to
    // track an in-progress click/drag; with an unstable id, DragScalar/SliderScalar etc. register as
    // hovered but never as active, which looks exactly like "nothing happens when I click." Only
    // rebuild the entity/component list when the bound property pointer actually changes (i.e. a
    // different node got selected) - Show() itself already re-reads live values every frame via
    // RefreshAllProperties, so nothing about live-editing requires rebuilding the list itself.
    void DrawProperties(void* pProperties) noexcept
    {
        // Same window title regardless of node type ("Node Properties", matching the ABI-safe
        // fallback's own title exactly) - a per-type title (e.g. "Node Properties: Cube") makes ImGui
        // treat each node type's panel as a wholly separate window needing its own dock placement,
        // forcing a re-dock every time a different node gets selected. One stable title is one window.
        static xproperty::inspector s_Inspector("Node Properties");
        static void*                s_pBoundProperties = nullptr;
        if (s_pBoundProperties != pProperties)
        {
            s_Inspector.clear();
            s_Inspector.AppendEntity();
            s_Inspector.AppendEntityComponent(*xproperty::getObjectByType<cube_properties>(), pProperties);
            s_pBoundProperties = pProperties;
        }
        xproperty::settings::context Context;
        ImGui::SetNextWindowPos(ImVec2(1265, 90), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);
        s_Inspector.Show(Context, [] {});
    }

    const xnode_os_port_desc s_Outputs[1] = { { "Mesh", "Mesh" } };

    const xnode_os_node_type_desc s_NodeType =
    { .m_AbiVersion               = XNODE_OS_ABI_VERSION
    , .m_pName                    = "Cube"
    , .m_pCategory                = "Geometry"
    , .m_InputCount                = 0
    , .m_pInputs                   = nullptr
    , .m_OutputCount                = 1
    , .m_pOutputs                   = s_Outputs
    , .m_pExecute                   = &Execute
    , .m_pFreeOutputs               = &FreeOutputs
    , .m_PropertyStructSize         = sizeof(cube_properties)
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
