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

// Every "NodeOS/Query/..." command struct (Save, Load, BuildNode, CompileToCpp, GetNodeInfo,
// GetNodeProperties, GetNodeValues, RunGraph, ClearGraph, UnloadPlugin, ReloadPlugin, RescanPlugins,
// GetLog, ListNodes, Screenshot, SetView, GetView) - see Editor/NodeOS_Commands_Query.h. The last of
// the 13 split headers.
#include "Editor/NodeOS_Commands_Query.h"

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
