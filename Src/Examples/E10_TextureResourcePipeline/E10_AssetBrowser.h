#ifndef _E10_ASSETBROWSER_H
#define _E10_ASSETBROWSER_H
#pragma once
#include "../../../Tools/xgpu_imgui_breach.h"

namespace e10
{
    struct assert_browser
    {
    public:

        //=============================================================================

        void Show( bool bShow = true )
        {
            m_bRenderBrowser = bShow;
        }

        //=============================================================================

        bool IsVisible() const
        {
            return m_bRenderBrowser;
        }

        //=============================================================================

        void Render( e10::library_mgr& AssetMgr )
        {
            m_pAssetMgr = &AssetMgr;

            if (m_bRenderBrowser == false) return;

            // Main window
            MainWindow();
        }

        //=============================================================================

        xresource::full_guid getNewAsset()
        {
            if (m_LastGeneratedAsset.empty()) return m_LastGeneratedAsset;
            auto GUID = m_LastGeneratedAsset;
            m_LastGeneratedAsset.clear();
            return GUID;
        }

        //=============================================================================

        xresource::full_guid getSelectedAsset()
        {
            if (m_SelectedAsset.empty()) return m_SelectedAsset;
            auto GUID = m_SelectedAsset;
            m_SelectedAsset.clear();
            return GUID;
        }

    protected:

        enum class tab_type
        { DESC_BY_TYPE
        , DESCRIPTORS
        , ASSETS
        };

        struct path_node
        {
            xresource::full_guid m_Guid;
            std::string          m_Name;
        };

        enum class sort_base_on
        { NAME_ASCENDING
        , NAME_DESCENDING
        , TYPE_ASCENDING
        , TYPE_DESCENDING
        , LAST_MODIFIED_ASCENDING
        , LAST_MODIFIED_DESCENDING
        };

        struct drag_and_drop_folder_payload_t
        {
            xresource::full_guid m_Parent;
            xresource::full_guid m_Source;
            bool                 m_bSelection;
        };

        //=============================================================================

        static void Splitter( bool split_vertically, float thickness, float* size1, float* size2, float min_size1, float min_size2, float total_size, float total_height )
        {
            ImVec2 backup_pos = ImGui::GetCursorScreenPos();
            if (split_vertically)
                ImGui::SetCursorScreenPos(ImVec2(backup_pos.x + *size1, backup_pos.y));
            else
                ImGui::SetCursorScreenPos(ImVec2(backup_pos.x, backup_pos.y + *size1));

            // Make the splitter visible with a gray color
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));

            ImGui::Button("##splitter", ImVec2(split_vertically ? thickness : -1.0f, split_vertically ? total_height : thickness));

            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                ImGui::SetMouseCursor(split_vertically ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);

            if (ImGui::IsItemActive())
            {
                float mouse_delta = split_vertically ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y;
                float new_size1 = *size1 + mouse_delta;

                // Clamp the sizes
                new_size1 = std::max(min_size1, std::min(total_size - min_size2 - thickness, new_size1));

                *size1 = new_size1;
                *size2 = total_size - *size1 - thickness;
            }

            ImGui::PopStyleColor(3);
            ImGui::SetCursorScreenPos(backup_pos);
        }

        //=============================================================================

        static void RenderSearchBar(ImVec2 Size)
        {
            static char searchBuffer[256] = ""; // Buffer for search text
            auto x = ImGui::GetCursorPosX();

            if (ImGui::Button("\xe2\x96\xbc"))
            {

            }
            ImGui::SameLine(0, 0.1f);

            if (searchBuffer[0] != 0)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f)); // Gray
                if (ImGui::Button("X"))
                {
                    searchBuffer[0] = 0;
                }

                ImGui::SameLine(0, 0.1f);
                ImGui::PopStyleColor();
            }
            Size.x -= ImGui::GetCursorPosX() - x;

            // Style adjustments
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f); // Rounded corners

            // Full width for the input
            float inputWidth = Size.x;
            ImGui::PushItemWidth(inputWidth);

            // Search input field
            ImGui::InputText("##search", searchBuffer, sizeof(searchBuffer));

            // Check if input is focused or has text
            bool isActive = ImGui::IsItemActive(); // True when input is focused
            bool hasText = (searchBuffer[0] != '\0'); // True when buffer has content

            // Render magnifying glass only when input is inactive and empty
            if (!isActive && !hasText) {
                // Position the icon inside the input field
                ImVec2 inputPos = ImGui::GetItemRectMin(); // Top-left corner of input
                ImVec2 cursorPos = ImGui::GetCursorScreenPos(); // Current cursor pos
                float offsetX = inputPos.x + 10.0f; // 10px from left edge
                float offsetY = inputPos.y + 4.0f;  // Vertically center (adjust as needed)

                ImGui::SetCursorScreenPos(ImVec2(offsetX, offsetY)); // Move cursor to position icon
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f)); // Gray
                ImGui::Text("\xee\x9c\xa1"); // Magnifying glass

                ImGui::PopStyleColor();

                // Reset cursor position to avoid affecting layout
                ImGui::SetCursorScreenPos(cursorPos);
            }

            ImGui::PopItemWidth();
            ImGui::PopStyleVar(1); // Restore style vars
        }

        //=============================================================================
        xresource::full_guid m_DraggedDescriptorItem = {};
        void FolderWindow()
        {
            m_PathNodes.clear();
            static xresource::full_guid renameFolder = {};
            static char newName[128] = "";
            bool SelectedItemFound = false;
            for (auto& L : m_pAssetMgr->m_mLibraryDB)
            {
                auto RootNodeGUID = xresource::full_guid{ m_pAssetMgr->m_ProjectGUID.m_Instance, e10::folder_type_guid_v };
                auto Path = strXstr(L.second.m_Library.m_Path);
                Path = Path.substr(Path.rfind('\\') + 1);

                bool bFoundType = L.second.m_InfoByTypeDataBase.FindAsReadOnly(RootNodeGUID.m_Type
                , [&](const std::unique_ptr<e10::library_db::info_db>& Entry)
                {
                    
                    std::function<void(const xresource::full_guid&, const xresource::full_guid&)> DisplayNodes = [&](const xresource::full_guid& GUID, const xresource::full_guid& ParentGUID )
                    {
                        bool bFoundInstance = Entry->m_InfoDataBase.FindAsWrite(GUID.m_Instance
                        , [&](e10::library_db::info_node& InfoEntry)
                        {
                            std::string_view Name = GUID.m_Instance == m_pAssetMgr->m_ProjectGUID.m_Instance ? Path.c_str() : InfoEntry.m_Info.m_Name.c_str();
                            if (Name.empty()) Name = "<unnamed>";

                            if (SelectedItemFound == false)
                            {
                                if (m_PathNodes.empty() )
                                {
                                    m_PathNodes.push_back(path_node{ GUID, std::string(Name.substr( 0, Name.rfind('.') )) });
                                }
                                else
                                {
                                    m_PathNodes.push_back(path_node{ GUID, std::string(Name) });
                                }
                            }

                            ImVec2 nodePosition = ImGui::GetCursorPos();

                            const bool isSelected = m_ParentGUID == InfoEntry.m_Info.m_Guid;
                            bool& IsInTheoryOpen = m_IsTreeNodeOpen[GUID.m_Instance];
                            if (isSelected) ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);

                           // static std::array<std::array<const char*, 2>, 2> IconSet = { { { "\xee\xa0\xb8", "\xee\xa2\xb7" }, {"\xEE\xA3\x95", } } };
                            static std::array<std::array<const char*, 2>, 2> IconSet = { { { "\xEE\xB5\x81", "\xEE\xB5\x83" }, {"\xEE\xB5\x82", "\xEE\xB5\x84"} } };

                            const char* pCon = IconSet[InfoEntry.m_lChildLinks.size() >= 1 ? 1 : 0][IsInTheoryOpen];

                            //auto Str = (IsInTheoryOpen ? std::format("\xee\xa0\xb8 {}###{}", Name.data(), GUID.m_Instance.m_Value) : std::format("\xee\xa2\xb7 {}###{}", Name.data(), GUID.m_Instance.m_Value));

                            auto Str = std::format("{} {}###{}", pCon, Name.data(), GUID.m_Instance.m_Value);

                            if (IsInTheoryOpen) ImGui::SetNextItemOpen(IsInTheoryOpen);
                            bool bOpen = ImGui::TreeNodeEx(Str.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | (isSelected ? ImGuiTreeNodeFlags_Selected : 0));
                            if (isSelected) ImGui::PopFont();
                            IsInTheoryOpen = bOpen;

                            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                            {
                                m_ParentGUID = GUID;
                                m_SelectedType.clear();
                                m_SelectedLibrary = L.second.m_Library.m_GUID;
                            }

                            // Drag source
                            if (ParentGUID.empty() == false && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                            {
                                drag_and_drop_folder_payload_t Payload = { ParentGUID, GUID };

                                ImGui::SetDragDropPayload("DESCRIPTOR_GUID", &Payload, sizeof(Payload), false);
                                ImGui::Text(Str.substr(0,Str.find('#')).c_str());
                                ImGui::EndDragDropSource();
                                m_DraggedDescriptorItem = GUID;
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
                                            if (S == GUID)
                                            {
                                                bValidationFailed = true;
                                                break;
                                            }
                                        }

                                        if (bValidationFailed == false)
                                        {
                                            for (auto& S : m_SelectedItems)
                                            {
                                                if (auto Err = m_pAssetMgr->MoveDescriptor(m_SelectedLibrary, S, payload_n.m_Parent, GUID); Err)
                                                {
                                                    // Handle the error
                                                    int a = 0;
                                                }
                                            }
                                        }
                                    }
                                    else if (payload_n.m_Parent != GUID)
                                    {
                                        // Implement the logic to move the folder here
                                        // For example, update the parent GUID of the dragged folder to the target folder's GUID
                                        if ( auto Err = m_pAssetMgr->MoveDescriptor(L.first, payload_n.m_Source, payload_n.m_Parent, GUID); Err )
                                        {
                                            // Handle the error
                                            int a=0;
                                        }
                                    }
                                }
                                ImGui::EndDragDropTarget();
                            }


                            // If we're renaming this folder, show an input text box
                            if (renameFolder.empty() == false && renameFolder == GUID)
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
                                    InfoEntry.m_Info.m_Name = std::format("{}", newName);
                                    renameFolder.clear();
                                }
                                ImGui::PopStyleColor(3);

                            }
                            else if (GUID.m_Instance != m_pAssetMgr->m_ProjectGUID.m_Instance)
                            {
                                // Detect double-click for renaming
                                if (ImGui::IsItemClicked(0) && ImGui::IsMouseDoubleClicked(0))
                                {
                                    renameFolder = GUID;
                                    strncpy_s(newName, sizeof(newName), InfoEntry.m_Info.m_Name.c_str(), InfoEntry.m_Info.m_Name.length() + 1);
                                }
                            }

                            if (bOpen)
                            {
                                for (auto& ChildGuid : InfoEntry.m_lChildLinks)
                                {
                                    // We only care about folders here
                                    if (ChildGuid.m_Type == e10::folder_type_guid_v) DisplayNodes(ChildGuid, GUID);
                                }

                                ImGui::TreePop();
                            }


                            if (SelectedItemFound == false) SelectedItemFound = (m_ParentGUID == InfoEntry.m_Info.m_Guid);
                            if (SelectedItemFound == false) m_PathNodes.pop_back();
                        });
                        assert(bFoundInstance);
                    };

                    DisplayNodes(RootNodeGUID, {0,0});
                });
                assert(bFoundType);

                /*
                for (auto& T : L.m_InfoByTypeDataBase)
                {
                    std::string TypeView;
                    if (auto e = m_pAssetMgr->m_AssetPluginsDB.m_mPluginsByTypeGUID.find(T->m_TypeGUID); e == m_pAssetMgr->m_AssetPluginsDB.m_mPluginsByTypeGUID.end())
                    {
                        TypeView = std::format("{:X}", T->m_TypeGUID.m_Value);
                    }
                    else
                    {
                        TypeView = std::format("{} - {:X}", m_pAssetMgr->m_AssetPluginsDB.m_lPlugins[e->second].m_TypeName, T->m_TypeGUID.m_Value);
                    }
                    if (ImGui::TreeNodeEx(TypeView.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | (pSelected == T.get() ? ImGuiTreeNodeFlags_Selected : 0)))
                    {
                        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                        {
                            pSelected = T.get();
                            pm_SelectedLibrary = &L;
                        }


                        ImGui::TreePop();
                    }

                    if (SelectedItemFound == false) SelectedItemFound = (pSelected == T.get());

                }
                */

                // let the user unselect the item
                if (SelectedItemFound == false )//|| (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()))
                {
                    m_SelectedType.clear();
                    m_SelectedLibrary.clear();
                    m_ParentGUID.clear();
                }
            }
        }

        //=============================================================================

        void ByTypeFolderWindow()
        {
            bool SelectedItemFound = false;
            for (auto& L : m_pAssetMgr->m_mLibraryDB)
            {
                auto Path = strXstr(L.second.m_Library.m_Path);
                if (ImGui::TreeNodeEx(Path.substr(Path.rfind('\\') + 1).c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth))
                {
                    for (auto& T : L.second.m_InfoByTypeDataBase)
                    {
                        std::string TypeView;
                        if (auto e = m_pAssetMgr->m_AssetPluginsDB.m_mPluginsByTypeGUID.find(T.second->m_TypeGUID); e == m_pAssetMgr->m_AssetPluginsDB.m_mPluginsByTypeGUID.end())
                        {
                            TypeView = std::format("{:X}", T.second->m_TypeGUID.m_Value);
                        }
                        else
                        {
                            TypeView = std::format("{} - {:X}", m_pAssetMgr->m_AssetPluginsDB.m_lPlugins[e->second].m_TypeName, T.second->m_TypeGUID.m_Value);
                        }
                        if (ImGui::TreeNodeEx(TypeView.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | (m_SelectedType == T.second->m_TypeGUID ? ImGuiTreeNodeFlags_Selected : 0)))
                        {
                            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                            {
                                m_SelectedType = T.second->m_TypeGUID;
                                m_SelectedLibrary = L.second.m_Library.m_GUID;
                            }


                            ImGui::TreePop();
                        }

                        if (SelectedItemFound == false) SelectedItemFound = (m_SelectedType == T.second->m_TypeGUID);

                    }
                    ImGui::TreePop();
                }
            }

            // let the user unselect the item
            if (SelectedItemFound == false || (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()))
            {
                m_SelectedType.clear();
                m_SelectedLibrary.clear();
                m_ParentGUID.clear();
            }
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

        void AddResourcePopUp()
        {
            for (auto& E : m_pAssetMgr->m_AssetPluginsDB.m_lPlugins)
            {
                if (ImGui::MenuItem(E.m_TypeName.c_str()))
                {
                    auto LibGUID = m_SelectedLibrary.empty() ? m_pAssetMgr->m_ProjectGUID : m_SelectedLibrary;
                    m_LastGeneratedAsset = m_pAssetMgr->NewAsset(E.m_TypeGUID, LibGUID, m_ParentGUID);

                    if (E.m_TypeGUID == e10::folder_type_guid_v)
                    {
                        m_pAssetMgr->m_mLibraryDB.FindAsReadOnly(LibGUID, [&](const e10::library_db& Lib)
                        {
                            Lib.m_InfoByTypeDataBase.FindAsReadOnly(m_LastGeneratedAsset.m_Type, [&](const std::unique_ptr<e10::library_db::info_db>& InfoDB)
                            {
                                InfoDB->m_InfoDataBase.FindAsWrite(m_LastGeneratedAsset.m_Instance, [&](e10::library_db::info_node& Node)
                                {
                                    Node.m_Info.m_Name = std::format("New Folder - {:X}", m_LastGeneratedAsset.m_Instance.m_Value);
                                });
                            });
                        });
                    }
                    else
                    {
                        if (m_bAutoClose) m_bRenderBrowser = false;
                    }
                }
            }
        }

        //=============================================================================

        static bool WrappedButton (const char* label, const ImVec2& size = ImVec2(0, 0))
        {
            ImGuiContext& g = *ImGui::GetCurrentContext();
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            if (window->SkipItems)
                return false;

            ImGuiID id = window->GetID(NULL);

            // Calculate button rectangle
            ImVec2 pos = window->DC.CursorPos;
            ImVec2 end_pos;
            end_pos.x = pos.x + size.x;
            end_pos.y = pos.y + size.y;
            const ImRect bb(pos, end_pos);

            ImGui::ItemSize(bb);
            if (!ImGui::ItemAdd(bb, id))
                return false;

            bool hovered, held;
            bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_PressedOnDoubleClick);

            // Render button background
            const ImU32 col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_ButtonActive :
                hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
            ImGui::RenderNavHighlight(bb, id);
            ImGui::RenderFrame(bb.Min, bb.Max, col, true, ImGui::GetStyle().FrameRounding);

            // Calculate available text area with proper padding
            ImVec2 padding = ImGui::GetStyle().FramePadding;
            float text_width = size.x - (padding.x * 2); // Reduce width by padding on both sides

            // Set wrapping position relative to button start
            ImGui::PushTextWrapPos(pos.x + text_width);

            // Calculate text position with padding
            ImVec2 text_pos;
            text_pos.x = pos.x + padding.x;
            text_pos.y = pos.y + padding.y;

            // Clip text to button bounds
            ImGui::PushClipRect(bb.Min, bb.Max, true);
            ImGui::RenderTextWrapped(text_pos, label, NULL, text_width);
            ImGui::PopClipRect();
            ImGui::PopTextWrapPos();

            return  pressed;
        }

        /*
        static bool WrappedButton2(const char* label, const ImVec2& size, ImU32 Color, const char* pIcon )
        {
            ImGuiContext& g = *ImGui::GetCurrentContext();
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            if (window->SkipItems)
                return false;

            ImGuiID id = window->GetID(NULL);

            // Calculate button rectangle
            ImVec2 pos = window->DC.CursorPos;
            ImVec2 end_pos;
            end_pos.x = pos.x + size.x;
            end_pos.y = pos.y + size.y;
            const ImRect bb(pos, end_pos);

            ImGui::ItemSize(bb);
            if (!ImGui::ItemAdd(bb, id))
                return false;

            bool hovered, held;
            bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_PressedOnDoubleClick);

            // Render button background
            const ImU32 col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_ButtonActive :
                hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
            ImGui::RenderNavHighlight(bb, id);
            ImGui::RenderFrame(bb.Min, bb.Max, col, true, ImGui::GetStyle().FrameRounding);

            // Calculate available text area with proper padding
            ImVec2 padding = ImGui::GetStyle().FramePadding;
            float text_width = size.x - (padding.x * 2); // Reduce width by padding on both sides

            // Set wrapping position relative to button start
            ImGui::PushTextWrapPos(pos.x + text_width);

            // Calculate text position with padding
            ImVec2 text_pos;
            text_pos.x = pos.x + padding.x;
            text_pos.y = pos.y + padding.y;

            if (pIcon)
            {
                ImGui::PushFont(xgpu::tools::imgui::getFont(2));
                ImGui::PushStyleColor(ImGuiCol_Text, Color); //IM_COL32(123, 107, 52, 255));
                ImGui::RenderTextWrapped(text_pos, pIcon, NULL, 1000);          // "\xEE\xA3\x95"
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }

            // Clip text to button bounds
            ImGui::PushClipRect(bb.Min, bb.Max, true);
            ImGui::RenderTextWrapped(text_pos, label, NULL, text_width);
            ImGui::PopClipRect();
            ImGui::PopTextWrapPos();

            return  pressed;
        }
        */

        static int WrappedButton2(xresource::instance_guid G, const char* label, const ImVec2& size, ImU32 Color, const char* pIcon, bool& held)
        {
            ImGuiContext& g = *ImGui::GetCurrentContext();
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            if (window->SkipItems)
                return false;

            ImGuiID id = static_cast<ImGuiID>(G.m_Value); // Unique ID based on instance GUID

            // Calculate button rectangle
            ImVec2 pos = window->DC.CursorPos;
            ImVec2 end_pos = { pos.x + size.x, pos.y + size.y };
            const ImRect bb(pos, end_pos);

            ImGui::ItemSize(bb);
            if (!ImGui::ItemAdd(bb, id))
                return false;

            bool hovered;
            int TypeOfPress = 0;

            // Use default behavior (remove PressedOnDoubleClick) or explicitly allow drag
            bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_MouseButtonLeft);

            if (hovered)
            {
                TypeOfPress = pressed ? 1 : 0;

                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    TypeOfPress = 2;
                }
            }

            // Render button background
            const ImU32 col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_ButtonActive :
                hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
            ImGui::RenderNavHighlight(bb, id);
            ImGui::RenderFrame(bb.Min, bb.Max, col, true, ImGui::GetStyle().FrameRounding);

            // Calculate available text area with proper padding
            ImVec2 padding = ImGui::GetStyle().FramePadding;
            float text_width = size.x - (padding.x * 2);

            // Set wrapping position relative to button start
            ImGui::PushTextWrapPos(pos.x + text_width);

            // Calculate text position with padding
            ImVec2 text_pos = { pos.x + padding.x, pos.y + padding.y };

            if (pIcon)
            {
                ImGui::PushFont(xgpu::tools::imgui::getFont(2));
                ImGui::PushStyleColor(ImGuiCol_Text, Color);
                ImGui::RenderTextWrapped(text_pos, pIcon, NULL, 1000);
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }

            // Clip text to button bounds
            ImGui::PushClipRect(bb.Min, bb.Max, true);
            ImGui::RenderTextWrapped(text_pos, label, NULL, text_width);
            ImGui::PopClipRect();
            ImGui::PopTextWrapPos();

            return TypeOfPress;
        }


        //=============================================================================

        void AssetWindowByType()
        {
            m_pAssetMgr->m_mLibraryDB.FindAsReadOnly(m_SelectedLibrary,
            [&](const e10::library_db& Lib)
            {
                Lib.m_InfoByTypeDataBase.FindAsReadOnly(m_SelectedType,
                [&](const std::unique_ptr<e10::library_db::info_db>& InfoDB)
                {
                    float old_font_size = ImGui::GetFont()->Scale;
                    ImGui::GetFont()->Scale *= 0.95f;
                    ImGui::PushFont(ImGui::GetFont());

                    ImVec2 button_sz(80, 80);
                    ImGuiStyle& style = ImGui::GetStyle();

                    int buttons_count = InfoDB->m_InfoDataBase.size();
                    float window_visible_x2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
                    int n = 0;
                    for (auto& I : InfoDB->m_InfoDataBase)
                    {
                        ImGui::PushID(n);

                        std::string TypeView;
                        std::string NameView;
                        if (auto e = m_pAssetMgr->m_AssetPluginsDB.m_mPluginsByTypeGUID.find(I.second.m_Info.m_Guid.m_Type); e == m_pAssetMgr->m_AssetPluginsDB.m_mPluginsByTypeGUID.end())
                        {
                            TypeView = std::format("{:X}", I.second.m_Info.m_Guid.m_Type.m_Value);
                        }
                        else
                        {
                            TypeView = m_pAssetMgr->m_AssetPluginsDB.m_lPlugins[e->second].m_TypeName;
                        }

                        if (I.second.m_Info.m_Name.empty())
                        {
                            NameView = std::format("{:X}", I.second.m_Info.m_Guid.m_Instance.m_Value);
                        }
                        else
                        {
                            NameView = std::format("{}\n{:X}", I.second.m_Info.m_Name, I.second.m_Info.m_Guid.m_Instance.m_Value);
                        }


                        if (WrappedButton(std::format("{}\n{}", NameView, TypeView).c_str(), button_sz))
                        {
                            //if (ImGui::IsMouseDoubleClicked(0) && ImGui::IsItemHovered())
                            {
                                m_SelectedAsset = I.second.m_Info.m_Guid;
                                if (m_bAutoClose) m_bRenderBrowser = false;
                            }
                        }
                        float last_button_x2 = ImGui::GetItemRectMax().x;
                        float next_button_x2 = last_button_x2 + style.ItemSpacing.x + button_sz.x; // Expected position if next button was on same line
                        if (n + 1 < buttons_count && next_button_x2 < window_visible_x2)
                            ImGui::SameLine();
                        ImGui::PopID();
                        n++;
                    }

                    ImGui::GetFont()->Scale = old_font_size;
                    ImGui::PopFont();
                });
            });

            /*
            for (auto& I : pSelected->m_InfoDataBase)
            {
                if (ImGui::Selectable(std::format("{} - {:X}", I.m_Info.m_Name.c_str(), I.m_Info.m_Guid).c_str()))
                {
                    Compiler->SetupDescriptor(I.m_Info.m_TypeGuid, I.m_Info.m_Guid);
                    Inspectors[0].clear();
                    Inspectors[0].AppendEntity();
                    Inspectors[0].AppendEntityComponent(*xproperty::getObject(*Compiler->m_pInfo), Compiler->m_pInfo.get());
                    Inspectors[0].AppendEntityComponent(*Compiler->m_pDescriptor->getProperties(), Compiler->m_pDescriptor.get());
                    m_bRenderBrowser = false;
                }
            }
            */
        }

        //=============================================================================

        void AssetWindowByFolder()
        {
            struct temp_node
            {
                std::string             m_ResourceName;
                std::string_view        m_TypeNameView;
                xresource::full_guid    m_ResourceGUID;
                bool                    m_bHasChildren;
            };

            bool ClearSelection = false;
            bool ActionHappen   = false;

           // ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.345f, 0.345f, 0.345f, 1.00f));
            m_pAssetMgr->m_mLibraryDB.FindAsReadOnly(m_SelectedLibrary
            , [&](const e10::library_db& Lib)
            {
                    Lib.m_InfoByTypeDataBase.FindAsReadOnly(m_ParentGUID.m_Type
                ,[&]( const std::unique_ptr<library_db::info_db>& Entry )
                {
                    Entry->m_InfoDataBase.FindAsReadOnly(m_ParentGUID.m_Instance
                    , [&](const e10::library_db::info_node& Node)
                    {
                        std::vector<temp_node> TempNodes;

                        //
                        // Collect all the nodes into our vector
                        //
                        for (auto& E : Node.m_lChildLinks)
                        {
                            //
                            // Allow to filter by type
                            // 
                            bool bFilter = false;
                            for( auto& I : m_FilterByType )
                            {
                                if (I == E.m_Type)
                                {
                                    bFilter = true;
                                    break;
                                }
                            }
                            if (bFilter) continue;

                            //
                            // Otherwise , add the node
                            //
                            temp_node Temp;

                            Temp.m_ResourceGUID = E;

                            if (auto e = m_pAssetMgr->m_AssetPluginsDB.m_mPluginsByTypeGUID.find(E.m_Type); e == m_pAssetMgr->m_AssetPluginsDB.m_mPluginsByTypeGUID.end())
                            {
                                Temp.m_TypeNameView ="<unknown>";
                            }
                            else
                            {
                                Temp.m_TypeNameView = m_pAssetMgr->m_AssetPluginsDB.m_lPlugins[e->second].m_TypeName;
                            }

                            // Get the name
                            Lib.m_InfoByTypeDataBase.FindAsReadOnly(E.m_Type
                            , [&](const std::unique_ptr<library_db::info_db>& Entry)
                            {
                                Entry->m_InfoDataBase.FindAsReadOnly(E.m_Instance
                                , [&](const e10::library_db::info_node& Node)
                                {
                                    Temp.m_ResourceName = Node.m_Info.m_Name;
                                    Temp.m_bHasChildren = !Node.m_lChildLinks.empty();
                                });
                            });

                            TempNodes.emplace_back(std::move(Temp));
                        }

                        // Comparison function to sort by the string member
                        switch (m_ShortBasedOn)
                        {
                        case sort_base_on::NAME_ASCENDING:
                            std::sort(TempNodes.begin(), TempNodes.end()
                            , [](const temp_node& a, const temp_node& b)
                            {
                                if (a.m_ResourceGUID.m_Type == e10::folder_type_guid_v && b.m_ResourceGUID.m_Type != e10::folder_type_guid_v) return true;
                                if (a.m_ResourceGUID.m_Type != e10::folder_type_guid_v && b.m_ResourceGUID.m_Type == e10::folder_type_guid_v) return false;

                                if (a.m_ResourceName.empty() || b.m_ResourceName.empty()) return a.m_ResourceGUID.m_Instance.m_Value < b.m_ResourceGUID.m_Instance.m_Value;
                                return a.m_ResourceName < b.m_ResourceName;
                            });
                            break;
                        case sort_base_on::NAME_DESCENDING:
                            std::sort(TempNodes.begin(), TempNodes.end()
                            , [](const temp_node& a, const temp_node& b)
                            {
                                if (a.m_ResourceGUID.m_Type == e10::folder_type_guid_v && b.m_ResourceGUID.m_Type != e10::folder_type_guid_v) return true;
                                if (a.m_ResourceGUID.m_Type != e10::folder_type_guid_v && b.m_ResourceGUID.m_Type == e10::folder_type_guid_v) return false;

                                if (a.m_ResourceName.empty() || b.m_ResourceName.empty()) return a.m_ResourceGUID.m_Instance.m_Value > b.m_ResourceGUID.m_Instance.m_Value;
                                return a.m_ResourceName > b.m_ResourceName;
                            });
                            break;
                        case sort_base_on::TYPE_ASCENDING:
                            std::sort(TempNodes.begin(), TempNodes.end()
                            , [](const temp_node& a, const temp_node& b)
                            {
                                if (a.m_ResourceGUID.m_Type == e10::folder_type_guid_v && b.m_ResourceGUID.m_Type != e10::folder_type_guid_v) return true;
                                if (a.m_ResourceGUID.m_Type != e10::folder_type_guid_v && b.m_ResourceGUID.m_Type == e10::folder_type_guid_v) return false;

                                if (a.m_TypeNameView.empty() || b.m_TypeNameView.empty()) return a.m_ResourceGUID.m_Type.m_Value < b.m_ResourceGUID.m_Type.m_Value;
                                return a.m_TypeNameView < b.m_TypeNameView;
                            });
                            break;
                        case sort_base_on::TYPE_DESCENDING:
                            std::sort(TempNodes.begin(), TempNodes.end()
                            , [](const temp_node& a, const temp_node& b)
                            {
                                if (a.m_ResourceGUID.m_Type == e10::folder_type_guid_v && b.m_ResourceGUID.m_Type != e10::folder_type_guid_v) return true;
                                if (a.m_ResourceGUID.m_Type != e10::folder_type_guid_v && b.m_ResourceGUID.m_Type == e10::folder_type_guid_v) return false;

                                if (a.m_TypeNameView.empty() || b.m_TypeNameView.empty()) return a.m_ResourceGUID.m_Type.m_Value > b.m_ResourceGUID.m_Type.m_Value;
                                return a.m_TypeNameView > b.m_TypeNameView;
                            });
                            break;
                        }

                        float old_font_size = ImGui::GetFont()->Scale;
                        ImGui::GetFont()->Scale *= 0.95f;
                        ImGui::PushFont(ImGui::GetFont());

                        ImVec2 button_sz(80, 80+15);
                        ImGuiStyle& style = ImGui::GetStyle();


                        int buttons_count = (int)Node.m_lChildLinks.size();
                        float window_visible_x2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
                        int n = 0;


                        auto ExtraSpacing = ImGui::GetWindowContentRegionMax().x / (style.ItemSpacing.x + button_sz.x);
                        auto HowManyCanFit = int(ExtraSpacing);
                        auto StyleSpacing = style.ItemSpacing.x;
                        float StartOffset;
                        if (HowManyCanFit > 0 && HowManyCanFit < Node.m_lChildLinks.size() )
                        {

                            ExtraSpacing = (ExtraSpacing - HowManyCanFit) * (style.ItemSpacing.x + button_sz.x);
                            ExtraSpacing = ExtraSpacing / HowManyCanFit;

                            auto ButtonGrowth = ExtraSpacing / 2.5f;
                            StyleSpacing += ExtraSpacing - ButtonGrowth;

                            if (HowManyCanFit > 1 )
                            {
                                button_sz.x += ButtonGrowth;
                                button_sz.y += ButtonGrowth;
                            }

                            StartOffset = ExtraSpacing / 2;
                        }
                        else
                        {
                            ExtraSpacing = 0;
                            StartOffset  = 0;
                        }
                        bool bNewLine = true;
                        ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);

                        for( auto& E : TempNodes )
                        {
                            ImGui::PushID(n);
                            ImU32 Color = ImGui::ColorConvertFloat4ToU32(textColor);


                            const char* pIcon=nullptr;

                            if (E.m_ResourceGUID.m_Type == e10::folder_type_guid_v)
                            {
                                
                                pIcon = E.m_bHasChildren ? "\xEE\xA3\x95" : "\xEE\xA2\xB7";
                                Color = IM_COL32(123, 107, 52, 255);
                                // ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.345f*1.4f, 0.3f * 1.4f, 0.145f * 1.4f, 1.00f));
                            }
                            else
                            {
                                pIcon = "\xEE\xAE\x9F";
                            }
                            if (bNewLine)ImGui::SetCursorPosX(StartOffset);

                            std::string StringOne = E.m_ResourceName.empty() ? std::string_view("<Unknown>").data() : std::string_view(E.m_ResourceName).data();

                            
                            bool held=false;

                            int ThisSelected = -1;
                            for ( auto& S : m_SelectedItems )
                            {
                                if (S == E.m_ResourceGUID)
                                {
                                    ThisSelected = static_cast<int>(&S - m_SelectedItems.data());
                                    break;
                                }
                            }

                            if (ThisSelected == -1) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

                            int PressType = 0;
                            if (int PressType = WrappedButton2(E.m_ResourceGUID.m_Instance, std::format("\n\n\n\n\n\n{}", StringOne ).c_str(), button_sz, Color, pIcon, held); PressType == 2)
                            {
                                //if (ImGui::IsMouseDoubleClicked(0) && ImGui::IsItemHovered())
                                if (E.m_ResourceGUID.m_Type == e10::folder_type_guid_v )
                                {
                                    if (m_ParentGUID.empty() == false)
                                    {
                                        m_IsTreeNodeOpen[m_ParentGUID.m_Instance] = true;
                                    }

                                    m_ParentGUID = E.m_ResourceGUID;
                                }
                                else
                                {
                                    m_SelectedAsset = E.m_ResourceGUID;
                                    if (m_bAutoClose) m_bRenderBrowser = false;
                                }
                            }
                            else if (PressType == 1)
                            {
                                if (held)
                                {
                                    int a = 0;
                                }
                                else
                                {
                                    if ( m_SelectedItems.size() == 0 || ImGui::GetIO().KeyCtrl )
                                    {
                                        if (ThisSelected == -1) m_SelectedItems.push_back(E.m_ResourceGUID);
                                        else                    m_SelectedItems.erase(m_SelectedItems.begin()+ThisSelected);
                                        ClearSelection = false;
                                        ActionHappen  = true;
                                    }
                                    else
                                    {
                                        if (ImGui::GetIO().KeyShift)
                                        {
                                            size_t index1 = static_cast<std::size_t>(&E - TempNodes.data());
                                            size_t index2 = 0;
                                            for ( auto& S : TempNodes )
                                            {
                                                if (m_SelectedItems[0] == S.m_ResourceGUID )
                                                {
                                                    break;
                                                }
                                                ++index2;
                                            }

                                            if (index1>index2)
                                            {
                                                std::swap(index1, index2);
                                                ++index1;
                                                ++index2;
                                            }

                                            for( ; index1 != index2; ++index1)
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
                            if (held && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
                            {
                                ClearSelection = false;
                                ActionHappen   = true;

                                //ImGui::GetCurrentContext()->LastItemData.ID = static_cast<ImGuiID>(E.m_ResourceGUID.m_Instance.m_Value);
                                ImGui::PushID(static_cast<int>(E.m_ResourceGUID.m_Instance.m_Value));
                                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                                {
                                    drag_and_drop_folder_payload_t Payload = { m_ParentGUID, E.m_ResourceGUID,  m_SelectedItems.size() > 1 };

                                    ImGui::SetDragDropPayload("DESCRIPTOR_GUID", &Payload, sizeof(Payload));
                                    if (m_SelectedItems.size()>1) 
                                    {
                                        WrappedButton2(E.m_ResourceGUID.m_Instance, "\n\n\n\n\n\nActive Selection", button_sz, Color, "\xEE\x98\x9F", held);
                                    }
                                    else
                                    {
                                        WrappedButton2(E.m_ResourceGUID.m_Instance, std::format("\n\n\n\n\n\n{}", StringOne).c_str(), button_sz, Color, pIcon, held);
                                    }
                                    ImGui::EndDragDropSource();
                                    m_DraggedDescriptorItem = E.m_ResourceGUID;
                                }
                                ImGui::PopID();
                            }
                            else if (PressType == 0 && held == false)
                            {
                                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) == false ) ClearSelection = false;
                                else
                                {
                                   if (ActionHappen==false) ClearSelection = true;
                                }
                            }


                            // Drag target
                            if (E.m_ResourceGUID.m_Type == e10::folder_type_guid_v)
                            {
                                if (ImGui::BeginDragDropTarget())
                                {
                                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DESCRIPTOR_GUID") )
                                    {
                                        IM_ASSERT(payload->DataSize == sizeof(drag_and_drop_folder_payload_t));
                                        drag_and_drop_folder_payload_t payload_n = *(const drag_and_drop_folder_payload_t*)payload->Data;
                                        m_DraggedDescriptorItem={};

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

                                            if (bValidationFailed==false)
                                            {
                                                for (auto& S : m_SelectedItems)
                                                {
                                                    if (auto Err = m_pAssetMgr->MoveDescriptor(m_SelectedLibrary, S, payload_n.m_Parent, E.m_ResourceGUID); Err)
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
                                            if (auto Err = m_pAssetMgr->MoveDescriptor(m_SelectedLibrary, payload_n.m_Source, payload_n.m_Parent, E.m_ResourceGUID); Err)
                                            {
                                                // Handle the error
                                                int a = 0;
                                            }
                                        }
                                    }
                                    ImGui::EndDragDropTarget();
                                }
                            }



                            if (ImGui::IsItemHovered())
                            {
                                ImGui::BeginTooltip();
                                std::string StringTwo = (E.m_TypeNameView.empty() ? std::string_view("<Unknown>").data() : E.m_TypeNameView.data());
                                std::string Text = std::format("Instance Name : {}\n"
                                                               "Type Name     : {}\n"
                                                               "Instance GUID : {:X}\n"
                                                               "Type GUID     : {:X}"
                                                               , StringOne
                                                               , StringTwo
                                                               , E.m_ResourceGUID.m_Instance.m_Value
                                                               , E.m_ResourceGUID.m_Type.m_Value
                                                               );

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
                    });
                });
            });

            if (ClearSelection)
            {
                m_SelectedAsset = {};
                m_SelectedItems.clear();
            }

            //ImGui::PopStyleColor(1);
        }

        //=============================================================================

        /*
        void RenderPath()
        {
            for (auto& E : m_PathNodes)
            {
                if (ImGui::Selectable(E.m_Name.c_str(), false, ImGuiSelectableFlags_DontClosePopups))
                {
                    m_ParentGUID = E.m_Guid;
                }
                ImGui::SameLine();
            }

            static char buffer[1024] = "";
            ImGui::PushItemWidth(-FLT_MIN);
            if (ImGui::InputText("##Path", buffer, sizeof(buffer))) {
                // This block runs when the string changes (e.g., on Enter or focus loss)
                // You can process buffer here if needed
            }
            ImGui::PopItemWidth();
        }
        */
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
            draw_list->AddRectFilled(
                start_pos,
                ImVec2(start_pos.x + ImGui::GetContentRegionAvail().x, start_pos.y + line_height),
                button_bg_color_u32
            );

            // Push style modifications to remove button appearance
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);           // Remove border
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, style.FramePadding.y));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));        // Transparent background
            // Note: We don't override ImGuiCol_Text, so buttons use the existing text color

            for (auto& E : m_PathNodes)
            {
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));
                if ( &E == &m_PathNodes.back()) ImGui::PushFont(xgpu::tools::imgui::getFont(1));
                if (ImGui::Button(E.m_Name.c_str()))
                {
                    m_ParentGUID = E.m_Guid;
                }
                if (&E == &m_PathNodes.back()) ImGui::PopFont();
                ImGui::PopStyleColor(1);
                ImGui::SameLine(0, 0);

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, style.FramePadding.y));
                if (ImGui::Button(">"))
                {
                    m_ParentGUID = E.m_Guid;
                }
                ImGui::PopStyleVar(1);
                ImGui::SameLine(0,0);
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

        void MainWindow()
        {
            ImGui::SetNextWindowBgAlpha(0.9f);
            ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Resources", nullptr, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse))
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));        // Transparent background

                // Get available space
                float total_width = ImGui::GetContentRegionAvail().x;
                float total_height = ImGui::GetContentRegionAvail().y - 40.0f; // Subtract space for button
                static float size1 = total_width * 0.2f;
                static float size2 = total_width * 0.8f;
                static float ButtonWidth = 4.0f;
                Splitter(true, ButtonWidth, &size1, &size2, 100.0f, 100.0f, total_width, total_height);

                // Left panel
                bool SelectedItemFound = false;

                auto a = ImGui::GetCursorScreenPos();

                ImGui::BeginGroup();
                RenderSearchBar(ImVec2(size1 - ButtonWidth, total_height));

                ImGui::BeginChild("LeftWindow", ImVec2(size1 - ButtonWidth, total_height - (ImGui::GetCursorScreenPos().y - a.y)));

                ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.245f, 0.245f, 0.245f, 0.8f));
                ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_None;
                if (ImGui::BeginTabBar("FolderOrganization", tab_bar_flags))
                {
                    if (ImGui::BeginTabItem("\xEE\xA3\xAF Descriptors"))
                    {
                        if (ImGui::BeginChild("FrameWindow", ImVec2{}))
                        {
                            m_TabType = tab_type::DESCRIPTORS;
                            FolderWindow();
                        }

                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("\xEE\x9C\xA1 Search"))
                    {
                        if (ImGui::BeginChild("FrameWindow", ImVec2{}))
                        {
                            m_TabType = tab_type::DESC_BY_TYPE;
                            ByTypeFolderWindow();
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("\xEE\x9C\x93 Compilation"))
                    {
                        if (ImGui::BeginChild("FrameWindow", ImVec2{}))
                        {
                            m_TabType = (tab_type)-1;
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("\xEE\x9C\xB4 Favorites"))
                    {
                        if (ImGui::BeginChild("FrameWindow", ImVec2{}))
                        {
                            m_TabType = tab_type::DESC_BY_TYPE;
                            m_TabType = (tab_type)-1;
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("\xEE\xA2\xA5 Assets"))
                    {
                        if (ImGui::BeginChild("FrameWindow", ImVec2{}))
                        {
                            m_TabType = tab_type::ASSETS;
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("\xEE\x9F\x85 Plugins"))
                    {
                        if (ImGui::BeginChild("FrameWindow", ImVec2{}))
                        {
                            m_TabType = (tab_type)-1;
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }


                    ImGui::EndTabBar();
                }
                ImGui::PopStyleColor(2);

                ImGui::EndChild();
                ImGui::EndGroup();

                ImGui::SameLine();

                a.x = ImGui::GetCursorScreenPos().x + ButtonWidth;
                ImGui::SetCursorScreenPos(a);

                // Right panel
                ImGui::BeginGroup();
                // ImGui::PushFont(xgpu::tools::imgui::getFont(1));

                if (ScaleButton("\xee\xa5\x88", 1.2f))
                {
                    ImGui::OpenPopup("Add Resource");
                }

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));        // Transparent background
                ImGui::SameLine(0, 3.5f);

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

                if (m_TabType == tab_type::DESCRIPTORS)
                {
                    ImGui::SameLine();
                    //ImGui::PushFont(xgpu::tools::imgui::getFont(1));

                    if (ScaleButton("\xEE\x9C\xAB" , 1.0f))   // "\xef\x82\x8d" //\xef\x82\x8d  \\\xee\xa4\xb8
                    {

                    }

                    ImGui::SameLine(0, 1.5f);
                    if (ScaleButton("\xEE\x9C\xAA", 1.0f)) //"\xef\x82\x8" //\xef\x82\x8f   \xee\xa4\xb7
                    {

                    }
                    //ImGui::PopFont();

                    ImGui::SameLine();
                    ImGui::PopStyleColor(1);

                    RenderPath();
                }
                else
                {
                    ImGui::PopStyleColor(1);
                }

                if (ImGui::BeginPopup("Filter Resource"))
                {
                    if (ImGui::BeginMenu("\xEE\xA3\x8B Sort By"))
                    {
                        if (ImGui::MenuItem( "\xEF\x82\xAD Name (a->Z)", nullptr, m_ShortBasedOn == sort_base_on::NAME_ASCENDING))
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

                        if (ImGui::MenuItem("\xEF\x82\xAE Name (Z->a)", nullptr, m_ShortBasedOn == sort_base_on::TYPE_DESCENDING))
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
                            for (auto& E : m_pAssetMgr->m_AssetPluginsDB.m_lPlugins)
                            {
                                m_FilterByType.emplace_back(E.m_TypeGUID);
                            }
                        }

                        if (ImGui::MenuItem("\xee\x9c\x9c View All Types"))
                        {
                            m_FilterByType.clear();
                        }

                        ImGui::SeparatorText("Type by Type");

                        for (auto& E : m_pAssetMgr->m_AssetPluginsDB.m_lPlugins)
                        {
                            bool Selected = [&]
                            {
                                for ( auto& T : m_FilterByType)
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

                    ImGui::EndPopup(); // Close the popup scope
                }


                if (ImGui::BeginPopup("Add Resource"))
                {
                    AddResourcePopUp();
                    ImGui::EndPopup(); // Close the popup scope
                }


                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.145f, 0.145f, 0.145f, 0.80f)); 
                if (ImGui::BeginChild("Panel", ImVec2(-1, total_height - (ImGui::GetCursorScreenPos().y - a.y))))
                {
                    switch (m_TabType)
                    {
                    case tab_type::DESCRIPTORS:
                        AssetWindowByFolder();
                        break;
                    case tab_type::DESC_BY_TYPE:
                        if (m_SelectedType.empty() == false)
                            AssetWindowByType();
                        break;
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::EndGroup();

                if (m_bAutoClose)
                {
                    ImGui::Separator();
                    if (ScaleButton(" Close ", 1.5f))
                    {
                        m_bRenderBrowser = false;
                    }
                }

                ImGui::End();
            }
        }

        bool                    m_bAutoClose            = true;
        bool                    m_bRenderBrowser        = false;
        tab_type                m_TabType               = tab_type::DESCRIPTORS;
        xresource::full_guid    m_SelectedLibrary       = {};
        xresource::type_guid    m_SelectedType          = {};
        xresource::full_guid    m_ParentGUID            = {};
        e10::library_mgr*       m_pAssetMgr             = nullptr;
        xresource::full_guid    m_LastGeneratedAsset    = {};
        xresource::full_guid    m_SelectedAsset         = {};
        std::vector<path_node>  m_PathNodes             = {};
        std::unordered_map<xresource::instance_guid, bool> m_IsTreeNodeOpen;
        sort_base_on            m_ShortBasedOn          = sort_base_on::NAME_ASCENDING;
        std::vector<xresource::type_guid> m_FilterByType;
        std::vector<xresource::full_guid> m_SelectedItems;
    };

} // namespace e10
#endif