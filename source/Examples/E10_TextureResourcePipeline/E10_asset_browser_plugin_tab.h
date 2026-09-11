#ifndef E10_ASSET_BROWSER_PLUGIN_TAB_H
#define E10_ASSET_BROWSER_PLUGIN_TAB_H
#pragma once

// Resource Plugin window - Phase 2 of the Asset Browser window-split plan (see plan file
// lively-knitting-sifakis.md). DOCKABLE-only (see browser_registration<>'s own comment in
// E10_AssetBrowser.h - never makes sense inside a one-shot POPUP asset picker): left = selectable list
// of every loaded plugin, right = that plugin's existing reflected properties via the standard
// xproperty::inspector pattern already used throughout this codebase for a single object (e.g.
// E20_Material_Instance_Editor.cpp's own "Inspector"/AppendEntityComponent shape), plus a view-only
// "Git" section showing which depot the plugin lives in.
//
// The git-remote lookup shells out per-plugin, lazily, cached until a manual refresh - deliberately
// lives HERE, not in E10_PluginMgr.h/asset_plugins_db, to keep the asset mgr headless (same split this
// project already used for the plugin icon atlas's GPU-upload half - see
// feedback_asset_mgr_stays_headless memory / E10_PluginIconAtlas.h's own top comment). No new stored/
// serialized field on pipeline_plugin - each plugin folder is already its own git clone on disk
// (example.lionprj/Cache/Plugins/*.plugin), so the URL is always accurate and there's nothing to keep
// in sync across 13+ plugin manifests.

#include <cstdio>

namespace e10
{
    // Inherits xproperty::inspector (rather than holding one as a member) deliberately: inspector's
    // only PUBLIC render entry point (Show(Context, Callback)) always opens its own top-level
    // ImGui::Begin(m_pName) window - confirmed by reading its .cpp, and confirmed live: this is exactly
    // why E29's "Entity Properties" panel is itself a normal independently-dockable window (its
    // xproperty::inspector is literally named "Entity Properties"), not something embedded inside
    // another panel. Direct user correction: the plugin's property grid must render FIXED, inline,
    // beside the Git panel - not as a separate floating/dockable window. The no-arg Show() that
    // actually draws rows without opening a window exists (xPropertyImGuiInspector.h ~line 640) but is
    // `protected` - inheriting is the standard, intended way to reach a protected member from outside
    // the library without patching xproperty itself (the class already has a virtual destructor, so
    // it's designed to support being a base).
    struct plugin_tab : e10::asset_browser_tab_base, xproperty::inspector
    {
        plugin_tab(assert_browser& Browser, const char* pName)
            : asset_browser_tab_base{ Browser, pName }
            , xproperty::inspector{ "Resource Plugin Properties" }
            , m_AssetMgr{ *Browser.getAssetMgr() }
        {
            // This tab is DOCKABLE-only (see its registration below), so it only ever renders under
            // E29 - same reasoning/comment as E29_LevelScene_Editor.cpp's own EntityInspector override.
            m_Settings.m_bRenderBackgroundDepth  = false;
            m_Settings.m_bRenderLeftBackground   = false;
            m_Settings.m_bRenderRightBackground  = false;
            // Same row-spacing override as EntityInspector (E29_LevelScene_Editor.cpp) - these are
            // per-instance settings the inspector pushes itself, not tied to the ambient ImGuiStyle.
            m_Settings.m_FramePadding            = ImVec2(4.0f, 3.0f);   // was {1, 3.5} - +2px, same follow-up as EntityInspector (buttons/fields, not row gap)
            // ItemSpacing.x specifically: this is what leaves an unpainted gap between the component
            // header's own left box (TreeNodeEx) and its right-column fill (a separate AddRectFilled
            // call) - confirmed by direct pixel measurement (an ~8px strip of raw background showing
            // through at exactly 2x this value) after a direct user follow-up with a screenshot: "the
            // dark divider that breaks the background color of the header... literally breaks it in
            // two". Not a border/color issue (already checked) - genuinely unpainted space between two
            // separately-drawn rects, from the columns' own gap reservation.
            m_Settings.m_ItemSpacing             = ImVec2(1.0f, 1.0f);   // was {0.5, 2.0}, then {4, 1}
            m_Settings.m_TableFramePadding       = ImVec2(4.0f, 1.0f);   // was {2, 6}
        }

        //=============================================================================
        // One-shot, cached, synchronous `git -C <PluginPath> remote get-url origin` capture.
        // Deliberately simple (_popen, not the richer async CreateProcess+pipe machinery
        // library_mgr::RunCommandLine uses for the long-running compiler pipeline - that solves a
        // different, harder problem than a single quick query here). Never throws - this is purely
        // informational UI, so any failure (not a git repo, no "origin" remote, git not on PATH) just
        // shows up as an empty/failed result string, not a crash.
        static std::string QueryGitRemoteURL(const std::wstring& PluginPath) noexcept
        {
            std::string Cmd = std::format("git -C \"{}\" remote get-url origin 2>&1", xstrtool::To(PluginPath));
            std::string Result;
            if (FILE* pPipe = _popen(Cmd.c_str(), "r"))
            {
                char Buffer[512];
                while (fgets(Buffer, sizeof(Buffer), pPipe)) Result += Buffer;
                _pclose(pPipe);
            }
            while (!Result.empty() && (Result.back() == '\n' || Result.back() == '\r')) Result.pop_back();
            return Result;
        }

        //=============================================================================

        void RebuildInspector() noexcept
        {
            // Rebuild only on an actual selection change, never every frame - re-constructing an
            // xproperty::inspector's content every frame makes every widget's ImGui id unstable
            // (address-seeded), so clicks register hover but never activate (see
            // xproperty_inspector_must_persist_across_frames memory).
            clear();
            if (m_SelectedIndex < 0 || m_SelectedIndex >= static_cast<int>(m_AssetMgr.m_AssetPluginsDB.m_lPlugins.size()))
                return;

            auto& Plugin = m_AssetMgr.m_AssetPluginsDB.m_lPlugins[m_SelectedIndex];
            AppendEntity();
            AppendEntityComponent(*xproperty::getObject(Plugin), &Plugin);
        }

        //=============================================================================

        void LeftPanel() noexcept override
        {
            // Every DOCKABLE left-paneled tab gets the same search bar re-hosted above it (see
            // RenderDockableWindows()'s own comment) - a plain case-insensitive substring filter here
            // is enough for a list this short (~a dozen plugins today); the asset grid's own fuzzy
            // Damerau-Levenshtein ranking (virtual_tree_tab's RightPanel) is solving a different
            // problem (ranking many, possibly-misspelled matches) that doesn't apply at this scale.
            auto ContainsCaseInsensitive = [](std::string_view Haystack, std::string_view Needle) noexcept
            {
                if (Needle.empty()) return true;
                auto ToLower = [](unsigned char c) noexcept { return static_cast<char>(std::tolower(c)); };
                return std::ranges::search(Haystack, Needle, {}, ToLower, ToLower).begin() != Haystack.end();
            };

            auto& Plugins = m_AssetMgr.m_AssetPluginsDB.m_lPlugins;
            for (int i = 0; i < static_cast<int>(Plugins.size()); ++i)
            {
                auto& Plugin = Plugins[i];
                if (!ContainsCaseInsensitive(Plugin.m_TypeName, m_Browser.m_SearchString)) continue;

                ImGui::PushID(i);

                if (auto Icon = m_AssetMgr.m_AssetPluginsDB.getIconRef(Plugin.m_TypeGUID); Icon.isValid())
                {
                    ImGui::Image((ImTextureRef)(void*)Icon.m_pTexture, ImVec2(22, 22)
                                , ImVec2(Icon.m_U0, Icon.m_V0), ImVec2(Icon.m_U1, Icon.m_V1));
                    ImGui::SameLine();
                }

                if (ImGui::Selectable(Plugin.m_TypeName.c_str(), m_SelectedIndex == i) && m_SelectedIndex != i)
                {
                    m_SelectedIndex = i;
                    RebuildInspector();
                }

                // First entry of what will grow into a bigger context menu - explorer link only for now.
                if (ImGui::BeginPopupContextItem("PluginContextMenu"))
                {
                    if (ImGui::MenuItem("  Open in Explorer"))
                    {
                        auto Str = std::format(L"explorer \"{}\"", Plugin.m_PluginPath);
                        system(xstrtool::To(Str).data());
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }
        }

        //=============================================================================

        void RightPanel() noexcept override
        {
            const bool bHasSelection = m_SelectedIndex >= 0 && m_SelectedIndex < static_cast<int>(m_AssetMgr.m_AssetPluginsDB.m_lPlugins.size());

            // Properties on the left, Git on the right - swapped from the original layout per direct
            // user request - separated by a draggable Splitter() (assert_browser::Splitter, made public
            // for exactly this reuse - see its own comment) rather than a fixed width; another direct
            // user request reversing the earlier "fixed, not dynamic" call for this specific border.
            // The panel structure itself always renders, selection or not ("the property window should
            // always be there") - only the CONTENT inside each side is conditional, never the layout.
            const float TotalWidth  = ImGui::GetContentRegionAvail().x;
            const float TotalHeight = ImGui::GetContentRegionAvail().y;
            constexpr float ButtonWidth = 4.0f;

            // 0.5 (was 0.7) - direct user request: the Plugins window should read as roughly three even
            // columns (plugin list | properties | git), "1/3, 1/3, then the rest" - the OUTER List-vs-
            // Right split already defaults the list to ~1/3 of the whole window
            // (RenderDockableWindows()'s own m_DockableSplitSize default), so Properties needs to be
            // about HALF of the remaining ~2/3 (not 70% of it) for Properties/Git to each land close to
            // another 1/3 of the total width.
            if (m_PropertiesSplitSize < 0.0f)
                m_PropertiesSplitSize = TotalWidth * 0.5f;
            m_PropertiesSplitSize = std::clamp(m_PropertiesSplitSize, 100.0f, std::max(100.0f, TotalWidth - 100.0f - ButtonWidth));

            float PropertiesWidth = m_PropertiesSplitSize;
            float GitWidth        = TotalWidth - PropertiesWidth - ButtonWidth;
            assert_browser::Splitter(true, ButtonWidth, &PropertiesWidth, &GitWidth, 100.0f, 100.0f, TotalWidth, TotalHeight);
            m_PropertiesSplitSize = PropertiesWidth;

            if (ImGui::BeginChild("Properties", ImVec2(PropertiesWidth, TotalHeight)))
            {
                if (!bHasSelection)
                {
                    ImGui::TextDisabled("Select a plugin on the left.");
                }
                else
                {
                    // The inline (no-arg) Show() renders rows only - it assumes the 2-column layout,
                    // m_pContext, and the inspector's own key style vars are already set up (that setup
                    // is normally done by the public Show(Context, Callback) overload, around its own
                    // ImGui::Begin() - see this struct's own top comment for why that overload isn't
                    // used here). Replicated 1-for-1 from that overload's own "Key styles" block
                    // (xPropertyImGuiInspector.cpp) so the embedded grid isn't missing the padding/
                    // spacing/rounding that makes the standalone inspector windows elsewhere in this
                    // codebase look consistent - direct user correction, this was missing before.
                    xproperty::settings::context Context;
                    m_pContext = &Context;

                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, m_Settings.m_WindowPadding);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, m_Settings.m_FramePadding);
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, m_Settings.m_ItemSpacing);
                    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, m_Settings.m_IndentSpacing);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);

                    // Header background matches the panel's own WindowBg exactly (not just "neutral
                    // gray" - direct user follow-up, with a screenshot: a visibly highlighted bar with
                    // nothing drawn on it, spanning the whole empty value column, read as a big dead
                    // gap). Entity Properties keeps its own neutral-gray header because that column
                    // actually has something in it (the "X" remove-component button) - this panel's
                    // properties are read-only, nothing is ever drawn in that space, so the header
                    // blends into the row instead of highlighting emptiness. This tab is DOCKABLE-only
                    // (browser_registration<>'s own T_DOCKABLE_ONLY, above), so it only ever renders
                    // under E29.
                    ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0x38 / 255.0f, 0x38 / 255.0f, 0x38 / 255.0f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0x4A / 255.0f, 0x4A / 255.0f, 0x4A / 255.0f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0x4A / 255.0f, 0x4A / 255.0f, 0x4A / 255.0f, 1.0f));

                    // border=false - the legacy Columns() API defaults to drawing a live, draggable
                    // vertical separator line between columns; direct user follow-up screenshot after
                    // the header-color fix above ("the gap still there") - the color fix couldn't
                    // touch this because it isn't a color mismatch, it's a real border line ImGui
                    // draws independently of any Style.Colors value used elsewhere in this panel.
                    const float TotalWidth = ImGui::GetContentRegionAvail().x;
                    ImGui::Columns(2, nullptr, false);
                    // Even 50/50 split left a big dead gap after short labels like "TypeName" before
                    // the value even starts (direct user follow-up: "big separation between the left
                    // and right columns").
                    ImGui::SetColumnWidth(0, TotalWidth * 0.42f);
                    ImGui::Separator();
                    Show();
                    ImGui::Columns(1);
                    ImGui::Separator();

                    ImGui::PopStyleColor(3);
                    ImGui::PopStyleVar(6);
                }
            }
            ImGui::EndChild();

            ImGui::SameLine();

            if (ImGui::BeginChild("Git", ImVec2(GitWidth, TotalHeight)))
            {
                ImGui::TextUnformatted("Git");
                ImGui::Separator();

                if (!bHasSelection)
                {
                    ImGui::TextDisabled("Select a plugin on the left.");
                }
                else
                {
                    auto& Plugin = m_AssetMgr.m_AssetPluginsDB.m_lPlugins[m_SelectedIndex];

                    // Lazily query + cache per plugin path - a git invocation is a real process spawn,
                    // not something to pay every single frame this window happens to be visible.
                    auto CacheIt = m_GitRemoteCache.find(Plugin.m_PluginPath);
                    if (CacheIt == m_GitRemoteCache.end())
                        CacheIt = m_GitRemoteCache.emplace(Plugin.m_PluginPath, QueryGitRemoteURL(Plugin.m_PluginPath)).first;

                    if (CacheIt->second.empty() || CacheIt->second.starts_with("fatal:"))
                        ImGui::TextDisabled("Not a git repository (or no 'origin' remote)");
                    else
                        ImGui::TextWrapped("%s", CacheIt->second.c_str());

                    if (ImGui::SmallButton("Refresh"))
                        m_GitRemoteCache[Plugin.m_PluginPath] = QueryGitRemoteURL(Plugin.m_PluginPath);
                }
            }
            ImGui::EndChild();
        }

        e10::library_mgr&                              m_AssetMgr;
        int                                             m_SelectedIndex       = -1;
        float                                           m_PropertiesSplitSize = -1.0f;
        std::unordered_map<std::wstring, std::string>  m_GitRemoteCache      = {};
    };

    namespace
    {
        // "Plugins" (was "Resource Plugins") - direct user request, same icon kept (already confirmed
        // rendering correctly, no reason to change it).
        inline browser_registration<plugin_tab, "\xEE\x9F\x85 Plugins", 2.0f, true, true > g_PluginTab{};
    }
}

#endif
