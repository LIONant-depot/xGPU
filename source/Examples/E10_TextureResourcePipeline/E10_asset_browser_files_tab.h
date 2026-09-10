#ifndef E10_ASSET_BROWSER_FILES_TAB_H
#define E10_ASSET_BROWSER_FILES_TAB_H
#pragma once

#include <unordered_set>
#include <array>
#include <cstring>
#include <cstdio>
#include <format>
#include "imgui_internal.h"     // For BeginDragDropTargetCustom (background drop target, 5C)

// Asset (real filesystem) window - Phase 3 of the Asset Browser window-split plan (see plan file
// lively-knitting-sifakis.md). READ-ONLY BROWSING ONLY - Phase 4 adds Copy/Cut/Rename/Delete plus
// cascade-updating any descriptor that references a moved/renamed file; none of that exists yet, this
// phase is purely visual/navigational so it carries no lock-order or command-design risk at all.
// DOCKABLE-only (see browser_registration<>'s own comment in E10_AssetBrowser.h).
//
// Left = folder tree of each library's REAL Assets/ folder on disk (<Library.m_Path>/Assets - NOT
// library_db's virtual descriptor tree, which is what virtual_tree_tab already browses). Right = the
// files (and subfolders, Explorer-style) inside the selected folder, with a navigation bar (back/
// forward/history + clickable breadcrumb) deliberately styled and structured as close as reasonably
// possible to virtual_tree_tab's own RenderNavigationPath()/RenderPath() - same background bar,
// transparent buttons, bold current segment, per-segment ">" sibling-jump popup, back/forward/history
// with pruned-on-navigate history list - adapted from (library, folder-guid) pairs to
// (library, std::filesystem::path) pairs, since this browses real folders, not descriptor-tree nodes.
//
// Rebuilds its directory listing from disk every frame for every expanded node, same as
// virtual_tree_tab already rebuilds its own asset grid from library_db every frame - no caching layer,
// matching this project's own "keep it simple first" discipline. Real project Assets/ folders are not
// large enough for this to matter in practice; revisit only if it demonstrably does.

namespace e10
{
    struct files_tab : e10::asset_browser_tab_base
    {
        files_tab(assert_browser& Browser, const char* pName)
            : asset_browser_tab_base{ Browser, pName }
            , m_AssetMgr{ *Browser.getAssetMgr() }
        {
        }

        //=============================================================================
        // Folder icon - drawn as actual vector shapes (ImDrawList), NOT a font glyph. Three different
        // icon-font codepoints were tried first (\xEE\xA3\x95/\xEE\xA2\xB7 - the pair virtual_tree_tab/
        // E29::FolderIcon use - and \xEE\xA2\xA5, the "Assets" tab label's own icon), and a zoomed
        // screenshot proved ALL THREE render as the same missing-glyph tofu box in this build - even
        // the tab label icon, which only looked like a real icon at normal screenshot resolution. Given
        // that pattern, the back/forward navigation chevrons below are ALSO drawn as vector shapes
        // rather than gambling on more codepoints from the same broken font. Empty vs. full folder is
        // the fill color (dim vs. tan) rather than a different shape - "empty" means no entries at all
        // on disk (neither files nor subfolders).
        static void DrawFolderIconAt(ImVec2 P0, float Size, bool bEmpty) noexcept
        {
            auto*       pDL   = ImGui::GetWindowDrawList();
            const ImU32 Color = bEmpty ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : IM_COL32(255, 205, 90, 255);

            const float TabW  = Size * 0.55f;
            const float TabH  = Size * 0.22f;
            const float BodyY = P0.y + TabH * 0.7f;

            pDL->AddRectFilled(ImVec2(P0.x, P0.y), ImVec2(P0.x + TabW, BodyY + 1.0f), Color, 1.0f, ImDrawFlags_RoundCornersTop);
            pDL->AddRectFilled(ImVec2(P0.x, BodyY), ImVec2(P0.x + Size, P0.y + Size), Color, 1.5f, ImDrawFlags_RoundCornersBottom);
        }

        // For rows with no tree arrow (RightPanel's flat Selectable list) - icon immediately precedes
        // the label, as its own reserved-space item.
        static void DrawFolderIcon(bool bEmpty) noexcept
        {
            const float  Size = ImGui::GetTextLineHeight() * 0.85f;
            const ImVec2 P0   = ImGui::GetCursorScreenPos();
            DrawFolderIconAt(P0, Size, bEmpty);
            ImGui::Dummy(ImVec2(Size, Size));
            ImGui::SameLine(0.0f, 2.0f);
        }

        // For tree rows: TreeNodeEx draws its own arrow + label as one atomic call, so the icon can't
        // simply precede it the way DrawFolderIcon() does - it belongs AFTER the arrow, not before it.
        // Reserves a gap sized in leading spaces (the base font, Consolas, is monospace, so N spaces
        // reserves an exact, predictable pixel width) baked into the label text itself, then paints the
        // icon into that gap using ImGui::GetTreeNodeToLabelSpacing() - the exact, ImGui-documented
        // arrow-to-label offset - measured from the cursor position captured BEFORE the TreeNodeEx call
        // (NOT GetItemRectMin() afterward - under ImGuiTreeNodeFlags_SpanFullWidth that returns the
        // full-row HIT-BOX origin at the window's own left edge, not the indent-adjusted arrow position,
        // which under-shoots more at every nesting depth and paints the icon on top of the arrow).
        bool TreeNodeWithFolderIcon(const std::string& Name, ImGuiTreeNodeFlags Flags, bool bEmpty) noexcept
        {
            const float Size       = ImGui::GetTextLineHeight() * 0.85f;
            const float SpaceWidth = ImGui::CalcTextSize(" ").x;
            const int   NumSpaces  = static_cast<int>(std::ceil(Size / SpaceWidth));

            const ImVec2 RowStart = ImGui::GetCursorScreenPos();

            const std::string PaddedLabel = std::string(static_cast<std::size_t>(NumSpaces), ' ') + Name;
            const bool bOpen = ImGui::TreeNodeEx(PaddedLabel.c_str(), Flags);

            const ImVec2 ItemMax = ImGui::GetItemRectMax();
            const ImVec2 IconPos
            {
                RowStart.x + ImGui::GetTreeNodeToLabelSpacing(),
                RowStart.y + (ItemMax.y - RowStart.y - Size) * 0.5f
            };
            DrawFolderIconAt(IconPos, Size, bEmpty);

            return bOpen;
        }

        // Vector-drawn back/forward chevron, matching virtual_tree_tab's own ScaleButton-rendered
        // "\xEE\x9C\xAB"/"\xEE\x9C\xAA" glyphs functionally (a disable-able small nav button) without
        // the same missing-glyph risk (see this struct's own top comment).
        static bool ChevronButton(const char* pStrID, bool bLeft, bool bEnabled) noexcept
        {
            ImGui::PushID(pStrID);
            ImGui::BeginDisabled(!bEnabled);

            const float  Size = ImGui::GetFrameHeight();
            const ImVec2 P0   = ImGui::GetCursorScreenPos();
            const bool   bClicked = ImGui::InvisibleButton("##chevron", ImVec2(Size, Size));
            const bool   bHovered = ImGui::IsItemHovered();

            auto* pDL = ImGui::GetWindowDrawList();
            if (bHovered && bEnabled)
                pDL->AddRectFilled(P0, ImVec2(P0.x + Size, P0.y + Size), ImGui::GetColorU32(ImGuiCol_ButtonHovered), 3.0f);

            const ImU32 Color = ImGui::GetColorU32(bEnabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
            const float Pad   = Size * 0.32f;
            if (bLeft)
                pDL->AddTriangleFilled(ImVec2(P0.x + Size - Pad, P0.y + Pad), ImVec2(P0.x + Size - Pad, P0.y + Size - Pad), ImVec2(P0.x + Pad, P0.y + Size * 0.5f), Color);
            else
                pDL->AddTriangleFilled(ImVec2(P0.x + Pad, P0.y + Pad), ImVec2(P0.x + Pad, P0.y + Size - Pad), ImVec2(P0.x + Size - Pad, P0.y + Size * 0.5f), Color);

            ImGui::EndDisabled();
            ImGui::PopID();
            return bClicked && bEnabled;
        }

        static bool IsFolderEmpty(const std::filesystem::path& FullPath) noexcept
        {
            std::error_code Ec;
            for (auto& Entry : std::filesystem::directory_iterator(FullPath, Ec))
            {
                (void)Entry;
                return false;
            }
            return true;
        }

        // Whether Ancestor is Target itself or a proper prefix of it (component-wise, not substring) -
        // used to decide which tree nodes to force-open so a history/breadcrumb jump is visible in the
        // tree, mirroring virtual_tree_tab's own OpenLeftTreeTo.
        static bool IsAncestorOrSelf(const std::filesystem::path& Ancestor, const std::filesystem::path& Target) noexcept
        {
            auto AIt = Ancestor.begin(), AEnd = Ancestor.end();
            auto TIt = Target.begin(), TEnd = Target.end();
            for (; AIt != AEnd; ++AIt, ++TIt)
            {
                if (TIt == TEnd || *AIt != *TIt) return false;
            }
            return true;
        }

        //=============================================================================
        // Path history - same shape as virtual_tree_tab's own m_PathHistoryList/m_PathHistoryListRU/
        // m_PathHistoryIndex (UpdateHistoryLRU/PathHistoryUpdate there), just keyed on
        // (library::guid, std::filesystem::path) instead of (library::guid, e10::folder::guid). This is
        // the ONE place selection changes - every navigation (tree click, breadcrumb, sibling popup,
        // history list, right-panel double-click) routes through PathHistoryUpdate() so back/forward
        // stays consistent regardless of where the click came from.

        struct path_history_entry { library::guid m_gLibrary; std::filesystem::path m_Folder; };

        // One row of the RightPanel table - see RightPanel()'s own m_CachedEntries comment for why this
        // is now cached instead of rebuilt from disk every frame.
        struct file_entry { std::wstring m_Name; bool m_bDirectory; std::uintmax_t m_Size; std::filesystem::file_time_type m_LastWriteTime; };

        void UpdateHistoryLRU() noexcept
        {
            auto& Cur = m_PathHistoryList[m_PathHistoryIndex];

            bool bFound = false;
            for (auto& E : m_PathHistoryListRU)
            {
                if (E.m_gLibrary == Cur.m_gLibrary && E.m_Folder == Cur.m_Folder)
                {
                    path_history_entry Temp(std::move(E));
                    m_PathHistoryListRU.erase(m_PathHistoryListRU.begin() + static_cast<int>(&E - m_PathHistoryListRU.data()));
                    m_PathHistoryListRU.insert(m_PathHistoryListRU.begin(), std::move(Temp));
                    bFound = true;
                    break;
                }
            }
            if (!bFound) m_PathHistoryListRU.insert(m_PathHistoryListRU.begin(), Cur);
            if (m_PathHistoryListRU.size() > 50) m_PathHistoryListRU.resize(50);

            // Multi-select is scoped to one open folder (matches Explorer's own per-folder selection
            // reset, and E29's own per-scene reset for its entity multi-select) - only clear on an
            // ACTUAL folder change, not every call (UpdateHistoryLRU also runs on a re-click of the
            // already-open folder via the breadcrumb, which shouldn't wipe an in-progress selection).
            if (m_SelectedLibrary != Cur.m_gLibrary || m_SelectedFolder != Cur.m_Folder)
            {
                ClearMultiSelect();
                m_bEntriesCacheDirty = true;
            }

            m_SelectedLibrary = Cur.m_gLibrary;
            m_SelectedFolder  = Cur.m_Folder;
            m_SelectedFile.clear();
            m_ExpandToFolder  = Cur.m_Folder;
        }

        //=============================================================================
        // Multi-select - mirrors E29's own entity multi-select model (E29_LevelSceneEditorKit.h's
        // editor_state: unordered_set for membership + a parallel vector for click order, plain click
        // replaces/collapses to one, Ctrl-click toggles membership without touching the anchor) with
        // one addition that model doesn't have: Shift range-select, real Explorer parity this codebase
        // didn't have anywhere yet. m_MultiSelectAnchor is the range-select start point (the last
        // PLAIN click); Ctrl-click never moves it, matching E29's own "ctrl-click never touches primary
        // selection" rule.
        void ClearMultiSelect() noexcept
        {
            m_MultiSelected.clear();
            m_MultiSelectOrder.clear();
            m_MultiSelectAnchor.clear();
        }

        void SelectSingle(const std::wstring& Name) noexcept
        {
            m_MultiSelected.clear();
            m_MultiSelectOrder.clear();
            m_MultiSelected.insert(Name);
            m_MultiSelectOrder.push_back(Name);
            m_MultiSelectAnchor = Name;
        }

        void ToggleMultiSelect(const std::wstring& Name) noexcept
        {
            if (auto It = m_MultiSelected.find(Name); It != m_MultiSelected.end())
            {
                m_MultiSelected.erase(It);
                m_MultiSelectOrder.erase(std::find(m_MultiSelectOrder.begin(), m_MultiSelectOrder.end(), Name));
            }
            else
            {
                m_MultiSelected.insert(Name);
                m_MultiSelectOrder.push_back(Name);
            }

            // Seed the anchor if nothing has been plain-clicked yet this folder, so a Shift-click with
            // no prior plain click still has somewhere sensible to range from.
            if (m_MultiSelectAnchor.empty()) m_MultiSelectAnchor = Name;
        }

        // SortedNames is the CURRENT on-screen sorted order (folders first, then alphabetical - the
        // exact order RightPanel's own table already sorts into) so the range matches what the user
        // visually sees between the anchor and the Shift-clicked row.
        void RangeSelect(const std::vector<std::wstring>& SortedNames, const std::wstring& ToName) noexcept
        {
            if (m_MultiSelectAnchor.empty())
            {
                SelectSingle(ToName);
                return;
            }

            auto ItAnchor = std::find(SortedNames.begin(), SortedNames.end(), m_MultiSelectAnchor);
            auto ItTo     = std::find(SortedNames.begin(), SortedNames.end(), ToName);
            if (ItAnchor == SortedNames.end() || ItTo == SortedNames.end())
            {
                SelectSingle(ToName);
                return;
            }

            if (ItAnchor > ItTo) std::swap(ItAnchor, ItTo);

            m_MultiSelected.clear();
            m_MultiSelectOrder.clear();
            for (auto It = ItAnchor; It <= ItTo; ++It)
            {
                m_MultiSelected.insert(*It);
                m_MultiSelectOrder.push_back(*It);
            }
            // Anchor itself is deliberately preserved (not reset to ToName) so repeated Shift-clicks
            // keep extending/contracting the range from the same start point - matches Explorer.
        }

        // Shared by both file and folder rows in RightPanel()'s table. Plain click replaces the whole
        // selection with just this row; Ctrl-click toggles membership; Shift-click range-selects from
        // the anchor. m_SelectedFile tracks the PRIMARY row (the last-clicked one) regardless of
        // modifier - used by 5B's inline-rename ("acts on the primary-selected row") and by any
        // single-target display.
        void HandleRowClick(const std::vector<std::wstring>& SortedNames, const std::wstring& Name) noexcept
        {
            auto& IO = ImGui::GetIO();
            if (IO.KeyShift)      RangeSelect(SortedNames, Name);
            else if (IO.KeyCtrl)  ToggleMultiSelect(Name);
            else                  SelectSingle(Name);

            m_SelectedFile = Name;
        }

        //=============================================================================
        // Real file mutations (Phase 5B) - Rename/Cut/Copy/Paste/Delete, wired to the SAME
        // MoveAssetFile/CopyAssetFile/ComputeTrashPath primitives Phase 4 already proved via CLI. Every
        // one of these goes through m_Browser.m_On*AssetFile when set (E29 wires it to an undo-routed
        // xundo command - see RegisterAssetBrowserCallbacks, E29_LevelSceneEditorKit.h) and falls back
        // to a direct, non-undo-routed library_mgr call otherwise - same opt-in shape
        // virtual_tree_tab's own Rename/Move/Trash call sites already established for the VIRTUAL
        // descriptor tree, just for the real Assets folder instead.
        //
        // MoveAssetFile/CopyAssetFile take paths relative to the LIBRARY ROOT (e.g.
        // "Assets\\Textures\\wood.png"), NOT relative to the Assets folder alone (which is what
        // m_SelectedFolder already is) - ToLibraryRelPath bridges the two.
        std::wstring ToLibraryRelPath(const std::filesystem::path& RelToAssets) const noexcept
        {
            return (std::filesystem::path(L"Assets") / RelToAssets).wstring();
        }

        // Every mutation takes an EXPLICIT LibraryGuid rather than assuming m_SelectedLibrary (the
        // library currently open in the RIGHT panel) - the LEFT tree browses every open library at
        // once, so a rename/cut/paste/drop started from a tree row belonging to a DIFFERENT library
        // than whatever the right panel happens to show must still use ITS OWN library, not the
        // right panel's.
        //
        // These Execute* methods are the REAL, unconditional workers - no dependent-check, no
        // confirmation, just the mutation. Everything else in this file that wants to rename/move/
        // delete a file goes through StageOrExecute() below instead, which checks for descriptor
        // impact FIRST and only reaches these once that's been confirmed (or found to be a non-issue).
        void ExecuteMoveOrRename(const library::guid& LibraryGuid, const std::wstring& OldRelPath, const std::wstring& NewRelPath) noexcept
        {
            m_bEntriesCacheDirty = true;
            if (m_Browser.m_OnMoveAssetFile) m_Browser.m_OnMoveAssetFile(LibraryGuid, OldRelPath, NewRelPath);
            else
            {
                const auto Result = m_AssetMgr.MoveAssetFile(LibraryGuid, OldRelPath, NewRelPath);
                if (!Result.m_bSuccess)
                {
                    std::printf("[AssetTree] Move/rename failed: '%s' -> '%s': %s\n", xstrtool::To(OldRelPath).c_str(), xstrtool::To(NewRelPath).c_str(), Result.m_Error.c_str());
                    std::fflush(stdout);
                }
            }
        }

        // Copy never needs a dependent-impact check - a freshly created copy references nothing that
        // any existing resource could be depending on (see CopyAssetFile's own "zero dependents by
        // construction" comment), so this is called directly everywhere, never through StageOrExecute.
        void DoCopy(const library::guid& LibraryGuid, const std::wstring& SourceRelPath, const std::wstring& NewRelPath) noexcept
        {
            m_bEntriesCacheDirty = true;
            if (m_Browser.m_OnCopyAssetFile) m_Browser.m_OnCopyAssetFile(LibraryGuid, SourceRelPath, NewRelPath);
            else
            {
                const auto Result = m_AssetMgr.CopyAssetFile(LibraryGuid, SourceRelPath, NewRelPath);
                if (!Result.m_bSuccess)
                {
                    std::printf("[AssetTree] Copy failed: '%s' -> '%s': %s\n", xstrtool::To(SourceRelPath).c_str(), xstrtool::To(NewRelPath).c_str(), Result.m_Error.c_str());
                    std::fflush(stdout);
                }
            }
        }

        void ExecuteDeleteToTrash(const library::guid& LibraryGuid, const std::wstring& RelPath) noexcept
        {
            m_bEntriesCacheDirty = true;
            if (m_Browser.m_OnDeleteAssetFileToTrash)
                m_Browser.m_OnDeleteAssetFileToTrash(LibraryGuid, RelPath);
            else
            {
                const std::wstring TrashPath = m_AssetMgr.ComputeTrashPath(LibraryGuid, RelPath);
                const auto Result = m_AssetMgr.MoveAssetFile(LibraryGuid, RelPath, TrashPath);
                if (!Result.m_bSuccess)
                {
                    std::printf("[AssetTree] Delete-to-trash failed: '%s': %s\n", xstrtool::To(RelPath).c_str(), Result.m_Error.c_str());
                    std::fflush(stdout);
                }
            }
        }

        //-----------------------------------------------------------------------
        // Descriptor-impact warning (direct user request: "Probably we should issue a warning to the
        // user for any change where descriptors may need to be updated. (Such renames, deletes, moves,
        // delete folders, etc)"). The actual counting lives on library_mgr itself
        // (library_mgr::CountDependents) - shared with the E29 command layer's own -Force flag (see
        // E29_Commands_AssetFiles.h) so CLI/AI-driven commands get the exact same answer an interactive
        // click would, and a real person's "Proper Commands should take in a flag to suppress those
        // dialogs... so AI can do its job" concern about UI-only logic accidentally blocking automation
        // never applies here - this is a thin forwarding call, not a second implementation.
        std::size_t CountDependentsRecursive(const library::guid& LibraryGuid, const std::wstring& LibraryRelPath) const noexcept
        {
            return m_AssetMgr.CountDependents(LibraryGuid, LibraryRelPath);
        }

        // Old.empty() is never valid; New.empty() means Delete-to-trash (TrashPath is computed fresh
        // at Execute time, matching the existing "trash path is a pure query, minted at the moment of
        // the real action" convention - see ComputeTrashPath's own top comment), non-empty means
        // Move/Rename.
        struct pending_item { library::guid m_Library; std::wstring m_Old; std::wstring m_New; };
        struct pending_confirmation { std::vector<pending_item> m_Items; std::size_t m_DependentCount; };

        // Checks dependents for the WHOLE batch up front, so a multi-item Delete/Move/Paste only ever
        // shows ONE combined confirmation - a synchronous C++ loop within a single ImGui frame cannot
        // "pause" partway through and wait for a modal that only resolves on a LATER frame, so batching
        // the check (rather than gating each item as it's individually mutated) is required, not just
        // tidier. Returns true if everything executed immediately (nothing affected); false if the
        // batch was staged and is now waiting on RenderPendingConfirmationModal/
        // ExecutePendingConfirmation.
        bool StageOrExecute(std::vector<pending_item> Items) noexcept
        {
            std::size_t Total = 0;
            for (auto& It : Items) Total += CountDependentsRecursive(It.m_Library, It.m_Old);

            if (Total == 0)
            {
                for (auto& It : Items)
                {
                    if (It.m_New.empty()) ExecuteDeleteToTrash(It.m_Library, It.m_Old);
                    else                  ExecuteMoveOrRename(It.m_Library, It.m_Old, It.m_New);
                }
                return true;
            }

            m_PendingConfirmation = pending_confirmation{ std::move(Items), Total };
            return false;
        }

        void ExecutePendingConfirmation() noexcept
        {
            if (!m_PendingConfirmation.has_value()) return;
            std::vector<pending_item> Items = std::move(m_PendingConfirmation->m_Items);
            m_PendingConfirmation.reset();
            for (auto& It : Items)
            {
                if (It.m_New.empty()) ExecuteDeleteToTrash(It.m_Library, It.m_Old);
                else                  ExecuteMoveOrRename(It.m_Library, It.m_Old, It.m_New);
            }
        }

        // Called every frame regardless of tab content (from LeftPanel(), which - unlike RightPanel() -
        // never early-returns) so the modal keeps rendering across frames until the user answers it,
        // exactly like any other ImGui modal popup.
        void RenderPendingConfirmationModal() noexcept
        {
            if (m_PendingConfirmation.has_value())
                ImGui::OpenPopup("Descriptor Impact Warning");

            if (ImGui::BeginPopupModal("Descriptor Impact Warning", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                if (m_PendingConfirmation.has_value())
                {
                    const bool bIsDelete = m_PendingConfirmation->m_Items.size() == 1 && m_PendingConfirmation->m_Items.front().m_New.empty();
                    const bool bMultiple = m_PendingConfirmation->m_Items.size() > 1;

                    if (bIsDelete)
                        ImGui::Text("%zu other resource(s) reference this file.\nDeleting it moves it to Trash and updates those resources\nto point at the Trash location - they'll break for real if\nthe Trash is later emptied.", m_PendingConfirmation->m_DependentCount);
                    else
                        ImGui::Text("%zu other resource(s) reference %s affected by this change.\nContinuing will update %s Descriptor.txt to point at the\nnew location.", m_PendingConfirmation->m_DependentCount, bMultiple ? "file(s)" : "the file", bMultiple ? "their" : "its");

                    ImGui::Separator();
                    if (ImGui::Button("Continue", ImVec2(120, 0)))
                    {
                        ExecutePendingConfirmation();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SetItemDefaultFocus();
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    {
                        m_PendingConfirmation.reset();
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndPopup();
            }
        }

        //-----------------------------------------------------------------------
        // Inline rename - F2 or the context menu, acting on ONE item at a time, named by its FULL
        // Assets-relative path (not just a bare filename) so the SAME rename state can unambiguously
        // identify either a RIGHT-panel row (m_SelectedFolder / name) or a LEFT-tree folder at any
        // depth (its own RelPath) - two tree folders can share a bare name at different nesting
        // levels, so a bare name alone isn't a safe identity for the tree case. Enter or losing focus
        // for any other reason commits (IsItemDeactivated() fires on both); Escape reverts and cancels
        // without calling MoveAssetFile at all.
        void StartRename(const library::guid& LibraryGuid, const std::filesystem::path& AssetsRelPath) noexcept
        {
            m_RenameLibrary     = LibraryGuid;
            m_RenameTargetPath  = AssetsRelPath;
            const std::string Narrow = xstrtool::To(AssetsRelPath.filename().wstring());
            strcpy_s(m_RenameBuffer.data(), m_RenameBuffer.size(), Narrow.c_str());
            m_bRenameFocusPending = true;
        }

        void CommitRename() noexcept
        {
            if (m_RenameTargetPath.empty()) return;

            const std::wstring NewName = xstrtool::To(std::string(m_RenameBuffer.data()));
            if (!NewName.empty() && NewName != m_RenameTargetPath.filename().wstring())
            {
                const std::filesystem::path NewRelToAssets = m_RenameTargetPath.parent_path() / NewName;
                const std::wstring          OldRel          = ToLibraryRelPath(m_RenameTargetPath);
                const std::wstring          NewRel          = ToLibraryRelPath(NewRelToAssets);

                // Only touch the RIGHT panel's own selection state if the rename actually happened
                // right away (not deferred to a confirmation) AND the renamed item lives in the folder
                // the right panel currently has open (a tree-folder rename elsewhere shouldn't perturb
                // an unrelated selection).
                if (StageOrExecute({ { m_RenameLibrary, OldRel, NewRel } })
                    && m_RenameLibrary == m_SelectedLibrary && m_RenameTargetPath.parent_path() == m_SelectedFolder)
                {
                    SelectSingle(NewName);
                    m_SelectedFile = NewName;
                }
            }
            m_RenameTargetPath.clear();
        }

        void CancelRename() noexcept { m_RenameTargetPath.clear(); }

        //-----------------------------------------------------------------------
        // Cut/Copy/Paste clipboard - real Explorer semantics: Copy's clipboard survives repeated
        // pastes, Cut's clipboard is spent after exactly one. Entries are stored relative to the
        // Assets root of m_ClipboardLibrary (matching m_SelectedFolder's own convention) so a paste
        // target can be computed relative to whatever folder happens to be open at paste time, not the
        // one that was open at cut/copy time. The multi-select-driven overloads serve the RIGHT panel
        // (always within m_SelectedLibrary); the single-item overloads serve the LEFT tree, which can
        // act on any open library, not just whichever one the right panel currently shows.
        void CutSelection() noexcept
        {
            if (m_MultiSelectOrder.empty()) return;
            m_Clipboard.clear();
            for (auto& Name : m_MultiSelectOrder) m_Clipboard.push_back(m_SelectedFolder / Name);
            m_bClipboardIsCut  = true;
            m_ClipboardLibrary = m_SelectedLibrary;
        }

        void CopySelection() noexcept
        {
            if (m_MultiSelectOrder.empty()) return;
            m_Clipboard.clear();
            for (auto& Name : m_MultiSelectOrder) m_Clipboard.push_back(m_SelectedFolder / Name);
            m_bClipboardIsCut  = false;
            m_ClipboardLibrary = m_SelectedLibrary;
        }

        void CutSingle(const library::guid& LibraryGuid, const std::filesystem::path& AssetsRelPath) noexcept
        {
            m_Clipboard        = { AssetsRelPath };
            m_bClipboardIsCut  = true;
            m_ClipboardLibrary = LibraryGuid;
        }

        void CopySingle(const library::guid& LibraryGuid, const std::filesystem::path& AssetsRelPath) noexcept
        {
            m_Clipboard        = { AssetsRelPath };
            m_bClipboardIsCut  = false;
            m_ClipboardLibrary = LibraryGuid;
        }

        // Cross-library paste is deliberately not supported (different library roots entirely) - the
        // clipboard is just dropped rather than attempting a cross-library file operation nothing else
        // in this codebase does either.
        void PasteClipboardInto(const library::guid& DestLibrary, const std::filesystem::path& DestFolderRelToAssets) noexcept
        {
            if (m_Clipboard.empty() || m_ClipboardLibrary != DestLibrary) return;

            if (m_bClipboardIsCut)
            {
                // Batched through StageOrExecute (not one DoMoveOrRename call per clipboard entry) so a
                // multi-item cut-paste with descriptor impact shows ONE combined confirmation instead
                // of one modal per item mid-loop.
                std::vector<pending_item> Items;
                for (auto& SrcRelToAssets : m_Clipboard)
                    Items.push_back({ DestLibrary, ToLibraryRelPath(SrcRelToAssets), ToLibraryRelPath(DestFolderRelToAssets / SrcRelToAssets.filename()) });
                StageOrExecute(std::move(Items));
            }
            else
            {
                for (auto& SrcRelToAssets : m_Clipboard) // Copy never needs the dependent check.
                    DoCopy(DestLibrary, ToLibraryRelPath(SrcRelToAssets), ToLibraryRelPath(DestFolderRelToAssets / SrcRelToAssets.filename()));
            }

            // Cut's clipboard is spent after one paste attempt (whether it executed immediately or is
            // still pending confirmation); Copy's stays populated for repeated pastes - matches real
            // Explorer exactly.
            if (m_bClipboardIsCut) m_Clipboard.clear();
        }

        // RIGHT panel's own paste target - always the currently open folder in m_SelectedLibrary.
        void PasteClipboard() noexcept
        {
            PasteClipboardInto(m_SelectedLibrary, m_SelectedFolder);
        }

        //-----------------------------------------------------------------------
        // Delete-to-trash. The multi-select overload (RIGHT panel) trashes the WHOLE active selection -
        // each file gets its own separate DeleteAssetFileToTrash call/undo entry (no composite/batch
        // undo concept exists in this codebase - see the plan's own "explicitly out of scope" note),
        // not one call for many paths. The single-item overload serves the LEFT tree.
        void DeleteSelectionToTrash() noexcept
        {
            std::vector<pending_item> Items;
            for (auto& Name : m_MultiSelectOrder)
                Items.push_back({ m_SelectedLibrary, ToLibraryRelPath(m_SelectedFolder / Name), {} });
            StageOrExecute(std::move(Items));

            ClearMultiSelect();
            m_SelectedFile.clear();
        }

        void DeleteSingleToTrash(const library::guid& LibraryGuid, const std::filesystem::path& AssetsRelPath) noexcept
        {
            StageOrExecute({ { LibraryGuid, ToLibraryRelPath(AssetsRelPath), {} } });
        }

        //=============================================================================
        // In-app drag-and-drop (5C) - a real Win32 OLE drag to/from actual Windows Explorer has zero
        // precedent anywhere in this codebase (confirmed by an exhaustive search) and is a genuinely
        // separate, larger effort left for a later pass (see the plan's own "explicitly out of scope"
        // note) - this is IN-APP only, dragging within the Asset Tree's own tree/table.
        //
        // The payload is a small POD (ImGui::SetDragDropPayload memcpy's it into its own storage) that
        // names the FULL Assets-relative path of the ONE row the drag started on (not just a bare
        // name - a LEFT-tree folder drag has no "current folder" to join a bare name against the way a
        // RIGHT-panel row does), plus a flag - the actual list of paths being dragged is resolved at
        // drop time from m_MultiSelectOrder/m_SelectedFolder for a whole-selection drag, both still
        // valid (Selectable() only fires HandleRowClick on a completed click-without-drag, so a drag
        // gesture never itself mutates the selection mid-drag - dragging one of several already-
        // selected rows naturally keeps the whole group, matching real Explorer). This makes "drag the
        // whole active multi-selection if the dragged row is part of one, else just that one row" a
        // general, first-class rule for the RIGHT panel, per this phase's own survey notes on the
        // existing (narrower) entity-drag precedent in E29_PrefabAuthoring.h - the LEFT tree has no
        // multi-select concept of its own, so a tree drag is always exactly the one folder.
        struct file_drag_payload
        {
            library::guid m_Library;
            wchar_t       m_SourcePath[520];    // Assets-relative path of the row the drag started on
            bool          m_bWholeSelection;    // true = drag the whole active RIGHT-panel multi-selection instead
        };

        std::vector<std::filesystem::path> ResolveDragSources(const file_drag_payload& Payload) const noexcept
        {
            std::vector<std::filesystem::path> Result;
            if (Payload.m_bWholeSelection)
            {
                Result.reserve(m_MultiSelectOrder.size());
                for (auto& N : m_MultiSelectOrder) Result.push_back(m_SelectedFolder / N);
            }
            else
            {
                Result.push_back(std::filesystem::path(Payload.m_SourcePath));
            }
            return Result;
        }

        // DestLibrary/DestFolderRelToAssets describe the drop TARGET (a tree/table folder row, or the
        // currently-open folder for a background drop). Cross-library drag-drop is rejected outright -
        // MoveAssetFile/CopyAssetFile both operate within a single library root, same restriction
        // PasteClipboard already applies. Ctrl held while dropping = Copy, matching real Explorer's own
        // drag-modifier convention; default = Move.
        void HandleFileDrop(const file_drag_payload& Payload, const library::guid& DestLibrary, const std::filesystem::path& DestFolderRelToAssets) noexcept
        {
            if (Payload.m_Library != DestLibrary) return;

            const bool bCopy = ImGui::GetIO().KeyCtrl;
            const std::vector<std::filesystem::path> Sources = ResolveDragSources(Payload);

            if (bCopy) // Copy never needs the dependent check.
            {
                for (auto& SrcRelToAssets : Sources)
                    DoCopy(DestLibrary, ToLibraryRelPath(SrcRelToAssets), ToLibraryRelPath(DestFolderRelToAssets / SrcRelToAssets.filename()));
            }
            else
            {
                // Batched through StageOrExecute so a whole-multi-selection drag with descriptor impact
                // shows ONE combined confirmation, not one modal per dragged item.
                std::vector<pending_item> Items;
                for (auto& SrcRelToAssets : Sources)
                    Items.push_back({ DestLibrary, ToLibraryRelPath(SrcRelToAssets), ToLibraryRelPath(DestFolderRelToAssets / SrcRelToAssets.filename()) });
                StageOrExecute(std::move(Items));
            }
        }

        void PathHistoryUpdate(library::guid gLibrary, std::filesystem::path Folder) noexcept
        {
            if (m_PathHistoryList.empty())
            {
                m_PathHistoryList.push_back({ gLibrary, Folder });
                m_PathHistoryIndex = 0;
            }
            else if (m_PathHistoryList[m_PathHistoryIndex].m_gLibrary != gLibrary || m_PathHistoryList[m_PathHistoryIndex].m_Folder != Folder)
            {
                m_PathHistoryList.resize(std::min<std::size_t>(m_PathHistoryIndex + 1ull, m_PathHistoryList.size()));
                m_PathHistoryList.push_back({ gLibrary, Folder });
                if (m_PathHistoryList.size() > 50) m_PathHistoryList.erase(m_PathHistoryList.begin());
                m_PathHistoryIndex = static_cast<std::uint32_t>(m_PathHistoryList.size()) - 1;
            }

            UpdateHistoryLRU();
        }

        std::string BuildPathString(const path_history_entry& E) const noexcept
        {
            std::string Result = "Assets";
            for (auto& Part : E.m_Folder) Result += "\\" + xstrtool::To(Part.wstring());
            return Result;
        }

        //=============================================================================
        // Recursively renders one folder's subtree. RelPath is relative to the library's own Assets
        // root (empty = the root itself). Clicking a folder's LABEL (not just its expand arrow)
        // navigates to it via PathHistoryUpdate. The open/closed state itself is ImGui's own built-in
        // tree arrow (ImGuiTreeNodeFlags_OpenOnArrow); m_ExpandToFolder force-opens every ancestor of a
        // history/breadcrumb jump so the new selection is actually visible without manual expanding.
        void RenderFolder(const library::guid& LibraryGuid, const std::filesystem::path& AssetsRoot, const std::filesystem::path& RelPath) noexcept
        {
            const std::filesystem::path FullPath = AssetsRoot / RelPath;
            std::error_code Ec;

            bool bHasAnyEntry = false;
            std::vector<std::filesystem::path> SubDirs;
            if (std::filesystem::is_directory(FullPath, Ec) && !Ec)
            {
                for (auto& Entry : std::filesystem::directory_iterator(FullPath, Ec))
                {
                    if (Ec) break;
                    bHasAnyEntry = true;
                    std::error_code IsDirEc;
                    if (Entry.is_directory(IsDirEc) && !IsDirEc) SubDirs.push_back(Entry.path().filename());
                }
                std::sort(SubDirs.begin(), SubDirs.end());
            }

            const bool        bIsSelected  = (m_SelectedLibrary == LibraryGuid) && (m_SelectedFolder == RelPath) && m_SelectedFile.empty();
            const bool        bIsRoot      = RelPath.empty();
            const std::string Label        = bIsRoot ? "Assets" : xstrtool::To(RelPath.filename().wstring());
            const bool        bIsRenamingThis = !bIsRoot && (m_RenameLibrary == LibraryGuid) && (m_RenameTargetPath == RelPath);

            if (m_ExpandToFolder.has_value() && LibraryGuid == m_SelectedLibrary && IsAncestorOrSelf(RelPath, *m_ExpandToFolder))
                ImGui::SetNextItemOpen(true);

            const ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth
                                            | (bIsRoot ? ImGuiTreeNodeFlags_DefaultOpen : 0)
                                            | (bIsSelected ? ImGuiTreeNodeFlags_Selected : 0);

            const ImVec2 RowStart = ImGui::GetCursorScreenPos();

            if (!bHasAnyEntry) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            const bool bOpen = TreeNodeWithFolderIcon(Label, Flags, !bHasAnyEntry);
            if (!bHasAnyEntry) ImGui::PopStyleColor();

            // Inline rename overlay (5D) - drawn ON TOP of the label rather than replacing the
            // TreeNodeEx call outright, so the arrow/expand/children machinery above keeps working
            // unmodified while renaming. Same Enter/focus-loss-commits, Escape-cancels behavior as the
            // RIGHT panel's own inline rename.
            if (bIsRenamingThis)
            {
                ImGui::SetCursorScreenPos(ImVec2(RowStart.x + ImGui::GetTreeNodeToLabelSpacing(), RowStart.y));
                ImGui::PushID("TreeRename");
                if (m_bRenameFocusPending) { ImGui::SetKeyboardFocusHere(); m_bRenameFocusPending = false; }
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::InputText("##treerename", m_RenameBuffer.data(), m_RenameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                if (ImGui::IsItemDeactivated())
                {
                    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) CancelRename();
                    else                                      CommitRename();
                }
                ImGui::PopID();
            }

            if (!bIsRenamingThis && ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                PathHistoryUpdate(LibraryGuid, RelPath);

            if (!bIsRenamingThis)
            {
                // Drag source (5D) - a tree folder always drags as exactly itself (the tree has no
                // multi-select concept of its own, unlike the RIGHT panel's file list). The Assets
                // root can't be dragged (nothing to reparent it into).
                if (!bIsRoot && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 12.0f) && ImGui::BeginDragDropSource())
                {
                    file_drag_payload Payload{};
                    Payload.m_Library = LibraryGuid;
                    wcsncpy_s(Payload.m_SourcePath, RelPath.wstring().c_str(), _TRUNCATE);
                    Payload.m_bWholeSelection = false;

                    ImGui::SetDragDropPayload("E10_ASSET_FILE_DRAG", &Payload, sizeof(Payload));
                    ImGui::TextUnformatted(Label.c_str());
                    ImGui::EndDragDropSource();
                }

                // Drop target (5C) - drop any in-app file/folder drag onto a LEFT-tree row to move/copy
                // it there, regardless of which folder is currently open on the right.
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* Pl = ImGui::AcceptDragDropPayload("E10_ASSET_FILE_DRAG"))
                    {
                        IM_ASSERT(Pl->DataSize == sizeof(file_drag_payload));
                        HandleFileDrop(*static_cast<const file_drag_payload*>(Pl->Data), LibraryGuid, RelPath);
                    }
                    ImGui::EndDragDropTarget();
                }

                // Right-click context menu (5D) - Rename/Cut/Copy/Delete act on exactly this one
                // folder; Paste lands INSIDE it. The Assets root can't be renamed, cut, or deleted
                // (it's the library's own fixed folder), but CAN still receive a paste.
                if (ImGui::BeginPopupContextItem())
                {
                    if (ImGui::MenuItem("Rename", "F2", false, !bIsRoot))
                        StartRename(LibraryGuid, RelPath);
                    if (ImGui::MenuItem("Cut", "Ctrl+X", false, !bIsRoot))
                        CutSingle(LibraryGuid, RelPath);
                    if (ImGui::MenuItem("Copy", "Ctrl+C", false, !bIsRoot))
                        CopySingle(LibraryGuid, RelPath);
                    if (ImGui::MenuItem("Paste", "Ctrl+V", false, !m_Clipboard.empty()))
                        PasteClipboardInto(LibraryGuid, RelPath);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete", "Del", false, !bIsRoot))
                        DeleteSingleToTrash(LibraryGuid, RelPath);
                    ImGui::EndPopup();
                }
            }

            if (bOpen)
            {
                for (auto& Dir : SubDirs) RenderFolder(LibraryGuid, AssetsRoot, RelPath / Dir);
                ImGui::TreePop();
            }
        }

        //=============================================================================

        void LeftPanel() noexcept override
        {
            // LeftPanel(), unlike RightPanel(), never early-returns - the one reliable place to render
            // this every frame regardless of what's currently selected.
            RenderPendingConfirmationModal();

            for (auto& L : m_AssetMgr.m_mLibraryDB)
            {
                const auto AssetsRoot = std::filesystem::path(L.second->m_Library.m_Path) / L"Assets";
                RenderFolder(L.second->m_Library.m_GUID, AssetsRoot, {});
            }
            // Consume the force-open request exactly once - after this, ancestors keep whatever
            // open/closed state the user leaves them in until the next navigation.
            m_ExpandToFolder.reset();
        }

        //=============================================================================
        // Styled breadcrumb bar - deliberately as close as reasonably possible to virtual_tree_tab's
        // own RenderPath() (E10_asset_browser_virtual_tree_tab.h): same background-bar-drawn-behind-
        // transparent-buttons technique, same bold-last-segment treatment, same per-segment ">" button
        // opening a popup of that level's OTHER children (siblings) for a sideways jump, same
        // fill-remaining-width trailer. Adapted from descriptor-tree path_node children to real
        // subfolders on disk.
        void RenderPath() noexcept
        {
            ImGuiStyle& style     = ImGui::GetStyle();
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2      start_pos = ImGui::GetCursorScreenPos();

            const float line_height = ImGui::GetTextLineHeightWithSpacing();
            const ImU32 button_bg_color_u32 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.145f, 0.145f, 0.145f, 0.80f));

            draw_list->PushClipRect(start_pos, ImVec2(start_pos.x + ImGui::GetContentRegionAvail().x, start_pos.y + line_height), true);

            draw_list->AddRectFilled(start_pos, ImVec2(start_pos.x + ImGui::GetContentRegionAvail().x, start_pos.y + line_height), button_bg_color_u32);

            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, style.FramePadding.y));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));

            struct segment { std::string m_Name; std::filesystem::path m_Path; };
            std::vector<segment> Segments;
            Segments.push_back({ "Assets", {} });
            {
                std::filesystem::path Accum;
                for (auto& Part : m_SelectedFolder)
                {
                    Accum /= Part;
                    Segments.push_back({ xstrtool::To(Part.wstring()), Accum });
                }
            }

            static int iSelectedPath = -1;
            for (std::size_t i = 0; i < Segments.size(); ++i)
            {
                auto&      Seg    = Segments[i];
                const bool bLast  = (i + 1 == Segments.size());

                if (bLast) ImGui::PushFont(xgpu::tools::imgui::getFont(1));
                if (ImGui::Button(Seg.m_Name.c_str())) PathHistoryUpdate(m_SelectedLibrary, Seg.m_Path);
                if (bLast) ImGui::PopFont();
                ImGui::SameLine(0, 0);

                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Button(">"))
                {
                    ImGui::OpenPopup("Path Context Menu Files");
                    iSelectedPath = static_cast<int>(i);
                }
                ImGui::PopID();
                ImGui::SameLine(0, 0);
            }

            if (ImGui::BeginPopup("Path Context Menu Files"))
            {
                if (iSelectedPath >= 0 && static_cast<std::size_t>(iSelectedPath) < Segments.size())
                {
                    m_AssetMgr.m_mLibraryDB.FindAsReadOnly(m_SelectedLibrary, [&](const std::unique_ptr<library_db>& Library)
                    {
                        const auto AssetsRoot = std::filesystem::path(Library->m_Library.m_Path) / L"Assets";
                        const auto FullPath   = AssetsRoot / Segments[iSelectedPath].m_Path;
                        std::error_code Ec;

                        std::vector<std::filesystem::path> SubDirs;
                        for (auto& Entry : std::filesystem::directory_iterator(FullPath, Ec))
                        {
                            if (Ec) break;
                            std::error_code IsDirEc;
                            if (Entry.is_directory(IsDirEc) && !IsDirEc) SubDirs.push_back(Entry.path().filename());
                        }
                        std::sort(SubDirs.begin(), SubDirs.end());

                        const bool                  bHasNext = (static_cast<std::size_t>(iSelectedPath) + 1) < Segments.size();
                        const std::filesystem::path NextName = bHasNext ? Segments[iSelectedPath + 1].m_Path.filename() : std::filesystem::path{};

                        for (auto& Dir : SubDirs)
                        {
                            const std::string Name = xstrtool::To(Dir.wstring());
                            if (ImGui::MenuItem(Name.c_str(), nullptr, bHasNext && Dir == NextName))
                                PathHistoryUpdate(m_SelectedLibrary, Segments[iSelectedPath].m_Path / Dir);
                        }
                    });
                }
                ImGui::EndPopup();
            }
            else
            {
                iSelectedPath = -1;
            }

            draw_list->PopClipRect();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);

            ImGui::PushItemWidth(-FLT_MIN);
            ImGui::Text(" ");
            ImGui::PopItemWidth();
        }

        //=============================================================================
        // Back/forward + history-list button + the styled breadcrumb (RenderPath) - functionally
        // mirrors virtual_tree_tab's own RenderNavigationPath().
        void RenderNavigationPath() noexcept
        {
            if (ChevronButton("back", true, m_PathHistoryIndex != 0))
            {
                m_PathHistoryIndex--;
                UpdateHistoryLRU();
            }

            ImGui::SameLine(0, 2.0f);

            if (ChevronButton("fwd", false, (m_PathHistoryIndex + 1) < m_PathHistoryList.size()))
            {
                m_PathHistoryIndex++;
                UpdateHistoryLRU();
            }

            ImGui::SameLine(0, 6.0f);

            if (ImGui::SmallButton("History")) ImGui::OpenPopup("Path History Files");
            ImGui::SameLine(0, 4.0f);

            RenderPath();

            if (ImGui::BeginPopup("Path History Files"))
            {
                if (m_PathHistoryListRU.empty()) ImGui::TextDisabled("(no history yet)");
                for (auto& E : m_PathHistoryListRU)
                {
                    if (ImGui::Selectable(BuildPathString(E).c_str()))
                    {
                        PathHistoryUpdate(E.m_gLibrary, E.m_Folder);
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndPopup();
            }
        }

        //=============================================================================

        void RightPanel() noexcept override
        {
            if (m_SelectedLibrary.empty())
            {
                ImGui::TextDisabled("Select a folder on the left.");
                return;
            }

            RenderNavigationPath();

            const bool bFoundLibrary = m_AssetMgr.m_mLibraryDB.FindAsReadOnly(m_SelectedLibrary, [&](const std::unique_ptr<library_db>& Library)
            {
                const auto     AssetsRoot = std::filesystem::path(Library->m_Library.m_Path) / L"Assets";
                const auto     FullPath   = AssetsRoot / m_SelectedFolder;
                std::error_code Ec;

                if (!std::filesystem::is_directory(FullPath, Ec) || Ec)
                {
                    ImGui::TextDisabled("Folder no longer exists.");
                    return;
                }

                // REAL BUG FOUND LIVE (2026-09-11): this used to rebuild Entries from disk EVERY SINGLE
                // FRAME (directory_iterator + per-file file_size() stat + sort), and that instability is
                // exactly what was silently swallowing every ordinary click in real use - confirmed by
                // reading imgui.cpp's own NewFrame() "ActiveIdIsAlive" bookkeeping: if a widget that was
                // ACTIVE on the previous frame isn't re-submitted with an IDENTICAL ID this frame (e.g.
                // because a background thread - the file-system watcher, the compiler queue, Windows
                // Search/AV - touched something in this same folder between the press frame and the
                // release frame, however briefly reordering/perturbing the fresh enumeration), ImGui
                // automatically clears g.ActiveId as a safety net against a permanently-stuck active
                // widget - and a cleared ActiveId means ButtonBehavior() no longer reports the eventual
                // release as "pressed", exactly matching every DIAG log captured live (RawActiveId=0 at
                // release, despite matching the item's id at press). Synthetic zero-latency test clicks
                // never spanned enough real frames to ever hit this; a real human click reliably does.
                // Fix: only rebuild the cache when the folder actually changes or a mutation just
                // happened (m_bEntriesCacheDirty, set in UpdateHistoryLRU and by every Do*/Delete* call) -
                // not every frame. Same content, stable IDs frame-to-frame, and also a straightforward
                // performance win (no more full re-stat of every file 60 times a second).
                bool bFreshlyRebuilt = false;
                if (m_bEntriesCacheDirty || m_CachedEntriesFolder != FullPath)
                {
                    m_CachedEntries.clear();
                    for (auto& It : std::filesystem::directory_iterator(FullPath, Ec))
                    {
                        if (Ec) break;
                        std::error_code TypeEc, SizeEc, TimeEc;
                        const bool bDir = It.is_directory(TypeEc) && !TypeEc;
                        m_CachedEntries.push_back({ It.path().filename().wstring(), bDir, bDir ? 0 : It.file_size(SizeEc), It.last_write_time(TimeEc) });
                    }
                    // Sorted below, once the table's own sort specs (persisted per-table by ImGui
                    // itself, same as column widths) are available - BeginTable hasn't run yet here.
                    m_CachedEntriesFolder = FullPath;
                    m_bEntriesCacheDirty  = false;
                    bFreshlyRebuilt       = true;
                }
                std::vector<file_entry>& Entries = m_CachedEntries;

                // Keyboard shortcuts (5B) - only while this window has focus and nothing else (like the
                // rename InputText itself) is the active widget, so typing "x"/"c"/"v" while renaming
                // doesn't also fire Cut/Copy/Paste. (Moved below the table's own sort-spec application so
                // Ctrl+A selects in the CURRENT on-screen sorted order, not whatever order the cache was
                // last built in.)

                // ScrollY + TableSetupScrollFreeze(0, 1) makes the table its own bounded, internally-
                // scrolling region with the header row pinned - direct user correction: "Name"/"Size"
                // were scrolling out of view along with the rows instead of staying put. Sortable +
                // per-column sort flags give the "Name"/"Size"/"Date Modified" headers real Explorer-
                // style sort arrows - direct user request (the "Date Modified" column itself is also a
                // direct user request, "like explorer").
                if (ImGui::BeginTable("Files", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable, ImGui::GetContentRegionAvail()))
                {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("Name",          ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortAscending);
                    ImGui::TableSetupColumn("Size",          ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortAscending, 90.0f);
                    ImGui::TableSetupColumn("Date Modified", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 160.0f);
                    ImGui::TableHeadersRow();

                    // Folders always cluster first regardless of sort column (matches Explorer); within
                    // each cluster, sort by whichever column/direction the user clicked. Re-sort on an
                    // actual header click (SpecsDirty) OR right after a fresh cache rebuild (navigation/
                    // mutation), since ImGui only marks SpecsDirty on a genuine spec CHANGE, not on every
                    // frame the table exists - a rebuilt-but-unsorted cache needs applying the CURRENT
                    // (already-established) spec at least once, not just future clicks.
                    if (ImGuiTableSortSpecs* SortSpecs = ImGui::TableGetSortSpecs())
                    {
                        if (SortSpecs->SpecsDirty || bFreshlyRebuilt)
                        {
                            const int  SortColumn  = SortSpecs->SpecsCount > 0 ? SortSpecs->Specs[0].ColumnIndex : 0;
                            const bool bDescending = SortSpecs->SpecsCount > 0 && SortSpecs->Specs[0].SortDirection == ImGuiSortDirection_Descending;

                            std::sort(Entries.begin(), Entries.end(), [SortColumn, bDescending](const file_entry& A, const file_entry& B) noexcept
                            {
                                if (A.m_bDirectory != B.m_bDirectory) return A.m_bDirectory > B.m_bDirectory; // folders first, always

                                int Cmp;
                                switch (SortColumn)
                                {
                                    case 1:  Cmp = (A.m_Size < B.m_Size) ? -1 : (A.m_Size > B.m_Size ? 1 : 0); break;
                                    case 2:  Cmp = (A.m_LastWriteTime < B.m_LastWriteTime) ? -1 : (A.m_LastWriteTime > B.m_LastWriteTime ? 1 : 0); break;
                                    default: Cmp = A.m_Name.compare(B.m_Name); break;
                                }
                                return bDescending ? (Cmp > 0) : (Cmp < 0);
                            });
                            SortSpecs->SpecsDirty = false;
                        }
                    }

                    // The exact on-screen order Shift range-select walks - built AFTER sorting so it
                    // matches what the user actually sees, not whatever order the cache was built in.
                    std::vector<std::wstring> SortedNames;
                    SortedNames.reserve(Entries.size());
                    for (auto& E : Entries) SortedNames.push_back(E.m_Name);

                    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::IsAnyItemActive())
                    {
                        auto& IO = ImGui::GetIO();
                        if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A))
                        {
                            m_MultiSelected.clear();
                            m_MultiSelectOrder.clear();
                            for (auto& N : SortedNames) { m_MultiSelected.insert(N); m_MultiSelectOrder.push_back(N); }
                        }
                        else if (ImGui::IsKeyPressed(ImGuiKey_F2) && m_MultiSelected.size() == 1 && !m_SelectedFile.empty())
                            StartRename(m_SelectedLibrary, m_SelectedFolder / m_SelectedFile);
                        else if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X))
                            CutSelection();
                        else if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C))
                            CopySelection();
                        else if (IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V))
                            PasteClipboard();
                        else if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !m_MultiSelected.empty())
                            DeleteSelectionToTrash();
                    }

                    for (auto& E : Entries)
                    {
                        ImGui::PushID(E.m_Name.c_str());
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);

                        const std::string Name           = xstrtool::To(E.m_Name);
                        const bool        bMultiSelected = m_MultiSelected.contains(E.m_Name);
                        const bool        bIsRenamingThis = (m_RenameLibrary == m_SelectedLibrary) && (m_RenameTargetPath == m_SelectedFolder / E.m_Name);

                        // Folders get the same drawn icon the tree uses, regardless of rename state.
                        if (E.m_bDirectory)
                        {
                            const bool bEmpty = IsFolderEmpty(FullPath / E.m_Name);
                            DrawFolderIcon(bEmpty);
                            if (!bIsRenamingThis) ImGui::PushStyleColor(ImGuiCol_Text, bEmpty ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) : ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
                        }

                        if (bIsRenamingThis)
                        {
                            // Inline rename (5B) - swaps the Selectable for an InputText over this one
                            // row; Enter or any other focus-loss commits (both fire IsItemDeactivated),
                            // Escape reverts and cancels. See CommitRename/CancelRename's own comments.
                            if (m_bRenameFocusPending) { ImGui::SetKeyboardFocusHere(); m_bRenameFocusPending = false; }
                            ImGui::SetNextItemWidth(-FLT_MIN);
                            ImGui::InputText("##rename", m_RenameBuffer.data(), m_RenameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                            if (ImGui::IsItemDeactivated())
                            {
                                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) CancelRename();
                                else                                      CommitRename();
                            }
                        }
                        else
                        {
                            // Selectable (not plain Text) for BOTH rows so hover highlights, not just
                            // the selected one. Single click just selects (matches files); DOUBLE click
                            // navigates INTO the folder - Explorer convention, direct user request.
                            // Highlight reflects the whole multi-selection, not just one "primary" row -
                            // Ctrl/Shift-clicked rows light up exactly like a real Explorer multi-select.
                            if (E.m_bDirectory)
                            {
                                if (ImGui::Selectable(Name.c_str(), bMultiSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
                                {
                                    // A double-click always opens the folder (matches Explorer -
                                    // Ctrl/Shift held during a double-click still just opens it, doesn't
                                    // also toggle multi-select on top); anything else goes through the
                                    // shared click handler so plain/Ctrl/Shift behave consistently with
                                    // file rows.
                                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                                    {
                                        m_SelectedFile = E.m_Name;
                                        PathHistoryUpdate(Library->m_Library.m_GUID, m_SelectedFolder / E.m_Name);
                                    }
                                    else
                                        HandleRowClick(SortedNames, E.m_Name);
                                }
                            }
                            else
                            {
                                if (ImGui::Selectable(Name.c_str(), bMultiSelected, ImGuiSelectableFlags_SpanAllColumns))
                                    HandleRowClick(SortedNames, E.m_Name);

                                // Dependent-usage hover hint (direct user request: "on hover for the
                                // files you could open a hint popup showing which resources are using
                                // that file... (bound the list just in case)"). DelayNormal matches
                                // Explorer's own "don't pop instantly" tooltip feel; capped to 10 names
                                // via GetDependentNames' own MaxCount so a heavily-referenced file (e.g.
                                // a shared texture) can't spam an enormous tooltip.
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                                {
                                    std::size_t Total = 0;
                                    const auto  Names = m_AssetMgr.GetDependentNames(m_SelectedLibrary, ToLibraryRelPath(m_SelectedFolder / E.m_Name), 10, Total);
                                    if (Total > 0)
                                    {
                                        ImGui::BeginTooltip();
                                        ImGui::Text("Used by %zu resource%s:", Total, Total == 1 ? "" : "s");
                                        for (auto& N : Names) ImGui::BulletText("%s", N.c_str());
                                        if (Total > Names.size()) ImGui::Text("...and %zu more", Total - Names.size());
                                        ImGui::EndTooltip();
                                    }
                                }
                            }

                            // Drag source (5C) - both files and folders can be dragged; carries just
                            // this row's own name plus a "drag the whole selection instead" flag - see
                            // file_drag_payload's own top comment for why the actual path list isn't
                            // embedded here.
                            //
                            // Gated behind a deliberately larger manual drag-distance check (12px, vs
                            // ImGui's default ~6px io.MouseDragThreshold) rather than calling
                            // BeginDragDropSource() unconditionally - imgui_widgets.cpp's own
                            // ButtonBehavior() explicitly refuses to report a PressedOnClickRelease item
                            // (which is what plain Selectable() is) as "pressed" on release if
                            // g.DragDropActive was ever true during that same press-hold, so an
                            // over-eager drag source can swallow ordinary clicks. BeginDragDropSource()
                            // sets g.DragDropActive true the moment IsMouseDragging() crosses ImGui's
                            // ~6px io.MouseDragThreshold - trivial to cross with any real mouse's natural
                            // jitter during an ordinary click, essentially never with a scripted, perfectly
                            // stationary synthetic click (which is why this passed every one of my own
                            // earlier synthetic tests). Fix: only ever CALL BeginDragDropSource() once the
                            // mouse has already moved a deliberately larger, unambiguous distance (12px, a
                            // LOCAL override via IsMouseDragging's own threshold param - NOT the global
                            // io.MouseDragThreshold, so nothing else in this app that relies on the default
                            // is affected) - an ordinary click's jitter never reaches 12px, so
                            // BeginDragDropSource() is simply never invoked during a plain click and
                            // g.DragDropActive never gets set, leaving Selectable()'s normal click-release
                            // path completely untouched.
                            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 12.0f) && ImGui::BeginDragDropSource())
                            {
                                file_drag_payload Payload{};
                                Payload.m_Library = m_SelectedLibrary;
                                wcsncpy_s(Payload.m_SourcePath, (m_SelectedFolder / E.m_Name).wstring().c_str(), _TRUNCATE);
                                Payload.m_bWholeSelection = bMultiSelected && m_MultiSelected.size() > 1;

                                ImGui::SetDragDropPayload("E10_ASSET_FILE_DRAG", &Payload, sizeof(Payload));
                                ImGui::TextUnformatted(Payload.m_bWholeSelection ? std::format("{} items", m_MultiSelected.size()).c_str() : Name.c_str());
                                ImGui::EndDragDropSource();
                            }

                            // Drop target (5C) - only folder rows accept a drop (dropping onto a FILE
                            // row makes no sense - nothing here treats a file as a container).
                            if (E.m_bDirectory && ImGui::BeginDragDropTarget())
                            {
                                if (const ImGuiPayload* Pl = ImGui::AcceptDragDropPayload("E10_ASSET_FILE_DRAG"))
                                {
                                    IM_ASSERT(Pl->DataSize == sizeof(file_drag_payload));
                                    HandleFileDrop(*static_cast<const file_drag_payload*>(Pl->Data), Library->m_Library.m_GUID, m_SelectedFolder / E.m_Name);
                                }
                                ImGui::EndDragDropTarget();
                            }

                            // Right-click context menu (5B) - if the right-clicked row isn't already
                            // part of an active multi-selection, right-click selects just it first,
                            // matching Explorer's own behavior, so "Delete" etc. never silently acts on
                            // a stale, unrelated selection.
                            if (ImGui::BeginPopupContextItem())
                            {
                                if (!bMultiSelected) { SelectSingle(E.m_Name); m_SelectedFile = E.m_Name; }

                                if (ImGui::MenuItem("Rename", "F2", false, m_MultiSelected.size() == 1))
                                    StartRename(m_SelectedLibrary, m_SelectedFolder / m_SelectedFile);
                                if (ImGui::MenuItem("Cut", "Ctrl+X"))
                                    CutSelection();
                                if (ImGui::MenuItem("Copy", "Ctrl+C"))
                                    CopySelection();
                                if (ImGui::MenuItem("Paste", "Ctrl+V", false, !m_Clipboard.empty()))
                                    PasteClipboard();
                                ImGui::Separator();
                                if (ImGui::MenuItem("Delete", "Del"))
                                    DeleteSelectionToTrash();
                                ImGui::EndPopup();
                            }
                        }

                        if (E.m_bDirectory && !bIsRenamingThis) ImGui::PopStyleColor();

                        ImGui::TableSetColumnIndex(1);
                        if (!E.m_bDirectory) ImGui::Text("%llu", static_cast<unsigned long long>(E.m_Size));

                        // Date Modified - direct user request, "like explorer". Reuses this codebase's
                        // own existing ConvertToStdTime helper (E10_AssetMgr.h) for consistency with
                        // the Virtual Tree's own descriptor-timestamp display, just a shorter format
                        // (no seconds/timezone) matching Explorer's own compact column.
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%s", std::format("{:%m/%d/%Y %I:%M %p}", e10::ConvertToStdTime(E.m_LastWriteTime)).c_str());
                        ImGui::PopID();
                    }

                    // Right-click on empty table background (no row under the cursor) - Paste only,
                    // the one action that makes sense with nothing selected. NoOpenOverExistingPopup
                    // keeps this from stealing a right-click a row's own context menu above already
                    // claimed this same frame.
                    if (ImGui::BeginPopupContextWindow("Files Background Context", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverExistingPopup))
                    {
                        if (ImGui::MenuItem("Paste", "Ctrl+V", false, !m_Clipboard.empty()))
                            PasteClipboard();
                        ImGui::EndPopup();
                    }

                    // Drop onto the empty table background (5C) - the third drop target the plan
                    // calls for, alongside LEFT-tree rows and RIGHT-panel folder rows above: "move/copy
                    // into the currently open folder". BeginDragDropTargetCustom lets a drop target
                    // exist without being attached to a specific submitted item.
                    if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->ContentRegionRect, ImGui::GetID("FilesBackgroundDropTarget")))
                    {
                        if (const ImGuiPayload* Pl = ImGui::AcceptDragDropPayload("E10_ASSET_FILE_DRAG"))
                        {
                            IM_ASSERT(Pl->DataSize == sizeof(file_drag_payload));
                            HandleFileDrop(*static_cast<const file_drag_payload*>(Pl->Data), Library->m_Library.m_GUID, m_SelectedFolder);
                        }
                        ImGui::EndDragDropTarget();
                    }

                    ImGui::EndTable();
                }
            });

            if (!bFoundLibrary) ImGui::TextDisabled("Library not found.");
        }

        e10::library_mgr&                     m_AssetMgr;
        library::guid                         m_SelectedLibrary = {};
        std::filesystem::path                 m_SelectedFolder  = {};
        std::wstring                          m_SelectedFile    = {};

        std::vector<path_history_entry>       m_PathHistoryList   = {};
        std::vector<path_history_entry>       m_PathHistoryListRU = {};
        std::uint32_t                         m_PathHistoryIndex  = 0;
        std::optional<std::filesystem::path>  m_ExpandToFolder    = {};

        // Multi-select, scoped to the current folder (m_SelectedFolder) - see ClearMultiSelect's own
        // comment for why it's cleared there rather than here.
        std::unordered_set<std::wstring>      m_MultiSelected      = {};
        std::vector<std::wstring>             m_MultiSelectOrder   = {};
        std::wstring                          m_MultiSelectAnchor  = {};

        // Inline rename (5B) - m_RenameTargetPath empty means "nothing is being renamed right now".
        // m_RenameTargetPath is the FULL Assets-relative path (not a bare name - see StartRename's own
        // comment for why), m_RenameLibrary the library it belongs to (may differ from m_SelectedLibrary
        // when a LEFT-tree folder in another open library is being renamed).
        library::guid                          m_RenameLibrary       = {};
        std::filesystem::path                 m_RenameTargetPath    = {};
        std::array<char, 260>                 m_RenameBuffer        = {};
        bool                                  m_bRenameFocusPending = false;

        // Cut/Copy/Paste clipboard (5B) - entries are Assets-root-relative paths (see CutSelection's
        // own comment for why).
        std::vector<std::filesystem::path>    m_Clipboard        = {};
        bool                                  m_bClipboardIsCut  = false;
        library::guid                         m_ClipboardLibrary = {};

        // RightPanel()'s file-listing cache - see that function's own comment for the real click-
        // eating bug this fixes. Dirty = true forces a rebuild on the next RightPanel() call; set on
        // folder navigation (UpdateHistoryLRU) and after every real mutation (DoMoveOrRename/DoCopy/
        // DoDeleteToTrash) so a rename/paste/drop's result shows up immediately rather than waiting for
        // an unrelated navigation.
        std::vector<file_entry>               m_CachedEntries       = {};
        std::filesystem::path                 m_CachedEntriesFolder = {};
        bool                                  m_bEntriesCacheDirty  = true;

        // Descriptor-impact confirmation (5E) - see StageOrExecute/RenderPendingConfirmationModal's
        // own comments. Empty = nothing pending.
        std::optional<pending_confirmation>   m_PendingConfirmation = {};
    };

    namespace
    {
        inline browser_registration<files_tab, "Asset Tree", 3.0f, true, true > g_FilesTab{};
    }
}

#endif
