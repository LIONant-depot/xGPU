#ifndef E29_DOCUMENT_SESSION_H
#define E29_DOCUMENT_SESSION_H
#pragma once

// Level document session: dirty tracking (undo watermark), File>Save enable, Close, and
// save-before-open when double-clicking / dropping another Level. Included from the kit umbrella
// after SaveEverything / OpenLevel / CloseScene are declared.

namespace e29
{
    //---------------------------------------------------------------------------
    inline void MarkDocumentClean(editor_state& State, xundo::system& Undo) noexcept
    {
        State.m_CleanUndoIndex = Undo.GetUndoIndex();
    }

    inline bool HasUnsavedDocumentChanges(const editor_state& State, const xundo::system& Undo) noexcept
    {
        if (State.m_CurrentLevel.empty()) return false;
        return Undo.GetUndoIndex() != State.m_CleanUndoIndex;
    }

    // Unload every open scene, clear CurrentLevel / selection, and Reset undo (old steps no longer
    // refer to a live document - see xundo::system::Reset).
    inline void CloseLevel(xecs::game_mgr::instance& GameMgr, editor_state& State, xundo::system& Undo) noexcept
    {
        if (State.m_CurrentLevel.empty() && State.m_OpenScenes.empty())
        {
            Undo.Reset();
            State.m_CleanUndoIndex = 0;
            return;
        }

        const auto Scenes = State.m_OpenScenes;
        for (const auto SceneGuid : Scenes)
            CloseScene(GameMgr, State, SceneGuid);

        State.m_CurrentLevel = {};
        State.m_SelectedEntityId    = xecs::scene::invalid_permanent_id_v;
        State.m_SelectedEntity      = {};
        State.m_SelectedEntityScene = {};
        State.m_MultiSelectedEntityIds.clear();
        State.m_MultiSelectOrder.clear();
        State.m_MultiSelectScene = {};

        // CLI Close (or any immediate close) cancels a pending File>Close / open-other prompt.
        State.m_bAwaitingSaveBeforeClose = false;
        State.m_PendingOpenLevelAfterClose = {};
        State.m_bPendingOpenWantsGameReload = false;
        State.m_bPendingStartGameReloadAfterOpen = false;

        Undo.Reset();
        State.m_CleanUndoIndex = 0;
    }

    // Applies Save/Don't Save resolution then Close and optional OpenLevel.
    inline void FinishPendingDocumentAction(xecs::game_mgr::instance& GameMgr, editor_state& State, xundo::system& Undo, bool bSaveFirst) noexcept
    {
        if (bSaveFirst)
        {
            SaveEverything(GameMgr, State);
            MarkDocumentClean(State, Undo);
        }

        const auto PendingOpen = State.m_PendingOpenLevelAfterClose;
        const bool bWantReload = State.m_bPendingOpenWantsGameReload;
        State.m_PendingOpenLevelAfterClose = {};
        State.m_bPendingOpenWantsGameReload = false;

        CloseLevel(GameMgr, State, Undo);

        if (!PendingOpen.empty() && PendingOpen.m_Type == xecs::level::type_guid_v)
        {
            OpenLevel(GameMgr, State, PendingOpen);
            MarkDocumentClean(State, Undo);
            // Caller / next-frame consumer starts GameReload when this stays true.
            State.m_bPendingStartGameReloadAfterOpen = bWantReload;
        }
        else
        {
            State.m_bPendingStartGameReloadAfterOpen = false;
        }
    }

    // Close current Level. If dirty, opens Save/Don't Save/Cancel modal.
    inline void RequestCloseLevel(xecs::game_mgr::instance& GameMgr, editor_state& State, xundo::system& Undo) noexcept
    {
        if (State.isPlaying()) return;
        if (State.m_CurrentLevel.empty() && State.m_OpenScenes.empty()) return;
        if (State.m_bAwaitingSaveBeforeClose) return;

        State.m_PendingOpenLevelAfterClose = {};
        State.m_bPendingOpenWantsGameReload = false;
        State.m_bPendingStartGameReloadAfterOpen = false;

        if (!HasUnsavedDocumentChanges(State, Undo))
        {
            CloseLevel(GameMgr, State, Undo);
            return;
        }

        State.m_bAwaitingSaveBeforeClose = true;
    }

    // Open a Level, running Close (with save prompt if dirty) first when another Level is loaded.
    // Returns true if OpenLevel ran immediately (caller may StartGameReload). false = no-op or
    // deferred to the modal (sets m_bPendingStartGameReloadAfterOpen when bStartGameReload).
    inline bool RequestOpenLevel(xecs::game_mgr::instance& GameMgr, editor_state& State, xundo::system& Undo, xresource::full_guid LevelGuid, bool bStartGameReload) noexcept
    {
        if (LevelGuid.m_Type != xecs::level::type_guid_v) return false;
        if (State.isPlaying()) return false;
        if (State.m_bAwaitingSaveBeforeClose) return false;

        const xecs::level::guid AsLevel{ .m_Instance = LevelGuid.m_Instance };
        if (!State.m_CurrentLevel.empty() && State.m_CurrentLevel.m_Instance == AsLevel.m_Instance)
            return false; // already the open Level

        if (State.m_CurrentLevel.empty() && State.m_OpenScenes.empty())
        {
            OpenLevel(GameMgr, State, LevelGuid);
            MarkDocumentClean(State, Undo);
            return bStartGameReload;
        }

        if (!HasUnsavedDocumentChanges(State, Undo))
        {
            CloseLevel(GameMgr, State, Undo);
            OpenLevel(GameMgr, State, LevelGuid);
            MarkDocumentClean(State, Undo);
            return bStartGameReload;
        }

        State.m_PendingOpenLevelAfterClose = LevelGuid;
        State.m_bPendingOpenWantsGameReload = bStartGameReload;
        State.m_bAwaitingSaveBeforeClose = true;
        return false;
    }

    // Same OpenPopup-every-frame convention as RenderKeepTweaksModal / RenderErrorPopup.
    inline void RenderSaveBeforeCloseModal(xecs::game_mgr::instance& GameMgr, editor_state& State, xundo::system& Undo) noexcept
    {
        if (State.m_bAwaitingSaveBeforeClose)
            ImGui::OpenPopup("Save changes?##E29Document");

        if (ImGui::BeginPopupModal("Save changes?##E29Document", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const bool bOpeningOther = !State.m_PendingOpenLevelAfterClose.empty();
            ImGui::TextUnformatted("The current Level has unsaved changes.");
            ImGui::TextWrapped(bOpeningOther
                ? "Save before opening the other Level?"
                : "Save before closing?");
            ImGui::Separator();

            if (ImGui::Button("Save", ImVec2(110.0f, 0.0f)))
            {
                State.m_bAwaitingSaveBeforeClose = false;
                FinishPendingDocumentAction(GameMgr, State, Undo, /*bSaveFirst*/ true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Don't Save", ImVec2(110.0f, 0.0f)))
            {
                State.m_bAwaitingSaveBeforeClose = false;
                FinishPendingDocumentAction(GameMgr, State, Undo, /*bSaveFirst*/ false);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f)))
            {
                State.m_bAwaitingSaveBeforeClose = false;
                State.m_PendingOpenLevelAfterClose = {};
                State.m_bPendingOpenWantsGameReload = false;
                State.m_bPendingStartGameReloadAfterOpen = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

} // namespace e29

#endif // E29_DOCUMENT_SESSION_H