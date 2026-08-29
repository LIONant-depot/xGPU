#pragma once
// Command-string builders (the first, already-inline "namespace commands" block), extracted from the
// monolithic E27_NodeOS_Editor.cpp (header #6).
#include "NodeOS_Common.h"
#include "NodeOS_Types.h"

namespace nodeos
{
    //================================================================================================
    // Commands - pure helpers (command-string building + xundo::system::Execute dispatch), split out
    // here (ahead of DrawGraphCanvas/DrawNodePropertiesPanel, which call them) from the actual
    // xundo::command_base-derived classes further down this file (which need
    // SerializePropertiesToString/ApplyPropertiesFromString, not defined until later) - see that
    // later "namespace commands" block's own comment for the full explanation. Every graph mutation
    // becomes a string command executed through System.Execute(), which has zero ImGui/xgpu
    // dependency: this is the same entry point a future headless runner or "command source" driver
    // plugin would call.
    //================================================================================================
    namespace commands
    {
        // Base64 - used ONLY for SetProperties' Before/After payloads (arbitrary property text that
        // could contain characters awkward for a space/tab-delimited command line, e.g. a file path).
        // Every other command's arguments are plain ids/csv-of-ids, which need no encoding at all.
        inline std::string Base64Encode(const std::string& In)
        {
            static constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string Out;
            Out.reserve(((In.size() + 2) / 3) * 4);
            std::size_t i = 0;
            for (; i + 2 < In.size(); i += 3)
            {
                const std::uint32_t N = (std::uint32_t(std::uint8_t(In[i])) << 16) | (std::uint32_t(std::uint8_t(In[i + 1])) << 8) | std::uint8_t(In[i + 2]);
                Out += Alphabet[(N >> 18) & 0x3F]; Out += Alphabet[(N >> 12) & 0x3F];
                Out += Alphabet[(N >> 6) & 0x3F];  Out += Alphabet[N & 0x3F];
            }
            const std::size_t Rem = In.size() - i;
            if (Rem == 1)
            {
                const std::uint32_t N = std::uint32_t(std::uint8_t(In[i])) << 16;
                Out += Alphabet[(N >> 18) & 0x3F]; Out += Alphabet[(N >> 12) & 0x3F]; Out += "==";
            }
            else if (Rem == 2)
            {
                const std::uint32_t N = (std::uint32_t(std::uint8_t(In[i])) << 16) | (std::uint32_t(std::uint8_t(In[i + 1])) << 8);
                Out += Alphabet[(N >> 18) & 0x3F]; Out += Alphabet[(N >> 12) & 0x3F]; Out += Alphabet[(N >> 6) & 0x3F]; Out += '=';
            }
            return Out;
        }

        inline std::string Base64Decode(const std::string& In)
        {
            auto DecodeChar = [](char C) -> int
            {
                if (C >= 'A' && C <= 'Z') return C - 'A';
                if (C >= 'a' && C <= 'z') return C - 'a' + 26;
                if (C >= '0' && C <= '9') return C - '0' + 52;
                if (C == '+') return 62;
                if (C == '/') return 63;
                return -1; // padding ('=') or terminator
            };
            std::string Out;
            Out.reserve((In.size() / 4) * 3);
            int Bits = 0, NumBits = 0;
            for (char C : In)
            {
                const int V = DecodeChar(C);
                if (V < 0) break;
                Bits = (Bits << 6) | V;
                NumBits += 6;
                if (NumBits >= 8)
                {
                    NumBits -= 8;
                    Out += static_cast<char>((Bits >> NumBits) & 0xFF);
                }
            }
            return Out;
        }

        // Node/link ids are guids (xresource::guid_generator::Instance64()), not small counting
        // numbers, so they're always WRITTEN as hex (0x-prefixed, matching this engine's own
        // Plugin.config convention for a u64 guid field - e.g. "#8B3C028882EA813D") rather than a huge,
        // unreadable decimal string. Parsing accepts EITHER form, in case a hand-typed or
        // agent-generated command uses plain decimal instead.
        inline std::string FormatGuid(std::uint64_t Id) { return std::format("0x{:016x}", Id); }
        inline std::uint64_t ParseGuid(const std::string& S)
        {
            if (S.size() > 2 && S[0] == '0' && (S[1] == 'x' || S[1] == 'X'))
                return std::stoull(S.substr(2), nullptr, 16);
            return std::stoull(S, nullptr, 10);
        }

        // Comma-separated guid lists - used by DeleteNodes/ReorderNodes/Select for "a set of node ids"
        // without needing a separate -flag per id. xcmdline requires a required option to have at
        // least 1 argument, so an empty set is encoded as the literal "-" rather than an empty string.
        inline std::string JoinIds(const std::vector<std::uint64_t>& Ids)
        {
            std::string Out;
            for (std::size_t i = 0; i < Ids.size(); ++i) { if (i) Out += ','; Out += FormatGuid(Ids[i]); }
            return Out.empty() ? std::string("-") : Out;
        }
        inline std::vector<std::uint64_t> SplitIds(const std::string& Csv)
        {
            std::vector<std::uint64_t> Out;
            if (Csv.empty() || Csv == "-") return Out;
            std::size_t Pos = 0;
            while (Pos < Csv.size())
            {
                const std::size_t Comma = Csv.find(',', Pos);
                Out.push_back(ParseGuid(Csv.substr(Pos, Comma == std::string::npos ? std::string::npos : Comma - Pos)));
                Pos = (Comma == std::string::npos) ? Csv.size() : Comma + 1;
            }
            return Out;
        }


        // Plugins are identified by their Plugins/<DirName>/ folder name, not by an absolute source
        // path - a folder name is guaranteed unique within this scan (see plugin_source_entry's own
        // comment) and stays meaningful across machines/checkouts, in a saved graph file, or in a
        // command an AI agent constructs without knowing this machine's absolute path layout.
        inline plugin_source_entry* FindSourceByDirName(std::vector<plugin_source_entry>& Sources, const std::string& DirName)
        {
            for (auto& S : Sources) if (S.m_DirName == DirName) return &S;
            return nullptr;
        }

        // Shared by create_node_cmd's own -After/-Before resolution and select_cmd's -MarkerAfter/
        // -MarkerBefore resolution (and now create_spine_cmd's -AnchorNode) - all three need the exact
        // same "which spine, and which dense order within it, does this already-known node id sit at"
        // lookup against the current node list.
        inline bool ResolveNodeSpineAndOrder(const std::vector<node_instance>& Nodes, std::uint64_t NodeId, std::uint64_t& OutSpineId, int& OutOrder) noexcept
        {
            for (auto& N : Nodes)
                if (N.m_Id == NodeId) { OutSpineId = N.m_SpineId; OutOrder = N.m_Order; return true; }
            return false;
        }

        // A control node and its owned End/End-Else marker are never independently deletable - the
        // pair is one unit (NODE_SCRIPTING_DESIGN.md section 4.1). Deleting either one pulls the other
        // in too. Used by DeleteNodes' Redo AND BackupCurrenState, so Undo restores exactly what was
        // actually removed, including anything cascade-added.
        inline std::vector<std::uint64_t> ExpandOwnershipCascade(const std::vector<node_instance>& Nodes, std::vector<std::uint64_t> Ids) noexcept
        {
            bool bChanged = true;
            while (bChanged)
            {
                bChanged = false;
                for (auto& N : Nodes)
                {
                    const bool bOwnerInSet = std::find(Ids.begin(), Ids.end(), N.m_Id) != Ids.end();
                    if (bOwnerInSet && N.m_OwnedEndId != 0 && std::find(Ids.begin(), Ids.end(), N.m_OwnedEndId) == Ids.end())
                    {
                        Ids.push_back(N.m_OwnedEndId);
                        bChanged = true;
                    }
                    const bool bMarkerInSet = N.m_OwnedEndId != 0 && std::find(Ids.begin(), Ids.end(), N.m_OwnedEndId) != Ids.end();
                    if (bMarkerInSet && !bOwnerInSet)
                    {
                        Ids.push_back(N.m_Id);
                        bChanged = true;
                    }
                }
            }
            return Ids;
        }

        inline void WriteString(xundo::undo_file& File, const std::string& S)
        {
            const std::uint32_t Len = static_cast<std::uint32_t>(S.size());
            File.Write(Len);
            if (Len) File.Write(S.data(), Len);
        }
        inline std::string ReadString(xundo::undo_file& File)
        {
            std::uint32_t Len = 0; File.Read(Len);
            std::string S; S.resize(Len);
            if (Len) File.Read(S.data(), Len);
            return S;
        }

        // Command-string builders - one per registered command name (see the xundo::command_base
        // classes further down this file). Pure string formatting, no xundo dependency at all.
        // CreateNode is addressed relative to an EXISTING node's id (-After/-Before), not a raw order
        // index or an invented "gap" identity - the id is something a caller (human or agent) already
        // has from having just created or observed that node, so no separate discovery/query step is
        // ever needed to place a new node relative to one that's already there. Neither flag given
        // means "append at the end" (or "the only node", if the graph is empty).
        // TypeName defaults to empty ("-TypeName" omitted entirely), preserving the exact old command
        // string for the common single-type-per-plugin case - only a multi-type plugin (see
        // xnode_os_plugin_api.h's NodeOS_CreateFactories) needs it, to say WHICH of its registered
        // types this particular node is, since -PluginDir alone is now ambiguous between them.
        inline std::string MakeCreateNodeAppend(std::uint64_t Id, const std::string& PluginDir, const std::string& TypeName = {})
        {
            return std::format("CreateNode -Id {} -PluginDir {}", FormatGuid(Id), PluginDir) + (TypeName.empty() ? "" : std::format(" -TypeName {}", TypeName));
        }
        inline std::string MakeCreateNodeAfter(std::uint64_t Id, const std::string& PluginDir, std::uint64_t AfterNodeId, const std::string& TypeName = {})
        {
            return std::format("CreateNode -Id {} -PluginDir {} -After {}", FormatGuid(Id), PluginDir, FormatGuid(AfterNodeId)) + (TypeName.empty() ? "" : std::format(" -TypeName {}", TypeName));
        }
        inline std::string MakeCreateNodeBefore(std::uint64_t Id, const std::string& PluginDir, std::uint64_t BeforeNodeId, const std::string& TypeName = {})
        {
            return std::format("CreateNode -Id {} -PluginDir {} -Before {}", FormatGuid(Id), PluginDir, FormatGuid(BeforeNodeId)) + (TypeName.empty() ? "" : std::format(" -TypeName {}", TypeName));
        }
        // Same four placements as CreateNode, for a control node (If/ForEachLoop) that owns a
        // paired End/End-Else marker (NODE_SCRIPTING_DESIGN.md section 4.1) - the marker is always
        // created right after the owner, in the same spine, in the same command, along with the
        // read-only link between them (LinkId is caller-minted, same "Redo never invents an id" rule
        // as everything else - see connect_cmd's own MakeConnect for the existing precedent). This
        // link is never something the user drags into existence - the system wires it automatically,
        // and it's rendered read-only (Connect/DeleteLink both refuse to touch it) specifically
        // because it isn't an ordinary, user-editable connection.
        // Optional 2nd hop (Owner -> Mid -> End2) - see create_owned_pair_cmd's own comment. Nothing
        // uses this today (Function used to, before it merged its owned marker into itself), kept as
        // generic plumbing for a future owner type that genuinely needs a 2-level marker chain.
        // End2Id == 0 means "no second hop", the common (currently the only) case.
        struct owned_pair_2nd_hop { std::uint64_t m_End2Id = 0; std::string m_End2PluginDir; std::uint64_t m_Link2Id = 0; };
        inline std::string Opt2ndHopSuffix(const owned_pair_2nd_hop& Hop2)
        {
            if (Hop2.m_End2Id == 0) return {};
            return std::format(" -End2Id {} -End2PluginDir {} -Link2Id {}", FormatGuid(Hop2.m_End2Id), Hop2.m_End2PluginDir, FormatGuid(Hop2.m_Link2Id));
        }
        inline std::string MakeCreateOwnedPairAppend(std::uint64_t Id, const std::string& PluginDir, std::uint64_t EndId, const std::string& EndPluginDir, std::uint64_t LinkId, const owned_pair_2nd_hop& Hop2 = {})
        {
            return std::format("CreateOwnedPair -Id {} -PluginDir {} -EndId {} -EndPluginDir {} -LinkId {}", FormatGuid(Id), PluginDir, FormatGuid(EndId), EndPluginDir, FormatGuid(LinkId)) + Opt2ndHopSuffix(Hop2);
        }
        inline std::string MakeCreateOwnedPairAfter(std::uint64_t Id, const std::string& PluginDir, std::uint64_t EndId, const std::string& EndPluginDir, std::uint64_t LinkId, std::uint64_t AfterNodeId, const owned_pair_2nd_hop& Hop2 = {})
        {
            return std::format("CreateOwnedPair -Id {} -PluginDir {} -EndId {} -EndPluginDir {} -LinkId {} -After {}", FormatGuid(Id), PluginDir, FormatGuid(EndId), EndPluginDir, FormatGuid(LinkId), FormatGuid(AfterNodeId)) + Opt2ndHopSuffix(Hop2);
        }
        inline std::string MakeCreateOwnedPairBefore(std::uint64_t Id, const std::string& PluginDir, std::uint64_t EndId, const std::string& EndPluginDir, std::uint64_t LinkId, std::uint64_t BeforeNodeId, const owned_pair_2nd_hop& Hop2 = {})
        {
            return std::format("CreateOwnedPair -Id {} -PluginDir {} -EndId {} -EndPluginDir {} -LinkId {} -Before {}", FormatGuid(Id), PluginDir, FormatGuid(EndId), EndPluginDir, FormatGuid(LinkId), FormatGuid(BeforeNodeId)) + Opt2ndHopSuffix(Hop2);
        }
        inline std::string MakeCreateOwnedPairInSpine(std::uint64_t Id, const std::string& PluginDir, std::uint64_t EndId, const std::string& EndPluginDir, std::uint64_t LinkId, std::uint64_t SpineId, const owned_pair_2nd_hop& Hop2 = {})
        {
            return std::format("CreateOwnedPair -Id {} -PluginDir {} -EndId {} -EndPluginDir {} -LinkId {} -InSpine {}", FormatGuid(Id), PluginDir, FormatGuid(EndId), EndPluginDir, FormatGuid(LinkId), FormatGuid(SpineId)) + Opt2ndHopSuffix(Hop2);
        }

        // -InSpine is the only way to place a node into a currently-empty spine - there's no existing
        // node in it yet to address -After/-Before relative to.
        inline std::string MakeCreateNodeInSpine(std::uint64_t Id, const std::string& PluginDir, std::uint64_t SpineId, const std::string& TypeName = {})
        {
            return std::format("CreateNode -Id {} -PluginDir {} -InSpine {}", FormatGuid(Id), PluginDir, FormatGuid(SpineId)) + (TypeName.empty() ? "" : std::format(" -TypeName {}", TypeName));
        }

        enum class node_placement_kind { Append, After, Before, InSpine };

        // Every "add a node" UI path funnels through here to decide plain CreateNode vs.
        // CreateOwnedPair - the one place that reads xnode_os_node_factory::needsOwnedEndMarker(), so
        // no call site has to know "If"/"ForEachLoop" by name. RefId is the -After/-Before node id or
        // the -InSpine spine id; ignored for Append.
        inline std::string BuildCreateNodeCommand
        (
            std::vector<plugin_source_entry>& Sources
        ,   std::vector<available_node_type>& AvailableTypes
        ,   plugin_source_entry&               OwnerSrc
        ,   xnode_os_node_factory*              pOwnerType
        ,   node_placement_kind                 Kind
        ,   std::uint64_t                        RefId
        ) noexcept
        {
            const auto NewId = xresource::guid_generator::Instance64();
            if (pOwnerType->needsOwnedEndMarker())
            {
                const std::string EndDir(pOwnerType->getOwnedEndMarkerPluginDir());
                auto* pEndSrc = FindSourceByDirName(Sources, EndDir);
                if (pEndSrc)
                {
                    auto* pEndType = EnsureLoadedAndGetType(*pEndSrc, AvailableTypes);
                    if (pEndType)
                    {
                        const auto EndId  = xresource::guid_generator::Instance64();
                        const auto LinkId = xresource::guid_generator::Instance64();

                        // Discover an optional 2nd hop the same generic way the 1st hop was found -
                        // no hardcoded node-name check here (see create_owned_pair_cmd's own comment
                        // for why this stays a 2-hop special case rather than a general N-way chain;
                        // nothing currently needs it, Function included, now that its owned marker is
                        // merged into itself).
                        owned_pair_2nd_hop Hop2;
                        if (pEndType->needsOwnedEndMarker())
                        {
                            const std::string End2Dir(pEndType->getOwnedEndMarkerPluginDir());
                            auto* pEnd2Src = FindSourceByDirName(Sources, End2Dir);
                            if (pEnd2Src && EnsureLoadedAndGetType(*pEnd2Src, AvailableTypes))
                            {
                                Hop2.m_End2Id        = xresource::guid_generator::Instance64();
                                Hop2.m_End2PluginDir = End2Dir;
                                Hop2.m_Link2Id       = xresource::guid_generator::Instance64();
                            }
                            // Second-level plugin missing/failed to compile - fall through with
                            // Hop2 empty rather than failing the whole placement; the user still gets
                            // the owner + its immediate marker, same "never do nothing" policy as the
                            // 1st-hop fallback below.
                        }

                        switch (Kind)
                        {
                        case node_placement_kind::Append:  return MakeCreateOwnedPairAppend (NewId, OwnerSrc.m_DirName, EndId, EndDir, LinkId, Hop2);
                        case node_placement_kind::After:   return MakeCreateOwnedPairAfter  (NewId, OwnerSrc.m_DirName, EndId, EndDir, LinkId, RefId, Hop2);
                        case node_placement_kind::Before:  return MakeCreateOwnedPairBefore (NewId, OwnerSrc.m_DirName, EndId, EndDir, LinkId, RefId, Hop2);
                        case node_placement_kind::InSpine: return MakeCreateOwnedPairInSpine(NewId, OwnerSrc.m_DirName, EndId, EndDir, LinkId, RefId, Hop2);
                        }
                    }
                }
                // Marker plugin missing/failed to compile - fall through to a plain create rather than
                // silently doing nothing, so the user sees the node and a compile error in the log
                // instead of the "+" click appearing to do nothing at all.
            }
            // Always passed through (not just when OwnerSrc has multiple types) - harmless for the
            // common single-type case (create_node_cmd's own -TypeName lookup finds the same one
            // type either way) and means the persisted command is unambiguous regardless of whether
            // this source ever gains a second type later.
            const std::string TypeName(pOwnerType->getName());
            switch (Kind)
            {
            case node_placement_kind::Append:  return MakeCreateNodeAppend (NewId, OwnerSrc.m_DirName, TypeName);
            case node_placement_kind::After:   return MakeCreateNodeAfter  (NewId, OwnerSrc.m_DirName, RefId, TypeName);
            case node_placement_kind::Before:  return MakeCreateNodeBefore (NewId, OwnerSrc.m_DirName, RefId, TypeName);
            case node_placement_kind::InSpine: return MakeCreateNodeInSpine(NewId, OwnerSrc.m_DirName, RefId, TypeName);
            }
            return {};
        }

        inline std::string MakeDeleteNodes(const std::vector<std::uint64_t>& Ids) { return std::format("DeleteNodes -Ids {}", JoinIds(Ids)); }
        inline std::string MakeDeleteLink(std::uint64_t Id) { return std::format("DeleteLink -Id {}", FormatGuid(Id)); }
        // Guid-only now - no index arguments at all (see link_instance's own comment: every pin on
        // every node carries a real per-instance guid, mirroring the material graph's connection
        // design). Both guids are required, not optional - there is no index left to fall back to.
        inline std::string MakeConnect(std::uint64_t Id, std::uint64_t SourceNode, std::uint64_t TargetNode, std::uint64_t SourceOutputGuid, std::uint64_t TargetInputGuid)
        {
            return std::format("Connect -Id {} -SourceNode {} -TargetNode {} -SourceOutputGuid {} -TargetInputGuid {}"
                               , FormatGuid(Id), FormatGuid(SourceNode), FormatGuid(TargetNode), FormatGuid(SourceOutputGuid), FormatGuid(TargetInputGuid));
        }
        inline std::string MakeReorderNodes(const std::vector<std::uint64_t>& NewOrder) { return std::format("ReorderNodes -Ids {}", JoinIds(NewOrder)); }
        // Moves node(s) into a DIFFERENT spine at a given position - addressed the same way CreateNode
        // addresses insertion (-After/-Before an existing node, or -InSpine to append to a spine
        // regardless of its current size).
        inline std::string MakeMoveNodesToSpineAfter(const std::vector<std::uint64_t>& Ids, std::uint64_t AfterNodeId)
        {
            return std::format("MoveNodesToSpine -Ids {} -After {}", JoinIds(Ids), FormatGuid(AfterNodeId));
        }
        inline std::string MakeMoveNodesToSpineBefore(const std::vector<std::uint64_t>& Ids, std::uint64_t BeforeNodeId)
        {
            return std::format("MoveNodesToSpine -Ids {} -Before {}", JoinIds(Ids), FormatGuid(BeforeNodeId));
        }
        inline std::string MakeMoveNodesToSpineIn(const std::vector<std::uint64_t>& Ids, std::uint64_t SpineId)
        {
            return std::format("MoveNodesToSpine -Ids {} -InSpine {}", JoinIds(Ids), FormatGuid(SpineId));
        }
        inline std::string MakeSetProperties(std::uint64_t NodeId, const std::string& Before, const std::string& After)
        {
            return std::format("SetProperties -NodeId {} -Before {} -After {}", FormatGuid(NodeId), Base64Encode(Before), Base64Encode(After));
        }
        // Disabling (-Enable 0) needs none of EndId/EndPluginDir/LinkId - use MakeSetEndElseDisable.
        inline std::string MakeSetEndElseEnable(std::uint64_t OwnerId, std::uint64_t EndId, const std::string& EndPluginDir, std::uint64_t LinkId)
        {
            return std::format("SetEndElseState -OwnerId {} -Enable 1 -EndId {} -EndPluginDir {} -LinkId {}", FormatGuid(OwnerId), FormatGuid(EndId), EndPluginDir, FormatGuid(LinkId));
        }
        inline std::string MakeSetEndElseDisable(std::uint64_t OwnerId)
        {
            return std::format("SetEndElseState -OwnerId {} -Enable 0", FormatGuid(OwnerId));
        }
        // Every field is OPTIONAL and simply omitted when not selected - "nothing selected" is the bare
        // command "Select" with no flags at all, not a "-Nodes -" placeholder or a "-Link 0x000...0"
        // sentinel next to a real-looking id that could be mistaken for an actual (nonexistent) link.
        // The insert-marker selection is addressed the same way CreateNode addresses insertion -
        // relative to an existing node (-MarkerAfter/-MarkerBefore <nodeid>), not a raw, shifting index.
        inline std::string MakeSelectNodes(const std::vector<std::uint64_t>& Nodes)
        {
            return Nodes.empty() ? std::string("Select") : std::format("Select -Nodes {}", JoinIds(Nodes));
        }
        inline std::string MakeSelectLink(std::uint64_t Link) { return std::format("Select -Link {}", FormatGuid(Link)); }
        inline std::string MakeSelectMarkerAfter(std::uint64_t NodeId)  { return std::format("Select -MarkerAfter {}",  FormatGuid(NodeId)); }
        inline std::string MakeSelectMarkerBefore(std::uint64_t NodeId) { return std::format("Select -MarkerBefore {}", FormatGuid(NodeId)); }
        inline std::string MakeSelectMarkerSpine(std::uint64_t SpineId) { return std::format("Select -MarkerSpine {}", FormatGuid(SpineId)); }
        inline std::string MakeClearSelection() { return "ClearSelection"; }

        // CreateSpine - places a new, empty spine directly at an absolute -Y. -Column/-NewColumn fold
        // "attach to an existing column" vs. "synthesize a new one" into one command, same pattern
        // Select already uses for its own several mutually exclusive concerns. -NewColumnId is minted
        // by the CALLER (never inside Redo()), matching this codebase's standing rule for every id this
        // command system ever creates. -NewColumn carries a dummy "1" argument rather than appearing
        // bare - xcmdline::parser::hasOption() only reports an option present if it actually collected
        // an argument, so a bare flag immediately followed by another flag (nothing non-flag trailing
        // it) leaves its own args empty and fails minArgs, erroring the WHOLE command before Redo()
        // ever runs (the exact bug already hit and fixed once for -AnchorAfter/-AnchorBefore).
        inline std::string MakeCreateSpineNewColumn(std::uint64_t SpineId, float Y, std::uint64_t NeighborColumnId, char Side, std::uint64_t NewColumnId)
        {
            return std::format("CreateSpine -Id {} -Y {:.3f} -NewColumn 1 -NewColumnId {} -NeighborColumn {} -Side {}"
                               , FormatGuid(SpineId), Y, FormatGuid(NewColumnId), FormatGuid(NeighborColumnId), Side);
        }
        inline std::string MakeCreateSpineExistingColumn(std::uint64_t SpineId, float Y, std::uint64_t ColumnId)
        {
            return std::format("CreateSpine -Id {} -Y {:.3f} -Column {}", FormatGuid(SpineId), Y, FormatGuid(ColumnId));
        }
        inline std::string MakeDeleteSpine(std::uint64_t SpineId) { return std::format("DeleteSpine -Id {}", FormatGuid(SpineId)); }

        // SetSpinePosition - sets a spine's absolute (Y, Column) directly - drag it anywhere within
        // its own column (any pixel), or straight into a different already-existing column.
        inline std::string MakeSetSpinePosition(std::uint64_t SpineId, float Y, std::uint64_t ColumnId)
        {
            return std::format("SetSpinePosition -Id {} -Y {:.3f} -Column {}", FormatGuid(SpineId), Y, FormatGuid(ColumnId));
        }
        // Same, but the destination column doesn't exist yet - splice a new one in beside -NeighborColumn
        // first. Mirrors MakeCreateSpineNewColumn's own dummy "1" on -NewColumn for the same xcmdline
        // bare-flag reason.
        inline std::string MakeSetSpinePositionNewColumn(std::uint64_t SpineId, float Y, std::uint64_t NeighborColumnId, char Side, std::uint64_t NewColumnId)
        {
            return std::format("SetSpinePosition -Id {} -Y {:.3f} -NewColumn 1 -NewColumnId {} -NeighborColumn {} -Side {}"
                               , FormatGuid(SpineId), Y, FormatGuid(NewColumnId), FormatGuid(NeighborColumnId), Side);
        }

        // Wraps system::Execute with logging - EVERY command, not just failures, so nodeos_debug.log
        // is a genuine audit trail of everything that happened to the graph (the same log an AI agent
        // driving this through a future "command source" plugin would read).
        inline void Run(xundo::system& System, const std::string& Cmd)
        {
            if (auto Err = System.Execute(Cmd); !Err.empty())
                Debugger(std::format("Node OS: command failed: '{}' ({})", Cmd, Err));
            else
                Debugger(std::format("Node OS: {}", Cmd));
        }
    }
}
