#ifndef E29_PANEL_PLAY_TRANSPORT_H
#define E29_PANEL_PLAY_TRANSPORT_H
#pragma once

// Play / Step / Pause / Stop buttons, shared by the menu-bar transport and the editor toolbar - they differ
// only in layout (transport_layout). All behavior lives in level/E29_PlaySession.h (RequestPlay/...), which
// the CLI Play/Pause/Step/Stop commands use too.
//
// Unity-style, worked out to keep the mouse from ever landing on a moved button: two fixed slots, centered as
// a PAIR - slot 1 is Play/Stop, slot 2 is Step while Stopped/Paused or Pause while Playing (same position, only
// the icon/action changes). A third slot - Pause again, pressed, to Resume - appears only while Paused, to the
// right of the pair. Glyphs are Segoe MDL2 (U+E768 play, E769 pause, E71A stop, E893 step), merged into font 4.
namespace e29
{
    struct transport_layout
    {
        ImVec2 m_ButtonSize;
        bool   m_bHorizontal;   // false: stacked top-to-bottom (toolbar docked left/right), no centering
        bool   m_bTooltips;
    };

    inline void RenderPlayTransport( editor_context& Ed, game_plugin_state& Plugin, const transport_layout& Layout ) noexcept
    {
        auto& State = Ed.State();
        using play_state = editor_state::play_state;
        constexpr const char* PlayIcon  = "\xEE\x9D\xA8";
        constexpr const char* PauseIcon = "\xEE\x9D\xA9";
        constexpr const char* StopIcon  = "\xEE\x9C\x9A";
        constexpr const char* StepIcon  = "\xEE\xA2\x93";

        // Captured once, before any button: clicking slot 2's Pause must not make slot 3 appear this same frame.
        const bool bStopped = State.m_PlayState == play_state::Stopped;
        const bool bPlaying = State.m_PlayState == play_state::Playing;
        const bool bPaused  = State.m_PlayState == play_state::Paused;

        auto Slot = [&](const char* Icon, bool bDisabled, bool bPressed, auto&& OnClick) noexcept
        {
            ImGui::PushFont(xgpu::tools::imgui::getFont(4));
            if (bPressed) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_Header]);
            ImGui::BeginDisabled(bDisabled);
            if (ImGui::Button(Icon, Layout.m_ButtonSize)) OnClick();
            ImGui::EndDisabled();
            if (bPressed) ImGui::PopStyleColor();
            ImGui::PopFont();
        };
        auto Tip = [&](const char* Title, const char* Hint) noexcept
        {
            if (!Layout.m_bTooltips || !ImGui::IsItemHovered()) return;
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(Title);
            ImGui::TextDisabled("%s", Hint);
            ImGui::EndTooltip();
        };
        auto Next = [&]() noexcept { if (Layout.m_bHorizontal) ImGui::SameLine(); };

        // Centered on the two-slot pair ALWAYS, so slots 1 and 2 stay pixel-fixed when slot 3 appears.
        if (Layout.m_bHorizontal)
            ImGui::SameLine((ImGui::GetWindowWidth() - (Layout.m_ButtonSize.x * 2.0f + ImGui::GetStyle().ItemSpacing.x)) * 0.5f);

        Slot(bStopped ? PlayIcon : StopIcon, Plugin.m_bBuilding, false, [&]
        {
            if (bStopped) RequestPlay(Ed, Plugin);
            else          RequestStop(Ed, std::nullopt);
        });
        Tip(bStopped ? "Play" : "Stop", bStopped ? "Start playback" : "Stop playback");

        Next();
        if (bPlaying)
        {
            Slot(PauseIcon, false, false, [&] { RequestPause(State); });
            Tip("Pause", "Pause playback");
        }
        else
        {
            Slot(StepIcon, Plugin.m_bBuilding, false, [&] { RequestStep(Ed, Plugin); });
            Tip("Step", "Run one frame");
        }

        if (bPaused)
        {
            Next();
            Slot(PauseIcon, false, true, [&] { RequestResume(State); });
            Tip("Resume", "Resume playback");
        }
    }
}

#endif // E29_PANEL_PLAY_TRANSPORT_H
