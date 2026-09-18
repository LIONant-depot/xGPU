#ifndef E29_COMMANDS_SCRIPTING_H
#define E29_COMMANDS_SCRIPTING_H
#pragma once

// Scripting resource's own "source_db" file management - Phase 1 of the Scripting/Script-module
// resource-type plan (see the standing plan file's own "Scripting/Script-module resource types +
// Project Settings panel" section). Mirrors xecs::scene's own entity_db in spirit (a resource's
// ".desc" folder holds a nested subfolder of individually-tracked files, invisible to
// info_node/library_db - a pure filesystem convention, not a second resource-tracking layer) but
// deliberately simpler: a Scripting resource is never "loaded live" into a running ECS world the
// way a Scene is, so there's no async-safe live-editing manager needed here, just plain add/
// remove/list file operations wrapped as xundo commands (matching CreateAsset/DeleteAsset's own
// command_base shape in E29_Commands_AssetBrowser.h, since adding/removing a source file is the
// same kind of reversible content operation, not a real external round-trip).
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_AssetBrowser.h"
#include <fstream>

namespace e29::commands
{
    // Resolves an asset's own ".desc" folder as a real, absolute filesystem path (info.txt's own
    // path, minus the filename) - same getNodeInfo-based derivation RevertResourceWholeFolder
    // (E29_Commands_SourceControl.h) already uses, just returning the ABSOLUTE path directly
    // instead of a library-root-relative key, since this is real filesystem I/O, not a git
    // pathspec. Empty return means the asset guid didn't resolve in the given library.
    inline std::wstring ResolveAssetDescFolder(e10::library::guid LibraryGuid, xresource::full_guid AssetGuid) noexcept
    {
        std::wstring FolderPath;
        // NOT noexcept - getNodeInfo's own function_traits deduction doesn't handle a noexcept
        // lambda's operator() type (xgpu_xcontainer_noexcept_lambda_trait_trap).
        e10::g_LibMgr.getNodeInfo(LibraryGuid, AssetGuid, [&](const e10::library_db::info_node& Node)
        {
            const auto SlashPos = Node.m_Path.find_last_of(L'\\');
            FolderPath = (SlashPos == std::wstring::npos) ? Node.m_Path : Node.m_Path.substr(0, SlashPos);
        });
        return FolderPath;
    }

    inline std::wstring ScriptSourceDbFolder(e10::library::guid LibraryGuid, xresource::full_guid AssetGuid) noexcept
    {
        const auto Desc = ResolveAssetDescFolder(LibraryGuid, AssetGuid);
        return Desc.empty() ? Desc : (Desc + L"\\source_db");
    }

    //================================================================================================
    // AddScriptSourceFile - creates an empty .cpp/.h file under a Scripting resource's own
    // "source_db" folder (created on first use). Undo removes exactly that file - safe because
    // Redo only ever creates an EMPTY file; a real edit to its content is a separate save/write
    // path this command never touches.
    //================================================================================================
    struct add_script_source_file_cmd : xundo::command_base
    {
        add_script_source_file_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "AddScriptSourceFile", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Creates a new, empty source file under a Scripting resource's own source_db folder (undoable). Usage: AddScriptSourceFile -Library hexguid -Asset assetguid -FileName base64"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary  = m_Parser.addOption("Library",  "Library instance guid, 16 hex digits", true, 1);
            m_hAsset    = m_Parser.addOption("Asset",    "Scripting asset guid, 32 hex digits",  true, 1);
            m_hFileName = m_Parser.addOption("FileName", "File name (e.g. \"Foo.cpp\"), Base64",  true, 1);
        }

        std::string Redo() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg    = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            auto FileNameArg = m_Parser.getOptionArgAs<std::string>(m_hFileName, 0);
            if (std::holds_alternative<xerr>(LibraryArg) || std::holds_alternative<xerr>(AssetArg) || std::holds_alternative<xerr>(FileNameArg))
                return "AddScriptSourceFile: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto AssetGuid   = ParseAssetGuid(std::get<std::string>(AssetArg));
            const auto FileName    = DecodeAssetPath(std::get<std::string>(FileNameArg));

            const auto SourceDb = ScriptSourceDbFolder(LibraryGuid, AssetGuid);
            if (SourceDb.empty()) return "AddScriptSourceFile: asset not found";

            std::error_code Ec;
            std::filesystem::create_directories(SourceDb, Ec);
            const auto FilePath = SourceDb + L"\\" + FileName;
            if (std::filesystem::exists(FilePath, Ec)) return "AddScriptSourceFile: a file with that name already exists";

            std::ofstream Out(FilePath, std::ios::binary);
            if (!Out.is_open()) return "AddScriptSourceFile: failed to create the file";
            Out.close();
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto LibraryArg  = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg    = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            auto FileNameArg = m_Parser.getOptionArgAs<std::string>(m_hFileName, 0);

            const std::uint64_t Library = std::holds_alternative<xerr>(LibraryArg) ? 0 : std::strtoull(std::get<std::string>(LibraryArg).c_str(), nullptr, 16);
            File.Write(Library);
            WriteString(File, std::holds_alternative<xerr>(AssetArg) ? std::string(32, '0') : std::get<std::string>(AssetArg));
            WriteString(File, std::holds_alternative<xerr>(FileNameArg) ? std::string() : std::get<std::string>(FileNameArg));
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Library = 0; File.Read(Library);
            const std::string Asset       = ReadString(File);
            const std::string FileNameB64 = ReadString(File);

            const auto LibraryGuid = ParseLibraryGuid(std::format("{:016X}", Library));
            const auto AssetGuid   = ParseAssetGuid(Asset);
            const auto FileName    = DecodeAssetPath(FileNameB64);

            const auto SourceDb = ScriptSourceDbFolder(LibraryGuid, AssetGuid);
            if (SourceDb.empty()) return;
            std::error_code Ec;
            std::filesystem::remove(SourceDb + L"\\" + FileName, Ec);
        }

        xcmdline::parser::handle m_hLibrary, m_hAsset, m_hFileName;
    };

    //================================================================================================
    // RemoveScriptSourceFile - deletes a file from a Scripting resource's own source_db folder.
    // Undo restores it with its EXACT prior content (snapshotted in BackupCurrenState) - unlike
    // AddScriptSourceFile's own Undo, Redo here can be destroying real, non-empty edits, so the
    // backup must carry the file's own bytes, not just its name.
    //================================================================================================
    struct remove_script_source_file_cmd : xundo::command_base
    {
        remove_script_source_file_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "RemoveScriptSourceFile", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Deletes a file from a Scripting resource's own source_db folder (undoable - restores its exact content). Usage: RemoveScriptSourceFile -Library hexguid -Asset assetguid -FileName base64"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary  = m_Parser.addOption("Library",  "Library instance guid, 16 hex digits", true, 1);
            m_hAsset    = m_Parser.addOption("Asset",    "Scripting asset guid, 32 hex digits",  true, 1);
            m_hFileName = m_Parser.addOption("FileName", "File name (e.g. \"Foo.cpp\"), Base64",  true, 1);
        }

        std::string Redo() noexcept override
        {
            auto LibraryArg  = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg    = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            auto FileNameArg = m_Parser.getOptionArgAs<std::string>(m_hFileName, 0);
            if (std::holds_alternative<xerr>(LibraryArg) || std::holds_alternative<xerr>(AssetArg) || std::holds_alternative<xerr>(FileNameArg))
                return "RemoveScriptSourceFile: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto AssetGuid   = ParseAssetGuid(std::get<std::string>(AssetArg));
            const auto FileName    = DecodeAssetPath(std::get<std::string>(FileNameArg));

            const auto SourceDb = ScriptSourceDbFolder(LibraryGuid, AssetGuid);
            if (SourceDb.empty()) return "RemoveScriptSourceFile: asset not found";

            std::error_code Ec;
            std::filesystem::remove(SourceDb + L"\\" + FileName, Ec);
            if (Ec) return "RemoveScriptSourceFile: failed to delete the file";
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto LibraryArg  = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg    = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            auto FileNameArg = m_Parser.getOptionArgAs<std::string>(m_hFileName, 0);

            const std::uint64_t Library = std::holds_alternative<xerr>(LibraryArg) ? 0 : std::strtoull(std::get<std::string>(LibraryArg).c_str(), nullptr, 16);
            File.Write(Library);
            WriteString(File, std::holds_alternative<xerr>(AssetArg) ? std::string(32, '0') : std::get<std::string>(AssetArg));
            WriteString(File, std::holds_alternative<xerr>(FileNameArg) ? std::string() : std::get<std::string>(FileNameArg));

            // The file's own exact content, so Undo can restore it byte-for-byte, not just re-create
            // an empty placeholder the way AddScriptSourceFile's own Undo is allowed to.
            std::string Content;
            if (!std::holds_alternative<xerr>(LibraryArg) && !std::holds_alternative<xerr>(AssetArg) && !std::holds_alternative<xerr>(FileNameArg))
            {
                const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
                const auto AssetGuid   = ParseAssetGuid(std::get<std::string>(AssetArg));
                const auto FileName    = DecodeAssetPath(std::get<std::string>(FileNameArg));
                const auto SourceDb    = ScriptSourceDbFolder(LibraryGuid, AssetGuid);
                if (!SourceDb.empty())
                {
                    std::ifstream In(SourceDb + L"\\" + FileName, std::ios::binary);
                    if (In.is_open())
                        Content.assign((std::istreambuf_iterator<char>(In)), std::istreambuf_iterator<char>());
                }
            }
            WriteString(File, Content);
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Library = 0; File.Read(Library);
            const std::string Asset       = ReadString(File);
            const std::string FileNameB64 = ReadString(File);
            const std::string Content     = ReadString(File);

            const auto LibraryGuid = ParseLibraryGuid(std::format("{:016X}", Library));
            const auto AssetGuid   = ParseAssetGuid(Asset);
            const auto FileName    = DecodeAssetPath(FileNameB64);

            const auto SourceDb = ScriptSourceDbFolder(LibraryGuid, AssetGuid);
            if (SourceDb.empty()) return;
            std::error_code Ec;
            std::filesystem::create_directories(SourceDb, Ec);
            std::ofstream Out(SourceDb + L"\\" + FileName, std::ios::binary);
            if (Out.is_open()) Out.write(Content.data(), static_cast<std::streamsize>(Content.size()));
        }

        xcmdline::parser::handle m_hLibrary, m_hAsset, m_hFileName;
    };

    //================================================================================================
    // ListScriptSourceFiles - every file name currently under a Scripting resource's own source_db
    // folder, one per line. Instant, read-only - matches ListAssets' own "discovery command" shape.
    //================================================================================================
    struct list_script_source_files_query_cmd : xundo::query_command_base
    {
        list_script_source_files_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "ListScriptSourceFiles", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Lists every file under a Scripting resource's own source_db folder. Usage: ListScriptSourceFiles -Library hexguid -Asset assetguid"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary = m_Parser.addOption("Library", "Library instance guid, 16 hex digits", true, 1);
            m_hAsset   = m_Parser.addOption("Asset",   "Scripting asset guid, 32 hex digits",  true, 1);
        }

        std::string Query() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg   = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            if (std::holds_alternative<xerr>(LibraryArg) || std::holds_alternative<xerr>(AssetArg))
                return "ListScriptSourceFiles: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto AssetGuid   = ParseAssetGuid(std::get<std::string>(AssetArg));

            const auto SourceDb = ScriptSourceDbFolder(LibraryGuid, AssetGuid);
            if (SourceDb.empty()) return "ListScriptSourceFiles: asset not found";

            std::error_code Ec;
            if (!std::filesystem::exists(SourceDb, Ec)) return "(no source_db folder yet - nothing added)";

            std::string Out;
            for (auto& Entry : std::filesystem::directory_iterator(SourceDb, Ec))
                if (Entry.is_regular_file())
                    Out += xstrtool::To(Entry.path().filename().wstring()) + "\n";
            return Out.empty() ? "(empty)" : Out;
        }

        xcmdline::parser::handle m_hLibrary, m_hAsset;
    };
}

#endif // E29_COMMANDS_SCRIPTING_H
