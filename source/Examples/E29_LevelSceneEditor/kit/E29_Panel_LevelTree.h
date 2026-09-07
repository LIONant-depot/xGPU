#ifndef E29_PANEL_LEVEL_TREE_H
#define E29_PANEL_LEVEL_TREE_H
#pragma once

// Extracted from E29_LevelSceneEditorKit.h (mechanical move, phase 1 of the kit split - see that
// file's own top comment). Meant to be included via the umbrella (E29_LevelSceneEditorKit.h) only,
// after every symbol this panel calls (editor_state, the folder/scene/prefab helpers, Debugger,
// entity_drag_payload_t, ...) is already defined - not designed to be included standalone.

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
    void RenderLevelTreePanel(xecs::game_mgr::instance& GameMgr, editor_state& State) noexcept
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
                if (ImGui::BeginTable("LevelTree", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersV | ImGuiTableFlags_ScrollY, ImVec2(0.0f, ImGui::GetContentRegionAvail().y)))
                {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 64.0f);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    const bool bLevelOpen = ImGui::TreeNodeEx(LevelLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth);

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
                                    pLevel->m_Scenes.push_back(NewSceneGuid);
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
                            const bool bSceneExpanded = ImGui::TreeNodeEx(SceneLabel.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth | (bIsOpenScene ? ImGuiTreeNodeFlags_Selected : 0));

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
                                    e29::ShowCreateMenuItems(GameMgr, SceneGuid, *pMenuScene, xecs::scene::invalid_folder_id_v);
                                ImGui::Separator();
                                if (ImGui::MenuItem("Remove Scene"))
                                {
                                    pLevel->m_Scenes.erase(pLevel->m_Scenes.begin() + iScene);
                                    e29::CloseScene(GameMgr, State, SceneGuid);
                                    ImGui::EndPopup();
                                    if (bSceneExpanded) ImGui::TreePop();
                                    ImGui::PopID();
                                    break; // pLevel->m_Scenes was just mutated mid-iteration
                                }
                                ImGui::EndPopup();
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
                                            e29::InstantiatePrefabIntoScene(GameMgr, *pDropScene, xecs::prefab::guid{ Dropped.m_Source.m_Instance, Dropped.m_Source.m_Type });
                                    }
                                }
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("E29_ENTITY_DRAG"))
                                {
                                    IM_ASSERT(payload->DataSize == sizeof(e29::entity_drag_payload_t));
                                    auto& Dropped = *reinterpret_cast<const e29::entity_drag_payload_t*>(payload->Data);
                                    if (auto* pDropScene = GameMgr.m_SceneMgr.Find(Dropped.m_SceneGuid))
                                        e29::ReparentEntityIntoFolder(*pDropScene, Dropped.m_Id, xecs::scene::invalid_folder_id_v);
                                }
                                ImGui::EndDragDropTarget();
                            }

                            ImGui::TableSetColumnIndex(1);
                            if (ImGui::SmallButton("Remove"))
                            {
                                pLevel->m_Scenes.erase(pLevel->m_Scenes.begin() + iScene);
                                e29::CloseScene(GameMgr, State, SceneGuid);
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
                                                    if (ImGui::GetIO().KeyCtrl)
                                                    {
                                                        // Toggles multi-select membership WITHOUT
                                                        // touching the primary selection/Properties
                                                        // panel - scoped to one scene at a time (a
                                                        // prefab's members must all come from the same
                                                        // live scene).
                                                        if (State.m_MultiSelectScene != SceneGuid)
                                                        {
                                                            State.m_MultiSelectedEntityIds.clear();
                                                            State.m_MultiSelectOrder.clear();
                                                            State.m_MultiSelectScene = SceneGuid;
                                                        }
                                                        if (State.m_MultiSelectedEntityIds.contains(Id))
                                                        {
                                                            State.m_MultiSelectedEntityIds.erase(Id);
                                                            std::erase(State.m_MultiSelectOrder, Id);
                                                        }
                                                        else
                                                        {
                                                            State.m_MultiSelectedEntityIds.insert(Id);
                                                            State.m_MultiSelectOrder.push_back(Id);
                                                        }
                                                    }
                                                    else
                                                    {
                                                        // Seeded with JUST this entity, not cleared to
                                                        // empty - matches the standard "click A, then
                                                        // ctrl-click B" convention (Explorer, Unity):
                                                        // a plain click alone is a single selection of
                                                        // one, but it's also the natural START of a
                                                        // multi-selection a following ctrl-click ADDS
                                                        // to, ending with {A, B} rather than losing A
                                                        // entirely the moment B is ctrl-clicked.
                                                        State.m_MultiSelectedEntityIds = { Id };
                                                        State.m_MultiSelectOrder       = { Id };
                                                        State.m_MultiSelectScene       = SceneGuid;
                                                        State.m_SelectedEntityId      = Id;
                                                        State.m_SelectedEntity        = Entity;
                                                        State.m_SelectedEntityScene   = SceneGuid;
                                                        State.m_bEntityInspectorDirty = true;
                                                    }
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
                                                // Recurses into Entity's own children (if any) rather than
                                                // just deleting this one row. Whichever entity/entities were
                                                // actually selected (this row or a now-deleted descendant of
                                                // it) get their selection cleared by checking survival
                                                // afterward, rather than only comparing against Id directly.
                                                e29::DeleteEntitySubtree(GameMgr, *pScene, SceneGuid, Entity);
                                                if (State.m_SelectedEntityScene == SceneGuid && !pScene->m_LocalToRuntime.contains(State.m_SelectedEntityId))
                                                {
                                                    State.m_SelectedEntityId    = xecs::scene::invalid_permanent_id_v;
                                                    State.m_SelectedEntity      = {};
                                                    State.m_SelectedEntityScene = {};
                                                }

                                                // Same survival check for the MULTI-select set/order -
                                                // a cascading delete can take out several ids at once
                                                // (the clicked row plus every descendant), any of
                                                // which might also have been part of an active
                                                // multi-selection.
                                                if (State.m_MultiSelectScene == SceneGuid)
                                                {
                                                    std::erase_if(State.m_MultiSelectedEntityIds, [&](auto Id) noexcept { return !pScene->m_LocalToRuntime.contains(Id); });
                                                    std::erase_if(State.m_MultiSelectOrder, [&](auto Id) noexcept { return !pScene->m_LocalToRuntime.contains(Id); });
                                                }
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
                                                // "Default" is rendered separately as a special, locked
                                                // folder - excluded here so it never ALSO gets the
                                                // generic New Folder/Delete/drag-drop treatment every
                                                // other root-level folder gets.
                                                if (ParentId == xecs::scene::invalid_folder_id_v && F.m_Name == "Default") continue;
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
                                                    e29::ShowCreateMenuItems(GameMgr, SceneGuid, *pScene, FolderId);
                                                    ImGui::Separator();
                                                    if (ImGui::MenuItem("Delete Folder"))
                                                    {
                                                        e29::DeleteFolder(*pScene, FolderId);
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
                                                    // folder instead of always falling out to "Default".
                                                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DESCRIPTOR_GUID"))
                                                    {
                                                        IM_ASSERT(payload->DataSize == sizeof(e10::drag_and_drop_folder_payload_t));
                                                        auto& Dropped = *reinterpret_cast<const e10::drag_and_drop_folder_payload_t*>(payload->Data);
                                                        if (Dropped.m_Source.m_Type == xecs::prefab::type_guid_v)
                                                            e29::InstantiatePrefabIntoScene(GameMgr, *pScene, xecs::prefab::guid{ Dropped.m_Source.m_Instance, Dropped.m_Source.m_Type }, FolderId);
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
                                                            // creating a duplicate-looking entry.
                                                            bool bHasParent = false;
                                                            if (auto DroppedIt = pScene->m_LocalToRuntime.find(Dropped.m_Id); DroppedIt != pScene->m_LocalToRuntime.end())
                                                            {
                                                                auto& DroppedDetails = GameMgr.m_ComponentMgr.getEntityDetails(DroppedIt->second);
                                                                bHasParent = DroppedDetails.m_pPool && DroppedDetails.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::parent>.m_BitID);
                                                            }
                                                            if (!bHasParent)
                                                                e29::ReparentEntityIntoFolder(*pScene, Dropped.m_Id, FolderId);
                                                        }
                                                    }
                                                    ImGui::EndDragDropTarget();
                                                }

                                                ImGui::TableSetColumnIndex(1);
                                                if (ImGui::SmallButton("X"))
                                                {
                                                    e29::DeleteFolder(*pScene, FolderId);
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
                                        // drop target (drag a Scene asset onto it to add a dependency)
                                        // and a per-entry delete button below.
                                        {
                                            ImGui::PushID("Dependencies");
                                            ImGui::TableNextRow();
                                            ImGui::TableSetColumnIndex(0);
                                            const std::string DepLabel = std::format("{} Dependencies", e29::FolderIcon(!pScene->m_ParentScenes.empty()));
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
                                                            if (e29::WouldCreateDependencyCycle(GameMgr, SceneGuid, NewParent))
                                                                e29::Debugger("Can't add that dependency: it already depends on this scene (would create a circular scene dependency)");
                                                            else
                                                                pScene->m_ParentScenes.push_back(NewParent);
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
                                                        pScene->m_ParentScenes.erase(pScene->m_ParentScenes.begin() + iDep);
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

                                        // Any entity not currently in any folder gets adopted into
                                        // "Default" (auto-created the first time it's actually needed)
                                        // - there's no more "loose at scene root" state at all. Runs
                                        // before the root-folder render below so a freshly-created
                                        // Default folder (or one that just gained a new member) renders
                                        // correctly the same frame.
                                        {
                                            std::unordered_set<xecs::scene::permanent_id> FolderedEntities;
                                            for (auto& F : pScene->m_Folders)
                                                for (auto EId : F.m_Entities) FolderedEntities.insert(EId);

                                            // An entity with a xecs::component::parent is neither
                                            // "foldered" nor "unfoldered" - it's rendered nested under its
                                            // parent's own row instead (RenderEntityRow's own
                                            // RenderChildEntities call), so it must never get force-
                                            // adopted into Default just for lacking folder membership.
                                            std::vector<xecs::scene::permanent_id> Unfoldered;
                                            for (auto& Pair : pScene->m_LocalToRuntime)
                                            {
                                                if (FolderedEntities.contains(Pair.first)) continue;
                                                auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(Pair.second);
                                                if (Details.m_pPool && Details.m_pPool->m_pArchetype->getComponentBits().getBit(xecs::component::type::info_v<xecs::component::parent>.m_BitID))
                                                    continue;
                                                Unfoldered.push_back(Pair.first);
                                            }

                                            // Always ensured, not just when something actually needs
                                            // adopting into it - every scene should show a "Default (N)"
                                            // row for discoverability/consistency, even at N=0, rather
                                            // than only appearing the first time it's actually needed.
                                            const auto DefaultId = e29::EnsureDefaultFolder(*pScene);
                                            for (auto Id : Unfoldered) e29::ReparentEntityIntoFolder(*pScene, Id, DefaultId);
                                        }

                                        // "Default" is a special, locked folder - same treatment as
                                        // Dependencies: no delete, no New Entity/New Folder menu (nothing
                                        // can be created directly inside it, and no subfolders of it
                                        // either), no manual drag-drop INTO it (only the automatic
                                        // adoption pass above ever populates it) - it's a temporary
                                        // holding area, not a real destination. The entities inside it
                                        // are perfectly ordinary rows, freely draggable OUT to a real
                                        // folder - that's the whole point. Rendered here, once,
                                        // separately from the generic recursive walk below (which
                                        // excludes it by name at the root level for exactly this reason).
                                        if (auto It = std::find_if(pScene->m_Folders.begin(), pScene->m_Folders.end(), [](auto& F) noexcept { return F.m_Parent == xecs::scene::invalid_folder_id_v && F.m_Name == "Default"; }); It != pScene->m_Folders.end())
                                        {
                                            const auto DefaultId = It->m_Id;
                                            ImGui::PushID("Default");
                                            ImGui::TableNextRow();
                                            ImGui::TableSetColumnIndex(0);
                                            const std::string DefaultLabel = std::format("{} Default ({})", e29::FolderIcon(!It->m_Entities.empty()), It->m_Entities.size());
                                            const bool bDefaultOpen = ImGui::TreeNodeEx(DefaultLabel.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth);
                                            if (bDefaultOpen)
                                            {
                                                std::vector<xecs::scene::permanent_id> MemberIds;
                                                if (auto FreshIt = std::find_if(pScene->m_Folders.begin(), pScene->m_Folders.end(), [&](auto& F) noexcept { return F.m_Id == DefaultId; }); FreshIt != pScene->m_Folders.end())
                                                    MemberIds = FreshIt->m_Entities;
                                                for (auto EId : MemberIds)
                                                    if (auto EIt = pScene->m_LocalToRuntime.find(EId); EIt != pScene->m_LocalToRuntime.end())
                                                        RenderEntityRow(EId, EIt->second);
                                                ImGui::TreePop();
                                            }
                                            ImGui::PopID();
                                        }

                                        RenderFolderChildren(xecs::scene::invalid_folder_id_v); // root-level user folders (Default excluded - rendered specially above)

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
