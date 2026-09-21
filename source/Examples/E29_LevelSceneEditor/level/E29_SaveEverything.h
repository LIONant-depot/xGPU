#pragma once

// Saving the open level, its scenes and the library.
// Split out of E29_LevelSceneEditorKit.h; included from there at the position this code used to occupy.
namespace e29
{
    // The single "Save" action - persists everything currently open (the Level's descriptor, the
    // open Scene's entities + its descriptor) plus the underlying project/library metadata, rather
    // than requiring separate Save Level/Save Scene actions the user has to remember to hit.
    void SaveEverything(xecs::game_mgr::instance& GameMgr, editor_state& State) noexcept
    {
        if (!State.m_CurrentLevel.empty() && GameMgr.m_LevelMgr.Find(State.m_CurrentLevel))
        {
            if (auto Err = GameMgr.m_LevelMgr.Save(State.m_CurrentLevel); Err)
                xeditor::NotifyError(std::format("Failed to save Level: {}", Err.getMessage()));
        }

        for (auto& SceneGuid : State.m_OpenScenes)
        {
            // Plain console log, NOT xeditor::NotifyError() - this is routine save progress (every normal save
            // has SOME pending changes, that's the whole point of saving), not a failure. Routing it
            // through xeditor::NotifyError() before this exact distinction existed meant an ordinary Save popped
            // an "Error" modal every time.
            if (auto* pScene = GameMgr.m_SceneMgr.Find(SceneGuid))
            {
                std::printf("[SaveEverything] scene has %zu pending entity change(s)\n", pScene->m_PendingChanges.size());
                std::fflush(stdout);
            }
            if (auto Err = GameMgr.m_SceneMgr.SaveScene(SceneGuid); Err)
                xeditor::NotifyError(std::format("Failed to save Scene: {}", Err.getMessage()));
        }

        xproperty::settings::context Context;
        e10::g_LibMgr.Save(Context);
    }
}
