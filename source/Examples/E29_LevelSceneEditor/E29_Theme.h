#ifndef E29_THEME_H
#define E29_THEME_H
#pragma once

#include <array>

// Unity 6-inspired editor theme - direct user request: "change the colors/spacing/etc of example 29
// theme to kind match unity... The end goal is to make Example 29 look as professional as possible."
//
// Every color below is one of Unity's OWN published dark-theme design tokens (see
// https://www.foundations.unity.com/fundamentals/color-palette), not eyeballed off a screenshot - the
// comment on each line names the exact Unity token it came from, so a future pass can cross-check
// against an updated palette instead of re-guessing. Layout numbers (padding/rounding/border sizes)
// are tuned by eye against Unity 6's own compact, mostly-rectangular look (near-zero rounding, thin
// 1px borders everywhere) - Unity doesn't publish those as tokens the way it does colors.
//
// Scoped to E29 ONLY: this mutates the one global ImGuiStyle, but every xGPU example is its own
// separate process invocation of xGPU_unit_test.exe (see E29_Example()'s own call site - this is
// called once, right after xgpu::tools::imgui::CreateInstance()) - E10/E19-28 never call this
// function, so their own look is byte-for-byte unaffected.
namespace e29::theme
{
    // Named ColorRGB, not RGB - <windows.h>'s own RGB(r,g,b) macro would otherwise silently swallow
    // this function's definition in any translation unit that pulls in Win32 headers.
    inline ImVec4 ColorRGB(int R, int G, int B, int A = 255) noexcept
    {
        return ImVec4(R / 255.0f, G / 255.0f, B / 255.0f, A / 255.0f);
    }

    inline void ApplyUnityInspiredTheme() noexcept
    {
        ImGuiStyle& Style = ImGui::GetStyle();

        //
        // Layout - Unity's editor reads as noticeably more compact and flat than ImGui's own
        // defaults: tight padding, thin 1px borders throughout, and almost no corner rounding (a
        // small 2px hint on buttons/frames/tabs, none at all on windows/popups/children).
        //
        // FramePadding.y/ItemSpacing.y specifically: direct user comparison against Unity's own
        // Inspector (Transform/Position/Rotation/Scale rows) - Unity packs rows MUCH tighter
        // vertically than these original values produced. Tightened rather than left "compact enough"
        // - Unity's density is a big part of why it reads as a serious tool rather than a hobby one.
        Style.WindowPadding            = ImVec2(6, 6);
        Style.FramePadding             = ImVec2(6, 1);
        Style.ItemSpacing              = ImVec2(6, 1);
        Style.ItemInnerSpacing         = ImVec2(4, 4);
        Style.CellPadding              = ImVec2(6, 1);
        Style.IndentSpacing            = 16.0f;
        Style.ScrollbarSize            = 14.0f;
        Style.GrabMinSize              = 10.0f;

        Style.WindowRounding           = 0.0f;
        Style.ChildRounding            = 0.0f;
        Style.PopupRounding            = 2.0f;
        Style.FrameRounding            = 2.0f;
        Style.ScrollbarRounding        = 2.0f;
        Style.GrabRounding             = 2.0f;
        Style.TabRounding              = 2.0f;

        Style.WindowBorderSize         = 1.0f;
        Style.ChildBorderSize          = 1.0f;
        Style.PopupBorderSize          = 1.0f;
        // FrameBorderSize was 1.0f - direct user report, with a screenshot: a component header row's
        // LEFT half (the label, rendered as an ImGuiTreeNodeFlags_Framed TreeNode, which draws its own
        // stroke when FrameBorderSize>0) showed a visible outline while the RIGHT half (a plain
        // AddRectFilled from separate xproperty code, which never strokes a border at all) didn't -
        // two different rendering techniques feeding one visual row, so no color/value tweak could
        // ever make them match. Real Unity doesn't outline frames at all - fields are told apart by
        // fill-color contrast only (confirmed against the reference Position/Rotation/Scale screenshot
        // - no visible stroke on any of them) - so 0 here is more correct, not just a workaround.
        Style.FrameBorderSize          = 0.0f;
        Style.TabBorderSize            = 0.0f;
        Style.TabBarBorderSize         = 1.0f;
        Style.SeparatorTextBorderSize  = 1.0f;

        Style.WindowTitleAlign         = ImVec2(0.0f, 0.5f);
        Style.SelectableTextAlign      = ImVec2(0.0f, 0.0f);
        Style.ButtonTextAlign          = ImVec2(0.5f, 0.5f);

        //
        // Colors - named after Unity's own design-token names, dark theme
        // (foundations.unity.com/fundamentals/color-palette).
        //
        const ImVec4 Window            = ColorRGB(0x38, 0x38, 0x38);  // Unity "Window"
        const ImVec4 DefaultBg         = ColorRGB(0x28, 0x28, 0x28);  // Unity "Default Background"
        const ImVec4 InputField        = ColorRGB(0x2A, 0x2A, 0x2A);  // Unity "Input Field"
        const ImVec4 Toolbar           = ColorRGB(0x3C, 0x3C, 0x3C);  // Unity "Toolbar"
        const ImVec4 AppToolbar        = ColorRGB(0x19, 0x19, 0x19);  // Unity "App Toolbar"
        const ImVec4 ButtonBg          = ColorRGB(0x58, 0x58, 0x58);  // Unity "Button"
        const ImVec4 Dropdown          = ColorRGB(0x51, 0x51, 0x51);  // Unity "Dropdown"
        const ImVec4 InspectorTitle    = ColorRGB(0x3E, 0x3E, 0x3E);  // Unity "Inspector Titlebar"

        const ImVec4 DefaultBorder     = ColorRGB(0x23, 0x23, 0x23);  // Unity "Default Border" / "Toolbar Border"

        const ImVec4 Highlight         = ColorRGB(0x2C, 0x5D, 0x87);  // Unity "Highlight Background" (selection blue)
        const ImVec4 FocusBorder       = ColorRGB(0x3A, 0x79, 0xBB);  // Unity "Input Field Border Focus"
        const ImVec4 AccentFocus       = ColorRGB(0x7B, 0xAE, 0xFA);  // Unity "Button Border Accent Focus"
        const ImVec4 HighlightText     = ColorRGB(0x4C, 0x7E, 0xFF);  // Unity "Highlight Text"

        const ImVec4 TextDefault       = ColorRGB(0xD2, 0xD2, 0xD2);  // Unity "Default Text"
        const ImVec4 TextButton        = ColorRGB(0xEE, 0xEE, 0xEE);  // Unity "Button Text"
        const ImVec4 TextLabel         = ColorRGB(0xC4, 0xC4, 0xC4);  // Unity "Label Text" / "Toolbar Button Text"
        const ImVec4 TextDisabled      = ColorRGB(0x7A, 0x7A, 0x7A);

        auto& C = Style.Colors;

        C[ImGuiCol_Text]                       = TextDefault;
        C[ImGuiCol_TextDisabled]               = TextDisabled;
        C[ImGuiCol_WindowBg]                   = Window;
        C[ImGuiCol_ChildBg]                    = ImVec4(0, 0, 0, 0);  // parent window's own bg shows through
        C[ImGuiCol_PopupBg]                    = ImVec4(Window.x, Window.y, Window.z, 0.98f);
        C[ImGuiCol_Border]                     = DefaultBorder;
        C[ImGuiCol_BorderShadow]               = ImVec4(0, 0, 0, 0);

        C[ImGuiCol_FrameBg]                    = InputField;
        C[ImGuiCol_FrameBgHovered]             = ColorRGB(0x33, 0x33, 0x33);
        C[ImGuiCol_FrameBgActive]              = ColorRGB(0x3A, 0x3A, 0x3A);

        // TitleBg/TitleBgActive are NOT just a floating-window titlebar color - ImGui paints the WHOLE
        // dock tab-bar strip's backdrop with these FIRST, then draws each individual tab rect on top
        // (DockNodeUpdateTabBar, imgui.cpp) - any small gap between adjacent tab rects lets this
        // backdrop show through. These used to be AppToolbar/InspectorTitle (a near-black tone picked
        // for a plain titlebar, unaware of this second role) - much darker than the Tab/TabDimmed
        // colors below, so every gap between tabs read as a hard vertical divider (direct user report,
        // with a screenshot: "you brought back the dividers again"). Matched tightly to Tab/TabDimmed
        // (below) instead, so a gap is invisible rather than merely a better-chosen contrast - keep
        // these two pairs in sync if either changes.
        C[ImGuiCol_TitleBg]                    = ColorRGB(0x1C, 0x1C, 0x1C);  // == TabDimmed
        C[ImGuiCol_TitleBgActive]              = ColorRGB(0x20, 0x20, 0x20);  // == Tab
        C[ImGuiCol_TitleBgCollapsed]           = ColorRGB(0x1C, 0x1C, 0x1C);
        C[ImGuiCol_MenuBarBg]                  = AppToolbar;

        C[ImGuiCol_ScrollbarBg]                = DefaultBg;
        C[ImGuiCol_ScrollbarGrab]              = ButtonBg;
        C[ImGuiCol_ScrollbarGrabHovered]       = ColorRGB(0x6E, 0x6E, 0x6E);
        C[ImGuiCol_ScrollbarGrabActive]        = FocusBorder;

        C[ImGuiCol_CheckMark]                  = AccentFocus;
        C[ImGuiCol_SliderGrab]                 = Dropdown;
        C[ImGuiCol_SliderGrabActive]           = FocusBorder;

        C[ImGuiCol_Button]                     = ButtonBg;
        C[ImGuiCol_ButtonHovered]              = ColorRGB(0x66, 0x66, 0x66);
        C[ImGuiCol_ButtonActive]               = FocusBorder;

        C[ImGuiCol_Header]                     = Highlight;
        C[ImGuiCol_HeaderHovered]              = ColorRGB(0x33, 0x60, 0x88);
        C[ImGuiCol_HeaderActive]               = FocusBorder;

        C[ImGuiCol_Separator]                  = DefaultBorder;
        C[ImGuiCol_SeparatorHovered]           = FocusBorder;
        C[ImGuiCol_SeparatorActive]            = AccentFocus;

        C[ImGuiCol_ResizeGrip]                 = ImVec4(ButtonBg.x, ButtonBg.y, ButtonBg.z, 0.30f);
        C[ImGuiCol_ResizeGripHovered]          = ImVec4(FocusBorder.x, FocusBorder.y, FocusBorder.z, 0.60f);
        C[ImGuiCol_ResizeGripActive]           = FocusBorder;

        C[ImGuiCol_InputTextCursor]            = TextButton;

        // Direct user comparison against Unity's own Project/Console tabs: the active tab there is
        // UNMISTAKABLE - clearly brighter than its inactive siblings, popping forward to meet the
        // content below. The original values here (Tab #282828 vs TabSelected #3E3E3E, and worse,
        // TabDimmed #232323 vs TabDimmedSelected #303030) were only a ~13-22 point gap out of 255 -
        // read as "all six tabs look identical" exactly as reported. Widened deliberately on both the
        // focused-dockspace pair (Tab/TabSelected) AND the unfocused one (TabDimmed/TabDimmedSelected,
        // used whenever the app itself isn't the OS-focused window) so the active tab stays obvious
        // either way, matching Unity's own "always know which tab is open" behavior.
        C[ImGuiCol_TabHovered]                 = ColorRGB(0x50, 0x50, 0x50);
        C[ImGuiCol_Tab]                         = ColorRGB(0x20, 0x20, 0x20);
        C[ImGuiCol_TabSelected]                = ColorRGB(0x48, 0x48, 0x48);
        C[ImGuiCol_TabSelectedOverline]        = FocusBorder;
        C[ImGuiCol_TabDimmed]                  = ColorRGB(0x1C, 0x1C, 0x1C);
        C[ImGuiCol_TabDimmedSelected]          = ColorRGB(0x3C, 0x3C, 0x3C);
        C[ImGuiCol_TabDimmedSelectedOverline]  = DefaultBorder;

        C[ImGuiCol_DockingPreview]             = ImVec4(Highlight.x, Highlight.y, Highlight.z, 0.55f);
        C[ImGuiCol_DockingEmptyBg]             = DefaultBg;

        C[ImGuiCol_PlotLines]                  = TextLabel;
        C[ImGuiCol_PlotLinesHovered]           = AccentFocus;
        C[ImGuiCol_PlotHistogram]              = FocusBorder;
        C[ImGuiCol_PlotHistogramHovered]       = AccentFocus;

        C[ImGuiCol_TableHeaderBg]              = Toolbar;
        C[ImGuiCol_TableBorderStrong]          = DefaultBorder;
        C[ImGuiCol_TableBorderLight]           = ImVec4(DefaultBorder.x, DefaultBorder.y, DefaultBorder.z, 0.60f);
        C[ImGuiCol_TableRowBg]                 = ImVec4(0, 0, 0, 0);
        C[ImGuiCol_TableRowBgAlt]              = ImVec4(1, 1, 1, 0.02f);

        C[ImGuiCol_TextLink]                   = HighlightText;
        C[ImGuiCol_TextSelectedBg]             = ImVec4(Highlight.x, Highlight.y, Highlight.z, 0.55f);
        C[ImGuiCol_TreeLines]                  = ImVec4(TextLabel.x, TextLabel.y, TextLabel.z, 0.25f);

        C[ImGuiCol_DragDropTarget]             = AccentFocus;
        C[ImGuiCol_UnsavedMarker]              = AccentFocus;

        C[ImGuiCol_NavCursor]                  = AccentFocus;
        C[ImGuiCol_NavWindowingHighlight]      = ImVec4(1, 1, 1, 0.70f);
        C[ImGuiCol_NavWindowingDimBg]          = ImVec4(0.2f, 0.2f, 0.2f, 0.40f);
        C[ImGuiCol_ModalWindowDimBg]           = ImVec4(0.1f, 0.1f, 0.1f, 0.50f);
    }

    // ImGui::Checkbox's checkmark is hardcoded (imgui_widgets.cpp) to fill ~2/3 of the box with
    // thickness = size/5 - no style var controls this ratio, so against E29_Theme.h's larger 17px UI
    // font (a bigger checkbox box than ImGui's own ~13px default assumes) the mark comes out
    // noticeably bolder/bigger than Unity's own delicate, thin tick (direct user comparison
    // screenshot: "our tick mark is way too large"). Drawn manually instead of calling
    // ImGui::Checkbox - same interaction model (InvisibleButton + click-to-toggle), but the checkmark
    // is scaled down independently of the box size.
    inline bool UnityCheckbox(const char* StrId, bool* pValue) noexcept
    {
        // Plain float math throughout, not ImVec2 operator+ - this translation unit doesn't define
        // IMGUI_DEFINE_MATH_OPERATORS, so ImVec2 has no arithmetic operators available.
        const float Sz  = ImGui::GetFrameHeight();
        const ImVec2 Pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(StrId, ImVec2(Sz, Sz));
        const bool bHovered = ImGui::IsItemHovered();
        const bool bActive  = ImGui::IsItemActive();
        bool bChanged = false;
        if (ImGui::IsItemClicked()) { *pValue = !*pValue; bChanged = true; }

        ImDrawList* DrawList = ImGui::GetWindowDrawList();
        const ImU32 BgCol = ImGui::GetColorU32(bActive ? ImGuiCol_FrameBgActive : bHovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
        DrawList->AddRectFilled(Pos, ImVec2(Pos.x + Sz, Pos.y + Sz), BgCol, ImGui::GetStyle().FrameRounding);
        if (*pValue)
        {
            // A plain 2-segment tick drawn by hand, sized independently of the box - ImGui's own
            // RenderCheckMark (which draws THIS exact shape) lives in imgui_internal.h, not worth
            // pulling in the internal API for one glyph. Pad ~0.34*Sz (mark spans ~0.32*Sz) vs
            // ImGui::Checkbox's own ~0.17*Sz pad (mark spans ~0.83*Sz) - noticeably smaller, and the
            // explicit thickness below (not scaled off Sz/5 the way ImGui's own mark is) stays thin.
            const ImU32  CheckCol = ImGui::GetColorU32(ImGuiCol_CheckMark);
            const float  Pad      = Sz * 0.34f;
            const float  MarkSz   = Sz - Pad * 2.0f;
            const float  Third    = MarkSz / 3.0f;
            const float  Px       = Pos.x + Pad;
            const float  Py       = Pos.y + Pad;
            const ImVec2 A(Px,           Py + MarkSz * 0.55f);
            const ImVec2 B(Px + Third,   Py + MarkSz);
            const ImVec2 C(Px + MarkSz,  Py);
            const float  Thickness = (Sz * 0.09f > 1.0f) ? Sz * 0.09f : 1.0f;
            const std::array<ImVec2, 3> Points{ A, B, C };
            DrawList->AddPolyline(Points.data(), 3, CheckCol, ImDrawFlags_None, Thickness);
        }
        return bChanged;
    }
}

#endif
