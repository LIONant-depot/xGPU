#ifndef E29_PANEL_ENTITY_PROPERTIES_H
#define E29_PANEL_ENTITY_PROPERTIES_H
#pragma once

// Extracted from E29_LevelSceneEditorKit.h (mechanical move, phase 1 of the kit split - see that
// file's own top comment). Meant to be included via the umbrella (E29_LevelSceneEditorKit.h) only,
// after entity_inspector_bridge and everything it depends on are already defined - not designed to
// be included standalone.
//
// Add/Remove Component commands (E29_Commands_ComponentEdit.h, which pulls in
// E29_Commands_PropertyEdit.h/E29_CommandContext.h/xundo_system.h itself) included directly here -
// same self-sufficiency reasoning as kit/E29_Panel_LevelTree.h's own top comment for why.
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_ComponentEdit.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_ApplyOverrides.h"
#include "source/Examples/E29_LevelSceneEditor/GameProject/E29_GameRegistration.h"

namespace e29
{
    //---------------------------------------------------------------------------
    // Entity Properties panel - the selected entity's components, plus Add/Remove Component and
    // (when applicable) prefab-override actions. Owns its own ImGui::Begin/End. Bridge carries the
    // inspector-to-override-tracking state (see entity_inspector_bridge's own comment) - construct
    // one alongside EntityInspector and call Bridge.RegisterCallbacks(...) once at setup before
    // calling this every frame.
    //---------------------------------------------------------------------------
    void RenderEntityPropertiesPanel(xecs::game_mgr::instance& GameMgr, editor_state& State, xproperty::inspector& EntityInspector, entity_inspector_bridge& Bridge, xundo::system& Undo, bool bReadOnly = false) noexcept
    {
        ImGui::SetNextWindowPos(ImVec2(18, 18), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(480, 500), ImGuiCond_FirstUseEver);
        const bool bWindowVisible = ImGui::Begin(e29::editor_tabs::kInspectorWindow);
        if (bWindowVisible && bReadOnly) ImGui::BeginDisabled();
        e29::diagnostics::Log("window begin: %s visible=%d", e29::editor_tabs::kInspectorWindow, bWindowVisible ? 1 : 0);
        if (bWindowVisible)
        {
            if (State.m_SelectedEntity.isValid() == false || State.m_SelectedEntityScene.empty())
            {
                ImGui::TextDisabled("Select an entity in the Level Editor panel.");
            }
            else if (auto* pScene = GameMgr.m_SceneMgr.Find(State.m_SelectedEntityScene))
            {
                // Pointers (not references) so RefreshEntityView() below can rebind them after
                // AddOrRemoveComponents migrates State.m_SelectedEntity to a new handle - without this,
                // adding/removing a component and rebuilding the inspector in the SAME frame would
                // still walk the OLD archetype's DataSpan (captured before the migration), so the
                // just-added component silently wouldn't appear until some later, unrelated dirty flag
                // flip (e.g. reselecting the entity) rebuilt it with fresh data.
                auto* pDetails = &GameMgr.m_ComponentMgr.getEntityDetails(State.m_SelectedEntity);
                if (pDetails->m_pPool == nullptr)
                {
                    // Defensive hardening, not a fix for a known-live bug: getEntityDetails succeeding
                    // (generation matched) with a still-null pool shouldn't happen given the map-
                    // corruption root cause is fixed and the stricter selection-liveness check
                    // (DeleteSubtreeByPermanentId, E29_Commands_EntityLifecycle.h) - kept as a second
                    // line of defense per direct review feedback, same failure class as the crash phase
                    // 4 hit (a stale/dangling handle a few lines below would otherwise dereference a
                    // nullptr). Does NOT guard against that assert itself - getEntityDetails asserts
                    // internally on a generation mismatch before ever returning, so the real fix is
                    // never reaching this call with a stale handle in the first place.
                    ImGui::TextDisabled("Select an entity in the Level Editor panel.");
                    ImGui::End();
                    return;
                }
                auto* pArchetype = pDetails->m_pPool->m_pArchetype;
                auto  DataSpan   = pArchetype->getDataComponentInfos();

                auto RefreshEntityView = [&]() noexcept
                {
                    pDetails   = &GameMgr.m_ComponentMgr.getEntityDetails(State.m_SelectedEntity);
                    pArchetype = pDetails->m_pPool->m_pArchetype;
                    DataSpan   = pArchetype->getDataComponentInfos();
                };

                // Popup selector (grouped + searchable) — replaces the flat BeginCombo list.
                // OpenPopup/BeginPopup share this panel's ID stack (see Error popup comments in kit).
                {
                    constexpr const char* kAddComponentPopupId = "AddComponentPopup";
                    if (ImGui::Button("Add Component"))
                        ImGui::OpenPopup(kAddComponentPopupId);

                    ImGui::SetNextWindowSize(ImVec2(320.0f, 360.0f), ImGuiCond_Appearing);
                    if (ImGui::BeginPopup(kAddComponentPopupId))
                    {
                        if (e29::RenderComponentSelectorPopupContents(State, Undo, pDetails->m_pPool))
                            RefreshEntityView();
                        ImGui::EndPopup();
                    }
                }

                // Prefabs are created by dragging an entity from the Level Editor tree onto a folder
                // in the asset browser (see e29::entity_to_prefab_drop) - Unity-style, no button.

// Unity-style Apply / Revert for instance overrides. Shown when the selection is
                // under a prefab instance that has property overrides and/or HierarchyDiffs.
                if (auto Ctx = e29::FindContainingPrefabInstance(GameMgr, State.m_SelectedEntity); Ctx.m_pPI
                    && (!Ctx.m_pPI->m_lComponents.empty() || !Ctx.m_pPI->m_HierarchyDiffs.empty()))
                {
                    if (auto RootIt = pScene->m_RuntimeToLocal.find(Ctx.m_RootEntity.m_Value); RootIt != pScene->m_RuntimeToLocal.end())
                    {
                        const auto SceneHex = e29::commands::FormatSceneGuid(State.m_SelectedEntityScene);
                        const auto RootHex  = e29::commands::FormatEntityId(RootIt->second);

                        if (!Ctx.m_pPI->m_lComponents.empty() || !Ctx.m_pPI->m_HierarchyDiffs.empty())
                        {
                            if (ImGui::Button("Apply"))
                            {
                                e29::commands::Run(e29::LevelDocUndo(Undo), std::format("ApplyOverrides -Scene {} -Id {}", SceneHex, RootHex));
                            }
                            // Tooltip (only show when hovering) — same format as Play transport buttons
                            if (ImGui::IsItemHovered())
                            {
                                ImGui::BeginTooltip();
                                ImGui::Text("Apply");
                                ImGui::TextDisabled("Push all overrides on this prefab instance into the Prefab asset");
                                ImGui::EndTooltip();
                            }
                        }
                        if (!Ctx.m_pPI->m_HierarchyDiffs.empty())
                        {
                            ImGui::SameLine();
                            if (ImGui::Button("Revert Hierarchy"))
                            {
                                e29::commands::Run(e29::LevelDocUndo(Undo), std::format("RevertHierarchyOverrides -Scene {} -Id {}", SceneHex, RootHex));
                            }
                            if (ImGui::IsItemHovered())
                            {
                                ImGui::BeginTooltip();
                                ImGui::Text("Revert Hierarchy");
                                ImGui::TextDisabled("Restore removed children / drop added children; leave property overrides");
                                ImGui::EndTooltip();
                            }
                        }
                        if (!Ctx.m_pPI->m_lComponents.empty()
                            || !Ctx.m_pPI->m_ComponentDiffs.empty()
                            || !Ctx.m_pPI->m_HierarchyDiffs.empty())
                        {
                            ImGui::SameLine();
                            if (ImGui::Button("Revert All"))
                            {
                                e29::commands::Run(e29::LevelDocUndo(Undo), std::format("RevertAllOverrides -Scene {} -Id {}", SceneHex, RootHex));
                            }
                            if (ImGui::IsItemHovered())
                            {
                                ImGui::BeginTooltip();
                                ImGui::Text("Revert All");
                                ImGui::TextDisabled("Re-sync from Prefab (keeps root Transform); clears all overrides");
                                ImGui::EndTooltip();
                            }
                        }
                    }
                }

                ImGui::Separator();

                // Category filter bar - direct user design: lives OUTSIDE the inspector (not grouped
                // headers inside the component list itself), defaults to "All" (no filter), and only
                // shows categories actually present on THIS entity's own attached components - "if
                // the entity does not have the category then we do not need to add that particular
                // category at the top". Categories come from e29::g_ComponentDisplayInfo
                // (E29_GamePluginLoad.h), populated from whatever Script-Module components the
                // currently-loaded Game.dll generation self-registered with a category (built-in
                // engine components like Transform/Name never appear here, since they never go
                // through E29_REGISTER_COMPONENT - see that macro's own comment).
                {
                    std::vector<std::string> PresentCategories;
                    for (auto pInfo : DataSpan)
                    {
                        if (auto It = e29::g_ComponentDisplayInfo.find(pInfo->m_pName); It != e29::g_ComponentDisplayInfo.end() && !It->second.m_Category.empty())
                            if (std::find(PresentCategories.begin(), PresentCategories.end(), It->second.m_Category) == PresentCategories.end())
                                PresentCategories.push_back(It->second.m_Category);
                    }

                    if (!PresentCategories.empty())
                    {
                        std::sort(PresentCategories.begin(), PresentCategories.end());

                        auto FilterButton = [&](const std::string& Label, const std::string& Value) noexcept
                        {
                            const bool bSelected = (State.m_ComponentCategoryFilter == Value);
                            if (bSelected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                            if (ImGui::SmallButton(Label.c_str()) && !bSelected)
                            {
                                State.m_ComponentCategoryFilter = Value;
                                State.m_bEntityInspectorDirty   = true;
                            }
                            if (bSelected) ImGui::PopStyleColor();
                            ImGui::SameLine();
                        };

                        FilterButton("All", "");
                        for (auto& Category : PresentCategories)
                            FilterButton(Category, Category);
                        ImGui::NewLine();
                        ImGui::Separator();
                    }
                }

                if (State.m_bEntityInspectorDirty)
                {
                    std::printf("[EntityDrag] Entity Properties inspector REBUILDING (m_bEntityInspectorDirty) for SelectedEntityId=%u\n", State.m_SelectedEntityId);
                    std::fflush(stdout);
                    EntityInspector.clear();
                    Bridge.m_ComponentMap.clear();
                    EntityInspector.AppendEntity();

                    // Filtered (per the category bar above, "" = All) + sorted by priority -
                    // uncategorized components (every built-in engine component) sort first, by
                    // construction, so Transform/Name stay pinned at the top exactly as they are
                    // today without needing to touch their own definitions.
                    std::vector<const xecs::component::type::info*> SortedComponents(DataSpan.begin(), DataSpan.end());
                    std::erase_if(SortedComponents, [&](const xecs::component::type::info* pInfo) noexcept
                    {
                        if (State.m_ComponentCategoryFilter.empty()) return false;
                        auto It = e29::g_ComponentDisplayInfo.find(pInfo->m_pName);
                        return It == e29::g_ComponentDisplayInfo.end() || It->second.m_Category != State.m_ComponentCategoryFilter;
                    });
                    std::stable_sort(SortedComponents.begin(), SortedComponents.end(), [](const xecs::component::type::info* A, const xecs::component::type::info* B) noexcept
                    {
                        auto ItA = e29::g_ComponentDisplayInfo.find(A->m_pName);
                        auto ItB = e29::g_ComponentDisplayInfo.find(B->m_pName);
                        const bool bHasA = ItA != e29::g_ComponentDisplayInfo.end();
                        const bool bHasB = ItB != e29::g_ComponentDisplayInfo.end();
                        if (!bHasA && !bHasB) return false;
                        if (!bHasA) return true;
                        if (!bHasB) return false;
                        return ItA->second.m_Priority < ItB->second.m_Priority;
                    });

                    for (auto pInfo : SortedComponents)
                    {
                        if (e29::IsInternalComponent(pInfo)) continue;
                        if (pInfo->m_pPropertyTable == nullptr) continue;

                        if (pDetails->m_pPool->findIndexComponentFromInfo(*pInfo) < 0) continue; // not actually present

                        // pBase is a FAKE pointer (nullptr, never dereferenced) - pInfo is the stable
                        // identity carried as pUserData instead. The REAL pointer into pool memory is
                        // resolved fresh every frame by Bridge's m_OnGetComponentPointer (registered
                        // below in RegisterCallbacks), never cached here across frames - see that
                        // callback's own comment for why a raw pointer captured only at rebuild time
                        // (the previous design) goes stale the moment anything invalidates it without
                        // routing back through this dirty-flag rebuild first (an archetype
                        // migration elsewhere, or - the case that actually surfaced this - Phase 8's
                        // hot reload destroying and recreating the whole pool).
                        EntityInspector.AppendEntityComponent(*pInfo->m_pPropertyTable, nullptr, const_cast<xecs::component::type::info*>(pInfo));
                    }
                    State.m_bEntityInspectorDirty = false;
                }

                // Component headers use ImGuiCol_Header, which is ALSO the tree/list selection color
                // (E29_Theme.h's Unity-inspired blue) - fine for a hierarchy row, but real Unity's own
                // Inspector component headers are a neutral gray ("Inspector Titlebar", #3E3E3E), not
                // blue; blue is reserved for actual selection. Overridden locally, only around this
                // Show() call, so tree/list selection elsewhere stays the real selection blue.
                ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0x3E / 255.0f, 0x3E / 255.0f, 0x3E / 255.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0x4A / 255.0f, 0x4A / 255.0f, 0x4A / 255.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0x4A / 255.0f, 0x4A / 255.0f, 0x4A / 255.0f, 1.0f));
                // Show() internally uses the legacy ImGui::Columns(2) (xPropertyImGuiInspector.cpp),
                // which draws a live, draggable ImGuiCol_Separator line down the whole label/value
                // split by default - a real border line, not a color mismatch, so the earlier Header
                // color fix alone couldn't remove it (direct user follow-up: "the gap still there").
                // Matched to WindowBg instead of touching the shared Columns() call itself (which
                // would also change every other example's own column-resize border) - resting state
                // blends into the flat row, but SeparatorHovered/Active are left as the theme's own
                // values so dragging the label/value split is still discoverable on hover.
                ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0x38 / 255.0f, 0x38 / 255.0f, 0x38 / 255.0f, 1.0f));
                // REVERTED: pre-opening/closing our own Columns(2) here to seed a width for Show()'s
                // own internal Columns(2) call triggered a real ImGui "2 visible items with conflicting
                // ID" debug error (confirmed live) - not a safe pattern, back this out rather than
                // ship a broken column-ID state. The actual reported issue (a dark divider line
                // splitting the header's own background) is separate from column width entirely -
                // being investigated on its own, not re-attempting this approach.
                // ShowEmbedded (not Show(Context, Callback)) - that overload always opens its OWN
                // independent ImGui::Begin/End window using EntityInspector's own name ("Inspector"),
                // which, called from INSIDE this panel's already-open kInspectorWindow, created a
                // second, genuinely separate floating "Inspector" window instead of rendering the
                // properties into this one - the real bug behind "two windows both called Inspector".
                // ShowEmbedded draws directly into the current window, no Begin/End of its own.
                xproperty::settings::context Context;
                EntityInspector.ShowEmbedded(Context);
                ImGui::PopStyleColor(4);

                // A component header's "[X]" (entity_inspector_bridge::m_OnComponentHeaderRender) only
                // ever records the request while Show() is mid-iteration over this same component list
                // - now that it's returned, it's safe to actually mutate the archetype, same call the
                // "Remove Component" combo below makes for the same action.
                if (Bridge.m_pPendingRemoveComponent)
                {
                    // Routed through the command/undo system (documentation/E29_LevelSceneEditor/command_undo_system_plan.md
                    // memory, phase 3 - commands/E29_Commands_ComponentEdit.h) - remove_component_cmd
                    // snapshots the component's current property values before removing it, so Undo
                    // can restore it exactly, not just re-add it with default values.
                    e29::commands::Run(e29::LevelDocUndo(Undo), std::format("RemoveComponent -Scene {} -Id {} -Component {:016X}"
                        , e29::commands::FormatSceneGuid(State.m_SelectedEntityScene)
                        , e29::commands::FormatEntityId(State.m_SelectedEntityId)
                        , Bridge.m_pPendingRemoveComponent->m_Guid.m_Value
                        ));
                    Bridge.m_pPendingRemoveComponent = nullptr;
                    RefreshEntityView();
                }
            }
        }
                if (bWindowVisible && bReadOnly) ImGui::EndDisabled();
        ImGui::End();
        e29::diagnostics::Log("window end: %s", e29::editor_tabs::kInspectorWindow);
    }
} // namespace e29

#endif // E29_PANEL_ENTITY_PROPERTIES_H
