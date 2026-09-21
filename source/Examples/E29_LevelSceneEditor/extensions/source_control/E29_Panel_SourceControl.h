#ifndef E29_PANEL_SOURCE_CONTROL_H
#define E29_PANEL_SOURCE_CONTROL_H
#pragma once

// Source Control panel - Phase 4C/4D of the source-control plan (source_control_abstraction_spec_v1_3.md).
// An INDEPENDENT tab, not a mode of the Asset Tree/Resource Browser - direct user correction
// (2026-09-17): "the asset window has its mission and is completely different to the source control
// window... Source control is involved in Assets, Resources, Entities, Project settings, etc." This
// panel talks only to e10::source_control:: (the centralized cache, E10_SourceControlCache.h) and the
// E29_Commands_SourceControl.h command bus - it never reaches into
// E10_asset_browser_files_tab.h/E10_asset_browser_virtual_tree_tab.h's own code, and they never reach
// into this file either.
//
// Same self-sufficiency convention as every other kit/E29_Panel_*.h (see E29_Panel_LevelTree.h's own
// top comment) - includes what it names rather than relying on a distant caller's include order.
#include "source/Examples/E29_LevelSceneEditor/E29_EditorTabs.h"
#include "source/Examples/E29_LevelSceneEditor/extensions/source_control/E29_Commands_SourceControl.h"
#include "dependencies/xresource_pipeline_v2/source/editor/E10_SourceControlCache.h"
#include <cmath>
#include <unordered_set>

namespace e29
{
    // A small rotating-dots spinner, drawn with plain ImDrawList primitives - same "never a font
    // glyph" discipline the SC badges themselves already follow. Animates off ImGui::GetTime(), so
    // it only actually spins while this panel is visible and rendering (every frame, like any other
    // ImGui content) - no separate timer/state needed.
    //
    // Real bug found live (2026-09-17): the first version sized each dot as Radius*0.22 with Radius
    // itself already small (a fraction of the text line height) - the dots rounded down to well
    // under a pixel and were effectively invisible, which is why "nothing is spinning" even though
    // the code was running. Dot radius is now floored at a real, always-visible pixel size instead
    // of a pure percentage of an already-small orbit radius.
    inline void DrawLoadingSpinner(ImDrawList* DrawList, ImVec2 Center, float Radius, ImU32 Color) noexcept
    {
        constexpr int   NumDots = 8;
        constexpr float SpeedRadPerSec = 3.0f;
        const float DotRadius = std::max(1.75f, Radius * 0.35f);
        const float Time = static_cast<float>(ImGui::GetTime());
        for (int i = 0; i < NumDots; ++i)
        {
            const float Fraction = static_cast<float>(i) / static_cast<float>(NumDots);
            const float Angle    = Time * SpeedRadPerSec - Fraction * 2.0f * 3.14159265f;
            const ImVec2 P{ Center.x + Radius * std::cos(Angle), Center.y + Radius * std::sin(Angle) };
            const float Alpha = 0.15f + 0.85f * (1.0f - Fraction); // brightest dot leads, tail fades out
            const ImU32 DotColor = (Color & 0x00FFFFFFu) | (static_cast<ImU32>(Alpha * 255.0f) << 24);
            DrawList->AddCircleFilled(P, DotRadius, DotColor);
        }
    }
}

namespace e29
{
    // One row = one pending (modified/untracked/conflicted) or locked file, from ANY currently open
    // library - "whole project" scope, not scoped to one library's Assets folder (direct user
    // direction: Assets/Resources/Entities/Project-settings are all just files under the same repo,
    // and GetStatus/ListLocks already run unfiltered against the real working tree).
    struct sc_panel_row
    {
        e10::library::guid       m_Library;
        std::wstring             m_RootPath;      // this library's real root - what ResolveLibraryRootPath would return
        std::wstring             m_RelativePath;  // NormalizeKey'd, relative to m_RootPath
        std::wstring             m_Key;            // composite "<LibraryHex>|<RelativePath>" - unique across the whole project
        sc::FileStatus            m_Status{};
        std::optional<sc::LockInfo> m_Lock;
    };

    inline std::wstring SourceControlRowKey(e10::library::guid LibraryGuid, const std::wstring& RelativePath) noexcept
    {
        return xstrtool::To(e29::commands::FormatLibraryGuid(LibraryGuid)) + L"|" + RelativePath;
    }

    // Auto-changelist categories - direct user design: "per library should be a change list that
    // matches/works with similar organization" onto Depot -> Resource Library -> {Scenes & Levels,
    // Resources, Assets, Project Files}. "Entities inside scenes" (as originally described) isn't its
    // own tier here - a Scene is one committable file as far as git is concerned, there's no way to
    // stage or commit just one entity's change within it; a Scene row showing WHICH entities changed
    // inside it is a real, separate future feature (needs actual entity-level diffing, which doesn't
    // exist yet), not a change to commit granularity.
    enum class sc_category : std::uint8_t { ScenesAndLevels, Resources, Assets, ProjectFiles };
    inline constexpr std::size_t sc_category_count_v = 4;

    inline const char* SourceControlCategoryLabel(sc_category Category) noexcept
    {
        switch (Category)
        {
            case sc_category::ScenesAndLevels: return "Scenes & Levels";
            case sc_category::Resources:       return "Resources";
            case sc_category::Assets:          return "Assets";
            default:                            return "Project Files";
        }
    }

    // A pending path's classification: which tier it belongs to, plus (for anything under a real
    // descriptor/resource root) enough to group EVERY pending file belonging to the SAME resource
    // together - direct user design: "one scene should be one folder" - a single Scene resource can
    // have several simultaneously-pending files (info.txt, Descriptor.txt, the compiled binary, its
    // dependencies.txt log) that would otherwise show as disconnected, unrelated-looking rows.
    // m_TypeNameLower/m_HexInstance are empty for Assets/Project Files - those have no "owning
    // resource" concept to group by.
    struct sc_path_classification
    {
        sc_category  m_Category;
        std::wstring m_TypeNameLower;
        std::wstring m_HexInstance;
    };

    // Classifies a library-root-relative, NormalizeKey'd (lowercase, backslash-normalized) pending
    // path - path-based, not resource-guid-based, deliberately: git status (or a deleted file) only
    // ever gives us a path, which may not even resolve to a currently-loaded resource. Mirrors the
    // real on-disk roots library::m_UserDescriptorPath/m_SysDescriptorPath/m_ResourcePath establish
    // (E10_AssetMgr.h) and process_info_job::LoadInfo's own DependencyPath formula for the Logs root -
    // every one of them shares the same "<TypeName>\<xx>\<yy>\<hexInstance...>" shape right after its
    // own root segment (the instance hex is NOT zero-padded to a fixed width - e.g. a real Folder
    // resource's own file is literally "1.desc", not "0000...0001.desc"). Level/Scene resources
    // (xecs_level.h/xecs_scene.h) are the only two type names singled out into their own tier; every
    // other resource type (Material, Texture, Font, GeomStatic, ...) shares the generic "Resources"
    // tier. Anything under neither Assets\ nor a recognized descriptor/resource root (e.g.
    // .gitattributes, .pi\settings.json, Project.config\Library.config.txt itself) falls into
    // "Project Files" - real pending files this system already showed with nowhere better to go.
    inline sc_path_classification ClassifyPendingPath(const std::wstring& RelativePath) noexcept
    {
        auto StartsWith = [&](std::wstring_view Prefix) noexcept
        {
            return RelativePath.size() >= Prefix.size() && std::wstring_view(RelativePath).substr(0, Prefix.size()) == Prefix;
        };

        if (StartsWith(L"assets\\")) return { sc_category::Assets, {}, {} };

        std::wstring_view TypeSegment;
        if (StartsWith(L"descriptors\\"))
            TypeSegment = std::wstring_view(RelativePath).substr(12);
        else if (StartsWith(L"cache\\descriptors\\"))
            TypeSegment = std::wstring_view(RelativePath).substr(18);
        else if (StartsWith(L"cache\\resources\\logs\\"))
            TypeSegment = std::wstring_view(RelativePath).substr(21);
        else if (StartsWith(L"cache\\resources\\platforms\\"))
        {
            auto Rest = std::wstring_view(RelativePath).substr(26);
            if (auto Slash = Rest.find(L'\\'); Slash != std::wstring_view::npos)
                TypeSegment = Rest.substr(Slash + 1);
        }

        if (TypeSegment.empty()) return { sc_category::ProjectFiles, {}, {} };

        // TypeSegment = "<Type>\<xx>\<yy>\<hexInstance...>" (trailing suffix like ".desc\info.txt" or
        // ".log\dependencies.txt" or nothing at all for a compiled binary - only the LEADING hex run
        // right after the second '\' is the instance value).
        const auto Slash1 = TypeSegment.find(L'\\');
        const auto Type   = Slash1 == std::wstring_view::npos ? TypeSegment : TypeSegment.substr(0, Slash1);
        const auto Category = (Type == L"level" || Type == L"scene") ? sc_category::ScenesAndLevels : sc_category::Resources;

        // TypeSegment's remaining segments are exactly [xx, yy, hexInstance(+suffix), ...] - three
        // more '\'-splits needed to walk past xx and yy and land on the hex segment itself.
        std::wstring HexInstance;
        if (Slash1 != std::wstring_view::npos)
        {
            const auto Rest1 = TypeSegment.substr(Slash1 + 1); // "<xx>\<yy>\<hexInstance...>"
            if (const auto Slash2 = Rest1.find(L'\\'); Slash2 != std::wstring_view::npos)
            {
                const auto Rest2 = Rest1.substr(Slash2 + 1); // "<yy>\<hexInstance...>"
                if (const auto Slash3 = Rest2.find(L'\\'); Slash3 != std::wstring_view::npos)
                {
                    const auto Rest3 = Rest2.substr(Slash3 + 1); // "<hexInstance...>[\<rest>]"
                    const auto Slash4 = Rest3.find(L'\\');
                    const auto HexSeg = Slash4 == std::wstring_view::npos ? Rest3 : Rest3.substr(0, Slash4);

                    auto IsHexDigit = [](wchar_t C) noexcept { return (C >= L'0' && C <= L'9') || (C >= L'a' && C <= L'f') || (C >= L'A' && C <= L'F'); };
                    std::size_t Len = 0;
                    while (Len < HexSeg.size() && IsHexDigit(HexSeg[Len])) ++Len;
                    HexInstance = std::wstring(HexSeg.substr(0, Len));
                }
            }
        }

        return { Category, std::wstring(Type), HexInstance };
    }

    // A resource's identity, globally unique across every open library/depot - "LibraryHex|
    // TypeLower|Hex" - used to track a changelist's LIVE reference to a whole resource ("drag the
    // Scene 1 folder" - direct user design, 2026-09-18) rather than a frozen snapshot of whichever
    // files happened to be pending at drop time. sc_resource_group::m_GroupKey (left-tree-only,
    // "TypeLower|Hex") is scoped inside one already-known library's own bucket, so it isn't unique on
    // its own outside that context - this is the same identity with the library folded in, safe to
    // compare/store anywhere in the panel.
    inline std::wstring SourceControlGlobalGroupKey(e10::library::guid LibraryGuid, const std::wstring& TypeNameLower, const std::wstring& HexInstance) noexcept
    {
        return xstrtool::To(e29::commands::FormatLibraryGuid(LibraryGuid)) + L"|" + TypeNameLower + L"|" + HexInstance;
    }

    // A whole CATEGORY's identity within one library - "LibraryHex|CategoryIndex" - the same live-
    // reference concept as SourceControlGlobalGroupKey one level up the hierarchy (direct user design,
    // 2026-09-18: dragging a category folder like "Scenes & Levels" as a whole superset).
    inline std::wstring SourceControlGlobalCategoryKey(e10::library::guid LibraryGuid, sc_category Category) noexcept
    {
        return xstrtool::To(e29::commands::FormatLibraryGuid(LibraryGuid)) + L"|" + std::to_wstring(static_cast<int>(Category));
    }

    // A whole LIBRARY's identity - just "LibraryHex" - the same live-reference concept one level up
    // from SourceControlGlobalCategoryKey (direct user design, 2026-09-18: "drag the entire library to
    // my change list... this means the same thing as (All)", scoped to one library instead of a depot).
    inline std::wstring SourceControlGlobalLibraryKey(e10::library::guid LibraryGuid) noexcept
    {
        return xstrtool::To(e29::commands::FormatLibraryGuid(LibraryGuid));
    }

    // Groups by the CACHED depot identity (library::m_DepotProviderId/m_DepotRepositoryId, Phase B) -
    // two libraries sharing one depot correctly land in the same group, rather than each getting its
    // own redundant top-level entry. A library never yet validated (empty m_DepotProviderId) falls
    // back to its own path as a singleton group key - still renders sensibly rather than being
    // silently dropped or lumped in with libraries it may share nothing with.
    inline std::pair<std::string, std::string> SourceControlDepotKeyAndName(e10::library::guid LibraryGuid) noexcept
    {
        std::string ProviderId, RepositoryId;
        std::wstring Path;
        e10::g_LibMgr.m_mLibraryDB.FindAsReadOnly(LibraryGuid, [&](const std::unique_ptr<e10::library_db>& DB)
        {
            ProviderId   = DB->m_Library.m_DepotProviderId;
            RepositoryId = DB->m_Library.m_DepotRepositoryId;
            Path         = DB->m_Library.m_Path;
        });

        if (!ProviderId.empty())
            return { ProviderId + "|" + RepositoryId, RepositoryId };

        const auto PathStr = xstrtool::To(Path);
        return { "path|" + PathStr, PathStr + " (depot not yet validated)" };
    }

    // Same "read the root folder's own stored Name" source of truth virtual_tree_tab/files_tab's own
    // GetLibraryDisplayName already use (E10_asset_browser_files_tab.h:380) - a free function here
    // rather than a class member, since this panel has no assert_browser instance of its own to hang
    // it off of.
    inline std::string SourceControlLibraryDisplayName(e10::library::guid LibraryGuid) noexcept
    {
        std::string Name = "<unnamed>";
        e10::g_LibMgr.getInfo(LibraryGuid, xresource::full_guid{ LibraryGuid.m_Instance, e10::folder::type_guid_v }, [&](const xresource_pipeline::info& Info)
        {
            if (!Info.m_Name.empty()) Name = Info.m_Name;
        });
        return Name;
    }

    // A changelist is purely LOCAL bookkeeping - which pending files the user currently intends to
    // commit together, and under what message - never written to git in any form until Commit is
    // actually pressed. It can never drift out of sync with the real repo because it doesn't
    // represent repo state at all, just a grouping of it (see the plan's "Research" section for the
    // full reasoning vs. Perforce-style server-side changelists).
    // A changelist belongs to exactly one depot (direct user requirement) - a real commit is always
    // scoped to one repo, so a changelist spanning two depots could never actually be submitted as
    // one unit anyway; scoping it up front means the UI never lets you build one you can't commit.
    // m_DepotKey matches SourceControlDepotKeyAndName's own key shape, so membership can be checked
    // directly against whichever depot group a row currently falls under.
    struct sc_changelist
    {
        std::string                m_DepotKey;
        std::string                m_Name;
        std::string                m_Comment;
        std::vector<std::wstring>  m_Keys; // sc_panel_row::m_Key, insertion order preserved - explicit,
                                            // individually-picked files (or what a live group flattens
                                            // down to once its own membership is edited - see below).

        // A LIVE reference to a whole resource (SourceControlGlobalGroupKey format) - direct user
        // design (2026-09-18): "if the user drags the 'Scene 1' folder... he does not care about the
        // actual files there... before submitting[,] if the user does something to make more entities
        // ready to check in in 'scene 1' those entities will automatically be included." Resolved
        // fresh from the live row set EVERY time this changelist's files are displayed/committed - NOT
        // a snapshot - so a resource's newly-pending files ride along automatically. The moment the
        // user edits this group's own membership from inside the changelist (removes one specific
        // file), it "flattens": the entry here is dropped and every file it currently resolved to
        // (minus the one removed) is added to m_Keys instead - the group stops being tracked as a
        // concept because it no longer matches "the whole resource" (see
        // SourceControlRemoveRowFromChangelist).
        std::vector<std::wstring>  m_LiveGroupKeys;

        // A LIVE reference to a whole CATEGORY within one library (SourceControlGlobalCategoryKey
        // format - e.g. dragging the "Scenes & Levels" folder as a whole) - the same live-reference
        // concept as m_LiveGroupKeys, one level up the hierarchy. Direct user design (2026-09-18): "the
        // superset already includes the subset... we just want the superset" - assigning a category
        // live absorbs (removes) any resource-level m_LiveGroupKeys/explicit m_Keys entries it already
        // covers, in every changelist, since tracking both would be redundant. Flattens the same way a
        // resource group does - editing one row's membership degrades this ONE level (the category
        // reference is dropped, its OTHER resources become individual live m_LiveGroupKeys entries so
        // they keep their own folders, and only the specific resource the edited row belonged to
        // flattens further into explicit m_Keys) rather than exploding the whole category to flat
        // files at once.
        std::vector<std::wstring>  m_LiveCategoryKeys;

        // A LIVE reference to a WHOLE LIBRARY (SourceControlGlobalLibraryKey format) - one level up
        // from m_LiveCategoryKeys, same concept. Direct user design (2026-09-18): "I should be able to
        // drag the entire library to my change list... this means the same thing as (All)" - i.e.
        // dynamically tracks every currently AND later pending file in this one library, never a
        // snapshot, exactly like "(All)" does for a whole depot but scoped to one library. Absorbs any
        // already-tracked category/resource/explicit entries this library covers (same "superset
        // includes the subset" rule as m_LiveCategoryKeys). Degrades ONE level on edit: the library
        // reference drops, every OTHER category becomes its own live m_LiveCategoryKeys entry, and only
        // the specific category the edited row belonged to degrades further (same rule, recursively).
        std::vector<std::wstring>  m_LiveLibraryKeys;
    };

    struct source_control_panel_state
    {
        // No more "Default Changelist" entry (direct user decision: the Depot -> Library -> Category
        // auto-tree below now covers that role entirely) - starts empty; a real entry only exists
        // once the user actually creates one for the rare case the auto-categories don't fit.
        std::vector<sc_changelist> m_Changelists{};
        int                         m_ActiveChangelist = -1;

        // Multi-select, scoped to whichever list (the auto-tree, or a changelist's own file list) was
        // clicked last - same unordered_set/order/anchor idiom already proven in
        // E10_asset_browser_files_tab.h, a fresh instance here rather than reused from there (Phase 4C
        // correction: this panel owns its own interaction state end to end). Also doubles as the
        // "Commit Selected" input, at whatever granularity - a whole category (via its own "Select
        // All"), an arbitrary sub-selection, or a single item - direct user requirement: "should still
        // be flexible."
        std::unordered_set<std::wstring> m_MultiSelected;
        std::vector<std::wstring>         m_MultiSelectOrder;
        std::wstring                      m_MultiSelectAnchor;

        // Set ONLY by a resource-group or category FOLDER's own drag source (RenderSourceControlFolder/
        // RenderSourceControlCategoryFolder/the top-level category drag), never by a plain row-level
        // drag - tags the drag-in-flight as "a whole concept" (a resource, or a whole category) rather
        // than a flat file selection, so the drop handler assigns it to the target changelist as a
        // LIVE reference (m_LiveGroupKeys/m_LiveCategoryKeys) instead of a snapshot of individual keys.
        // Reset by every OTHER drag source at its own drag-start so a stale value never leaks into an
        // unrelated drag, and consumed (reset again) by the drop handler once read. m_ImplicitChildKey
        // covers the one real nested-folder case in this tree - dragging the OUTER "Resources" category
        // must also carry its own nested "Scenes & Levels" child along (real bug found live: "If I drag
        // the resources[,] the Scenes & Levels stay behind" - Resources is now the visual PARENT of
        // Scenes & Levels, so dragging it should behave like dragging any parent folder, taking its
        // child with it, matching "the superset already includes the subset").
        struct drag_scope { enum class kind { Resource, Category, Library } m_Kind; std::wstring m_Key; std::optional<std::wstring> m_ImplicitChildKey; };
        std::optional<drag_scope>        m_DragSourceScope;

        std::string m_StatusLine;
        // Per-depot, not one shared buffer - keeps a partially-typed name intact if the user picks a
        // different depot and comes back, and (before this) a single shared buffer showed identical,
        // confusing text in every depot's own field at once. Keyed by depot key, default-constructed
        // (zero-filled) on first access.
        std::unordered_map<std::string, std::array<char, 128>> m_NewChangelistNameByDepot;

        // Which depot's own changelists the Changelists panel currently shows - direct user design:
        // "you select a depot and from that depot it should give you all the change lists... the
        // user can only set one [set] at a time not all the different depots['] change lists." Empty
        // means "nothing picked yet" (RenderSourceControlPanel auto-selects a sensible default - the
        // one depot if there's only one, else the first alphabetically - so the common single-depot
        // case needs no extra click).
        std::string m_SelectedChangelistDepotKey;

        // -2 (a sentinel distinct from -1 = "nothing active" and >=0 = "a real S.m_Changelists
        // index") means the "(All)" pseudo-entry is active - direct user design: the changelist list
        // should offer "the actual depot which mean all" alongside real named changelists, a one-
        // click way to comment-and-commit EVERY pending file in the depot without first sorting it
        // into a named changelist. Its own comment buffer is per-depot for the same reason
        // m_NewChangelistNameByDepot is.
        std::unordered_map<std::string, std::array<char, 4096>> m_AllDepotCommentByDepot;

        // Left/right split, user-resizable via a draggable divider - direct user request: "the
        // window should have a splitter between the right and the left sections, just like the
        // other windows" (the Assets view named as the concrete example). Negative = "not yet
        // initialized for this instance", same sentinel convention e10::assert_browser's own
        // m_SplitSize1 already uses for the identical purpose.
        float m_SplitSize = -1.0f;

        // Revision-gated row cache (direct user report: rebuilding this list was "taking a long
        // time" - it was being rebuilt from scratch, across every open library, EVERY FRAME the
        // panel was visible, the exact class of bug already fixed once for files_tab's own badges -
        // "there is no reason to sync the FPS of the editor with the computation"). Rebuilt only when
        // e10::source_control::SourceControlRevision() has actually changed since the last time this
        // panel applied it, same idiom files_tab already uses.
        std::vector<sc_panel_row> m_CachedRows;
        std::uint64_t              m_LastAppliedRevision = static_cast<std::uint64_t>(-1);

        // Minimum-visible-duration debounce for the loading spinner: a scan of a small repo can
        // start and finish inside a single frame, which would make IsScanInProgress() true for zero
        // perceivable time - the user would never actually see it, even though it's genuinely
        // working. Bumped forward every frame a scan is really in progress; the spinner stays drawn
        // until this time passes, so even an instant scan flashes it at least once.
        double m_SpinnerVisibleUntil = 0.0;

        // Deferred "Undo Changes" confirm request - a real Dear ImGui pitfall found live-testing the
        // Level Tree's own new SC Revert feature (see documentation/E29_LevelSceneEditor/level_tree_source_control_column.md):
        // clicking a MenuItem closes ITS OWN enclosing popup ("SCRowContext") the same way a real
        // right-click menu always does, so a BeginPopupModal nested directly inside it only ever
        // rendered for the single frame of the click - the next frame "SCRowContext" isn't reached,
        // Dear ImGui treats the OpenPopup'd id as abandoned, and force-closes it before the user can
        // ever see it. This confirm modal was never actually visible before this fix - nobody had
        // clicked through it end to end since it shipped. Fixed by only recording a request at the
        // MenuItem; the real OpenPopup/BeginPopupModal pair lives at this flag's own render call site
        // (RenderSourceControlPanel's own end, always reached every frame).
        bool m_bUndoChangesConfirmPending = false;
    };
    inline source_control_panel_state g_SourceControlPanel;

    inline void SourceControlSelectSingle(const std::wstring& Key) noexcept
    {
        auto& S = g_SourceControlPanel;
        S.m_MultiSelected.clear();
        S.m_MultiSelectOrder.clear();
        S.m_MultiSelected.insert(Key);
        S.m_MultiSelectOrder.push_back(Key);
        S.m_MultiSelectAnchor = Key;
    }

    inline void SourceControlToggleMultiSelect(const std::wstring& Key) noexcept
    {
        auto& S = g_SourceControlPanel;
        if (S.m_MultiSelected.erase(Key))
        {
            S.m_MultiSelectOrder.erase(std::remove(S.m_MultiSelectOrder.begin(), S.m_MultiSelectOrder.end(), Key), S.m_MultiSelectOrder.end());
        }
        else
        {
            S.m_MultiSelected.insert(Key);
            S.m_MultiSelectOrder.push_back(Key);
        }
        S.m_MultiSelectAnchor = Key;
    }

    inline void SourceControlHandleRowClick(const std::vector<std::wstring>& OrderedKeys, const std::wstring& Key) noexcept
    {
        auto& S = g_SourceControlPanel;
        ImGuiIO& IO = ImGui::GetIO();
        if (IO.KeyCtrl)
        {
            SourceControlToggleMultiSelect(Key);
            return;
        }
        if (IO.KeyShift && !S.m_MultiSelectAnchor.empty())
        {
            auto ItAnchor = std::find(OrderedKeys.begin(), OrderedKeys.end(), S.m_MultiSelectAnchor);
            auto ItTarget = std::find(OrderedKeys.begin(), OrderedKeys.end(), Key);
            if (ItAnchor != OrderedKeys.end() && ItTarget != OrderedKeys.end())
            {
                if (ItAnchor > ItTarget) std::swap(ItAnchor, ItTarget);
                S.m_MultiSelected.clear();
                S.m_MultiSelectOrder.clear();
                for (auto It = ItAnchor; It <= ItTarget; ++It)
                {
                    S.m_MultiSelected.insert(*It);
                    S.m_MultiSelectOrder.push_back(*It);
                }
                return;
            }
        }
        SourceControlSelectSingle(Key);
    }

    // Which custom changelist (index into m_Changelists) currently owns Row, or -1 meaning "not in
    // any custom changelist" - i.e. still shown in the Depot -> Library -> Category auto-tree. No
    // more "index 0 is Default" special case (there is no Default entry anymore). Takes the whole Row
    // (not just its Key) because live-group membership needs Row's own classification to compute the
    // SAME global group key a changelist's m_LiveGroupKeys entry would carry.
    inline int SourceControlChangelistOf(const sc_panel_row& Row) noexcept
    {
        auto& S = g_SourceControlPanel;
        for (std::size_t i = 0; i < S.m_Changelists.size(); ++i)
            if (std::find(S.m_Changelists[i].m_Keys.begin(), S.m_Changelists[i].m_Keys.end(), Row.m_Key) != S.m_Changelists[i].m_Keys.end())
                return static_cast<int>(i);

        const auto Class = ClassifyPendingPath(Row.m_RelativePath);

        const auto LibraryKey = SourceControlGlobalLibraryKey(Row.m_Library);
        for (std::size_t i = 0; i < S.m_Changelists.size(); ++i)
            if (std::find(S.m_Changelists[i].m_LiveLibraryKeys.begin(), S.m_Changelists[i].m_LiveLibraryKeys.end(), LibraryKey) != S.m_Changelists[i].m_LiveLibraryKeys.end())
                return static_cast<int>(i);

        const auto CategoryKey = SourceControlGlobalCategoryKey(Row.m_Library, Class.m_Category);
        for (std::size_t i = 0; i < S.m_Changelists.size(); ++i)
            if (std::find(S.m_Changelists[i].m_LiveCategoryKeys.begin(), S.m_Changelists[i].m_LiveCategoryKeys.end(), CategoryKey) != S.m_Changelists[i].m_LiveCategoryKeys.end())
                return static_cast<int>(i);

        if (!Class.m_HexInstance.empty())
        {
            const auto GlobalKey = SourceControlGlobalGroupKey(Row.m_Library, Class.m_TypeNameLower, Class.m_HexInstance);
            for (std::size_t i = 0; i < S.m_Changelists.size(); ++i)
                if (std::find(S.m_Changelists[i].m_LiveGroupKeys.begin(), S.m_Changelists[i].m_LiveGroupKeys.end(), GlobalKey) != S.m_Changelists[i].m_LiveGroupKeys.end())
                    return static_cast<int>(i);
        }
        return -1;
    }

    // ChangelistIndex < 0 removes Key from every custom changelist's EXPLICIT list (back to the
    // auto-tree) without re-inserting anywhere. Only ever touches m_Keys - callers dealing with a row
    // that might be live-group-owned should go through SourceControlRemoveRowFromChangelist instead,
    // which knows how to flatten a group properly.
    inline void SourceControlAssignToChangelist(const std::wstring& Key, int ChangelistIndex) noexcept
    {
        auto& S = g_SourceControlPanel;
        for (auto& CL : S.m_Changelists)
            CL.m_Keys.erase(std::remove(CL.m_Keys.begin(), CL.m_Keys.end(), Key), CL.m_Keys.end());
        if (ChangelistIndex >= 0 && ChangelistIndex < static_cast<int>(S.m_Changelists.size()))
            S.m_Changelists[ChangelistIndex].m_Keys.push_back(Key);
    }

    // Assigns a WHOLE LIBRARY to a changelist as a LIVE reference - direct user design (2026-09-18):
    // "I should be able to drag the entire library to my change list... this means the same thing as
    // (All)" - dynamically tracks every currently and later pending file in this one library.
    // Absorbs (removes) any category/resource-level live reference AND explicit key this library
    // already covers, in every changelist - "the superset already includes the subset." Defined before
    // SourceControlAssignGroupToChangelist/SourceControlAssignCategoryToChangelist (which now call the
    // auto-promotion check right below) so both can call it directly.
    inline void SourceControlAssignLibraryToChangelist(const std::vector<sc_panel_row>& AllRows, const std::wstring& LibraryKey, int ChangelistIndex) noexcept
    {
        auto& S = g_SourceControlPanel;
        for (auto& CL : S.m_Changelists)
            CL.m_LiveLibraryKeys.erase(std::remove(CL.m_LiveLibraryKeys.begin(), CL.m_LiveLibraryKeys.end(), LibraryKey), CL.m_LiveLibraryKeys.end());

        for (auto& Row : AllRows)
        {
            if (SourceControlGlobalLibraryKey(Row.m_Library) != LibraryKey) continue;
            const auto Class = ClassifyPendingPath(Row.m_RelativePath);

            for (auto& CL : S.m_Changelists)
            {
                CL.m_Keys.erase(std::remove(CL.m_Keys.begin(), CL.m_Keys.end(), Row.m_Key), CL.m_Keys.end());
                const auto CategoryKey = SourceControlGlobalCategoryKey(Row.m_Library, Class.m_Category);
                CL.m_LiveCategoryKeys.erase(std::remove(CL.m_LiveCategoryKeys.begin(), CL.m_LiveCategoryKeys.end(), CategoryKey), CL.m_LiveCategoryKeys.end());
                if (!Class.m_HexInstance.empty())
                {
                    const auto ResourceKey = SourceControlGlobalGroupKey(Row.m_Library, Class.m_TypeNameLower, Class.m_HexInstance);
                    CL.m_LiveGroupKeys.erase(std::remove(CL.m_LiveGroupKeys.begin(), CL.m_LiveGroupKeys.end(), ResourceKey), CL.m_LiveGroupKeys.end());
                }
            }
        }

        if (ChangelistIndex >= 0 && ChangelistIndex < static_cast<int>(S.m_Changelists.size()))
            S.m_Changelists[ChangelistIndex].m_LiveLibraryKeys.push_back(LibraryKey);
    }

    // After a resource/category gets assigned live to a changelist, checks whether EVERY pending row
    // in the owning library now resolves to that SAME changelist (whether via that assignment, an
    // earlier one, or an explicit key) - if so, auto-promotes the whole thing to ONE
    // m_LiveLibraryKeys reference. Direct user design (2026-09-18): "once all the sub-folders of the
    // library are in the 'Files In' they should all be replaced by the library name (because that is
    // the equivalent of them)... if then you open the library name you will find all the folders as
    // expected" - so this isn't just a display grouping, it's a real promotion (SourceControlAssignLibraryToChangelist
    // already renders identically whether "genuinely" live or auto-promoted, and still shows every
    // category nested inside when opened).
    inline void SourceControlPromoteToLibraryIfFullyCovered(const std::vector<sc_panel_row>& AllRows, e10::library::guid LibraryGuid, int ChangelistIndex) noexcept
    {
        if (ChangelistIndex < 0) return;
        bool bAnyRow = false;
        for (auto& Row : AllRows)
        {
            if (Row.m_Library != LibraryGuid) continue;
            bAnyRow = true;
            if (SourceControlChangelistOf(Row) != ChangelistIndex) return;
        }
        if (!bAnyRow) return;
        SourceControlAssignLibraryToChangelist(AllRows, SourceControlGlobalLibraryKey(LibraryGuid), ChangelistIndex);
    }

    // Assigns a WHOLE CATEGORY within one library to a changelist as a LIVE reference - the same
    // concept as SourceControlAssignGroupToChangelist, one level up. Direct user design (2026-09-18):
    // "the superset already includes the subset... we just want the superset" - sweeps away every
    // resource-level live reference AND explicit key this category already covers, in every
    // changelist, so a previously-separate "Scene 1" folder collapses into the new, broader category
    // reference instead of being tracked twice. Defined before SourceControlAssignGroupToChangelist
    // (which now calls SourceControlPromoteToCategoryIfFullyCovered, defined right below) so both can
    // call it directly.
    inline void SourceControlAssignCategoryToChangelist(const std::vector<sc_panel_row>& AllRows, const std::wstring& CategoryKey, int ChangelistIndex) noexcept
    {
        auto& S = g_SourceControlPanel;
        for (auto& CL : S.m_Changelists)
            CL.m_LiveCategoryKeys.erase(std::remove(CL.m_LiveCategoryKeys.begin(), CL.m_LiveCategoryKeys.end(), CategoryKey), CL.m_LiveCategoryKeys.end());

        e10::library::guid OwningLibrary{};
        for (auto& Row : AllRows)
        {
            const auto Class = ClassifyPendingPath(Row.m_RelativePath);
            if (SourceControlGlobalCategoryKey(Row.m_Library, Class.m_Category) != CategoryKey) continue;
            OwningLibrary = Row.m_Library;

            for (auto& CL : S.m_Changelists)
            {
                CL.m_Keys.erase(std::remove(CL.m_Keys.begin(), CL.m_Keys.end(), Row.m_Key), CL.m_Keys.end());
                if (!Class.m_HexInstance.empty())
                {
                    const auto ResourceKey = SourceControlGlobalGroupKey(Row.m_Library, Class.m_TypeNameLower, Class.m_HexInstance);
                    CL.m_LiveGroupKeys.erase(std::remove(CL.m_LiveGroupKeys.begin(), CL.m_LiveGroupKeys.end(), ResourceKey), CL.m_LiveGroupKeys.end());
                }
            }
        }

        if (ChangelistIndex >= 0 && ChangelistIndex < static_cast<int>(S.m_Changelists.size()))
            S.m_Changelists[ChangelistIndex].m_LiveCategoryKeys.push_back(CategoryKey);
        SourceControlPromoteToLibraryIfFullyCovered(AllRows, OwningLibrary, ChangelistIndex);
    }

    // After a resource gets assigned/left live in a changelist, checks whether EVERY pending row in
    // its OWN (library, category) now resolves to that SAME changelist - if so, auto-promotes them
    // all into ONE m_LiveCategoryKeys reference (which itself may cascade into a full library
    // promotion too). Real bug found live: "sections of the folder seem to have been left behind" -
    // degrading a live CATEGORY (or a live LIBRARY, whose own degrade explodes the dragged row's own
    // category straight to per-resource entries) scatters that category's OTHER resources into N
    // independent live-group references and NEVER re-consolidates them, even once every one of them
    // ends up back in the same changelist together - this is the missing, symmetric counterpart to
    // SourceControlPromoteToLibraryIfFullyCovered, one level down.
    inline void SourceControlPromoteToCategoryIfFullyCovered(const std::vector<sc_panel_row>& AllRows, e10::library::guid LibraryGuid, sc_category Category, int ChangelistIndex) noexcept
    {
        if (ChangelistIndex < 0) return;
        bool bAnyRow = false;
        for (auto& Row : AllRows)
        {
            if (Row.m_Library != LibraryGuid) continue;
            if (ClassifyPendingPath(Row.m_RelativePath).m_Category != Category) continue;
            bAnyRow = true;
            if (SourceControlChangelistOf(Row) != ChangelistIndex) return;
        }
        if (!bAnyRow) return;
        SourceControlAssignCategoryToChangelist(AllRows, SourceControlGlobalCategoryKey(LibraryGuid, Category), ChangelistIndex);
    }

    // Assigns a WHOLE resource (by its global group key) to a changelist as a LIVE reference - direct
    // user design: "drags the 'Scene 1' folder... he does not care about the actual files there."
    // Removes the live reference from every OTHER changelist first (a resource can only be live-
    // tracked in one place), and also sweeps every explicit m_Keys entry belonging to this SAME
    // resource out of every changelist - otherwise re-dragging a previously-flattened group would
    // leave stale duplicate flat entries sitting alongside the new live one.
    inline void SourceControlAssignGroupToChangelist(const std::vector<sc_panel_row>& AllRows, const std::wstring& GlobalGroupKey, int ChangelistIndex) noexcept
    {
        auto& S = g_SourceControlPanel;
        for (auto& CL : S.m_Changelists)
            CL.m_LiveGroupKeys.erase(std::remove(CL.m_LiveGroupKeys.begin(), CL.m_LiveGroupKeys.end(), GlobalGroupKey), CL.m_LiveGroupKeys.end());

        e10::library::guid OwningLibrary{};
        sc_category OwningCategory = sc_category::Resources;
        for (auto& Row : AllRows)
        {
            const auto Class = ClassifyPendingPath(Row.m_RelativePath);
            if (Class.m_HexInstance.empty()) continue;
            if (SourceControlGlobalGroupKey(Row.m_Library, Class.m_TypeNameLower, Class.m_HexInstance) != GlobalGroupKey) continue;
            OwningLibrary = Row.m_Library;
            OwningCategory = Class.m_Category;
            for (auto& CL : S.m_Changelists)
                CL.m_Keys.erase(std::remove(CL.m_Keys.begin(), CL.m_Keys.end(), Row.m_Key), CL.m_Keys.end());
        }

        if (ChangelistIndex >= 0 && ChangelistIndex < static_cast<int>(S.m_Changelists.size()))
            S.m_Changelists[ChangelistIndex].m_LiveGroupKeys.push_back(GlobalGroupKey);
        SourceControlPromoteToCategoryIfFullyCovered(AllRows, OwningLibrary, OwningCategory, ChangelistIndex);
        SourceControlPromoteToLibraryIfFullyCovered(AllRows, OwningLibrary, ChangelistIndex);
    }

    // Removes exactly Row from whichever changelist currently contains it, explicit-key, live-library,
    // live-category, or live-group membership alike - direct user design: editing a live concept's own
    // membership one file at a time "flattens" it, since it no longer matches "the whole
    // resource"/"the whole category"/"the whole library" as a concept - and ONLY that edit ever
    // flattens anything ("the only way that a folder should be flatten is if I remove a file/folder
    // from inside the folder"). Each level degrades by exactly ONE step rather than exploding straight
    // to flat files: dropping a live LIBRARY turns every OTHER category in it into its own live
    // category reference, except the one Row belonged to, which degrades ONE level further (turns every
    // OTHER resource in THAT category into its own live resource reference, except the one Row
    // belonged to, which flattens fully into explicit keys) - the same rule applied recursively.
    inline void SourceControlRemoveRowFromChangelist(const std::vector<sc_panel_row>& AllRows, const sc_panel_row& Row) noexcept
    {
        auto& S = g_SourceControlPanel;
        const int Index = SourceControlChangelistOf(Row);
        if (Index < 0) return;
        auto& CL = S.m_Changelists[Index];

        if (std::find(CL.m_Keys.begin(), CL.m_Keys.end(), Row.m_Key) != CL.m_Keys.end())
        {
            CL.m_Keys.erase(std::remove(CL.m_Keys.begin(), CL.m_Keys.end(), Row.m_Key), CL.m_Keys.end());
            return;
        }

        const auto Class = ClassifyPendingPath(Row.m_RelativePath);
        const auto LibraryKey  = SourceControlGlobalLibraryKey(Row.m_Library);
        const auto CategoryKey = SourceControlGlobalCategoryKey(Row.m_Library, Class.m_Category);
        const std::wstring ResourceKey = Class.m_HexInstance.empty() ? std::wstring{}
            : SourceControlGlobalGroupKey(Row.m_Library, Class.m_TypeNameLower, Class.m_HexInstance);

        if (auto It = std::find(CL.m_LiveLibraryKeys.begin(), CL.m_LiveLibraryKeys.end(), LibraryKey); It != CL.m_LiveLibraryKeys.end())
        {
            CL.m_LiveLibraryKeys.erase(It);
            std::unordered_set<std::wstring> SeenCategoryKeys;
            std::unordered_set<std::wstring> SeenResourceKeys;
            for (auto& OtherRow : AllRows)
            {
                if (OtherRow.m_Key == Row.m_Key) continue;
                if (OtherRow.m_Library != Row.m_Library) continue;
                const auto OtherClass = ClassifyPendingPath(OtherRow.m_RelativePath);
                if (OtherClass.m_Category != Class.m_Category)
                {
                    // A different category - one level down, its own live category reference, not
                    // split any further.
                    const auto OtherCategoryKey = SourceControlGlobalCategoryKey(OtherRow.m_Library, OtherClass.m_Category);
                    if (SeenCategoryKeys.insert(OtherCategoryKey).second) CL.m_LiveCategoryKeys.push_back(OtherCategoryKey);
                    continue;
                }
                // Same category as Row - degrade it exactly like a live-category removal would.
                if (OtherClass.m_HexInstance.empty()) { CL.m_Keys.push_back(OtherRow.m_Key); continue; }
                const auto OtherResourceKey = SourceControlGlobalGroupKey(OtherRow.m_Library, OtherClass.m_TypeNameLower, OtherClass.m_HexInstance);
                if (OtherResourceKey == ResourceKey) { CL.m_Keys.push_back(OtherRow.m_Key); continue; }
                if (SeenResourceKeys.insert(OtherResourceKey).second) CL.m_LiveGroupKeys.push_back(OtherResourceKey);
            }
            return;
        }

        if (auto It = std::find(CL.m_LiveCategoryKeys.begin(), CL.m_LiveCategoryKeys.end(), CategoryKey); It != CL.m_LiveCategoryKeys.end())
        {
            CL.m_LiveCategoryKeys.erase(It);
            std::unordered_set<std::wstring> SeenResourceKeys;
            for (auto& OtherRow : AllRows)
            {
                if (OtherRow.m_Key == Row.m_Key) continue;
                if (OtherRow.m_Library != Row.m_Library) continue;
                const auto OtherClass = ClassifyPendingPath(OtherRow.m_RelativePath);
                if (OtherClass.m_Category != Class.m_Category) continue;

                if (OtherClass.m_HexInstance.empty()) { CL.m_Keys.push_back(OtherRow.m_Key); continue; }
                const auto OtherResourceKey = SourceControlGlobalGroupKey(OtherRow.m_Library, OtherClass.m_TypeNameLower, OtherClass.m_HexInstance);
                if (OtherResourceKey == ResourceKey) { CL.m_Keys.push_back(OtherRow.m_Key); continue; }
                if (SeenResourceKeys.insert(OtherResourceKey).second) CL.m_LiveGroupKeys.push_back(OtherResourceKey);
            }
            return;
        }

        if (Class.m_HexInstance.empty()) return; // not explicit, not live at any level, not resource-
                                                  // shaped - nothing left to flatten (shouldn't
                                                  // normally happen).
        CL.m_LiveGroupKeys.erase(std::remove(CL.m_LiveGroupKeys.begin(), CL.m_LiveGroupKeys.end(), ResourceKey), CL.m_LiveGroupKeys.end());
        for (auto& OtherRow : AllRows)
        {
            if (OtherRow.m_Key == Row.m_Key) continue;
            const auto OtherClass = ClassifyPendingPath(OtherRow.m_RelativePath);
            if (OtherClass.m_HexInstance.empty()) continue;
            if (SourceControlGlobalGroupKey(OtherRow.m_Library, OtherClass.m_TypeNameLower, OtherClass.m_HexInstance) != ResourceKey) continue;
            CL.m_Keys.push_back(OtherRow.m_Key);
        }
    }

    // Detaches EVERY row currently matching a Category/Resource scope from wherever it actually
    // lives right now - explicit key, live-resource, live-category, OR a BROADER live-category/
    // live-library reference covering it - by routing each one through
    // SourceControlRemoveRowFromChangelist (which already knows how to degrade whatever level
    // actually covers a row). Real bug found live: "I can drag sub folder from the Pending list to
    // the Files in... I can not do the opposite" - dragging a nested folder (e.g. "Scene 1" sitting
    // inside a live "Resources" category, or "Resources" sitting inside a live library) back out via
    // SourceControlAssignGroupToChangelist(Key, -1)/SourceControlAssignCategoryToChangelist(Key, -1)
    // was a silent no-op, because m_LiveGroupKeys/m_LiveCategoryKeys never actually held this item's
    // OWN key when a BROADER reference was what covered it instead - only the "main library" level
    // ever really worked, because a library is never itself nested under anything broader. Library-
    // kind scopes don't need this (already always their own top-level entry, nothing broader exists
    // above them) - only used for Category/Resource kinds.
    inline void SourceControlDetachScopeRows(const std::vector<sc_panel_row>& AllRows, const source_control_panel_state::drag_scope& Scope) noexcept
    {
        std::vector<const sc_panel_row*> Matching;
        for (auto& Row : AllRows)
        {
            const auto Class = ClassifyPendingPath(Row.m_RelativePath);
            bool bMatches = false;
            switch (Scope.m_Kind)
            {
                case source_control_panel_state::drag_scope::kind::Resource:
                    bMatches = !Class.m_HexInstance.empty() && SourceControlGlobalGroupKey(Row.m_Library, Class.m_TypeNameLower, Class.m_HexInstance) == Scope.m_Key;
                    break;
                case source_control_panel_state::drag_scope::kind::Category:
                {
                    const auto RowCategoryKey = SourceControlGlobalCategoryKey(Row.m_Library, Class.m_Category);
                    bMatches = (RowCategoryKey == Scope.m_Key) || (Scope.m_ImplicitChildKey.has_value() && RowCategoryKey == *Scope.m_ImplicitChildKey);
                    break;
                }
                case source_control_panel_state::drag_scope::kind::Library:
                    bMatches = SourceControlGlobalLibraryKey(Row.m_Library) == Scope.m_Key;
                    break;
            }
            if (bMatches) Matching.push_back(&Row);
        }
        for (auto* Row : Matching)
            SourceControlRemoveRowFromChangelist(AllRows, *Row);
    }

    // Guards a drop onto a changelist's own file box - a changelist belongs to exactly ONE depot
    // (direct user requirement, and already enforced by the context menu's "Add to Changelist"
    // submenu only offering same-depot lists) - real bug found live: dragging from a DIFFERENT depot
    // was never checked at all, letting a changelist end up with files from two repos it could never
    // actually commit together. Stale keys (a row no longer in AllRows) are skipped, not treated as a
    // mismatch - they can't block a legitimate same-depot drop. Returns the FIRST mismatching row (so
    // the caller can name it - real user confusion found live: two different libraries can have a
    // pending file at the identical relative path, e.g. each one's own "library.config.txt", making a
    // genuine cross-depot mismatch look like an unexplained rejection without naming which file it
    // actually was) or nullptr when everything matches.
    inline const sc_panel_row* SourceControlFindDepotMismatch(const std::vector<sc_panel_row>& AllRows, const std::vector<std::wstring>& Keys, const std::string& DepotKey) noexcept
    {
        for (auto& Key : Keys)
        {
            auto It = std::find_if(AllRows.begin(), AllRows.end(), [&](const sc_panel_row& R) { return R.m_Key == Key; });
            if (It == AllRows.end()) continue;
            if (SourceControlDepotKeyAndName(It->m_Library).first != DepotKey) return &*It;
        }
        return nullptr;
    }

    // Same depot guard as SourceControlFindDepotMismatch, but for a resource/category/library-SCOPED
    // drag (m_DragSourceScope) - checks the scope's own key directly against a matching row's depot,
    // instead of going through m_MultiSelectOrder. A scoped drag's real assignment
    // (SourceControlAssign*ToChangelist) is driven entirely by the scope's key re-scanning AllRows, NOT
    // by m_MultiSelectOrder - m_MultiSelectOrder is only a highlight/display side effect of starting
    // the drag, updated purely on IsItemActive()/IsMouseDragging() timing, so checking it here risked
    // testing stale or already-superseded content instead of what's actually about to be assigned
    // (real bug found live: dragging "Assets" right after a successful "Resources" drag into the same
    // changelist was wrongly rejected as cross-depot). Every row matching one scope's key necessarily
    // shares one library (the key format embeds the library hex), so checking just the first match is
    // exact, not a sample. Returns that representative row on mismatch (for naming it), or nullptr.
    inline const sc_panel_row* SourceControlFindScopeDepotMismatch(const std::vector<sc_panel_row>& AllRows, const source_control_panel_state::drag_scope& Scope, const std::string& DepotKey) noexcept
    {
        for (auto& Row : AllRows)
        {
            const auto Class = ClassifyPendingPath(Row.m_RelativePath);
            bool bMatches = false;
            switch (Scope.m_Kind)
            {
                case source_control_panel_state::drag_scope::kind::Resource:
                    bMatches = !Class.m_HexInstance.empty() && SourceControlGlobalGroupKey(Row.m_Library, Class.m_TypeNameLower, Class.m_HexInstance) == Scope.m_Key;
                    break;
                case source_control_panel_state::drag_scope::kind::Category:
                {
                    const auto RowCategoryKey = SourceControlGlobalCategoryKey(Row.m_Library, Class.m_Category);
                    bMatches = (RowCategoryKey == Scope.m_Key) || (Scope.m_ImplicitChildKey.has_value() && RowCategoryKey == *Scope.m_ImplicitChildKey);
                    break;
                }
                case source_control_panel_state::drag_scope::kind::Library:
                    bMatches = SourceControlGlobalLibraryKey(Row.m_Library) == Scope.m_Key;
                    break;
            }
            if (!bMatches) continue;
            return (SourceControlDepotKeyAndName(Row.m_Library).first == DepotKey) ? nullptr : &Row;
        }
        return nullptr; // nothing currently matches this scope - nothing to reject
    }

    // Aggregates GetAllPendingChanges across EVERY currently open library - "whole project" scope.
    // Same e10::g_LibMgr.m_mLibraryDB iteration idiom PumpSourceControlIdleWork already uses.
    inline std::vector<sc_panel_row> BuildSourceControlRows() noexcept
    {
        std::vector<sc_panel_row> Rows;
        for (auto& Lib : e10::g_LibMgr.m_mLibraryDB)
        {
            const auto& RootPath = Lib.second->m_Library.m_Path;
            for (auto& Entry : e10::source_control::GetAllPendingChanges(RootPath))
            {
                sc_panel_row Row;
                Row.m_Library      = Lib.first;
                Row.m_RootPath     = RootPath;
                Row.m_RelativePath = Entry.m_RelativePath;
                Row.m_Key          = SourceControlRowKey(Lib.first, Entry.m_RelativePath);
                Row.m_Status       = Entry.m_Status;
                Row.m_Lock         = Entry.m_Lock;
                Rows.push_back(std::move(Row));
            }
        }
        return Rows;
    }

    // Resolves a resource's own display name given its TypeName (lowercase, as parsed from a path)
    // and hex instance value, within a specific library - "one scene should be one folder" (direct
    // user design) needs a real name for that folder, not a bare hex guid. Looks up the plugin
    // registered under that type name (case-insensitive linear scan over m_lPlugins - cheap, ~15
    // plugins) to get its real type_guid, then the resource's own stored Name via getInfo. Falls back
    // to "<TypeName> <hex>" when either step can't resolve (a deleted info.txt, an unregistered/
    // renamed plugin) - never silently drops a real pending file for lack of a pretty name.
    inline std::string SourceControlResolveResourceName(e10::library::guid LibraryGuid, const std::wstring& TypeNameLower, const std::wstring& HexInstance) noexcept
    {
        const std::string Fallback = std::format("{} {}", xstrtool::To(TypeNameLower), xstrtool::To(HexInstance));
        if (HexInstance.empty()) return Fallback;

        xresource::type_guid TypeGuid{};
        bool bFoundType = false;
        for (auto& Plugin : e10::g_LibMgr.m_AssetPluginsDB.m_lPlugins)
        {
            std::wstring Lower = xstrtool::To(Plugin.m_TypeName);
            std::transform(Lower.begin(), Lower.end(), Lower.begin(), [](wchar_t C) { return static_cast<wchar_t>(std::towlower(C)); });
            if (Lower == TypeNameLower) { TypeGuid = Plugin.m_TypeGUID; bFoundType = true; break; }
        }
        if (!bFoundType) return Fallback;

        const auto Instance = std::strtoull(xstrtool::To(HexInstance).c_str(), nullptr, 16);
        std::string Name;
        e10::g_LibMgr.getInfo(LibraryGuid, xresource::full_guid{ .m_Instance = { Instance }, .m_Type = TypeGuid }, [&](const xresource_pipeline::info& Info)
        {
            if (!Info.m_Name.empty()) Name = Info.m_Name;
        });
        return Name.empty() ? Fallback : Name;
    }

    // One resource's own pending files, grouped together under its real name (direct user design:
    // "one scene should be one folder") - a single Scene/Material/etc. resource can have several
    // simultaneously-pending files (info.txt, Descriptor.txt, compiled binary, dependencies.txt) that
    // would otherwise show as disconnected rows with no visible connection to each other.
    struct sc_resource_group
    {
        std::wstring                       m_GroupKey; // "TypeLower|Hex" - unique within one library
        std::string                        m_DisplayName;
        std::vector<const sc_panel_row*>  m_Rows;
    };

    struct sc_category_bucket
    {
        sc_category                        m_Category;
        std::vector<sc_resource_group>    m_ResourceGroups; // Scenes & Levels / Resources - grouped by owning resource
        std::vector<const sc_panel_row*>  m_UngroupedRows;   // Assets / Project Files - no owning-resource concept to group by

        std::size_t RowCount() const noexcept
        {
            std::size_t N = m_UngroupedRows.size();
            for (auto& G : m_ResourceGroups) N += G.m_Rows.size();
            return N;
        }
    };

    struct sc_library_group
    {
        e10::library::guid                m_Library;
        std::string                        m_DisplayName;
        std::array<sc_category_bucket, sc_category_count_v> m_Categories{};
    };

    struct sc_depot_group
    {
        std::string                        m_Key;
        std::string                        m_DisplayName;
        std::vector<sc_library_group>      m_Libraries;
    };

    // Groups every row NOT already assigned to a custom changelist (same "in Default" predicate the
    // manual-changelist system below already uses - SourceControlChangelistOf's own -1/0-means-
    // unassigned rule) into Depot -> Resource Library -> Category -> (Resource, for Scenes & Levels/
    // Resources) or a flat list (for Assets/Project Files). This IS the replacement for the old flat
    // "Default Changelist" display - direct user requirement: the common case needs zero manual
    // sorting, a file only leaves this auto-tree once explicitly moved into a custom changelist via
    // the existing "Add to Changelist" context menu. Cheap to rebuild every call - a handful of
    // pending files in practice, no separate revision-gate needed beyond BuildSourceControlRows' own
    // caller-side caching.
    inline std::vector<sc_depot_group> BuildDepotGroups(const std::vector<sc_panel_row>& Rows) noexcept
    {
        std::vector<sc_depot_group> Depots;
        for (auto& Row : Rows)
        {
            if (SourceControlChangelistOf(Row) >= 0) continue; // already in a custom changelist

            auto [DepotKey, DepotName] = SourceControlDepotKeyAndName(Row.m_Library);

            auto DepotIt = std::find_if(Depots.begin(), Depots.end(), [&](const sc_depot_group& D) { return D.m_Key == DepotKey; });
            if (DepotIt == Depots.end())
            {
                Depots.push_back(sc_depot_group{ DepotKey, DepotName, {} });
                DepotIt = std::prev(Depots.end());
            }

            auto LibIt = std::find_if(DepotIt->m_Libraries.begin(), DepotIt->m_Libraries.end(), [&](const sc_library_group& L) { return L.m_Library == Row.m_Library; });
            if (LibIt == DepotIt->m_Libraries.end())
            {
                sc_library_group NewLib;
                NewLib.m_Library     = Row.m_Library;
                NewLib.m_DisplayName = SourceControlLibraryDisplayName(Row.m_Library);
                for (std::size_t i = 0; i < NewLib.m_Categories.size(); ++i)
                    NewLib.m_Categories[i].m_Category = static_cast<sc_category>(i);
                DepotIt->m_Libraries.push_back(std::move(NewLib));
                LibIt = std::prev(DepotIt->m_Libraries.end());
            }

            const auto Class = ClassifyPendingPath(Row.m_RelativePath);
            auto& Bucket = LibIt->m_Categories[static_cast<std::size_t>(Class.m_Category)];
            Bucket.m_Category = Class.m_Category;

            if (Class.m_HexInstance.empty())
            {
                Bucket.m_UngroupedRows.push_back(&Row);
                continue;
            }

            const std::wstring GroupKey = Class.m_TypeNameLower + L"|" + Class.m_HexInstance;
            auto GroupIt = std::find_if(Bucket.m_ResourceGroups.begin(), Bucket.m_ResourceGroups.end(), [&](const sc_resource_group& G) { return G.m_GroupKey == GroupKey; });
            if (GroupIt == Bucket.m_ResourceGroups.end())
            {
                sc_resource_group NewGroup;
                NewGroup.m_GroupKey    = GroupKey;
                NewGroup.m_DisplayName = SourceControlResolveResourceName(Row.m_Library, Class.m_TypeNameLower, Class.m_HexInstance);
                Bucket.m_ResourceGroups.push_back(std::move(NewGroup));
                GroupIt = std::prev(Bucket.m_ResourceGroups.end());
            }
            GroupIt->m_Rows.push_back(&Row);
        }

        std::sort(Depots.begin(), Depots.end(), [](const sc_depot_group& A, const sc_depot_group& B) { return A.m_DisplayName < B.m_DisplayName; });
        for (auto& D : Depots)
        {
            std::sort(D.m_Libraries.begin(), D.m_Libraries.end(), [](const sc_library_group& A, const sc_library_group& B) { return A.m_DisplayName < B.m_DisplayName; });
            for (auto& L : D.m_Libraries)
                for (auto& Bucket : L.m_Categories)
                    std::sort(Bucket.m_ResourceGroups.begin(), Bucket.m_ResourceGroups.end(), [](const sc_resource_group& A, const sc_resource_group& B) { return A.m_DisplayName < B.m_DisplayName; });
        }

        return Depots;
    }

    // Every depot ANY pending row belongs to, regardless of custom-changelist membership - unlike
    // BuildDepotGroups (which only covers rows still in the auto-tree), the Changelists panel needs
    // to know a depot exists even once every one of its files has already been moved into a custom
    // changelist, so "+ New Changelist" still has somewhere to attach a brand-new one.
    inline std::vector<std::pair<std::string, std::string>> CollectDistinctDepots(const std::vector<sc_panel_row>& Rows) noexcept
    {
        std::vector<std::pair<std::string, std::string>> Depots; // Key, DisplayName
        for (auto& Row : Rows)
        {
            auto KeyName = SourceControlDepotKeyAndName(Row.m_Library);
            if (std::find_if(Depots.begin(), Depots.end(), [&](auto& D) { return D.first == KeyName.first; }) == Depots.end())
                Depots.push_back(std::move(KeyName));
        }
        std::sort(Depots.begin(), Depots.end(), [](auto& A, auto& B) { return A.second < B.second; });
        return Depots;
    }

    // Every row belonging to DepotKey, regardless of custom-changelist membership - backs the
    // "(All)" pseudo-entry ("the actual depot which mean all" - direct user design): a one-click way
    // to comment-and-commit EVERY pending file in a depot without first sorting any of it into a
    // named changelist.
    inline std::vector<const sc_panel_row*> RowsForDepot(const std::vector<sc_panel_row>& Rows, const std::string& DepotKey) noexcept
    {
        std::vector<const sc_panel_row*> Result;
        for (auto& Row : Rows)
            if (SourceControlDepotKeyAndName(Row.m_Library).first == DepotKey)
                Result.push_back(&Row);
        return Result;
    }

    // Shared by the Pending Changes list AND a changelist's own file list (Phase 4C - ONE
    // implementation, not duplicated per list, per the project's no-redundant-data rule). Acts on the whole
    // active multi-selection when the right-clicked row is part of one, otherwise just that one row -
    // same "right-click preserves/collapses selection" rule E10_asset_browser_files_tab.h's own
    // RowContext popup already established (independently re-implemented here, not shared code, per
    // the Phase 4C design correction).
    inline void RenderSourceControlContextMenu(xundo::system& Undo, const std::vector<sc_panel_row>& AllRows, const std::wstring& ClickedKey) noexcept
    {
        auto& S = g_SourceControlPanel;
        if (!S.m_MultiSelected.contains(ClickedKey))
            SourceControlSelectSingle(ClickedKey);

        if (ImGui::BeginPopupContextItem("SCRowContext"))
        {
            std::vector<const sc_panel_row*> Selected;
            for (auto& Row : AllRows)
                if (S.m_MultiSelected.contains(Row.m_Key))
                    Selected.push_back(&Row);

            const bool bAnyLfsUnlocked = std::any_of(Selected.begin(), Selected.end(), [](const sc_panel_row* R)
                { return R->m_Status.lfsTracked && (!R->m_Lock || R->m_Lock->ownership != sc::LockOwnership::CurrentUser); });
            const bool bAnyLockedByMe  = std::any_of(Selected.begin(), Selected.end(), [](const sc_panel_row* R)
                { return R->m_Lock && R->m_Lock->ownership == sc::LockOwnership::CurrentUser; });
            const bool bAnyModified    = std::any_of(Selected.begin(), Selected.end(), [](const sc_panel_row* R)
                { return R->m_Status.modified; });

            if (ImGui::MenuItem("Lock", nullptr, false, bAnyLfsUnlocked))
            {
                for (auto* R : Selected)
                    xeditor::RunQuery(Undo, std::format("SourceControlLock -Library {} -Path {}"
                        , e29::commands::FormatLibraryGuid(R->m_Library), e29::commands::EncodeAssetPath(R->m_RelativePath)));
            }
            if (ImGui::MenuItem("Unlock", nullptr, false, bAnyLockedByMe))
            {
                for (auto* R : Selected)
                    xeditor::RunQuery(Undo, std::format("SourceControlUnlock -Library {} -Path {}"
                        , e29::commands::FormatLibraryGuid(R->m_Library), e29::commands::EncodeAssetPath(R->m_RelativePath)));
            }
            if (ImGui::MenuItem("Undo Changes...", nullptr, false, bAnyModified))
                S.m_bUndoChangesConfirmPending = true;

            // A changelist belongs to one depot (direct user requirement) - only offer changelists
            // whose depot matches every selected row's own depot. A selection spanning more than one
            // depot has no matching changelist to offer at all (there's no such thing as a single
            // changelist that could ever actually commit files from two different repos).
            std::string CommonDepotKey;
            bool bSingleDepot = !Selected.empty();
            for (auto* R : Selected)
            {
                const auto Key = SourceControlDepotKeyAndName(R->m_Library).first;
                if (CommonDepotKey.empty()) CommonDepotKey = Key;
                else if (CommonDepotKey != Key) { bSingleDepot = false; break; }
            }

            if (ImGui::BeginMenu("Add to Changelist", bSingleDepot && std::any_of(S.m_Changelists.begin(), S.m_Changelists.end(), [&](const sc_changelist& CL) { return CL.m_DepotKey == CommonDepotKey; })))
            {
                for (std::size_t i = 0; i < S.m_Changelists.size(); ++i)
                {
                    if (S.m_Changelists[i].m_DepotKey != CommonDepotKey) continue;
                    if (ImGui::MenuItem(S.m_Changelists[i].m_Name.c_str()))
                        for (auto* R : Selected)
                        {
                            // Clears any PRIOR membership first (explicit, or flattens a live group
                            // this one row was part of elsewhere) before adding it here explicitly -
                            // this context-menu path always picks individual files, never "the whole
                            // resource concept" (only a folder drag does that).
                            SourceControlRemoveRowFromChangelist(AllRows, *R);
                            SourceControlAssignToChangelist(R->m_Key, static_cast<int>(i));
                        }
                }
                ImGui::EndMenu();
            }

            // Only meaningful when the clicked selection is currently sitting in a custom changelist
            // at all - moves it back to the Depot -> Library -> Category auto-tree. Goes through
            // SourceControlRemoveRowFromChangelist (not a flat SourceControlAssignToChangelist(-1))
            // so removing one file out of a LIVE resource group flattens the rest of that group into
            // explicit entries instead of silently vanishing them too.
            const bool bAnyInCustomList = std::any_of(Selected.begin(), Selected.end(), [](const sc_panel_row* R) { return SourceControlChangelistOf(*R) >= 0; });
            if (ImGui::MenuItem("Remove from Changelist", nullptr, false, bAnyInCustomList))
                for (auto* R : Selected) SourceControlRemoveRowFromChangelist(AllRows, *R);

            ImGui::EndPopup();
        }
    }

    // Deferred "Undo Changes" confirm modal - see source_control_panel_state::m_bUndoChangesConfirmPending's
    // own comment for why this can't live inline at the MenuItem. Called ONCE, unconditionally, from
    // RenderSourceControlPanel's own stable end - recomputes Selected from the SAME persistent
    // S.m_MultiSelected the MenuItem itself read, so no separate snapshot state is needed.
    inline void RenderSourceControlUndoChangesConfirmModal(xundo::system& Undo, const std::vector<sc_panel_row>& AllRows) noexcept
    {
        auto& S = g_SourceControlPanel;
        if (S.m_bUndoChangesConfirmPending)
        {
            ImGui::OpenPopup("Undo Changes##SCConfirm");
            S.m_bUndoChangesConfirmPending = false;
        }
        if (ImGui::BeginPopupModal("Undo Changes##SCConfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            std::vector<const sc_panel_row*> Selected;
            for (auto& Row : AllRows)
                if (S.m_MultiSelected.contains(Row.m_Key))
                    Selected.push_back(&Row);

            ImGui::Text("Discard local changes to %zu file(s)? This cannot be undone.", Selected.size());
            ImGui::Separator();
            if (ImGui::Button("Discard Changes", ImVec2(160, 0)))
            {
                for (auto* R : Selected)
                    xeditor::RunQuery(Undo, std::format("SourceControlRevert -Library {} -Path {}"
                        , e29::commands::FormatLibraryGuid(R->m_Library), e29::commands::EncodeAssetPath(R->m_RelativePath)));
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // Same badge derivation the Asset Tree's own badges/tooltip already use - one place, not
    // recomputed ad hoc at each of the 3 sites below (draw, tooltip, sort).
    inline e10::asset_status_badge SourceControlRowStatusBadge(const sc_panel_row& Row) noexcept
    {
        if (Row.m_Status.untracked) return e10::asset_status_badge::Untracked;
        if (Row.m_Status.modified || Row.m_Status.staged || Row.m_Status.conflicted) return e10::asset_status_badge::Modified;
        return e10::asset_status_badge::Clean; // reached via a lock-only row - see GetAllPendingChanges' own comment
    }

    inline e10::asset_lock_badge SourceControlRowLockBadge(const sc_panel_row& Row) noexcept
    {
        if (!Row.m_Lock) return e10::asset_lock_badge::None;
        return Row.m_Lock->ownership == sc::LockOwnership::CurrentUser ? e10::asset_lock_badge::LockedByMe : e10::asset_lock_badge::LockedByOther;
    }

    // Identical priority order to files_tab's own SourceControlSortRank (untracked, modified,
    // locked+modified(gold), locked-by-other(red), clean, locked+clean(green)) - direct user
    // request: this panel should look and sort "very similar to the asset view".
    inline int SourceControlSortRank(const sc_panel_row& Row) noexcept
    {
        const auto Status = SourceControlRowStatusBadge(Row);
        const auto Lock   = SourceControlRowLockBadge(Row);

        if (Lock == e10::asset_lock_badge::None)
        {
            switch (Status)
            {
                case e10::asset_status_badge::Untracked: return 0;
                case e10::asset_status_badge::Modified:  return 1;
                case e10::asset_status_badge::Clean:     return 4;
                default:                                 return 6;
            }
        }
        if (Lock == e10::asset_lock_badge::LockedByOther) return 3;
        return (Status == e10::asset_status_badge::Modified) ? 2 : 5;
    }

    // Renders one row's CELLS - caller must already be inside an active table row (TableNextRow()
    // already called), same split files_tab's own row loop uses. Column 0 = the real drawn SC badge
    // (DrawSourceControlBadge - not a text glyph, matching the Asset Tree's own icon look exactly);
    // column 1 = the FULL relative path (not a bare filename - this list spans every folder in the
    // project, so a bare name would be ambiguous, direct user request to show the whole path here).
    inline void RenderSourceControlRowCells(xundo::system& Undo, const std::vector<sc_panel_row>& AllRows, const sc_panel_row& Row) noexcept
    {
        auto& S = g_SourceControlPanel;
        // Row.m_Key (a stable, content-derived wstring), NOT &Row's own address - real bug found live
        // ("you can drag sections but not folder or files"): m_CachedRows gets reassigned (a fresh
        // vector, new addresses) every time a background scan bumps SourceControlRevision(), which can
        // land mid-drag; an address-keyed PushID scope makes ImGui's per-frame active-id/drag tracking
        // silently discontinuous the instant that happens, breaking the drag with no visible error.
        const auto RowIdStr = xstrtool::To(Row.m_Key);
        ImGui::PushID(RowIdStr.c_str());

        const auto StatusBadge = SourceControlRowStatusBadge(Row);
        const auto LockBadge   = SourceControlRowLockBadge(Row);

        ImGui::TableSetColumnIndex(0);
        {
            const ImVec2 CellMin = ImGui::GetCursorScreenPos();
            const float  RowH    = ImGui::GetTextLineHeight();
            constexpr float BadgeSize = 11.0f; // matches files_tab's own SC column badge size
            e10::DrawSourceControlBadge(ImGui::GetWindowDrawList()
                , { CellMin.x + BadgeSize * 0.5f, CellMin.y + RowH * 0.5f }, BadgeSize, StatusBadge, LockBadge);

            ImGui::InvisibleButton("##SCHover", ImVec2(ImGui::GetContentRegionAvail().x, RowH));
            if (ImGui::IsItemHovered())
            {
                const char* Title = ""; const char* Desc = "";
                e10::GetSourceControlTooltipText(StatusBadge, LockBadge, Title, Desc);
                ImGui::BeginTooltip();
                ImGui::Text("%s", Title);
                ImGui::TextDisabled("%s", Desc);
                ImGui::EndTooltip();
            }
        }

        ImGui::TableSetColumnIndex(1);
        const bool bSelected = S.m_MultiSelected.contains(Row.m_Key);
        if (ImGui::Selectable(xstrtool::To(Row.m_RelativePath).c_str(), bSelected, ImGuiSelectableFlags_SpanAllColumns))
        {
            std::vector<std::wstring> OrderedKeys;
            OrderedKeys.reserve(AllRows.size());
            for (auto& R : AllRows) OrderedKeys.push_back(R.m_Key);
            SourceControlHandleRowClick(OrderedKeys, Row.m_Key);
        }

        // Drag source, onto a changelist in the right panel (direct user requirement: "the user can
        // create a change list and either drag a folder or 'files' to it"). Drags the WHOLE active
        // multi-selection when this row is part of one - same "drag the group, not just the one row
        // you happened to grab" rule this codebase already established for the Asset Tree - otherwise
        // just this single row. The payload itself carries no data - the real content is read
        // directly from g_SourceControlPanel.m_MultiSelectOrder at drop time, since it's already
        // shared global state and every row here is a stable key into it.
        //
        // Gated behind an explicit 12px drag-distance check, NOT a bare BeginDragDropSource() call -
        // same real, already-diagnosed bug class E10_asset_browser_files_tab.h's own row drag source
        // hit and fixed: imgui_widgets.cpp's ButtonBehavior() refuses to report a PressedOnClickRelease
        // item (a plain Selectable()) as "pressed" on release once g.DragDropActive went true during
        // that same press-hold, so an unguarded BeginDragDropSource() swallows ordinary clicks the
        // instant the mouse crosses ImGui's own tiny ~6px default drag threshold - trivially crossed by
        // a real mouse's natural jitter (confirmed live: a plain click on a folder row silently turned
        // into a spurious selection-only "drag" here). The 12px LOCAL override (IsMouseDragging's own
        // threshold param, not the global io.MouseDragThreshold) is deliberately far past ordinary
        // click jitter, so a plain click's own Selectable-driven selection logic above is never
        // preempted.
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 12.0f) && ImGui::BeginDragDropSource())
        {
            if (!bSelected) SourceControlSelectSingle(Row.m_Key);
            // A single row is always a flat, explicit pick - even if it happens to be the only
            // pending file for its resource right now, dragging the ROW (not the folder heading)
            // means "just this file," never "the whole resource concept."
            S.m_DragSourceScope.reset();
            int Dummy = 0;
            ImGui::SetDragDropPayload("SC_DRAG_SELECTION", &Dummy, sizeof(Dummy));
            ImGui::Text("%zu file(s)", S.m_MultiSelectOrder.size());
            ImGui::EndDragDropSource();
        }

        RenderSourceControlContextMenu(Undo, AllRows, Row.m_Key);

        ImGui::PopID();
    }

    // Shared table shell for the Pending Changes list, a changelist's own file list, AND (new) each
    // category bucket in the Depot -> Library -> Category auto-tree (Phase 4 follow-up, direct user
    // request: "it should look very similar to the asset view... the sorting, the look, etc").
    // RowsToShow is a FILTERED subset of AllRows - AllRows stays the full list so multi-select/
    // context-menu keep operating across the whole selection, not just what's visible in this
    // particular table. MaxHeight (0 = fill available height, the original 2 call sites' own
    // behavior, unchanged) caps a nested category table to a bounded size instead of one category
    // claiming the whole panel's remaining vertical space while sibling categories/libraries/depots
    // stacked below it get squeezed to nothing.
    inline void RenderSourceControlTable(xundo::system& Undo, const std::vector<sc_panel_row>& AllRows
        , std::vector<const sc_panel_row*> RowsToShow, const char* TableId, float MaxHeight = 0.0f) noexcept
    {
        float Height = ImGui::GetContentRegionAvail().y;
        if (MaxHeight > 0.0f)
        {
            const float HeaderH  = ImGui::GetFrameHeightWithSpacing();
            const float NaturalH = HeaderH + static_cast<float>(RowsToShow.size()) * ImGui::GetTextLineHeightWithSpacing() + 4.0f;
            Height = std::min(std::min(NaturalH, MaxHeight), Height);
        }
        if (!ImGui::BeginTable(TableId, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable, ImVec2(ImGui::GetContentRegionAvail().x, Height)))
            return;

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##SC",  ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_PreferSortAscending, 16.0f);
        ImGui::TableSetupColumn("Path",  ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortAscending);

        // Manual header row - same "drawn icon instead of a text label" discipline as files_tab's
        // own SC column header.
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        ImGui::TableSetColumnIndex(0);
        ImGui::TableHeader("##SC");
        {
            const ImVec2 CellMin = ImGui::GetItemRectMin();
            const ImVec2 CellMax = ImGui::GetItemRectMax();
            const ImVec2 Center{ (CellMin.x + CellMax.x) * 0.5f, (CellMin.y + CellMax.y) * 0.5f };
            e10::DrawPadlockShape(ImGui::GetWindowDrawList(), Center, 11.0f, IM_COL32(180, 180, 185, 255));
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Source Control");
                ImGui::TextDisabled("Tracked/untracked/modified status, and lock ownership");
                ImGui::EndTooltip();
            }
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::TableHeader("Path");

        // Sorted every frame (not gated on SpecsDirty like files_tab's own table) - RowsToShow is a
        // cheap filter of an already-cached row list, re-sorting it is inexpensive even for a large
        // pending-changes list, and this sidesteps a real staleness case files_tab doesn't have to
        // worry about: a background scan can add/remove rows between header clicks, and those need
        // to land in the right sorted position without the user re-clicking a header to force it.
        if (ImGuiTableSortSpecs* SortSpecs = ImGui::TableGetSortSpecs())
        {
            const int  SortColumn  = SortSpecs->SpecsCount > 0 ? SortSpecs->Specs[0].ColumnIndex : 1;
            const bool bDescending = SortSpecs->SpecsCount > 0 && SortSpecs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
            std::sort(RowsToShow.begin(), RowsToShow.end(), [SortColumn, bDescending](const sc_panel_row* A, const sc_panel_row* B) noexcept
            {
                const int Cmp = (SortColumn == 0) ? (SourceControlSortRank(*A) - SourceControlSortRank(*B)) : A->m_RelativePath.compare(B->m_RelativePath);
                return bDescending ? (Cmp > 0) : (Cmp < 0);
            });
            SortSpecs->SpecsDirty = false;
        }

        for (auto* Row : RowsToShow)
        {
            ImGui::TableNextRow();
            RenderSourceControlRowCells(Undo, AllRows, *Row);
        }

        ImGui::EndTable();
    }

    // The Depot -> Resource Library -> Category auto-tree - replaces the old flat "Pending Changes"
    // list (direct user design). A category node's own right-click offers "Select All", which just
    // populates the shared multi-select - committing a whole category, a sub-selection, or a single
    // item is then the SAME "Commit Selected" action (see RenderSourceControlPanel), not three
    // separate code paths, matching "should still be flexible" without three different UIs to learn.
    // Empty categories/libraries/depots are skipped entirely - showing an empty "Resources (0)" node
    // for every library in every depot would be exactly the "adds more work" clutter this whole
    // redesign is meant to avoid.
    // Shared by every "folder" level (category, resource group) in the tree below - selects exactly
    // RowsToSelect, replacing whatever was selected before. Used both by a folder's own right-click
    // "Select All" and as the first step of starting a drag (so dragging a folder that ISN'T already
    // the active selection still drags everything under it, not just whatever was selected before).
    inline void SourceControlSelectRows(const std::vector<const sc_panel_row*>& RowsToSelect) noexcept
    {
        auto& S = g_SourceControlPanel;
        S.m_MultiSelected.clear();
        S.m_MultiSelectOrder.clear();
        for (auto* Row : RowsToSelect) { S.m_MultiSelected.insert(Row->m_Key); S.m_MultiSelectOrder.push_back(Row->m_Key); }
        S.m_MultiSelectAnchor.clear();
    }

    // One "folder" node in the tree - a category bucket's own resource group, or the category's
    // flat ungrouped rows. Renders the TreeNodeEx, its "Select All" context menu, its drag source
    // (direct user requirement: "drag a folder or 'files' to it"), and (if open) the row table
    // itself - one implementation shared by every folder level instead of three near-duplicates.
    // GlobalGroupKey (SourceControlGlobalGroupKey format, empty = "not a single coherent resource" -
    // never the case at either real call site today, but kept optional for robustness) tags this
    // folder's OWN drag source as "the whole resource concept" (direct user design, 2026-09-18:
    // "drags the 'Scene 1' folder... he does not care about the actual files there") so a drop onto a
    // changelist assigns it as a LIVE reference rather than a flat snapshot of whatever files happen
    // to be in it right now.
    inline void RenderSourceControlFolder(xundo::system& Undo, const std::vector<sc_panel_row>& AllRows
        , const std::string& Label, const std::vector<const sc_panel_row*>& RowsInFolder, const std::wstring& GlobalGroupKey = L"") noexcept
    {
        if (RowsInFolder.empty()) return;

        auto& S = g_SourceControlPanel;
        // Real bug found live ("every time I drag a file the folder... closes"): a bare
        // "Label (Count)" string IS the TreeNodeEx's own identity hash input when no "###" separator
        // pins a stable id - ImGui persists open/closed state per ID, so the instant Count changes
        // (a background rescan, a badge recompute, anything) this becomes a DIFFERENT widget to ImGui
        // and reverts to its default (closed - this folder has no DefaultOpen flag). "###Node" pins
        // the identity to just the enclosing PushID scope (already unique per folder), leaving only
        // the DISPLAY text free to vary with Count.
        const bool bOpen = ImGui::TreeNodeEx(std::format("{} ({})###Node", Label, RowsInFolder.size()).c_str(), ImGuiTreeNodeFlags_SpanFullWidth);
        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Select All")) SourceControlSelectRows(RowsInFolder);
            ImGui::EndPopup();
        }
        // Same explicit 12px drag-distance guard as the row-level drag source above (see its own
        // comment) - without it, a plain click on the folder's TreeNodeEx spuriously fires this
        // block and silently overwrites the current selection (confirmed live) instead of just
        // toggling the tree open/closed the way a plain click on a folder should.
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 12.0f) && ImGui::BeginDragDropSource())
        {
            SourceControlSelectRows(RowsInFolder);
            S.m_DragSourceScope = GlobalGroupKey.empty() ? std::optional<source_control_panel_state::drag_scope>{}
                : std::optional<source_control_panel_state::drag_scope>{ source_control_panel_state::drag_scope{ source_control_panel_state::drag_scope::kind::Resource, GlobalGroupKey } };
            int Dummy = 0;
            ImGui::SetDragDropPayload("SC_DRAG_SELECTION", &Dummy, sizeof(Dummy));
            ImGui::Text("%s (%zu file(s))", Label.c_str(), RowsInFolder.size());
            ImGui::EndDragDropSource();
        }
        if (bOpen)
        {
            ImGui::Indent();
            RenderSourceControlTable(Undo, AllRows, RowsInFolder, "SCFolderTable", 180.0f);
            ImGui::Unindent();
            ImGui::TreePop();
        }
    }

    // A category bucket's own direct content (its resource-group folders, then its ungrouped rows) -
    // factored out so it can be reused both for a real top-level category AND for the nested
    // "Scenes & Levels" folder (direct user correction, 2026-09-18: "Scene & Levels should be under
    // Resources" - it's a subfolder of Resources now, not a sibling top-level category).
    inline void RenderSourceControlCategoryContents(xundo::system& Undo, const std::vector<sc_panel_row>& AllRows, const sc_category_bucket& Bucket) noexcept
    {
        for (auto& Group : Bucket.m_ResourceGroups)
        {
            // Group.m_GroupKey, NOT &Group's address - see RenderSourceControlDepotTree's own comment
            // on this exact bug class.
            const auto GroupIdStr = xstrtool::To(Group.m_GroupKey);
            ImGui::PushID(GroupIdStr.c_str());
            // Group.m_GroupKey is "TypeLower|Hex" (library-scoped, since Group already lives inside
            // one known library's own bucket) - prefixing the library turns it into the SAME global
            // format SourceControlGlobalGroupKey produces, without re-deriving Type/Hex from scratch.
            const std::wstring GlobalGroupKey = Group.m_Rows.empty() ? std::wstring{}
                : (xstrtool::To(e29::commands::FormatLibraryGuid(Group.m_Rows.front()->m_Library)) + L"|" + Group.m_GroupKey);
            RenderSourceControlFolder(Undo, AllRows, std::format("\xEE\xA3\x95 {}", Group.m_DisplayName), Group.m_Rows, GlobalGroupKey);
            ImGui::PopID();
        }
        if (!Bucket.m_UngroupedRows.empty())
            RenderSourceControlTable(Undo, AllRows, Bucket.m_UngroupedRows, "SCCategoryTable", 180.0f);
    }

    // One category-shaped folder node: TreeNodeEx + "Select All" + drag source + its contents when
    // open, PLUS an optional NESTED child category rendered first inside it (used for exactly one real
    // case today - "Scenes & Levels" living under "Resources"). Shared by every real top-level category
    // AND the nested "Scenes & Levels" folder itself - one implementation instead of near-duplicating
    // the same TreeNodeEx/Select-All/drag block again. LibraryGuid/Category identify this bucket for
    // the drag source, exactly the way RenderSourceControlFolder's own GlobalGroupKey identifies a
    // resource - dragging this WHOLE category tags the drag as "a whole category concept" (direct user
    // design, 2026-09-18: "the superset already includes the subset... we just want the superset").
    // When NestedBucket is non-null, "Select All"/drag/commit-resolution for THIS folder include the
    // nested bucket's rows too, and the drag tags m_ImplicitChildKey with the nested category's own key
    // - real bug found live: "if I drag the resources[,] the Scenes & Levels stay behind" - Resources
    // is Scenes & Levels' actual visual PARENT, so dragging it must take its child along, same as
    // dragging any other parent folder would.
    inline void RenderSourceControlCategoryFolder(xundo::system& Undo, const std::vector<sc_panel_row>& AllRows, const char* Label
        , const sc_category_bucket& Bucket, e10::library::guid LibraryGuid, sc_category Category
        , const char* NestedLabel = nullptr, const sc_category_bucket* NestedBucket = nullptr, sc_category NestedCategory = sc_category::Resources) noexcept
    {
        const std::size_t Total = Bucket.RowCount() + (NestedBucket ? NestedBucket->RowCount() : 0);
        if (Total == 0) return;

        // "###Node" pins this TreeNodeEx's identity to the enclosing PushID scope, not the visible
        // "Label (Total)" text - see RenderSourceControlFolder's own comment for why a bare count-
        // bearing label breaks persisted open/closed state the instant the count changes.
        const bool bOpen = ImGui::TreeNodeEx(std::format("{} ({})###Node", Label, Total).c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth);
        auto CollectEverything = [&]() noexcept
        {
            std::vector<const sc_panel_row*> Everything = Bucket.m_UngroupedRows;
            for (auto& G : Bucket.m_ResourceGroups) Everything.insert(Everything.end(), G.m_Rows.begin(), G.m_Rows.end());
            if (NestedBucket)
            {
                Everything.insert(Everything.end(), NestedBucket->m_UngroupedRows.begin(), NestedBucket->m_UngroupedRows.end());
                for (auto& G : NestedBucket->m_ResourceGroups) Everything.insert(Everything.end(), G.m_Rows.begin(), G.m_Rows.end());
            }
            return Everything;
        };
        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Select All")) SourceControlSelectRows(CollectEverything());
            ImGui::EndPopup();
        }
        // Same explicit 12px drag-distance guard as every other drag source in this file.
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 12.0f) && ImGui::BeginDragDropSource())
        {
            const auto Everything = CollectEverything();
            SourceControlSelectRows(Everything);
            g_SourceControlPanel.m_DragSourceScope = source_control_panel_state::drag_scope{ source_control_panel_state::drag_scope::kind::Category, SourceControlGlobalCategoryKey(LibraryGuid, Category)
                , NestedBucket ? std::optional<std::wstring>{ SourceControlGlobalCategoryKey(LibraryGuid, NestedCategory) } : std::optional<std::wstring>{} };
            int Dummy = 0;
            ImGui::SetDragDropPayload("SC_DRAG_SELECTION", &Dummy, sizeof(Dummy));
            ImGui::Text("%s (%zu file(s))", Label, Everything.size());
            ImGui::EndDragDropSource();
        }
        if (bOpen)
        {
            ImGui::Indent();
            if (NestedBucket)
                RenderSourceControlCategoryFolder(Undo, AllRows, NestedLabel, *NestedBucket, LibraryGuid, NestedCategory);
            RenderSourceControlCategoryContents(Undo, AllRows, Bucket);
            ImGui::Unindent();
            ImGui::TreePop();
        }
    }

    // One LIBRARY node - Category -> Resource nested underneath (with Scenes & Levels folded under
    // Resources, per the earlier nesting correction) - plus its own Select All/drag source. Direct
    // user request (2026-09-18): "I should be able to drag the entire library to my change list...
    // this means the same thing as (All)" (dynamically tracks everything in this one library, the same
    // "live, not a snapshot" way a resource/category is, just one level further up). Shared by the left
    // Pending Changes tree AND (via a throwaway sc_library_group) a changelist's own file-box display,
    // so a live-tracked whole library nests identically in both places.
    inline void RenderSourceControlLibraryFolder(xundo::system& Undo, const std::vector<sc_panel_row>& AllRows, const sc_library_group& Lib) noexcept
    {
        auto& S = g_SourceControlPanel;
        std::size_t LibTotal = 0;
        for (auto& Cat : Lib.m_Categories) LibTotal += Cat.RowCount();
        if (LibTotal == 0) return;

        ImGui::PushID(static_cast<int>(Lib.m_Library.m_Instance.m_Value));
        // "###Node" - same stable-id-vs-display-text separation as RenderSourceControlFolder's own.
        const bool bLibOpen = ImGui::TreeNodeEx(std::format("{} ({})###Node", Lib.m_DisplayName, LibTotal).c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth);
        auto CollectEverything = [&]() noexcept
        {
            std::vector<const sc_panel_row*> Everything;
            for (auto& Cat : Lib.m_Categories)
            {
                Everything.insert(Everything.end(), Cat.m_UngroupedRows.begin(), Cat.m_UngroupedRows.end());
                for (auto& G : Cat.m_ResourceGroups) Everything.insert(Everything.end(), G.m_Rows.begin(), G.m_Rows.end());
            }
            return Everything;
        };
        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Select All")) SourceControlSelectRows(CollectEverything());
            ImGui::EndPopup();
        }
        // Same explicit 12px drag-distance guard as every other drag source in this file.
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 12.0f) && ImGui::BeginDragDropSource())
        {
            const auto Everything = CollectEverything();
            SourceControlSelectRows(Everything);
            S.m_DragSourceScope = source_control_panel_state::drag_scope{ source_control_panel_state::drag_scope::kind::Library, SourceControlGlobalLibraryKey(Lib.m_Library) };
            int Dummy = 0;
            ImGui::SetDragDropPayload("SC_DRAG_SELECTION", &Dummy, sizeof(Dummy));
            ImGui::Text("%s (%zu file(s))", Lib.m_DisplayName.c_str(), Everything.size());
            ImGui::EndDragDropSource();
        }
        if (bLibOpen)
        {
            for (auto& Category : Lib.m_Categories)
            {
                // Scenes & Levels is rendered NESTED under Resources (direct user correction,
                // 2026-09-18: "Scene & Levels should be under Resources" - a subfolder, not a sibling
                // top-level category) via RenderSourceControlCategoryFolder's own NestedBucket - skip
                // its own top-level slot entirely here.
                if (Category.m_Category == sc_category::ScenesAndLevels) continue;

                auto& ScenesBucket = Lib.m_Categories[static_cast<std::size_t>(sc_category::ScenesAndLevels)];
                const bool bIsResources = (Category.m_Category == sc_category::Resources);
                ImGui::PushID(static_cast<int>(Category.m_Category));
                RenderSourceControlCategoryFolder(Undo, AllRows, SourceControlCategoryLabel(Category.m_Category), Category, Lib.m_Library, Category.m_Category
                    , bIsResources ? "Scenes & Levels" : nullptr, bIsResources ? &ScenesBucket : nullptr, sc_category::ScenesAndLevels);
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    inline void RenderSourceControlDepotTree(xundo::system& Undo, const std::vector<sc_panel_row>& AllRows) noexcept
    {
        auto& S = g_SourceControlPanel;
        const auto Depots = BuildDepotGroups(AllRows);
        // Every depot that has ANY pending row at all, regardless of changelist membership - direct
        // user correction, 2026-09-18: "the depot is gone (should not be gone)". BuildDepotGroups only
        // returns depots with something STILL unassigned, so a depot whose entire pending list has
        // already been moved into changelists would otherwise vanish from this tree entirely, leaving
        // no way to click it again to re-select it for the Changelists panel on the right.
        const auto AllDepots = CollectDistinctDepots(AllRows);

        if (AllDepots.empty())
        {
            ImGui::TextDisabled("(clean - nothing pending)");
            return;
        }

        for (auto& [DepotKey, DepotDisplayName] : AllDepots)
        {
            ImGui::PushID(DepotKey.c_str());

            auto DepotIt = std::find_if(Depots.begin(), Depots.end(), [&](const sc_depot_group& D) { return D.m_Key == DepotKey; });
            std::size_t DepotTotal = 0;
            if (DepotIt != Depots.end())
                for (auto& Lib : DepotIt->m_Libraries) for (auto& Cat : Lib.m_Categories) DepotTotal += Cat.RowCount();

            // Clicking a depot's own row selects it for the Changelists panel too (direct user
            // design: "to select the depot the user can click on the depot on the left. Then a
            // special selection will select it and stay selected") - one gesture, one place, instead
            // of a separate depot picker duplicated on the right. ImGuiTreeNodeFlags_Selected gives
            // it the same persistent highlight ImGui already uses for a selected tree row elsewhere.
            const bool bDepotSelected = (DepotKey == S.m_SelectedChangelistDepotKey);
            // "###Node" - same stable-id-vs-display-text separation as RenderSourceControlFolder's own.
            const bool bDepotOpen = ImGui::TreeNodeEx(std::format("\xEE\xA3\xB1 {} ({})###Node", DepotDisplayName, DepotTotal).c_str()
                , ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth | (bDepotSelected ? ImGuiTreeNodeFlags_Selected : 0));
            if (ImGui::IsItemClicked())
                S.m_SelectedChangelistDepotKey = DepotKey;
            if (bDepotOpen)
            {
                if (DepotIt != Depots.end())
                    for (auto& Lib : DepotIt->m_Libraries)
                        RenderSourceControlLibraryFolder(Undo, AllRows, Lib);
                else
                    ImGui::TextDisabled("(everything here is already in a changelist)");
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    // RunQuery, not Run() - Run() treats ANY non-empty Execute() result as a routing failure and logs
    // it as "command failed" (see E29_CommandContext.h's own Run), which is wrong for a Query command
    // whose OWN successful result text (e.g. "Pulled", "Outcome: Published") is exactly that non-empty
    // string. This panel needs the literal text back to decide Pull-succeeded vs. Pull-hit-a-conflict,
    // so it calls System.Execute directly and classifies the result itself via each command's own
    // "<Verb>: " failure-message convention (every SourceControl* command already formats its
    // failures that way - see E29_Commands_SourceControl.h).
    [[nodiscard]] inline std::string SourceControlRunQuery(xundo::system& Undo, const std::string& Cmd) noexcept
    {
        xeditor::LogConsole(Cmd, xeditor::log_source::User);
        std::string Result = Undo.Execute(Cmd);
        if (!Result.empty()) xeditor::LogConsole(Result, xeditor::log_source::System);
        return Result;
    }

    // Commit flow for an arbitrary set of keys: Pull -> (abort on conflict) -> Commit (stage+commit+
    // push all paths, GROUPED PER LIBRARY, as one real commit per library - a commit is scoped to one
    // repo, so this is what actually makes "select a whole category, a sub-selection, or just one
    // item, then Commit" all work uniformly (direct user requirement: "should still be flexible").
    // Implements the user's confirmed "pull first, then merge, then submit, then push" order at the UI
    // level - SourceControlCommit already stages+commits+pushes in one call, so "submit" and "push"
    // are one step here, matching the command's own existing behavior rather than inventing a
    // separate push. Keys is pruned IN PLACE of whatever actually committed - shared by
    // SourceControlCommitChangelist (CL.m_Keys) and the new selection-based commit (a plain
    // std::vector snapshot of m_MultiSelectOrder) so both get correct "no stale leftover entry"
    // behavior for free, not two divergent copies of this pruning logic.
    inline std::string SourceControlCommitKeys(xundo::system& Undo, std::vector<std::wstring>& Keys, const std::string& Comment) noexcept
    {
        if (Keys.empty() || Comment.empty()) return {};

        std::unordered_map<std::string, std::vector<std::wstring>> PathsByLibraryHex; // FormatLibraryGuid -> relative paths
        std::unordered_map<std::string, e10::library::guid> LibraryByHex;
        for (auto& Key : Keys)
        {
            const auto Sep = Key.find(L'|');
            if (Sep == std::wstring::npos) continue;
            const std::string LibHex = xstrtool::To(Key.substr(0, Sep));
            const std::wstring RelPath = Key.substr(Sep + 1);
            PathsByLibraryHex[LibHex].push_back(RelPath);
        }
        for (auto& Row : BuildSourceControlRows())
        {
            const std::string LibHex = e29::commands::FormatLibraryGuid(Row.m_Library);
            if (PathsByLibraryHex.count(LibHex)) LibraryByHex[LibHex] = Row.m_Library;
        }

        std::string Summary;
        for (auto& [LibHex, Paths] : PathsByLibraryHex)
        {
            if (!LibraryByHex.count(LibHex)) continue; // library no longer open - skip, report below
            const auto LibraryGuidStr = LibHex;

            const std::string PullResult = SourceControlRunQuery(Undo, std::format("SourceControlPull -Library {}", LibraryGuidStr));
            if (PullResult.starts_with("SourceControlPull: "))
            {
                Summary += std::format("Pull failed for library {}: {} - resolve with your normal git tooling, then retry.\n", LibraryGuidStr, PullResult);
                continue; // never auto-resolve - skip committing THIS library's paths, try the rest
            }

            std::string JoinedPaths;
            for (auto& P : Paths) { JoinedPaths += xstrtool::To(P); JoinedPaths += '\n'; }
            const auto PathsB64 = xeditor::Base64Encode(JoinedPaths);
            const auto MsgB64   = xeditor::Base64Encode(Comment);

            const std::string CommitResult = SourceControlRunQuery(Undo, std::format("SourceControlCommit -Library {} -Paths {} -Message {}"
                , LibraryGuidStr, PathsB64, MsgB64));
            Summary += CommitResult + "\n";

            if (!CommitResult.starts_with("SourceControlCommit: "))
            {
                // Committed (successfully or at least attempted, per Submit's own Outcome reporting) -
                // these keys are no longer pending; drop them so a stale entry doesn't linger after
                // the next Pending Changes refresh removes the underlying file.
                for (auto& P : Paths)
                {
                    const auto Key = SourceControlRowKey(LibraryByHex[LibHex], P);
                    Keys.erase(std::remove(Keys.begin(), Keys.end(), Key), Keys.end());
                }
            }

            // Neither Pull nor Commit themselves bump SourceControlRevision() - kick a fresh scan so
            // the Pending Changes list catches up promptly instead of waiting for the next idle period.
            e29::source_control::LaunchSourceControlStatusScan(e29::commands::ResolveLibraryRootPath(LibraryByHex[LibHex]));
        }

        return Summary;
    }

    // Resolves a changelist's FULL current row set - its explicit m_Keys, every row any of its
    // m_LiveLibraryKeys/m_LiveCategoryKeys covers, and every row any of its m_LiveGroupKeys resolves
    // to - fresh from AllRows every call (never a stale snapshot). Shared by the commit flow, the "can
    // commit" check, and the file-box display, so all three agree on exactly the same set at all times.
    inline std::vector<const sc_panel_row*> SourceControlResolveChangelistRows(const std::vector<sc_panel_row>& AllRows, const sc_changelist& CL) noexcept
    {
        std::vector<const sc_panel_row*> Result;
        for (auto& Row : AllRows)
        {
            if (std::find(CL.m_Keys.begin(), CL.m_Keys.end(), Row.m_Key) != CL.m_Keys.end()) { Result.push_back(&Row); continue; }

            if (!CL.m_LiveLibraryKeys.empty())
            {
                const auto LibraryKey = SourceControlGlobalLibraryKey(Row.m_Library);
                if (std::find(CL.m_LiveLibraryKeys.begin(), CL.m_LiveLibraryKeys.end(), LibraryKey) != CL.m_LiveLibraryKeys.end())
                {
                    Result.push_back(&Row);
                    continue;
                }
            }

            const auto Class = ClassifyPendingPath(Row.m_RelativePath);

            if (!CL.m_LiveCategoryKeys.empty())
            {
                const auto CategoryKey = SourceControlGlobalCategoryKey(Row.m_Library, Class.m_Category);
                if (std::find(CL.m_LiveCategoryKeys.begin(), CL.m_LiveCategoryKeys.end(), CategoryKey) != CL.m_LiveCategoryKeys.end())
                {
                    Result.push_back(&Row);
                    continue;
                }
            }

            if (CL.m_LiveGroupKeys.empty() || Class.m_HexInstance.empty()) continue;
            const auto GlobalKey = SourceControlGlobalGroupKey(Row.m_Library, Class.m_TypeNameLower, Class.m_HexInstance);
            if (std::find(CL.m_LiveGroupKeys.begin(), CL.m_LiveGroupKeys.end(), GlobalKey) != CL.m_LiveGroupKeys.end())
                Result.push_back(&Row);
        }
        return Result;
    }

    inline void SourceControlCommitChangelist(xundo::system& Undo, const std::vector<sc_panel_row>& AllRows, sc_changelist& CL) noexcept
    {
        // Resolve live groups fresh and commit them together with the explicit keys as ONE real
        // commit - direct user design: a live "Scene 1" reference commits together with whatever it
        // currently, actually resolves to, not a snapshot taken back when it was dragged in.
        const auto ResolvedRows = SourceControlResolveChangelistRows(AllRows, CL);
        std::vector<std::wstring> Keys;
        Keys.reserve(ResolvedRows.size());
        for (auto* Row : ResolvedRows) Keys.push_back(Row->m_Key);

        g_SourceControlPanel.m_StatusLine = SourceControlCommitKeys(Undo, Keys, CL.m_Comment);

        // Keys was pruned in place to whatever did NOT commit. A live group's members always share
        // one library (a resource group is a single-library concept), and SourceControlCommitKeys
        // commits/fails per library atomically - so a live group either fully committed (none of its
        // rows remain pending) or fully failed (all of them do), never partially, making "does any
        // resolved row of this group still remain" an exact test, not a guess.
        const std::unordered_set<std::wstring> StillPending(Keys.begin(), Keys.end());
        CL.m_Keys.erase(std::remove_if(CL.m_Keys.begin(), CL.m_Keys.end(), [&](const std::wstring& Key) { return !StillPending.contains(Key); }), CL.m_Keys.end());
        CL.m_LiveGroupKeys.erase(std::remove_if(CL.m_LiveGroupKeys.begin(), CL.m_LiveGroupKeys.end(), [&](const std::wstring& GroupKey)
        {
            for (auto* Row : ResolvedRows)
            {
                const auto Class = ClassifyPendingPath(Row->m_RelativePath);
                if (Class.m_HexInstance.empty()) continue;
                if (SourceControlGlobalGroupKey(Row->m_Library, Class.m_TypeNameLower, Class.m_HexInstance) == GroupKey && StillPending.contains(Row->m_Key))
                    return false; // still pending - keep tracking this group live
            }
            return true; // nothing from this group remains pending - drop the now-empty live reference
        }), CL.m_LiveGroupKeys.end());
        CL.m_LiveCategoryKeys.erase(std::remove_if(CL.m_LiveCategoryKeys.begin(), CL.m_LiveCategoryKeys.end(), [&](const std::wstring& CategoryKey)
        {
            for (auto* Row : ResolvedRows)
            {
                const auto Class = ClassifyPendingPath(Row->m_RelativePath);
                if (SourceControlGlobalCategoryKey(Row->m_Library, Class.m_Category) == CategoryKey && StillPending.contains(Row->m_Key))
                    return false; // still pending - keep tracking this category live
            }
            return true; // nothing from this category remains pending - drop the now-empty live reference
        }), CL.m_LiveCategoryKeys.end());
        CL.m_LiveLibraryKeys.erase(std::remove_if(CL.m_LiveLibraryKeys.begin(), CL.m_LiveLibraryKeys.end(), [&](const std::wstring& LibraryKey)
        {
            for (auto* Row : ResolvedRows)
                if (SourceControlGlobalLibraryKey(Row->m_Library) == LibraryKey && StillPending.contains(Row->m_Key))
                    return false; // still pending - keep tracking this library live
            return true; // nothing from this library remains pending - drop the now-empty live reference
        }), CL.m_LiveLibraryKeys.end());
    }

    // Renders a changelist's (or the "(All)" pseudo-entry's) own file set - direct user design
    // (2026-09-18): "keep the folder form over just flatten things out... larger concepts [are]
    // easier to understand than piece by piece."
    //
    // CL == nullptr (the "(All)" pseudo-entry - no per-item live/flat tracking exists there at all)
    // groups EVERY classified row by its owning resource unconditionally, for the same readability
    // reason the left Pending Changes tree always folders a resource regardless of file count
    // (RenderSourceControlFolder never flattens a 1-file resource either). No category-level nesting
    // there - "(All)" is a flat everything-in-the-depot view, not backed by any live tracking to nest.
    //
    // CL != nullptr (a real changelist) is stricter and mirrors the Pending Changes tree's own
    // Library -> Category -> Resource nesting exactly - direct user correction, 2026-09-18: "if I open
    // B [a live category, or now a live library] I will find A [what it covers] in there... the same
    // way it was in the pending list." A row whose Library is one of CL's m_LiveLibraryKeys renders
    // nested under that library folder (via RenderSourceControlLibraryFolder, same as the left tree,
    // reusing a real sc_library_group built on the fly); otherwise a row whose (Library, Category) is
    // one of CL's m_LiveCategoryKeys renders nested under a category folder; otherwise a row whose
    // resource is one of CL's m_LiveGroupKeys renders as its own top-level resource folder. Everything
    // else (explicit m_Keys, including files that merely happen to share a resource with a live one)
    // stays flat, never auto-folding into a look-alike group. A row count is NEVER the deciding factor
    // for whether something folders (a live reference with only 1 pending file right now still renders
    // as a folder) - the ONLY thing that turns a live folder back into flat rows is
    // SourceControlRemoveRowFromChangelist actually dropping its live reference (removing one member
    // from inside it) - never a display-time heuristic here ("the only way that a folder should be
    // flatten is if I remove a file/folder from inside the folder").
    inline void RenderSourceControlChangelistFiles(xundo::system& Undo, const std::vector<sc_panel_row>& AllRows
        , const std::vector<const sc_panel_row*>& RowsToShow, const char* TableId, const sc_changelist* CL = nullptr) noexcept
    {
        struct changelist_group { std::wstring m_GroupKey; std::string m_DisplayName; std::vector<const sc_panel_row*> m_Rows; };
        struct changelist_category { std::wstring m_CategoryKey; sc_category m_Category; e10::library::guid m_Library; std::vector<changelist_group> m_Groups; std::vector<const sc_panel_row*> m_Ungrouped; };

        std::vector<sc_library_group> Libraries; // reuses the real left-tree type directly - a live
                                                  // library's rows slot into it exactly like BuildDepotGroups
                                                  // already does, so RenderSourceControlLibraryFolder
                                                  // renders it identically to the left tree.
        std::vector<changelist_category> Categories;
        std::vector<changelist_group> Groups;
        std::vector<const sc_panel_row*> Flat;

        for (auto* Row : RowsToShow)
        {
            const auto Class = ClassifyPendingPath(Row->m_RelativePath);

            if (CL != nullptr)
            {
                const auto LibraryKey = SourceControlGlobalLibraryKey(Row->m_Library);
                if (std::find(CL->m_LiveLibraryKeys.begin(), CL->m_LiveLibraryKeys.end(), LibraryKey) != CL->m_LiveLibraryKeys.end())
                {
                    auto LIt = std::find_if(Libraries.begin(), Libraries.end(), [&](const sc_library_group& L) { return L.m_Library == Row->m_Library; });
                    if (LIt == Libraries.end())
                    {
                        sc_library_group NewLib;
                        NewLib.m_Library     = Row->m_Library;
                        NewLib.m_DisplayName = SourceControlLibraryDisplayName(Row->m_Library);
                        for (std::size_t i = 0; i < NewLib.m_Categories.size(); ++i)
                            NewLib.m_Categories[i].m_Category = static_cast<sc_category>(i);
                        Libraries.push_back(std::move(NewLib));
                        LIt = std::prev(Libraries.end());
                    }
                    auto& Bucket = LIt->m_Categories[static_cast<std::size_t>(Class.m_Category)];
                    if (Class.m_HexInstance.empty()) { Bucket.m_UngroupedRows.push_back(Row); continue; }

                    const std::wstring LocalResKey = Class.m_TypeNameLower + L"|" + Class.m_HexInstance;
                    auto GIt = std::find_if(Bucket.m_ResourceGroups.begin(), Bucket.m_ResourceGroups.end(), [&](const sc_resource_group& G) { return G.m_GroupKey == LocalResKey; });
                    if (GIt == Bucket.m_ResourceGroups.end())
                    {
                        Bucket.m_ResourceGroups.push_back(sc_resource_group{ LocalResKey, SourceControlResolveResourceName(Row->m_Library, Class.m_TypeNameLower, Class.m_HexInstance), {} });
                        GIt = std::prev(Bucket.m_ResourceGroups.end());
                    }
                    GIt->m_Rows.push_back(Row);
                    continue;
                }

                const auto CategoryKey = SourceControlGlobalCategoryKey(Row->m_Library, Class.m_Category);
                if (std::find(CL->m_LiveCategoryKeys.begin(), CL->m_LiveCategoryKeys.end(), CategoryKey) != CL->m_LiveCategoryKeys.end())
                {
                    auto CIt = std::find_if(Categories.begin(), Categories.end(), [&](const changelist_category& C) { return C.m_CategoryKey == CategoryKey; });
                    if (CIt == Categories.end())
                    {
                        Categories.push_back(changelist_category{ CategoryKey, Class.m_Category, Row->m_Library, {}, {} });
                        CIt = std::prev(Categories.end());
                    }
                    if (Class.m_HexInstance.empty()) { CIt->m_Ungrouped.push_back(Row); continue; }

                    const std::wstring LocalResKey = Class.m_TypeNameLower + L"|" + Class.m_HexInstance;
                    auto GIt = std::find_if(CIt->m_Groups.begin(), CIt->m_Groups.end(), [&](const changelist_group& G) { return G.m_GroupKey == LocalResKey; });
                    if (GIt == CIt->m_Groups.end())
                    {
                        CIt->m_Groups.push_back(changelist_group{ LocalResKey, SourceControlResolveResourceName(Row->m_Library, Class.m_TypeNameLower, Class.m_HexInstance), {} });
                        GIt = std::prev(CIt->m_Groups.end());
                    }
                    GIt->m_Rows.push_back(Row);
                    continue;
                }
            }

            if (Class.m_HexInstance.empty()) { Flat.push_back(Row); continue; }

            const std::wstring GroupKey = SourceControlGlobalGroupKey(Row->m_Library, Class.m_TypeNameLower, Class.m_HexInstance);
            const bool bGroupable = (CL == nullptr) || (std::find(CL->m_LiveGroupKeys.begin(), CL->m_LiveGroupKeys.end(), GroupKey) != CL->m_LiveGroupKeys.end());
            if (!bGroupable) { Flat.push_back(Row); continue; }

            auto It = std::find_if(Groups.begin(), Groups.end(), [&](const changelist_group& G) { return G.m_GroupKey == GroupKey; });
            if (It == Groups.end())
            {
                Groups.push_back(changelist_group{ GroupKey, SourceControlResolveResourceName(Row->m_Library, Class.m_TypeNameLower, Class.m_HexInstance), {} });
                It = std::prev(Groups.end());
            }
            It->m_Rows.push_back(Row);
        }

        // NOTE: there is deliberately NO "group multiple separate live entries under a cosmetic
        // library heading" step here - direct user correction, 2026-09-18: "the library name should
        // not show up in the 'Files in' unless everything is in there." A library folder appears in
        // this view ONLY via a real m_LiveLibraryKeys reference (dragged directly, or auto-promoted by
        // SourceControlPromoteToLibraryIfFullyCovered once every pending row in that library resolves
        // to this SAME changelist) - partial coverage (e.g. Resources + Assets dragged separately, but
        // Project Files still pending elsewhere) stays as independent top-level folders, never
        // prematurely wrapped.

        if (Libraries.empty() && Categories.empty() && Groups.empty())
        {
            RenderSourceControlTable(Undo, AllRows, Flat, TableId);
            return;
        }

        ImGui::BeginChild(std::format("{}Scroll", TableId).c_str(), ImVec2(0, 0));
        for (auto& L : Libraries)
            RenderSourceControlLibraryFolder(Undo, AllRows, L);
        for (auto& C : Categories)
        {
            // Scenes & Levels nests under its own library's Resources entry when one is ALSO present
            // here (matches the Pending Changes tree's own nesting, and how dragging Resources now
            // always carries Scenes & Levels along too) - skip its own top-level slot to avoid
            // rendering it twice. If Resources isn't live here (only Scenes & Levels was dragged on
            // its own), fall through and render it as its own top-level folder below.
            if (C.m_Category == sc_category::ScenesAndLevels
                && std::any_of(Categories.begin(), Categories.end(), [&](const changelist_category& Other) { return Other.m_Category == sc_category::Resources && Other.m_Library == C.m_Library; }))
                continue;

            ImGui::PushID(xstrtool::To(C.m_CategoryKey).c_str());
            sc_category_bucket Bucket;
            Bucket.m_Category = C.m_Category;
            Bucket.m_UngroupedRows = C.m_Ungrouped;
            for (auto& G : C.m_Groups)
                Bucket.m_ResourceGroups.push_back(sc_resource_group{ G.m_GroupKey, G.m_DisplayName, G.m_Rows });

            if (C.m_Category == sc_category::Resources)
            {
                auto ScenesIt = std::find_if(Categories.begin(), Categories.end(), [&](const changelist_category& Other) { return Other.m_Category == sc_category::ScenesAndLevels && Other.m_Library == C.m_Library; });
                if (ScenesIt != Categories.end())
                {
                    sc_category_bucket ScenesBucket;
                    ScenesBucket.m_Category = sc_category::ScenesAndLevels;
                    ScenesBucket.m_UngroupedRows = ScenesIt->m_Ungrouped;
                    for (auto& G : ScenesIt->m_Groups)
                        ScenesBucket.m_ResourceGroups.push_back(sc_resource_group{ G.m_GroupKey, G.m_DisplayName, G.m_Rows });
                    RenderSourceControlCategoryFolder(Undo, AllRows, SourceControlCategoryLabel(C.m_Category), Bucket, C.m_Library, C.m_Category
                        , "Scenes & Levels", &ScenesBucket, sc_category::ScenesAndLevels);
                    ImGui::PopID();
                    continue;
                }
            }

            RenderSourceControlCategoryFolder(Undo, AllRows, SourceControlCategoryLabel(C.m_Category), Bucket, C.m_Library, C.m_Category);
            ImGui::PopID();
        }
        for (auto& G : Groups)
        {
            ImGui::PushID(xstrtool::To(G.m_GroupKey).c_str());
            RenderSourceControlFolder(Undo, AllRows, std::format("\xEE\xA3\x95 {}", G.m_DisplayName), G.m_Rows, G.m_GroupKey);
            ImGui::PopID();
        }
        if (!Flat.empty())
            RenderSourceControlTable(Undo, AllRows, Flat, std::format("{}Ungrouped", TableId).c_str(), 180.0f);
        ImGui::EndChild();
    }

    void RenderSourceControlPanel(xundo::system& Undo, bool bEmbedded = false) noexcept
    {
        auto& S = g_SourceControlPanel;

        if (!bEmbedded)
        {
            ImGui::SetNextWindowSize(ImVec2(760, 320), ImGuiCond_FirstUseEver);
            const bool bVisible = ImGui::Begin(e29::editor_tabs::kSourceControlWindow);
            if (!bVisible) { ImGui::End(); return; }
        }

        // Rebuild only when a background scan actually published something new - see
        // source_control_panel_state::m_CachedRows' own comment. BuildSourceControlRows() copies both
        // caches' contents out from under a mutex for every open library, then formats/sorts them
        // into rows - real, non-trivial work that has no reason to repeat 60 times a second.
        const std::uint64_t CurrentRevision = e10::source_control::SourceControlRevision().load(std::memory_order_relaxed);
        if (S.m_LastAppliedRevision != CurrentRevision)
        {
            S.m_CachedRows = BuildSourceControlRows();
            S.m_LastAppliedRevision = CurrentRevision;
        }
        const auto& Rows = S.m_CachedRows;

        // Depot selection + display name - computed here (not inside the Changelists panel below)
        // so it can render on the SAME line as "Pull All" (direct user request). Selected by
        // clicking a depot's own row in the LEFT tree (RenderSourceControlDepotTree); auto-picks a
        // sensible default (the only depot if there's just one, else the first alphabetically) so
        // the common single-depot case shows something useful with zero clicks.
        const auto AllDepots = CollectDistinctDepots(Rows);
        if (S.m_SelectedChangelistDepotKey.empty() || std::none_of(AllDepots.begin(), AllDepots.end(), [&](auto& D) { return D.first == S.m_SelectedChangelistDepotKey; }))
            S.m_SelectedChangelistDepotKey = AllDepots.empty() ? std::string{} : AllDepots.front().first;
        std::string SelectedDepotName;
        for (auto& [DepotKey, DepotName] : AllDepots)
            if (DepotKey == S.m_SelectedChangelistDepotKey) SelectedDepotName = DepotName;

        // Computed here (not after the top bar) so the depot name below can align its X position
        // with where the right "Changelists" column actually starts. User-resizable via a draggable
        // divider (direct user request: "the window should have a splitter between the right and
        // the left sections, just like the other windows" - the Assets view named as the concrete
        // example) - reuses e10::assert_browser::Splitter (made public for exactly this kind of
        // reuse) rather than a second, separately-invented draggable-divider implementation.
        // S.m_SplitSize persists the LEFT width across frames; only initialized once (negative
        // sentinel) and clamped every frame after.
        constexpr float SplitterThickness = 4.0f;
        const float TotalWidth = ImGui::GetContentRegionAvail().x;
        if (S.m_SplitSize < 0.0f) S.m_SplitSize = TotalWidth * 0.55f;
        S.m_SplitSize = std::clamp(S.m_SplitSize, 100.0f, std::max(100.0f, TotalWidth - 100.0f - SplitterThickness));
        float LeftWidth  = S.m_SplitSize;
        float RightWidth = TotalWidth - LeftWidth - SplitterThickness;

        if (ImGui::Button("Pull All"))
        {
            for (auto& Lib : e10::g_LibMgr.m_mLibraryDB)
            {
                S.m_StatusLine = SourceControlRunQuery(Undo, std::format("SourceControlPull -Library {}", e29::commands::FormatLibraryGuid(Lib.first)));
                e29::source_control::LaunchSourceControlStatusScan(Lib.second->m_Library.m_Path); // Pull doesn't itself bump the revision - kick a fresh scan so the list catches up promptly
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%zu pending change(s) across %zu open library/ies", Rows.size(), (std::size_t)e10::g_LibMgr.m_mLibraryDB.size());

        // Aligned with the Changelists column below it (direct user request) - SameLine's offset
        // parameter is an absolute X position from the window's own left edge, the same LeftWidth
        // the "SCChangelists" child below is positioned at via ImGui::SameLine() right after
        // "SCPending" ends (BeginChild's own implicit cursor placement).
        ImGui::SameLine(LeftWidth + ImGui::GetStyle().ItemSpacing.x);
        if (SelectedDepotName.empty())
            ImGui::TextDisabled("Changelists for: (no depot selected)");
        else
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Changelists for: \xEE\xA3\xB1 %s", SelectedDepotName.c_str());

        // Splitter drawn right here, at the cursor position where the two columns are about to
        // start (same convention e10::assert_browser::Splitter's own established call sites use) -
        // it positions its drag-handle using *size1's CURRENT value, updates size1/size2 only while
        // actively being dragged, then restores the cursor so the BeginChild calls below start
        // exactly where they would have without it.
        e10::assert_browser::Splitter(true, SplitterThickness, &LeftWidth, &RightWidth, 100.0f, 100.0f, TotalWidth, ImGui::GetContentRegionAvail().y);
        S.m_SplitSize = LeftWidth;

        ImGui::BeginChild("SCPending", ImVec2(LeftWidth, -ImGui::GetFrameHeightWithSpacing()), true);
        ImGui::TextUnformatted("Pending Changes");
        {
            // TEMP diagnostic (2026-09-17) - edge-print only (not every frame) so we can confirm
            // whether the RENDER loop ever actually observes IsScanInProgress()==true at all.
            static bool bWasScanning = false;
            const bool bIsScanning = e29::source_control::IsScanInProgress();
            if (bIsScanning != bWasScanning)
            {
                std::printf("[SC] panel observed IsScanInProgress() -> %s at t=%.3f\n", bIsScanning ? "true" : "false", ImGui::GetTime());
                std::fflush(stdout);
                bWasScanning = bIsScanning;
            }
        }
        if (e29::source_control::IsScanInProgress())
            S.m_SpinnerVisibleUntil = ImGui::GetTime() + 0.4; // keep bumping forward while genuinely scanning
        if (ImGui::GetTime() < S.m_SpinnerVisibleUntil)
        {
            // Direct user request: without this, nothing on screen distinguishes "scan finished,
            // this IS the whole list" from "still filling in" - the chunked/parallel scan (previous
            // phase) means the list can legitimately grow for a little while after the tab opens.
            // The 0.4s floor above (not just a raw IsScanInProgress() check) matters in practice: a
            // scan of a small repo can start and finish inside one frame, and without a minimum
            // visible duration the spinner would never actually be seen even though it ran for real.
            ImGui::SameLine();
            const float LineH = ImGui::GetTextLineHeight();
            const float R     = LineH * 0.55f; // real, always-visible size - see DrawLoadingSpinner's own comment on the sizing bug this replaces
            const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
            const ImVec2 Center{ CursorPos.x + R, CursorPos.y + LineH * 0.5f };
            DrawLoadingSpinner(ImGui::GetWindowDrawList(), Center, R, IM_COL32(230, 180, 60, 255));
            ImGui::Dummy(ImVec2(R * 2.0f + 4.0f, LineH));
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Scanning for changes...");
                ImGui::TextDisabled("The list below may still be filling in.");
                ImGui::EndTooltip();
            }
        }
        // Fills the rest of the left panel now - the standalone "Commit message..." + "Commit
        // Selected" bar that used to live below this was removed (direct user decision, 2026-09-17):
        // it duplicated the right panel's own changelist-based commit flow. Committing any
        // granularity - a whole folder, a sub-selection, or one file - now goes through ONE path:
        // drag/assign the selection into a changelist (an existing one, or a quick new one created
        // for exactly this) and comment/commit from there.
        ImGui::BeginChild("SCDepotTreeScroll", ImVec2(0, 0));
        // Drop target for "SC_DRAG_SELECTION" - direct user request: dragging back FROM a changelist's
        // "Files in" box TO here restores it to the auto-tree, and "no matter how much back and
        // forth... if I move all the folders from Files In back to the Pending list[,] the Pending
        // list will be restored back fully as it was originally." A resource/category folder drag
        // (m_DragSourceScope tagged) is removed as ONE clean live reference (via the same
        // Assign*ToChangelist(..., -1) "unassign" path a real assign already supports) rather than
        // flattening then re-erasing row by row; a plain row/multi-select drag goes through
        // SourceControlRemoveRowFromChangelist per row, same as the context menu's own "Remove from
        // Changelist" action. BeginDragDropTargetCustom + an explicit rect, NOT the bare
        // BeginDragDropTarget() - see the file box's own drop-target fix for why the bare form binds
        // to the wrong item.
        {
            const ImVec2 PendingBoxMin = ImGui::GetWindowPos();
            const ImVec2 PendingBoxSize = ImGui::GetWindowSize();
            const ImRect PendingBoxRect(PendingBoxMin, ImVec2(PendingBoxMin.x + PendingBoxSize.x, PendingBoxMin.y + PendingBoxSize.y));
            if (ImGui::BeginDragDropTargetCustom(PendingBoxRect, ImGui::GetID("SCPendingDropTarget")))
            {
                if (ImGui::AcceptDragDropPayload("SC_DRAG_SELECTION"))
                {
                    // Category/Resource kinds go through SourceControlDetachScopeRows, NOT a direct
                    // Assign*ToChangelist(..., -1) call - real bug found live: that direct call is a
                    // no-op when the item is nested inside a BROADER live reference (e.g. "Scene 1"
                    // covered by a live "Resources" category, or "Resources" covered by a live
                    // library) rather than its own independent entry, since m_LiveGroupKeys/
                    // m_LiveCategoryKeys never actually held this item's key in that case. Library
                    // kind is unaffected (never nested under anything broader) so it keeps the direct
                    // call, which already worked correctly.
                    if (S.m_DragSourceScope.has_value() && S.m_DragSourceScope->m_Kind == source_control_panel_state::drag_scope::kind::Resource)
                        SourceControlDetachScopeRows(Rows, *S.m_DragSourceScope);
                    else if (S.m_DragSourceScope.has_value() && S.m_DragSourceScope->m_Kind == source_control_panel_state::drag_scope::kind::Category)
                        SourceControlDetachScopeRows(Rows, *S.m_DragSourceScope);
                    else if (S.m_DragSourceScope.has_value() && S.m_DragSourceScope->m_Kind == source_control_panel_state::drag_scope::kind::Library)
                        SourceControlAssignLibraryToChangelist(Rows, S.m_DragSourceScope->m_Key, -1);
                    else
                        for (auto& Key : S.m_MultiSelectOrder)
                        {
                            auto ItRow = std::find_if(Rows.begin(), Rows.end(), [&](const sc_panel_row& R) { return R.m_Key == Key; });
                            if (ItRow != Rows.end()) SourceControlRemoveRowFromChangelist(Rows, *ItRow);
                        }
                    S.m_DragSourceScope.reset();
                }
                ImGui::EndDragDropTarget();
            }
        }
        RenderSourceControlDepotTree(Undo, Rows);
        ImGui::EndChild();
        ImGui::EndChild(); // closes the OUTER "SCPending" child (BeginChild above) - real bug found
                            // live: removing the "Commit Selected" block took this call with it,
                            // leaving "SCPending" never closed - ImGui's own End() at the bottom of
                            // this function then asserts "Must call EndChild() and not End()!" and
                            // corrupts the window's layout state for the rest of that frame and beyond.

        ImGui::SameLine();

        ImGui::BeginChild("SCChangelists", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true);

        // Depot name now renders up on the shared top bar, next to "Pull All" (direct user
        // request) - AllDepots/SelectedDepotName are computed there, once, before either child
        // panel begins; this panel just uses S.m_SelectedChangelistDepotKey directly.
        ImGui::TextUnformatted("Changelists");
        ImGui::Separator();

        if (!S.m_SelectedChangelistDepotKey.empty())
        {
            const auto& DepotKey = S.m_SelectedChangelistDepotKey;
            ImGui::PushID(DepotKey.c_str());

            // "(All)" - the pseudo-entry representing "the actual depot which mean all" (direct
            // user design), ALWAYS first. Not a real sc_changelist - selecting it sets the -2
            // sentinel (see m_AllDepotCommentByDepot's own comment); its own file list (below) is
            // every pending row in this depot, not scoped to any custom-changelist assignment.
            {
                const bool bAllSelected = (S.m_ActiveChangelist == -2);
                ImGui::Selectable(std::format("  (All) ({})", RowsForDepot(Rows, DepotKey).size()).c_str(), bAllSelected);
                if (ImGui::IsItemClicked()) S.m_ActiveChangelist = -2;
            }

            // Just a picker here - NOT a drop target (direct user correction: "User should drag and
            // drop in the file box not in the list box[,] of the change list"). Dropping happens on
            // the FILE box below, for whichever changelist is currently active - selecting one here
            // first, then dropping into its own file list, matches "select one, then its files show
            // below" more directly than dropping straight onto a bare name in this list.
            for (std::size_t i = 0; i < S.m_Changelists.size(); ++i)
            {
                if (S.m_Changelists[i].m_DepotKey != DepotKey) continue;
                ImGui::PushID(static_cast<int>(i));
                const bool bSelected = (S.m_ActiveChangelist == static_cast<int>(i));
                ImGui::Selectable(std::format("  {} ({})", S.m_Changelists[i].m_Name, S.m_Changelists[i].m_Keys.size()).c_str(), bSelected);
                if (ImGui::IsItemClicked()) S.m_ActiveChangelist = static_cast<int>(i);
                ImGui::PopID();
            }

            auto& NameBuf = S.m_NewChangelistNameByDepot[DepotKey];
            ImGui::SetNextItemWidth(-60.0f);
            ImGui::InputTextWithHint("##NewChangelist", "New changelist name...", NameBuf.data(), NameBuf.size());
            ImGui::SameLine();
            if (ImGui::Button("+ New") && NameBuf[0] != '\0')
            {
                S.m_Changelists.push_back(sc_changelist{ DepotKey, NameBuf.data(), "", {} });
                S.m_ActiveChangelist = static_cast<int>(S.m_Changelists.size()) - 1;
                NameBuf[0] = '\0';
            }
            ImGui::PopID();
            ImGui::Separator();
        }

        if (S.m_ActiveChangelist == -2 && !S.m_SelectedChangelistDepotKey.empty())
        {
            // "(All)" - same comment/commit/files shape as a real changelist below, sourced from
            // EVERY pending row in this depot instead of one changelist's own m_Keys. No drag-drop
            // target here (direct user design implies dropping only makes sense onto a real,
            // assignable changelist - "All" already includes everything by definition).
            const auto& DepotKey = S.m_SelectedChangelistDepotKey;
            auto& CommentBuf = S.m_AllDepotCommentByDepot[DepotKey];
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextMultiline("##SCAllComment", CommentBuf.data(), CommentBuf.size(), ImVec2(-1.0f, 60));

            auto DepotRows = RowsForDepot(Rows, DepotKey);
            const bool bCanCommit = !DepotRows.empty() && CommentBuf[0] != '\0';
            if (!bCanCommit) ImGui::BeginDisabled();
            if (ImGui::Button("Commit && Push"))
            {
                std::vector<std::wstring> Keys;
                Keys.reserve(DepotRows.size());
                for (auto* Row : DepotRows) Keys.push_back(Row->m_Key);
                S.m_StatusLine = SourceControlCommitKeys(Undo, Keys, CommentBuf.data());
                CommentBuf[0] = '\0';
            }
            if (!bCanCommit) ImGui::EndDisabled();

            if (!S.m_StatusLine.empty())
                ImGui::TextWrapped("%s", S.m_StatusLine.c_str());

            ImGui::Separator();
            ImGui::TextDisabled("Files (all pending in this depot):");
            ImGui::BeginChild("SCChangelistFiles", ImVec2(0, 0), true);
            RenderSourceControlChangelistFiles(Undo, Rows, DepotRows, "SCChangelistTable");
            ImGui::EndChild();
        }
        else if (S.m_ActiveChangelist >= 0 && S.m_ActiveChangelist < static_cast<int>(S.m_Changelists.size()))
        {
            auto& CL = S.m_Changelists[S.m_ActiveChangelist];

            // Comment + Commit sit ABOVE the file list (direct user request) - the file list below
            // then stretches to fill whatever's left, maximizing its use of the tab's space instead of
            // being capped at a fixed height.
            char CommentBuf[4096];
            std::snprintf(CommentBuf, sizeof(CommentBuf), "%s", CL.m_Comment.c_str());
            if (ImGui::InputTextMultiline("##SCComment", CommentBuf, sizeof(CommentBuf), ImVec2(-1.0f, 60)))
                CL.m_Comment = CommentBuf;

            // Resolved ONCE here (explicit keys + live-group members, fresh) and reused below for both
            // the "can commit" check and the file-box display, so they never disagree.
            const auto ChangelistRows = SourceControlResolveChangelistRows(Rows, CL);
            const bool bCanCommit = !ChangelistRows.empty() && !CL.m_Comment.empty();
            if (!bCanCommit) ImGui::BeginDisabled();
            if (ImGui::Button("Commit && Push"))
                SourceControlCommitChangelist(Undo, Rows, CL);
            if (!bCanCommit) ImGui::EndDisabled();

            if (!S.m_StatusLine.empty())
                ImGui::TextWrapped("%s", S.m_StatusLine.c_str());

            ImGui::Separator();
            ImGui::TextDisabled("Files in \"%s\":", CL.m_Name.c_str());
            // Size (0,0): the last element in this child, so it fills every remaining pixel down to
            // the bottom of the tab rather than a fixed height. Drop target for "SC_DRAG_SELECTION"
            // (direct user correction: "User should drag and drop in the file box not in the list
            // box[,] of the change list") - drops the CURRENT global multi-selection (set by the drag
            // source at pickup time) into THIS active changelist.
            ImGui::BeginChild("SCChangelistFiles", ImVec2(0, 0), true);
            // Real bug found live (2026-09-17, direct user report: "the target is in the list box in
            // the name of the change list. That should not be the case"): the bare
            // ImGui::BeginDragDropTarget() does NOT bind to "the current child window" - per its own
            // implementation (imgui.cpp), it binds to whatever g.LastItemData was, i.e. the LAST
            // ITEM SUBMITTED BEFORE BeginChild() (here, the "Files in ..." label, itself right below
            // the changelist picker section) - BeginChild() only registers the child frame as an item
            // of the PARENT window at EndChild() time, not at BeginChild() time. So the real
            // drop-accept rect ended up bound near/inside the changelist list section above, not this
            // file box. Fixed with BeginDragDropTargetCustom() + an EXPLICIT rect taken from THIS
            // child window's own bounds (valid now that we're actually inside it) - this is the
            // correct way to make a whole child window (not just its last item) a drop target.
            const ImVec2 SCFilesBoxMin = ImGui::GetWindowPos();
            const ImVec2 SCFilesBoxSize = ImGui::GetWindowSize();
            const ImRect SCFilesBoxRect(SCFilesBoxMin, ImVec2(SCFilesBoxMin.x + SCFilesBoxSize.x, SCFilesBoxMin.y + SCFilesBoxSize.y));
            if (ImGui::BeginDragDropTargetCustom(SCFilesBoxRect, ImGui::GetID("SCChangelistFilesDropTarget")))
            {
                if (ImGui::AcceptDragDropPayload("SC_DRAG_SELECTION"))
                {
                    // A changelist belongs to exactly ONE depot - real bug found live: dragging from a
                    // DIFFERENT depot into this changelist was never checked, letting one changelist
                    // end up spanning two repos it could never actually commit together. Reject the
                    // whole drop (no partial assignment) when the dragged selection isn't entirely
                    // from CL's own depot - same invariant the "Add to Changelist" context menu already
                    // enforces, just missing here until now. A scoped (folder) drag is checked against
                    // its OWN key, not m_MultiSelectOrder - see SourceControlFindScopeDepotMismatch's
                    // own comment for why. Both variants return the actual mismatching row (not just a
                    // bool) so the message can name it - real user confusion found live: two different
                    // libraries can have a pending file at the identical relative path (e.g. each
                    // library's own "library.config.txt"), so an unqualified "wrong depot" message left
                    // the user unable to tell what was actually dragged.
                    const sc_panel_row* pMismatch = S.m_DragSourceScope.has_value()
                        ? SourceControlFindScopeDepotMismatch(Rows, *S.m_DragSourceScope, CL.m_DepotKey)
                        : SourceControlFindDepotMismatch(Rows, S.m_MultiSelectOrder, CL.m_DepotKey);
                    if (pMismatch != nullptr)
                    {
                        auto TargetDepotRowIt = std::find_if(Rows.begin(), Rows.end(), [&](const sc_panel_row& R) { return SourceControlDepotKeyAndName(R.m_Library).first == CL.m_DepotKey; });
                        const std::string TargetDepotName = (TargetDepotRowIt != Rows.end()) ? SourceControlDepotKeyAndName(TargetDepotRowIt->m_Library).second : CL.m_DepotKey;
                        S.m_StatusLine = std::format("Can't move \"{}\" into \"{}\" - it belongs to {}, not {} (a changelist can only hold files from one depot)."
                            , xstrtool::To(pMismatch->m_RelativePath), CL.m_Name
                            , SourceControlDepotKeyAndName(pMismatch->m_Library).second, TargetDepotName);
                    }
                    // A resource-folder drag (RenderSourceControlFolder) or a category-folder drag
                    // (RenderSourceControlCategoryFolder / the top-level category node) tags
                    // m_DragSourceScope - assign the WHOLE concept as a live reference (direct user
                    // design: "drags the 'Scene 1' folder... more entities ready to check in... will
                    // automatically be included"; a category drag additionally absorbs any
                    // already-tracked subset - "the superset already includes the subset"). A plain
                    // row drag is a flat, explicit pick. Category/Resource kinds are DETACHED first
                    // (SourceControlDetachScopeRows) - real bug found live: re-dragging an item that's
                    // currently nested inside a BROADER live reference (e.g. moving "Scene 1" - itself
                    // covered by a live "Resources" category - into a DIFFERENT changelist) must
                    // actually pull it out of that broader reference first, or it silently ends up
                    // "in" both changelists at once since the covering reference was never touched.
                    else if (S.m_DragSourceScope.has_value() && S.m_DragSourceScope->m_Kind == source_control_panel_state::drag_scope::kind::Resource)
                    {
                        SourceControlDetachScopeRows(Rows, *S.m_DragSourceScope);
                        SourceControlAssignGroupToChangelist(Rows, S.m_DragSourceScope->m_Key, S.m_ActiveChangelist);
                    }
                    else if (S.m_DragSourceScope.has_value() && S.m_DragSourceScope->m_Kind == source_control_panel_state::drag_scope::kind::Category)
                    {
                        SourceControlDetachScopeRows(Rows, *S.m_DragSourceScope);
                        SourceControlAssignCategoryToChangelist(Rows, S.m_DragSourceScope->m_Key, S.m_ActiveChangelist);
                        if (S.m_DragSourceScope->m_ImplicitChildKey.has_value())
                            SourceControlAssignCategoryToChangelist(Rows, *S.m_DragSourceScope->m_ImplicitChildKey, S.m_ActiveChangelist);
                    }
                    else if (S.m_DragSourceScope.has_value() && S.m_DragSourceScope->m_Kind == source_control_panel_state::drag_scope::kind::Library)
                        SourceControlAssignLibraryToChangelist(Rows, S.m_DragSourceScope->m_Key, S.m_ActiveChangelist);
                    else
                        for (auto& Key : S.m_MultiSelectOrder) SourceControlAssignToChangelist(Key, S.m_ActiveChangelist);
                    S.m_DragSourceScope.reset();
                }
                ImGui::EndDragDropTarget();
            }
            RenderSourceControlChangelistFiles(Undo, Rows, ChangelistRows, "SCChangelistTable", &CL);
            ImGui::EndChild();
        }
        ImGui::EndChild();

        // Stable, always-reached call site for the "Undo Changes" confirm modal - see
        // source_control_panel_state::m_bUndoChangesConfirmPending's own comment for why this can't
        // live inline at the MenuItem that requests it.
        RenderSourceControlUndoChangesConfirmModal(Undo, Rows);

        if (!bEmbedded)
            ImGui::End();
    }
}

#endif // E29_PANEL_SOURCE_CONTROL_H
