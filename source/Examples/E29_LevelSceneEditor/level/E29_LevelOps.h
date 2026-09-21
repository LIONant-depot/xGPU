#pragma once

// The Level tree's icons and opening a Level.
namespace e29
{
    // Same icon glyphs the asset browser's own virtual folder tree uses for a folder-with-children
    // vs. an empty one (E10_asset_browser_virtual_tree_tab.h) - reused verbatim rather than picking new
    // ones, per direct user request. Both come from the icon font already loaded once, globally, for
    // the whole app (source/Tools/xgpu_imgui_breach.cpp) - E29 shares that same font atlas already.
    const char* FolderIcon(bool bHasChildren) noexcept
    {
        return bHasChildren ? "\xEE\xA3\x95" : "\xEE\xA2\xB7";
    }

    // Level/Scene row icons - direct user request. Segoe MDL2 Assets "Globe"/"Video" codepoints,
    // distinct from FolderIcon's own pair above and from every icon already used elsewhere in this
    // app (E10_AssetBrowser.h's tab icons) - picked and confirmed via a live screenshot, not inferred
    // from font metadata (see e10_asset_tree_polish_pass2 memory for why metadata alone isn't
    // trustworthy for this specific font).
    constexpr const char* LevelIcon() noexcept { return "\xEE\x9D\xB4"; }
    constexpr const char* SceneIcon() noexcept { return "\xEE\xA4\x9B"; }

    // Dependencies folder - Segoe MDL2 Assets "Link" (U+E71B). Same E7xx band as Search
    // (\xEE\x9C\xA1) / Refresh (\xEE\x9C\xAC), which already render in this atlas; deliberately
    // NOT FolderIcon so the synthesized Dependencies row can't be mistaken for a user folder.
    constexpr const char* DependenciesIcon() noexcept { return "\xEE\x9C\x9B"; }

    void OpenLevel(xecs::game_mgr::instance& GameMgr, editor_state& State, xresource::full_guid LevelGuid)
    {
        const xecs::level::guid Guid{ .m_Instance = LevelGuid.m_Instance };
        if (auto Err = GameMgr.m_LevelMgr.Load(Guid); Err)
        {
            xeditor::NotifyError(std::format("Failed to load Level: {}", Err.getMessage()));
            return;
        }
        State.m_CurrentLevel = Guid;
        State.m_bLevelEditorOpen = true;

        // Every scene that's part of a Level is loaded automatically the moment the Level itself
        // opens - direct user request ("Scenes should always be loaded if they are part of the
        // level") - rather than requiring a separate click-to-open per scene. Activate (already in
        // the engine, xecs::level::mgr::Activate - "for now, activating a level just means
        // requesting every scene it owns") does the actual RequestLoad cascade; this just also keeps
        // State.m_OpenScenes in sync so the Level tree's own bIsOpenScene checks reflect it.
        if (auto* pLevel = GameMgr.m_LevelMgr.Find(Guid))
        {
            if (auto Err = GameMgr.m_LevelMgr.Activate(Guid); Err)
            {
                xeditor::NotifyError(std::format("Failed to activate Level (load its scenes): {}", Err.getMessage()));
                return;
            }
            for (auto& SceneGuid : pLevel->m_Scenes)
                if (std::find(State.m_OpenScenes.begin(), State.m_OpenScenes.end(), SceneGuid) == State.m_OpenScenes.end())
                    State.m_OpenScenes.push_back(SceneGuid);
        }
    }

}
