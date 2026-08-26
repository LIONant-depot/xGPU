#pragma once
// The small ImGui leaf panels, extracted from the monolithic E27_NodeOS_Editor.cpp (header #8) and
// merged into one file per the split plan - none is more than a few hundred lines and none has
// interesting internal structure: DrawNodeLibraryPanel, DrawRuntimeLogPanel,
// DrawNodePropertiesEmptyState, DrawFunctionPinEditor, DrawNodePropertiesPanel. These were physically
// scattered around DrawGraphCanvas in the original file (DrawNodeLibraryPanel before it, the other
// four after it) - gathered here verbatim, in the plan's listed order.
#include "NodeOS_Common.h"
#include "NodeOS_Types.h"
#include "NodeOS_PropertySerialize.h"
#include "NodeOS_CommandBuilders.h"

namespace nodeos
{
    //------------------------------------------------------------------------------------------------
    static void DrawNodeLibraryPanel(std::vector<plugin_source_entry>& Sources, std::vector<available_node_type>& AvailableTypes, bool& bDirty)
    {
        // Pick up any background compile (started by the button below) that finished since last frame,
        // before drawing anything - so this frame's log/"loaded" text already reflects it. Runs off the
        // UI thread (std::async) so the editor never freezes while a plugin recompiles.
        for (auto& Src : Sources)
        {
            if (Src.m_bCompiling && Src.m_Future.valid() && Src.m_Future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                auto Result = Src.m_Future.get();
                Src.m_bCompiling = false;
                if (MergeCompileResult(Src, Result, AvailableTypes))
                    bDirty = true;
            }
        }

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(430, 500), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Node Library"))
        {
            ImGui::TextWrapped("Auto-discovered from Plugins/ - NOT part of this program's own build. Placing one "
                                "from the Add Node menu compiles+loads it automatically the first time; the button "
                                "below is only for forcing a recompile after editing a plugin's source.");
            ImGui::Separator();

            for (auto& Src : Sources)
            {
                ImGui::PushID(Src.m_SourcePath.c_str());
                ImGui::BulletText("%s", Src.m_DisplayName.c_str());
                ImGui::TextWrapped("%s", Src.m_SourcePath.c_str());

                if (Src.m_bCompiling)
                {
                    ImGui::BeginDisabled();
                    ImGui::Button("Compiling...");
                    ImGui::EndDisabled();
                }
                else if (ImGui::Button(Src.m_bLoaded ? "Recompile & Reload" : "Compile & Load"))
                {
                    Src.m_bCompiling = true;
                    Src.m_Future = std::async(std::launch::async, CompilePluginWorker, Src.m_SourcePath);
                }

                if (Src.m_bLoaded)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "loaded");
                }

                if (!Src.m_CompileLog.empty())
                {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0.3f));
                    ImGui::BeginChild(("log" + Src.m_SourcePath).c_str(), ImVec2(0, 60), true);
                    ImGui::TextUnformatted(Src.m_CompileLog.c_str());
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                }
                ImGui::Separator();
                ImGui::PopID();
            }
        }
        ImGui::End();
    }

    // The one on-screen surface for whatever a running program logs (see GetRuntimeLog/host_bridge
    // above) - Print's real output lands here. Cleared automatically at the start of every run
    // (RunProgram), not accumulated across runs, so each click shows exactly that run's own trace.
    static void DrawRuntimeLogPanel()
    {
        ImGui::SetNextWindowPos(ImVec2(1265, 440), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(200, 150), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Console"))
        {
            if (ImGui::SmallButton("Clear")) GetRuntimeLog().clear();
            ImGui::Separator();
            for (auto& Line : GetRuntimeLog())
                ImGui::TextUnformatted(Line.c_str());
        }
        ImGui::End();
    }

    static void DrawNodePropertiesEmptyState(const char* pMessage)
    {
        ImGui::SetNextWindowPos(ImVec2(1265, 90), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Node Properties"))
            ImGui::TextDisabled("%s", pMessage);
        ImGui::End();
    }

    //------------------------------------------------------------------------------------------------
    // Dockable panel for the currently-selected node's properties - active only when exactly one node
    // is selected (multi-select property editing is out of scope for now). Draws with the HOST's own
    // real xproperty::inspector over the node's own real getProperties() object, uniformly for every
    // node type - no plugin ever needs to compile its own copy of ImGui/xPropertyImGuiInspector.cpp or
    // implement its own drawing function just to show a property panel (see xnode_os_plugin_api.h's
    // top comment for why a real xproperty::type::object crossing the DLL boundary is safe now).
    //
    // The Inspector instance itself MUST persist across frames rather than being rebuilt from scratch
    // on every call - see cube_node.cpp's old DrawProperties comment (from before this became the
    // host's job) for the full explanation: Show() seeds each row's ImGui id partly from the address
    // of its component-list slot, so a fresh AppendEntity()/AppendEntityComponent() call every frame
    // makes every widget's id unstable, which looks exactly like "nothing happens when I click." Only
    // rebuild when a different node gets selected.
    //------------------------------------------------------------------------------------------------
    // Function's own pin-list encoding, decoded/re-encoded HOST-SIDE for the pin editor below - a
    // duplicate of function_node.cpp's identical private DecodePins, since plugin internals never
    // cross the DLL boundary (xnode_os_plugin_api.h's top comment); the host only ever reads/writes
    // the InputsSpec/OutputsSpec STRING PROPERTY through reflection, same as every other inline
    // widget in this file touches a node's properties.
    struct host_pin_spec { std::string m_Name, m_Type; bool m_bRequired = true; bool m_bReadOnly = true; };
    static std::vector<host_pin_spec> DecodeHostPinSpec(const std::string& Spec)
    {
        std::vector<host_pin_spec> Out;
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
                host_pin_spec Pin;
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
    static std::string EncodeHostPinSpec(const std::vector<host_pin_spec>& Pins)
    {
        std::string Out;
        for (auto& P : Pins)
        {
            if (!Out.empty()) Out += '|';
            Out += P.m_Name; Out += ':'; Out += P.m_Type; Out += ':';
            Out += (P.m_bRequired ? '1' : '0'); Out += ':'; Out += (P.m_bReadOnly ? '1' : '0');
        }
        return Out;
    }
    static constexpr const char* s_FunctionPinTypes[] = { "Float", "Int", "Short", "Bool", "Any", "Span<Any>" };

    // Draws one Add/Remove/edit pin table (Inputs or Outputs) for a Function node, directly in the
    // properties panel - Function's port COUNT is user-editable, unlike every other node type here,
    // so it needs real table UI rather than the single inline widget Constant/Compare use for their
    // one fixed slot. Every edit re-encodes the whole spec string and commits it through the same
    // undo-safe SetProperties command the rest of this panel uses - the node's own getInputs()/
    // getOutputs() derive the local-mirrored pins from this same spec directly (see
    // function_node.cpp), so there's no separate instance to keep in sync anymore.
    static void DrawFunctionPinEditor(xundo::system& System, xnode_os_node* pFnNode, std::uint64_t FnNodeId, const char* pSpecMemberName, const char* pLabel)
    {
        const xproperty::type::object* pObj = pFnNode->getProperties();
        const xproperty::type::members* pSpecMember = nullptr;
        for (auto& M : pObj->m_Members) if (std::strcmp(M.m_pName, pSpecMemberName) == 0) { pSpecMember = &M; break; }
        if (!pSpecMember) return;

        xproperty::any SpecOut; xproperty::settings::context ReadCtx;
        std::string SpecText;
        if (pSpecMember->TryRead(pFnNode, SpecOut, ReadCtx) && SpecOut.is<std::string>())
            SpecText = SpecOut.get<std::string>();
        auto Pins = DecodeHostPinSpec(SpecText);

        auto Commit = [&](std::vector<host_pin_spec>& NewPins)
        {
            const std::string Before = SerializePropertiesToString(pFnNode);
            xproperty::any In{ EncodeHostPinSpec(NewPins) }; xproperty::settings::context WriteCtx;
            (void)pSpecMember->TryWrite(pFnNode, In, WriteCtx);
            const std::string After = SerializePropertiesToString(pFnNode);
            if (After != Before)
                commands::Run(System, commands::MakeSetProperties(FnNodeId, Before, After));
        };

        ImGui::TextUnformatted(pLabel);
        int RemoveIndex = -1;
        for (int i = 0; i < (int)Pins.size(); ++i)
        {
            ImGui::PushID(i);
            auto& Pin = Pins[i];
            char NameBuf[64]; strncpy_s(NameBuf, Pin.m_Name.c_str(), _TRUNCATE);
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::InputText("##name", NameBuf, sizeof(NameBuf)))
            {
                Pin.m_Name = NameBuf;
                Commit(Pins);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::BeginCombo("##type", Pin.m_Type.c_str()))
            {
                for (auto* pTypeName : s_FunctionPinTypes)
                {
                    const bool bSel = Pin.m_Type == pTypeName;
                    if (ImGui::Selectable(pTypeName, bSel)) { Pin.m_Type = pTypeName; Commit(Pins); }
                    if (bSel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            bool bReq = Pin.m_bRequired;
            if (ImGui::Checkbox("Req", &bReq)) { Pin.m_bRequired = bReq; Commit(Pins); }
            ImGui::SameLine();
            bool bRO = Pin.m_bReadOnly;
            if (ImGui::Checkbox("RO", &bRO)) { Pin.m_bReadOnly = bRO; Commit(Pins); }
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) RemoveIndex = i;
            ImGui::PopID();
        }
        if (RemoveIndex >= 0)
        {
            Pins.erase(Pins.begin() + RemoveIndex);
            Commit(Pins);
        }
        if (ImGui::SmallButton(std::format("+ Add {}", pLabel).c_str()))
        {
            Pins.push_back({ std::format("{}{}", pLabel[0], Pins.size() + 1), "Float", true, true });
            Commit(Pins);
        }
    }

    static void DrawNodePropertiesPanel(std::vector<node_instance>& Nodes, const std::set<std::uint64_t>& SelectedNodes, xundo::system& System, std::vector<plugin_source_entry>& Sources, std::vector<available_node_type>& AvailableTypes)
    {
        // A control node and its owned End/End-Else marker(s) select as one compound group now (see
        // the click-selection cascade in DrawGraphCanvas) - so "exactly one node selected" no longer
        // holds for them. If this selection is exactly one such group, resolve its root (the one
        // member nobody ELSE in the group owns - rules out an EndElse marker, which is itself pointed
        // to by the owner, in favor of the actual If/ForEachLoop) and show properties for THAT,
        // merged with its owned marker's own properties (just "IsElse" today) - the whole group reads
        // as one node to the user (NODE_SCRIPTING_DESIGN.md section 4.1), so an edit like the Else
        // toggle belongs on the same panel as the owner, even though it's still physically stored on
        // the End marker itself (getOutputs()'s dynamic "ElseEnd" pin has to read its own instance's
        // field - see end_marker_node.cpp).
        std::uint64_t PrimaryId = 0;
        if (SelectedNodes.size() == 1)
        {
            PrimaryId = *SelectedNodes.begin();
        }
        else
        {
            for (auto Id : SelectedNodes)
            {
                const auto Group = commands::ExpandOwnershipCascade(Nodes, { Id });
                if (std::set<std::uint64_t>(Group.begin(), Group.end()) != SelectedNodes) continue;
                const bool bOwnedByOther = std::any_of(Nodes.begin(), Nodes.end(), [&](auto& N) { return N.m_OwnedEndId == Id; });
                if (!bOwnedByOther) { PrimaryId = Id; break; }
            }
        }
        if (PrimaryId == 0)
        {
            DrawNodePropertiesEmptyState(SelectedNodes.empty() ? "Select a node to see its properties." : "Select a single node to see its properties.");
            return;
        }

        auto It = std::find_if(Nodes.begin(), Nodes.end(), [&](auto& N) { return N.m_Id == PrimaryId; });
        if (It == Nodes.end() || !It->m_pNode) { DrawNodePropertiesEmptyState("(node no longer exists)"); return; }

        xnode_os_node* pNode = It->m_pNode;
        const std::uint64_t NodeId = It->m_Id;

        xnode_os_node* pMarkerNode = nullptr;
        std::uint64_t  MarkerNodeId = 0;
        // The End/End-Else marker type is shared by every owner (If, ForEachLoop, ...), but "Else" -
        // a second branch - is only a meaningful concept for If. Merging it into a ForEachLoop's own
        // panel would show a checkbox that does nothing sensible there, so this stays scoped to the
        // one owner type it actually applies to (same name-based special-case this function already
        // used before the merge existed, for exactly the same reason).
        if (It->m_OwnedEndId != 0 && pNode->m_pFactory->getName() == "If")
        {
            auto MarkerIt = std::find_if(Nodes.begin(), Nodes.end(), [&](auto& N) { return N.m_Id == It->m_OwnedEndId; });
            if (MarkerIt != Nodes.end() && MarkerIt->m_pNode && HasAnyProperties(MarkerIt->m_pNode))
            {
                pMarkerNode  = MarkerIt->m_pNode;
                MarkerNodeId = MarkerIt->m_Id;
            }
        }

        // HasAnyProperties, not the narrower HasSerializableProperties - the real xproperty::inspector
        // below can render scopes/lists/anything reflected, so gating its own visibility on the
        // var-only check would incorrectly show "has no properties" for a node whose reflected data
        // is entirely, say, a list. (SerializePropertiesToString further down, used only for the
        // undo Before/After diff, stays var-only on its own - that's the one job it genuinely can't
        // do more of yet - but it degrades safely to an empty snapshot rather than failing.)
        const bool bOwnHasProps = HasAnyProperties(pNode);
        if (!bOwnHasProps && !pMarkerNode)
        {
            DrawNodePropertiesEmptyState(std::format("{} has no properties.", pNode->m_pFactory->getName()).c_str());
            return;
        }

        static xproperty::inspector s_Inspector("Node Properties");
        static void* s_pBoundNode       = nullptr;
        static void* s_pBoundMarkerNode = nullptr;
        if (s_pBoundNode != pNode || s_pBoundMarkerNode != pMarkerNode)
        {
            s_Inspector.clear();
            s_Inspector.AppendEntity();
            if (bOwnHasProps) s_Inspector.AppendEntityComponent(*pNode->getProperties(), pNode);
            if (pMarkerNode)  s_Inspector.AppendEntityComponent(*pMarkerNode->getProperties(), pMarkerNode);
            s_pBoundNode       = pNode;
            s_pBoundMarkerNode = pMarkerNode;
        }

        const std::string Before       = bOwnHasProps ? SerializePropertiesToString(pNode)       : std::string{};
        const std::string MarkerBefore = pMarkerNode  ? SerializePropertiesToString(pMarkerNode)  : std::string{};
        xproperty::settings::context Context;
        ImGui::SetNextWindowPos(ImVec2(1265, 90), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);
        s_Inspector.Show(Context, [] {});
        const std::string After       = bOwnHasProps ? SerializePropertiesToString(pNode)      : std::string{};
        const std::string MarkerAfter = pMarkerNode  ? SerializePropertiesToString(pMarkerNode) : std::string{};

        if (bOwnHasProps && After != Before)
            commands::Run(System, commands::MakeSetProperties(NodeId, Before, After));

        if (pMarkerNode && MarkerAfter != MarkerBefore)
        {
            // The End node's own "IsElse" checkbox (NODE_SCRIPTING_DESIGN.md section 4.2) is the one
            // property edit that means more than "just store this value" - it also creates or
            // removes a further, paired End marker. Detected generically off the serialized snapshot
            // rather than casting pMarkerNode to any concrete plugin type (never safe across the
            // plugin DLL boundary - see xnode_os_plugin_api.h).
            // SetProperties always runs FIRST: SetEndElseState's own Redo() reads the marker's live
            // getOutputs() to find its new "ElseEnd" pin index, which only reliably reflects
            // IsElse==true if SetProperties (whose Redo() actually applies the After snapshot via
            // ApplyPropertiesFromString) has already run - true during this initial edit only because
            // the ImGui widget mutated pMarkerNode directly, but not guaranteed during a later redo
            // replay unless the ordering is explicit here too.
            commands::Run(System, commands::MakeSetProperties(MarkerNodeId, MarkerBefore, MarkerAfter));

            const bool bWasElse = ReadBoolPropertyFromSnapshot(MarkerBefore, "IsElse");
            const bool bIsElse  = ReadBoolPropertyFromSnapshot(MarkerAfter,  "IsElse");
            if (bIsElse && !bWasElse)
            {
                auto* pEndSrc = commands::FindSourceByDirName(Sources, "End");
                if (pEndSrc && EnsureLoadedAndGetType(*pEndSrc, AvailableTypes))
                    commands::Run(System, commands::MakeSetEndElseEnable(MarkerNodeId, xresource::guid_generator::Instance64(), pEndSrc->m_DirName, xresource::guid_generator::Instance64()));
            }
            else if (!bIsElse && bWasElse)
                commands::Run(System, commands::MakeSetEndElseDisable(MarkerNodeId));
        }

        // Function's signature is user-editable (add/remove/rename/retype pins, toggle Required/
        // ReadOnly) - the raw InputsSpec/OutputsSpec text fields above are a harmless power-user
        // escape hatch (same "the encoded string is still just an ordinary property" spirit as
        // Constant's Value), but this table is the real, intended editing surface.
        if (pNode->m_pFactory->getName() == "Function")
        {
            ImGui::Separator();
            DrawFunctionPinEditor(System, pNode, NodeId, "InputsSpec",  "Inputs");
            ImGui::Separator();
            DrawFunctionPinEditor(System, pNode, NodeId, "OutputsSpec", "Outputs");
        }
    }
}
