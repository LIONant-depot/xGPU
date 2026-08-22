// If - a control-flow node under the flat-spine, recursive-descent model
// (NODE_SCRIPTING_DESIGN.md section 4). Only one data pin, Condition - no exec pins at all. Its
// true-branch content is just whatever nodes physically follow it in the same spine, up to its
// owned End marker (created automatically alongside it - see needsOwnedEndMarker() below). Execute
// is a no-op here: this is the UI/ownership half of the feature only, compilation isn't wired up
// yet (see the design doc's own history of the compiler prototype, which is a separate, standalone
// codepath under Scripting/).
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"

namespace
{
    struct if_node : xnode_os_node
    {
        XPROPERTY_VDEF("if_node", if_node)

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            static const xnode_os_port_desc s_Inputs[1] = { { "Condition", "Bool" } };
            return s_Inputs;
        }

        // "End" is the read-only ownership pin - the host creates and connects it automatically,
        // to this node's own owned End marker, in CreateOwnedPair. Never dragged by the user (see
        // link_instance::m_bReadOnly in E27_NodeOS_Editor.cpp).
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            static const xnode_os_port_desc s_Outputs[1] = { { "End", "Scope" } };
            return s_Outputs;
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
