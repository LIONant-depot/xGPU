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
// the external ones since they're decoded from the exact same two spec strings on every call.
// m_bLocalScope is what lets E27_NodeOS_Editor.cpp's IsDataLinkScopeValid restrict these specific
// pins to links whose OTHER endpoint is physically inside this node's own scope span - see its own
// comment for why a stray wire out of a local pin would let data escape a scope that stops
// existing once the function returns.
//
// The pin list itself is user-editable (Add/Remove Input/Output, each with Name/Type/Required/
// ReadOnly - see E27_NodeOS_Editor.cpp's DrawNodePropertiesPanel "Function" block for the actual
// editor UI), stored as two encoded strings ("Name:Type:Required:ReadOnly", '|'-joined per pin) -
// same "store what was typed, interpret it later" approach constant_node.cpp's Value already uses.
// getInputs()/getOutputs() decode into PER-INSTANCE mutable storage, rebuilt fresh (never mutated
// in place) each time they're called, so no returned const char* can dangle after a resize - see
// this session's own thread_local-pointer-aliasing lesson: a shared/static buffer would corrupt
// across the multiple Function instances one graph can have, which a shared thread_local never hit
// before only because nothing else's port list varied in COUNT, only in type.
#include "../../SDK/xnode_os_plugin_api.h"
#include "../../SDK/xnode_os_shared_types.h"
#include <vector>
#include <string>

namespace
{
    struct function_pin { std::string m_Name, m_Type; bool m_bRequired = true; bool m_bReadOnly = true; };

    inline std::vector<function_pin> DecodePins(const std::string& Spec)
    {
        std::vector<function_pin> Out;
        std::size_t Pos = 0;
        while (Pos < Spec.size())
        {
            const std::size_t Bar = Spec.find('|', Pos);
            const std::string Entry = Spec.substr(Pos, Bar == std::string::npos ? std::string::npos : Bar - Pos);
            const std::size_t C1 = Entry.find(':');
            const std::size_t C2 = (C1 == std::string::npos) ? std::string::npos : Entry.find(':', C1 + 1);
            const std::size_t C3 = (C2 == std::string::npos) ? std::string::npos : Entry.find(':', C2 + 1);
            if (C1 != std::string::npos && C2 != std::string::npos && C3 != std::string::npos)
            {
                function_pin Pin;
                Pin.m_Name      = Entry.substr(0, C1);
                Pin.m_Type      = Entry.substr(C1 + 1, C2 - C1 - 1);
                Pin.m_bRequired = Entry[C2 + 1] == '1';
                Pin.m_bReadOnly = Entry[C3 + 1] == '1';
                Out.push_back(std::move(Pin));
            }
            if (Bar == std::string::npos) break;
            Pos = Bar + 1;
        }
        return Out;
    }

    struct function_node : xnode_os_node
    {
        std::string m_Name        = "MyFunction";
        std::string m_InputsSpec  = "A:Float:1:1";
        std::string m_OutputsSpec = "Result:Float:1:0";

        mutable std::vector<std::string>       m_InStorage,  m_OutStorage;
        mutable std::vector<xnode_os_port_desc> m_InDescs,   m_OutDescs;

        XPROPERTY_VDEF
        ( "function_node", function_node
        , obj_member<"Name",        &function_node::m_Name>
        , obj_member<"InputsSpec",  &function_node::m_InputsSpec>
        , obj_member<"OutputsSpec", &function_node::m_OutputsSpec>
        )

        // Reserves ALL storage up front (two strings per pin, external+mirror combined) before taking
        // any .c_str() pointer, so the vector never reallocates again mid-loop - every pointer handed
        // out below stays valid for as long as Storage itself isn't touched again (i.e. until the
        // next getInputs/getOutputs call, which rebuilds it fresh). OwnSpec drives the external
        // (caller-facing) pins in their own declared direction; MirrorSpec drives the local
        // (body-facing) pins in the OPPOSITE direction, flagged m_bLocalScope.
        static void Rebuild(const std::string& OwnSpec, const std::string& MirrorSpec, std::vector<std::string>& Storage, std::vector<xnode_os_port_desc>& Descs)
        {
            const auto OwnPins    = DecodePins(OwnSpec);
            const auto MirrorPins = DecodePins(MirrorSpec);
            Storage.clear(); Storage.reserve((OwnPins.size() + MirrorPins.size()) * 2);
            Descs.clear();   Descs.reserve(OwnPins.size() + MirrorPins.size());
            for (auto& P : OwnPins)    { Storage.push_back(P.m_Name); Storage.push_back(P.m_Type); }
            for (auto& P : MirrorPins) { Storage.push_back(P.m_Name); Storage.push_back(P.m_Type); }
            std::size_t Slot = 0;
            for (auto& P : OwnPins)    { Descs.push_back({ Storage[Slot * 2].c_str(), Storage[Slot * 2 + 1].c_str(), P.m_bRequired, P.m_bReadOnly, false }); ++Slot; }
            for (auto& P : MirrorPins) { Descs.push_back({ Storage[Slot * 2].c_str(), Storage[Slot * 2 + 1].c_str(), P.m_bRequired, P.m_bReadOnly, true  }); ++Slot; }
        }

        // A fixed "Exec" input, always FIRST, then declared Inputs (external), then the mirror of
        // declared Outputs (local - the body writes its return values here). Not part of the user-
        // editable spec, same treatment as "End" being a fixed, always-present output below. Input
        // only, no matching Exec output: per NODE_SCRIPTING_DESIGN.md's exec-flow addition, the
        // CALLER (a Call node) gets control back once this function returns, not this node itself -
        // Function has no notion of "what comes after," only "run when triggered." Being first shifts
        // every declared input's own index by one - any link already wired to this node's inputs from
        // before this change points at the wrong pin now and needs re-wiring.
        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            Rebuild(m_InputsSpec, m_OutputsSpec, m_InStorage, m_InDescs);
            m_InDescs.insert(m_InDescs.begin(), { "Exec", "Exec", true, true, false });
            return m_InDescs;
        }

        // Declared Outputs (external) first, then the mirror of declared Inputs (local - the body
        // reads its parameters here), then "End" - the read-only ownership pin to this node's own End
        // marker, which must stay LAST (see for_each_loop_node.cpp's own comment on why).
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            Rebuild(m_OutputsSpec, m_InputsSpec, m_OutStorage, m_OutDescs);
            m_OutDescs.push_back({ "End", "Scope", true, true, false });
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
