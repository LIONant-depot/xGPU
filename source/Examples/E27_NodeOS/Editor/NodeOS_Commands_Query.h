#pragma once
// Every "NodeOS/Query/..." command struct, extracted from the monolithic E27_NodeOS_Editor.cpp
// (header #13): Save, Load, BuildNode, CompileToCpp, GetNodeInfo, GetNodeProperties, GetNodeValues,
// RunGraph, ClearGraph, UnloadPlugin, ReloadPlugin, RescanPlugins, GetLog, ListNodes, Screenshot,
// SetView, GetView.
//
// This is the last of the 13 split headers - once this one is included, every command struct that
// used to live in the monolith's single "namespace commands" block (originally split across this
// file, NodeOS_CommandBuilders.h, NodeOS_CommandContext.h, and NodeOS_Commands_Edit.h) is available,
// and the .cpp's own leftover "namespace nodeos { ... }" wrapper has nothing left inside it.
#include "NodeOS_Common.h"
#include "NodeOS_Types.h"
#include "NodeOS_PropertySerialize.h"
#include "NodeOS_SaveLoad.h"
#include "NodeOS_Interpreter.h"
#include "NodeOS_Codegen.h"
#include "NodeOS_CommandBuilders.h"
#include "NodeOS_CanvasSupport.h"
#include "NodeOS_UI_CommandConsole.h"
#include "NodeOS_CommandContext.h"

namespace nodeos
{
    //================================================================================================
    // Commands - every graph mutation becomes a string command executed through xundo::system::
    // Execute(), which has zero ImGui/xgpu dependency: the ImGui interaction code above builds a
    // command string and calls the exact same entry point a future headless runner or "command
    // source" driver plugin would call (see this file's top comment). Selection changes go through
    // this SAME history as data commands (explicit choice - Ctrl+Z steps back through selection
    // changes too, not just data edits).
    //================================================================================================
    namespace commands
    {
        //================================================================================================
        // ListNodes - first proof-of-concept query command: read-only, no Redo/Undo/BackupCurrenState,
        // registered against xundo::query_command_base (not command_base) so it can never become an
        // undo step and never needs a database mutation to answer. Reached via the central router as
        // "NodeOS/Query/ListNodes" (see xundo::history::Route, xundo_history.h) - this is deliberately
        // the simplest possible query, meant to prove the routing plumbing end-to-end before designing
        // richer ones (resolved wildcard type, scope/nesting, a full node dump, a Validate pass).
        //================================================================================================
        struct list_nodes_query_cmd : xundo::query_command_base
        {
            list_nodes_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "ListNodes", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Lists every node's Id and Type. Usage: ListNodes"; }
            void RegisterArguments() noexcept override {} // takes no arguments at all

            std::string Query() noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                std::string Out;
                for (auto& N : Ctx.m_Nodes)
                    Out += std::format("{:#x}  {}\n", N.m_Id, N.m_pNode ? std::string(N.m_pNode->m_pFactory->getName()) : std::string("?"));
                return Out;
            }
        };

        //================================================================================================
        // GetLog - returns the Command Console's own full log verbatim: every command run through it
        // so far, whether typed into the UI or sent over NodeOSCLI's pipe (both paths append to the
        // exact same m_ConsoleLog, tagged with a "$ " vs. "> " echo marker for the latter - see
        // PumpCommandConsolePipe/DrawCommandConsolePanel). This is what makes the pipe genuinely
        // two-way: NodeOSCLI already lets an external caller send a command the human sees; this is
        // how that caller can also see what the human just did, without any UI automation - just
        // another Query, over the same pipe, using the exact same getCommandHelp()/routing
        // conventions as every other command.
        //================================================================================================
        struct get_log_query_cmd : xundo::query_command_base
        {
            get_log_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "GetLog", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Returns the Command Console's full log (UI-typed and pipe-driven commands alike). Usage: GetLog"; }
            void RegisterArguments() noexcept override {} // takes no arguments at all

            std::string Query() noexcept override
            {
                // Flattened back into plain text here - color is a UI-only concern, meaningless to a
                // CLI response - see console_log_entry's own comment. ConsoleLogLinePrefix() is the
                // same "> "/"$ " marker DrawCommandConsolePanel colors on render, applied here in
                // plain text so the actor that authored each line is still visible, just uncolored.
                std::string Out;
                for (auto& Entry : get<node_os_command_context>().m_ConsoleLog)
                    Out += std::string(ConsoleLogLinePrefix(Entry.m_Source)) + Entry.m_Text + "\n";
                return Out;
            }
        };

        //================================================================================================
        // Load/Save - the SAME LoadGraph/SaveGraph the UI's own Load/Save buttons already call
        // directly (bypassing xundo entirely - neither button was ever routed through command_base's
        // Redo/Undo), just reachable over the pipe too. Registered as queries (not edits) for exactly
        // that reason: there's no undo/backup behavior to give up by skipping command_base, since the
        // existing UI path never had any either. Mainly here so an external caller can force a reload
        // without clicking the UI button - useful when a fresh launch doesn't auto-load for whatever
        // reason (a real, still-open question - see this session's own notes on it).
        //================================================================================================
        struct load_graph_query_cmd : xundo::query_command_base
        {
            load_graph_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "Load", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Reloads the graph from disk, replacing everything currently in memory. Usage: Load [-Path filepath]"; }
            void RegisterArguments() noexcept override
            {
                m_hPath = m_Parser.addOption("Path", "Graph file path (defaults to the checked-in example graph)", false, 1);
            }

            std::string Query() noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                std::string Path = "D:/LIONant/xGPU/source/Examples/E27_NodeOS/graph.txt";
                if (m_Parser.hasOption(m_hPath))
                {
                    auto PathArg = m_Parser.getOptionArgAs<std::string>(m_hPath, 0);
                    if (!std::holds_alternative<xerr>(PathArg)) Path = std::get<std::string>(PathArg);
                }
                const bool bOk = LoadGraph(Path, Ctx.m_Nodes, Ctx.m_Links, Ctx.m_Sources, Ctx.m_AvailableTypes, Ctx.m_Spines, Ctx.m_Columns);
                Ctx.m_bDirty = true; // re-run the freshly loaded graph, same deferred path the UI's own Load button relies on
                // The UI's own Load button always did this (its own comment: "any existing undo
                // history refers to node/link ids that may no longer mean anything in the new graph")
                // - this CLI/pipe path had been missing it, a real gap since a fresh set of Edit
                // commands issued right after a Load would otherwise accumulate against a History
                // still shaped around the PREVIOUS graph.
                m_System.Reset();
                return bOk ? std::format("Loaded '{}' - {} nodes, {} links", Path, Ctx.m_Nodes.size(), Ctx.m_Links.size())
                           : std::format("Load failed for '{}' - see log", Path);
            }
            xcmdline::parser::handle m_hPath;
        };

        struct save_graph_query_cmd : xundo::query_command_base
        {
            save_graph_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "Save", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Saves the current graph to disk. Usage: Save [-Path filepath]"; }
            void RegisterArguments() noexcept override
            {
                m_hPath = m_Parser.addOption("Path", "Graph file path (defaults to the checked-in example graph)", false, 1);
            }

            std::string Query() noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                std::string Path = "D:/LIONant/xGPU/source/Examples/E27_NodeOS/graph.txt";
                if (m_Parser.hasOption(m_hPath))
                {
                    auto PathArg = m_Parser.getOptionArgAs<std::string>(m_hPath, 0);
                    if (!std::holds_alternative<xerr>(PathArg)) Path = std::get<std::string>(PathArg);
                }
                const bool bOk = SaveGraph(Path, Ctx.m_Nodes, Ctx.m_Links, Ctx.m_AvailableTypes, Ctx.m_Spines, Ctx.m_Columns);
                return bOk ? std::format("Saved '{}' - {} nodes, {} links", Path, Ctx.m_Nodes.size(), Ctx.m_Links.size())
                           : std::format("Save failed for '{}' - see log", Path);
            }
            xcmdline::parser::handle m_hPath;
        };

        //================================================================================================
        // GetNodeProperties - a node's reflected property values, verbatim (Name/Kind/Value rows, the
        // same format SerializePropertiesToString already produces for undo snapshots - reused as-is
        // rather than inventing a second, prettier-but-redundant text format).
        //================================================================================================
        struct get_node_properties_query_cmd : xundo::query_command_base
        {
            get_node_properties_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "GetNodeProperties", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Returns a node's reflected properties as Name/Kind/Value rows. Usage: GetNodeProperties -Id N"; }
            void RegisterArguments() noexcept override { m_hId = m_Parser.addOption("Id", "Node id", true, 1); }

            std::string Query() noexcept override
            {
                auto IdArg = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                if (std::holds_alternative<xerr>(IdArg)) return "GetNodeProperties: bad arguments";
                const auto Id = ParseGuid(std::get<std::string>(IdArg));

                auto& Ctx = get<node_os_command_context>();
                auto It = std::find_if(Ctx.m_Nodes.begin(), Ctx.m_Nodes.end(), [&](auto& N) { return N.m_Id == Id; });
                if (It == Ctx.m_Nodes.end())  return std::format("GetNodeProperties: no such node {:#x}", Id);
                if (!It->m_pNode)              return std::format("GetNodeProperties: node {:#x} has no resolved plugin", Id);
                if (!HasAnyProperties(It->m_pNode)) return "(no properties)";
                return SerializePropertiesToString(It->m_pNode);
            }
            xcmdline::parser::handle m_hId;
        };

        //================================================================================================
        // GetNodeInfo - type/topology plus every pin's effective (wildcard-resolved) type and what's
        // actually wired to it - the "am I looking at the right node, and is it connected the way I
        // think" situational-awareness query GetNodeProperties alone can't answer (property VALUES
        // don't say anything about topology/wiring).
        //================================================================================================
        struct get_node_info_query_cmd : xundo::query_command_base
        {
            get_node_info_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "GetNodeInfo", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Returns a node's type, topology, and pin wiring. Usage: GetNodeInfo -Id N"; }
            void RegisterArguments() noexcept override { m_hId = m_Parser.addOption("Id", "Node id", true, 1); }

            std::string Query() noexcept override
            {
                auto IdArg = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                if (std::holds_alternative<xerr>(IdArg)) return "GetNodeInfo: bad arguments";
                const auto Id = ParseGuid(std::get<std::string>(IdArg));

                auto& Ctx = get<node_os_command_context>();
                auto It = std::find_if(Ctx.m_Nodes.begin(), Ctx.m_Nodes.end(), [&](auto& N) { return N.m_Id == Id; });
                if (It == Ctx.m_Nodes.end()) return std::format("GetNodeInfo: no such node {:#x}", Id);
                if (!It->m_pNode)             return std::format("GetNodeInfo: node {:#x} has no resolved plugin", Id);

                std::string Out = std::format("Id: {:#x}\nType: {}\nOrder: {}\nSpineId: {:#x}\nOwnedEndId: {:#x}\n"
                    , It->m_Id, It->m_pNode->m_pFactory->getName(), It->m_Order, It->m_SpineId, It->m_OwnedEndId);

                const auto Inputs = It->m_pNode->getInputs();
                Out += "Inputs:\n";
                for (int i = 0; i < (int)Inputs.size(); ++i)
                {
                    const char* pEffType = EffectiveTypeName(Id, It->m_pNode, Inputs[i].m_pTypeName, Ctx.m_Nodes, Ctx.m_Links);
                    std::string Wire = "(unconnected)";
                    for (auto& L : Ctx.m_Links)
                        if (L.m_TargetNode == Id && L.m_TargetInput == i)
                        {
                            auto SrcIt = std::find_if(Ctx.m_Nodes.begin(), Ctx.m_Nodes.end(), [&](auto& N) { return N.m_Id == L.m_SourceNode; });
                            Wire = std::format("<- {:#x}[{}] ({})", L.m_SourceNode, L.m_SourceOutput
                                , (SrcIt != Ctx.m_Nodes.end() && SrcIt->m_pNode) ? std::string(SrcIt->m_pNode->m_pFactory->getName()) : std::string("?"));
                            break;
                        }
                    Out += std::format("  {} : {} {}\n", Inputs[i].m_pName, pEffType, Wire);
                }

                const auto Outputs = It->m_pNode->getOutputs();
                Out += "Outputs:\n";
                for (int i = 0; i < (int)Outputs.size(); ++i)
                {
                    const char* pEffType = EffectiveTypeName(Id, It->m_pNode, Outputs[i].m_pTypeName, Ctx.m_Nodes, Ctx.m_Links);
                    int ConnectedCount = 0;
                    for (auto& L : Ctx.m_Links) if (L.m_SourceNode == Id && L.m_SourceOutput == i) ++ConnectedCount;
                    Out += std::format("  {} : {} ({} connection{})\n", Outputs[i].m_pName, pEffType, ConnectedCount, ConnectedCount == 1 ? "" : "s");
                }
                return Out;
            }
            xcmdline::parser::handle m_hId;
        };

        //================================================================================================
        // RunGraph - forces a re-run through the SAME deferred path an edit already triggers
        // (Ctx.m_bDirty=true -> nodeos::ExecuteGraph at the top of the next frame). Exists so
        // GetNodeValues (below) has something fresh to read without needing a UI click - two separate
        // NodeOSCLI invocations are two separate pipe connections, each landing on a different frame
        // of PumpCommandConsolePipe, so by the time a follow-up GetNodeValues call arrives the run has
        // already happened - no deferred-response bridge needed here (contrast with Screenshot).
        //================================================================================================
        struct run_graph_query_cmd : xundo::query_command_base
        {
            run_graph_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "RunGraph", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Forces the graph to re-run (same deferred path as any edit) - run this before GetNodeValues to see fresh results. Usage: RunGraph"; }
            void RegisterArguments() noexcept override {}

            std::string Query() noexcept override
            {
                get<node_os_command_context>().m_bDirty = true;
                return "Graph will re-run at the top of the next frame.";
            }
        };

        //================================================================================================
        // GetNodeValues - GetNodeInfo shows STRUCTURE (type, wiring, declared types); this shows the
        // actual runtime VALUES flowing through a node right now - what an input currently resolves to
        // (a live wire's upstream output, or an unconnected pin's own literal), and what an output
        // actually produced on the last run (node.m_CachedOutputs, populated by Execute()). Reuses
        // GetInputValue/PortTypeToPreview - the exact same value-resolution and type-dispatch-to-text
        // logic the canvas's own pin-hover preview already uses, so this reports the same thing a
        // human looking at the graph would see, never a second, drifting formatting path.
        //================================================================================================
        struct get_node_values_query_cmd : xundo::query_command_base
        {
            get_node_values_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "GetNodeValues", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Returns a node's actual runtime input/output values (not just wiring) - run RunGraph first if the graph hasn't executed since your last edit. Usage: GetNodeValues -Id N"; }
            void RegisterArguments() noexcept override { m_hId = m_Parser.addOption("Id", "Node id", true, 1); }

            std::string Query() noexcept override
            {
                auto IdArg = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                if (std::holds_alternative<xerr>(IdArg)) return "GetNodeValues: bad arguments";
                const auto Id = ParseGuid(std::get<std::string>(IdArg));

                auto& Ctx = get<node_os_command_context>();
                auto It = std::find_if(Ctx.m_Nodes.begin(), Ctx.m_Nodes.end(), [&](auto& N) { return N.m_Id == Id; });
                if (It == Ctx.m_Nodes.end()) return std::format("GetNodeValues: no such node {:#x}", Id);
                if (!It->m_pNode)            return std::format("GetNodeValues: node {:#x} has no resolved plugin", Id);

                std::string Out = std::format("Id: {:#x}\nType: {}\nHasRun: {}\n"
                    , It->m_Id, It->m_pNode->m_pFactory->getName()
                    , It->m_bHasRun ? "yes" : "no (run RunGraph first to get fresh values)");
                if (!It->m_LastError.empty())
                    Out += std::format("LastError: {}\n", It->m_LastError);

                // A fresh scratch per call, not shared with anything else running this same frame -
                // GetInputValue's own literal-resolution needs live storage for the duration of these
                // reads, same as ExecuteGraph's own LiteralScratch.
                literal_storage Scratch;

                const auto Inputs = It->m_pNode->getInputs();
                Out += "Inputs:\n";
                for (int i = 0; i < (int)Inputs.size(); ++i)
                {
                    const char* pEffType = EffectiveTypeName(Id, It->m_pNode, Inputs[i].m_pTypeName, Ctx.m_Nodes, Ctx.m_Links);
                    void* pValue = GetInputValue(Id, i, Ctx.m_Nodes, Ctx.m_Links, Scratch);
                    // Copied into a real std::string immediately - PortTypeToPreview's return is
                    // backed by a shared thread_local scratch buffer, so holding onto the raw
                    // const char* across another call (the NEXT iteration's own PortTypeToPreview,
                    // below) would silently corrupt this one - see [[xgpu_thread_local_pointer_aliasing]].
                    const std::string Preview = PortTypeToPreview(pEffType, pValue);
                    Out += std::format("  {} : {} = {}\n", Inputs[i].m_pName, pEffType, Preview.empty() ? "(none)" : Preview);
                }

                const auto Outputs = It->m_pNode->getOutputs();
                Out += "Outputs:\n";
                for (int i = 0; i < (int)Outputs.size(); ++i)
                {
                    const char* pEffType = EffectiveTypeName(Id, It->m_pNode, Outputs[i].m_pTypeName, Ctx.m_Nodes, Ctx.m_Links);
                    void* pValue = (It->m_bHasRun && i < (int)It->m_CachedOutputs.size()) ? It->m_CachedOutputs[i] : nullptr;
                    const std::string Preview = PortTypeToPreview(pEffType, pValue);
                    Out += std::format("  {} : {} = {}\n", Outputs[i].m_pName, pEffType, Preview.empty() ? "(none)" : Preview);
                }
                return Out;
            }
            xcmdline::parser::handle m_hId;
        };

        //================================================================================================
        // CompileToCpp - the SAME GenerateCpp/CompileAndRunGeneratedCpp pipeline the "Compile to C++"
        // UI button calls, reachable from the CLI/pipe so a generated-code regression (like a new node
        // type's codegen case being missing entirely) can be caught without clicking through the UI.
        // Runs the generated program too, not just compiles it - "it produced valid-looking text" and
        // "it actually compiles and runs correctly" are different claims, and only the second one is
        // what this reports as success.
        //================================================================================================
        struct compile_to_cpp_query_cmd : xundo::query_command_base
        {
            compile_to_cpp_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "CompileToCpp", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Compiles the current graph - a program graph (OnEvent/ExecutionCall) generates C++, compiles it standalone, and runs it; a node-definition graph (NodeBuilder) publishes it as a real plugin instead (same as the BuildNode command) - the graph itself says which. Usage: CompileToCpp"; }
            void RegisterArguments() noexcept override {}

            std::string Query() noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                // The graph already says what it is - dispatch automatically rather than making the
                // caller separately check and know to reach for a different command.
                if (auto* pBuilder = FindTheNodeBuilder(Ctx.m_Nodes))
                    return BuildNodeFromFunction(*pBuilder, Ctx.m_Nodes, Ctx.m_Links, Ctx.m_Sources, Ctx.m_AvailableTypes, Ctx.m_Spines, Ctx.m_Columns);
                const std::string Source = GenerateCpp(Ctx.m_Nodes, Ctx.m_Links, Ctx.m_Spines);
                const auto Result = CompileAndRunGeneratedCpp(Source);
                std::string Out = std::format("Compile: {}\n", Result.m_bCompileOk ? "OK" : "FAILED");
                if (!Result.m_bCompileOk)
                    return Out + Result.m_CompileLog;
                Out += std::format("Run: {}\n", Result.m_bRanOk ? "OK" : "FAILED");
                Out += "--- Output ---\n" + Result.m_RunOutput;
                return Out;
            }
        };

        //================================================================================================
        // BuildNode - "compile a Node" (see BuildNodeFromFunction, NODEBUILDER_PROBLEM_STATEMENT.md).
        // v1 is deliberately command-only, not wired into the live Exec-pin dispatch RunSpineRange/
        // EmitSpineRange already give Function/Execute/ExecutionCall - see node_builder_node.cpp's own
        // top comment for why. Takes the NodeBuilder INSTANCE's id (not the target Function's), same
        // "-Id" convention GetNodeProperties/GetNodeInfo already use.
        //================================================================================================
        struct build_node_query_cmd : xundo::query_command_base
        {
            build_node_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "BuildNode", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Compiles a NodeBuilder's own declared signature and body into a genuine new native node type. Usage: BuildNode -Id N"; }
            void RegisterArguments() noexcept override { m_hId = m_Parser.addOption("Id", "NodeBuilder instance id", true, 1); }

            std::string Query() noexcept override
            {
                auto IdArg = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                if (std::holds_alternative<xerr>(IdArg)) return "BuildNode: bad arguments";
                const auto Id = ParseGuid(std::get<std::string>(IdArg));

                auto& Ctx = get<node_os_command_context>();
                auto* pBuilder = FindNodeById(Id, Ctx.m_Nodes);
                if (!pBuilder) return std::format("BuildNode: no such node {:#x}", Id);
                if (!pBuilder->m_pNode || pBuilder->m_pNode->m_pFactory->getName() != "NodeBuilder")
                    return std::format("BuildNode: node {:#x} is not a NodeBuilder", Id);

                return BuildNodeFromFunction(*pBuilder, Ctx.m_Nodes, Ctx.m_Links, Ctx.m_Sources, Ctx.m_AvailableTypes, Ctx.m_Spines, Ctx.m_Columns);
            }
            xcmdline::parser::handle m_hId;
        };

        //================================================================================================
        // ClearGraph - destroys every node/link and resets Spines/Columns to a single empty root, the
        // same baseline -CodegenSelfTest's own standalone setup starts from. This is step 2 of the
        // safe plugin-reload sequence (see ReloadPlugin, below, and [[xgpu_plugin_dll_hotreload]]):
        // every node's m_pNode is destroyed through its OWN factory's DestroyNodeInstance while that
        // factory's module is still loaded - not just abandoned - so nothing is left holding a
        // dangling vtable pointer once UnloadPlugin actually FreeLibrary's it. Exposed standalone
        // too (not just folded into ReloadPlugin) since "wipe the canvas back to empty" is also
        // useful entirely on its own, independent of any plugin work.
        //================================================================================================
        struct clear_graph_query_cmd : xundo::query_command_base
        {
            clear_graph_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "ClearGraph", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Destroys every node/link and resets to a single empty root spine/column - the safe first step before unloading a plugin DLL still referenced by live nodes. Usage: ClearGraph"; }
            void RegisterArguments() noexcept override {}

            std::string Query() noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                const std::size_t NodeCount = Ctx.m_Nodes.size();

                for (auto& N : Ctx.m_Nodes) DestroyNodeInstance(N);
                Ctx.m_Nodes.clear();
                Ctx.m_Links.clear();

                Ctx.m_Selection.m_SelectedNodes.clear();
                Ctx.m_Selection.m_SelectedLink        = 0;
                Ctx.m_Selection.m_SelectedGapSpineId  = 0;
                Ctx.m_Selection.m_SelectedGapIndex    = -1;

                Ctx.m_Columns.clear();
                Ctx.m_Spines.clear();
                Ctx.m_Columns.push_back({ xresource::guid_generator::Instance64(), 0, 0, true });
                Ctx.m_Spines.push_back({ xresource::guid_generator::Instance64(), Ctx.m_Columns.front().m_Id, true, geo::TOP });

                Ctx.m_bDirty = true;
                // Every id ClearGraph just wiped is now meaningless to any undo/redo entry recorded
                // before this point - the exact same reasoning the UI's own Load button already
                // applies after replacing the whole graph (see its own comment). Safe to call from
                // inside a Query() dispatched BY this same m_System: Reset() only touches
                // m_History/m_LRU/m_UndoIndex, never the registered-command maps the outer Query()
                // call is currently iterating.
                m_System.Reset();
                return std::format("Cleared {} node(s) - graph reset to a single empty root spine/column (undo history reset too)", NodeCount);
            }
        };

        //================================================================================================
        // UnloadPlugin - FreeLibrary's a plugin's currently-loaded DLL. Refuses (rather than crashing)
        // if any live node's m_pNode->m_pFactory still lives in that module - ClearGraph (or deleting
        // just the offending nodes) first is what makes this safe, never something this command can
        // skip past. See ReloadPlugin, below, for the one-shot version of the full safe sequence.
        //================================================================================================
        // Calls the plugin's own NodeOS_DestroyFactory on every factory it registered (never `delete`
        // through the abstract base - see xnode_os_plugin_api.h's own comment on why), THEN
        // FreeLibrary's the module. Shared by UnloadPlugin and ReloadPlugin so this ordering is never
        // duplicated - skipping the destroy step would leak each factory object (a small, real leak:
        // /MDd means the plugin's own `new` goes through the SAME shared CRT heap as the host, so
        // FreeLibrary alone does not reclaim it).
        inline void DestroyFactoriesAndFreeModule(HMODULE Module, const std::vector<xnode_os_node_factory*>& Factories) noexcept
        {
            if (auto* pDestroy = (xnode_os_pfn_destroy_factory*)GetProcAddress(Module, XNODE_OS_DESTROY_FACTORY_NAME))
                for (auto* pFactory : Factories) pDestroy(*pFactory);
            FreeLibrary(Module);
        }

        struct unload_plugin_query_cmd : xundo::query_command_base
        {
            unload_plugin_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "UnloadPlugin", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Frees a plugin's currently-loaded DLL from memory - refuses if any live node still uses it (ClearGraph first). Usage: UnloadPlugin -DirName dirname"; }
            void RegisterArguments() noexcept override { m_hDirName = m_Parser.addOption("DirName", "The plugin's Plugins/<DirName>/ folder name", true, 1); }

            std::string Query() noexcept override
            {
                auto DirNameArg = m_Parser.getOptionArgAs<std::string>(m_hDirName, 0);
                if (std::holds_alternative<xerr>(DirNameArg)) return "UnloadPlugin: bad arguments";
                const std::string DirName = std::get<std::string>(DirNameArg);

                auto& Ctx = get<node_os_command_context>();
                auto* pSrc = FindSourceByDirName(Ctx.m_Sources, DirName);
                if (!pSrc)                              return std::format("UnloadPlugin: no such plugin source '{}'", DirName);
                if (!pSrc->m_bLoaded || !pSrc->m_Module) return std::format("UnloadPlugin: '{}' is not currently loaded", DirName);

                std::vector<xnode_os_node_factory*> DoomedFactories;
                for (auto& T : Ctx.m_AvailableTypes)
                    if (T.m_Module == pSrc->m_Module) DoomedFactories.push_back(T.m_pFactory);
                const std::size_t StillInUse = std::count_if(Ctx.m_Nodes.begin(), Ctx.m_Nodes.end(), [&](auto& N)
                    { return N.m_pNode && std::find(DoomedFactories.begin(), DoomedFactories.end(), N.m_pNode->m_pFactory) != DoomedFactories.end(); });
                if (StillInUse > 0)
                    return std::format("UnloadPlugin: refused - {} node(s) still use '{}'. Run ClearGraph first.", StillInUse, DirName);

                std::erase_if(Ctx.m_AvailableTypes, [&](auto& T) { return T.m_Module == pSrc->m_Module; });
                DestroyFactoriesAndFreeModule(pSrc->m_Module, DoomedFactories);
                pSrc->m_Module  = nullptr;
                pSrc->m_bLoaded = false;
                return std::format("Unloaded '{}'", DirName);
            }
            xcmdline::parser::handle m_hDirName;
        };

        //================================================================================================
        // RescanPlugins - ScanPluginSources only ever runs once, at E27_Example startup, so a plugin
        // folder dropped onto disk AFTER the editor is already running is invisible to it until this
        // is called (or the editor restarts) - the other half of "adding a new DLL shouldn't require
        // exiting the editor" alongside ReloadPlugin (which handles the "I edited an EXISTING
        // plugin's source" case; this handles "I just added a BRAND NEW plugin type"). Purely
        // additive: an already-known DirName's entry (m_bLoaded/m_Module/m_CompileLog) is left
        // completely untouched, so this can never disturb a plugin that's already loaded and in use.
        //================================================================================================
        struct rescan_plugins_query_cmd : xundo::query_command_base
        {
            rescan_plugins_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "RescanPlugins", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Re-scans Plugins/ for folders added since the editor started (or the last RescanPlugins) and makes them addable - no restart needed. Usage: RescanPlugins"; }
            void RegisterArguments() noexcept override {}

            std::string Query() noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                auto Fresh = ScanPluginSources("D:/LIONant/xGPU/source/Examples/E27_NodeOS/Plugins");

                std::vector<std::string> NewlyFound;
                for (auto& F : Fresh)
                {
                    if (FindSourceByDirName(Ctx.m_Sources, F.m_DirName)) continue;
                    NewlyFound.push_back(F.m_DirName);
                    Ctx.m_Sources.push_back(std::move(F));
                }
                std::sort(Ctx.m_Sources.begin(), Ctx.m_Sources.end(), [](auto& A, auto& B) { return A.m_DisplayName < B.m_DisplayName; });

                if (NewlyFound.empty()) return "No new plugin folders found.";
                std::string Out = std::format("Found {} new plugin folder(s) - addable now via CreateNode/the Node Library:\n", NewlyFound.size());
                for (auto& Name : NewlyFound) Out += "  " + Name + "\n";
                return Out;
            }
        };

        //================================================================================================
        // ReloadPlugin - the full safe hot-reload sequence in one call, for when a plugin's own .cpp
        // source just changed and the running session needs to pick that up without restarting:
        //   1. Save  - the graph is about to be torn down; must reflect what's on the canvas NOW.
        //   2. Clear - destroy every node instance via its OWN (still-loaded) factory.
        //   3. Unload - safe now that nothing references the module.
        //   4+5. Recompile + load - CompileAndLoadPlugin always does both in one shot; this codebase
        //        has no "compiled but not yet loaded" state to split into two separate steps (a fresh,
        //        never-reused .dll filename is written, then immediately LoadLibrary'd).
        //   6. Reload - LoadGraph's own EnsureLoadedAndGetType resolves every saved node against
        //        whatever's loaded now, including the plugin just swapped.
        // Each step after Save is best-effort-continue rather than abort-on-failure past that point:
        // by the time step 2 has run, the canvas is already empty, so getting back to a WORKING
        // graph (even the old one) matters more than stopping halfway through.
        //================================================================================================
        struct reload_plugin_query_cmd : xundo::query_command_base
        {
            reload_plugin_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "ReloadPlugin", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Hot-reloads a plugin whose source just changed: Save, ClearGraph, UnloadPlugin, recompile+load the DLL, then reload the saved graph. Usage: ReloadPlugin -DirName dirname [-Path filepath]"; }
            void RegisterArguments() noexcept override
            {
                m_hDirName = m_Parser.addOption("DirName", "The plugin's Plugins/<DirName>/ folder name", true, 1);
                m_hPath    = m_Parser.addOption("Path", "Graph file path (defaults to the checked-in example graph)", false, 1);
            }

            std::string Query() noexcept override
            {
                auto DirNameArg = m_Parser.getOptionArgAs<std::string>(m_hDirName, 0);
                if (std::holds_alternative<xerr>(DirNameArg)) return "ReloadPlugin: bad arguments";
                const std::string DirName = std::get<std::string>(DirNameArg);

                auto& Ctx = get<node_os_command_context>();
                std::string Path = "D:/LIONant/xGPU/source/Examples/E27_NodeOS/graph.txt";
                if (m_Parser.hasOption(m_hPath))
                {
                    auto PathArg = m_Parser.getOptionArgAs<std::string>(m_hPath, 0);
                    if (!std::holds_alternative<xerr>(PathArg)) Path = std::get<std::string>(PathArg);
                }

                auto* pSrc = FindSourceByDirName(Ctx.m_Sources, DirName);
                if (!pSrc) return std::format("ReloadPlugin: no such plugin source '{}'", DirName);

                std::string Out;

                // 1. Save
                if (!SaveGraph(Path, Ctx.m_Nodes, Ctx.m_Links, Ctx.m_AvailableTypes, Ctx.m_Spines, Ctx.m_Columns))
                    return std::format("ReloadPlugin: save to '{}' failed - aborting before touching anything", Path);
                Out += std::format("1. Saved '{}'.\n", Path);

                // 2. Clear
                for (auto& N : Ctx.m_Nodes) DestroyNodeInstance(N);
                Ctx.m_Nodes.clear();
                Ctx.m_Links.clear();
                Ctx.m_Selection.m_SelectedNodes.clear();
                Ctx.m_Selection.m_SelectedLink       = 0;
                Ctx.m_Selection.m_SelectedGapSpineId = 0;
                Ctx.m_Selection.m_SelectedGapIndex   = -1;
                m_System.Reset(); // see clear_graph_query_cmd's own comment - every id just wiped is meaningless to any earlier undo entry
                Out += "2. Cleared the editor.\n";

                // 3. Unload - a plugin that was never loaded yet (first reload after a fresh launch)
                //    just skips this step rather than failing.
                if (pSrc->m_bLoaded && pSrc->m_Module)
                {
                    std::vector<xnode_os_node_factory*> OldFactories;
                    for (auto& T : Ctx.m_AvailableTypes) if (T.m_Module == pSrc->m_Module) OldFactories.push_back(T.m_pFactory);
                    std::erase_if(Ctx.m_AvailableTypes, [&](auto& T) { return T.m_Module == pSrc->m_Module; });
                    DestroyFactoriesAndFreeModule(pSrc->m_Module, OldFactories);
                    pSrc->m_Module  = nullptr;
                    pSrc->m_bLoaded = false;
                    Out += "3. Unloaded the old DLL.\n";
                }
                else
                    Out += "3. (nothing to unload - not previously loaded).\n";

                // 4+5. Recompile + load
                if (!CompileAndLoadPlugin(*pSrc, Ctx.m_AvailableTypes))
                    return Out + std::format("4/5. ReloadPlugin: recompile of '{}' FAILED - graph left empty; fix the source and run ReloadPlugin again, or Load to restore the old graph against whatever's still available.\n{}", DirName, pSrc->m_CompileLog);
                Out += "4/5. Recompiled and loaded the new DLL.\n";

                // 6. Reload the saved graph
                const bool bLoadOk = LoadGraph(Path, Ctx.m_Nodes, Ctx.m_Links, Ctx.m_Sources, Ctx.m_AvailableTypes, Ctx.m_Spines, Ctx.m_Columns);
                Ctx.m_bDirty = true;
                Out += bLoadOk ? std::format("6. Reloaded '{}' - {} nodes, {} links.", Path, Ctx.m_Nodes.size(), Ctx.m_Links.size())
                                : std::format("6. ReloadPlugin: reload of '{}' failed - see log", Path);
                return Out;
            }
            xcmdline::parser::handle m_hDirName;
            xcmdline::parser::handle m_hPath;
        };

        //================================================================================================
        // Screenshot - lets an AI/CLI caller (or a human, over the pipe or the UI) see the graph
        // without a monitor. Can't capture synchronously from inside Query(): the actual GPU readback
        // only happens inside MainWindow.PageFlip(), called once per frame from E27_Example's main
        // loop tail, long after this Query() call returns - see xgpu_vulkan_window.cpp's own
        // Screenshot()/PageFlip() comments. So this only ARMS the request; E27_Example's own
        // PageFlip hook (right after PageFlip() returns, same frame) does the actual capture+
        // WriteScreenshotImage and clears the flag.
        //================================================================================================
        struct screenshot_query_cmd : xundo::query_command_base
        {
            screenshot_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "Screenshot", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Captures the current window as a PNG - written right after this frame finishes rendering. Usage: Screenshot [-Path filepath]"; }
            void RegisterArguments() noexcept override { m_hPath = m_Parser.addOption("Path", "Output image path - extension picks the format (.png/.bmp/.tga/.jpg); defaults to a fixed .png under CompiledPlugins", false, 1); }

            std::string Query() noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                std::string Path = "D:/LIONant/xGPU/source/Examples/E27_NodeOS/CompiledPlugins/screenshot.png";
                if (m_Parser.hasOption(m_hPath))
                {
                    auto PathArg = m_Parser.getOptionArgAs<std::string>(m_hPath, 0);
                    if (!std::holds_alternative<xerr>(PathArg)) Path = std::get<std::string>(PathArg);
                }
                Ctx.m_ScreenshotPath       = Path;
                Ctx.m_bScreenshotRequested = true;
                return std::format("Screenshot requested - will be written to '{}' right after this frame renders.", Path);
            }
            xcmdline::parser::handle m_hPath;
        };

        //================================================================================================
        // SetView/GetView - direct pan/zoom control over the graph canvas (canvas_view::m_PanX/m_PanY,
        // screen-space pixels; m_Zoom, clamped [0.3, 2.5] same as the mouse-wheel handler in
        // DrawGraphCanvas). Exists so a Screenshot can actually be aimed at a specific part of a large
        // graph without needing UI mouse-drag/wheel input at all - GetView first, to see where you
        // are, then SetView to move, then Screenshot. Plain float parsing via std::string rather than
        // trusting xcmdline::parser's own numeric template instantiations, which aren't exercised
        // anywhere else in this corpus.
        //================================================================================================
        static bool TryParseFloatArg(xcmdline::parser& Parser, xcmdline::parser::handle Handle, float& Out) noexcept
        {
            if (!Parser.hasOption(Handle)) return false;
            auto Arg = Parser.getOptionArgAs<std::string>(Handle, 0);
            if (std::holds_alternative<xerr>(Arg)) return false;
            try { Out = std::stof(std::get<std::string>(Arg)); return true; } catch (...) { return false; }
        }

        struct set_view_query_cmd : xundo::query_command_base
        {
            set_view_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "SetView", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Sets the graph canvas's pan/zoom directly - each argument optional, unset ones keep their current value. Usage: SetView [-PanX x] [-PanY y] [-Zoom z]"; }
            void RegisterArguments() noexcept override
            {
                m_hPanX = m_Parser.addOption("PanX", "Screen-space X pan offset in pixels", false, 1);
                m_hPanY = m_Parser.addOption("PanY", "Screen-space Y pan offset in pixels", false, 1);
                m_hZoom = m_Parser.addOption("Zoom", "Zoom factor, clamped to [0.3, 2.5]", false, 1);
            }

            std::string Query() noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                float V;
                if (TryParseFloatArg(m_Parser, m_hPanX, V)) Ctx.m_View.m_PanX = V;
                if (TryParseFloatArg(m_Parser, m_hPanY, V)) Ctx.m_View.m_PanY = V;
                if (TryParseFloatArg(m_Parser, m_hZoom, V)) Ctx.m_View.m_Zoom = std::clamp(V, 0.3f, 2.5f);
                return std::format("View: Pan=({:.1f}, {:.1f}) Zoom={:.2f}", Ctx.m_View.m_PanX, Ctx.m_View.m_PanY, Ctx.m_View.m_Zoom);
            }
            xcmdline::parser::handle m_hPanX, m_hPanY, m_hZoom;
        };

        struct get_view_query_cmd : xundo::query_command_base
        {
            get_view_query_cmd(xundo::system& System, void* pDataBase) noexcept : query_command_base(System, "GetView", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Returns the graph canvas's current pan/zoom. Usage: GetView"; }
            void RegisterArguments() noexcept override {}

            std::string Query() noexcept override
            {
                auto& V = get<node_os_command_context>().m_View;
                return std::format("Pan=({:.1f}, {:.1f}) Zoom={:.2f}", V.m_PanX, V.m_PanY, V.m_Zoom);
            }
        };
    }
}
