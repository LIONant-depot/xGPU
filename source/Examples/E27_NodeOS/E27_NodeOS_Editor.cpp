//-----------------------------------------------------------------------------------
//
// E27 - Node OS: the actual point of the RTCS/ai_programming research was never "fill out a form
// and get a manifest" (see E26) - it's a composable OS where wiring nodes together produces a real
// program that executes and does something. And critically: a node type must not require stopping
// the whole system and rebuilding it in Visual Studio to exist - it has to be creatable from
// *inside* the running tool.
//
// So this example proves the actual load-bearing claim: a node's behavior lives in its own .cpp
// file (source/Examples/E27_NodeOS/Plugins/<Name>/*.cpp), completely absent from this executable's own build -
// grep the CMakeLists.txt, it is not there. Pressing "Compile & Load" in the Node Library panel
// below shells out to the local MSVC toolchain (vcvarsall.bat + cl.exe /LD) right now, while this
// program is running, turns that .cpp into a DLL, and LoadLibrary's it - the resulting node type
// appears in the canvas's Add Node palette with zero CMake reconfigure and zero Visual Studio IDE
// involvement. Wire two such nodes together and press Execute: the host calls straight into code
// it was never compiled with, in dependency order, and shows the real result.
//
// The canvas itself is hand-rolled (plain ImDrawList calls), not a third-party node-editor library -
// the graph view is the single most important piece of a node-based system, and depending on someone
// else's library for it means never fully owning it. Its design (auto vertical stacking by order, no
// free dragging, orthogonal "highway" wire routing with per-side lane packing, a port's rendered side
// chosen by wire direction so no wire ever crosses over its own destination node, shape/color/fill
// visual encoding) is a direct port of _ai_programming/ai_programming/rslgraph-ui's own SVG canvas
// (apps/rslgraph-ui/src/canvas/{Canvas,NodeView,geometry}.tsx) - the original prototype's design for
// exactly this problem, translated from React+SVG to ImGui draw-list calls. rslgraph-ui itself never
// implemented node/link selection or deletion; this port adds both.
//
//-----------------------------------------------------------------------------------

// Core data structs (available_node_type/node_instance/node_topology/link_instance/column/spine/
// graph_header/plugin_compile_result/plugin_source_entry), the runtime log, and the whole
// plugin-compile pipeline (cl.exe shell-out, PCH cache, CompileAndLoadPlugin) - see
// Editor/NodeOS_Types.h. Included first: everything else in this file depends on it.
#include "Editor/NodeOS_Types.h"

// The property-row cluster (property_kind/property_row, ReadEnumAsInt/WriteEnumFromInt/
// ReadBoolProperty, ReflectedMemberToRow/ApplyRowToMember, HasSerializableProperties/HasAnyProperties,
// PushResolvedTypeDebugProperty/PushPinConnectedFlags, SerializeReflectedMembers/
// SerializePropertiesToString/ApplyPropertiesFromString/ReadBoolPropertyFromSnapshot) - see
// Editor/NodeOS_PropertySerialize.h. Moved this early specifically so DrawGraphCanvas, further below,
// no longer needs forward declarations for any of it.
#include "Editor/NodeOS_PropertySerialize.h"

// Whole-graph Save/Load (SaveGraphItems/LoadGraphItems templates, SaveGraph, LoadGraph) - see
// Editor/NodeOS_SaveLoad.h.
#include "Editor/NodeOS_SaveLoad.h"

// The graph interpreter (FindNodeById/IsRealDataPort/IsPullableNodeType/EnsureNodeRun/PullInputValue/
// SEH_CallExecute/RunOrdinaryNode/RunExecTarget/RunSpineRange/HasNodeBuilder/FindTheNodeBuilder/
// RunNodeBuilderBody/RunProgram) - see Editor/NodeOS_Interpreter.h. Forward-declares
// ResolveUnconnectedLiteral (real definition in Editor/NodeOS_CanvasSupport.h, header #7, included
// later) rather than depending on that header out of numbered order.
#include "Editor/NodeOS_Interpreter.h"

// The real C++ codegen backend (CppVar/EmitOrdinaryNode/EmitExecTarget/EmitSpineRange/GenerateCpp,
// GenerateNodePluginCpp/BuildNodeFromFunction, CompileAndRunGeneratedCpp) - see
// Editor/NodeOS_Codegen.h. Forward-declares FindMemberByName (real definition in
// Editor/NodeOS_CanvasSupport.h, header #7, included later), same pattern as
// NodeOS_Interpreter.h's ResolveUnconnectedLiteral forward declaration.
#include "Editor/NodeOS_Codegen.h"

// The first, already-inline "namespace commands" block: pure command-string builders (Base64Encode/
// Decode, JoinIds/SplitIds, FindSourceByDirName, ExpandOwnershipCascade, every Make* builder,
// commands::Run) - see Editor/NodeOS_CommandBuilders.h.
#include "Editor/NodeOS_CommandBuilders.h"

// Canvas-support cluster (mesh_preview_system, geo namespace, port_ref, GetInputValue/
// ResolveUnconnectedLiteral, EffectiveTypeName/ResolveNodeWildcardType, FindMemberByName,
// canvas_drag/canvas_selection/canvas_node_drag/canvas_spine_drag/canvas_delete_spine_confirm/
// canvas_view, ExecuteGraph) - see Editor/NodeOS_CanvasSupport.h. Provides the real definitions
// NodeOS_Interpreter.h/NodeOS_Codegen.h forward-declared (ResolveUnconnectedLiteral/FindMemberByName).
#include "Editor/NodeOS_CanvasSupport.h"

// The small ImGui leaf panels, merged into one file: DrawNodeLibraryPanel, DrawRuntimeLogPanel,
// DrawNodePropertiesEmptyState, DrawFunctionPinEditor, DrawNodePropertiesPanel - see
// Editor/NodeOS_UI_Panels.h.
#include "Editor/NodeOS_UI_Panels.h"

// DrawGraphCanvas alone (~1830 lines, the single largest function in the file) - see
// Editor/NodeOS_UI_Canvas.h.
#include "Editor/NodeOS_UI_Canvas.h"

// Command Console UI + named-pipe bridge (console_log_entry/ConsoleLogTokenize, palette/autocomplete
// scoring, ProcessConsoleCommand, DrawCommandConsolePanel, command_console_pipe_bridge,
// CommandConsolePipeThreadMain, PumpCommandConsolePipe) - see Editor/NodeOS_UI_CommandConsole.h.
#include "Editor/NodeOS_UI_CommandConsole.h"

// node_os_command_context + BackupSelection/RestoreSelection - shared by both "NodeOS/Edit/..." and
// "NodeOS/Query/..." commands - see Editor/NodeOS_CommandContext.h.
#include "Editor/NodeOS_CommandContext.h"

// Every "NodeOS/Edit/..." command struct (CreateNode, Connect, DeleteNodes, Select, ClearSelection,
// CreateSpine, DeleteSpine, SetSpinePosition, SetProperties, MoveNodesToSpine, ReorderNodes,
// DeleteLink, SetEndElseState, CreateOwnedPair) - see Editor/NodeOS_Commands_Edit.h.
#include "Editor/NodeOS_Commands_Edit.h"

namespace nodeos
{

    // property_kind/property_row and the whole property-row serialization cluster (ReadEnumAsInt/
    // WriteEnumFromInt/ReadBoolProperty/ReflectedMemberToRow/ApplyRowToMember/HasSerializableProperties/
    // HasAnyProperties/PushResolvedTypeDebugProperty/PushPinConnectedFlags/SerializeReflectedMembers/
    // SerializePropertiesToString/ApplyPropertiesFromString/ReadBoolPropertyFromSnapshot) now live in
    // Editor/NodeOS_PropertySerialize.h (included at the top of this file).



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
        // Base64Encode/Decode, JoinIds/SplitIds, FindSourceByDirName, WriteString/ReadString, and the
        // free Make*/Run helpers live EARLIER in this file (right after DestroyNodeInstance) - they
        // need to be visible to DrawGraphCanvas/DrawNodePropertiesPanel, which are defined before this
        // point, and ordinary single-pass C++ lookup means a name has to already be declared above the
        // point that uses it. The actual xundo::command_base-derived classes below stay here because
        // THEY need SerializePropertiesToString/ApplyPropertiesFromString, which aren't defined until
        // just above this point.

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

//------------------------------------------------------------------------------------------------

int E27_Example()
{
    // TEMPORARY, non-interactive self-test for the codegen backend - loads the real saved graph,
    // generates C++, compiles and runs it, and writes the result to a file. Deliberately BEFORE any
    // xgpu instance/device/window/ImGui exists - none of LoadGraph/GenerateCpp/
    // CompileAndRunGeneratedCpp need a GPU at all, and an earlier version of this hook placed after
    // that setup hit a heap-corruption crash on exit (a real, pre-existing xgpu/window teardown
    // fragility when the app exits before ever entering its normal render loop, not anything to do
    // with the codegen work itself - avoided entirely by never creating that stack in the first
    // place for this path). Only fires with -CodegenSelfTest on the command line, so ordinary
    // launches (no flag) are completely unaffected. Exists purely so this can be verified from
    // outside the app (no click-driven testing) - remove once the codegen pipeline itself is done
    // being validated.
    if (std::strstr(GetCommandLineA(), "-CodegenSelfTest"))
    {
        std::vector<nodeos::plugin_source_entry> Sources = nodeos::ScanPluginSources("D:/LIONant/xGPU/source/Examples/E27_NodeOS/Plugins");
        std::vector<nodeos::available_node_type> AvailableTypes;
        std::vector<nodeos::node_instance>       Nodes;
        std::vector<nodeos::link_instance>       Links;
        std::vector<nodeos::spine>  Spines  { nodeos::spine {  xresource::guid_generator::Instance64(), 0, true, nodeos::geo::TOP } };
        std::vector<nodeos::column> Columns { nodeos::column { xresource::guid_generator::Instance64(), 0, 0, true } };
        Spines.front().m_ColumnId = Columns.front().m_Id;

        std::string Report;
        if (!nodeos::LoadGraph("D:/LIONant/xGPU/source/Examples/E27_NodeOS/graph.txt", Nodes, Links, Sources, AvailableTypes, Spines, Columns))
            Report = "[self-test] LoadGraph FAILED\n";
        else if (nodeos::HasNodeBuilder(Nodes))
            Report = "[self-test] refused - graph contains a NodeBuilder node (node definition, not a program)\n";
        else
        {
            const std::string GeneratedSource = nodeos::GenerateCpp(Nodes, Links, Spines);
            const auto Result = nodeos::CompileAndRunGeneratedCpp(GeneratedSource);
            Report += "=== GENERATED SOURCE ===\n" + GeneratedSource + "\n";
            Report += std::format("=== COMPILE {} ===\n{}\n", Result.m_bCompileOk ? "OK" : "FAILED", Result.m_CompileLog);
            if (Result.m_bCompileOk)
                Report += std::format("=== RUN {} - OUTPUT ===\n{}\n", Result.m_bRanOk ? "OK" : "FAILED", Result.m_RunOutput);

            // TEMPORARY - the interpreter (RunProgram/RunSpineRange's "If" handling and GetInputValue's
            // literal fallback) is never exercised by codegen at all; running it here too, on the exact
            // same loaded Nodes/Links, proves the interpreter's own conditional-branch and
            // literal-value fixes independently rather than trusting they match codegen by inspection
            // alone. Harmless to run after codegen above - RunProgram only touches m_bHasRun/
            // m_CachedOutputs, which GenerateCpp/CompileAndRunGeneratedCpp never read.
            nodeos::literal_storage InterpScratch;
            nodeos::RunProgram(Nodes, Links, Spines, InterpScratch);
            Report += "=== INTERPRETER (Execute Graph) OUTPUT ===\n";
            for (auto& Line : nodeos::GetRuntimeLog()) Report += Line + "\n";
            for (auto& N : Nodes)
                if (N.m_pNode && !N.m_bHasRun && N.m_pNode->m_pFactory->getName() != "End")
                    Report += std::format("[not reached: {} #{:x}]\n", N.m_pNode->m_pFactory->getName(), N.m_Id & 0xffffff);
        }
        std::ofstream Out("D:/LIONant/xGPU/source/Examples/E27_NodeOS/CompiledPlugins/_codegen_selftest_report.txt");
        Out << Report;
        Out.close();
        for (auto& N : Nodes) nodeos::DestroyNodeInstance(N);

        // Bisected empirically (TerminateProcess checkpoints after every step above, one rebuild):
        // nothing in this self-test's own code corrupts the heap - every checkpoint up through here
        // is clean. The "not allocated by _aligned routines" Debug Error only appears during the
        // process's NORMAL exit teardown (global/static destructors, DLL_PROCESS_DETACH for plugin
        // DLLs loaded above) - a pre-existing fragility unrelated to codegen, most likely a Debug
        // host CRT heap disagreeing with a Release-built plugin DLL's CRT heap (plugins are compiled
        // by a separate Release-by-default tool - see xgpu_plugin_compiler_debug_release memory) once
        // that DLL is unloaded. Terminating here instead of falling through to that teardown sidesteps
        // it entirely for this self-test's own purpose (verifying the codegen pipeline itself).
        TerminateProcess(GetCurrentProcess(), 0);
    }

    // SCRATCH, temporary v1 verification for NodeBuilder - builds a tiny self-contained NodeBuilder
    // ("AddTwoGen": A,B:Float -> Sum:Float, body = one Math Expression node, no owned scope, no
    // separate Function) directly in C++ (no saved-file text-format guessing), triggers
    // BuildNodeFromFunction, then directly instantiates and Executes the freshly-published node type
    // to confirm it actually computes the right answer, not just "compiled". Also exercises the two
    // new graph-purpose checks (OnEvent+NodeBuilder mix, >1 NodeBuilder). Only fires with
    // -NodeBuilderSelfTest.
    if (std::strstr(GetCommandLineA(), "-NodeBuilderSelfTest"))
    {
        using namespace nodeos;
        std::vector<plugin_source_entry> Sources = ScanPluginSources("D:/LIONant/xGPU/source/Examples/E27_NodeOS/Plugins");
        std::vector<available_node_type> AvailableTypes;

        const auto GetFactory = [&](const char* DirName) -> xnode_os_node_factory*
        {
            for (auto& S : Sources) if (S.m_DirName == DirName) return EnsureLoadedAndGetType(S, AvailableTypes);
            return nullptr;
        };
        auto* pMathFactory    = GetFactory("MathExpression");
        auto* pBuilderFactory = GetFactory("NodeBuilder");
        auto* pEndFactory     = GetFactory("End");
        auto* pOnEventFactory = GetFactory("OnEvent");
        auto* pConstFactory   = GetFactory("Constant");
        auto* pPrintFactory   = GetFactory("Print");

        std::string Report;
        if (!pMathFactory || !pBuilderFactory || !pEndFactory || !pOnEventFactory || !pConstFactory || !pPrintFactory)
            Report = "[nodebuilder-selftest] failed to load one of MathExpression/NodeBuilder/End/OnEvent/Constant/Print\n";
        else
        {
            const auto SetStr = [](xnode_os_node* pNode, const char* pName, const std::string& Value)
            {
                auto* pM = FindMemberByName(pNode->getProperties(), pName);
                assert(pM);
                xproperty::any In{ Value }; xproperty::settings::context Ctx;
                (void)pM->TryWrite(pNode, In, Ctx);
            };
            const auto SetFloat = [](xnode_os_node* pNode, const char* pName, float Value)
            {
                auto* pM = FindMemberByName(pNode->getProperties(), pName);
                assert(pM);
                xproperty::any In{ Value }; xproperty::settings::context Ctx;
                (void)pM->TryWrite(pNode, In, Ctx);
            };

            // Two spines, two columns - the definition (Builder+body+End) lives in its own, NON-root
            // spine/column; the test rig (Constant/Constant/Print) lives in the ROOT spine/column.
            // This is the layout confirmed correct: visually separates "the node's own definition"
            // from "how it's being tested" the same way you'd keep test code in its own file - and
            // critically, RunProgram only ever positionally walks the ROOT spine (RunSpineRange
            // (RootSpineId, 0, INT_MAX, ...)), so Print (which has no Exec pin and only ever runs when
            // its own spine's walk reaches it - see print_node.cpp's own comment) MUST be in the root
            // spine or it silently never runs at all. Confirmed the hard way: putting the test rig in
            // a NEW (necessarily non-root) spine instead left Print permanently "not reached this
            // run" - RunNodeBuilderBody's own pull-triggered mirroring is spine-agnostic (it already
            // works cross-spine, same as any other cross-spine data link in this graph model), so the
            // fix is simply which of the two spines gets bIsRoot, not anything about the pull logic.
            std::vector<column> Columns
            { column { 10, 0, 11, true }  // root column (test rig)
            , column { 11, 10, 0, false } // definition's own column
            };
            std::vector<spine> Spines
            { spine { 1, 10, true,  0.0f } // root spine - test rig
            , spine { 2, 11, false, 0.0f } // definition's own spine
            };

            std::vector<node_instance> Nodes;
            std::vector<link_instance> Links;
            Nodes.push_back(CreateNodeInstance(1, pBuilderFactory, 0, 2)); // NodeBuilder "AddTwoGen" - definition spine
            Nodes.push_back(CreateNodeInstance(2, pMathFactory,    1, 2)); // body: A + B
            Nodes.push_back(CreateNodeInstance(3, pEndFactory,     2, 2)); // Builder's own owned End
            Nodes.push_back(CreateNodeInstance(4, pConstFactory,   0, 1)); // Constant A = 3 - root/test spine
            Nodes.push_back(CreateNodeInstance(5, pConstFactory,   1, 1)); // Constant B = 4
            Nodes.push_back(CreateNodeInstance(6, pPrintFactory,   2, 1)); // Print(Builder.Sum)

            Nodes[0].m_OwnedEndId = 3;
            SetStr(Nodes[0].m_pNode, "Name",        "AddTwoGen");
            SetStr(Nodes[0].m_pNode, "InputsSpec",  "A:Float:1:1|B:Float:1:1");
            SetStr(Nodes[0].m_pNode, "OutputsSpec", "Sum:Float:1:0");
            SetFloat(Nodes[3].m_pNode, "Value Float", 3.0f);
            SetFloat(Nodes[4].m_pNode, "Value Float", 4.0f);

            // Builder.getOutputs() = [Sum(ext,0), A(mirror,1), B(mirror,2), End(3)] - body reads its
            // parameters from the mirror outputs. Builder.getInputs() = [A(ext,0), B(ext,1), Sum
            // (mirror,2)] - body writes its result into the Sum mirror input. No Exec pin - see
            // node_builder_node.cpp's own top comment. The last three links cross spines - already-
            // proven-valid, same "world scope" mechanism any other cross-spine data link uses.
            Links.push_back({ .m_Id = 101, .m_SourceNode = 1, .m_SourceOutput = 1, .m_TargetNode = 2, .m_TargetInput = 0 }); // Builder.A -> Math.A
            Links.push_back({ .m_Id = 102, .m_SourceNode = 1, .m_SourceOutput = 2, .m_TargetNode = 2, .m_TargetInput = 1 }); // Builder.B -> Math.B
            Links.push_back({ .m_Id = 103, .m_SourceNode = 2, .m_SourceOutput = 0, .m_TargetNode = 1, .m_TargetInput = 2 }); // Math.Result -> Builder.Sum
            Links.push_back({ .m_Id = 104, .m_SourceNode = 4, .m_SourceOutput = 0, .m_TargetNode = 1, .m_TargetInput = 0 }); // ConstA -> Builder.A (external, cross-spine)
            Links.push_back({ .m_Id = 105, .m_SourceNode = 5, .m_SourceOutput = 0, .m_TargetNode = 1, .m_TargetInput = 1 }); // ConstB -> Builder.B (external, cross-spine)
            Links.push_back({ .m_Id = 106, .m_SourceNode = 1, .m_SourceOutput = 0, .m_TargetNode = 6, .m_TargetInput = 0 }); // Builder.Sum (external, cross-spine) -> Print

            // Saved as the actual example artifact - a real, loadable graph.txt-format file showing
            // the two-spine layout: definition on one side, a working test rig on the other, exactly
            // the state a user opening this file in the running editor would see.
            const std::string ExamplePath = "D:/LIONant/xGPU/source/Examples/E27_NodeOS/graph_nodebuilder_example.txt";
            Report += SaveGraph(ExamplePath, Nodes, Links, AvailableTypes, Spines, Columns)
                    ? std::format("[nodebuilder-selftest] saved example to '{}'\n", ExamplePath)
                    : "[nodebuilder-selftest] SaveGraph FAILED\n";

            Report += BuildNodeFromFunction(Nodes[0], Nodes, Links, Sources, AvailableTypes, Spines, Columns) + "\n";

            xnode_os_node_factory* pGenFactory = nullptr;
            for (auto& T : AvailableTypes) if (T.m_pFactory->getName() == "AddTwoGen") { pGenFactory = T.m_pFactory; break; }
            if (!pGenFactory)
                Report += "[nodebuilder-selftest] published type 'AddTwoGen' not found in AvailableTypes\n";
            else
            {
                xnode_os_node& Gen = pGenFactory->CreateNodeInstance();
                float A = 3.0f, B = 4.0f;
                void* Inputs[2]  = { &A, &B };
                void* Outputs[1] = { nullptr };
                Gen.Execute(Inputs, Outputs);
                const float Sum = Outputs[0] ? *static_cast<float*>(Outputs[0]) : -999.0f;
                Report += std::format("[nodebuilder-selftest] AddTwoGen(3, 4) = {} (expected 7)\n", Sum);
                Gen.FreeOutputs(Outputs);
                pGenFactory->DestroyNodeInstance(Gen);
            }

            // Interpreter TEST MODE, via RunProgram (NOT direct instantiate-and-Execute like above) -
            // proves RunNodeBuilderBody's pull-triggered mirror dance actually works end to end across
            // the two spines, AND (since Math Expression, positioned INSIDE the body range, gets
            // reached by the OUTER positional walk BEFORE Print's own pull of Builder.Sum can reach
            // it) exercises the reentrancy fix in RunOrdinaryNode - Math Expression's Execute() must
            // run exactly once, not twice.
            {
                literal_storage TestScratch;
                for (auto& N : Nodes) { N.m_bHasRun = false; N.m_LastError.clear(); N.m_CachedOutputs.clear(); }
                RunProgram(Nodes, Links, Spines, TestScratch);
                Report += "[nodebuilder-selftest] interpreter test-mode log:\n";
                for (auto& Line : GetRuntimeLog()) Report += "  " + Line + "\n";

                // The core promise, checked directly: BuildNode with the test rig STILL PRESENT must
                // exclude it entirely (it lives in a different spine entirely, structurally outside
                // the body range) - the generated .cpp must still be exactly the AddTwo logic, no
                // trace of Print/Constant.
                Report += "[nodebuilder-selftest] BuildNode with test rig present: " + BuildNodeFromFunction(Nodes[0], Nodes, Links, Sources, AvailableTypes, Spines, Columns) + "\n";
                {
                    std::ifstream GenFile("D:/LIONant/xGPU/source/Examples/E27_NodeOS/Plugins/AddTwoGen/AddTwoGen_node.cpp");
                    std::stringstream GenBuf; GenBuf << GenFile.rdbuf();
                    const std::string GenText = GenBuf.str();
                    Report += (GenText.find("Print") == std::string::npos && GenText.find("malloc(sizeof(float)) * 2") == std::string::npos)
                        ? "[nodebuilder-selftest] generated .cpp correctly excludes the test rig\n"
                        : "[nodebuilder-selftest] generated .cpp LEAKED test-rig content - BUG\n";
                }
            }

            // Graph-purpose validation check - BuildNode (publish time) still refuses a mixed graph.
            // RunProgram (test mode) does NOT refuse this - a stray OnEvent alongside a NodeBuilder is
            // harmless there (OnEvent is a no-op label either way) - only publishing needs a single,
            // unambiguous purpose.
            {
                Nodes.push_back(CreateNodeInstance(7, pOnEventFactory, 3, 1)); // stray OnEvent
                const std::string MixedResult = BuildNodeFromFunction(Nodes[0], Nodes, Links, Sources, AvailableTypes, Spines, Columns);
                Report += std::format("[nodebuilder-selftest] BuildNode with a stray OnEvent present: {}\n", MixedResult);

                DestroyNodeInstance(Nodes.back());
                Nodes.pop_back();
            }

            // Round-trip check: the example file just saved is the actual artifact a user would open -
            // load it back into FRESH containers (proves the on-disk format itself, not just the
            // in-memory objects above, carries everything NodeBuilder needs) and run BuildNode again
            // from there.
            {
                std::vector<node_instance>       RtNodes;
                std::vector<link_instance>       RtLinks;
                std::vector<spine>               RtSpines;
                std::vector<column>              RtColumns;
                std::vector<plugin_source_entry> RtSources = ScanPluginSources("D:/LIONant/xGPU/source/Examples/E27_NodeOS/Plugins");
                std::vector<available_node_type> RtAvailableTypes;
                if (!LoadGraph(ExamplePath, RtNodes, RtLinks, RtSources, RtAvailableTypes, RtSpines, RtColumns))
                    Report += "[nodebuilder-selftest] round-trip LoadGraph of the saved example FAILED\n";
                else
                {
                    node_instance* pRtBuilder = nullptr;
                    for (auto& N : RtNodes) if (N.m_pNode && N.m_pNode->m_pFactory->getName() == "NodeBuilder") { pRtBuilder = &N; break; }
                    Report += !pRtBuilder
                        ? "[nodebuilder-selftest] round-trip: no NodeBuilder node found after Load\n"
                        : "[nodebuilder-selftest] round-trip: " + BuildNodeFromFunction(*pRtBuilder, RtNodes, RtLinks, RtSources, RtAvailableTypes, RtSpines, RtColumns) + "\n";
                }
                for (auto& N : RtNodes) DestroyNodeInstance(N);
            }

            for (auto& N : Nodes) DestroyNodeInstance(N);
        }

        std::ofstream Out("D:/LIONant/xGPU/source/Examples/E27_NodeOS/CompiledPlugins/_nodebuilder_selftest_report.txt");
        Out << Report;
        Out.close();
        TerminateProcess(GetCurrentProcess(), 0);
    }

    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = false, .m_bEnableRenderDoc = false, .m_pLogErrorFunc = nodeos::Debugger, .m_pLogWarning = nodeos::Debugger }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err)
        return xgpu::getErrorInt(Err);

    xgpu::window MainWindow;
    if (auto Err = Device.Create(MainWindow, {}); Err)
        return xgpu::getErrorInt(Err);

    xgpu::tools::imgui::CreateInstance(MainWindow);

    // Overrides ImGui's own default dark theme's blue accent (Button/Header/FrameBg/Tab/CheckMark/
    // SliderGrab/ResizeGrip/ScrollbarGrab all default to a saturated blue) with neutral dark grays,
    // matching the rest of this editor's own Unity-inspired chrome (theme::* above) - a real style
    // EDIT, not a PushStyleColor scope, since this is meant to hold for the app's entire lifetime,
    // not one widget/frame. E27 is the only example this build actually runs (see main.cpp), so
    // there's no other example's own look to preserve by scoping this more narrowly.
    {
        ImGuiStyle& Style = ImGui::GetStyle();
        Style.Colors[ImGuiCol_Button]              = ImVec4(0.24f, 0.24f, 0.24f, 1.0f);
        Style.Colors[ImGuiCol_ButtonHovered]       = ImVec4(0.32f, 0.32f, 0.32f, 1.0f);
        Style.Colors[ImGuiCol_ButtonActive]        = ImVec4(0.40f, 0.40f, 0.40f, 1.0f);
        Style.Colors[ImGuiCol_FrameBg]             = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
        Style.Colors[ImGuiCol_FrameBgHovered]      = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
        Style.Colors[ImGuiCol_FrameBgActive]       = ImVec4(0.24f, 0.24f, 0.24f, 1.0f);
        // A combo box's own closed button uses FrameBg, but the dropdown LIST it opens is a
        // separate ImGui color (PopupBg) - left at ImGui's own default (a different near-black,
        // slightly-transparent shade) it made every open dropdown visibly mismatch every other edit
        // box's background. Pinned to the exact same opaque color as FrameBg so every edit
        // surface - closed or open - reads as one consistent background.
        Style.Colors[ImGuiCol_PopupBg]             = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
        Style.Colors[ImGuiCol_Header]              = ImVec4(0.24f, 0.24f, 0.24f, 1.0f);
        Style.Colors[ImGuiCol_HeaderHovered]       = ImVec4(0.32f, 0.32f, 0.32f, 1.0f);
        Style.Colors[ImGuiCol_HeaderActive]        = ImVec4(0.40f, 0.40f, 0.40f, 1.0f);
        Style.Colors[ImGuiCol_CheckMark]           = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
        Style.Colors[ImGuiCol_SliderGrab]          = ImVec4(0.45f, 0.45f, 0.45f, 1.0f);
        Style.Colors[ImGuiCol_SliderGrabActive]    = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
        Style.Colors[ImGuiCol_Tab]                 = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
        Style.Colors[ImGuiCol_TabHovered]          = ImVec4(0.32f, 0.32f, 0.32f, 1.0f);
        Style.Colors[ImGuiCol_TabActive]           = ImVec4(0.33f, 0.33f, 0.33f, 1.0f);
        Style.Colors[ImGuiCol_TabUnfocused]        = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
        Style.Colors[ImGuiCol_TabUnfocusedActive]  = ImVec4(0.24f, 0.24f, 0.24f, 1.0f);
        Style.Colors[ImGuiCol_ResizeGrip]          = ImVec4(0.35f, 0.35f, 0.35f, 0.5f);
        Style.Colors[ImGuiCol_ResizeGripHovered]   = ImVec4(0.45f, 0.45f, 0.45f, 0.7f);
        Style.Colors[ImGuiCol_ResizeGripActive]    = ImVec4(0.55f, 0.55f, 0.55f, 0.9f);
        Style.Colors[ImGuiCol_ScrollbarGrab]       = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
        Style.Colors[ImGuiCol_ScrollbarGrabHovered]= ImVec4(0.45f, 0.45f, 0.45f, 1.0f);
        Style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
    }

    // Auto-discovered, not hardcoded: every Plugins/<Folder>/*.cpp here becomes an Add Node menu entry
    // immediately, in its not-yet-compiled state - dropping a new plugin folder in is the entire
    // integration step for a new native node kind.
    std::vector<nodeos::plugin_source_entry> Sources = nodeos::ScanPluginSources("D:/LIONant/xGPU/source/Examples/E27_NodeOS/Plugins");
    std::vector<nodeos::available_node_type> AvailableTypes;
    std::vector<nodeos::node_instance>       Nodes;
    std::vector<nodeos::link_instance>       Links;

    // There is always exactly one root spine living in exactly one root column - every other spine/
    // column this session ever creates starts out attached next to one of the existing ones via
    // CreateSpine. m_Y seeds at geo::TOP, same starting point as before any spine was ever dragged.
    std::vector<nodeos::spine>  Spines  { nodeos::spine {  xresource::guid_generator::Instance64(), 0, true, nodeos::geo::TOP } };
    std::vector<nodeos::column> Columns { nodeos::column { xresource::guid_generator::Instance64(), 0, 0, true } };
    Spines.front().m_ColumnId = Columns.front().m_Id;

    nodeos::mesh_preview_system MeshPreview;
    if (!MeshPreview.Init(Device))
        return 1;

    nodeos::canvas_drag       Drag;
    nodeos::canvas_selection  Selection;
    nodeos::canvas_view       View;
    nodeos::canvas_node_drag  NodeDrag;
    nodeos::canvas_spine_drag SpineDrag;
    nodeos::canvas_delete_spine_confirm DeleteSpineConfirm;

    bool bDirty = false; // persists across frames - see the deferred-execute comment below
    char GraphPathBuffer[260] = "D:/LIONant/xGPU/source/Examples/E27_NodeOS/graph.txt";
    std::string GraphStatus;

    // Read-only - this is generated output ("do not hand-edit" is right there in the file's own
    // first line), not something the user edits back into the graph. SetText() only happens right
    // after a "Compile to C++" click; the widget otherwise just keeps showing whatever it last held.
    TextEditor GeneratedCodeEditor;
    GeneratedCodeEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
    GeneratedCodeEditor.SetReadOnly(true);
    GeneratedCodeEditor.SetText("// Click \"Compile to C++\" to generate source here.\n");

    // Owned here (not as a DrawCommandConsolePanel local static) so both the pipe server's own
    // per-frame pump (PumpCommandConsolePipe) AND get_log_query_cmd (reached through CmdContext,
    // just below) can reach the SAME visible log a UI-typed command uses - see all three call sites'
    // own comments for why. The TextEditor widget that actually RENDERS this is a DrawCommandConsolePanel
    // local static instead (nothing outside that function ever needs the widget itself, only this data).
    std::vector<nodeos::console_log_entry> ConsoleLog;

    // The screenshot_query_cmd/PageFlip-hook bridge - see screenshot_query_cmd's own comment for why
    // this can't just capture synchronously inside Query(). ScreenshotPixels/W/H are the actual
    // Screenshot() destination, filled in by MainWindow.PageFlip() itself once it's called below.
    bool                        bScreenshotRequested = false;
    std::string                  ScreenshotPath;
    std::vector<std::uint32_t>   ScreenshotPixels;
    int                           ScreenshotW = 0, ScreenshotH = 0;

    // Every graph mutation (add/delete node, connect, reorder, edit a property, change selection)
    // goes through this System - see the "Commands" sections above for why: it's the one entry point
    // with zero ImGui/xgpu dependency that a future headless runner or driver plugin could call
    // identically to how the ImGui code below calls it. bAutoLoadSave=false - a fresh undo stack each
    // run, since a stale on-disk history from a previous, differently-shaped graph would be more
    // confusing than useful for this example.
    nodeos::commands::node_os_command_context CmdContext{ Nodes, Links, Selection, Sources, AvailableTypes, bDirty, Spines, Columns, ConsoleLog, bScreenshotRequested, ScreenshotPath, View };
    xundo::system NodeOsUndo;
    if (auto Err = NodeOsUndo.Init("D:/LIONant/xGPU/source/Examples/E27_NodeOS/UndoHistory", false); !Err.empty())
        nodeos::Debugger(std::format("Node OS: xundo Init failed: {}", Err));
    nodeos::commands::create_node_cmd     CmdCreateNode(NodeOsUndo, &CmdContext);
    nodeos::commands::create_owned_pair_cmd CmdCreateOwnedPair(NodeOsUndo, &CmdContext);
    nodeos::commands::set_end_else_state_cmd CmdSetEndElseState(NodeOsUndo, &CmdContext);
    nodeos::commands::delete_nodes_cmd    CmdDeleteNodes(NodeOsUndo, &CmdContext);
    nodeos::commands::delete_link_cmd     CmdDeleteLink(NodeOsUndo, &CmdContext);
    nodeos::commands::connect_cmd         CmdConnect(NodeOsUndo, &CmdContext);
    nodeos::commands::reorder_nodes_cmd   CmdReorderNodes(NodeOsUndo, &CmdContext);
    nodeos::commands::move_nodes_to_spine_cmd CmdMoveNodesToSpine(NodeOsUndo, &CmdContext);
    nodeos::commands::set_properties_cmd  CmdSetProperties(NodeOsUndo, &CmdContext);
    nodeos::commands::select_cmd          CmdSelect(NodeOsUndo, &CmdContext);
    nodeos::commands::clear_selection_cmd CmdClearSelection(NodeOsUndo, &CmdContext);
    nodeos::commands::create_spine_cmd    CmdCreateSpine(NodeOsUndo, &CmdContext);
    nodeos::commands::delete_spine_cmd    CmdDeleteSpine(NodeOsUndo, &CmdContext);
    nodeos::commands::set_spine_position_cmd CmdSetSpinePosition(NodeOsUndo, &CmdContext);
    nodeos::commands::list_nodes_query_cmd CmdListNodes(NodeOsUndo, &CmdContext);
    nodeos::commands::get_log_query_cmd   CmdGetLog(NodeOsUndo, &CmdContext);
    nodeos::commands::load_graph_query_cmd CmdLoadGraph(NodeOsUndo, &CmdContext);
    nodeos::commands::save_graph_query_cmd CmdSaveGraph(NodeOsUndo, &CmdContext);
    nodeos::commands::get_node_properties_query_cmd CmdGetNodeProperties(NodeOsUndo, &CmdContext);
    nodeos::commands::get_node_info_query_cmd CmdGetNodeInfo(NodeOsUndo, &CmdContext);
    nodeos::commands::run_graph_query_cmd      CmdRunGraph(NodeOsUndo, &CmdContext);
    nodeos::commands::get_node_values_query_cmd CmdGetNodeValues(NodeOsUndo, &CmdContext);
    nodeos::commands::compile_to_cpp_query_cmd  CmdCompileToCpp(NodeOsUndo, &CmdContext);
    nodeos::commands::build_node_query_cmd     CmdBuildNode(NodeOsUndo, &CmdContext);
    nodeos::commands::clear_graph_query_cmd    CmdClearGraph(NodeOsUndo, &CmdContext);
    nodeos::commands::unload_plugin_query_cmd  CmdUnloadPlugin(NodeOsUndo, &CmdContext);
    nodeos::commands::rescan_plugins_query_cmd CmdRescanPlugins(NodeOsUndo, &CmdContext);
    nodeos::commands::reload_plugin_query_cmd  CmdReloadPlugin(NodeOsUndo, &CmdContext);
    nodeos::commands::screenshot_query_cmd     CmdScreenshot(NodeOsUndo, &CmdContext);
    nodeos::commands::set_view_query_cmd       CmdSetView(NodeOsUndo, &CmdContext);
    nodeos::commands::get_view_query_cmd       CmdGetView(NodeOsUndo, &CmdContext);

    // Central command router (xundo::history::Route, xundo_history.h) - addresses NodeOsUndo's
    // commands through one namespaced string ("NodeOS/Edit/<Cmd>" for mutations, "NodeOS/Query/<Cmd>"
    // for read-only introspection) instead of a caller needing this xundo::system reference directly.
    // The debugging/AI-facing entry point this whole mechanism exists for - see DrawCommandConsolePanel.
    xundo::history NodeOsHistory;
    NodeOsHistory.AddSystem("NodeOS", 1, NodeOsUndo);

    // NodeOSCLI.cpp's server half - one background thread, detached, for the app's whole lifetime.
    // Detached rather than joined at shutdown on purpose: this is a local dev/debug feature, and
    // ConnectNamedPipe blocks indefinitely with no client connected, so there's no clean way to wake
    // it for a graceful join without real cancellation plumbing that nothing here needs - the thread
    // simply dies with the process, like the plugin-compile worker threads elsewhere in this file.
    nodeos::command_console_pipe_bridge CommandConsolePipeBridge;
    std::thread(nodeos::CommandConsolePipeThreadMain, std::ref(CommandConsolePipeBridge)).detach();

    while (Instance.ProcessInputEvents())
    {
        if (xgpu::tools::imgui::BeginRendering(true))
            continue;

        nodeos::PumpCommandConsolePipe(CommandConsolePipeBridge, NodeOsHistory, ConsoleLog);

        // Ctrl+Z / Ctrl+Y (also Ctrl+Shift+Z for Redo) - guarded by WantTextInput so typing "z" into a
        // property text field never gets mistaken for an undo shortcut.
        if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Z) && !ImGui::GetIO().KeyShift) { NodeOsUndo.Undo(); bDirty = true; }
            else if (ImGui::IsKeyPressed(ImGuiKey_Y) || (ImGui::IsKeyPressed(ImGuiKey_Z) && ImGui::GetIO().KeyShift)) { NodeOsUndo.Redo(); bDirty = true; }
        }

        // Deferred to the TOP of the frame, before anything else touches MeshPreview: ExecuteGraph can
        // erase mesh_preview_system entries (RebuildIfMesh's null-value branch, e.g. when a link that
        // used to carry a mesh gets removed by a node/link deletion), which destroys the xgpu::texture
        // an ImGui::Image() call captured a raw pointer to. If ExecuteGraph ran AFTER DrawGraphCanvas
        // in the SAME frame that made the change, that pointer would already be sitting in this frame's
        // ImGui draw list, and Render() below would dereference it after it was freed - a real crash
        // reproduced by deleting a node with a live mesh flowing out of (or into) it. Running it here
        // instead means any erase happens before DrawPreviewSquare/ImGui::Image are ever called again,
        // so a pruned entry is simply never captured in the first place.
        if (bDirty)
        {
            nodeos::ExecuteGraph(Device, Nodes, Links, Spines, MeshPreview);
            bDirty = false;
        }

        MeshPreview.RenderAll(MainWindow);

        // A fresh compile, a new/inserted node, a new/removed connection, a deletion, or a property
        // edit all mark this dirty so the graph re-runs (at the top of the NEXT frame, per above) and
        // every mesh preview reflects it - no manual "Execute Graph" click required for the common
        // case; the button below remains for a manual force-rerun.
        nodeos::DrawNodeLibraryPanel(Sources, AvailableTypes, bDirty);
        nodeos::DrawGraphCanvas(Sources, AvailableTypes, Nodes, Links, MeshPreview, Drag, Selection, View, NodeDrag, SpineDrag, DeleteSpineConfirm, Spines, Columns, bDirty, NodeOsUndo);
        nodeos::DrawNodePropertiesPanel(Nodes, Selection.m_SelectedNodes, NodeOsUndo, Sources, AvailableTypes);
        nodeos::DrawRuntimeLogPanel();
        nodeos::DrawCommandConsolePanel(NodeOsHistory, ConsoleLog);

        ImGui::SetNextWindowPos(ImVec2(300, 620), ImGuiCond_FirstUseEver);
        // Passing an explicit empty callback rather than relying on Render()'s own defaulted one -
        // MSVC independently re-evaluates a defaulted decltype([](){}) template default argument at
        // each call site, producing two DIFFERENT closure types for the same call and a hard error.
        GeneratedCodeEditor.Render("Generated C++##codegen", ImVec2(600, 300), true, [](){});

        ImGui::SetNextWindowPos(ImVec2(1265, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(200, 80), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Run"))
        {
            if (ImGui::Button("Execute Graph"))
                bDirty = true; // same deferred path, not an immediate call - see the comment above

            // Generates real C++ from the current graph, compiles it into a genuinely standalone
            // .exe (NODE_SCRIPTING_DESIGN.md's stated end goal, as opposed to Execute Graph's own
            // in-editor interpreter), runs it, and reports the actual captured output - not just
            // "it compiled." Immediate, not deferred through bDirty, since codegen never touches
            // MeshPreview/GPU textures the way ExecuteGraph does.
            if (ImGui::Button("Compile to C++"))
            {
              // The graph already says what it is - dispatch automatically rather than requiring a
              // separate command/button for the NodeBuilder case.
              if (auto* pBuilder = nodeos::FindTheNodeBuilder(Nodes))
              {
                nodeos::GetRuntimeLog().clear();
                nodeos::GetRuntimeLog().push_back("[nodebuilder] " + nodeos::BuildNodeFromFunction(*pBuilder, Nodes, Links, Sources, AvailableTypes, Spines, Columns));
              }
              else
              {
                const std::string GeneratedSource = nodeos::GenerateCpp(Nodes, Links, Spines);
                GeneratedCodeEditor.SetText(GeneratedSource);
                const auto CodegenResult = nodeos::CompileAndRunGeneratedCpp(GeneratedSource);
                nodeos::GetRuntimeLog().clear();
                nodeos::GetRuntimeLog().push_back(std::format("[codegen] source: {}", CodegenResult.m_SourcePath));
                if (!CodegenResult.m_bCompileOk)
                {
                    nodeos::GetRuntimeLog().push_back("[codegen] COMPILE FAILED:");
                    nodeos::GetRuntimeLog().push_back(CodegenResult.m_CompileLog);
                }
                else
                {
                    nodeos::GetRuntimeLog().push_back("[codegen] compiled OK - actual program output:");
                    nodeos::GetRuntimeLog().push_back(CodegenResult.m_RunOutput);
                }
              }
            }

            ImGui::Separator();

            // Undo/Redo, plus a dropdown over the FULL history (not just one step at a time) - every
            // entry is the exact command string that was executed (the same one an AI agent driving
            // this through a future "command source" plugin would see/issue), so this doubles as a
            // plain-text audit trail of the session, not just an undo control.
            {
                const int UndoIndex = NodeOsUndo.GetUndoIndex();
                const std::size_t HistoryCount = NodeOsUndo.GetHistoryCount();

                ImGui::BeginDisabled(UndoIndex == 0);
                if (ImGui::Button("Undo")) { NodeOsUndo.Undo(); bDirty = true; }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(UndoIndex >= (int)HistoryCount);
                if (ImGui::Button("Redo")) { NodeOsUndo.Redo(); bDirty = true; }
                ImGui::EndDisabled();

                ImGui::SetNextItemWidth(-1);
                const std::string Preview = (UndoIndex > 0) ? NodeOsUndo.GetHistoryDisplayString((std::size_t)UndoIndex - 1) : std::string("(nothing to undo)");
                if (ImGui::BeginCombo("##History", Preview.c_str()))
                {
                    if (HistoryCount == 0)
                        ImGui::TextDisabled("No commands yet.");
                    for (std::size_t i = 0; i < HistoryCount; ++i)
                    {
                        // Selecting an entry jumps the WHOLE timeline to "everything through this
                        // command has been applied" - i.e. this command becomes the new top of the
                        // undo stack, matching what clicking a step in a history panel means in most
                        // editors (Photoshop/Word's undo dropdown, etc). Only top-level entries are
                        // selectable this way - a GROUP command (System.Execute(name, {sub-commands}),
                        // none of Node OS's own commands currently use one, but the tree rendering below
                        // supports it generically) is one atomic undo step, so its sub-commands are shown
                        // as an expandable tree underneath purely for visibility, never as their own
                        // jump targets.
                        const bool bApplied  = (int)i < UndoIndex; // still in effect vs already undone
                        const bool bIsCurrent = ((int)i == UndoIndex - 1);
                        if (!bApplied) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

                        const std::string Label = std::format("[{:03}] {}", i, NodeOsUndo.GetHistoryCommandString(i));
                        if (NodeOsUndo.IsHistoryGroup(i))
                        {
                            ImGui::PushID((int)i);
                            const bool bOpen = ImGui::TreeNodeEx(Label.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | (bIsCurrent ? ImGuiTreeNodeFlags_Selected : 0));
                            // OpenOnArrow means clicking the arrow toggles open/closed without also
                            // counting as "clicked" here - IsItemToggledOpen() tells the two apart, so
                            // expanding the tree to look at it never jumps the undo position by accident.
                            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) { NodeOsUndo.JumpTo((int)i + 1); bDirty = true; }
                            if (bOpen)
                            {
                                for (std::size_t j = 0; j < NodeOsUndo.GetHistorySubCommandCount(i); ++j)
                                    ImGui::BulletText("%s", NodeOsUndo.GetHistorySubCommandString(i, j).c_str());
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }
                        else if (ImGui::Selectable(Label.c_str(), bIsCurrent))
                        {
                            NodeOsUndo.JumpTo((int)i + 1);
                            bDirty = true;
                        }
                        if (!bApplied) ImGui::PopStyleColor();
                    }
                    ImGui::EndCombo();
                }
            }

            ImGui::Separator();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##GraphPath", GraphPathBuffer, sizeof(GraphPathBuffer));
            if (ImGui::Button("Save"))
                GraphStatus = nodeos::SaveGraph(GraphPathBuffer, Nodes, Links, AvailableTypes, Spines, Columns) ? "Saved." : "Save failed - see log.";
            ImGui::SameLine();
            if (ImGui::Button("Load"))
            {
                Selection.m_SelectedNodes.clear();
                Selection.m_SelectedLink = 0;
                Selection.m_SelectedGapSpineId = 0;
                Selection.m_SelectedGapIndex   = -1;
                GraphStatus = nodeos::LoadGraph(GraphPathBuffer, Nodes, Links, Sources, AvailableTypes, Spines, Columns) ? "Loaded." : "Load failed - see log.";
                bDirty = true; // re-run the freshly loaded graph, same deferred path as everything else
                // Load replaces Nodes/Links wholesale (not through commands), so any existing undo
                // history refers to node/link ids that may no longer mean anything in the new graph -
                // clear it rather than let Ctrl+Z do something confusing against unrelated state.
                NodeOsUndo.Reset();
            }
            if (!GraphStatus.empty())
                ImGui::TextDisabled("%s", GraphStatus.c_str());
        }
        ImGui::End();

        // Arms the capture - MUST be called before PageFlip() (see screenshot_query_cmd's own
        // comment: the actual GPU readback happens inside PageFlip/EndFrame, not here). This still
        // captures the FULL frame including everything drawn above, since nothing's been submitted
        // to the GPU yet at this point either way.
        if (bScreenshotRequested)
            MainWindow.Screenshot(ScreenshotPixels, ScreenshotW, ScreenshotH);

        xgpu::tools::imgui::Render();
        MainWindow.PageFlip();

        // ScreenshotPixels/W/H are only valid AFTER PageFlip() returns (see above) - this is the one
        // and only place that's true, so the actual TGA write has to happen right here, not inside
        // screenshot_query_cmd::Query() (which ran, at the earliest, at the top of THIS same frame,
        // long before this line).
        if (bScreenshotRequested)
        {
            nodeos::WriteScreenshotImage(ScreenshotPath, ScreenshotPixels, ScreenshotW, ScreenshotH);
            bScreenshotRequested = false;
        }
    }

    return 0;
}
