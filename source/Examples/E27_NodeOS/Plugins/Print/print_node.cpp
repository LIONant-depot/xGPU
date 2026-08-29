// Print - a leaf action node: takes a Value, produces nothing. No "Exec" input pin needed to
// sequence IT specifically (under the flat-spine execution model, it just runs when the spine
// reaches it) - Exec pins came back narrowly for OnEvent/ExecutionCall/Execute/Function/Call only
// (see E27_NodeOS_Editor.cpp's IsNoPreviewType comment), not for ordinary leaf actions like this one.
//
// Value is typed "Any" - a wildcard, not a fixed concrete type (same mechanism as Compare/Math
// Expression's A/B - see E27_NodeOS_Editor.cpp's ResolveNodeWildcardType) - because the actual rule
// for "can this be printed" is simply "does operator<< exist for it", which is a C++/compiler-level
// question, not something this node's own type declaration can answer.
//
// Execute() reads Value as a Float and routes it through ixnode_os_host::Log() - the ONE sanctioned
// way for a plugin to reach host services (xnode_os_plugin_api.h's own doc), stored here from the
// Host reference NodeOS_CreateFactory receives once at plugin load, handed to each instance in
// CreateNodeInstance(). Float-only for now: every value in this corpus is Float today, and Value's
// wildcard type isn't threaded into Execute()'s type-erased void* signature at all - genuinely
// dispatching on the resolved type would need that plumbing added to the plugin ABI, deferred until
// a real second value type actually needs printing.
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include "dependencies/xresource_guid/source/xresource_guid.h"
#include <cstdio>
#include <string>
#include <cassert>

namespace
{
    struct print_node : xnode_os_node
    {
        ixnode_os_host* m_pHost      = nullptr; // wiring infra, set in CreateNodeInstance - not user state, stays unreflected like m_pFactory
        std::string     m_LastPrinted;          // set by Execute() - live debug info, see "Last Printed" below

        // Stable per-instance guid for the fixed pin below - reflected (DONT_SHOW) so the saved value
        // is restored on load rather than a fresh xresource::guid_generator::Instance64() regenerating
        // (which would stop matching any saved link) - same pattern end_marker_node.cpp's
        // m_OwnerGuid/m_ElseEndGuid use.
        std::uint64_t m_ValueGuid = xresource::guid_generator::Instance64();

        XPROPERTY_VDEF
        ( "print_node", print_node
        , obj_member<"Last Printed"
            , +[](const print_node& O, bool bRead, std::string& Value) { assert(bRead); Value = O.m_LastPrinted; }
            , member_flags<xproperty::flags::SHOW_READONLY, xproperty::flags::DONT_SAVE>
            , member_help<"What this node actually printed the last time it ran - live debug info, never itself saved. Empty until Execute() runs at least once.">>
        , obj_member<"ValueGuid", &print_node::m_ValueGuid, member_flags<xproperty::flags::DONT_SHOW>>
        )

        // Per-instance port guids (not a shared static array) - every pin needs its own stable identity
        // unique to THIS node instance so links can reference it by guid rather than by array position
        // (see xnode_os_port_desc::m_Guid's own comment; link_instance no longer stores a plain index).
        // Not const-only-initialized - getInputs() re-syncs m_Guid from the reflected field above on
        // every call, so a guid restored by deserialization AFTER construction still takes effect.
        mutable xnode_os_port_desc m_Inputs[1] = { { "Value", "Any", true, true, false, 0 } };

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            m_Inputs[0].m_Guid = m_ValueGuid;
            return m_Inputs;
        }
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            return {};
        }
        void Execute(void** Inputs, void** /*Outputs*/) noexcept override
        {
            if (!m_pHost || !Inputs[0]) return;
            char Buf[64];
            std::snprintf(Buf, sizeof(Buf), "%.2f", *static_cast<const float*>(Inputs[0]));
            m_LastPrinted = Buf;
            m_pHost->Log(Buf);
        }
    };
}
XPROPERTY_VREG(print_node)

namespace
{
    struct print_node_factory : xnode_os_node_factory
    {
        ixnode_os_host* m_pHost = nullptr;

        XPROPERTY_VDEF("print_node_factory", print_node_factory)

        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "Print"; }
        std::string_view getCategory() const noexcept override { return "Debug"; }

        xnode_os_node& CreateNodeInstance() override
        {
            auto* pNode = new print_node();
            pNode->m_pFactory = this;
            pNode->m_pHost    = m_pHost;
            return *pNode;
        }
        void DestroyNodeInstance(xnode_os_node& Node) override
        {
            delete static_cast<print_node*>(&Node);
        }
    };
}
XPROPERTY_VREG(print_node_factory)

extern "C" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host& Host) noexcept
{
    auto* pFactory = new print_node_factory();
    pFactory->m_pHost = &Host;
    return *pFactory;
}
extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    delete static_cast<print_node_factory*>(&Factory);
}
