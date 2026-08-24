// Random - fills a real gap in the corpus so far: nothing produces a non-deterministic value at
// all yet (Constant is fixed, MathExpression/Compare/BoolExpression are pure functions of their
// inputs). Min/Max follow the exact same "wireable Any... no, Float pin with a same-named literal
// backing store while unconnected" convention MathExpression's A/B already establish (see that
// file's own comment) - not a new pattern, just applied to a new node.
//
// One std::mt19937 PER INSTANCE (a plain member, not a static/global) - each node instance gets its
// own independent stream, and there's no cross-instance or cross-DLL shared state to worry about,
// matching this corpus's usual node-instance-is-plain-owning-memory model.
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include <cstdlib>
#include <random>
#include <format>
#include <cassert>

namespace
{
    struct random_node : xnode_os_node
    {
        float        m_Min           = 0.0f;  // used only while Min is unconnected - see FindMemberByName
        float        m_Max           = 1.0f;  // used only while Max is unconnected - see FindMemberByName
        bool         m_bMinConnected = false; // pushed by the host each frame - see "Min Connected"/PushPinConnectedFlags
        bool         m_bMaxConnected = false; // pushed by the host each frame - see "Max Connected"/PushPinConnectedFlags
        float        m_LastResult    = 0.0f;  // set by Execute() - live debug info, see "Last Result" below
        std::mt19937 m_Rng{ std::random_device{}() };

        XPROPERTY_VDEF
        ( "random_node", random_node
        , obj_member<"Min", &random_node::m_Min
            , member_dynamic_flags<+[](const random_node& O) { xproperty::flags::type F{}; F.m_bDontShow = F.m_bDontSave = O.m_bMinConnected; return F; }>
            , member_help<"Min's own value while its pin is unconnected - hidden once a wire is attached.">>
        , obj_member<"Max", &random_node::m_Max
            , member_dynamic_flags<+[](const random_node& O) { xproperty::flags::type F{}; F.m_bDontShow = F.m_bDontSave = O.m_bMaxConnected; return F; }>
            , member_help<"Max's own value while its pin is unconnected - hidden once a wire is attached.">>
        , obj_member<"Min Connected", &random_node::m_bMinConnected, member_flags<xproperty::flags::DONT_SAVE, xproperty::flags::DONT_SHOW>>
        , obj_member<"Max Connected", &random_node::m_bMaxConnected, member_flags<xproperty::flags::DONT_SAVE, xproperty::flags::DONT_SHOW>>
        , obj_member<"Last Result"
            , +[](const random_node& O, bool bRead, std::string& Value) { assert(bRead); Value = std::format("{}", O.m_LastResult); }
            , member_flags<xproperty::flags::SHOW_READONLY, xproperty::flags::DONT_SAVE>
            , member_help<"The value produced by the most recent Execute() - live debug info, never itself saved.">>
        )

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            static const xnode_os_port_desc s_Inputs[2] = { { "Min", "Float" }, { "Max", "Float" } };
            return s_Inputs;
        }
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            static const xnode_os_port_desc s_Outputs[1] = { { "Value", "Float" } };
            return s_Outputs;
        }
        void Execute(void** Inputs, void** Outputs) noexcept override
        {
            const float Min = Inputs[0] ? *static_cast<float*>(Inputs[0]) : m_Min;
            const float Max = Inputs[1] ? *static_cast<float*>(Inputs[1]) : m_Max;
            std::uniform_real_distribution<float> Dist(std::min(Min, Max), std::max(Min, Max));
            const float Result = Dist(m_Rng);
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
XPROPERTY_VREG(random_node)

namespace
{
    struct random_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("random_node_factory", random_node_factory)

        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "Random"; }
        std::string_view getCategory() const noexcept override { return "Math"; }

        xnode_os_node& CreateNodeInstance() override
        {
            auto* pNode = new random_node();
            pNode->m_pFactory = this;
            return *pNode;
        }
        void DestroyNodeInstance(xnode_os_node& Node) override
        {
            delete static_cast<random_node*>(&Node);
        }
    };
}
XPROPERTY_VREG(random_node_factory)

extern "C" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host& /*Host*/) noexcept
{
    return *new random_node_factory();
}
extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    delete static_cast<random_node_factory*>(&Factory);
}
