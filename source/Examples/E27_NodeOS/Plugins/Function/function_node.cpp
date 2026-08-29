// Function - a user-defined scope-owning node (NODE_SCRIPTING_DESIGN.md). Not owned by anything
// itself, so it sits wherever it's placed and doubles as its own call site: other nodes wire
// directly into its declared Inputs and read its declared Outputs, same as any ordinary node - no
// separate "Call" node exists yet (multi-call-site reuse is a deliberately deferred design pass).
//
// One node carries FOUR pin groups, not two - "external" (the call-site contract) and "local"
// (the function's own body-facing view of that same contract, roles flipped):
//   getInputs()  = declared Inputs (external - caller wires real arguments here)
//                + mirrored Outputs, flagged m_bLocalScope (local - body WRITES its return values here)
//   getOutputs() = declared Outputs (external - caller reads real results here)
//                + mirrored Inputs, flagged m_bLocalScope (local - body READS its parameters here)
//                + "End" (the owned-scope marker, always last)
// This used to be two separate node instances (Function + a "LocalConnections" node it owned),
// kept in sync by the host across the DLL boundary. Collapsed into one node: simpler (no
// host-side resync pass, no 2nd ownership hop) and the local pins can never drift out of sync with
// the external ones since they're mirrored from the exact same two pin lists on every call.
// m_bLocalScope is what lets E27_NodeOS_Editor.cpp's IsDataLinkScopeValid restrict these specific
// pins to links whose OTHER endpoint is physically inside this node's own scope span - see its own
// comment for why a stray wire out of a local pin would let data escape a scope that stops
// existing once the function returns.
//
// The pin list itself is user-editable directly in the node's own property panel now - m_Inputs/
// m_Outputs are real reflected std::vector<pin_descriptor> members, so add/remove/reorder/per-field
// editing all come for free from the host's shared xproperty::inspector (see E04_Properties.cpp's
// array_ops_smoke_test for the same std::vector<struct> pattern), same as every other node type here.
// This replaces the old "Name:Type:Required:ReadOnly"-encoded, '|'-joined spec strings plus a
// hand-rolled ImGui table (DrawFunctionPinEditor, removed from NodeOS_UI_Panels.h alongside this) -
// there is no more decode/re-encode round trip, and getInputs()/getOutputs() read pin names directly
// off the live vectors (stable for as long as they're not resized mid-call, which they never are).
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include "dependencies/xresource_guid/source/xresource_guid.h"
#include <vector>
#include <string>
#include <array>
#include <cstdint>

namespace
{
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

    // One user-declared input or output parameter - a real reflected element of function_node's own
    // m_Inputs/m_Outputs arrays, shown directly in the property panel (name, type dropdown, and the
    // Required/Read Only flags that used to be "Req"/"RO" checkboxes in the old hand-rolled table).
    //
    // Derives from xproperty::base and uses XPROPERTY_VDEF (not the plain XPROPERTY_DEF that E04's
    // own array_item/curve_keyframe use) - required specifically because this struct is a list_props
    // ELEMENT TYPE declared inside a plugin DLL, inspected from the host EXE. The generic collector's
    // per-element object lookup (xproperty.h's read_list, ~line 2270) resolves a plain struct's
    // reflection via a template variable (type::get_obj_info<T>), which is `inline` and therefore
    // PER-BINARY - the plugin DLL's own registration never reaches the host's copy of that variable,
    // so the host hits `assert(type::get_obj_info<atomic_t> != nullptr)` the instant it tries to walk
    // this array (confirmed live: crashed exactly there the moment the Function node was selected).
    // A type derived from xproperty::base takes a DIFFERENT, cross-DLL-safe branch instead - it calls
    // the instance's own VIRTUAL getProperties() (a vtable call, safe across the DLL boundary, same
    // as function_node's own getProperties() already is) rather than reading that per-binary
    // template variable at all. E04's plain-struct arrays never hit this because everything there
    // lives in one binary, so both the write and the read side of get_obj_info<T> agree.
    struct pin_descriptor : xproperty::base
    {
        std::string   m_Name      = "Value";
        pin_type      m_Type      = pin_type::FLOAT;
        bool          m_bRequired = true;
        bool          m_bReadOnly = true;

        // Stable identity, independent of this pin's position in m_Inputs/m_Outputs - what
        // xnode_os_port_desc::m_Guid/link_instance's own guid fields are FOR (see their own comments).
        // The default member initializer mints a fresh guid the moment a pin is genuinely NEW - this
        // file's own two default pins (via the 4-arg constructor below, which doesn't mention m_Guid
        // in its own init list, so this default still runs) and a brand-new element the array
        // controls' own Insert/Add default-constructs before the user fills in a name. Deliberately
        // NOT regenerated on copy/move (no custom copy/move constructor here, so the compiler-
        // generated ones do a plain memberwise copy of this field too) - std::vector reallocates and
        // reorders pin_descriptor elements internally on every ordinary insert/erase/Swap, and every
        // one of those existing pins must keep the SAME guid it already had, or moving/growing the
        // array would itself scramble every link into it. Hidden from the property panel (DONT_SHOW
        // below) but still saved - internal bookkeeping the user never edits, but that must survive a
        // save/load round trip.
        std::uint64_t m_Guid = xresource::guid_generator::Instance64();

        // Deriving from xproperty::base costs pin_descriptor its aggregate-ness (an overridden virtual
        // function disqualifies it), so the brace-init this file's default pins use below needs a real
        // constructor now rather than aggregate list-init.
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

    struct function_node : xnode_os_node
    {
        std::string                 m_Name    = "MyFunction";
        std::vector<pin_descriptor> m_Inputs  = { { "A",      pin_type::FLOAT, true, true  } };
        std::vector<pin_descriptor> m_Outputs = { { "Result", pin_type::FLOAT, true, false } };

        // Stable per-instance guids for the two fixed pins below (not part of the user-editable
        // vectors above, so they don't get one from pin_descriptor's own default member initializer).
        // Same xresource::guid_generator::Instance64() every other per-instance guid in this codebase
        // uses (node/link ids, pin_descriptor::m_Guid) - reflected below with DONT_SHOW so the SAVED
        // value is restored on load instead of a fresh one regenerating (which would stop matching any
        // saved link) - mirrors the material graph's own pin_guid persistence exactly.
        std::uint64_t m_ExecGuid = xresource::guid_generator::Instance64();
        std::uint64_t m_EndGuid  = xresource::guid_generator::Instance64();

        mutable std::vector<xnode_os_port_desc> m_InDescs, m_OutDescs;

        XPROPERTY_VDEF
        ( "function_node", function_node
        , obj_member<"Name",    &function_node::m_Name>
        , obj_member<"Inputs",  &function_node::m_Inputs,  member_ui_open<true>>
        , obj_member<"Outputs", &function_node::m_Outputs, member_ui_open<true>>
        , obj_member<"ExecGuid", &function_node::m_ExecGuid, member_flags<flags::DONT_SHOW>>
        , obj_member<"EndGuid",  &function_node::m_EndGuid,  member_flags<flags::DONT_SHOW>>
        )

        // XOR salt deriving a mirror pin's own guid from its source pin_descriptor's m_Guid - stable
        // (same source guid always produces the same mirror guid) and distinct from it (so the
        // external "B" and its own local mirror "B" never collide as the SAME port identity, even
        // though they share a name and originate from the same pin_descriptor). Arbitrary odd 64-bit
        // constant (golden-ratio-derived, the usual choice for XOR-based decorrelation) - its only
        // requirement is staying fixed forever, since it's baked into every saved link's guid.
        static constexpr std::uint64_t kMirrorGuidSalt = 0x9E3779B97F4A7C15ULL;

        // Builds one side's port list: OwnPins (this side's own declared pins, external) followed by
        // MirrorPins (the OTHER side's declared pins, mirrored back with roles flipped and
        // m_bLocalScope set - the function body's own view of its own contract). Reads pin names
        // directly off the live vectors' own std::string storage - stable for the duration of this
        // call, which is all a caller ever needs (see this file's own top comment). Each port's own
        // m_Guid carries the SOURCE pin_descriptor's stable identity through (XORed with the mirror
        // salt for the local-mirror copy) - see xnode_os_port_desc::m_Guid's own comment for why this
        // is what keeps an existing link attached to the right pin after Inputs/Outputs gets a pin
        // inserted, deleted, or reordered.
        static void Rebuild(const std::vector<pin_descriptor>& OwnPins, const std::vector<pin_descriptor>& MirrorPins, std::vector<xnode_os_port_desc>& Descs)
        {
            Descs.clear();
            Descs.reserve(OwnPins.size() + MirrorPins.size());
            for (auto& P : OwnPins)    Descs.push_back({ P.m_Name.c_str(), PinTypeNameOf(P.m_Type), P.m_bRequired, P.m_bReadOnly, false, P.m_Guid });
            for (auto& P : MirrorPins) Descs.push_back({ P.m_Name.c_str(), PinTypeNameOf(P.m_Type), P.m_bRequired, P.m_bReadOnly, true,  P.m_Guid ^ kMirrorGuidSalt });
        }

        // A fixed "Exec" input, always FIRST, then declared Inputs (external), then the mirror of
        // declared Outputs (local - the body writes its return values here). Not part of the user-
        // editable list, same treatment as "End" being a fixed, always-present output below. Input
        // only, no matching Exec output: per NODE_SCRIPTING_DESIGN.md's exec-flow addition, the
        // CALLER (a Call node) gets control back once this function returns, not this node itself -
        // Function has no notion of "what comes after," only "run when triggered." Being first shifts
        // every declared input's own index by one - any link already wired to this node's inputs from
        // before this change points at the wrong pin now and needs re-wiring.
        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            Rebuild(m_Inputs, m_Outputs, m_InDescs);
            m_InDescs.insert(m_InDescs.begin(), { "Exec", "Exec", true, true, false, m_ExecGuid });
            return m_InDescs;
        }

        // Declared Outputs (external) first, then the mirror of declared Inputs (local - the body
        // reads its parameters here), then "End" - the read-only ownership pin to this node's own End
        // marker, which must stay LAST (see for_each_loop_node.cpp's own comment on why).
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            Rebuild(m_Outputs, m_Inputs, m_OutDescs);
            m_OutDescs.push_back({ "End", "Scope", true, true, false, m_EndGuid });
            return m_OutDescs;
        }

        void Execute(void** /*Inputs*/, void** /*Outputs*/) noexcept override {}
    };
}
XPROPERTY_VREG(function_node)

namespace
{
    struct function_node_factory : xnode_os_node_factory
    {
        XPROPERTY_VDEF("function_node_factory", function_node_factory)

        std::string_view getVersion()  const noexcept override { return "1.0"; }
        std::string_view getName()     const noexcept override { return "Function"; }
        std::string_view getCategory() const noexcept override { return "Flow Control"; }

        bool             needsOwnedEndMarker()        const noexcept override { return true; }
        std::string_view getOwnedEndMarkerPluginDir() const noexcept override { return "End"; }

        xnode_os_node& CreateNodeInstance() override
        {
            auto* pNode = new function_node();
            pNode->m_pFactory = this;
            return *pNode;
        }

        void DestroyNodeInstance(xnode_os_node& Node) override
        {
            delete static_cast<function_node*>(&Node);
        }
    };
}
XPROPERTY_VREG(function_node_factory)

extern "C" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host& /*Host*/) noexcept
{
    return *new function_node_factory();
}

extern "C" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& Factory) noexcept
{
    delete static_cast<function_node_factory*>(&Factory);
}
