// If - a control-flow node under the flat-spine, recursive-descent model
// (NODE_SCRIPTING_DESIGN.md section 4). Only one data pin, Condition - no exec pins at all. Its
// true-branch content is just whatever nodes physically follow it in the same spine, up to its
// owned End marker (created automatically alongside it - see needsOwnedEndMarker() below). Execute
// is a no-op here: this is the UI/ownership half of the feature only, compilation isn't wired up
// yet (see the design doc's own history of the compiler prototype, which is a separate, standalone
// codepath under Scripting/).
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include "dependencies/xresource_guid/source/xresource_guid.h"

namespace
{
    struct if_node : xnode_os_node
    {
        // Stable per-instance guids for the fixed pins below - reflected (DONT_SHOW) so the saved
        // values are restored on load rather than a fresh xresource::guid_generator::Instance64()
        // regenerating (which would stop matching any saved link) - same pattern end_marker_node.cpp's
        // m_OwnerGuid/m_ElseEndGuid use.
        std::uint64_t m_ConditionGuid = xresource::guid_generator::Instance64();
        std::uint64_t m_EndGuid       = xresource::guid_generator::Instance64();

        XPROPERTY_VDEF
        ( "if_node", if_node
        , obj_member<"ConditionGuid", &if_node::m_ConditionGuid, member_flags<xproperty::flags::DONT_SHOW>>
        , obj_member<"EndGuid",       &if_node::m_EndGuid,       member_flags<xproperty::flags::DONT_SHOW>>
        )

        // Not const-only-initialized - getInputs()/getOutputs() re-sync m_Guid from the reflected
        // fields above on every call, so a guid restored by deserialization AFTER construction still
        // takes effect.
        mutable xnode_os_port_desc m_Inputs[1]  = { { "Condition", "Bool", true, true, false, 0 } };
        mutable xnode_os_port_desc m_Outputs[1] = { { "End", "Scope", true, true, false, 0 } };

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            m_Inputs[0].m_Guid = m_ConditionGuid;
            return m_Inputs;
        }

        // "End" is the read-only ownership pin - the host creates and connects it automatically,
        // to this node's own owned End marker, in CreateOwnedPair. Never dragged by the user (see
        // link_instance::m_bReadOnly in E27_NodeOS_Editor.cpp).
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            m_Outputs[0].m_Guid = m_EndGuid;
            return m_Outputs;
        }
        void Execute(void** /*Inputs*/, void** /*Outputs*/) noexcept override {}
    };
}
XPROPERTY_VREG(if_node)

namespace
{
    struct if_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("if_node_factory", if_node_factory)

        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "If"; }
        std::string_view getCategory() const noexcept override { return "Flow Control"; }

        // Owns a plain End marker by default - upgrading it to an End-Else (adding a false branch)
        // is a separate, not-yet-built editor interaction (see Plugins/EndElse's own file comment).
        bool             needsOwnedEndMarker()        const noexcept override { return true; }
        std::string_view getOwnedEndMarkerPluginDir() const noexcept override { return "End"; }

        xnode_os_node& CreateNodeInstance() override
        {
            auto* pNode = new if_node();
            pNode->m_pFactory = this;
            return *pNode;
        }

        void DestroyNodeInstance(xnode_os_node& Node) override
        {
            delete static_cast<if_node*>(&Node);
        }
    };
}
XPROPERTY_VREG(if_node_factory)

extern "C" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host& /*Host*/) noexcept
{
    return *new if_node_factory();
}

extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    delete static_cast<if_node_factory*>(&Factory);
}
