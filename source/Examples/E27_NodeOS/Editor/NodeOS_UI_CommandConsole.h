#pragma once
// Command Console UI + named-pipe bridge, extracted from the monolithic E27_NodeOS_Editor.cpp
// (header #10): console_log_entry/ConsoleLogTokenize, palette/autocomplete scoring,
// ProcessConsoleCommand, DrawCommandConsolePanel, command_console_pipe_bridge,
// CommandConsolePipeThreadMain, PumpCommandConsolePipe.
#include "NodeOS_Common.h"
#include "NodeOS_Types.h"

namespace nodeos
{
    // Debugging/AI-facing entry point for xundo::history::Route (xundo_history.h) - a plain text box
    // for "Namespace/Edit-or-Query/Command -args..." strings, e.g. "NodeOS/Query/ListNodes" or
    // "NodeOS/Edit/Connect -Id ... ". Exists specifically so a query command never NEEDS a bespoke
    // ImGui widget to be reachable - the same text protocol an AI or a script would use to drive this
    // works here too, with the same self-documenting "-h" help every command already exposes.
    //
    // Help and autocomplete both key off xundo::history::GetRoutableCommands() - every registered
    // command's fully-qualified name ("NodeOS/Edit/Connect") paired with its own one-line
    // getCommandHelp() text, gathered fresh each call (command counts here are small - dozens, not
    // thousands - so there's no need to cache this across frames).
    //
    // Autocomplete scoring reuses the same weighted substring/prefix Damerau-Levenshtein distance
    // E10_TextureResourcePipeline's asset browser already uses for its own fuzzy search box
    // (E10_asset_browser_virtual_tree_tab.h) - xstrtool::SubstringDamerauLevenshteinDistanceI - rather
    // than inventing a second fuzzy-match convention. The result UI is deliberately much lighter than
    // that asset browser's icon grid/popup: a plain Selectable() list under the input is all a text
    // command needs.
    // Who authored a Command Console log entry - the log's own "> Cmd" echo line is colored by this
    // (User=green if typed into the UI, Pipe=teal if it arrived over NodeOSCLI's named pipe - an
    // AI-facing color on purpose); the RESULT that follows is always logged as its own separate
    // System-sourced entry (white), since the app authored that text, not whoever typed the command.
    // DrawCommandConsolePanel and PumpCommandConsolePipe both push an echo entry then, if non-empty,
    // a result entry - never one blended entry.
    enum class console_log_source { System, User, Pipe };
    struct console_log_entry
    {
        std::string         m_Text;
        console_log_source  m_Source;
    };

    // TextEditor (E19_TextEditor.h, already used for the "Generated C++" panel) is a real code-editor
    // widget: unlike InputTextMultiline it can color individual lines AND still give one continuous
    // mouse-drag/copy selection across the whole log - the two things a naive "one InputTextMultiline
    // per colored entry" approach couldn't do at once. Coloring is keyed off a plain-text marker
    // prefixed onto each User/Pipe echo line ("> "/"$ ") rather than any out-of-band per-line state,
    // because TextEditor's own tokenizer callback (LanguageDefinition::mTokenize) is a stateless C
    // function pointer with no way to be told which log entry a line came from - the marker IS the
    // only channel available. System lines carry no marker and fall through to the Default color.
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
    // palette color ahead of mTokenize's own result (TextEditor::GetGlyphColor checks mComment/
    // mMultiLineComment first, before mColorIndex) - pointing all three at a marker that will never
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
    // get_log_query_cmd's flattened CLI text agree - m_Text itself is always the RAW command/result
    // text with no marker baked in, exactly so there's only one place this happens, not two.
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

    // UserData for CommandConsoleCallback, below - built fresh each frame from DrawCommandConsolePanel's
    // own local state, since a single free-function callback can't capture anything.
    struct command_console_nav_state
    {
        const std::vector<const xundo::history::routable_command*>* m_pScored;
        std::vector<std::string>*                                   m_pCmdHistory;
        int*                                                         m_pHighlighted;
        int*                                                         m_pHistoryPos;
        std::string*                                                 m_pHistoryStash;
        bool*                                                        m_pJustFilled;
        bool*                                                        m_pForceCursorEnd;
    };

    // Two jobs, dispatched by pData->EventFlag:
    //
    // CallbackAlways fires every frame the widget is active - forces the cursor to the end of the text
    // right after a programmatic buffer rewrite (a suggestion click, applied on the NEXT frame via
    // DrawCommandConsolePanel's own bApplyPendingFill/SetKeyboardFocusHere dance), since ImGui doesn't
    // reliably land it there on every version/path by default.
    //
    // CallbackHistory fires specifically when the WIDGET ITSELF is active and Up/Down are pressed -
    // this is the one correct way to handle arrow-key history/suggestion nav in an InputText. Polling
    // ImGui::IsKeyPressed(ImGuiKey_DownArrow) after the fact does NOT work here: this app has ImGui's
    // keyboard navigation enabled, which claims Up/Down globally to move focus between WIDGETS unless
    // something has already claimed those keys for itself - CallbackHistory is InputText's own,
    // documented way to make that claim, exactly for shell-style history use cases like this one.
    // Mutating the buffer via pData->DeleteChars/InsertChars (rather than writing CmdBuffer directly)
    // is what makes the change take effect immediately, from inside the widget's own active edit
    // session - no deferred "next frame" trick needed here, unlike the mouse-click pick path.
    static int CommandConsoleCallback(ImGuiInputTextCallbackData* pData)
    {
        auto* pState = static_cast<command_console_nav_state*>(pData->UserData);

        if (pData->EventFlag == ImGuiInputTextFlags_CallbackAlways)
        {
            if (*pState->m_pForceCursorEnd)
            {
                pData->CursorPos = pData->SelectionStart = pData->SelectionEnd = pData->BufTextLen;
                *pState->m_pForceCursorEnd = false;
            }
            return 0;
        }

        if (pData->EventFlag == ImGuiInputTextFlags_CallbackHistory)
        {
            auto& Scored       = *pState->m_pScored;
            auto& CmdHistory   = *pState->m_pCmdHistory;
            auto& Highlighted  = *pState->m_pHighlighted;
            auto& HistoryPos   = *pState->m_pHistoryPos;
            auto& HistoryStash = *pState->m_pHistoryStash;

            const bool bUp              = pData->EventKey == ImGuiKey_UpArrow;
            const bool bBrowsingHistory = HistoryPos != -1;

            // Prioritizes continuing an already-open history browse (so it doesn't flip back to
            // suggestion-nav just because a recalled line happens to also fuzzy-match something),
            // otherwise: text with live suggestions browses THOSE, an empty box walks CmdHistory.
            if ((bBrowsingHistory || Scored.empty()) && !CmdHistory.empty())
            {
                std::string NewText;
                if (bUp)
                {
                    if (HistoryPos == -1) { HistoryStash.assign(pData->Buf, pData->BufTextLen); HistoryPos = (int)CmdHistory.size() - 1; }
                    else                    HistoryPos = std::max(0, HistoryPos - 1);
                    NewText = CmdHistory[HistoryPos];
                }
                else // Down
                {
                    if (!bBrowsingHistory) return 0; // nothing to recall forward to - stay put
                    ++HistoryPos;
                    if (HistoryPos >= (int)CmdHistory.size()) { HistoryPos = -1; NewText = HistoryStash; }
                    else                                        NewText = CmdHistory[HistoryPos];
                }
                pData->DeleteChars(0, pData->BufTextLen);
                pData->InsertChars(0, NewText.c_str()); // also advances CursorPos to the end - "ready to add whatever you like"
                *pState->m_pJustFilled = true;
            }
            else if (!Scored.empty())
            {
                if (bUp) Highlighted = (Highlighted <= 0) ? (int)Scored.size() - 1 : Highlighted - 1;
                else     Highlighted = (Highlighted + 1 >= (int)Scored.size()) ? 0 : Highlighted + 1;
            }
            return 0;
        }

        return 0;
    }

    // Shared by DrawCommandConsolePanel's own Enter/Run handling AND the named-pipe server (below) -
    // one place implementing "help"/"<cmd> -h"/plain routing, so a pipe-driven command (from
    // NodeOSCLI, or any other external caller) behaves identically to one typed into the UI, not a
    // second, silently-drifting copy of the same dispatch logic.
    static std::string ProcessConsoleCommand(std::string_view Cmd, xundo::history& History, const std::vector<xundo::history::routable_command>& Routable)
    {
        std::string Out;
        if (Cmd == "help" || Cmd == "Help" || Cmd == "?")
        {
            for (auto& C : Routable)
                Out += (C.m_Help.empty() ? C.m_FullName : std::format("{} - {}", C.m_FullName, C.m_Help)) + "\n";
        }
        else if (Cmd.size() > 2 && Cmd.substr(Cmd.size() - 2) == "-h")
        {
            // Bypasses Route() here on purpose: the underlying command's own "-h" handling
            // (xundo::system::Execute/Query) writes straight to std::cout via xcmdline::
            // parser::printHelp() - nothing this GUI process's console window (or a pipe client)
            // ever sees - so look the help text up directly instead of dispatching.
            std::string_view FullName = Cmd.substr(0, Cmd.find(' '));
            Out = History.GetCommandHelpFor(FullName) + "\n";
        }
        else
        {
            // Route()'s empty-string return is ambiguous on purpose for Edit commands ("ran fine,
            // nothing worth reporting" - xundo::system::Execute's own convention), but that's wrong
            // for a Query: an empty answer ("no nodes") is itself real, useful information, not
            // "nothing happened" - silently swallowing it here is exactly what made a genuinely-
            // empty ListNodes result look like the command did nothing at all. Only Query commands
            // get an explicit placeholder for an empty answer; Edit commands keep the existing
            // silent-success convention.
            std::string Result = History.Route(Cmd);
            if (!Result.empty())
                Out = Result + "\n";
            else if (Cmd.find("/Query/") != std::string_view::npos)
                Out = "(empty result)\n";
        }
        return Out;
    }

    static void DrawCommandConsolePanel(xundo::history& History, std::vector<console_log_entry>& LogEntries)
    {
        static char                     CmdBuffer[256] = "";
        static std::string              PrevCmdBuffer;      // detects genuine typing vs. a programmatic fill, below
        // LogEntries is owned by the caller (E27_Example), not a local static here - the named-pipe
        // server (below) needs to append to the SAME visible log a pipe-driven command isn't a
        // secret side-channel from a UI reading over your shoulder.
        static std::string              PendingFill;        // value to apply into CmdBuffer, see bApplyPendingFill
        static bool                     bApplyPendingFill = false;
        static bool                     bRefocus  = false;
        static bool                     bForceCursorEnd = false; // consumed by CommandConsoleCallback's CallbackAlways branch, above
        static int                      Highlighted = -1;   // keyboard-selected suggestion row, -1 = none
        static std::vector<std::string> CmdHistory;         // previously RUN command lines, oldest first - shell-style recall
        static int                      HistoryPos = -1;    // index into CmdHistory currently shown, -1 = not browsing (fresh line)
        static std::string              HistoryStash;       // what was typed before Up first opened history browsing, restored on Down past the newest entry

        // One persistent TextEditor for the whole log's lifetime - matching the established
        // "Inspector must persist across frames" pattern (rebuilding a widget like this every frame
        // makes its internal state, here the colorizer's cached ranges and the scroll position,
        // unstable). SetImGuiChildIgnored(true) is what lets Render() below draw straight into a
        // BeginChild WE own (so it lives inside this "Command Console" panel, not as its own
        // separate top-level window - contrast with GeneratedCodeEditor, which IS its own window).
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

        ImGui::SetNextWindowPos(ImVec2(1265, 610), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(340, 260), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Command Console"))
        {
            ImGui::TextDisabled("NodeOS/Edit/<Cmd> or NodeOS/Query/<Cmd> - Down/Up: suggestions, or history when empty");

            // Applying a picked suggestion/history entry (or refocusing) must happen HERE, immediately
            // before InputText is drawn - ImGui's InputText owns its own internal edit buffer once
            // active, and silently ignores an external write to CmdBuffer made AFTER it's already been
            // submitted this frame. SetKeyboardFocusHere(0) right before the call is what makes
            // InputText reload from CmdBuffer instead of keeping its own stale internal copy; the
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

            // Computed BEFORE InputText (not after, as a first version of this did) specifically so
            // CommandConsoleCallback's CallbackHistory handling - which fires DURING the InputText
            // call below - has this frame's ranking ready when Up/Down are pressed. The one cost is
            // that the rendered suggestion list can lag a single frame behind a just-typed character;
            // arrow-key nav actually working is worth that trade.
            std::vector<const xundo::history::routable_command*> Scored;
            if (CmdBuffer[0])
            {
                struct scored_t { const xundo::history::routable_command* m_pCmd; std::size_t m_Distance; };
                std::vector<scored_t> Ranked;
                Ranked.reserve(Routable.size());
                for (auto& Cmd : Routable)
                    Ranked.push_back({ &Cmd, xstrtool::SubstringDamerauLevenshteinDistanceI(CmdBuffer, Cmd.m_FullName) });
                std::sort(Ranked.begin(), Ranked.end(), [](auto& A, auto& B) { return A.m_Distance < B.m_Distance; });

                const int MaxSuggestions = 8;
                Scored.reserve(std::min<std::size_t>(Ranked.size(), MaxSuggestions));
                for (int i = 0; i < (int)Ranked.size() && i < MaxSuggestions; ++i)
                    Scored.push_back(Ranked[i].m_pCmd);
            }

            static bool bJustFilled = false; // set by CommandConsoleCallback's history branch, or the mouse-pick paths below
            command_console_nav_state NavState{ &Scored, &CmdHistory, &Highlighted, &HistoryPos, &HistoryStash, &bJustFilled, &bForceCursorEnd };

            bool bEnter = ImGui::InputText("##cmd", CmdBuffer, sizeof(CmdBuffer)
                , ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_CallbackHistory
                , CommandConsoleCallback, &NavState);
            const bool bInputActive = ImGui::IsItemActive();
            ImGui::SameLine();
            bool bRunClicked = ImGui::SmallButton("Run");
            if (ImGui::SmallButton("Clear")) LogEntries.clear();

            // Distinguishes genuine typing from a programmatic fill (either CommandConsoleCallback's
            // own history-recall, just above, or a mouse-pick below) - only real typing should drop
            // the keyboard-highlighted suggestion and exit history-browsing mode.
            const bool bUserEdited = (CmdBuffer != PrevCmdBuffer) && !bJustFilled;
            bJustFilled = false;
            if (bUserEdited) { Highlighted = -1; HistoryPos = -1; }

            if (bInputActive && ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                CmdBuffer[0] = 0;
                Highlighted = -1;
                HistoryPos  = -1;
            }

            bool bPickedByKeyboard = false;
            if (!Scored.empty())
            {
                const float RowH = 18.0f;
                if (ImGui::BeginChild("##suggestions", ImVec2(0, std::min<float>(8, (float)Scored.size()) * RowH + 4), true))
                {
                    for (int i = 0; i < (int)Scored.size(); ++i)
                    {
                        auto& Cmd = *Scored[i];
                        ImGui::PushID(i);
                        if (ImGui::Selectable(Cmd.m_FullName.c_str(), Highlighted == i))
                        {
                            PendingFill = Cmd.m_FullName + " "; // trailing space - cursor lands ready for -args
                            bApplyPendingFill = true; bJustFilled = true;
                            Highlighted = -1;
                        }
                        if (Highlighted == i) ImGui::SetScrollHereY();
                        if (ImGui::IsItemHovered() && !Cmd.m_Help.empty())
                            ImGui::SetTooltip("%s", Cmd.m_Help.c_str());
                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();

                // Enter while a row is highlighted confirms THAT row instead of running the raw text -
                // matches how every other autocomplete (shell, IDE, address bar) treats Enter-on-a-
                // highlighted-item as "accept the suggestion," not "submit whatever's literally typed."
                if (bEnter && Highlighted >= 0)
                {
                    PendingFill = Scored[Highlighted]->m_FullName + " ";
                    bApplyPendingFill = true; bJustFilled = true;
                    Highlighted = -1;
                    bPickedByKeyboard = true;
                }
            }

            PrevCmdBuffer.assign(CmdBuffer);

            if (!bPickedByKeyboard && (bEnter || bRunClicked) && CmdBuffer[0])
            {
                std::string_view Cmd(CmdBuffer);
                // Trim trailing whitespace the autocomplete fill-in leaves behind.
                while (!Cmd.empty() && Cmd.back() == ' ') Cmd.remove_suffix(1);

                if (CmdHistory.empty() || CmdHistory.back() != Cmd)
                    CmdHistory.emplace_back(Cmd);
                HistoryPos = -1;

                LogEntries.push_back({ std::string(Cmd), console_log_source::User });
                if (std::string Result = ProcessConsoleCommand(Cmd, History, Routable); !Result.empty())
                    LogEntries.push_back({ std::move(Result), console_log_source::System });
                CmdBuffer[0] = 0;
                Highlighted = -1;
                bRefocus = true; // applied at the top of the panel on the NEXT frame - see bApplyPendingFill's own comment above
            }

            ImGui::Separator();

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

            // Own BeginChild (rather than letting Render() open its own top-level window) is what
            // SetImGuiChildIgnored(true), above, expects - this way the log lives inside the Command
            // Console panel, sharing space with the input box and suggestions above it.
            if (ImGui::BeginChild("##ConsoleLogChild", ImGui::GetContentRegionAvail(), true))
            {
                // Explicit empty callback, not Render's own defaulted decltype([](){}) - MSVC
                // independently re-evaluates a defaulted template default argument at each call
                // site, producing two DIFFERENT closure types for the same call and a hard error
                // (same issue GeneratedCodeEditor.Render's own call site works around).
                LogEditor.Render("##output", ImVec2(0, 0), false, [](){});
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

    //------------------------------------------------------------------------------------------------
    // Named-pipe server for NodeOSCLI.cpp - lets an external process (a script, an AI) drive the same
    // Route()/help dispatch the Command Console panel uses, without any UI automation at all: connect
    // to \\.\pipe\E27_NodeOS_Console, write one command line, read back the response, disconnect.
    //
    // Runs on its own background thread for the app's whole lifetime (launched once from E27_Example,
    // detached - this is a local dev/debug feature, not something that needs a graceful shutdown path;
    // the thread dies with the process). CreateNamedPipe/ConnectNamedPipe/ReadFile/WriteFile all block,
    // which is fine on a DEDICATED thread but must never run on the main render thread.
    //
    // The actual command dispatch can't happen on this pipe thread, though: Route() ultimately calls
    // into Redo()/Query() implementations that read/mutate the SAME Nodes/Links/etc. the main thread
    // is drawing and editing every frame - running that from a second thread would race. So this
    // thread only ever reads the request text and hands it to the main thread via a small mutex+
    // condition_variable bridge (command_console_pipe_bridge, below); the main loop's own per-frame
    // check (see E27_Example) does the actual ProcessConsoleCommand() call and posts the answer back.
    //------------------------------------------------------------------------------------------------
    struct command_console_pipe_bridge
    {
        std::mutex              m_Mutex;
        std::condition_variable m_Cond;
        bool                    m_bHasRequest  = false; // pipe thread -> main thread: a command is waiting
        bool                    m_bHasResponse = false; // main thread -> pipe thread: the answer is ready
        std::string             m_Request;
        std::string             m_Response;
    };

    static void CommandConsolePipeThreadMain(command_console_pipe_bridge& Bridge)
    {
        for (;;)
        {
            HANDLE hPipe = CreateNamedPipeA(
                "\\\\.\\pipe\\E27_NodeOS_Console",
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                1,      // one client at a time - NodeOSCLI is a one-shot connect/send/read/exit tool
                65536, 65536,
                0, nullptr);
            if (hPipe == INVALID_HANDLE_VALUE)
            {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            const BOOL bConnected = ConnectNamedPipe(hPipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
            if (bConnected)
            {
                std::string Request;
                char        Buf[4096];
                DWORD       BytesRead = 0;
                while (ReadFile(hPipe, Buf, sizeof(Buf) - 1, &BytesRead, nullptr) && BytesRead > 0)
                {
                    Buf[BytesRead] = 0;
                    Request += Buf;
                    if (Request.find('\n') != std::string::npos) break;
                }
                while (!Request.empty() && (Request.back() == '\n' || Request.back() == '\r')) Request.pop_back();

                std::string Response;
                if (!Request.empty())
                {
                    std::unique_lock<std::mutex> Lock(Bridge.m_Mutex);
                    Bridge.m_Request      = Request;
                    Bridge.m_bHasRequest  = true;
                    Bridge.m_bHasResponse = false;
                    Bridge.m_Cond.notify_all();
                    Bridge.m_Cond.wait(Lock, [&] { return Bridge.m_bHasResponse; });
                    Response = Bridge.m_Response;
                }

                DWORD BytesWritten = 0;
                WriteFile(hPipe, Response.data(), static_cast<DWORD>(Response.size()), &BytesWritten, nullptr);
                FlushFileBuffers(hPipe);
            }
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
        }
    }

    // Called once per frame from E27_Example's main loop - the only place that's actually safe to run
    // ProcessConsoleCommand() from, since it touches the same graph data the rest of the frame does.
    // Appends into LogEntries too (the SAME vector DrawCommandConsolePanel renders) so a pipe-driven
    // command is visible in the UI exactly like one typed there - never a silent side-channel. Tagged
    // console_log_source::Pipe rather than User so it renders in its own distinct color ("$ " marker,
    // teal) - see console_log_source's own comment for why that's a real, meaningful distinction and
    // not just decoration.
    static void PumpCommandConsolePipe(command_console_pipe_bridge& Bridge, xundo::history& History, std::vector<console_log_entry>& LogEntries)
    {
        std::unique_lock<std::mutex> Lock(Bridge.m_Mutex, std::try_to_lock);
        if (!Lock.owns_lock() || !Bridge.m_bHasRequest || Bridge.m_bHasResponse)
            return;

        const std::string Cmd = Bridge.m_Request;
        Lock.unlock();

        const auto Routable = History.GetRoutableCommands();
        const std::string Result = ProcessConsoleCommand(Cmd, History, Routable);

        LogEntries.push_back({ Cmd, console_log_source::Pipe });
        if (!Result.empty())
            LogEntries.push_back({ Result, console_log_source::System });

        Lock.lock();
        Bridge.m_Response     = Result;
        Bridge.m_bHasResponse = true;
        Bridge.m_bHasRequest  = false;
        Lock.unlock();
        Bridge.m_Cond.notify_all();
    }
}
