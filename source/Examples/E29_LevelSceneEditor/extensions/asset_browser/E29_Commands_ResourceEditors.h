#ifndef E29_COMMANDS_RESOURCE_EDITORS_H
#define E29_COMMANDS_RESOURCE_EDITORS_H
#pragma once

// E29 hosts the resource editors of the plugins (Texture, Static Geom, ...): each one is a window with its own document and undo system,
// kept in xeditor::open_resource_editors. Including a plugin's editor header registers its factory. Open editors are also listed in
// xeditor::host, so `list` and Name\Command reach them like the Level; the two commands below are the guid-addressed way in.

#include "Plugins/xtexture.plugin/source/Editor/xtexture_editor.h"
#include "plugins/xgeom_static.plugin/source/Editor/xgeom_static_editor.h"
#include "plugins/xmaterial_instance.plugin/source/Editor/xmaterial_instance_editor.h"
#include "plugins/xmaterial.plugin/source/Editor/xmaterial_editor.h"
#include "plugins/xfont.plugin/source/Editor/xfont_editor.h"
#include "plugins/xanim_package.plugin/source/Editor/xanim_package_editor.h"
#include "plugins/xgeom_skin.plugin/source/Editor/xgeom_skin_editor.h"
#include "plugins/xskeleton.plugin/source/Editor/xskeleton_editor.h"
#include "plugins/xlevel.plugin/source/Editor/xlevel_command_context.h"
#include "dependencies/xeditor/include/xeditor/host.h"
#include "source/Examples/E29_LevelSceneEditor/extensions/command_console/E29_CommandConsolePipe.h"

namespace e29
{
    // A picture of the whole window, for whoever drives the editor by commands and wants to see it. The capture happens when the next frame is
    // presented: the app calls BeforeFlip and AfterFlip around the page flip.
    struct window_capture
    {
        bool                        m_bPending   = false;       // a capture was asked for
        bool                        m_bRequested = false;       // the window was told to capture this frame
        std::wstring                m_Path;
        std::vector<std::uint32_t>  m_Pixels;
        int                         m_Width = 0, m_Height = 0;

        void BeforeFlip(xgpu::window& Window) noexcept
        {
            if (m_bPending && !m_bRequested) m_bRequested = Window.Screenshot(m_Pixels, m_Width, m_Height);
        }

        // The file appears complete or not at all: it is written under a temporary name and renamed
        void AfterFlip() noexcept
        {
            if (!m_bRequested) return;
            m_bPending = m_bRequested = false;
            if (m_Width <= 0 || m_Height <= 0 || m_Pixels.size() < static_cast<std::size_t>(m_Width) * m_Height) return;

            // An xbitmap of raw pixels starts with the offset of its first mip: one slot ahead of the pixels. The alpha of a back buffer is
            // whatever the pipeline left there, so it is made opaque.
            std::vector<std::uint32_t> Padded(1 + static_cast<std::size_t>(m_Width) * m_Height);
            Padded[0] = sizeof(xbitmap::mip);
            for (std::size_t i = 0; i < static_cast<std::size_t>(m_Width) * m_Height; ++i) Padded[1 + i] = m_Pixels[i] | 0xFF000000u;

            xbitmap Bitmap;
            Bitmap.setup(m_Width, m_Height, xbitmap::format::B8G8R8A8, static_cast<std::uint64_t>(m_Width) * m_Height * sizeof(std::uint32_t), std::as_writable_bytes(std::span(Padded)), false, 1, 1);

            std::filesystem::path Final(m_Path);
            std::filesystem::path Temp = Final;
            Temp.replace_extension(L".part" + Final.extension().wstring());
            if (xbmp::tools::writers::SaveSTDImage(Temp.wstring(), Bitmap)) return;
            std::error_code Ec;
            std::filesystem::rename(Temp, Final, Ec);
            if (Ec) { std::filesystem::remove(Final, Ec); std::filesystem::rename(Temp, Final, Ec); }
        }
    };

    inline window_capture g_WindowCapture;
}

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

    struct capture_window_cmd : xlevel::commands::level_query_command
    {
        capture_window_cmd(xundo::system& System, void* pDataBase) noexcept : xlevel::commands::level_query_command(System, "CaptureWindow", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Saves a picture of the whole editor window (png, bmp, tga or jpg by the extension). The file appears when the next frame is presented. Usage: CaptureWindow -File path"; }
        void RegisterArguments() noexcept override { m_hFile = m_Parser.addOption("File", "Where to write the picture", true, 1); }
        std::string Query() noexcept override
        {
            auto FileArg = m_Parser.getOptionArgAs<std::string>(m_hFile, 0);
            if (std::holds_alternative<xerr>(FileArg)) return "CaptureWindow: bad arguments";
            const std::filesystem::path Path = xstrtool::To(std::get<std::string>(FileArg));
            if (Path.extension().empty()) return "CaptureWindow: the file needs an extension (png, bmp, tga or jpg)";
            if (g_WindowCapture.m_bPending) return "CaptureWindow: a capture is already waiting for the next frame";
            std::error_code Ec;
            if (Path.has_parent_path()) std::filesystem::create_directories(Path.parent_path(), Ec);
            std::filesystem::remove(Path, Ec);
            g_WindowCapture.m_Path     = Path.wstring();
            g_WindowCapture.m_bPending = true;
            return "CaptureWindow: queued, the file appears when the next frame is presented";
        }
        xcmdline::parser::handle m_hFile;
    };

    struct close_resource_editor_cmd : xlevel::commands::level_query_command
    {
        close_resource_editor_cmd(xundo::system& System, void* pDataBase) noexcept : xlevel::commands::level_query_command(System, "CloseResourceEditor", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Closes the editor of a resource, like its window's close button (unsaved changes are dropped). Usage: CloseResourceEditor -Asset assetguid"; }
        void RegisterArguments() noexcept override { m_hAsset = m_Parser.addOption("Asset", "Resource guid, 32 hex digits", true, 1); }
        std::string Query() noexcept override
        {
            auto AssetArg = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            if (std::holds_alternative<xerr>(AssetArg)) return "CloseResourceEditor: bad arguments";
            auto* pEditors = FindResourceEditors();
            auto* pEditor  = pEditors ? pEditors->Find(e10::commands::ParseAssetGuid(std::get<std::string>(AssetArg))) : nullptr;
            if (!pEditor) return "CloseResourceEditor: no open editor for that resource";
            pEditor->m_bOpen = false;       // the host drops it at the start of the next frame
            return "";
        }
        xcmdline::parser::handle m_hAsset;
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
