#pragma once

// Wires the Asset Browser's mutation hooks to E29's command/undo system.
// Split out of E29_LevelSceneEditorKit.h; included from there at the position this code used to occupy.
namespace e29
{
    // Wires the Asset Browser's optional mutation hooks (e10::assert_browser::m_OnRenameAsset/
    // m_OnMoveAsset/m_OnDeleteAsset/m_OnRestoreAsset/m_OnCreateAsset, E10_AssetBrowser.h) to E29's own
    // command/undo system, so a real click in the browser panel - not just a CLI/AI call - becomes an
    // undo-routed E29_Commands_AssetBrowser.h command. Same additive, opt-in pattern as
    // entity_inspector_bridge::RegisterCallbacks just above; every other example that embeds the same
    // browser leaves these hooks unset and is byte-for-byte unaffected. Plain free function (not a
    // whole bridge struct) since these hooks need no persistent per-frame render state, unlike the
    // property inspector's own m_ComponentMap.
    inline void RegisterAssetBrowserCallbacks(e10::assert_browser& Browser, xundo::system& Undo, xgpu::window& MainWindow) noexcept
    {
        // OS-level (Explorer) drag-out (E10_AssetOleDrag.h) needs to know the real Win32 rect of the
        // main window to tell "has this drag left our own app" apart from an ordinary in-app drag -
        // see m_OnGetMainWindowHandle's own comment in E10_AssetBrowser.h for the multi-viewport/
        // undocked-panel scope limit this deliberately accepts.
        Browser.m_OnGetMainWindowHandle = [&MainWindow](void) -> std::size_t
        {
            return MainWindow.getSystemWindowHandle();
        };

        Browser.m_OnRenameAsset = [&Undo](e10::library::guid LibraryGuid, xresource::full_guid Asset, std::string_view NewName)
        {
            xeditor::Run(Undo, std::format("RenameAsset -Library {} -Asset {} -Name {}"
                , e29::commands::FormatLibraryGuid(LibraryGuid), e29::commands::FormatAssetGuid(Asset), xeditor::Base64Encode(std::string(NewName))));
        };

        Browser.m_OnMoveAsset = [&Undo](e10::library::guid LibraryGuid, xresource::full_guid Asset, xresource::full_guid OldParent, xresource::full_guid NewParent)
        {
            xeditor::Run(Undo, std::format("MoveAsset -Library {} -Asset {} -OldParent {} -NewParent {}"
                , e29::commands::FormatLibraryGuid(LibraryGuid), e29::commands::FormatAssetGuid(Asset), e29::commands::FormatAssetGuid(OldParent), e29::commands::FormatAssetGuid(NewParent)));
        };

        Browser.m_OnDeleteAsset = [&Undo](e10::library::guid LibraryGuid, xresource::full_guid Asset)
        {
            xeditor::Run(Undo, std::format("DeleteAsset -Library {} -Asset {}"
                , e29::commands::FormatLibraryGuid(LibraryGuid), e29::commands::FormatAssetGuid(Asset)));
        };

        Browser.m_OnRestoreAsset = [&Undo](e10::library::guid LibraryGuid, xresource::full_guid Asset, xresource::full_guid NewParent)
        {
            xeditor::Run(Undo, std::format("RestoreAsset -Library {} -Asset {} -Parent {}"
                , e29::commands::FormatLibraryGuid(LibraryGuid), e29::commands::FormatAssetGuid(Asset), e29::commands::FormatAssetGuid(NewParent)));
        };

        // Needs the resulting guid back synchronously (the browser immediately selects it) - mints a
        // fresh instance guid itself, same call NewAsset's own auto-generate path uses internally
        // (xresource::instance_guid::GenerateGUID), so the command string always names an explicit id
        // rather than relying on CreateAsset's Redo to invent one (it deliberately never does - see
        // that command's own top comment on why Redo must stay deterministic/re-runnable).
        Browser.m_OnCreateAsset = [&Undo](e10::library::guid LibraryGuid, xresource::type_guid Type, xresource::full_guid Parent, std::string_view Name) -> xresource::full_guid
        {
            xresource::instance_guid NewInstance{};
            NewInstance.GenerateGUID();
            const xresource::full_guid NewAsset{ .m_Instance = NewInstance, .m_Type = Type };

            xeditor::Run(Undo, std::format("CreateAsset -Library {} -Type {:016X} -Asset {} -Parent {} -Name {}"
                , e29::commands::FormatLibraryGuid(LibraryGuid), Type.m_Value, e29::commands::FormatAssetGuid(NewAsset)
                , e29::commands::FormatAssetGuid(Parent), xeditor::Base64Encode(std::string(Name))));
            return NewAsset;
        };

        // Library-level "Dependencies" sub-node (virtual_tree_tab.h/files_tab.h's own root-row
        // drag-target + per-entry remove, see the "Multi-library project model" plan section) -
        // routes through AddLibraryDependency/RemoveLibraryDependency (undo-tracked, cycle-checked)
        // rather than mutating library_mgr directly, same reasoning as every hook above. ParentPath
        // is only actually consulted by the command when Parent isn't already loaded this session -
        // harmless to always pass it (the common case here, since the UI can only drag a library row
        // that is by definition already loaded and rendering in one of these same trees).
        Browser.m_OnAddLibraryDependency = [&Undo](e10::library::guid Owner, e10::library::guid Parent, const std::wstring& ParentPath)
        {
            xeditor::Run(e29::LevelDocUndo(), std::format("AddLibraryDependency -Library {} -Parent {} -ParentPath {}"
                , e29::commands::FormatLibraryGuid(Owner), e29::commands::FormatLibraryGuid(Parent), xeditor::Base64Encode(xstrtool::To(ParentPath))));
        };

        Browser.m_OnRemoveLibraryDependency = [&Undo](e10::library::guid Owner, e10::library::guid Parent)
        {
            xeditor::Run(e29::LevelDocUndo(), std::format("RemoveLibraryDependency -Library {} -Parent {}"
                , e29::commands::FormatLibraryGuid(Owner), e29::commands::FormatLibraryGuid(Parent)));
        };

        // Raw Assets-folder file hooks (Phase 5 of the window-split plan) - route files_tab's own
        // Rename/Move/Cut-Paste/Delete/Copy UI actions through the SAME MoveAssetFile/CopyAssetFile
        // xundo commands Phase 4 already proved via CLI (E29_Commands_AssetFiles.h), rather than a
        // second, competing call path into library_mgr.
        //
        // -Force 1 is passed HERE unconditionally: these hooks only ever fire from files_tab's own
        // StageOrExecute, which already ran the SAME CountDependents check and (if anything was
        // affected) already got the user's explicit "Continue" on its own confirmation modal before
        // calling this hook at all - re-running the command's own dependent-count gate here would just
        // reject a change the user already approved. The command-level gate exists for the OTHER path
        // into these commands - a human or AI issuing them directly via the Command Console/CLI, which
        // has no modal to click and must use its own -Force 1 deliberately instead.
        //
        // Batched - files_tab hands the WHOLE multi-item gesture here in one call; RunGroup turns it
        // into ONE undo/redo step for every item, not N separate ones (direct user correction: "a
        // 5-file delete should be 1 undo/redo step... the operation should be grouped" - xundo::system
        // already supports this via its own grouped Execute(), this was just never wired through it).
        Browser.m_OnMoveAssetFileBatch = [&Undo](e10::library::guid LibraryGuid, const std::vector<std::pair<std::wstring, std::wstring>>& Items) -> bool
        {
            std::vector<std::string> Cmds;
            Cmds.reserve(Items.size());
            for (auto& [OldRelPath, NewRelPath] : Items)
                Cmds.push_back(std::format("MoveAssetFile -Library {} -OldPath {} -NewPath {} -Force 1"
                    , e29::commands::FormatLibraryGuid(LibraryGuid), e29::commands::EncodeAssetPath(OldRelPath), e29::commands::EncodeAssetPath(NewRelPath)));
            return xeditor::RunGroup(Undo, "MoveAssetFile (multiple)", Cmds);
        };

        // -TrashPath must be pre-minted by the CALLER (ComputeTrashPath is a pure query, not something
        // Redo() can compute itself - see E29_Commands_AssetFiles.h's own top comment) - this hook is
        // exactly the call site that comment said didn't exist yet.
        Browser.m_OnDeleteAssetFileToTrashBatch = [&Undo](e10::library::guid LibraryGuid, const std::vector<std::wstring>& RelPaths) -> bool
        {
            std::vector<std::string> Cmds;
            Cmds.reserve(RelPaths.size());
            for (auto& RelPath : RelPaths)
            {
                const std::wstring TrashPath = e10::g_LibMgr.ComputeTrashPath(LibraryGuid, RelPath);
                Cmds.push_back(std::format("DeleteAssetFileToTrash -Library {} -Path {} -TrashPath {} -Force 1"
                    , e29::commands::FormatLibraryGuid(LibraryGuid), e29::commands::EncodeAssetPath(RelPath), e29::commands::EncodeAssetPath(TrashPath)));
            }
            return xeditor::RunGroup(Undo, "DeleteAssetFileToTrash (multiple)", Cmds);
        };

        Browser.m_OnRestoreAssetFileFromTrash = [&Undo](e10::library::guid LibraryGuid, const std::wstring& TrashRelPath, const std::wstring& OriginalRelPath)
        {
            xeditor::Run(Undo, std::format("RestoreAssetFileFromTrash -Library {} -TrashPath {} -OriginalPath {}"
                , e29::commands::FormatLibraryGuid(LibraryGuid), e29::commands::EncodeAssetPath(TrashRelPath), e29::commands::EncodeAssetPath(OriginalRelPath)));
        };

        Browser.m_OnCopyAssetFile = [&Undo](e10::library::guid LibraryGuid, const std::wstring& SourceRelPath, const std::wstring& NewRelPath)
        {
            xeditor::Run(Undo, std::format("CopyAssetFile -Library {} -SourcePath {} -NewPath {}"
                , e29::commands::FormatLibraryGuid(LibraryGuid), e29::commands::EncodeAssetPath(SourceRelPath), e29::commands::EncodeAssetPath(NewRelPath)));
        };
    }

    //---------------------------------------------------------------------------
    // The three UI panels below moved to standalone files under kit/ - phase 1 of the kit split
    // (direct user request, following an external review's proposed module boundaries). Included
    // here, in the same order they used to appear in this file, rather than left for the caller to
    // include separately - this file remains the one umbrella #include (E29_LevelScene_Editor.cpp
    // still just does #include "E29_LevelSceneEditorKit.h"), unchanged from the outside. Mechanical
    // move only - no behavior change; see each file's own top comment.
    //---------------------------------------------------------------------------
}
