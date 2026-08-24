// Math Expression (formerly "Math") - one node, an Operator dropdown, instead of a separate
// Add/Subtract/Multiply/Divide box each - same consolidation as Compare/Bool Expression.
//
// A, B, and Result are all typed "Any" - a wildcard, not one fixed concrete type (see
// E27_NodeOS_Editor.cpp's ResolveNodeWildcardType) - exactly like Compare's A/B, one level further:
// here the OUTPUT is wildcard too, since a math expression's result is naturally the same type as
// its operands (int + int is int, float + float is float) - once either A or B gets wired, the whole
// node - both inputs AND the output - resolves to that one type, no separate rule needed: Result is
// just another "Any" pin as far as ResolveNodeWildcardType is concerned, so it picks up the same
// resolution automatically.
//
// Subtraction and division get BOTH orderings as separate enum entries (A-B vs B-A, A/B vs B/A)
// rather than a "swap inputs" gesture, since they aren't commutative - the same reasoning Compare
// doesn't need (a Greater-Than/Less-Than pair would be redundant with two input orderings already).
//
// Execute() (interpreter) and the codegen backend's own EmitOrdinaryNode "Math Expression" case
// (E27_NodeOS_Editor.cpp) both switch on m_Operator independently, working from the Operator
// property's raw serialized form - ReflectedMemberToRow stores an enum as ReadEnumAsInt's numeric
// value, never the display name - and, unlike Compare's single infix-operator-token substitution,
// the REVERSE variants (B-A, B/A) need the operand ORDER swapped too, not just a different token.
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include <array>
#include <cstdlib>
#include <string>
#include <format>
#include <cassert>

namespace
{
    enum class math_expr_op : std::uint8_t { ADD, SUBTRACT, SUBTRACT_REVERSE, MULTIPLY, DIVIDE, DIVIDE_REVERSE };

    static constexpr auto math_expr_op_v = std::array
    { xproperty::settings::enum_item("A + B", math_expr_op::ADD)
    , xproperty::settings::enum_item("A - B", math_expr_op::SUBTRACT)
    , xproperty::settings::enum_item("B - A", math_expr_op::SUBTRACT_REVERSE)
    , xproperty::settings::enum_item("A * B", math_expr_op::MULTIPLY)
    , xproperty::settings::enum_item("A / B", math_expr_op::DIVIDE)
    , xproperty::settings::enum_item("B / A", math_expr_op::DIVIDE_REVERSE)
    };

    struct math_expression_node : xnode_os_node
    {
        math_expr_op m_Operator     = math_expr_op::ADD;
        float        m_A            = 0.0f;  // used only while A is unconnected - see E27_NodeOS_Editor.cpp's FindMemberByName
        float        m_B            = 0.0f;  // used only while B is unconnected - see E27_NodeOS_Editor.cpp's FindMemberByName
        bool         m_bAConnected  = false; // pushed by the host each frame - see "A Connected"/PushPinConnectedFlags below
        bool         m_bBConnected  = false; // pushed by the host each frame - see "B Connected"/PushPinConnectedFlags below
        float        m_LastResult   = 0.0f;  // set by Execute() - live debug info, see "Last Result" below
        std::string  m_ResolvedType = "Any"; // pushed in by the host each frame - see "Resolved Type" below

        XPROPERTY_VDEF
        ( "math_expression_node", math_expression_node
        , obj_member<"Operator", &math_expression_node::m_Operator, member_enum_span<math_expr_op_v>
            , member_help<"Which arithmetic operation A and B are combined with.">>
        , obj_member<"A", &math_expression_node::m_A
            , member_dynamic_flags<+[](const math_expression_node& O) { xproperty::flags::type F{}; F.m_bDontShow = F.m_bDontSave = O.m_bAConnected; return F; }>
            , member_help<"A's own value while its pin is unconnected - hidden once a wire is attached, since the wire overrides it. Named to match the pin itself, so the host's generic 'find a property with the same name as this pin' hook picks it up automatically.">>
        , obj_member<"B", &math_expression_node::m_B
            , member_dynamic_flags<+[](const math_expression_node& O) { xproperty::flags::type F{}; F.m_bDontShow = F.m_bDontSave = O.m_bBConnected; return F; }>
            , member_help<"B's own value while its pin is unconnected - hidden once a wire is attached, since the wire overrides it. Named to match the pin itself, so the host's generic 'find a property with the same name as this pin' hook picks it up automatically.">>
        , obj_member<"A Connected", &math_expression_node::m_bAConnected, member_flags<xproperty::flags::DONT_SAVE, xproperty::flags::DONT_SHOW>>
        , obj_member<"B Connected", &math_expression_node::m_bBConnected, member_flags<xproperty::flags::DONT_SAVE, xproperty::flags::DONT_SHOW>>
        , obj_member<"Resolved Type", &math_expression_node::m_ResolvedType
            , member_flags<xproperty::flags::SHOW_READONLY, xproperty::flags::DONT_SAVE>
            , member_help<"The concrete type A/B/Result currently resolve to, based on what's wired in right now - live debug info, pushed in by the host each frame, never itself saved.">>
        , obj_member<"Last Result"
            , +[](const math_expression_node& O, bool bRead, std::string& Value) { assert(bRead); Value = std::format("{}", O.m_LastResult); }
            , member_flags<xproperty::flags::SHOW_READONLY, xproperty::flags::DONT_SAVE>
            , member_help<"The value produced by the most recent Execute() - live debug info, never itself saved.">>
        )

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            static const xnode_os_port_desc s_Inputs[2] = { { "A", "Any" }, { "B", "Any" } };
            return s_Inputs;
        }
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            static const xnode_os_port_desc s_Outputs[1] = { { "Result", "Any" } };
            return s_Outputs;
        }
        // A/B/Result are wildcard "Any" pins at the wiring/UI level, but every concrete producer this
        // corpus has today resolves that wildcard to Float - reading both as float* is the same "the
        // only real width so far" simplification Compare's own Execute() already leans on.
        void Execute(void** Inputs, void** Outputs) noexcept override
        {
            const float A = Inputs[0] ? *static_cast<float*>(Inputs[0]) : 0.0f;
            const float B = Inputs[1] ? *static_cast<float*>(Inputs[1]) : 0.0f;
            float Result = 0.0f;
            switch (m_Operator)
            {
                case math_expr_op::ADD:             Result = A + B; break;
                case math_expr_op::SUBTRACT:        Result = A - B; break;
                case math_expr_op::SUBTRACT_REVERSE: Result = B - A; break;
                case math_expr_op::MULTIPLY:        Result = A * B; break;
                case math_expr_op::DIVIDE:          Result = A / B; break;
                case math_expr_op::DIVIDE_REVERSE:   Result = B / A; break;
            }
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
XPROPERTY_VREG(math_expression_node)

namespace
{
    struct math_expression_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("math_expression_node_factory", math_expression_node_factory)

        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "Math Expression"; }
        std::string_view getCategory() const noexcept override { return "Math"; }

        xnode_os_node& CreateNodeInstance() override
        {
            auto* pNode = new math_expression_node();
            pNode->m_pFactory = this;
            return *pNode;
        }
        void DestroyNodeInstance(xnode_os_node& Node) override
        {
            delete static_cast<math_expression_node*>(&Node);
        }
    };
}
XPROPERTY_VREG(math_expression_node_factory)

extern "C" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host& /*Host*/) noexcept
{
    return *new math_expression_node_factory();
}
extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    delete static_cast<math_expression_node_factory*>(&Factory);
}
