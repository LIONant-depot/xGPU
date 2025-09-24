#include "E10_AssetBrowser.h"
#include "E10_AssetMgr.h"

#include "imgui.h"
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS // Allows ImVec2 arithmetic
#endif
#include "imgui_internal.h"       // For TreeNodeBehavior, GetIDWithSeed, etc.

namespace e10
{
    struct virtual_tree_tab : e10::asset_browser_tab_base
    {
        e10::library_mgr& m_AssetMgr;

        //=============================================================================

        virtual_tree_tab(assert_browser& Browser, const char* pName)
            : asset_browser_tab_base{ Browser, pName }
            , m_AssetMgr{ *Browser.getAssetMgr() }
        {
            m_AssetMgr.m_OnOpenProjectEvent.Register<&e10::virtual_tree_tab::OnOpenNewProject>(*this);
            m_AssetMgr.m_OnCloseProjectEvent.Register<&e10::virtual_tree_tab::OnCloseProject>(*this);

            if (m_AssetMgr.m_ProjectGUID.isValid())
            {
                m_IsTreeNodeOpen[{m_AssetMgr.m_ProjectGUID.m_Instance}] = true;
            }
        }

        //=============================================================================

        ~virtual_tree_tab()
        {
            m_AssetMgr.m_OnOpenProjectEvent.RemoveDelegates(this);
            m_AssetMgr.m_OnCloseProjectEvent.RemoveDelegates(this);
        }

        //=============================================================================

        enum class sort_base_on
        { NAME_ASCENDING
        , NAME_DESCENDING
        , TYPE_ASCENDING
        , TYPE_DESCENDING
        , LAST_MODIFIED_ASCENDING
        , LAST_MODIFIED_DESCENDING
        };

        //=============================================================================

        struct drag_and_drop_folder_payload_t
        {
            e10::folder::guid           m_Parent;
            xresource::full_guid        m_Source;
            bool                        m_bSelection;
        };

        //=============================================================================

        struct path_node
        {
            e10::folder::guid           m_Guid;
            std::string                 m_Name;
            std::vector<path_node>      m_Children;
        };

        //=============================================================================

        struct path_history_entry
        {
            library::guid               m_gLibrary;
            folder::guid                m_gFolder;
        };

        //=============================================================================

        void OnOpenNewProject( e10::library_mgr&)
        {
            // Have the default library open from the start
            m_IsTreeNodeOpen[ {m_AssetMgr.m_ProjectGUID.m_Instance} ] = true;
        }

        //=============================================================================

        void OnCloseProject(e10::library_mgr&)
        {
        }

        //=============================================================================

        static int WrappedButton2(xresource::instance_guid G, const char* label, const ImVec2& size, ImU32 Color, const char* pIcon, bool& held, bool bModified = false )
        {
            ImGuiContext& g = *ImGui::GetCurrentContext();
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            if (window->SkipItems)
                return false;

            ImGui::BeginGroup();

            ImGuiID id = static_cast<ImGuiID>(G.m_Value);

            // Calculate padding
            ImVec2 padding = ImGui::GetStyle().FramePadding;
            float text_width = size.x - (padding.x * 2);

            // Calculate button rectangle with FIXED height (size.y)
            ImVec2 pos = window->DC.CursorPos;
            ImVec2 end_pos = { pos.x + size.x, pos.y + size.y };
            ImRect bb(pos, end_pos);

            ImGui::ItemSize(bb);
            if (!ImGui::ItemAdd(bb, id))
            {
                ImGui::EndGroup();
                return false;
            }

            bool hovered;
            int TypeOfPress = 0;

            // Single call to ButtonBehavior with both left and right mouse buttons
            ImGui::ButtonBehavior(bb, id, &hovered, &held,
                ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

            if (hovered)
            {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, false))
                    TypeOfPress = 1;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    TypeOfPress = 2; // Left double-click
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right, false))
                    TypeOfPress = 3; // Right click
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Right))
                    TypeOfPress = 4; // Right double-click
            }

            // Render button background
            const ImU32 col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_ButtonActive :
                hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
            ImGui::RenderNavHighlight(bb, id);
            ImGui::RenderFrame(bb.Min, bb.Max, col, true, ImGui::GetStyle().FrameRounding);

            // Start at top with padding
            ImGui::SetCursorScreenPos({ pos.x + padding.x, pos.y + padding.y });

            // Render icon (centered horizontally)
            if (pIcon)
            {
                ImGui::PushFont(xgpu::tools::imgui::getFont(2));
                ImGui::PushStyleColor(ImGuiCol_Text, Color);
                float icon_width = ImGui::CalcTextSize(pIcon).x;
                ImGui::SetCursorPosX(pos.x + (size.x - icon_width) * 0.5f - window->Pos.x); // Center icon
                ImGui::Text("%s", pIcon);
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }

            // Move cursor below icon with spacing (but ensure it stays within bounds)
            float text_start_y = ImGui::GetCursorPosY() + padding.y;

            // Print a green start if the resource has been modified
            if (bModified)
            {
                ImGui::SetCursorScreenPos({ pos.x + padding.x, pos.y + padding.y });
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 189, 0, 255)); // Green (R=0, G=255, B=0, A=255)
                ImGui::Text("\xEE\xB6\xAD");
                ImGui::PopStyleColor(); // Restore default
            }

            ImGui::SetCursorPosY(text_start_y);

            // Clip text to button bounds
            ImGui::PushClipRect(bb.Min, bb.Max, true);

            // print the name of the asset
            const float LetterWidth   = ImGui::CalcTextSize("A").x;
            const int   NCharsPerLine = static_cast<int>(size.x / LetterWidth);
            const int   StrLen        = static_cast<int>(std::strlen(label));
            const int   MaxLines      = 2;

            if (StrLen > NCharsPerLine)
            {
                for (int i = 0; i < StrLen && i < NCharsPerLine * MaxLines; i += NCharsPerLine)
                {
                    ImGui::Text("%.*s", (StrLen - i) < NCharsPerLine ? (StrLen - i) : NCharsPerLine, label + i);
                }
            }
            else
            {
                // static constexpr char spaces[] = "                                              ";
                // const int NSpaces = static_cast<int>((NCharsPerLine - StrLen)/2.0 + 1.5f);
                // ImGui::Text("%s%s", &spaces[sizeof(spaces) - NSpaces], label);

                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + LetterWidth * ((NCharsPerLine - StrLen) / 2.0f+0.5f));
                ImGui::Text("%s", label);
            }

            ImGui::PopClipRect();
            ImGui::EndGroup();

            // Ensure the cursor doesn't advance beyond the button
            window->DC.CursorPos.y = pos.y + size.y + 5;

            return TypeOfPress;
        }

        //=============================================================================

        void RenderPath()
        {
            ImGuiStyle& style = ImGui::GetStyle();

            // Get the draw list for custom rendering
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 start_pos = ImGui::GetCursorScreenPos(); // Starting position of the line
            start_pos.x -= 4; // Offset to align the buttons with the search bar

            // Calculate the height of the line in advance (approx.)
            float line_height = ImGui::GetTextLineHeightWithSpacing();

            // Get the default button background color from the current style
            ImVec4 button_bg_color = ImVec4(0.145f, 0.145f, 0.145f, 0.80f);//ImGui::GetStyle().Colors[ImGuiCol_Button];
            ImU32 button_bg_color_u32 = ImGui::ColorConvertFloat4ToU32(button_bg_color);

            draw_list->PushClipRect(start_pos, ImVec2(start_pos.x + ImGui::GetContentRegionAvail().x, start_pos.y + line_height), true);

            // Draw the button background before the buttons
            m_PathHistoryPos  = start_pos;
            m_PathHistorySize = { ImGui::GetContentRegionAvail().x, line_height }; //ImVec2(start_pos.x + ImGui::GetContentRegionAvail().x, start_pos.y + line_height);
            draw_list->AddRectFilled( m_PathHistoryPos
                                    , { m_PathHistoryPos.x + m_PathHistorySize.x, m_PathHistoryPos.y + m_PathHistorySize.y }
                                    , button_bg_color_u32
                                    );

            // Push style modifications to remove button appearance
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);           // Remove border
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, style.FramePadding.y));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));        // Transparent background
            // Note: We don't override ImGuiCol_Text, so buttons use the existing text color

            std::vector<path_node> PathNodeList = {};

            if (m_PathHistoryList.empty() == false)
            {
                auto E = m_PathHistoryList[m_PathHistoryIndex];

                //
                // Create the Full Path 
                //
                m_AssetMgr.m_mLibraryDB.FindAsReadOnly(E.m_gLibrary, [&](const std::unique_ptr<e10::library_db>& Lib)
                {
                    Lib->m_InfoByTypeDataBase.FindAsReadOnly(E.m_gFolder.m_Type, [&](const std::unique_ptr<library_db::info_db>& Entry)
                    {
                        std::function<void(xresource::instance_guid )> GetFolderName = [&]( xresource::instance_guid Guid ) -> void
                        {
                            Entry->m_InfoDataBase.FindAsReadOnly(Guid, [&](const e10::library_db::info_node& InfoEntry)
                            {
                                path_node& PathNode = PathNodeList.emplace_back();
                                PathNode.m_Guid.m_Instance = InfoEntry.m_Info.m_Guid.m_Instance;
                                PathNode.m_Name            = InfoEntry.m_Info.m_Name;

                                if (PathNode.m_Name.empty()) PathNode.m_Name = "<unnamed>";

                                for (auto& C : InfoEntry.m_lChildLinks)
                                {
                                    if (C.m_Type == e10::folder::type_guid_v)
                                    {
                                        if (C.m_Instance.m_Value == (E.m_gLibrary.m_Instance.m_Value + 2))
                                            continue;

                                        auto& Child = PathNode.m_Children.emplace_back();
                                        Child.m_Guid.m_Instance = C.m_Instance;

                                        // Get the name of the child
                                        bool bFound = Entry->m_InfoDataBase.FindAsReadOnly(Child.m_Guid.m_Instance, [&](const e10::library_db::info_node& InfoEntry)
                                        {
                                            Child.m_Name = InfoEntry.m_Info.m_Name;
                                        });
                                        assert(bFound);
                                    }
                                }

                                // Make sure the list is shorted
                                if( PathNode.m_Children.empty() == false )
                                {
                                    std::sort(PathNode.m_Children.begin(), PathNode.m_Children.end(), [](const path_node& A, const path_node& B)
                                    {
                                        return A.m_Name < B.m_Name;
                                    });
                                }

                                //
                                // Check to see if it has a parent
                                //
                                for ( auto& C : InfoEntry.m_Info.m_RscLinks)
                                {
                                    if (C.m_Type == e10::folder::type_guid_v)
                                    {
                                        // If this entry is in the trash then skip it!
                                        if (C.m_Instance.m_Value == (e10::folder::trash_guid_v.m_Instance.m_Value))
                                           break;

                                        GetFolderName(C.m_Instance);
                                        break;
                                    }
                                }
                            });
                        };

                        GetFolderName(E.m_gFolder.m_Instance);
                    });
                });
            }

            //
            // Render the path
            //
            static int iSelectedPath = -1;
            for (auto& E : std::views::reverse(PathNodeList))
            {
                const int Index = static_cast<int>(&E - &PathNodeList[0]);

                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));
                if (&E == &PathNodeList.back()) ImGui::PushFont(xgpu::tools::imgui::getFont(1));
                if (ImGui::Button(E.m_Name.c_str()))
                {
                    m_ParentGUID = E.m_Guid;
                    PathHistoryUpdate( m_SelectedLibrary, m_ParentGUID );
                }
                if (&E == &PathNodeList.back()) ImGui::PopFont();
                ImGui::PopStyleColor(1);
                ImGui::SameLine(0, 0);

                // Terminate early if the selected path does not have any children
                if (Index == 0 && E.m_Children.empty())
                {
                    ImGui::SameLine(0, 0);
                    break;
                }

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, style.FramePadding.y));
                ImGui::PushID(Index);
                if (ImGui::Button(">"))
                {
                    ImGui::PopID();
                    ImGui::OpenPopup("Path Context Menu");
                    iSelectedPath = Index;
                }
                else
                {
                    ImGui::PopID();
                }
                
                ImGui::PopStyleVar(1);
                ImGui::SameLine(0, 0);
            }
            

            // Handle the context menu for the path
            if ( ImGui::BeginPopup("Path Context Menu") )
            {
                for (auto& E : PathNodeList[iSelectedPath].m_Children)
                {
                    bool CheckMark = false;

                    // Add a check mark for existing path...
                    if ((iSelectedPath + 1) < PathNodeList.size() && E.m_Guid == PathNodeList[iSelectedPath + 1].m_Guid)
                    {
                        CheckMark = true;
                        //  Name = std::format("\xEE\x9C\xBE {}", E.m_Name);
                    }

                    if (ImGui::MenuItem(E.m_Name.c_str(), nullptr, CheckMark))
                    {
                        m_ParentGUID = E.m_Guid;
                        PathHistoryUpdate(m_SelectedLibrary, m_ParentGUID);

                        // If we change the directory we should open the tree to show the new directory
                        for (int i = 0; i <= iSelectedPath; ++i)
                        {
                            m_IsTreeNodeOpen[PathNodeList[i].m_Guid] = true;
                        }
                    }
                }
                ImGui::EndPopup();
            }
            else
            {
                iSelectedPath = -1;
            }

            // Pop the clip modifications
            draw_list->PopClipRect();

            // Pop the style modifications
            ImGui::PopStyleColor(1);
            ImGui::PopStyleVar(2);

            // Fill the rest of the space with empty text
            ImGui::PushItemWidth(-FLT_MIN);
            ImGui::Text(" ");
            ImGui::PopItemWidth();
        }

        //=============================================================================

        static bool ScaleButton(const char* pTxt, float Scale)
        {
            float old_font_size = ImGui::GetFont()->Scale;
            ImGui::GetFont()->Scale *= Scale;
            ImGui::PushFont(ImGui::GetFont());
            bool pressed = ImGui::Button(pTxt);
            ImGui::GetFont()->Scale = old_font_size;
            ImGui::PopFont();

            return pressed;
        }

        //=============================================================================

        void OpenLeftTreeTo( e10::library::guid gLibrary, e10::folder::guid gFolder )
        {
            //
            // Make sure that this path is open in the left tree
            //
            m_AssetMgr.m_mLibraryDB.FindAsReadOnly(gLibrary, [&](const std::unique_ptr<e10::library_db>& Lib)
            {
                Lib->m_InfoByTypeDataBase.FindAsReadOnly(e10::folder::type_guid_v, [&](const std::unique_ptr<library_db::info_db>& Entry)
                {
                    std::function<void(xresource::instance_guid)> Open = [&](xresource::instance_guid Guid) -> void
                    {
                        Entry->m_InfoDataBase.FindAsReadOnly(Guid, [&](const e10::library_db::info_node& InfoEntry)
                        {
                            m_IsTreeNodeOpen[{InfoEntry.m_Info.m_Guid.m_Instance}] = true;
                            for ( auto& E : InfoEntry.m_Info.m_RscLinks)
                            {
                                if ( E != e10::folder::trash_guid_v)
                                {
                                    if ( E.m_Type == e10::folder::type_guid_v)
                                    {
                                        Open(E.m_Instance);
                                        return;
                                    }
                                }
                            }
                        });
                    };

                    // Start opening things...
                    Open(gFolder.m_Instance);
                });
            });
        }

        //=============================================================================

        void UpdateHistoryLRU()
        {
            bool bFound = false;
            for (auto& E : m_PathHistoryListRU)
            {
                if (E.m_gLibrary == m_PathHistoryList[m_PathHistoryIndex].m_gLibrary && E.m_gFolder == m_PathHistoryList[m_PathHistoryIndex].m_gFolder)
                {
                    path_history_entry Temp(std::move(E));
                    m_PathHistoryListRU.erase(m_PathHistoryListRU.begin() + static_cast<int>(&E - m_PathHistoryListRU.data()));
                    m_PathHistoryListRU.insert(m_PathHistoryListRU.begin(), std::move(Temp));
                    bFound = true;
                    break;
                }
            }

            if (bFound == false)
            {
                m_PathHistoryListRU.insert(m_PathHistoryListRU.begin(), m_PathHistoryList[m_PathHistoryIndex]);
            }

            m_SelectedItems.clear();
            OpenLeftTreeTo(m_PathHistoryList[m_PathHistoryIndex].m_gLibrary, m_PathHistoryList[m_PathHistoryIndex].m_gFolder);

        }

        //=============================================================================

        void PathHistoryUpdate( library::guid gLibrary, e10::folder::guid gFolder )
        {
            if (m_PathHistoryList.empty() ) 
            {
                m_PathHistoryList.push_back({ gLibrary, gFolder });
                m_PathHistoryIndex = 0;
            }
            else if( m_PathHistoryList[m_PathHistoryIndex].m_gLibrary != gLibrary || m_PathHistoryList[m_PathHistoryIndex].m_gFolder != gFolder)
            {
                // Prune the history if we are not at the end
                m_PathHistoryList.resize(std::min( m_PathHistoryIndex+1ull, m_PathHistoryList.size()) );

                // Add the new entry to the history
                m_PathHistoryList.push_back({ gLibrary, gFolder });

                // Make sure we don't have too many entries in the history
                if (m_PathHistoryList.size() > 50)
                {
                    m_PathHistoryList.erase(m_PathHistoryList.begin());
                }

                // Set the current index to the last entry
                m_PathHistoryIndex = static_cast<std::uint32_t>(m_PathHistoryList.size()) - 1;
            }

            assert(m_PathHistoryIndex < m_PathHistoryList.size());

            m_SelectedLibrary = m_PathHistoryList[m_PathHistoryIndex].m_gLibrary;
            m_ParentGUID      = m_PathHistoryList[m_PathHistoryIndex].m_gFolder;


            //
            // Update the Recently Used list...
            //
            UpdateHistoryLRU();
        }

        //=============================================================================

        std::string BuildPathString( e10::library::guid gLibrary, e10::folder::guid gFolder )
        {
            std::string FolderName;

            //
            // Create the Full Path 
            //
            m_AssetMgr.m_mLibraryDB.FindAsReadOnly(gLibrary, [&](const std::unique_ptr<e10::library_db>& Lib)
            {
                Lib->m_InfoByTypeDataBase.FindAsReadOnly(gFolder.m_Type, [&](const std::unique_ptr<library_db::info_db>& Entry)
                {
                    std::function<void(xresource::instance_guid)> GetFolderName = [&]( xresource::instance_guid Guid ) -> void
                    {
                        Entry->m_InfoDataBase.FindAsReadOnly(Guid, [&](const e10::library_db::info_node& InfoEntry)
                        {
                            if (FolderName.empty()) FolderName = InfoEntry.m_Info.m_Name.empty() ? "<unnamed>" : InfoEntry.m_Info.m_Name;
                            else                    FolderName = std::format( "{}\\{}", InfoEntry.m_Info.m_Name.empty()?"<unnamed>": InfoEntry.m_Info.m_Name, FolderName);

                            //
                            // Check to see if it has a parent
                            //
                            for ( auto& C : InfoEntry.m_Info.m_RscLinks)
                            {
                                if (C.m_Type == e10::folder::type_guid_v)
                                {
                                    // If this entry is in the trash then skip it!
                                    if (C.m_Instance.m_Value == e10::folder::trash_guid_v.m_Instance.m_Value)
                                       break;

                                    GetFolderName(C.m_Instance);
                                    break;
                                }
                            }
                        });
                    };

                    GetFolderName(gFolder.m_Instance);
                });
            });

            return FolderName;
        }

        //=============================================================================

        void RenderNavigationPath()
        {
            ImGui::SameLine();
            //ImGui::PushFont(xgpu::tools::imgui::getFont(1));
            if (m_PathHistoryIndex==0) 
            {
                ImGui::BeginDisabled();
                ScaleButton("\xEE\x9C\xAB", 1.0f);
                ImGui::EndDisabled();
            }
            else if (ScaleButton("\xEE\x9C\xAB", 1.0f))
            {
                m_PathHistoryIndex--;
                m_SelectedLibrary   = m_PathHistoryList[m_PathHistoryIndex].m_gLibrary;
                m_ParentGUID        = m_PathHistoryList[m_PathHistoryIndex].m_gFolder;
                UpdateHistoryLRU();
            }

            ImGui::SameLine(0, 1.5f);
            if ((m_PathHistoryIndex+1) >= m_PathHistoryList.size())
            {
                ImGui::BeginDisabled();
                ScaleButton("\xEE\x9C\xAA", 1.0f);
                ImGui::EndDisabled();
            }
            else if (ScaleButton("\xEE\x9C\xAA", 1.0f)) //"\xef\x82\x8" //\xef\x82\x8f   \xee\xa4\xb7
            {
                m_PathHistoryIndex  = static_cast<std::uint32_t>(m_PathHistoryIndex + 1ull);
                m_SelectedLibrary   = m_PathHistoryList[m_PathHistoryIndex].m_gLibrary;
                m_ParentGUID        = m_PathHistoryList[m_PathHistoryIndex].m_gFolder;
                UpdateHistoryLRU();
            }
            //ImGui::PopFont();

            //
            // Display Full stack
            //
            ImGui::SameLine(0, 0.8f); //\xEE\xA0\x9C
            if (ScaleButton("\xee\xa5\xb2", 0.8f)) //"\xef\x82\x8" //\xef\x82\x8f   \xee\xa4\xb7
            {
                m_PathHistoryShow = true;
            }
            ImGui::SameLine(0, 1.0f);

            //
            // Render the path
            //
            RenderPath();

            //
            // Handle the history Popup
            //
            if ( m_PathHistoryShow )
            {
                ImGui::SetNextWindowPos({ m_PathHistoryPos.x, m_PathHistoryPos.y + 24} );
                ImGui::SetNextWindowSize({ m_PathHistorySize.x, m_PathHistorySize.y + 24*4 });
                ImGui::OpenPopup("Path History");
            }

            if (ImGui::BeginPopup("Path History"))
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));        // Transparent background
                ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.245f, 0.245f, 0.245f, 0.8f));
                if (ImGui::BeginTabBar("History", ImGuiTabBarFlags_None))
                {
                    if (ImGui::BeginTabItem("\xEE\xA0\x9C History"))
                    {
                        ImGui::SetNextWindowBgAlpha(0.0f);
                        if (ImGui::BeginChild("FrameRU", ImVec2{}, ImGuiTabBarFlags_None))
                        {
                            for (auto& E : m_PathHistoryListRU)
                            {
                                std::string FolderName = BuildPathString(E.m_gLibrary, E.m_gFolder);

                                //
                                // Display as a button
                                //
                                if (FolderName.empty() == false)
                                {
                                    FolderName = std::format("\xEE\xA0\x9C {}", FolderName);

                                    const auto Index = static_cast<std::uint32_t>(&E - m_PathHistoryListRU.data());
                                    ImGui::PushID(Index);
                                    if (ImGui::Button(FolderName.c_str()))
                                    {
                                        PathHistoryUpdate(E.m_gLibrary, E.m_gFolder);
                                        m_PathHistoryShow  = false;
                                        ImGui::CloseCurrentPopup();
                                    }
                                    ImGui::PopID();
                                }
                            }
                            ImGui::EndChild();
                        }
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("\xEE\xA0\xB5 Navigation"))
                    {
                        ImGui::SetNextWindowBgAlpha(0.0f);
                        if (ImGui::BeginChild("FrameWindow", ImVec2{}, ImGuiTabBarFlags_None))
                        {
                            for (auto& E : std::views::reverse( m_PathHistoryList ))
                            {
                                std::string FolderName = BuildPathString(E.m_gLibrary, E.m_gFolder);

                                //
                                // Display as a button
                                //
                                if(FolderName.empty() == false)
                                {
                                    auto Index = static_cast<std::uint32_t>(&E - m_PathHistoryList.data());

                                    if (Index == m_PathHistoryIndex)
                                    {
                                        FolderName = std::format("\xEE\x9C\xBE {}", FolderName);
                                    }
                                    else
                                    {
                                        FolderName = std::format("  {}", FolderName);
                                    }

                                    ImGui::PushID(Index);
                                    if (ImGui::Button(FolderName.c_str()))
                                    {
                                        m_ParentGUID       = E.m_gFolder;
                                        m_SelectedLibrary  = E.m_gLibrary;
                                        m_PathHistoryShow  = false;
                                        m_PathHistoryIndex = static_cast<std::uint32_t>(&E - m_PathHistoryList.data());
                                        UpdateHistoryLRU();
                                        ImGui::CloseCurrentPopup();
                                    }
                                    ImGui::PopID();
                                }
                            }

                            ImGui::EndChild();
                        }

                        ImGui::EndTabItem();
                    }

                    // If user clicks outside the popup, close it
                    if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()))
                    {
                        ImGui::CloseCurrentPopup();
                        m_PathHistoryShow = false;
                    }

                    ImGui::EndTabBar();
                }

                ImGui::PopStyleColor(2);
                ImGui::EndPopup(); 
            }
        }

        //=============================================================================

        void AddResourcePopUp()
        {
            for (auto& E : m_AssetMgr.m_AssetPluginsDB.m_lPlugins)
            {
                std::string ResourceType = std::format("{}  {}", ConvertHexEscapes(E.m_Icon), E.m_TypeName );

                if (ImGui::MenuItem(ResourceType.c_str()))
                {
                    auto LibGUID            = m_SelectedLibrary.empty() ? m_AssetMgr.m_ProjectGUID : m_SelectedLibrary;
                    auto LastGeneratedAsset = m_AssetMgr.NewAsset(LibGUID, { {}, E.m_TypeGUID }, m_ParentGUID);

                    m_SelectedItems.clear();
                    m_SelectedItems.push_back(LastGeneratedAsset);
                }
            }
        }

        //=============================================================================

        void AddResourceButton()
        {
            if ( m_ParentGUID == e10::folder::trash_guid_v )
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0, 0, 1));        
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0, 0, 1));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0, 0, 1));
                if (ScaleButton("\xEE\x9C\xB8", 1.2f))
                {
                    ImGui::OpenPopup("Delete All Resources");
                }
                ImGui::PopStyleColor(3);

                if (ImGui::BeginPopup("Delete All Resources"))
                {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "Warning: This operation can not be undo");
                    if ( ImGui::MenuItem("\xEE\x9D\x8D  Empty Trashcan") )
                    {
                        if ( auto Err = m_AssetMgr.EmptyTrashcan(m_SelectedLibrary); Err.empty() == false )
                        {
                            int a = 0;
                        }

                        m_SelectedItems.clear();
                    }
                    
                    ImGui::EndPopup(); // Close the popup scope
                }
            }
            else if (m_ParentGUID.isValid())
            {
                bool bIsDeleted = false;
                m_AssetMgr.getInfo( m_SelectedLibrary, m_ParentGUID, [&](xresource_pipeline::info& Info)
                {
                    if (Info.m_RscLinks.empty() == false && Info.m_RscLinks[0] == e10::folder::trash_guid_v )
                    {
                        bIsDeleted = true;
                    }
                });
                if (bIsDeleted) return;

                if (ScaleButton("\xee\xa5\x88", 1.2f))
                {
                    ImGui::OpenPopup("Add Resource");
                }

                if (ImGui::BeginPopup("Add Resource"))
                {
                    AddResourcePopUp();
                    ImGui::EndPopup(); // Close the popup scope
                }
            }
        }

        //=============================================================================

        void AddViewMenuButton()
        {
            static bool keepPopupOpen = false;
            static ImVec2 popupPos = ImVec2(0, 0);
            if (ScaleButton("\xEE\x9E\xB3\xee\xa5\xb2", 0.9f))   // 
            {
                keepPopupOpen = true;
                popupPos = ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y);
            }
            //ImGui::PopFont();

            if (keepPopupOpen)
            {
                // Lock position
                ImGui::SetNextWindowPos(popupPos, ImGuiCond_Always);
                ImGui::OpenPopup("Filter Resource");
            }

            if (ImGui::BeginPopup("Filter Resource"))
            {
                if (ImGui::BeginMenu("\xEE\xA3\x8B Sort By"))
                {
                    if (ImGui::MenuItem("\xEF\x82\xAD Name (a->Z)", nullptr, m_ShortBasedOn == sort_base_on::NAME_ASCENDING))
                    {
                        m_ShortBasedOn = sort_base_on::NAME_ASCENDING;
                        ImGui::CloseCurrentPopup();
                        keepPopupOpen = false;
                    }

                    if (ImGui::MenuItem("\xEF\x82\xAE Name (Z->a)", nullptr, m_ShortBasedOn == sort_base_on::NAME_DESCENDING))
                    {
                        m_ShortBasedOn = sort_base_on::NAME_DESCENDING;
                        ImGui::CloseCurrentPopup();
                        keepPopupOpen = false;
                    }

                    if (ImGui::MenuItem("\xEF\x82\xAD Type (a->Z)", nullptr, m_ShortBasedOn == sort_base_on::TYPE_ASCENDING))
                    {
                        m_ShortBasedOn = sort_base_on::TYPE_ASCENDING;
                        ImGui::CloseCurrentPopup();
                        keepPopupOpen = false;
                    }

                    if (ImGui::MenuItem("\xEF\x82\xAE Type (Z->a)", nullptr, m_ShortBasedOn == sort_base_on::TYPE_DESCENDING))
                    {
                        m_ShortBasedOn = sort_base_on::TYPE_DESCENDING;
                        ImGui::CloseCurrentPopup();
                        keepPopupOpen = false;
                    }

                    if (ImGui::MenuItem("\xEF\x82\xAD Last Modified (a->Z)", nullptr, m_ShortBasedOn == sort_base_on::LAST_MODIFIED_ASCENDING))
                    {
                        m_ShortBasedOn = sort_base_on::LAST_MODIFIED_ASCENDING;
                        ImGui::CloseCurrentPopup();
                        keepPopupOpen = false;
                    }

                    if (ImGui::MenuItem("\xEF\x82\xAE Last Modified (Z->a)", nullptr, m_ShortBasedOn == sort_base_on::LAST_MODIFIED_DESCENDING))
                    {
                        m_ShortBasedOn = sort_base_on::LAST_MODIFIED_DESCENDING;
                        ImGui::CloseCurrentPopup();
                        keepPopupOpen = false;
                    }

                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("\xee\x9c\x9c Filter By Type"))
                {
                    if (ImGui::MenuItem("\xee\x9c\x9c Remove All Types"))
                    {
                        for (auto& E : m_AssetMgr.m_AssetPluginsDB.m_lPlugins)
                        {
                            m_FilterByType.emplace_back(E.m_TypeGUID);
                        }
                    }

                    if (ImGui::MenuItem("\xee\x9c\x9c View All Types"))
                    {
                        m_FilterByType.clear();
                    }

                    ImGui::SeparatorText("Type by Type");

                    for (auto& E : m_AssetMgr.m_AssetPluginsDB.m_lPlugins)
                    {
                        bool Selected = [&]
                            {
                                for (auto& T : m_FilterByType)
                                {
                                    if (T == E.m_TypeGUID) return true;
                                }
                                return false;
                            }();

                        if (ImGui::MenuItem(std::format("\xee\x9c\x9c Remove {}", E.m_TypeName).c_str(), nullptr, &Selected))
                        {
                            if (Selected)
                            {
                                m_FilterByType.emplace_back(E.m_TypeGUID);
                            }
                            else
                            {
                                m_FilterByType.erase(std::ranges::remove(m_FilterByType, E.m_TypeGUID).begin(), m_FilterByType.end());
                            }
                        }
                    }
                    ImGui::EndMenu();
                }

                //ImGui::SeparatorText("Build in");

                // If user clicks outside the popup, close it
                if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()))
                {
                    ImGui::CloseCurrentPopup();
                    keepPopupOpen = false;
                }

                if (ImGui::MenuItem( "Display Deleted Items", nullptr, &m_DisplayDeletedItems ))
                {
                    
                }

                ImGui::EndPopup(); // Close the popup scope
            }
        }

        //=============================================================================

        void LeftPanel() override
        {
            struct folder
            {
                e10::folder::guid           m_Guid;
                std::string_view            m_Name;
                std::vector<folder>         m_Children;
                int                         m_nDeletedItems;
                bool                        m_isRoot:1
                ,                           m_isTrash:1
                ,                           m_isOpen:1
                ,                           m_isSelected:1
                ,                           m_isEmpty:1
                ,                           m_isDeleted;
            };

            static xresource::full_guid renameFolder = {};
            static char newName[128] = "";
            bool SelectedItemFound = false;
            for (auto& L : m_AssetMgr.m_mLibraryDB)
            {
                //
                // Collect the library folder information
                //
                folder RootFolder;
                {
                    const bool bFoundType = L.second->m_InfoByTypeDataBase.FindAsReadOnly(e10::folder::type_guid_v, [&](const std::unique_ptr<e10::library_db::info_db>& Entry)
                    {
                        std::function<void(const e10::folder::guid&, const e10::folder::guid&, folder&)> CollectFolders = [&](const e10::folder::guid& GUID, const e10::folder::guid& ParentGUID, folder& Folder )
                        {
                            const bool bFoundInstance = Entry->m_InfoDataBase.FindAsReadOnly(GUID.m_Instance, [&](const e10::library_db::info_node& InfoEntry)
                            {
                                Folder.m_Guid.m_Instance    = GUID.m_Instance;
                                Folder.m_Name               = InfoEntry.m_Info.m_Name.empty() ? "<unnamed>" : InfoEntry.m_Info.m_Name.c_str();
                                Folder.m_isRoot             = GUID.m_Instance == L.first.m_Instance;
                                Folder.m_isTrash            = GUID.m_Instance.m_Value == (e10::folder::trash_guid_v.m_Instance.m_Value);
                                Folder.m_isOpen             = m_IsTreeNodeOpen[GUID];
                                Folder.m_isSelected         = m_ParentGUID == GUID;
                                Folder.m_isEmpty            = InfoEntry.m_lChildLinks.empty();
                                Folder.m_nDeletedItems      = static_cast<int>(Folder.m_isTrash ? InfoEntry.m_lChildLinks.size() : 0);
                                Folder.m_isDeleted          = InfoEntry.m_Info.m_RscLinks.empty() == false && InfoEntry.m_Info.m_RscLinks[0] == e10::folder::trash_guid_v && Folder.m_isRoot == false;

                                //
                                // See if we have the selected item
                                //
                                if (SelectedItemFound == false)
                                {
                                    if (Folder.m_isSelected)
                                    {
                                        // Tell the system that we found the selected item
                                        SelectedItemFound = true;
                                    }
                                }

                                // Trash-can does not have children as folders
                                if (Folder.m_isTrash) return;

                                //
                                // Fill the rest with the tree if applicable
                                //
                                if (Folder.m_isOpen || Folder.m_isSelected)
                                {
                                    for (auto& ChildGuid : InfoEntry.m_lChildLinks)
                                    {
                                        // We only care about folders here
                                        if (ChildGuid.m_Type == e10::folder::type_guid_v)
                                        {
                                            CollectFolders({ChildGuid.m_Instance}, GUID, Folder.m_Children.emplace_back());
                                        }
                                    }
                                }
                            });

                            assert(bFoundInstance);
                        };

                        CollectFolders({ L.first.m_Instance }, {}, RootFolder);
                    });
                    assert(bFoundType);
                }

                //
                // Short the folders by the name
                //
                {
                    std::function<void(folder&)> SortFolders = [&](folder& F)
                    {
                        std::sort(F.m_Children.begin(), F.m_Children.end(), [](const folder& A, const folder& B)
                        {
                            // Trash is always first
                            if ( A.m_isTrash ) return true;
                            if ( B.m_isTrash ) return false;
                            return A.m_Name < B.m_Name;
                        });
                        for (auto& C : F.m_Children) SortFolders(C);
                    };
                    SortFolders(RootFolder);
                }

                //
                // Process the folders
                //
                std::function<void(const folder&, const e10::folder::guid)> DisplayNodes = [&](const folder& Folder, const e10::folder::guid ParentGUID )
                {
                    //
                    // We do not display deleted folders (for now)
                    //
                    if ( Folder.m_isDeleted ) return;

                    ImVec2          nodePosition    = ImGui::GetCursorPos();
                    bool&           IsInTheoryOpen  = m_IsTreeNodeOpen[Folder.m_Guid];
                    if (Folder.m_isSelected) ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);

                    static constexpr std::array<std::array<const char*, 2>, 2> IconSet = { { { "\xee\xa0\xb8", "\xee\xa2\xb7" }, {"\xEE\xA3\x95", "\xEE\xB5\x84"} } };

                    //static constexpr std::array<std::array<const char*, 2>, 2> IconSet = { { { "\xEE\xB5\x81", "\xEE\xB5\x83" }, {"\xEE\xB5\x82", "\xEE\xB5\x84"} } };

                    static constexpr std::array< const char*, 2> IconSetRoot = { "\xEE\xB1\x90", "\xEE\xB1\x91" };
                    // IconSetRoot[InfoEntry.m_lChildLinks.size() >= 1 ? 1 : 0]
                    // IconSet[InfoEntry.m_lChildLinks.size() >= 1 ? 1 : 0][IsInTheoryOpen]
                    

                    const char* pCon            = Folder.m_isTrash ? "\xEE\x9D\x8D" : Folder.m_isRoot ? (L.first.m_Type == e10::project::type_guid_v? "\xEE\xB0\xA6" : "\xEE\xA3\xB1") : Folder.m_isEmpty ? "\xEE\xA2\xB7" : "\xEE\xA3\x95";
                    const auto AdditionaFlags   = Folder.m_isEmpty ? (ImGuiTreeNodeFlags_Leaf ) : 0u;
                    const auto Str              = Folder.m_isTrash ? std::format("{} {} ({})###{}", pCon, Folder.m_Name.data(), Folder.m_nDeletedItems, Folder.m_Guid.m_Instance.m_Value)
                                                                   : std::format("{} {}###{}", pCon, Folder.m_Name.data(), Folder.m_Guid.m_Instance.m_Value);

                    if (IsInTheoryOpen) ImGui::SetNextItemOpen(IsInTheoryOpen);
                    bool bOpen = ImGui::TreeNodeEx(Str.c_str(), AdditionaFlags | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow | (Folder.m_isSelected ? ImGuiTreeNodeFlags_Selected : 0));
                    if (Folder.m_isSelected) ImGui::PopFont();
                    IsInTheoryOpen = bOpen;

                    const auto FullGuid = e10::folder::guid{ Folder.m_Guid };

                    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                    {
                        m_ParentGUID = FullGuid;
                        m_SelectedType.clear();
                        m_SelectedLibrary = L.second->m_Library.m_GUID;
                        SelectedItemFound = true;
                        PathHistoryUpdate( m_SelectedLibrary, m_ParentGUID );
                    }

                    // Drag source
                    if (!Folder.m_isRoot && !Folder.m_isTrash && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                    {
                        drag_and_drop_folder_payload_t Payload = { {ParentGUID}, FullGuid };

                        ImGui::SetDragDropPayload("DESCRIPTOR_GUID", &Payload, sizeof(Payload), false);
                        ImGui::Text(Str.substr(0, Str.find('#')).c_str());
                        ImGui::EndDragDropSource();
                        m_DraggedDescriptorItem = Payload.m_Source;
                    }

                    // Drag target
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DESCRIPTOR_GUID"))
                        {
                            IM_ASSERT(payload->DataSize == sizeof(drag_and_drop_folder_payload_t));
                            drag_and_drop_folder_payload_t payload_n = *(const drag_and_drop_folder_payload_t*)payload->Data;
                            m_DraggedDescriptorItem = {};
                            // Move the folder

                            if (payload_n.m_bSelection)
                            {
                                assert(!m_SelectedItems.empty());

                                bool bValidationFailed = false;
                                for (auto& S : m_SelectedItems)
                                {
                                    if (S == FullGuid)
                                    {
                                        bValidationFailed = true;
                                        break;
                                    }
                                }

                                if (bValidationFailed == false)
                                {
                                    if (Folder.m_isTrash)
                                    {
                                        for (auto& S : m_SelectedItems)
                                        {
                                            if (auto Err = m_AssetMgr.MoveToTrash(m_SelectedLibrary, S); Err.empty() == false )
                                            {
                                                // Handle the error
                                                int a = 0;
                                            }
                                        }
                                    }
                                    else
                                    {
                                        for (auto& S : m_SelectedItems)
                                        {
                                            if (auto Err = m_AssetMgr.MoveDescriptor(m_SelectedLibrary, S, payload_n.m_Parent, FullGuid); Err)
                                            {
                                                // Handle the error
                                                int a = 0;
                                            }
                                        }
                                    }
                                }
                            }
                            else if (payload_n.m_Parent != FullGuid)
                            {
                                if (Folder.m_isTrash)
                                {
                                    // If we are dropping into the trash, we should move the item to the trash
                                    if (auto Err = m_AssetMgr.MoveToTrash(L.second->m_Library.m_GUID, payload_n.m_Source); Err.empty() == false)
                                    {
                                        // Handle the error
                                        int a = 0;
                                    }
                                }
                                else
                                {
                                    // Implement the logic to move the folder here
                                    // For example, update the parent GUID of the dragged folder to the target folder's GUID
                                    if (auto Err = m_AssetMgr.MoveDescriptor(L.first, payload_n.m_Source, payload_n.m_Parent, FullGuid); Err)
                                    {
                                        // Handle the error
                                        int a = 0;
                                    }
                                }
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }


                    // If we're renaming this folder, show an input text box
                    if (renameFolder.empty() == false && renameFolder == FullGuid)
                    {
                        ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
                        color.w = 1; // Set alpha
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, color);

                        color = ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered);
                        color.w = 1; // Set alpha
                        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, color);

                        color = ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive);
                        color.w = 1; // Set alpha
                        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, color);

                        nodePosition.x += 20.0f; // Indent the input box
                        nodePosition.y -= 2.0f; // Align the input box with the node text
                        ImGui::SetCursorPos(nodePosition); // Set cursor position to overlap the node
                        ImGui::SetKeyboardFocusHere();
                        ImGui::SetNextItemWidth(-1);
                        ImGui::InputText("##Rename", newName, sizeof(newName));
                        if (ImGui::IsItemDeactivatedAfterEdit())
                        {
                            if ( auto Err = m_AssetMgr.RenameDescriptor(L.first, FullGuid, newName); Err )
                            {
                                printf("Failed to rename the folder with error[%s]\n", Err.getMessage().data());
                            }
                            renameFolder.clear();
                        }

                        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) 
                        {
                            ImGui::CloseCurrentPopup(); // Close without saving
                            renameFolder.clear();
                        }

                        // If user clicks outside the popup, close it
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsWindowHovered() )
                        {
                            ImGui::CloseCurrentPopup();
                            renameFolder.clear();
                        }

                        ImGui::PopStyleColor(3);

                    }
                    else if (FullGuid.m_Instance != m_AssetMgr.m_ProjectGUID.m_Instance && FullGuid != e10::folder::trash_guid_v)
                    {
                        // Detect double-click for renaming
                        if (ImGui::IsItemClicked(0) && ImGui::IsMouseDoubleClicked(0))
                        {
                            renameFolder = FullGuid;
                            strncpy_s(newName, sizeof(newName), Folder.m_Name.data(), Folder.m_Name.length() + 1);
                        }
                    }

                    if (bOpen)
                    {
                        for (auto& Child : Folder.m_Children)
                        {
                            // We only care about folders here
                            DisplayNodes(Child, Folder.m_Guid);
                        }

                        ImGui::TreePop();
                    }
                };

                DisplayNodes(RootFolder, {0} );
            }

            // let the user unselect the item
            if (0)
            if (SelectedItemFound == false)
            {
                m_SelectedType.clear();
                m_SelectedLibrary.clear();
                m_ParentGUID.clear();
            }
        }

        //=============================================================================

        void TopControls()
        {
            AddResourceButton();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));        // Transparent background
            ImGui::SameLine(0, 3.5f);

            AddViewMenuButton();
            RenderNavigationPath();
            ImGui::PopStyleColor(1);
        }

        //=============================================================================
        /*
        std::string ConvertHexEscapes(const std::string_view input)
        {
            std::vector<unsigned char> bytes;
            for (size_t i = 0; i < input.size(); ++i) 
            {
                if (input[i] == '\\' && i + 1 < input.size() && ( input[i + 1] == 'x' || input[i + 1] == 'X')) 
                {
                    if (i + 3 < input.size()) 
                    {
                        unsigned int byte;
                        std::stringstream ss;
                        ss << std::hex << input.substr(i + 2, 2);
                        ss >> byte;
                        bytes.push_back(static_cast<unsigned char>(byte));
                        i += 3; // Skip the next three characters (\xNN)
                    }
                    else if (i + 2 < input.size()) 
                    {
                        unsigned int byte;
                        std::stringstream ss;
                        ss << std::hex << input.substr(i + 2, 1);
                        ss >> byte;
                        bytes.push_back(static_cast<unsigned char>(byte));
                        i += 2; // Skip the next two characters (\xN)
                    }
                }
                else 
                {
                    bytes.push_back(static_cast<unsigned char>(input[i]));
                }
            }
            return std::string(bytes.begin(), bytes.end());
        }
        */

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
                    const auto low  = hex_lookup[input[i + 3]];
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

        void RightPanel() override
        {
            TopControls();

            struct temp_node
            {
                std::string             m_ResourceName;
                std::string_view        m_TypeNameView;
                std::string_view        m_IconView;
                xresource::full_guid    m_ResourceGUID;
                bool                    m_bHasChildren:1
                ,                       m_bModified:1
                ,                       m_bDeleted:1;
            };

            //
            // Collect all the nodes into our vector
            //
            std::vector<temp_node> TempNodes;

            const bool isParentTrashcan = m_SelectedLibrary.isValid() && m_ParentGUID == e10::folder::trash_guid_v;

            m_AssetMgr.m_mLibraryDB.FindAsReadOnly(m_SelectedLibrary
            , [&](const std::unique_ptr<e10::library_db>& Lib)
            {
                Lib->m_InfoByTypeDataBase.FindAsReadOnly(m_ParentGUID.m_Type
                , [&](const std::unique_ptr<library_db::info_db>& Entry)
                {
                    Entry->m_InfoDataBase.FindAsReadOnly(m_ParentGUID.m_Instance
                    , [&](const e10::library_db::info_node& Node)
                    {
                        TempNodes.reserve(Node.m_lChildLinks.size());

                        for (auto& E : Node.m_lChildLinks)
                        {
                            // Skip the trash can...
                            if (E.m_Type == e10::folder::type_guid_v && E.m_Instance.m_Value == (e10::folder::trash_guid_v.m_Instance.m_Value)) continue;

                            //
                            // Allow to filter by type
                            // 
                            bool bFilter = false;
                            for (auto& I : m_FilterByType)
                            {
                                if (I == E.m_Type)
                                {
                                    bFilter = true;
                                    break;
                                }
                            }
                            if (bFilter) continue;

                            //
                            // Otherwise, add the node
                            //
                            temp_node Temp;

                            Temp.m_ResourceGUID = E;

                            if (auto e = m_AssetMgr.m_AssetPluginsDB.m_mPluginsByTypeGUID.find(E.m_Type); e == m_AssetMgr.m_AssetPluginsDB.m_mPluginsByTypeGUID.end())
                            {
                                Temp.m_TypeNameView = "<unknown>";
                            }
                            else
                            {
                                auto& Plugin = m_AssetMgr.m_AssetPluginsDB.m_lPlugins[e->second];
                                Temp.m_TypeNameView = Plugin.m_TypeName;
                                Temp.m_IconView     = Plugin.m_Icon;
                            }

                            // Get the name
                            Lib->m_InfoByTypeDataBase.FindAsReadOnly(E.m_Type
                                , [&](const std::unique_ptr<library_db::info_db>& Entry)
                                {
                                    Entry->m_InfoDataBase.FindAsReadOnly(E.m_Instance
                                        , [&](const e10::library_db::info_node& Node)
                                        {
                                            Temp.m_ResourceName = Node.m_Info.m_Name;
                                            Temp.m_bHasChildren = !Node.m_lChildLinks.empty();
                                            Temp.m_bModified    = Node.m_InfoChangeCount > 0;
                                            Temp.m_bDeleted     = Node.m_Info.m_RscLinks.empty() == false && Node.m_Info.m_RscLinks[0] == e10::folder::trash_guid_v;
                                        });
                                });

                            TempNodes.emplace_back(std::move(Temp));
                        }
                    });
                });
            });

            //
            // Short the vector nodes
            //
            switch (m_ShortBasedOn)
            {
            case sort_base_on::NAME_ASCENDING:
                std::sort(TempNodes.begin(), TempNodes.end()
                    , [](const temp_node& a, const temp_node& b)
                    {
                        if (a.m_ResourceGUID.m_Type == e10::folder::type_guid_v && b.m_ResourceGUID.m_Type != e10::folder::type_guid_v) return true;
                        if (a.m_ResourceGUID.m_Type != e10::folder::type_guid_v && b.m_ResourceGUID.m_Type == e10::folder::type_guid_v) return false;

                        if (a.m_ResourceName.empty() || b.m_ResourceName.empty()) return a.m_ResourceGUID.m_Instance.m_Value < b.m_ResourceGUID.m_Instance.m_Value;
                        return a.m_ResourceName < b.m_ResourceName;
                    });
                break;
            case sort_base_on::NAME_DESCENDING:
                std::sort(TempNodes.begin(), TempNodes.end()
                    , [](const temp_node& a, const temp_node& b)
                    {
                        if (a.m_ResourceGUID.m_Type == e10::folder::type_guid_v && b.m_ResourceGUID.m_Type != e10::folder::type_guid_v) return true;
                        if (a.m_ResourceGUID.m_Type != e10::folder::type_guid_v && b.m_ResourceGUID.m_Type == e10::folder::type_guid_v) return false;

                        if (a.m_ResourceName.empty() || b.m_ResourceName.empty()) return a.m_ResourceGUID.m_Instance.m_Value > b.m_ResourceGUID.m_Instance.m_Value;
                        return a.m_ResourceName > b.m_ResourceName;
                    });
                break;
            case sort_base_on::TYPE_ASCENDING:
                std::sort(TempNodes.begin(), TempNodes.end()
                    , [](const temp_node& a, const temp_node& b)
                    {
                        if (a.m_ResourceGUID.m_Type == e10::folder::type_guid_v && b.m_ResourceGUID.m_Type != e10::folder::type_guid_v) return true;
                        if (a.m_ResourceGUID.m_Type != e10::folder::type_guid_v && b.m_ResourceGUID.m_Type == e10::folder::type_guid_v) return false;

                        if (a.m_TypeNameView.empty() || b.m_TypeNameView.empty())
                        {
                            if ( a.m_ResourceGUID.m_Type.m_Value < b.m_ResourceGUID.m_Type.m_Value ) return true;
                            if ( a.m_ResourceGUID.m_Type.m_Value > b.m_ResourceGUID.m_Type.m_Value ) return false;
                        }
                        else
                        {
                            if (a.m_TypeNameView < b.m_TypeNameView) return true;
                            if (a.m_TypeNameView > b.m_TypeNameView) return false;
                        }

                        if (a.m_ResourceName.empty() || b.m_ResourceName.empty()) return a.m_ResourceGUID.m_Instance.m_Value < b.m_ResourceGUID.m_Instance.m_Value;
                        return a.m_ResourceName < b.m_ResourceName;
                    });
                break;
            case sort_base_on::TYPE_DESCENDING:
                std::sort(TempNodes.begin(), TempNodes.end()
                    , [](const temp_node& a, const temp_node& b)
                    {
                        if (a.m_ResourceGUID.m_Type == e10::folder::type_guid_v && b.m_ResourceGUID.m_Type != e10::folder::type_guid_v) return true;
                        if (a.m_ResourceGUID.m_Type != e10::folder::type_guid_v && b.m_ResourceGUID.m_Type == e10::folder::type_guid_v) return false;

                        if (a.m_TypeNameView.empty() || b.m_TypeNameView.empty())
                        {
                            if (a.m_ResourceGUID.m_Type.m_Value > b.m_ResourceGUID.m_Type.m_Value) return true;
                            if (a.m_ResourceGUID.m_Type.m_Value < b.m_ResourceGUID.m_Type.m_Value) return false;
                        }
                        else
                        {
                            if (a.m_TypeNameView > b.m_TypeNameView) return true;
                            if (a.m_TypeNameView < b.m_TypeNameView) return false;
                        }

                        if (a.m_ResourceName.empty() || b.m_ResourceName.empty()) return a.m_ResourceGUID.m_Instance.m_Value > b.m_ResourceGUID.m_Instance.m_Value;
                        return a.m_ResourceName > b.m_ResourceName;
                    });
                break;
            }

            //
            // Ready to render our list...
            //
            bool    ClearSelection  = false;
            bool    ActionHappen    = false;

            float   old_font_size   = ImGui::GetFont()->Scale;
            ImGui::GetFont()->Scale *= 0.95f;
            ImGui::PushFont(ImGui::GetFont());

            ImVec2 button_sz(80, 80 + 20);
            ImGuiStyle& style = ImGui::GetStyle();


            int buttons_count = (int)TempNodes.size();
            float window_visible_x2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
            int n = 0;


            auto ExtraSpacing  = ImGui::GetWindowContentRegionMax().x / (style.ItemSpacing.x + button_sz.x);
            auto HowManyCanFit = int(ExtraSpacing);
            auto StyleSpacing  = style.ItemSpacing.x;
            float StartOffset;
            if (HowManyCanFit > 0 && HowManyCanFit < TempNodes.size())
            {

                ExtraSpacing = (ExtraSpacing - HowManyCanFit) * (style.ItemSpacing.x + button_sz.x);
                ExtraSpacing = ExtraSpacing / HowManyCanFit;

                auto ButtonGrowth = ExtraSpacing / 2.5f;
                StyleSpacing += ExtraSpacing - ButtonGrowth;

                if (HowManyCanFit > 1)
                {
                    button_sz.x += ButtonGrowth;
                    button_sz.y += ButtonGrowth;
                }

                StartOffset = ExtraSpacing / 2;
            }
            else
            {
                ExtraSpacing = 0;
                StartOffset = 0;
            }
            bool bNewLine = true;
            ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);

            for (auto& E : TempNodes)
            {
                if (m_DisplayDeletedItems == false && E.m_bDeleted && isParentTrashcan==false) continue;

                ImGui::PushID(n);
                ImU32 Color = ImGui::ColorConvertFloat4ToU32(textColor);


                const char* pIcon = nullptr;

                if (E.m_ResourceGUID.m_Type == e10::folder::type_guid_v)
                {
                    pIcon = E.m_bHasChildren ? "\xEE\xA3\x95" : "\xEE\xA2\xB7";
                    Color = IM_COL32(123, 107, 52, 255);
                    // ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.345f*1.4f, 0.3f * 1.4f, 0.145f * 1.4f, 1.00f));
                }
                else
                {
                    static std::string Icon;

                    Icon = ConvertHexEscapes(E.m_IconView);
                    pIcon = Icon.c_str();
                }
                if (bNewLine)ImGui::SetCursorPosX(StartOffset);

                std::string StringOne = E.m_ResourceName.empty() ? std::string_view("<Unknown>").data() : std::string_view(E.m_ResourceName).data();


                bool held = false;

                int ThisSelected = -1;
                for (auto& S : m_SelectedItems)
                {
                    if (S == E.m_ResourceGUID)
                    {
                        ThisSelected = static_cast<int>(&S - m_SelectedItems.data());
                        break;
                    }
                }

                if (ThisSelected == -1) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

                int PressType = 0;
                if (int PressType = WrappedButton2(E.m_ResourceGUID.m_Instance, std::format("{}", StringOne).c_str(), button_sz, Color, pIcon, held, E.m_bModified); PressType == 2)
                {
                    if (E.m_ResourceGUID.m_Type == e10::folder::type_guid_v)
                    {
                        if (m_ParentGUID.empty() == false)
                        {
                            m_IsTreeNodeOpen[m_ParentGUID] = true;
                        }

                        m_ParentGUID.m_Instance = E.m_ResourceGUID.m_Instance;
                        PathHistoryUpdate(m_SelectedLibrary, m_ParentGUID);
                    }
                    else
                    {
                        m_Browser.setSelection(m_SelectedLibrary, E.m_ResourceGUID, {});
                    }
                }
                else if (PressType == 3)
                {
                    ImGui::OpenPopup("Resource Menu");
                    m_ResourceMenuMousePos = ImGui::GetMousePos();
                }
                else if (PressType == 1)
                {
                    if (held)
                    {
                        if (m_SelectedItems.size() == 0 || ImGui::GetIO().KeyCtrl)
                        {
                            if (ThisSelected == -1) m_SelectedItems.push_back(E.m_ResourceGUID);
                            else                    m_SelectedItems.erase(m_SelectedItems.begin() + ThisSelected);
                            ClearSelection = false;
                            ActionHappen = true;
                        }
                        else
                        {
                            if (ImGui::GetIO().KeyShift)
                            {
                                size_t index1 = static_cast<std::size_t>(&E - TempNodes.data());
                                size_t index2 = 0;
                                for (auto& S : TempNodes)
                                {
                                    if (m_SelectedItems[0] == S.m_ResourceGUID)
                                    {
                                        break;
                                    }
                                    ++index2;
                                }

                                if (index1 > index2)
                                {
                                    std::swap(index1, index2);
                                    ++index1;
                                    ++index2;
                                }

                                for (; index1 != index2; ++index1)
                                {
                                    m_SelectedItems.push_back(TempNodes[index1].m_ResourceGUID);
                                    ClearSelection = false;
                                }
                                ActionHappen = true;
                            }
                            else
                            {
                                m_SelectedItems.clear();
                                m_SelectedItems.push_back(E.m_ResourceGUID);
                                ClearSelection = false;
                                ActionHappen = true;
                            }
                        }
                    }
                }
                if (ThisSelected == -1) ImGui::PopStyleColor(1);

                //
                // Begging Drag and drop for the button
                //
                bool bBeenDrag = false;
                if (held && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
                {
                    ClearSelection = false;
                    ActionHappen   = true;

                    //ImGui::GetCurrentContext()->LastItemData.ID = static_cast<ImGuiID>(E.m_ResourceGUID.m_Instance.m_Value);
                    ImGui::PushID(static_cast<int>(E.m_ResourceGUID.m_Instance.m_Value));
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                    {
                        bBeenDrag = true;

                        drag_and_drop_folder_payload_t Payload = { m_ParentGUID, E.m_ResourceGUID,  m_SelectedItems.size() > 1 };

                        ImGui::SetDragDropPayload("DESCRIPTOR_GUID", &Payload, sizeof(Payload));
                        if (m_SelectedItems.size() > 1)
                        {
                            WrappedButton2(E.m_ResourceGUID.m_Instance, "nActive Selection", button_sz, Color, "\xEE\x98\x9F", held);
                        }
                        else
                        {
                            WrappedButton2(E.m_ResourceGUID.m_Instance, std::format("{}", StringOne).c_str(), button_sz, Color, pIcon, held, E.m_bModified);
                        }
                        ImGui::EndDragDropSource();
                        m_DraggedDescriptorItem = E.m_ResourceGUID;
                    }
                    ImGui::PopID();
                }
                else if (PressType == 0 && held == false)
                {
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) == false) ClearSelection = false;
                    else if (ActionHappen == false)
                    {
                        ImGuiWindow*    window      = ImGui::GetCurrentWindow();
                        ImVec2          window_pos  = window->Pos;               // Top-left corner of the window
                        ImVec2          window_size = window->Size;             // Size of the window
                        ImVec2          mouse_pos   = ImGui::GetMousePos();       // Current mouse position

                        // Create a small border on the left to avoid the Scroll bar...
                        window_size.x -= 20;

                        // Avoid the top menu as well
                        window_pos.y += 30;

                        // Check if the mouse click is within the window bounds
                        if (mouse_pos.x >= window_pos.x && mouse_pos.x <= (window_pos.x + window_size.x) &&
                            mouse_pos.y >= window_pos.y && mouse_pos.y <= (window_pos.y + window_size.y))
                        {
                            if (m_CouldDownTimer == 0)ClearSelection = true;
                        }
                    }
                }


                // Drag target
                if (E.m_ResourceGUID.m_Type == e10::folder::type_guid_v)
                {
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DESCRIPTOR_GUID"))
                        {
                            IM_ASSERT(payload->DataSize == sizeof(drag_and_drop_folder_payload_t));
                            drag_and_drop_folder_payload_t payload_n = *(const drag_and_drop_folder_payload_t*)payload->Data;
                            m_DraggedDescriptorItem = {};

                            //
                            // Handle single and selection
                            //
                            if (payload_n.m_bSelection)
                            {
                                assert(!m_SelectedItems.empty());

                                bool bValidationFailed = false;
                                for (auto& S : m_SelectedItems)
                                {
                                    if (S == E.m_ResourceGUID)
                                    {
                                        bValidationFailed = true;
                                        break;
                                    }
                                }

                                if (bValidationFailed == false)
                                {
                                    for (auto& S : m_SelectedItems)
                                    {
                                        if (auto Err = m_AssetMgr.MoveDescriptor(m_SelectedLibrary, S, payload_n.m_Parent, E.m_ResourceGUID); Err)
                                        {
                                            // Handle the error
                                            int a = 0;
                                        }
                                    }
                                }
                            }
                            else if (payload_n.m_Parent != E.m_ResourceGUID)
                            {
                                // Implement the logic to move the folder here
                                // For example, update the parent GUID of the dragged folder to the target folder's GUID
                                if (auto Err = m_AssetMgr.MoveDescriptor(m_SelectedLibrary, payload_n.m_Source, payload_n.m_Parent, E.m_ResourceGUID); Err)
                                {
                                    // Handle the error
                                    int a = 0;
                                }
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                }

                if (--m_CouldDownTimer<=0) m_CouldDownTimer =0;
                if (ImGui::BeginPopup("Resource Menu"))
                {
                    m_CouldDownTimer = 100;
                    if (ImGui::MenuItem("  View Details"))
                    {

                    }

                    if (ImGui::BeginMenu("  Show in Explorer"))
                    {
                        if (ImGui::MenuItem("  Final Resource (Root)"))
                        {
                            auto Str = std::format(L"explorer {}\\Cache\\Resources\\Platforms\\WINDOWS\\", m_AssetMgr.m_ProjectPath);
                            system(xstrtool::To(Str).data());
                        }

                        if (ImGui::MenuItem("  Assets (Root)"))
                        {
                            auto Str = std::format(L"explorer {}\\Assets\\", m_AssetMgr.m_ProjectPath);
                            system(xstrtool::To(Str).data());
                        }

                        m_AssetMgr.m_mLibraryDB.FindAsReadOnly(m_SelectedLibrary, [&](const std::unique_ptr<e10::library_db>& Lib)
                        {
                            Lib->m_InfoByTypeDataBase.FindAsReadOnly(E.m_ResourceGUID.m_Type, [&](const std::unique_ptr<library_db::info_db>& Entry)
                            {
                                Entry->m_InfoDataBase.FindAsReadOnly(E.m_ResourceGUID.m_Instance, [&](const e10::library_db::info_node& InfoEntry)
                                {
                                    if (ImGui::MenuItem("  Descriptor (Root)"))
                                    {
                                        auto Str = InfoEntry.m_Path;

                                        auto pos = Str.rfind(L"Cache");
                                        if (pos != std::wstring::npos)
                                        {
                                            pos = Str.rfind(L"Descriptors");
                                            Str.resize(pos + 11);
                                        }
                                        else
                                        {
                                            pos = Str.rfind(L"Descriptors");
                                            Str.resize(pos + 11);
                                        }

                                        Str = std::format(L"explorer {}", Str);
                                        system(xstrtool::To(Str).data());
                                    }

                                    if ( InfoEntry.m_bHasResource )
                                    {
                                        if (ImGui::MenuItem("  Final Resource"))
                                        {
                                            auto Str = InfoEntry.m_Path;

                                            auto pos = Str.rfind(L"Cache");
                                            if ( pos != std::wstring::npos )
                                            {
                                                pos = Str.rfind(L"Descriptors");
                                                Str.replace(pos, 11, L"Resources\\Platforms\\WINDOWS");
                                            }
                                            else
                                            {
                                                pos = Str.rfind(L"Descriptors");
                                                Str.replace(pos, 11, L"Cache\\Resources\\Platforms\\WINDOWS");
                                            }

                                            pos = Str.rfind(L'\\');
                                            Str.erase(pos - sizeof("desc"));

                                            if (std::filesystem::exists(Str))
                                            {
                                                Str = std::format(L"explorer /select,{}", Str);
                                                system(xstrtool::To(Str).data());
                                            }
                                        }
                                    }

                                    if (std::filesystem::exists(InfoEntry.m_Path))
                                    {
                                        if (ImGui::MenuItem("  Descriptor"))
                                        {
                                            // ShellExecute(NULL, L"open", L"explorer", InfoEntry.m_Path.c_str(), NULL, SW_SHOW);
                                            auto Str = std::format(L"explorer /select,{}", InfoEntry.m_Path);
                                            system(xstrtool::To(Str).data());
                                        }
                                    }

                                    if (InfoEntry.m_bHasDependencies && InfoEntry.m_Dependencies.m_Assets.empty() == false )
                                    {
                                        if (ImGui::BeginMenu("  Assets"))
                                        {
                                            for( auto& E : InfoEntry.m_Dependencies.m_Assets)
                                            {
                                                auto pos = E.rfind('\\') + 1;
                                                if (ImGui::MenuItem(std::format( "###{}{}", (void*) & E, std::string_view(xstrtool::To(E).data() + pos, E.size() - pos).data()).c_str()))
                                                {
                                                    //ShellExecute(NULL, L"open", L"explorer", xcore::string::To<wchar_t>(xcore::string::Fmt("%s\\Assets", strXstr(O.m_pLibraryMgr->m_ProjectPath).c_str())).data(), NULL, SW_SHOW);
                                                    auto Str = std::format(L"explorer /select,{}\\{}", m_AssetMgr.m_ProjectPath, E);
                                                    system(xstrtool::To(Str).data());
                                                }
                                            }

                                            ImGui::EndMenu();
                                        }
                                    }

                                    if (ImGui::MenuItem("  Log File"))
                                    {
                                        auto Str = InfoEntry.m_Path;

                                        auto pos = Str.rfind(L"Cache");
                                        if (pos != std::wstring::npos)
                                        {
                                            pos = Str.rfind(L"Descriptors");
                                            Str.replace(pos, 11, L"Resources\\Logs");
                                        }
                                        else
                                        {
                                            pos = Str.rfind(L"Descriptors");
                                            Str.replace(pos, 11, L"Cache\\Resources\\Logs");
                                        }

                                        pos = Str.rfind(L'\\') - sizeof("desc");
                                        Str.erase(pos);
                                        Str.insert(pos, L".log\\Log.txt");

                                        if (std::filesystem::exists(Str))
                                        {
                                            system(std::format("explorer /select,{}", xstrtool::To(Str) ).c_str());
                                        }
                                    }
                                });
                            });
                        });

                        ImGui::EndMenu(); // Close the popup scope
                    }

                    ImGui::Separator();

                    if (ImGui::MenuItem("  Open"))
                    {
                        if (E.m_ResourceGUID.m_Type == e10::folder::type_guid_v)
                        {
                            if (m_ParentGUID.empty() == false)
                            {
                                m_IsTreeNodeOpen[m_ParentGUID] = true;
                            }

                            m_ParentGUID.m_Instance = E.m_ResourceGUID.m_Instance;
                            PathHistoryUpdate( m_SelectedLibrary, m_ParentGUID );
                        }
                        else
                        {
                            m_Browser.setSelection(m_SelectedLibrary, E.m_ResourceGUID, {});
                        }
                    }

                    if (ImGui::MenuItem("  Rename"))
                    {
                        m_RenameLibrary = m_SelectedLibrary;
                        m_RenameItem    = E.m_ResourceGUID;
                        strcpy_s(m_RenameNewName.data(), m_RenameNewName.size(), E.m_ResourceName.c_str());
                        ImGui::CloseCurrentPopup();
                        
                    }

                    if (isParentTrashcan)
                    {
                        if (ImGui::BeginMenu("  Restore"))
                        {
                            if (m_SelectedItems.empty())
                            {
                                if (auto ParentFolder = m_AssetMgr.getValidParentFolder(m_SelectedLibrary, E.m_ResourceGUID); ParentFolder.isValid())
                                {
                                    if (ImGui::MenuItem("  To Original Location"))
                                    {
                                        m_AssetMgr.MoveFromTrashTo(m_SelectedLibrary, E.m_ResourceGUID, ParentFolder);
                                    }
                                }
                            }
                            else
                            {
                                bool bAllSelectedItemHaveValidParents = true;
                                for (auto& SelcE : m_SelectedItems)
                                {
                                    if (auto ParentFolder = m_AssetMgr.getValidParentFolder(m_SelectedLibrary, SelcE); ParentFolder.isValid() == false)
                                    {
                                        bAllSelectedItemHaveValidParents = false;
                                    }
                                }

                                if (bAllSelectedItemHaveValidParents && ImGui::MenuItem("  To Original Location"))
                                {
                                    for (auto& SelcE : m_SelectedItems)
                                    {
                                        if (auto ParentFolder = m_AssetMgr.getValidParentFolder(m_SelectedLibrary, SelcE); ParentFolder.isValid() )
                                        {
                                            m_AssetMgr.MoveFromTrashTo(m_SelectedLibrary, SelcE, ParentFolder);
                                        }
                                    }
                                }
                            }

                            if (ImGui::MenuItem("  To Root"))
                            {
                                if (m_SelectedItems.empty())
                                {
                                    m_AssetMgr.MoveFromTrashTo(m_SelectedLibrary, E.m_ResourceGUID, {});
                                }
                                else
                                {
                                    for (auto& SelcE : m_SelectedItems)
                                    {
                                        m_AssetMgr.MoveFromTrashTo(m_SelectedLibrary, SelcE, {});
                                    }
                                }
                            }

                            ImGui::EndMenu();
                        }
                    }
                    else
                    {
                        if (ImGui::MenuItem("  Delete"))
                        {
                            if (m_SelectedItems.empty())
                            {
                                if (auto Err = m_AssetMgr.MoveToTrash(m_SelectedLibrary, E.m_ResourceGUID); !Err.empty())
                                {
                                    printf("Error: %s\n", Err.c_str());
                                }
                            }
                            else
                            {
                                for (auto& E : m_SelectedItems)
                                {
                                    if (auto Err = m_AssetMgr.MoveToTrash(m_SelectedLibrary, E); !Err.empty())
                                    {
                                        printf("Error: %s\n", Err.c_str());
                                    }
                                }
                            }
                        }

                        if (ImGui::MenuItem("  Copy Link"))
                        {

                        }

                        if (ImGui::MenuItem("  Duplicate"))
                        {

                        }
                    }

                    ImGui::Separator();
                    if (ImGui::MenuItem("  Recompile Item"))
                    {
                        m_AssetMgr.RecompileResource(m_SelectedLibrary, E.m_ResourceGUID);
                    }

                    if (m_SelectedItems.empty() == false )
                    {
                        if (ImGui::MenuItem("  Recompile Selected Items"))
                        {
                            for (auto& SelcE : m_SelectedItems)
                            {
                                m_AssetMgr.RecompileResource(m_SelectedLibrary, SelcE);
                            }
                        }
                    }
                    



                    ImGui::EndPopup(); // Close the popup scope
                }

                if ( m_RenameItem == E.m_ResourceGUID )
                {
                    ImGui::OpenPopup("Resource Rename");
                    ImGui::SetNextWindowPos(m_ResourceMenuMousePos);
                }
                

                if (ImGui::BeginPopup("Resource Rename"))
                {
                    if (m_RenameFirstOpen)
                    {
                        ImGui::SetKeyboardFocusHere();
                        m_RenameFirstOpen = false;
                    }
                    ImGui::InputText("New Name", m_RenameNewName.data(), m_RenameNewName.size());

                    bool bEnterPressed = false;
                    // Optional: Ensure cursor is at the end (if there's existing text)
                    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) bEnterPressed = true;

                    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                    {
                        ImGui::CloseCurrentPopup(); // Close without saving
                        m_RenameItem.clear();
                    }

                    // If user clicks outside the popup, close it
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered())
                    {
                        ImGui::CloseCurrentPopup();
                        m_RenameItem.clear();
                    }


                    if ( ImGui::Button(" Cancel ") )
                    {
                        m_RenameItem.clear();
                        m_RenameFirstOpen = true;
                        ImGui::CloseCurrentPopup(); // Close on Enter
                    }

                    ImGui::SameLine(0,10);
                    if (ImGui::Button(" OK ") || bEnterPressed )
                    {
                        if (auto Err = m_AssetMgr.RenameDescriptor(m_RenameLibrary, m_RenameItem, m_RenameNewName.data()); Err)
                        {
                            printf("Error: %s\n", Err.getMessage().data() );
                        }
                        m_RenameItem.clear();
                        m_RenameFirstOpen = true;
                        ImGui::CloseCurrentPopup(); // Close on Enter
                    }

                    ImGui::EndPopup(); // Close the popup scope
                }


                if (ImGui::IsItemHovered() && !bBeenDrag)
                {
                    ImGui::BeginTooltip();
                    std::string Text;

                    m_AssetMgr.getNodeInfo( m_SelectedLibrary, E.m_ResourceGUID, [&]( e10::library_db::info_node& NodeInfo )
                    {
                        Text = std::format(
                              "Instance Name        : {}"
                            "\nType Name            : {}"
                            "\nInstance GUID        : {:X}"
                            "\nType GUID            : {:X}"
                            "\nInfo Last Read       : {:%Y-%m-%d %I:%M:%S %p %Z}"
                            "\nInfo Last Write      : {:%Y-%m-%d %I:%M:%S %p %Z}"
                            "\nDescriptor Last Write: {}"
                            "\nResource Last Write  : {}"
                            "\nDependencies         : {}"
                            "\nComment              : {}"
                            , StringOne
                            , E.m_TypeNameView.empty() ? std::string_view("<Unknown>").data() : E.m_TypeNameView.data()
                            , NodeInfo.m_Info.m_Guid.m_Instance.m_Value
                            , NodeInfo.m_Info.m_Guid.m_Type.m_Value
                            , ConvertToStdTime(NodeInfo.m_InfoReadTime)
                            , ConvertToStdTime(NodeInfo.m_InfoTime)
                            , NodeInfo.m_bHasDescriptor ? std::format("{:%Y-%m-%d %I:%M:%S %p %Z}", ConvertToStdTime(NodeInfo.m_DescriptorTime)) : "Never"
                            , NodeInfo.m_bHasResource   ? std::format("{:%Y-%m-%d %I:%M:%S %p %Z}", ConvertToStdTime(NodeInfo.m_ResourceTime)) : "Never"
                            , [&]()->std::string
                            {
                                if (NodeInfo.m_bHasDependencies == false || false == NodeInfo.m_Dependencies.hasDependencies() ) return {"No Dependencies"};

                                std::string Dependencies;

                                if (NodeInfo.m_Dependencies.m_Resources.empty() == false)
                                {
                                    std::string Assets = std::format("\n    Resources Count: {}", NodeInfo.m_Dependencies.m_Resources.size());
                                    for (auto& E : NodeInfo.m_Dependencies.m_Resources)
                                    {
                                        //Assets = std::format("{}\n        [{}] {}", Assets, static_cast<int>(&E - NodeInfo.m_Dependencies.m_Resources.data()), E);
                                    }

                                    Dependencies += Assets;
                                }

                                if ( NodeInfo.m_Dependencies.m_Assets.empty() == false )
                                {
                                    std::string Assets = std::format("\n    Asset Count: {}", NodeInfo.m_Dependencies.m_Assets.size() );
                                    for (auto& E : NodeInfo.m_Dependencies.m_Assets)
                                    {
                                        Assets = std::format( "{}\n        [{}] {}", Assets, static_cast<int>(&E - NodeInfo.m_Dependencies.m_Assets.data()), xstrtool::To(E) );
                                    }

                                    Dependencies += Assets;
                                }

                                if (NodeInfo.m_Dependencies.m_VirtualAssets.empty() == false)
                                {
                                    std::string Assets = std::format("\n    Virtual Asset Count: {}", NodeInfo.m_Dependencies.m_VirtualAssets.size());
                                    for (auto& E : NodeInfo.m_Dependencies.m_VirtualAssets)
                                    {
                                        Assets = std::format("{}\n        [{}] {}", Assets, static_cast<int>(&E - NodeInfo.m_Dependencies.m_VirtualAssets.data()), xstrtool::To(E) );
                                    }

                                    Dependencies += Assets;
                                }


                                return Dependencies;
                            }()
                            , NodeInfo.m_Info.m_Comment
                        );
                    });

                    ImGui::TextUnformatted(Text.c_str());
                    ImGui::EndTooltip();
                }


                //   if (E.m_ResourceGUID.m_Type == e10::folder_type_guid_v)ImGui::PopStyleColor(1);
                float last_button_x2 = ImGui::GetItemRectMax().x;
                float next_button_x2 = last_button_x2 + StyleSpacing + button_sz.x; // Expected position if next button was on same line
                if (n + 1 < buttons_count && next_button_x2 < window_visible_x2)
                {
                    ImGui::SameLine(0, StyleSpacing);
                    bNewLine = false;
                }
                else
                {
                    bNewLine = true;
                }


                ImGui::PopID();
                n++;
            }

            ImGui::GetFont()->Scale = old_font_size;
            ImGui::PopFont();

            if (ClearSelection)
            {
                m_SelectedItems.clear();
            }
        }

        //=============================================================================

        bool                                                m_RenameFirstOpen       = true;
        library::guid                                       m_RenameLibrary         = {};
        xresource::full_guid                                m_RenameItem            = {};
        std::array<char, 256>                               m_RenameNewName         = {};
        ImVec2                                              m_ResourceMenuMousePos;
        

        int                                                 m_CouldDownTimer        = {};
        library::guid                                       m_SelectedLibrary       = {};
        std::vector<xresource::full_guid>                   m_SelectedItems         = {};
        folder::guid                                        m_ParentGUID            = {};
        std::vector<xresource::type_guid>                   m_FilterByType          = {};
        sort_base_on                                        m_ShortBasedOn          = sort_base_on::NAME_ASCENDING;
        std::unordered_map<e10::folder::guid, bool>         m_IsTreeNodeOpen        = {};
        xresource::full_guid                                m_DraggedDescriptorItem = {};
        xresource::type_guid                                m_SelectedType          = {};
        std::vector<path_history_entry>                     m_PathHistoryListRU     = {};
        std::vector<path_history_entry>                     m_PathHistoryList       = {};
        std::uint32_t                                       m_PathHistoryIndex      = {};
        ImVec2                                              m_PathHistoryPos        = {};
        ImVec2                                              m_PathHistorySize       = {};
        bool                                                m_PathHistoryShow       = {};
        bool                                                m_DisplayDeletedItems   = {false};
    };

    //=============================================================================

    namespace
    {
        inline browser_registration<virtual_tree_tab, "\xEE\xA3\xAF Virtual Tree", 0.0f > g_VirtualTree{};
    }
}
