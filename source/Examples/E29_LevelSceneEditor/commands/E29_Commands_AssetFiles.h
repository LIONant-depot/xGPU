#ifndef E29_COMMANDS_ASSET_FILES_H
#define E29_COMMANDS_ASSET_FILES_H
#pragma once

// Raw Asset FILE command/undo layer - Phase 4 of the Asset Browser window-split plan (see plan file
// lively-knitting-sifakis.md). Wraps e10::library_mgr's own new raw-file primitives (E10_AssetMgr.h:
// MoveAssetFile/ComputeTrashPath/CopyAssetFile) as thin xundo commands - same "not modifying
// library_mgr itself" shape E29_Commands_AssetBrowser.h already established for the VIRTUAL descriptor
// tree's own Rename/Move/Delete commands, just for the REAL Assets/ folder on disk instead.
//
// Every path argument here is a full path relative to the LIBRARY ROOT (e.g.
// "Assets\\Textures\\wood.png" - matching library_db::asset::m_Path's own convention), Base64-encoded
// since real paths contain backslashes/spaces that would otherwise collide with the CLI's own token
// splitting - same reasoning Name already gets Base64-encoded for in E29_Commands_AssetBrowser.h.
//
// DeleteAssetFileToTrash's own -TrashPath argument is REQUIRED, not auto-computed by Redo() itself:
// Redo() only ever sees its own command-line arguments (it has no access to whatever
// BackupCurrenState computed, and BackupCurrenState runs BEFORE Redo so it can't know a value Redo
// would only decide at call time either) - so the exact trash destination must be pre-minted by the
// CALLER via library_mgr::ComputeTrashPath BEFORE constructing this command string, the same "-Id
// pre-minted by the caller" shape CreateAsset/MakePrefab already use for their own Redo-determinism
// (E29_Commands_AssetBrowser.h), just applied to a path instead of a guid. The eventual UI hook
// (assert_browser's own m_On* callbacks, RegisterAssetBrowserCallbacks) is where that pre-mint call
// belongs, mirroring m_OnCreateAsset's own existing pattern - not built yet as of this pass, CLI-only
// for now (matches this whole project's own "discovery/CLI first, UI wiring verified last" phasing).
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"

namespace e29::commands
{
    inline std::string EncodeAssetPath(const std::wstring& Path) noexcept { return Base64Encode(xstrtool::To(Path)); }
    inline std::wstring DecodeAssetPath(const std::string& Encoded) noexcept { return xstrtool::To(Base64Decode(Encoded)); }

    // "-Force 1" bypasses the dependent-count warning below - the CLI/AI equivalent of clicking
    // "Continue" on the Asset Tree's own confirmation dialog (E10_asset_browser_files_tab.h's
    // StageOrExecute/RenderPendingConfirmationModal), since there is no dialog for a script to click.
    // Direct user request: "Proper Commands should take in a flag to suppress those dialogs... so AI
    // can do its job."
    inline bool IsForced(const xcmdline::parser& Parser, xcmdline::parser::handle hForce) noexcept
    {
        auto ForceArg = Parser.getOptionArgAs<std::string>(hForce, 0);
        return !std::holds_alternative<xerr>(ForceArg) && std::get<std::string>(ForceArg) == "1";
    }

    //================================================================================================
    // RenameAssetFile / MoveAssetFile - both are thin, IDENTICAL wraps of library_mgr::MoveAssetFile
    // (a rename IS a move within the same folder - there is only one underlying primitive), kept as
    // two separately-named commands purely for discoverability/intent clarity from the CLI or an AI,
    // matching how RenameAsset/MoveAsset already coexist as two names in
    // E29_Commands_AssetBrowser.h. Undo is the exact inverse MoveAssetFile call, old/new swapped - it
    // re-cascades back through every dependent's Descriptor.txt exactly like Redo did forward.
    //================================================================================================
    struct rename_asset_file_cmd : xundo::command_base
    {
        rename_asset_file_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "RenameAssetFile", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Renames a real file in the Assets folder, cascading the change into every dependent resource's Descriptor.txt (undoable). Fails with a count if other resources depend on it, unless -Force 1 is passed. Usage: RenameAssetFile -Library hexguid -OldPath base64 -NewPath base64 [-Force 1]"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary = m_Parser.addOption("Library", "Library instance guid, 16 hex digits",                  true, 1);
            m_hOldPath = m_Parser.addOption("OldPath",  "Current path relative to the library root, Base64",    true, 1);
            m_hNewPath = m_Parser.addOption("NewPath",  "New path relative to the library root, Base64",        true, 1);
            m_hForce   = m_Parser.addOption("Force",    "Pass 1 to skip the dependent-count check and proceed anyway (for AI/script use - there is no dialog to click)", false, 1);
        }

        std::string Redo() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto OldArg      = m_Parser.getOptionArgAs<std::string>(m_hOldPath, 0);
            auto NewArg      = m_Parser.getOptionArgAs<std::string>(m_hNewPath, 0);
            if (std::holds_alternative<xerr>(LibraryArg) || std::holds_alternative<xerr>(OldArg) || std::holds_alternative<xerr>(NewArg))
                return "RenameAssetFile: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto OldPath     = DecodeAssetPath(std::get<std::string>(OldArg));
            const auto NewPath     = DecodeAssetPath(std::get<std::string>(NewArg));

            if (!IsForced(m_Parser, m_hForce))
            {
                if (const auto Count = e10::g_LibMgr.CountDependents(LibraryGuid, OldPath); Count > 0)
                    return std::format("RenameAssetFile: {} other resource(s) depend on this - pass -Force 1 to proceed anyway", Count);
            }

            const auto Result = e10::g_LibMgr.MoveAssetFile(LibraryGuid, OldPath, NewPath);
            if (!Result.m_bSuccess) return std::format("RenameAssetFile: {}", Result.m_Error);
            if (!Result.m_FailedDependents.empty()) return std::format("RenameAssetFile: moved, but {} dependent(s) could not be updated - see log", Result.m_FailedDependents.size());
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto OldArg      = m_Parser.getOptionArgAs<std::string>(m_hOldPath, 0);
            auto NewArg      = m_Parser.getOptionArgAs<std::string>(m_hNewPath, 0);

            const std::uint64_t Library = std::holds_alternative<xerr>(LibraryArg) ? 0 : std::strtoull(std::get<std::string>(LibraryArg).c_str(), nullptr, 16);
            File.Write(Library);
            WriteString(File, std::holds_alternative<xerr>(OldArg) ? std::string() : std::get<std::string>(OldArg));
            WriteString(File, std::holds_alternative<xerr>(NewArg) ? std::string() : std::get<std::string>(NewArg));
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Library = 0; File.Read(Library);
            const std::string OldArg = ReadString(File);
            const std::string NewArg = ReadString(File);

            const auto LibraryGuid = ParseLibraryGuid(std::format("{:016X}", Library));
            // Undo is the exact inverse move - it re-cascades back through the same files Redo did,
            // so it never needs its own dependent-count check (Force doesn't apply to Undo).
            e10::g_LibMgr.MoveAssetFile(LibraryGuid, DecodeAssetPath(NewArg), DecodeAssetPath(OldArg));
        }

        xcmdline::parser::handle m_hLibrary, m_hOldPath, m_hNewPath, m_hForce;
    };

    struct move_asset_file_cmd : xundo::command_base
    {
        move_asset_file_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "MoveAssetFile", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Moves a real file in the Assets folder to a different folder, cascading the change into every dependent resource's Descriptor.txt (undoable). Fails with a count if other resources depend on it, unless -Force 1 is passed. Usage: MoveAssetFile -Library hexguid -OldPath base64 -NewPath base64 [-Force 1]"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary = m_Parser.addOption("Library", "Library instance guid, 16 hex digits",                true, 1);
            m_hOldPath = m_Parser.addOption("OldPath",  "Current path relative to the library root, Base64",  true, 1);
            m_hNewPath = m_Parser.addOption("NewPath",  "New path relative to the library root, Base64",      true, 1);
            m_hForce   = m_Parser.addOption("Force",    "Pass 1 to skip the dependent-count check and proceed anyway (for AI/script use - there is no dialog to click)", false, 1);
        }

        std::string Redo() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto OldArg      = m_Parser.getOptionArgAs<std::string>(m_hOldPath, 0);
            auto NewArg      = m_Parser.getOptionArgAs<std::string>(m_hNewPath, 0);
            if (std::holds_alternative<xerr>(LibraryArg) || std::holds_alternative<xerr>(OldArg) || std::holds_alternative<xerr>(NewArg))
                return "MoveAssetFile: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto OldPath     = DecodeAssetPath(std::get<std::string>(OldArg));
            const auto NewPath     = DecodeAssetPath(std::get<std::string>(NewArg));

            if (!IsForced(m_Parser, m_hForce))
            {
                if (const auto Count = e10::g_LibMgr.CountDependents(LibraryGuid, OldPath); Count > 0)
                    return std::format("MoveAssetFile: {} other resource(s) depend on this - pass -Force 1 to proceed anyway", Count);
            }

            const auto Result = e10::g_LibMgr.MoveAssetFile(LibraryGuid, OldPath, NewPath);
            if (!Result.m_bSuccess) return std::format("MoveAssetFile: {}", Result.m_Error);
            if (!Result.m_FailedDependents.empty()) return std::format("MoveAssetFile: moved, but {} dependent(s) could not be updated - see log", Result.m_FailedDependents.size());
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto OldArg      = m_Parser.getOptionArgAs<std::string>(m_hOldPath, 0);
            auto NewArg      = m_Parser.getOptionArgAs<std::string>(m_hNewPath, 0);

            const std::uint64_t Library = std::holds_alternative<xerr>(LibraryArg) ? 0 : std::strtoull(std::get<std::string>(LibraryArg).c_str(), nullptr, 16);
            File.Write(Library);
            WriteString(File, std::holds_alternative<xerr>(OldArg) ? std::string() : std::get<std::string>(OldArg));
            WriteString(File, std::holds_alternative<xerr>(NewArg) ? std::string() : std::get<std::string>(NewArg));
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Library = 0; File.Read(Library);
            const std::string OldArg = ReadString(File);
            const std::string NewArg = ReadString(File);

            const auto LibraryGuid = ParseLibraryGuid(std::format("{:016X}", Library));
            // Undo is the exact inverse move - it re-cascades back through the same files Redo did, so
            // it never needs its own dependent-count check (Force doesn't apply to Undo).
            e10::g_LibMgr.MoveAssetFile(LibraryGuid, DecodeAssetPath(NewArg), DecodeAssetPath(OldArg));
        }

        xcmdline::parser::handle m_hLibrary, m_hOldPath, m_hNewPath, m_hForce;
    };

    //================================================================================================
    // DeleteAssetFileToTrash - soft delete, symmetric with RestoreAssetFileFromTrash below. -TrashPath
    // is REQUIRED (see this file's own top comment for why it can't be computed inside Redo() itself) -
    // call e10::g_LibMgr.ComputeTrashPath(Library, Path) first to get it.
    //================================================================================================
    struct delete_asset_file_cmd : xundo::command_base
    {
        delete_asset_file_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "DeleteAssetFileToTrash", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Moves a real Assets file to the trash (undoable - restores it). -TrashPath must be pre-computed via ComputeTrashPath. Fails with a count if other resources depend on it, unless -Force 1 is passed. Usage: DeleteAssetFileToTrash -Library hexguid -Path base64 -TrashPath base64 [-Force 1]"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary   = m_Parser.addOption("Library",   "Library instance guid, 16 hex digits",                       true, 1);
            m_hPath      = m_Parser.addOption("Path",      "Path relative to the library root, Base64",                  true, 1);
            m_hTrashPath = m_Parser.addOption("TrashPath", "Destination trash path, Base64 - from ComputeTrashPath",     true, 1);
            m_hForce     = m_Parser.addOption("Force",     "Pass 1 to skip the dependent-count check and proceed anyway (for AI/script use - there is no dialog to click)", false, 1);
        }

        std::string Redo() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto PathArg     = m_Parser.getOptionArgAs<std::string>(m_hPath, 0);
            auto TrashArg    = m_Parser.getOptionArgAs<std::string>(m_hTrashPath, 0);
            if (std::holds_alternative<xerr>(LibraryArg) || std::holds_alternative<xerr>(PathArg) || std::holds_alternative<xerr>(TrashArg))
                return "DeleteAssetFileToTrash: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto Path        = DecodeAssetPath(std::get<std::string>(PathArg));
            const auto TrashPath   = DecodeAssetPath(std::get<std::string>(TrashArg));

            if (!IsForced(m_Parser, m_hForce))
            {
                if (const auto Count = e10::g_LibMgr.CountDependents(LibraryGuid, Path); Count > 0)
                    return std::format("DeleteAssetFileToTrash: {} other resource(s) depend on this - deleting will leave them pointing at the Trash location. Pass -Force 1 to proceed anyway", Count);
            }

            const auto Result = e10::g_LibMgr.MoveAssetFile(LibraryGuid, Path, TrashPath);
            if (!Result.m_bSuccess) return std::format("DeleteAssetFileToTrash: {}", Result.m_Error);
            if (!Result.m_FailedDependents.empty()) return std::format("DeleteAssetFileToTrash: moved, but {} dependent(s) could not be updated - see log", Result.m_FailedDependents.size());
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto PathArg     = m_Parser.getOptionArgAs<std::string>(m_hPath, 0);
            auto TrashArg    = m_Parser.getOptionArgAs<std::string>(m_hTrashPath, 0);

            const std::uint64_t Library = std::holds_alternative<xerr>(LibraryArg) ? 0 : std::strtoull(std::get<std::string>(LibraryArg).c_str(), nullptr, 16);
            File.Write(Library);
            WriteString(File, std::holds_alternative<xerr>(PathArg) ? std::string() : std::get<std::string>(PathArg));
            WriteString(File, std::holds_alternative<xerr>(TrashArg) ? std::string() : std::get<std::string>(TrashArg));
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Library = 0; File.Read(Library);
            const std::string PathArg  = ReadString(File);
            const std::string TrashArg = ReadString(File);

            const auto LibraryGuid = ParseLibraryGuid(std::format("{:016X}", Library));
            e10::g_LibMgr.MoveAssetFile(LibraryGuid, DecodeAssetPath(TrashArg), DecodeAssetPath(PathArg));
        }

        xcmdline::parser::handle m_hLibrary, m_hPath, m_hTrashPath, m_hForce;
    };

    //================================================================================================
    // RestoreAssetFileFromTrash - the forward counterpart to DeleteAssetFileToTrash's own Undo (a
    // genuine action in its own right, e.g. a Trash-panel "Restore" button, not just something that
    // happens via Ctrl+Z). Fails loudly (no silent overwrite) if OriginalPath is already occupied -
    // MoveAssetFile's own existing "target already exists" check already covers this, nothing extra
    // needed here.
    //================================================================================================
    struct restore_asset_file_cmd : xundo::command_base
    {
        restore_asset_file_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "RestoreAssetFileFromTrash", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Restores a trashed Assets file back to its original (or a chosen) path (undoable). Usage: RestoreAssetFileFromTrash -Library hexguid -TrashPath base64 -OriginalPath base64"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary      = m_Parser.addOption("Library",      "Library instance guid, 16 hex digits",       true, 1);
            m_hTrashPath    = m_Parser.addOption("TrashPath",    "Current trash path, Base64",                 true, 1);
            m_hOriginalPath = m_Parser.addOption("OriginalPath", "Path to restore to, Base64",                 true, 1);
        }

        std::string Redo() noexcept override
        {
            auto LibraryArg  = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto TrashArg     = m_Parser.getOptionArgAs<std::string>(m_hTrashPath, 0);
            auto OriginalArg  = m_Parser.getOptionArgAs<std::string>(m_hOriginalPath, 0);
            if (std::holds_alternative<xerr>(LibraryArg) || std::holds_alternative<xerr>(TrashArg) || std::holds_alternative<xerr>(OriginalArg))
                return "RestoreAssetFileFromTrash: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto TrashPath   = DecodeAssetPath(std::get<std::string>(TrashArg));
            const auto OriginalPath = DecodeAssetPath(std::get<std::string>(OriginalArg));

            const auto Result = e10::g_LibMgr.MoveAssetFile(LibraryGuid, TrashPath, OriginalPath);
            if (!Result.m_bSuccess) return std::format("RestoreAssetFileFromTrash: {}", Result.m_Error);
            if (!Result.m_FailedDependents.empty()) return std::format("RestoreAssetFileFromTrash: moved, but {} dependent(s) could not be updated - see log", Result.m_FailedDependents.size());
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto LibraryArg  = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto TrashArg     = m_Parser.getOptionArgAs<std::string>(m_hTrashPath, 0);
            auto OriginalArg  = m_Parser.getOptionArgAs<std::string>(m_hOriginalPath, 0);

            const std::uint64_t Library = std::holds_alternative<xerr>(LibraryArg) ? 0 : std::strtoull(std::get<std::string>(LibraryArg).c_str(), nullptr, 16);
            File.Write(Library);
            WriteString(File, std::holds_alternative<xerr>(TrashArg) ? std::string() : std::get<std::string>(TrashArg));
            WriteString(File, std::holds_alternative<xerr>(OriginalArg) ? std::string() : std::get<std::string>(OriginalArg));
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Library = 0; File.Read(Library);
            const std::string TrashArg    = ReadString(File);
            const std::string OriginalArg = ReadString(File);

            const auto LibraryGuid = ParseLibraryGuid(std::format("{:016X}", Library));
            e10::g_LibMgr.MoveAssetFile(LibraryGuid, DecodeAssetPath(OriginalArg), DecodeAssetPath(TrashArg));
        }

        xcmdline::parser::handle m_hLibrary, m_hTrashPath, m_hOriginalPath;
    };

    //================================================================================================
    // CopyAssetFile - the one command in this file whose Undo does a genuinely real deletion... except
    // it doesn't, quite: rather than a raw std::filesystem::remove, Undo routes the just-created copy
    // through the SAME soft-delete primitive every other "delete" in this system already uses
    // (ComputeTrashPath + MoveAssetFile to it) - nothing here ever does a silent, unrecoverable delete
    // outside that one shared Trash mechanism, at the cost of an occasional harmless trash entry left
    // behind by an undone copy. A fresh copy has zero dependents by construction (nothing could
    // reference a path that didn't exist a moment ago), so there's no cascade to worry about either way.
    //================================================================================================
    struct copy_asset_file_cmd : xundo::command_base
    {
        copy_asset_file_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "CopyAssetFile", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Copies a real Assets file to a new path (undoable - Undo trashes the copy, it does not permanently delete it). Usage: CopyAssetFile -Library hexguid -SourcePath base64 -NewPath base64"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary    = m_Parser.addOption("Library",    "Library instance guid, 16 hex digits",             true, 1);
            m_hSourcePath = m_Parser.addOption("SourcePath", "Path to copy from, relative to library root, Base64", true, 1);
            m_hNewPath    = m_Parser.addOption("NewPath",    "Path to copy to, relative to library root, Base64",   true, 1);
        }

        std::string Redo() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto SourceArg   = m_Parser.getOptionArgAs<std::string>(m_hSourcePath, 0);
            auto NewArg      = m_Parser.getOptionArgAs<std::string>(m_hNewPath, 0);
            if (std::holds_alternative<xerr>(LibraryArg) || std::holds_alternative<xerr>(SourceArg) || std::holds_alternative<xerr>(NewArg))
                return "CopyAssetFile: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto SourcePath  = DecodeAssetPath(std::get<std::string>(SourceArg));
            const auto NewPath     = DecodeAssetPath(std::get<std::string>(NewArg));

            const auto Result = e10::g_LibMgr.CopyAssetFile(LibraryGuid, SourcePath, NewPath);
            if (!Result.m_bSuccess) return std::format("CopyAssetFile: {}", Result.m_Error);
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto NewArg      = m_Parser.getOptionArgAs<std::string>(m_hNewPath, 0);

            const std::uint64_t Library = std::holds_alternative<xerr>(LibraryArg) ? 0 : std::strtoull(std::get<std::string>(LibraryArg).c_str(), nullptr, 16);
            File.Write(Library);
            WriteString(File, std::holds_alternative<xerr>(NewArg) ? std::string() : std::get<std::string>(NewArg));
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Library = 0; File.Read(Library);
            const std::string NewArg = ReadString(File);

            const auto LibraryGuid = ParseLibraryGuid(std::format("{:016X}", Library));
            const auto NewPath     = DecodeAssetPath(NewArg);
            const auto TrashPath   = e10::g_LibMgr.ComputeTrashPath(LibraryGuid, NewPath);
            e10::g_LibMgr.MoveAssetFile(LibraryGuid, NewPath, TrashPath);
        }

        xcmdline::parser::handle m_hLibrary, m_hSourcePath, m_hNewPath;
    };
}

#endif // E29_COMMANDS_ASSET_FILES_H
