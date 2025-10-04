#ifndef _E10_ASSETBROWSER_H
#define _E10_ASSETBROWSER_H
#pragma once
#include "source/Tools/xgpu_imgui_breach.h"
#include "E10_AssetMgr.h"

namespace e10
{
    struct assert_browser;
    struct asset_browser_tab_base;

    struct browser_registration_base
    {
        virtual std::unique_ptr<asset_browser_tab_base> CreateInstance(assert_browser& Browser) noexcept = 0;
        browser_registration_base(float Sortkey ) noexcept : m_SortKey{ Sortkey }
        {
            //
            // Insert Sort
            //
            auto p = &g_pHead;
            while (*p && (*p)->m_SortKey < Sortkey)
                p = &(*p)->m_pNext;

            m_pNext = *p;
            *p      = this;
        }

        float                                    m_SortKey;
        browser_registration_base*               m_pNext    = nullptr;
        inline constinit static browser_registration_base* g_pHead    = nullptr;
    };

    template< typename T, xproperty::details::fixed_string TabName, float T_SORT_KEY >
    struct browser_registration : browser_registration_base
    {
        browser_registration() noexcept : browser_registration_base{ T_SORT_KEY } {}
        std::unique_ptr<asset_browser_tab_base> CreateInstance(assert_browser& Browser) noexcept override
        {
            return std::make_unique<T>(Browser, TabName.m_Value);
        }
    };

    // asset browser tab base
    struct asset_browser_tab_base
    {
        virtual void LeftPanel()    = 0;
        virtual void RightPanel()   = 0;

        asset_browser_tab_base( assert_browser& Browser, const char* pName ) : m_Browser{ Browser }, m_pName(pName){}

        assert_browser&     m_Browser;
        const char*         m_pName;
    };

    //=============================================================================
    //=============================================================================

    struct assert_browser
    {
        const void* getCurrentID(void)
        {
            return m_pPopupUID;
        }

        //=============================================================================

        void ShowAsPopup( const void* pUID, const char* pWindowName )
        {
            assert(m_pPopupUID == nullptr);

            m_bRenderBrowser = true;
            m_pPopupUID      = pUID;
            m_pWindowName    = pWindowName;
        }

        void Show( bool bShow = true )
        {
            m_bRenderBrowser = bShow;
        }

        //=============================================================================

        bool isVisible() const
        {
            return m_bRenderBrowser;
        }

        //=============================================================================

        void RenderAsPopup(e10::library_mgr& AssetMgr, xresource::mgr& ResourceMgr)
        {
            if (m_bRenderBrowser == false) return;
            bool UsedtoBeVisible = m_bRenderBrowser;
            Render(AssetMgr, ResourceMgr);
            if (not isVisible() && UsedtoBeVisible)
            {
                // The user must have cancel this thing... 
                if (m_SelectedAsset.empty()) m_pPopupUID = nullptr;
            }
        }

        //=============================================================================

        void Render( e10::library_mgr& AssetMgr, xresource::mgr& ResourceMgr )
        {
            if (m_bRenderBrowser == false) return;

            if (m_pAssetMgr == nullptr)
            {
                // Set the asset manager
                m_pAssetMgr     = &AssetMgr;
                m_pResourceMgr  = &ResourceMgr;

                // Register all the tabs
                for (auto p = browser_registration_base::g_pHead; p; p = p->m_pNext)
                {
                    m_Tabs.push_back(p->CreateInstance(*this));
                }
            }

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
            m_pPopupUID = nullptr;
            return GUID;
        }

        //=============================================================================

        library::guid getSelectedLibrary()
        {
            if (m_SelectedLibrary.empty()) return m_SelectedLibrary;
            auto GUID = m_SelectedLibrary;
            m_SelectedLibrary.clear();
            return GUID;
        }

        //=============================================================================

        auto getAssetMgr() noexcept
        {
            return m_pAssetMgr;
        }

        //=============================================================================

        bool isAutoClose() const noexcept
        {
            return m_bAutoClose;
        }

        //=============================================================================

        void setSelection(library::guid Library, xresource::full_guid LastSelect, xresource::full_guid NewResource )
        {
            m_SelectedLibrary    = Library;
            m_SelectedAsset      = LastSelect;
            m_LastGeneratedAsset = NewResource;
            if (isAutoClose()) Show(false);
        }

    protected:

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

        void RenderSearchBar(ImVec2 Size)
        {
            std::array<char,256> searchBuffer{0}; // Buffer for search text

            strcpy_s( searchBuffer.data(), searchBuffer.size(), m_SearchString.c_str());

            auto x = ImGui::GetCursorPosX();

            if (ImGui::Button("\xe2\x96\xbc"))
            {
                m_SearchString.clear();
                searchBuffer[0]=0;
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
            const bool NewContent = ImGui::InputText("##search", searchBuffer.data(), searchBuffer.size());

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

            // Copy back the string
            m_SearchString = std::string_view(searchBuffer.data());
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

        void MainWindow()
        {
            ImGui::SetNextWindowBgAlpha(0.9f);
            ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
            
            if (ImGui::Begin(m_pWindowName, nullptr, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse))
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
                asset_browser_tab_base* pTabSelected = nullptr;
                if (ImGui::BeginTabBar("FolderOrganization", tab_bar_flags))
                {
                    for( auto& pE : m_Tabs )
                    {
                        if (ImGui::BeginTabItem(pE->m_pName))
                        {
                            if (ImGui::BeginChild("FrameWindow", ImVec2{}))
                            {
                                pE->LeftPanel();
                                pTabSelected = pE.get();
                            }

                            ImGui::EndChild();
                            ImGui::EndTabItem();
                        }
                    }

                    /*
                    if (ImGui::BeginTabItem("\xEE\x9C\x93 Compilation"))
                    {
                        if (ImGui::BeginChild("FrameWindow", ImVec2{}))
                        {
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                    */

                    if (ImGui::BeginTabItem("\xEE\x9C\xB4 Favorites"))
                    {
                        if (ImGui::BeginChild("FrameWindow", ImVec2{}))
                        {
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("\xEE\xA2\xA5 Assets"))
                    {
                        if (ImGui::BeginChild("FrameWindow", ImVec2{}))
                        {
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("\xEE\x9F\x85 Plugins"))
                    {
                        if (ImGui::BeginChild("FrameWindow", ImVec2{}))
                        {
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

                //
                // Right panel
                //
                ImGui::BeginGroup();
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.145f, 0.145f, 0.145f, 0.80f)); 
                if (ImGui::BeginChild("Panel", ImVec2(-1, total_height - (ImGui::GetCursorScreenPos().y - a.y))))
                {
                    if (pTabSelected)
                    {
                        pTabSelected->RightPanel();
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::EndGroup();

                //
                // Close button
                //
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

        using tab_list = std::vector<std::unique_ptr<asset_browser_tab_base>>;

        e10::library_mgr*       m_pAssetMgr             = nullptr;
        xresource::mgr*         m_pResourceMgr          = nullptr;
        xresource::full_guid    m_LastGeneratedAsset    = {};
        xresource::full_guid    m_SelectedAsset         = {};
        library::guid           m_SelectedLibrary       = {};
        bool                    m_bAutoClose            = true;
        bool                    m_bRenderBrowser        = false;
        tab_list                m_Tabs                  = {};
        const void*             m_pPopupUID             = {};
        const char*             m_pWindowName           = "Resource Browser";

    public:
        std::string             m_SearchString          = {};
    };

} // namespace e10
#endif