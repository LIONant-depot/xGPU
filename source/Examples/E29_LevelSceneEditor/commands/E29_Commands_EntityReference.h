#ifndef E29_COMMANDS_ENTITY_REFERENCE_H
#define E29_COMMANDS_ENTITY_REFERENCE_H
#pragma once

// SetEntityReference - gap #3 of [[e29_command_undo_known_gaps]]. Assigning/clearing an
// xecs::component::entity-valued property (entity_reference::m_Target, drag-dropped onto or cleared
// via the "X" button in the inspector, entity_inspector_bridge::m_OnEntityReferenceRender,
// E29_LevelSceneEditorKit.h) used to mutate directly - not undoable, and not visible to a CLI/AI
// agent either, unlike every other property edit (set_property_cmd, E29_Commands_PropertyEdit.h).
//
// Mirrors set_property_cmd closely (same Redo/BackupCurrenState/Undo shape, same
// HasPropertyOverride/RecordPropertyOverride/RemovePropertyOverride bookkeeping) with ONE real
// difference: Before/After are encoded as {SceneGuid, permanent_id} pairs, NEVER a raw runtime
// xecs::component::entity handle. A raw handle is meaningless the instant its target entity is
// destroyed and a new one takes its slot - delete_entity_cmd's own top comment
// (E29_Commands_EntityLifecycle.h) already establishes exactly this reasoning for parent/children/
// entity_reference fields captured across a delete/undo boundary; the same risk applies here across
// an ordinary undo/redo boundary too; the entity referenced today might not be the entity an old
// undo_file record's raw handle would resolve to tomorrow. Resolved back to a live handle at
// Redo/Undo time via the target scene's own m_LocalToRuntime, same mechanism DeleteEntity's own
// reference-remap uses - permanent_id 0 (xecs::scene::invalid_permanent_id_v) is the "no reference"
// sentinel in both directions, so no separate "is this cleared" flag is needed.
//
// Deliberately does NOT own the cross-scene Dependencies edge (pOwningScene->m_ParentScenes) or the
// circular-dependency refusal check - both stay exactly where they already correctly live, in the
// drag-drop ACCEPTANCE logic (m_OnEntityReferenceRender itself, before it calls Run() with this
// command), the same way CreateEntity's own command doesn't decide "is this a valid drop location"
// either - that's a decision made before the command runs, not part of the command itself.
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_PropertyEdit.h"

namespace e29::commands
{
    // Resolves {SceneGuid, Id} to a live entity handle - Id == invalid_permanent_id_v (0, "no
    // reference") always resolves to an invalid/default entity regardless of SceneGuid. A missing
    // scene/id (the target was deleted sometime between BackupCurrenState and a later Undo/Redo) also
    // resolves to invalid rather than failing - matches DeleteEntity's own "a single corrupted/missing
    // snapshot must not take the whole subtree down" permissiveness.
    inline xecs::component::entity ResolveEntityReferenceTarget(xecs::scene::guid SceneGuid, xecs::scene::permanent_id Id) noexcept
    {
        if (Id == xecs::scene::invalid_permanent_id_v || !e29::g_pGameMgr) return {};
        auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
        if (!pScene) return {};
        auto It = pScene->m_LocalToRuntime.find(Id);
        return It != pScene->m_LocalToRuntime.end() ? It->second : xecs::component::entity{};
    }

    // Reverse direction - given a live handle, find which currently-OPEN scene owns it and its
    // permanent_id. Walks State.m_OpenScenes, same as ResolveEntityReference's own walk
    // (E29_PrefabOverrides.h, used by the inspector's read-only label rendering) - a target whose
    // owning scene isn't open can't be encoded any more than it can be displayed there; returns
    // {invalid_permanent_id_v} in that case, which SetEntityReference's own Redo/Undo already treat
    // as "no reference" symmetrically.
    inline std::pair<xecs::scene::guid, xecs::scene::permanent_id> FindEntityOwningScene(xecs::component::entity Entity) noexcept
    {
        if (!Entity.isValid() || !e29::g_pGameMgr || !e29::g_pState) return { xecs::scene::guid{}, xecs::scene::invalid_permanent_id_v };
        for (auto& SceneGuid : e29::g_pState->m_OpenScenes)
        {
            auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
            if (!pScene) continue;
            if (auto It = pScene->m_RuntimeToLocal.find(Entity.m_Value); It != pScene->m_RuntimeToLocal.end())
                return { SceneGuid, It->second };
        }
        return { xecs::scene::guid{}, xecs::scene::invalid_permanent_id_v };
    }

    // Applies an already-resolved entity handle directly (no string round trip needed for the LIVE
    // apply step - only the undo_file's own persisted Before/After need the handle-unsafe encoding
    // this file's own top comment explains). Mirrors SetLivePropertyValue's shape
    // (E29_Commands_PropertyEdit.h) minus the StringToAny decode.
    inline void SetLiveEntityReferenceValue(const resolved_property_target& Target, const std::string& Path, xecs::component::entity Value) noexcept
    {
        if (!Target.m_pInfo) return;
        xproperty::any Any;
        Any.set<xecs::component::entity>(Value);
        std::string SetError;
        xproperty::settings::context Context;
        xproperty::sprop::setProperty(SetError, Target.m_pInstance, *Target.m_pInfo->m_pPropertyTable, xproperty::sprop::container::prop{ Path, Any }, Context);
    }

    struct set_entity_reference_cmd : xundo::command_base
    {
        set_entity_reference_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "SetEntityReference", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override
        {
            return "Assigns or clears an entity-reference property (undoable, restores the previous target AND the prefab-override bookkeeping on Undo). Usage: SetEntityReference -Scene hexguid -Id hexid -Component hex64 -Path base64 -AfterScene hexguid -AfterId hexid (AfterId 00000000 = clear)";
        }
        void RegisterArguments() noexcept override
        {
            m_hScene      = m_Parser.addOption("Scene",      "Scene guid of the entity holding the reference, 16 hex digits",  true, 1);
            m_hId         = m_Parser.addOption("Id",         "Entity permanent_id holding the reference, 8 hex digits",       true, 1);
            m_hComponent  = m_Parser.addOption("Component",  "Component type guid, 16 hex digits",                            true, 1);
            m_hPath       = m_Parser.addOption("Path",       "Property path, Base64-encoded",                                 true, 1);
            m_hAfterScene = m_Parser.addOption("AfterScene", "Target entity's scene guid, 16 hex digits (0 = clear)",         true, 1);
            m_hAfterId    = m_Parser.addOption("AfterId",    "Target entity's permanent_id, 8 hex digits (0 = clear)",        true, 1);
        }

        std::string Redo() noexcept override
        {
            auto SceneArg      = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg         = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
            auto CompArg       = m_Parser.getOptionArgAs<std::string>(m_hComponent, 0);
            auto PathArg       = m_Parser.getOptionArgAs<std::string>(m_hPath, 0);
            auto AfterSceneArg = m_Parser.getOptionArgAs<std::string>(m_hAfterScene, 0);
            auto AfterIdArg    = m_Parser.getOptionArgAs<std::string>(m_hAfterId, 0);
            if (std::holds_alternative<xerr>(SceneArg) || std::holds_alternative<xerr>(IdArg) || std::holds_alternative<xerr>(CompArg)
                || std::holds_alternative<xerr>(PathArg) || std::holds_alternative<xerr>(AfterSceneArg) || std::holds_alternative<xerr>(AfterIdArg))
                return "SetEntityReference: bad arguments";

            const auto SceneGuid  = ParseSceneGuid(std::get<std::string>(SceneArg));
            const auto Id         = ParseEntityId(std::get<std::string>(IdArg));
            const auto CompGuid   = std::strtoull(std::get<std::string>(CompArg).c_str(), nullptr, 16);
            const auto Path       = Base64Decode(std::get<std::string>(PathArg));
            const auto AfterScene = ParseSceneGuid(std::get<std::string>(AfterSceneArg));
            const auto AfterId    = ParseEntityId(std::get<std::string>(AfterIdArg));

            const auto Target = ResolvePropertyTarget(SceneGuid, Id, CompGuid);
            if (!Target.m_pInfo) return "SetEntityReference: target not found";

            // A non-zero AfterId that fails to resolve is a real error (e.g. a stale/typo'd id from a
            // CLI/AI caller) - distinct from AfterId == 0, which always means "clear" and always
            // succeeds. Checked explicitly rather than silently clearing, so a bad CLI call fails
            // loudly instead of quietly assigning nothing.
            const auto AfterEntity = ResolveEntityReferenceTarget(AfterScene, AfterId);
            if (AfterId != xecs::scene::invalid_permanent_id_v && !AfterEntity.isValid())
                return "SetEntityReference: after-target not found";

            SetLiveEntityReferenceValue(Target, Path, AfterEntity);

            xproperty::any AnyVal; AnyVal.set<xecs::component::entity>(AfterEntity);
            std::array<char, 256> Buffer{};
            const auto Len = FormatPropertyValue(Buffer, AnyVal);
            const std::string ValueStr(Buffer.data(), Len > 0 ? static_cast<std::size_t>(Len) : 0);
            RecordPropertyOverride(Target, SceneGuid, Id, Path, ValueStr);
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto SceneArg = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg    = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
            auto CompArg  = m_Parser.getOptionArgAs<std::string>(m_hComponent, 0);
            auto PathArg  = m_Parser.getOptionArgAs<std::string>(m_hPath, 0);

            const std::uint64_t Scene     = std::holds_alternative<xerr>(SceneArg) ? 0 : std::strtoull(std::get<std::string>(SceneArg).c_str(), nullptr, 16);
            const std::uint32_t Id        = std::holds_alternative<xerr>(IdArg) ? 0 : ParseEntityId(std::get<std::string>(IdArg));
            const std::uint64_t Component = std::holds_alternative<xerr>(CompArg) ? 0 : std::strtoull(std::get<std::string>(CompArg).c_str(), nullptr, 16);
            const std::string   Path      = std::holds_alternative<xerr>(PathArg) ? std::string{} : Base64Decode(std::get<std::string>(PathArg));

            const auto SceneGuid = xecs::scene::guid{ .m_Instance = { Scene } };

            // Read the CURRENT live value (before Redo() overwrites it) and encode it the same
            // handle-unsafe way as AfterScene/AfterId - resolved back to a live entity fresh every
            // time it's used, never stored/compared as a raw handle. Also records whether an override
            // already existed for this Path, same "restore vs remove entirely" distinction
            // set_property_cmd's own BackupCurrenState makes (HasPropertyOverride's own comment).
            std::uint64_t BeforeScene = 0;
            std::uint32_t BeforeId    = 0;
            bool          bHadOverride = false;
            if (const auto Target = ResolvePropertyTarget(SceneGuid, static_cast<xecs::scene::permanent_id>(Id), Component); Target.m_pInfo)
            {
                bHadOverride = HasPropertyOverride(Target.m_Entity, *Target.m_pInfo, Path);

                // No single-property "get" API exists in xproperty/sprop (only setProperty) - read the
                // current value the same way the pre-existing "Revert Override" action's own base-value
                // lookup does (entity_inspector_bridge::m_OnOverrideReset, E29_LevelSceneEditorKit.h):
                // walk every property via the same collector DescribeEntity/SnapshotComponentProperties
                // already use, and keep the one whose path matches.
                xproperty::settings::context Context;
                xproperty::any CurrentValue;
                xproperty::sprop::collector(Target.m_pInstance, *Target.m_pInfo->m_pPropertyTable, Context, [&](const char* pPropertyName, xproperty::any&& Data, const xproperty::type::members&, bool, const void*) noexcept
                {
                    if (Path == pPropertyName) CurrentValue = std::move(Data);
                });
                if (CurrentValue.m_pType && CurrentValue.getTypeGuid() == xproperty::settings::var_type<xecs::component::entity>::guid_v)
                {
                    const auto [OwningScene, OwningId] = FindEntityOwningScene(CurrentValue.get<xecs::component::entity>());
                    BeforeScene = OwningScene.m_Instance.m_Value;
                    BeforeId    = OwningId;
                }
            }

            File.Write(Scene);
            File.Write(Id);
            File.Write(Component);
            WriteString(File, Path);
            File.Write(BeforeScene);
            File.Write(BeforeId);
            File.Write(bHadOverride);
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Scene = 0;       File.Read(Scene);
            std::uint32_t Id = 0;          File.Read(Id);
            std::uint64_t Component = 0;   File.Read(Component);
            const std::string Path = ReadString(File);
            std::uint64_t BeforeScene = 0; File.Read(BeforeScene);
            std::uint32_t BeforeId = 0;    File.Read(BeforeId);
            bool bHadOverride = false;     File.Read(bHadOverride);

            const auto SceneGuid = xecs::scene::guid{ .m_Instance = { Scene } };
            const auto Target = ResolvePropertyTarget(SceneGuid, static_cast<xecs::scene::permanent_id>(Id), Component);
            if (!Target.m_pInfo) return;

            const auto BeforeSceneGuid = xecs::scene::guid{ .m_Instance = { BeforeScene } };
            const auto BeforeEntity    = ResolveEntityReferenceTarget(BeforeSceneGuid, static_cast<xecs::scene::permanent_id>(BeforeId));
            SetLiveEntityReferenceValue(Target, Path, BeforeEntity);

            if (bHadOverride)
            {
                xproperty::any AnyVal; AnyVal.set<xecs::component::entity>(BeforeEntity);
                std::array<char, 256> Buffer{};
                const auto Len = FormatPropertyValue(Buffer, AnyVal);
                const std::string ValueStr(Buffer.data(), Len > 0 ? static_cast<std::size_t>(Len) : 0);
                RecordPropertyOverride(Target, SceneGuid, static_cast<xecs::scene::permanent_id>(Id), Path, ValueStr);
            }
            else
            {
                // This SetEntityReference was the edit that FIRST overrode this property - Undo must
                // remove the override entirely, same reasoning/precedent as set_property_cmd's own
                // Undo (RemovePropertyOverride's own comment).
                RemovePropertyOverride(Target, SceneGuid, static_cast<xecs::scene::permanent_id>(Id), Path);
            }
        }

        xcmdline::parser::handle m_hScene, m_hId, m_hComponent, m_hPath, m_hAfterScene, m_hAfterId;
    };
}

#endif // E29_COMMANDS_ENTITY_REFERENCE_H
