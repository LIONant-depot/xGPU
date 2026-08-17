#include "source/xGPU.h"
#include "source/tools/xgpu_imgui_breach.h"

#include "dependencies/xproperty/source/xcore/my_properties.h"
#include "dependencies/xproperty/source/examples/imgui/xPropertyImGuiInspector.h"
#include "dependencies/imgui-node-editor/imgui_node_editor.h"

#include "example.lionprj/Cache/Plugins/xrtcs.plugin/source/Graph/xrtcs_document.h"
#include "example.lionprj/Cache/Plugins/xrtcs.plugin/source/Graph/xrtcs_operations.h"
#include "example.lionprj/Cache/Plugins/xrtcs.plugin/source/Graph/xrtcs_demo_data.h"

namespace ed = ax::NodeEditor;

//-----------------------------------------------------------------------------------
//
// E26 - RTCS Editor: authors a Template (slots/bindings/references), edits a Resolution
// Manifest (per-slot Selected/Omitted/Failed dispositions), triggers resolve() to produce a
// Resolved Graph + sealed Configuration, and views a Provenance panel over the occurrence chain.
// v1 scope covers exactly the first 9 formalized RTCS contracts (identity through templates) -
// see xrtcs.plugin/README.md. execution/authorization/evidence/decision (spec sections 9-11) is
// prose-only in the spec and stays out of scope here.
//
// Template canvas (this pass): one imgui-node-editor node per construction_slot, wired by
// template_binding edges. Every slot is one node with a single input pin (top of the diamond of
// possible wires into it) and a single output pin - bindings are authored at slot granularity,
// not per-port, so a slot's distinct contract ports (see contract_content::m_Ports) are shown as
// text inside the node rather than as separate pins; this keeps pin-drag wiring simple for a demo
// while still letting every binding in the spec's own worked example be drawn and edited. Node
// border color = the slot's construction_reference kind (Contract/ExactSemantic/Alias/Parameter);
// node fill = the active manifest's disposition for that slot (selected/omitted/failed/no entry) -
// this is the "fill=resolution state" the manifest panel (next pass) will let the user change live.
// Node/link selection and delete are real (rslgraph-ui itself never finished this - see memory);
// dragging between a slot's output and another slot's input authors a brand-new template_binding.
//
//-----------------------------------------------------------------------------------

namespace e26
{
    static void Debugger(std::string_view View)
    {
        printf("%s\n", View.data());
    }

    //------------------------------------------------------------------------------------------------
    // Node/pin id mapping: node id == slot id (separate id namespace from pins/links in
    // imgui-node-editor, so no need to offset). Pin ids need to be distinct from each other within
    // the same slot, so out-pin = SlotId*2, in-pin = SlotId*2+1 (slot ids are minted from xrtcs'
    // single global counter, so this can never collide with another slot's pins). Link id == binding id.
    //------------------------------------------------------------------------------------------------
    static std::uint64_t OutPinOf(xrtcs::id SlotId) noexcept { return SlotId.m_Value * 2; }
    static std::uint64_t InPinOf (xrtcs::id SlotId) noexcept { return SlotId.m_Value * 2 + 1; }
    static xrtcs::id     SlotOfPin(std::uint64_t PinValue, bool& bWasOutput) noexcept
    {
        bWasOutput = (PinValue % 2) == 0;
        return xrtcs::id{ PinValue / 2 };
    }

    static ImColor ReferenceKindColor(xrtcs::reference_kind Kind) noexcept
    {
        switch (Kind)
        {
        case xrtcs::reference_kind::Contract:      return ImColor(90, 150, 235, 255);   // blue
        case xrtcs::reference_kind::ExactSemantic: return ImColor(90, 200, 140, 255);   // green
        case xrtcs::reference_kind::Alias:         return ImColor(190, 120, 235, 255);  // purple
        case xrtcs::reference_kind::Parameter:     return ImColor(235, 165, 80, 255);   // orange
        }
        return ImColor(160, 160, 160, 255);
    }

    static const char* ReferenceKindName(xrtcs::reference_kind Kind) noexcept
    {
        switch (Kind)
        {
        case xrtcs::reference_kind::Contract:      return "Contract";
        case xrtcs::reference_kind::ExactSemantic: return "ExactSemantic";
        case xrtcs::reference_kind::Alias:         return "Alias";
        case xrtcs::reference_kind::Parameter:     return "Parameter";
        }
        return "?";
    }

    // What a slot's reference points at, resolved to a human-readable label - meaning depends on
    // m_Kind (see xrtcs_template.h's construction_reference comments).
    static std::string ReferenceTargetLabel(xrtcs::document& Doc, const xrtcs::construction_reference& Ref)
    {
        switch (Ref.m_Kind)
        {
        case xrtcs::reference_kind::Contract:
            if (auto* pC = Doc.FindContract(Ref.m_Target))
                return pC->m_Name;
            return "<missing contract>";
        case xrtcs::reference_kind::ExactSemantic:
            if (auto* pS = Doc.FindSemantic(Ref.m_Target))
                return pS->m_Name;
            return "<missing semantic>";
        default:
            return std::format("id #{}", Ref.m_Target.m_Value);
        }
    }

    // Fill color for the active manifest's disposition on this slot - null (no entry at all) reads
    // as "not part of this manifest", distinct from an explicit Omitted disposition.
    static ImColor DispositionFillColor(const xrtcs::resolution_manifest* pManifest, xrtcs::id SlotId) noexcept
    {
        if (!pManifest) return ImColor(60, 60, 60, 200);
        for (auto& E : pManifest->m_Entries)
        {
            if (E.m_SlotId.m_Value != SlotId.m_Value) continue;
            switch (E.m_Disposition)
            {
            case xrtcs::disposition::selected: return ImColor(40, 110, 60, 220);   // green fill
            case xrtcs::disposition::omitted:  return ImColor(70, 70, 70, 160);    // hollow gray
            case xrtcs::disposition::failed:   return ImColor(140, 40, 40, 220);   // red fill
            }
        }
        return ImColor(60, 60, 60, 200);
    }

    //------------------------------------------------------------------------------------------------
    // Removes a slot and every binding that touches it (cascade - a binding can't dangle on a
    // deleted endpoint), both from the document and from the owning template's own id vectors.
    //------------------------------------------------------------------------------------------------
    static void DeleteSlotCascade(xrtcs::document& Doc, xrtcs::construction_template_content& Tpl, xrtcs::id SlotId)
    {
        std::erase(Tpl.m_Slots, SlotId);
        for (auto It = Tpl.m_Bindings.begin(); It != Tpl.m_Bindings.end(); )
        {
            auto* pBinding = Doc.FindBinding(*It);
            if (pBinding && (pBinding->m_SourceSlot.m_Value == SlotId.m_Value || pBinding->m_TargetSlot.m_Value == SlotId.m_Value))
            {
                Doc.m_Bindings.erase(It->m_Value);
                It = Tpl.m_Bindings.erase(It);
            }
            else ++It;
        }
        Doc.m_Slots.erase(SlotId.m_Value);
    }

    static void DeleteBinding(xrtcs::construction_template_content& Tpl, xrtcs::document& Doc, xrtcs::id BindingId)
    {
        std::erase(Tpl.m_Bindings, BindingId);
        Doc.m_Bindings.erase(BindingId.m_Value);
    }

    //------------------------------------------------------------------------------------------------
    static void DrawTemplateCanvas(xrtcs::document& Doc, xrtcs::id TemplateId, xrtcs::id ManifestId, ed::EditorContext* pEditor, xrtcs::id& OutSelectedSlot)
    {
        auto* pTpl = Doc.FindTemplate(TemplateId);
        if (!pTpl) return;
        auto* pManifest = Doc.FindManifest(ManifestId);

        ImGui::SetNextWindowPos(ImVec2(360, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(700, 420), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("RTCS Template Canvas"))
        {
            ImGui::End();
            return;
        }
        ImGui::TextDisabled("Template: %s | drag between pins to wire a binding, Delete to remove selection, right-click for Add Slot", pTpl->m_Name.c_str());

        ed::SetCurrentEditor(pEditor);
        ed::Begin("RTCS Template Editor");

        for (auto SlotId : pTpl->m_Slots)
        {
            auto* pSlot = Doc.FindSlot(SlotId);
            if (!pSlot) continue;

            const ImColor BorderColor = ReferenceKindColor(pSlot->m_Reference.m_Kind);
            const ImColor FillColor   = DispositionFillColor(pManifest, SlotId);

            ed::PushStyleColor(ed::StyleColor_NodeBorder, BorderColor);
            ed::PushStyleColor(ed::StyleColor_NodeBg, FillColor);
            ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 2.0f);

            ed::BeginNode(SlotId.m_Value);
            ImGui::TextUnformatted(pSlot->m_Name.c_str());
            ImGui::TextDisabled("%s | %s", ReferenceKindName(pSlot->m_Reference.m_Kind), pSlot->m_Requirement == xrtcs::slot_requirement::required ? "required" : "optional");
            ImGui::Text("-> %s", ReferenceTargetLabel(Doc, pSlot->m_Reference).c_str());

            ed::BeginPin(InPinOf(SlotId), ed::PinKind::Input);
            ImGui::Text("in");
            ed::EndPin();
            ImGui::SameLine();
            ed::BeginPin(OutPinOf(SlotId), ed::PinKind::Output);
            ImGui::Text("out");
            ed::EndPin();

            ed::EndNode();

            if (ed::GetNodePosition(SlotId.m_Value).x == 0 && ed::GetNodePosition(SlotId.m_Value).y == 0 && (pSlot->m_CanvasX != 0 || pSlot->m_CanvasY != 0))
                ed::SetNodePosition(SlotId.m_Value, ImVec2(pSlot->m_CanvasX, pSlot->m_CanvasY));

            ed::PopStyleVar();
            ed::PopStyleColor(2);
        }

        for (auto BindingId : pTpl->m_Bindings)
        {
            auto* pBinding = Doc.FindBinding(BindingId);
            if (!pBinding) continue;
            ed::Link(BindingId.m_Value, OutPinOf(pBinding->m_SourceSlot), InPinOf(pBinding->m_TargetSlot));
        }

        // --- Handle creating a new binding by dragging between two pins ---
        if (ed::BeginCreate())
        {
            ed::PinId A, B;
            if (ed::QueryNewLink(&A, &B))
            {
                bool bAIsOut = false, bBIsOut = false;
                const auto SlotA = SlotOfPin(A.Get(), bAIsOut);
                const auto SlotB = SlotOfPin(B.Get(), bBIsOut);

                if (bAIsOut != bBIsOut && SlotA.m_Value != SlotB.m_Value)
                {
                    if (ed::AcceptNewItem(ImColor(0, 255, 0), 2.0f))
                    {
                        xrtcs::id SourceSlot = bAIsOut ? SlotA : SlotB;
                        xrtcs::id TargetSlot = bAIsOut ? SlotB : SlotA;

                        auto& Binding = Doc.AddBinding
                        ({ .m_SourceSlot    = SourceSlot
                         , .m_SourcePort    = "out"
                         , .m_TargetSlot    = TargetSlot
                         , .m_TargetPort    = "in"
                         , .m_ExecutionKind = xrtcs::execution_semantics::dataflow
                         , .m_Participation = xrtcs::binding_participation::required
                         });
                        pTpl->m_Bindings.push_back(Binding.m_BindingId);
                    }
                }
                else
                {
                    ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                }
            }
        }
        ed::EndCreate();

        // --- Handle deleting selected nodes/links ---
        if (ed::BeginDelete())
        {
            ed::LinkId DeadLink;
            while (ed::QueryDeletedLink(&DeadLink))
                if (ed::AcceptDeletedItem())
                    DeleteBinding(*pTpl, Doc, xrtcs::id{ DeadLink.Get() });

            ed::NodeId DeadNode;
            while (ed::QueryDeletedNode(&DeadNode))
                if (ed::AcceptDeletedItem())
                    DeleteSlotCascade(Doc, *pTpl, xrtcs::id{ DeadNode.Get() });
        }
        ed::EndDelete();

        // --- Right-click canvas: add a new slot at the cursor ---
        const ImVec2 PopupPos = ImGui::GetMousePos();
        ed::Suspend();
        if (ed::ShowBackgroundContextMenu())
            ImGui::OpenPopup("RTCS_AddSlotPopup");

        if (ImGui::BeginPopup("RTCS_AddSlotPopup"))
        {
            if (ImGui::MenuItem("Add Slot"))
            {
                auto& NewSlot = Doc.AddSlot
                ({ .m_Name        = "new_slot"
                 , .m_Reference   = { .m_Kind = xrtcs::reference_kind::ExactSemantic }
                 , .m_Requirement = xrtcs::slot_requirement::optional
                 , .m_CanvasX     = PopupPos.x
                 , .m_CanvasY     = PopupPos.y
                 });
                pTpl->m_Slots.push_back(NewSlot.m_SlotId);
                ed::SetNodePosition(NewSlot.m_SlotId.m_Value, PopupPos);
            }
            ImGui::EndPopup();
        }
        ed::Resume();

        {
            ed::NodeId SelectedNode;
            OutSelectedSlot = (ed::GetSelectedNodes(&SelectedNode, 1) == 1) ? xrtcs::id{ SelectedNode.Get() } : xrtcs::null_id;
        }

        ed::End();
        ed::SetCurrentEditor(nullptr);
        ImGui::End();
    }

    //------------------------------------------------------------------------------------------------
    // Slot Properties panel - an xproperty::inspector bound to whatever construction_slot is
    // currently selected on the Template canvas. Name/Requirement/Role/Reference.Kind are real
    // reflected fields; Reference.Target/Policy stay DONT_SHOW (raw ids aren't hand-editable) and
    // get their own human-readable pickers injected via Show()'s callback, same pattern as the
    // Manifest panel's "Selected Implementation" picker. Rebuilt fresh every frame rather than
    // bound once, since construction_slot lives in a std::vector-backed map entry whose address can
    // move when other slots are added/removed.
    //------------------------------------------------------------------------------------------------
    static void DrawSlotPropertiesPanel(xrtcs::document& Doc, xrtcs::id SelectedSlotId)
    {
        auto* pSlot = Doc.FindSlot(SelectedSlotId);
        if (!pSlot)
        {
            ImGui::SetNextWindowPos(ImVec2(1065, 425), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(400, 60), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("RTCS Slot Properties"))
                ImGui::TextDisabled("Select a slot node on the Template Canvas to edit its properties.");
            ImGui::End();
            return;
        }

        const std::string Title = std::format("Slot Properties: {}", pSlot->m_Name);
        xproperty::inspector Inspector(Title.c_str());
        Inspector.AppendEntity();
        Inspector.AppendEntityComponent(*xproperty::getObject(*pSlot), pSlot);

        // Candidates for the Reference.Target/Policy pickers, keyed by what m_Kind currently is.
        std::vector<std::pair<xrtcs::id, std::string>> ContractCandidates;
        for (auto& [Key, C] : Doc.m_Contracts) ContractCandidates.emplace_back(xrtcs::id{ Key }, C.m_Name);
        std::vector<std::pair<xrtcs::id, std::string>> PolicyCandidates;
        for (auto& [Key, P] : Doc.m_Policies) PolicyCandidates.emplace_back(xrtcs::id{ Key }, P.m_Name);
        std::vector<std::pair<xrtcs::id, std::string>> SemanticCandidates;
        for (auto& [Key, S] : Doc.m_SemanticContents) SemanticCandidates.emplace_back(xrtcs::id{ Key }, S.m_Name);

        auto DrawIdPicker = [](const char* pLabel, xrtcs::id& Target, const std::vector<std::pair<xrtcs::id, std::string>>& Candidates)
        {
            int CurrentIndex = -1;
            for (int i = 0; i < (int)Candidates.size(); ++i)
                if (Candidates[i].first.m_Value == Target.m_Value) { CurrentIndex = i; break; }

            const std::string Preview = CurrentIndex >= 0 ? Candidates[CurrentIndex].second : "(none)";
            if (ImGui::BeginCombo(pLabel, Preview.c_str()))
            {
                for (int i = 0; i < (int)Candidates.size(); ++i)
                {
                    const bool bIsSel = (i == CurrentIndex);
                    if (ImGui::Selectable(Candidates[i].second.c_str(), bIsSel)) Target = Candidates[i].first;
                    if (bIsSel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        };

        xproperty::settings::context Context;
        ImGui::SetNextWindowPos(ImVec2(1065, 425), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(400, 260), ImGuiCond_FirstUseEver);
        Inspector.Show(Context, [&]
        {
            switch (pSlot->m_Reference.m_Kind)
            {
            case xrtcs::reference_kind::Contract:
                DrawIdPicker("Reference Contract", pSlot->m_Reference.m_Target, ContractCandidates);
                DrawIdPicker("Reference Policy",   pSlot->m_Reference.m_Policy, PolicyCandidates);
                break;
            case xrtcs::reference_kind::ExactSemantic:
                DrawIdPicker("Reference Target", pSlot->m_Reference.m_Target, SemanticCandidates);
                break;
            default:
                ImGui::TextDisabled("Alias/Parameter reference targets aren't modeled in this demo.");
                break;
            }
        });
    }

    //------------------------------------------------------------------------------------------------
    // Manifest panel: the template a manifest resolves is only reachable through its request
    // occurrence (resolution_manifest -> resolution_request -> construction_template_content) -
    // there's no direct manifest -> template pointer in the data model (matches resolution.ai: a
    // manifest's body only names the request it answers).
    //------------------------------------------------------------------------------------------------
    static xrtcs::construction_template_content* TemplateForManifest(xrtcs::document& Doc, const xrtcs::resolution_manifest& M)
    {
        auto* pRequest = Doc.FindRequest(M.m_RequestOccurrenceId);
        if (!pRequest) return nullptr;
        return Doc.FindTemplate(pRequest->m_TemplateContentId);
    }

    // Manifest summary: closed/not-closed status plus one row per slot - either an editable
    // xproperty::inspector for its resolution_entry (opens as its own dockable window, titled by
    // slot name so it can be told apart/grouped with the others), or an "Add Entry" button when no
    // entry exists yet for that slot. Disposition/Rationale/OmissionReason/FailureDiagnostic are
    // all real reflected fields (member_enum_span for the enum) - the inspector renders and writes
    // them with no hand-rolled ImGui::Combo/InputText involved. m_SelectedSemantic stays
    // DONT_SHOW (it's an opaque id, not something to hand-edit) and gets its own human-readable
    // candidate-name picker injected via Show()'s callback, the same way E19's node params inject a
    // custom resource picker into an otherwise-reflected row.
    static void DrawManifestPanel(xrtcs::document& Doc, xrtcs::id ManifestId)
    {
        ImGui::SetNextWindowPos(ImVec2(0, 90), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(350, 160), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("RTCS Manifest"))
        {
            ImGui::End();
            return;
        }

        auto* pManifest = Doc.FindManifest(ManifestId);
        if (!pManifest) { ImGui::TextDisabled("No manifest selected."); ImGui::End(); return; }

        auto* pTpl = TemplateForManifest(Doc, *pManifest);
        if (!pTpl) { ImGui::TextDisabled("Manifest's request/template could not be resolved."); ImGui::End(); return; }

        std::string ClosedReason;
        const bool bClosed = xrtcs::is_closed(Doc, *pTpl, *pManifest, ClosedReason);
        if (bClosed) ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Manifest is closed - ready to resolve.");
        else         ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.3f, 1.0f), "Not closed: %s", ClosedReason.c_str());
        ImGui::Separator();
        ImGui::TextDisabled("Each slot's entry opens as its own \"Manifest: <slot>\" window below/dockable.");

        for (auto SlotId : pTpl->m_Slots)
        {
            auto* pSlot = Doc.FindSlot(SlotId);
            if (!pSlot) continue;

            bool bHasEntry = false;
            for (auto& E : pManifest->m_Entries)
                if (E.m_SlotId.m_Value == SlotId.m_Value) { bHasEntry = true; break; }

            if (bHasEntry) { ImGui::BulletText("%s", pSlot->m_Name.c_str()); continue; }

            ImGui::PushID((int)SlotId.m_Value);
            ImGui::BulletText("%s - no manifest entry yet", pSlot->m_Name.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Add Entry"))
                pManifest->m_Entries.push_back({ .m_SlotId = SlotId, .m_Disposition = xrtcs::disposition::omitted, .m_OmissionReason = "not yet decided" });
            ImGui::PopID();
        }

        ImGui::End();

        // Every leaf-valued semantic content in the document is a legal candidate implementation -
        // the demo doesn't yet filter these through is_admissible()'s policy check (xrtcs_operations.h),
        // so an operator can select any leaf here, the same way a resolver's own candidate pool would
        // need a real admissibility filter layered on top in a non-demo implementation.
        std::vector<std::pair<xrtcs::id, std::string>> Candidates;
        for (auto& [Key, S] : Doc.m_SemanticContents)
            if (xrtcs::IsLeafValued(S))
                Candidates.emplace_back(xrtcs::id{ Key }, S.m_Name);

        int WindowIndex = 0;
        for (auto& Entry : pManifest->m_Entries)
        {
            auto* pSlot = Doc.FindSlot(Entry.m_SlotId);
            const std::string Title = std::format("Manifest: {}", pSlot ? pSlot->m_Name : std::format("slot #{}", Entry.m_SlotId.m_Value));

            xproperty::inspector Inspector(Title.c_str());
            Inspector.AppendEntity();
            Inspector.AppendEntityComponent(*xproperty::getObject(Entry), &Entry);

            ImGui::SetNextWindowPos(ImVec2(360, 425.0f + 120.0f * WindowIndex), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(340, 110), ImGuiCond_FirstUseEver);
            ++WindowIndex;

            xproperty::settings::context Context;
            Inspector.Show(Context, [&]
            {
                if (Entry.m_Disposition != xrtcs::disposition::selected) return;

                int CandidateIndex = -1;
                for (int i = 0; i < (int)Candidates.size(); ++i)
                    if (Candidates[i].first.m_Value == Entry.m_SelectedSemantic.m_Value) { CandidateIndex = i; break; }

                const std::string PreviewName = CandidateIndex >= 0 ? Candidates[CandidateIndex].second : "(none selected)";
                if (ImGui::BeginCombo("Selected Implementation", PreviewName.c_str()))
                {
                    for (int i = 0; i < (int)Candidates.size(); ++i)
                    {
                        const bool bIsSel = (i == CandidateIndex);
                        if (ImGui::Selectable(Candidates[i].second.c_str(), bIsSel))
                        {
                            Entry.m_SelectedSemantic  = Candidates[i].first;
                            Entry.m_ValidatedContract = (pSlot && pSlot->m_Reference.m_Kind == xrtcs::reference_kind::Contract) ? pSlot->m_Reference.m_Target : xrtcs::null_id;
                        }
                        if (bIsSel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            });
        }
    }

    static std::string SemanticName(xrtcs::document& Doc, xrtcs::id SemanticId)
    {
        if (auto* pS = Doc.FindSemantic(SemanticId)) return pS->m_Name;
        return std::format("<missing semantic #{}>", SemanticId.m_Value);
    }

    //------------------------------------------------------------------------------------------------
    // Resolved Graph + Configuration panel - read-only view of one resolve() call's output, reached
    // through its resolution_result occurrence: manifest -> resolved graph (members/bindings) ->
    // root semantic -> sealed configuration (closure + required environment). This is deliberately
    // read-only (unlike the Template canvas and Manifest panel) - a resolved graph/configuration is
    // the OUTPUT of resolve(), never hand-edited; to change it, edit the manifest and resolve again.
    //------------------------------------------------------------------------------------------------
    static void DrawResolvedGraphPanel(xrtcs::document& Doc, xrtcs::id ResultId)
    {
        ImGui::SetNextWindowPos(ImVec2(1065, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(400, 420), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("RTCS Resolved Graph"))
        {
            ImGui::End();
            return;
        }

        auto* pResult = Doc.FindResult(ResultId);
        if (!pResult)
        {
            ImGui::TextDisabled("Not yet resolved - press \"Resolve Museum Heist Manifest\" in RTCS Controls.");
            ImGui::End();
            return;
        }

        auto* pGraph = Doc.FindResolvedGraph(pResult->m_ResolvedGraphId);
        auto* pRoot  = Doc.FindSemantic(pResult->m_RootSemanticId);
        auto* pCfg   = Doc.FindConfiguration(pResult->m_ConfigurationId);

        ImGui::Text("Result occurrence #%llu (manifest #%llu)", (unsigned long long)pResult->m_OccurrenceId.m_Value, (unsigned long long)pResult->m_ManifestOccurrenceId.m_Value);
        ImGui::Separator();

        if (pGraph)
        {
            ImGui::TextUnformatted("Resolved Graph");
            if (auto* pBoundary = Doc.FindContract(pGraph->m_Boundary))
                ImGui::Text("  Boundary contract: %s", pBoundary->m_Name.c_str());

            ImGui::Text("  Members (%zu):", pGraph->m_Members.size());
            for (auto MemberId : pGraph->m_Members)
                ImGui::BulletText("%s", SemanticName(Doc, MemberId).c_str());

            ImGui::Text("  Bindings (%zu):", pGraph->m_Bindings.size());
            for (auto& B : pGraph->m_Bindings)
                ImGui::BulletText("%s.%s -> %s.%s", SemanticName(Doc, B.m_SourceSemantic).c_str(), B.m_SourcePort.c_str(), SemanticName(Doc, B.m_TargetSemantic).c_str(), B.m_TargetPort.c_str());
        }
        else ImGui::TextDisabled("Resolved graph missing.");

        ImGui::Separator();
        if (pRoot)
        {
            ImGui::Text("Root Semantic: %s", pRoot->m_Name.c_str());
            if (auto* pIface = Doc.FindContract(pRoot->m_Interface))
                ImGui::Text("  Interface: %s", pIface->m_Name.c_str());
        }
        else ImGui::TextDisabled("Root semantic missing.");

        ImGui::Separator();
        if (pCfg)
        {
            ImGui::TextUnformatted("Sealed Configuration");
            ImGui::Text("  Resolved closure (%zu, excludes root):", pCfg->m_ResolvedSemanticClosure.size());
            for (auto MemberId : pCfg->m_ResolvedSemanticClosure)
                ImGui::BulletText("%s", SemanticName(Doc, MemberId).c_str());

            ImGui::Text("  Required environment (%zu):", pCfg->m_RequiredEnvironment.size());
            for (auto EnvId : pCfg->m_RequiredEnvironment)
            {
                auto It = Doc.m_EnvironmentNames.find(EnvId.m_Value);
                ImGui::BulletText("%s", It == Doc.m_EnvironmentNames.end() ? std::format("#{}", EnvId.m_Value).c_str() : It->second.c_str());
            }
        }
        else ImGui::TextDisabled("Configuration missing.");

        ImGui::End();
    }

    static std::string FormatFrontier(const xrtcs::causal_frontier& F)
    {
        if (F.m_Counters.empty()) return "{}";
        std::string Out = "{ ";
        for (auto& [Replica, Counter] : F.m_Counters)
            Out += std::format("R{}:{} ", Replica, Counter);
        Out += "}";
        return Out;
    }

    //------------------------------------------------------------------------------------------------
    // Provenance panel - every occurrence in the document (request/manifest/result - the three
    // OccurrenceEnvelope<T> concrete types, see xrtcs_resolution.h) alongside its causal frontier
    // vector clock, plus an on-demand happens_before/concurrent check between any two selected
    // occurrences (causal-frontier.ai's own relations, xrtcs_identity.h). RTCS replaces wall-clock
    // provenance with this: "did A cause B" is answered from the frontiers alone, never from when
    // either was clicked into existence.
    //------------------------------------------------------------------------------------------------
    static void DrawProvenancePanel(xrtcs::document& Doc)
    {
        ImGui::SetNextWindowPos(ImVec2(360, 425), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(700, 300), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("RTCS Provenance"))
        {
            ImGui::End();
            return;
        }

        struct occurrence_row { const char* m_Kind; xrtcs::id m_Id; const xrtcs::causal_frontier* m_pFrontier; };
        std::vector<occurrence_row> Rows;
        for (auto& [Key, R] : Doc.m_Requests)  Rows.push_back({ "Request",  R.m_OccurrenceId, &R.m_CausalFrontier });
        for (auto& [Key, M] : Doc.m_Manifests) Rows.push_back({ "Manifest", M.m_OccurrenceId, &M.m_CausalFrontier });
        for (auto& [Key, S] : Doc.m_Results)   Rows.push_back({ "Result",   S.m_OccurrenceId, &S.m_CausalFrontier });

        ImGui::TextUnformatted("Occurrence chain");
        if (ImGui::BeginTable("ProvenanceTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Kind");
            ImGui::TableSetupColumn("Occurrence Id");
            ImGui::TableSetupColumn("Causal Frontier");
            ImGui::TableHeadersRow();
            for (auto& Row : Rows)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(Row.m_Kind);
                ImGui::TableSetColumnIndex(1); ImGui::Text("#%llu", (unsigned long long)Row.m_Id.m_Value);
                ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(FormatFrontier(*Row.m_pFrontier).c_str());
            }
            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Compare two occurrences");
        static int IndexA = 0, IndexB = 0;
        if (!Rows.empty())
        {
            IndexA = std::min(IndexA, (int)Rows.size() - 1);
            IndexB = std::min(IndexB, (int)Rows.size() - 1);

            auto RowLabel = [&](int i) { return std::format("{} #{}", Rows[i].m_Kind, Rows[i].m_Id.m_Value); };

            if (ImGui::BeginCombo("A", RowLabel(IndexA).c_str()))
            {
                for (int i = 0; i < (int)Rows.size(); ++i)
                    if (ImGui::Selectable(RowLabel(i).c_str(), i == IndexA)) IndexA = i;
                ImGui::EndCombo();
            }
            if (ImGui::BeginCombo("B", RowLabel(IndexB).c_str()))
            {
                for (int i = 0; i < (int)Rows.size(); ++i)
                    if (ImGui::Selectable(RowLabel(i).c_str(), i == IndexB)) IndexB = i;
                ImGui::EndCombo();
            }

            const auto& FA = *Rows[IndexA].m_pFrontier;
            const auto& FB = *Rows[IndexB].m_pFrontier;
            if (xrtcs::happens_before(FA, FB))      ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "A happens-before B");
            else if (xrtcs::happens_before(FB, FA)) ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "B happens-before A");
            else if (xrtcs::concurrent(FA, FB))     ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "A and B are concurrent (neither caused the other)");
            else                                    ImGui::TextDisabled("A and B are identical frontiers");
        }
        else ImGui::TextDisabled("No occurrences recorded yet.");

        ImGui::End();
    }

}

//------------------------------------------------------------------------------------------------

int E26_Example()
{
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = false, .m_bEnableRenderDoc = false, .m_pLogErrorFunc = e26::Debugger, .m_pLogWarning = e26::Debugger }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err)
        return xgpu::getErrorInt(Err);

    xgpu::window MainWindow;
    if (auto Err = Device.Create(MainWindow, {}); Err)
        return xgpu::getErrorInt(Err);

    xgpu::tools::imgui::CreateInstance(MainWindow);

    // "Build a sandwich" loads first - a deliberately trivial worked example (no domain knowledge
    // required) that sharpens the editor's own UX before showing the spec's own richer museum-heist
    // worked example. Both are one button away from each other at any time.
    xrtcs::document Doc;
    xrtcs::demo_ids DemoIds = xrtcs::CreateSandwichDemo(Doc);
    std::string ResolveStatus = "Not yet resolved.";
    xrtcs::id   CurrentResultId = xrtcs::null_id;
    xrtcs::id   SelectedSlotId = xrtcs::null_id;

    ed::Config Config;
    ed::EditorContext* pEditor = ed::CreateEditor(&Config);

    while (Instance.ProcessInputEvents())
    {
        if (xgpu::tools::imgui::BeginRendering(true))
            continue;

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(350, 110), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("RTCS Controls"))
        {
            if (ImGui::Button("Load Sandwich Demo"))
            {
                Doc = {};
                DemoIds       = xrtcs::CreateSandwichDemo(Doc);
                CurrentResultId = xrtcs::null_id;
                ResolveStatus  = "Not yet resolved.";
            }
            ImGui::SameLine();
            if (ImGui::Button("Load Museum Heist Demo"))
            {
                Doc = {};
                DemoIds       = xrtcs::CreateMuseumHeistDemo(Doc);
                CurrentResultId = xrtcs::null_id;
                ResolveStatus  = "Not yet resolved.";
            }

            if (ImGui::Button("Resolve Manifest"))
            {
                auto Result = xrtcs::resolve(Doc, DemoIds.m_TemplateId, DemoIds.m_ManifestId);
                ResolveStatus = Result.m_bSuccess ? "Resolved successfully." : ("Resolve failed: " + Result.m_Error);
                if (Result.m_bSuccess) CurrentResultId = Result.m_ResultOccurrenceId;
            }
            ImGui::TextWrapped("%s", ResolveStatus.c_str());
        }
        ImGui::End();

        e26::DrawTemplateCanvas(Doc, DemoIds.m_TemplateId, DemoIds.m_ManifestId, pEditor, SelectedSlotId);
        e26::DrawSlotPropertiesPanel(Doc, SelectedSlotId);
        e26::DrawManifestPanel(Doc, DemoIds.m_ManifestId);
        e26::DrawResolvedGraphPanel(Doc, CurrentResultId);
        e26::DrawProvenancePanel(Doc);
        xgpu::tools::imgui::Render();
        MainWindow.PageFlip();
    }

    return 0;
}
