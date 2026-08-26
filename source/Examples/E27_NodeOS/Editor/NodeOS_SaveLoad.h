#pragma once
// Whole-graph Save/Load, extracted from the monolithic E27_NodeOS_Editor.cpp (header #3). Needs
// NodeOS_PropertySerialize.h for HasAnyProperties/SerializeReflectedMembers.
#include "NodeOS_Common.h"
#include "NodeOS_Types.h"
#include "NodeOS_PropertySerialize.h"

namespace nodeos
{
    // Generic "self-serializing list" writer/reader, shared by Links/Columns/Spines (node_instance
    // can't use this directly - it needs the topology/plugin-properties split, see node_topology and
    // SaveGraph/LoadGraph's own per-node loops). Writes/reads one reflected xProperties record per
    // item via T's own XPROPERTY_DEF (link_instance/column/spine) - the item count itself lives in the
    // single combined "Header" record (see SaveGraph's own top comment), not here.
    template< typename T >
    static xerr SaveGraphItems(xtextfile::stream& Stream, const std::vector<T>& Items)
    {
        for (auto& Item : Items)
        {
            xproperty::settings::context Context;
            if (auto Err = xproperty::sprop::serializer::Stream(Stream, const_cast<T&>(Item), Context); Err)
                return Err;
        }
        return {};
    }

    // Mirrors SaveGraphItems - Count comes from the file's own Header record, read once up front.
    template< typename T >
    static xerr LoadGraphItems(xtextfile::stream& Stream, std::int32_t Count, std::vector<T>& Items)
    {
        Items.clear();
        Items.reserve(static_cast<std::size_t>(Count));
        for (std::int32_t i = 0; i < Count; ++i)
        {
            T Item{};
            xproperty::settings::context Context;
            if (auto Err = xproperty::sprop::serializer::Stream(Stream, Item, Context); Err)
                return Err;
            Items.push_back(std::move(Item));
        }
        return {};
    }

    //------------------------------------------------------------------------------------------------
    // Save/load the whole graph (nodes + their properties + links) as a plain xtextfile - the same
    // text-file convention every other engine tool uses.
    //
    // File shape - a single leading graph_header's own "xProperties" record (one plain row, no per-
    // node/link/column/spine bookkeeping in it) states how many of each follow - same official
    // xproperty::sprop::serializer::Stream() every other record below uses, so the whole file goes
    // through one serialization path, not "every record except the header":
    //   [ graph_header's own "xProperties" record ]
    //   { ColumnCount:d SpineCount:d NodeCount:d LinkCount:d }
    // ...then, in that same Column/Spine/Node/Link order (dependency order - see below):
    //   [ column's own "xProperties" record ]   x ColumnCount
    //   [ spine's own "xProperties" record ]    x SpineCount
    // ...and per node, self-contained:
    //   [ node_topology's own "xProperties" record ]                  <- once per node, always present
    //   [ the plugin's own "xProperties" record, if HasAnyProperties ] <- once per node, conditional
    // ...repeated NodeCount times, then:
    //   [ link_instance's own "xProperties" record ]  x LinkCount
    //
    // Deliberately NOT one shared "Nodes" table (fixed columns, one row per node) kept in lockstep-by-
    // array-order with a separately-counted sequence of per-node property blocks, the way this used to
    // work - that two-parallel-sequences design is what caused a real, previously-shipped bug: a
    // node's reflected shape gaining a new DONT_SAVE-only member made the OLD HasAnyProperties
    // predicate ("has any member at all") say a property block would follow when the real serializer's
    // collector (which skips DONT_SAVE members) actually wrote zero bytes for it - so the reader
    // consumed the NEXT node's own block instead, silently cascading a misalignment through the rest
    // of the file. Serializing each node as its own self-describing record sidesteps the whole class
    // of bug: xproperty's own "Name"/"Value" row format means a missing/renamed field is just a
    // missing row, not a positional-column desync - no hand-rolled tolerant-missing-field checks
    // needed either (OwnedEndId's old FIELD_NOT_FOUND tolerance is gone, now moot for the same reason).
    //------------------------------------------------------------------------------------------------
    static bool SaveGraph(const std::string& Utf8Path, const std::vector<node_instance>& Nodes, const std::vector<link_instance>& Links, const std::vector<available_node_type>& AvailableTypes
                         , const std::vector<spine>& Spines, const std::vector<column>& Columns
                         )
    {
        const std::wstring WPath(Utf8Path.begin(), Utf8Path.end()); // ASCII-safe path is all this demo needs

        xtextfile::stream Stream;
        if (auto Err = Stream.Open(false, WPath, xtextfile::file_type::TEXT); Err)
        {
            Debugger(std::format("Node OS: failed to open '{}' for saving", Utf8Path));
            return false;
        }

        const auto FindSourcePath = [&](const xnode_os_node_factory* pFactory) -> std::string
        {
            // The plugin's DIRECTORY NAME, not its absolute .cpp path (kept as "Source" in the field
            // name/comment for continuity, but see plugin_source_entry's own comment on why a folder
            // name is the actual identity) - stays meaningful if the repo ever moves and matches what
            // AddNode/DeleteNodes commands already use.
            for (auto& T : AvailableTypes) if (T.m_pFactory == pFactory) return T.m_DirName;
            return {};
        };

        // One combined header up front - see SaveGraph's own top comment for the file shape. Same
        // official xproperty::sprop::serializer::Stream() every other record below uses.
        {
            graph_header Header
            { .ColumnCount = static_cast<std::int32_t>(Columns.size())
            , .SpineCount  = static_cast<std::int32_t>(Spines.size())
            , .NodeCount   = static_cast<std::int32_t>(Nodes.size())
            , .LinkCount   = static_cast<std::int32_t>(Links.size())
            };
            xproperty::settings::context Context;
            if (auto Err = xproperty::sprop::serializer::Stream(Stream, Header, Context); Err)
            {
                Debugger(std::format("Node OS: failed writing Header record: {}", Err.getMessage()));
                return false;
            }
        }

        // Dependency order: Columns -> Spines (references ColumnId) -> Nodes (references SpineId) ->
        // Links (references node ids) - so LoadGraph can validate each cross-reference immediately
        // against what's already been loaded, rather than loading everything blind and only checking
        // at the very end.
        if (auto Err = SaveGraphItems(Stream, Columns); Err)
        {
            Debugger(std::format("Node OS: failed writing Columns: {}", Err.getMessage()));
            return false;
        }

        if (auto Err = SaveGraphItems(Stream, Spines); Err)
        {
            Debugger(std::format("Node OS: failed writing Spines: {}", Err.getMessage()));
            return false;
        }

        for (auto& N : Nodes)
        {
            node_topology Topology;
            Topology.Id         = N.m_Id;
            Topology.Source     = N.m_pNode ? FindSourcePath(N.m_pNode->m_pFactory) : std::string{};
            Topology.Type       = N.m_pNode ? std::string(N.m_pNode->m_pFactory->getName()) : std::string{};
            Topology.Order      = N.m_Order;
            Topology.SpineId    = N.m_SpineId;
            // The owner->marker relationship (If/ForEachLoop -> its own End/End-Else) - 0 if this node
            // doesn't own one. Without this, every save/load round-trip silently flattened the whole
            // graph's nesting: a reloaded owner would show as owning nothing, which desyncs cascading
            // delete/drag/select AND (once IsDataLinkScopeValid existed) makes every node look like
            // unnested "world scope", since ComputeEnclosingChains/ComputeScopeDepths both derive
            // nesting purely from this one field.
            Topology.OwnedEndId = N.m_OwnedEndId;

            xproperty::settings::context TopologyContext;
            if (auto Err = xproperty::sprop::serializer::Stream(Stream, Topology, TopologyContext); Err)
            {
                Debugger(std::format("Node OS: failed writing topology for node {}: {}", N.m_Id, Err.getMessage()));
                return false;
            }

            if (HasAnyProperties(N.m_pNode))
            {
                if (auto Err = SerializeReflectedMembers(Stream, N.m_pNode); Err)
                {
                    Debugger(std::format("Node OS: failed writing properties for node {}: {}", N.m_Id, Err.getMessage()));
                    return false;
                }
            }
        }

        // Links last - they reference node ids, so this is the last thing that needs Nodes to already
        // exist (see SaveGraphItems/link_instance's own XPROPERTY_DEF).
        if (auto Err = SaveGraphItems(Stream, Links); Err)
        {
            Debugger(std::format("Node OS: failed writing Links: {}", Err.getMessage()));
            return false;
        }

        return true;
    }

    // Mirrors SaveGraph. A node whose recorded Source/Type can no longer be resolved fails the WHOLE
    // load rather than silently skipping just that node: skipping it would still leave its own
    // "xProperties" record (if HasAnyProperties said it had one) sitting unread in the file, desyncing
    // every record after it - a loud, whole-file failure beats a quietly corrupted partial load.
    static bool LoadGraph(const std::string& Utf8Path, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links
                         , std::vector<plugin_source_entry>& Sources, std::vector<available_node_type>& AvailableTypes
                         , std::vector<spine>& Spines, std::vector<column>& Columns
                         )
    {
        const std::wstring WPath(Utf8Path.begin(), Utf8Path.end());

        std::vector<node_instance> NewNodes;
        std::vector<link_instance> NewLinks;
        std::vector<column>        NewColumns;
        std::vector<spine>         NewSpines;

        xtextfile::stream Stream;
        if (auto Err = Stream.Open(true, WPath, xtextfile::file_type::TEXT); Err)
        {
            Debugger(std::format("Node OS: failed to open '{}' for loading", Utf8Path));
            return false;
        }

        std::int32_t ColumnCount = 0, SpineCount = 0, NodeCount = 0, LinkCount = 0;
        {
            graph_header Header;
            xproperty::settings::context Context;
            if (auto Err = xproperty::sprop::serializer::Stream(Stream, Header, Context); Err)
            {
                Debugger(std::format("Node OS: failed reading Header record: {}", Err.getMessage()));
                return false;
            }
            ColumnCount = Header.ColumnCount;
            SpineCount  = Header.SpineCount;
            NodeCount   = Header.NodeCount;
            LinkCount   = Header.LinkCount;
        }

        // Dependency order mirrors SaveGraph: Columns -> Spines (validates ColumnId immediately
        // against the just-loaded Columns) -> Nodes (validates SpineId immediately against the
        // just-loaded Spines) -> Links (references node ids, so it comes last).
        if (auto Err = LoadGraphItems(Stream, ColumnCount, NewColumns); Err)
        {
            Debugger(std::format("Node OS: failed reading Columns: {}", Err.getMessage()));
            return false;
        }

        // Every Left/RightId a column claims must resolve among the columns just loaded - a dangling
        // reference fails the whole load, same policy as every check below (a loud failure beats a
        // quietly corrupted graph).
        {
            auto HasColumn = [&](std::uint64_t Id) { return std::any_of(NewColumns.begin(), NewColumns.end(), [&](auto& Co) { return Co.m_Id == Id; }); };
            for (auto& Co : NewColumns)
            {
                if ((Co.m_LeftId != 0 && !HasColumn(Co.m_LeftId)) || (Co.m_RightId != 0 && !HasColumn(Co.m_RightId)))
                {
                    Debugger("Node OS: failed loading - a Column's Left/RightId does not resolve");
                    return false;
                }
            }
        }

        if (auto Err = LoadGraphItems(Stream, SpineCount, NewSpines); Err)
        {
            Debugger(std::format("Node OS: failed reading Spines: {}", Err.getMessage()));
            return false;
        }

        // Every ColumnId a spine claims must resolve among the columns already loaded above.
        {
            auto HasColumn = [&](std::uint64_t Id) { return std::any_of(NewColumns.begin(), NewColumns.end(), [&](auto& Co) { return Co.m_Id == Id; }); };
            for (auto& Sp : NewSpines)
                if (!HasColumn(Sp.m_ColumnId))
                {
                    Debugger("Node OS: failed loading - a Spine's ColumnId does not resolve");
                    return false;
                }
        }

        NewNodes.reserve(static_cast<std::size_t>(NodeCount));

        // Each node is its own self-contained unit - its own node_topology record, immediately
        // followed by its own plugin-reflected "xProperties" record if it has one - so this is a plain
        // manual loop, not Stream.Record's per-row iteration (that's for many rows of ONE record; here
        // every node is a SEPARATE record).
        {
            auto HasSpine = [&](std::uint64_t Id) { return std::any_of(NewSpines.begin(), NewSpines.end(), [&](auto& Sp) { return Sp.m_Id == Id; }); };
            for (std::int32_t i = 0; i < NodeCount; ++i)
            {
                node_topology Topology;
                xproperty::settings::context TopologyContext;
                if (auto Err = xproperty::sprop::serializer::Stream(Stream, Topology, TopologyContext); Err)
                {
                    Debugger(std::format("Node OS: failed reading topology for node index {}: {}", i, Err.getMessage()));
                    for (auto& M : NewNodes) DestroyNodeInstance(M);
                    return false;
                }

                if (!HasSpine(Topology.SpineId))
                {
                    Debugger(std::format("Node OS: failed loading - node {}'s SpineId does not resolve", Topology.Id));
                    for (auto& M : NewNodes) DestroyNodeInstance(M);
                    return false;
                }

                auto SrcIt = std::find_if(Sources.begin(), Sources.end(), [&](auto& S) { return S.m_DirName == Topology.Source; });
                if (SrcIt == Sources.end())
                {
                    Debugger(std::format("Node OS: a saved node's plugin source no longer exists: {}", Topology.Source));
                    for (auto& M : NewNodes) DestroyNodeInstance(M);
                    return false;
                }

                // EnsureLoadedAndGetType ensures SrcIt is compiled+loaded, but only ever returns the
                // FIRST type it registered - fine for a single-type plugin (the common case, where
                // that's trivially the only candidate), but wrong for a multi-type one (see
                // xnode_os_plugin_api.h's NodeOS_CreateFactories): a saved node naming the SECOND or
                // THIRD registered type (e.g. "Cos"/"Tan" from a "Trig" source whose first type is
                // "Sin") would otherwise fail this check and reject the ENTIRE load, not just place
                // the wrong node - search the rest of this source's own AvailableTypes entries for
                // the one Topology.Type actually names before giving up.
                auto* pFactory = EnsureLoadedAndGetType(*SrcIt, AvailableTypes);
                if (pFactory && Topology.Type != pFactory->getName())
                {
                    pFactory = nullptr;
                    for (auto& T : AvailableTypes)
                        if (T.m_DirName == SrcIt->m_DirName && T.m_pFactory->getName() == Topology.Type) { pFactory = T.m_pFactory; break; }
                }
                if (!pFactory)
                {
                    Debugger(std::format("Node OS: a saved node's type no longer matches its plugin source: {}", Topology.Type));
                    for (auto& M : NewNodes) DestroyNodeInstance(M);
                    return false;
                }

                NewNodes.push_back(CreateNodeInstance(Topology.Id, pFactory, Topology.Order, Topology.SpineId));
                NewNodes.back().m_OwnedEndId = Topology.OwnedEndId;

                if (HasAnyProperties(NewNodes.back().m_pNode))
                {
                    if (auto Err = SerializeReflectedMembers(Stream, NewNodes.back().m_pNode); Err)
                    {
                        Debugger(std::format("Node OS: failed reading properties for node {}: {}", Topology.Id, Err.getMessage()));
                        for (auto& M : NewNodes) DestroyNodeInstance(M);
                        return false;
                    }
                }
            }
        }

        // Links last - they reference node ids, so Nodes must already be loaded to validate against.
        if (auto Err = LoadGraphItems(Stream, LinkCount, NewLinks); Err)
        {
            Debugger(std::format("Node OS: failed reading Links: {}", Err.getMessage()));
            for (auto& N : NewNodes) DestroyNodeInstance(N);
            return false;
        }

        // m_bReadOnly isn't persisted at all (not even as a column) - an ownership link's read-only-
        // ness is fully implied by whether some node's own m_OwnedEndId matches it, so re-deriving
        // here can never drift out of sync with the Nodes record the way storing it a second time
        // could. Without this, every save/load round-trip silently downgraded every owner<->End
        // ownership link to an ordinary, user-editable/deletable one - and, since bScopeInvalid's red
        // coloring explicitly skips read-only links, made them wrongly subject to that check too
        // (an End marker is always inside its owner's own ComputeScopeSpan by construction).
        for (auto& L : NewLinks)
            for (auto& N : NewNodes)
                if (N.m_Id == L.m_SourceNode && N.m_OwnedEndId == L.m_TargetNode) { L.m_bReadOnly = true; break; }

        for (auto& N : Nodes) DestroyNodeInstance(N);
        Nodes          = std::move(NewNodes);
        Links          = std::move(NewLinks);
        Spines         = std::move(NewSpines);
        Columns        = std::move(NewColumns);

        return true;
    }
}
