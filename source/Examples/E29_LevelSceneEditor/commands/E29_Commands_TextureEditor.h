#ifndef E29_COMMANDS_TEXTURE_EDITOR_H
#define E29_COMMANDS_TEXTURE_EDITOR_H
#pragma once

// E29 as a HOST opening the standalone Texture editor (source/lives in the texture plugin itself,
// Plugins/xtexture.plugin/source/Editor/xtexture_editor.h) - per direct user instruction, this is
// deliberately E29-side integration code, not a change to E10 (E10 keeps its own, separate,
// untouched editing UI). Registered into E29's own xundo::system, so it's reachable via E29's
// existing Command Console pipe/E29CLI.exe for free - no separate console needed for this.
#include "Plugins/xtexture.plugin/source/Editor/xtexture_editor.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"
#include <vector>
#include <memory>
#include <algorithm>

namespace e29
{
    // Zero, one, or many simultaneously open Texture editor instances - each its own
    // dock-isolated window (xeditor::IsolateEditorDockspace, keyed by the open resource's own
    // guid), never a single shared slot the way E10's own model works.
    inline std::vector<std::unique_ptr<xtexture_editor::session>> g_OpenTextureEditors;

    // Bound once from E29 main after Device create � sessions need it for E10-taught preview upload.
    inline xgpu::device* g_pTextureEditorDevice = nullptr;

    inline void RenderOpenTextureEditors() noexcept
    {
        // Erase CLOSED sessions at the START of the next frame, not right after Render().
        // ImGui close sets m_bOpen=false while Begin() still returns true for that frame, so
        // AddCustomRenderCallback is queued; imgui::Render() runs those callbacks later in the
        // SAME frame. Destroying here-after-Render was a use-after-free (Draw3D -> getFormat).
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

            // Focus, don't duplicate, if this exact resource is already open (§8.2's one-writable-
            // session-per-identity policy) - a real reason this list-of-sessions model exists,
            // not just plumbing.
            for (auto& S : e29::g_OpenTextureEditors)
                if (S && S->m_Document.getGuid() == AssetGuid) { S->Focus(); return "OpenTextureEditor: already open, focused"; }

            e29::g_OpenTextureEditors.push_back(std::make_unique<xtexture_editor::session>(AssetGuid, LibraryGuid, e29::g_pTextureEditorDevice));
            return e29::g_OpenTextureEditors.back()->m_Document.m_pDescriptor ? "" : "OpenTextureEditor: failed to load descriptor";
        }
        xcmdline::parser::handle m_hLibrary, m_hAsset;
    };

    // Forwards a session-scoped command to a specific open Texture editor's OWN private
    // xundo::system, addressed by the resource's guid. This is the real reason a forwarder is
    // needed at all: each session's undo/command scope is deliberately LOCAL to that one editor
    // instance (never shared globally, per the framework's own §7.3/§6.3), so E29's top-level
    // console/CLI cannot reach a session's commands directly - it has to go through this one
    // multiplexer, exactly matching the "one shared console, many session-addressable commands"
    // shape the framework's own problem statement calls for (§7.5/§8.5), just realized here as
    // E29's own console being that one shared console rather than a separate new one.
    struct texture_editor_command_cmd : xundo::query_command_base
    {
        texture_editor_command_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "TextureEditorCommand", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Forwards a command to a specific open Texture editor session. Usage: TextureEditorCommand -Asset assetguid -Cmd base64(\"SetSRGB -Value 0\")"; }
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

                // Same dual-registry dispatch xeditor::PumpConsole already does - Edit and Query
                // commands live in two separate maps on xundo::system, and calling the wrong one
                // just returns a generic "not found" rather than dispatching.
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
