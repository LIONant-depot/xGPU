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
#include "dependencies/xresource_guid/source/xresource_guid.h"

namespace
{
    struct execute_node : xnode_os_node
    {
        // Stable per-instance guid for the "Exec" input pin below - reflected (DONT_SHOW) so the
        // saved value is restored on load rather than a fresh xresource::guid_generator::Instance64()
        // regenerating (which would stop matching any saved link) - same pattern end_marker_node.cpp's
        // m_OwnerGuid uses.
        std::uint64_t m_ExecGuid = xresource::guid_generator::Instance64();

        XPROPERTY_VDEF
        ( "execute_node", execute_node
        , obj_member<"ExecGuid", &execute_node::m_ExecGuid, member_flags<xproperty::flags::DONT_SHOW>>
        )

        // Per-instance port guid (not a shared static array) - every pin needs its own stable identity
        // unique to THIS node instance so links can reference it by guid rather than by array position
        // (see xnode_os_port_desc::m_Guid's own comment; link_instance no longer stores a plain index).
        // Not const-only-initialized - getInputs() re-syncs m_Guid from the reflected field above on
        // every call, so a guid restored by deserialization AFTER construction still takes effect.
        mutable xnode_os_port_desc m_Inputs[1] = { { "Exec", "Exec", true, true, false, 0 } };

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            m_Inputs[0].m_Guid = m_ExecGuid;
            return m_Inputs;
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
