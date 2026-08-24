// Crash Test - a deliberate, permanent QA tool, NOT a real language feature: Execute() always
// null-derefs on purpose. This exists to verify that RunOrdinaryNode's SEH containment
// (E27_NodeOS_Editor.cpp's SEH_CallExecute) actually keeps the whole editor alive when a plugin's
// Execute() crashes, instead of taking the process down with it - the point being that adding a new,
// still-buggy plugin should never cost you the whole session.
//
// How to use it: drop one into the graph and run it. If the editor is still standing afterward and
// the node shows a red "Execute() crashed (exception code ...)" LastError right on its own body in
// the canvas, containment is working. If the whole app disappears instead, it isn't - or someone
// weakened/removed the wrapping around Node.m_pNode->Execute() in RunOrdinaryNode.
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"

namespace
{
    struct crash_test_node : xnode_os_node
    {
        XPROPERTY_VDEF("crash_test_node", crash_test_node)

        std::span<const xnode_os_port_desc> getInputs() const noexcept override { return {}; }
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override { return {}; }
        void Execute(void** /*Inputs*/, void** /*Outputs*/) noexcept override
        {
            int* pNull = nullptr;
            *pNull = 1; // deliberate access violation - see this file's own top comment
        }
    };
}
XPROPERTY_VREG(crash_test_node)

namespace
{
    struct crash_test_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("crash_test_node_factory", crash_test_node_factory)

        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "Crash Test"; }
        std::string_view getCategory() const noexcept override { return "Debug"; }

        xnode_os_node& CreateNodeInstance() override
        {
            auto* pNode = new crash_test_node();
            pNode->m_pFactory = this;
            return *pNode;
        }
        void DestroyNodeInstance(xnode_os_node& Node) override
        {
            delete static_cast<crash_test_node*>(&Node);
        }
    };
}
XPROPERTY_VREG(crash_test_node_factory)

extern "C" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host& /*Host*/) noexcept
{
    return *new crash_test_node_factory();
}
extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    delete static_cast<crash_test_node_factory*>(&Factory);
}
