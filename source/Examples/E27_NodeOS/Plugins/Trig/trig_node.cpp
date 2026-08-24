// Trig - the first multi-type-per-DLL plugin in this corpus (see xnode_os_plugin_api.h's
// NodeOS_CreateFactories): Sin/Cos/Tan, three distinct node TYPES registered from ONE compiled DLL,
// instead of three separate Plugins/ folders each needing their own compile+load - the "too many
// DLLs" scaling concern this ABI addition exists for. Also fills a genuine gap: MathExpression only
// covers +,-,*,/ - nothing computes a trig function yet.
//
// Deliberately three separate hand-written structs (not one template parameterized by which libm
// function to call), matching every other node type in this corpus (MathExpression/Compare/
// BoolExpression are each one hand-written struct too) - and specifically NOT templated because
// XPROPERTY_VDEF's first argument is a plain string name used to derive this type's reflection
// GUID; three template instantiations sharing that same literal would register three different
// C++ types under one colliding reflected name, which is a real registry hazard, not just style.
// Each gets its own distinct name ("sin_node"/"cos_node"/"tan_node") for exactly that reason.
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include <cmath>
#include <cstdlib>
#include <format>
#include <cassert>

namespace
{
    struct sin_node : xnode_os_node
    {
        float m_Angle           = 0.0f; // radians - used only while Angle is unconnected
        bool  m_bAngleConnected = false;
        float m_LastResult      = 0.0f; // set by Execute() - live debug info

        XPROPERTY_VDEF
        ( "sin_node", sin_node
        , obj_member<"Angle", &sin_node::m_Angle
            , member_dynamic_flags<+[](const sin_node& O) { xproperty::flags::type F{}; F.m_bDontShow = F.m_bDontSave = O.m_bAngleConnected; return F; }>
            , member_help<"Angle in radians, while its pin is unconnected - hidden once a wire is attached.">>
        // Deliberately NOT DONT_SAVE - see random_node.cpp's own comment on why (a node with only
        // ONE input, always DontSave once connected, has nothing else keeping HasAnyProperties()
        // true - exactly the case that broke Load when this and cos_node's own Angle were both wired).
        , obj_member<"Angle Connected", &sin_node::m_bAngleConnected, member_flags<xproperty::flags::DONT_SHOW>>
        , obj_member<"Last Result"
            , +[](const sin_node& O, bool bRead, std::string& Value) { assert(bRead); Value = std::format("{}", O.m_LastResult); }
            , member_flags<xproperty::flags::SHOW_READONLY, xproperty::flags::DONT_SAVE>
            , member_help<"The value produced by the most recent Execute() - live debug info, never itself saved.">>
        )

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            static const xnode_os_port_desc s_Inputs[1] = { { "Angle", "Float" } };
            return s_Inputs;
        }
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            static const xnode_os_port_desc s_Outputs[1] = { { "Result", "Float" } };
            return s_Outputs;
        }
        void Execute(void** Inputs, void** Outputs) noexcept override
        {
            const float Angle = Inputs[0] ? *static_cast<float*>(Inputs[0]) : m_Angle;
            const float Result = std::sin(Angle);
            m_LastResult = Result;
            auto* p = static_cast<float*>(std::malloc(sizeof(float)));
            *p = Result;
            Outputs[0] = p;
        }
        void FreeOutputs(void** Outputs) noexcept override { std::free(Outputs[0]); }
    };

    struct cos_node : xnode_os_node
    {
        float m_Angle           = 0.0f;
        bool  m_bAngleConnected = false;
        float m_LastResult      = 0.0f;

        XPROPERTY_VDEF
        ( "cos_node", cos_node
        , obj_member<"Angle", &cos_node::m_Angle
            , member_dynamic_flags<+[](const cos_node& O) { xproperty::flags::type F{}; F.m_bDontShow = F.m_bDontSave = O.m_bAngleConnected; return F; }>
            , member_help<"Angle in radians, while its pin is unconnected - hidden once a wire is attached.">>
        // Deliberately NOT DONT_SAVE - see sin_node's own comment on why.
        , obj_member<"Angle Connected", &cos_node::m_bAngleConnected, member_flags<xproperty::flags::DONT_SHOW>>
        , obj_member<"Last Result"
            , +[](const cos_node& O, bool bRead, std::string& Value) { assert(bRead); Value = std::format("{}", O.m_LastResult); }
            , member_flags<xproperty::flags::SHOW_READONLY, xproperty::flags::DONT_SAVE>
            , member_help<"The value produced by the most recent Execute() - live debug info, never itself saved.">>
        )

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            static const xnode_os_port_desc s_Inputs[1] = { { "Angle", "Float" } };
            return s_Inputs;
        }
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            static const xnode_os_port_desc s_Outputs[1] = { { "Result", "Float" } };
            return s_Outputs;
        }
        void Execute(void** Inputs, void** Outputs) noexcept override
        {
            const float Angle = Inputs[0] ? *static_cast<float*>(Inputs[0]) : m_Angle;
            const float Result = std::cos(Angle);
            m_LastResult = Result;
            auto* p = static_cast<float*>(std::malloc(sizeof(float)));
            *p = Result;
            Outputs[0] = p;
        }
        void FreeOutputs(void** Outputs) noexcept override { std::free(Outputs[0]); }
    };

    struct tan_node : xnode_os_node
    {
        float m_Angle           = 0.0f;
        bool  m_bAngleConnected = false;
        float m_LastResult      = 0.0f;

        XPROPERTY_VDEF
        ( "tan_node", tan_node
        , obj_member<"Angle", &tan_node::m_Angle
            , member_dynamic_flags<+[](const tan_node& O) { xproperty::flags::type F{}; F.m_bDontShow = F.m_bDontSave = O.m_bAngleConnected; return F; }>
            , member_help<"Angle in radians, while its pin is unconnected - hidden once a wire is attached.">>
        // Deliberately NOT DONT_SAVE - see sin_node's own comment on why.
        , obj_member<"Angle Connected", &tan_node::m_bAngleConnected, member_flags<xproperty::flags::DONT_SHOW>>
        , obj_member<"Last Result"
            , +[](const tan_node& O, bool bRead, std::string& Value) { assert(bRead); Value = std::format("{}", O.m_LastResult); }
            , member_flags<xproperty::flags::SHOW_READONLY, xproperty::flags::DONT_SAVE>
            , member_help<"The value produced by the most recent Execute() - live debug info, never itself saved.">>
        )

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            static const xnode_os_port_desc s_Inputs[1] = { { "Angle", "Float" } };
            return s_Inputs;
        }
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            static const xnode_os_port_desc s_Outputs[1] = { { "Result", "Float" } };
            return s_Outputs;
        }
        void Execute(void** Inputs, void** Outputs) noexcept override
        {
            const float Angle = Inputs[0] ? *static_cast<float*>(Inputs[0]) : m_Angle;
            const float Result = std::tan(Angle);
            m_LastResult = Result;
            auto* p = static_cast<float*>(std::malloc(sizeof(float)));
            *p = Result;
            Outputs[0] = p;
        }
        void FreeOutputs(void** Outputs) noexcept override { std::free(Outputs[0]); }
    };
}
XPROPERTY_VREG(sin_node)
XPROPERTY_VREG(cos_node)
XPROPERTY_VREG(tan_node)

namespace
{
    struct sin_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("sin_node_factory", sin_node_factory)
        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "Sin"; }
        std::string_view getCategory() const noexcept override { return "Math"; }
        xnode_os_node& CreateNodeInstance() override { auto* pNode = new sin_node(); pNode->m_pFactory = this; return *pNode; }
        void DestroyNodeInstance(xnode_os_node& Node) override { delete static_cast<sin_node*>(&Node); }
    };
    struct cos_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("cos_node_factory", cos_node_factory)
        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "Cos"; }
        std::string_view getCategory() const noexcept override { return "Math"; }
        xnode_os_node& CreateNodeInstance() override { auto* pNode = new cos_node(); pNode->m_pFactory = this; return *pNode; }
        void DestroyNodeInstance(xnode_os_node& Node) override { delete static_cast<cos_node*>(&Node); }
    };
    struct tan_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("tan_node_factory", tan_node_factory)
        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "Tan"; }
        std::string_view getCategory() const noexcept override { return "Math"; }
        xnode_os_node& CreateNodeInstance() override { auto* pNode = new tan_node(); pNode->m_pFactory = this; return *pNode; }
        void DestroyNodeInstance(xnode_os_node& Node) override { delete static_cast<tan_node*>(&Node); }
    };
}
XPROPERTY_VREG(sin_node_factory)
XPROPERTY_VREG(cos_node_factory)
XPROPERTY_VREG(tan_node_factory)

// The new multi-factory entry point (see xnode_os_plugin_api.h) - calls RegisterFn once per factory
// instead of returning a single one. NodeOS_CreateFactory (singular) is deliberately NOT exported
// here - a plugin implements one or the other, never both, so the host's "try multi first, else
// fall back to single" GetProcAddress logic never has to pick between two valid answers.
extern "C" XNODE_OS_EXPORT void NodeOS_CreateFactories(ixnode_os_host& /*Host*/, void* pUserData, xnode_os_pfn_register_factory* RegisterFn) noexcept
{
    RegisterFn(pUserData, *new sin_node_factory());
    RegisterFn(pUserData, *new cos_node_factory());
    RegisterFn(pUserData, *new tan_node_factory());
}
extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    // getName() still works after the derived object is only reachable through the abstract base -
    // the vtable is intact, this just reads which concrete type to delete through.
    const auto Name = Factory.getName();
    if (Name == "Sin")      delete static_cast<sin_node_factory*>(&Factory);
    else if (Name == "Cos") delete static_cast<cos_node_factory*>(&Factory);
    else if (Name == "Tan") delete static_cast<tan_node_factory*>(&Factory);
}
