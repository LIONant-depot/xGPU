#ifndef E29_COMMANDS_PROPERTY_EDIT_H
#define E29_COMMANDS_PROPERTY_EDIT_H
#pragma once

// Property editing - phase 2 of [[e29_command_undo_system_plan]] (memory). Replaces
// entity_inspector_bridge::m_OnPropertyChanged's own DIRECT, unconditional override-recording
// (E29_LevelSceneEditorKit.h - "every commit... writes only the new value... there is no automatic
// 'values now match, drop the diff' comparison anywhere") with a real xundo command whose Redo()/
// Undo() BOTH apply a value AND record the override for THAT value - so Undo doesn't just revert the
// live property, it also correctly reverts what the override bookkeeping remembers, instead of
// leaving a stale "overridden to the value you just undid" entry behind. Direct user warning before
// this was written: "careful with resetting the overrides."
//
// Deliberately does NOT go through xproperty::inspector::BeginEdit/CommitEdit's own whole-component
// snapshot bracket (see this project's own research into that mechanism before writing this file) -
// that bracket's m_OnChangeEvent notification carries a bracket LABEL and a multi-line blob, not a
// real property path and scalar value, which is exactly why the EXISTING "Revert Override" action
// has to set m_bSuppressOverrideTracking around it to avoid corrupting m_PropertyOverrides. This
// command instead calls xproperty::sprop::setProperty directly with the exact scalar Before/After
// values entity_inspector_bridge::m_OnPropertyChanged already has on hand (Cmd.m_Original/
// Cmd.m_NewValue, from the ORDINARY per-row commit path, not the bracket) - no suppression flag
// needed because this path never fires m_OnChangeEvent again in the first place.
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"

namespace e29::commands
{
    // Shared by set_property_cmd's own Redo()/Undo() - resolves the live component pointer for
    // {SceneGuid, Id, ComponentGuid}, matching entity_inspector_bridge::m_OnGetComponentPointer's own
    // pool-lookup pattern exactly (E29_LevelSceneEditorKit.h) - re-derived fresh every call rather
    // than cached, since a pool address is only ever valid for the frame/call that resolved it.
    struct resolved_property_target
    {
        xecs::component::entity            m_Entity{};
        const xecs::component::type::info* m_pInfo = nullptr;
        void*                              m_pInstance = nullptr;
    };

    inline resolved_property_target ResolvePropertyTarget(xecs::scene::guid SceneGuid, xecs::scene::permanent_id Id, std::uint64_t ComponentGuidValue) noexcept
    {
        resolved_property_target Out;
        if (!e29::g_pGameMgr) return Out;

        auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid);
        if (!pScene) return Out;
        auto It = pScene->m_LocalToRuntime.find(Id);
        if (It == pScene->m_LocalToRuntime.end()) return Out;
        Out.m_Entity = It->second;

        Out.m_pInfo = e29::g_pGameMgr->m_ComponentMgr.findComponentTypeInfo(xecs::component::type::guid{ ComponentGuidValue });
        if (!Out.m_pInfo) return Out;

        auto& Details = e29::g_pGameMgr->m_ComponentMgr.getEntityDetails(Out.m_Entity);
        if (!Details.m_pPool) { Out.m_pInfo = nullptr; return Out; }
        const auto iType = Details.m_pPool->findIndexComponentFromInfo(*Out.m_pInfo);
        if (iType < 0) { Out.m_pInfo = nullptr; return Out; }

        Out.m_pInstance = &Details.m_pPool->m_pComponent[iType][Details.m_PoolIndex.m_Value * Out.m_pInfo->m_Size];
        return Out;
    }

    // Sets the LIVE property value only (StringToAny-decode + xproperty::sprop::setProperty) - no
    // override bookkeeping at all. Split out from the old, single ApplyPropertyAndRecordOverride so
    // Undo can also reach the "set the value, then REMOVE the override" path below (see
    // RemovePropertyOverride's own comment) without duplicating the decode/setProperty call.
    inline void SetLivePropertyValue(const resolved_property_target& Target, const std::string& Path, std::uint32_t TypeGuid, const std::string& ValueStr) noexcept
    {
        xproperty::any Value;
        std::string    ValueStrMutable = ValueStr; // StringToAny takes a non-const span
        xproperty::settings::StringToAny(Value, TypeGuid, std::span<char>(ValueStrMutable.data(), ValueStrMutable.size()));

        std::string SetError;
        xproperty::settings::context Context;
        xproperty::sprop::setProperty(SetError, Target.m_pInstance, *Target.m_pInfo->m_pPropertyTable, xproperty::sprop::container::prop{ Path, Value }, Context);
    }

    // Non-creating check: does an override entry already exist for this exact Path? Read-only
    // counterpart to FindOrCreateOverrideEntry - used by set_property_cmd::BackupCurrenState (which
    // xundo::system::Execute always calls BEFORE Redo() - see set_property_cmd's own top comment) to
    // record whether THIS edit is the one that would newly create the override, so Undo can tell
    // "restore the override to its prior value" (one already existed) apart from "there was no
    // override at all" (must be fully removed on Undo, not just set back to Before - see
    // RemovePropertyOverride's own comment, and the direct user report this fixes: "when I override a
    // property then I undo it... it needs to go back to a non-overwritten property").
    inline bool HasPropertyOverride(xecs::component::entity Entity, const xecs::component::type::info& Info, const std::string& Path) noexcept
    {
        if (!e29::g_pGameMgr) return false;
        auto Ctx = e29::FindContainingPrefabInstance(*e29::g_pGameMgr, Entity);
        if (Ctx.m_pPI == nullptr) return false;
        for (auto& C : Ctx.m_pPI->m_lComponents)
        {
            if (C.m_ComponentTypeGuid != Info.m_Guid.m_Value) continue;
            if (!std::ranges::equal(C.m_MemberPath, Ctx.m_MemberPath)) continue;
            for (auto& O : C.m_PropertyOverrides)
                if (O.m_PropertyName == Path) return true;
            return false;
        }
        return false;
    }

    // Records/updates the override entry for Path with ValueStr - exactly matching
    // entity_inspector_bridge::m_OnPropertyChanged's own former inline bookkeeping (dirty-marking the
    // edited entity AND the prefab instance's own root when they differ, then FindOrCreateOverrideEntry
    // + write-or-update the matching prefab_property_override). Used by Redo() (After value) and by
    // Undo() ONLY when an override already existed before this edit (Before value) - see
    // set_property_cmd::Undo for the other case.
    inline void RecordPropertyOverride(const resolved_property_target& Target, xecs::scene::guid SceneGuid, xecs::scene::permanent_id Id, const std::string& Path, const std::string& ValueStr) noexcept
    {
        if (!Target.m_pInfo || !e29::g_pGameMgr) return;

        e29::g_pGameMgr->m_SceneMgr.MarkEntityDirty(SceneGuid, Id);

        auto Ctx = e29::FindContainingPrefabInstance(*e29::g_pGameMgr, Target.m_Entity);
        if (Ctx.m_pPI == nullptr) return;

        if (Ctx.m_RootEntity.m_Value != Target.m_Entity.m_Value)
        {
            if (auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid))
            {
                if (auto RootIt = pScene->m_RuntimeToLocal.find(Ctx.m_RootEntity.m_Value); RootIt != pScene->m_RuntimeToLocal.end())
                    e29::g_pGameMgr->m_SceneMgr.MarkEntityDirty(SceneGuid, RootIt->second);
            }
        }

        auto& CompOverride = e29::FindOrCreateOverrideEntry(*Ctx.m_pPI, Target.m_pInfo->m_Guid.m_Value, Ctx.m_MemberPath);
        for (auto& O : CompOverride.m_PropertyOverrides)
        {
            if (O.m_PropertyName == Path)
            {
                O.m_PropertyValueAsString = ValueStr;
                return;
            }
        }
        CompOverride.m_PropertyOverrides.push_back(xecs::editor::prefab_property_override{ .m_PropertyName = Path, .m_PropertyValueAsString = ValueStr });
    }

    // Removes the override entry for Path entirely - the property goes back to genuinely inherited
    // from the prefab, not merely reset to whatever value it held before this edit. Exact same
    // bookkeeping as entity_inspector_bridge's own "Revert Override" action
    // (E29_LevelSceneEditorKit.h, m_OnOverrideReset): erase the matching property override, and if the
    // owning component's override list is now empty, erase that whole component-override entry too -
    // otherwise a component would be left behind in xecs::editor::prefab_instance::m_lComponents with
    // an empty m_PropertyOverrides, which every other override-authoring path in this codebase treats
    // as "this component has overrides" (e.g. the Entity Properties panel's own override-tint check).
    inline void RemovePropertyOverride(const resolved_property_target& Target, xecs::scene::guid SceneGuid, xecs::scene::permanent_id Id, const std::string& Path) noexcept
    {
        if (!Target.m_pInfo || !e29::g_pGameMgr) return;

        auto Ctx = e29::FindContainingPrefabInstance(*e29::g_pGameMgr, Target.m_Entity);
        if (Ctx.m_pPI == nullptr) return;

        for (auto& C : Ctx.m_pPI->m_lComponents)
        {
            if (C.m_ComponentTypeGuid != Target.m_pInfo->m_Guid.m_Value) continue;
            if (!std::ranges::equal(C.m_MemberPath, Ctx.m_MemberPath)) continue;
            std::erase_if(C.m_PropertyOverrides, [&](auto& O) noexcept { return O.m_PropertyName == Path; });
            if (C.m_PropertyOverrides.empty())
            {
                auto& MemberPath = Ctx.m_MemberPath;
                const auto ComponentGuidValue = Target.m_pInfo->m_Guid.m_Value;
                std::erase_if(Ctx.m_pPI->m_lComponents, [&](auto& CC) noexcept { return CC.m_ComponentTypeGuid == ComponentGuidValue && std::ranges::equal(CC.m_MemberPath, MemberPath); });
            }
            break;
        }

        e29::g_pGameMgr->m_SceneMgr.MarkEntityDirty(SceneGuid, Id);
        if (Ctx.m_RootEntity.m_Value != Target.m_Entity.m_Value)
        {
            if (auto* pScene = e29::g_pGameMgr->m_SceneMgr.Find(SceneGuid))
            {
                if (auto RootIt = pScene->m_RuntimeToLocal.find(Ctx.m_RootEntity.m_Value); RootIt != pScene->m_RuntimeToLocal.end())
                    e29::g_pGameMgr->m_SceneMgr.MarkEntityDirty(SceneGuid, RootIt->second);
            }
        }
    }

    //================================================================================================
    // SetProperty - one committed edit on one property (the ordinary per-row inspector commit path,
    // not the whole-component snapshot bracket - see this file's own top comment). BackupCurrenState
    // writes EVERYTHING Undo(File) needs (Scene/Id/Component/Path/TypeGuid/Before) - Undo() never has
    // fresh command-line args available the way Redo() does (xundo::system::Undo() calls Undo(File)
    // directly, without re-Parse()-ing the stored command string first), and relying on this
    // command's own m_Parser state would break the moment a SECOND property edit ran Execute() again
    // before the FIRST one was undone (m_Parser is one shared instance per command TYPE, not per
    // history entry).
    //================================================================================================
    struct set_property_cmd : xundo::command_base
    {
        set_property_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "SetProperty", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override
        {
            return "Sets one property to a new value (undoable, restores the previous value AND the prefab-override bookkeeping on Undo). Usage: SetProperty -Scene hexguid -Id id -Component hex64 -Path base64 -TypeGuid hex32 -Before base64 -After base64";
        }
        void RegisterArguments() noexcept override
        {
            m_hScene     = m_Parser.addOption("Scene",     "Scene guid, 16 hex digits",              true, 1);
            m_hId        = m_Parser.addOption("Id",        "Entity permanent_id",                    true, 1);
            m_hComponent = m_Parser.addOption("Component", "Component type guid, 16 hex digits",     true, 1);
            m_hPath      = m_Parser.addOption("Path",      "Property path, base64",                  true, 1);
            m_hTypeGuid  = m_Parser.addOption("TypeGuid",  "Property value type guid, 8 hex digits", true, 1);
            m_hBefore    = m_Parser.addOption("Before",    "Previous value, base64",                 true, 1);
            m_hAfter     = m_Parser.addOption("After",     "New value, base64",                      true, 1);
        }

        std::string Redo() noexcept override
        {
            auto SceneArg = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg    = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
            auto CompArg  = m_Parser.getOptionArgAs<std::string>(m_hComponent, 0);
            auto PathArg  = m_Parser.getOptionArgAs<std::string>(m_hPath, 0);
            auto TypeArg  = m_Parser.getOptionArgAs<std::string>(m_hTypeGuid, 0);
            auto AfterArg = m_Parser.getOptionArgAs<std::string>(m_hAfter, 0);
            if (std::holds_alternative<xerr>(SceneArg) || std::holds_alternative<xerr>(IdArg) || std::holds_alternative<xerr>(CompArg)
                || std::holds_alternative<xerr>(PathArg) || std::holds_alternative<xerr>(TypeArg) || std::holds_alternative<xerr>(AfterArg))
                return "SetProperty: bad arguments";

            const auto SceneGuid = ParseSceneGuid(std::get<std::string>(SceneArg));
            const auto Id         = static_cast<xecs::scene::permanent_id>(std::stoul(std::get<std::string>(IdArg)));
            const auto CompGuid   = std::strtoull(std::get<std::string>(CompArg).c_str(), nullptr, 16);
            const auto Path       = Base64Decode(std::get<std::string>(PathArg));
            const auto TypeGuid   = static_cast<std::uint32_t>(std::strtoul(std::get<std::string>(TypeArg).c_str(), nullptr, 16));
            const auto After      = Base64Decode(std::get<std::string>(AfterArg));

            const auto Target = ResolvePropertyTarget(SceneGuid, Id, CompGuid);
            if (!Target.m_pInfo) return "SetProperty: target not found";

            SetLivePropertyValue(Target, Path, TypeGuid, After);
            RecordPropertyOverride(Target, SceneGuid, Id, Path, After);
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto SceneArg = m_Parser.getOptionArgAs<std::string>(m_hScene, 0);
            auto IdArg    = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
            auto CompArg  = m_Parser.getOptionArgAs<std::string>(m_hComponent, 0);
            auto PathArg  = m_Parser.getOptionArgAs<std::string>(m_hPath, 0);
            auto TypeArg  = m_Parser.getOptionArgAs<std::string>(m_hTypeGuid, 0);
            auto BeforeArg = m_Parser.getOptionArgAs<std::string>(m_hBefore, 0);

            const std::uint64_t Scene    = std::holds_alternative<xerr>(SceneArg) ? 0 : std::strtoull(std::get<std::string>(SceneArg).c_str(), nullptr, 16);
            const std::uint32_t Id        = std::holds_alternative<xerr>(IdArg) ? 0 : static_cast<std::uint32_t>(std::stoul(std::get<std::string>(IdArg)));
            const std::uint64_t Component = std::holds_alternative<xerr>(CompArg) ? 0 : std::strtoull(std::get<std::string>(CompArg).c_str(), nullptr, 16);
            const std::uint32_t TypeGuid  = std::holds_alternative<xerr>(TypeArg) ? 0 : static_cast<std::uint32_t>(std::strtoul(std::get<std::string>(TypeArg).c_str(), nullptr, 16));
            const std::string   Path      = std::holds_alternative<xerr>(PathArg) ? std::string{} : Base64Decode(std::get<std::string>(PathArg));
            const std::string   Before    = std::holds_alternative<xerr>(BeforeArg) ? std::string{} : Base64Decode(std::get<std::string>(BeforeArg));

            // Resolve the target and record whether an override already exists for THIS Path BEFORE
            // Redo() runs (xundo::system::Execute always calls BackupCurrenState before Redo - see
            // this struct's own top comment) - Undo needs this to tell "restore the override" apart
            // from "remove the override entirely" (see RemovePropertyOverride's own comment).
            bool bHadOverride = false;
            {
                const auto SceneGuid = xecs::scene::guid{ .m_Instance = { Scene } };
                if (const auto Target = ResolvePropertyTarget(SceneGuid, static_cast<xecs::scene::permanent_id>(Id), Component); Target.m_pInfo)
                    bHadOverride = HasPropertyOverride(Target.m_Entity, *Target.m_pInfo, Path);
            }

            File.Write(Scene);
            File.Write(Id);
            File.Write(Component);
            File.Write(TypeGuid);
            WriteString(File, Path);
            WriteString(File, Before);
            File.Write(bHadOverride);
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Scene = 0;     File.Read(Scene);
            std::uint32_t Id = 0;        File.Read(Id);
            std::uint64_t Component = 0; File.Read(Component);
            std::uint32_t TypeGuid = 0;  File.Read(TypeGuid);
            const std::string Path   = ReadString(File);
            const std::string Before = ReadString(File);
            bool bHadOverride = false;   File.Read(bHadOverride);

            const auto SceneGuid = xecs::scene::guid{ .m_Instance = { Scene } };
            const auto Target = ResolvePropertyTarget(SceneGuid, static_cast<xecs::scene::permanent_id>(Id), Component);
            if (!Target.m_pInfo) return;

            SetLivePropertyValue(Target, Path, TypeGuid, Before);
            if (bHadOverride)
                RecordPropertyOverride(Target, SceneGuid, static_cast<xecs::scene::permanent_id>(Id), Path, Before);
            else
                // This SetProperty was the edit that FIRST overrode this property - Undo must remove
                // the override entirely (not merely restore it to the pre-edit value), so the property
                // goes back to genuinely inherited-from-prefab. Direct user report this fixes: "when I
                // override a property then I undo it... it needs to go back to a non-overwritten
                // property."
                RemovePropertyOverride(Target, SceneGuid, static_cast<xecs::scene::permanent_id>(Id), Path);
        }

        xcmdline::parser::handle m_hScene, m_hId, m_hComponent, m_hPath, m_hTypeGuid, m_hBefore, m_hAfter;
    };
}

#endif // E29_COMMANDS_PROPERTY_EDIT_H
