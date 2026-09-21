#ifndef E29_COMMANDS_PLAYSESSION_H
#define E29_COMMANDS_PLAYSESSION_H
#pragma once

// Play/Pause/Stop/GetPlayState - added mid-session, direct user observation while watching this
// session drive the app via CLI: "I see you are using the mouse for play/pause/undo.... I think
// those should be query commands too... makes your life easier" (plus a follow-up: "you may also
// want to add a query to know what the current state is"). Every field these touch
// (State.m_PlayState/m_bPlayRequested/m_bStopRequested, GamePlugin.m_bBuilding) was already a plain
// flag the menu-bar buttons themselves just set and let the existing per-frame polling
// (PollGameReload/the deferred-Stop consumption, both in E29_LevelScene_Editor.cpp) pick up - these
// commands set the SAME flags rather than re-implementing any of that machinery, so a CLI/AI-driven
// Play/Stop behaves identically to a real click, transport recompile-check and all.
//
// Deliberately Query, not Edit - none of these mutate SCENE CONTENT the way CreateEntity/SetProperty
// etc. do (same reasoning OpenLevel/Undo/Redo/Save already established, E29_Commands_Workspace.h);
// undo-routing a transport-state change makes no sense.
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"

namespace e29::commands
{
    inline const char* PlayStateName(e29::editor_state::play_state S) noexcept
    {
        switch (S)
        {
        case e29::editor_state::play_state::Stopped: return "Stopped";
        case e29::editor_state::play_state::Playing: return "Playing";
        case e29::editor_state::play_state::Paused:  return "Paused";
        }
        return "Unknown";
    }

    //================================================================================================
    // Play - mirrors the menu-bar "Play"/"Resume" button exactly (E29_LevelScene_Editor.cpp): from
    // Stopped, kicks off the same recompile-check (StartGameReload, XECS_BUILD_SHARED builds) or the
    // same direct Save+enter-Playing fallback (non-shared builds); from Paused, just resumes. A no-op
    // (with an explanatory result, not silence) if already Playing or a build is already in flight -
    // same guard the button's own BeginDisabled already enforces.
    //================================================================================================
    struct play_query_cmd : scene_query_command
    {
        play_query_cmd(xundo::system& System, void* pDataBase) noexcept : scene_query_command(System, "Play", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Starts Play (from Stopped, recompile-checks first) or resumes it (from Paused). Usage: Play"; }
        void RegisterArguments() noexcept override {}
        std::string Query() noexcept override
        {
            if (!e29::g_pGamePlugin) return "Play: no game plugin state";
            return e29::RequestPlay(EditorContext(), *e29::g_pGamePlugin);
        }
    };

    //================================================================================================
    // Pause - mirrors the "Pause" button: only meaningful while actually Playing.
    //================================================================================================
    struct pause_query_cmd : scene_query_command
    {
        pause_query_cmd(xundo::system& System, void* pDataBase) noexcept : scene_query_command(System, "Pause", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Pauses a running Play session. Usage: Pause"; }
        void RegisterArguments() noexcept override {}
        std::string Query() noexcept override
        {
            return e29::RequestPause(State());
        }
    };

    //================================================================================================
    // Step - mirrors the toolbar "Step" button: runs exactly one frame. From Stopped it starts Play first
    // (same path as Play) and lands Paused after the first tick; from Paused it ticks once and stays Paused.
    //================================================================================================
    struct step_query_cmd : scene_query_command
    {
        step_query_cmd(xundo::system& System, void* pDataBase) noexcept : scene_query_command(System, "Step", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Runs one frame (from Stopped: starts Play, ticks once, lands Paused; from Paused: ticks once). Usage: Step"; }
        void RegisterArguments() noexcept override {}
        std::string Query() noexcept override
        {
            if (!e29::g_pGamePlugin) return "Step: no game plugin state";
            return e29::RequestStep(EditorContext(), *e29::g_pGamePlugin);
        }
    };

    //================================================================================================
    // Stop - mirrors the "Stop" button: routes through the SAME RequestStop (E29_PlaySession.h) the
    // button itself calls, which sets the deferred flag (m_bStopRequested) consumed at the same clean
    // top-of-frame point PollGameReload runs from - never performed immediately here, for the exact
    // reason the button's own comment gives (StopPlaySession's destroy/recreate can't safely run
    // nested inside an active ImGui frame, and Query() runs outside one anyway, so deferring is not
    // just safe but the ONLY correct way to trigger it from here too).
    //
    // -Keep answers "keep property tweaks made during Play?" up front - for AI/script use, there is no
    // confirmation dialog for a script to click (same reasoning as -Force on the Asset File commands,
    // E10_Commands_AssetFiles.h: "there is no dialog to click"). Omitting it when there IS something
    // to ask about defers to the same confirmation modal the UI shows (RequestStop sets the identical
    // m_bAwaitingKeepTweaksAnswer flag either way) - Stop stays on hold until it's answered one way or
    // the other, by a human or a follow-up -Keep call.
    //================================================================================================
    struct stop_query_cmd : scene_query_command
    {
        stop_query_cmd(xundo::system& System, void* pDataBase) noexcept : scene_query_command(System, "Stop", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Stops Play/Pause, reverting to the pre-Play disk state. If properties changed while Playing, pass -Keep true|false to decide up front, or answer the confirmation dialog. Usage: Stop [-Keep true|false]"; }
        void RegisterArguments() noexcept override
        {
            m_hKeep = m_Parser.addOption("Keep", "true to keep property tweaks made while Playing, false to discard them - answers the 'keep changes?' question up front (for AI/script use - there is no dialog to click)", false, 1);
        }
        std::string Query() noexcept override
        {
            std::optional<bool> KeepOverride;
            if (auto KeepArg = m_Parser.getOptionArgAs<std::string>(m_hKeep, 0); !std::holds_alternative<xerr>(KeepArg))
            {
                const auto& S = std::get<std::string>(KeepArg);
                KeepOverride = (S == "true" || S == "1");
            }

            return e29::RequestStop(EditorContext(), KeepOverride);
        }
        xcmdline::parser::handle m_hKeep;
    };

    //================================================================================================
    // GetPlayState - the read-only counterpart the other three need to be useful headlessly: no
    // synthetic mouse click can show a CLI/AI caller what the menu-bar buttons currently look like.
    //================================================================================================
    struct get_play_state_query_cmd : scene_query_command
    {
        get_play_state_query_cmd(xundo::system& System, void* pDataBase) noexcept : scene_query_command(System, "GetPlayState", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Reports the current Play/Pause/Stop transport state. Usage: GetPlayState"; }
        void RegisterArguments() noexcept override {}
        std::string Query() noexcept override
        {
            auto& State = get<editor_context>().m_State;
            const bool bBuilding = e29::g_pGamePlugin && e29::g_pGamePlugin->m_bBuilding;
            return std::format("PlayState={} Building={} PlayRequested={} StopRequested={}"
                , PlayStateName(State.m_PlayState), bBuilding, State.m_bPlayRequested, State.m_bStopRequested);
        }
    };
}

#endif // E29_COMMANDS_PLAYSESSION_H
