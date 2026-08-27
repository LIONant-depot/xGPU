// Constant - no inputs, one output, a Type dropdown and a Value field. Unlike Compare/Math
// Expression's "Any" pins (resolved from whatever gets WIRED to them - see E27_NodeOS_Editor.cpp's
// ResolveNodeWildcardType), this node's own output type is resolved from its OWN "Type" property
// instead - it doesn't need the host's cross-node wildcard machinery at all, since getOutputs() has
// direct access to m_Type on the same object. Type is scoped to the small set of atomic scalars this
// corpus already recognizes (Float/Int/Short/Bool) rather than every xproperty-registered type in
// the abstract - nothing else is registered here yet to make a broader set meaningful; extending
// const_type_v's own array is the entire integration step for a genuinely new atomic type later.
//
// First worked example of leaning on xproperty as the one serialize/inspect/debug surface (see the
// conversation that motivated this - the four Value* members below replace what used to be a single
// "store what was typed, interpret it later" string): each is its OWN properly-typed member instead
// of shared text, shown/hidden per m_Type via member_dynamic_flags (same pattern xtexture.plugin's
// descriptor uses for its compression-format union - only the active one is ever shown or saved).
// "Effective Output" is a computed, read-only, never-saved property mirroring the live value this
// node actually outputs - the debug-info idea: real-time state visible in the inspector without
// pretending to be persistent config. "Reset" is a virtual string property styled as a button
// (member_ui<std::string>::button) whose write side zeroes whichever Value field is active -
// deliberately not a real obj_action here, since Reset has no reason to exist as a standalone class
// method outside the property system; the lambda keeps it self-contained instead of growing the
// class's own API surface just to support a UI button.
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include <array>
#include <string>
#include <format>
#include <cstdlib>
#include <cstdint>

namespace
{
    enum class const_type : std::uint8_t { FLOAT, INT, SHORT, BOOL };

    static constexpr auto const_type_v = std::array
    { xproperty::settings::enum_item("Float", const_type::FLOAT)
    , xproperty::settings::enum_item("Int",   const_type::INT)
    , xproperty::settings::enum_item("Short", const_type::SHORT)
    , xproperty::settings::enum_item("Bool",  const_type::BOOL)
    };

    constexpr const char* TypeNameOf(const_type T) noexcept
    {
        switch (T)
        {
            case const_type::FLOAT: return "Float";
            case const_type::INT:   return "Int";
            case const_type::SHORT: return "Short";
            case const_type::BOOL:  return "Bool";
        }
        return "Float";
    }

    struct constant_node : xnode_os_node
    {
        const_type   m_Type        = const_type::FLOAT;
        float        m_ValueFloat  = 0.0f;
        std::int32_t m_ValueInt    = 0;
        std::int16_t m_ValueShort  = 0;
        bool         m_ValueBool   = false;

        // The one place that knows "whichever Value* field m_Type currently selects" - read by the
        // Effective Output property below, and by the host's own codegen backend (EmitOrdinaryNode's
        // "Constant" case in E27_NodeOS_Editor.cpp), so that logic only lives here once.
        float ActiveValueAsFloat() const noexcept
        {
            switch (m_Type)
            {
                case const_type::FLOAT: return m_ValueFloat;
                case const_type::INT:   return static_cast<float>(m_ValueInt);
                case const_type::SHORT: return static_cast<float>(m_ValueShort);
                case const_type::BOOL:  return m_ValueBool ? 1.0f : 0.0f;
            }
            return 0.0f;
        }

        // Not a plain static span like every other node type here - Value's port TYPE genuinely
        // depends on this instance's own m_Type, so it has to be rebuilt (into per-instance storage,
        // not a function-local static shared by every constant_node) each time it's asked for.
        mutable xnode_os_port_desc m_OutputDesc[1] = { { "Value", "Float" } };

        XPROPERTY_VDEF
        ( "constant_node", constant_node
        , obj_member<"Type",  &constant_node::m_Type, member_enum_span<const_type_v>
            , member_help<"Which scalar type this node's output pin resolves to. Switches which Value field below is shown/used and what Execute() actually allocates.">>
        , obj_member<"Value Float", &constant_node::m_ValueFloat
            , member_dynamic_flags<+[](const constant_node& O)
                { xproperty::flags::type F{}; F.m_bDontShow = F.m_bDontSave = O.m_Type != const_type::FLOAT; return F; }>
            , member_help<"The float value this node outputs when Type is Float.">>
        , obj_member<"Value Int", &constant_node::m_ValueInt
            , member_dynamic_flags<+[](const constant_node& O)
                { xproperty::flags::type F{}; F.m_bDontShow = F.m_bDontSave = O.m_Type != const_type::INT; return F; }>
            , member_help<"The integer value this node outputs when Type is Int.">>
        , obj_member<"Value Short", &constant_node::m_ValueShort
            , member_dynamic_flags<+[](const constant_node& O)
                { xproperty::flags::type F{}; F.m_bDontShow = F.m_bDontSave = O.m_Type != const_type::SHORT; return F; }>
            , member_help<"The short-integer value this node outputs when Type is Short.">>
        , obj_member<"Value Bool", &constant_node::m_ValueBool
            , member_dynamic_flags<+[](const constant_node& O)
                { xproperty::flags::type F{}; F.m_bDontShow = F.m_bDontSave = O.m_Type != const_type::BOOL; return F; }>
            , member_help<"The boolean value this node outputs when Type is Bool.">>
        , obj_member<"Effective Output"
            , +[](const constant_node& O, bool bRead, std::string& Value) { assert(bRead); Value = std::format("{}", O.ActiveValueAsFloat()); }
            , member_flags<xproperty::flags::SHOW_READONLY, xproperty::flags::DONT_SAVE>
            , member_help<"Live preview of what this node currently outputs - derived from Value, never itself saved.">>
        , obj_member<"Reset"
            , +[](constant_node& O, bool bRead, std::string& Value)
              {
                  if (bRead) Value = "Reset to 0";
                  else       { O.m_ValueFloat = 0.0f; O.m_ValueInt = 0; O.m_ValueShort = 0; O.m_ValueBool = false; }
              }
            , member_ui<std::string>::button<>
            , member_flags<xproperty::flags::DONT_SAVE>
            , member_help<"Zeroes whichever Value field is currently active.">>
        )

        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            return {};
        }
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            m_OutputDesc[0] = { "Value", TypeNameOf(m_Type) };
            return m_OutputDesc;
        }
        // Allocates a real value matching m_Type, straight from the already-typed field - no text
        // parsing left to do, unlike the old single-string m_Value this replaced. Same malloc/
        // FreeOutputs convention cube_node.cpp already established (the host frees this via
        // FreeOutputs before the next Execute or on teardown, never this instance directly).
        void Execute(void** /*Inputs*/, void** Outputs) noexcept override
        {
            switch (m_Type)
            {
                case const_type::FLOAT: { auto* p = static_cast<float*>(std::malloc(sizeof(float))); *p = m_ValueFloat; Outputs[0] = p; break; }
                case const_type::INT:   { auto* p = static_cast<std::int32_t*>(std::malloc(sizeof(std::int32_t))); *p = m_ValueInt; Outputs[0] = p; break; }
                case const_type::SHORT: { auto* p = static_cast<std::int16_t*>(std::malloc(sizeof(std::int16_t))); *p = m_ValueShort; Outputs[0] = p; break; }
                case const_type::BOOL:  { auto* p = static_cast<bool*>(std::malloc(sizeof(bool))); *p = m_ValueBool; Outputs[0] = p; break; }
            }
        }
        void FreeOutputs(void** Outputs) noexcept override
        {
            std::free(Outputs[0]);
        }
    };
}
XPROPERTY_VREG(constant_node)

namespace
{
    struct constant_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("constant_node_factory", constant_node_factory)

        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "Constant"; }
        std::string_view getCategory() const noexcept override { return "Math"; }

        xnode_os_node& CreateNodeInstance() override
        {
            auto* pNode = new constant_node();
            pNode->m_pFactory = this;
            return *pNode;
        }
        void DestroyNodeInstance(xnode_os_node& Node) override
        {
            delete static_cast<constant_node*>(&Node);
        }
    };
}
XPROPERTY_VREG(constant_node_factory)

extern "C" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host& /*Host*/) noexcept
{
    return *new constant_node_factory();
}
extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    delete static_cast<constant_node_factory*>(&Factory);
}
