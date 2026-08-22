// Not - same family as And/Or, but a single Bool in, Bool out.
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"

namespace
{
    struct not_node : xnode_os_node
    {
        XPROPERTY_VDEF("not_node", not_node)

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            static const xnode_os_port_desc s_Inputs[1] = { { "A", "Bool" } };
            return s_Inputs;
        }
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            static const xnode_os_port_desc s_Outputs[1] = { { "Result", "Bool" } };
            return s_Outputs;
        }
        void Execute(void** /*Inputs*/, void** /*Outputs*/) noexcept override {}
    };
}
XPROPERTY_VREG(not_node)

namespace
{
    struct not_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("not_node_factory", not_node_factory)

        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "Not"; }
        std::string_view getCategory() const noexcept override { return "Logic"; }

        xnode_os_node& CreateNodeInstance() override
        {
            auto* pNode = new not_node();
            pNode->m_pFactory = this;
            return *pNode;
        }
        void DestroyNodeInstance(xnode_os_node& Node) override
        {
            delete static_cast<not_node*>(&Node);
        }
    };
}
XPROPERTY_VREG(not_node_factory)

extern "C" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host& /*Host*/) noexcept
{
    return *new not_node_factory();
}
extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    delete static_cast<not_node_factory*>(&Factory);
}
