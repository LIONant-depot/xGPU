#ifndef XGPU_IMGUI_TIMELINE_H
#define XGPU_IMGUI_TIMELINE_H
#pragma once
#include "imgui.h"

#include <string>
#include <vector>
#include <span>
#include <cmath>
#include <cstdio>
#include <algorithm>

// A small, self-contained ImGui timeline/scrubber widget - built in-house rather than pulling in a
// third-party sequencer (ImGuizmo's ImSequencer, ImTimeline, etc.): this codebase hand-builds every
// other editor widget (property inspector, asset browser, wedge renderer), and a custom widget
// integrates its own zoom/pan/scrub feel and color palette directly rather than adapting someone
// else's API/data model to fit. Time is always in SECONDS; FPS is only used to snap the playhead to
// whole frames while scrubbing. Lives in source/tools (not a per-example folder) since it's meant to
// be reused by any editor with a clip/sequence to scrub - E24_AnimPackageEditor is its first user,
// not its only intended one.
//
// The gutter/ruler split is two REAL ImGui child windows (not one canvas plus manual column math and
// a PushClipRect at every draw call): a child window clips both its own rendering AND its own
// hit-testing to its bounds natively, so a narrow gutter (or a long clip/track name) can't visually or
// functionally spill into the ruler - that was a recurring bug with the manual-clipping approach, since
// clip rects only ever affected rendering, never a widget's own hit-test region.
//
// `track`/`event` exist now purely as a rendering surface for a feature not built yet: authored
// "events" (a name + start time + duration, living on a named track/lane) that Draw() already knows
// how to lay out and render. Nothing constructs a `track` yet - every caller today passes an empty
// span - but the shape is here so adding real event authoring later is a data-model exercise, not a
// widget rewrite.
namespace xgpu::tools::imgui::timeline
{
    struct event
    {
        std::string     m_Name      = {};
        float           m_Start     = 0.0f;    // seconds
        float           m_Duration  = 0.0f;    // seconds
        std::uint32_t   m_Color     = IM_COL32(90, 170, 250, 255);
    };

    struct track
    {
        std::string         m_Name   = {};
        std::vector<event>  m_Events = {};
    };

    // Persistent view state (zoom/pan/drag) - one instance per timeline widget in the UI, kept
    // alive across frames by the caller (a local `static` or a member on the caller's own state).
    struct state
    {
        float   m_ViewStart     = 0.0f;    // seconds - left edge of the visible window
        float   m_ViewDuration  = 4.0f;    // seconds visible across the widget's full width
        bool    m_bPanning      = false;
        float   m_PanStartMouseX    = 0.0f;
        float   m_PanStartViewStart = 0.0f;
        float   m_GutterWidth = 120.0f;    // width of the left "properties" column - user-draggable
                                            // via the divider between the two columns
    };

    //-------------------------------------------------------------------------

    // Classic "nice numbers for graph labels" algorithm, restricted to a 1-2-5 decade progression
    // (...0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100...) - computed directly from PixelsPerUnit/
    // MinPixelSpacing rather than scanning a fixed lookup table, so it scales to any zoom level or
    // unit range without a hardcoded min/max, while always landing tick values on round, base-10
    // boundaries instead of arbitrary multiples.
    inline float NiceTickStep(float PixelsPerUnit, float MinPixelSpacing)
    {
        const float RawStep    = MinPixelSpacing / std::max(PixelsPerUnit, 1.0e-6f);
        const float Magnitude  = std::pow(10.0f, std::floor(std::log10(RawStep)));
        const float Residual   = RawStep / Magnitude;
        const float NiceResidual = (Residual <= 1.0f) ? 1.0f : (Residual <= 2.0f) ? 2.0f : (Residual <= 5.0f) ? 5.0f : 10.0f;
        return NiceResidual * Magnitude;
    }

    // One notch finer than Step (assumed to already be an exact 1-2-5-decade value, e.g. NiceTickStep's
    // own return value) - used to build the cascading fade tiers in Draw() below. Always divides by a
    // clean divisor of Step itself (2 or 5), NEVER hops sideways to the nearest 1-2-5 neighbor: hopping
    // (e.g. 5 -> 2) would overlay two grids that don't nest - 2 doesn't evenly divide 5, so their tick
    // positions interleave unevenly and read as patchy/missing ticks. Dividing keeps every tier a clean
    // subdivision of the one above it: 10->5->1, 20->10->5->1, 50->10->5->1...
    inline float NiceStepFiner(float Step)
    {
        const float Magnitude = std::pow(10.0f, std::floor(std::log10(Step) + 1.0e-4f));
        const float Residual  = Step / Magnitude;
        const float Divisor   = (Residual > 3.5f) ? 5.0f : 2.0f;   // residual 5 -> /5 (to 1); residual 1 or 2 -> /2 (to 5 or 1)
        return Step / Divisor;
    }

    // How zoomed-in the view currently is, as a percentage where 100% = fully zoomed out (the whole
    // clip visible, i.e. State.m_ViewDuration == the same MaxView cap Draw() itself enforces) - kept
    // as a shared helper rather than duplicating the MaxView formula at each call site, so this stays
    // consistent if that cap ever changes.
    inline float GetZoomPercent(const state& State, float ContentDuration)
    {
        const float MaxView = std::max(std::max(ContentDuration, 0.001f), 1.0f);
        return (State.m_ViewDuration > 0.0f) ? (MaxView / State.m_ViewDuration) * 100.0f : 100.0f;
    }

    //-------------------------------------------------------------------------
    // Draws the ruler + scrubbable playhead + clip-extent bar + any event tracks. `Time` is the
    // playhead position (seconds) - clamped to [0, ContentDuration] and, if FPS > 0, snapped to the
    // nearest whole frame while the user is actively scrubbing. Returns true the frame `Time` was
    // changed by user interaction (so the caller can stop autoplay-advancing it, reset loop counters,
    // etc. - the same thing a manual slider edit would already need to do).
    inline bool Draw
    ( state&                  State
    , float&                  Time
    , float                   ContentDuration
    , float                   FPS
    , std::span<const track>  Tracks
    , const char*             StrID
    , const char*             ClipName = nullptr   // shown in the gutter's clip-extent-bar row, same
                                                    // spot a track's own name shows for its row - null
                                                    // if the caller has nothing to name yet
    , float                   MinHeight = 0.0f     // stretches the widget's background to at least
                                                    // this tall (e.g. "whatever's left in my window
                                                    // above a footer line") even past its own rows'
                                                    // natural height, so the table still reads as
                                                    // filling the panel instead of leaving dead space
    )
    {
        bool bChanged = false;
        ContentDuration = std::max(ContentDuration, 0.001f);
        // Capped at the content's own length (with a 1s floor for near-instant clips) - no reason to
        // zoom out past "the whole clip fits", so this IS the max zoom-out.
        const float MaxView = std::max(ContentDuration, 1.0f);
        State.m_ViewDuration = std::min(State.m_ViewDuration, MaxView);

        // Wide enough for a tick's stacked "1.33s"/"f80" label to read clearly - reused below both to
        // pick the labeled tick tier AND as the max-zoom-in cap: once a single FRAME already has this
        // much room, it's as legible as a tick ever needs to be, so there's no reason to zoom in
        // any further.
        constexpr float MajorTickSpacingPx = 70.0f;
        const bool      bFrameStepped      = FPS > 0.0f;

        ImGui::PushID(StrID);
        ImGui::BeginGroup();

        // Tall enough for the gutter's two stacked Time/Frame INPUT WIDGETS (taller than plain text -
        // frame padding included), which happens to comfortably fit the ruler side's own two stacked
        // tick-label text lines too.
        const float RulerHeight = 4.0f + ImGui::GetFrameHeightWithSpacing() + ImGui::GetFrameHeight();
        constexpr float TrackHeight = 22.0f;
        constexpr float TrackGap    = 4.0f;
        const float NaturalContentHeight = RulerHeight + TrackGap + TrackHeight + (Tracks.empty() ? 0.0f : TrackGap + static_cast<float>(Tracks.size()) * (TrackHeight + TrackGap));
        const float ContentHeight = std::max(NaturalContentHeight, MinHeight);

        constexpr float DividerWidth = 6.0f;
        const float AvailWidth   = ImGui::GetContentRegionAvail().x;
        State.m_GutterWidth      = std::clamp(State.m_GutterWidth, 50.0f, std::max(AvailWidth - DividerWidth - 50.0f, 50.0f));
        const float GutterWidth  = State.m_GutterWidth;
        const float RulerWidth   = std::max(AvailWidth - GutterWidth - DividerWidth, 50.0f);

        // Max zoom-in: once a single frame is at least MajorTickSpacingPx wide, it's already perfectly
        // legible - scales with the ruler's own pixel width and the clip's FPS instead of a fixed
        // constant.
        const float MinView = bFrameStepped ? std::max(RulerWidth / (MajorTickSpacingPx * FPS), 0.01f) : 0.05f;
        State.m_ViewDuration = std::clamp(State.m_ViewDuration, MinView, MaxView);

        // Pan (right-drag) is checked against the WHOLE widget's own bounding box, computed before
        // either child window opens, and applied here rather than inside the ruler child - so a
        // right-drag started over the gutter still pans the ruler, matching how it worked before the
        // child-window split (a child window would otherwise hard-partition hover/active per column).
        {
            const ImVec2 WidgetP0 = ImGui::GetCursorScreenPos();
            const ImVec2 WidgetP1 = ImVec2(WidgetP0.x + AvailWidth, WidgetP0.y + ContentHeight);
            const float  PixelsPerSecond = RulerWidth / State.m_ViewDuration;

            if (State.m_bPanning)
            {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
                    State.m_ViewStart = State.m_PanStartViewStart - (ImGui::GetIO().MousePos.x - State.m_PanStartMouseX) / PixelsPerSecond;
                else
                    State.m_bPanning = false;
            }
            else if (ImGui::IsMouseHoveringRect(WidgetP0, WidgetP1, false) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                State.m_bPanning          = true;
                State.m_PanStartMouseX    = ImGui::GetIO().MousePos.x;
                State.m_PanStartViewStart = State.m_ViewStart;
            }
        }

        // Max left pan: never scroll into negative time - a clip has no "before frame 0", so there's
        // nothing meaningful to show there. A little overscroll past the END still reads as
        // intentional (confirms "this is where the clip actually stops"), just not before the start.
        {
            const float MinStart = 0.0f;
            const float MaxStart = std::max(MinStart, ContentDuration - State.m_ViewDuration * 0.25f);
            State.m_ViewStart = std::clamp(State.m_ViewStart, MinStart, MaxStart);
        }

        //-----------------------------------------------------------------
        // Gutter child: Time/Frame edit boxes, the clip's own name, track names.
        //-----------------------------------------------------------------
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::BeginChild("##gutter", ImVec2(GutterWidth, ContentHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
        {
            ImDrawList*  pGutter  = ImGui::GetWindowDrawList();
            const ImVec2 GutterP0 = ImGui::GetCursorScreenPos();
            const ImVec2 GutterP1 = ImVec2(GutterP0.x + GutterWidth, GutterP0.y + ContentHeight);
            pGutter->AddRectFilled(GutterP0, GutterP1, IM_COL32(22, 22, 26, 255));

            // Row 1: Time/Frame - editable; type a value and press Enter (or click away) to jump the
            // playhead straight there, same as any other ImGui numeric field - each one only re-syncs
            // its displayed text from Time/FPS while NOT actively being typed into, so mid-edit
            // keystrokes are never clobbered by playback advancing Time underneath it. Labels padded to
            // the wider of the two ("Frame:") so both edit boxes start at the same X and share the
            // same width.
            {
                const float LabelWidth = std::max(ImGui::CalcTextSize("Time:").x, ImGui::CalcTextSize("Frame:").x);
                const float FieldX     = GutterP0.x + 4.0f + LabelWidth + 4.0f;
                const float FieldWidth = std::max(GutterWidth - 8.0f - LabelWidth - 4.0f, 20.0f);
                const float Field1Y    = GutterP0.y + 2.0f;
                const float Field2Y    = Field1Y + ImGui::GetFrameHeightWithSpacing();
                const float LabelYOff  = ImGui::GetStyle().FramePadding.y;   // vertically centers the label text against the taller input box

                pGutter->AddText(ImVec2(GutterP0.x + 4.0f, Field1Y + LabelYOff), IM_COL32(200, 200, 210, 255), "Time:");
                if (FPS > 0.0f)
                    pGutter->AddText(ImVec2(GutterP0.x + 4.0f, Field2Y + LabelYOff), IM_COL32(200, 200, 210, 255), "Frame:");

                ImGui::SetCursorScreenPos(ImVec2(FieldX, Field1Y));
                ImGui::SetNextItemWidth(FieldWidth);
                ImGui::InputFloat("##time_edit", &Time, 0.0f, 0.0f, "%.3fs");
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    Time = std::clamp(Time, 0.0f, ContentDuration);
                    if (FPS > 0.0f) Time = std::round(Time * FPS) / FPS;
                    bChanged = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Time - type a value, Enter to jump there");

                if (FPS > 0.0f)
                {
                    int Frame = static_cast<int>(std::lround(Time * FPS));
                    ImGui::SetCursorScreenPos(ImVec2(FieldX, Field2Y));
                    ImGui::SetNextItemWidth(FieldWidth);
                    ImGui::InputInt("##frame_edit", &Frame, 0, 0);
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        Time = std::clamp(static_cast<float>(Frame) / FPS, 0.0f, ContentDuration);
                        bChanged = true;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Frame - type a value, Enter to jump there");
                }

                if (!ImGui::IsAnyItemActive() && ImGui::IsWindowHovered())
                    ImGui::SetTooltip("drag = scrub, scroll = zoom, right-drag = pan");
            }

            // Row dividers - gutter side only spans this child's own width; the ruler side draws its
            // own half further down, leaving a small gap at the divider button, which reads fine.
            auto DrawGutterRowDivider = [&](float Y) { pGutter->AddLine(ImVec2(GutterP0.x, Y), ImVec2(GutterP1.x, Y), IM_COL32(70, 70, 78, 255), 1.0f); };
            DrawGutterRowDivider(GutterP0.y + RulerHeight);

            // Row 2: the clip's own name - same convention as a track's own name cell below.
            if (ClipName)
                pGutter->AddText(ImVec2(GutterP0.x + 4.0f, GutterP0.y + RulerHeight + TrackGap + 3.0f), IM_COL32(200, 200, 210, 255), ClipName);
            DrawGutterRowDivider(GutterP0.y + RulerHeight + TrackGap + TrackHeight + TrackGap * 0.5f);

            // Event track names (empty today - see the file comment).
            float TrackY = GutterP0.y + RulerHeight + TrackGap + TrackHeight + TrackGap;
            for (auto& Track : Tracks)
            {
                pGutter->AddText(ImVec2(GutterP0.x + 4.0f, TrackY + 3.0f), IM_COL32(160, 160, 170, 255), Track.m_Name.c_str());
                TrackY += TrackHeight + TrackGap;
                DrawGutterRowDivider(TrackY - TrackGap * 0.5f);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();

        //-----------------------------------------------------------------
        // Divider - a thin button between the two child windows; dragging it resizes the gutter.
        //-----------------------------------------------------------------
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::InvisibleButton("##gutter_divider", ImVec2(DividerWidth, ContentHeight));
        {
            const bool bDividerHot = ImGui::IsItemHovered() || ImGui::IsItemActive();
            if (bDividerHot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (ImGui::IsItemActive()) State.m_GutterWidth += ImGui::GetIO().MouseDelta.x;

            const ImVec2 DivMin = ImGui::GetItemRectMin();
            const ImVec2 DivMax = ImGui::GetItemRectMax();
            const float  DivX   = (DivMin.x + DivMax.x) * 0.5f;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(DivX, DivMin.y), ImVec2(DivX, DivMax.y)
                , bDividerHot ? IM_COL32(140, 170, 220, 255) : IM_COL32(60, 60, 68, 255), bDividerHot ? 2.0f : 1.0f);
        }

        //-----------------------------------------------------------------
        // Ruler child: ticks, clip-extent bar, event boxes, playhead - all natively clipped/hit-tested
        // to this child's own bounds, no manual bookkeeping needed for where the gutter ends.
        //-----------------------------------------------------------------
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::BeginChild("##ruler", ImVec2(RulerWidth, ContentHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
        {
            ImDrawList*  pDraw = ImGui::GetWindowDrawList();
            const ImVec2 P0    = ImGui::GetCursorScreenPos();
            const ImVec2 P1    = ImVec2(P0.x + RulerWidth, P0.y + ContentHeight);
            pDraw->AddRectFilled(P0, P1, IM_COL32(28, 28, 32, 255));

            ImGui::InvisibleButton("##canvas", ImVec2(RulerWidth, ContentHeight), ImGuiButtonFlags_MouseButtonLeft);
            const bool bHovered = ImGui::IsItemHovered();
            const bool bActive  = ImGui::IsItemActive();

            const float PixelsPerSecond = RulerWidth / State.m_ViewDuration;
            auto TimeToX = [&](float T) { return P0.x + (T - State.m_ViewStart) * PixelsPerSecond; };
            auto XToTime = [&](float X) { return State.m_ViewStart + (X - P0.x) / PixelsPerSecond; };

            // Zoom to cursor (mouse wheel)
            if (bHovered && ImGui::GetIO().MouseWheel != 0.0f)
            {
                const float MouseTime  = XToTime(ImGui::GetIO().MousePos.x);
                const float ZoomFactor = std::pow(1.15f, -ImGui::GetIO().MouseWheel);
                State.m_ViewDuration   = std::clamp(State.m_ViewDuration * ZoomFactor, MinView, MaxView);
                const float NewPixelsPerSecond = RulerWidth / State.m_ViewDuration;
                State.m_ViewStart = MouseTime - (ImGui::GetIO().MousePos.x - P0.x) / NewPixelsPerSecond;

                const float MinStart = 0.0f;
                const float MaxStart = std::max(MinStart, ContentDuration - State.m_ViewDuration * 0.25f);
                State.m_ViewStart = std::clamp(State.m_ViewStart, MinStart, MaxStart);
            }

            // Ruler ticks: a continuous cascade of "nice" 1-2-5-decade tick tiers (...1,2,5,10,20,50,
            // 100 frames-or-seconds...), each fading in/out by its own pixel spacing - the same
            // technique the scene's own floor grid uses (E21_GridShader_frag.glsl's logA/logB
            // crossfade) so zooming slides smoothly between tick densities instead of ticks popping
            // in/out at a hard threshold. Only the LABEL is a discrete choice: exactly one tier - the
            // coarsest that still clears MajorTickSpacingPx - gets numbers; every finer tier stays
            // unlabeled background texture, fading continuously as the view zooms. In frame-stepped
            // mode the cascade bottoms out at exactly 1 frame (never a fractional one), so there's
            // always a real, visible tick to scrub onto - every frame, not just every other one.
            constexpr float TickFadeStartPx = 8.0f;    // a tier starts fading in once its ticks are this close together...
            constexpr float TickFadeEndPx   = 24.0f;   // ...and reaches full opacity by this spacing
            constexpr int   MaxFadeTiers    = 4;       // cap on how many nice-steps finer than Major to cascade through

            const float PixelsPerUnit    = bFrameStepped ? (PixelsPerSecond / FPS) : PixelsPerSecond;
            const float MajorStep        = NiceTickStep(PixelsPerUnit, MajorTickSpacingPx);   // frames if bFrameStepped, else seconds
            const float MajorStepSeconds = bFrameStepped ? (MajorStep / FPS) : MajorStep;

            float TierSteps[MaxFadeTiers + 1];
            int   nTiers = 0;
            {
                float S = MajorStep;
                for (int i = 0; i <= MaxFadeTiers; ++i)
                {
                    TierSteps[nTiers++] = S;
                    if (bFrameStepped && S <= 1.0f + 1.0e-4f) break;   // already at the single-frame floor

                    float Next = NiceStepFiner(S);
                    if (bFrameStepped) Next = std::max(Next, 1.0f);
                    if (Next * PixelsPerUnit < TickFadeStartPx * 0.5f) break;  // next tier would be fully invisible
                    S = Next;
                }
            }

            for (int Tier = 0; Tier < nTiers; ++Tier)
            {
                const float Step      = TierSteps[Tier];
                const bool  bMajor    = (Tier == 0);
                const float PxSpacing = Step * PixelsPerUnit;
                const float Alpha     = bMajor ? 1.0f : std::clamp((PxSpacing - TickFadeStartPx) / (TickFadeEndPx - TickFadeStartPx), 0.0f, 1.0f);
                if (Alpha <= 0.003f) continue;

                const std::uint8_t LineA        = static_cast<std::uint8_t>(Alpha * (bMajor ? 255.0f : 130.0f));
                const float         StepSeconds = bFrameStepped ? (Step / FPS) : Step;
                const float         FirstTick   = std::floor(State.m_ViewStart / StepSeconds) * StepSeconds;

                for (float T = FirstTick; T <= State.m_ViewStart + State.m_ViewDuration + StepSeconds; T += StepSeconds)
                {
                    const float X = TimeToX(T);
                    if (X < P0.x - 2.0f || X > P1.x + 2.0f) continue;

                    const float TickTop = P0.y + (bMajor ? 4.0f : 12.0f);
                    pDraw->AddLine(ImVec2(X, TickTop), ImVec2(X, P0.y + RulerHeight), IM_COL32(120, 120, 130, LineA), 1.0f);

                    if (bMajor)
                    {
                        char SecBuf[32];
                        snprintf(SecBuf, sizeof(SecBuf), MajorStepSeconds < 1.0f ? "%.2fs" : "%.1fs", T);
                        pDraw->AddText(ImVec2(X + 3.0f, P0.y + 1.0f), IM_COL32(200, 200, 210, 255), SecBuf);

                        if (FPS > 0.0f)
                        {
                            char FrameBuf[32];
                            snprintf(FrameBuf, sizeof(FrameBuf), "f%lld", std::llround(T * FPS));
                            pDraw->AddText(ImVec2(X + 3.0f, P0.y + 1.0f + ImGui::GetTextLineHeight()), IM_COL32(150, 150, 160, 255), FrameBuf);
                        }
                    }
                }
            }

            auto DrawRulerRowDivider = [&](float Y) { pDraw->AddLine(ImVec2(P0.x, Y), ImVec2(P1.x, Y), IM_COL32(70, 70, 78, 255), 1.0f); };
            DrawRulerRowDivider(P0.y + RulerHeight);

            // Clip-extent bar - anchors the ruler to "this is where the clip's own data actually
            // lives"; space beyond it (visible when zoomed/panned past the end) stays plain background.
            {
                const float BarTop = P0.y + RulerHeight + TrackGap;
                const float BarBot = BarTop + TrackHeight;
                const float XStart = std::max(TimeToX(0.0f), P0.x);
                const float XEnd   = std::min(TimeToX(ContentDuration), P1.x);
                if (XEnd > XStart)
                    pDraw->AddRectFilled(ImVec2(XStart, BarTop), ImVec2(XEnd, BarBot), IM_COL32(70, 110, 170, 180), 3.0f);
                pDraw->AddRect(ImVec2(XStart, BarTop), ImVec2(XEnd, BarBot), IM_COL32(110, 150, 210, 255), 3.0f);
                DrawRulerRowDivider(BarBot + TrackGap * 0.5f);
            }

            // Event boxes (names are drawn separately, in the gutter child above).
            float TrackY = P0.y + RulerHeight + TrackGap + TrackHeight + TrackGap;
            for (auto& Track : Tracks)
            {
                for (auto& Ev : Track.m_Events)
                {
                    const float XStart = TimeToX(Ev.m_Start);
                    const float XEnd   = TimeToX(Ev.m_Start + Ev.m_Duration);
                    if (XEnd < P0.x || XStart > P1.x) continue;

                    const ImVec2 A(std::max(XStart, P0.x), TrackY);
                    const ImVec2 B(std::min(XEnd, P1.x), TrackY + TrackHeight);
                    pDraw->AddRectFilled(A, B, Ev.m_Color, 3.0f);
                    pDraw->AddRect(A, B, IM_COL32(0, 0, 0, 120), 3.0f);
                    pDraw->PushClipRect(A, B, true);
                    pDraw->AddText(ImVec2(A.x + 3.0f, A.y + 3.0f), IM_COL32(20, 20, 20, 255), Ev.m_Name.c_str());
                    pDraw->PopClipRect();
                }
                TrackY += TrackHeight + TrackGap;
                DrawRulerRowDivider(TrackY - TrackGap * 0.5f);
            }

            // Scrub: left-click or left-drag on the ruler seeks the playhead there, snapping to whole
            // frames if FPS is known - discrete, predictable scrubbing rather than continuous float
            // jitter that never quite lands on a real frame. Naturally confined to the ruler child now
            // (its own InvisibleButton), no more manual "did this drag start over the ruler" tracking.
            if (bActive && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                float NewTime = std::clamp(XToTime(ImGui::GetIO().MousePos.x), 0.0f, ContentDuration);
                if (FPS > 0.0f) NewTime = std::round(NewTime * FPS) / FPS;
                if (std::abs(NewTime - Time) > 1.0e-6f) { Time = NewTime; bChanged = true; }
            }

            // Playhead - a full-height line plus a small triangular grip at the top, matching the
            // accent color used nowhere else in this canvas so it always reads as "the one draggable
            // thing". If Time is currently panned out of view it's simply not drawn (outside P0..P1),
            // rather than bleeding into the gutter.
            {
                const float X = TimeToX(Time);
                pDraw->AddLine(ImVec2(X, P0.y), ImVec2(X, P1.y), IM_COL32(255, 110, 60, 255), 2.0f);
                pDraw->AddTriangleFilled(ImVec2(X - 6.0f, P0.y), ImVec2(X + 6.0f, P0.y), ImVec2(X, P0.y + 10.0f), IM_COL32(255, 110, 60, 255));
            }

            // Shown on plain hover AND throughout an active scrub drag, so it never drops out mid-drag.
            const bool bScrubbing = bActive && ImGui::IsMouseDown(ImGuiMouseButton_Left);
            if (bHovered || bScrubbing)
            {
                const float HoverTime = XToTime(ImGui::GetIO().MousePos.x);
                if (HoverTime >= -0.001f && HoverTime <= ContentDuration + 0.001f)
                {
                    if (FPS > 0.0f) ImGui::SetTooltip("%.3fs   f%lld", HoverTime, std::llround(HoverTime * FPS));
                    else             ImGui::SetTooltip("%.3fs", HoverTime);
                }
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::EndGroup();
        ImGui::PopID();
        return bChanged;
    }
}

#endif
