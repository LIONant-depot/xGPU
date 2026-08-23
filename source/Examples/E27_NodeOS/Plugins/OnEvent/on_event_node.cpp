// OnEvent - a pure documentation marker: no inputs, no outputs, no wiring implications at all. It
// exists purely so a human reading the graph can see WHICH event a nearby spine is meant to respond
// to (e.g. placed near an ExecutionCall - see execution_call_node.cpp) without OnEvent itself being
// part of the actual exec-flow wiring. The thing that actually fires an Exec pulse is
// ExecutionCall; OnEvent is just a label.
//
// WHICH event (tick, click, etc.) isn't modeled yet - deliberately just a bare marker for now, same
// "ship the mechanism, defer the taxonomy" approach this corpus already used for Constant's Type
// dropdown before it needed a real registry of types.
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"

namespace
{
    struct on_event_node : xnode_os_node
    {
        XPROPERTY_VDEF("on_event_node", on_event_node)

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            return {};
        }
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            return {};
        }
        void Execute(void** /*Inputs*/, void** /*Outputs*/) noexcept override {}
    };
}
XPROPERTY_VREG(on_event_node)

namespace
{
    struct on_event_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("on_event_node_factory", on_event_node_factory)

        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "OnEvent"; }
        std::string_view getCategory() const noexcept override { return "Flow Control"; }

        xnode_os_node& CreateNodeInstance() override
        {
            auto* pNode = new on_event_node();
            pNode->m_pFactory = this;
            return *pNode;
        }
        void DestroyNodeInstance(xnode_os_node& Node) override
        {
            delete static_cast<on_event_node*>(&Node);
        }
    };
}
XPROPERTY_VREG(on_event_node_factory)

extern "C" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host& /*Host*/) noexcept
{
    return *new on_event_node_factory();
}
extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    delete static_cast<on_event_node_factory*>(&Factory);
}
