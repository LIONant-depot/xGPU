// Execute - a callable entry point into its own spine, triggered by an "Exec" input (from an
// OnEvent, or from a Call - see call_node.cpp). Deliberately owns NO scope (needsOwnedEndMarker() is
// the inherited default: false) - unlike Function, which opens a real body with its own parameter/
// return-value scope, Execute is just a label partway (conventionally at the top) of an ordinary
// spine: whatever follows it just runs via the flat-spine's own normal top-to-bottom sequencing,
// with no isolated local scope of its own. That's the whole distinction the user drew between the
// two: a Call into a Function is a real subroutine call (its own parameters, its own locals); a
// Call into an Execute reads/writes whatever's already reachable from where it sits - closer to
// invoking a C++ lambda captured by reference ([&]) than calling a real function.
//
// No Exec output either: an Execute doesn't "return" anywhere on its own - it's the calling Call
// node whose OWN Exec output fires once whatever follows Execute in its spine finishes running.
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"

namespace
{
    struct execute_node : xnode_os_node
    {
        XPROPERTY_VDEF("execute_node", execute_node)

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            static const xnode_os_port_desc s_Inputs[1] = { { "Exec", "Exec" } };
            return s_Inputs;
        }
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            return {};
        }
        void Execute(void** /*Inputs*/, void** /*Outputs*/) noexcept override {}
    };
}
XPROPERTY_VREG(execute_node)

namespace
{
    struct execute_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("execute_node_factory", execute_node_factory)

        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "Execute"; }
        std::string_view getCategory() const noexcept override { return "Flow Control"; }

        xnode_os_node& CreateNodeInstance() override
        {
            auto* pNode = new execute_node();
            pNode->m_pFactory = this;
            return *pNode;
        }
        void DestroyNodeInstance(xnode_os_node& Node) override
        {
            delete static_cast<execute_node*>(&Node);
        }
    };
}
XPROPERTY_VREG(execute_node_factory)

extern "C" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host& /*Host*/) noexcept
{
    return *new execute_node_factory();
}
extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    delete static_cast<execute_node_factory*>(&Factory);
}
