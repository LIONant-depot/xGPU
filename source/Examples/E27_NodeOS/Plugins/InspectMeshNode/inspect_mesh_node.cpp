// Inspect Mesh node plugin - a second, completely independent DLL. It has never seen CubeNode's
// source and does not link against it; the only thing tying them together is the shared ABI
// (xnode_os_plugin_api.h) and the shared "Mesh" data layout (xnode_os_shared_types.h) both happen
// to agree on. That is the actual proof: the host can wire this node's input to any other node's
// "Mesh"-typed output - Cube today, an importer or a modeling-tool node later - without either side
// being recompiled.
//
// No output port at all - the vertex/index/triangle counts live as read-only properties instead of
// a "Report" text output stapled to the canvas. inspect_mesh_node IS the property object (via
// xproperty::base, inherited through xnode_os_node) - the host's own real xproperty::inspector
// draws it directly, disabling the Mesh/* fields for edit because they're obj_member_ro (read-only)
// - a distinction the old ABI-safe primitive walk had no concept of at all. Unlike cube_node.cpp,
// this plugin DOES still need the raw imgui/xPropertyImGuiInspector.cpp includes below, purely for
// the linker: obj_member_ro captures a "how to draw me, disabled" function pointer at registration
// time regardless of the member's actual type, which needs xproperty::ui's real Render templates
// linked in even though this plugin never calls Show() itself (confirmed empirically - a plain,
// non-read-only member never captures that pointer and needs none of this).
#define IMGUI_DEFINE_MATH_OPERATORS
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include "dependencies/xproperty/source/examples/imgui/xPropertyImGuiInspector.cpp"
#include "dependencies/imgui/imgui.cpp"
#include "dependencies/imgui/imgui_draw.cpp"
#include "dependencies/imgui/imgui_widgets.cpp"
#include "dependencies/imgui/imgui_tables.cpp"

namespace
{
    // A nested "Mesh" scope so the inspector shows Vertices/Indices/Triangles grouped under it,
    // rather than three flat top-level fields - obj_member_ro marks each read-only (m_bConst), which
    // the real inspector's Flags.m_bShowReadOnly disables the widget for.
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
    // No outputs - Execute's only job is to refresh this node's own read-only properties from
    // whatever mesh is currently connected.
    struct inspect_mesh_node : xnode_os_node
    {
        mesh_stats m_Mesh;

        XPROPERTY_VDEF
        ( "inspect_mesh_node", inspect_mesh_node
        , obj_member<"Mesh", &inspect_mesh_node::m_Mesh>
        )

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            static const xnode_os_port_desc s_Inputs[1] = { { "Mesh", "Mesh" } };
            return s_Inputs;
        }

        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            return {};
        }

        void Execute(void** Inputs, void** /*Outputs*/) noexcept override
        {
            if (auto* pMesh = static_cast<xnode_os_mesh_data*>(Inputs[0]))
            {
                m_Mesh.m_Vertices  = static_cast<int>(pMesh->m_VertexCount);
                m_Mesh.m_Indices   = static_cast<int>(pMesh->m_IndexCount);
                m_Mesh.m_Triangles = static_cast<int>(pMesh->m_IndexCount / 3);
            }
            else
            {
                m_Mesh.m_Vertices = m_Mesh.m_Indices = m_Mesh.m_Triangles = 0;
            }
        }
    };
}
XPROPERTY_VREG(inspect_mesh_node)

namespace
{
    struct inspect_mesh_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("inspect_mesh_node_factory", inspect_mesh_node_factory)

        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "Inspect Mesh"; }
        std::string_view getCategory() const noexcept override { return "Debug"; }

        xnode_os_node& CreateNodeInstance() override
        {
            auto* pNode = new inspect_mesh_node();
            pNode->m_pFactory = this;
            return *pNode;
        }

        void DestroyNodeInstance(xnode_os_node& Node) override
        {
            delete static_cast<inspect_mesh_node*>(&Node);
        }
    };
}
XPROPERTY_VREG(inspect_mesh_node_factory)

extern "C" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host& /*Host*/) noexcept
{
    return *new inspect_mesh_node_factory();
}

extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    delete static_cast<inspect_mesh_node_factory*>(&Factory);
}
