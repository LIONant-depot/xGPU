#ifndef E29_PANEL_LEVEL_TREE_H
#define E29_PANEL_LEVEL_TREE_H
#pragma once

// Extracted from E29_LevelSceneEditorKit.h (mechanical move, phase 1 of the kit split - see that
// file's own top comment). Meant to be included via the umbrella (E29_LevelSceneEditorKit.h) only,
// after every symbol this panel calls (editor_state, the folder/scene/prefab helpers, Debugger,
// entity_drag_payload_t, ...) is already defined - not designed to be included standalone.
//
// Selection commands (E29_Commands_Selection.h, which pulls in E29_CommandContext.h/xundo_system.h
// itself) included directly here - not relying on E29_LevelScene_Editor.cpp's own later include of
// them - this panel is reached through the kit umbrella BEFORE that .cpp's own includes run, and a
// file that names a type/function should include what declares it rather than depend on a distant
// caller's own include order. #pragma once makes the .cpp's own later include a safe no-op.
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_Selection.h"

// Create/Delete Entity commands (E29_Commands_EntityLifecycle.h) - needs DeleteEntitySubtree
// (kit/E29_PrefabAuthoring.h), already included by the umbrella well before this panel is reached
// (see that file's own top comment for why this panel can safely assume it).
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_EntityLifecycle.h"

// InstantiatePrefab/MoveToFolder commands (E29_Commands_SceneOrganization.h) - same "this panel is
// reached before the umbrella's own later include runs" reasoning as the two includes just above.
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_SceneOrganization.h"

// AddScene/RemoveScene (E29_Commands_Level.h) - Level membership edits, same "include what you name"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_SceneDependency.h"
// self-sufficiency as the command includes above. OpenLevel/List* live here too; this panel only
// needs the two undoable membership commands.
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_Level.h"

namespace e29
{
    //---------------------------------------------------------------------------
    // Level Editor panel - one tree: Level -> Scenes -> Folders -> Entities. Clicking a Scene's label
    // opens it (loads its entities) WITHOUT closing any other already-open scene - any number of
    // scenes can be open/expanded at once, each independently. Clicking an Entity selects it for the
    // Properties panel. A scene's dependency edges render as a fixed "Dependencies" folder right
    // under that scene's own row, not a separate section. Owns its own ImGui::Begin/End - callable
    // directly from a main loop with no surrounding window boilerplate needed.
    //---------------------------------------------------------------------------
    void RenderLevelTreePanel(xecs::game_mgr::instance& GameMgr, editor_state& State, xundo::system& Undo) noexcept
    {
        ImGui::SetNextWindowPos(ImVec2(915, 18), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(360, 680), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Level Editor"))
        {
            if (State.m_CurrentLevel.empty())
            {
                ImGui::TextDisabled("Create or open a Level from the asset browser.");
            }
            else if (auto* pLevel = GameMgr.m_LevelMgr.Find(State.m_CurrentLevel))
            {
                std::string LevelLabel;
                e29::RemapGUIDToString(LevelLabel, xresource::full_guid{ State.m_CurrentLevel.m_Instance, State.m_CurrentLevel.m_Type });

                // Search box, visually matching the asset browser's own (RenderTreeSearchBar's own
                // comment). Adding entities/folders is right-click-in-place on a Scene/Folder row now
                // (BeginPopupContextItem, below).
                e29::RenderTreeSearchBar(State.m_TreeSearchString, ImGui::GetContentRegionAvail().x);

                // The whole Level -> Scene -> Folder -> Entity hierarchy lives in one real
                // ImGui::BeginTable now (ImGui's own documented "tree inside a table" shape -
                // ImGuiTreeNodeFlags_SpanFullWidth on every tree row so hover/selection spans the Name
                // column). Column 1 ("Actions") is intentionally minimal today (just each row's own
                // remove/delete button) - the second column exists so future per-row content (type
                // badges, visibility toggles, etc.) has somewhere to go without another rewrite. Sized
                // to fill the rest of the window's height (ImGuiTableFlags_ScrollY so it scrolls
                // internally instead of pushing the window's own edge) rather than only as tall as its
                // content.
                // NoSavedSettings: ImGui persists per-table column widths in imgui_e29.ini across
                // sessions by table id+column-count hash - a width picked BEFORE E29_Theme.h switched
                // the default font from Consolas to the wider proportional Segoe UI would otherwise keep
                // overriding the (now correct) 80px default below forever, clipping "Remove" to "Remov"
                // on every future launch. Neither column here is something a user meaningfully needs to
                // hand-resize and remember between sessions, so always-reset-to-default is the right
                // call, not a narrower one-off ini edit.
                if (ImGui::BeginTable("LevelTree", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoSavedSettings, ImVec2(0.0f, ImGui::GetContentRegionAvail().y)))
                {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80.0f);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    const std::string LevelLabelWithIcon = std::format("{} {}", e29::LevelIcon(), LevelLabel);
                    const bool bLevelOpen = ImGui::TreeNodeEx(LevelLabelWithIcon.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth);

                    // Drag a Scene asset from the asset browser onto the Level's own row to add it.
                    // Same "DESCRIPTOR_GUID" payload the Scene row already decodes for
                    // prefab-instantiation, just checked against the Scene type instead. Skips an
                    // already-present scene rather than adding a duplicate.
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DESCRIPTOR_GUID"))
                        {
                            IM_ASSERT(payload->DataSize == sizeof(e10::drag_and_drop_folder_payload_t));
                            auto& Dropped = *reinterpret_cast<const e10::drag_and_drop_folder_payload_t*>(payload->Data);
                            if (Dropped.m_Source.m_Type == xecs::scene::type_guid_v)
                            {
                                const xecs::scene::guid NewSceneGuid{ .m_Instance = Dropped.m_Source.m_Instance };
                                if (std::find(pLevel->m_Scenes.begin(), pLevel->m_Scenes.end(), NewSceneGuid) == pLevel->m_Scenes.end())
                                    e29::commands::Run(Undo, std::format("AddScene -Level {:016X} -Scene {}"
                                        , State.m_CurrentLevel.m_Instance.m_Value
                                        , e29::commands::FormatSceneGuid(NewSceneGuid)));
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    if (bLevelOpen)
                    {
                        for (std::size_t iScene = 0; iScene < pLevel->m_Scenes.size(); ++iScene)
                        {
                            ImGui::PushID(static_cast<int>(iScene));
                            const auto SceneGuid = pLevel->m_Scenes[iScene];

                            std::string SceneLabel;
                            e29::RemapGUIDToString(SceneLabel, xresource::full_guid{ SceneGuid.m_Instance, SceneGuid.m_Type });

                            const bool bIsOpenScene = std::find(State.m_OpenScenes.begin(), State.m_OpenScenes.end(), SceneGuid) != State.m_OpenScenes.end();

                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            const std::string SceneLabelWithIcon = std::format("{} {}", e29::SceneIcon(), SceneLabel);
                            // Open/loaded is STATUS, not selection focus. Still use Selected so
                            // TreeNode paints a fill, but tint Header* grey locally so it does not
                            // collide with entity-selection blue (ImGuiCol_Header from E29_Theme).
                            if (bIsOpenScene)
                            {
                                ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.29f, 0.29f, 0.29f, 1.0f)); // ~0x4A
                                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.35f, 0.35f, 0.35f, 1.0f)); // ~0x59
                                ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.40f, 0.40f, 0.40f, 1.0f)); // ~0x66
                            }
                            const bool bSceneExpanded = ImGui::TreeNodeEx(SceneLabelWithIcon.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth | (bIsOpenScene ? ImGuiTreeNodeFlags_Selected : 0));
                            if (bIsOpenScene)
                                ImGui::PopStyleColor(3);

                            // Two independent pieces of state used to collide: ImGui's own
                            // expand/collapse (bSceneExpanded, toggled by ImGuiTreeNodeFlags_OpenOnArrow
                            // on an ARROW click specifically) vs our own scene-residency tracking
                            // (bIsOpenScene/State.m_OpenScenes, previously driven ONLY by
                            // IsItemClicked() on the row's LABEL). Clicking the arrow expands the row
                            // WITHOUT satisfying IsItemClicked() the same way a label click does, so a
                            // row could sit expanded forever showing an inert "(click to open)"
                            // placeholder. Fixed by making "expanded" simply IMPLY "should be open" -
                            // whichever click actually toggled it, OpenScene's own residency check
                            // (State.m_OpenScenes) makes this a cheap no-op once already loaded, so
                            // calling it every frame the row is expanded is safe.
                            if ((bSceneExpanded && !bIsOpenScene) || ImGui::IsItemClicked())
                                e29::OpenScene(GameMgr, State, xresource::full_guid{ SceneGuid.m_Instance, SceneGuid.m_Type });

                            // Right-click: same "New Entity"/"New Folder" the Folder row's own menu
                            // offers (landing at this scene's root), plus removing the scene itself.
                            if (ImGui::BeginPopupContextItem())
                            {
                                if (auto* pMenuScene = GameMgr.m_SceneMgr.Find(SceneGuid))
                                    e29::ShowCreateMenuItems(SceneGuid, *pMenuScene, xecs::scene::invalid_folder_id_v, Undo);
                                ImGui::Separator();
                                if (ImGui::MenuItem("Remove Scene"))
                                {
                                    e29::commands::Run(Undo, std::format("RemoveScene -Level {:016X} -Scene {}"
                                        , State.m_CurrentLevel.m_Instance.m_Value
                                        , e29::commands::FormatSceneGuid(SceneGuid)));
                                    ImGui::EndPopup();
                                    if (bSceneExpanded) ImGui::TreePop();
                                    ImGui::PopID();
                                    break; // pLevel->m_Scenes was just mutated mid-iteration
                                }
                                ImGui::EndPopup();
                            }

                            // Drag this Level-Tree scene onto another scene's Dependencies folder
                            // (same DESCRIPTOR_GUID + e10::drag_and_drop_folder_payload_t the asset
                            // browser emits for Scene assets). Also works as a drop onto the Level
                            // row (AddScene skips duplicates). SourceAllowNullID: TreeNodeEx items
                            // don't always have a stable ImGui ID the way Button/Selectable do.
                            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                            {
                                e10::drag_and_drop_folder_payload_t Payload{};
                                Payload.m_Source     = xresource::full_guid{ SceneGuid.m_Instance, SceneGuid.m_Type };
                                Payload.m_bSelection = false;
                                ImGui::SetDragDropPayload("DESCRIPTOR_GUID", &Payload, sizeof(Payload));
                                ImGui::Text("%s", SceneLabel.c_str());
                                ImGui::EndDragDropSource();
                            }

                            // Drop a Prefab asset from the asset browser here to instantiate it -
                            // decodes the SAME "DESCRIPTOR_GUID" payload the browser's own asset icons
                            // already drag (see e10::drag_and_drop_folder_payload_t). ALSO accepts an
                            // entity dragged out of a folder back to loose/root (E29_ENTITY_DRAG,
                            // reusing the same payload struct the prefab-creation drag already uses -
                            // it already carries exactly {SceneGuid, Id}).
                            if (bIsOpenScene && ImGui::BeginDragDropTarget())
                            {
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DESCRIPTOR_GUID"))
                                {
                                    IM_ASSERT(payload->DataSize == sizeof(e10::drag_and_drop_folder_payload_t));
                                    auto& Dropped = *reinterpret_cast<const e10::drag_and_drop_folder_payload_t*>(payload->Data);
                                    if (Dropped.m_Source.m_Type == xecs::prefab::type_guid_v)
                                    {
                                        if (auto* pDropScene = GameMgr.m_SceneMgr.Find(SceneGuid))
                                        {
                                            const auto NewId = e29::NextFreeEntityId(*pDropScene);
                                            e29::commands::Run(Undo, std::format("InstantiatePrefab -Scene {} -Id {} -Prefab {:016X} -Folder {:08X}"
                                                , e29::commands::FormatSceneGuid(SceneGuid), e29::commands::FormatEntityId(NewId)
                                                , Dropped.m_Source.m_Instance.m_Value, static_cast<std::uint32_t>(xecs::scene::invalid_folder_id_v)));
                                        }
                                    }
                                }
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("E29_ENTITY_DRAG"))
                                {
                                    IM_ASSERT(payload->DataSize == sizeof(e29::entity_drag_payload_t));
                                    auto& Dropped = *reinterpret_cast<const e29::entity_drag_payload_t*>(payload->Data);
                                    e29::commands::Run(Undo, std::format("MoveToFolder -Scene {} -Id {} -Folder {:08X}"
                                        , e29::commands::FormatSceneGuid(Dropped.m_SceneGuid), e29::commands::FormatEntityId(Dropped.m_Id)
                                        , static_cast<std::uint32_t>(xecs::scene::invalid_folder_id_v)));
                                }
                                ImGui::EndDragDropTarget();
                            }

                            ImGui::TableSetColumnIndex(1);
                            if (ImGui::SmallButton("Remove"))
                            {
                                e29::commands::Run(Undo, std::format("RemoveScene -Level {:016X} -Scene {}"
                                    , State.m_CurrentLevel.m_Instance.m_Value
                                    , e29::commands::FormatSceneGuid(SceneGuid)));
                                if (bSceneExpanded) ImGui::TreePop();
                                ImGui::PopID();
                                break; // pLevel->m_Scenes was just mutated mid-iteration
                            }

                            if (bSceneExpanded)
                            {
                                if (bIsOpenScene)
                                {
                                    if (auto* pScene = GameMgr.m_SceneMgr.Find(SceneGuid))
                                    {
                                        // One entity row, used both for folder members and loose
                                        // (unfoldered) entities below - returns true if it just mutated
                                        // pScene->m_LocalToRuntime (deleted), telling the caller's own
                                        // loop over a SNAPSHOT (never the live map/vector directly - see
                                        // every call site below) that this id is now stale.
                                        // Forward-declared as std::function (not auto) - RenderEntityRow
                                        // and RenderChildEntities are mutually recursive (a row renders
                                        // its own children right after itself; rendering a child is just
                                        // calling RenderEntityRow again), the same "declare empty, assign
                                        // after both bodies are written" pattern RenderFolderChildren's
                                        // own self-recursion already uses below, just across a pair.
                                        std::function<bool(xecs::scene::permanent_id, xecs::component::entity)> RenderEntityRow;

                                        // An entity with a xecs::component::parent is never placed via
                                        // folder membership (see the Default-folder adoption pass below,
                                        // which excludes parented entities from its "unfoldered"
                                        // computation) - it's rendered here instead, nested directly
                                        // under its parent's own row. Snapshots the children list before
                                        // recursing (an entity delete/reparent fired from arbitrary depth
                                        // in this recursion must not invalidate an iterator/reference held
                                        // across those calls - same discipline RenderFolderChildren's own
                                        // folder walk already follows).
                                        std::function<void(xecs::component::entity)> RenderChildEntities = [&](xecs::component::entity Parent) noexcept
                                        {
                                            auto& PDetails = GameMgr.m_ComponentMgr.getEntityDetails(Parent);
                                            if (PDetails.m_pPool == nullptr) return;
                                            if (PDetails.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID) == false) return;

                                            auto ChildEntities = PDetails.m_pPool->getComponent<xecs::component::children>(PDetails.m_PoolIndex).m_List;
                                            for (auto ChildEntity : ChildEntities)
                                            {
                                                if (auto ChildIt = pScene->m_RuntimeToLocal.find(ChildEntity.m_Value); ChildIt != pScene->m_RuntimeToLocal.end())
                                                    RenderEntityRow(ChildIt->second, ChildEntity);
                                            }
                                        };

                                        RenderEntityRow = [&](xecs::scene::permanent_id Id, xecs::component::entity Entity) -> bool
                                        {
                                            std::string EntityLabel = std::format("Entity #{}", Id);
                                            bool bHasChildren = false;
                                            if (auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(Entity); Details.m_pPool)
                                            {
                                                auto Bits = Details.m_pPool->m_pArchetype->getComponentBits();
                                                if (Bits.getBit(xecs::component::type::info_v<e29::name>.m_BitID))
                                                    EntityLabel = Details.m_pPool->getComponent<e29::name>(Details.m_PoolIndex).m_Value;
                                                bHasChildren = Bits.getBit(xecs::component::type::info_v<xecs::component::children>.m_BitID);
                                            }
                                            auto* pPI = e29::FindPrefabInstance(GameMgr, Entity);
                                            if (pPI)
                                            {
                                                std::string PrefabName;
                                                e29::RemapGUIDToString(PrefabName, pPI->m_PrefabInstance);
                                                EntityLabel += std::format(" (Prefab: {})", PrefabName);
                                            }

                                            // Search filters entities only (folders always stay visible
                                            // so a match nested inside one is still reachable).
                                            if (!State.m_TreeSearchString.empty() && !e29::ContainsCaseInsensitive(EntityLabel, State.m_TreeSearchString))
                                                return false;

                                            ImGui::PushID(static_cast<int>(Id));
                                            ImGui::TableNextRow();
                                            ImGui::TableSetColumnIndex(0);
                                            const bool bEntitySelected = (State.m_SelectedEntityId == Id);
                                            const bool bMultiSelected  = (State.m_MultiSelectScene == SceneGuid) && State.m_MultiSelectedEntityIds.contains(Id);
                                            // Prefab instances render in blue, matching Unity's own
                                            // Hierarchy convention - real GameObjects/entities stay the
                                            // default text color. Unlike the "(Prefab: X)" label suffix
                                            // above (root-only, via the direct pPI check), the tint
                                            // applies to the WHOLE instance subtree - any entity
                                            // structurally inside a prefab instance is still part of it.
                                            // An entity WITH children renders like a folder (expandable,
                                            // arrow) so RenderChildEntities has somewhere to nest under;
                                            // a leaf keeps the plain bullet style every entity used to have.
                                            const bool bPartOfPrefabInstance = e29::FindContainingPrefabInstance(GameMgr, Entity).m_pPI != nullptr;
                                            const ImGuiTreeNodeFlags SelFlag  = (bEntitySelected || bMultiSelected) ? ImGuiTreeNodeFlags_Selected : 0;
                                            const ImGuiTreeNodeFlags TreeFlags = bHasChildren
                                                ? (ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth | SelFlag)
                                                : (ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_SpanFullWidth | SelFlag);
                                            if (bPartOfPrefabInstance) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 170, 255, 255));
                                            const bool bEntityOpen = ImGui::TreeNodeEx(EntityLabel.c_str(), TreeFlags);
                                            if (bPartOfPrefabInstance) ImGui::PopStyleColor();

                                            // Select on mouse-UP, not mouse-DOWN (ImGui::IsItemClicked
                                            // fires on press) - selecting on press reassigns
                                            // State.m_SelectedEntity (rebuilding the WHOLE Entity
                                            // Properties inspector, m_bEntityInspectorDirty) before a
                                            // drag onto one of its OWN property rows (e.g. an
                                            // EntityReference's drop target) could ever get going,
                                            // exactly the "select on press" pitfall most drag-capable
                                            // list/tree widgets (Windows Explorer included) avoid.
                                            // GetMouseDragDelta (not IsMouseDragging, which needs the
                                            // button still held to report anything - already false by
                                            // the time a release is detected) is the one ImGui query
                                            // documented to still reflect the drag distance on the
                                            // exact release frame.
                                            if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                                            {
                                                const ImVec2 Drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                                                if (Drag.x == 0.0f && Drag.y == 0.0f) // released without ever dragging past the threshold
                                                {
                                                    // Routed through the command/undo system
                                                    // (commands/E29_Commands_Selection.h) instead of
                                                    // mutating State directly - phase 1 of
                                                    // [[e29_command_undo_system_plan]] (memory). Ctrl
                                                    // held = ToggleMultiSelect (multi-select only,
                                                    // primary selection untouched); plain click =
                                                    // Select (primary selection + reset multi-select
                                                    // to just this entity) - same two behaviors as
                                                    // before, just undoable now via Ctrl+Z.
                                                    if (ImGui::GetIO().KeyCtrl)
                                                        e29::commands::Run(Undo, std::format("ToggleMultiSelect -Scene {} -Id {}", e29::commands::FormatSceneGuid(SceneGuid), e29::commands::FormatEntityId(Id)));
                                                    else
                                                        e29::commands::Run(Undo, std::format("Select -Scene {} -Id {}", e29::commands::FormatSceneGuid(SceneGuid), e29::commands::FormatEntityId(Id)));
                                                }
                                            }

                                            // Three independent drag intents from the same row, picked
                                            // apart by the consumer at drop time (which target it landed
                                            // on), NOT by payload type name: dropping on the asset
                                            // browser creates a Prefab (e29::entity_to_prefab_drop);
                                            // dropping on a folder/scene-root row here reparents it;
                                            // dropping on an xecs::component::entity-typed property row
                                            // in the Entity Properties inspector assigns a reference (see
                                            // entity_inspector_bridge::m_OnEntityReferenceRender).
                                            // ImGui::SetDragDropPayload writes into ONE global payload
                                            // slot - calling it multiple times with different type-name
                                            // strings does NOT register multiple simultaneous payloads,
                                            // each call just overwrites the last, so only one name can
                                            // ever be shared here (same pattern as this codebase's own
                                            // "DESCRIPTOR_GUID" reuse).
                                            const bool bBeganDragSource = ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID);
                                            if (bBeganDragSource)
                                            {
                                                e29::entity_drag_payload_t Payload{ SceneGuid, Id };
                                                ImGui::SetDragDropPayload("E29_ENTITY_DRAG", &Payload, sizeof(Payload));
                                                ImGui::Text("%s", EntityLabel.c_str());
                                                ImGui::EndDragDropSource();
                                            }

                                            bool bDeleted = false;
                                            auto DoDeleteEntity = [&]() noexcept
                                            {
                                                // Routed through the command/undo system
                                                // ([[e29_command_undo_system_plan]] memory, phase 4 -
                                                // commands/E29_Commands_EntityLifecycle.h) instead of
                                                // calling DeleteEntitySubtree directly - selection/
                                                // multi-select survival cleanup (this row or a now-
                                                // deleted descendant of it) now lives in the command's
                                                // own DeleteSubtreeByPermanentId, shared with Undo so
                                                // it behaves identically from either direction.
                                                e29::commands::Run(Undo, std::format("DeleteEntity -Scene {} -Id {}", e29::commands::FormatSceneGuid(SceneGuid), e29::commands::FormatEntityId(Id)));
                                                bDeleted = true;
                                            };

                                            // Making a prefab is drag-and-drop only (ctrl-click to
                                            // multi-select, drag any selected row onto the asset
                                            // browser - entity_to_prefab_drop::OnDrop, generalized to
                                            // check for an active multi-selection), matching the
                                            // existing single-entity convention rather than a separate
                                            // menu action.
                                            if (ImGui::BeginPopupContextItem())
                                            {
                                                // "New Entity" here creates a CHILD of this row - the
                                                // only entity-row context menu action previously offered
                                                // was Delete; there was no way at all to create an entity
                                                // parented under another entity (direct user report).
                                                // Routed through the same CreateEntity command
                                                // ShowCreateMenuItems uses (E29_LevelSceneEditorKit.h),
                                                // just with -Parent instead of -Folder.
                                                if (ImGui::MenuItem("New Entity"))
                                                {
                                                    const auto NewId = e29::NextFreeEntityId(*pScene);
                                                    e29::commands::Run(Undo, std::format("CreateEntity -Scene {} -Id {} -Folder {:08X} -Parent {}"
                                                        , e29::commands::FormatSceneGuid(SceneGuid)
                                                        , e29::commands::FormatEntityId(NewId)
                                                        , static_cast<std::uint32_t>(xecs::scene::invalid_folder_id_v)
                                                        , e29::commands::FormatEntityId(Id)
                                                        ));
                                                }
                                                ImGui::Separator();
                                                if (ImGui::MenuItem("Delete Entity")) DoDeleteEntity();
                                                ImGui::EndPopup();
                                            }

                                            ImGui::TableSetColumnIndex(1);
                                            if (!bDeleted && ImGui::SmallButton("X")) DoDeleteEntity();

                                            // TreeNodeEx above (no NoTreePushOnOpen for a bHasChildren
                                            // row) already pushed a node onto ImGui's own ID/tree stack
                                            // whenever it returned bEntityOpen==true - that push MUST be
                                            // balanced by exactly one TreePop() call regardless of what
                                            // happens to the underlying entity afterward. Gating BOTH
                                            // calls on the same "!bDeleted" skips the TreePop the moment
                                            // a row that's both expanded AND just got deleted - corrupting
                                            // ImGui's stack, which surfaces as an unrelated-looking
                                            // IM_ASSERT/abort on a LATER frame or a later row. Only the
                                            // "render children" call itself should skip when deleted -
                                            // there's nothing left to walk into for a just-deleted subtree.
                                            if (bHasChildren && bEntityOpen)
                                            {
                                                if (!bDeleted) RenderChildEntities(Entity);
                                                ImGui::TreePop();
                                            }

                                            ImGui::PopID();
                                            return bDeleted;
                                        };

                                        // Recursive folder walk. Snapshots ids (never a live reference
                                        // into pScene->m_Folders) before recursing/rendering, then
                                        // re-finds each by id fresh right before use - the tree is
                                        // mutated in-place by "+ New Folder"/delete/reparent actions
                                        // fired from arbitrary depths in this same recursion, which
                                        // would invalidate any iterator/pointer held across those calls.
                                        std::function<void(xecs::scene::folder_id)> RenderFolderChildren = [&](xecs::scene::folder_id ParentId)
                                        {
                                            std::vector<xecs::scene::folder_id> ChildIds;
                                            for (auto& F : pScene->m_Folders)
                                            {
                                                if (F.m_Parent != ParentId) continue;
                                                ChildIds.push_back(F.m_Id);
                                            }

                                            // Alphabetical among siblings at this SAME level - the live-tree
                                            // analog of the saved file's own (depth, then name) ordering
                                            // (SaveSceneDescriptor), so what's on screen matches what's on disk.
                                            std::sort(ChildIds.begin(), ChildIds.end(), [&](auto A, auto B) noexcept
                                            {
                                                auto ItA = std::find_if(pScene->m_Folders.begin(), pScene->m_Folders.end(), [&](auto& F) noexcept { return F.m_Id == A; });
                                                auto ItB = std::find_if(pScene->m_Folders.begin(), pScene->m_Folders.end(), [&](auto& F) noexcept { return F.m_Id == B; });
                                                return ItA->m_Name < ItB->m_Name;
                                            });

                                            for (auto FolderId : ChildIds)
                                            {
                                                auto It = std::find_if(pScene->m_Folders.begin(), pScene->m_Folders.end(), [&](auto& F) noexcept { return F.m_Id == FolderId; });
                                                if (It == pScene->m_Folders.end()) continue; // deleted earlier this same frame

                                                ImGui::PushID(static_cast<int>(FolderId));
                                                ImGui::TableNextRow();
                                                ImGui::TableSetColumnIndex(0);
                                                const bool bFolderHasChildren = !It->m_Entities.empty() || std::any_of(pScene->m_Folders.begin(), pScene->m_Folders.end(), [&](auto& F) noexcept { return F.m_Parent == FolderId; });
                                                const std::string FolderLabel = std::format("{} {}", e29::FolderIcon(bFolderHasChildren), It->m_Name);
                                                const bool bFolderOpen = ImGui::TreeNodeEx(FolderLabel.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth);

                                                bool bFolderDeleted = false;
                                                if (ImGui::BeginPopupContextItem())
                                                {
                                                    e29::ShowCreateMenuItems(SceneGuid, *pScene, FolderId, Undo);
                                                    ImGui::Separator();
                                                    if (ImGui::MenuItem("Delete Folder"))
                                                    {
                                                        e29::commands::Run(Undo, std::format("DeleteFolder -Scene {} -Id {:08X}"
                                                            , e29::commands::FormatSceneGuid(SceneGuid)
                                                            , static_cast<std::uint32_t>(FolderId)
                                                            ));
                                                        bFolderDeleted = true;
                                                    }
                                                    ImGui::EndPopup();
                                                }
                                                if (bFolderDeleted)
                                                {
                                                    if (bFolderOpen) ImGui::TreePop();
                                                    ImGui::PopID();
                                                    continue; // It/this folder no longer exists - nothing left to render for it
                                                }

                                                if (ImGui::BeginDragDropTarget())
                                                {
                                                    // Drop a Prefab asset directly onto a folder row -
                                                    // matches the Scene row's own "DESCRIPTOR_GUID"
                                                    // handling, except the new instance lands in THIS
                                                    // folder instead of always landing loose at scene root.
                                                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DESCRIPTOR_GUID"))
                                                    {
                                                        IM_ASSERT(payload->DataSize == sizeof(e10::drag_and_drop_folder_payload_t));
                                                        auto& Dropped = *reinterpret_cast<const e10::drag_and_drop_folder_payload_t*>(payload->Data);
                                                        if (Dropped.m_Source.m_Type == xecs::prefab::type_guid_v)
                                                        {
                                                            const auto NewId = e29::NextFreeEntityId(*pScene);
                                                            e29::commands::Run(Undo, std::format("InstantiatePrefab -Scene {} -Id {} -Prefab {:016X} -Folder {:08X}"
                                                                , e29::commands::FormatSceneGuid(SceneGuid), e29::commands::FormatEntityId(NewId)
                                                                , Dropped.m_Source.m_Instance.m_Value, static_cast<std::uint32_t>(FolderId)));
                                                        }
                                                    }
                                                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("E29_ENTITY_DRAG"))
                                                    {
                                                        IM_ASSERT(payload->DataSize == sizeof(e29::entity_drag_payload_t));
                                                        auto& Dropped = *reinterpret_cast<const e29::entity_drag_payload_t*>(payload->Data);
                                                        if (Dropped.m_SceneGuid == SceneGuid)
                                                        {
                                                            // An entity with a xecs::component::parent is
                                                            // never placed via folder membership - it
                                                            // renders nested under its parent's own row
                                                            // instead (RenderChildEntities); dropping one
                                                            // onto a folder here is a no-op rather than
                                                            // creating a duplicate-looking entry. Pre-
                                                            // filtered here (rather than relying solely on
                                                            // move_to_folder_cmd's own identical check) so
                                                            // a normal invalid drop stays silent instead of
                                                            // logging a doomed command to the console.
                                                            bool bHasParent = false;
                                                            if (auto DroppedIt = pScene->m_LocalToRuntime.find(Dropped.m_Id); DroppedIt != pScene->m_LocalToRuntime.end())
                                                            {
                                                                auto& DroppedDetails = GameMgr.m_ComponentMgr.getEntityDetails(DroppedIt->second);
                                                                bHasParent = DroppedDetails.m_pPool && DroppedDetails.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::parent>.m_BitID);
                                                            }
                                                            if (!bHasParent)
                                                                e29::commands::Run(Undo, std::format("MoveToFolder -Scene {} -Id {} -Folder {:08X}"
                                                                    , e29::commands::FormatSceneGuid(SceneGuid), e29::commands::FormatEntityId(Dropped.m_Id)
                                                                    , static_cast<std::uint32_t>(FolderId)));
                                                        }
                                                    }
                                                    ImGui::EndDragDropTarget();
                                                }

                                                ImGui::TableSetColumnIndex(1);
                                                if (ImGui::SmallButton("X"))
                                                {
                                                    e29::commands::Run(Undo, std::format("DeleteFolder -Scene {} -Id {:08X}"
                                                        , e29::commands::FormatSceneGuid(SceneGuid)
                                                        , static_cast<std::uint32_t>(FolderId)
                                                        ));
                                                    if (bFolderOpen) ImGui::TreePop();
                                                    ImGui::PopID();
                                                    continue; // It/this folder no longer exists - nothing left to render for it
                                                }

                                                if (bFolderOpen)
                                                {
                                                    RenderFolderChildren(FolderId);

                                                    // Snapshot this folder's own member ids too - a
                                                    // nested delete/reparent could otherwise mutate
                                                    // It->m_Entities while this exact loop walks it.
                                                    std::vector<xecs::scene::permanent_id> MemberIds;
                                                    if (auto FreshIt = std::find_if(pScene->m_Folders.begin(), pScene->m_Folders.end(), [&](auto& F) noexcept { return F.m_Id == FolderId; }); FreshIt != pScene->m_Folders.end())
                                                        MemberIds = FreshIt->m_Entities;
                                                    for (auto EId : MemberIds)
                                                        if (auto EIt = pScene->m_LocalToRuntime.find(EId); EIt != pScene->m_LocalToRuntime.end())
                                                            RenderEntityRow(EId, EIt->second);

                                                    ImGui::TreePop();
                                                }
                                                ImGui::PopID();
                                            }
                                        };

                                        // Special, fixed folder for this scene's dependencies - NOT a
                                        // real entry in pScene->m_Folders (so it can never be confused
                                        // with a user folder), synthesized here from
                                        // pScene->m_ParentScenes directly. Always the first child of the
                                        // scene's own row: "there are no parent scenes in reality, Scenes
                                        // have dependencies" - and the folder itself "can not be moved or
                                        // touched", so no delete/drag-source on the folder row, only a
                                        // drop target (drag a Scene from the asset browser OR from this Level Tree onto it to add a dependency)
                                        // and a per-entry delete button below.
                                        {
                                            ImGui::PushID("Dependencies");
                                            ImGui::TableNextRow();
                                            ImGui::TableSetColumnIndex(0);
                                            const std::string DepLabel = std::format("{} Dependencies", e29::DependenciesIcon());
                                            const bool bDepOpen = ImGui::TreeNodeEx(DepLabel.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth);

                                            if (ImGui::BeginDragDropTarget())
                                            {
                                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DESCRIPTOR_GUID"))
                                                {
                                                    IM_ASSERT(payload->DataSize == sizeof(e10::drag_and_drop_folder_payload_t));
                                                    auto& Dropped = *reinterpret_cast<const e10::drag_and_drop_folder_payload_t*>(payload->Data);
                                                    if (Dropped.m_Source.m_Type == xecs::scene::type_guid_v && Dropped.m_Source.m_Instance != SceneGuid.m_Instance)
                                                    {
                                                        const xecs::scene::guid NewParent{ .m_Instance = Dropped.m_Source.m_Instance };
                                                        if (std::find(pScene->m_ParentScenes.begin(), pScene->m_ParentScenes.end(), NewParent) == pScene->m_ParentScenes.end())
                                                        {
                                                            e29::commands::Run(Undo, std::format("AddSceneDependency -Scene {} -Parent {}"
                                                                , e29::commands::FormatSceneGuid(SceneGuid)
                                                                , e29::commands::FormatSceneGuid(NewParent)));
                                                        }
                                                    }
                                                }
                                                ImGui::EndDragDropTarget();
                                            }

                                            if (bDepOpen)
                                            {
                                                for (std::size_t iDep = 0; iDep < pScene->m_ParentScenes.size(); ++iDep)
                                                {
                                                    ImGui::PushID(static_cast<int>(iDep));
                                                    std::string DepName;
                                                    e29::RemapGUIDToString(DepName, xresource::full_guid{ pScene->m_ParentScenes[iDep].m_Instance, pScene->m_ParentScenes[iDep].m_Type });

                                                    ImGui::TableNextRow();
                                                    ImGui::TableSetColumnIndex(0);
                                                    ImGui::TreeNodeEx(DepName.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_SpanFullWidth);

                                                    bool bDepRemoved = false;
                                                    if (ImGui::BeginPopupContextItem())
                                                    {
                                                        if (ImGui::MenuItem("Remove Dependency")) bDepRemoved = true;
                                                        ImGui::EndPopup();
                                                    }

                                                    ImGui::TableSetColumnIndex(1);
                                                    if (bDepRemoved || ImGui::SmallButton("X"))
                                                    {
                                                        const auto ParentGuid = pScene->m_ParentScenes[iDep];
                                                        e29::commands::Run(Undo, std::format("RemoveSceneDependency -Scene {} -Parent {}"
                                                            , e29::commands::FormatSceneGuid(SceneGuid)
                                                            , e29::commands::FormatSceneGuid(ParentGuid)));
                                                        ImGui::PopID();
                                                        break; // pScene->m_ParentScenes was just mutated mid-iteration
                                                    }
                                                    ImGui::PopID();
                                                }
                                                ImGui::TreePop();
                                            }
                                            ImGui::PopID();
                                        }

                                        // Prune any folder membership id that no longer resolves to a
                                        // live entity - a folder's Entities[] list is never itself
                                        // scrubbed except through the specific "delete this one entity"/
                                        // "reparent this one entity" code paths, so anything that ever
                                        // got out of that silently accumulates: the folder's own header
                                        // count (Default's own "(N)" label included) then disagrees with
                                        // what actually renders underneath it, since the render loop
                                        // below already skips ids it can't resolve. Runs before both the
                                        // folder-count label and the auto-adopt pass right below, so a
                                        // pruned id can be correctly picked back up as "unfoldered" and
                                        // re-adopted into Default the SAME frame instead of just
                                        // vanishing from bookkeeping with no visible trace.
                                        for (auto& F : pScene->m_Folders)
                                            std::erase_if(F.m_Entities, [&](auto Id) noexcept { return !pScene->m_LocalToRuntime.contains(Id); });

                                        // Any entity not currently in any folder renders directly at
                                        // scene root, no wrapping folder - direct user request (reverses
                                        // an earlier one: "remove the default folder"). An entity with a
                                        // xecs::component::parent is neither "foldered" nor "unfoldered"
                                        // - it's rendered nested under its parent's own row instead
                                        // (RenderEntityRow's own RenderChildEntities call), so it must
                                        // never ALSO render again here as if it were loose.
                                        {
                                            std::unordered_set<xecs::scene::permanent_id> FolderedEntities;
                                            for (auto& F : pScene->m_Folders)
                                                for (auto EId : F.m_Entities) FolderedEntities.insert(EId);

                                            std::vector<xecs::scene::permanent_id> Unfoldered;
                                            for (auto& Pair : pScene->m_LocalToRuntime)
                                            {
                                                if (FolderedEntities.contains(Pair.first)) continue;
                                                auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(Pair.second);
                                                if (Details.m_pPool && Details.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::parent>.m_BitID))
                                                    continue;
                                                Unfoldered.push_back(Pair.first);
                                            }

                                            for (auto Id : Unfoldered)
                                                if (auto EIt = pScene->m_LocalToRuntime.find(Id); EIt != pScene->m_LocalToRuntime.end())
                                                    RenderEntityRow(Id, EIt->second);
                                        }

                                        RenderFolderChildren(xecs::scene::invalid_folder_id_v); // root-level user folders

                                        // Prefabs are still instantiated by dragging one from the asset
                                        // browser onto this scene's own row in the tree (see the drop
                                        // target attached to it above) - Unity-style, no button.
                                    }
                                }
                                else
                                {
                                    ImGui::TableNextRow();
                                    ImGui::TableSetColumnIndex(0);
                                    ImGui::TextDisabled("(click to open)");
                                }
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }

                        // "Runtime" - a read-only row, sibling to this Level's own Scene rows, showing
                        // how many live ECS entities exist right now that are NOT registered in ANY
                        // currently-open Scene's own m_LocalToRuntime map - i.e. entities spawned
                        // directly into the ECS (xecs::archetype::instance::CreateEntity) without ever
                        // going through a Scene at all, which can only actually happen while the game
                        // is Playing. Direct user request: "the only time that folder will be populated
                        // is when the game is running and entities are spawned... those entities don't
                        // belong in any scene," so this sits under the LEVEL, not inside any particular
                        // Scene the way the old "Default" folder used to. No expand arrow (nothing to
                        // list - this is a count only, not an entity browser), no drag-drop target, no
                        // context menu, no delete button - inherently read-only by simply never wiring
                        // up any of those affordances, same "synthesized every frame, not a real
                        // persisted node" spirit as the "Dependencies" row already uses elsewhere in
                        // this tree. The underlying "an entity can exist outside any Scene" capability
                        // already exists at the ECS level (CreateEntity has never required a Scene) -
                        // this row is purely a UI surface over it, no new engine plumbing.
                        {
                            int TotalLive = 0;
                            for (auto& pArchetype : GameMgr.m_ArchetypeMgr.m_lArchetype)
                                for (auto pF = pArchetype->getFamilyHead(); pF; pF = pF->m_Next.get())
                                    for (auto pP = &pF->m_DefaultPool; pP; pP = pP->m_Next.get())
                                        TotalLive += pP->Size();

                            int Claimed = 0;
                            for (auto& OpenSceneGuid : State.m_OpenScenes)
                                if (auto* pOpenScene = GameMgr.m_SceneMgr.Find(OpenSceneGuid))
                                    Claimed += static_cast<int>(pOpenScene->m_LocalToRuntime.size());

                            const int RuntimeCount = std::max(0, TotalLive - Claimed);

                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            const std::string RuntimeLabel = std::format("{} Runtime ({})", e29::FolderIcon(RuntimeCount != 0), RuntimeCount);

                            // Distinct color (not a distinct icon - FolderIcon's own codepoints are
                            // already confirmed to render as blank tofu boxes in this font build, see
                            // e10_asset_tree_polish_pass2 memory, so a new icon here would be equally
                            // unreliable) - direct user request: "a special icon... or better yet a
                            // different color" to visually set this read-only, synthesized row apart
                            // from real user folders at a glance.
                            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 200, 90, 255));
                            ImGui::TreeNodeEx(RuntimeLabel.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth);
                            ImGui::PopStyleColor();
                        }

                        // Adding a Scene to this Level is now drag-and-drop onto the Level's own row
                        // (see the drop target attached to it above) - Unity-style, no button.

                        ImGui::TreePop();
                    }
                    ImGui::EndTable();
                }
                // Scene dependencies now render as a fixed "Dependencies" folder directly under each
                // Scene's own row inside the tree above, not a separate section here.
            }
        }
        ImGui::End();
    }

} // namespace e29

#endif // E29_PANEL_LEVEL_TREE_H
