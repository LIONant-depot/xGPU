#pragma once

// entity_inspector_bridge: inspector callbacks -> prefab-override and entity-reference commands.
// Split out of E29_LevelSceneEditorKit.h; included from there at the position this code used to occupy.
namespace e29
{
    //---------------------------------------------------------------------------
    // Entity Properties inspector wiring - bundles the prefab-override tracking/revert bookkeeping
    // and the entity_reference drag-drop-assign rendering that any xECS editor built on this kit
    // needs the moment it lets a component hold a raw xecs::component::entity field. Constructed once
    // alongside the owning example's own xproperty::inspector; RegisterCallbacks(...) wires all five
    // delegates in one call.
    //
    // Callbacks are stored as std::function MEMBERS (not locals inside RegisterCallbacks) specifically
    // so they outlive that one setup call: xdelegate::Register(T_CLASS&) binds to the callable object
    // BY REFERENCE, so whatever it's given must live as long as the inspector keeps calling it - a
    // local lambda inside RegisterCallbacks would be destroyed the moment that function returns (the
    // same "must be a named local that outlives the registration, not a temporary passed straight into
    // Register(...)" pitfall this codebase's own established convention already warns about elsewhere).
    // A std::function member is a fixed, nameable type a struct CAN hold (unlike the anonymous type of
    // a raw lambda), and since Register binds to ITS address, that address stays valid for exactly as
    // long as this bridge object does.
    //---------------------------------------------------------------------------
    struct entity_inspector_bridge
    {
        // Inspector-to-override pipeline: the inspector's callbacks only give us (type::object&, void*
        // pInstance) - m_ComponentMap closes the gap back to "which xECS component type is this",
        // rebuilt every time the inspector's content is (see RenderEntityPropertiesPanel's own
        // m_bEntityInspectorDirty block). m_bSuppressOverrideTracking guards the one re-entrancy risk:
        // the revert callback's own BeginEdit/CommitEdit bracket (used to write the prefab's base value
        // back into the instance) fires m_OnChangeEvent itself once committed - without the guard, a
        // revert would immediately re-record the very override it just removed.
        std::unordered_map<void*, const xecs::component::type::info*> m_ComponentMap;
        bool                                                           m_bSuppressOverrideTracking = false;

        // Set by the component-header callback when its "[X]" is clicked - processed once, right
        // after the owning inspector's Show(...) returns for the frame, rather than mutating the
        // entity's archetype WHILE the inspector is still mid-iteration over this same entity's
        // component list.
        const xecs::component::type::info* m_pPendingRemoveComponent = nullptr;

        std::function<void(xproperty::inspector&, const xproperty::ui::undo::cmd&)>                                                      m_OnPropertyChanged;
        std::function<void(xproperty::inspector&, const xproperty::type::object&, void*, std::string_view, const xproperty::any&, bool&)> m_OnOverrideCheck;
        std::function<void(xproperty::inspector&, const xproperty::type::object&, void*, std::string_view)>                              m_OnOverrideReset;
        std::function<void(xproperty::inspector&, const xproperty::type::object&, void*)>                                                m_OnComponentHeaderRender;
        std::function<void(xproperty::inspector&, const xproperty::type::object&, void*, std::string_view, const xproperty::any&, bool&)> m_OnEntityReferenceRender;

        // The "fake pointer, resolved by callback" indirection xproperty's own inspector expects
        // for any component instance whose address isn't a stable, owning member (see
        // E10_TextureResourcePipeline.cpp's identical use for the same reason - a selected asset's
        // descriptor can be reloaded/relocated out from under the inspector). xECS component-pool
        // addresses are exactly this case: an archetype migration, or a full world destroy/recreate
        // (Phase 8's hot reload) invalidates them independent of anything on the inspector's own
        // side. Registered in RegisterCallbacks; re-derives the CURRENT real pointer fresh every
        // time xproperty::inspector::Show() calls it (twice a frame - see xPropertyImGuiInspector's
        // own m_OnGetComponentPointer comment) rather than trusting anything cached from a prior
        // frame. Also the only place m_ComponentMap gets written now (previously written once, at
        // rebuild time, keyed by the same pointer that's now fake) - keyed by the freshly-resolved
        // real pointer, matching what m_OnOverrideCheck/m_OnPropertyChanged/etc. actually receive
        // from xproperty this same frame.
        std::function<void(xproperty::inspector&, const int, void*&, void*)> m_OnGetComponentPointer;

        void RegisterCallbacks(xproperty::inspector& Inspector, editor_context& Ed) noexcept
        {
            // xdelegate::Register(...) unconditionally push_back's - it has no dedup and no
            // Unregister at all (confirmed reading dependencies/xdelegate/source/xdelegate.h
            // directly). This method is called MORE than once on the SAME Inspector across this
            // session's lifetime (once at startup, again after Phase 8's ReloadGame recreates
            // GameMgr) - without clearing first, every callback below silently accumulates a
            // second, third, ... registration and fires that many times per event, which is
            // exactly the "properties/rows rendering doubled" bug a reload produced (confirmed live
            // - stacked "X"/duplicate rows in the Entity Properties panel after one reload).
            // E10_TextureResourcePipeline.cpp already established this exact idiom
            // (`Inspectors[0].m_OnGetComponentPointer.m_Delegates.clear();` before its own
            // re-Register) for the same reason - mirrored here for all six delegates this bridge
            // owns, not just the one E10 happened to need it for.
            Inspector.m_OnChangeEvent.m_Delegates.clear();
            Inspector.m_OnOverrideCheck.m_Delegates.clear();
            Inspector.m_OnOverrideReset.m_Delegates.clear();
            Inspector.m_OnComponentHeaderRender.m_Delegates.clear();
            Inspector.m_OnCustomRenderReplaceValue.m_Delegates.clear();
            Inspector.m_OnGetComponentPointer.m_Delegates.clear();

            // xdelegate::Register(T_CLASS&) binds to the lambda OBJECT itself (by reference) rather
            // than copying/erasing it into a std::function - so each callback must be a named local
            // that outlives the registration; here that "local" is the std::function MEMBER itself
            // (see this struct's own comment), assigned below and then registered.
            // Routed through the command/undo system (documentation/E29_LevelSceneEditor/command_undo_system_plan.md, phase
            // 2 - scene/commands/E29_Commands_PropertyEdit.h) instead of applying the value/recording the
            // override directly here - this is the ORDINARY per-row commit path (Cmd.m_Name is a real
            // property path, Cmd.m_NewValue/m_Original real scalar values), never the whole-component
            // BeginEdit/CommitEdit snapshot bracket the Revert Override action below uses (that one's
            // own m_OnChangeEvent notification carries a bracket label and a multi-line blob instead -
            // m_bSuppressOverrideTracking is what keeps THIS callback from misinterpreting that case).
            // set_property_cmd's own Redo()/Undo() do what this callback used to do inline (mark
            // dirty, FindOrCreateOverrideEntry) - the difference, and the whole point of this move, is
            // that Undo() now runs that SAME logic with the BEFORE value, so undoing an edit correctly
            // reverts the override bookkeeping too, not just the live property (direct user caution:
            // "careful with resetting the overrides").
            m_OnPropertyChanged = [this, &Ed](xproperty::inspector&, const xproperty::ui::undo::cmd& Cmd)
            {
                if (m_bSuppressOverrideTracking) return;

                auto It = m_ComponentMap.find(Cmd.m_pClassObject);
                if (It == m_ComponentMap.end()) return;
                auto& State = Ed.m_State;

                std::array<char, 256> BeforeBuffer{}, AfterBuffer{};
                const auto BeforeLen = e29::commands::FormatPropertyValue(BeforeBuffer, Cmd.m_Original);
                const auto AfterLen  = e29::commands::FormatPropertyValue(AfterBuffer, Cmd.m_NewValue);
                const std::string Before(BeforeBuffer.data(), BeforeLen > 0 ? static_cast<std::size_t>(BeforeLen) : 0);
                const std::string After(AfterBuffer.data(), AfterLen > 0 ? static_cast<std::size_t>(AfterLen) : 0);
                const std::uint32_t TypeGuid = Cmd.m_NewValue.m_pType ? Cmd.m_NewValue.m_pType->m_GUID : 0;

                xeditor::Run(Ed.m_Undo, std::format("SetProperty -Scene {} -Id {} -Component {:016X} -Path {} -TypeGuid {:08X} -Before {} -After {}"
                    , e29::commands::FormatSceneGuid(State.m_SelectedEntityScene)
                    , e29::commands::FormatEntityId(State.m_SelectedEntityId)
                    , It->second->m_Guid.m_Value
                    , xeditor::Base64Encode(Cmd.m_Name)
                    , TypeGuid
                    , xeditor::Base64Encode(Before)
                    , xeditor::Base64Encode(After)
                    ));
            };
            Inspector.m_OnChangeEvent.Register(m_OnPropertyChanged);

            m_OnOverrideCheck = [this, &Ed](xproperty::inspector&, const xproperty::type::object&, void* pInstance, std::string_view Path, const xproperty::any&, bool& bOut)
            {
                auto& GameMgr = Ed.World();
                auto& State   = Ed.m_State;
                bOut = false;

                auto It = m_ComponentMap.find(pInstance);
                if (It == m_ComponentMap.end()) return;

                auto Ctx = e29::FindContainingPrefabInstance(GameMgr, State.m_SelectedEntity);
                if (Ctx.m_pPI == nullptr) return;

                for (auto& C : Ctx.m_pPI->m_lComponents)
                {
                    if (C.m_ComponentTypeGuid != It->second->m_Guid.m_Value) continue;
                    if (std::ranges::equal(C.m_MemberPath, Ctx.m_MemberPath) == false) continue;
                    for (auto& O : C.m_PropertyOverrides)
                        if (O.m_PropertyName == Path) { bOut = true; return; }
                }
            };
            Inspector.m_OnOverrideCheck.Register(m_OnOverrideCheck);

            // Routed through RevertOverride (scene/commands/E29_Commands_PropertyEdit.h) instead of the
            // old inline BeginEdit/setProperty/erase_if path - same live+bookkeeping result, but
            // Ctrl+Z restores the overridden value and re-records the override entry.
            m_OnOverrideReset = [this, &Ed](xproperty::inspector& /*Inspector*/, const xproperty::type::object& Obj, void* pInstance, std::string_view Path)
            {
                auto& GameMgr = Ed.World();
                auto& State   = Ed.m_State;
                auto It = m_ComponentMap.find(pInstance);
                if (It == m_ComponentMap.end()) return;

                auto Ctx = e29::FindContainingPrefabInstance(GameMgr, State.m_SelectedEntity);
                if (Ctx.m_pPI == nullptr) return;

                if (auto Err = GameMgr.m_PrefabMgr.EnsureLoaded(Ctx.m_pPI->m_PrefabInstance); Err)
                {
                    xeditor::NotifyError(std::format("Failed to load source prefab for revert: {}", Err.getMessage()));
                    return;
                }

                auto RootIt = GameMgr.m_PrefabMgr.m_PrefabList.find(Ctx.m_pPI->m_PrefabInstance.m_Instance.m_Value);
                if (RootIt == GameMgr.m_PrefabMgr.m_PrefabList.end()) return;

                // Same MemberPath, walked from the PREFAB's own root instead of the placed instance's
                // root - reaches the corresponding source member (see
                // prefab_component_override::m_MemberPath).
                const auto BaseEntity = xecs::persist::details::ResolveMemberPath(GameMgr, RootIt->second, Ctx.m_MemberPath);
                if (BaseEntity.isValid() == false) return;

                auto& RootDetails = GameMgr.m_ComponentMgr.getEntityDetails(BaseEntity);
                const auto iType  = RootDetails.m_pPool->findIndexComponentFromInfo(*It->second);
                if (iType < 0) return;
                auto* pRootData = &RootDetails.m_pPool->m_pComponent[iType][RootDetails.m_PoolIndex.m_Value * It->second->m_Size];

                xproperty::settings::context Context;
                xproperty::any               BaseValue;
                xproperty::any               CurrentValue;
                bool                         bFoundBase = false;
                bool                         bFoundCurrent = false;
                xproperty::sprop::collector(pRootData, Obj, Context, [&](const char* pPropertyName, xproperty::any&& Data, const xproperty::type::members&, bool, const void*) noexcept
                {
                    if (Path == pPropertyName) { BaseValue = std::move(Data); bFoundBase = true; }
                });
                xproperty::sprop::collector(pInstance, Obj, Context, [&](const char* pPropertyName, xproperty::any&& Data, const xproperty::type::members&, bool, const void*) noexcept
                {
                    if (Path == pPropertyName) { CurrentValue = std::move(Data); bFoundCurrent = true; }
                });
                if (bFoundBase == false || bFoundCurrent == false) return;

                std::array<char, 256> BeforeBuffer{}, AfterBuffer{};
                const auto BeforeLen = e29::commands::FormatPropertyValue(BeforeBuffer, CurrentValue);
                const auto AfterLen  = e29::commands::FormatPropertyValue(AfterBuffer, BaseValue);
                const std::string Before(BeforeBuffer.data(), BeforeLen > 0 ? static_cast<std::size_t>(BeforeLen) : 0);
                const std::string After(AfterBuffer.data(), AfterLen > 0 ? static_cast<std::size_t>(AfterLen) : 0);
                const std::uint32_t TypeGuid = BaseValue.m_pType ? BaseValue.m_pType->m_GUID
                    : (CurrentValue.m_pType ? CurrentValue.m_pType->m_GUID : 0);

                xeditor::Run(Ed.m_Undo, std::format("RevertOverride -Scene {} -Id {} -Component {:016X} -Path {} -TypeGuid {:08X} -Before {} -After {}"
                    , e29::commands::FormatSceneGuid(State.m_SelectedEntityScene)
                    , e29::commands::FormatEntityId(State.m_SelectedEntityId)
                    , It->second->m_Guid.m_Value
                    , xeditor::Base64Encode(std::string(Path))
                    , TypeGuid
                    , xeditor::Base64Encode(Before)
                    , xeditor::Base64Encode(After)
                    ));
            };
            Inspector.m_OnOverrideReset.Register(m_OnOverrideReset);

            // "[X]" on a component's own header row - resolves back to which xECS component this is via
            // the SAME m_ComponentMap the property callbacks above already use, so it stays in sync
            // with whatever's currently appended. Excludes the same components the "Remove Component"
            // combo already excludes (internal bookkeeping components, and Name - every entity stays
            // nameable) - one shared exclusion list, not two independently maintained ones. Only
            // records the request (m_pPendingRemoveComponent); the actual AddOrRemoveComponents call
            // happens after the inspector's Show(...) returns for this frame.
            m_OnComponentHeaderRender = [this](xproperty::inspector&, const xproperty::type::object&, void* pInstance)
            {
                auto It = m_ComponentMap.find(pInstance);
                if (It == m_ComponentMap.end()) return;
                auto* pInfo = It->second;
                if (e29::IsInternalComponent(pInfo)) return;
                // Name is a regular, removable component like any other now - an entity with none of
                // its own components at all (not even Name) is a legitimate state.

                // NOT ImGui::SameLine() here - SameLine(x) positions using CursorPosPrevLine.y (the Y
                // of whichever line a real widget last finished on), not the actual current cursor.
                // Nothing real draws between NextColumn() and this callback firing, so for every
                // component AFTER the first, CursorPosPrevLine.y is still stale from the PREVIOUS
                // component's last property row - visible live as this button rendering on top of
                // whatever row happened to be last, not its own header. GetCursorScreenPos() (the
                // real, current cursor - already correctly placed by the caller right before this
                // fires) has no such staleness, so compute the absolute position from that instead.
                const ImVec2 RowPos = ImGui::GetCursorScreenPos();
                const float  AvailW = ImGui::GetContentRegionAvail().x;
                ImGui::SetCursorScreenPos(ImVec2(RowPos.x + AvailW - 20.0f, RowPos.y));
                // Borderless/transparent-at-rest, only picking up a background on hover - matches
                // Unity's own small inline toolbar icon buttons (direct user comparison screenshot:
                // a bordered gray box vs Unity's flat "?"/drag-handle/"..." icons that only highlight
                // on hover). ButtonHovered/ButtonActive are left as the theme's own values so the
                // hover feedback itself still reads as a real button, just not a boxed one at rest.
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
                if (ImGui::SmallButton("X")) m_pPendingRemoveComponent = pInfo;
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this component from the entity");
            };
            Inspector.m_OnComponentHeaderRender.Register(m_OnComponentHeaderRender);

            // Custom render for ANY xecs::component::entity-valued property (today, only
            // xecs::component::entity_reference::m_Target - but this is a value-type check, not a
            // per-property tag, so it applies automatically to any FUTURE component with an
            // entity-reference field too) - the shared inspector has no default draw style registered
            // for the raw 'entity' atomic type at all, so this is not optional polish, it's what makes
            // entity_reference safe to add to an entity in the first place. Drag a row from the Level
            // tree (E29_ENTITY_DRAG, the same shared payload reparenting/prefab-creation already use)
            // onto this property to assign it; "X" clears it. Shows "<unresolved>" rather than
            // crashing when the target is valid but its owning scene isn't currently open
            // (ResolveEntityReference can't search a scene nobody loaded) - the underlying
            // value/reference is untouched either way, this is purely a display limitation.
            m_OnEntityReferenceRender = [this, &Ed](xproperty::inspector& Inspector, const xproperty::type::object& Obj, void* pInstance, std::string_view Path, const xproperty::any& Value, bool& bHandled)
            {
                auto& GameMgr = Ed.World();
                auto& State   = Ed.m_State;
                if (Value.m_pType == nullptr || Value.m_pType->m_GUID != xproperty::settings::var_type<xecs::component::entity>::guid_v) return;
                bHandled = true;

                const auto CurrentValue = Value.get<xecs::component::entity>();
                std::string     Label;
                xecs::scene::guid TargetScene;
                const bool bResolved = e29::ResolveEntityReference(GameMgr, State, CurrentValue, Label, TargetScene);
                if (!bResolved) Label = CurrentValue.isValid() ? "<unresolved>" : "None";

                // A plain Text/TextUnformatted's own "last item" rect is only as wide as its glyphs -
                // dropping anywhere else in this (usually much wider) property cell would silently miss
                // BeginDragDropTarget's hover check entirely. Selectable with an explicit size fills the
                // REST of the cell with a real, hoverable rect.
                const float AvailWidth  = ImGui::GetContentRegionAvail().x;
                const bool  bShowClear  = CurrentValue.isValid();
                ImGui::Selectable(Label.c_str(), false, ImGuiSelectableFlags_None, ImVec2(bShowClear ? AvailWidth - 24.0f : AvailWidth, 0.0f));

                // Attached to the Selectable specifically, immediately after it and BEFORE the "X"
                // button below (which would otherwise become the new "last item" and steal the drop
                // target down to its own tiny rect the moment a reference is already assigned).
                const bool bIsDropTarget = ImGui::BeginDragDropTarget();
                if (bIsDropTarget)
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("E29_ENTITY_DRAG"))
                    {
                        IM_ASSERT(payload->DataSize == sizeof(e29::entity_drag_payload_t));
                        auto& Dropped = *reinterpret_cast<const e29::entity_drag_payload_t*>(payload->Data);
                        if (auto* pDropScene = GameMgr.m_SceneMgr.Find(Dropped.m_SceneGuid))
                        {
                            if (auto It = pDropScene->m_LocalToRuntime.find(Dropped.m_Id); It != pDropScene->m_LocalToRuntime.end())
                            {
                                // A reference pointing outside the owning entity's own scene needs an
                                // EXPLICIT Dependencies entry (pOwningScene->m_ParentScenes) to resolve
                                // on save/reload. That edge is authored only via the Dependencies folder
                                // (AddSceneDependency) - never auto-inferred from this drop. Missing or
                                // cyclic edges are refused before SetEntityReference runs.
                                bool bRefused = false;
                                if (Dropped.m_SceneGuid != State.m_SelectedEntityScene)
                                {
                                    if (auto* pOwningScene = GameMgr.m_SceneMgr.Find(State.m_SelectedEntityScene))
                                    {
                                        const bool bAlreadyDependency = std::find(pOwningScene->m_ParentScenes.begin(), pOwningScene->m_ParentScenes.end(), Dropped.m_SceneGuid) != pOwningScene->m_ParentScenes.end();
                                        if (!bAlreadyDependency && e29::WouldCreateDependencyCycle(GameMgr, State.m_SelectedEntityScene, Dropped.m_SceneGuid))
                                        {
                                            xeditor::NotifyError("Can't assign that reference: its scene already depends on this one (would create a circular scene dependency)");
                                            bRefused = true;
                                        }
                                        else if (!bAlreadyDependency)
                                        {
                                            // Explicit deps only: cross-scene refs require the user to
                                            // drag the target scene into this scene's Dependencies folder
                                            // first. Auto-adding ParentScenes from entity refs is what made
                                            // the graph unstable / hard to reason about.
                                            xeditor::NotifyError("Can't assign that reference: add the target scene under Dependencies first");
                                            bRefused = true;
                                        }
                                    }
                                }

                                // Routed through the command system (gap #3, documentation/E29_LevelSceneEditor/command_undo_known_gaps.md)
                                // instead of BeginEdit/setProperty/CommitEdit directly - see this file's
                                // own top comment (E29_Commands_EntityReference.h) for why AfterScene/
                                // AfterId (not the raw runtime handle It->second) are what actually cross
                                // into the command string.
                                if (!bRefused)
                                {
                                    auto CompIt = m_ComponentMap.find(pInstance);
                                    if (CompIt != m_ComponentMap.end())
                                    {
                                        xeditor::Run(Ed.m_Undo, std::format("SetEntityReference -Scene {} -Id {} -Component {:016X} -Path {} -AfterScene {} -AfterId {}"
                                            , e29::commands::FormatSceneGuid(State.m_SelectedEntityScene)
                                            , e29::commands::FormatEntityId(State.m_SelectedEntityId)
                                            , CompIt->second->m_Guid.m_Value
                                            , xeditor::Base64Encode(std::string(Path))
                                            , e29::commands::FormatSceneGuid(Dropped.m_SceneGuid)
                                            , e29::commands::FormatEntityId(Dropped.m_Id)));
                                    }
                                }
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                if (bShowClear)
                {
                    ImGui::SameLine();
                    // Same borderless/hover-only treatment as the component-header "X" above - this is
                    // the entity-reference "Target" field's own clear button, visible in the same panel.
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
                    const bool bClearClicked = ImGui::SmallButton("X");
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                    if (bClearClicked)
                    {
                        // AfterScene/AfterId 0/0 is SetEntityReference's own "clear" sentinel - same
                        // routing/reasoning as the assign path just above.
                        auto CompIt = m_ComponentMap.find(pInstance);
                        if (CompIt != m_ComponentMap.end())
                        {
                            xeditor::Run(Ed.m_Undo, std::format("SetEntityReference -Scene {} -Id {} -Component {:016X} -Path {} -AfterScene {} -AfterId {}"
                                , e29::commands::FormatSceneGuid(State.m_SelectedEntityScene)
                                , e29::commands::FormatEntityId(State.m_SelectedEntityId)
                                , CompIt->second->m_Guid.m_Value
                                , xeditor::Base64Encode(std::string(Path))
                                , e29::commands::FormatSceneGuid(xecs::scene::guid{})
                                , e29::commands::FormatEntityId(xecs::scene::invalid_permanent_id_v)));
                        }
                    }
                }
            };
            Inspector.m_OnCustomRenderReplaceValue.Register(m_OnEntityReferenceRender);

            // See this member's own declaration comment for why this exists at all. pUserData is
            // the pInfo passed to AppendEntityComponent's own pUserData argument (RenderEntity
            // PropertiesPanel's dirty-rebuild block) - re-derive the CURRENT pool address for that
            // exact component type on the CURRENTLY selected entity, the same lookup that block
            // itself uses, just re-run fresh instead of cached.
            m_OnGetComponentPointer = [this, &Ed](xproperty::inspector&, const int, void*& pObject, void* pUserData) noexcept
            {
                auto& GameMgr = Ed.World();
                auto& State   = Ed.m_State;
                pObject = nullptr;
                if (State.m_SelectedEntity.isValid() == false) return;

                auto* pInfo = static_cast<const xecs::component::type::info*>(pUserData);
                auto& Details = GameMgr.m_ComponentMgr.getEntityDetails(State.m_SelectedEntity);
                if (Details.m_pPool == nullptr) return;

                const auto iType = Details.m_pPool->findIndexComponentFromInfo(*pInfo);
                if (iType < 0) return;

                auto* pData = &Details.m_pPool->m_pComponent[iType][Details.m_PoolIndex.m_Value * pInfo->m_Size];
                pObject = pData;

                // Keyed by the freshly-resolved real pointer, matching what m_OnOverrideCheck/
                // m_OnPropertyChanged/m_OnComponentHeaderRender actually receive from xproperty this
                // same frame (xproperty temporarily overwrites the fake pointer with exactly this
                // value for the duration of its own Render pass - see xPropertyImGuiInspector.cpp's
                // own Show()).
                m_ComponentMap[pData] = pInfo;
            };
            Inspector.m_OnGetComponentPointer.Register(m_OnGetComponentPointer);
        }
    };
}
