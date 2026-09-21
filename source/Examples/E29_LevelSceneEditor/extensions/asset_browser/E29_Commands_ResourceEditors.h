#ifndef E29_COMMANDS_RESOURCE_EDITORS_H
#define E29_COMMANDS_RESOURCE_EDITORS_H
#pragma once

// E29 hosts the resource editors of the plugins (Texture, Static Geom, ...): each one is a window with its own document and undo system,
// kept in xeditor::open_resource_editors. Including a plugin's editor header registers its factory. Open editors are also listed in
// xeditor::host, so `list` and Name\Command reach them like the Level; the two commands below are the guid-addressed way in.

#include "Plugins/xtexture.plugin/source/Editor/xtexture_editor.h"
#include "plugins/xgeom_static.plugin/source/Editor/xgeom_static_editor.h"
#include "plugins/xlevel.plugin/source/Editor/xlevel_command_context.h"
#include "dependencies/xeditor/include/xeditor/host.h"
#include "source/Examples/E29_LevelSceneEditor/extensions/command_console/E29_CommandConsolePipe.h"

namespace e29::commands
{
    inline xeditor::open_resource_editors* FindResourceEditors() noexcept
    {
        auto* pHost = xeditor::host::current();
        return pHost ? pHost->find<xeditor::open_resource_editors>() : nullptr;
    }

    struct open_resource_editor_cmd : xlevel::commands::level_query_command
    {
        open_resource_editor_cmd(xundo::system& System, void* pDataBase) noexcept : xlevel::commands::level_query_command(System, "OpenResourceEditor", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Opens the editor of a resource (Texture, GeomStatic, ...) in its own dock-isolated window, or focuses it when it is already open. Usage: OpenResourceEditor -Asset assetguid [-Library hexguid]"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary = m_Parser.addOption("Library", "Library instance guid, 16 hex digits (default: the library that has the resource)", false, 1);
            m_hAsset   = m_Parser.addOption("Asset",   "Resource guid, 32 hex digits",                                                       true,  1);
        }
        std::string Query() noexcept override
        {
            auto AssetArg = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            if (std::holds_alternative<xerr>(AssetArg)) return "OpenResourceEditor: bad arguments";

            auto* pEditors = FindResourceEditors();
            if (!pEditors) return "OpenResourceEditor: no editor host";

            const auto AssetGuid = e10::commands::ParseAssetGuid(std::get<std::string>(AssetArg));
            if (!xeditor::open_resource_editors::HasEditorFor(AssetGuid.m_Type)) return "OpenResourceEditor: this resource type has no editor";

            auto LibraryGuid = xeditor::open_resource_editors::FindLibraryOf(AssetGuid);
            if (m_Parser.hasOption(m_hLibrary))
            {
                auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
                if (std::holds_alternative<xerr>(LibraryArg)) return "OpenResourceEditor: bad arguments";
                LibraryGuid = e10::commands::ParseLibraryGuid(std::get<std::string>(LibraryArg));
            }
            if (LibraryGuid.empty()) return "OpenResourceEditor: no open library has that resource";

            auto* pEditor = pEditors->Open(AssetGuid, LibraryGuid);
            return pEditor && pEditor->isLoaded() ? "" : "OpenResourceEditor: failed to load the descriptor";
        }
        xcmdline::parser::handle m_hLibrary, m_hAsset;
    };

    // The guid-addressed way to reach an open editor's own commands (the friendlier way is Name\Command from `list`).
    struct resource_editor_command_cmd : xlevel::commands::level_query_command
    {
        resource_editor_command_cmd(xundo::system& System, void* pDataBase) noexcept : xlevel::commands::level_query_command(System, "ResourceEditorCommand", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Runs a command of an open resource editor, addressed by resource guid. Usage: ResourceEditorCommand -Asset assetguid -Cmd base64(\"SetProperty ...\")"; }
        void RegisterArguments() noexcept override
        {
            m_hAsset = m_Parser.addOption("Asset", "Resource guid, 32 hex digits", true, 1);
            m_hCmd   = m_Parser.addOption("Cmd",   "Inner command string, Base64",  true, 1);
        }
        std::string Query() noexcept override
        {
            auto AssetArg = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            auto CmdArg   = m_Parser.getOptionArgAs<std::string>(m_hCmd, 0);
            if (std::holds_alternative<xerr>(AssetArg) || std::holds_alternative<xerr>(CmdArg))
                return "ResourceEditorCommand: bad arguments";

            auto* pEditors = FindResourceEditors();
            auto* pEditor  = pEditors ? pEditors->Find(e10::commands::ParseAssetGuid(std::get<std::string>(AssetArg))) : nullptr;
            if (!pEditor) return "ResourceEditorCommand: no open editor for that resource";
            return xeditor::host::run_on(pEditor->getUndo(), xeditor::Base64Decode(std::get<std::string>(CmdArg)));
        }
        xcmdline::parser::handle m_hAsset, m_hCmd;
    };
}

#endif // E29_COMMANDS_RESOURCE_EDITORS_H
