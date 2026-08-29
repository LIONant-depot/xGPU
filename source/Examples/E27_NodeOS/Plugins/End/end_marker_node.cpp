// End - a pure position marker (NODE_SCRIPTING_DESIGN.md section 4.1/4.2). Execute is a no-op -
// the only thing that will ever matter about it is WHERE it sits in its spine, read directly off
// m_OwnedEndId on whichever node owns it. Never created directly by the user: the live editor's
// "add a node" UI creates it automatically, paired with its owner, via
// commands::BuildCreateNodeCommand (E27_NodeOS_Editor.cpp) - see If/ForEachLoop's own
// needsOwnedEndMarker()/getOwnedEndMarkerPluginDir() overrides for how an owner names this folder.
//
// IsElse is the one thing on this node a user DOES edit directly - a checkbox (an ordinary
// reflected bool renders as one via the existing generic inspector) that turns a plain End into an
// End-Else. Checking it is what the design doc calls "the end-else" case: this node then owns a
// FURTHER End of its own (created/removed by commands::set_end_else_state_cmd, not by this class -
// a plugin never issues xundo commands itself), and grows a second, read-only output pin
// ("ElseEnd") to visibly connect to it. getOutputs() varies per-instance on purpose (see
// xnode_os_plugin_api.h's own comment on why that's supported at all).
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include "dependencies/xresource_guid/source/xresource_guid.h"

namespace
{
    struct end_marker_node : xnode_os_node
    {
        bool m_bIsElse = false;

        // Stable per-instance guids for the two fixed pins below - reflected (DONT_SHOW) so the saved
        // value is restored on load rather than a fresh xresource::guid_generator::Instance64()
        // regenerating (which would stop matching any saved link) - same pattern function_node.cpp's
        // m_ExecGuid/m_EndGuid use. "ElseEnd" keeps its guid whether or not it's CURRENTLY exposed
        // (see getOutputs() below), so toggling IsElse off and back on doesn't swap in a different
        // identity for the same conceptual pin.
        std::uint64_t m_OwnerGuid   = xresource::guid_generator::Instance64();
        std::uint64_t m_ElseEndGuid = xresource::guid_generator::Instance64();

        // Not const-only-initialized - getInputs()/getOutputs() re-sync m_Guid from the reflected
        // fields above on every call, so a guid restored by deserialization AFTER construction still
        // takes effect (same reasoning as constant_node.cpp's own m_OutputDesc).
        mutable xnode_os_port_desc m_Inputs[1]  = { { "Owner",   "Scope", true, true, false, 0 } };
        mutable xnode_os_port_desc m_Outputs[1] = { { "ElseEnd", "Scope", true, true, false, 0 } };

        XPROPERTY_VDEF
        ( "end_marker_node", end_marker_node
        , obj_member<"IsElse", &end_marker_node::m_bIsElse, member_help<"Turns a plain End into an End-Else. When checked, this marker owns a further End of its own and grows a second, read-only 'ElseEnd' output pin connecting to it.">>
        , obj_member<"OwnerGuid",   &end_marker_node::m_OwnerGuid,   member_flags<xproperty::flags::DONT_SHOW>>
        , obj_member<"ElseEndGuid", &end_marker_node::m_ElseEndGuid, member_flags<xproperty::flags::DONT_SHOW>>
        )

        // "Owner" is the read-only ownership pin - the host connects it automatically, to whichever
        // If/ForEachLoop (or End-Else) created this marker. Never dragged by the user.
        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            m_Inputs[0].m_Guid = m_OwnerGuid;
            return m_Inputs;
        }

        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            m_Outputs[0].m_Guid = m_ElseEndGuid;
            return m_bIsElse ? std::span<const xnode_os_port_desc>(m_Outputs) : std::span<const xnode_os_port_desc>{};
        }

        void Execute(void** /*Inputs*/, void** /*Outputs*/) noexcept override {}
    };
}
XPROPERTY_VREG(end_marker_node)

namespace
{
    struct end_marker_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("end_marker_node_factory", end_marker_node_factory)

        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "End"; }
        std::string_view getCategory() const noexcept override { return "Flow Control"; }

        xnode_os_node& CreateNodeInstance() override
        {
            auto* pNode = new end_marker_node();
            pNode->m_pFactory = this;
            return *pNode;
        }

        void DestroyNodeInstance(xnode_os_node& Node) override
        {
            delete static_cast<end_marker_node*>(&Node);
        }
    };
}
XPROPERTY_VREG(end_marker_node_factory)

extern "C" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host& /*Host*/) noexcept
{
    return *new end_marker_node_factory();
}

extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    delete static_cast<end_marker_node_factory*>(&Factory);
}
