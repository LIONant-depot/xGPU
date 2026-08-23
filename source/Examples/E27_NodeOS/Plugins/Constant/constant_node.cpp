// Constant - no inputs, one output, a Type dropdown and a Value field. Unlike Compare/Math
// Expression's "Any" pins (resolved from whatever gets WIRED to them - see E27_NodeOS_Editor.cpp's
// ResolveNodeWildcardType), this node's own output type is resolved from its OWN "Type" property
// instead - it doesn't need the host's cross-node wildcard machinery at all, since getOutputs() has
// direct access to m_Type on the same object. Type is scoped to the small set of atomic scalars this
// corpus already recognizes (Float/Int/Short/Bool) rather than every xproperty-registered type in
// the abstract - nothing else is registered here yet to make a broader set meaningful; extending
// const_type_v's own array is the entire integration step for a genuinely new atomic type later.
// Value is stored as plain text regardless of Type and parsed according to it once execution is
// wired up - same "store what was typed, interpret it later" approach the canvas's own inline-
// literal-on-unconnected-pin feature already uses for Compare/Math Expression's A/B.
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include <array>
#include <string>
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
        const_type  m_Type  = const_type::FLOAT;
        std::string m_Value = "0";

        // Not a plain static span like every other node type here - Value's port TYPE genuinely
        // depends on this instance's own m_Type, so it has to be rebuilt (into per-instance storage,
        // not a function-local static shared by every constant_node) each time it's asked for.
        mutable xnode_os_port_desc m_OutputDesc[1] = { { "Value", "Float" } };

        XPROPERTY_VDEF
        ( "constant_node", constant_node
        , obj_member<"Type",  &constant_node::m_Type, member_enum_span<const_type_v>>
        , obj_member<"Value", &constant_node::m_Value>
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
        // Allocates a real value matching m_Type, parsed from m_Value - same malloc/FreeOutputs
        // convention cube_node.cpp already established (the host frees this via FreeOutputs before
        // the next Execute or on teardown, never this instance directly). Bool/Int/Short are stored
        // at their own real width even though the pin-typing/wiring side of this corpus treats every
        // scalar as interchangeable "Float" for connection purposes - Execute() still needs to hand
        // back the ACTUAL width Print (or anything else) would read.
        void Execute(void** /*Inputs*/, void** Outputs) noexcept override
        {
            switch (m_Type)
            {
                case const_type::FLOAT: { auto* p = static_cast<float*>(std::malloc(sizeof(float))); *p = std::strtof(m_Value.c_str(), nullptr); Outputs[0] = p; break; }
                case const_type::INT:   { auto* p = static_cast<std::int32_t*>(std::malloc(sizeof(std::int32_t))); *p = std::atoi(m_Value.c_str()); Outputs[0] = p; break; }
                case const_type::SHORT: { auto* p = static_cast<std::int16_t*>(std::malloc(sizeof(std::int16_t))); *p = static_cast<std::int16_t>(std::atoi(m_Value.c_str())); Outputs[0] = p; break; }
                case const_type::BOOL:  { auto* p = static_cast<bool*>(std::malloc(sizeof(bool))); *p = (m_Value == "1" || m_Value == "true"); Outputs[0] = p; break; }
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
