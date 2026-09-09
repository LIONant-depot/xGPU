#ifndef E29_COMMANDS_ASSET_BROWSER_H
#define E29_COMMANDS_ASSET_BROWSER_H
#pragma once

// Asset Browser command/undo layer - the foundation Make Prefab was deliberately left out of the
// prior gap-closing pass for (see [[e29_command_undo_known_gaps]]): CreatePrefabFromGroupRoot calls
// AssetMgr.NewAsset to create a real Prefab asset on disk, and no command in this system had ever had
// to reverse an asset-library creation. Direct user framing: "I think make prefab or create prefab
// instance may depend on the asset browser... We need to make it work with commands as well so it
// can be added into a global undo system." This file wraps e10::library_mgr's own real mutation
// primitives (E10_AssetMgr.h) as thin xundo commands - not modifying library_mgr itself, so the other
// 7 examples that embed the same Asset Browser (E10, E19-E21, E23-E25, E28) are unaffected.
//
// Every real mutation lives in library_mgr: NewAsset (writes info.txt to disk immediately + inserts
// into the in-memory tree), RenameDescriptor/MoveDescriptor/MoveToTrash/MoveFromTrashTo (in-memory
// only until a manual Save flushes dirty descriptors to disk). EmptyTrashcan does real, irreversible
// std::filesystem::remove_all and the UI already warns "cannot be undone" - deliberately EXCLUDED
// from this undo layer, stays exactly as it is today. DeleteDescriptor is dead/commented-out code -
// MoveToTrash/MoveFromTrashTo is the only removal primitive that exists at all, which is why
// CreateAsset's own Undo (below) can only trash what it created, not make it vanish from disk.
//
// An asset guid is a genuine xresource::full_guid (instance+type - Prefab/Scene/Level/Texture/...
// all share one library) unlike Scene/Level/Library's def_guid<> (one fixed, compile-time-known
// type), so it needs both halves on the command line - see ParseAssetGuid/FormatAssetGuid
// (E29_CommandContext.h), 32 hex digits (16 instance + 16 type) as one token, same "one guid, one
// argument" shape every other guid convention here already has.
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"

namespace e29::commands
{
    // The root folder for a given Library - e10::library_mgr's own convention (E10_AssetMgr.h,
    // OpenProject: `RootGUID = { m_ProjectGUID.m_Instance, folder::type_guid_v }`) is that a
    // library's root folder shares its OWN instance value, just tagged as a folder-type asset instead
    // of a library. Used as ListAssets' own default -Parent, so a fresh AI/CLI session can discover
    // the whole tree starting from nothing but a Library guid (ListLevels/ListScenes's own already-
    // known guids would otherwise be the only bootstrap, which begs the question for anything that
    // ISN'T a Level/Scene).
    inline xresource::full_guid LibraryRootFolderGuid(e10::library::guid LibraryGuid) noexcept
    {
        return xresource::full_guid{ .m_Instance = LibraryGuid.m_Instance, .m_Type = e10::folder::type_guid_v };
    }

    //================================================================================================
    // ListAssets - every direct child of -Parent (default: the Library's own root folder), one per
    // line as "assetguid  TypeName  Name". Type name resolved via m_AssetPluginsDB the same way
    // NewAsset's own error-message construction already does; falls back to the raw hex type guid for
    // an asset type with no registered plugin name (matches DescribeEntity's own "show the guid raw
    // rather than fail" fallback for an unregistered component type).
    //================================================================================================
    struct list_assets_query_cmd : xundo::query_command_base
    {
        list_assets_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "ListAssets", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Lists every direct child of an asset (default: the Library's own root folder). Usage: ListAssets -Library hexguid [-Parent assetguid]"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary = m_Parser.addOption("Library", "Library instance guid, 16 hex digits",                          true,  1);
            m_hParent  = m_Parser.addOption("Parent",  "Parent asset guid, 32 hex digits (default: the library root)", false, 1);
        }

        std::string Query() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            if (std::holds_alternative<xerr>(LibraryArg)) return "ListAssets: bad arguments";
            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));

            auto ParentArg = m_Parser.getOptionArgAs<std::string>(m_hParent, 0);
            const auto ParentGuid = std::holds_alternative<xerr>(ParentArg) ? LibraryRootFolderGuid(LibraryGuid) : ParseAssetGuid(std::get<std::string>(ParentArg));

            std::string Out;
            // Deliberately NOT noexcept - library_mgr::getInfo/getNodeInfo pick a const-vs-write
            // overload via details::function_traits<T_CALLBACK>::arg<0>, the same kind of trait-
            // matching machinery that already broke on a noexcept callback once this session
            // ([[xgpu_xcontainer_noexcept_lambda_trait_trap]], a DIFFERENT library's DIFFERENT trait
            // template) - not risking a second instance of that exact bug class here.
            // A trashed asset is still linked from its OLD parent's own m_lChildLinks - confirmed
            // live: MoveToTrash only prepends the trash tag to the asset's OWN m_RscLinks, it never
            // unlinks the old parent's child-list entry. Skipped here (unless the caller is actually
            // asking to list the trash folder itself) so a listing doesn't show a deleted asset as
            // still present in two places at once - same "front of m_RscLinks is the trash tag" check
            // MoveToTrash's own source establishes as the trashed-or-not signal.
            const bool bListingTrash = ParentGuid == e10::folder::trash_guid_v;
            const bool bFound = e10::g_LibMgr.getNodeInfo(LibraryGuid, ParentGuid, [&](const e10::library_db::info_node& Node)
            {
                for (auto& Child : Node.m_lChildLinks)
                {
                    if (!bListingTrash)
                    {
                        bool bIsTrashed = false;
                        e10::g_LibMgr.getInfo(LibraryGuid, Child, [&](const xresource_pipeline::info& Info)
                        {
                            bIsTrashed = !Info.m_RscLinks.empty() && Info.m_RscLinks.front() == e10::folder::trash_guid_v;
                        });
                        if (bIsTrashed) continue;
                    }

                    std::string TypeName = std::format("{:016X}", Child.m_Type.m_Value);
                    if (auto* pPlugin = e10::g_LibMgr.m_AssetPluginsDB.find(Child.m_Type)) TypeName = pPlugin->m_TypeName;

                    std::string Name = "(unnamed)";
                    e10::g_LibMgr.getInfo(LibraryGuid, Child, [&](const xresource_pipeline::info& Info) { Name = Info.m_Name; });

                    Out += std::format("{}  {}  {}\n", FormatAssetGuid(Child), TypeName, Name);
                }
            });
            if (!bFound) return "ListAssets: parent not found";
            return Out;
        }

        xcmdline::parser::handle m_hLibrary, m_hParent;
    };

    //================================================================================================
    // DescribeAsset - one asset's name/type, every parent link (m_RscLinks - an asset can have more
    // than one, e.g. shared into multiple folders), and every direct child (m_lChildLinks) - the
    // discovery command MoveAsset/RenameAsset/DeleteAsset need before they can target anything, same
    // "never need to read a raw file by hand" reasoning DescribeEntity was built for.
    //================================================================================================
    struct describe_asset_query_cmd : xundo::query_command_base
    {
        describe_asset_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "DescribeAsset", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Shows an asset's name, type, parent links, and children. Usage: DescribeAsset -Library hexguid -Asset assetguid"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary = m_Parser.addOption("Library", "Library instance guid, 16 hex digits", true, 1);
            m_hAsset   = m_Parser.addOption("Asset",   "Asset guid, 32 hex digits",             true, 1);
        }

        std::string Query() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg   = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            if (std::holds_alternative<xerr>(LibraryArg) || std::holds_alternative<xerr>(AssetArg)) return "DescribeAsset: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto AssetGuid   = ParseAssetGuid(std::get<std::string>(AssetArg));

            std::string Out;
            const bool bFound = e10::g_LibMgr.getNodeInfo(LibraryGuid, AssetGuid, [&](const e10::library_db::info_node& Node) // deliberately not noexcept - see ListAssets' own comment
            {
                Out += std::format("Name: {}\n", Node.m_Info.m_Name);
                Out += std::format("Type: {:016X}\n", AssetGuid.m_Type.m_Value);

                Out += std::format("Parents ({}):\n", Node.m_Info.m_RscLinks.size());
                for (auto& Parent : Node.m_Info.m_RscLinks)
                    Out += std::format("  {}\n", FormatAssetGuid(Parent));

                Out += std::format("Children ({}):\n", Node.m_lChildLinks.size());
                for (auto& Child : Node.m_lChildLinks)
                    Out += std::format("  {}\n", FormatAssetGuid(Child));
            });
            if (!bFound) return "DescribeAsset: asset not found";
            return Out;
        }

        xcmdline::parser::handle m_hLibrary, m_hAsset;
    };

    //================================================================================================
    // RenameAsset - Redo/Undo both call RenameDescriptor with the new/old name. BackupCurrenState
    // reads the CURRENT name via getInfo before Redo runs (same ordering set_property_cmd's own
    // comment already establishes: xundo::system::Execute always calls BackupCurrenState before
    // Redo). In-memory only until a real SaveAssets - matches RenameDescriptor's own behavior exactly.
    //================================================================================================
    struct rename_asset_cmd : xundo::command_base
    {
        rename_asset_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "RenameAsset", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Renames an asset (undoable). Usage: RenameAsset -Library hexguid -Asset assetguid -Name base64"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary = m_Parser.addOption("Library", "Library instance guid, 16 hex digits", true, 1);
            m_hAsset   = m_Parser.addOption("Asset",   "Asset guid, 32 hex digits",             true, 1);
            m_hName    = m_Parser.addOption("Name",    "New name, Base64-encoded",              true, 1);
        }

        std::string Redo() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg   = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            auto NameArg    = m_Parser.getOptionArgAs<std::string>(m_hName, 0);
            if (std::holds_alternative<xerr>(LibraryArg) || std::holds_alternative<xerr>(AssetArg) || std::holds_alternative<xerr>(NameArg))
                return "RenameAsset: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto AssetGuid   = ParseAssetGuid(std::get<std::string>(AssetArg));
            const auto Name        = Base64Decode(std::get<std::string>(NameArg));

            if (auto Err = e10::g_LibMgr.RenameDescriptor(LibraryGuid, AssetGuid, Name); Err)
                return std::format("RenameAsset: {}", Err.getMessage());
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg   = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);

            const std::uint64_t Library = std::holds_alternative<xerr>(LibraryArg) ? 0 : std::strtoull(std::get<std::string>(LibraryArg).c_str(), nullptr, 16);
            const std::string   Asset   = std::holds_alternative<xerr>(AssetArg) ? std::string(32, '0') : std::get<std::string>(AssetArg);
            File.Write(Library);
            WriteString(File, Asset);

            std::string OldName;
            if (!std::holds_alternative<xerr>(LibraryArg) && !std::holds_alternative<xerr>(AssetArg))
            {
                const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
                const auto AssetGuid   = ParseAssetGuid(std::get<std::string>(AssetArg));
                e10::g_LibMgr.getInfo(LibraryGuid, AssetGuid, [&](const xresource_pipeline::info& Info) { OldName = Info.m_Name; });
            }
            WriteString(File, OldName);
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Library = 0; File.Read(Library);
            const std::string Asset   = ReadString(File);
            const std::string OldName = ReadString(File);

            const auto LibraryGuid = ParseLibraryGuid(std::format("{:016X}", Library));
            const auto AssetGuid   = ParseAssetGuid(Asset);
            e10::g_LibMgr.RenameDescriptor(LibraryGuid, AssetGuid, OldName);
        }

        xcmdline::parser::handle m_hLibrary, m_hAsset, m_hName;
    };

    //================================================================================================
    // MoveAsset - thin wrap of MoveDescriptor, which already takes the CURRENT parent explicitly (the
    // caller must know it - no auto-derive-the-only-parent shortcut, matching MoveDescriptor's own
    // multi-parent-capable design). Undo is the exact inverse call, source/target simply swapped.
    //================================================================================================
    struct move_asset_cmd : xundo::command_base
    {
        move_asset_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "MoveAsset", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Moves an asset from one parent to another (undoable). Usage: MoveAsset -Library hexguid -Asset assetguid -OldParent assetguid -NewParent assetguid"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary   = m_Parser.addOption("Library",   "Library instance guid, 16 hex digits", true, 1);
            m_hAsset     = m_Parser.addOption("Asset",     "Asset guid, 32 hex digits",             true, 1);
            m_hOldParent = m_Parser.addOption("OldParent", "Current parent asset guid, 32 hex digits", true, 1);
            m_hNewParent = m_Parser.addOption("NewParent", "Target parent asset guid, 32 hex digits",  true, 1);
        }

        std::string Redo() noexcept override
        {
            auto LibraryArg   = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg     = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            auto OldParentArg = m_Parser.getOptionArgAs<std::string>(m_hOldParent, 0);
            auto NewParentArg = m_Parser.getOptionArgAs<std::string>(m_hNewParent, 0);
            if (std::holds_alternative<xerr>(LibraryArg) || std::holds_alternative<xerr>(AssetArg) || std::holds_alternative<xerr>(OldParentArg) || std::holds_alternative<xerr>(NewParentArg))
                return "MoveAsset: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto AssetGuid   = ParseAssetGuid(std::get<std::string>(AssetArg));
            const auto OldParent   = ParseAssetGuid(std::get<std::string>(OldParentArg));
            const auto NewParent   = ParseAssetGuid(std::get<std::string>(NewParentArg));

            if (auto Err = e10::g_LibMgr.MoveDescriptor(LibraryGuid, AssetGuid, OldParent, NewParent); Err)
                return std::format("MoveAsset: {}", Err.getMessage());
            return {};
        }

        // Nothing beyond the command's own args to snapshot - OldParent/NewParent are already fully
        // explicit in the command string itself (unlike MoveToFolder's own entity-side counterpart,
        // there's no implicit "wherever it currently is" to discover - the caller must always name
        // OldParent, matching MoveDescriptor's own required-argument shape).
        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto LibraryArg   = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg     = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            auto OldParentArg = m_Parser.getOptionArgAs<std::string>(m_hOldParent, 0);
            auto NewParentArg = m_Parser.getOptionArgAs<std::string>(m_hNewParent, 0);

            const std::uint64_t Library = std::holds_alternative<xerr>(LibraryArg) ? 0 : std::strtoull(std::get<std::string>(LibraryArg).c_str(), nullptr, 16);
            File.Write(Library);
            WriteString(File, std::holds_alternative<xerr>(AssetArg) ? std::string(32, '0') : std::get<std::string>(AssetArg));
            WriteString(File, std::holds_alternative<xerr>(OldParentArg) ? std::string(32, '0') : std::get<std::string>(OldParentArg));
            WriteString(File, std::holds_alternative<xerr>(NewParentArg) ? std::string(32, '0') : std::get<std::string>(NewParentArg));
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Library = 0; File.Read(Library);
            const std::string Asset     = ReadString(File);
            const std::string OldParent = ReadString(File);
            const std::string NewParent = ReadString(File);

            const auto LibraryGuid = ParseLibraryGuid(std::format("{:016X}", Library));
            e10::g_LibMgr.MoveDescriptor(LibraryGuid, ParseAssetGuid(Asset), ParseAssetGuid(NewParent), ParseAssetGuid(OldParent));
        }

        xcmdline::parser::handle m_hLibrary, m_hAsset, m_hOldParent, m_hNewParent;
    };

    //================================================================================================
    // DeleteAsset - Redo = MoveToTrash, Undo = MoveFromTrashTo back to the parent BackupCurrenState
    // captured (MoveFromTrashTo takes an explicit restore-to parent - it doesn't remember one on its
    // own). Fully reversible, matches DeleteEntity's own soft-delete-shaped UX - the asset's own
    // parent link(s) are preserved by MoveToTrash itself (confirmed reading its source), this just
    // records which one to hand back to MoveFromTrashTo.
    //================================================================================================
    struct delete_asset_cmd : xundo::command_base
    {
        delete_asset_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "DeleteAsset", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Moves an asset to the trash (undoable - restores it). Usage: DeleteAsset -Library hexguid -Asset assetguid"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary = m_Parser.addOption("Library", "Library instance guid, 16 hex digits", true, 1);
            m_hAsset   = m_Parser.addOption("Asset",   "Asset guid, 32 hex digits",             true, 1);
        }

        std::string Redo() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg   = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            if (std::holds_alternative<xerr>(LibraryArg) || std::holds_alternative<xerr>(AssetArg))
                return "DeleteAsset: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto AssetGuid   = ParseAssetGuid(std::get<std::string>(AssetArg));

            if (auto Err = e10::g_LibMgr.MoveToTrash(LibraryGuid, AssetGuid); !Err.empty())
                return std::format("DeleteAsset: {}", Err);
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg   = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);

            const std::uint64_t Library = std::holds_alternative<xerr>(LibraryArg) ? 0 : std::strtoull(std::get<std::string>(LibraryArg).c_str(), nullptr, 16);
            File.Write(Library);
            WriteString(File, std::holds_alternative<xerr>(AssetArg) ? std::string(32, '0') : std::get<std::string>(AssetArg));

            // The first parent link is what MoveToTrash preserves/restores against (confirmed reading
            // its own source - the trash tag is prepended, the real parent(s) stay in m_RscLinks).
            std::string OldParent(32, '0');
            if (!std::holds_alternative<xerr>(LibraryArg) && !std::holds_alternative<xerr>(AssetArg))
            {
                const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
                const auto AssetGuid   = ParseAssetGuid(std::get<std::string>(AssetArg));
                e10::g_LibMgr.getInfo(LibraryGuid, AssetGuid, [&](const xresource_pipeline::info& Info)
                {
                    if (!Info.m_RscLinks.empty()) OldParent = FormatAssetGuid(Info.m_RscLinks.front());
                });
            }
            WriteString(File, OldParent);
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Library = 0; File.Read(Library);
            const std::string Asset     = ReadString(File);
            const std::string OldParent = ReadString(File);

            const auto LibraryGuid = ParseLibraryGuid(std::format("{:016X}", Library));
            e10::g_LibMgr.MoveFromTrashTo(LibraryGuid, ParseAssetGuid(Asset), ParseAssetGuid(OldParent));
        }

        xcmdline::parser::handle m_hLibrary, m_hAsset;
    };

    //================================================================================================
    // RestoreAsset - the Asset Browser's own Trash context menu ("Restore > To Original Location" /
    // "To Root") is a genuine FORWARD action distinct from DeleteAsset's own Undo: it can restore to
    // a DIFFERENT parent than the one the asset was deleted from (e.g. "To Root"), not just reverse
    // the most recent delete. Redo = MoveFromTrashTo(Parent); Undo = MoveToTrash again - an exact
    // structural mirror of DeleteAsset, just with an explicit target parent instead of one captured
    // from history.
    //================================================================================================
    struct restore_asset_cmd : xundo::command_base
    {
        restore_asset_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "RestoreAsset", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Restores a trashed asset to a chosen parent (undoable). Usage: RestoreAsset -Library hexguid -Asset assetguid -Parent assetguid"; }
        void RegisterArguments() noexcept override
        {
            m_hLibrary = m_Parser.addOption("Library", "Library instance guid, 16 hex digits", true, 1);
            m_hAsset   = m_Parser.addOption("Asset",   "Asset guid, 32 hex digits",             true, 1);
            m_hParent  = m_Parser.addOption("Parent",  "Parent asset guid, 32 hex digits (0 = library root)", true, 1);
        }

        std::string Redo() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg   = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            auto ParentArg  = m_Parser.getOptionArgAs<std::string>(m_hParent, 0);
            if (std::holds_alternative<xerr>(LibraryArg) || std::holds_alternative<xerr>(AssetArg) || std::holds_alternative<xerr>(ParentArg))
                return "RestoreAsset: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto AssetGuid   = ParseAssetGuid(std::get<std::string>(AssetArg));
            const auto ParentGuid  = ParseAssetGuid(std::get<std::string>(ParentArg));

            if (auto Err = e10::g_LibMgr.MoveFromTrashTo(LibraryGuid, AssetGuid, ParentGuid); !Err.empty())
                return std::format("RestoreAsset: {}", Err);
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg   = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);

            const std::uint64_t Library = std::holds_alternative<xerr>(LibraryArg) ? 0 : std::strtoull(std::get<std::string>(LibraryArg).c_str(), nullptr, 16);
            File.Write(Library);
            WriteString(File, std::holds_alternative<xerr>(AssetArg) ? std::string(32, '0') : std::get<std::string>(AssetArg));
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Library = 0; File.Read(Library);
            const std::string Asset = ReadString(File);

            const auto LibraryGuid = ParseLibraryGuid(std::format("{:016X}", Library));
            e10::g_LibMgr.MoveToTrash(LibraryGuid, ParseAssetGuid(Asset));
        }

        xcmdline::parser::handle m_hLibrary, m_hAsset, m_hParent;
    };

    //================================================================================================
    // CreateAsset - Redo calls NewAsset with an EXPLICIT, caller-pre-minted instance guid (same
    // "-Id pre-minted by the caller" convention create_entity_cmd/instantiate_prefab_cmd already
    // established, needed so Redo stays deterministic/re-runnable across an Undo/Redo cycle).
    //
    // DOCUMENTED ASYMMETRY, not hidden: Undo = MoveToTrash the created asset. MoveToTrash/
    // MoveFromTrashTo is the ONLY reversal primitive this asset system has at all (DeleteDescriptor is
    // dead code) - Undo cannot make the info.txt file and its directory vanish from disk the way Redo
    // made them appear; it can only move the entry to Trash, exactly like every other "delete" in
    // this system already only ever means (nothing here does real permanent single-asset deletion
    // except EmptyTrashcan, which stays outside the undo system entirely - see this file's own top
    // comment).
    //
    // Redo's own re-run guard is more careful than it first looks - confirmed the hard way, live:
    // NewAsset writes info.txt to disk IMMEDIATELY and unconditionally (unlike Rename/Move/Delete,
    // in-memory only until a real Save), so a re-Redo after Undo needs to distinguish "already exists
    // AND is currently trashed" (call MoveFromTrashTo to restore it) from "already exists but is NOT
    // trashed" (an asset created by an earlier process/session that outlived this one, or whose
    // trashing was itself only ever in-memory and never saved - nothing left to do). Calling
    // MoveFromTrashTo on a non-trashed node hits its own `m_RscLinks[0] == trash_guid_v` assert.
    //================================================================================================
    struct create_asset_cmd : xundo::command_base
    {
        create_asset_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "CreateAsset", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override
        {
            return "Creates a new asset (undoable - Undo moves it to the trash; the on-disk info.txt this writes is NOT deleted, only MoveToTrash/EmptyTrashcan can do that - see this command's own comment). Usage: CreateAsset -Library hexguid -Type hexguid -Asset assetguid -Parent assetguid -Name base64";
        }
        void RegisterArguments() noexcept override
        {
            m_hLibrary = m_Parser.addOption("Library", "Library instance guid, 16 hex digits",                          true, 1);
            m_hType    = m_Parser.addOption("Type",    "Asset type guid, 16 hex digits",                                true, 1);
            m_hAsset   = m_Parser.addOption("Asset",   "New asset's guid, 32 hex digits, pre-minted by the caller",     true, 1);
            m_hParent  = m_Parser.addOption("Parent",  "Parent asset guid, 32 hex digits",                              true, 1);
            m_hName    = m_Parser.addOption("Name",    "Asset name, Base64-encoded",                                    true, 1);
        }

        std::string Redo() noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto TypeArg    = m_Parser.getOptionArgAs<std::string>(m_hType, 0);
            auto AssetArg   = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);
            auto ParentArg  = m_Parser.getOptionArgAs<std::string>(m_hParent, 0);
            auto NameArg    = m_Parser.getOptionArgAs<std::string>(m_hName, 0);
            if (std::holds_alternative<xerr>(LibraryArg) || std::holds_alternative<xerr>(TypeArg) || std::holds_alternative<xerr>(AssetArg) || std::holds_alternative<xerr>(ParentArg) || std::holds_alternative<xerr>(NameArg))
                return "CreateAsset: bad arguments";

            const auto LibraryGuid = ParseLibraryGuid(std::get<std::string>(LibraryArg));
            const auto AssetGuid   = ParseAssetGuid(std::get<std::string>(AssetArg));
            const auto ParentGuid  = ParseAssetGuid(std::get<std::string>(ParentArg));
            const auto Name        = Base64Decode(std::get<std::string>(NameArg));

            if (AssetGuid.m_Instance.empty() || AssetGuid.m_Type.empty()) return "CreateAsset: bad asset guid";

            // A re-Redo (after a prior Undo trashed this exact guid, in the SAME undo history) must
            // NOT call NewAsset again - confirmed live: NewAsset's own Insert-into-index call is
            // insert-only, it silently does nothing when the key already exists, so a second NewAsset
            // call leaves the still-trashed node exactly as it was. Detect "already exists" via
            // getInfo, exactly like DeleteAsset's own Undo does - but "exists" alone isn't enough to
            // know it's actually trashed: NewAsset writes its info.txt to disk IMMEDIATELY and
            // unconditionally (unlike Rename/Move/Delete, which stay in-memory only until a real
            // Save), so an asset created in an EARLIER process/session can be found "already existing"
            // here without ever having been trashed by THIS session's own Undo - confirmed live, the
            // hard way: calling MoveFromTrashTo on a non-trashed node hits its own
            // `m_RscLinks[0] == trash_guid_v` assert. Only call MoveFromTrashTo when the existing
            // node's own front RscLink genuinely IS the trash tag; otherwise this Redo has nothing
            // left to do (the asset already exists, in the right parent, from an earlier run).
            bool bAlreadyExists = false;
            bool bCurrentlyTrashed = false;
            e10::g_LibMgr.getInfo(LibraryGuid, AssetGuid, [&](const xresource_pipeline::info& Info)
            {
                bAlreadyExists = true;
                bCurrentlyTrashed = !Info.m_RscLinks.empty() && Info.m_RscLinks.front() == e10::folder::trash_guid_v;
            });

            if (bAlreadyExists && bCurrentlyTrashed)
                e10::g_LibMgr.MoveFromTrashTo(LibraryGuid, AssetGuid, ParentGuid);
            else if (!bAlreadyExists)
                e10::g_LibMgr.NewAsset(LibraryGuid, AssetGuid, ParentGuid, Name);
            return {};
        }

        void BackupCurrenState(xundo::undo_file& File) noexcept override
        {
            auto LibraryArg = m_Parser.getOptionArgAs<std::string>(m_hLibrary, 0);
            auto AssetArg   = m_Parser.getOptionArgAs<std::string>(m_hAsset, 0);

            const std::uint64_t Library = std::holds_alternative<xerr>(LibraryArg) ? 0 : std::strtoull(std::get<std::string>(LibraryArg).c_str(), nullptr, 16);
            File.Write(Library);
            WriteString(File, std::holds_alternative<xerr>(AssetArg) ? std::string(32, '0') : std::get<std::string>(AssetArg));
        }

        void Undo(xundo::undo_file& File) noexcept override
        {
            std::uint64_t Library = 0; File.Read(Library);
            const std::string Asset = ReadString(File);

            const auto LibraryGuid = ParseLibraryGuid(std::format("{:016X}", Library));
            e10::g_LibMgr.MoveToTrash(LibraryGuid, ParseAssetGuid(Asset));
        }

        xcmdline::parser::handle m_hLibrary, m_hType, m_hAsset, m_hParent, m_hName;
    };

    //================================================================================================
    // SaveAssets - wraps library_mgr::Save(Context), flushing every dirty descriptor across every
    // open library to disk (the function itself takes no library guid - it walks m_mLibraryDB
    // wholesale). Mirrors E29's own existing Save command's exact reasoning
    // (E29_Commands_Workspace.h) - RenameAsset/MoveAsset/DeleteAsset are all in-memory-only until
    // this runs.
    //================================================================================================
    struct save_assets_query_cmd : xundo::query_command_base
    {
        save_assets_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "SaveAssets", pDataBase) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "Flushes every changed asset descriptor, in every open library, to disk. Usage: SaveAssets"; }
        void RegisterArguments() noexcept override {}

        std::string Query() noexcept override
        {
            xproperty::settings::context Context;
            e10::g_LibMgr.Save(Context);
            return "Saved";
        }
    };
}

#endif // E29_COMMANDS_ASSET_BROWSER_H
