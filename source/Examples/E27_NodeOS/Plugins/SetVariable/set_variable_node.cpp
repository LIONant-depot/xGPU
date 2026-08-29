// SetVariable - writes a named Float variable. See GetVariable's own comment for why this is a
// separate node type rather than a mode toggle on one shared node. A leaf-shaped write: takes a
// Value, produces nothing - like Print, it just runs when the spine reaches it (NODE_SCRIPTING_
// DESIGN.md section 4), no exec pin needed to sequence it. Execute() is a no-op for now, same as
// every other node type in this corpus.
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include "dependencies/xresource_guid/source/xresource_guid.h"
#include <string>

namespace
{
    struct set_variable_node : xnode_os_node
    {
        std::string m_Name          = "MyVariable";
        float       m_Value         = 0.0f;   // used only while Value is unconnected - see E27_NodeOS_Editor.cpp's FindMemberByName
        bool        m_bValueConnected = false; // pushed by the host each frame - see "Value Connected"/PushPinConnectedFlags

        // Stable per-instance guid for the fixed pin below - reflected (DONT_SHOW) so the saved value
        // is restored on load rather than a fresh xresource::guid_generator::Instance64() regenerating
        // (which would stop matching any saved link) - same pattern end_marker_node.cpp's
        // m_OwnerGuid/m_ElseEndGuid use.
        std::uint64_t m_ValueGuid = xresource::guid_generator::Instance64();

        XPROPERTY_VDEF
        ( "set_variable_node", set_variable_node
        , obj_member<"Name", &set_variable_node::m_Name, member_help<"The variable's name to write to. Must match a GetVariable node's own Name to resolve to the same storage slot once variable resolution is wired up.">>
        , obj_member<"Value", &set_variable_node::m_Value
            , member_dynamic_flags<+[](const set_variable_node& O) { xproperty::flags::type F{}; F.m_bDontShow = F.m_bDontSave = O.m_bValueConnected; return F; }>
            , member_help<"Value's own value while its pin is unconnected - hidden once a wire is attached, since the wire overrides it. Named to match the pin itself, so the host's generic 'find a property with the same name as this pin' hook picks it up automatically.">>
        , obj_member<"Value Connected", &set_variable_node::m_bValueConnected, member_flags<xproperty::flags::DONT_SAVE, xproperty::flags::DONT_SHOW>>
        , obj_member<"ValueGuid", &set_variable_node::m_ValueGuid, member_flags<xproperty::flags::DONT_SHOW>>
        )

        // Per-instance port guids (not a shared static array) - every pin needs its own stable identity
        // unique to THIS node instance so links can reference it by guid rather than by array position
        // (see xnode_os_port_desc::m_Guid's own comment; link_instance no longer stores a plain index).
        // Not const-only-initialized - getInputs() re-syncs m_Guid from the reflected field above on
        // every call, so a guid restored by deserialization AFTER construction still takes effect.
        mutable xnode_os_port_desc m_Inputs[1] = { { "Value", "Float", true, true, false, 0 } };

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            m_Inputs[0].m_Guid = m_ValueGuid;
            return m_Inputs;
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
