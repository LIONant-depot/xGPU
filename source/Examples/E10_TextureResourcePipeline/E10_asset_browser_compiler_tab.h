#ifndef ASSERT_BROWSER_COMPILER_TAB
#define ASSERT_BROWSER_COMPILER_TAB
#pragma once
#include "E10_AssetBrowser.h"
#include "E10_AssetMgr.h"
#include "imgui.h"
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#   define IMGUI_DEFINE_MATH_OPERATORS // Allows ImVec2 arithmetic
#endif
#include "imgui_internal.h"       // For TreeNodeBehavior, GetIDWithSeed, etc.

namespace e10
{
    //=============================================================================

    static bool ScaleButton(const char* pTxt, float Scale)
    {
        float old_font_size = ImGui::GetFont()->Scale;
        ImGui::GetFont()->Scale *= Scale;
        ImGui::PushFont(ImGui::GetFont());
        bool x = ImGui::Button(pTxt);
        ImGui::GetFont()->Scale = old_font_size;
        ImGui::PopFont();
        return x;
    }

    //=============================================================================

    static void ScaleText(const char* pTxt, float Scale)
    {
        float old_font_size = ImGui::GetFont()->Scale;
        ImGui::GetFont()->Scale *= Scale;
        ImGui::PushFont(ImGui::GetFont());
        ImGui::Text(pTxt);
        ImGui::GetFont()->Scale = old_font_size;
        ImGui::PopFont();
    }

    //=============================================================================

    ImVec2 CalcWrappedTextSize(const char* text, float wrap_width) {
        if (!text || wrap_width <= 0.0f) return ImVec2(0.0f, 0.0f);
        ImVec2 total_size(0.0f, 0.0f);
        const char* line_start = text;
        const ImGuiStyle& style = ImGui::GetStyle();
        float line_height = ImGui::GetTextLineHeightWithSpacing();
        int total_lines = 0;

        while (*line_start) {
            const char* line_end = line_start;
            while (*line_end && *line_end != '\n') ++line_end;

            // Calculate wrapped lines for this segment
            ImVec2 line_size = ImGui::CalcTextSize(line_start, line_end, false, wrap_width);
            int wrapped_lines = std::max(1, (int)std::ceil(line_size.x / wrap_width));
            total_size.x = std::max(total_size.x, std::min(line_size.x, wrap_width));
            total_size.y += wrapped_lines * line_height;
            total_lines += wrapped_lines;

            line_start = (*line_end == '\n') ? line_end + 1 : line_end;
        }

        // Add child window padding
        total_size.y += style.WindowPadding.y * 2;
        return total_size;
    }

    //=============================================================================

    std::string_view get_last_line(const std::string& str)
{
        if (str.empty()) return "";
        auto pos = str.rfind('\n');
        if (pos == std::string::npos) return str; // No newline, return entire string
        return std::string_view(str.data() + pos + 1, str.size() - (pos + 1)); // View from last newline to end
    }

    //=============================================================================
    inline
    float ComputeWidthComboBox(std::span< const char* const> items)
    {
        float max_width = 0.0f;
        for (const char* item : items)
        {
            ImVec2 text_size = ImGui::CalcTextSize(item);
            max_width = std::max(max_width, text_size.x);
        }

        // Add padding for dropdown arrow and frame
        max_width += ImGui::GetStyle().FramePadding.x * 2 + 20.0f; // Adjust 20.0f for arrow

        return max_width;
    }

    //=============================================================================

    [[nodiscard]] std::string ConvertHexEscapes(const std::string_view input) noexcept
    {
        constexpr static auto hex_lookup = []()consteval
            {
                std::array<std::uint8_t, 256> arr{};
                for (auto& val : arr) val = 255; // Invalid by default
                for (char c = '0'; c <= '9'; ++c) arr[c] = c - '0';
                for (char c = 'a'; c <= 'f'; ++c) arr[c] = 10 + c - 'a';
                for (char c = 'A'; c <= 'F'; ++c) arr[c] = 10 + c - 'A';
                return arr;
            }();

        std::string result;

        // Pre-allocate based on input size
        result.reserve(input.size());

        for (size_t i = 0; i < input.size();)
        {
            if (i + 3 < input.size() && input[i] == '\\' && (input[i + 1] == 'x' || input[i + 1] == 'X'))
            {
                const auto high = hex_lookup[input[i + 2]];
                const auto low = hex_lookup[input[i + 3]];
                if (high < 16 && low < 16)
                { // Valid hex pair
                    result.push_back((high << 4) | low);
                    i += 4;
                }
                else
                {
                    // Literal '\'
                    result.push_back(input[i]);
                    ++i;
                }
            }
            else
            {
                result.push_back(input[i]);
                ++i;
            }
        }
        return result;
    }

    //=============================================================================

    struct compiler_tab : e10::asset_browser_tab_base
    {
        e10::library_mgr& m_AssetMgr;

        //=============================================================================

        compiler_tab(assert_browser& Browser, const char* pName)
            : asset_browser_tab_base{ Browser, pName }
            , m_AssetMgr{ *Browser.getAssetMgr() }
        {
            m_AssetMgr.m_OnOpenProjectEvent.Register<&e10::compiler_tab::OnOpenNewProject>(*this);
            m_AssetMgr.m_OnCloseProjectEvent.Register<&e10::compiler_tab::OnCloseProject>(*this);

            if (m_AssetMgr.m_ProjectGUID.isValid())
            {
            }
        }

        //=============================================================================

        void OnOpenNewProject(e10::library_mgr&)
        {
        }

        //=============================================================================

        void OnCloseProject(e10::library_mgr&)
        {
        }

        //=============================================================================

        void LeftPanel() override
        {
            
        }


        //=============================================================================

        void RenderEntry( compilation::historical_entry& E )
        {
            // Ensure unique IDs
            const std::uint64_t UID = reinterpret_cast<std::size_t>(E.m_Entry.m_FullGuid.m_Instance.m_Pointer) + (E.m_Log ? reinterpret_cast<std::size_t>(E.m_Log.get()) : 0);
            ImGui::PushID(reinterpret_cast<void*>(UID));

            bool& expanded = m_Expanded[UID];

            static float AvariableWidth = ImGui::GetContentRegionAvail().x - 20;
            /*
            float itemHeight = expanded ? 45.0f * 2 : 45.0f * 1;
            if (expanded)
            {
                if (E.m_Log)
                {
                    xcore::lock::scope lock(*E.m_Log);
                    auto& Log = E.m_Log->get();
                    // true = wrap
                    ImVec2 textSize = CalcWrappedTextSize(Log.m_Log.c_str(), AvariableWidth);
                    itemHeight = 45.0f + textSize.y + ImGui::GetStyle().FramePadding.y * 2;
                }
            }
            */

            ImGui::BeginChild("##child", ImVec2(0, expanded?275.0f:45.0f), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            ImGui::Selectable("##selectable", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(0, 45.0f));


            // Draw content on top of the selectable
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImGui::GetItemRectMin());

            if (ImGui::ArrowButton("##down", expanded? ImGuiDir_Down : ImGuiDir_Right))
            {
                expanded = !expanded;
            }

            ImGui::SameLine(20);

            // Draw the main entry
            std::string_view TypeName;
            m_AssetMgr.m_mLibraryDB.FindAsReadOnly(E.m_Entry.m_gLibrary, [&](const std::unique_ptr<library_db>& Library)
            {
                Library->m_InfoByTypeDataBase.FindAsReadOnly(E.m_Entry.m_FullGuid.m_Type, [&](const std::unique_ptr<library_db::info_db>& InfoDB)
                {
                    std::string Icon;
                    if (auto e = m_AssetMgr.m_AssetPluginsDB.m_mPluginsByTypeGUID.find(E.m_Entry.m_FullGuid.m_Type); e == m_AssetMgr.m_AssetPluginsDB.m_mPluginsByTypeGUID.end())
                    {
                        // unable to find the type...
                    }
                    else
                    {
                        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[2]);
                        auto& Plugin = m_AssetMgr.m_AssetPluginsDB.m_lPlugins[e->second];
                        TypeName    = Plugin.m_TypeName;
                        ScaleText(ConvertHexEscapes(Plugin.m_Icon).c_str(), 0.5f);
                        ImGui::PopFont();
                        ImGui::SameLine();
                    }

                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    if ( false == InfoDB->m_InfoDataBase.FindAsReadOnly(E.m_Entry.m_FullGuid.m_Instance, [&](const library_db::info_node& InfoNode)
                    {
                        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
                        if (InfoNode.m_Info.m_Name.empty())
                        {
                            ScaleText(std::format("{:X}", E.m_Entry.m_FullGuid.m_Instance.m_Value).c_str(), 1.5f);
                        }
                        else
                        {
                            ScaleText(InfoNode.m_Info.m_Name.c_str(), 1.5f);
                        }
                        ImGui::PopFont();
                    }))
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
                        ScaleText(std::format("{:X} (Not in DBase)", E.m_Entry.m_FullGuid.m_Instance.m_Value).c_str(), 1.5f);
                        ImGui::PopStyleColor();
                    }

                    pos.y += 24;
                    ImGui::SetCursorScreenPos(pos);
                });
            });

            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
            ImGui::Text("Status:");
            ImGui::SameLine();
            ImGui::PopFont();
            if ( E.m_Log )
            {
                xcontainer::lock::scope lock(*E.m_Log);
                auto& Log = E.m_Log->get();

                if ( Log.m_Result == compilation::historical_entry::result::FAILURE)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
                    ImGui::Text("ERRORS where found while compiling");
                    ImGui::PopStyleColor();
                }
                else if (Log.m_Result == compilation::historical_entry::result::COMPILING )
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 0, 255));
                    ImGui::Text("%s",std::format( "COMPILING: {} ", get_last_line(Log.m_Log)).c_str());
                    ImGui::PopStyleColor();
                }
                else if (Log.m_Result == compilation::historical_entry::result::COMPILING_WARNINGS)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 0, 255));
                    ImGui::Text("%s", std::format("COMPILING (Found Warnings!): {} ", get_last_line(Log.m_Log)).c_str());
                    ImGui::PopStyleColor();
                }
                else if (Log.m_Result == compilation::historical_entry::result::SUCCESS)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 180, 180, 255));
                    ImGui::Text("DONE COMPILING");
                    ImGui::PopStyleColor();
                }
                else if (Log.m_Result == compilation::historical_entry::result::SUCCESS_WARNINGS)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 180, 0, 255));
                    ImGui::Text("DONE COMPILING (But there were warnings)");
                    ImGui::PopStyleColor();
                }
                else 
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 0, 255));
                    ImGui::Text("UNKNOWN STATE!!");
                    ImGui::PopStyleColor();
                }
            }
            else
            {
                ImGui::Text(std::format("Waiting to Compile with priority {}", E.m_Entry.m_Priority).c_str());
            }
            

            // Optional: Add spacing or a separator
            if (expanded) 
            {
                ImGui::Separator();
                ImGui::Indent();
                ImGui::BeginChild("##childLog", ImVec2(0, 0), false);
                if (E.m_Log)
                {
                    xcontainer::lock::scope lock(*E.m_Log);
                    auto& Log = E.m_Log->get();
                    ImGui::PushTextWrapPos(AvariableWidth);
                    ImGui::TextUnformatted(Log.m_Log.c_str());
                    ImGui::PopTextWrapPos();

                    // Update for the future
                    AvariableWidth = ImGui::GetContentRegionAvail().x;
                }

                ImGui::EndChild();
                ImGui::Unindent();
            }
            ImGui::EndChild();
            ImGui::PopID();
            
        }

        //=============================================================================

        void RightPanel() override
        {
            const ImVec4 normalColor = ImGui::GetStyle().Colors[ImGuiCol_Button];

            // fully transparent (buttons and child windows)
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0)); 
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

            //
            // Main Menu
            //
            {
                if (ImGui::Button("\xEE\x9C\x80\xee\xa5\xb2"))   // \xEE\xB7\xA3
                {
                    // Get button position and size
                    ImVec2 button_pos = ImGui::GetItemRectMin();
                    ImVec2 button_size = ImGui::GetItemRectSize();

                    // Set popup position just below the button
                    ImGui::SetNextWindowPos(ImVec2(button_pos.x, button_pos.y + button_size.y));
                    ImGui::OpenPopup("Compilation Main Menu");
                }

                if (ImGui::BeginPopup("Compilation Main Menu"))
                {
                    if (ImGui::BeginMenu("  Recompile"))
                    {
                        if (ImGui::MenuItem("  All Resources"))
                        {

                        }

                        if (ImGui::MenuItem("  Errors Resources"))
                        {

                        }
                        ImGui::EndMenu();
                    }

                    if (ImGui::MenuItem("  Setting"))
                    {

                    }
                    ImGui::EndPopup();
                }
            }

            //
            // Auto Compile
            //
            {
                ImGui::SameLine();

                const bool  OldState = m_AssetMgr.m_Compilation.m_AutoCompilation.load();
                if (OldState) 
                {
                    ImGui::PushStyleColor(ImGuiCol_Button,          normalColor);
                }

                if (ImGui::Button(OldState ? "\xEE\xA6\x9A Compile" : "\xEE\xA6\x9A" ) ) // :  "\xEE\x9F\x89"
                {
                    m_AssetMgr.m_Compilation.m_AutoCompilation.store(!OldState);

                    // let us help star the process...
                    if (OldState==false) m_AssetMgr.m_Compilation.StartCompilation();
                }

                if (ImGui::IsItemHovered()) ImGui::SetTooltip(OldState ? "Press if you want to make the compilation system be manual" : "Press if you want the compilation system to be automatic");

                if (OldState) 
                {
                    ImGui::PopStyleColor();
                }
                else
                {
                    ImGui::SameLine();
                    if (m_AssetMgr.m_Compilation.m_PauseCompilation.load()) ImGui::BeginDisabled();
                    if ( ImGui::Button("\xEF\x96\xB0 Compile") )
                    {
                        // Trigger compilation manually
                        m_AssetMgr.m_Compilation.StartCompilation();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Press to run compilation process");
                    if (m_AssetMgr.m_Compilation.m_PauseCompilation.load()) ImGui::EndDisabled();
                }
            }

            //
            // Pause
            //
            {
                ImGui::SameLine();
                const bool OldState = m_AssetMgr.m_Compilation.m_PauseCompilation.load();

                if (OldState)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, normalColor);
                }

                if ( ImGui::Button( " \xEE\x98\xAE " )) //OldState ? "\xEF\x96\xB0" : "\xEE\x98\xAE" ))
                {
                    m_AssetMgr.m_Compilation.PauseCompilation(!OldState);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(OldState ? "Press if you want to resume the compilation process" : "Press to pause the compilation process");

                if (OldState)
                {
                    ImGui::PopStyleColor();
                }

            }

            //
            // Options
            //
            {
                std::scoped_lock lk(m_AssetMgr.m_Compilation.m_Settings.m_Mutex);

                // Debug Level
                {
                    ImGui::SameLine();
                    ImGui::Text("Debug:");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Debug level use to compile the resources with");

                    constexpr static std::array items     = { "D0 - None", "D1 - Basic", "Dz - Maximum" };
                    static const float          max_width = ComputeWidthComboBox(items);

                    ImGui::SameLine(0,0);
                    ImGui::PushItemWidth(max_width);

                    int Index = static_cast<int>(m_AssetMgr.m_Compilation.m_Settings.m_DebugLevel);
                    if (ImGui::Combo("##DebugCombo", &Index, items.data(), static_cast<int>(items.size())))
                    {
                        m_AssetMgr.m_Compilation.m_Settings.m_DebugLevel = static_cast<compilation::settings::debug>(Index);
                    }
                    ImGui::PopItemWidth();
                }

                // Optimize
                {
                    ImGui::SameLine();
                    ImGui::Text("Opt:");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Optimization level use to compile the resources with");

                    constexpr static std::array items       = { "O0 - None", "O1 - Normal", "Oz - Maximum" };
                    static const float          max_width   = ComputeWidthComboBox(items);

                    ImGui::SameLine(0,0);
                    ImGui::PushItemWidth(max_width);

                    int Index = static_cast<int>(m_AssetMgr.m_Compilation.m_Settings.m_OptimizationLevel);
                    if (ImGui::Combo("##OptimizationCombo", &Index, items.data(), static_cast<int>(items.size())))
                    {
                        m_AssetMgr.m_Compilation.m_Settings.m_OptimizationLevel = static_cast<compilation::settings::optimization>(Index);
                    }
                    ImGui::PopItemWidth();
                }
            }

            //
            // Show the workers working
            //
            if (false)
            {
                ImGui::SameLine();
                int Workers = m_AssetMgr.m_Compilation.m_WorkersWorking.load(std::memory_order_relaxed);
                ImGui::Text("Workers WIP: %d", Workers);
            }
            
            

            //
            // Begin displaying items
            //
            ImGui::BeginChild("##items");

            //
            // First we want to print the entries been compile
            //
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 0, 255));
            {
                std::lock_guard lock(m_AssetMgr.m_Compilation.m_Compiling.m_Mutex);
                for (auto& E : std::views::reverse(m_AssetMgr.m_Compilation.m_Compiling.m_List) )
                {
                    RenderEntry(E);
                }
            }
            ImGui::PopStyleColor();

            //
            // List of entries waiting for compilation
            //
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
            {
                std::lock_guard lock(m_AssetMgr.m_Compilation.m_Queue.m_Mutex);
                for (auto& E : m_AssetMgr.m_Compilation.m_Queue.getContainer())
                {
                    compilation::historical_entry I;
                    I.m_Entry = E;
                    RenderEntry(I);
                }
            }
            ImGui::PopStyleColor();

            //
            // Display some of the Historical entries
            //
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 120, 255));
            {
                std::lock_guard lock(m_AssetMgr.m_Compilation.m_Historical.m_Mutex);
                size_t count = std::min<size_t>(50, m_AssetMgr.m_Compilation.m_Historical.m_List.size());

                for (auto it = m_AssetMgr.m_Compilation.m_Historical.m_List.rbegin(); it != m_AssetMgr.m_Compilation.m_Historical.m_List.rbegin() + count; ++it)
                {
                    RenderEntry(*it);
                }
            }
            ImGui::PopStyleColor();

            // sub window
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
        }

        std::unordered_map<std::uint64_t, bool> m_Expanded;
    };

    //=============================================================================

    namespace
    {
        inline browser_registration<compiler_tab, "\xEE\x9C\x93 Compilation", 1.0f > g_CompilationTab{};
    }
}
#endif