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

namespace e29
{
    //---------------------------------------------------------------------------
    // Entity Properties panel - the selected entity's components, plus Add/Remove Component and
    // (when applicable) prefab-override actions. Owns its own ImGui::Begin/End. Bridge carries the
    // inspector-to-override-tracking state (see entity_inspector_bridge's own comment) - construct
    // one alongside EntityInspector and call Bridge.RegisterCallbacks(...) once at setup before
    // calling this every frame.
    //---------------------------------------------------------------------------
    void RenderEntityPropertiesPanel(xecs::game_mgr::instance& GameMgr, editor_state& State, xproperty::inspector& EntityInspector, entity_inspector_bridge& Bridge, xundo::system& Undo) noexcept
    {
        ImGui::SetNextWindowPos(ImVec2(18, 18), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(480, 500), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Entity Properties"))
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

                if (ImGui::BeginCombo("###AddComponent", "Add Component"))
                {
                    for (auto& Pair : xecs::component::mgr::s_Registry.m_ComponentInfoMap)
                    {
                        auto* pInfo = Pair.second;
                        if (pInfo->m_TypeID != xecs::component::type::id::DATA) continue;
                        if (e29::IsInternalComponent(pInfo)) continue;
                        // findIndexComponentFromInfo, not getComponentBits().getBit() - see
                        // [[xecs_getbit_vs_findindexcomponentfrominfo]] (a runtime-assigned component
                        // bit checked this way can read as absent/invalid even when the component is
                        // genuinely present).
                        if (pDetails->m_pPool->findIndexComponentFromInfo(*pInfo) >= 0) continue; // already present

                        if (ImGui::Selectable(pInfo->m_pName))
                        {
                            // Routed through the command/undo system ([[e29_command_undo_system_plan]]
                            // memory, phase 3 - commands/E29_Commands_ComponentEdit.h) instead of
                            // calling GameMgr.AddOrRemoveComponents directly - add_component_cmd::Redo
                            // does the exact same migration+remap this used to do inline, and its own
                            // Undo removes the component again.
                            e29::commands::Run(Undo, std::format("AddComponent -Scene {} -Id {} -Component {:016X}"
                                , e29::commands::FormatSceneGuid(State.m_SelectedEntityScene)
                                , e29::commands::FormatEntityId(State.m_SelectedEntityId)
                                , pInfo->m_Guid.m_Value
                                ));
                            RefreshEntityView();
                        }
                    }
                    ImGui::EndCombo();
                }

                // Prefabs are created by dragging an entity from the Level Editor tree onto a folder
                // in the asset browser (see e29::entity_to_prefab_drop) - Unity-style, no button.

                // Unity's "Apply to Prefab" - only shown when the selected entity is structurally
                // part of SOME prefab instance (root or plain member), matching how the blue tint/
                // "(Prefab: X)" label already decide the same thing. Applies EVERY override this one
                // instance currently has recorded, across however many members its own m_MemberPath
                // entries address, in one action - the closest Unity equivalent to its default
                // top-level "Apply All".
                if (auto Ctx = e29::FindContainingPrefabInstance(GameMgr, State.m_SelectedEntity); Ctx.m_pPI && !Ctx.m_pPI->m_lComponents.empty())
                {
                    if (ImGui::Button("Apply Overrides to Prefab"))
                    {
                        if (auto Err = xecs::persist::details::ApplyInstanceOverridesToPrefab(GameMgr, Ctx.m_RootEntity); Err)
                        {
                            e29::Debugger(std::format("Failed to apply overrides to prefab: {}", Err.getMessage()));
                        }
                        else if (auto RootIt = pScene->m_RuntimeToLocal.find(Ctx.m_RootEntity.m_Value); RootIt != pScene->m_RuntimeToLocal.end())
                        {
                            GameMgr.m_SceneMgr.MarkEntityDirty(State.m_SelectedEntityScene, RootIt->second);
                            State.m_bEntityInspectorDirty = true; // the just-applied property no longer shows as overridden
                        }
                    }
                }

                ImGui::Separator();

                if (State.m_bEntityInspectorDirty)
                {
                    std::printf("[EntityDrag] Entity Properties inspector REBUILDING (m_bEntityInspectorDirty) for SelectedEntityId=%u\n", State.m_SelectedEntityId);
                    std::fflush(stdout);
                    EntityInspector.clear();
                    Bridge.m_ComponentMap.clear();
                    EntityInspector.AppendEntity();
                    for (auto pInfo : DataSpan)
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
                xproperty::settings::context Context;
                EntityInspector.Show(Context, []{});
                ImGui::PopStyleColor(4);

                // A component header's "[X]" (entity_inspector_bridge::m_OnComponentHeaderRender) only
                // ever records the request while Show() is mid-iteration over this same component list
                // - now that it's returned, it's safe to actually mutate the archetype, same call the
                // "Remove Component" combo below makes for the same action.
                if (Bridge.m_pPendingRemoveComponent)
                {
                    // Routed through the command/undo system ([[e29_command_undo_system_plan]]
                    // memory, phase 3 - commands/E29_Commands_ComponentEdit.h) - remove_component_cmd
                    // snapshots the component's current property values before removing it, so Undo
                    // can restore it exactly, not just re-add it with default values.
                    e29::commands::Run(Undo, std::format("RemoveComponent -Scene {} -Id {} -Component {:016X}"
                        , e29::commands::FormatSceneGuid(State.m_SelectedEntityScene)
                        , e29::commands::FormatEntityId(State.m_SelectedEntityId)
                        , Bridge.m_pPendingRemoveComponent->m_Guid.m_Value
                        ));
                    Bridge.m_pPendingRemoveComponent = nullptr;
                    RefreshEntityView();
                }
            }
        }
        ImGui::End();
    }

} // namespace e29

#endif // E29_PANEL_ENTITY_PROPERTIES_H
