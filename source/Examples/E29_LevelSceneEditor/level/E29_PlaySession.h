#ifndef E29_PLAY_SESSION_H
#define E29_PLAY_SESSION_H
#pragma once

// Play: the transport state machine (Play, Pause, Step, Stop) and what Stop does with the property edits made while playing.
// The build the game code needs before Play starts is reached through play_gate.
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace e29
{
    // What Play needs from whatever builds the game's code: whether a build is running, and a way to start the
    // recompile-check that Play waits for. The game module provides one to the host; an editor without one enters Playing
    // right away.
    struct play_gate
    {
        std::function<bool()> m_IsBuilding;
        std::function<void()> m_StartBuild;
    };

    //---------------------------------------------------------------------------
    // The transport state machine. The menu-bar transport, the editor toolbar and the CLI commands all go
    // through these, so they cannot drift apart. The process-wide single-Play lock (xeditor::host) is
    // taken by RequestPlay and released only where a play session really ends: StopPlaySession, or
    // CancelPlayRequest when the build a pending Play was waiting for fails.
    //---------------------------------------------------------------------------

    // Stopped/Paused -> Playing. Writes V1 (the real disk save Stop restores from - it must be disk, not the
    // fast binary Vn bridge, because Stop needs the Level tree back) and marks the undo point Stop rewinds to.
    inline void EnterPlaying( editor_context& Ed ) noexcept
    {
        SaveEverything(Ed.World(), Ed.State());
        Ed.State().m_PlayHistoryBoundary = Ed.m_Undo.GetUndoIndex();
        Ed.State().m_PlayState           = editor_state::play_state::Playing;
    }

    // A pending Play that will never start (its build failed): drop it and release the Play lock it took.
    inline void CancelPlayRequest( editor_state& State ) noexcept
    {
        if (!State.m_bPlayRequested) return;
        State.m_bPlayRequested = false;
        State.m_bStepOneFrame  = false;
        xeditor::host::current()->end_play(&State);
    }

    inline void RequestResume( editor_state& State ) noexcept
    {
        if (State.m_PlayState == editor_state::play_state::Paused) State.m_PlayState = editor_state::play_state::Playing;
    }

    // From Stopped: recompile-check first (shared builds; PollGameReload then calls EnterPlaying) or enter
    // Playing directly. From Paused: resume. Returns a short status for the CLI; the buttons ignore it.
    inline std::string RequestPlay( editor_context& Ed ) noexcept
    {
        auto& State = Ed.State();
        using play_state = editor_state::play_state;
        if (State.m_PlayState == play_state::Playing) return "Play: already playing";
        auto* pGate = xeditor::host::current()->find<play_gate>();
        if (pGate && pGate->m_IsBuilding())          return "Play: a build is already in flight";
        if (State.m_PlayState == play_state::Paused)  { RequestResume(State); return "Resumed"; }

        auto& Host = *xeditor::host::current();
        if (!Host.try_begin_play(&State))
        {
            xeditor::diagnostics::Log("Play refused: another Play session is already active");
            State.m_bPlayBusyPopup = true;
            return "Play: another Play session is already active";
        }
        if (pGate)
        {
            State.m_bPlayRequested = true;
            pGate->m_StartBuild();
            return "Play requested (recompile-check in progress)";
        }
        EnterPlaying(Ed);
        return "Playing";
    }

    inline std::string RequestPause( editor_state& State ) noexcept
    {
        if (State.m_PlayState != editor_state::play_state::Playing) return "Pause: not playing";
        State.m_PlayState = editor_state::play_state::Paused;
        return "Paused";
    }

    // One frame. From Paused: one tick, stays Paused. From Stopped: starts Play, runs the first tick, lands Paused.
    inline std::string RequestStep( editor_context& Ed ) noexcept
    {
        auto& State = Ed.State();
        using play_state = editor_state::play_state;
        if (State.m_PlayState == play_state::Playing) return "Step: pause first";
        if (State.m_PlayState == play_state::Stopped)
        {
            const std::string Result = RequestPlay(Ed);
            if (State.m_PlayState == play_state::Stopped && !State.m_bPlayRequested) return Result; // refused
        }
        State.m_bStepOneFrame = true;
        return "Step";
    }

    //---------------------------------------------------------------------------
    // Splits one flat "-Key value -Key2 value2 ..." command string (the exact shape every command in
    // this system's own std::format calls already produces - no embedded spaces, every value here is
    // hex or Base64) into a name->value map. A throwaway xcmdline::parser/command_base could do this
    // too, but that machinery exists to VALIDATE input as it's typed; this is just reading text this
    // same codebase already wrote, so a plain split is enough. Token[0] (the command name itself,
    // e.g. "SetProperty") is skipped - callers that need it already filtered on it before calling this.
    //---------------------------------------------------------------------------
    inline std::unordered_map<std::string, std::string> ParseFlatArgs(const std::string& CmdStr) noexcept
    {
        std::unordered_map<std::string, std::string> Out;
        std::istringstream Stream(CmdStr);
        const std::vector<std::string> Tokens{ std::istream_iterator<std::string>(Stream), std::istream_iterator<std::string>() };
        for (std::size_t i = 1; i + 1 < Tokens.size(); i += 2)
        {
            if (Tokens[i].empty() || Tokens[i][0] != '-') continue;
            Out[Tokens[i].substr(1)] = Tokens[i + 1];
        }
        return Out;
    }

    // One property, on one entity, that changed at least once while Playing - After is whatever it
    // was left at when Stop was pressed (the LAST SetProperty seen for this exact Scene/Id/Component/
    // Path); Before is the value it had the FIRST time it changed during this play session, which is
    // exactly the value V1 already captured at Play-entry (Play always writes V1 before anything can
    // change) - reusing it here means Undo-ing this "keep" back to what it truly was pre-Play needs no
    // separate live read of the just-restored entity.
    struct kept_property_tweak
    {
        std::string m_Scene, m_Id, m_Component, m_Path, m_TypeGuid, m_Before, m_After;
    };

    //---------------------------------------------------------------------------
    // Walks every history entry pushed since Play started (State.m_PlayHistoryBoundary), keeping only
    // SetProperty entries - direct user request/scope decision: only plain property edits are ever
    // carried back into the persistent scene, never anything structural (CreateEntity/AddComponent/
    // etc, if they somehow also happened while Playing) - matches Unreal's own "Keep Simulation
    // Changes", which is similarly restricted to actors that already existed before simulating (see
    // documentation/E29_LevelSceneEditor/playmode_keep_property_tweaks.md for the full research/design). Deduplicated by
    // (Scene, Id, Component, Path) - a property dragged back and forth several times during one play
    // session collapses to a single entry, not a replay of every intermediate value.
    //---------------------------------------------------------------------------
    inline std::vector<kept_property_tweak> CollectPlayModePropertyTweaks(xundo::system& Undo, int BoundaryIndex) noexcept
    {
        std::vector<kept_property_tweak> Tweaks;
        std::unordered_map<std::string, std::size_t> KeyToIndex;

        const auto Count = static_cast<int>(Undo.GetHistoryCount());
        for (int i = BoundaryIndex; i < Count; ++i)
        {
            const std::string& Cmd = Undo.GetHistoryCommandString(static_cast<std::size_t>(i));
            if (Cmd.compare(0, 12, "SetProperty ") != 0) continue;

            const auto Args = ParseFlatArgs(Cmd);
            const auto ItScene = Args.find("Scene");     if (ItScene == Args.end()) continue;
            const auto ItId    = Args.find("Id");        if (ItId    == Args.end()) continue;
            const auto ItComp  = Args.find("Component"); if (ItComp  == Args.end()) continue;
            const auto ItPath  = Args.find("Path");      if (ItPath  == Args.end()) continue;
            const auto ItType  = Args.find("TypeGuid");  if (ItType  == Args.end()) continue;
            const auto ItAfter = Args.find("After");     if (ItAfter == Args.end()) continue;
            const auto ItBefore = Args.find("Before");   if (ItBefore == Args.end()) continue;

            const std::string Key = ItScene->second + '|' + ItId->second + '|' + ItComp->second + '|' + ItPath->second;
            if (auto KIt = KeyToIndex.find(Key); KIt != KeyToIndex.end())
            {
                Tweaks[KIt->second].m_After = ItAfter->second; // later entries win - keep the LAST value
            }
            else
            {
                KeyToIndex[Key] = Tweaks.size();
                Tweaks.push_back({ ItScene->second, ItId->second, ItComp->second, ItPath->second, ItType->second, ItBefore->second, ItAfter->second });
            }
        }
        return Tweaks;
    }

    //---------------------------------------------------------------------------
    // Formats each collected tweak into the exact "SetProperty ..." command string StopPlaySession
    // will later Run() if the answer to "keep these?" turns out to be yes - done up front (rather than
    // carrying the kept_property_tweak structs themselves all the way to Stop time) so
    // editor_state::m_PendingKeepTweaksCommands (E29_LevelSceneEditorKit.h, needed by the confirmation
    // modal below - which is declared and used well before this file's own types would otherwise be
    // visible there) can just be a plain std::vector<std::string>, no new type dependency.
    //---------------------------------------------------------------------------
    inline std::vector<std::string> CollectPlayModeKeepCommands(xundo::system& Undo, int BoundaryIndex) noexcept
    {
        std::vector<std::string> Out;
        for (auto& T : CollectPlayModePropertyTweaks(Undo, BoundaryIndex))
        {
            Out.push_back(std::format("SetProperty -Scene {} -Id {} -Component {} -Path {} -TypeGuid {} -Before {} -After {}"
                , T.m_Scene, T.m_Id, T.m_Component, T.m_Path, T.m_TypeGuid, T.m_Before, T.m_After
                ));
        }
        return Out;
    }

    //---------------------------------------------------------------------------
    // Drops any kept-tweak command whose target entity did NOT survive the V1 restore (created only
    // during Play - the one case this feature was deliberately never meant to touch, matching
    // Unreal's own "actors already in the level" restriction). Filtering here, BEFORE the group runs,
    // rather than letting a stale target fail inside it matters because xundo's own
    // Execute(group_name, Cmds) (xundo_system.h) aborts the ENTIRE group - and pushes NO history entry
    // at all - the moment any one sub-command's Redo() fails. Without this filter, one stale target
    // among several legitimate kept properties would silently swallow every other one alongside it
    // AND leave the ones that already ran for real un-recorded (mutated, but with no undo entry to
    // revert them).
    //---------------------------------------------------------------------------
    inline std::vector<std::string> FilterSurvivingTargets(xecs::game_mgr::instance& GameMgr, const std::vector<std::string>& Commands) noexcept
    {
        std::vector<std::string> Out;
        Out.reserve(Commands.size());
        for (auto& Cmd : Commands)
        {
            const auto Args = ParseFlatArgs(Cmd);
            const auto ItScene = Args.find("Scene"); if (ItScene == Args.end()) continue;
            const auto ItId    = Args.find("Id");    if (ItId    == Args.end()) continue;

            const auto SceneGuid = xscene::commands::ParseSceneGuid(ItScene->second);
            const auto Id        = xscene::commands::ParseEntityId(ItId->second);
            auto* pScene = GameMgr.m_SceneMgr.Find(SceneGuid);
            if (pScene && pScene->m_LocalToRuntime.contains(Id))
                Out.push_back(Cmd);
        }
        return Out;
    }

    //---------------------------------------------------------------------------
    // The single decision point for BOTH real Stop triggers (the menu-bar button, via
    // std::nullopt - it never knows the answer up front; the CLI Stop command, via its own -Keep
    // argument when given) - direct user request for a lightweight confirmation rather than either
    // silent always-keep or a full per-entity/per-property picker ("we do not need Unreal's per-entity
    // or per-property selection... we can always add that later, the core system is in place now").
    //
    // KeepOverride already decided (an explicit -Keep, or the confirmation modal's own Keep/Discard
    // button): resolves m_PendingKeepTweaksCommands right now and flags the real (deferred)
    // StopPlaySession to run next frame. KeepOverride absent: finds out whether there's even anything
    // to ask about; if not, Stops immediately same as always; if so, stages the pending commands,
    // freezes the world (Paused - nothing else should happen mid-question) and sets
    // m_bAwaitingKeepTweaksAnswer so RenderKeepTweaksModal (below) opens the dialog on the very next
    // frame - the real Stop stays on hold until that dialog (or a script's own follow-up -Keep call)
    // answers it. Returns a short status string - useful for a CLI/AI caller, ignored by the button.
    //---------------------------------------------------------------------------
    inline std::string RequestStop(editor_context& Ed, std::optional<bool> KeepOverride) noexcept
    {
        auto& State = Ed.State();
        if (State.m_PlayState == editor_state::play_state::Stopped) return "Stop: already stopped";

        if (KeepOverride.has_value())
        {
            State.m_PendingKeepTweaksCommands = *KeepOverride ? CollectPlayModeKeepCommands(Ed.m_Undo, State.m_PlayHistoryBoundary) : std::vector<std::string>{};
            State.m_bAwaitingKeepTweaksAnswer = false;
            State.m_bStopRequested = true;
            return "Stop requested";
        }

        auto Pending = CollectPlayModeKeepCommands(Ed.m_Undo, State.m_PlayHistoryBoundary);
        if (Pending.empty())
        {
            State.m_PendingKeepTweaksCommands.clear();
            State.m_bStopRequested = true;
            return "Stop requested";
        }

        State.m_PendingKeepTweaksCommands = std::move(Pending);
        State.m_bAwaitingKeepTweaksAnswer = true;
        State.m_PlayState = editor_state::play_state::Paused;
        return std::format("Stop: {} propert{} changed during Play - pass -Keep true|false, or answer the confirmation dialog"
            , State.m_PendingKeepTweaksCommands.size(), State.m_PendingKeepTweaksCommands.size() == 1 ? "y" : "ies");
    }

    //---------------------------------------------------------------------------
    // "You changed N properties while Playing - keep them?" - renders every frame regardless (same
    // ImGui::OpenPopup/BeginPopupModal-every-frame convention the Asset Tree's own
    // RenderPendingConfirmationModal already established, E10_asset_browser_files_tab.h), so the
    // dialog keeps showing across frames until answered, including one opened by a CLI Stop call with
    // no -Keep (RequestStop sets the exact same m_bAwaitingKeepTweaksAnswer flag either way). Only
    // decides Keep-vs-Discard here; the real Stop itself still runs at the usual deferred, safe frame
    // boundary (RequestStop just re-flags m_bStopRequested).
    //---------------------------------------------------------------------------
    inline void RenderKeepTweaksModal(editor_context& Ed) noexcept
    {
        auto& State = Ed.State();
        if (State.m_bAwaitingKeepTweaksAnswer)
            ImGui::OpenPopup("Keep Play Mode Changes?");

        if (ImGui::BeginPopupModal("Keep Play Mode Changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const auto Count = State.m_PendingKeepTweaksCommands.size();
            ImGui::Text("You changed %zu propert%s while Playing.", Count, Count == 1 ? "y" : "ies");
            ImGui::TextWrapped("Keep them in the scene, or discard and revert to how it was before Play?");
            ImGui::Separator();

            if (ImGui::Button("Keep", ImVec2(120.0f, 0.0f)))
            {
                RequestStop(Ed, true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard", ImVec2(120.0f, 0.0f)))
            {
                RequestStop(Ed, false);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
}

#endif // E29_PLAY_SESSION_H
