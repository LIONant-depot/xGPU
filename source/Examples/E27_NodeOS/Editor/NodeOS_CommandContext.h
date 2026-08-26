#pragma once
// node_os_command_context + BackupSelection/RestoreSelection, extracted from the monolithic
// E27_NodeOS_Editor.cpp (header #11). Used by BOTH "NodeOS/Edit/..." commands
// (Editor/NodeOS_Commands_Edit.h) AND "NodeOS/Query/..." commands (Editor/NodeOS_Commands_Query.h) -
// confirmed create_node_cmd (Edit) and screenshot_query_cmd/set_view_query_cmd (Query) both use the
// context, and select_cmd/clear_selection_cmd (both Edit) use BackupSelection/RestoreSelection - so
// this stays its own shared header per the split plan, never folded into either.
#include "NodeOS_Common.h"
#include "NodeOS_Types.h"
#include "NodeOS_CanvasSupport.h"
#include "NodeOS_UI_CommandConsole.h"

namespace nodeos
{
    namespace commands
    {
        //--------------------------------------------------------------------------------------------
        // The one "database" every command mutates, retrieved via command_base::get<node_os_command_
        // context>() - plain references into E27_Example()'s own locals, not owned here.
        //--------------------------------------------------------------------------------------------
        struct node_os_command_context
        {
            std::vector<node_instance>&        m_Nodes;
            std::vector<link_instance>&        m_Links;
            canvas_selection&                  m_Selection;
            std::vector<plugin_source_entry>&  m_Sources;
            std::vector<available_node_type>&  m_AvailableTypes;
            bool&                              m_bDirty;
            std::vector<spine>&                 m_Spines;
            std::vector<column>&                m_Columns;
            std::vector<console_log_entry>&     m_ConsoleLog;     // the SAME log DrawCommandConsolePanel renders and
                                                                    // PumpCommandConsolePipe appends to - see get_log_query_cmd,
                                                                    // below, for why a query needs to read this back.
            bool&                               m_bScreenshotRequested; // arms the capture - see screenshot_query_cmd and
            std::string&                        m_ScreenshotPath;       // E27_Example's PageFlip hook, which actually writes the file.
            canvas_view&                        m_View;                 // pan/zoom - see SetView/GetView, below.
        };

        // Shared by select_cmd and clear_selection_cmd - both snapshot/restore the exact same set.
        inline void BackupSelection(node_os_command_context& Ctx, xundo::undo_file& File) noexcept
        {
            auto& S = Ctx.m_Selection;
            File.Write(static_cast<std::uint32_t>(S.m_SelectedNodes.size()));
            for (auto Id : S.m_SelectedNodes) File.Write(Id);
            File.Write(S.m_SelectedLink);
            File.Write(S.m_SelectedGapSpineId);
            File.Write(S.m_SelectedGapIndex);
        }
        inline void RestoreSelection(node_os_command_context& Ctx, xundo::undo_file& File) noexcept
        {
            auto& S = Ctx.m_Selection;
            std::uint32_t Count = 0; File.Read(Count);
            S.m_SelectedNodes.clear();
            for (std::uint32_t i = 0; i < Count; ++i) { std::uint64_t Id = 0; File.Read(Id); S.m_SelectedNodes.insert(Id); }
            File.Read(S.m_SelectedLink);
            File.Read(S.m_SelectedGapSpineId);
            File.Read(S.m_SelectedGapIndex);
        }
    }
}
