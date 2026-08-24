// Clamp - a genuinely missing numeric utility (nothing in the corpus so far restricts a value to a
// range), not a redundant restatement of MathExpression's Operator dropdown - three real inputs
// (Value/Min/Max), not two, so it doesn't fit that consolidation. Same wireable-pin-with-literal-
// fallback convention as MathExpression's A/B (see that file's own comment) for all three.
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
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
        , obj_member<"Value Connected", &clamp_node::m_bValueConnected, member_flags<xproperty::flags::DONT_SAVE, xproperty::flags::DONT_SHOW>>
        , obj_member<"Min Connected",   &clamp_node::m_bMinConnected,   member_flags<xproperty::flags::DONT_SAVE, xproperty::flags::DONT_SHOW>>
        , obj_member<"Max Connected",   &clamp_node::m_bMaxConnected,   member_flags<xproperty::flags::DONT_SAVE, xproperty::flags::DONT_SHOW>>
        , obj_member<"Last Result"
            , +[](const clamp_node& O, bool bRead, std::string& Value) { assert(bRead); Value = std::format("{}", O.m_LastResult); }
            , member_flags<xproperty::flags::SHOW_READONLY, xproperty::flags::DONT_SAVE>
            , member_help<"The value produced by the most recent Execute() - live debug info, never itself saved.">>
        )

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            static const xnode_os_port_desc s_Inputs[3] = { { "Value", "Float" }, { "Min", "Float" }, { "Max", "Float" } };
            return s_Inputs;
        }
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            static const xnode_os_port_desc s_Outputs[1] = { { "Result", "Float" } };
            return s_Outputs;
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
