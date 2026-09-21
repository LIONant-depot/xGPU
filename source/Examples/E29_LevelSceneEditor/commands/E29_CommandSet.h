#ifndef E29_COMMAND_SET_H
#define E29_COMMAND_SET_H
#pragma once

// Every command the editor registers, constructed once at startup. Constructing a command registers it with the
// xundo system it is given: document commands go to the Level session's undo, workspace commands to the
// workspace undo. Meant to be included after the command headers (see E29_App.h).

namespace e29
{
    struct command_set
    {
        e29::commands::open_texture_editor_cmd           CmdOpenTextureEditor;
        e29::commands::texture_editor_command_cmd        CmdTextureEditorCommand;
        e29::commands::select_cmd                        CmdSelect;
        e29::commands::toggle_multi_select_cmd           CmdToggleMultiSelect;
        e29::commands::clear_selection_cmd               CmdClearSelection;
        e29::commands::set_property_cmd                  CmdSetProperty;
        e29::commands::revert_override_cmd               CmdRevertOverride;
        e29::commands::apply_overrides_cmd               CmdApplyOverrides;
        e29::commands::revert_hierarchy_overrides_cmd    CmdRevertHierarchyOverrides;
        e29::commands::revert_all_overrides_cmd          CmdRevertAllOverrides;
        e29::commands::add_component_cmd                 CmdAddComponent;
        e29::commands::remove_component_cmd              CmdRemoveComponent;
        e29::commands::create_entity_cmd                 CmdCreateEntity;
        e29::commands::delete_entity_cmd                 CmdDeleteEntity;
        e29::commands::say_query_cmd                     CmdSay;
        e29::commands::get_log_query_cmd                 CmdGetLog;
        e29::commands::open_level_cmd                    CmdOpenLevel;
        e29::commands::close_scene_cmd                   CmdCloseScene;
        e29::commands::add_scene_cmd                     CmdAddScene;
        e29::commands::remove_scene_cmd                  CmdRemoveScene;
        e29::commands::add_scene_dependency_cmd          CmdAddSceneDependency;
        e29::commands::remove_scene_dependency_cmd       CmdRemoveSceneDependency;
        e29::commands::add_library_dependency_cmd        CmdAddLibraryDependency;
        e29::commands::remove_library_dependency_cmd     CmdRemoveLibraryDependency;
        e29::commands::create_library_query_cmd          CmdCreateLibrary;
        e29::commands::list_legal_reference_libraries_query_cmd CmdListLegalReferenceLibraries;
        e29::commands::list_levels_query_cmd             CmdListLevels;
        e29::commands::list_scenes_query_cmd             CmdListScenes;
        e29::commands::list_entities_query_cmd           CmdListEntities;
        e29::commands::list_folders_query_cmd            CmdListFolders;
        e29::commands::audit_component_usage_query_cmd   CmdAuditComponentUsage;
        e29::commands::undo_query_cmd                    CmdUndo;
        e29::commands::redo_query_cmd                    CmdRedo;
        e29::commands::undo_query_cmd                    CmdLevelUndo;
        e29::commands::redo_query_cmd                    CmdLevelRedo;
        e29::commands::save_query_cmd                    CmdSave;
        e29::commands::close_query_cmd                   CmdClose;
        e29::commands::serialize_roundtrip_query_cmd     CmdSerializeRoundtrip;
        e29::commands::describe_entity_query_cmd         CmdDescribeEntity;
        e29::commands::list_component_types_query_cmd    CmdListComponentTypes;
        e29::commands::set_entity_reference_cmd          CmdSetEntityReference;
        e29::commands::play_query_cmd                    CmdPlay;
        e29::commands::pause_query_cmd                   CmdPause;
        e29::commands::step_query_cmd                    CmdStep;
        e29::commands::stop_query_cmd                    CmdStop;
        e29::commands::get_play_state_query_cmd          CmdGetPlayState;
        e29::commands::instantiate_prefab_cmd            CmdInstantiatePrefab;
        e29::commands::move_to_folder_cmd                CmdMoveToFolder;
        e29::commands::create_folder_cmd                 CmdCreateFolder;
        e29::commands::delete_folder_cmd                 CmdDeleteFolder;
        e29::commands::list_assets_query_cmd             CmdListAssets;
        e29::commands::describe_asset_query_cmd          CmdDescribeAsset;
        e29::commands::rename_asset_cmd                  CmdRenameAsset;
        e29::commands::move_asset_cmd                    CmdMoveAsset;
        e29::commands::delete_asset_cmd                  CmdDeleteAsset;
        e29::commands::restore_asset_cmd                 CmdRestoreAsset;
        e29::commands::create_asset_cmd                  CmdCreateAsset;
        e29::commands::save_assets_query_cmd             CmdSaveAssets;
        e29::commands::add_script_source_file_cmd        CmdAddScriptSourceFile;
        e29::commands::remove_script_source_file_cmd     CmdRemoveScriptSourceFile;
        e29::commands::list_script_source_files_query_cmd CmdListScriptSourceFiles;
        e29::commands::add_project_module_reference_cmd  CmdAddProjectModuleReference;
        e29::commands::remove_project_module_reference_cmd CmdRemoveProjectModuleReference;
        e29::commands::list_project_module_references_query_cmd CmdListProjectModuleReferences;
        e29::commands::set_script_source_file_content_cmd CmdSetScriptSourceFileContent;
        e29::commands::rename_script_source_file_cmd     CmdRenameScriptSourceFile;
        e29::commands::regenerate_project_module_sources_query_cmd CmdRegenerateProjectModuleSources;
        e29::commands::rename_asset_file_cmd             CmdRenameAssetFile;
        e29::commands::move_asset_file_cmd               CmdMoveAssetFile;
        e29::commands::delete_asset_file_cmd             CmdDeleteAssetFile;
        e29::commands::restore_asset_file_cmd            CmdRestoreAssetFile;
        e29::commands::copy_asset_file_cmd               CmdCopyAssetFile;
        e29::commands::make_prefab_cmd                   CmdMakePrefab;
        e29::commands::make_prefab_variant_cmd           CmdMakePrefabVariant;
        e29::commands::recompile_all_query_cmd           CmdRecompileAll;
        e29::commands::recompile_errors_query_cmd        CmdRecompileErrors;
        e29::commands::compile_start_query_cmd           CmdCompileStart;
        e29::commands::compile_pause_query_cmd           CmdCompilePause;
        e29::commands::compile_auto_query_cmd            CmdCompileAuto;
        e29::commands::compile_status_query_cmd          CmdCompileStatus;
        e29::commands::run_sanity_check_query_cmd        CmdRunSanityCheck;
        e29::commands::source_control_status_query_cmd   CmdSourceControlStatus;
        e29::commands::source_control_depot_status_query_cmd CmdSourceControlDepotStatus;
        e29::commands::source_control_refresh_query_cmd  CmdSourceControlRefresh;
        e29::commands::source_control_list_locks_query_cmd CmdSourceControlListLocks;
        e29::commands::source_control_lock_query_cmd     CmdSourceControlLock;
        e29::commands::source_control_unlock_query_cmd   CmdSourceControlUnlock;
        e29::commands::source_control_revert_query_cmd   CmdSourceControlRevert;
        e29::commands::source_control_stage_query_cmd    CmdSourceControlStage;
        e29::commands::source_control_commit_query_cmd   CmdSourceControlCommit;
        e29::commands::source_control_pull_query_cmd     CmdSourceControlPull;
        e29::commands::source_control_push_query_cmd     CmdSourceControlPush;

        command_set(xundo::system& Workspace, xundo::system& Level, void* pContext) noexcept
        : CmdOpenTextureEditor(Workspace, pContext)
        , CmdTextureEditorCommand(Workspace, pContext)
        , CmdSelect(Level, pContext)
        , CmdToggleMultiSelect(Level, pContext)
        , CmdClearSelection(Level, pContext)
        , CmdSetProperty(Level, pContext)
        , CmdRevertOverride(Level, pContext)
        , CmdApplyOverrides(Level, pContext)
        , CmdRevertHierarchyOverrides(Level, pContext)
        , CmdRevertAllOverrides(Level, pContext)
        , CmdAddComponent(Level, pContext)
        , CmdRemoveComponent(Level, pContext)
        , CmdCreateEntity(Level, pContext)
        , CmdDeleteEntity(Level, pContext)
        , CmdSay(Workspace, pContext)
        , CmdGetLog(Workspace, pContext)
        , CmdOpenLevel(Workspace, pContext)
        , CmdCloseScene(Level, pContext)
        , CmdAddScene(Level, pContext)
        , CmdRemoveScene(Level, pContext)
        , CmdAddSceneDependency(Level, pContext)
        , CmdRemoveSceneDependency(Level, pContext)
        , CmdAddLibraryDependency(Level, pContext)
        , CmdRemoveLibraryDependency(Level, pContext)
        , CmdCreateLibrary(Workspace, pContext)
        , CmdListLegalReferenceLibraries(Workspace, pContext)
        , CmdListLevels(Workspace, pContext)
        , CmdListScenes(Level, pContext)
        , CmdListEntities(Level, pContext)
        , CmdListFolders(Level, pContext)
        , CmdAuditComponentUsage(Level, pContext)
        , CmdUndo(Workspace, pContext)
        , CmdRedo(Workspace, pContext)
        , CmdLevelUndo(Level, pContext)
        , CmdLevelRedo(Level, pContext)
        , CmdSave(Workspace, pContext)
        , CmdClose(Workspace, pContext)
        , CmdSerializeRoundtrip(Level, pContext)
        , CmdDescribeEntity(Level, pContext)
        , CmdListComponentTypes(Level, pContext)
        , CmdSetEntityReference(Level, pContext)
        , CmdPlay(Workspace, pContext)
        , CmdPause(Workspace, pContext)
        , CmdStep(Workspace, pContext)
        , CmdStop(Workspace, pContext)
        , CmdGetPlayState(Workspace, pContext)
        , CmdInstantiatePrefab(Level, pContext)
        , CmdMoveToFolder(Level, pContext)
        , CmdCreateFolder(Level, pContext)
        , CmdDeleteFolder(Level, pContext)
        , CmdListAssets(Workspace, pContext)
        , CmdDescribeAsset(Workspace, pContext)
        , CmdRenameAsset(Workspace, pContext)
        , CmdMoveAsset(Workspace, pContext)
        , CmdDeleteAsset(Workspace, pContext)
        , CmdRestoreAsset(Workspace, pContext)
        , CmdCreateAsset(Workspace, pContext)
        , CmdSaveAssets(Workspace, pContext)
        , CmdAddScriptSourceFile(Workspace, pContext)
        , CmdRemoveScriptSourceFile(Workspace, pContext)
        , CmdListScriptSourceFiles(Workspace, pContext)
        , CmdAddProjectModuleReference(Workspace, pContext)
        , CmdRemoveProjectModuleReference(Workspace, pContext)
        , CmdListProjectModuleReferences(Workspace, pContext)
        , CmdSetScriptSourceFileContent(Workspace, pContext)
        , CmdRenameScriptSourceFile(Workspace, pContext)
        , CmdRegenerateProjectModuleSources(Workspace, pContext)
        , CmdRenameAssetFile(Workspace, pContext)
        , CmdMoveAssetFile(Workspace, pContext)
        , CmdDeleteAssetFile(Workspace, pContext)
        , CmdRestoreAssetFile(Workspace, pContext)
        , CmdCopyAssetFile(Workspace, pContext)
        , CmdMakePrefab(Level, pContext)
        , CmdMakePrefabVariant(Level, pContext)
        , CmdRecompileAll(Workspace, pContext)
        , CmdRecompileErrors(Workspace, pContext)
        , CmdCompileStart(Workspace, pContext)
        , CmdCompilePause(Workspace, pContext)
        , CmdCompileAuto(Workspace, pContext)
        , CmdCompileStatus(Workspace, pContext)
        , CmdRunSanityCheck(Workspace, pContext)
        , CmdSourceControlStatus(Workspace, pContext)
        , CmdSourceControlDepotStatus(Workspace, pContext)
        , CmdSourceControlRefresh(Workspace, pContext)
        , CmdSourceControlListLocks(Workspace, pContext)
        , CmdSourceControlLock(Workspace, pContext)
        , CmdSourceControlUnlock(Workspace, pContext)
        , CmdSourceControlRevert(Workspace, pContext)
        , CmdSourceControlStage(Workspace, pContext)
        , CmdSourceControlCommit(Workspace, pContext)
        , CmdSourceControlPull(Workspace, pContext)
        , CmdSourceControlPush(Workspace, pContext)
        {}
    };
}

#endif // E29_COMMAND_SET_H
