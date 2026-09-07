#ifndef E29_PANEL_SYSTEM_REGISTRY_H
#define E29_PANEL_SYSTEM_REGISTRY_H
#pragma once

// Extracted from E29_LevelSceneEditorKit.h (mechanical move, phase 1 of the kit split - see that
// file's own top comment). Meant to be included via the umbrella (E29_LevelSceneEditorKit.h) only -
// not designed to be included standalone.

namespace e29
{
    //---------------------------------------------------------------------------
    // System Registry panel - lists every registered Update system (xecs::system::mgr::
    // GetUpdateSystemRows), letting the user drag-reorder (via each row's own name Selectable, grip
    // glyph included - see its own comment below) and enable/disable each one directly, making
    // execution order/enabled DATA instead of whatever order GameMgr.RegisterSystems<...>() happened
    // to list them in at compile time. While State.isPlaying(), edits still go through the exact same
    // mgr calls - they're already only ever transient in that state, since GameMgr.Stop() calls
    // RestoreFromSnapshot() on the way out - this panel just surfaces that distinction with a note so
    // it isn't a silent surprise later.
    //---------------------------------------------------------------------------
    void RenderSystemRegistryPanel(xecs::game_mgr::instance& GameMgr, editor_state& State) noexcept
    {
        // Stacked below the Entity Properties panel (18,18 / 480x500) rather than at the Level
        // Editor panel's own (915,18) spot, so the two don't land on top of each other on a
        // completely fresh imgui.ini - purely a first-launch default, freely rearrangeable/dockable
        // afterward like every other panel here.
        ImGui::SetNextWindowPos(ImVec2(18, 530), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(480, 220), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("System Registry"))
        {
            if (State.isPlaying())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.3f, 1.0f));
                ImGui::TextWrapped("Play Mode - changes here are temporary and revert on Stop.");
                ImGui::PopStyleColor();
                ImGui::Separator();
            }

            bool bChanged = false;
            auto Rows      = GameMgr.m_SystemMgr.GetUpdateSystemRows();
            if (Rows.empty())
            {
                ImGui::TextDisabled("No Update systems registered.");
            }
            else if (ImGui::BeginTable("SystemRegistry", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersV, ImVec2(0.0f, ImGui::GetContentRegionAvail().y)))
            {
                // Just wide enough for the checkbox itself (frame height + a little breathing room
                // either side) - not an arbitrary wide column.
                ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() + 12.0f);
                ImGui::TableSetupColumn("Name",    ImGuiTableColumnFlags_WidthStretch);

                for (std::size_t i = 0; i < Rows.size(); ++i)
                {
                    auto& Row = Rows[i];
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    bool bEnabled = Row.m_bEnabled;
                    if (ImGui::Checkbox("##Enabled", &bEnabled))
                    {
                        GameMgr.m_SystemMgr.SetUpdateSystemEnabled(Row.m_Guid, bEnabled);
                        bChanged = true;
                    }

                    // ONE real, visible item serves as BOTH the drag source and the drop target -
                    // an earlier attempt used a SEPARATE invisible full-row Selectable as the drop
                    // target alongside a plain Dummy as the drag source, overlapping it; even with
                    // SetNextItemAllowOverlap that didn't reliably work (confirmed live - dragging
                    // stopped registering at all once the second item was layered on top). Simplest
                    // fix, per this project's own established lesson
                    // ([[xgpu_imgui_overlapping_invisible_buttons]]): don't have two competing
                    // interactive items sharing the same screen space in the first place. The grip
                    // glyph used to be a SEPARATE item drawn via ImDrawList before this Selectable -
                    // no matter how the cursor position was juggled, that meant a drag could only ever
                    // start with the press landing exactly over the icon's own tiny rect, not the
                    // Selectable's (confirmed live). Folding it into the SAME label string (direct
                    // user fix suggestion) makes the icon part of this ONE Selectable's own hit-rect,
                    // so a drag can start from anywhere across the whole row, icon included. Plain
                    // ASCII ":: " rather than a specific icon-font codepoint, to avoid any
                    // tofu/unmapped-glyph risk.
                    ImGui::TableSetColumnIndex(1);
                    if (!bEnabled) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                    const std::string Label      = std::string(":: ") + (Row.m_pName ? Row.m_pName : "(unnamed system)");
                    // An explicit, precomputed width - NOT the "-1 = auto-fill" sentinel, which does
                    // not compute correctly for a Selectable inside a table cell (confirmed live: the
                    // label rendered clipped down to a single glyph). Matches OnEntityReferenceRender's
                    // own already-proven pattern elsewhere in this file (GetContentRegionAvail().x
                    // computed explicitly, not -1).
                    const float AvailWidth = ImGui::GetContentRegionAvail().x;
                    ImGui::Selectable(Label.c_str(), false, ImGuiSelectableFlags_None, ImVec2(AvailWidth, 0.0f));
                    if (!bEnabled) ImGui::PopStyleColor();

                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                    {
                        ImGui::SetDragDropPayload("E29_SYSTEM_REORDER", &Row.m_Guid, sizeof(Row.m_Guid));
                        ImGui::TextUnformatted(Row.m_pName ? Row.m_pName : "(unnamed system)");
                        ImGui::EndDragDropSource();
                    }

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("E29_SYSTEM_REORDER"))
                        {
                            IM_ASSERT(payload->DataSize == sizeof(xecs::system::type::guid));
                            const auto SourceGuid = *reinterpret_cast<const xecs::system::type::guid*>(payload->Data);

                            int SourceIndex = -1;
                            for (std::size_t k = 0; k < Rows.size(); ++k)
                                if (Rows[k].m_Guid == SourceGuid) { SourceIndex = static_cast<int>(k); break; }

                            if (SourceIndex >= 0 && SourceIndex != static_cast<int>(i))
                            {
                                const int Delta = (static_cast<int>(i) > SourceIndex) ? 1 : -1;
                                for (int Step = SourceIndex; Step != static_cast<int>(i); Step += Delta)
                                    GameMgr.m_SystemMgr.MoveUpdateSystem(SourceGuid, Delta);
                                bChanged = true;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            // Persisted immediately, but only on an actual edit this frame (not every frame the
            // window happens to be open) - mirrors Unity's own Script Execution Order behavior.
            // While playing, Move/SetEnabled above only ever mutate the live, in-memory order;
            // GameMgr.Stop()'s RestoreFromSnapshot() discards it, so writing to disk here would just
            // save a value about to be thrown away.
            if (bChanged && !State.isPlaying())
            {
                if (auto Err = GameMgr.m_SystemMgr.Save(); Err)
                    Debugger(std::format("Failed to save System Registry order: {}", Err.getMessage()));
            }
        }
        ImGui::End();
    }

} // namespace e29

#endif // E29_PANEL_SYSTEM_REGISTRY_H
