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
#include <vector>
#include <string>

namespace
{
    struct node_builder_pin { std::string m_Name, m_Type; bool m_bRequired = true; bool m_bReadOnly = true; };

    inline std::vector<node_builder_pin> DecodePins(const std::string& Spec)
    {
        std::vector<node_builder_pin> Out;
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
                node_builder_pin Pin;
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

    struct node_builder_node : xnode_os_node
    {
        std::string m_Name        = "MyNode";            // Plugins/<Name>/ folder + published node type name - same role Function's own "Name" plays
        std::string m_InputsSpec  = "A:Float:1:1";
        std::string m_OutputsSpec = "Result:Float:1:0";

        mutable std::vector<std::string>       m_InStorage,  m_OutStorage;
        mutable std::vector<xnode_os_port_desc> m_InDescs,   m_OutDescs;

        XPROPERTY_VDEF
        ( "node_builder_node", node_builder_node
        , obj_member<"Name", &node_builder_node::m_Name
            , member_help<"Published node type name - becomes both the Plugins/<Name>/ folder and the palette entry.">>
        , obj_member<"InputsSpec",  &node_builder_node::m_InputsSpec>
        , obj_member<"OutputsSpec", &node_builder_node::m_OutputsSpec>
        )

        // Same Rebuild as function_node.cpp - not shared via a common header on purpose (each plugin
        // is its own DLL, self-contained, matching this corpus's usual no-cross-plugin-linkage rule).
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

        // Declared Inputs (external - what the PUBLISHED node's own Inputs[] will be), then the
        // mirror of declared Outputs (local - this node's own body writes its results here).
        std::span<const xnode_os_port_desc> getInputs() const noexcept override
        {
            Rebuild(m_InputsSpec, m_OutputsSpec, m_InStorage, m_InDescs);
            return m_InDescs;
        }

        // Declared Outputs (external), then the mirror of declared Inputs (local - the body reads its
        // parameters here), then "End" - the owned-scope marker, must stay last.
        std::span<const xnode_os_port_desc> getOutputs() const noexcept override
        {
            Rebuild(m_OutputsSpec, m_InputsSpec, m_OutStorage, m_OutDescs);
            m_OutDescs.push_back({ "End", "Scope", true, true, false });
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
