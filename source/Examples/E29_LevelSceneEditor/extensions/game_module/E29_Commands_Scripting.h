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
#include "source/Examples/E29_LevelSceneEditor/extensions/asset_browser/E29_Commands_AssetBrowser.h"
#include "source/Examples/E29_LevelSceneEditor/extensions/game_module/E29_ProjectScriptConfig.h"
#include "source/Examples/E29_LevelSceneEditor/extensions/game_module/E29_GameModuleSources.h"
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
            RegenerateGameModuleSources();
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto LibraryArg  = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg    = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            auto FileNameArg = m_Parser.getOptionArgAs<std::string>(m_hFileName, 0);

            const std::uint64_t Library = std::holds_alternative<xerr>(LibraryArg) ? 0 : std::strtoull(std::get<std::string>(LibraryArg).c_str(), nullptr, 16);
            File.Write(Library);
            xeditor::WriteString(File, std::holds_alternative<xerr>(AssetArg) ? std::string(32, '0') : std::get<std::string>(AssetArg));
            xeditor::WriteString(File, std::holds_alternative<xerr>(FileNameArg) ? std::string() : std::get<std::string>(FileNameArg));
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Library = 0; File.Read(Library);
            const std::string Asset       = xeditor::ReadString(File);
            const std::string FileNameB64 = xeditor::ReadString(File);

            const auto LibraryGuid = ParseLibraryGuid(std::format("{:016X}", Library));
            const auto AssetGuid   = ParseAssetGuid(Asset);
            const auto FileName    = DecodeAssetPath(FileNameB64);

            const auto SourceDb = ScriptSourceDbFolder(LibraryGuid, AssetGuid);
            if (SourceDb.empty()) return;
            std::error_code Ec;
            std::filesystem::remove(SourceDb + L"\\" + FileName, Ec);
            RegenerateGameModuleSources();
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
            RegenerateGameModuleSources();
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto LibraryArg  = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg    = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            auto FileNameArg = m_Parser.getOptionArgAs<std::string>(m_hFileName, 0);

            const std::uint64_t Library = std::holds_alternative<xerr>(LibraryArg) ? 0 : std::strtoull(std::get<std::string>(LibraryArg).c_str(), nullptr, 16);
            File.Write(Library);
            xeditor::WriteString(File, std::holds_alternative<xerr>(AssetArg) ? std::string(32, '0') : std::get<std::string>(AssetArg));
            xeditor::WriteString(File, std::holds_alternative<xerr>(FileNameArg) ? std::string() : std::get<std::string>(FileNameArg));

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
            xeditor::WriteString(File, Content);
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Library = 0; File.Read(Library);
            const std::string Asset       = xeditor::ReadString(File);
            const std::string FileNameB64 = xeditor::ReadString(File);
            const std::string Content     = xeditor::ReadString(File);

            const auto LibraryGuid = ParseLibraryGuid(std::format("{:016X}", Library));
            const auto AssetGuid   = ParseAssetGuid(Asset);
            const auto FileName    = DecodeAssetPath(FileNameB64);

            const auto SourceDb = ScriptSourceDbFolder(LibraryGuid, AssetGuid);
            if (SourceDb.empty()) return;
            std::error_code Ec;
            std::filesystem::create_directories(SourceDb, Ec);
            std::ofstream Out(SourceDb + L"\\" + FileName, std::ios::binary);
            if (Out.is_open()) Out.write(Content.data(), static_cast<std::streamsize>(Content.size()));
            RegenerateGameModuleSources();
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

    //================================================================================================
    // SetScriptSourceFileContent - overwrites an EXISTING source_db file's content wholesale
    // (undoable - previous content snapshotted, same shape as RemoveScriptSourceFile's own backup).
    // The file itself must already exist (AddScriptSourceFile first) - this only ever changes bytes,
    // never the file LIST, so it deliberately does NOT call RegenerateGameModuleSources(): a content-
    // only edit needs no cmake reconfigure, MSBuild picks up the changed timestamp on its own next
    // build (same "reconfigure only when the file list changes" rule the whole build integration
    // already follows). This is the ONLY command-bus path to actually write real code into a module -
    // without it an AI would have to bypass the undo system entirely to author anything.
    //================================================================================================
    struct set_script_source_file_content_cmd : xundo::command_base
    {
        set_script_source_file_content_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "SetScriptSourceFileContent", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Overwrites an existing source_db file's content (undoable - restores prior content). Usage: SetScriptSourceFileContent -Library hexguid -Asset assetguid -FileName base64 -Content base64"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary  = m_Parser.addOption("Library",  "Library instance guid, 16 hex digits", true, 1);
            m_hAsset    = m_Parser.addOption("Asset",    "Scripting asset guid, 32 hex digits",  true, 1);
            m_hFileName = m_Parser.addOption("FileName", "File name (e.g. \"Foo.cpp\"), Base64",  true, 1);
            m_hContent  = m_Parser.addOption("Content",  "New file content, Base64",              true, 1);
        }

        std::string Redo() noexcept override
        {
            auto LibraryArg  = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg    = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            auto FileNameArg = m_Parser.getOptionArgAs<std::string>(m_hFileName, 0);
            auto ContentArg  = m_Parser.getOptionArgAs<std::string>(m_hContent, 0);
            if (std::holds_alternative<xerr>(LibraryArg) || std::holds_alternative<xerr>(AssetArg) || std::holds_alternative<xerr>(FileNameArg) || std::holds_alternative<xerr>(ContentArg))
                return "SetScriptSourceFileContent: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto AssetGuid   = ParseAssetGuid(std::get<std::string>(AssetArg));
            const auto FileName    = DecodeAssetPath(std::get<std::string>(FileNameArg));
            const auto Content     = xeditor::Base64Decode(std::get<std::string>(ContentArg));

            const auto SourceDb = ScriptSourceDbFolder(LibraryGuid, AssetGuid);
            if (SourceDb.empty()) return "SetScriptSourceFileContent: asset not found";

            const auto FilePath = SourceDb + L"\\" + FileName;
            std::error_code Ec;
            if (!std::filesystem::exists(FilePath, Ec)) return "SetScriptSourceFileContent: no such file - AddScriptSourceFile first";

            std::ofstream Out(FilePath, std::ios::binary | std::ios::trunc);
            if (!Out.is_open()) return "SetScriptSourceFileContent: failed to open the file for writing";
            Out.write(Content.data(), static_cast<std::streamsize>(Content.size()));
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto LibraryArg  = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg    = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            auto FileNameArg = m_Parser.getOptionArgAs<std::string>(m_hFileName, 0);

            const std::uint64_t Library = std::holds_alternative<xerr>(LibraryArg) ? 0 : std::strtoull(std::get<std::string>(LibraryArg).c_str(), nullptr, 16);
            File.Write(Library);
            xeditor::WriteString(File, std::holds_alternative<xerr>(AssetArg) ? std::string(32, '0') : std::get<std::string>(AssetArg));
            xeditor::WriteString(File, std::holds_alternative<xerr>(FileNameArg) ? std::string() : std::get<std::string>(FileNameArg));

            std::string PrevContent;
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
                        PrevContent.assign((std::istreambuf_iterator<char>(In)), std::istreambuf_iterator<char>());
                }
            }
            xeditor::WriteString(File, PrevContent);
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Library = 0; File.Read(Library);
            const std::string Asset        = xeditor::ReadString(File);
            const std::string FileNameB64  = xeditor::ReadString(File);
            const std::string PrevContent  = xeditor::ReadString(File);

            const auto LibraryGuid = ParseLibraryGuid(std::format("{:016X}", Library));
            const auto AssetGuid   = ParseAssetGuid(Asset);
            const auto FileName    = DecodeAssetPath(FileNameB64);

            const auto SourceDb = ScriptSourceDbFolder(LibraryGuid, AssetGuid);
            if (SourceDb.empty()) return;
            std::ofstream Out(SourceDb + L"\\" + FileName, std::ios::binary | std::ios::trunc);
            if (Out.is_open()) Out.write(PrevContent.data(), static_cast<std::streamsize>(PrevContent.size()));
        }

        xcmdline::parser::handle m_hLibrary, m_hAsset, m_hFileName, m_hContent;
    };

    //================================================================================================
    // RenameScriptSourceFile - renames a file within a Scripting resource's own source_db folder
    // (undoable). Changes the file LIST (not just content), so - unlike SetScriptSourceFileContent -
    // this DOES call RegenerateGameModuleSources() on both Redo and Undo.
    //================================================================================================
    struct rename_script_source_file_cmd : xundo::command_base
    {
        rename_script_source_file_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "RenameScriptSourceFile", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Renames a file under a Scripting resource's own source_db folder (undoable). Usage: RenameScriptSourceFile -Library hexguid -Asset assetguid -OldFileName base64 -NewFileName base64"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary     = m_Parser.addOption("Library",     "Library instance guid, 16 hex digits",    true, 1);
            m_hAsset       = m_Parser.addOption("Asset",       "Scripting asset guid, 32 hex digits",     true, 1);
            m_hOldFileName = m_Parser.addOption("OldFileName", "Current file name, Base64",                true, 1);
            m_hNewFileName = m_Parser.addOption("NewFileName", "New file name, Base64",                    true, 1);
        }

        std::string Redo() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg   = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            auto OldArg     = m_Parser.getOptionArgAs<std::string>(m_hOldFileName, 0);
            auto NewArg     = m_Parser.getOptionArgAs<std::string>(m_hNewFileName, 0);
            if (std::holds_alternative<xerr>(LibraryArg) || std::holds_alternative<xerr>(AssetArg) || std::holds_alternative<xerr>(OldArg) || std::holds_alternative<xerr>(NewArg))
                return "RenameScriptSourceFile: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto AssetGuid   = ParseAssetGuid(std::get<std::string>(AssetArg));
            const auto OldName     = DecodeAssetPath(std::get<std::string>(OldArg));
            const auto NewName     = DecodeAssetPath(std::get<std::string>(NewArg));

            const auto SourceDb = ScriptSourceDbFolder(LibraryGuid, AssetGuid);
            if (SourceDb.empty()) return "RenameScriptSourceFile: asset not found";

            std::error_code Ec;
            if (!std::filesystem::exists(SourceDb + L"\\" + OldName, Ec)) return "RenameScriptSourceFile: no such file";
            if (std::filesystem::exists(SourceDb + L"\\" + NewName, Ec)) return "RenameScriptSourceFile: a file with that name already exists";

            std::filesystem::rename(SourceDb + L"\\" + OldName, SourceDb + L"\\" + NewName, Ec);
            if (Ec) return "RenameScriptSourceFile: rename failed";
            RegenerateGameModuleSources();
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg   = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            auto OldArg     = m_Parser.getOptionArgAs<std::string>(m_hOldFileName, 0);
            auto NewArg     = m_Parser.getOptionArgAs<std::string>(m_hNewFileName, 0);

            const std::uint64_t Library = std::holds_alternative<xerr>(LibraryArg) ? 0 : std::strtoull(std::get<std::string>(LibraryArg).c_str(), nullptr, 16);
            File.Write(Library);
            xeditor::WriteString(File, std::holds_alternative<xerr>(AssetArg) ? std::string(32, '0') : std::get<std::string>(AssetArg));
            xeditor::WriteString(File, std::holds_alternative<xerr>(OldArg) ? std::string() : std::get<std::string>(OldArg));
            xeditor::WriteString(File, std::holds_alternative<xerr>(NewArg) ? std::string() : std::get<std::string>(NewArg));
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Library = 0; File.Read(Library);
            const std::string Asset  = xeditor::ReadString(File);
            const std::string OldB64 = xeditor::ReadString(File);
            const std::string NewB64 = xeditor::ReadString(File);

            const auto LibraryGuid = ParseLibraryGuid(std::format("{:016X}", Library));
            const auto AssetGuid   = ParseAssetGuid(Asset);
            const auto OldName     = DecodeAssetPath(OldB64);
            const auto NewName     = DecodeAssetPath(NewB64);

            const auto SourceDb = ScriptSourceDbFolder(LibraryGuid, AssetGuid);
            if (SourceDb.empty()) return;
            std::error_code Ec;
            std::filesystem::rename(SourceDb + L"\\" + NewName, SourceDb + L"\\" + OldName, Ec);
            RegenerateGameModuleSources();
        }

        xcmdline::parser::handle m_hLibrary, m_hAsset, m_hOldFileName, m_hNewFileName;
    };

    //================================================================================================
    // AddProjectModuleReference / RemoveProjectModuleReference - the project's own build-membership
    // list (Project.config\Script.config.txt's ModuleRefs, see E29_ProjectScriptConfig.h). Persisted
    // immediately on every Redo/Undo, same convention AddLibraryDependency/RemoveLibraryDependency
    // already use (E29_Commands_LibraryDependency.h) - no separate "Save" step. No cycle/orphan
    // checks here (unlike library dependencies) - this is a flat membership list, not a graph edge; a
    // module-to-module dependency graph (if/when that's built) is a separate, later concern.
    //================================================================================================
    struct add_project_module_reference_cmd : xundo::command_base
    {
        add_project_module_reference_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "AddProjectModuleReference", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Adds a Script-Module resource to the project's build membership list (undoable, persisted immediately). Usage: AddProjectModuleReference -Module assetguid"; }
        void RegisterArguments() noexcept override
        {
            m_hModule = m_Parser.addOption("Module", "Script-Module asset guid, 32 hex digits", true, 1);
        }

        std::string Redo() noexcept override
        {
            auto ModuleArg = m_Parser.getOptionArgAs<std::string>(m_hModule, 0);
            if (std::holds_alternative<xerr>(ModuleArg)) return "AddProjectModuleReference: bad arguments";

            const auto ModuleGuid = ParseAssetGuid(std::get<std::string>(ModuleArg));
            auto& Refs = g_ScriptConfig.m_ModuleRefs;
            if (std::find(Refs.begin(), Refs.end(), ModuleGuid) != Refs.end()) return {};

            Refs.push_back(ModuleGuid);
            if (auto Err = SaveScriptConfig(e10::g_LibMgr.m_ProjectPath, g_ScriptConfig); Err)
                return std::format("AddProjectModuleReference: {}", Err.getMessage());
            RegenerateGameModuleSources();
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto ModuleArg = m_Parser.getOptionArgAs<std::string>(m_hModule, 0);
            xeditor::WriteString(File, std::holds_alternative<xerr>(ModuleArg) ? std::string(32, '0') : std::get<std::string>(ModuleArg));
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            const auto ModuleGuid = ParseAssetGuid(xeditor::ReadString(File));
            auto& Refs = g_ScriptConfig.m_ModuleRefs;
            if (auto It = std::find(Refs.begin(), Refs.end(), ModuleGuid); It != Refs.end())
                Refs.erase(It);
            SaveScriptConfig(e10::g_LibMgr.m_ProjectPath, g_ScriptConfig);
            RegenerateGameModuleSources();
        }

        xcmdline::parser::handle m_hModule;
    };

    struct remove_project_module_reference_cmd : xundo::command_base
    {
        remove_project_module_reference_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "RemoveProjectModuleReference", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Removes a Script-Module resource from the project's build membership list (undoable, persisted immediately). Usage: RemoveProjectModuleReference -Module assetguid"; }
        void RegisterArguments() noexcept override
        {
            m_hModule = m_Parser.addOption("Module", "Script-Module asset guid, 32 hex digits", true, 1);
        }

        std::string Redo() noexcept override
        {
            auto ModuleArg = m_Parser.getOptionArgAs<std::string>(m_hModule, 0);
            if (std::holds_alternative<xerr>(ModuleArg)) return "RemoveProjectModuleReference: bad arguments";

            const auto ModuleGuid = ParseAssetGuid(std::get<std::string>(ModuleArg));
            auto& Refs = g_ScriptConfig.m_ModuleRefs;
            auto It = std::find(Refs.begin(), Refs.end(), ModuleGuid);
            if (It == Refs.end()) return "RemoveProjectModuleReference: not a project module reference";
            // Captured as an INDEX, not kept as an iterator - Refs.erase(It) below invalidates It
            // itself (a stale iterator used later for a revert-insert would be undefined behavior).
            const auto OriginalIndex = static_cast<std::size_t>(std::distance(Refs.begin(), It));

            // Component-registry compatibility plan, Phase 5 - RESTORED to its originally-designed
            // strength (2026-09-19): a real trial rebuild-and-probe, reverting the removal on a
            // genuine mismatch. An earlier version of this session descoped it to a static warning
            // after live testing surfaced what looked like a CMake/MSBuild incremental-build
            // reliability gap - since root-caused and fixed (BuildGamePluginIfStale now touches
            // cmake_pch.cxx before every build - see that function's own comment for the full isolated
            // repro/fix), so the trial rebuild this needs is trustworthy again. Verified live: 4
            // consecutive module add/remove cycles through the real Play/reload path all correctly
            // reflected the change afterward.
            std::vector<xecs::scene::component_dependency> PluginOwnedBefore;
            if (g_pGamePlugin && g_pGamePlugin->m_hModule)
            {
                game_plugin_candidate CurrentView{ g_pGamePlugin->m_hModule, {} };
                PluginOwnedBefore = ProbeCandidateComponents(CurrentView);
            }

            std::vector<xecs::scene::component_dependency> RequiredFromOpenScenes;
            if (g_pState)
            {
                std::unordered_set<std::uint64_t> PluginOwnedGuids;
                for (auto& D : PluginOwnedBefore) PluginOwnedGuids.insert(D.m_Guid.m_Value);

                for (auto& SceneGuid : g_pState->m_OpenScenes)
                    for (auto& Dep : xecs::scene::LoadSceneComponentDependencies(e10::g_LibMgr.m_ProjectPath, SceneGuid))
                        if (PluginOwnedGuids.contains(Dep.m_Guid.m_Value))
                            RequiredFromOpenScenes.push_back(Dep);
            }

            Refs.erase(It);
            if (auto Err = SaveScriptConfig(e10::g_LibMgr.m_ProjectPath, g_ScriptConfig); Err)
            {
                Refs.insert(Refs.begin() + static_cast<std::ptrdiff_t>(OriginalIndex), ModuleGuid); // restore in-memory state to match what's still on disk
                return std::format("RemoveProjectModuleReference: {}", Err.getMessage());
            }
            RegenerateGameModuleSources();

            // Only worth a real trial compile if removing this module could plausibly affect anything
            // currently open - skip it entirely (the common case) rather than pay a compile for a
            // guaranteed-safe removal.
            if (!RequiredFromOpenScenes.empty() && g_pGamePlugin)
            {
                // Wait for any in-flight ASYNC build already started elsewhere (window-focus-regain
                // fires automatically - see StartGameReload's own comment) to finish first - a real
                // race found live earlier this session: two concurrent `cmake --build` invocations
                // against the same output DLL raced, and whichever finished last won regardless of
                // which fragment it was building from. Safe to .wait() without .get()'ing it, since
                // PollGameReload (the only other consumer) runs on this same main thread.
                if (g_pGamePlugin->m_bBuilding) g_pGamePlugin->m_BuildFuture.wait();

                const auto BuildResult = BuildGamePluginIfStale(*g_pGamePlugin, GetLatestModuleSourceWriteTime());
                if (BuildResult == build_result::Rebuilt)
                {
                    const std::uint32_t TrialGeneration = g_pGamePlugin->m_Token.m_Generation + 1000000; // scratch-only, never Commit'ed
                    auto Candidate = PrepareGamePluginCandidate(g_pGamePlugin->m_CompiledDllPath, TrialGeneration);
                    if (Candidate.m_hModule)
                    {
                        const auto NewManifest = ProbeCandidateComponents(Candidate);
                        std::unordered_set<std::uint64_t> Available;
                        for (auto& D : NewManifest) Available.insert(D.m_Guid.m_Value);

                        auto Missing = CheckComponentCompatibility(RequiredFromOpenScenes, [&](xecs::component::type::guid Guid) noexcept
                        {
                            return Available.contains(Guid.m_Value);
                        });
                        DiscardGamePluginCandidate(Candidate);

                        if (!Missing.empty())
                        {
                            // Revert - put the reference back exactly as it was, re-persist, re-
                            // regenerate the fragment so a LATER real reload rebuilds WITH the module
                            // again (not the trial DLL this command just discarded).
                            Refs.insert(Refs.begin() + static_cast<std::ptrdiff_t>(OriginalIndex), ModuleGuid);
                            SaveScriptConfig(e10::g_LibMgr.m_ProjectPath, g_ScriptConfig);
                            RegenerateGameModuleSources();

                            std::string Names;
                            for (auto& Dep : Missing) Names += (Names.empty() ? "" : ", ") + Dep.m_Name;
                            return std::format("RemoveProjectModuleReference: refused - {} currently-open component(s) would break: {}", Missing.size(), Names);
                        }
                    }
                }
                // A build failure here (bad code elsewhere, unrelated to this removal) isn't this
                // command's problem to solve - the removal already committed, same as it would have
                // without this check at all; the existing reload machinery will surface the failure
                // through its own normal path next time it runs.
            }

            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto ModuleArg = m_Parser.getOptionArgAs<std::string>(m_hModule, 0);
            std::uint32_t Index = 0;
            if (!std::holds_alternative<xerr>(ModuleArg))
            {
                const auto ModuleGuid = ParseAssetGuid(std::get<std::string>(ModuleArg));
                auto& Refs = g_ScriptConfig.m_ModuleRefs;
                if (auto It = std::find(Refs.begin(), Refs.end(), ModuleGuid); It != Refs.end())
                    Index = static_cast<std::uint32_t>(std::distance(Refs.begin(), It));
            }
            xeditor::WriteString(File, std::holds_alternative<xerr>(ModuleArg) ? std::string(32, '0') : std::get<std::string>(ModuleArg));
            File.Write(Index);
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            const auto ModuleGuid = ParseAssetGuid(xeditor::ReadString(File));
            std::uint32_t Index = 0; File.Read(Index);

            auto& Refs = g_ScriptConfig.m_ModuleRefs;
            if (std::find(Refs.begin(), Refs.end(), ModuleGuid) == Refs.end())
            {
                const auto Idx = std::min<std::size_t>(Index, Refs.size());
                Refs.insert(Refs.begin() + static_cast<std::ptrdiff_t>(Idx), ModuleGuid);
            }
            SaveScriptConfig(e10::g_LibMgr.m_ProjectPath, g_ScriptConfig);
            RegenerateGameModuleSources();
        }

        xcmdline::parser::handle m_hModule;
    };

    //================================================================================================
    // ListProjectModuleReferences - the project's current build-membership list, one guid per line.
    // Discovery command, same "never need to read a raw file by hand" reasoning every other list
    // command in this system was built for.
    //================================================================================================
    struct list_project_module_references_query_cmd : xundo::query_command_base
    {
        list_project_module_references_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "ListProjectModuleReferences", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Lists the project's current Script-Module build-membership list. Usage: ListProjectModuleReferences"; }
        void RegisterArguments() noexcept override {}

        std::string Query() noexcept override
        {
            if (g_ScriptConfig.m_ModuleRefs.empty()) return "(empty)";
            std::string Out;
            for (auto& G : g_ScriptConfig.m_ModuleRefs)
                Out += FormatAssetGuid(G) + "\n";
            return Out;
        }
    };

    //================================================================================================
    // RegenerateProjectModuleSources - force-regenerates GameProject\E29_Game_Modules.cmake from the
    // CURRENT build-membership list, on demand. Every mutating command in this file already triggers
    // this as a side effect - this exists for recovery/debugging (e.g. after a raw file edit made
    // outside the command bus) rather than any normal workflow needing to call it directly.
    //================================================================================================
    struct regenerate_project_module_sources_query_cmd : xundo::query_command_base
    {
        regenerate_project_module_sources_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "RegenerateProjectModuleSources", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Force-regenerates the CMake module-sources fragment from the current build-membership list. Usage: RegenerateProjectModuleSources"; }
        void RegisterArguments() noexcept override {}

        std::string Query() noexcept override
        {
            RegenerateGameModuleSources();
            return "RegenerateProjectModuleSources: regenerated";
        }
    };
}

#endif // E29_COMMANDS_SCRIPTING_H
