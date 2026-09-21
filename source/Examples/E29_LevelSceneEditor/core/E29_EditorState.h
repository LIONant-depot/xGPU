#pragma once

// editor_state: what is open, selected and playing.
// Split out of E29_LevelSceneEditorKit.h; included from there at the position this code used to occupy.
namespace e29
{
    //---------------------------------------------------------------------------
    // Editor state - which Level/Scene (if any) is currently open, and which entity (if any) is
    // currently selected for component editing. Level/Scene instances themselves live inside
    // GameMgr.m_LevelMgr/m_SceneMgr - this just remembers which guid to Find() each frame.
    //---------------------------------------------------------------------------

    struct editor_state
    {
        xecs::level::guid   m_CurrentLevel  = {};

        // Every Scene the user has opened stays resident/expanded until removed from the Level -
        // opening one no longer closes any other (previously OpenScene force-closed "the" current
        // scene first; the user explicitly wants all of them open at once if they choose to).
        std::vector<xecs::scene::guid>  m_OpenScenes;

        xecs::scene::permanent_id  m_SelectedEntityId    = xecs::scene::invalid_permanent_id_v;
        xecs::component::entity    m_SelectedEntity      = {};
        xecs::scene::guid          m_SelectedEntityScene = {}; // which OPEN scene m_SelectedEntity belongs to

        bool m_bEntityInspectorDirty = true;

        std::string m_TreeSearchString;

        // Entity Properties panel's own category filter bar (E29_Panel_EntityProperties.h) - empty
        // means "All" (no filter, every attached component shown). Scoped here, not a local static,
        // matching m_TreeSearchString's own convention for per-panel UI state that should survive
        // across frames/selection changes.
        std::string m_ComponentCategoryFilter;

        // Add Component popup (E29_Panel_ComponentSelector.h) - search string for filtering
        // the component list. Same convention as m_TreeSearchString/m_ComponentCategoryFilter.
        std::string m_ComponentSelectorSearchString;

        // Add Component popup - per-category open/closed state (persists across frames).
        // Key = category name (empty string for "Uncategorized"), value = true if expanded.
        std::unordered_map<std::string, bool> m_ComponentSelectorCategoryOpen;

        // Ctrl-click toggle set, separate from the "primary" selection triad above (which still only
        // ever drives the Properties panel - ctrl-clicking never touches it). Only meaningful for
        // "Make Prefab" acting on a group; scoped to ONE scene at a time (a prefab's members must all
        // come from the same live scene to walk children/read components together) - a plain click
        // (no modifier) anywhere clears this set, matching common editor convention.
        std::unordered_set<xecs::scene::permanent_id>  m_MultiSelectedEntityIds;
        // Same membership as m_MultiSelectedEntityIds, but in actual click order (a plain unordered_set
        // has no defined iteration order at all - not even insertion order) - DetermineGroupRoot uses
        // this to find "the first entity the user actually selected", e.g. to inherit ITS folder/parent
        // for a synthetic group root, per direct user request. Kept in lockstep with
        // m_MultiSelectedEntityIds at every mutation site rather than derived from it.
        std::vector<xecs::scene::permanent_id>         m_MultiSelectOrder;
        xecs::scene::guid                              m_MultiSelectScene;

        // Stopped: editing normally, GameMgr.Run() never called. Playing: ticking every frame.
        // Paused: a live play session (world stays exactly as it is, Stop will still revert it) but
        // GameMgr.Run() is NOT called this frame - matches Unity's own Play/Pause/Stop transport,
        // and is also what lets a code-edit reload happen "at rest" mid-session without losing
        // anything (see E29_GamePlugin.h's PollGameReload, which treats Playing and Paused
        // identically - both are "a play session is live").
        enum class play_state : std::uint8_t { Stopped, Playing, Paused };

        // See RenderSystemRegistryPanel's own comment for why this matters beyond just "is the game
        // ticking": it's also what that panel checks to decide whether an enable/reorder edit is a
        // permanent, persisted change or a transient one that GameMgr.Stop()'s own
        // xecs::system::mgr::RestoreFromSnapshot() will discard.
        play_state m_PlayState = play_state::Stopped;
        bool isPlaying() const noexcept { return m_PlayState != play_state::Stopped; }

        // Set by the toolbar's "Step" button, direct user request to mirror Unity's own frame-advance
        // control - consumed at the SAME tick gate that calls GameMgr.Run() (E29_LevelScene_Editor.cpp,
        // "if (m_PlayState == Playing) Run()"), regardless of how Playing was reached (the immediate
        // path or the XECS_BUILD_SHARED async reload path via m_bPlayRequested above) - the flag just
        // waits until the NEXT real tick happens, whenever that is, then consumes itself. From Stopped,
        // Step starts play normally and this makes that very first tick immediately re-pause; from
        // Paused, the tick gate runs ONE tick without ever leaving Paused (this flag is the only thing
        // that authorizes Run() while Paused).
        bool m_bStepOneFrame = false;

        // Set by the "Play" button (Stopped -> Playing only - Paused -> Playing is just a resume,
        // no recompile-check needed) and consumed by PollGameReload once the recompile-check it
        // kicks off resolves - Play must never actually start ticking against a DLL that might still
        // be mid-rebuild. See PollGameReload's own comment for the full sequencing.
        bool m_bLevelEditorOpen = true; // peer Level root tab (Texture-shaped close)
        bool m_bPlayBusyPopup = false; // DESIGN 4.6 fail-loud when Play singleton busy
        bool m_bPlayRequested = false;

        // Set by the "Stop" button; consumed at the same clean top-of-frame point PollGameReload
        // runs from, never at the point of the click itself - see the button's own comment in
        // E29_LevelScene_Editor.cpp for why (StopPlaySession does the same heavy destroy/recreate
        // work a reload does, and the click happens nested inside an active ImGui menu-bar scope).
        bool m_bStopRequested = false;

        // Undo.GetUndoIndex() at the exact moment Play actually starts (PollGameReload, right where
        // it writes V1 via SaveEverything) - direct user request: property edits made while Playing
        // (a value tweaked in the Inspector while watching it react) are automatically carried back
        // into the persistent scene on Stop, same reasoning as Unreal's own "Keep Simulation Changes"
        // (researched against Unity/Godot too - neither has a first-party equivalent; see
        // documentation/E29_LevelSceneEditor/playmode_keep_property_tweaks.md for the full design). StopPlaySession reads
        // every SetProperty entry pushed since this index, replays the deduped result as brand-new
        // SetProperty commands against the just-restored (V1/disk) scene, then discards the rest of
        // the play session's history - so Ctrl+Z after Stop never lands on a stale, play-session-only
        // undo entry.
        int m_PlayHistoryBoundary = 0;

        // The exact "SetProperty ..." command strings StopPlaySession will replay if the answer to
        // "keep these Play-mode tweaks?" is yes - populated by RequestStop() (E29_PlaySession.h)
        // either right away (an explicit -Keep on the CLI Stop, or the modal's own Keep/Discard
        // button) or left pending for m_bAwaitingKeepTweaksAnswer's modal to resolve. Cleared once
        // consumed by the deferred Stop itself.
        std::vector<std::string> m_PendingKeepTweaksCommands;

        // True while "You changed N properties while Playing - keep them?" is open and the real Stop
        // is on hold (world frozen via Paused so nothing else can happen mid-question) - direct user
        // request: a lightweight confirmation rather than silently always-keep or a full per-entity/
        // per-property picker (deferred - "we can always add that later, the core system is in place
        // now"). Never set at all when there's nothing to ask about (RequestStop stops right there).
        bool m_bAwaitingKeepTweaksAnswer = false;

        // Undo.GetUndoIndex() at last Save / successful Level open / Close. File>Save greys out
        // when equal; Close / open-another-Level prompt when unequal (HasUnsavedDocumentChanges).
        int m_CleanUndoIndex = 0;

        // Save-before-Close / Save-before-OpenLevel modal (RenderSaveBeforeCloseModal).
        bool                 m_bAwaitingSaveBeforeClose = false;
        // empty Instance+Type = Close only; otherwise open this Level after Save/Don't Save.
        xresource::full_guid m_PendingOpenLevelAfterClose = {};
        // Intent while the Save-before-close modal is open (or stored across Finish). NOT the
        // one-shot consume flag - that is m_bPendingStartGameReloadAfterOpen, set only after a
        // Level actually opened so the editor cannot StartGameReload before the user answers.
        bool                 m_bPendingOpenWantsGameReload = false;
        // One-shot: FinishPendingDocumentAction sets this after OpenLevel; editor frame consumes.
        bool                 m_bPendingStartGameReloadAfterOpen = false;
    };
}
