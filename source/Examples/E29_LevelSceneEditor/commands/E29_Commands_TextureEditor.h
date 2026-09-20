#ifndef E29_COMMANDS_TEXTURE_EDITOR_H
#define E29_COMMANDS_TEXTURE_EDITOR_H
#pragma once

// E29 hosts the standalone Texture editor (plugin session + UI). Plugin sessions are
// bridged into xeditor::host.m_Sessions as borrowed document/undo so list / Name\Cmd
// work like Level. TextureEditorCommand remains as an AI/tooling alias (guid-addressed).

#include "Plugins/xtexture.plugin/source/Editor/xtexture_editor.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"
#include "dependencies/xeditor/include/xeditor/host.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandConsolePipe.h"
#include <vector>
#include <memory>
#include <algorithm>

namespace e29
{
    inline std::vector<std::unique_ptr<xtexture_editor::session>> g_OpenTextureEditors;
    inline xgpu::device* g_pTextureEditorDevice = nullptr;

    // Bridge open plugin sessions into Host.m_Sessions (borrowed doc/undo). Level uses
    // owned sessions; only is_borrowed() entries are managed here.
    inline void SyncOpenTextureEditorsToHost(xeditor::host& Host) noexcept
    {

        // Only bridge m_bOpen sessions. Closed editors stay in g_OpenTextureEditors until
        // RenderOpenTextureEditors erase_if (next frame) — if we kept borrowing them, erase
        // would leave host.m_Sessions dangling into freed session/undo (close crash / heap junk).
        std::erase_if(Host.m_Sessions, [&](std::unique_ptr<xeditor::session>& U) noexcept
        {
            if (!U || !U->is_borrowed()) return false;
            for (auto& S : g_OpenTextureEditors)
                if (S && S->m_bOpen && U->m_pBorrowedUndo == &S->m_Undo) return false;
            return true; // orphaned or closed texture bridge
        });

        for (auto& S : g_OpenTextureEditors)
        {
            if (!S || !S->m_bOpen) continue;
            xeditor::session* Hit = nullptr;
            for (auto& U : Host.m_Sessions)
            {
                if (U && U->m_pBorrowedUndo == &S->m_Undo) { Hit = U.get(); break; }
            }
            if (!Hit)
            {
                auto NS = std::make_unique<xeditor::session>();
                NS->m_pBorrowedDocument = &S->m_Document;
                NS->m_pBorrowedUndo     = &S->m_Undo;
                Host.m_Sessions.push_back(std::move(NS));
            }
            else
            {
                Hit->m_pBorrowedDocument = &S->m_Document;
                Hit->m_pBorrowedUndo     = &S->m_Undo;
            }
        }
    }

    inline void RenderOpenTextureEditors() noexcept
    {
        // Drop host bridges for closed sessions before destroying them.
        if (g_pEditorHost)
            SyncOpenTextureEditorsToHost(*g_pEditorHost);
        std::erase_if(g_OpenTextureEditors, [](auto& S) noexcept { return !S || !S->m_bOpen; });
        for (auto& S : g_OpenTextureEditors) if (S) S->Render();
    }

}

namespace e29::commands
{
    struct open_texture_editor_cmd : xundo::query_command_base
    {
        open_texture_editor_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "OpenTextureEditor", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Opens the standalone Texture editor for a resource, in its own dock-isolated window. Usage: OpenTextureEditor -Library hexguid -Asset assetguid"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary = m_Parser.addOption("Library", "Library instance guid, 16 hex digits", true, 1);
            m_hAsset   = m_Parser.addOption("Asset",   "Texture asset guid, 32 hex digits",     true, 1);
        }
        std::string Query() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg   = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            if (std::holds_alternative<xerr>(LibraryArg) || std::holds_alternative<xerr>(AssetArg))
                return "OpenTextureEditor: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto AssetGuid   = ParseAssetGuid(std::get<std::string>(AssetArg));

            for (auto& S : e29::g_OpenTextureEditors)
                if (S && S->m_Document.getGuid() == AssetGuid) { S->Focus(); return "OpenTextureEditor: already open, focused"; }

            e29::g_OpenTextureEditors.push_back(std::make_unique<xtexture_editor::session>(AssetGuid, LibraryGuid, e29::g_pTextureEditorDevice));
            return e29::g_OpenTextureEditors.back()->m_Document.m_pDescriptor ? "" : "OpenTextureEditor: failed to load descriptor";
        }
        xcmdline::parser::handle m_hLibrary, m_hAsset;
    };

    // AI/tooling alias: guid-addressed forwarder. Prefer TextureDisplayName\SetSRGB via host
    // once the session is listed; this command stays so automation never loses access.
    struct texture_editor_command_cmd : xundo::query_command_base
    {
        texture_editor_command_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "TextureEditorCommand", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "AI/tooling alias: forwards a command to an open Texture session by asset guid. Prefer Name\\Cmd from list when possible. Usage: TextureEditorCommand -Asset assetguid -Cmd base64(\"SetSRGB -Value 0\")"; }
        void RegisterArguments() noexcept override
        {
            m_hAsset = m_Parser.addOption("Asset", "Texture asset guid, 32 hex digits", true, 1);
            m_hCmd   = m_Parser.addOption("Cmd",   "Inner command string, Base64",       true, 1);
        }
        std::string Query() noexcept override
        {
            auto AssetArg = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            auto CmdArg   = m_Parser.getOptionArgAs<std::string>(m_hCmd, 0);
            if (std::holds_alternative<xerr>(AssetArg) || std::holds_alternative<xerr>(CmdArg))
                return "TextureEditorCommand: bad arguments";

            const auto AssetGuid = ParseAssetGuid(std::get<std::string>(AssetArg));
            const auto InnerCmd  = Base64Decode(std::get<std::string>(CmdArg));

            for (auto& S : e29::g_OpenTextureEditors)
            {
                if (!S || S->m_Document.getGuid() != AssetGuid) continue;
                const std::string Name = InnerCmd.substr(0, InnerCmd.find(' '));
                const auto QueryNames  = S->m_Undo.GetQueryCommandNames();
                const bool bIsQuery    = std::find(QueryNames.begin(), QueryNames.end(), Name) != QueryNames.end();
                return bIsQuery ? S->m_Undo.Query(InnerCmd) : S->m_Undo.Execute(InnerCmd);
            }
            return "TextureEditorCommand: no open session for that asset";
        }
        xcmdline::parser::handle m_hAsset, m_hCmd;
    };
}

#endif // E29_COMMANDS_TEXTURE_EDITOR_H
