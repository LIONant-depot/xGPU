#pragma once

// editor_state: the scene state plus what is specific to a Level editor: which Level is open, whether it is playing, and
// where its document stands (saved or not).
namespace e29
{
    struct editor_state : scene_state
    {
        xecs::level::guid   m_CurrentLevel  = {};

        std::string m_TreeSearchString;

        // Stopped: editing normally, the world is never ticked. Playing: ticking every frame. Paused: a live play session
        // (the world stays exactly as it is and Stop still reverts it) that is not ticked this frame, which is also what lets
        // a code-edit reload happen "at rest" mid-session (PollGameReload treats Playing and Paused alike).
        enum class play_state : std::uint8_t { Stopped, Playing, Paused };

        // Also what the System Registry panel checks to tell a permanent, persisted edit from a transient one that Stop
        // will discard.
        play_state m_PlayState = play_state::Stopped;
        bool isPlaying() const noexcept { return m_PlayState != play_state::Stopped; }

        // Set by the toolbar's Step button and consumed at the tick gate that runs the world, whichever way Playing was
        // reached: it waits for the next real tick. From Stopped, Step starts play and that first tick immediately
        // re-pauses; from Paused, the gate runs ONE tick without leaving Paused (this flag alone authorizes a tick then).
        bool m_bStepOneFrame = false;

        bool m_bLevelEditorOpen = true;   // peer Level root tab (Texture-shaped close)
        bool m_bPlayBusyPopup   = false;  // shown when another Play session is already active
        // Set by Play (Stopped -> Playing only; Paused -> Playing is a plain resume) and consumed by PollGameReload once the
        // recompile-check it starts resolves: Play must never tick against a DLL that might still be rebuilding.
        bool m_bPlayRequested = false;

        // Set by Stop and consumed at the same clean top-of-frame point as PollGameReload, never at the click itself:
        // stopping destroys and recreates the world, and the click happens inside an active ImGui menu-bar scope.
        bool m_bStopRequested = false;

        // The undo index at the moment Play starts. Property edits made while Playing are carried back into the persistent
        // scene on Stop (see RequestStop): it replays every SetProperty pushed since this index against the restored scene
        // and discards the rest of the play session's history.
        int m_PlayHistoryBoundary = 0;

        // The SetProperty commands Stop will replay if the answer to "keep these Play-mode tweaks?" is yes: filled by
        // RequestStop right away (an explicit -Keep, or the modal's button) or left pending for the modal, and cleared once
        // the deferred Stop consumes them.
        std::vector<std::string> m_PendingKeepTweaksCommands;

        // True while "keep them?" is open and the real Stop is on hold, with the world frozen (Paused) so nothing else can
        // happen mid-question. Never set when there is nothing to ask about.
        bool m_bAwaitingKeepTweaksAnswer = false;

        // The undo index at the last Save, successful Level open or Close. File>Save greys out when equal; Close and
        // opening another Level prompt when unequal (HasUnsavedDocumentChanges).
        int m_CleanUndoIndex = 0;

        // The save-before-Close / save-before-OpenLevel modal (RenderSaveBeforeCloseModal).
        bool                 m_bAwaitingSaveBeforeClose = false;
        // Empty = Close only; otherwise open this Level after Save / Don't Save.
        xresource::full_guid m_PendingOpenLevelAfterClose = {};
        // The intent while that modal is open. NOT the one-shot consume flag below, so the editor cannot start a game reload
        // before the person has answered.
        bool                 m_bPendingOpenWantsGameReload = false;
        // One-shot: set by FinishPendingDocumentAction after a Level really opened; the editor frame consumes it.
        bool                 m_bPendingStartGameReloadAfterOpen = false;
    };

    // A Level editor's context: the scene context plus access to its Level state. The scene code only ever sees the base.
    struct editor_context : scene_context
    {
        editor_context(editor_state& State, std::unique_ptr<xecs::game_mgr::instance>& pWorld, xundo::system& Undo) noexcept
            : scene_context{ State, pWorld, Undo } {}

        editor_state& State() noexcept { return static_cast<editor_state&>(m_State); }
    };

    inline editor_context* FindEditorContext() noexcept
    {
        auto* pHost = xeditor::host::current();
        return pHost ? pHost->find<editor_context>() : nullptr;
    }
}
