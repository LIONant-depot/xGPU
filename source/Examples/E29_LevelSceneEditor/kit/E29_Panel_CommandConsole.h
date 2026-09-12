#ifndef E29_PANEL_COMMAND_CONSOLE_H
#define E29_PANEL_COMMAND_CONSOLE_H
#pragma once

// Command Console panel - phase 6 of [[e29_command_undo_system_plan]] (memory), direct user request:
// "you can bring over the command window from example 27". Near-direct port of E27_NodeOS's own
// DrawCommandConsolePanel (source/Examples/E27_NodeOS/Editor/NodeOS_UI_CommandConsole.h) - a plain
// text box for "Namespace/Edit-or-Query/Command -args..." strings routed through
// xundo::history::Route(), the SAME dispatch phase 5's named-pipe server
// (commands/E29_CommandConsolePipe.h) already uses. Exists specifically so a query command never
// NEEDS a bespoke ImGui widget to be reachable - the same text protocol an AI or a script (E29CLI.cpp)
// already uses works here too, with the same self-documenting "-h" help every command exposes.
//
// Reuses console_log_entry/console_log_source/ProcessConsoleCommand from
// commands/E29_CommandConsolePipe.h rather than redefining them - the SAME log a pipe-driven command
// already appends to (phase 5) is what this panel renders, so a Say/GetLog/SetProperty/etc. sent over
// the pipe shows up here exactly like one typed into this box, never a silent side-channel.
//
// Help and autocomplete both key off xundo::history::GetRoutableCommands() - every registered
// command's fully-qualified name ("E29/Edit/SetProperty") paired with its own one-line
// getCommandHelp()/getCommandHelp() text, gathered fresh each call (command count here is small -
// currently 10 - so no need to cache this across frames).
//
// Autocomplete scoring reuses the same weighted substring/prefix Damerau-Levenshtein distance
// E10_TextureResourcePipeline's asset browser already uses for its own fuzzy search box -
// xstrtool::SubstringDamerauLevenshteinDistanceI - rather than inventing a second fuzzy-match
// convention. Already included transitively via the kit umbrella (E29_LevelSceneEditorKit.h's own
// top-of-file include).
#include "source/Examples/E19_MaterialEditor/E19_TextEditor.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandConsolePipe.h"

namespace e29
{
    // Who authored a Command Console log entry - the log's own "> Cmd" echo line is colored by this
    // (User=green if typed into the UI, Pipe=teal if it arrived over E29CLI's named pipe - an
    // AI-facing color on purpose). DrawCommandConsolePanel and PumpCommandConsolePipe (phase 5) both
    // push an echo entry then, if non-empty, a result entry - never one blended entry.
    static bool ConsoleLogTokenize(const char* in_begin, const char* in_end, const char*& out_begin, const char*& out_end, TextEditor::PaletteIndex& paletteIndex)
    {
        const size_t Len = static_cast<size_t>(in_end - in_begin);
        if (Len >= 2 && in_begin[0] == '>' && in_begin[1] == ' ')
        {
            out_begin = in_begin; out_end = in_end; paletteIndex = TextEditor::PaletteIndex::KnownIdentifier; // green - User, see BuildConsoleLogPalette
            return true;
        }
        if (Len >= 2 && in_begin[0] == '$' && in_begin[1] == ' ')
        {
            out_begin = in_begin; out_end = in_end; paletteIndex = TextEditor::PaletteIndex::Preprocessor; // teal - Pipe, the AI's "favorite color"
            return true;
        }
        return false; // System line - falls through to Default
    }

    // A from-scratch LanguageDefinition rather than repurposing CPlusPlus()'s - this text isn't code,
    // it just needs mTokenize wired up and comment-detection neutralized. Leaving mCommentStart/
    // mCommentEnd/mSingleLineComment at their default EMPTY strings is a trap, not a no-op:
    // ColorizeInternal's comment scan treats an empty marker as matching at every position, which
    // would flag the entire log as "inside a multi-line comment" and force everything to the Comment
    // palette color ahead of mTokenize's own result - pointing all three at a marker that will never
    // appear in real log text sidesteps that entirely.
    static const TextEditor::LanguageDefinition& ConsoleLogLanguageDefinition() noexcept
    {
        static const TextEditor::LanguageDefinition LangDef = [] {
            TextEditor::LanguageDefinition Def;
            Def.mName            = "ConsoleLog";
            Def.mTokenize        = ConsoleLogTokenize;
            Def.mAutoIndentation = false;
            Def.mCommentStart = Def.mCommentEnd = Def.mSingleLineComment = "\x01\x01\x01__no_console_log_comment_marker__\x01\x01\x01";
            return Def;
        }();
        return LangDef;
    }

    static TextEditor::Palette BuildConsoleLogPalette() noexcept
    {
        TextEditor::Palette Pal = TextEditor::GetDarkPalette();
        Pal[(int)TextEditor::PaletteIndex::KnownIdentifier] = IM_COL32(115, 217, 115, 255); // green - User
        Pal[(int)TextEditor::PaletteIndex::Preprocessor]    = IM_COL32( 51, 191, 191, 255); // teal  - Pipe (AI)
        return Pal;
    }

    // The one place that decides what marker a line gets, so DrawCommandConsolePanel's rendering and
    // a future flattened-CLI-text query (if one's ever added) agree - m_Text itself is always the RAW
    // command/result text with no marker baked in, exactly so there's only one place this happens.
    static const char* ConsoleLogLinePrefix(console_log_source Source) noexcept
    {
        switch (Source)
        {
            case console_log_source::User: return "> ";
            case console_log_source::Pipe: return "$ ";
            default:                       return "";
        }
    }

    // Splits on '\n' without a regex/stream detour - SetTextLines wants one entry per line, and a log
    // entry's text is plain, host-authored strings, never anything exotic enough to need more than this.
    static std::vector<std::string> SplitLines(std::string_view Text)
    {
        std::vector<std::string> Out;
        std::size_t Start = 0;
        for (;;)
        {
            const std::size_t Nl = Text.find('\n', Start);
            Out.emplace_back(Text.substr(Start, Nl == std::string_view::npos ? std::string_view::npos : Nl - Start));
            if (Nl == std::string_view::npos) break;
            Start = Nl + 1;
        }
        return Out;
    }

    // CallbackAlways is the only job left here - forces the cursor to the end of the text right after
    // a programmatic buffer rewrite (a suggestion/history pick, applied on the NEXT frame via
    // DrawCommandConsolePanel's own bApplyPendingFill/SetKeyboardFocusHere dance), since ImGui doesn't
    // reliably land it there on every version/path by default. UserData is just the bool directly now -
    // arrow-key history/suggestion nav used to also live here (a CallbackHistory handler), removed
    // entirely rather than worked around. Real reason, found the hard way (a live ImGui assert, not a
    // guess): ImGui flatly refuses ImGuiInputTextFlags_CallbackHistory together with
    // ImGuiInputTextFlags_Multiline (imgui_widgets.cpp: "Assertion failed: !((flags &
    // ImGuiInputTextFlags_CallbackHistory) && (flags & ImGuiInputTextFlags_Multiline))") - multiline
    // already owns Up/Down for moving the cursor between wrapped/real lines and doesn't support
    // reassigning them. Direct user follow-up once told this: history now works like the Asset
    // Browser's own path-history popup (E10_AssetBrowser.h's RenderPathHistoryPopup) - a small "History"
    // button opens a click-to-recall popup (below) instead of relying on arrow keys at all; suggestions
    // stay mouse-click-only, same as they always were alongside the (now-removed) keyboard cycling.
    static int CommandConsoleCallback(ImGuiInputTextCallbackData* pData)
    {
        auto* pForceCursorEnd = static_cast<bool*>(pData->UserData);
        if (pData->EventFlag == ImGuiInputTextFlags_CallbackAlways && *pForceCursorEnd)
        {
            pData->CursorPos = pData->SelectionStart = pData->SelectionEnd = pData->BufTextLen;
            *pForceCursorEnd = false;
        }
        return 0;
    }

    static void DrawCommandConsolePanel(xundo::history& History, std::vector<console_log_entry>& LogEntries)
    {
        static char                     CmdBuffer[2048] = ""; // bumped from 256 - a base64-encoded SetProperty value alone can run well past that, and the box now wraps/grows instead of horizontal-scrolling anyway
        // LogEntries is owned by the caller (E29_LevelScene_Editor.cpp), not a local static here - the
        // named-pipe server (phase 5) needs to append to the SAME visible log a pipe-driven command
        // isn't a secret side-channel from a UI reading over your shoulder.
        static std::string              PendingFill;        // value to apply into CmdBuffer, see bApplyPendingFill
        static bool                     bApplyPendingFill = false;
        static bool                     bRefocus  = false;
        static bool                     bForceCursorEnd = false; // consumed by CommandConsoleCallback's CallbackAlways branch, above
        static std::vector<std::string> CmdHistory;         // previously RUN command lines, oldest first - shown in the History popup, below
        static bool                     bShowHistoryPopup = false;

        // One persistent TextEditor for the whole log's lifetime - matching the established
        // "Inspector must persist across frames" pattern (rebuilding a widget like this every frame
        // makes its internal state, here the colorizer's cached ranges and the scroll position,
        // unstable). SetImGuiChildIgnored(true) is what lets Render() below draw straight into a
        // BeginChild WE own (so it lives inside this "Command Console" panel, not as its own separate
        // top-level window).
        static TextEditor LogEditor;
        static bool       bLogEditorInit = false;
        static size_t     LastRenderedEntryCount = ~size_t(0); // forces the first-frame build below
        if (!bLogEditorInit)
        {
            LogEditor.SetLanguageDefinition(ConsoleLogLanguageDefinition());
            LogEditor.SetPalette(BuildConsoleLogPalette());
            LogEditor.SetReadOnly(true);
            LogEditor.SetImGuiChildIgnored(true);
            LogEditor.SetShowWhitespaces(false);
            bLogEditorInit = true;
        }

        // Bottom-right, completing the existing bottom row (System Registry at x=18, Game.dll Log at
        // x=506, both y=530 size 220 - kit/E29_Panel_SystemRegistry.h / plugin/E29_GamePluginLog.h) -
        // E27's own (1265, 18) default position was copied blindly at first and landed almost entirely
        // off the RIGHT edge of E29's own 1288-wide window (Level Tree already occupies x=[915,1275] at
        // that y), confirmed via a live screenshot. ImGuiCond_FirstUseEver only applies once ever
        // anyway (imgui.ini remembers wherever it actually ends up after that), but it should at least
        // START on-screen.
        ImGui::SetNextWindowPos(ImVec2(990, 530), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(280, 220), ImGuiCond_FirstUseEver);
        // "Commands" (was "Command Console") + an icon - direct user request, matching the same
        // tab-icon convention already applied elsewhere (Resources/Assets/Compilation/Plugins/Log).
        // E27_NodeOS has its own SEPARATE "Command Console" panel (NodeOS_UI_CommandConsole.h) -
        // deliberately untouched, a different example entirely.
        if (ImGui::Begin("\xEE\xA3\xBD Commands"))
        {
            // Applying a picked suggestion/history entry (or refocusing) must happen HERE, immediately
            // before the input widget is drawn - ImGui's InputText(Multiline) owns its own internal
            // edit buffer once active, and silently ignores an external write to CmdBuffer made AFTER
            // it's already been submitted this frame. SetKeyboardFocusHere(0) right before the call is
            // what makes it reload from CmdBuffer instead of keeping its own stale internal copy; the
            // paired bForceCursorEnd/callback (above) is what puts the cursor at the end of the newly
            // filled text. A dedicated bApplyPendingFill bool (rather than just checking
            // "!PendingFill.empty()") is what lets history-Down restore an EMPTY stashed line - an
            // empty PendingFill string still needs to be applied, it's not "nothing to do".
            if (bApplyPendingFill)
            {
                std::snprintf(CmdBuffer, sizeof(CmdBuffer), "%s", PendingFill.c_str());
                bApplyPendingFill = false;
                bRefocus = true;
                bForceCursorEnd = true;
            }
            if (bRefocus)
            {
                ImGui::SetKeyboardFocusHere(0);
                bRefocus = false;
            }

            const auto Routable = History.GetRoutableCommands(); // {m_FullName, m_Help} per command - see xundo_history.h

            // Computed BEFORE the input widget (not after, as a first version of this did) specifically
            // so its rendered height can be subtracted from the log's own height further down (the
            // input is now pinned to the BOTTOM - direct user request - so the log above it has to
            // leave room for it first, not just flow after it). The one cost is the rendered suggestion
            // list can lag a single frame behind a just-typed character - imperceptible in practice.
            // Capped at 5 (was 8) - direct user request to make the autocomplete popup a bit smaller;
            // also directly shrinks SuggestionsH below since fewer rows are ever rendered. Declared
            // outside the if-block so both this and the height calc further down share one definition.
            constexpr int MaxSuggestions = 5;
            std::vector<const xundo::history::routable_command*> Scored;
            if (CmdBuffer[0])
            {
                struct scored_t { const xundo::history::routable_command* m_pCmd; std::size_t m_Distance; };
                std::vector<scored_t> Ranked;
                Ranked.reserve(Routable.size());
                for (auto& Cmd : Routable)
                    Ranked.push_back({ &Cmd, xstrtool::SubstringDamerauLevenshteinDistanceI(CmdBuffer, Cmd.m_FullName) });
                std::sort(Ranked.begin(), Ranked.end(), [](auto& A, auto& B) { return A.m_Distance < B.m_Distance; });

                Scored.reserve(std::min<std::size_t>(Ranked.size(), MaxSuggestions));
                for (int i = 0; i < (int)Ranked.size() && i < MaxSuggestions; ++i)
                    Scored.push_back(Ranked[i].m_pCmd);
            }

            // Auto-grow height, direct user request ("similar to the input of an AI... if the line goes
            // too long it wraps and it grows in space until a max-n lines"): measured via CalcTextSize's
            // own wrap-width support rather than hand-rolling word-wrap - it returns exactly the height
            // the text will actually take once InputTextMultiline wraps it at the same width, so the box
            // grows in lockstep with what's really displayed instead of guessing from character count.
            //
            // History/Run now sit BESIDE the input (direct user request) rather than below it, both
            // matching its height - so their widths have to be reserved from the row's total width
            // BEFORE computing the input's own wrap width, not after.
            constexpr int   MinInputLines = 1;
            constexpr int   MaxInputLines = 6;
            const float     LineHeight    = ImGui::GetTextLineHeight();
            const float     FramePadY     = ImGui::GetStyle().FramePadding.y;
            const float     ItemSpacingX  = ImGui::GetStyle().ItemSpacing.x;
            const float     HistoryBtnW   = 36.0f; // icon-only now (no "History" label) - square-ish, matches the row's height
            const float     RunBtnW       = 56.0f;
            const float     ClearBtnW     = 28.0f; // only present when CmdBuffer has text - see below
            // GetContentRegionAvail() can come back with a stale, way-too-narrow value for this row -
            // confirmed live both on this window's very first appearing frame after launch AND again
            // after opening/closing the History popup (self-healing the instant any tab is switched
            // away and back, but otherwise sticking around) - an ImGui docking/popup-interaction
            // quirk, not a math error here. A single "is it absurdly small" threshold wasn't tight
            // enough to catch the post-popup case (it came back merely narrow, not tiny), so instead:
            // remember the last frame's PLAUSIBLE width (this panel is always docked many hundreds of
            // pixels wide in practice) and keep using that instead of a fresh-but-suspect reading,
            // rather than re-deriving from GetWindowWidth() which could be equally affected.
            static float s_LastGoodRowW = 0.0f;
            const float  RawRowW         = ImGui::GetContentRegionAvail().x;
            const float  PlausibleMinW   = HistoryBtnW + RunBtnW + 400.0f;
            const bool   bRowWPlausible  = RawRowW >= PlausibleMinW;
            const float  TotalRowW       = bRowWPlausible ? RawRowW
                                          : (s_LastGoodRowW > 0.0f ? s_LastGoodRowW : (ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x * 2.0f));
            if (bRowWPlausible) s_LastGoodRowW = RawRowW;
            // The Clear ("X") button only exists in the row while CmdBuffer has text (see below) -
            // checked here too so the wrap-width estimate (and the suggestions popup, which also only
            // ever shows while CmdBuffer has text) both size against the input's REAL width, not an
            // estimate that's ~ClearBtnW too generous.
            const bool      bHasTextEarly = CmdBuffer[0] != 0;
            const float     InputBoxW     = std::max(20.0f, TotalRowW - HistoryBtnW - RunBtnW - ItemSpacingX * 2.0f - (bHasTextEarly ? (ClearBtnW + ItemSpacingX) : 0.0f));
            const float     IconInset     = 20.0f; // left margin reserved for the ">" prompt glyph, below
            const float     InputWrapW    = std::max(1.0f, InputBoxW - IconInset - ImGui::GetStyle().FramePadding.x * 2.0f);
            const ImVec2    WrappedSize   = ImGui::CalcTextSize(CmdBuffer, nullptr, false, InputWrapW);
            const int       NeededLines   = std::clamp((int)std::ceil((WrappedSize.y + 0.5f) / LineHeight), MinInputLines, MaxInputLines);
            const float     InputBoxH     = NeededLines * LineHeight + FramePadY * 2.0f + 2.0f;

            // bSuggestionsWillRender mirrors EXACTLY the condition the suggestions block itself uses
            // further below - real bug, found live: SuggestionsH was reserving space whenever Scored was
            // merely non-empty, but the box only actually DRAWS when History isn't ALSO open (see
            // "Mutually exclusive with the History popup" below). With text typed and History open, that
            // mismatch reserved height for a box that never rendered - nothing filled the gap it left, so
            // the log stopped short and the whole row/popup sat too high, well short of the window's
            // true bottom edge. Reserving only what will actually draw removes the hole entirely.
            const bool  bSuggestionsWillRender = !Scored.empty() && !ImGui::IsPopupOpen("CmdHistoryPopup");
            const float SuggestionRowH  = 16.0f;
            const float SuggestionsH    = bSuggestionsWillRender ? (std::min<float>((float)MaxSuggestions, (float)Scored.size()) * SuggestionRowH + 6.0f) : 0.0f;
            const float Spacing         = ImGui::GetStyle().ItemSpacing.y;
            // ReservedBottom is ONLY what the Suggestions/Row zone itself needs - this MUST stay exactly
            // this, not grow, because it doubles as "how far the row's own zone sits from the window's
            // true bottom edge" (see the negative-height BeginChild below). A first attempt folded
            // HistoryPopupH straight into this value to stop the log overlapping the popup - but since
            // the row is positioned relative to this SAME number, that also silently pushed the row back
            // up away from the bottom (confirmed live: the row moved up when History was open with real
            // history entries, undoing the "always flush at the bottom" fix). HistoryPopupH is kept as
            // its own separate value instead, consumed explicitly as blank space ABOVE the row (see the
            // Dummy() below it), so it can never affect where the row itself ends up.
            const float ReservedBottom  = InputBoxH + SuggestionsH + Spacing * 2.0f; // no separate button row anymore - History/Run sit beside the input
            // Same reasoning as SuggestionsH just above, applied to the History popup instead - it's a
            // floating overlay (positioned via SetWindowPos, not normal layout flow), so nothing about
            // drawing it naturally reserves room for it against the log the way a real widget would;
            // without this, its top edge (which extends PopupH upward from the row) visibly overlapped
            // the log's own bottom instead of the two meeting cleanly - direct user follow-up. PopupH's
            // formula is duplicated from where the popup itself computes it further below (both derive
            // from CmdHistory.size() alone, so they can never disagree).
            const float HistoryPopupH   = ImGui::IsPopupOpen("CmdHistoryPopup") ? (std::clamp((float)CmdHistory.size() * 20.0f + 16.0f, 40.0f, 200.0f) + Spacing) : 0.0f;

            // Rebuilt only when the log actually grew/shrank (not every frame) - SetTextLines resets
            // TextEditor's internal colorizer range and scroll position, so doing it unconditionally
            // would fight the user's own scrolling every single frame.
            if (LogEntries.size() != LastRenderedEntryCount)
            {
                std::vector<std::string> Lines;
                for (auto& Entry : LogEntries)
                {
                    const char* Prefix = ConsoleLogLinePrefix(Entry.m_Source);
                    for (auto& Line : SplitLines(Entry.m_Text))
                        Lines.push_back(Prefix + Line);
                }
                LogEditor.SetTextLines(Lines);
                LogEditor.SetCursorPosition(TextEditor::Coordinates((int)Lines.size(), 0)); // scrolls to the newest entry
                LastRenderedEntryCount = LogEntries.size();
            }

            // A small "Clear Log" control above the log itself, left-aligned - History/Run no longer
            // have room to carry it (they're now sized to match the input row exactly, see below), and
            // this is a reasonable, unobtrusive home for a log-wide action.
            if (ImGui::SmallButton("Clear Log")) LogEntries.clear();

            // Log now renders FIRST (was last) - direct user request to move the input to the bottom,
            // matching a chat/terminal layout: scrollback on top, composer pinned below.
            //
            // Two prior approaches both failed to keep the row flush at the bottom (confirmed live via
            // a temporary bright-tinted window background: the WINDOW itself correctly filled all the
            // way down, but the row still stopped well short of that, meaning BOTH a plain
            // "GetContentRegionAvail().y - ReservedBottom" calc AND a manual "GetWindowHeight() -
            // ReservedBottom" calc were computing too small a value on this specific docked/tabbed
            // window). Switched to Dear ImGui's own canonical idiom for this exact console-with-a-
            // fixed-footer layout (the same technique imgui_demo.cpp's ShowExampleAppConsole uses) - a
            // NEGATIVE child height. ImGui computes "the real available space to the window's bottom
            // edge, right now, at this exact call" internally for a negative size rather than us
            // pre-computing it from a separately-queried window/region size that may not reflect the
            // window's true, currently-settled bounds on this particular kind of window.
            if (ImGui::BeginChild("##ConsoleLogChild", ImVec2(0.0f, -(ReservedBottom + HistoryPopupH)), true))
            {
                // Explicit empty callback, not Render's own defaulted decltype([](){}) - MSVC
                // independently re-evaluates a defaulted template default argument at each call
                // site, producing two DIFFERENT closure types for the same call and a hard error
                // (same issue GeneratedCodeEditor.Render's own call site works around).
                LogEditor.Render("##output", ImVec2(0, 0), false, [](){});
            }
            ImGui::EndChild();
            if (HistoryPopupH > 0.0f)
                ImGui::Dummy(ImVec2(0.0f, HistoryPopupH)); // consumes exactly the extra room reserved above, so the row itself still lands in the same ReservedBottom-sized zone as always

            // Suggestions - mouse-click-only now (see CommandConsoleCallback's own comment for why
            // keyboard cycling was removed rather than worked around). Renders ABOVE the input (it's at
            // the bottom) - matches how a chat/IDE/shell autocomplete popup anchored to a bottom
            // composer always opens upward, never off the bottom of the window. Indented/sized to land
            // exactly above the input box itself (not the full row, which also carries History/Run) -
            // direct user request - using the same HistoryBtnW/InputBoxW math the row below uses, since
            // the input box hasn't been drawn yet at this point in the frame.
            //
            // Mutually exclusive with the History popup (direct user request: "auto complete or History
            // should be open not both") - reuses bSuggestionsWillRender (computed above, alongside
            // SuggestionsH) rather than repeating the condition separately - the two had drifted apart
            // once already (see SuggestionsH's own comment) and must never be allowed to again.
            if (bSuggestionsWillRender)
            {
                // Suggestions only ever show while CmdBuffer has text (Scored is only populated then),
                // which means the Clear ("X") button is always present too whenever this runs - its
                // width has to be included in the offset or the suggestions box would drift left of the
                // input's real position.
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + RunBtnW + ItemSpacingX + HistoryBtnW + ItemSpacingX + ClearBtnW + ItemSpacingX);
                if (ImGui::BeginChild("##suggestions", ImVec2(InputBoxW, SuggestionsH), true))
                {
                    for (int i = 0; i < (int)Scored.size(); ++i)
                    {
                        auto& Cmd = *Scored[i];
                        ImGui::PushID(i);
                        if (ImGui::Selectable(Cmd.m_FullName.c_str()))
                        {
                            PendingFill = Cmd.m_FullName + " "; // trailing space - cursor lands ready for -args
                            bApplyPendingFill = true;
                        }
                        if (ImGui::IsItemHovered() && !Cmd.m_Help.empty())
                            ImGui::SetTooltip("%s", Cmd.m_Help.c_str());
                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();
            }

            // Run | History | Input, all one row, all sharing the input's own (auto-grown) height -
            // direct user request, simplified after two earlier layouts both had real problems: Run
            // submits, same as pressing Enter; History opens the click-to-recall popup (further below).
            // Same history glyph as the Asset/Resource browsers' own history button (E10_AssetBrowser.h's
            // "\xee\xa5\xb2", opened via assert_browser::RenderPathHistoryPopup) - direct user request to
            // keep the icon language consistent across panels, no "History" text label (that panel has
            // never used one either).
            const bool bRunClicked = ImGui::Button("Run", ImVec2(RunBtnW, InputBoxH));
            ImGui::SameLine();

            if (ImGui::Button("\xee\xa5\xb2", ImVec2(HistoryBtnW, InputBoxH)))
                bShowHistoryPopup = true;
            ImGui::SameLine();

            // Clear ("X") button - a REAL, separate widget drawn BEFORE the input, same structural
            // pattern as the Asset/Resource browsers' own search box (E10_AssetBrowser.h's
            // RenderSearchBar: its own "X" button is likewise a plain sequential widget to the LEFT of
            // the InputText, never overlaid on top of it). The earlier version drew this "X" as an
            // overlay AFTER InputTextMultiline, positioned on top of the input's own rect via
            // SetCursorScreenPos - ImGui gives hover/click priority to the FIRST item submitted at a
            // given screen position, so every click there was silently swallowed by the input box
            // underneath (confirmed live: CmdBuffer never actually cleared, even after adding
            // SetNextItemAllowOverlap). Drawing it as its own widget in the row sidesteps the whole
            // class of bug instead of fighting it.
            const bool  bHasText  = CmdBuffer[0] != 0;
            if (bHasText)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                if (ImGui::Button("X##clearcmd", ImVec2(ClearBtnW, InputBoxH)))
                    CmdBuffer[0] = 0;
                ImGui::PopStyleColor();
                ImGui::SameLine();
            }

            // The input itself - InputTextMultiline (not InputText) is what makes wrapping/growing
            // possible at all; ImGuiInputTextFlags_CtrlEnterForNewLine flips multiline's own default
            // Enter-inserts-a-newline behavior so plain Enter submits instead (matching every chat
            // composer's convention) - there's never a reason for a literal newline in a command
            // string here, so Ctrl+Enter's own "add a newline instead" escape hatch simply goes unused.
            // No CallbackHistory - see CommandConsoleCallback's own comment for why that's gone for good
            // reason, not just simplified away.
            //
            // ImGuiInputTextFlags_WordWrap is REQUIRED - Multiline does NOT word-wrap by default (a long
            // line just runs off the right edge/scrolls horizontally instead); imgui_widgets.cpp only
            // wraps when this flag is explicitly set (asserted to require Multiline and to be
            // incompatible with Password mode, neither of which applies here). Missing this flag is why
            // long commands weren't visually wrapping despite the box correctly growing taller.
            //
            // Drawn LAST, with size.x == -1.0f - NEGATIVE, not 0: ImGui's CalcItemSize treats exactly
            // 0.0f as "use the default item width" (unrelated to available space), and only a negative
            // value as "fill remaining content region" (region_max.x + size.x). Passing plain 0.0f here
            // first (a real bug, not just a cosmetic miss) made the box stop hundreds of pixels short of
            // the right edge - confirmed live. -1 is the same "stretch to the edge" idiom already used
            // elsewhere in this codebase (e.g. E29_LevelSceneEditorKit.h's own ImVec2(-1, 0) buttons).
            bool bEnter = ImGui::InputTextMultiline("##cmd", CmdBuffer, sizeof(CmdBuffer), ImVec2(-1.0f, InputBoxH)
                , ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine | ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_WordWrap
                , CommandConsoleCallback, &bForceCursorEnd);
            const bool bInputActive = ImGui::IsItemActive();
            const ImVec2 InputBoxMin = ImGui::GetItemRectMin();
            const ImVec2 InputBoxMax = ImGui::GetItemRectMax();

            // Placeholder text - still an overlay drawn ON TOP of the input (matching the Asset/
            // Resource browsers' own magnifying-glass icon, RenderSearchBar above), which is safe here
            // because plain TextUnformatted is not interactive - there's no click to steal, unlike the
            // clear button above.
            {
                const ImVec2& BoxMin = InputBoxMin;

                if (!bInputActive && !bHasText)
                {
                    // Doubles as the old top-of-panel hint line (removed as its own row now that
                    // space is tighter with the input at the bottom) - placeholder text INSIDE the box
                    // is the more "AI input"-like way to surface it anyway: present when there's
                    // nothing typed yet, gone the instant you start.
                    const ImVec2 Saved = ImGui::GetCursorScreenPos();
                    ImGui::SetCursorScreenPos(ImVec2(BoxMin.x + 6.0f, BoxMin.y + FramePadY));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
                    ImGui::TextUnformatted("> E29/Edit|Query/<Cmd> -args..."); // a plain chevron, not a Segoe MDL2 glyph - guaranteed to render in any font, no icon-tofu risk
                    ImGui::PopStyleColor();
                    ImGui::SetCursorScreenPos(Saved);
                }

                // A real, immediate-abort assert (imgui.cpp:11319 - "Code uses SetCursorPos()/
                // SetCursorScreenPos() to extend window/parent boundaries. Please submit an item e.g.
                // Dummy() afterwards") fires if Input ends up the last widget submitted in the row and
                // nothing follows the SetCursorScreenPos restore above. A zero-size Dummy() is ImGui's
                // own documented fix - registers the restored cursor position as a real item. Harmless
                // (and still necessary) even when the placeholder branch above didn't run.
                ImGui::Dummy(ImVec2(0.0f, 0.0f));
            }

            if (bInputActive && ImGui::IsKeyPressed(ImGuiKey_Escape))
                CmdBuffer[0] = 0;

            // History popup - direct user follow-up, matching the Asset Browser's own path-history
            // popup (E10_AssetBrowser.h's RenderPathHistoryPopup): the History button opens a
            // click-to-recall list instead of arrow-key browsing (which multiline can't support - see
            // CommandConsoleCallback's own comment). Anchored to the INPUT box's own rect (not the
            // button's) - direct user request that it land directly above the text window with the
            // same width as the text window, same as the suggestions popup above - so it never renders
            // off the bottom of the window and lines up with the input rather than the narrow button.
            // A plain BeginPopup (not modal/menu) always carries ImGuiWindowFlags_AlwaysAutoResize
            // internally, which silently ignores SetNextWindowSize (confirmed live: the popup kept
            // auto-shrinking to fit its own text instead of matching the input box's width) -
            // SetNextWindowSizeConstraints is what actually wins against AutoResize, and has to be
            // reissued every frame the popup is open (not just the opening frame), since AutoResize
            // keeps re-fitting the window every single frame otherwise.
            const float PopupW = InputBoxMax.x - InputBoxMin.x;
            const float PopupH = std::clamp((float)CmdHistory.size() * 20.0f + 16.0f, 40.0f, 200.0f);
            if (bShowHistoryPopup)
            {
                ImGui::SetNextWindowPos(ImVec2(InputBoxMin.x, InputBoxMin.y - PopupH));
                ImGui::OpenPopup("CmdHistoryPopup");
                bShowHistoryPopup = false;
            }
            ImGui::SetNextWindowSizeConstraints(ImVec2(PopupW, PopupH), ImVec2(PopupW, PopupH));
            if (ImGui::BeginPopup("CmdHistoryPopup"))
            {
                // Force position directly on the now-open window every frame (SetWindowPos, not
                // SetNextWindowPos) - confirmed live that the SetNextWindowPos issued at OpenPopup time
                // was NOT enough on its own: the popup still rendered BELOW the input row instead of
                // above it, evidence a plain popup's own positioning (it normally anchors near the mouse/
                // reference position that triggered it) was winning out over our one-time placement.
                // Reasserting the position on the already-open window, every frame, sidesteps whatever
                // is overriding it rather than fighting to find the exact cause.
                ImGui::SetWindowPos(ImVec2(InputBoxMin.x, InputBoxMin.y - PopupH));

                // NOTE: deliberately NOT auto-closing this just because Scored is non-empty - that was
                // a real bug (found and reverted): Scored reflects CmdBuffer's CONTENT, not "the user is
                // typing right now", so if text was already in the buffer (which is exactly why
                // Suggestions were showing in the first place) clicking History would open this popup
                // and then immediately self-close on the very same condition, one frame later - History
                // could never actually stay open in the exact scenario it's meant for. The Suggestions
                // block's own "!IsPopupOpen(history)" gate already fully covers "History wins when
                // explicitly opened" (see above); dismissing THIS popup is left to ImGui's normal popup
                // lifecycle (click outside, Escape, or picking an entry), same as every other popup here.
                if (CmdHistory.empty())
                    ImGui::TextDisabled("No history yet");
                else
                {
                    for (auto It = CmdHistory.rbegin(); It != CmdHistory.rend(); ++It) // most recent first
                    {
                        if (ImGui::Selectable(It->c_str()))
                        {
                            PendingFill = *It + " ";
                            bApplyPendingFill = true;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
                ImGui::EndPopup();
            }

            if ((bEnter || bRunClicked) && CmdBuffer[0])
            {
                std::string_view Cmd(CmdBuffer);
                // Trim trailing whitespace the autocomplete fill-in leaves behind.
                while (!Cmd.empty() && Cmd.back() == ' ') Cmd.remove_suffix(1);

                if (CmdHistory.empty() || CmdHistory.back() != Cmd)
                    CmdHistory.emplace_back(Cmd);

                LogEntries.push_back({ std::string(Cmd), console_log_source::User });
                if (std::string Result = ProcessConsoleCommand(Cmd, History, Routable); !Result.empty())
                    LogEntries.push_back({ std::move(Result), console_log_source::System });
                CmdBuffer[0] = 0;
                bRefocus = true; // applied at the top of the panel on the NEXT frame - see bApplyPendingFill's own comment above
            }
        }
        ImGui::End();
    }
}

#endif // E29_PANEL_COMMAND_CONSOLE_H
