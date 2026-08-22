// Print - a leaf action node: takes a Value, produces nothing. No "Exec" input pin (that Blueprint-
// style plumbing was removed entirely - see E27_NodeOS_Editor.cpp's IsNoPreviewType comment): under
// the flat-spine execution model (NODE_SCRIPTING_DESIGN.md section 4), this node simply runs when
// the spine reaches it, same as everything else on the spine - nothing needs to be wired in or out
// of it to sequence it.
//
// Value is typed "Any" - a wildcard, not a fixed concrete type (same mechanism as Compare/Math
// Expression's A/B - see E27_NodeOS_Editor.cpp's ResolveNodeWildcardType) - because the actual rule
// for "can this be printed" is simply "does operator<< exist for it", which is a C++/compiler-level
// question, not something this node's own type declaration can answer: once compilation is wired
// up, whatever's wired into Value just needs to satisfy operator<<(std::ostream&, T) - true for
// every atomic scalar in this corpus and any xproperty-registered type that defines one - so there's
// nothing to enumerate or restrict here at all. Execute() is a no-op for now, same as every other
// node type in this corpus.
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"

namespace
{
    struct print_node : xnode_os_node
    {
        XPROPERTY_VDEF("print_node", print_node)

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            static const xnode_os_port_desc s_Inputs[1] = { { "Value", "Any" } };
            return s_Inputs;
        }
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            return {};
        }
        void Execute(void** /*Inputs*/, void** /*Outputs*/) noexcept override {}
    };
}
XPROPERTY_VREG(print_node)

namespace
{
    struct print_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("print_node_factory", print_node_factory)

        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "Print"; }
        std::string_view getCategory() const noexcept override { return "Debug"; }

        xnode_os_node& CreateNodeInstance() override
        {
            auto* pNode = new print_node();
            pNode->m_pFactory = this;
            return *pNode;
        }
        void DestroyNodeInstance(xnode_os_node& Node) override
        {
            delete static_cast<print_node*>(&Node);
        }
    };
}
XPROPERTY_VREG(print_node_factory)

extern "C" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host& /*Host*/) noexcept
{
    return *new print_node_factory();
}
extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    delete static_cast<print_node_factory*>(&Factory);
}
