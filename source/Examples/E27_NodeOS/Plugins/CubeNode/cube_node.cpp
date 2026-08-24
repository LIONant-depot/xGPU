// Cube node plugin - a native Node OS node type that lives entirely in its own DLL, compiled
// completely separately from the Node OS host executable. The host never has this file's code
// linked into it; it discovers "Cube" purely by loading CubeNode.dll and calling
// NodeOS_CreateFactory, the entry point at the bottom of this file. This is the proof that a node's
// behavior doesn't require touching (or recompiling) the host at all.
//
// Its dimensions are real, editable xproperty state - written exactly like every other reflected
// object in this engine (XPROPERTY_VDEF/XPROPERTY_VREG/obj_member), not a Node-OS-specific scheme.
// cube_node IS the property object (via xproperty::base, inherited through xnode_os_node) - the
// host's own real xproperty::inspector draws it directly, no plugin-authored drawing CODE needed
// (see xnode_os_plugin_api.h's top comment for why a real xproperty::type::object crossing the DLL
// boundary is safe).
//
// No ImGui/inspector includes needed here, same as export_mesh_node.cpp/inspect_mesh_node.cpp:
// member_ui_base stores a (Type,Style) GUID pair rather than a resolved function pointer, so
// attaching a style (or none, as here) only ever stores plain data - the host resolves the real
// drawer through its own registry at draw time (see xproperty's my_property_ui.h/
// xPropertyImGuiInspector.cpp). Width/Height/Depth below are plain, unstyled members either way.
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include <cstdlib>
#include <cstring>

namespace
{
    //--------------------------------------------------------------------------------------
    // A box built from this node's own Width/Height/Depth - 8 corner vertices, 12 triangles
    // (2 per face, 36 indices). No normals/UVs - this first proof only needs enough real data for a
    // downstream node to report real counts.
    //--------------------------------------------------------------------------------------
    struct cube_node : xnode_os_node
    {
        float m_Width  = 1.0f;
        float m_Height = 1.0f;
        float m_Depth  = 1.0f;

        XPROPERTY_VDEF
        ( "cube_node", cube_node
        , obj_member<"Width",  &cube_node::m_Width,  member_help<"Size along X. Halved on each side of the box's own center to place its 8 corner vertices.">>
        , obj_member<"Height", &cube_node::m_Height, member_help<"Size along Y. Halved on each side of the box's own center to place its 8 corner vertices.">>
        , obj_member<"Depth",  &cube_node::m_Depth,  member_help<"Size along Z. Halved on each side of the box's own center to place its 8 corner vertices.">>
        )

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            return {};
        }

        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            static const xnode_os_port_desc s_Outputs[1] = { { "Mesh", "Mesh" } };
            return s_Outputs;
        }

        void Execute(void** /*Inputs*/, void** Outputs) noexcept override
        {
            const float X = m_Width  * 0.5f;
            const float Y = m_Height * 0.5f;
            const float Z = m_Depth  * 0.5f;

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

        void FreeOutputs(void** Outputs) noexcept override
        {
            if (auto* pMesh = static_cast<xnode_os_mesh_data*>(Outputs[0]))
            {
                std::free(pMesh->m_pPositions);
                std::free(pMesh->m_pIndices);
                std::free(pMesh);
            }
        }
    };
}
XPROPERTY_VREG(cube_node)

namespace
{
    struct cube_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("cube_node_factory", cube_node_factory)

        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "Cube"; }
        std::string_view getCategory() const noexcept override { return "Geometry"; }

        xnode_os_node& CreateNodeInstance() override
        {
            auto* pNode = new cube_node();
            pNode->m_pFactory = this;
            return *pNode;
        }

        void DestroyNodeInstance(xnode_os_node& Node) override
        {
            delete static_cast<cube_node*>(&Node);
        }
    };
}
XPROPERTY_VREG(cube_node_factory)

extern "C" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host& /*Host*/) noexcept
{
    return *new cube_node_factory();
}

extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    delete static_cast<cube_node_factory*>(&Factory);
}
