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

namespace
{
    struct end_marker_node : xnode_os_node
    {
        bool m_bIsElse = false;

        XPROPERTY_VDEF
        ( "end_marker_node", end_marker_node
        , obj_member<"IsElse", &end_marker_node::m_bIsElse>
        )

        // "Owner" is the read-only ownership pin - the host connects it automatically, to whichever
        // If/ForEachLoop (or End-Else) created this marker. Never dragged by the user.
        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            static const xnode_os_port_desc s_Inputs[1] = { { "Owner", "Scope" } };
            return s_Inputs;
        }

        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            static const xnode_os_port_desc s_Outputs[1] = { { "ElseEnd", "Scope" } };
            return m_bIsElse ? std::span<const xnode_os_port_desc>(s_Outputs) : std::span<const xnode_os_port_desc>{};
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
