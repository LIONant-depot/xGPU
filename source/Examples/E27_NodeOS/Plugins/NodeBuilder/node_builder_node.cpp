// NodeBuilder - a graph-purpose marker, the same role OnEvent plays for "this is a runnable
// program," except this one says "this graph defines and compiles a new native node type." A graph
// may contain OnEvent(s) (a program) XOR exactly one NodeBuilder (a node definition) - never both;
// enforced host-side (BuildNodeFromFunction refuses if any OnEvent exists or more than one
// NodeBuilder does; RunProgram/GenerateCpp refuse if any NodeBuilder exists) - see
// E27_NodeOS_Editor.cpp.
//
// Owns a scope, exactly like Function does (needsOwnedEndMarker) - NOT like Execute. The first
// version of this file skipped the End marker on the reasoning "the whole graph IS the node, there's
// nothing to separate" - wrong once test-rig content (Constants feeding this node's own external
// pins to preview it live, a Print reading its external output) needs to coexist in the SAME graph:
// without an explicit boundary, "positionally inside the body" (same spine, reachable by the body
// walk) and "wired to the external/global pins" (test-rig, semantically outside) can contradict each
// other - confirmed the hard way, building exactly that contradiction (test-rig nodes placed in the
// same spine as the body, ALSO wired to the external pins) produced a graph with no coherent meaning.
// The owned End marker resolves it the same way Function's already does: the body is
// [this node's Order + 1, its own End's Order) - test-rig content simply lives in the same spine,
// positioned AFTER the End marker, unambiguously outside that range.
//
// No Exec pin, no Success/Failure pins: NodeBuilder never itself executes as part of a running
// program - "compile me" is a design-time/tooling action (the "BuildNode -Id N" command), the same
// category as Save/Load/CompileToCpp, none of which have a corresponding node with Exec pins either.
//
// A generated node needing to hold state across Execute() calls needs nothing special here -
// CreateNodeInstance() already returns a genuine C++ struct per plugin, so persistent state is just
// whatever member that struct happens to have, same as any hand-written plugin (Random's own
// std::mt19937 member is the existing proof this already works, no new mechanism required).
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include "dependencies/xresource_guid/source/xresource_guid.h"
#include <vector>
#include <string>
#include <array>
#include <cstdint>

namespace
{
    // Same pin_type/pin_descriptor shape as function_node.cpp's own (down to the guid field and why
    // it needs xproperty::base+XPROPERTY_VDEF rather than the plain XPROPERTY_DEF a same-binary
    // struct could use) - not shared via a common header on purpose, each plugin is its own DLL,
    // self-contained, matching this corpus's usual no-cross-plugin-linkage rule. This replaces the old
    // "Name:Type:Required:ReadOnly"-encoded, '|'-joined spec strings (node_builder_pin/DecodePins) -
    // m_InputsSpec/m_OutputsSpec are real reflected std::vector<pin_descriptor> now, so add/remove/
    // reorder/per-field editing come for free from the host's shared xproperty::inspector, same as
    // every other array-typed property in this codebase.
    enum class pin_type : std::uint8_t { FLOAT, INT, SHORT, BOOL, ANY, SPAN_ANY };

    static constexpr auto pin_type_v = std::array
    { xproperty::settings::enum_item("Float",     pin_type::FLOAT)
    , xproperty::settings::enum_item("Int",       pin_type::INT)
    , xproperty::settings::enum_item("Short",     pin_type::SHORT)
    , xproperty::settings::enum_item("Bool",      pin_type::BOOL)
    , xproperty::settings::enum_item("Any",       pin_type::ANY)
    , xproperty::settings::enum_item("Span<Any>", pin_type::SPAN_ANY)
    };

    constexpr const char* PinTypeNameOf(pin_type T) noexcept
    {
        switch (T)
        {
            case pin_type::FLOAT:    return "Float";
            case pin_type::INT:      return "Int";
            case pin_type::SHORT:    return "Short";
            case pin_type::BOOL:     return "Bool";
            case pin_type::ANY:      return "Any";
            case pin_type::SPAN_ANY: return "Span<Any>";
        }
        return "Float";
    }

    struct pin_descriptor : xproperty::base
    {
        std::string   m_Name      = "Value";
        pin_type      m_Type      = pin_type::FLOAT;
        bool          m_bRequired = true;
        bool          m_bReadOnly = true;
        std::uint64_t m_Guid      = xresource::guid_generator::Instance64();

        pin_descriptor() noexcept = default;
        pin_descriptor(std::string Name, pin_type Type, bool bRequired, bool bReadOnly) noexcept
            : m_Name(std::move(Name)), m_Type(Type), m_bRequired(bRequired), m_bReadOnly(bReadOnly) {}

        XPROPERTY_VDEF
        ( "Pin", pin_descriptor
        , obj_member<"Name",      &pin_descriptor::m_Name>
        , obj_member<"Type",      &pin_descriptor::m_Type, member_enum_span<pin_type_v>>
        , obj_member<"Required",  &pin_descriptor::m_bRequired>
        , obj_member<"Read Only", &pin_descriptor::m_bReadOnly>
        , obj_member<"Guid",      &pin_descriptor::m_Guid, member_flags<flags::DONT_SHOW>>
        )
    };
    XPROPERTY_VREG(pin_descriptor)

    struct node_builder_node : xnode_os_node
    {
        std::string                 m_Name    = "MyNode";  // Plugins/<Name>/ folder + published node type name - same role Function's own "Name" plays
        std::vector<pin_descriptor> m_Inputs  = { { "A",      pin_type::FLOAT, true, true  } };
        std::vector<pin_descriptor> m_Outputs = { { "Result", pin_type::FLOAT, true, false } };

        // Stable per-instance guid for the fixed "End" pin below - see function_node.cpp's identical
        // m_ExecGuid/m_EndGuid for why this needs to be its own reflected (DONT_SHOW) field rather
        // than generated inline at the injection site.
        std::uint64_t m_EndGuid = xresource::guid_generator::Instance64();

        mutable std::vector<xnode_os_port_desc> m_InDescs, m_OutDescs;

        XPROPERTY_VDEF
        ( "node_builder_node", node_builder_node
        , obj_member<"Name", &node_builder_node::m_Name
            , member_help<"Published node type name - becomes both the Plugins/<Name>/ folder and the palette entry.">>
        , obj_member<"Inputs",  &node_builder_node::m_Inputs,  member_ui_open<true>>
        , obj_member<"Outputs", &node_builder_node::m_Outputs, member_ui_open<true>>
        , obj_member<"EndGuid", &node_builder_node::m_EndGuid, member_flags<flags::DONT_SHOW>>
        )

        // Same mirror-guid salt as function_node.cpp's own (see its comment) - each plugin mints its
        // own independently, they never need to agree with each other's, only be internally stable.
        static constexpr std::uint64_t kMirrorGuidSalt = 0x9E3779B97F4A7C15ULL;

        // Same Rebuild as function_node.cpp - not shared via a common header on purpose (each plugin
        // is its own DLL, self-contained, matching this corpus's usual no-cross-plugin-linkage rule).
        static void Rebuild(const std::vector<pin_descriptor>& OwnPins, const std::vector<pin_descriptor>& MirrorPins, std::vector<xnode_os_port_desc>& Descs)
        {
            Descs.clear();
            Descs.reserve(OwnPins.size() + MirrorPins.size());
            for (auto& P : OwnPins)    Descs.push_back({ P.m_Name.c_str(), PinTypeNameOf(P.m_Type), P.m_bRequired, P.m_bReadOnly, false, P.m_Guid });
            for (auto& P : MirrorPins) Descs.push_back({ P.m_Name.c_str(), PinTypeNameOf(P.m_Type), P.m_bRequired, P.m_bReadOnly, true,  P.m_Guid ^ kMirrorGuidSalt });
        }

        // Declared Inputs (external - what the PUBLISHED node's own Inputs[] will be), then the
        // mirror of declared Outputs (local - this node's own body writes its results here).
        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            Rebuild(m_Inputs, m_Outputs, m_InDescs);
            return m_InDescs;
        }

        // Declared Outputs (external), then the mirror of declared Inputs (local - the body reads its
        // parameters here), then "End" - the owned-scope marker, must stay last.
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            Rebuild(m_Outputs, m_Inputs, m_OutDescs);
            m_OutDescs.push_back({ "End", "Scope", true, true, false, m_EndGuid });
            return m_OutDescs;
        }

        void Execute(void** /*Inputs*/, void** /*Outputs*/) noexcept override {}
    };
}
XPROPERTY_VREG(node_builder_node)

namespace
{
    struct node_builder_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("node_builder_node_factory", node_builder_node_factory)

        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "NodeBuilder"; }
        std::string_view getCategory() const noexcept override { return "Tools"; }

        bool             needsOwnedEndMarker()        const noexcept override { return true; }
        std::string_view getOwnedEndMarkerPluginDir() const noexcept override { return "End"; }

        xnode_os_node& CreateNodeInstance() override
        {
            auto* pNode = new node_builder_node();
            pNode->m_pFactory = this;
            return *pNode;
        }
        void DestroyNodeInstance(xnode_os_node& Node) override
        {
            delete static_cast<node_builder_node*>(&Node);
        }
    };
}
XPROPERTY_VREG(node_builder_node_factory)

extern "C" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host& /*Host*/) noexcept
{
    return *new node_builder_node_factory();
}
extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    delete static_cast<node_builder_node_factory*>(&Factory);
}
