// Bool Expression (formerly "Logic") - one node, an Operator dropdown, instead of a separate
// And/Or/Xor/... box per combination - same consolidation as Compare/Math Expression (see those
// files' own comments for the Unity Shader Graph precedent). The nine choices below cover every
// combination of AND/OR/XOR with an optional NOT on either operand that was actually asked for -
// not the full sixteen-function boolean truth-table space, just this fixed, named set.
//
// Once compilation is wired up, this needs no branching in the compiler either: the design doc's
// _prop[N] substitution can splice the right C++ expression straight in per enum value (e.g.
// "(!$input[A] || !$input[B])" for "NOT A OR NOT B"), so long as each enum value maps to its own
// fixed expression template. Execute() is a no-op for now, same as every other node type in this
// corpus so far.
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include "dependencies/xresource_guid/source/xresource_guid.h"
#include <array>

namespace
{
    enum class bool_expr_op : std::uint8_t
    { A_AND_B, A_OR_B, A_XOR_B
    , NOT_A_OR_NOT_B, NOT_A_OR_B, A_OR_NOT_B
    , NOT_A_AND_NOT_B, NOT_A_AND_B, A_AND_NOT_B
    };

    static constexpr auto bool_expr_op_v = std::array
    { xproperty::settings::enum_item("A AND B",         bool_expr_op::A_AND_B)
    , xproperty::settings::enum_item("A OR B",          bool_expr_op::A_OR_B)
    , xproperty::settings::enum_item("A XOR B",         bool_expr_op::A_XOR_B)
    , xproperty::settings::enum_item("NOT A OR NOT B",  bool_expr_op::NOT_A_OR_NOT_B)
    , xproperty::settings::enum_item("NOT A OR B",      bool_expr_op::NOT_A_OR_B)
    , xproperty::settings::enum_item("A OR NOT B",      bool_expr_op::A_OR_NOT_B)
    , xproperty::settings::enum_item("NOT A AND NOT B", bool_expr_op::NOT_A_AND_NOT_B)
    , xproperty::settings::enum_item("NOT A AND B",     bool_expr_op::NOT_A_AND_B)
    , xproperty::settings::enum_item("A AND NOT B",     bool_expr_op::A_AND_NOT_B)
    };

    struct bool_expression_node : xnode_os_node
    {
        bool_expr_op m_Operator = bool_expr_op::A_AND_B;

        // Stable per-instance guids for the pins below - reflected (DONT_SHOW) so the saved value is
        // restored on load rather than a fresh xresource::guid_generator::Instance64() regenerating
        // (which would stop matching any saved link) - same pattern end_marker_node.cpp's m_OwnerGuid
        // uses.
        std::uint64_t m_AGuid      = xresource::guid_generator::Instance64();
        std::uint64_t m_BGuid      = xresource::guid_generator::Instance64();
        std::uint64_t m_ResultGuid = xresource::guid_generator::Instance64();

        XPROPERTY_VDEF
        ( "bool_expression_node", bool_expression_node
        , obj_member<"Operator", &bool_expression_node::m_Operator, member_enum_span<bool_expr_op_v>
            , member_help<"Which fixed boolean combination of A and B (with an optional NOT on either) this node computes.">>
        , obj_member<"AGuid",      &bool_expression_node::m_AGuid,      member_flags<xproperty::flags::DONT_SHOW>>
        , obj_member<"BGuid",      &bool_expression_node::m_BGuid,      member_flags<xproperty::flags::DONT_SHOW>>
        , obj_member<"ResultGuid", &bool_expression_node::m_ResultGuid, member_flags<xproperty::flags::DONT_SHOW>>
        )

        // Per-instance port guids (not a shared static array) - every pin needs its own stable identity
        // unique to THIS node instance so links can reference it by guid rather than by array position
        // (see xnode_os_port_desc::m_Guid's own comment; link_instance no longer stores a plain index).
        // Not const-only-initialized - getInputs()/getOutputs() re-sync m_Guid from the reflected
        // fields above on every call, so a guid restored by deserialization AFTER construction still
        // takes effect.
        mutable xnode_os_port_desc m_Inputs[2]  = { { "A", "Bool", true, true, false, 0 }
                                                   , { "B", "Bool", true, true, false, 0 }
                                                   };
        mutable xnode_os_port_desc m_Outputs[1] = { { "Result", "Bool", true, true, false, 0 } };

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            m_Inputs[0].m_Guid = m_AGuid;
            m_Inputs[1].m_Guid = m_BGuid;
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
XPROPERTY_VREG(bool_expression_node)

namespace
{
    struct bool_expression_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("bool_expression_node_factory", bool_expression_node_factory)

        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "Bool Expression"; }
        std::string_view getCategory() const noexcept override { return "Logic"; }

        xnode_os_node& CreateNodeInstance() override
        {
            auto* pNode = new bool_expression_node();
            pNode->m_pFactory = this;
            return *pNode;
        }
        void DestroyNodeInstance(xnode_os_node& Node) override
        {
            delete static_cast<bool_expression_node*>(&Node);
        }
    };
}
XPROPERTY_VREG(bool_expression_node_factory)

extern "C" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host& /*Host*/) noexcept
{
    return *new bool_expression_node_factory();
}
extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    delete static_cast<bool_expression_node_factory*>(&Factory);
}
