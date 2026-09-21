#pragma once

// The 'create entity / folder' menu items shared by the Level tree's context menus.
// Split out of E29_LevelSceneEditorKit.h; included from there at the position this code used to occupy.
namespace e29
{
    // Shared "New Entity"/"New Folder" menu content, landing directly under TargetFolder (invalid =
    // loose at scene root) - used by
    // BOTH the Scene row's and the Folder row's own right-click context menu. A separate toolbar "+"
    // with a persistent "which row is the target" selection was tried first and dropped per direct
    // user feedback once right-click-in-place existed - it made the "+" redundant.
    // Assumes it's called from inside an already-open popup (BeginPopupContextItem/BeginPopup).
    //
    // "New Entity" routed through the command/undo system (documentation/E29_LevelSceneEditor/command_undo_system_plan.md,
    // phase 4 - scene/commands/E29_Commands_EntityLifecycle.h) - create_entity_cmd::Redo does the exact
    // migration this used to do inline, Undo deletes it again. "New Folder" now routed too (external
    // review flagged it as the one glaring inconsistency left next to CreateEntity/DeleteEntity sitting
    // right beside it in this same menu) - CreateFolder/DeleteFolder commands, commands/
    // E29_Commands_SceneOrganization.h. Moved here (was originally much earlier in this file) since
    // this routing needs both e29::g_pGameMgr/g_pState (just declared, E29_PrefabAuthoring.h above) and
    // xeditor::Run (just included above) - neither was available at the function's original
    // position.
    void ShowCreateMenuItems(xecs::scene::guid SceneGuid, xecs::scene::instance& Scene, xecs::scene::folder_id TargetFolder, xundo::system& Undo) noexcept
    {
        if (ImGui::MenuItem("New Entity"))
        {
            const auto Id = NextFreeEntityId(Scene);
            xeditor::Run(e29::LevelDocUndo(), std::format("CreateEntity -Scene {} -Id {} -Folder {:08X}"
                , e29::commands::FormatSceneGuid(SceneGuid)
                , e29::commands::FormatEntityId(Id)
                , static_cast<std::uint32_t>(TargetFolder)
                ));
        }
        if (ImGui::MenuItem("New Folder"))
        {
            const auto Id = NextFreeFolderId(Scene);
            xeditor::Run(e29::LevelDocUndo(), std::format("CreateFolder -Scene {} -Id {:08X} -Parent {:08X} -Name {}"
                , e29::commands::FormatSceneGuid(SceneGuid)
                , static_cast<std::uint32_t>(Id)
                , static_cast<std::uint32_t>(TargetFolder)
                , xeditor::Base64Encode("New Folder")
                ));
        }
    }
}
