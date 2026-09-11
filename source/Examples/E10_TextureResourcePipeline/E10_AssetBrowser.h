#ifndef _E10_ASSETBROWSER_H
#define _E10_ASSETBROWSER_H
#pragma once
#include "source/Tools/xgpu_imgui_breach.h"
#include "source/Tools/xgpu_xcore_bitmap_helpers.h"
#include "E10_AssetMgr.h"

namespace e10
{
    struct assert_browser;
    struct asset_browser_tab_base;

    //------------------------------------------------------------------------------------------------
    // ImGui's own tooltip auto-placement (FindBestWindowPosForPopup) tries to avoid the viewport edges
    // using the tooltip's PREVIOUS frame size, but has nowhere left to flip to once the mouse itself is
    // already right at an edge/corner - a wide/tall tooltip near the screen edge gets clipped past it,
    // unreadable (direct user report: "you forgot to flip the tooltip direction if it is too close to
    // the right edge of the screen... it will get cut off otherwise"). Anchoring the window's OWN pivot
    // corner to whichever side of the viewport the mouse is on makes it grow back TOWARD the center
    // instead of past the edge, regardless of content size.
    //
    // Same fix already exists as xproperty::inspector's own PlaceTooltipAwayFromEdges() (internal
    // linkage inside xPropertyImGuiInspector.cpp's anonymous namespace, not reachable from here) -
    // duplicated rather than exposed through a new public xproperty API, since it's this small and
    // self-contained. Call once, right before ImGui::BeginTooltip().
    //
    // A plain "which half of the viewport is the mouse in" split (the first version of this function)
    // picks a side without ever checking whether that side actually HAS enough room for the tooltip,
    // so it could still get clipped up to ~50% in the worst case (direct user report, after confirming
    // the flip existed but wasn't aggressive enough: "about 50% is being cut off by the screen worse
    // case... you should be careful either edge (left/right)").
    //
    // A second attempt checked real remaining space against ImGui::GetMainViewport()'s own
    // WorkPos/WorkSize - WORSE than the 50% split for the right edge specifically (direct user report,
    // with a screenshot showing the tooltip rendering entirely past the real screen edge despite a
    // "correctly" computed flip). Root cause, confirmed via a temporary diagnostic printf:
    // GetMainViewport()->WorkPos/WorkSize reflects THIS APP WINDOW's own bounds (e.g. a window sitting
    // at desktop x=1280..2552 on a multi-monitor desktop), NOT the real monitor/screen edge - and
    // xgpu_imgui_breach.cpp registers exactly ONE fake, giant (10000x10000) "monitor" for its own
    // unrelated reasons, so ImGui's own platform_io.Monitors list can't be used to find the real
    // screen bounds either. There is no ImGui-level source of truth for "where does the real screen
    // actually end" in this app - querying Win32 directly for the monitor under the cursor is the only
    // reliable option. (This file already uses raw Win32 elsewhere in this codebase for exactly this
    // reason - not a new precedent.)
    inline void PlaceTooltipAwayFromEdges() noexcept
    {
        constexpr ImVec2 AssumedSize(480.0f, 400.0f);

        const ImVec2 MouseF = ImGui::GetIO().MousePos;
        const POINT  Mouse{ static_cast<LONG>(MouseF.x), static_cast<LONG>(MouseF.y) };
        const HMONITOR hMonitor = ::MonitorFromPoint(Mouse, MONITOR_DEFAULTTONEAREST);
        MONITORINFO MonitorInfo{ sizeof(MONITORINFO) };
        ::GetMonitorInfo(hMonitor, &MonitorInfo);

        const float SpaceRight = static_cast<float>(MonitorInfo.rcWork.right)  - MouseF.x;
        const float SpaceLeft  = MouseF.x - static_cast<float>(MonitorInfo.rcWork.left);
        const float SpaceBelow = static_cast<float>(MonitorInfo.rcWork.bottom) - MouseF.y;
        const float SpaceAbove = MouseF.y - static_cast<float>(MonitorInfo.rcWork.top);

        const ImVec2 Pivot
        ( (SpaceRight < AssumedSize.x && SpaceLeft  > SpaceRight) ? 1.0f : 0.0f
        , (SpaceBelow < AssumedSize.y && SpaceAbove > SpaceBelow) ? 1.0f : 0.0f
        );
        // Small offset matching ImGui's own default tooltip placement, signed to lead AWAY from the
        // edge the pivot just chose (e.g. pivot 1.0 on the right edge subtracts, so the window still
        // clears the cursor instead of sitting under/on top of it).
        constexpr float Offset = 16.0f;
        ImGui::SetNextWindowPos(ImVec2(MouseF.x + (Pivot.x > 0.0f ? -Offset : Offset), MouseF.y + (Pivot.y > 0.0f ? -Offset : Offset)), ImGuiCond_Always, Pivot);
    }

    //------------------------------------------------------------------------------------------------
    // The GPU-upload half of the plugin icon atlas - deliberately NOT in E10_PluginMgr.h/
    // E10_PluginIconAtlas.h, which must stay headless (see asset_plugins_db::m_IconAtlasGPUHandle's
    // own comment). Lazily uploads Db.m_IconAtlasBitmap (built headlessly at OpenProject time) into
    // ONE shared xgpu::texture the first time ANY assert_browser instance calls this - g_LibMgr/
    // asset_plugins_db is a single process-wide global every example's own browser widget (and every
    // shared popup picker) points at, so this makes every one of them reuse the same upload rather
    // than each re-uploading its own copy. A no-op (fast pointer cast) once populated.
    inline xgpu::texture* EnsureIconAtlasTexture(asset_plugins_db& Db, xgpu::device& Device) noexcept
    {
        if (!Db.m_IconAtlasGPUHandle)
        {
            if (Db.m_IconAtlasBitmap.getWidth() == 0) return nullptr; // nothing built yet (no project open)

            auto pTexture = std::make_shared<xgpu::texture>();
            if (auto Err = xgpu::tools::bitmap::Create(*pTexture, Device, Db.m_IconAtlasBitmap); Err)
                return nullptr;

            Db.m_IconAtlasGPUHandle = std::move(pTexture); // shared_ptr<xgpu::texture> -> shared_ptr<void>
        }
        return static_cast<xgpu::texture*>(Db.m_IconAtlasGPUHandle.get());
    }

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

    // T_DOCKABLE_ONLY: this tab never appears in a POPUP picker's tab bar - only in DOCKABLE mode's
    // own independent-windows layout (E10_AssetBrowser.h's MainWindow()). Defaults to false so every
    // pre-existing registration (virtual_tree_tab, compiler_tab) needs zero changes to keep showing up
    // in both modes exactly as before; new DOCKABLE-only windows (Resource Plugin, Asset) pass true.
    // T_HAS_LEFT_PANEL: whether DOCKABLE mode should give this tab a left+right split (calling both
    // LeftPanel() and RightPanel()) or just fill the whole window with RightPanel() alone. Defaults to
    // true (virtual_tree_tab's existing shape); compiler_tab's LeftPanel() is already empty, so its own
    // registration passes false rather than reserving a permanently-blank left column in its own window.
    template< typename T, xproperty::details::fixed_string TabName, float T_SORT_KEY, bool T_DOCKABLE_ONLY = false, bool T_HAS_LEFT_PANEL = true >
    struct browser_registration : browser_registration_base
    {
        browser_registration() noexcept : browser_registration_base{ T_SORT_KEY } {}
        std::unique_ptr<asset_browser_tab_base> CreateInstance(assert_browser& Browser) noexcept override
        {
            auto p = std::make_unique<T>(Browser, TabName.m_Value);
            p->m_bDockableOnly = T_DOCKABLE_ONLY;
            p->m_bHasLeftPanel = T_HAS_LEFT_PANEL;
            return p;
        }
    };

    // The payload shape every folder/asset drag source in this browser uses ("DESCRIPTOR_GUID" -
    // moved here from virtual_tree_tab, which now just aliases it, so external consumers wanting to
    // accept a dropped asset - e.g. a scene tree instantiating a dropped Prefab - can decode it
    // without including that tab's own implementation header).
    struct drag_and_drop_folder_payload_t
    {
        e10::folder::guid           m_Parent;
        xresource::full_guid        m_Source;
        bool                        m_bSelection;
    };

    // Generic extension point for "drag something from outside the asset browser onto one of its
    // folders to create a brand-new asset there" (e.g. dragging a scene entity onto a folder to turn
    // it into a Prefab, Unity-style). The browser's folder drop targets don't need to know what's
    // being dragged or how to build the resulting asset - they just accept the named ImGui payload
    // and forward the raw bytes here. Self-registers via a linked list, same pattern as
    // browser_registration_base above, so a consumer (e.g. E29) only needs to instantiate one static
    // instance of a subclass - no change to this header or the browser's own code required per
    // consumer.
    struct external_drop_registration_base
    {
        // Called when a matching payload is dropped on folder ParentGUID (which belongs to
        // LibraryGUID). Return the newly created asset's guid so the browser can select it, or an
        // empty guid if the drop should be treated as a no-op (e.g. validation failed).
        virtual xresource::full_guid OnDrop(library_mgr& AssetMgr, library::guid LibraryGUID, xresource::full_guid ParentGUID, const void* pData, std::size_t Size) const noexcept = 0;

        external_drop_registration_base(const char* pPayloadName) noexcept : m_pPayloadName{ pPayloadName }
        {
            m_pNext = g_pHead;
            g_pHead = this;
        }

        const char*                                       m_pPayloadName;
        external_drop_registration_base*                  m_pNext = nullptr;
        inline constinit static external_drop_registration_base* g_pHead = nullptr;
    };

    // asset browser tab base
    struct asset_browser_tab_base
    {
        virtual void LeftPanel()    = 0;
        virtual void RightPanel()   = 0;

        asset_browser_tab_base( assert_browser& Browser, const char* pName ) : m_Browser{ Browser }, m_pName(pName){}

        assert_browser&     m_Browser;
        const char*         m_pName;

        // Set post-construction by browser_registration<>::CreateInstance from its own template
        // params - see that template's own comment for what each means.
        bool                m_bDockableOnly     = false;
        bool                m_bHasLeftPanel     = true;

        // DOCKABLE mode's own per-tab left-panel splitter width (independent of POPUP mode's single
        // shared assert_browser::m_SplitSize1 - each DOCKABLE window is now its own independent
        // ImGui window, so each needs its own remembered splitter position). Negative = uninitialized,
        // same convention as m_SplitSize1.
        float               m_DockableSplitSize = -1.0f;
    };

    //=============================================================================
    //=============================================================================

    struct assert_browser
    {
        // How this browser instance presents itself. POPUP (the default, matching every existing
        // call site) is a one-shot modal picker: undockable, auto-closes the moment a selection is
        // made, and shows a bottom "Close" button so the user can cancel out. DOCKABLE is a
        // persistent, always-open core tool window (e.g. "File > Asset Browser..."): it docks like
        // any other panel, never auto-closes, and has no Close affordance at all since it isn't
        // meant to be dismissed.
        enum class display_mode : std::uint8_t
        { POPUP
        , DOCKABLE
        };

        const void* getCurrentID(void)
        {
            return m_pPopupUID;
        }

        //=============================================================================

        void ShowAsPopup(e10::library_mgr& AssetMgr, const void* pUID, std::span<const xresource::type_guid> Types, xresource::type_guid AdditionalType )
        {
            assert(m_pPopupUID == nullptr);

            m_bRenderBrowser = true;
            m_pPopupUID      = pUID;

            //
            // Select all the filters
            //
            m_FilterByType.clear();
            for (auto& E : AssetMgr.m_AssetPluginsDB.m_lPlugins)
            {
                bool bFound = false;
                for ( auto& J : Types)
                {
                    if ( E.m_TypeGUID == J )
                    {
                        bFound = true;
                        break;
                    }
                }

                if (not bFound) 
                {
                    if (E.m_TypeGUID != AdditionalType)
                    {
                        // We always leave the folder in...
                        if ( E.m_TypeGUID != e10::folder::type_guid_v )
                        {
                            m_FilterByType.push_back(E.m_TypeGUID);
                        }
                    }
                }
            }

            //
            // Create the dialog name
            //
            std::string PopNameName{ "Select: {" };

            if (not AdditionalType.empty())
            {
                if (auto P = AssetMgr.m_AssetPluginsDB.find(AdditionalType); P)
                {
                    PopNameName = std::format("{} {}", PopNameName, P->m_TypeName);
                }
            }

            for (auto& E : Types)
            {
                if (auto P = AssetMgr.m_AssetPluginsDB.find(E); P)
                {
                    PopNameName = std::format( "{} {}", PopNameName, P->m_TypeName );
                }
            }

            PopNameName += " }###908312702";

            xstrtool::Copy( m_WindowName, PopNameName );
        }

        void ClosePopup()
        {
            m_pPopupUID = nullptr;
            Show(false);
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

        // Called once per example, right after that example's own local xgpu::device is created (the
        // same place/timing OpenProject used to require a Device for, before that got reverted back
        // to headless - see E10_AssetMgr.h's own comment). Not a constructor parameter: several
        // assert_browser instances in this codebase are static-duration globals (shared popup
        // pickers) constructed before main() runs, i.e. before any device exists anywhere - a setter
        // called later, once a device is actually live, is the only shape that works for those too.
        void SetDevice(xgpu::device& Device) noexcept { m_pDevice = &Device; }

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

            // Lazily upload the shared plugin-icon atlas texture (E10_PluginIconAtlas.h built the
            // CPU bitmap headlessly at OpenProject time; this is the GPU half, deliberately kept out
            // of the headless asset-mgr code - see EnsureIconAtlasTexture's own comment). A no-op
            // once any browser instance has already done this. Silently skipped if SetDevice was
            // never called - icons just don't render, same graceful-degradation as any other
            // missing-icon case, rather than a crash.
            if (m_pDevice) EnsureIconAtlasTexture(AssetMgr.m_AssetPluginsDB, *m_pDevice);

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
            return m_DisplayMode == display_mode::POPUP;
        }

        //=============================================================================

        void setDisplayMode(display_mode Mode) noexcept
        {
            m_DisplayMode = Mode;
        }

        //=============================================================================

        void setSelection(library::guid Library, xresource::full_guid LastSelect, xresource::full_guid NewResource )
        {
            m_SelectedLibrary    = Library;
            m_SelectedAsset      = LastSelect;
            m_LastGeneratedAsset = NewResource;
            if (isAutoClose()) Show(false);
        }

        // Public (not protected like the rest of this section below) - a small, self-contained,
        // static utility with no dependency on assert_browser's own state, reused by tab structs that
        // are NOT derived from assert_browser (e.g. plugin_tab's own Git/Properties resizable split -
        // E10_asset_browser_plugin_tab.h) for the exact same draggable-divider behavior
        // RenderDockableWindows() already uses for the outer Left/Right split.
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

    protected:

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

    public:
        // ScaleButton/RenderPathHistoryPopup are called from files_tab.h/virtual_tree_tab.h, which hold
        // assert_browser only by reference (not derived from it), so they need real public access, not
        // the protected level everything else in this block uses - re-closed with `protected:` again
        // right after RenderPathHistoryPopup so nothing else here is accidentally exposed.
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
        // Shared two-tab path-history popup - originally virtual_tree_tab's own inline
        // RenderNavigationPath() block (its "History"/"Navigation" tabs over the descriptor-folder
        // tree's own history), now factored out here so files_tab's real-filesystem history can use the
        // EXACT same widget rather than a hand-rolled lookalike - direct user request: "keep things
        // consistent... if this means we can refactor code... the less code the better." Both tabs'
        // consumers keep their own path_history_entry shape (folder-guid based vs filesystem-path
        // based) and their own PathHistoryUpdate/UpdateHistoryLRU logic - this only owns the WIDGET,
        // parameterized on how to turn one entry into a display string and what happens when one is
        // picked from each list:
        //   ToString(entry) -> std::string                  (empty string = skip this row entirely)
        //   OnPickRecency(entry)                             ("History" tab - jump via the caller's own
        //                                                      PathHistoryUpdate, may truncate/append)
        //   OnPickStack(index)                                ("Navigation" tab - jump to that EXACT
        //                                                      index in the caller's own linear stack,
        //                                                      preserving the rest of it - NOT the same
        //                                                      as calling PathHistoryUpdate again)
        // bShow/Pos/Size are the caller's own m_PathHistoryShow/m_PathHistoryPos/m_PathHistorySize
        // (captured from the caller's own breadcrumb-bar draw, e.g. RenderPath()'s start_pos/line_height).
        template<typename T_ENTRY, typename T_TO_STRING, typename T_ON_PICK_RECENCY, typename T_ON_PICK_STACK>
        static void RenderPathHistoryPopup(bool& bShow, ImVec2 Pos, ImVec2 Size
                                           , const std::vector<T_ENTRY>& RecencyList
                                           , const std::vector<T_ENTRY>& LinearStack, std::uint32_t CurrentIndex
                                           , T_TO_STRING&& ToString, T_ON_PICK_RECENCY&& OnPickRecency, T_ON_PICK_STACK&& OnPickStack) noexcept
        {
            if (bShow)
            {
                ImGui::SetNextWindowPos ({ Pos.x, Pos.y + 24 });
                ImGui::SetNextWindowSize({ Size.x, Size.y + 24 * 4 });
                ImGui::OpenPopup("Path History");
            }

            if (!ImGui::BeginPopup("Path History")) return;

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Tab,    ImVec4(0.245f, 0.245f, 0.245f, 0.8f));

            if (ImGui::BeginTabBar("History"))
            {
                if (ImGui::BeginTabItem("\xEE\xA0\x9C History"))
                {
                    ImGui::SetNextWindowBgAlpha(0.0f);
                    if (ImGui::BeginChild("FrameRU"))
                    {
                        for (std::uint32_t i = 0; i < RecencyList.size(); ++i)
                        {
                            std::string Name = ToString(RecencyList[i]);
                            if (!Name.empty())
                            {
                                ImGui::PushID(static_cast<int>(i));
                                if (ImGui::Button(std::format("\xEE\xA0\x9C {}", Name).c_str()))
                                {
                                    OnPickRecency(RecencyList[i]);
                                    bShow = false;
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
                    if (ImGui::BeginChild("FrameWindow"))
                    {
                        for (std::uint32_t i = static_cast<std::uint32_t>(LinearStack.size()); i-- > 0; )
                        {
                            std::string Name = ToString(LinearStack[i]);
                            if (!Name.empty())
                            {
                                Name = (i == CurrentIndex) ? std::format("\xEE\x9C\xBE {}", Name) : std::format("  {}", Name);
                                ImGui::PushID(static_cast<int>(i));
                                if (ImGui::Button(Name.c_str()))
                                {
                                    OnPickStack(i);
                                    bShow = false;
                                    ImGui::CloseCurrentPopup();
                                }
                                ImGui::PopID();
                            }
                        }
                        ImGui::EndChild();
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered())
                {
                    ImGui::CloseCurrentPopup();
                    bShow = false;
                }

                ImGui::EndTabBar();
            }

            ImGui::PopStyleColor(2);
            ImGui::EndPopup();
        }

    protected:
        //=============================================================================

        // Independent-windows layout for DOCKABLE mode - see browser_registration<>'s own comment for
        // T_DOCKABLE_ONLY/T_HAS_LEFT_PANEL. Every tab in m_Tabs (POPUP-shared ones like virtual_tree_tab/
        // compiler_tab AND anything registered DOCKABLE_ONLY) gets its own free-standing
        // ImGui::Begin/End window here instead of sharing one window + tab bar - POPUP mode (the 8
        // other examples' asset pickers) never calls this, see MainWindow()'s own branch.
        void RenderDockableWindows()
        {
            int Index = 0;
            for (auto& pTab : m_Tabs)
            {
                // Staggered default position - every DOCKABLE window's FirstUseEver applies
                // independently, so leaving them all at the same implicit default would stack every
                // window exactly on top of the first one the very first time this ever runs (before
                // the user has dragged/docked anything). Only affects first-ever layout; imgui.ini
                // remembers whatever the user arranges afterward.
                ImGui::SetNextWindowPos(ImVec2(20.0f + 40.0f * Index, 20.0f + 40.0f * Index), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2(500, 500), ImGuiCond_FirstUseEver);
                ++Index;
                if (ImGui::Begin(pTab->m_pName))
                {
                    if (pTab->m_bHasLeftPanel)
                    {
                        const float total_width  = ImGui::GetContentRegionAvail().x;
                        const float total_height = ImGui::GetContentRegionAvail().y;
                        constexpr float ButtonWidth = 4.0f;

                        // total_width can be 0 on the very first frame a brand-new DOCKABLE window
                        // renders (before its true docked size is established) - REAL BUG, caught live
                        // by direct user correction after the first fix attempt (0.25 -> 0.5) still
                        // showed "the default size": the OLD code unconditionally computed AND
                        // PERSISTED total_width*0.5 into m_DockableSplitSize on whatever frame first ran
                        // this, even when total_width was 0 that frame - the very next line's clamp then
                        // locked it at the 100px FLOOR forever (m_DockableSplitSize was no longer
                        // negative, so the "first-time default" branch never got a second chance to run
                        // once total_width became real). Confirmed via a one-shot printf probe:
                        // "total_width=0.0 -> m_DockableSplitSize=0.0" for every DOCKABLE tab, every
                        // single launch. Fixed by gating the ENTIRE split/persist block on total_width
                        // actually being real - on the rare single frame it isn't, this tab's left/right
                        // content is simply skipped for that one frame (nothing has ever been drawn for
                        // a just-created window yet anyway, so there's nothing visible to lose), and the
                        // -1.0f sentinel survives untouched to try again next frame.
                        if (total_width > 1.0f)
                        {
                            // ~0.32 (was 0.25, briefly 0.5 then 0.35) - direct user request: the left
                            // tree's default width was too small to be useful. 0.5 (a literal "double")
                            // turned out to be too much once seen live; settled on "1/3 or a bit less" -
                            // still a real improvement (~28% wider than the original 0.25) without
                            // giving away a third-plus of the window to the tree. m_DockableSplitSize is
                            // per-instance runtime state (never persisted to imgui.ini the way window
                            // pos/size are), so this default applies fresh every single launch, not just
                            // the very first one - matching the complaint exactly ("the user to ALWAYS
                            // have to resize it before it comes useful"). Applies to every DOCKABLE tab
                            // that has a left panel (Resources, Assets, Plugins) - the POPUP-mode
                            // default just below in MainWindow() (the 8 other examples' asset pickers)
                            // is intentionally left alone, not part of this request.
                            if (pTab->m_DockableSplitSize < 0.0f)
                                pTab->m_DockableSplitSize = total_width * 0.32f;
                            pTab->m_DockableSplitSize = std::clamp(pTab->m_DockableSplitSize, 100.0f, std::max(100.0f, total_width - 100.0f - ButtonWidth));

                            float size1 = pTab->m_DockableSplitSize;
                            float size2 = total_width - size1 - ButtonWidth;
                            Splitter(true, ButtonWidth, &size1, &size2, 100.0f, 100.0f, total_width, total_height);
                            pTab->m_DockableSplitSize = size1;

                            // Search bar above the tree - same POPUP-mode placement (RenderSearchBar,
                            // MainWindow()'s own left-column group), just re-hosted here. Only
                            // virtual_tree_tab currently reads m_SearchString (its own RightPanel()'s
                            // filtering), but this renders for any left-paneled DOCKABLE tab, matching
                            // where it visually sat before rather than special-casing one tab by name.
                            auto SearchBarTop = ImGui::GetCursorScreenPos();
                            ImGui::BeginGroup();
                            RenderSearchBar(ImVec2(size1, total_height));

                            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.11f, 0.11f, 0.11f, 0.75f)); // was 0.145/0.80 - direct user request, "a bit darker"
                            if (ImGui::BeginChild("Left", ImVec2(size1, total_height - (ImGui::GetCursorScreenPos().y - SearchBarTop.y))))
                                pTab->LeftPanel();
                            ImGui::EndChild();
                            ImGui::EndGroup();

                            ImGui::SameLine();

                            if (ImGui::BeginChild("Right", ImVec2(size2, total_height)))
                                pTab->RightPanel();
                            ImGui::EndChild();
                            ImGui::PopStyleColor();
                        }
                    }
                    else
                    {
                        // Direct user correction: a no-left-panel DOCKABLE tab (Compilation today)
                        // rendered its RightPanel() straight into the raw window, never getting the
                        // same lighter ImGuiCol_ChildBg the has-left-panel branch above pushes around
                        // its own "Left"/"Right" children - it read as flatly darker than Resources/
                        // Assets/Plugins right next to it. Same color, same "Right"-shaped child, so
                        // any tab taking this branch matches the others automatically.
                        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.11f, 0.11f, 0.11f, 0.75f)); // was 0.145/0.80 - direct user request, "a bit darker"
                        if (ImGui::BeginChild("Right", ImGui::GetContentRegionAvail()))
                            pTab->RightPanel();
                        ImGui::EndChild();
                        ImGui::PopStyleColor();
                    }
                }
                // Always call End() regardless of Begin()'s return value - same ImGui rule/gotcha
                // called out in the POPUP path below (a docked-but-inactive or collapsed window still
                // needs its End()).
                ImGui::End();
            }
        }

        //=============================================================================

        void MainWindow()
        {
            // DOCKABLE mode (E29's persistent tool window, the only consumer today) gets its own
            // independent-windows layout instead of the shared-window/tab-bar body below. POPUP mode
            // (every other example's one-shot asset picker) falls through to that body completely
            // unchanged - deliberately untouched by this split, see RenderDockableWindows()'s own
            // comment.
            if (m_DisplayMode == display_mode::DOCKABLE)
            {
                RenderDockableWindows();
                return;
            }

            ImGui::SetNextWindowBgAlpha(0.9f);
            ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
            
            // POPUP stays a floating, undockable overlay - docking it or leaving it parked in
            // someone's layout would make "+" pickers behave inconsistently. DOCKABLE docks like any
            // other tool window instead.
            const ImGuiWindowFlags WindowFlags = ImGuiWindowFlags_NoCollapse | (m_DisplayMode == display_mode::POPUP ? ImGuiWindowFlags_NoDocking : ImGuiWindowFlags_None);
            if (ImGui::Begin(m_WindowName.data(), nullptr, WindowFlags))
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));        // Transparent background

                // Get available space
                float total_width = ImGui::GetContentRegionAvail().x;
                // Only POPUP reserves room at the bottom for the Close button + separator - DOCKABLE
                // has neither, so it should use the full available height instead of leaving that
                // space empty.
                float total_height = ImGui::GetContentRegionAvail().y - (m_DisplayMode == display_mode::POPUP ? 40.0f : 0.0f);
                constexpr float ButtonWidth = 4.0f;
                // m_SplitSize1 is per-instance state (was previously a function-local `static`, which
                // meant every assert_browser instance in the process - the DOCKABLE main browser AND
                // the POPUP asset picker - shared the exact same splitter position, initialized once
                // from whichever instance's width happened to run MainWindow() first). Re-clamp every
                // frame (not just while actively dragging - Splitter() itself only clamps on an active
                // drag) so a window resize/dock/undock can't leave the left panel claiming more width
                // than the window currently has, which pushed the right "Panel" (the file list) off
                // past the visible edge - i.e. looked exactly like "the files disappeared".
                if (m_SplitSize1 < 0.0f)
                    m_SplitSize1 = total_width * 0.2f;
                m_SplitSize1 = std::clamp(m_SplitSize1, 100.0f, std::max(100.0f, total_width - 100.0f - ButtonWidth));
                float size1 = m_SplitSize1;
                float size2 = total_width - size1 - ButtonWidth;
                Splitter(true, ButtonWidth, &size1, &size2, 100.0f, 100.0f, total_width, total_height);
                m_SplitSize1 = size1;

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
                        // DOCKABLE-only tabs (Resource Plugin, Asset - see browser_registration<>'s own
                        // comment) never make sense inside a one-shot POPUP picker's tab bar.
                        if (pE->m_bDockableOnly) continue;

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
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.11f, 0.11f, 0.11f, 0.75f)); // was 0.145/0.80 - direct user request, "a bit darker" 
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
                // Close button - POPUP only; DOCKABLE is a permanent panel with nothing to cancel out of.
                //
                if (m_DisplayMode == display_mode::POPUP)
                {
                    ImGui::Separator();
                    if (ScaleButton(" Close ", 1.5f))
                    {
                        m_bRenderBrowser = false;
                    }
                }
            }
            // Always call End() regardless of Begin()'s return value (ImGui's own documented rule) -
            // this was previously INSIDE the if block above, so it never ran whenever Begin() returned
            // false (a docked-but-not-the-active-tab window, or collapsed/clipped) - permanently
            // unbalancing ImGui's window stack from that frame on ("Missing EndChild()" assert every
            // frame afterward). Never surfaced before because E29 is the only DOCKABLE user of this
            // browser - POPUP mode forces ImGuiWindowFlags_NoDocking, so Begin() there never returns
            // false this way.
            ImGui::End();
        }

        using tab_list = std::vector<std::unique_ptr<asset_browser_tab_base>>;

        e10::library_mgr*                   m_pAssetMgr             = nullptr;
        xresource::mgr*                     m_pResourceMgr          = nullptr;
        xgpu::device*                       m_pDevice               = nullptr;   // see SetDevice()
        xresource::full_guid                m_LastGeneratedAsset    = {};
        xresource::full_guid                m_SelectedAsset         = {};
        library::guid                       m_SelectedLibrary       = {};
        display_mode                        m_DisplayMode           = display_mode::POPUP;
        bool                                m_bRenderBrowser        = false;
        // Left-panel width of MainWindow()'s splitter, in pixels. Negative = "not yet initialized for
        // this instance" (see MainWindow() for why this must be per-instance, not a function-local
        // static shared by every assert_browser in the process).
        float                               m_SplitSize1            = -1.0f;
        tab_list                            m_Tabs                  = {};
        std::array<char,256>                m_WindowName            = {"Resource Browser"};

    public:

        bool isPopup() const noexcept {return m_pPopupUID; }

        const void*                         m_pPopupUID             = {};
        std::vector<xresource::type_guid>   m_FilterByType          = {};
        std::string                         m_SearchString          = {};

        // Optional interception hooks for the browser's own real mutations (rename/move-between-
        // folders/trash/create) - default-empty, so every existing consumer (E10, E19-E21, E23-E25,
        // E28) is byte-for-byte unaffected. Set by a consumer that wants these actions to go through
        // its own undo/command system (E29_LevelSceneEditorKit.h's RegisterAssetBrowserCallbacks) -
        // same additive, opt-in pattern already used for xproperty::inspector's own
        // m_OnPropertyChanged/m_OnEntityReferenceRender (entity_inspector_bridge::RegisterCallbacks).
        // Each call site in virtual_tree_tab checks the hook first and calls it INSTEAD of
        // library_mgr directly when set - never both, so a wired-up consumer's own undo history is
        // the single source of truth for what actually happened, not a duplicate/racing mutation.
        std::function<void(library::guid, xresource::full_guid /*Asset*/, std::string_view /*NewName*/)>
            m_OnRenameAsset;
        std::function<void(library::guid, xresource::full_guid /*Asset*/, xresource::full_guid /*OldParent*/, xresource::full_guid /*NewParent*/)>
            m_OnMoveAsset;
        std::function<void(library::guid, xresource::full_guid /*Asset*/)>
            m_OnDeleteAsset;
        std::function<void(library::guid, xresource::full_guid /*Asset*/, xresource::full_guid /*NewParent*/)>
            m_OnRestoreAsset;
        // Returns the guid actually assigned to the new asset (mirrors NewAsset's own return value) -
        // the caller (AddResourcePopUp etc.) needs it back synchronously to update selection, exactly
        // as it already does with NewAsset's own return today.
        std::function<xresource::full_guid(library::guid, xresource::type_guid /*Type*/, xresource::full_guid /*Parent*/, std::string_view /*Name*/)>
            m_OnCreateAsset;

        // Same opt-in, default-empty pattern as the five hooks above, for the REAL Assets-folder file
        // mutations (Phase 4/5 of the window-split plan - E10_AssetMgr.h's MoveAssetFile/CopyAssetFile,
        // wrapped as xundo commands in E29_Commands_AssetFiles.h). files_tab (E10_asset_browser_
        // files_tab.h) checks these first and calls them INSTEAD of library_mgr directly when set - only
        // E29 (RegisterAssetBrowserCallbacks) wires them today, every other DOCKABLE-only consumer of
        // files_tab (there are none yet) would fall back to a direct, non-undo-routed call. Rename and
        // Move share one hook (MoveAssetFile is the single underlying primitive for both, matching
        // rename_asset_file_cmd/move_asset_file_cmd's own "two names, one wrap" shape).
        //
        // Move/Delete are ALWAYS batched (a vector, even for a single item) - direct user correction:
        // "a 5-file delete should be 1 undo/redo step... the operation should be grouped." xundo::system
        // already has a grouped-execute API (Execute(group_name, vector<string>) - one history entry for
        // every sub-command); these hooks exist so files_tab can hand a WHOLE multi-item gesture (a
        // multi-select delete, a multi-cut paste, a whole-selection drag) to ONE call, which
        // RegisterAssetBrowserCallbacks turns into ONE grouped Execute() rather than N separate ones.
        // Restore/Copy stay single-item - neither has a multi-item call site today.
        //
        // Both return true on success - a real bug found live: PasteClipboardInto used to spend the
        // cut clipboard unconditionally, even when the paste had just failed outright (e.g. pasting
        // into the same folder the files were cut from), so a failed paste silently lost the user's
        // clipboard instead of leaving it intact to retry. The caller needs a real success signal to
        // fix that, not just "did this hook exist".
        std::function<bool(library::guid, const std::vector<std::pair<std::wstring, std::wstring>>& /*Old,New pairs*/)>
            m_OnMoveAssetFileBatch;
        std::function<bool(library::guid, const std::vector<std::wstring>& /*RelPaths*/)>
            m_OnDeleteAssetFileToTrashBatch;
        std::function<void(library::guid, const std::wstring& /*TrashRelPath*/, const std::wstring& /*OriginalRelPath*/)>
            m_OnRestoreAssetFileFromTrash;
        std::function<void(library::guid, const std::wstring& /*SourceRelPath*/, const std::wstring& /*NewRelPath*/)>
            m_OnCopyAssetFile;

        // Optional hook so files_tab can find the real Win32 HWND currently hosting this browser, for
        // real OS-level (Explorer) drag-out (E10_AssetOleDrag.h) - it needs a screen-space window rect
        // to decide "has the drag left our own app" every frame. Returns a std::size_t castable to HWND
        // (matches xgpu::window::getSystemWindowHandle's own return type), or 0 if not wired - default-
        // empty like every hook above, so OS drag-out is simply disabled for every consumer except E29
        // (RegisterAssetBrowserCallbacks). Deliberately scoped to the MAIN window only: a docked Asset
        // Tree lives inside it, but an undocked panel spawns its own multi-viewport child HWND that this
        // hook can't see - dragging from an undocked panel falls back to in-app-only behavior rather
        // than risk hijacking the gesture against the wrong window's rect.
        std::function<std::size_t(void)>
            m_OnGetMainWindowHandle;
    };

} // namespace e10
#endif