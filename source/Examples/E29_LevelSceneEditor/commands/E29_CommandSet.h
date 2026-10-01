#ifndef E29_COMMAND_SET_H
#define E29_COMMAND_SET_H
#pragma once

// Every command the editor registers, constructed once at startup. Constructing a command registers it with the
// xundo system it is given: document commands go to the Level session's undo, workspace commands to the
// workspace undo. Scene commands are given the scene context, everything else the Level editor's.
// Meant to be included after the command headers (see E29_App.h).

namespace e29
{
    struct command_set
    {
        e29::commands::open_resource_editor_cmd          CmdOpenResourceEditor;
        e29::commands::resource_editor_command_cmd       CmdResourceEditorCommand;
        e29::commands::close_resource_editor_cmd         CmdCloseResourceEditor;
        e29::commands::capture_window_cmd                CmdCaptureWindow;
        xscene::commands::select_cmd                        CmdSelect;
        xscene::commands::toggle_multi_select_cmd           CmdToggleMultiSelect;
        xscene::commands::clear_selection_cmd               CmdClearSelection;
        xscene::commands::set_property_cmd                  CmdSetProperty;
        xscene::commands::revert_override_cmd               CmdRevertOverride;
        xscene::commands::apply_overrides_cmd               CmdApplyOverrides;
        xscene::commands::revert_hierarchy_overrides_cmd    CmdRevertHierarchyOverrides;
        xscene::commands::revert_all_overrides_cmd          CmdRevertAllOverrides;
        xscene::commands::add_component_cmd                 CmdAddComponent;
        xscene::commands::remove_component_cmd              CmdRemoveComponent;
        xscene::commands::create_entity_cmd                 CmdCreateEntity;
        xscene::commands::delete_entity_cmd                 CmdDeleteEntity;
        e29::commands::say_query_cmd                     CmdSay;
        e29::commands::get_log_query_cmd                 CmdGetLog;
        xlevel::commands::open_level_cmd                    CmdOpenLevel;
        xlevel::commands::close_scene_cmd                   CmdCloseScene;
        xlevel::commands::add_scene_cmd                     CmdAddScene;
        xlevel::commands::remove_scene_cmd                  CmdRemoveScene;
        xlevel::commands::add_scene_dependency_cmd          CmdAddSceneDependency;
        xlevel::commands::remove_scene_dependency_cmd       CmdRemoveSceneDependency;
        xresource_editor::commands::add_library_dependency_cmd        CmdAddLibraryDependency;
        xresource_editor::commands::remove_library_dependency_cmd     CmdRemoveLibraryDependency;
        xresource_editor::commands::create_library_query_cmd          CmdCreateLibrary;
        xresource_editor::commands::list_legal_reference_libraries_query_cmd CmdListLegalReferenceLibraries;
        xresource_editor::commands::list_libraries_query_cmd          CmdListLibraries;
        xlevel::commands::list_levels_query_cmd             CmdListLevels;
        xlevel::commands::list_scenes_query_cmd             CmdListScenes;
        xlevel::commands::list_entities_query_cmd           CmdListEntities;
        xlevel::commands::list_folders_query_cmd            CmdListFolders;
        xlevel::commands::audit_component_usage_query_cmd   CmdAuditComponentUsage;
        xlevel::commands::undo_query_cmd                    CmdUndo;
        xlevel::commands::redo_query_cmd                    CmdRedo;
        xlevel::commands::undo_query_cmd                    CmdLevelUndo;
        xlevel::commands::redo_query_cmd                    CmdLevelRedo;
        xlevel::commands::save_query_cmd                    CmdSave;
        xlevel::commands::close_query_cmd                   CmdClose;
        e29::commands::serialize_roundtrip_query_cmd     CmdSerializeRoundtrip;
        xlevel::commands::describe_entity_query_cmd         CmdDescribeEntity;
        xlevel::commands::list_component_types_query_cmd    CmdListComponentTypes;
        xscene::commands::set_entity_reference_cmd          CmdSetEntityReference;
        xlevel::commands::play_query_cmd                    CmdPlay;
        xlevel::commands::pause_query_cmd                   CmdPause;
        xlevel::commands::step_query_cmd                    CmdStep;
        xlevel::commands::stop_query_cmd                    CmdStop;
        xlevel::commands::get_play_state_query_cmd          CmdGetPlayState;
        xscene::commands::instantiate_prefab_cmd            CmdInstantiatePrefab;
        xscene::commands::move_to_folder_cmd                CmdMoveToFolder;
        xscene::commands::create_folder_cmd                 CmdCreateFolder;
        xscene::commands::delete_folder_cmd                 CmdDeleteFolder;
        xresource_editor::commands::list_assets_query_cmd             CmdListAssets;
        xresource_editor::commands::describe_asset_query_cmd          CmdDescribeAsset;
        xresource_editor::commands::rename_asset_cmd                  CmdRenameAsset;
        xresource_editor::commands::move_asset_cmd                    CmdMoveAsset;
        xresource_editor::commands::delete_asset_cmd                  CmdDeleteAsset;
        xresource_editor::commands::restore_asset_cmd                 CmdRestoreAsset;
        xresource_editor::commands::create_asset_cmd                  CmdCreateAsset;
        xresource_editor::commands::save_assets_query_cmd             CmdSaveAssets;
        e29::commands::add_script_source_file_cmd        CmdAddScriptSourceFile;
        e29::commands::remove_script_source_file_cmd     CmdRemoveScriptSourceFile;
        e29::commands::list_script_source_files_query_cmd CmdListScriptSourceFiles;
        e29::commands::add_project_module_reference_cmd  CmdAddProjectModuleReference;
        e29::commands::remove_project_module_reference_cmd CmdRemoveProjectModuleReference;
        e29::commands::list_project_module_references_query_cmd CmdListProjectModuleReferences;
        e29::commands::set_script_source_file_content_cmd CmdSetScriptSourceFileContent;
        e29::commands::rename_script_source_file_cmd     CmdRenameScriptSourceFile;
        e29::commands::regenerate_project_module_sources_query_cmd CmdRegenerateProjectModuleSources;
        xresource_editor::commands::rename_asset_file_cmd             CmdRenameAssetFile;
        xresource_editor::commands::move_asset_file_cmd               CmdMoveAssetFile;
        xresource_editor::commands::delete_asset_file_cmd             CmdDeleteAssetFile;
        xresource_editor::commands::restore_asset_file_cmd            CmdRestoreAssetFile;
        xresource_editor::commands::copy_asset_file_cmd               CmdCopyAssetFile;
        xscene::commands::make_prefab_cmd                   CmdMakePrefab;
        xscene::commands::make_prefab_variant_cmd           CmdMakePrefabVariant;
        xresource_editor::commands::recompile_all_query_cmd           CmdRecompileAll;
        xresource_editor::commands::recompile_errors_query_cmd        CmdRecompileErrors;
        xresource_editor::commands::compile_start_query_cmd           CmdCompileStart;
        xresource_editor::commands::compile_pause_query_cmd           CmdCompilePause;
        xresource_editor::commands::compile_auto_query_cmd            CmdCompileAuto;
        xresource_editor::commands::compile_status_query_cmd          CmdCompileStatus;
        e29::commands::run_sanity_check_query_cmd        CmdRunSanityCheck;
        e29::commands::get_idle_tasks_query_cmd          CmdGetIdleTasks;
        xresource_editor::commands::source_control_status_query_cmd   CmdSourceControlStatus;
        xresource_editor::commands::source_control_depot_status_query_cmd CmdSourceControlDepotStatus;
        xresource_editor::commands::source_control_refresh_query_cmd  CmdSourceControlRefresh;
        xresource_editor::commands::source_control_list_locks_query_cmd CmdSourceControlListLocks;
        xresource_editor::commands::source_control_lock_query_cmd     CmdSourceControlLock;
        xresource_editor::commands::source_control_unlock_query_cmd   CmdSourceControlUnlock;
        xresource_editor::commands::source_control_revert_query_cmd   CmdSourceControlRevert;
        xresource_editor::commands::source_control_stage_query_cmd    CmdSourceControlStage;
        xresource_editor::commands::source_control_commit_query_cmd   CmdSourceControlCommit;
        xresource_editor::commands::source_control_pull_query_cmd     CmdSourceControlPull;
        xresource_editor::commands::source_control_push_query_cmd     CmdSourceControlPush;

        command_set(xundo::system& Workspace, xundo::system& Level, xscene::scene_context* pScene, xlevel::level_context* pEditor) noexcept
        : CmdOpenResourceEditor(Workspace, pEditor)
        , CmdResourceEditorCommand(Workspace, pEditor)
        , CmdCloseResourceEditor(Workspace, pEditor)
        , CmdCaptureWindow(Workspace, pEditor)
        , CmdSelect(Level, pScene)
        , CmdToggleMultiSelect(Level, pScene)
        , CmdClearSelection(Level, pScene)
        , CmdSetProperty(Level, pScene)
        , CmdRevertOverride(Level, pScene)
        , CmdApplyOverrides(Level, pScene)
        , CmdRevertHierarchyOverrides(Level, pScene)
        , CmdRevertAllOverrides(Level, pScene)
        , CmdAddComponent(Level, pScene)
        , CmdRemoveComponent(Level, pScene)
        , CmdCreateEntity(Level, pScene)
        , CmdDeleteEntity(Level, pScene)
        , CmdSay(Workspace, pEditor)
        , CmdGetLog(Workspace, pEditor)
        , CmdOpenLevel(Workspace, pEditor)
        , CmdCloseScene(Level, pEditor)
        , CmdAddScene(Level, pEditor)
        , CmdRemoveScene(Level, pEditor)
        , CmdAddSceneDependency(Level, pEditor)
        , CmdRemoveSceneDependency(Level, pEditor)
        , CmdAddLibraryDependency(Level, pEditor)
        , CmdRemoveLibraryDependency(Level, pEditor)
        , CmdCreateLibrary(Workspace, pEditor)
        , CmdListLegalReferenceLibraries(Workspace, pEditor)
        , CmdListLibraries(Workspace, pEditor)
        , CmdListLevels(Workspace, pEditor)
        , CmdListScenes(Level, pEditor)
        , CmdListEntities(Level, pEditor)
        , CmdListFolders(Level, pEditor)
        , CmdAuditComponentUsage(Level, pEditor)
        , CmdUndo(Workspace, pEditor)
        , CmdRedo(Workspace, pEditor)
        , CmdLevelUndo(Level, pEditor)
        , CmdLevelRedo(Level, pEditor)
        , CmdSave(Workspace, pEditor)
        , CmdClose(Workspace, pEditor)
        , CmdSerializeRoundtrip(Level, pEditor)
        , CmdDescribeEntity(Level, pEditor)
        , CmdListComponentTypes(Level, pEditor)
        , CmdSetEntityReference(Level, pScene)
        , CmdPlay(Workspace, pEditor)
        , CmdPause(Workspace, pEditor)
        , CmdStep(Workspace, pEditor)
        , CmdStop(Workspace, pEditor)
        , CmdGetPlayState(Workspace, pEditor)
        , CmdInstantiatePrefab(Level, pScene)
        , CmdMoveToFolder(Level, pScene)
        , CmdCreateFolder(Level, pScene)
        , CmdDeleteFolder(Level, pScene)
        , CmdListAssets(Workspace, pEditor)
        , CmdDescribeAsset(Workspace, pEditor)
        , CmdRenameAsset(Workspace, pEditor)
        , CmdMoveAsset(Workspace, pEditor)
        , CmdDeleteAsset(Workspace, pEditor)
        , CmdRestoreAsset(Workspace, pEditor)
        , CmdCreateAsset(Workspace, pEditor)
        , CmdSaveAssets(Workspace, pEditor)
        , CmdAddScriptSourceFile(Workspace, pEditor)
        , CmdRemoveScriptSourceFile(Workspace, pEditor)
        , CmdListScriptSourceFiles(Workspace, pEditor)
        , CmdAddProjectModuleReference(Workspace, pEditor)
        , CmdRemoveProjectModuleReference(Workspace, pEditor)
        , CmdListProjectModuleReferences(Workspace, pEditor)
        , CmdSetScriptSourceFileContent(Workspace, pEditor)
        , CmdRenameScriptSourceFile(Workspace, pEditor)
        , CmdRegenerateProjectModuleSources(Workspace, pEditor)
        , CmdRenameAssetFile(Workspace, pEditor)
        , CmdMoveAssetFile(Workspace, pEditor)
        , CmdDeleteAssetFile(Workspace, pEditor)
        , CmdRestoreAssetFile(Workspace, pEditor)
        , CmdCopyAssetFile(Workspace, pEditor)
        , CmdMakePrefab(Level, pScene)
        , CmdMakePrefabVariant(Level, pScene)
        , CmdRecompileAll(Workspace, pEditor)
        , CmdRecompileErrors(Workspace, pEditor)
        , CmdCompileStart(Workspace, pEditor)
        , CmdCompilePause(Workspace, pEditor)
        , CmdCompileAuto(Workspace, pEditor)
        , CmdCompileStatus(Workspace, pEditor)
        , CmdRunSanityCheck(Workspace, pEditor)
        , CmdGetIdleTasks(Workspace, pEditor)
        , CmdSourceControlStatus(Workspace, pEditor)
        , CmdSourceControlDepotStatus(Workspace, pEditor)
        , CmdSourceControlRefresh(Workspace, pEditor)
        , CmdSourceControlListLocks(Workspace, pEditor)
        , CmdSourceControlLock(Workspace, pEditor)
        , CmdSourceControlUnlock(Workspace, pEditor)
        , CmdSourceControlRevert(Workspace, pEditor)
        , CmdSourceControlStage(Workspace, pEditor)
        , CmdSourceControlCommit(Workspace, pEditor)
        , CmdSourceControlPull(Workspace, pEditor)
        , CmdSourceControlPush(Workspace, pEditor)
        {}
    };
}

#endif // E29_COMMAND_SET_H
