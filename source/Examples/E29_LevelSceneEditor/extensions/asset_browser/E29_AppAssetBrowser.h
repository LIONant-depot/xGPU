#pragma once

// e29::app: wiring between the Asset Browser and the editor.
namespace e29
{
    // Everything the Asset Browser needs from the editor: command-routed mutations, source-control badges and locks,
    // open-asset routing (Level / Texture), and the extra Project Settings sections.
    inline void app::WireAssetBrowser()
    {
        e29::RegisterAssetBrowserCallbacks(AsserBrowser, E29Undo, MainWindow);



        // "Scripting" section in the merged Plugins/Project Settings tab (e10::plugin_tab,

        // E10_asset_browser_plugin_tab.h) - the project's Script-Module build-membership list

        // (Project.config\Script.config.txt, e29::g_ScriptConfig.m_ModuleRefs), rendered as a normal

        // xproperty::inspector array field (WireResourcePickerCallbacks already gives every full_guid

        // element a working click-to-browse picker for free, and the array gets the standard Unity-style

        // insert/delete/drag controls - see xproperty_array_element_controls). NOT the Dependencies-node

        // drag-drop pattern an earlier pass here used - that assumed the Resources tab could be docked

        // and visible AT THE SAME TIME as this one, which is false: "Project Settings" and "Resources"

        // are tabs in the SAME tab strip, mutually exclusive on screen, so a drag source and this drop

        // target could never both be visible. The inspector's own click-to-open-popup picker has no such

        // docking assumption at all - direct user correction.

        //

        // No m_OnPropertyChanged hook wired here (unlike EntityInspector's own edits, which route through

        // InspectorBridge into the undo system) - a snapshot/diff around the render call instead: simple,

        // catches every mutation kind the array control can make (insert/delete/reorder/reassign)

        // uniformly, and doesn't require raw edits here to go through xundo the way every other project-

        // level-settings edit in this codebase already doesn't either (Library.config.txt's own

        // ParentLibraries has no raw-inspector-edit path at all, only command-driven Add/Remove).

        //

        // Reuses plugin_tab's OWN inherited xproperty::inspector (passed in by RightPanel()) rather than

        // carrying a second, redundant instance - direct user correction: "you have one inspector

        // working with the plugin... why did you reinvent the wheel?". WireResourcePickerCallbacks is

        // registered lazily, once, the first time this section is actually selected - E29 has no way to

        // reach plugin_tab's own instance ahead of time (it's created generically inside assert_browser's

        // own tab list), so "wire on first use" is the only hook point available, not a startup call.

        AsserBrowser.m_ExtraPluginTabSections.push_back(

        {

            "Scripting",

            [](xproperty::inspector& Inspector)

            {

                static bool bWired = false;

                if (!bWired) { e29::WireResourcePickerCallbacks(Inspector); bWired = true; }



                // REAL BUG FOUND LIVE (2026-09-19): rebuilding (clear/AppendEntity/AppendEntityComponent)

                // on EVERY frame - not just when the data actually changed - makes every widget's ImGui id

                // unstable, so a tree node's own open/closed state can never persist between frames; the

                // ModuleRefs array node fought itself and flickered continuously the instant it was

                // expanded. Same documented failure mode as xproperty_inspector_must_persist_across_frames/

                // xgpu_imgui_per_frame_rebuild_activeid_bug - rebuild ONLY when s_BuiltWith says the

                // structure is stale (first render, or the data changed since the frame that built it,

                // whether from this same UI or an external CLI command), never unconditionally.

                static std::vector<xresource::full_guid> s_BuiltWith;

                if (e29::g_ScriptConfig.m_ModuleRefs != s_BuiltWith)

                {

                    Inspector.clear();

                    Inspector.AppendEntity();

                    Inspector.AppendEntityComponent(*xproperty::getObjectByType<e29::script_config>(), &e29::g_ScriptConfig);

                }



                // Separate, frame-local snapshot - did THIS ShowEmbedded call itself edit the array (an

                // insert/delete/reassign via the array's own controls)? Distinct from the staleness check

                // above, which would otherwise misfire a save on the very first render of an already-

                // populated list (stale-vs-s_BuiltWith is true then too, but nothing was actually edited).

                const auto BeforeThisRender = e29::g_ScriptConfig.m_ModuleRefs;



                xproperty::settings::context Context;

                Inspector.ShowEmbedded(Context);



                if (e29::g_ScriptConfig.m_ModuleRefs != BeforeThisRender)

                {

                    if (auto Err = e29::SaveScriptConfig(e10::g_LibMgr.m_ProjectPath, e29::g_ScriptConfig); Err)

                        xeditor::NotifyError(std::format("Failed to save Script.config.txt: {}", Err.getMessage()));

                    e29::RegenerateGameModuleSources();

                }

                s_BuiltWith = e29::g_ScriptConfig.m_ModuleRefs;

            }

        });



        // Source Control status/lock badges (Phase 3) - wired directly here rather than inside

        // RegisterAssetBrowserCallbacks (kit/E29_LevelSceneEditorKit.h): that function is defined in a

        // header included FIRST in this .cpp, before commands/E29_Commands_SourceControl.h and

        // extensions/source_control/E29_SourceControlStatus.h are - a lambda body referencing their

        // symbols from inside that header would fail to compile. This call site, further down the

        // .cpp, is past every needed include.

        //

        // Two separate hooks, not one combined value (direct user design decision, see

        // asset_status_badge/asset_lock_badge's own comment in E10_AssetBrowser.h): a file can be both

        // modified AND locked by you at once, and the lock signal must stay visible either way.

        AsserBrowser.m_OnGetAssetStatusBadge = [](e10::library::guid LibraryGuid, const std::wstring& RelativePath) -> int

        {

            const auto RootPath = e29::commands::ResolveLibraryRootPath(LibraryGuid);

            if (RootPath.empty()) return static_cast<int>(e10::asset_status_badge::None);



            // Untracked (new, not yet known to source control) vs Modified (tracked, has changes) -

            // direct user distinction: "usually most editors have a small + signifying a new file...

            // the dot does usually mean modified". GetCachedFileStatus only ever holds entries git

            // itself reported as changed (see its own comment) - a path present here but with neither

            // flag set (e.g. staged-only) still reads as Modified, matching "not clean" being the

            // meaningful signal for those.

            if (auto Status = e10::source_control::GetCachedFileStatus(RootPath, RelativePath))

                return static_cast<int>(Status->untracked ? e10::asset_status_badge::Untracked : e10::asset_status_badge::Modified);



            // Not in the changed-files cache: Clean if this root has actually been scanned at least

            // once, None (draw nothing) if it hasn't - GetLastRefreshTime is the only way to tell

            // "checked, all good" apart from "haven't checked yet" (see asset_status_badge's own

            // comment on why None and Clean are different values, not the same thing).

            return static_cast<int>(e10::source_control::GetLastRefreshTime(RootPath)

                ? e10::asset_status_badge::Clean : e10::asset_status_badge::None);

        };



        AsserBrowser.m_OnGetAssetLockBadge = [](e10::library::guid LibraryGuid, const std::wstring& RelativePath) -> int

        {

            const auto RootPath = e29::commands::ResolveLibraryRootPath(LibraryGuid);

            if (RootPath.empty()) return static_cast<int>(e10::asset_lock_badge::None);



            if (auto Lock = e10::source_control::GetCachedLockStatus(RootPath, RelativePath))

            {

                return static_cast<int>(Lock->ownership == sc::LockOwnership::CurrentUser

                    ? e10::asset_lock_badge::LockedByMe : e10::asset_lock_badge::LockedByOther);

            }

            return static_cast<int>(e10::asset_lock_badge::None);

        };



        AsserBrowser.m_OnGetSourceControlRevision = []() -> std::uint64_t

        {

            return e10::source_control::SourceControlRevision().load(std::memory_order_relaxed);

        };



        // Lock-before-edit gating (Phase 4B). Always calls PrepareEdit rather than pre-filtering with the

        // status cache: PrepareEdit already runs BatchIsLfsTracked internally and reports success

        // trivially for a non-LFS/text file (see sc_git_lfs_provider.hpp's own PrepareEdit), so there's no

        // separate "is this even lockable" check to duplicate here - one call already covers both "not

        // lockable" and "lockable and I got/kept the lock" as success, and only "lockable but someone else

        // holds it" (or another Require failure) as the one real refusal case files_tab needs to ask about.

        AsserBrowser.m_OnBeforeOpenAssetFile = [](e10::library::guid LibraryGuid, const std::wstring& RelativePath) -> bool

        {

            const auto RootPath = e29::commands::ResolveLibraryRootPath(LibraryGuid);

            if (RootPath.empty()) return true; // not a recognized library - nothing to gate



            auto* pWorkspace = e29::source_control::GetOrCreateWorkspace(RootPath);

            if (!pWorkspace) return true; // not a git working tree



            sc::PrepareEditRequest Request;

            Request.paths = { sc::WorkspacePath{ RelativePath } };

            Request.policy.lockRequirement = sc::LockRequirement::Require;



            const auto Result = pWorkspace->PrepareEdit(Request);

            if (Result.files.empty()) return true; // shouldn't happen - fail open rather than block



            // Same immediate-cache-update fix as the SourceControlLock/Unlock commands - "Open for Edit"

            // reaches PrepareEdit directly (not through the command bus), so it needs its own copy of

            // this rather than relying on theirs.

            if (Result.files.front().coordination.lock)

                e10::source_control::PublishSingleLock(RootPath, RelativePath, Result.files.front().coordination.lock);



            return Result.files.front().OperationSucceeded();

        };



        // Demand-driven scan priority (direct user request, 2026-09-17): when the Asset Tree navigates

        // to a real folder, that folder's status/lock data is requested at HIGH priority right away,

        // rather than waiting for its turn in the idle-triggered background sweep.

        AsserBrowser.m_OnFolderNavigated = [](e10::library::guid LibraryGuid, const std::wstring& RelativeFolderPath)

        {

            const auto RootPath = e29::commands::ResolveLibraryRootPath(LibraryGuid);

            if (RootPath.empty()) return;

            e29::source_control::RequestPriorityScan(RootPath, RelativeFolderPath);

        };



        // Editor Framework: double-click a Texture resource opens the standalone Texture editor

        // (Plugins/xtexture.plugin/source/Editor/xtexture_editor.h) - direct type check for now rather

        // than the generic xeditor::registry lookup (that registry's CreateDocument/CreateUI factories

        // aren't wired up yet - real follow-up work, not done under today's time budget). Every other

        // resource type's double-click behavior is unchanged (today's inert setSelection-only default).

        AsserBrowser.m_OnOpenAsset = [](e10::library::guid LibraryGuid, xresource::full_guid AssetGuid)
            {
                if (AssetGuid.m_Type == xecs::level::type_guid_v)
                {
                    if (e29::g_pGameMgr == nullptr || e29::g_pState == nullptr || e29::FindLevelUndo() == nullptr)
                        return;
                    if (e29::RequestOpenLevel(*e29::g_pGameMgr, *e29::g_pState, *e29::FindLevelUndo(), AssetGuid, /*bStartGameReload*/ true))
                        e29::g_pState->m_bPendingStartGameReloadAfterOpen = true;
                    return;
                }
                if (AssetGuid.m_Type != xrsc::texture_type_guid_v) return;
                for (auto& S : e29::g_OpenTextureEditors)
                    if (S && S->m_Document.getGuid() == AssetGuid) { S->Focus(); return; }
                e29::g_OpenTextureEditors.push_back(std::make_unique<xtexture_editor::session>(AssetGuid, LibraryGuid, e29::g_pTextureEditorDevice));
            };
    // Manual Lock/Unlock from the Asset Tree's own right-click menu (direct user request, 2026-09-17:

        // "we should always give the user the manual option to do it... just in case the user is doing

        // something special"). xeditor::RunQuery(), not Run() - SourceControlLock/Unlock are query_command_base

        // (see E29_CommandContext.h's own RunQuery comment for the real, previously-latent bug this

        // fixes: Run()'s plain Execute() call can never find a query-registered command). Source

        // Control* already no-op safely on a non-lockable/already-in-the-requested-state file, so no

        // pre-filtering here.

        AsserBrowser.m_OnLockAssetFile = [this](e10::library::guid LibraryGuid, const std::wstring& RelativePath)

        {

            xeditor::RunQuery(E29Undo, std::format("SourceControlLock -Library {} -Path {}"

                , e29::commands::FormatLibraryGuid(LibraryGuid), e29::commands::EncodeAssetPath(RelativePath)));

        };

        AsserBrowser.m_OnUnlockAssetFile = [this](e10::library::guid LibraryGuid, const std::wstring& RelativePath)

        {

            xeditor::RunQuery(E29Undo, std::format("SourceControlUnlock -Library {} -Path {}"

                , e29::commands::FormatLibraryGuid(LibraryGuid), e29::commands::EncodeAssetPath(RelativePath)));

        };



        // "SC Revert" from the Resources tab's per-tile "Resource Menu" and the Assets tab's own

        // RowContext menu (file or folder row) - direct user request. RelativePath may name either a

        // single file or a folder; RunRevertUnderFolder's own KeyIsUnderPrefix match already treats an

        // exact-match (a file naming itself) and a prefix-match (a folder) uniformly, so this callback

        // never needs to know which kind of path it was handed.

        AsserBrowser.m_OnRevertAssetPath = [this](e10::library::guid LibraryGuid, const std::wstring& RelativePath)

        {

            const auto RootPath = e29::commands::ResolveLibraryRootPath(LibraryGuid);

            if (RootPath.empty()) return;

            e29::commands::RunRevertUnderFolder(E29Undo, LibraryGuid, RootPath, RelativePath);

        };
    }
}
