#ifndef E29_GAME_PLUGIN_LOG_H
#define E29_GAME_PLUGIN_LOG_H
#pragma once

// Extracted from E29_GamePlugin.h (mechanical move, phase 3 of the kit split - see the umbrella
// file's own top comment). The on-screen log surface for Game.dll build/load activity - mutex-
// guarded since BuildGamePluginIfStale (E29_GamePluginBuild.h) logs from a background thread while
// the main thread renders it every frame. Meant to be included via the umbrella
// (E29_GamePlugin.h) only, after <mutex>/<vector>/<string> and ImGui are already available.

namespace e29
{
    // Mirrors E27_NodeOS's own GetRuntimeLog()/DrawRuntimeLogPanel() pattern exactly
    // (Editor/NodeOS_Types.h / Editor/NodeOS_UI_Panels.h) - the one on-screen surface for Game.dll
    // build/load activity, since printf's own output has no visible console in this GUI-only app
    // (confirmed live: the user had no way to tell whether an auto-rebuild had even been attempted).
    //
    // Mutex-guarded (unlike NodeOS's own version) because BuildGamePluginIfStale now runs on a
    // background thread (see StartGameReload) so the editor's own render loop never blocks on a
    // slow compile - LogGamePlugin() is called from that thread, GetGamePluginLog()'s CONTENTS are
    // iterated from the main thread every frame in RenderGamePluginLogPanel(), and std::vector has
    // no thread-safety of its own for a concurrent push_back/iterate pair.
    inline std::mutex& GetGamePluginLogMutex() noexcept
    {
        static std::mutex s_Mutex;
        return s_Mutex;
    }

    inline std::vector<std::string>& GetGamePluginLog() noexcept
    {
        static std::vector<std::string> s_Log;
        return s_Log;
    }

    inline void LogGamePlugin( std::string_view Msg ) noexcept
    {
        std::printf("%.*s\n", static_cast<int>(Msg.size()), Msg.data());
        std::fflush(stdout);
        std::lock_guard Lock(GetGamePluginLogMutex());
        GetGamePluginLog().emplace_back(Msg);
    }

    inline void RenderGamePluginLogPanel() noexcept
    {
        // Matches E27_NodeOS's own DrawRuntimeLogPanel exactly (no autoscroll - an earlier version
        // of this function added a GetScrollY()/GetScrollMaxY()/SetScrollHereY() check here; pulled
        // back out after a live crash while docking this window. Root cause turned out to be
        // unrelated to this function entirely - E10_AssetBrowser.h's own MainWindow() was calling
        // ImGui::End() INSIDE its `if (ImGui::Begin(...))` block, skipping it whenever Begin()
        // returned false (a docked-but-not-the-active-tab window) - permanently unbalancing
        // ImGui's window stack from that frame on. Fixed there; this function was never the
        // problem, so it's kept in its simpler, proven-stable form regardless.
        ImGui::SetNextWindowPos(ImVec2(506, 530), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(480, 220), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Game.dll Log"))
        {
            std::lock_guard Lock(GetGamePluginLogMutex());
            if (ImGui::SmallButton("Clear")) GetGamePluginLog().clear();
            ImGui::Separator();
            // A child window of its own, rather than relying on the outer window's own scrollbars -
            // ImGuiWindowFlags_HorizontalScrollbar only kicks in when content actually overflows the
            // region, and build/compile output routinely has lines far wider than this panel's default
            // size (long paths, full compiler command lines) that would otherwise just get clipped.
            if (ImGui::BeginChild("GameLogScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar))
            {
                for (auto& Line : GetGamePluginLog())
                    ImGui::TextUnformatted(Line.c_str());
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

} // namespace e29

#endif // E29_GAME_PLUGIN_LOG_H
