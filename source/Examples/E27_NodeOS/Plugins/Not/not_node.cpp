// Not - same family as And/Or, but a single Bool in, Bool out.
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include "dependencies/xresource_guid/source/xresource_guid.h"

namespace
{
    struct not_node : xnode_os_node
    {
        // Stable per-instance guids for the two fixed pins below - reflected (DONT_SHOW) so the saved
        // value is restored on load rather than a fresh xresource::guid_generator::Instance64()
        // regenerating (which would stop matching any saved link) - same pattern end_marker_node.cpp's
        // m_OwnerGuid/m_ElseEndGuid use.
        std::uint64_t m_AGuid      = xresource::guid_generator::Instance64();
        std::uint64_t m_ResultGuid = xresource::guid_generator::Instance64();

        XPROPERTY_VDEF
        ( "not_node", not_node
        , obj_member<"AGuid",      &not_node::m_AGuid,      member_flags<xproperty::flags::DONT_SHOW>>
        , obj_member<"ResultGuid", &not_node::m_ResultGuid, member_flags<xproperty::flags::DONT_SHOW>>
        )

        // Per-instance port guids (not a shared static array) - every pin needs its own stable identity
        // unique to THIS node instance so links can reference it by guid rather than by array position
        // (see xnode_os_port_desc::m_Guid's own comment; link_instance no longer stores a plain index).
        // Not const-only-initialized - getInputs()/getOutputs() re-sync m_Guid from the reflected
        // fields above on every call, so a guid restored by deserialization AFTER construction still
        // takes effect.
        mutable xnode_os_port_desc m_Inputs[1]  = { { "A", "Bool", true, true, false, 0 } };
        mutable xnode_os_port_desc m_Outputs[1] = { { "Result", "Bool", true, true, false, 0 } };

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            m_Inputs[0].m_Guid = m_AGuid;
            return m_Inputs;
        }
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            m_Outputs[0].m_Guid = m_ResultGuid;
            return m_Outputs;
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
