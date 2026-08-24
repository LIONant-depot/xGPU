// Compare - one node, an Operator dropdown, instead of a separate GreaterThan/LessThan/Equals/...
// box per comparison (matching Unity Shader Graph's own Comparison node, verified before building
// this). The dropdown is reflected exactly like Cube's Width/Height/Depth - an ordinary
// xproperty enum member - so it renders in both the side properties panel AND directly in this
// node's own body on the canvas (E27_NodeOS_Editor.cpp's inline-enum-widget block), no special
// per-node-type UI code needed on the host side.
//
// A and B are typed "Any" - a wildcard, not a fixed concrete type - rather than being hardcoded to
// Float: the very first wire that lands on either one resolves what "Any" means for THIS instance,
// and the other Any pin is then constrained to that same type (E27_NodeOS_Editor.cpp's
// ResolveNodeWildcardType). This is what lets one Compare node serve floats, meshes, or any future
// atomic type without a per-type variant, one level further than the enum consolidation above -
// the wildcard resolution itself is entirely host-side/UI-level bookkeeping, invisible from here.
// The host also narrows the Operator dropdown to Equal/Not-Equal whenever the resolved type isn't
// an orderable atomic (Float today) - a struct-like comparison has no meaningful < or >.
//
// Execute() (interpreter) and the codegen backend's own EmitOrdinaryNode "Compare" case
// (E27_NodeOS_Editor.cpp) both switch on m_Operator independently rather than sharing one text-
// substitution table - codegen reads the reflected Operator property back as its raw serialized
// integer (ReflectedMemberToRow stores an enum as ReadEnumAsInt's numeric value, never the display
// name), and maps that same 0..5 ordering to a literal C++ operator token.
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include <array>
#include <cstdlib>
#include <string>
#include <cassert>

namespace
{
    enum class compare_op : std::uint8_t { GREATER, LESS, EQUAL, NOT_EQUAL, GREATER_OR_EQUAL, LESS_OR_EQUAL };

    static constexpr auto compare_op_v = std::array
    { xproperty::settings::enum_item("A Greater Than B",         compare_op::GREATER)
    , xproperty::settings::enum_item("A Less Than B",            compare_op::LESS)
    , xproperty::settings::enum_item("A Equal to B",             compare_op::EQUAL)
    , xproperty::settings::enum_item("A Not Equal to B",         compare_op::NOT_EQUAL)
    , xproperty::settings::enum_item("A Greater Or Equal To B",   compare_op::GREATER_OR_EQUAL)
    , xproperty::settings::enum_item("A Less Or Equal To B",      compare_op::LESS_OR_EQUAL)
    };

    struct compare_node : xnode_os_node
    {
        compare_op  m_Operator     = compare_op::GREATER;
        float       m_A            = 0.0f;    // used only while A is unconnected - see E27_NodeOS_Editor.cpp's FindMemberByName
        float       m_B            = 0.0f;    // used only while B is unconnected - see E27_NodeOS_Editor.cpp's FindMemberByName
        bool        m_bAConnected  = false;   // pushed by the host each frame - see "A Connected"/PushPinConnectedFlags below
        bool        m_bBConnected  = false;   // pushed by the host each frame - see "B Connected"/PushPinConnectedFlags below
        bool        m_LastResult   = false;   // set by Execute() - live debug info, see "Last Result" below
        std::string m_ResolvedType = "Any";   // pushed in by the host each frame - see "Resolved Type" below

        XPROPERTY_VDEF
        ( "compare_node", compare_node
        , obj_member<"Operator", &compare_node::m_Operator, member_enum_span<compare_op_v>
            , member_help<"Which comparison A and B are checked against.">>
        , obj_member<"A", &compare_node::m_A
            , member_dynamic_flags<+[](const compare_node& O) { xproperty::flags::type F{}; F.m_bDontShow = F.m_bDontSave = O.m_bAConnected; return F; }>
            , member_help<"A's own value while its pin is unconnected - hidden once a wire is attached, since the wire overrides it. Named to match the pin itself, so the host's generic 'find a property with the same name as this pin' hook picks it up automatically.">>
        , obj_member<"B", &compare_node::m_B
            , member_dynamic_flags<+[](const compare_node& O) { xproperty::flags::type F{}; F.m_bDontShow = F.m_bDontSave = O.m_bBConnected; return F; }>
            , member_help<"B's own value while its pin is unconnected - hidden once a wire is attached, since the wire overrides it. Named to match the pin itself, so the host's generic 'find a property with the same name as this pin' hook picks it up automatically.">>
        , obj_member<"A Connected", &compare_node::m_bAConnected, member_flags<xproperty::flags::DONT_SAVE, xproperty::flags::DONT_SHOW>>
        , obj_member<"B Connected", &compare_node::m_bBConnected, member_flags<xproperty::flags::DONT_SAVE, xproperty::flags::DONT_SHOW>>
        , obj_member<"Resolved Type", &compare_node::m_ResolvedType
            , member_flags<xproperty::flags::SHOW_READONLY, xproperty::flags::DONT_SAVE>
            , member_help<"The concrete type A/B currently resolve to, based on what's wired in right now - live debug info, pushed in by the host each frame, never itself saved.">>
        , obj_member<"Last Result"
            , +[](const compare_node& O, bool bRead, std::string& Value) { assert(bRead); Value = O.m_LastResult ? "true" : "false"; }
            , member_flags<xproperty::flags::SHOW_READONLY, xproperty::flags::DONT_SAVE>
            , member_help<"The result of the most recent Execute() - live debug info, never itself saved.">>
        )

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            static const xnode_os_port_desc s_Inputs[2] = { { "A", "Any" }, { "B", "Any" } };
            return s_Inputs;
        }
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            static const xnode_os_port_desc s_Outputs[1] = { { "Result", "Bool" } };
            return s_Outputs;
        }
        // A/B are wildcard "Any" pins at the wiring/UI level, but every concrete producer this corpus
        // has today (Constant, Compare's own A/B mirrors) resolves that wildcard to Float - reading
        // both as float* is the same "the only real width so far" simplification Constant's own
        // Execute() already leans on for its own Bool/Int/Short branches.
        void Execute(void** Inputs, void** Outputs) noexcept override
        {
            const float A = Inputs[0] ? *static_cast<float*>(Inputs[0]) : 0.0f;
            const float B = Inputs[1] ? *static_cast<float*>(Inputs[1]) : 0.0f;
            bool Result = false;
            switch (m_Operator)
            {
                case compare_op::GREATER:          Result = A >  B; break;
                case compare_op::LESS:             Result = A <  B; break;
                case compare_op::EQUAL:            Result = A == B; break;
                case compare_op::NOT_EQUAL:        Result = A != B; break;
                case compare_op::GREATER_OR_EQUAL: Result = A >= B; break;
                case compare_op::LESS_OR_EQUAL:    Result = A <= B; break;
            }
            m_LastResult = Result;
            auto* p = static_cast<bool*>(std::malloc(sizeof(bool)));
            *p = Result;
            Outputs[0] = p;
        }
        void FreeOutputs(void** Outputs) noexcept override
        {
            std::free(Outputs[0]);
        }
    };
}
XPROPERTY_VREG(compare_node)

namespace
{
    struct compare_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("compare_node_factory", compare_node_factory)

        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "Compare"; }
        std::string_view getCategory() const noexcept override { return "Logic"; }

        xnode_os_node& CreateNodeInstance() override
        {
            auto* pNode = new compare_node();
            pNode->m_pFactory = this;
            return *pNode;
        }
        void DestroyNodeInstance(xnode_os_node& Node) override
        {
            delete static_cast<compare_node*>(&Node);
        }
    };
}
XPROPERTY_VREG(compare_node_factory)

extern "C" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host& /*Host*/) noexcept
{
    return *new compare_node_factory();
}
extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    delete static_cast<compare_node_factory*>(&Factory);
}
