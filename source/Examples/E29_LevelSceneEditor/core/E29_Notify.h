#pragma once

// User-visible error notices (Debugger + the modal error popup).
// Split out of E29_LevelSceneEditorKit.h; included from there at the position this code used to occupy.
namespace e29
{
    // Debugger's own console output is invisible from inside the running app - every refusal/failure
    // that only ever went through it (load failures, the circular-dependency-cycle refusal, ...)
    // silently did nothing from the USER's own point of view, direct report: "it fails silently...
    // the user may be confused of why". These two file-static globals plus the modal render block
    // right after Debugger's own definition are a minimal, app-wide fix - every EXISTING Debugger(...)
    // call site benefits for free, not just newly-added ones.
    static std::string g_LastErrorMessage;
    static bool        g_bOpenErrorPopup = false;

    static void Debugger(std::string_view View)
    {
        // Flushed unconditionally - stdout redirected to a file is fully buffered rather than
        // line-buffered, so without this, a crash (or a debug-assert dialog that blocks the process
        // indefinitely) silently loses whatever log lines hadn't been flushed yet - exactly the
        // "can't tell what happened right before the crash" gap that makes these bugs hard to chase.
        printf("%.*s\n", static_cast<int>(View.size()), View.data());
        fflush(stdout);

        // Only the FLAG is set here, not ImGui::OpenPopup itself - Debugger is called from arbitrary,
        // often deeply-nested ID-stack contexts (mid-drag-drop, inside per-row PushID blocks, ...),
        // and OpenPopup(str_id) hashes its id against whatever ID stack is CURRENTLY active - calling
        // it here would give it a different internal id than the BeginPopupModal call below (made
        // from the main loop's own top-level, unnested scope), so the popup would silently never
        // actually open. RenderErrorPopup (called once per frame from that same top-level scope)
        // is the only place that ever calls OpenPopup, one frame later - by which point whatever
        // drag/drop or click triggered this Debugger call has already fully finished processing for
        // its own frame, so a real MODAL (blocks input, dims the background - "very obvious", direct
        // user request after trying the first, input-transparent toast version) can't ever eat an
        // in-flight mouse release.
        g_LastErrorMessage = std::string(View);
        g_bOpenErrorPopup  = true;
    }

    // Opens/renders the modal popup for the most recent Debugger(...) message, if any - called once
    // per frame from the main loop, right after BeginRendering, from the SAME top-level ID-stack
    // scope every frame (required for BeginPopupModal to ever actually find the popup OpenPopup
    // requested - see Debugger's own comment for why the two calls must share that scope).
    static void RenderErrorPopup() noexcept
    {
        if (g_bOpenErrorPopup)
        {
            ImGui::OpenPopup("Error##E29");
            g_bOpenErrorPopup = false;
        }

        ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Error##E29", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 400.0f);
            ImGui::TextUnformatted(g_LastErrorMessage.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Separator();
            if (ImGui::Button("OK", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
}
