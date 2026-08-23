// ExecutionCall - a manually/externally triggerable spine entry point: pure Exec source, no inputs,
// one "Exec" output. This was originally named "OnEvent" - renamed once a second, genuinely empty
// "OnEvent" node (see on_event_node.cpp) was introduced as a pure documentation marker with no pins
// at all, to avoid two different things sharing one name. ExecutionCall is the thing that actually
// fires an Exec pulse; OnEvent is just a human-readable label for WHICH event a spine responds to,
// with no wiring implications of its own.
//
// Sits at the top of its own spine, same convention as Execute (see execute_node.cpp) - not
// enforced by the host, just where it makes sense to place it, since nothing ABOVE it in the same
// spine would ever run as a result of this trigger (a spine's own ordinary top-to-bottom sequencing
// is unaffected by exec pins; they only wire SEPARATE spines/events together - see
// NODE_SCRIPTING_DESIGN.md's exec-flow addition).
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"

namespace
{
    struct execution_call_node : xnode_os_node
    {
        XPROPERTY_VDEF("execution_call_node", execution_call_node)

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            return {};
        }
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            static const xnode_os_port_desc s_Outputs[1] = { { "Exec", "Exec" } };
            return s_Outputs;
        }
        void Execute(void** /*Inputs*/, void** /*Outputs*/) noexcept override {}
    };
}
XPROPERTY_VREG(execution_call_node)

namespace
{
    struct execution_call_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("execution_call_node_factory", execution_call_node_factory)

        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "ExecutionCall"; }
        std::string_view getCategory() const noexcept override { return "Flow Control"; }

        xnode_os_node& CreateNodeInstance() override
        {
            auto* pNode = new execution_call_node();
            pNode->m_pFactory = this;
            return *pNode;
        }
        void DestroyNodeInstance(xnode_os_node& Node) override
        {
            delete static_cast<execution_call_node*>(&Node);
        }
    };
}
XPROPERTY_VREG(execution_call_node_factory)

extern "C" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host& /*Host*/) noexcept
{
    return *new execution_call_node_factory();
}
extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    delete static_cast<execution_call_node_factory*>(&Factory);
}
