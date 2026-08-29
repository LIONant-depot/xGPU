// Clamp - a genuinely missing numeric utility (nothing in the corpus so far restricts a value to a
// range), not a redundant restatement of MathExpression's Operator dropdown - three real inputs
// (Value/Min/Max), not two, so it doesn't fit that consolidation. Same wireable-pin-with-literal-
// fallback convention as MathExpression's A/B (see that file's own comment) for all three.
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include "dependencies/xresource_guid/source/xresource_guid.h"
#include <cstdlib>
#include <algorithm>
#include <format>
#include <cassert>

namespace
{
    struct clamp_node : xnode_os_node
    {
        float m_Value           = 0.0f;
        float m_Min             = 0.0f;
        float m_Max             = 1.0f;
        bool  m_bValueConnected = false;
        bool  m_bMinConnected   = false;
        bool  m_bMaxConnected   = false;
        float m_LastResult      = 0.0f; // set by Execute() - live debug info, see "Last Result" below

        // Stable per-instance guids for the pins below - reflected (DONT_SHOW) so the saved value is
        // restored on load rather than a fresh xresource::guid_generator::Instance64() regenerating
        // (which would stop matching any saved link) - same pattern end_marker_node.cpp's m_OwnerGuid
        // uses.
        std::uint64_t m_ValueGuid  = xresource::guid_generator::Instance64();
        std::uint64_t m_MinGuid    = xresource::guid_generator::Instance64();
        std::uint64_t m_MaxGuid    = xresource::guid_generator::Instance64();
        std::uint64_t m_ResultGuid = xresource::guid_generator::Instance64();

        XPROPERTY_VDEF
        ( "clamp_node", clamp_node
        , obj_member<"Value", &clamp_node::m_Value
            , member_dynamic_flags<+[](const clamp_node& O) { xproperty::flags::type F{}; F.m_bDontShow = F.m_bDontSave = O.m_bValueConnected; return F; }>
            , member_help<"Value's own value while its pin is unconnected - hidden once a wire is attached.">>
        , obj_member<"Min", &clamp_node::m_Min
            , member_dynamic_flags<+[](const clamp_node& O) { xproperty::flags::type F{}; F.m_bDontShow = F.m_bDontSave = O.m_bMinConnected; return F; }>
            , member_help<"Min's own value while its pin is unconnected - hidden once a wire is attached.">>
        , obj_member<"Max", &clamp_node::m_Max
            , member_dynamic_flags<+[](const clamp_node& O) { xproperty::flags::type F{}; F.m_bDontShow = F.m_bDontSave = O.m_bMaxConnected; return F; }>
            , member_help<"Max's own value while its pin is unconnected - hidden once a wire is attached.">>
        // Deliberately NOT DONT_SAVE - see random_node.cpp's own comment on why. Clamp has three
        // inputs that can all be simultaneously wired, unlike MathExpression/Compare's own two,
        // making it even more likely to hit the "zero saved properties" case this guards against.
        , obj_member<"Value Connected", &clamp_node::m_bValueConnected, member_flags<xproperty::flags::DONT_SHOW>>
        , obj_member<"Min Connected",   &clamp_node::m_bMinConnected,   member_flags<xproperty::flags::DONT_SHOW>>
        , obj_member<"Max Connected",   &clamp_node::m_bMaxConnected,   member_flags<xproperty::flags::DONT_SHOW>>
        , obj_member<"Last Result"
            , +[](const clamp_node& O, bool bRead, std::string& Value) { assert(bRead); Value = std::format("{}", O.m_LastResult); }
            , member_flags<xproperty::flags::SHOW_READONLY, xproperty::flags::DONT_SAVE>
            , member_help<"The value produced by the most recent Execute() - live debug info, never itself saved.">>
        , obj_member<"ValueGuid",  &clamp_node::m_ValueGuid,  member_flags<xproperty::flags::DONT_SHOW>>
        , obj_member<"MinGuid",    &clamp_node::m_MinGuid,    member_flags<xproperty::flags::DONT_SHOW>>
        , obj_member<"MaxGuid",    &clamp_node::m_MaxGuid,    member_flags<xproperty::flags::DONT_SHOW>>
        , obj_member<"ResultGuid", &clamp_node::m_ResultGuid, member_flags<xproperty::flags::DONT_SHOW>>
        )

        // Per-instance port guids (not a shared static array) - every pin needs its own stable identity
        // unique to THIS node instance so links can reference it by guid rather than by array position
        // (see xnode_os_port_desc::m_Guid's own comment; link_instance no longer stores a plain index).
        // Not const-only-initialized - getInputs()/getOutputs() re-sync m_Guid from the reflected
        // fields above on every call, so a guid restored by deserialization AFTER construction still
        // takes effect.
        mutable xnode_os_port_desc m_Inputs[3]  = { { "Value", "Float", true, true, false, 0 }
                                                   , { "Min",   "Float", true, true, false, 0 }
                                                   , { "Max",   "Float", true, true, false, 0 }
                                                   };
        mutable xnode_os_port_desc m_Outputs[1] = { { "Result", "Float", true, true, false, 0 } };

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            m_Inputs[0].m_Guid = m_ValueGuid;
            m_Inputs[1].m_Guid = m_MinGuid;
            m_Inputs[2].m_Guid = m_MaxGuid;
            return m_Inputs;
        }
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            m_Outputs[0].m_Guid = m_ResultGuid;
            return m_Outputs;
        }
        void Execute(void** Inputs, void** Outputs) noexcept override
        {
            const float Value = Inputs[0] ? *static_cast<float*>(Inputs[0]) : m_Value;
            const float Min   = Inputs[1] ? *static_cast<float*>(Inputs[1]) : m_Min;
            const float Max   = Inputs[2] ? *static_cast<float*>(Inputs[2]) : m_Max;
            // std::clamp is UB when Min > Max - a swapped pair (someone wired Min/Max backwards) is a
            // real, reachable graph state here, not a "can't happen," so this is done by hand instead.
            const float Lo = std::min(Min, Max);
            const float Hi = std::max(Min, Max);
            const float Result = std::min(std::max(Value, Lo), Hi);
            m_LastResult = Result;
            auto* p = static_cast<float*>(std::malloc(sizeof(float)));
            *p = Result;
            Outputs[0] = p;
        }
        void FreeOutputs(void** Outputs) noexcept override
        {
            std::free(Outputs[0]);
        }
    };
}
XPROPERTY_VREG(clamp_node)

namespace
{
    struct clamp_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("clamp_node_factory", clamp_node_factory)

        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "Clamp"; }
        std::string_view getCategory() const noexcept override { return "Math"; }

        xnode_os_node& CreateNodeInstance() override
        {
            auto* pNode = new clamp_node();
            pNode->m_pFactory = this;
            return *pNode;
        }
        void DestroyNodeInstance(xnode_os_node& Node) override
        {
            delete static_cast<clamp_node*>(&Node);
        }
    };
}
XPROPERTY_VREG(clamp_node_factory)

extern "C" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host& /*Host*/) noexcept
{
    return *new clamp_node_factory();
}
extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    delete static_cast<clamp_node_factory*>(&Factory);
}
