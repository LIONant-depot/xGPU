// SetVariable - writes a named Float variable. See GetVariable's own comment for why this is a
// separate node type rather than a mode toggle on one shared node. A leaf-shaped write: takes a
// Value, produces nothing - like Print, it just runs when the spine reaches it (NODE_SCRIPTING_
// DESIGN.md section 4), no exec pin needed to sequence it. Execute() is a no-op for now, same as
// every other node type in this corpus.
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include <string>

namespace
{
    struct set_variable_node : xnode_os_node
    {
        std::string m_Name = "MyVariable";

        XPROPERTY_VDEF
        ( "set_variable_node", set_variable_node
        , obj_member<"Name", &set_variable_node::m_Name>
        )

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            static const xnode_os_port_desc s_Inputs[1] = { { "Value", "Float" } };
            return s_Inputs;
        }
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            return {};
        }
        void Execute(void** /*Inputs*/, void** /*Outputs*/) noexcept override {}
    };
}
XPROPERTY_VREG(set_variable_node)

namespace
{
    struct set_variable_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("set_variable_node_factory", set_variable_node_factory)

        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "Set Variable"; }
        std::string_view getCategory() const noexcept override { return "Variables"; }

        xnode_os_node& CreateNodeInstance() override
        {
            auto* pNode = new set_variable_node();
            pNode->m_pFactory = this;
            return *pNode;
        }
        void DestroyNodeInstance(xnode_os_node& Node) override
        {
            delete static_cast<set_variable_node*>(&Node);
        }
    };
}
XPROPERTY_VREG(set_variable_node_factory)

extern "C" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host& /*Host*/) noexcept
{
    return *new set_variable_node_factory();
}
extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    delete static_cast<set_variable_node_factory*>(&Factory);
}
