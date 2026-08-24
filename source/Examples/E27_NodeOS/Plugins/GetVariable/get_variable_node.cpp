// GetVariable - reads a named Float variable. Paired with SetVariable (see that file's own
// comment for why this is two separate node types rather than one with a mode switch: their pin
// shapes are opposites - source vs. sink - not a shared shape with a toggled detail, unlike Compare/
// Logic/Math's same-shape-different-operator consolidation). "Name" is a plain reflected string
// property, edited in the side panel exactly like Cube's Width/Height/Depth floats - the host's
// generic property-snapshot machinery already handles STRING members with no plugin-side UI code.
// Execute() is a no-op for now, same as every other node type in this corpus - resolving "Name" to
// an actual storage slot is compiler/runtime work, left for when that's wired up.
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include <string>

namespace
{
    struct get_variable_node : xnode_os_node
    {
        std::string m_Name = "MyVariable";

        XPROPERTY_VDEF
        ( "get_variable_node", get_variable_node
        , obj_member<"Name", &get_variable_node::m_Name, member_help<"The variable's name to read from. Must match a SetVariable node's own Name to resolve to the same storage slot once variable resolution is wired up.">>
        )

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            return {};
        }
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            static const xnode_os_port_desc s_Outputs[1] = { { "Value", "Float" } };
            return s_Outputs;
        }
        void Execute(void** /*Inputs*/, void** /*Outputs*/) noexcept override {}
    };
}
XPROPERTY_VREG(get_variable_node)

namespace
{
    struct get_variable_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("get_variable_node_factory", get_variable_node_factory)

        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "Get Variable"; }
        std::string_view getCategory() const noexcept override { return "Variables"; }

        xnode_os_node& CreateNodeInstance() override
        {
            auto* pNode = new get_variable_node();
            pNode->m_pFactory = this;
            return *pNode;
        }
        void DestroyNodeInstance(xnode_os_node& Node) override
        {
            delete static_cast<get_variable_node*>(&Node);
        }
    };
}
XPROPERTY_VREG(get_variable_node_factory)

extern "C" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host& /*Host*/) noexcept
{
    return *new get_variable_node_factory();
}
extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    delete static_cast<get_variable_node_factory*>(&Factory);
}
