namespace e10
{
    struct search_tab : e10::asset_browser_tab_base
    {
        search_tab(assert_browser& Browser, const char* pName)
            : asset_browser_tab_base{ Browser, pName }
            , m_AssetMgr{ *Browser.getAssetMgr() }
        {
        }

        //=============================================================================

        static bool WrappedButton(const char* label, const ImVec2& size = ImVec2(0, 0))
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

        //=============================================================================

        void LeftPanel() noexcept override
        {
            bool SelectedItemFound = false;
            for (auto& L : m_AssetMgr.m_mLibraryDB)
            {
                auto Path = strXstr(L.second.m_Library.m_Path);
                if (ImGui::TreeNodeEx(Path.substr(Path.rfind('\\') + 1).c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth))
                {
                    for (auto& T : L.second.m_InfoByTypeDataBase)
                    {
                        std::string TypeView;
                        if (auto e = m_AssetMgr.m_AssetPluginsDB.m_mPluginsByTypeGUID.find(T.second->m_TypeGUID); e == m_AssetMgr.m_AssetPluginsDB.m_mPluginsByTypeGUID.end())
                        {
                            TypeView = std::format("{:X}", T.second->m_TypeGUID.m_Value);
                        }
                        else
                        {
                            TypeView = std::format("{} - {:X}", m_AssetMgr.m_AssetPluginsDB.m_lPlugins[e->second].m_TypeName, T.second->m_TypeGUID.m_Value);
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

        void RightPanel() noexcept override
        {
            m_AssetMgr.m_mLibraryDB.FindAsReadOnly(m_SelectedLibrary,
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
                                if (auto e = m_AssetMgr.m_AssetPluginsDB.m_mPluginsByTypeGUID.find(I.second.m_Info.m_Guid.m_Type); e == m_AssetMgr.m_AssetPluginsDB.m_mPluginsByTypeGUID.end())
                                {
                                    TypeView = std::format("{:X}", I.second.m_Info.m_Guid.m_Type.m_Value);
                                }
                                else
                                {
                                    TypeView = m_AssetMgr.m_AssetPluginsDB.m_lPlugins[e->second].m_TypeName;
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
                                    m_Browser.setSelection(I.second.m_Info.m_Guid, {});
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


        e10::library_mgr&                                   m_AssetMgr;
        xresource::full_guid                                m_SelectedLibrary   = {};
        xresource::type_guid                                m_SelectedType      = {};
        xresource::full_guid                                m_ParentGUID        = {};

    };

    namespace
    {
        inline browser_registration<search_tab, "\xEE\x9C\xA1 Search", 1.0f> g_SearchTab{};
    }
}