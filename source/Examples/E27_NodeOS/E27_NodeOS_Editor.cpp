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

namespace nodeos
{

    // The forward declarations that used to live here (SerializePropertiesToString/
    // ReadBoolPropertyFromSnapshot/HasSerializableProperties/SerializeReflectedMembers/
    // PushResolvedTypeDebugProperty/PushPinConnectedFlags/ReadEnumAsInt/WriteEnumFromInt/
    // ReadBoolProperty) are gone: Editor/NodeOS_PropertySerialize.h is now included ahead of
    // DrawGraphCanvas, so every one of these is already a real definition by this point.

    //------------------------------------------------------------------------------------------------
    static void DrawGraphCanvas(std::vector<plugin_source_entry>& Sources, std::vector<available_node_type>& AvailableTypes, std::vector<node_instance>& Nodes
                               , std::vector<link_instance>& Links, mesh_preview_system& MeshPreview
                               , canvas_drag& Drag, canvas_selection& Selection, canvas_view& View
                               , canvas_node_drag& NodeDrag, canvas_spine_drag& SpineDrag
                               , canvas_delete_spine_confirm& DeleteSpineConfirm
                               , std::vector<spine>& Spines, std::vector<column>& Columns
                               
                               , bool& bDirty, xundo::system& System)
    {
        // Lives for this whole draw call only - every pin-preview GetInputValue() call below that
        // resolves an inline literal gets its own stable slot in here (see literal_storage's own
        // comment for why a shared single slot would be wrong).
        literal_storage LiteralScratch;

        ImGui::SetNextWindowPos(ImVec2(440, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(820, 560), ImGuiCond_FirstUseEver);
        // No native scrollbar - left-drag pan + wheel zoom (below) are the only way to navigate a
        // graph taller than the window, so the two don't fight over what "scrolling" means.
        if (!ImGui::Begin("Graph", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            ImGui::End();
            return;
        }

        // Nothing to lay out, wire, pan or zoom yet - a single big "+" centered in the window is the
        // whole UI until the very first node exists. No pan/zoom state applies here at all (the canvas
        // "can not be moved" while empty, per design), so this bypasses the entire rest of the function.
        if (Nodes.empty())
        {
            ImGui::TextDisabled("Click the + to add your first node.");
            const ImVec2 Origin = ImGui::GetCursorScreenPos();
            const ImVec2 Avail  = ImGui::GetContentRegionAvail();
            const ImVec2 Center{ Origin.x + Avail.x * 0.5f, Origin.y + Avail.y * 0.5f };
            const float  Radius = 22.0f;
            ImDrawList*  pDraw  = ImGui::GetWindowDrawList();
            const ImVec2 HitMin{ Center.x - Radius, Center.y - Radius }, HitMax{ Center.x + Radius, Center.y + Radius };
            const bool   bHovered = ImGui::IsMouseHoveringRect(HitMin, HitMax);

            pDraw->AddCircleFilled(Center, Radius, bHovered ? IM_COL32(56, 130, 246, 255) : IM_COL32(30, 41, 59, 255));
            pDraw->AddCircle(Center, Radius, IM_COL32(100, 116, 139, 255), 0, 2.0f);
            const float Arm = Radius * 0.45f;
            pDraw->AddLine({ Center.x - Arm, Center.y }, { Center.x + Arm, Center.y }, IM_COL32(226, 232, 240, 255), 3.0f);
            pDraw->AddLine({ Center.x, Center.y - Arm }, { Center.x, Center.y + Arm }, IM_COL32(226, 232, 240, 255), 3.0f);

            ImGui::SetCursorScreenPos(HitMin);
            ImGui::InvisibleButton("empty_add", ImVec2(Radius * 2, Radius * 2));
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                ImGui::OpenPopup("NodeOS_AddNodePopup");
            if (ImGui::BeginPopup("NodeOS_AddNodePopup"))
            {
                if (Sources.empty())
                    ImGui::TextDisabled("No plugin sources found under Plugins/.");
                for (auto& Src : Sources)
                    if (ImGui::MenuItem(Src.m_DisplayName.c_str()))
                    {
                        // Lazy compile: the very first placement of this type compiles+loads it right
                        // now (Sources is populated from a folder scan at startup, not from a manual
                        // "Compile & Load" click) - every later placement just reuses the loaded type.
                        if (auto* pType = EnsureLoadedAndGetType(Src, AvailableTypes))
                            commands::Run(System, commands::BuildCreateNodeCommand(Sources, AvailableTypes, Src, pType, commands::node_placement_kind::Append, 0));
                    }
                ImGui::EndPopup();
            }
            ImGui::End();
            return;
        }

        ImGui::TextDisabled("Click a + to add/insert a node | drag pin-to-pin to wire | drag a node onto a + to move it | right-drag empty space to pan, wheel to zoom | Delete to remove");
        const float AvailWidth = ImGui::GetContentRegionAvail().x;

        auto FindNode = [&](std::uint64_t Id) -> node_instance* { auto It = std::find_if(Nodes.begin(), Nodes.end(), [&](auto& N) { return N.m_Id == Id; }); return It == Nodes.end() ? nullptr : &*It; };
        auto DescOf   = [&](node_instance* pN) -> const xnode_os_node* { return pN ? pN->m_pNode : nullptr; };
        // EffectiveTypeName(Id, pDesc, RawType) - see the top-level definition above RowHeight for why
        // this needs to be a free function rather than a local lambda (RowHeight/NodeHeight need it too,
        // and can't see anything declared in here).

        auto FindColumn = [&](std::uint64_t Id) -> column* { for (auto& Co : Columns) if (Co.m_Id == Id) return &Co; return nullptr; };
        std::unordered_map<std::uint64_t, std::uint64_t> ColumnOfSpine;
        for (auto& S : Spines) ColumnOfSpine[S.m_Id] = S.m_ColumnId;
        auto ColumnOfNode = [&](std::uint64_t NodeId) -> std::uint64_t { auto* pN = FindNode(NodeId); return pN ? ColumnOfSpine[pN->m_SpineId] : 0; };

        // ---- Pass A: per-spine relative layout - each spine's own dense node order + sizes, Y
        // relative to that spine's own local origin (not yet placed in world/column space). Spine
        // (pure up/down connectivity, blind to highways) vs Column (the horizontal container that owns
        // them) is a deliberate split - see this file's own design-conversation history. ----
        struct spine_layout
        {
            std::vector<std::uint64_t> m_Order;               // this spine's own dense node order
            std::vector<float>          m_RelY, m_W, m_H;       // parallel to m_Order
            float                        m_RelBottom  = geo::NODE_GAP; // relative Y of the trailing gap
            float                        m_WidestNode = 120.0f;
        };
        std::unordered_map<std::uint64_t, spine_layout> SpineLayout;
        for (auto& S : Spines)
        {
            spine_layout SL;
            std::vector<std::uint64_t> SpineNodeIds;
            for (auto& N : Nodes) if (N.m_SpineId == S.m_Id) SpineNodeIds.push_back(N.m_Id);
            std::sort(SpineNodeIds.begin(), SpineNodeIds.end(), [&](std::uint64_t A, std::uint64_t B) { return FindNode(A)->m_Order < FindNode(B)->m_Order; });

            // A leading NODE_GAP (instead of starting flush at relative 0) reserves room for the
            // "insert before the first node" spine marker below, so it sits in a real gap identical in
            // size to every between-node gap rather than being crammed against the spine's own origin.
            float CursorY = geo::NODE_GAP;
            for (auto Id : SpineNodeIds)
            {
                auto* pDesc = DescOf(FindNode(Id));
                if (!pDesc) continue;
                const float W = NodeWidth(pDesc, Id, Nodes, Links), H = NodeHeight(pDesc, Id, Nodes, Links);
                SL.m_WidestNode = std::max(SL.m_WidestNode, W);
                SL.m_Order.push_back(Id); SL.m_RelY.push_back(CursorY); SL.m_W.push_back(W); SL.m_H.push_back(H);
                CursorY += H + geo::NODE_GAP;
            }
            SL.m_RelBottom = CursorY;
            SpineLayout.emplace(S.m_Id, std::move(SL));
        }

        // Relative Y of gap GapIndex (0 = before everything, m_Order.size() = after everything) within
        // a spine's own local layout - the exact formula every DrawInsertMarker call site already used
        // for the single-spine case, factored out so both the anchor walk (Pass B) and the marker-
        // drawing loop further below can share it. An empty spine's own single placeholder slot sits at
        // the same relative Y its first real node would (CursorY's own starting value above).
        auto GapRelY = [&](const spine_layout& SL, int GapIndex) -> float
        {
            if (SL.m_Order.empty()) return geo::NODE_GAP;
            if (GapIndex <= 0) return SL.m_RelY.front() - geo::NODE_GAP * 0.5f;
            if (GapIndex >= (int)SL.m_Order.size()) return SL.m_RelY.back() + SL.m_H.back() + geo::NODE_GAP * 0.5f;
            return SL.m_RelY[GapIndex - 1] + SL.m_H[GapIndex - 1] + geo::NODE_GAP * 0.5f;
        };

        // ---- Pass B: per-spine absolute Y. A spine's own m_Y IS its absolute world Y directly - no
        // derivation needed, root included (it just starts seeded at geo::TOP, like any other spine
        // starts wherever it was dropped). ----
        std::unordered_map<std::uint64_t, float> SpineAbsY;
        for (auto& S : Spines) SpineAbsY[S.m_Id] = S.m_Y;

        // Every node, in no particular meaningful order - only used where "for every node" is all that
        // matters (the render loop, the drag-to-connect port hit-test), never for relative ordering.
        std::vector<std::uint64_t> Order;
        for (auto& S : Spines) { auto& SL = SpineLayout[S.m_Id]; Order.insert(Order.end(), SL.m_Order.begin(), SL.m_Order.end()); }

        struct row_layout { std::uint64_t m_NodeId; float m_X, m_Y, m_W, m_H; };
        std::vector<row_layout> Layout;
        for (auto& S : Spines)
        {
            auto& SL = SpineLayout[S.m_Id];
            for (std::size_t i = 0; i < SL.m_Order.size(); ++i)
                Layout.push_back({ SL.m_Order[i], 0.0f, SpineAbsY[S.m_Id] + SL.m_RelY[i], SL.m_W[i], SL.m_H[i] });
        }
        auto FindRow = [&](std::uint64_t Id) -> row_layout* { auto It = std::find_if(Layout.begin(), Layout.end(), [&](auto& R) { return R.m_NodeId == Id; }); return It == Layout.end() ? nullptr : &*It; };

        // Is ColId's column to the right of OfColId's, walking the column chain structurally (never by
        // comparing X positions - those aren't known yet this early, and don't need to be: the chain's
        // own Left/Right links already say which way is which).
        auto IsColumnRightOf = [&](std::uint64_t ColId, std::uint64_t OfColId) -> bool
        {
            for (auto* pCo = FindColumn(OfColId); pCo && pCo->m_RightId; pCo = FindColumn(pCo->m_RightId))
                if (pCo->m_RightId == ColId) return true;
            return false;
        };

        // Cross-column connection ownership (the user's own spec): draw the direct line from source to
        // target and look at its diagonal direction - "down-right" or "up-left" (the horizontal and
        // vertical steps agree in sign) means the SOURCE's own column carries the vertical run;
        // "up-right" or "down-left" (they disagree) means the TARGET's column does. Same column (even
        // a different spine sharing it) is unambiguous - always that one column, no diagonal to reason
        // about.
        auto OwnerColumnOf = [&](const link_instance& L) -> std::uint64_t
        {
            const auto SrcCol = ColumnOfNode(L.m_SourceNode), DstCol = ColumnOfNode(L.m_TargetNode);
            if (SrcCol == DstCol) return SrcCol;
            auto* pSrcRow = FindRow(L.m_SourceNode); auto* pDstRow = FindRow(L.m_TargetNode);
            if (!pSrcRow || !pDstRow) return SrcCol;
            const bool bRight = IsColumnRightOf(DstCol, SrcCol);
            const bool bDown  = pDstRow->m_Y >= pSrcRow->m_Y;
            return (bRight == bDown) ? SrcCol : DstCol;
        };

        float TotalH = 0.0f;
        for (auto& S : Spines) TotalH = std::max(TotalH, SpineAbsY[S.m_Id] + SpineLayout[S.m_Id].m_RelBottom);
        TotalH += 20.0f;

        // Insert a new node instance at stacking position GapIndex within SpineId (0 = before
        // everything in that spine, that spine's own node count = after everything) - renumbers every
        // node's m_Order (spine-local, not global any more) to its dense index in the resulting stack
        // rather than doing arithmetic on the existing m_Order values, since deleting a node can leave
        // those with gaps. Takes the plugin SOURCE, not a type descriptor directly, so the very first
        // placement of a not-yet-compiled type can still compile+load it lazily.
        auto InsertNodeAt = [&](std::uint64_t SpineId, int GapIndex, plugin_source_entry& Src)
        {
            auto* pType = EnsureLoadedAndGetType(Src, AvailableTypes);
            if (!pType) return;
            auto& SL = SpineLayout[SpineId];
            if (SL.m_Order.empty()) { commands::Run(System, commands::BuildCreateNodeCommand(Sources, AvailableTypes, Src, pType, commands::node_placement_kind::InSpine, SpineId)); return; }

            const int Clamped = std::clamp(GapIndex, 0, (int)SL.m_Order.size());

            // Growth-blocking is a UI-LAYER GATE, never inside create_node_cmd::Redo() (which stays
            // 100% ImGui-free/headless-safe) - appending past the bottom must not be allowed to grow
            // into a sibling spine sharing this column. Measured via a throwaway instance, since a
            // not-yet-created node's own height depends on its real port list, not just its type; for
            // now, a collision here just silently rejects the append (same as an unresolved drag-drop)
            // - "fracturing" a spine to make room is deliberately left for later.
            if (Clamped >= (int)SL.m_Order.size())
            {
                auto& TempInstance = pType->CreateNodeInstance();
                // Sentinel id 0: this throwaway instance owns no real links, so every pin must size
                // as "disconnected" - Instance64() never mints 0, so no real link can match it.
                const float NewBottom = SpineAbsY[SpineId] + SL.m_RelBottom + NodeHeight(&TempInstance, 0, Nodes, Links);
                pType->DestroyNodeInstance(TempInstance);
                for (auto& S2 : Spines)
                {
                    if (S2.m_Id == SpineId || S2.m_ColumnId != ColumnOfSpine[SpineId]) continue;
                    auto& SL2 = SpineLayout[S2.m_Id];
                    if (SpineAbsY[SpineId] <= SpineAbsY[S2.m_Id] + SL2.m_RelBottom && SpineAbsY[S2.m_Id] <= NewBottom) return;
                }
            }

            // Addressed relative to whichever EXISTING node currently sits at this gap - see
            // create_node_cmd's own comment for why (node ids are already known/observable, an
            // invented "gap id" would need its own discovery step).
            const std::string Cmd = (Clamped < (int)SL.m_Order.size())
                ? commands::BuildCreateNodeCommand(Sources, AvailableTypes, Src, pType, commands::node_placement_kind::Before, SL.m_Order[Clamped])
                : commands::BuildCreateNodeCommand(Sources, AvailableTypes, Src, pType, commands::node_placement_kind::After, SL.m_Order.back());
            commands::Run(System, Cmd);
        };

        // Move an already-existing set of nodes (a drag-and-drop reorder or, now, a drag onto a
        // DIFFERENT spine's own marker) to stacking position GapIndex WITHIN SpineId. Same-spine drop:
        // a pure dense-renumber reorder (ReorderNodes), unchanged from before spines existed.
        // Cross-spine drop: issues MoveNodesToSpine instead, addressed the same way InsertNodeAt
        // addresses a new node (-After/-Before an existing node, or -InSpine for an empty target).
        auto MoveNodesTo = [&](std::uint64_t SpineId, const std::vector<std::uint64_t>& MovingIds, int GapIndex)
        {
            if (MovingIds.empty()) return;
            const bool bAllAlreadyHere = std::all_of(MovingIds.begin(), MovingIds.end(), [&](std::uint64_t Id) { auto* pN = FindNode(Id); return pN && pN->m_SpineId == SpineId; });
            auto& SL = SpineLayout[SpineId];

            if (bAllAlreadyHere)
            {
                std::vector<std::uint64_t> MovingInOrder, Remaining;
                for (auto Id : SL.m_Order)
                {
                    if (std::find(MovingIds.begin(), MovingIds.end(), Id) != MovingIds.end()) MovingInOrder.push_back(Id);
                    else Remaining.push_back(Id);
                }
                if (MovingInOrder.empty()) return;
                int Adjust = 0;
                for (int i = 0; i < GapIndex && i < (int)SL.m_Order.size(); ++i)
                    if (std::find(MovingIds.begin(), MovingIds.end(), SL.m_Order[i]) != MovingIds.end()) ++Adjust;
                const int NewGapIndex = std::clamp(GapIndex - Adjust, 0, (int)Remaining.size());
                Remaining.insert(Remaining.begin() + NewGapIndex, MovingInOrder.begin(), MovingInOrder.end());
                commands::Run(System, commands::MakeReorderNodes(Remaining));
                return;
            }

            // Cross-spine move: the same growth-blocking gate InsertNodeAt uses, checked only for an
            // append (past the end) - inserting into the middle is left unguarded for now, same
            // limitation InsertNodeAt already accepts. These are real (not throwaway) instances, so
            // their height is already known without needing to create/destroy a temporary one.
            const int Clamped = std::clamp(GapIndex, 0, (int)SL.m_Order.size());
            if (Clamped >= (int)SL.m_Order.size())
            {
                // A sibling that this exact move is about to drain completely (e.g. merging a whole
                // spine into another one sitting in the same column) isn't a real obstacle - by the time
                // the append actually lands there, it'll be gone.
                auto WouldBecomeEmpty = [&](std::uint64_t OtherSpineId)
                {
                    bool bHasAny = false;
                    for (auto& N : Nodes)
                        if (N.m_SpineId == OtherSpineId)
                        {
                            bHasAny = true;
                            if (std::find(MovingIds.begin(), MovingIds.end(), N.m_Id) == MovingIds.end()) return false;
                        }
                    return bHasAny;
                };
                float AddedHeight = 0.0f;
                for (auto Id : MovingIds) { auto* pN = FindNode(Id); if (pN && pN->m_pNode) AddedHeight += NodeHeight(pN->m_pNode, Id, Nodes, Links) + geo::NODE_GAP; }
                const float NewBottom = SpineAbsY[SpineId] + SL.m_RelBottom + AddedHeight;
                for (auto& S2 : Spines)
                {
                    if (S2.m_Id == SpineId || S2.m_ColumnId != ColumnOfSpine[SpineId] || WouldBecomeEmpty(S2.m_Id)) continue;
                    auto& SL2 = SpineLayout[S2.m_Id];
                    if (SpineAbsY[SpineId] <= SpineAbsY[S2.m_Id] + SL2.m_RelBottom && SpineAbsY[S2.m_Id] <= NewBottom) return;
                }
            }

            const std::string Cmd = SL.m_Order.empty() ? commands::MakeMoveNodesToSpineIn(MovingIds, SpineId)
                : (Clamped < (int)SL.m_Order.size() ? commands::MakeMoveNodesToSpineBefore(MovingIds, SL.m_Order[Clamped])
                                                     : commands::MakeMoveNodesToSpineAfter(MovingIds, SL.m_Order.back()));
            commands::Run(System, Cmd);
        };

        // ---- per-port side (L/R): chosen by wire direction, so a wire never crosses over its own
        // destination node - the source's output and the target's input both take the side that
        // matches whether the wire travels down (R) or up (L) in absolute world space. A link's
        // highway side is a pure per-link fact (which way it travels) - computing it fresh wherever
        // needed, rather than caching it keyed only by pin, is what fixes the "a pin with wires going
        // both up and down only ever renders on one side" bug. Compared by each node's own ABSOLUTE Y
        // (not a per-spine stacking index) specifically so this stays correct for a link between two
        // DIFFERENT spines that share a column - the highway/lane packing belongs to the column, not
        // the spine, and connect_cmd only forbids a link from leaving its column, not its spine.
        auto LinkSide = [&](const link_instance& L) -> char
        {
            auto* pSrcRow = FindRow(L.m_SourceNode); auto* pDstRow = FindRow(L.m_TargetNode);
            if (!pSrcRow || !pDstRow) return 'R';
            return (pDstRow->m_Y >= pSrcRow->m_Y) ? 'R' : 'L';
        };

        // Same column: the ORIGINAL rule above - both ends share one side, chosen purely by up/down
        // travel. Cross column: each end's pin instead renders on whichever of its own two edges faces
        // the OTHER column (so neither end ever has to route back around its own node to reach the
        // highway) - and the rail (within whichever column OwnerColumnOf resolves to) sits on that
        // same owning end's own facing side, so the vertical run starts already pointed the right way.
        auto LinkSides = [&](const link_instance& L, char& OutSourceSide, char& OutTargetSide, char& OutRailSide)
        {
            const auto SrcCol = ColumnOfNode(L.m_SourceNode), DstCol = ColumnOfNode(L.m_TargetNode);
            if (SrcCol == DstCol) { OutSourceSide = OutTargetSide = OutRailSide = LinkSide(L); return; }
            const bool bTargetRight = IsColumnRightOf(DstCol, SrcCol);
            OutSourceSide = bTargetRight ? 'R' : 'L';
            OutTargetSide = bTargetRight ? 'L' : 'R';
            OutRailSide   = (OwnerColumnOf(L) == SrcCol) ? OutSourceSide : OutTargetSide;
        };

        // Which side(s) a given pin actually needs a glyph rendered on - a set, not a single side, so a
        // pin used by links going in different directions (up vs down, or toward different columns)
        // gets one glyph per side actually in use.
        std::unordered_map<std::uint64_t, std::set<char>> PortSides;
        for (auto& Link : Links)
        {
            char SourceSide = 'R', TargetSide = 'R', RailSide = 'R';
            LinkSides(Link, SourceSide, TargetSide, RailSide);
            PortSides[OutPinOf(Link.m_SourceNode, Link.m_SourceOutput)].insert(SourceSide);
            PortSides[InPinOf(Link.m_TargetNode, Link.m_TargetInput)].insert(TargetSide);
        }
        // An unconnected pin defaults to the conventional side for its direction - input on the left,
        // output on the right - same as every other node-graph editor. A connected pin instead renders
        // wherever its actual link(s) go (PortSides above), which may not be the default side at all.
        auto SidesOf = [&](std::uint64_t PinId, bool bIsOutput) -> std::set<char>
        {
            auto It = PortSides.find(PinId);
            return It == PortSides.end() ? std::set<char>{ bIsOutput ? 'R' : 'L' } : It->second;
        };

        // Takes the side explicitly now - a port can have two valid anchor points (one per side it's
        // rendered on), so "the" anchor no longer makes sense without saying which one.
        auto PortAnchor = [&](std::uint64_t NodeId, const port_ref& P, char S) -> ImVec2
        {
            auto* pRow = FindRow(NodeId); auto* pDesc = DescOf(FindNode(NodeId));
            if (!pRow || !pDesc) return {};
            // An End marker has no body/rows at all - its pin (never drawn as a glyph) anchors
            // straight to the title bar's own edge instead.
            if (IsEndMarkerType(pDesc))
                return { (S == 'L') ? pRow->m_X : pRow->m_X + pRow->m_W, pRow->m_Y + geo::HEADER_H * 0.5f };
            float Y = pRow->m_Y + geo::HEADER_H + PreviewAreaHeight(pDesc);
            bool bHavePrevQ = false, bPrevQLocal = false;
            for (auto& Q : FlatPorts(pDesc))
            {
                // Same SECTION_GAP insertion as the draw loop (see LocalSectionGapTotal) - the anchor
                // walk has to reproduce every bit of the draw loop's own RowY accumulation, or a wire
                // ends up terminating above/below the glyph it's actually meant to touch, worse the
                // further down the port list the target sits.
                if (bHavePrevQ && Q.m_pDesc->m_bLocalScope && !bPrevQLocal) Y += geo::SECTION_GAP;
                bPrevQLocal = Q.m_pDesc->m_bLocalScope;
                bHavePrevQ = true;
                // Half of ROW_H specifically, matching the drawing loop's own CenterY (RowY +
                // ROW_H*0.5) - NOT half of RowHeight(), which also counts the value-line space below
                // the glyph and would anchor wires visibly below the actual drawn pin.
                if (Q.m_bIsOutput == P.m_bIsOutput && Q.m_Index == P.m_Index) { Y += geo::ROW_H * 0.5f; break; }
                Y += RowHeight(Q, NodeId, Links, EffectiveTypeName(NodeId, pDesc, Q.m_pDesc->m_pTypeName, Nodes, Links));
            }
            return { (S == 'L') ? pRow->m_X : pRow->m_X + pRow->m_W, Y };
        };

        // ---- lane packing: greedy interval partitioning, NOT rslgraph-ui's own laneOf (which is just
        // a stateless per-side counter - order of appearance, nothing more; verified directly in
        // Canvas.tsx). Sorting by Y-span length before assigning means a short/local hop always gets
        // first pick of the innermost lane, and only actually claims a new lane when it truly overlaps
        // something already there. Pooled PER COLUMN, keyed by OwnerColumnOf(Link) - a connection can
        // now span any two columns, but its vertical run only ever lives in ONE of them at a time (see
        // OwnerColumnOf's own comment) - no two columns' highways ever interact.
        //
        // Two spans that share the same SOURCE pin (one output fanning out to several inputs, e.g. a
        // Cube's Mesh feeding both Export Mesh and Inspect Mesh) are never considered to "overlap"
        // each other, even when their Y-ranges do - they're the same trunk visually, just splitting
        // off toward different destinations, not independent wires that happen to cross. Without this,
        // every fan-out target claimed its own parallel lane for the full length of the shared run,
        // which is exactly the "too many highways" the fan-out case was producing.
        struct link_lane_interval { float m_Lo, m_Hi; std::uint64_t m_SourcePin; };
        std::unordered_map<std::uint64_t, std::vector<std::vector<link_lane_interval>>> LaneIntervalsBySide[2]; // [0]=L, [1]=R, keyed by column id
        std::unordered_map<std::uint64_t, int> LaneOfLink;
        {
            struct link_span { std::uint64_t m_LinkId, m_ColumnId; int m_Side; float m_Lo, m_Hi; std::uint64_t m_SourcePin; };
            std::vector<link_span> Spans;
            for (auto& Link : Links)
            {
                auto* pSrcDesc = DescOf(FindNode(Link.m_SourceNode)); auto* pDstDesc = DescOf(FindNode(Link.m_TargetNode));
                if (!pSrcDesc || !pDstDesc) continue;
                const auto SrcOutputs = pSrcDesc->getOutputs(); const auto DstInputs = pDstDesc->getInputs();
                if (Link.m_SourceOutput >= (int)SrcOutputs.size() || Link.m_TargetInput >= (int)DstInputs.size()) continue;
                const port_ref OutP{ true, Link.m_SourceOutput, &SrcOutputs[Link.m_SourceOutput] };
                const port_ref InP { false, Link.m_TargetInput,  &DstInputs[Link.m_TargetInput] };
                char SourceSide = 'R', TargetSide = 'R', RailSide = 'R';
                LinkSides(Link, SourceSide, TargetSide, RailSide);
                const float FromY = PortAnchor(Link.m_SourceNode, OutP, SourceSide).y, ToY = PortAnchor(Link.m_TargetNode, InP, TargetSide).y;
                const int Side2 = (RailSide == 'R') ? 1 : 0;
                Spans.push_back({ Link.m_Id, OwnerColumnOf(Link), Side2, std::min(FromY, ToY), std::max(FromY, ToY), PinOf(OutP, Link.m_SourceNode) });
            }
            // Fan-out from one source pin (e.g. a Cube's Mesh feeding both Export Mesh and Inspect
            // Mesh) is one trunk, not several independent wires - a lane it claims has to be reserved
            // across the FULL combined range of every branch, not just whichever branch is being
            // placed at the moment. Without grouping branches together BEFORE lane assignment, a
            // short branch (the same source feeding a nearby target) can let an unrelated, different-
            // source link slot into the gap right after it, only for a LONGER branch of the same
            // trunk to collide with that unrelated link later and get bounced to its own lane anyway
            // - the short branch reused the lane fine, but the long one couldn't, because something
            // else had already moved into the space it needed too.
            struct span_group { std::uint64_t m_ColumnId; int m_Side; std::uint64_t m_SourcePin; float m_Lo, m_Hi; std::vector<std::uint64_t> m_LinkIds; };
            std::vector<span_group> Groups;
            for (auto& S : Spans)
            {
                auto It = std::find_if(Groups.begin(), Groups.end(), [&](auto& G) { return G.m_ColumnId == S.m_ColumnId && G.m_Side == S.m_Side && G.m_SourcePin == S.m_SourcePin; });
                if (It == Groups.end())
                    Groups.push_back({ S.m_ColumnId, S.m_Side, S.m_SourcePin, S.m_Lo, S.m_Hi, { S.m_LinkId } });
                else
                {
                    It->m_Lo = std::min(It->m_Lo, S.m_Lo);
                    It->m_Hi = std::max(It->m_Hi, S.m_Hi);
                    It->m_LinkIds.push_back(S.m_LinkId);
                }
            }
            std::sort(Groups.begin(), Groups.end(), [](auto& A, auto& B) { return (A.m_Hi - A.m_Lo) < (B.m_Hi - B.m_Lo); });
            for (auto& G : Groups)
            {
                auto& Lanes = LaneIntervalsBySide[G.m_Side][G.m_ColumnId];
                int ChosenLane = -1;
                for (int L = 0; L < (int)Lanes.size(); ++L)
                {
                    bool bOverlaps = false;
                    for (auto& Iv : Lanes[L])
                    {
                        if (Iv.m_SourcePin == G.m_SourcePin) continue; // same trunk - never blocks sharing a lane (groups are already merged by source pin, kept as a safety net)
                        if (G.m_Lo <= Iv.m_Hi && G.m_Hi >= Iv.m_Lo) { bOverlaps = true; break; }
                    }
                    if (!bOverlaps) { ChosenLane = L; break; }
                }
                if (ChosenLane < 0) { ChosenLane = (int)Lanes.size(); Lanes.push_back({}); }
                Lanes[ChosenLane].push_back({ G.m_Lo, G.m_Hi, G.m_SourcePin });
                for (auto LinkId : G.m_LinkIds) LaneOfLink[LinkId] = ChosenLane;
            }
        }
        auto LaneCountOf = [&](std::uint64_t ColId, int Side01) -> int
        {
            auto It = LaneIntervalsBySide[Side01].find(ColId);
            return It == LaneIntervalsBySide[Side01].end() ? 0 : (int)It->second.size();
        };

        // ---- Pass C: per-column X, root first then walking Left/Right outward - required order, not
        // a choice: each column's X depends on the previous column's own live highway extent on that
        // side. A column's width is its own boxes + its own highway lane extent - no two columns'
        // highways ever interact. ----
        std::unordered_map<std::uint64_t, float> ColumnWidestNode;
        for (auto& S : Spines)
        {
            auto& W = ColumnWidestNode[S.m_ColumnId];
            W = std::max(W, SpineLayout[S.m_Id].m_WidestNode);
        }
        auto HighwayBaseOf = [&](std::uint64_t ColId) -> float
        {
            auto It = ColumnWidestNode.find(ColId);
            return (It == ColumnWidestNode.end() ? 120.0f : It->second) * 0.5f + geo::ICON_CLEARANCE;
        };
        auto Extent = [&](std::uint64_t ColId, char Side) -> float
        {
            const int Count = LaneCountOf(ColId, Side == 'R' ? 1 : 0);
            return HighwayBaseOf(ColId) + std::max(0, Count - 1) * geo::LANE_GAP;
        };

        std::unordered_map<std::uint64_t, float> ColumnX;
        std::uint64_t RootColumnId = 0;
        for (auto& Co : Columns) if (Co.m_bIsRoot) { RootColumnId = Co.m_Id; break; }
        ColumnX[RootColumnId] = std::max(AvailWidth * 0.5f, 260.0f); // centered on the window, never so narrow the highways collide
        for (auto* pCo = FindColumn(RootColumnId); pCo && pCo->m_LeftId; )
        {
            auto* pNext = FindColumn(pCo->m_LeftId);
            if (!pNext) break;
            ColumnX[pNext->m_Id] = ColumnX[pCo->m_Id] - (Extent(pCo->m_Id, 'L') + geo::COLUMN_MARGIN + Extent(pNext->m_Id, 'R'));
            pCo = pNext;
        }
        for (auto* pCo = FindColumn(RootColumnId); pCo && pCo->m_RightId; )
        {
            auto* pNext = FindColumn(pCo->m_RightId);
            if (!pNext) break;
            ColumnX[pNext->m_Id] = ColumnX[pCo->m_Id] + (Extent(pCo->m_Id, 'R') + geo::COLUMN_MARGIN + Extent(pNext->m_Id, 'L'));
            pCo = pNext;
        }
        for (auto& R : Layout) R.m_X = ColumnX[ColumnOfNode(R.m_NodeId)] - R.m_W * 0.5f;

        const float SpineX = ColumnX[RootColumnId]; // the window always centers on the ROOT column - see ToScreen below, unchanged from before spines existed
        auto HighwayX = [&](std::uint64_t ColId, char S, int Lane) { const float D = HighwayBaseOf(ColId) + Lane * geo::LANE_GAP; return S == 'L' ? ColumnX[ColId] - D : ColumnX[ColId] + D; };

        const ImVec2 WindowOrigin = ImGui::GetCursorScreenPos();
        const float  AvailHeight  = ImGui::GetContentRegionAvail().y; // captured before canvas_bg (below) advances the cursor and shrinks it
        const bool bWindowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        // A PURE geometric "is the mouse over the canvas" test, deliberately not IsWindowHovered -
        // used only for wheel-zoom and right-drag-pan below, both of which need to keep working while
        // a pin-to-pin or node-reorder drag (left button) is in progress: only one item can ever be
        // ImGui's "active" item at a time, so once that left-button drag claims it, IsWindowHovered's
        // default "blocked by active item elsewhere" gating starves anything gated on it, exactly the
        // same class of bug this file has already hit (and fixed the same way) for port hover/node-drag
        // drop resolution. bWindowHovered itself stays as-is for the Delete-key check below, which
        // SHOULD respect normal window-focus/Z-order semantics.
        const bool bMouseInCanvasRect = ImGui::IsMouseHoveringRect(WindowOrigin, { WindowOrigin.x + AvailWidth, WindowOrigin.y + AvailHeight });
        const float WindowCenterX = WindowOrigin.x + AvailWidth * 0.5f;

        // Wheel zoom, anchored under the cursor on both axes. The canvas has no native scrollbar (see
        // ImGuiWindowFlags above); right-drag on empty canvas space (below) handles panning, both axes.
        if (bMouseInCanvasRect)
        {
            const float Wheel = ImGui::GetIO().MouseWheel;
            if (Wheel != 0.0f)
            {
                const float LocalXAtMouse = (ImGui::GetIO().MousePos.x - WindowCenterX - View.m_PanX) / View.m_Zoom + SpineX;
                const float LocalYAtMouse = (ImGui::GetIO().MousePos.y - WindowOrigin.y - View.m_PanY) / View.m_Zoom;
                View.m_Zoom = std::clamp(View.m_Zoom + Wheel * 0.1f, 0.3f, 2.5f);
                View.m_PanX = ImGui::GetIO().MousePos.x - WindowCenterX - (LocalXAtMouse - SpineX) * View.m_Zoom;
                View.m_PanY = ImGui::GetIO().MousePos.y - WindowOrigin.y - LocalYAtMouse * View.m_Zoom;
            }
        }

        auto ToScreen    = [&](ImVec2 P) { return ImVec2(WindowCenterX + View.m_PanX + (P.x - SpineX) * View.m_Zoom, WindowOrigin.y + View.m_PanY + P.y * View.m_Zoom); };
        auto ToScreenLen = [&](float L) { return L * View.m_Zoom; };
        ImDrawList* pDraw = ImGui::GetWindowDrawList();
        const ImVec2 MouseLocal{ SpineX + (ImGui::GetIO().MousePos.x - WindowCenterX - View.m_PanX) / View.m_Zoom, (ImGui::GetIO().MousePos.y - WindowOrigin.y - View.m_PanY) / View.m_Zoom };

        // Backdrop + grid, drawn before anything else so every node/wire sits on top of it. TEST:
        // swapped the old dot grid for a minor/major LINE grid, borrowing the visual language of
        // E25/E21's own ground-grid shader (E21_GridShader_frag.glsl) - a subtle minor line every
        // GridStep, a brighter major line every MajorGridDiv-th one (matching that shader's own
        // grid_uniform::m_MajorGridDiv default of 10). This is a hand-drawn ImGui approximation, not
        // the actual GPU shader - E27's canvas is a plain ImGui draw-list with no render-target/mesh
        // pass of its own to host a real pipeline in. Lines are laid out in WORLD space (inverting
        // ToScreen for the window's own visible rect) so they pan/zoom with the graph, same as the
        // dot grid did.
        {
            const ImVec2 WinMax{ WindowOrigin.x + AvailWidth, WindowOrigin.y + AvailHeight };
            pDraw->AddRectFilled(WindowOrigin, WinMax, theme::Canvas);

            constexpr float GridStep     = 32.0f; // world units between minor lines
            constexpr int   MajorGridDiv = 10;    // every Nth line is a major line
            const float WorldXMin = (WindowOrigin.x - WindowCenterX - View.m_PanX) / View.m_Zoom + SpineX;
            const float WorldXMax = (WinMax.x - WindowCenterX - View.m_PanX) / View.m_Zoom + SpineX;
            const float WorldYMin = (WindowOrigin.y - WindowOrigin.y - View.m_PanY) / View.m_Zoom;
            const float WorldYMax = (WinMax.y - WindowOrigin.y - View.m_PanY) / View.m_Zoom;
            const int FirstCol = (int)std::floor(WorldXMin / GridStep), LastCol = (int)std::ceil(WorldXMax / GridStep);
            const int FirstRow = (int)std::floor(WorldYMin / GridStep), LastRow = (int)std::ceil(WorldYMax / GridStep);
            // Zoomed out far enough to need more than this many lines in either direction, skip the
            // grid entirely rather than drawing thousands of them - it would be so dense at that
            // point it'd just read as a gray wash anyway.
            if (LastCol - FirstCol < 400 && LastRow - FirstRow < 400)
            {
                pDraw->PushClipRect(WindowOrigin, WinMax, true);
                for (int Col = FirstCol; Col <= LastCol; ++Col)
                {
                    const bool  bMajor = (Col % MajorGridDiv) == 0;
                    const float X = ToScreen({ Col * GridStep, 0.0f }).x;
                    pDraw->AddLine({ X, WindowOrigin.y }, { X, WinMax.y }, bMajor ? theme::GridMajor : theme::Grid, bMajor ? 1.5f : 1.0f);
                }
                for (int Row = FirstRow; Row <= LastRow; ++Row)
                {
                    const bool  bMajor = (Row % MajorGridDiv) == 0;
                    const float Y = ToScreen({ 0.0f, Row * GridStep }).y;
                    pDraw->AddLine({ WindowOrigin.x, Y }, { WinMax.x, Y }, bMajor ? theme::GridMajor : theme::Grid, bMajor ? 1.5f : 1.0f);
                }
                pDraw->PopClipRect();
            }
        }

        // Background catcher, submitted first so it sits "under" every node/pin/button widget
        // (each marked AllowOverlap below to win hover/clicks over this) - gives click-on-empty-space
        // (deselect), left-drag-to-pan, and right-click-for-Add-Node without needing per-region
        // invisible buttons. Sized to the window itself (not the full zoomed content) since panning,
        // not scrolling, covers the rest - a click anywhere in the visible window should count as a
        // background click.
        ImGui::SetNextItemAllowOverlap(); // first-submitted covering the whole window - without this it
                                           // permanently owns hover for the frame and blocks every node/
                                           // pin/button drawn after it (xgpu_imgui_overlapping_invisible_buttons).
        ImGui::SetCursorScreenPos(WindowOrigin);
        // InvisibleButton only reacts to the left mouse button unless told otherwise - panning needs
        // the right button explicitly enabled here, or IsItemActive()/IsMouseDragging(Right) below never
        // fires at all.
        ImGui::InvisibleButton("canvas_bg", ImGui::GetContentRegionAvail(), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

        // Right-drag pans, leaving left click free for pure selection (click a link, click a node) with
        // no drag-vs-click ambiguity to resolve on that button anymore. Tracked by hand (geometric
        // press test, then just follow the raw button state to release) rather than canvas_bg's own
        // IsItemActive(), for the same reason wheel-zoom above switched to bMouseInCanvasRect: a
        // concurrent left-button pin-to-pin or node-reorder drag already holds ImGui's one "active
        // item" slot, so gating panning on canvas_bg becoming active too would silently never fire
        // while such a drag is in progress - and panning/zooming while dragging a connection is
        // exactly the case this needs to keep working for.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && bMouseInCanvasRect)
            View.m_bPanDragActive = true;
        if (View.m_bPanDragActive)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
            {
                View.m_PanX += ImGui::GetIO().MouseDelta.x;
                View.m_PanY += ImGui::GetIO().MouseDelta.y;
            }
            else
                View.m_bPanDragActive = false;
        }
        const bool bBackgroundClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

        auto DrawHighwayPath = [&](std::uint64_t ColId, ImVec2 From, ImVec2 To, char S, int Lane, ImU32 Col, float Thickness, const float* pDash)
        {
            const float HX = HighwayX(ColId, S, Lane);
            (void)pDash; // no native dashed-line primitive - dashing omitted, solid preview line is distinguished by color instead
            pDraw->AddLine(ToScreen(From), ToScreen({ HX, From.y }), Col, Thickness);
            pDraw->AddLine(ToScreen({ HX, From.y }), ToScreen({ HX, To.y }), Col, Thickness);
            pDraw->AddLine(ToScreen({ HX, To.y }), ToScreen(To), Col, Thickness);
        };

        // One rail pair per column, each spanning the full graph height - harmless to draw past a
        // column's own actual content, and simpler than tracking each column's own vertical extent.
        // Drawn at the OUTERMOST lane actually in use on each side, not lane 0 - a column carrying
        // more than one parallel wire on a side needs its rail to reach as far out as its farthest
        // lane, or the backdrop line would cut through wires instead of framing all of them.
        for (auto& Co : Columns)
        {
            const int LOuter = std::max(0, LaneCountOf(Co.m_Id, 0) - 1);
            const int ROuter = std::max(0, LaneCountOf(Co.m_Id, 1) - 1);
            pDraw->AddLine(ToScreen({ HighwayX(Co.m_Id, 'L', LOuter), 0 }), ToScreen({ HighwayX(Co.m_Id, 'L', LOuter), TotalH }), theme::Rail, ToScreenLen(2.0f));
            pDraw->AddLine(ToScreen({ HighwayX(Co.m_Id, 'R', ROuter), 0 }), ToScreen({ HighwayX(Co.m_Id, 'R', ROuter), TotalH }), theme::Rail, ToScreenLen(2.0f));
        }

        const auto ScopeDepths     = ComputeScopeDepths(Nodes);
        const auto EnclosingChains = ComputeEnclosingChains(Nodes);

        for (auto& Link : Links)
        {
            auto* pSrcDesc = DescOf(FindNode(Link.m_SourceNode)); auto* pDstDesc = DescOf(FindNode(Link.m_TargetNode));
            if (!pSrcDesc || !pDstDesc) continue;
            const auto SrcOutputs = pSrcDesc->getOutputs(); const auto DstInputs = pDstDesc->getInputs();
            if (Link.m_SourceOutput >= (int)SrcOutputs.size() || Link.m_TargetInput >= (int)DstInputs.size()) continue;
            const port_ref OutP{ true, Link.m_SourceOutput, &SrcOutputs[Link.m_SourceOutput] };
            const port_ref InP { false, Link.m_TargetInput,  &DstInputs[Link.m_TargetInput] };
            const bool bSelected = (Selection.m_SelectedLink == Link.m_Id);
            // TypeColor("Scope") already matches the box border color, so an ordinary owner<->End
            // link (always a Scope pin) picks up the right color for free here - only its extra
            // thickness needs a read-only-specific branch. A Scope link's own two ends always share
            // the same depth by construction (an owner and its End marker both resolve to the same
            // stack size in ComputeScopeDepths), so either endpoint's depth darkens the whole wire,
            // matching the boxes it connects.
            // EffectiveTypeName, not the source pin's raw declared type - a wire out of a wildcard
            // output (Math Expression/Compare's own "Any" Result) must show the type it actually
            // RESOLVED to, matching what the pin glyph on either end already colors itself by, not
            // the perpetually-neutral "Any" gray every such wire would otherwise be stuck showing
            // even once it's carrying, say, a real Float.
            ImU32 Col = bSelected ? theme::Selected : TypeColor(EffectiveTypeName(Link.m_SourceNode, pSrcDesc, OutP.m_pDesc->m_pTypeName, Nodes, Links));
            if (!bSelected && Link.m_bReadOnly)
            {
                auto DepthIt = ScopeDepths.find(Link.m_SourceNode);
                Col = DarkenForDepth(Col, DepthIt == ScopeDepths.end() ? 0 : DepthIt->second);
            }
            // A data link (never a read-only ownership wire, which is always structurally valid by
            // construction) whose source the target could never actually reference in real nested
            // C++ - see IsDataLinkScopeValid - flags in a clear warning color regardless of type or
            // selection state. There's no compiler to catch this yet; this is the honest stand-in for
            // the diagnostic one would eventually give (matching how Unreal Blueprint/Unity Visual
            // Scripting both let the wire get drawn and only flag it later - just surfaced immediately
            // here instead of deferred, since there's nothing else to defer it to today).
            const bool bScopeInvalid = !Link.m_bReadOnly && !IsDataLinkScopeValid(Link.m_SourceNode, Link.m_SourceOutput, Link.m_TargetNode, Link.m_TargetInput, Nodes, EnclosingChains);
            if (bScopeInvalid) Col = IM_COL32(239, 68, 68, 255);
            const float Thickness = bScopeInvalid ? 3.0f : bSelected ? 3.0f : (Link.m_bReadOnly ? 4.0f : 2.0f);
            char SourceSide = 'R', TargetSide = 'R', RailSide = 'R';
            LinkSides(Link, SourceSide, TargetSide, RailSide);
            DrawHighwayPath(OwnerColumnOf(Link), PortAnchor(Link.m_SourceNode, OutP, SourceSide), PortAnchor(Link.m_TargetNode, InP, TargetSide)
                           , RailSide, LaneOfLink[Link.m_Id], Col, Thickness, nullptr);
        }

        if (Drag.m_bActive)
        {
            const char S = Drag.m_FromSide;
            const auto DragColumnId = ColumnOfNode(Drag.m_FromNode);
            DrawHighwayPath(DragColumnId, Drag.m_FromPos, MouseLocal, S, LaneCountOf(DragColumnId, S == 'R' ? 1 : 0), IM_COL32(125, 211, 252, 255), 2.0f, nullptr);
        }

        // Selecting a scope's ownership link highlights every box the scope actually contains - a
        // visual-only highlight (see ComputeScopeSpan), never added to Selection.m_SelectedNodes, so
        // inspecting a scope this way never makes its contents eligible for Delete or drag.
        std::set<std::uint64_t> HighlightedScopeSpan;
        if (Selection.m_SelectedLink != 0)
            for (auto& L : Links)
                if (L.m_Id == Selection.m_SelectedLink && L.m_bReadOnly)
                {
                    auto Span = ComputeScopeSpan(Nodes, L.m_SourceNode);
                    HighlightedScopeSpan.insert(Span.begin(), Span.end());
                    break;
                }

        const float FontSize = ImGui::GetFontSize() * View.m_Zoom;
        auto DrawText = [&](ImVec2 Pos, ImU32 Col, const char* pText) { pDraw->AddText(nullptr, FontSize, ToScreen(Pos), Col, pText); };

        // The drag origin's own effective type, fixed for the whole drag - computed once per frame
        // rather than once per candidate pin.
        auto* pFromDescForDrag = Drag.m_bActive ? DescOf(FindNode(Drag.m_FromNode)) : nullptr;
        std::string FromEffForDrag;
        if (pFromDescForDrag)
        {
            const char* pFromType = Drag.m_bFromIsOutput ? pFromDescForDrag->getOutputs()[Drag.m_FromIndex].m_pTypeName : pFromDescForDrag->getInputs()[Drag.m_FromIndex].m_pTypeName;
            FromEffForDrag = EffectiveTypeName(Drag.m_FromNode, pFromDescForDrag, pFromType, Nodes, Links);
        }

        // Whether a SPECIFIC candidate pin (opposite direction from the drag's own origin, PortIndex
        // into that direction's own port list) would actually accept the drop - by type AND scope
        // (IsDataLinkScopeValid). A node can carry both eligible and ineligible pins at once (a
        // Function's external vs. local-mirrored ports; ForEachLoop's Span vs. Element/Index), so
        // this is checked per-pin - both to dim one ineligible pin without fading its whole node (see
        // the port-row loop below) and, aggregated across a node's full port list in
        // NodeAcceptsDrag, to fade a node that has NO eligible pin anywhere on it.
        auto PortAcceptsDrag = [&](std::uint64_t CandidateId, int PortIndex) -> bool
        {
            if (!Drag.m_bActive || CandidateId == Drag.m_FromNode || !pFromDescForDrag) return true;
            auto* pCandDesc = DescOf(FindNode(CandidateId));
            if (!pCandDesc) return true;
            const auto CandPorts = Drag.m_bFromIsOutput ? pCandDesc->getInputs() : pCandDesc->getOutputs();
            if (PortIndex < 0 || PortIndex >= (int)CandPorts.size()) return true;
            const std::string ToEff = EffectiveTypeName(CandidateId, pCandDesc, CandPorts[PortIndex].m_pTypeName, Nodes, Links);
            if (!IsAnyKindOfWildcard(FromEffForDrag.c_str()) && !IsAnyKindOfWildcard(ToEff.c_str()) && FromEffForDrag != ToEff) return false;
            const std::uint64_t SrcForScope    = Drag.m_bFromIsOutput ? Drag.m_FromNode  : CandidateId;
            const std::uint64_t TgtForScope    = Drag.m_bFromIsOutput ? CandidateId      : Drag.m_FromNode;
            const int           SrcOutForScope = Drag.m_bFromIsOutput ? Drag.m_FromIndex : PortIndex;
            const int           TgtInForScope  = Drag.m_bFromIsOutput ? PortIndex        : Drag.m_FromIndex;
            return IsDataLinkScopeValid(SrcForScope, SrcOutForScope, TgtForScope, TgtInForScope, Nodes, EnclosingChains);
        };

        // A node fades out (in the draw loop below) only when NOT ONE of its ports would accept the
        // drop - if even one pin qualifies, the node stays visible and the per-pin dim below marks
        // its other, ineligible pins instead.
        auto NodeAcceptsDrag = [&](std::uint64_t CandidateId) -> bool
        {
            if (!Drag.m_bActive || CandidateId == Drag.m_FromNode) return true;
            auto* pCandDesc = DescOf(FindNode(CandidateId));
            if (!pCandDesc) return true;
            const auto CandPorts = Drag.m_bFromIsOutput ? pCandDesc->getInputs() : pCandDesc->getOutputs();
            for (int i = 0; i < (int)CandPorts.size(); ++i)
                if (PortAcceptsDrag(CandidateId, i)) return true;
            return false;
        };

        // Native ImGui widgets (the inline-literal InputFloat/InputInt boxes, the enum Combo dropdown)
        // have no notion of this canvas's own pan/zoom transform - left alone, their text stays
        // pinned at the base font size no matter how far the graph is zoomed, drifting out of step
        // with every hand-drawn label around them (row names, titles, pin types) the instant Zoom
        // isn't exactly 1.0. SetWindowFontScale is ImGui's own mechanism for exactly this: scales
        // every widget's text drawn while it's active, for the rest of this window. Reset back to
        // 1.0 right after the node loop, before any popup menu below it - a right-click "Add Node"
        // menu should stay a fixed, comfortable size regardless of how zoomed-in the canvas is.
        ImGui::SetWindowFontScale(View.m_Zoom);
        for (size_t oi = 0; oi < Order.size(); ++oi)
        {
            const auto Id = Order[oi];
            auto* pRow  = FindRow(Id);
            auto* pNode = FindNode(Id);
            auto* pDesc = DescOf(pNode);
            if (!pRow || !pDesc) continue;

            // Push this node's own live-resolved wildcard type (if it has one worth tracking - see
            // PushResolvedTypeDebugProperty) into its "Resolved Type" debug property, every frame -
            // cheap (a single by-name lookup that no-ops for every node type that doesn't declare
            // one) and keeps it current as the user rewires things.
            {
                const char* pResolved = ResolveNodeWildcardType(Id, pDesc, Nodes, Links);
                PushResolvedTypeDebugProperty(pDesc, pResolved ? pResolved : "Any");
            }
            PushPinConnectedFlags(pDesc, Id, Links);

            const ImVec2 P0 = ToScreen({ pRow->m_X, pRow->m_Y });
            const ImVec2 P1 = ToScreen({ pRow->m_X + pRow->m_W, pRow->m_Y + pRow->m_H });
            const bool bSelected      = Selection.m_SelectedNodes.contains(Id);
            const bool bBeingDragged  = NodeDrag.m_bActive && std::find(NodeDrag.m_MovingIds.begin(), NodeDrag.m_MovingIds.end(), Id) != NodeDrag.m_MovingIds.end();

            // Depth stands in for indentation (see ComputeScopeDepths) - the deeper a node's own
            // enclosing scope, the darker its fill; the border stays untouched so the box outline is
            // always visible no matter how deep.
            const auto DepthIt = ScopeDepths.find(Id);
            const int  Depth   = DepthIt == ScopeDepths.end() ? 0 : DepthIt->second;
            const bool bInHighlightedSpan = HighlightedScopeSpan.contains(Id);
            const bool bIsEndMarker = IsEndMarkerType(pDesc);
            pDraw->AddRectFilled(P0, P1, DarkenForDepth(theme::NodeBg, Depth), 0.0f);
            // Title-row strip, brighter than the body beneath it - drawn as its own rect ON TOP of
            // the body fill (not a separate widget), so the node's name/category line reads as a
            // real header at a glance instead of just floating text over the same flat body color.
            // Tinted by the node's own category (CategoryColor) rather than one flat gray for every
            // node, Unity-style - falls back to the old neutral gray for any category not listed
            // there. An End marker draws no header at all - it's deliberately just a plain title bar
            // with no body/ports beneath it, nothing to visually separate a "header" from, and no
            // category of its own to tint by.
            const auto NodeCategory = bIsEndMarker ? std::string_view{} : pDesc->m_pFactory->getCategory();
            if (!bIsEndMarker)
                pDraw->AddRectFilled(P0, { P1.x, P0.y + ToScreenLen(geo::HEADER_H) }, DarkenForDepth(CategoryColor(NodeCategory), Depth), 0.0f);
            pDraw->AddRect(P0, P1, bBeingDragged ? IM_COL32(56, 189, 248, 255) : (bSelected ? theme::Selected : (bInHighlightedSpan ? IM_COL32(125, 211, 252, 255) : theme::NodeBorder))
                          , 0.0f, 0, ToScreenLen((bSelected || bBeingDragged || bInHighlightedSpan) ? 2.5f : 1.5f));
            // Screen-space: title/category text is measured and positioned entirely in screen
            // pixels at the EXACT font size passed to AddText below. Measuring via unscaled
            // ImGui::CalcTextSize/GetFontSize here would silently pick up SetWindowFontScale's
            // zoom-scaled font (it's active for this whole loop, for native widgets' benefit) and
            // then get scaled AGAIN by ToScreen - a double-scale that's invisible at Zoom=1.0 (a
            // no-op) but drifts/overlaps at any other zoom. Same fix as the pin-type label below.
            // The title reads first at a glance, so it renders a bit bigger than every other label
            // on the node (row names, pin types, category) - TITLE_FONT_SCALE, matched by
            // NodeWidth's own TitleW reservation so a bigger title never re-collides with the
            // category text the way the original bug did.
            const float TitleFontSize = FontSize * geo::TITLE_FONT_SCALE;
            const float TitleYPx = P0.y + (ToScreenLen(geo::HEADER_H) - TitleFontSize) * 0.5f + 2.0f;
            const auto NodeName = pDesc->m_pFactory->getName();
            // An End marker's own factory name is just "End" for every instance - not clear enough
            // on its own (NODE_SCRIPTING_DESIGN.md section 4.2). Its actual displayed title is
            // computed contextually: "<Owner>-End" for a plain marker, "<Owner>-End-Else" once its
            // own IsElse checkbox is on, or "Else-End" for the further marker THAT one owns in turn
            // (recognized by its owner also being an End, not a real control node).
            std::string DisplayName(NodeName);
            if (NodeName == "End")
            {
                node_instance* pOwnerNode = nullptr;
                for (auto& N : Nodes) if (N.m_OwnedEndId == Id) { pOwnerNode = &N; break; }
                if (pOwnerNode && pOwnerNode->m_pNode)
                {
                    const auto OwnerName = pOwnerNode->m_pNode->m_pFactory->getName();
                    if (OwnerName == "End") DisplayName = "Else-End";
                    else
                    {
                        const bool bIsElse = pNode->m_pNode && ReadBoolPropertyFromSnapshot(SerializePropertiesToString(pNode->m_pNode), "IsElse");
                        DisplayName = std::string(OwnerName) + (bIsElse ? "-End-Else" : "-End");
                    }
                }
            }
            const ImVec2 NameSizePx = ImGui::GetFont()->CalcTextSizeA(TitleFontSize, FLT_MAX, 0.0f, DisplayName.c_str());
            const float TitleXPx = bIsEndMarker
                ? P0.x + (P1.x - P0.x) * 0.5f - NameSizePx.x * 0.5f
                : P0.x + ToScreenLen(10.0f);
            pDraw->AddText(nullptr, TitleFontSize, ImVec2(TitleXPx, TitleYPx), IM_COL32(226, 232, 240, 255), DisplayName.c_str());
            // An End marker's title bar shows only its name - no category label, no header/body
            // divider line (it has no body to divide from). The category label stays at the normal
            // (non-title) font size - it's an annotation next to the title, not a second title.
            if (!bIsEndMarker)
            {
                const ImVec2 CatSizePx = ImGui::GetFont()->CalcTextSizeA(FontSize, FLT_MAX, 0.0f, NodeCategory.data(), NodeCategory.data() + NodeCategory.size());
                const float CatXPx = P1.x - CatSizePx.x - ToScreenLen(10.0f);
                pDraw->AddText(nullptr, FontSize, ImVec2(CatXPx, P0.y + (ToScreenLen(geo::HEADER_H) - FontSize) * 0.5f), IM_COL32(100, 116, 139, 255), NodeCategory.data());
                pDraw->AddLine(ToScreen({ pRow->m_X, pRow->m_Y + geo::HEADER_H }), ToScreen({ pRow->m_X + pRow->m_W, pRow->m_Y + geo::HEADER_H }), theme::NodeBorder);
            }

            ImGui::PushID((int)Id);
            ImGui::SetNextItemAllowOverlap();
            ImGui::SetCursorScreenPos(P0);
            ImGui::InvisibleButton("body", ImVec2(ToScreenLen(pRow->m_W), ToScreenLen(pRow->m_H)));

            // Press decides the moving set (this node's whole multi-selection, if it's part of one)
            // before any drag distance is known, so a plain click still agrees with what a drag would
            // have moved. Only an actual drag (past the threshold) acts on it, marked via
            // NodeDrag.m_bActive - a click that never turns into a drag falls through to selection
            // below instead.
            if (ImGui::IsItemActivated())
            {
                NodeDrag.m_bActive = false;
                auto InitialIds = Selection.m_SelectedNodes.contains(Id)
                    ? std::vector<std::uint64_t>(Selection.m_SelectedNodes.begin(), Selection.m_SelectedNodes.end())
                    : std::vector<std::uint64_t>{ Id };
                NodeDrag.m_MovingIds = ExpandMoveSetForScopes(Nodes, std::move(InitialIds));
            }
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f))
                NodeDrag.m_bActive = true;

            if (ImGui::IsItemClicked() && !NodeDrag.m_bActive)
            {
                // A control node and its owned End/End-Else marker(s) are one compound node (NODE_
                // SCRIPTING_DESIGN.md section 4.1) - clicking any one piece selects the whole group,
                // same cascade DeleteNodes already uses so the two notions of "one unit" agree.
                const auto GroupIds = commands::ExpandOwnershipCascade(Nodes, { Id });

                // Toggle decided here, against the CURRENT selection, then issued as the full desired
                // end-state - Select's Redo() just sets exactly what's in the command string (so replay
                // during a later Redo() stays deterministic regardless of what's currently selected).
                const bool bToggle = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
                std::set<std::uint64_t> NewSelectedNodes = bToggle ? Selection.m_SelectedNodes : std::set<std::uint64_t>{};
                if (bToggle)
                {
                    if (NewSelectedNodes.contains(Id)) for (auto Gid : GroupIds) NewSelectedNodes.erase(Gid);
                    else                                 for (auto Gid : GroupIds) NewSelectedNodes.insert(Gid);
                }
                else
                    NewSelectedNodes = { GroupIds.begin(), GroupIds.end() };
                commands::Run(System, commands::MakeSelectNodes({ NewSelectedNodes.begin(), NewSelectedNodes.end() }));
            }

            // Reorder (drag a node onto a spine "+" marker) and delete (Delete key) both already exist
            // elsewhere, so the header no longer needs its own ^/v/x buttons for them.

            // Preview block - every Mesh-typed port's live render, grouped right under the header,
            // ABOVE the port rows: "what this node shows" first, "what it's wired to" underneath.
            float RowY = pRow->m_Y + geo::HEADER_H;
            if (MeshPortCount(pDesc) > 0)
            {
                RowY += geo::PREVIEW_GAP;
                for (auto& P : FlatPorts(pDesc))
                {
                    if (!IsMeshType(P.m_pDesc->m_pTypeName)) continue;
                    ImGui::SetNextItemAllowOverlap();
                    ImGui::SetCursorScreenPos(ToScreen({ pRow->m_X + pRow->m_W * 0.5f - mesh_preview_system::s_PreviewSize * 0.5f, RowY }));
                    MeshPreview.DrawPreviewSquare(PinOf(P, Id), View.m_Zoom);
                    RowY += mesh_preview_system::s_PreviewSize + geo::PREVIEW_GAP;
                }
            }

            // An End marker draws no port rows at all - no glyph, no label, nothing to drag - its
            // pin is purely the title-bar anchor point PortAnchor already computes above.
            const auto PortsForRow = FlatPorts(pDesc);
            bool bHavePrevPort = false, bPrevPortLocal = false;
            if (!IsEndMarkerType(pDesc))
            for (std::size_t PortIdx = 0; PortIdx < PortsForRow.size(); ++PortIdx)
            {
                auto& P = PortsForRow[PortIdx];
                // At the ONE point this node's ports switch from external to local-scope: a visible
                // gap (not just a color/line cue - see LocalSectionGapTotal, which reserves the
                // matching space in NodeHeight), a background tint matching what a node physically
                // INSIDE this scope would get from DarkenForDepth (same visual language as the rest
                // of this file's depth-based dimming), and a small caption so the transition reads
                // without having to infer it from the const/& type annotations alone. The tint runs
                // all the way to the node's own bottom edge (through the End row) rather than
                // stopping at the local ports' own extent, so there's no hard edge partway down.
                if (bHavePrevPort && P.m_pDesc->m_bLocalScope && !bPrevPortLocal)
                {
                    const float LineY = RowY + geo::SECTION_GAP * 0.5f;
                    // Inset from the node's own left/right border by a hair, and stop a hair short of
                    // the bottom border, so the tint sits INSIDE the node's outline rather than
                    // painting over it - reaches the very bottom (through the End row) rather than
                    // stopping at the local ports' own extent, so there's no hard edge partway down.
                    const float BorderInset = ToScreenLen(1.5f);
                    const ImVec2 TintMin = { ToScreen({ pRow->m_X, LineY }).x + BorderInset, ToScreen({ pRow->m_X, LineY }).y };
                    const ImVec2 TintMax = { P1.x - BorderInset, P1.y - ToScreenLen(1.0f) };
                    pDraw->AddRectFilled(TintMin, TintMax, DarkenForDepth(theme::NodeBg, Depth + 1), 0.0f);
                    pDraw->AddLine(ToScreen({ pRow->m_X + 6.0f, LineY }), ToScreen({ pRow->m_X + pRow->m_W - 6.0f, LineY }), IM_COL32(100, 116, 139, 255), ToScreenLen(1.5f));
                    DrawText({ pRow->m_X + 8.0f, LineY + 2.0f }, IM_COL32(100, 116, 139, 255), "locals");
                    RowY += geo::SECTION_GAP;
                }
                bPrevPortLocal = P.m_pDesc->m_bLocalScope;
                bHavePrevPort = true;

                // An Any pin (Compare's A/B) shows and colors as whatever it's currently resolved to
                // (see ResolveNodeWildcardType) - "Any" itself only while nothing has locked it in yet.
                const char* pEffType = EffectiveTypeName(Id, pDesc, P.m_pDesc->m_pTypeName, Nodes, Links);
                const float RH = RowHeight(P, Id, Links, pEffType);
                const float CenterY = RowY + geo::ROW_H * 0.5f;
                // A Scope pin darkens with its own node's depth too, matching the box and the wire.
                ImU32 Col = IsScopeType(pEffType) ? DarkenForDepth(TypeColor(pEffType), Depth) : TypeColor(pEffType);
                const bool bConnected = PortSides.contains(PinOf(P, Id));
                ImU32 Fill = bConnected ? Col : theme::CanvasDark;

                // Fade the SMALLEST thing that's actually invalid: a candidate pin (opposite
                // direction from the drag, different node) that fails type or scope dims on its own,
                // rather than only ever fading the whole node - a node can mix eligible and
                // ineligible pins on the same box (a Function's external vs. local-mirrored ports;
                // ForEachLoop's Span vs. its own Element/Index), and the whole-node overlay below
                // only fires when NOTHING on the node qualifies.
                ImU32 NameCol = IM_COL32(203, 213, 225, 255);
                if (Drag.m_bActive && Id != Drag.m_FromNode && P.m_bIsOutput != Drag.m_bFromIsOutput && !PortAcceptsDrag(Id, P.m_Index))
                {
                    Col     = WithAlpha(Col, 0.35f);
                    Fill    = WithAlpha(Fill, 0.35f);
                    NameCol = WithAlpha(NameCol, 0.35f);
                }

                // "End" gets no name label at all - it's the one port name that's the same on every
                // owner type, adds no information ("[Scope]" already says what it is), and dropping
                // it lets every OTHER port's own name stand out more.
                if (std::strcmp(P.m_pDesc->m_pName, "End") != 0)
                {
                    // Screen-space, same reasoning as the title/category fix above.
                    const ImVec2 PinNameSizePx = ImGui::GetFont()->CalcTextSizeA(FontSize, FLT_MAX, 0.0f, P.m_pDesc->m_pName);
                    const ImVec2 RowCenterPx = ToScreen({ pRow->m_X + pRow->m_W * 0.5f, CenterY });
                    pDraw->AddText(nullptr, FontSize, ImVec2(RowCenterPx.x - PinNameSizePx.x * 0.5f, RowCenterPx.y - PinNameSizePx.y * 0.5f), NameCol, P.m_pDesc->m_pName);
                }

                // A pin used by links going both up and down the stack needs a glyph on BOTH sides -
                // one per side actually in use (SidesOf), not just whichever link happened to be
                // processed last into a single shared "the" side.
                for (char S : SidesOf(PinOf(P, Id), P.m_bIsOutput))
                {
                    const float CX = (S == 'L') ? pRow->m_X : pRow->m_X + pRow->m_W;

                    // Glyph: a triangle pointing INTO the node for an input, OUT toward the highway for
                    // an output - matches rslgraph-ui NodeView.tsx's Glyph: pointRight = (side=='R') == isOutput.
                    const bool bPointRight = (S == 'R') == P.m_bIsOutput;
                    const float R = geo::GLYPH * 0.5f;
                    const ImVec2 Tip = bPointRight ? ToScreen({ CX + R, CenterY }) : ToScreen({ CX - R, CenterY });
                    const ImVec2 B1  = bPointRight ? ToScreen({ CX - R, CenterY - R }) : ToScreen({ CX + R, CenterY - R });
                    const ImVec2 B2  = bPointRight ? ToScreen({ CX - R, CenterY + R }) : ToScreen({ CX + R, CenterY + R });
                    pDraw->AddTriangleFilled(Tip, B1, B2, Fill);
                    pDraw->AddTriangle(Tip, B1, B2, Col, ToScreenLen(1.5f));

                    // Screen-space anchoring throughout, not a world-space position built from a
                    // screen-space width and then re-transformed - measure the label at the EXACT
                    // font size AddText will use, anchor from the row edge AFTER ToScreen (not
                    // before), and do the whole left/right offset in already-zoomed pixels. Round-
                    // tripping a width through world space and back was the earlier (unreliable)
                    // approach; this is the one that can't drift as View.m_Zoom changes, since
                    // nothing here mixes the two coordinate spaces.
                    // Reads as an annotation, not a name - smaller than the row/title text and
                    // tucked in close to its own pin (PIN_TYPE_FONT_SCALE/PIN_TYPE_INSET, matched by
                    // NodeWidth's own PortColW reservation so the box stays exactly as wide as this
                    // smaller, closer label actually needs).
                    const std::string TypeLabel = std::string("[") + DisplayTypeText(pDesc, P, pEffType) + "]";
                    const float PinTypeFontSize = FontSize * geo::PIN_TYPE_FONT_SCALE;
                    const ImVec2 TypeSizePx = ImGui::GetFont()->CalcTextSizeA(PinTypeFontSize, FLT_MAX, 0.0f, TypeLabel.c_str());
                    const ImVec2 RowEdgePx = ToScreen({ S == 'L' ? pRow->m_X : pRow->m_X + pRow->m_W, CenterY });
                    const float InsetPx = ToScreenLen(geo::PIN_TYPE_INSET);
                    const ImVec2 TypePosPx = (S == 'L')
                        ? ImVec2(RowEdgePx.x + InsetPx, RowEdgePx.y - TypeSizePx.y * 0.5f)
                        : ImVec2(RowEdgePx.x - InsetPx - TypeSizePx.x, RowEdgePx.y - TypeSizePx.y * 0.5f);
                    pDraw->AddText(nullptr, PinTypeFontSize, TypePosPx, Col, TypeLabel.c_str());

                    // Drag-to-connect hit target - generous, bigger than the visible glyph (rslgraph-ui's
                    // own NodeView.tsx uses a 13px-radius invisible circle over each port; this is wider
                    // still since the whole point is that grabbing it should be easy, not precise).
                    ImGui::PushID((int)PinOf(P, Id));
                    ImGui::PushID(S);
                    ImGui::SetNextItemAllowOverlap();
                    const ImVec2 PinMin = ToScreen({ CX - geo::PORT_HIT_RADIUS, CenterY - geo::PORT_HIT_RADIUS });
                    const ImVec2 PinMax = ToScreen({ CX + geo::PORT_HIT_RADIUS, CenterY + geo::PORT_HIT_RADIUS });
                    ImGui::SetCursorScreenPos(PinMin);
                    ImGui::InvisibleButton("pin", ImVec2(PinMax.x - PinMin.x, PinMax.y - PinMin.y));
                    // Direct geometric hit-test against the mouse position, not ImGui's own hover/active-id
                    // bookkeeping: while dragging, the ORIGIN pin's own InvisibleButton is still "active"
                    // (mouse held since that press), and IsItemHovered() silently returns false for every
                    // OTHER item while some item is active - exactly the case for every port dragged over.
                    // This codebase already hit this same class of bug once for the spine's node-drag drop
                    // (see MarkerPositions/MoveNodesTo below) and settled on hit-testing the cursor directly
                    // instead of chasing the right AllowWhenBlockedByActiveItem/AllowOverlap flag combination.
                    const bool bPinHovered = ImGui::IsMouseHoveringRect(PinMin, PinMax);
                    if (!Drag.m_bActive && bPinHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        Drag = { true, Id, P.m_bIsOutput, P.m_Index, { CX, CenterY }, S };
                    ImGui::PopID();
                    ImGui::PopID();

                    // While actively dragging a connection, the hover ring only shows over a port that
                    // would actually accept the drop - opposite direction, a different node, and a matching
                    // type name (the exact same validity check the drop-resolution below commits with) - so
                    // a wrong-type or same-direction port simply never lights up. Outside of a drag, hovering
                    // any port still rings it, as a plain "you can start a connection here" affordance.
                    bool bShowRing = bPinHovered;
                    if (bPinHovered && Drag.m_bActive)
                    {
                        const bool bSamePort      = (Id == Drag.m_FromNode && P.m_bIsOutput == Drag.m_bFromIsOutput && P.m_Index == Drag.m_FromIndex);
                        const bool bOppositeDir   = (P.m_bIsOutput != Drag.m_bFromIsOutput);
                        const bool bDifferentNode = (Id != Drag.m_FromNode);
                        // PortAcceptsDrag checks type AND scope now - a scope-forbidden pin (e.g. a
                        // Function's local-mirrored input reached from outside its own body) used to
                        // still ring here on type alone, inviting a drop the commit logic would then
                        // silently refuse; folded into the one shared check so the ring and the
                        // eventual drop-resolution never disagree.
                        bShowRing = !bSamePort && bOppositeDir && bDifferentNode && PortAcceptsDrag(Id, P.m_Index);
                    }
                    if (bShowRing)
                    {
                        // Sized off the glyph's own radius (R, above) rather than the generous
                        // PORT_HIT_RADIUS hit-test area - a close-fitting halo around the actual pin,
                        // not a big loose circle - and colored to match the pin's own type color
                        // (Col, same one the glyph and its "[Type]" label already use) instead of a
                        // fixed light blue, so the ring reads as "this pin" rather than a generic
                        // hover indicator that happens to not match what's being highlighted.
                        const float HR = R * 2.2f;
                        pDraw->AddCircle(ToScreen({ CX, CenterY }), ToScreenLen(HR), Col, 0, ToScreenLen(1.5f));
                    }
                }

                if (!IsMeshType(pEffType) && !IsScopeType(pEffType) && !IsNoPreviewType(pEffType) && !P.m_bIsOutput)
                {
                    void* pValue = P.m_bIsOutput
                        ? ((pNode->m_bHasRun && P.m_Index < (int)pNode->m_CachedOutputs.size()) ? pNode->m_CachedOutputs[P.m_Index] : nullptr)
                        : GetInputValue(Id, P.m_Index, Nodes, Links, LiteralScratch);

                    // Float/Int/Short are this corpus's numeric scalar types (see IsNoPreviewType's
                    // own comment for why Bool is excluded here - it's filtered out already, above,
                    // regardless of whether it's a fixed Bool pin or a wildcard resolved to Bool).
                    // Checked against the EFFECTIVE type, not the raw declared one, so an Any pin
                    // (Compare/Math Expression's A/B) resolved to one of these gets the same inline
                    // literal an ordinarily-typed pin of that type would - this is what "the compare
                    // node still lets you enter a value based on the known type" actually means.
                    const bool bIsFloat = std::strcmp(pEffType, "Float") == 0;
                    const bool bIsInt   = std::strcmp(pEffType, "Int") == 0 || std::strcmp(pEffType, "Short") == 0;

                    // Gated on bConnected (an actual wire in Links), NOT on pValue - pValue only
                    // reflects a produced runtime value, which none of these no-op-Execute() node
                    // types ever populate, so checking it would leave the widget showing forever even
                    // after a wire is attached. bConnected is exactly "is a wire here right now",
                    // which is the only thing that should ever hide the inline constant.
                    if (!P.m_bIsOutput && !bConnected && (bIsFloat || bIsInt))
                    {
                        // Inline constant, Unity-style: an unconnected scalar input isn't "no
                        // value", it's "whatever's typed right here". A same-named reflected
                        // property (see FindMemberByName - Compare/Math Expression's own "A"/"B",
                        // SetVariable's own "Value", same idea as Constant's Value* fields) is the
                        // pin's real, typed, undoable, saved backing store - every node type with a
                        // literal-editable pin declares one, so this is the only mechanism, not a
                        // fallback path (a literal is simply ignored once a wire exists either way,
                        // see the pValue check just above).
                        const std::uint64_t PinId = PinOf(P, Id);
                        auto* pLiteralMember = FindMemberByName(pNode->m_pNode->getProperties(), P.m_pDesc->m_pName);
                        std::string CurrentText = "0";
                        if (pLiteralMember)
                        {
                            xproperty::any Out; xproperty::settings::context ReadCtx;
                            if (pLiteralMember->TryRead(pNode->m_pNode, Out, ReadCtx))
                            {
                                if (Out.is<float>())             CurrentText = std::format("{}", Out.get<float>());
                                else if (Out.is<std::int32_t>()) CurrentText = std::to_string(Out.get<std::int32_t>());
                                else if (Out.is<std::int16_t>()) CurrentText = std::to_string(Out.get<std::int16_t>());
                            }
                        }

                        auto CommitLiteral = [&](const std::string&, const xproperty::any& TypedValue)
                        {
                            if (!pLiteralMember) return;
                            const std::string Before = SerializePropertiesToString(pNode->m_pNode);
                            xproperty::any In = TypedValue; xproperty::settings::context WriteCtx;
                            (void)pLiteralMember->TryWrite(pNode->m_pNode, In, WriteCtx);
                            const std::string After = SerializePropertiesToString(pNode->m_pNode);
                            if (After != Before)
                                commands::Run(System, commands::MakeSetProperties(Id, Before, After));
                        };

                        ImGui::PushID((int)PinId);
                        ImGui::SetNextItemAllowOverlap();
                        ImGui::SetCursorScreenPos(ToScreen({ pRow->m_X + 12.0f, RowY + geo::ROW_H }));
                        ImGui::SetNextItemWidth(ToScreenLen(pRow->m_W - 24.0f));

                        if (bIsInt)
                        {
                            int Val = 0; try { Val = std::stoi(CurrentText); } catch (...) {}
                            if (ImGui::InputInt("##lit", &Val, 0, 0))
                                CommitLiteral(std::to_string(Val), xproperty::any{ static_cast<std::int32_t>(Val) });
                        }
                        else
                        {
                            float Val = 0.0f; try { Val = std::stof(CurrentText); } catch (...) {}
                            if (ImGui::InputFloat("##lit", &Val, 0.0f, 0.0f, "%.3f"))
                                CommitLiteral(std::format("{}", Val), xproperty::any{ Val });
                        }
                        ImGui::PopID();
                    }
                    else
                    {
                        const char* pPreview = PortTypeToPreview(pEffType, pValue);
                        if (pPreview[0] != '\0')
                        {
                            // Screen-space, same reasoning as the title/category fix above.
                            const ImVec2 ValSizePx = ImGui::GetFont()->CalcTextSizeA(FontSize, FLT_MAX, 0.0f, pPreview);
                            const ImVec2 AnchorPx = ToScreen({ pRow->m_X + pRow->m_W * 0.5f, RowY + geo::ROW_H });
                            pDraw->AddText(nullptr, FontSize, ImVec2(AnchorPx.x - ValSizePx.x * 0.5f, AnchorPx.y), IM_COL32(148, 163, 184, 255), pPreview);
                        }
                    }
                }

                RowY += RH;
            }

            // Inline enum-property widgets, directly in the node body - e.g. Compare's own Operator
            // choice, not only reachable through the side properties panel. Reuses the exact same
            // xproperty reflection every other property path already uses; a change goes through the
            // same undo-safe SetProperties command the side panel issues (see DrawNodePropertiesPanel).
            if (pNode && pNode->m_pNode && HasSerializableProperties(pNode->m_pNode))
            {
                xnode_os_node* pRealNode = pNode->m_pNode;
                const xproperty::type::object* pObj = pRealNode->getProperties();
                for (auto& M : pObj->m_Members)
                {
                    auto* pVar = std::get_if<xproperty::type::members::var>(&M.m_Variant);
                    if (!pVar || !pVar->m_AtomicType.m_IsEnum) continue;

                    const int CurrentVal = ReadEnumAsInt(M, pRealNode);
                    const char* pCurrentName = "?";
                    for (auto& Item : pVar->m_AtomicType.m_RegisteredEnumSpan)
                        if ((int)Item.m_Value == CurrentVal) { pCurrentName = Item.m_pName; break; }

                    // Compare's own Operator choice narrows to Equal/Not Equal whenever its A/B pins
                    // (see ResolveNodeWildcardType) haven't resolved to an orderable atomic type -
                    // Float is the only one today (E27_NodeOS_Editor.cpp's IsNoPreviewType comment) -
                    // a struct-like comparison has no meaningful </<=/>/>=. This is UI-only filtering
                    // of the dropdown's own choices, not a change to what's stored. Matched by the
                    // enum's own underlying VALUE (2 = EQUAL, 3 = NOT_EQUAL in compare_node.cpp's own
                    // declaration order), not by display-name text - the host already has to know this
                    // plugin's specific 0..5 ordering elsewhere (EmitOrdinaryNode's own "Compare" case
                    // maps the same numbers to operator tokens), and a value match survives a display-
                    // text rename for free, unlike the name-based check this replaced.
                    const bool bIsCompareOperator = pRealNode->m_pFactory->getName() == "Compare" && std::strcmp(M.m_pName, "Operator") == 0;
                    const char* pResolvedCompareType = bIsCompareOperator ? ResolveNodeWildcardType(Id, pDesc, Nodes, Links) : nullptr;
                    const bool bOrderable = !bIsCompareOperator || (pResolvedCompareType && std::strcmp(pResolvedCompareType, "Float") == 0);

                    ImGui::PushID((int)Id);
                    ImGui::PushID(M.m_pName);
                    ImGui::SetNextItemAllowOverlap();
                    ImGui::SetCursorScreenPos(ToScreen({ pRow->m_X + 10.0f, RowY }));
                    ImGui::SetNextItemWidth(ToScreenLen(pRow->m_W - 20.0f));
                    if (ImGui::BeginCombo("##enum", pCurrentName))
                    {
                        for (auto& Item : pVar->m_AtomicType.m_RegisteredEnumSpan)
                        {
                            if (!bOrderable && (int)Item.m_Value != 2 && (int)Item.m_Value != 3)
                                continue;
                            const bool bIsSel = ((int)Item.m_Value == CurrentVal);
                            if (ImGui::Selectable(Item.m_pName, bIsSel))
                            {
                                const std::string Before = SerializePropertiesToString(pRealNode);
                                WriteEnumFromInt(M, pRealNode, (int)Item.m_Value);
                                const std::string After = SerializePropertiesToString(pRealNode);
                                if (After != Before)
                                    commands::Run(System, commands::MakeSetProperties(Id, Before, After));
                            }
                            if (bIsSel) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                    ImGui::PopID();
                    RowY += geo::ROW_H + 4.0f;
                }

                // Constant's own numeric Value, directly in the node body too - not just the side
                // panel - same look as Compare/Math Expression's inline literal on an unconnected
                // Any pin. A name-based special case, same as Compare's operator-filtering above:
                // constant_node.cpp now reflects one PROPERLY-TYPED member per Type (Value Float/
                // Value Int/Value Short/Value Bool, each shown/saved only when active - see that
                // file), rather than one shared string - picking the right widget AND the right
                // member name both need to know this node is specifically a Constant and read its
                // own sibling Type member; a fully generic "any property" inline editor wouldn't.
                if (pRealNode->m_pFactory->getName() == "Constant")
                {
                    const xproperty::type::members* pTypeMember = nullptr;
                    for (auto& M : pObj->m_Members)
                        if (std::strcmp(M.m_pName, "Type") == 0) { pTypeMember = &M; break; }
                    if (pTypeMember)
                    {
                        const char* pTypeName = "Float";
                        if (auto* pTypeVar = std::get_if<xproperty::type::members::var>(&pTypeMember->m_Variant))
                        {
                            const int TypeVal = ReadEnumAsInt(*pTypeMember, pRealNode);
                            for (auto& Item : pTypeVar->m_AtomicType.m_RegisteredEnumSpan)
                                if ((int)Item.m_Value == TypeVal) { pTypeName = Item.m_pName; break; }
                        }

                        const bool  bIsBool  = std::strcmp(pTypeName, "Bool")  == 0;
                        const bool  bIsInt   = std::strcmp(pTypeName, "Int")   == 0;
                        const bool  bIsShort = std::strcmp(pTypeName, "Short") == 0;
                        const char* pValueName = bIsBool ? "Value Bool" : bIsInt ? "Value Int" : bIsShort ? "Value Short" : "Value Float";

                        const xproperty::type::members* pValueMember = nullptr;
                        for (auto& M : pObj->m_Members)
                            if (std::strcmp(M.m_pName, pValueName) == 0) { pValueMember = &M; break; }

                        if (pValueMember)
                        {
                            auto CommitValue = [&](const xproperty::any& NewValue)
                            {
                                const std::string Before = SerializePropertiesToString(pRealNode);
                                xproperty::any In = NewValue; xproperty::settings::context WriteCtx;
                                (void)pValueMember->TryWrite(pRealNode, In, WriteCtx);
                                const std::string After = SerializePropertiesToString(pRealNode);
                                if (After != Before)
                                    commands::Run(System, commands::MakeSetProperties(Id, Before, After));
                            };

                            xproperty::any Out; xproperty::settings::context ReadCtx;
                            (void)pValueMember->TryRead(pRealNode, Out, ReadCtx);

                            ImGui::PushID((int)Id);
                            ImGui::PushID("ConstValue");
                            ImGui::SetNextItemAllowOverlap();
                            ImGui::SetCursorScreenPos(ToScreen({ pRow->m_X + 10.0f, RowY }));
                            ImGui::SetNextItemWidth(ToScreenLen(pRow->m_W - 20.0f));
                            if (bIsBool)
                            {
                                bool Val = Out.is<bool>() && Out.get<bool>();
                                if (ImGui::Checkbox("##constval", &Val))
                                    CommitValue(xproperty::any{ Val });
                            }
                            else if (bIsInt)
                            {
                                int Val = Out.is<std::int32_t>() ? Out.get<std::int32_t>() : 0;
                                if (ImGui::InputInt("##constval", &Val, 0, 0))
                                    CommitValue(xproperty::any{ static_cast<std::int32_t>(Val) });
                            }
                            else if (bIsShort)
                            {
                                int Val = Out.is<std::int16_t>() ? Out.get<std::int16_t>() : 0;
                                if (ImGui::InputInt("##constval", &Val, 0, 0))
                                    CommitValue(xproperty::any{ static_cast<std::int16_t>(Val) });
                            }
                            else
                            {
                                float Val = Out.is<float>() ? Out.get<float>() : 0.0f;
                                if (ImGui::InputFloat("##constval", &Val, 0.0f, 0.0f, "%.3f"))
                                    CommitValue(xproperty::any{ Val });
                            }
                            ImGui::PopID();
                            ImGui::PopID();
                            RowY += geo::ROW_H + 4.0f;
                        }
                    }
                }
            }

            if (!pNode->m_LastError.empty())
            {
                ImGui::SetNextItemAllowOverlap();
                ImGui::SetCursorScreenPos(ToScreen({ pRow->m_X + 8.0f, pRow->m_Y + pRow->m_H - 16.0f }));
                ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "%s", pNode->m_LastError.c_str());
            }
            // Fades the WHOLE node - drawn last, on top of everything else just rendered for it
            // (header, ports, inline widgets, error text) - when it has no port that could legally
            // accept the connection currently being dragged (see NodeAcceptsDrag). A dark, mostly-
            // opaque overlay matching the canvas background rather than touching every individual
            // color computed above - far less invasive than threading an alpha factor through this
            // whole block, and just as effective visually.
            if (Drag.m_bActive && !NodeAcceptsDrag(Id))
                pDraw->AddRectFilled(P0, P1, WithAlpha(theme::CanvasDark, 195.0f / 255.0f), 0.0f);

            ImGui::PopID();
        }

        // ---- the spine control: a "+" marker (with its box, and now two circles) in every gap of
        // every spine (before its first node, between each consecutive pair, after its last - or just
        // one, for a currently-empty spine) - left-click one to insert a node at that exact position;
        // drag a circle to grow a whole new spine off it. This is the "skeleton" the user asked to make
        // the stacking order itself directly editable, now extended to grow sideways too.
        struct marker_pos { std::uint64_t m_SpineId; int m_GapIndex; float m_X, m_Y; };
        std::vector<marker_pos> MarkerPositions;
        for (auto& Sp : Spines)
        {
            auto& SL = SpineLayout[Sp.m_Id];
            const float MarkerX = ColumnX[Sp.m_ColumnId];

            auto DrawInsertMarker = [&](int GapIndex)
            {
                const float Y = SpineAbsY[Sp.m_Id] + GapRelY(SL, GapIndex);
                MarkerPositions.push_back({ Sp.m_Id, GapIndex, MarkerX, Y });
                const bool   bSelected = (Selection.m_SelectedGapSpineId == Sp.m_Id && Selection.m_SelectedGapIndex == GapIndex);
                const ImVec2 Center = ToScreen({ MarkerX, Y });
                const float  HalfW = ToScreenLen(28.0f), HalfH = ToScreenLen(13.0f);
                const ImVec2 BoxMin{ Center.x - HalfW, Center.y - HalfH }, BoxMax{ Center.x + HalfW, Center.y + HalfH };
                const float  PlusR = ToScreenLen(10.0f);
                const ImVec2 PlusMin{ Center.x - PlusR, Center.y - PlusR }, PlusMax{ Center.x + PlusR, Center.y + PlusR };
                const bool bPlusHovered = ImGui::IsMouseHoveringRect(PlusMin, PlusMax);

                // Two independent things sharing this slot: a selectable box (clicking anywhere in it,
                // outside the + itself, selects it the same way clicking a node does - a future Ctrl+V
                // paste will target the current selection) and the + button (always opens the insert
                // popup, regardless of selection state - pressing it never itself selects the box).
                pDraw->AddRectFilled(BoxMin, BoxMax, theme::NodeBg, 0.0f);
                pDraw->AddRect(BoxMin, BoxMax, bSelected ? theme::Selected : theme::NodeBorder
                              , 0.0f, 0, ToScreenLen(bSelected ? 2.0f : 1.2f));

                pDraw->AddCircleFilled(Center, PlusR, bPlusHovered ? IM_COL32(56, 130, 246, 255) : IM_COL32(30, 41, 59, 255));
                pDraw->AddCircle(Center, PlusR, IM_COL32(100, 116, 139, 255), 0, ToScreenLen(1.2f));
                const float Arm = PlusR * 0.45f;
                pDraw->AddLine({ Center.x - Arm, Center.y }, { Center.x + Arm, Center.y }, IM_COL32(226, 232, 240, 255), ToScreenLen(1.5f));
                pDraw->AddLine({ Center.x, Center.y - Arm }, { Center.x, Center.y + Arm }, IM_COL32(226, 232, 240, 255), ToScreenLen(1.5f));

                ImGui::PushID("spine_insert");
                ImGui::PushID((int)Sp.m_Id); ImGui::PushID((int)(Sp.m_Id >> 32));
                ImGui::PushID(GapIndex);

                // Box hit region, submitted first (bigger, covers the + button's area too).
                ImGui::SetNextItemAllowOverlap();
                ImGui::SetCursorScreenPos(BoxMin);
                ImGui::InvisibleButton("box", ImVec2(HalfW * 2, HalfH * 2));
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                {
                    // Addressed relative to the node currently on one side of this gap, same reasoning
                    // as CreateNode's own -After/-Before (a raw, shifting GapIndex isn't something
                    // worth serializing into a durable command) - or, for a currently-empty spine, the
                    // spine itself (-MarkerSpine, legal only in that case).
                    const std::string Cmd = SL.m_Order.empty() ? commands::MakeSelectMarkerSpine(Sp.m_Id)
                        : (GapIndex < (int)SL.m_Order.size() ? commands::MakeSelectMarkerBefore(SL.m_Order[GapIndex]) : commands::MakeSelectMarkerAfter(SL.m_Order.back()));
                    commands::Run(System, Cmd);
                }

                // + button, submitted after with AllowOverlap - wins the click over the box beneath it
                // for its own (smaller) area, so clicking the + specifically never also selects the box.
                ImGui::SetNextItemAllowOverlap();
                ImGui::SetCursorScreenPos(PlusMin);
                ImGui::InvisibleButton("plus", ImVec2(PlusR * 2, PlusR * 2));
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                    ImGui::OpenPopup("NodeOS_SpineInsertPopup");

                if (ImGui::BeginPopup("NodeOS_SpineInsertPopup"))
                {
                    if (Sources.empty())
                        ImGui::TextDisabled("No plugin sources found under Plugins/.");
                    for (auto& Src : Sources)
                        if (ImGui::MenuItem(Src.m_DisplayName.c_str()))
                            InsertNodeAt(Sp.m_Id, GapIndex, Src);

                    // Only a real split when there's something on both sides of this exact gap - splitting
                    // at the very top (nothing above) or the very bottom (nothing below) wouldn't actually
                    // separate anything. The new spine lands in the SAME column, anchored at this gap's own
                    // current absolute Y, so its nodes stay exactly where they visually already are.
                    if (GapIndex > 0 && GapIndex < (int)SL.m_Order.size())
                    {
                        ImGui::Separator();
                        if (ImGui::MenuItem("Split spine here"))
                        {
                            const auto NewSpineId = xresource::guid_generator::Instance64();
                            const std::vector<std::uint64_t> Trailing(SL.m_Order.begin() + GapIndex, SL.m_Order.end());
                            commands::Run(System, commands::MakeCreateSpineExistingColumn(NewSpineId, Y, Sp.m_ColumnId));
                            commands::Run(System, commands::MakeMoveNodesToSpineIn(Trailing, NewSpineId));
                        }
                    }
                    ImGui::EndPopup();
                }

                // The two spine-control circles, positioned OUTSIDE the box's own edges so they never
                // overlap its click area (the same overlapping-InvisibleButton lesson this file already
                // learned once). Always present, even on a currently-empty spine's own lone placeholder -
                // that's the only way to grab and drag a still-empty spine anywhere.
                {
                    const float CircleR   = ToScreenLen(geo::SPINE_CIRCLE_R);
                    const float CircleGap = ToScreenLen(geo::SPINE_CIRCLE_GAP);
                    const ImVec2 CircleCenters[2] = { { BoxMin.x - CircleGap - CircleR, Center.y }, { BoxMax.x + CircleGap + CircleR, Center.y } };
                    for (int Side = 0; Side < 2; ++Side)
                    {
                        ImGui::PushID(Side);
                        ImGui::SetNextItemAllowOverlap();
                        ImGui::SetCursorScreenPos({ CircleCenters[Side].x - CircleR, CircleCenters[Side].y - CircleR });
                        ImGui::InvisibleButton("circle", ImVec2(CircleR * 2, CircleR * 2));
                        const bool bHovered = ImGui::IsItemHovered();
                        if (ImGui::IsItemActivated())
                        {
                            SpineDrag.m_bActive = false; // only flips true past the drag threshold below
                            SpineDrag.m_SpineId = Sp.m_Id;
                            SpineDrag.m_GrabY   = Y;
                        }
                        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f))
                            SpineDrag.m_bActive = true;
                        pDraw->AddCircleFilled(CircleCenters[Side], CircleR, bHovered ? IM_COL32(94, 234, 212, 255) : theme::NodeBorder);
                        pDraw->AddCircle(CircleCenters[Side], CircleR, IM_COL32(148, 163, 184, 255), 0, ToScreenLen(1.2f));
                        ImGui::PopID();
                    }
                }

                ImGui::PopID();
                ImGui::PopID(); ImGui::PopID();
                ImGui::PopID();
            };

            for (int GapIndex = 0; GapIndex <= (int)SL.m_Order.size(); ++GapIndex)
                DrawInsertMarker(GapIndex);
        }
        ImGui::SetWindowFontScale(1.0f); // matches the SetWindowFontScale(View.m_Zoom) set before this loop began

        // Resolve a node-drag drop by direct distance to MouseLocal, same pattern as the pin-to-pin
        // drag-to-connect resolution below - NOT the marker's own ImGui hover state. A marker sitting
        // under an ACTIVE (held-down) different widget (the dragged node's own body) is exactly the
        // overlapping-item scenario this codebase has already been burned by once
        // (xgpu_imgui_overlapping_invisible_buttons); hit-testing the cursor position directly sidesteps
        // it entirely instead of relying on getting every AllowOverlap/ActiveId interaction exactly right.
        if (NodeDrag.m_bActive && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            std::uint64_t BestSpineId = 0; int BestGap = -1; float Best = 40.0f;
            for (auto& M : MarkerPositions)
            {
                const float D = std::hypot(M.m_X - MouseLocal.x, M.m_Y - MouseLocal.y);
                if (D < Best) { Best = D; BestSpineId = M.m_SpineId; BestGap = M.m_GapIndex; }
            }
            // MoveNodesTo silently cancels on its own if the moving set doesn't already live entirely
            // in BestSpineId - cross-spine drag-reorder isn't supported yet.
            if (BestGap >= 0) MoveNodesTo(BestSpineId, NodeDrag.m_MovingIds, BestGap);
            NodeDrag.m_bActive = false;
        }

        // A dragged node that isn't dropped on a marker just cancels - it never had a floating ghost
        // position to snap back from. A thin line from each moving node's own slot to the cursor is the
        // only in-flight feedback (drawn here, once the markers above already had first chance to
        // consume the drop this frame).
        if (NodeDrag.m_bActive)
        {
            for (auto MovingId : NodeDrag.m_MovingIds)
            {
                auto* pRow = FindRow(MovingId);
                if (!pRow) continue;
                pDraw->AddLine(ToScreen({ pRow->m_X + pRow->m_W * 0.5f, pRow->m_Y + pRow->m_H * 0.5f }), ImGui::GetIO().MousePos
                              , IM_COL32(56, 189, 248, 180), ToScreenLen(1.5f));
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                NodeDrag.m_bActive = false;
        }

        // ---- spine-control drag: dropping on empty space within the dragged spine's own column
        // relocates it there (Step 1, unchanged); dropping on one of the "+" targets shown for every
        // OTHER existing column, or in the gaps between/around columns, attaches a new empty spine to
        // that column or splices in a brand-new one (Step 2). Every target is visible for the whole
        // drag, not just once some threshold is crossed - "whenever we drag the circle."
        if (SpineDrag.m_bActive)
        {
            const std::uint64_t OwnColumnId = ColumnOfSpine[SpineDrag.m_SpineId];
            const ImVec2 GrabWorldPos{ ColumnX[OwnColumnId], SpineDrag.m_GrabY };

            pDraw->AddLine(ToScreen(GrabWorldPos), ImGui::GetIO().MousePos, IM_COL32(94, 234, 212, 220), ToScreenLen(2.0f));

            // Every column, left to right, walking outward from the dragged spine's own.
            std::vector<column*> Ordered;
            {
                auto* pLeftmost = FindColumn(OwnColumnId);
                while (pLeftmost && pLeftmost->m_LeftId) pLeftmost = FindColumn(pLeftmost->m_LeftId);
                for (auto* p = pLeftmost; p; p = FindColumn(p->m_RightId)) Ordered.push_back(p);
            }

            // An existing-column "+" sits at that column's own center X, MouseLocal.y for its own Y -
            // exactly where every one of that column's own spine-control markers already lives too,
            // since they share the same X. When the mouse is close enough to one of those markers that
            // dropping there should merge into it instead (see the merge resolution below), the "+"
            // itself steps out of the way for the frame rather than winning the pick purely on X and
            // blocking the more specific target underneath it.
            auto NearOtherMarker = [&](std::uint64_t ColId)
            {
                for (auto& M : MarkerPositions)
                    if (M.m_SpineId != SpineDrag.m_SpineId && ColumnOfSpine[M.m_SpineId] == ColId && std::abs(M.m_Y - MouseLocal.y) < 40.0f)
                        return true;
                return false;
            };

            // One "+" per EXISTING column, including the dragged spine's own (drop -> attach a new,
            // empty spine there - the own-column one lands right in the dragged spine's own column
            // without disturbing it, same as any other column's), plus one in every gap between/around
            // columns (drop -> splice in a brand-new column).
            struct drop_target { std::uint64_t m_ColumnId; bool m_bNewColumn; bool m_bBetween; std::uint64_t m_NeighborColumnId; char m_Side; float m_X; };
            std::vector<drop_target> Targets;
            for (std::size_t i = 0; i < Ordered.size(); ++i)
            {
                auto* pCol = Ordered[i];
                if (!NearOtherMarker(pCol->m_Id))
                    Targets.push_back({ pCol->m_Id, false, false, 0, 'R', ColumnX[pCol->m_Id] });

                if (i == 0)
                {
                    const float GhostX = ColumnX[pCol->m_Id] - (Extent(pCol->m_Id, 'L') + geo::COLUMN_MARGIN + HighwayBaseOf(0));
                    Targets.push_back({ 0, true, false, pCol->m_Id, 'L', GhostX });
                }
                if (i + 1 < Ordered.size())
                {
                    // Midpoint of the actual GAP between the two columns' own facing highway edges -
                    // NOT the midpoint of their centers, which drifts toward whichever column is
                    // narrower whenever the two have different highway extents.
                    auto* pNext = Ordered[i + 1];
                    const float MidX = (ColumnX[pCol->m_Id] + Extent(pCol->m_Id, 'R') + ColumnX[pNext->m_Id] - Extent(pNext->m_Id, 'L')) * 0.5f;
                    Targets.push_back({ 0, true, true, pCol->m_Id, 'R', MidX });
                }
                else
                {
                    const float GhostX = ColumnX[pCol->m_Id] + (Extent(pCol->m_Id, 'R') + geo::COLUMN_MARGIN + HighwayBaseOf(0));
                    Targets.push_back({ 0, true, false, pCol->m_Id, 'R', GhostX });
                }
            }

            // The "+" targets follow the mouse's own Y continuously (not the fixed grab point) - the
            // user aims each one exactly where the new/attached spine should land. Drawn bigger than
            // the spine-control marker's own "+" (radius 10) since there can be many of these at once,
            // scattered across the whole canvas - they need to read clearly at a glance while dragging.
            // The BETWEEN-column ones are the exception: kept smaller than that so there's still room
            // in a narrow gap to drop past the "+" and land the spine directly in one of the two
            // flanking columns instead. PickRadiusFor is the ONE source of truth for "is the mouse over
            // this target" - shared by the highlight below and the actual drop resolution on release, so
            // a "+" never lights up as armed for a wider area than what will really register the drop.
            auto PickRadiusFor = [&](const drop_target& T) { return ToScreenLen(T.m_bBetween ? 9.0f : 20.0f); };
            for (auto& T : Targets)
            {
                const ImVec2 TCenter = ToScreen({ T.m_X, MouseLocal.y });
                const float  TR = ToScreenLen(T.m_bBetween ? 9.0f : 16.0f);
                const bool   bHere = std::abs(ImGui::GetIO().MousePos.x - TCenter.x) < PickRadiusFor(T);
                pDraw->AddCircleFilled(TCenter, TR, bHere ? IM_COL32(56, 130, 246, 255) : (T.m_bNewColumn ? IM_COL32(30, 41, 59, 180) : IM_COL32(30, 41, 59, 255)));
                pDraw->AddCircle(TCenter, TR, IM_COL32(100, 116, 139, 255), 0, ToScreenLen(1.2f));
                const float TArm = TR * 0.45f;
                pDraw->AddLine({ TCenter.x - TArm, TCenter.y }, { TCenter.x + TArm, TCenter.y }, IM_COL32(226, 232, 240, 220), ToScreenLen(1.5f));
                pDraw->AddLine({ TCenter.x, TCenter.y - TArm }, { TCenter.x, TCenter.y + TArm }, IM_COL32(226, 232, 240, 220), ToScreenLen(1.5f));
            }

            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                drop_target* pBest = nullptr; float BestD = 0.0f;
                for (auto& T : Targets)
                {
                    const float PickR = PickRadiusFor(T);
                    const float D = std::abs(ImGui::GetIO().MousePos.x - ToScreen({ T.m_X, MouseLocal.y }).x);
                    if (D < PickR && (!pBest || D < BestD)) { BestD = D; pBest = &T; }
                }

                if (pBest)
                {
                    const auto NewSpineId = xresource::guid_generator::Instance64();
                    const std::string Cmd = pBest->m_bNewColumn
                        ? commands::MakeCreateSpineNewColumn(NewSpineId, MouseLocal.y, pBest->m_NeighborColumnId, pBest->m_Side, xresource::guid_generator::Instance64())
                        : commands::MakeCreateSpineExistingColumn(NewSpineId, MouseLocal.y, pBest->m_ColumnId);
                    commands::Run(System, Cmd);
                }
                else
                {
                    // Dropped on a DIFFERENT spine's own node-gap marker (not the floating column "+"
                    // above, a stationary one belonging to an already-populated spine) - merge the whole
                    // dragged spine's nodes in at that exact gap, same as dragging a set of nodes onto
                    // another spine already does, and the old nodes at/after that gap shift down to make
                    // room (MoveNodesToSpine's own renumbering). The dragged spine ends up empty either
                    // way, so remove it the same way any empty spine goes - but only if the move actually
                    // happened; MoveNodesTo silently no-ops on a blocked append, and an empty spine has
                    // nothing to merge in the first place.
                    std::uint64_t MergeTargetSpineId = 0; int MergeTargetGap = -1; float MergeBestD = 40.0f;
                    for (auto& M : MarkerPositions)
                    {
                        if (M.m_SpineId == SpineDrag.m_SpineId) continue;
                        const float D = std::hypot(M.m_X - MouseLocal.x, M.m_Y - MouseLocal.y);
                        if (D < MergeBestD) { MergeBestD = D; MergeTargetSpineId = M.m_SpineId; MergeTargetGap = M.m_GapIndex; }
                    }

                    std::vector<std::uint64_t> MergingIds;
                    if (MergeTargetGap >= 0)
                        for (auto& N : Nodes) if (N.m_SpineId == SpineDrag.m_SpineId) MergingIds.push_back(N.m_Id);

                    if (!MergingIds.empty())
                    {
                        std::sort(MergingIds.begin(), MergingIds.end(), [&](std::uint64_t A, std::uint64_t B)
                                 { auto* pA = FindNode(A); auto* pB = FindNode(B); return (pA ? pA->m_Order : 0) < (pB ? pB->m_Order : 0); });
                        MoveNodesTo(MergeTargetSpineId, MergingIds, MergeTargetGap);
                        bool bStillHasNodes = false;
                        for (auto& N : Nodes) if (N.m_SpineId == SpineDrag.m_SpineId) { bStillHasNodes = true; break; }
                        if (!bStillHasNodes)
                            commands::Run(System, commands::MakeDeleteSpine(SpineDrag.m_SpineId));
                    }
                    else
                    {
                        // Not on a "+" or another spine's marker either - dropped somewhere in a column's
                        // own space instead (own column or a different one), so move the dragged spine
                        // there, preserving the exact offset between the grabbed marker and the spine's
                        // own top so the drag feels WYSIWYG regardless of which one of its gaps was
                        // grabbed. Dropped in genuinely empty space (past every column and every "+" too)
                        // - splice a brand-new column in at the nearest edge and move it there instead.
                        auto SpineIt = std::find_if(Spines.begin(), Spines.end(), [&](auto& S) { return S.m_Id == SpineDrag.m_SpineId; });
                        if (SpineIt != Spines.end())
                        {
                            const float GrabOffsetFromTop = SpineDrag.m_GrabY - SpineAbsY[SpineDrag.m_SpineId];
                            const float NewTopY = MouseLocal.y - GrabOffsetFromTop;

                            column* pHit = nullptr;
                            for (auto* pCol : Ordered)
                                if (MouseLocal.x >= ColumnX[pCol->m_Id] - Extent(pCol->m_Id, 'L')
                                 && MouseLocal.x <= ColumnX[pCol->m_Id] + Extent(pCol->m_Id, 'R')) { pHit = pCol; break; }

                            if (pHit)
                            {
                                commands::Run(System, commands::MakeSetSpinePosition(SpineDrag.m_SpineId, NewTopY, pHit->m_Id));
                            }
                            else if (MouseLocal.x < ColumnX[Ordered.front()->m_Id] - Extent(Ordered.front()->m_Id, 'L'))
                            {
                                const auto NewColumnId = xresource::guid_generator::Instance64();
                                commands::Run(System, commands::MakeSetSpinePositionNewColumn(SpineDrag.m_SpineId, NewTopY, Ordered.front()->m_Id, 'L', NewColumnId));
                            }
                            else if (MouseLocal.x > ColumnX[Ordered.back()->m_Id] + Extent(Ordered.back()->m_Id, 'R'))
                            {
                                const auto NewColumnId = xresource::guid_generator::Instance64();
                                commands::Run(System, commands::MakeSetSpinePositionNewColumn(SpineDrag.m_SpineId, NewTopY, Ordered.back()->m_Id, 'R', NewColumnId));
                            }
                            else
                            {
                                for (std::size_t i = 0; i + 1 < Ordered.size(); ++i)
                                {
                                    auto* pA = Ordered[i]; auto* pB = Ordered[i + 1];
                                    const float AEdge = ColumnX[pA->m_Id] + Extent(pA->m_Id, 'R');
                                    const float BEdge = ColumnX[pB->m_Id] - Extent(pB->m_Id, 'L');
                                    if (MouseLocal.x >= AEdge && MouseLocal.x <= BEdge)
                                    {
                                        const bool bCloserToA = (MouseLocal.x - AEdge) <= (BEdge - MouseLocal.x);
                                        auto* pNeighbor = bCloserToA ? pA : pB;
                                        const char Side = bCloserToA ? 'R' : 'L';
                                        const auto NewColumnId = xresource::guid_generator::Instance64();
                                        commands::Run(System, commands::MakeSetSpinePositionNewColumn(SpineDrag.m_SpineId, NewTopY, pNeighbor->m_Id, Side, NewColumnId));
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                SpineDrag.m_bActive = false;
            }
        }

        // --- resolve a drag-to-connect drop: hit-test every port, validate direction+type, single-
        // connection-per-input eviction (Canvas.tsx's onUp + connect.ts's evictionCandidate) ---
        if (Drag.m_bActive && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            std::uint64_t TargetNode = 0; bool bTargetIsOutput = false; int TargetIndex = 0;
            float Best = geo::PORT_HIT_RADIUS;
            for (auto Id : Order)
            {
                auto* pDesc = DescOf(FindNode(Id));
                if (!pDesc) continue;
                for (auto& P : FlatPorts(pDesc))
                {
                    // A pin rendered on both sides has two valid drop anchors now - check whichever one
                    // the mouse is actually closest to.
                    for (char S : SidesOf(PinOf(P, Id), P.m_bIsOutput))
                    {
                        const ImVec2 A = PortAnchor(Id, P, S);
                        const float D = std::hypot(A.x - MouseLocal.x, A.y - MouseLocal.y);
                        if (D <= Best) { Best = D; TargetNode = Id; bTargetIsOutput = P.m_bIsOutput; TargetIndex = P.m_Index; }
                    }
                }
            }
            if (TargetNode && !(TargetNode == Drag.m_FromNode && bTargetIsOutput == Drag.m_bFromIsOutput && TargetIndex == Drag.m_FromIndex) && bTargetIsOutput != Drag.m_bFromIsOutput)
            {
                const std::uint64_t OutNode = Drag.m_bFromIsOutput ? Drag.m_FromNode : TargetNode;
                const int            OutIdx  = Drag.m_bFromIsOutput ? Drag.m_FromIndex : TargetIndex;
                const std::uint64_t InNode  = Drag.m_bFromIsOutput ? TargetNode : Drag.m_FromNode;
                const int            InIdx   = Drag.m_bFromIsOutput ? TargetIndex : Drag.m_FromIndex;
                auto* pOutDesc = DescOf(FindNode(OutNode)); auto* pInDesc = DescOf(FindNode(InNode));
                const auto OutOutputs = pOutDesc ? pOutDesc->getOutputs() : std::span<const xnode_os_port_desc>{};
                const auto InInputs   = pInDesc   ? pInDesc->getInputs()   : std::span<const xnode_os_port_desc>{};
                const bool bBoundsOk = pOutDesc && pInDesc && OutNode != InNode && OutIdx < (int)OutOutputs.size() && InIdx < (int)InInputs.size();
                // A still-open wildcard on either end (Compare/Math Expression's Any pins, ForEachLoop's
                // Span<Any> - see IsAnyKindOfWildcard) accepts whatever the other end is - the
                // connection is what resolves it - rather than requiring an exact strcmp match the way
                // every ordinary, concretely-typed pin still does.
                const bool bTypesCompatible = bBoundsOk && [&]
                {
                    // Copied into a real std::string IMMEDIATELY - see the identical hazard/fix note
                    // on the drag-preview ring check above (ResolveNodeWildcardType's container-
                    // unwrap path returns a pointer into a shared thread_local buffer the second call
                    // below could otherwise silently overwrite before this comparison runs).
                    const std::string OutEff = EffectiveTypeName(OutNode, pOutDesc, OutOutputs[OutIdx].m_pTypeName, Nodes, Links);
                    const char* pInEff = EffectiveTypeName(InNode, pInDesc, InInputs[InIdx].m_pTypeName, Nodes, Links);
                    return IsAnyKindOfWildcard(OutEff.c_str()) || IsAnyKindOfWildcard(pInEff) || OutEff == pInEff;
                }();
                if (bBoundsOk && bTypesCompatible)
                {
                    // Eviction of any prior link into the same target input happens inside Connect's
                    // own Redo() now (and its Undo() restores whatever got evicted) - see connect_cmd.
                    commands::Run(System, commands::MakeConnect(xresource::guid_generator::Instance64(), OutNode, OutIdx, InNode, InIdx));
                }
            }
            Drag.m_bActive = false;
        }

        if (bBackgroundClicked)
        {
            std::uint64_t HitLink = 0; float Best = geo::LINK_HIT_DIST;
            for (auto& Link : Links)
            {
                auto* pSrcDesc = DescOf(FindNode(Link.m_SourceNode)); auto* pDstDesc = DescOf(FindNode(Link.m_TargetNode));
                if (!pSrcDesc || !pDstDesc) continue;
                const auto SrcOutputs = pSrcDesc->getOutputs(); const auto DstInputs = pDstDesc->getInputs();
                const port_ref OutP{ true, Link.m_SourceOutput, &SrcOutputs[Link.m_SourceOutput] };
                const port_ref InP { false, Link.m_TargetInput,  &DstInputs[Link.m_TargetInput] };
                char SourceSide = 'R', TargetSide = 'R', RailSide = 'R';
                LinkSides(Link, SourceSide, TargetSide, RailSide);
                const ImVec2 From = PortAnchor(Link.m_SourceNode, OutP, SourceSide), To = PortAnchor(Link.m_TargetNode, InP, TargetSide);
                const float HX = HighwayX(OwnerColumnOf(Link), RailSide, LaneOfLink[Link.m_Id]);
                const ImVec2 Pts[4] = { From, { HX, From.y }, { HX, To.y }, To };
                for (int s = 0; s < 3; ++s) { const float D = DistPointSegment(MouseLocal, Pts[s], Pts[s + 1]); if (D < Best) { Best = D; HitLink = Link.m_Id; } }
            }
            commands::Run(System, HitLink ? commands::MakeSelectLink(HitLink) : commands::MakeClearSelection());
        }

        // No separate "background click/right-click for Add Node" anymore - right-click is pan-only now,
        // and the trailing spine marker (GapIndex == Layout.size(), drawn below) already covers
        // "add a node after the last one", so nothing is lost.

        if (bWindowHovered && ImGui::IsKeyPressed(ImGuiKey_Delete))
        {
            if (Selection.m_SelectedLink)
                commands::Run(System, commands::MakeDeleteLink(Selection.m_SelectedLink));
            else if (!Selection.m_SelectedNodes.empty())
                commands::Run(System, commands::MakeDeleteNodes({ Selection.m_SelectedNodes.begin(), Selection.m_SelectedNodes.end() }));
            else if (Selection.m_SelectedGapSpineId)
            {
                bool bHasNodes = false;
                for (auto& N : Nodes) if (N.m_SpineId == Selection.m_SelectedGapSpineId) { bHasNodes = true; break; }
                if (bHasNodes)
                {
                    // DeleteSpine itself would just refuse this - ask first, since it means taking every
                    // node on the spine (and every link touching them) with it.
                    DeleteSpineConfirm.m_SpineId = Selection.m_SelectedGapSpineId;
                    ImGui::OpenPopup("NodeOS_DeleteSpineConfirm");
                }
                else
                {
                    commands::Run(System, commands::MakeDeleteSpine(Selection.m_SelectedGapSpineId));
                }
            }
        }

        if (ImGui::BeginPopupModal("NodeOS_DeleteSpineConfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            int NodeCount = 0;
            for (auto& N : Nodes) if (N.m_SpineId == DeleteSpineConfirm.m_SpineId) ++NodeCount;
            ImGui::Text("Delete this spine and its %d node%s?", NodeCount, NodeCount == 1 ? "" : "s");
            ImGui::Separator();
            if (ImGui::Button("Delete", ImVec2(120, 0)))
            {
                std::vector<std::uint64_t> Ids;
                for (auto& N : Nodes) if (N.m_SpineId == DeleteSpineConfirm.m_SpineId) Ids.push_back(N.m_Id);
                if (!Ids.empty()) commands::Run(System, commands::MakeDeleteNodes(Ids));
                commands::Run(System, commands::MakeDeleteSpine(DeleteSpineConfirm.m_SpineId));
                DeleteSpineConfirm.m_SpineId = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                DeleteSpineConfirm.m_SpineId = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::End();
    }

    //------------------------------------------------------------------------------------------------
    // Dependency-respecting evaluation: repeatedly execute any not-yet-run node whose every
    // connected input already has a producer that has run, until nothing changes. Good enough for
    // the small acyclic graphs this proof of concept cares about. No longer needs AvailableTypes at
    // all - each node instance carries its own behavior directly (node_instance::m_pNode).
    //------------------------------------------------------------------------------------------------
    // ---- Real spine/exec-flow interpreter (NODE_SCRIPTING_DESIGN.md's exec-flow addition) ----
    // This is what actually RUNS the program, replacing the older pure-dataflow "run whatever's
    // ready" fixed point that used to live in ExecuteGraph (still fine for graphs with zero exec
    // involvement, but wrong once OnEvent/ExecutionCall/Function/Execute exist - that model has no
    // concept of spine order, scope, or "only run if actually triggered," and would call a node's
    // Execute() the moment its inputs looked ready regardless of whether anything ever invokes it).

    // Debugging/AI-facing entry point for xundo::history::Route (xundo_history.h) - a plain text box
    // for "Namespace/Edit-or-Query/Command -args..." strings, e.g. "NodeOS/Query/ListNodes" or
    // "NodeOS/Edit/Connect -Id ... ". Exists specifically so a query command never NEEDS a bespoke
    // ImGui widget to be reachable - the same text protocol an AI or a script would use to drive this
    // works here too, with the same self-documenting "-h" help every command already exposes.
    //
    // Help and autocomplete both key off xundo::history::GetRoutableCommands() - every registered
    // command's fully-qualified name ("NodeOS/Edit/Connect") paired with its own one-line
    // getCommandHelp() text, gathered fresh each call (command counts here are small - dozens, not
    // thousands - so there's no need to cache this across frames).
    //
    // Autocomplete scoring reuses the same weighted substring/prefix Damerau-Levenshtein distance
    // E10_TextureResourcePipeline's asset browser already uses for its own fuzzy search box
    // (E10_asset_browser_virtual_tree_tab.h) - xstrtool::SubstringDamerauLevenshteinDistanceI - rather
    // than inventing a second fuzzy-match convention. The result UI is deliberately much lighter than
    // that asset browser's icon grid/popup: a plain Selectable() list under the input is all a text
    // command needs.
    // Who authored a Command Console log entry - the log's own "> Cmd" echo line is colored by this
    // (User=green if typed into the UI, Pipe=teal if it arrived over NodeOSCLI's named pipe - an
    // AI-facing color on purpose); the RESULT that follows is always logged as its own separate
    // System-sourced entry (white), since the app authored that text, not whoever typed the command.
    // DrawCommandConsolePanel and PumpCommandConsolePipe both push an echo entry then, if non-empty,
    // a result entry - never one blended entry.
    enum class console_log_source { System, User, Pipe };
    struct console_log_entry
    {
        std::string         m_Text;
        console_log_source  m_Source;
    };

    // TextEditor (E19_TextEditor.h, already used for the "Generated C++" panel) is a real code-editor
    // widget: unlike InputTextMultiline it can color individual lines AND still give one continuous
    // mouse-drag/copy selection across the whole log - the two things a naive "one InputTextMultiline
    // per colored entry" approach couldn't do at once. Coloring is keyed off a plain-text marker
    // prefixed onto each User/Pipe echo line ("> "/"$ ") rather than any out-of-band per-line state,
    // because TextEditor's own tokenizer callback (LanguageDefinition::mTokenize) is a stateless C
    // function pointer with no way to be told which log entry a line came from - the marker IS the
    // only channel available. System lines carry no marker and fall through to the Default color.
    static bool ConsoleLogTokenize(const char* in_begin, const char* in_end, const char*& out_begin, const char*& out_end, TextEditor::PaletteIndex& paletteIndex)
    {
        const size_t Len = static_cast<size_t>(in_end - in_begin);
        if (Len >= 2 && in_begin[0] == '>' && in_begin[1] == ' ')
        {
            out_begin = in_begin; out_end = in_end; paletteIndex = TextEditor::PaletteIndex::KnownIdentifier; // green - User, see BuildConsoleLogPalette
            return true;
        }
        if (Len >= 2 && in_begin[0] == '$' && in_begin[1] == ' ')
        {
            out_begin = in_begin; out_end = in_end; paletteIndex = TextEditor::PaletteIndex::Preprocessor; // teal - Pipe, the AI's "favorite color"
            return true;
        }
        return false; // System line - falls through to Default
    }

    // A from-scratch LanguageDefinition rather than repurposing CPlusPlus()'s - this text isn't code,
    // it just needs mTokenize wired up and comment-detection neutralized. Leaving mCommentStart/
    // mCommentEnd/mSingleLineComment at their default EMPTY strings is a trap, not a no-op:
    // ColorizeInternal's comment scan treats an empty marker as matching at every position, which
    // would flag the entire log as "inside a multi-line comment" and force everything to the Comment
    // palette color ahead of mTokenize's own result (TextEditor::GetGlyphColor checks mComment/
    // mMultiLineComment first, before mColorIndex) - pointing all three at a marker that will never
    // appear in real log text sidesteps that entirely.
    static const TextEditor::LanguageDefinition& ConsoleLogLanguageDefinition() noexcept
    {
        static const TextEditor::LanguageDefinition LangDef = [] {
            TextEditor::LanguageDefinition Def;
            Def.mName            = "ConsoleLog";
            Def.mTokenize        = ConsoleLogTokenize;
            Def.mAutoIndentation = false;
            Def.mCommentStart = Def.mCommentEnd = Def.mSingleLineComment = "\x01\x01\x01__no_console_log_comment_marker__\x01\x01\x01";
            return Def;
        }();
        return LangDef;
    }

    static TextEditor::Palette BuildConsoleLogPalette() noexcept
    {
        TextEditor::Palette Pal = TextEditor::GetDarkPalette();
        Pal[(int)TextEditor::PaletteIndex::KnownIdentifier] = IM_COL32(115, 217, 115, 255); // green - User
        Pal[(int)TextEditor::PaletteIndex::Preprocessor]    = IM_COL32( 51, 191, 191, 255); // teal  - Pipe (AI)
        return Pal;
    }

    // The one place that decides what marker a line gets, so DrawCommandConsolePanel's rendering and
    // get_log_query_cmd's flattened CLI text agree - m_Text itself is always the RAW command/result
    // text with no marker baked in, exactly so there's only one place this happens, not two.
    static const char* ConsoleLogLinePrefix(console_log_source Source) noexcept
    {
        switch (Source)
        {
            case console_log_source::User: return "> ";
            case console_log_source::Pipe: return "$ ";
            default:                       return "";
        }
    }

    // Splits on '\n' without a regex/stream detour - SetTextLines wants one entry per line, and a log
    // entry's text is plain, host-authored strings, never anything exotic enough to need more than this.
    static std::vector<std::string> SplitLines(std::string_view Text)
    {
        std::vector<std::string> Out;
        std::size_t Start = 0;
        for (;;)
        {
            const std::size_t Nl = Text.find('\n', Start);
            Out.emplace_back(Text.substr(Start, Nl == std::string_view::npos ? std::string_view::npos : Nl - Start));
            if (Nl == std::string_view::npos) break;
            Start = Nl + 1;
        }
        return Out;
    }

    // UserData for CommandConsoleCallback, below - built fresh each frame from DrawCommandConsolePanel's
    // own local state, since a single free-function callback can't capture anything.
    struct command_console_nav_state
    {
        const std::vector<const xundo::history::routable_command*>* m_pScored;
        std::vector<std::string>*                                   m_pCmdHistory;
        int*                                                         m_pHighlighted;
        int*                                                         m_pHistoryPos;
        std::string*                                                 m_pHistoryStash;
        bool*                                                        m_pJustFilled;
        bool*                                                        m_pForceCursorEnd;
    };

    // Two jobs, dispatched by pData->EventFlag:
    //
    // CallbackAlways fires every frame the widget is active - forces the cursor to the end of the text
    // right after a programmatic buffer rewrite (a suggestion click, applied on the NEXT frame via
    // DrawCommandConsolePanel's own bApplyPendingFill/SetKeyboardFocusHere dance), since ImGui doesn't
    // reliably land it there on every version/path by default.
    //
    // CallbackHistory fires specifically when the WIDGET ITSELF is active and Up/Down are pressed -
    // this is the one correct way to handle arrow-key history/suggestion nav in an InputText. Polling
    // ImGui::IsKeyPressed(ImGuiKey_DownArrow) after the fact does NOT work here: this app has ImGui's
    // keyboard navigation enabled, which claims Up/Down globally to move focus between WIDGETS unless
    // something has already claimed those keys for itself - CallbackHistory is InputText's own,
    // documented way to make that claim, exactly for shell-style history use cases like this one.
    // Mutating the buffer via pData->DeleteChars/InsertChars (rather than writing CmdBuffer directly)
    // is what makes the change take effect immediately, from inside the widget's own active edit
    // session - no deferred "next frame" trick needed here, unlike the mouse-click pick path.
    static int CommandConsoleCallback(ImGuiInputTextCallbackData* pData)
    {
        auto* pState = static_cast<command_console_nav_state*>(pData->UserData);

        if (pData->EventFlag == ImGuiInputTextFlags_CallbackAlways)
        {
            if (*pState->m_pForceCursorEnd)
            {
                pData->CursorPos = pData->SelectionStart = pData->SelectionEnd = pData->BufTextLen;
                *pState->m_pForceCursorEnd = false;
            }
            return 0;
        }

        if (pData->EventFlag == ImGuiInputTextFlags_CallbackHistory)
        {
            auto& Scored       = *pState->m_pScored;
            auto& CmdHistory   = *pState->m_pCmdHistory;
            auto& Highlighted  = *pState->m_pHighlighted;
            auto& HistoryPos   = *pState->m_pHistoryPos;
            auto& HistoryStash = *pState->m_pHistoryStash;

            const bool bUp              = pData->EventKey == ImGuiKey_UpArrow;
            const bool bBrowsingHistory = HistoryPos != -1;

            // Prioritizes continuing an already-open history browse (so it doesn't flip back to
            // suggestion-nav just because a recalled line happens to also fuzzy-match something),
            // otherwise: text with live suggestions browses THOSE, an empty box walks CmdHistory.
            if ((bBrowsingHistory || Scored.empty()) && !CmdHistory.empty())
            {
                std::string NewText;
                if (bUp)
                {
                    if (HistoryPos == -1) { HistoryStash.assign(pData->Buf, pData->BufTextLen); HistoryPos = (int)CmdHistory.size() - 1; }
                    else                    HistoryPos = std::max(0, HistoryPos - 1);
                    NewText = CmdHistory[HistoryPos];
                }
                else // Down
                {
                    if (!bBrowsingHistory) return 0; // nothing to recall forward to - stay put
                    ++HistoryPos;
                    if (HistoryPos >= (int)CmdHistory.size()) { HistoryPos = -1; NewText = HistoryStash; }
                    else                                        NewText = CmdHistory[HistoryPos];
                }
                pData->DeleteChars(0, pData->BufTextLen);
                pData->InsertChars(0, NewText.c_str()); // also advances CursorPos to the end - "ready to add whatever you like"
                *pState->m_pJustFilled = true;
            }
            else if (!Scored.empty())
            {
                if (bUp) Highlighted = (Highlighted <= 0) ? (int)Scored.size() - 1 : Highlighted - 1;
                else     Highlighted = (Highlighted + 1 >= (int)Scored.size()) ? 0 : Highlighted + 1;
            }
            return 0;
        }

        return 0;
    }

    // Shared by DrawCommandConsolePanel's own Enter/Run handling AND the named-pipe server (below) -
    // one place implementing "help"/"<cmd> -h"/plain routing, so a pipe-driven command (from
    // NodeOSCLI, or any other external caller) behaves identically to one typed into the UI, not a
    // second, silently-drifting copy of the same dispatch logic.
    static std::string ProcessConsoleCommand(std::string_view Cmd, xundo::history& History, const std::vector<xundo::history::routable_command>& Routable)
    {
        std::string Out;
        if (Cmd == "help" || Cmd == "Help" || Cmd == "?")
        {
            for (auto& C : Routable)
                Out += (C.m_Help.empty() ? C.m_FullName : std::format("{} - {}", C.m_FullName, C.m_Help)) + "\n";
        }
        else if (Cmd.size() > 2 && Cmd.substr(Cmd.size() - 2) == "-h")
        {
            // Bypasses Route() here on purpose: the underlying command's own "-h" handling
            // (xundo::system::Execute/Query) writes straight to std::cout via xcmdline::
            // parser::printHelp() - nothing this GUI process's console window (or a pipe client)
            // ever sees - so look the help text up directly instead of dispatching.
            std::string_view FullName = Cmd.substr(0, Cmd.find(' '));
            Out = History.GetCommandHelpFor(FullName) + "\n";
        }
        else
        {
            // Route()'s empty-string return is ambiguous on purpose for Edit commands ("ran fine,
            // nothing worth reporting" - xundo::system::Execute's own convention), but that's wrong
            // for a Query: an empty answer ("no nodes") is itself real, useful information, not
            // "nothing happened" - silently swallowing it here is exactly what made a genuinely-
            // empty ListNodes result look like the command did nothing at all. Only Query commands
            // get an explicit placeholder for an empty answer; Edit commands keep the existing
            // silent-success convention.
            std::string Result = History.Route(Cmd);
            if (!Result.empty())
                Out = Result + "\n";
            else if (Cmd.find("/Query/") != std::string_view::npos)
                Out = "(empty result)\n";
        }
        return Out;
    }

    static void DrawCommandConsolePanel(xundo::history& History, std::vector<console_log_entry>& LogEntries)
    {
        static char                     CmdBuffer[256] = "";
        static std::string              PrevCmdBuffer;      // detects genuine typing vs. a programmatic fill, below
        // LogEntries is owned by the caller (E27_Example), not a local static here - the named-pipe
        // server (below) needs to append to the SAME visible log a pipe-driven command isn't a
        // secret side-channel from a UI reading over your shoulder.
        static std::string              PendingFill;        // value to apply into CmdBuffer, see bApplyPendingFill
        static bool                     bApplyPendingFill = false;
        static bool                     bRefocus  = false;
        static bool                     bForceCursorEnd = false; // consumed by CommandConsoleCallback's CallbackAlways branch, above
        static int                      Highlighted = -1;   // keyboard-selected suggestion row, -1 = none
        static std::vector<std::string> CmdHistory;         // previously RUN command lines, oldest first - shell-style recall
        static int                      HistoryPos = -1;    // index into CmdHistory currently shown, -1 = not browsing (fresh line)
        static std::string              HistoryStash;       // what was typed before Up first opened history browsing, restored on Down past the newest entry

        // One persistent TextEditor for the whole log's lifetime - matching the established
        // "Inspector must persist across frames" pattern (rebuilding a widget like this every frame
        // makes its internal state, here the colorizer's cached ranges and the scroll position,
        // unstable). SetImGuiChildIgnored(true) is what lets Render() below draw straight into a
        // BeginChild WE own (so it lives inside this "Command Console" panel, not as its own
        // separate top-level window - contrast with GeneratedCodeEditor, which IS its own window).
        static TextEditor LogEditor;
        static bool       bLogEditorInit = false;
        static size_t     LastRenderedEntryCount = ~size_t(0); // forces the first-frame build below
        if (!bLogEditorInit)
        {
            LogEditor.SetLanguageDefinition(ConsoleLogLanguageDefinition());
            LogEditor.SetPalette(BuildConsoleLogPalette());
            LogEditor.SetReadOnly(true);
            LogEditor.SetImGuiChildIgnored(true);
            LogEditor.SetShowWhitespaces(false);
            bLogEditorInit = true;
        }

        ImGui::SetNextWindowPos(ImVec2(1265, 610), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(340, 260), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Command Console"))
        {
            ImGui::TextDisabled("NodeOS/Edit/<Cmd> or NodeOS/Query/<Cmd> - Down/Up: suggestions, or history when empty");

            // Applying a picked suggestion/history entry (or refocusing) must happen HERE, immediately
            // before InputText is drawn - ImGui's InputText owns its own internal edit buffer once
            // active, and silently ignores an external write to CmdBuffer made AFTER it's already been
            // submitted this frame. SetKeyboardFocusHere(0) right before the call is what makes
            // InputText reload from CmdBuffer instead of keeping its own stale internal copy; the
            // paired bForceCursorEnd/callback (above) is what puts the cursor at the end of the newly
            // filled text. A dedicated bApplyPendingFill bool (rather than just checking
            // "!PendingFill.empty()") is what lets history-Down restore an EMPTY stashed line - an
            // empty PendingFill string still needs to be applied, it's not "nothing to do".
            if (bApplyPendingFill)
            {
                std::snprintf(CmdBuffer, sizeof(CmdBuffer), "%s", PendingFill.c_str());
                bApplyPendingFill = false;
                bRefocus = true;
                bForceCursorEnd = true;
            }
            if (bRefocus)
            {
                ImGui::SetKeyboardFocusHere(0);
                bRefocus = false;
            }

            const auto Routable = History.GetRoutableCommands(); // {m_FullName, m_Help} per command - see xundo_history.h

            // Computed BEFORE InputText (not after, as a first version of this did) specifically so
            // CommandConsoleCallback's CallbackHistory handling - which fires DURING the InputText
            // call below - has this frame's ranking ready when Up/Down are pressed. The one cost is
            // that the rendered suggestion list can lag a single frame behind a just-typed character;
            // arrow-key nav actually working is worth that trade.
            std::vector<const xundo::history::routable_command*> Scored;
            if (CmdBuffer[0])
            {
                struct scored_t { const xundo::history::routable_command* m_pCmd; std::size_t m_Distance; };
                std::vector<scored_t> Ranked;
                Ranked.reserve(Routable.size());
                for (auto& Cmd : Routable)
                    Ranked.push_back({ &Cmd, xstrtool::SubstringDamerauLevenshteinDistanceI(CmdBuffer, Cmd.m_FullName) });
                std::sort(Ranked.begin(), Ranked.end(), [](auto& A, auto& B) { return A.m_Distance < B.m_Distance; });

                const int MaxSuggestions = 8;
                Scored.reserve(std::min<std::size_t>(Ranked.size(), MaxSuggestions));
                for (int i = 0; i < (int)Ranked.size() && i < MaxSuggestions; ++i)
                    Scored.push_back(Ranked[i].m_pCmd);
            }

            static bool bJustFilled = false; // set by CommandConsoleCallback's history branch, or the mouse-pick paths below
            command_console_nav_state NavState{ &Scored, &CmdHistory, &Highlighted, &HistoryPos, &HistoryStash, &bJustFilled, &bForceCursorEnd };

            bool bEnter = ImGui::InputText("##cmd", CmdBuffer, sizeof(CmdBuffer)
                , ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_CallbackHistory
                , CommandConsoleCallback, &NavState);
            const bool bInputActive = ImGui::IsItemActive();
            ImGui::SameLine();
            bool bRunClicked = ImGui::SmallButton("Run");
            if (ImGui::SmallButton("Clear")) LogEntries.clear();

            // Distinguishes genuine typing from a programmatic fill (either CommandConsoleCallback's
            // own history-recall, just above, or a mouse-pick below) - only real typing should drop
            // the keyboard-highlighted suggestion and exit history-browsing mode.
            const bool bUserEdited = (CmdBuffer != PrevCmdBuffer) && !bJustFilled;
            bJustFilled = false;
            if (bUserEdited) { Highlighted = -1; HistoryPos = -1; }

            if (bInputActive && ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                CmdBuffer[0] = 0;
                Highlighted = -1;
                HistoryPos  = -1;
            }

            bool bPickedByKeyboard = false;
            if (!Scored.empty())
            {
                const float RowH = 18.0f;
                if (ImGui::BeginChild("##suggestions", ImVec2(0, std::min<float>(8, (float)Scored.size()) * RowH + 4), true))
                {
                    for (int i = 0; i < (int)Scored.size(); ++i)
                    {
                        auto& Cmd = *Scored[i];
                        ImGui::PushID(i);
                        if (ImGui::Selectable(Cmd.m_FullName.c_str(), Highlighted == i))
                        {
                            PendingFill = Cmd.m_FullName + " "; // trailing space - cursor lands ready for -args
                            bApplyPendingFill = true; bJustFilled = true;
                            Highlighted = -1;
                        }
                        if (Highlighted == i) ImGui::SetScrollHereY();
                        if (ImGui::IsItemHovered() && !Cmd.m_Help.empty())
                            ImGui::SetTooltip("%s", Cmd.m_Help.c_str());
                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();

                // Enter while a row is highlighted confirms THAT row instead of running the raw text -
                // matches how every other autocomplete (shell, IDE, address bar) treats Enter-on-a-
                // highlighted-item as "accept the suggestion," not "submit whatever's literally typed."
                if (bEnter && Highlighted >= 0)
                {
                    PendingFill = Scored[Highlighted]->m_FullName + " ";
                    bApplyPendingFill = true; bJustFilled = true;
                    Highlighted = -1;
                    bPickedByKeyboard = true;
                }
            }

            PrevCmdBuffer.assign(CmdBuffer);

            if (!bPickedByKeyboard && (bEnter || bRunClicked) && CmdBuffer[0])
            {
                std::string_view Cmd(CmdBuffer);
                // Trim trailing whitespace the autocomplete fill-in leaves behind.
                while (!Cmd.empty() && Cmd.back() == ' ') Cmd.remove_suffix(1);

                if (CmdHistory.empty() || CmdHistory.back() != Cmd)
                    CmdHistory.emplace_back(Cmd);
                HistoryPos = -1;

                LogEntries.push_back({ std::string(Cmd), console_log_source::User });
                if (std::string Result = ProcessConsoleCommand(Cmd, History, Routable); !Result.empty())
                    LogEntries.push_back({ std::move(Result), console_log_source::System });
                CmdBuffer[0] = 0;
                Highlighted = -1;
                bRefocus = true; // applied at the top of the panel on the NEXT frame - see bApplyPendingFill's own comment above
            }

            ImGui::Separator();

            // Rebuilt only when the log actually grew/shrank (not every frame) - SetTextLines resets
            // TextEditor's internal colorizer range and scroll position, so doing it unconditionally
            // would fight the user's own scrolling every single frame.
            if (LogEntries.size() != LastRenderedEntryCount)
            {
                std::vector<std::string> Lines;
                for (auto& Entry : LogEntries)
                {
                    const char* Prefix = ConsoleLogLinePrefix(Entry.m_Source);
                    for (auto& Line : SplitLines(Entry.m_Text))
                        Lines.push_back(Prefix + Line);
                }
                LogEditor.SetTextLines(Lines);
                LogEditor.SetCursorPosition(TextEditor::Coordinates((int)Lines.size(), 0)); // scrolls to the newest entry
                LastRenderedEntryCount = LogEntries.size();
            }

            // Own BeginChild (rather than letting Render() open its own top-level window) is what
            // SetImGuiChildIgnored(true), above, expects - this way the log lives inside the Command
            // Console panel, sharing space with the input box and suggestions above it.
            if (ImGui::BeginChild("##ConsoleLogChild", ImGui::GetContentRegionAvail(), true))
            {
                // Explicit empty callback, not Render's own defaulted decltype([](){}) - MSVC
                // independently re-evaluates a defaulted template default argument at each call
                // site, producing two DIFFERENT closure types for the same call and a hard error
                // (same issue GeneratedCodeEditor.Render's own call site works around).
                LogEditor.Render("##output", ImVec2(0, 0), false, [](){});
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

    //------------------------------------------------------------------------------------------------
    // Named-pipe server for NodeOSCLI.cpp - lets an external process (a script, an AI) drive the same
    // Route()/help dispatch the Command Console panel uses, without any UI automation at all: connect
    // to \\.\pipe\E27_NodeOS_Console, write one command line, read back the response, disconnect.
    //
    // Runs on its own background thread for the app's whole lifetime (launched once from E27_Example,
    // detached - this is a local dev/debug feature, not something that needs a graceful shutdown path;
    // the thread dies with the process). CreateNamedPipe/ConnectNamedPipe/ReadFile/WriteFile all block,
    // which is fine on a DEDICATED thread but must never run on the main render thread.
    //
    // The actual command dispatch can't happen on this pipe thread, though: Route() ultimately calls
    // into Redo()/Query() implementations that read/mutate the SAME Nodes/Links/etc. the main thread
    // is drawing and editing every frame - running that from a second thread would race. So this
    // thread only ever reads the request text and hands it to the main thread via a small mutex+
    // condition_variable bridge (command_console_pipe_bridge, below); the main loop's own per-frame
    // check (see E27_Example) does the actual ProcessConsoleCommand() call and posts the answer back.
    //------------------------------------------------------------------------------------------------
    struct command_console_pipe_bridge
    {
        std::mutex              m_Mutex;
        std::condition_variable m_Cond;
        bool                    m_bHasRequest  = false; // pipe thread -> main thread: a command is waiting
        bool                    m_bHasResponse = false; // main thread -> pipe thread: the answer is ready
        std::string             m_Request;
        std::string             m_Response;
    };

    static void CommandConsolePipeThreadMain(command_console_pipe_bridge& Bridge)
    {
        for (;;)
        {
            HANDLE hPipe = CreateNamedPipeA(
                "\\\\.\\pipe\\E27_NodeOS_Console",
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                1,      // one client at a time - NodeOSCLI is a one-shot connect/send/read/exit tool
                65536, 65536,
                0, nullptr);
            if (hPipe == INVALID_HANDLE_VALUE)
            {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            const BOOL bConnected = ConnectNamedPipe(hPipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
            if (bConnected)
            {
                std::string Request;
                char        Buf[4096];
                DWORD       BytesRead = 0;
                while (ReadFile(hPipe, Buf, sizeof(Buf) - 1, &BytesRead, nullptr) && BytesRead > 0)
                {
                    Buf[BytesRead] = 0;
                    Request += Buf;
                    if (Request.find('\n') != std::string::npos) break;
                }
                while (!Request.empty() && (Request.back() == '\n' || Request.back() == '\r')) Request.pop_back();

                std::string Response;
                if (!Request.empty())
                {
                    std::unique_lock<std::mutex> Lock(Bridge.m_Mutex);
                    Bridge.m_Request      = Request;
                    Bridge.m_bHasRequest  = true;
                    Bridge.m_bHasResponse = false;
                    Bridge.m_Cond.notify_all();
                    Bridge.m_Cond.wait(Lock, [&] { return Bridge.m_bHasResponse; });
                    Response = Bridge.m_Response;
                }

                DWORD BytesWritten = 0;
                WriteFile(hPipe, Response.data(), static_cast<DWORD>(Response.size()), &BytesWritten, nullptr);
                FlushFileBuffers(hPipe);
            }
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
        }
    }

    // Called once per frame from E27_Example's main loop - the only place that's actually safe to run
    // ProcessConsoleCommand() from, since it touches the same graph data the rest of the frame does.
    // Appends into LogEntries too (the SAME vector DrawCommandConsolePanel renders) so a pipe-driven
    // command is visible in the UI exactly like one typed there - never a silent side-channel. Tagged
    // console_log_source::Pipe rather than User so it renders in its own distinct color ("$ " marker,
    // teal) - see console_log_source's own comment for why that's a real, meaningful distinction and
    // not just decoration.
    static void PumpCommandConsolePipe(command_console_pipe_bridge& Bridge, xundo::history& History, std::vector<console_log_entry>& LogEntries)
    {
        std::unique_lock<std::mutex> Lock(Bridge.m_Mutex, std::try_to_lock);
        if (!Lock.owns_lock() || !Bridge.m_bHasRequest || Bridge.m_bHasResponse)
            return;

        const std::string Cmd = Bridge.m_Request;
        Lock.unlock();

        const auto Routable = History.GetRoutableCommands();
        const std::string Result = ProcessConsoleCommand(Cmd, History, Routable);

        LogEntries.push_back({ Cmd, console_log_source::Pipe });
        if (!Result.empty())
            LogEntries.push_back({ Result, console_log_source::System });

        Lock.lock();
        Bridge.m_Response     = Result;
        Bridge.m_bHasResponse = true;
        Bridge.m_bHasRequest  = false;
        Lock.unlock();
        Bridge.m_Cond.notify_all();
    }

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

        //--------------------------------------------------------------------------------------------
        // The one "database" every command mutates, retrieved via command_base::get<node_os_command_
        // context>() - plain references into E27_Example()'s own locals, not owned here.
        //--------------------------------------------------------------------------------------------
        struct node_os_command_context
        {
            std::vector<node_instance>&        m_Nodes;
            std::vector<link_instance>&        m_Links;
            canvas_selection&                  m_Selection;
            std::vector<plugin_source_entry>&  m_Sources;
            std::vector<available_node_type>&  m_AvailableTypes;
            bool&                              m_bDirty;
            std::vector<spine>&                 m_Spines;
            std::vector<column>&                m_Columns;
            std::vector<console_log_entry>&     m_ConsoleLog;     // the SAME log DrawCommandConsolePanel renders and
                                                                    // PumpCommandConsolePipe appends to - see get_log_query_cmd,
                                                                    // below, for why a query needs to read this back.
            bool&                               m_bScreenshotRequested; // arms the capture - see screenshot_query_cmd and
            std::string&                        m_ScreenshotPath;       // E27_Example's PageFlip hook, which actually writes the file.
            canvas_view&                        m_View;                 // pan/zoom - see SetView/GetView, below.
        };

        // Shared by select_cmd and clear_selection_cmd - both snapshot/restore the exact same set.
        inline void BackupSelection(node_os_command_context& Ctx, xundo::undo_file& File) noexcept
        {
            auto& S = Ctx.m_Selection;
            File.Write(static_cast<std::uint32_t>(S.m_SelectedNodes.size()));
            for (auto Id : S.m_SelectedNodes) File.Write(Id);
            File.Write(S.m_SelectedLink);
            File.Write(S.m_SelectedGapSpineId);
            File.Write(S.m_SelectedGapIndex);
        }
        inline void RestoreSelection(node_os_command_context& Ctx, xundo::undo_file& File) noexcept
        {
            auto& S = Ctx.m_Selection;
            std::uint32_t Count = 0; File.Read(Count);
            S.m_SelectedNodes.clear();
            for (std::uint32_t i = 0; i < Count; ++i) { std::uint64_t Id = 0; File.Read(Id); S.m_SelectedNodes.insert(Id); }
            File.Read(S.m_SelectedLink);
            File.Read(S.m_SelectedGapSpineId);
            File.Read(S.m_SelectedGapIndex);
        }

        //================================================================================================
        // CreateNode - addressed relative to an EXISTING node's id (-After/-Before), never a raw order
        // index or an invented "gap" identity: see the design discussion this replaced (a two-command
        // "InsertNode" group keyed by a shifting numeric GapIndex) for why. Resolving -After/-Before
        // against the CURRENT node list happens once, right here, at Redo() time - so a stale reference
        // (the node no longer exists by the time this runs) fails cleanly instead of guessing.
        //================================================================================================
        struct create_node_cmd : xundo::command_base
        {
            create_node_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "CreateNode", pDataBase) { RegisterArguments(); }

            const char* getCommandHelp() const noexcept override { return "Creates a node. Usage: CreateNode -Id N -PluginDir dirname [-TypeName name] [-After id | -Before id | -InSpine spineid]"; }
            void RegisterArguments() noexcept override
            {
                m_hId        = m_Parser.addOption("Id",        "Node id",                                           true,  1);
                m_hPluginDir = m_Parser.addOption("PluginDir", "Plugin folder name under Plugins/ (e.g. CubeNode)",  true,  1);
                m_hTypeName  = m_Parser.addOption("TypeName",  "Which node type this plugin registers (only needed when it registers more than one - see NodeOS_CreateFactories; defaults to the first/only one)", false, 1);
                m_hAfter     = m_Parser.addOption("After",     "Insert right after this node id",                   false, 1);
                m_hBefore    = m_Parser.addOption("Before",    "Insert right before this node id - neither -After nor -Before means append at the end", false, 1);
                m_hInSpine   = m_Parser.addOption("InSpine",   "Append to this (currently empty) spine id - mutually exclusive with -After/-Before, the only way to place a node into a spine with no nodes yet", false, 1);
            }

            // Resolves -After/-Before/-InSpine (if given) against the CURRENT node/spine list into a
            // target spine id + a dense order index WITHIN THAT SPINE - shared by Redo (which needs it
            // to place the new node) and BackupCurrenState (which needs it to know the full pre-insert
            // layout for Undo).
            std::string ResolveTargetOrder(node_os_command_context& Ctx, int& OutTargetOrder, std::uint64_t& OutTargetSpineId) const noexcept
            {
                const bool bHasAfter   = m_Parser.hasOption(m_hAfter);
                const bool bHasBefore  = m_Parser.hasOption(m_hBefore);
                const bool bHasInSpine = m_Parser.hasOption(m_hInSpine);
                if ((bHasAfter ? 1 : 0) + (bHasBefore ? 1 : 0) + (bHasInSpine ? 1 : 0) > 1)
                    return "CreateNode: -After, -Before and -InSpine are mutually exclusive";

                if (bHasInSpine)
                {
                    auto RefArg = m_Parser.getOptionArgAs<std::string>(m_hInSpine, 0);
                    if (std::holds_alternative<xerr>(RefArg)) return "CreateNode: bad arguments";
                    const auto SpineId = ParseGuid(std::get<std::string>(RefArg));
                    bool bFound = false;
                    for (auto& S : Ctx.m_Spines) if (S.m_Id == SpineId) { bFound = true; break; }
                    if (!bFound) return "CreateNode: -InSpine spine no longer exists";
                    int Count = 0;
                    for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == SpineId) ++Count;
                    OutTargetOrder = Count; OutTargetSpineId = SpineId; return {};
                }

                if (!bHasAfter && !bHasBefore)
                {
                    // No placement given at all - append to the root spine, same as this command's
                    // behavior before spines existed.
                    for (auto& S : Ctx.m_Spines)
                        if (S.m_bIsRoot)
                        {
                            int Count = 0;
                            for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == S.m_Id) ++Count;
                            OutTargetOrder = Count; OutTargetSpineId = S.m_Id; return {};
                        }
                    return "CreateNode: no root spine exists";
                }

                auto RefArg = m_Parser.getOptionArgAs<std::string>(bHasAfter ? m_hAfter : m_hBefore, 0);
                if (std::holds_alternative<xerr>(RefArg)) return "CreateNode: bad arguments";
                const auto RefId = ParseGuid(std::get<std::string>(RefArg));

                std::uint64_t RefSpineId = 0; int RefOrder = 0;
                if (!ResolveNodeSpineAndOrder(Ctx.m_Nodes, RefId, RefSpineId, RefOrder)) return "CreateNode: -After/-Before node no longer exists";
                OutTargetSpineId = RefSpineId; OutTargetOrder = bHasAfter ? RefOrder + 1 : RefOrder; return {};
            }

            std::string Redo() noexcept override
            {
                auto Id        = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                auto PluginDir = m_Parser.getOptionArgAs<std::string>(m_hPluginDir, 0);
                if (std::holds_alternative<xerr>(Id) || std::holds_alternative<xerr>(PluginDir))
                    return "CreateNode: bad arguments";

                auto& Ctx = get<node_os_command_context>();
                int TargetOrder = 0; std::uint64_t TargetSpineId = 0;
                if (auto Err = ResolveTargetOrder(Ctx, TargetOrder, TargetSpineId); !Err.empty()) return Err;

                auto* pSrc = FindSourceByDirName(Ctx.m_Sources, std::get<std::string>(PluginDir));
                if (!pSrc) return "CreateNode: unknown plugin directory";
                // EnsureLoadedAndGetType's return is exactly what's wanted when -TypeName is omitted
                // (the first/only type) - also doubles as "make sure this source is actually compiled
                // and loaded" before the -TypeName lookup below, which only searches AvailableTypes.
                auto* pType = EnsureLoadedAndGetType(*pSrc, Ctx.m_AvailableTypes);
                if (!pType) return "CreateNode: failed to compile/load plugin";
                if (m_Parser.hasOption(m_hTypeName))
                {
                    auto TypeNameArg = m_Parser.getOptionArgAs<std::string>(m_hTypeName, 0);
                    if (std::holds_alternative<xerr>(TypeNameArg)) return "CreateNode: bad arguments";
                    const std::string& WantedName = std::get<std::string>(TypeNameArg);
                    pType = nullptr;
                    for (auto& T : Ctx.m_AvailableTypes)
                        if (T.m_DirName == pSrc->m_DirName && T.m_pFactory->getName() == WantedName) { pType = T.m_pFactory; break; }
                    if (!pType) return std::format("CreateNode: plugin '{}' has no node type named '{}'", pSrc->m_DirName, WantedName);
                }

                for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == TargetSpineId && N.m_Order >= TargetOrder) ++N.m_Order;
                Ctx.m_Nodes.push_back(CreateNodeInstance(ParseGuid(std::get<std::string>(Id)), pType, TargetOrder, TargetSpineId));
                Ctx.m_bDirty = true;
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto Id = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                File.Write(std::holds_alternative<xerr>(Id) ? std::uint64_t{0} : ParseGuid(std::get<std::string>(Id)));

                auto& Ctx = get<node_os_command_context>();
                File.Write(static_cast<std::uint32_t>(Ctx.m_Nodes.size()));
                for (auto& N : Ctx.m_Nodes) { File.Write(N.m_Id); File.Write(N.m_Order); }
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                std::uint64_t Id = 0; File.Read(Id);
                auto& Ctx = get<node_os_command_context>();
                std::erase_if(Ctx.m_Links, [&](auto& L) { return L.m_SourceNode == Id || L.m_TargetNode == Id; });
                for (auto& N : Ctx.m_Nodes) if (N.m_Id == Id) DestroyNodeInstance(N);
                std::erase_if(Ctx.m_Nodes, [&](auto& N) { return N.m_Id == Id; });
                Ctx.m_Selection.m_SelectedNodes.erase(Id);

                std::uint32_t Count = 0; File.Read(Count);
                for (std::uint32_t i = 0; i < Count; ++i)
                {
                    std::uint64_t NId = 0; int Order = 0; File.Read(NId); File.Read(Order);
                    for (auto& N : Ctx.m_Nodes) if (N.m_Id == NId) { N.m_Order = Order; break; }
                }
                Ctx.m_bDirty = true;
            }

            xcmdline::parser::handle m_hId, m_hPluginDir, m_hTypeName, m_hAfter, m_hBefore, m_hInSpine;
        };

        //================================================================================================
        // CreateOwnedPair - creates a control node (If/ForEachLoop) together with its owned End/
        // End-Else marker in one command, the marker always landing right after the owner in the same
        // spine. Placement (-After/-Before/-InSpine) addresses the OWNER, exactly like CreateNode -
        // the marker's own position is never independently specified, since it isn't independently
        // meaningful (NODE_SCRIPTING_DESIGN.md section 4.1: the marker is non-detachable, created and
        // destroyed with its owner - see DeleteNodes' cascade for the other half of that invariant).
        //================================================================================================
        struct create_owned_pair_cmd : xundo::command_base
        {
            create_owned_pair_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "CreateOwnedPair", pDataBase) { RegisterArguments(); }

            const char* getCommandHelp() const noexcept override { return "Creates a control node with its owned End marker. Usage: CreateOwnedPair -Id N -PluginDir dirname -EndId N -EndPluginDir dirname [-After id | -Before id | -InSpine spineid]"; }
            void RegisterArguments() noexcept override
            {
                m_hId           = m_Parser.addOption("Id",           "Owner node id",                                     true,  1);
                m_hPluginDir    = m_Parser.addOption("PluginDir",    "Owner's plugin folder name",                        true,  1);
                m_hEndId        = m_Parser.addOption("EndId",        "Marker node id",                                    true,  1);
                m_hEndPluginDir = m_Parser.addOption("EndPluginDir", "Marker's plugin folder name",                       true,  1);
                m_hLinkId       = m_Parser.addOption("LinkId",       "Read-only owner<->End link id",                     true,  1);
                m_hAfter        = m_Parser.addOption("After",        "Insert the owner right after this node id",         false, 1);
                m_hBefore       = m_Parser.addOption("Before",       "Insert the owner right before this node id",        false, 1);
                m_hInSpine      = m_Parser.addOption("InSpine",      "Append the owner to this (currently empty) spine",  false, 1);
                // Optional 2nd hop - a minimal, non-generic extension rather than a full N-way chain,
                // since nothing needs more than 2 hops today (Function used to, before its owned
                // marker merged into itself). When given, EndId/EndPluginDir describe the MIDDLE node (owned by the owner,
                // itself owning End2Id) instead of the terminal marker.
                m_hEnd2Id        = m_Parser.addOption("End2Id",        "Second-level marker id, owned by the first marker - only when the first marker itself needs one", false, 1);
                m_hEnd2PluginDir = m_Parser.addOption("End2PluginDir", "Second-level marker's plugin folder name",                                                        false, 1);
                m_hLink2Id       = m_Parser.addOption("Link2Id",       "Read-only first-marker<->second-marker link id",                                                  false, 1);
            }

            // Identical placement logic to create_node_cmd::ResolveTargetOrder - duplicated rather than
            // shared, since the two commands' parser handles are distinct members.
            std::string ResolveTargetOrder(node_os_command_context& Ctx, int& OutTargetOrder, std::uint64_t& OutTargetSpineId) const noexcept
            {
                const bool bHasAfter   = m_Parser.hasOption(m_hAfter);
                const bool bHasBefore  = m_Parser.hasOption(m_hBefore);
                const bool bHasInSpine = m_Parser.hasOption(m_hInSpine);
                if ((bHasAfter ? 1 : 0) + (bHasBefore ? 1 : 0) + (bHasInSpine ? 1 : 0) > 1)
                    return "CreateOwnedPair: -After, -Before and -InSpine are mutually exclusive";

                if (bHasInSpine)
                {
                    auto RefArg = m_Parser.getOptionArgAs<std::string>(m_hInSpine, 0);
                    if (std::holds_alternative<xerr>(RefArg)) return "CreateOwnedPair: bad arguments";
                    const auto SpineId = ParseGuid(std::get<std::string>(RefArg));
                    bool bFound = false;
                    for (auto& S : Ctx.m_Spines) if (S.m_Id == SpineId) { bFound = true; break; }
                    if (!bFound) return "CreateOwnedPair: -InSpine spine no longer exists";
                    int Count = 0;
                    for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == SpineId) ++Count;
                    OutTargetOrder = Count; OutTargetSpineId = SpineId; return {};
                }

                if (!bHasAfter && !bHasBefore)
                {
                    for (auto& S : Ctx.m_Spines)
                        if (S.m_bIsRoot)
                        {
                            int Count = 0;
                            for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == S.m_Id) ++Count;
                            OutTargetOrder = Count; OutTargetSpineId = S.m_Id; return {};
                        }
                    return "CreateOwnedPair: no root spine exists";
                }

                auto RefArg = m_Parser.getOptionArgAs<std::string>(bHasAfter ? m_hAfter : m_hBefore, 0);
                if (std::holds_alternative<xerr>(RefArg)) return "CreateOwnedPair: bad arguments";
                const auto RefId = ParseGuid(std::get<std::string>(RefArg));

                std::uint64_t RefSpineId = 0; int RefOrder = 0;
                if (!ResolveNodeSpineAndOrder(Ctx.m_Nodes, RefId, RefSpineId, RefOrder)) return "CreateOwnedPair: -After/-Before node no longer exists";
                OutTargetSpineId = RefSpineId; OutTargetOrder = bHasAfter ? RefOrder + 1 : RefOrder; return {};
            }

            std::string Redo() noexcept override
            {
                auto Id           = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                auto PluginDir    = m_Parser.getOptionArgAs<std::string>(m_hPluginDir, 0);
                auto EndId        = m_Parser.getOptionArgAs<std::string>(m_hEndId, 0);
                auto EndPluginDir = m_Parser.getOptionArgAs<std::string>(m_hEndPluginDir, 0);
                auto LinkId       = m_Parser.getOptionArgAs<std::string>(m_hLinkId, 0);
                if (std::holds_alternative<xerr>(Id) || std::holds_alternative<xerr>(PluginDir) || std::holds_alternative<xerr>(EndId) || std::holds_alternative<xerr>(EndPluginDir) || std::holds_alternative<xerr>(LinkId))
                    return "CreateOwnedPair: bad arguments";

                auto& Ctx = get<node_os_command_context>();
                int TargetOrder = 0; std::uint64_t TargetSpineId = 0;
                if (auto Err = ResolveTargetOrder(Ctx, TargetOrder, TargetSpineId); !Err.empty()) return Err;

                auto* pOwnerSrc = FindSourceByDirName(Ctx.m_Sources, std::get<std::string>(PluginDir));
                if (!pOwnerSrc) return "CreateOwnedPair: unknown owner plugin directory";
                auto* pOwnerType = EnsureLoadedAndGetType(*pOwnerSrc, Ctx.m_AvailableTypes);
                if (!pOwnerType) return "CreateOwnedPair: failed to compile/load owner plugin";

                auto* pEndSrc = FindSourceByDirName(Ctx.m_Sources, std::get<std::string>(EndPluginDir));
                if (!pEndSrc) return "CreateOwnedPair: unknown marker plugin directory";
                auto* pEndType = EnsureLoadedAndGetType(*pEndSrc, Ctx.m_AvailableTypes);
                if (!pEndType) return "CreateOwnedPair: failed to compile/load marker plugin";

                const bool bHasEnd2 = m_Parser.hasOption(m_hEnd2Id);
                xnode_os_node_factory* pEnd2Type = nullptr;
                std::string End2PluginDirStr;
                if (bHasEnd2)
                {
                    auto End2PluginDirArg = m_Parser.getOptionArgAs<std::string>(m_hEnd2PluginDir, 0);
                    if (std::holds_alternative<xerr>(End2PluginDirArg)) return "CreateOwnedPair: bad arguments";
                    End2PluginDirStr = std::get<std::string>(End2PluginDirArg);
                    auto* pEnd2Src = FindSourceByDirName(Ctx.m_Sources, End2PluginDirStr);
                    if (!pEnd2Src) return "CreateOwnedPair: unknown second-level marker plugin directory";
                    pEnd2Type = EnsureLoadedAndGetType(*pEnd2Src, Ctx.m_AvailableTypes);
                    if (!pEnd2Type) return "CreateOwnedPair: failed to compile/load second-level marker plugin";
                }

                // All nodes land together - shift everything at/after TargetOrder by however many
                // we're inserting (2, or 3 when a second hop is present).
                const int NodeCount = bHasEnd2 ? 3 : 2;
                for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == TargetSpineId && N.m_Order >= TargetOrder) N.m_Order += NodeCount;

                const auto OwnerId  = ParseGuid(std::get<std::string>(Id));
                const auto MarkerId = ParseGuid(std::get<std::string>(EndId));
                const auto LinkIdVal = ParseGuid(std::get<std::string>(LinkId));

                Ctx.m_Nodes.push_back(CreateNodeInstance(OwnerId, pOwnerType, TargetOrder, TargetSpineId));
                Ctx.m_Nodes.back().m_OwnedEndId = MarkerId;
                // The read-only ownership link - always the owner's LAST output pin (its dedicated
                // "End" pin, appended after any real data outputs it declares) to the marker's own
                // first (and only) input pin. Read the count off the just-created real instance
                // (captured now, before the next push_back can reallocate Ctx.m_Nodes and invalidate
                // this reference) rather than a throwaway instance that would need its own cleanup.
                const int OwnerOutputIdx = Ctx.m_Nodes.back().m_pNode ? (int)Ctx.m_Nodes.back().m_pNode->getOutputs().size() - 1 : 0;

                Ctx.m_Nodes.push_back(CreateNodeInstance(MarkerId, pEndType, TargetOrder + 1, TargetSpineId));
                Ctx.m_Links.push_back(link_instance{ LinkIdVal, OwnerId, std::max(OwnerOutputIdx, 0), MarkerId, 0, true });

                if (bHasEnd2)
                {
                    const auto End2IdVal  = ParseGuid(std::get<std::string>(m_Parser.getOptionArgAs<std::string>(m_hEnd2Id, 0)));
                    const auto Link2IdVal = ParseGuid(std::get<std::string>(m_Parser.getOptionArgAs<std::string>(m_hLink2Id, 0)));
                    Ctx.m_Nodes.back().m_OwnedEndId = End2IdVal; // the just-created middle marker owns the terminal one
                    const int MidOutputIdx = Ctx.m_Nodes.back().m_pNode ? (int)Ctx.m_Nodes.back().m_pNode->getOutputs().size() - 1 : 0;
                    Ctx.m_Nodes.push_back(CreateNodeInstance(End2IdVal, pEnd2Type, TargetOrder + 2, TargetSpineId));
                    Ctx.m_Links.push_back(link_instance{ Link2IdVal, MarkerId, std::max(MidOutputIdx, 0), End2IdVal, 0, true });
                }

                Ctx.m_bDirty = true;
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto Id     = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                auto EndId  = m_Parser.getOptionArgAs<std::string>(m_hEndId, 0);
                auto End2Id = m_Parser.getOptionArgAs<std::string>(m_hEnd2Id, 0);
                File.Write(std::holds_alternative<xerr>(Id)     ? std::uint64_t{0} : ParseGuid(std::get<std::string>(Id)));
                File.Write(std::holds_alternative<xerr>(EndId)  ? std::uint64_t{0} : ParseGuid(std::get<std::string>(EndId)));
                File.Write(std::holds_alternative<xerr>(End2Id) ? std::uint64_t{0} : ParseGuid(std::get<std::string>(End2Id)));

                auto& Ctx = get<node_os_command_context>();
                File.Write(static_cast<std::uint32_t>(Ctx.m_Nodes.size()));
                for (auto& N : Ctx.m_Nodes) { File.Write(N.m_Id); File.Write(N.m_Order); }
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                std::uint64_t Id = 0, EndId = 0, End2Id = 0; File.Read(Id); File.Read(EndId); File.Read(End2Id);
                auto& Ctx = get<node_os_command_context>();
                auto IsDoomed = [&](std::uint64_t X) { return X == Id || X == EndId || X == End2Id; };
                std::erase_if(Ctx.m_Links, [&](auto& L) { return IsDoomed(L.m_SourceNode) || IsDoomed(L.m_TargetNode); });
                for (auto& N : Ctx.m_Nodes) if (IsDoomed(N.m_Id)) DestroyNodeInstance(N);
                std::erase_if(Ctx.m_Nodes, [&](auto& N) { return IsDoomed(N.m_Id); });
                Ctx.m_Selection.m_SelectedNodes.erase(Id);
                Ctx.m_Selection.m_SelectedNodes.erase(EndId);
                Ctx.m_Selection.m_SelectedNodes.erase(End2Id);

                std::uint32_t Count = 0; File.Read(Count);
                for (std::uint32_t i = 0; i < Count; ++i)
                {
                    std::uint64_t NId = 0; int Order = 0; File.Read(NId); File.Read(Order);
                    for (auto& N : Ctx.m_Nodes) if (N.m_Id == NId) { N.m_Order = Order; break; }
                }
                Ctx.m_bDirty = true;
            }

            xcmdline::parser::handle m_hId, m_hPluginDir, m_hEndId, m_hEndPluginDir, m_hLinkId, m_hAfter, m_hBefore, m_hInSpine;
            xcmdline::parser::handle m_hEnd2Id, m_hEnd2PluginDir, m_hLink2Id;
        };

        //================================================================================================
        // SetEndElseState - the one bespoke command behind an End node's own "IsElse" checkbox
        // (NODE_SCRIPTING_DESIGN.md section 4.2). Enabling it creates a further, plain End marker
        // right after this node plus a read-only link from this node's now-appearing "ElseEnd" pin
        // to it (mirroring CreateOwnedPair, but the "owner" here already exists rather than being
        // created by this same command); disabling it removes that paired End again. The two arms
        // share one command because they're two faces of the exact same user action - one checkbox.
        //================================================================================================
        struct set_end_else_state_cmd : xundo::command_base
        {
            set_end_else_state_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "SetEndElseState", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Toggles an End node's else-pairing. Usage: SetEndElseState -OwnerId N -Enable 0|1 [-EndId N -EndPluginDir dirname -LinkId N]"; }
            void RegisterArguments() noexcept override
            {
                m_hOwnerId      = m_Parser.addOption("OwnerId",      "The End node whose else-pairing is changing",           true,  1);
                m_hEnable       = m_Parser.addOption("Enable",       "1 = create the paired End, 0 = remove it",              true,  1);
                m_hEndId        = m_Parser.addOption("EndId",        "New marker node id - only used when -Enable 1",         false, 1);
                m_hEndPluginDir = m_Parser.addOption("EndPluginDir", "New marker's plugin folder - only used when -Enable 1", false, 1);
                m_hLinkId       = m_Parser.addOption("LinkId",       "New read-only link id - only used when -Enable 1",      false, 1);
            }

            std::string Redo() noexcept override
            {
                auto OwnerArg  = m_Parser.getOptionArgAs<std::string>(m_hOwnerId, 0);
                auto EnableArg = m_Parser.getOptionArgAs<std::int64_t>(m_hEnable, 0);
                if (std::holds_alternative<xerr>(OwnerArg) || std::holds_alternative<xerr>(EnableArg)) return "SetEndElseState: bad arguments";
                auto& Ctx = get<node_os_command_context>();
                const auto OwnerId = ParseGuid(std::get<std::string>(OwnerArg));
                const bool bEnable = std::get<std::int64_t>(EnableArg) != 0;

                node_instance* pOwnerNode = nullptr;
                for (auto& N : Ctx.m_Nodes) if (N.m_Id == OwnerId) { pOwnerNode = &N; break; }
                if (!pOwnerNode) return "SetEndElseState: owner node no longer exists";

                if (bEnable)
                {
                    if (pOwnerNode->m_OwnedEndId != 0) return {}; // already paired - idempotent no-op

                    auto EndIdArg        = m_Parser.getOptionArgAs<std::string>(m_hEndId, 0);
                    auto EndPluginDirArg = m_Parser.getOptionArgAs<std::string>(m_hEndPluginDir, 0);
                    auto LinkIdArg       = m_Parser.getOptionArgAs<std::string>(m_hLinkId, 0);
                    if (std::holds_alternative<xerr>(EndIdArg) || std::holds_alternative<xerr>(EndPluginDirArg) || std::holds_alternative<xerr>(LinkIdArg))
                        return "SetEndElseState: -EndId/-EndPluginDir/-LinkId required when -Enable 1";

                    auto* pEndSrc = FindSourceByDirName(Ctx.m_Sources, std::get<std::string>(EndPluginDirArg));
                    if (!pEndSrc) return "SetEndElseState: unknown marker plugin directory";
                    auto* pEndType = EnsureLoadedAndGetType(*pEndSrc, Ctx.m_AvailableTypes);
                    if (!pEndType) return "SetEndElseState: failed to compile/load marker plugin";

                    const auto EndId       = ParseGuid(std::get<std::string>(EndIdArg));
                    const auto LinkIdVal   = ParseGuid(std::get<std::string>(LinkIdArg));
                    const auto TargetSpineId = pOwnerNode->m_SpineId;
                    const int  TargetOrder   = pOwnerNode->m_Order + 1;
                    for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == TargetSpineId && N.m_Order >= TargetOrder) ++N.m_Order;

                    // pOwnerNode is re-resolved after this push_back, since it may reallocate Ctx.m_Nodes.
                    Ctx.m_Nodes.push_back(CreateNodeInstance(EndId, pEndType, TargetOrder, TargetSpineId));
                    for (auto& N : Ctx.m_Nodes) if (N.m_Id == OwnerId) { pOwnerNode = &N; break; }
                    pOwnerNode->m_OwnedEndId = EndId;

                    // By the time this command runs, the owner's own IsElse property (a plain
                    // reflected bool on its own node type - see Plugins/End) has already been set by
                    // the SetProperties command issued alongside this one, so getOutputs() already
                    // reports its extra "ElseEnd" pin - always the last one.
                    const int OwnerOutputIdx = pOwnerNode->m_pNode ? (int)pOwnerNode->m_pNode->getOutputs().size() - 1 : 0;
                    Ctx.m_Links.push_back(link_instance{ LinkIdVal, OwnerId, std::max(OwnerOutputIdx, 0), EndId, 0, true });
                }
                else
                {
                    const auto OldEndId = pOwnerNode->m_OwnedEndId;
                    if (OldEndId == 0) return {}; // nothing paired - idempotent no-op

                    std::erase_if(Ctx.m_Links, [&](auto& L) { return L.m_SourceNode == OldEndId || L.m_TargetNode == OldEndId; });
                    std::uint64_t RemovedSpineId = 0; int RemovedOrder = 0;
                    for (auto& N : Ctx.m_Nodes) if (N.m_Id == OldEndId) { RemovedSpineId = N.m_SpineId; RemovedOrder = N.m_Order; DestroyNodeInstance(N); break; }
                    std::erase_if(Ctx.m_Nodes, [&](auto& N) { return N.m_Id == OldEndId; });
                    for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == RemovedSpineId && N.m_Order > RemovedOrder) --N.m_Order;
                    for (auto& N : Ctx.m_Nodes) if (N.m_Id == OwnerId) { N.m_OwnedEndId = 0; break; }
                }
                Ctx.m_bDirty = true;
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                auto OwnerArg = m_Parser.getOptionArgAs<std::string>(m_hOwnerId, 0);
                const auto OwnerId = std::holds_alternative<xerr>(OwnerArg) ? std::uint64_t{0} : ParseGuid(std::get<std::string>(OwnerArg));

                File.Write(static_cast<std::uint32_t>(Ctx.m_Nodes.size()));
                for (auto& N : Ctx.m_Nodes) { File.Write(N.m_Id); File.Write(N.m_Order); File.Write(N.m_OwnedEndId); }

                // Snapshot the currently-paired End (if any) in full, so Undo can recreate it if
                // Redo's -Enable 0 arm went on to delete it.
                std::uint64_t OldEndId = 0;
                for (auto& N : Ctx.m_Nodes) if (N.m_Id == OwnerId) { OldEndId = N.m_OwnedEndId; break; }
                if (!OldEndId) { File.Write(std::uint8_t{0}); return; }

                for (auto& N : Ctx.m_Nodes)
                {
                    if (N.m_Id != OldEndId) continue;
                    std::string PluginDir;
                    for (auto& T : Ctx.m_AvailableTypes) if (N.m_pNode && T.m_pFactory == N.m_pNode->m_pFactory) { PluginDir = T.m_DirName; break; }
                    File.Write(std::uint8_t{1});
                    File.Write(N.m_Id); WriteString(File, PluginDir); File.Write(N.m_Order); File.Write(N.m_SpineId);
                    link_instance FoundLink{}; bool bHasLink = false;
                    for (auto& L : Ctx.m_Links) if (L.m_TargetNode == OldEndId) { FoundLink = L; bHasLink = true; break; }
                    File.Write(bHasLink ? std::uint8_t{1} : std::uint8_t{0});
                    if (bHasLink) File.Write(FoundLink);
                    return;
                }
                File.Write(std::uint8_t{0});
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                auto& Ctx = get<node_os_command_context>();

                struct prior_row { std::uint64_t m_Id; int m_Order; std::uint64_t m_OwnedEndId; };
                std::uint32_t Count = 0; File.Read(Count);
                std::vector<prior_row> PriorState(Count);
                for (auto& R : PriorState) { File.Read(R.m_Id); File.Read(R.m_Order); File.Read(R.m_OwnedEndId); }

                auto WasThereBefore = [&](std::uint64_t Id) { for (auto& R : PriorState) if (R.m_Id == Id) return true; return false; };

                // Remove whatever the -Enable 1 arm might have added (an id absent from PriorState).
                std::erase_if(Ctx.m_Nodes, [&](auto& N) { if (WasThereBefore(N.m_Id)) return false; DestroyNodeInstance(N); return true; });
                std::erase_if(Ctx.m_Links, [&](auto& L) { return !WasThereBefore(L.m_SourceNode) || !WasThereBefore(L.m_TargetNode); });

                std::uint8_t bHadOldEnd = 0; File.Read(bHadOldEnd);
                if (bHadOldEnd)
                {
                    std::uint64_t EndId = 0; File.Read(EndId);
                    const std::string PluginDir = ReadString(File);
                    int Order = 0; File.Read(Order);
                    std::uint64_t SpineId = 0; File.Read(SpineId);
                    std::uint8_t bHasLink = 0; File.Read(bHasLink);
                    link_instance L{};
                    if (bHasLink) File.Read(L);

                    const bool bAlreadyThere = std::any_of(Ctx.m_Nodes.begin(), Ctx.m_Nodes.end(), [&](auto& N) { return N.m_Id == EndId; });
                    if (!bAlreadyThere)
                    {
                        auto* pSrc = FindSourceByDirName(Ctx.m_Sources, PluginDir);
                        auto* pFactory = pSrc ? EnsureLoadedAndGetType(*pSrc, Ctx.m_AvailableTypes) : nullptr;
                        if (pFactory)
                        {
                            Ctx.m_Nodes.push_back(CreateNodeInstance(EndId, pFactory, Order, SpineId));
                            if (bHasLink) Ctx.m_Links.push_back(L);
                        }
                    }
                }

                for (auto& R : PriorState)
                    for (auto& N : Ctx.m_Nodes)
                        if (N.m_Id == R.m_Id) { N.m_Order = R.m_Order; N.m_OwnedEndId = R.m_OwnedEndId; break; }

                Ctx.m_bDirty = true;
            }

            xcmdline::parser::handle m_hOwnerId, m_hEnable, m_hEndId, m_hEndPluginDir, m_hLinkId;
        };

        //================================================================================================
        // DeleteNodes - the heaviest command: must fully snapshot each deleted node's identity, order,
        // and complete property block (via SerializePropertiesToString) plus every cascade-deleted
        // link, so Undo can reconstruct all of it exactly - this is the "resize 10 entries down to 3"
        // case the earlier design discussion settled on.
        //================================================================================================
        struct delete_nodes_cmd : xundo::command_base
        {
            delete_nodes_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "DeleteNodes", pDataBase) { RegisterArguments(); }

            const char* getCommandHelp() const noexcept override { return "Deletes node(s) and any links touching them. Usage: DeleteNodes -Ids id[,id...]"; }
            void RegisterArguments() noexcept override { m_hIds = m_Parser.addOption("Ids", "Node ids, comma-separated", true, 1); }

            std::string Redo() noexcept override
            {
                auto IdsArg = m_Parser.getOptionArgAs<std::string>(m_hIds, 0);
                if (std::holds_alternative<xerr>(IdsArg)) return "DeleteNodes: bad arguments";
                auto& Ctx = get<node_os_command_context>();
                const auto Ids = ExpandOwnershipCascade(Ctx.m_Nodes, SplitIds(std::get<std::string>(IdsArg)));

                auto IsDoomed = [&](std::uint64_t Id) { return std::find(Ids.begin(), Ids.end(), Id) != Ids.end(); };
                std::erase_if(Ctx.m_Links, [&](auto& L) { return IsDoomed(L.m_SourceNode) || IsDoomed(L.m_TargetNode); });
                for (auto& N : Ctx.m_Nodes) if (IsDoomed(N.m_Id)) DestroyNodeInstance(N);
                std::erase_if(Ctx.m_Nodes, [&](auto& N) { return IsDoomed(N.m_Id); });
                for (auto Id : Ids) Ctx.m_Selection.m_SelectedNodes.erase(Id);
                Ctx.m_bDirty = true;
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto IdsArg = m_Parser.getOptionArgAs<std::string>(m_hIds, 0);
                auto& Ctx = get<node_os_command_context>();
                const auto Ids = std::holds_alternative<xerr>(IdsArg) ? std::vector<std::uint64_t>{} : ExpandOwnershipCascade(Ctx.m_Nodes, SplitIds(std::get<std::string>(IdsArg)));
                auto IsDoomed = [&](std::uint64_t Id) { return std::find(Ids.begin(), Ids.end(), Id) != Ids.end(); };

                struct node_snap { std::uint64_t m_Id; std::string m_PluginDir; int m_Order; std::uint64_t m_SpineId; std::string m_Properties; std::uint64_t m_OwnedEndId; };
                std::vector<node_snap> NodeSnaps;
                for (auto& N : Ctx.m_Nodes)
                {
                    if (!IsDoomed(N.m_Id)) continue;
                    std::string PluginDir;
                    for (auto& T : Ctx.m_AvailableTypes) if (N.m_pNode && T.m_pFactory == N.m_pNode->m_pFactory) { PluginDir = T.m_DirName; break; }
                    std::string Properties;
                    if (HasSerializableProperties(N.m_pNode))
                        Properties = SerializePropertiesToString(N.m_pNode);
                    NodeSnaps.push_back({ N.m_Id, PluginDir, N.m_Order, N.m_SpineId, Properties, N.m_OwnedEndId });
                }
                std::vector<link_instance> LinkSnaps;
                for (auto& L : Ctx.m_Links)
                    if (IsDoomed(L.m_SourceNode) || IsDoomed(L.m_TargetNode))
                        LinkSnaps.push_back(L);

                File.Write(static_cast<std::uint32_t>(NodeSnaps.size()));
                for (auto& S : NodeSnaps) { File.Write(S.m_Id); WriteString(File, S.m_PluginDir); File.Write(S.m_Order); File.Write(S.m_SpineId); WriteString(File, S.m_Properties); File.Write(S.m_OwnedEndId); }
                File.Write(static_cast<std::uint32_t>(LinkSnaps.size()));
                for (auto& L : LinkSnaps) File.Write(L);
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                std::uint32_t NodeCount = 0; File.Read(NodeCount);
                for (std::uint32_t i = 0; i < NodeCount; ++i)
                {
                    std::uint64_t Id = 0; File.Read(Id);
                    const std::string PluginDir = ReadString(File);
                    int Order = 0; File.Read(Order);
                    std::uint64_t SpineId = 0; File.Read(SpineId);
                    const std::string Properties = ReadString(File);
                    std::uint64_t OwnedEndId = 0; File.Read(OwnedEndId);

                    auto* pSrc = FindSourceByDirName(Ctx.m_Sources, PluginDir);
                    auto* pFactory = pSrc ? EnsureLoadedAndGetType(*pSrc, Ctx.m_AvailableTypes) : nullptr;
                    if (!pFactory) continue; // plugin source no longer resolvable - best effort, matching LoadGraph's own tolerance
                    Ctx.m_Nodes.push_back(CreateNodeInstance(Id, pFactory, Order, SpineId));
                    Ctx.m_Nodes.back().m_OwnedEndId = OwnedEndId;
                    if (!Properties.empty())
                        ApplyPropertiesFromString(Ctx.m_Nodes.back().m_pNode, Properties);
                }
                std::uint32_t LinkCount = 0; File.Read(LinkCount);
                for (std::uint32_t i = 0; i < LinkCount; ++i)
                {
                    link_instance L{}; File.Read(L);
                    Ctx.m_Links.push_back(L);
                }
                Ctx.m_bDirty = true;
            }

            xcmdline::parser::handle m_hIds;
        };

        //================================================================================================
        // DeleteLink
        //================================================================================================
        struct delete_link_cmd : xundo::command_base
        {
            delete_link_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "DeleteLink", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Deletes a link. Usage: DeleteLink -Id N"; }
            void RegisterArguments() noexcept override { m_hId = m_Parser.addOption("Id", "Link id", true, 1); }

            std::string Redo() noexcept override
            {
                auto Id = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                if (std::holds_alternative<xerr>(Id)) return "DeleteLink: bad arguments";
                auto& Ctx = get<node_os_command_context>();
                const auto IdVal = ParseGuid(std::get<std::string>(Id));
                for (auto& L : Ctx.m_Links)
                    if (L.m_Id == IdVal && L.m_bReadOnly) return "DeleteLink: this is an owner<->End ownership link - it can't be removed on its own, only by deleting one of the two nodes";
                std::erase_if(Ctx.m_Links, [&](auto& L) { return L.m_Id == IdVal; });
                if (Ctx.m_Selection.m_SelectedLink == IdVal) Ctx.m_Selection.m_SelectedLink = 0;
                Ctx.m_bDirty = true;
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto Id = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                const auto IdVal = std::holds_alternative<xerr>(Id) ? std::uint64_t{0} : ParseGuid(std::get<std::string>(Id));
                auto& Ctx = get<node_os_command_context>();
                for (auto& L : Ctx.m_Links)
                    if (L.m_Id == IdVal) { File.Write(std::uint8_t{1}); File.Write(L); return; }
                File.Write(std::uint8_t{0});
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                std::uint8_t bHad = 0; File.Read(bHad);
                if (!bHad) return;
                link_instance L{}; File.Read(L);
                auto& Ctx = get<node_os_command_context>();
                Ctx.m_Links.push_back(L);
                Ctx.m_bDirty = true;
            }

            xcmdline::parser::handle m_hId;
        };

        //================================================================================================
        // Connect - evicts any existing link into the same target input first (matching the existing
        // "single connection per input" rule), so Undo must be able to restore whichever link (if any)
        // that eviction removed.
        //================================================================================================
        struct connect_cmd : xundo::command_base
        {
            connect_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "Connect", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Connects two ports. Usage: Connect -Id N -SourceNode N -SourceOutput N -TargetNode N -TargetInput N"; }
            void RegisterArguments() noexcept override
            {
                m_hId           = m_Parser.addOption("Id",           "Link id",              true, 1);
                m_hSourceNode   = m_Parser.addOption("SourceNode",   "Source node id",       true, 1);
                m_hSourceOutput = m_Parser.addOption("SourceOutput", "Source output index",  true, 1);
                m_hTargetNode   = m_Parser.addOption("TargetNode",   "Target node id",       true, 1);
                m_hTargetInput  = m_Parser.addOption("TargetInput",  "Target input index",   true, 1);
            }

            // Shared by Redo and BackupCurrenState - both need the same 5 fields off m_Parser.
            bool ParseAll(link_instance& L) const noexcept
            {
                auto Id = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                auto SN = m_Parser.getOptionArgAs<std::string>(m_hSourceNode, 0);
                auto SO = m_Parser.getOptionArgAs<std::int64_t>(m_hSourceOutput, 0);
                auto TN = m_Parser.getOptionArgAs<std::string>(m_hTargetNode, 0);
                auto TI = m_Parser.getOptionArgAs<std::int64_t>(m_hTargetInput, 0);
                if (std::holds_alternative<xerr>(Id) || std::holds_alternative<xerr>(SN) || std::holds_alternative<xerr>(SO) || std::holds_alternative<xerr>(TN) || std::holds_alternative<xerr>(TI))
                    return false;
                L.m_Id           = ParseGuid(std::get<std::string>(Id));
                L.m_SourceNode   = ParseGuid(std::get<std::string>(SN));
                L.m_SourceOutput = static_cast<int>(std::get<std::int64_t>(SO));
                L.m_TargetNode   = ParseGuid(std::get<std::string>(TN));
                L.m_TargetInput  = static_cast<int>(std::get<std::int64_t>(TI));
                return true;
            }

            std::string Redo() noexcept override
            {
                link_instance L{};
                if (!ParseAll(L)) return "Connect: bad arguments";
                auto& Ctx = get<node_os_command_context>();
                // Any two nodes anywhere in the graph can connect, regardless of spine or column - the
                // highway belongs to the SOURCE node's own column: the wire travels that column's own
                // rail up/down to the target's Y, then jogs however far sideways it needs to reach the
                // target, crossing intervening columns if the target lives in a different one (see
                // DrawHighwayPath/its ColumnOfNode(Link.m_SourceNode) call in DrawGraphCanvas).
                auto SourceIt = std::find_if(Ctx.m_Nodes.begin(), Ctx.m_Nodes.end(), [&](auto& N) { return N.m_Id == L.m_SourceNode; });
                auto TargetIt = std::find_if(Ctx.m_Nodes.begin(), Ctx.m_Nodes.end(), [&](auto& N) { return N.m_Id == L.m_TargetNode; });
                if (SourceIt == Ctx.m_Nodes.end() || TargetIt == Ctx.m_Nodes.end()) return "Connect: source/target node no longer exists";
                // An owner<->End ownership link is read-only - dragging a new wire onto that same
                // target input must not silently evict it the way an ordinary rewire would.
                for (auto& X : Ctx.m_Links)
                    if (X.m_TargetNode == L.m_TargetNode && X.m_TargetInput == L.m_TargetInput && X.m_bReadOnly)
                        return "Connect: that input is a read-only owner<->End ownership pin - it can't be rewired";
                std::erase_if(Ctx.m_Links, [&](auto& X) { return X.m_TargetNode == L.m_TargetNode && X.m_TargetInput == L.m_TargetInput; });
                Ctx.m_Links.push_back(L);
                Ctx.m_bDirty = true;
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                link_instance L{};
                const bool bOk = ParseAll(L);
                File.Write(bOk ? L.m_TargetNode : std::uint64_t{0});
                File.Write(bOk ? L.m_TargetInput : 0);
                auto& Ctx = get<node_os_command_context>();
                for (auto& X : Ctx.m_Links)
                    if (bOk && X.m_TargetNode == L.m_TargetNode && X.m_TargetInput == L.m_TargetInput) { File.Write(std::uint8_t{1}); File.Write(X); return; }
                File.Write(std::uint8_t{0});
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                std::uint64_t TargetNode = 0; int TargetInput = 0;
                File.Read(TargetNode); File.Read(TargetInput);
                std::uint8_t bHadExisting = 0; File.Read(bHadExisting);
                link_instance Existing{};
                if (bHadExisting) File.Read(Existing);

                auto& Ctx = get<node_os_command_context>();
                // Unconditionally remove whatever currently sits in that slot - that's always exactly
                // the link Redo() added, regardless of whether an eviction happened too.
                std::erase_if(Ctx.m_Links, [&](auto& X) { return X.m_TargetNode == TargetNode && X.m_TargetInput == TargetInput; });
                if (bHadExisting) Ctx.m_Links.push_back(Existing);
                Ctx.m_bDirty = true;
            }

            xcmdline::parser::handle m_hId, m_hSourceNode, m_hSourceOutput, m_hTargetNode, m_hTargetInput;
        };

        //================================================================================================
        // CreateSpine - a genuinely new mutation shape: creates zero nodes, only the structural
        // containers (a spine, and optionally the column that houses it), placed directly at an
        // absolute world -Y - a spine's position is just (Y, ColumnId), nothing derived. -Column/
        // -NewColumn fold "attach to an already-existing column" vs. "synthesize a brand-new one right
        // on -Side of -NeighborColumn" into one command, the same way Select already folds its several
        // mutually exclusive concerns into one. -NewColumnId is minted by the CALLER (never inside
        // Redo()), matching this codebase's standing rule that Redo() never invents an id. No bDirty -
        // this never touches node/link data (matches reorder_nodes_cmd not setting it either).
        //================================================================================================
        struct create_spine_cmd : xundo::command_base
        {
            create_spine_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "CreateSpine", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override
            {
                return "Creates a new, empty spine. Usage: CreateSpine -Id spineid -Y yvalue "
                       "(-Column columnid | -NewColumn -NewColumnId id -NeighborColumn columnid -Side L|R)";
            }
            void RegisterArguments() noexcept override
            {
                m_hId             = m_Parser.addOption("Id",             "New spine id",                                    true,  1);
                m_hY              = m_Parser.addOption("Y",              "Absolute world Y for this spine's own top slot",  true,  1);
                m_hColumn         = m_Parser.addOption("Column",         "Attach to this already-existing column id",       false, 1);
                m_hNewColumn      = m_Parser.addOption("NewColumn",      "Synthesize a new column (value ignored)",         false, 1);
                m_hNewColumnId    = m_Parser.addOption("NewColumnId",    "Id for the new column",                           false, 1);
                m_hNeighborColumn = m_Parser.addOption("NeighborColumn", "The new column's own neighbor column id",         false, 1);
                m_hSide           = m_Parser.addOption("Side",           "Which side of -NeighborColumn the new one sits on: L or R", false, 1);
            }

            // Shared by Redo and BackupCurrenState - resolves and validates every argument without
            // mutating anything (the actual column creation only ever happens once, inside Redo()).
            std::string ResolveArgs(node_os_command_context& Ctx, std::uint64_t& OutSpineId, float& OutY, std::uint64_t& OutColumnId, bool& OutNewColumn
                                   , std::uint64_t& OutNewColumnId, std::uint64_t& OutNeighborColumnId, char& OutSide) const noexcept
            {
                auto IdArg = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                auto YArg  = m_Parser.getOptionArgAs<double>(m_hY, 0);
                if (std::holds_alternative<xerr>(IdArg) || std::holds_alternative<xerr>(YArg)) return "CreateSpine: bad arguments";
                OutSpineId = ParseGuid(std::get<std::string>(IdArg));
                OutY       = static_cast<float>(std::get<double>(YArg));

                const bool bHasColumn    = m_Parser.hasOption(m_hColumn);
                const bool bHasNewColumn = m_Parser.hasOption(m_hNewColumn);
                if (bHasColumn == bHasNewColumn) return "CreateSpine: exactly one of -Column/-NewColumn is required";

                if (bHasColumn)
                {
                    auto A = m_Parser.getOptionArgAs<std::string>(m_hColumn, 0);
                    if (std::holds_alternative<xerr>(A)) return "CreateSpine: bad arguments";
                    OutColumnId = ParseGuid(std::get<std::string>(A));
                    bool bFound = false; for (auto& Co : Ctx.m_Columns) if (Co.m_Id == OutColumnId) { bFound = true; break; }
                    if (!bFound) return "CreateSpine: -Column no longer exists";
                    OutNewColumn = false;
                    return {};
                }

                auto NCId = m_Parser.getOptionArgAs<std::string>(m_hNewColumnId, 0);
                auto NB   = m_Parser.getOptionArgAs<std::string>(m_hNeighborColumn, 0);
                auto Sd   = m_Parser.getOptionArgAs<std::string>(m_hSide, 0);
                if (std::holds_alternative<xerr>(NCId) || std::holds_alternative<xerr>(NB) || std::holds_alternative<xerr>(Sd)) return "CreateSpine: bad arguments";
                OutNewColumnId      = ParseGuid(std::get<std::string>(NCId));
                OutNeighborColumnId = ParseGuid(std::get<std::string>(NB));
                const auto& SideStr = std::get<std::string>(Sd);
                if (SideStr.empty() || (SideStr[0] != 'L' && SideStr[0] != 'R')) return "CreateSpine: -Side must be L or R";
                OutSide = SideStr[0];

                bool bNeighborFound = false;
                for (auto& Co : Ctx.m_Columns) if (Co.m_Id == OutNeighborColumnId) { bNeighborFound = true; break; }
                if (!bNeighborFound) return "CreateSpine: -NeighborColumn no longer exists";
                OutNewColumn = true;
                return {};
            }

            std::string Redo() noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                std::uint64_t SpineId = 0, ColumnId = 0, NewColumnId = 0, NeighborColumnId = 0; float Y = 0.0f; bool bNewColumn = false; char Side = 'R';
                if (auto Err = ResolveArgs(Ctx, SpineId, Y, ColumnId, bNewColumn, NewColumnId, NeighborColumnId, Side); !Err.empty()) return Err;

                if (bNewColumn)
                {
                    // Splices the new column in on -Side of -NeighborColumn - if the neighbor already
                    // had a column there (inserting BETWEEN two existing columns, not just past the
                    // outermost one), that far column is relinked to the new one instead, same as
                    // inserting into any doubly-linked list.
                    column NewCol{ NewColumnId, 0, 0, false };
                    for (auto& Co : Ctx.m_Columns)
                        if (Co.m_Id == NeighborColumnId)
                        {
                            std::uint64_t& NearPtr = (Side == 'R') ? Co.m_RightId : Co.m_LeftId;
                            const std::uint64_t OldFarNeighborId = NearPtr;
                            NearPtr = NewColumnId;
                            if (Side == 'R') { NewCol.m_LeftId = NeighborColumnId; NewCol.m_RightId = OldFarNeighborId; }
                            else             { NewCol.m_RightId = NeighborColumnId; NewCol.m_LeftId = OldFarNeighborId; }
                            if (OldFarNeighborId != 0)
                                for (auto& Co2 : Ctx.m_Columns)
                                    if (Co2.m_Id == OldFarNeighborId)
                                    {
                                        std::uint64_t& FarPtr = (Side == 'R') ? Co2.m_LeftId : Co2.m_RightId;
                                        FarPtr = NewColumnId;
                                        break;
                                    }
                            break;
                        }
                    Ctx.m_Columns.push_back(NewCol);
                    ColumnId = NewColumnId;
                }

                Ctx.m_Spines.push_back(spine{ SpineId, ColumnId, false, Y });
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                std::uint64_t SpineId = 0, ColumnId = 0, NewColumnId = 0, NeighborColumnId = 0; float Y = 0.0f; bool bNewColumn = false; char Side = 'R';
                const bool bOk = ResolveArgs(Ctx, SpineId, Y, ColumnId, bNewColumn, NewColumnId, NeighborColumnId, Side).empty();
                File.Write(bOk ? std::uint8_t{1} : std::uint8_t{0});
                File.Write(SpineId);
                File.Write(bNewColumn ? std::uint8_t{1} : std::uint8_t{0});
                File.Write(bNewColumn ? NewColumnId : ColumnId);
                File.Write(NeighborColumnId);
                File.Write(Side == 'R' ? std::uint8_t{1} : std::uint8_t{0});

                // The far neighbor (if any) that will need relinking on undo - whichever column
                // currently sits past -NeighborColumn on -Side, before the splice happens.
                std::uint64_t OldFarNeighborId = 0;
                if (bNewColumn)
                    for (auto& Co : Ctx.m_Columns)
                        if (Co.m_Id == NeighborColumnId) { OldFarNeighborId = (Side == 'R') ? Co.m_RightId : Co.m_LeftId; break; }
                File.Write(OldFarNeighborId);
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                std::uint8_t bOk = 0; File.Read(bOk);
                if (!bOk) return;
                std::uint64_t SpineId = 0; File.Read(SpineId);
                std::uint8_t bNewColumn = 0; File.Read(bNewColumn);
                std::uint64_t ColumnId = 0; File.Read(ColumnId);
                std::uint64_t NeighborColumnId = 0; File.Read(NeighborColumnId);
                std::uint8_t bSideR = 0; File.Read(bSideR);
                std::uint64_t OldFarNeighborId = 0; File.Read(OldFarNeighborId);

                auto& Ctx = get<node_os_command_context>();
                std::erase_if(Ctx.m_Spines, [&](auto& Sp) { return Sp.m_Id == SpineId; });
                if (bNewColumn)
                {
                    std::erase_if(Ctx.m_Columns, [&](auto& Co) { return Co.m_Id == ColumnId; });
                    for (auto& Co : Ctx.m_Columns)
                        if (Co.m_Id == NeighborColumnId)
                        {
                            if (bSideR) Co.m_RightId = OldFarNeighborId; else Co.m_LeftId = OldFarNeighborId;
                            break;
                        }
                    if (OldFarNeighborId != 0)
                        for (auto& Co : Ctx.m_Columns)
                            if (Co.m_Id == OldFarNeighborId)
                            {
                                if (bSideR) Co.m_LeftId = NeighborColumnId; else Co.m_RightId = NeighborColumnId;
                                break;
                            }
                }
            }

            xcmdline::parser::handle m_hId, m_hY, m_hColumn, m_hNewColumn, m_hNewColumnId, m_hNeighborColumn, m_hSide;
        };

        //================================================================================================
        // SetSpinePosition - sets a spine's absolute position directly: which column it lives in, and
        // its own world Y within it. No anchor/offset indirection at all - a spine's position IS
        // (Y, ColumnId), plain and settable. -NewColumn mirrors CreateSpine's own dual addressing, so a
        // spine can be dropped straight into a brand-new column spliced in beside an existing one.
        // Cascades to remove the OLD column too if the move empties it, bridging its own Left/Right
        // neighbors together, same as DeleteSpine's own cascade - no exceptions, even for the root
        // column: its m_bIsRoot flag transfers onto the destination column first, since Pass C's layout
        // walk needs exactly one root column to exist as its anchor, but doesn't care which one it is.
        // No bDirty - repositioning a spine never changes what's connected to what.
        //================================================================================================
        struct set_spine_position_cmd : xundo::command_base
        {
            set_spine_position_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "SetSpinePosition", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override
            {
                return "Moves a spine. Usage: SetSpinePosition -Id spineid -Y yvalue "
                       "(-Column columnid | -NewColumn -NewColumnId id -NeighborColumn columnid -Side L|R)";
            }
            void RegisterArguments() noexcept override
            {
                m_hId             = m_Parser.addOption("Id",             "Spine id to move",                                true,  1);
                m_hY              = m_Parser.addOption("Y",              "New absolute world Y",                            true,  1);
                m_hColumn         = m_Parser.addOption("Column",         "Move into this already-existing column id",       false, 1);
                m_hNewColumn      = m_Parser.addOption("NewColumn",      "Synthesize a new column (value ignored)",         false, 1);
                m_hNewColumnId    = m_Parser.addOption("NewColumnId",    "Id for the new column",                           false, 1);
                m_hNeighborColumn = m_Parser.addOption("NeighborColumn", "The new column's own neighbor column id",         false, 1);
                m_hSide           = m_Parser.addOption("Side",           "Which side of -NeighborColumn the new one sits on: L or R", false, 1);
            }

            // Shared by Redo and BackupCurrenState - resolves and validates every argument without
            // mutating anything (the actual column creation only ever happens once, inside Redo()).
            std::string ResolveArgs(node_os_command_context& Ctx, std::uint64_t& OutSpineId, float& OutY, std::uint64_t& OutColumnId, bool& OutNewColumn
                                   , std::uint64_t& OutNewColumnId, std::uint64_t& OutNeighborColumnId, char& OutSide) const noexcept
            {
                auto IdArg = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                auto YArg  = m_Parser.getOptionArgAs<double>(m_hY, 0);
                if (std::holds_alternative<xerr>(IdArg) || std::holds_alternative<xerr>(YArg)) return "SetSpinePosition: bad arguments";
                OutSpineId = ParseGuid(std::get<std::string>(IdArg));
                OutY       = static_cast<float>(std::get<double>(YArg));

                const bool bHasColumn    = m_Parser.hasOption(m_hColumn);
                const bool bHasNewColumn = m_Parser.hasOption(m_hNewColumn);
                if (bHasColumn == bHasNewColumn) return "SetSpinePosition: exactly one of -Column/-NewColumn is required";

                if (bHasColumn)
                {
                    auto A = m_Parser.getOptionArgAs<std::string>(m_hColumn, 0);
                    if (std::holds_alternative<xerr>(A)) return "SetSpinePosition: bad arguments";
                    OutColumnId = ParseGuid(std::get<std::string>(A));
                    bool bFound = false; for (auto& Co : Ctx.m_Columns) if (Co.m_Id == OutColumnId) { bFound = true; break; }
                    if (!bFound) return "SetSpinePosition: -Column no longer exists";
                    OutNewColumn = false;
                    return {};
                }

                auto NCId = m_Parser.getOptionArgAs<std::string>(m_hNewColumnId, 0);
                auto NB   = m_Parser.getOptionArgAs<std::string>(m_hNeighborColumn, 0);
                auto Sd   = m_Parser.getOptionArgAs<std::string>(m_hSide, 0);
                if (std::holds_alternative<xerr>(NCId) || std::holds_alternative<xerr>(NB) || std::holds_alternative<xerr>(Sd)) return "SetSpinePosition: bad arguments";
                OutNewColumnId      = ParseGuid(std::get<std::string>(NCId));
                OutNeighborColumnId = ParseGuid(std::get<std::string>(NB));
                const auto& SideStr = std::get<std::string>(Sd);
                if (SideStr.empty() || (SideStr[0] != 'L' && SideStr[0] != 'R')) return "SetSpinePosition: -Side must be L or R";
                OutSide = SideStr[0];

                bool bNeighborFound = false;
                for (auto& Co : Ctx.m_Columns) if (Co.m_Id == OutNeighborColumnId) { bNeighborFound = true; break; }
                if (!bNeighborFound) return "SetSpinePosition: -NeighborColumn no longer exists";
                OutNewColumn = true;
                return {};
            }

            // Shared by Redo and BackupCurrenState - a column with zero spines never persists, no
            // exceptions: if it's the one flagged m_bIsRoot, Redo() transfers that flag onto the
            // destination column first (Pass C's layout walk always needs exactly one root column to
            // exist as its anchor, it doesn't care which one).
            static bool WillRemoveOldColumn(node_os_command_context& Ctx, std::uint64_t SpineId, std::uint64_t OldColumnId, std::uint64_t DestColumnId) noexcept
            {
                if (OldColumnId == DestColumnId) return false;
                for (auto& Sp : Ctx.m_Spines) if (Sp.m_Id != SpineId && Sp.m_ColumnId == OldColumnId) return false;
                for (auto& Co : Ctx.m_Columns) if (Co.m_Id == OldColumnId) return true;
                return false;
            }

            std::string Redo() noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                std::uint64_t SpineId = 0, ColumnId = 0, NewColumnId = 0, NeighborColumnId = 0; float Y = 0.0f; bool bNewColumn = false; char Side = 'R';
                if (auto Err = ResolveArgs(Ctx, SpineId, Y, ColumnId, bNewColumn, NewColumnId, NeighborColumnId, Side); !Err.empty()) return Err;

                auto SpineIt = std::find_if(Ctx.m_Spines.begin(), Ctx.m_Spines.end(), [&](auto& Sp) { return Sp.m_Id == SpineId; });
                if (SpineIt == Ctx.m_Spines.end()) return "SetSpinePosition: spine no longer exists";
                const auto OldColumnId = SpineIt->m_ColumnId;

                if (bNewColumn)
                {
                    // Splices the new column in on -Side of -NeighborColumn, exactly like CreateSpine.
                    column NewCol{ NewColumnId, 0, 0, false };
                    for (auto& Co : Ctx.m_Columns)
                        if (Co.m_Id == NeighborColumnId)
                        {
                            std::uint64_t& NearPtr = (Side == 'R') ? Co.m_RightId : Co.m_LeftId;
                            const std::uint64_t OldFarNeighborId = NearPtr;
                            NearPtr = NewColumnId;
                            if (Side == 'R') { NewCol.m_LeftId = NeighborColumnId; NewCol.m_RightId = OldFarNeighborId; }
                            else             { NewCol.m_RightId = NeighborColumnId; NewCol.m_LeftId = OldFarNeighborId; }
                            if (OldFarNeighborId != 0)
                                for (auto& Co2 : Ctx.m_Columns)
                                    if (Co2.m_Id == OldFarNeighborId)
                                    {
                                        std::uint64_t& FarPtr = (Side == 'R') ? Co2.m_LeftId : Co2.m_RightId;
                                        FarPtr = NewColumnId;
                                        break;
                                    }
                            break;
                        }
                    Ctx.m_Columns.push_back(NewCol);
                    ColumnId = NewColumnId;
                }

                SpineIt->m_ColumnId = ColumnId;
                SpineIt->m_Y        = Y;

                if (OldColumnId != ColumnId)
                {
                    bool bOtherSpineInOldColumn = false;
                    for (auto& Sp : Ctx.m_Spines) if (Sp.m_ColumnId == OldColumnId) { bOtherSpineInOldColumn = true; break; }
                    if (!bOtherSpineInOldColumn)
                    {
                        auto ColIt = std::find_if(Ctx.m_Columns.begin(), Ctx.m_Columns.end(), [&](auto& Co) { return Co.m_Id == OldColumnId; });
                        // A column with zero spines never persists, no exceptions - bridge its own
                        // Left/Right neighbors together, same as DeleteSpine's own cascade. If this was
                        // the root column, transfer that flag onto where the spine is moving TO first, so
                        // exactly one column always stays flagged root.
                        if (ColIt != Ctx.m_Columns.end())
                        {
                            if (ColIt->m_bIsRoot)
                                for (auto& Co : Ctx.m_Columns) if (Co.m_Id == ColumnId) { Co.m_bIsRoot = true; break; }
                            const auto LeftId = ColIt->m_LeftId, RightId = ColIt->m_RightId;
                            for (auto& Co : Ctx.m_Columns)
                            {
                                if (LeftId  != 0 && Co.m_Id == LeftId)  Co.m_RightId = RightId;
                                if (RightId != 0 && Co.m_Id == RightId) Co.m_LeftId  = LeftId;
                            }
                            Ctx.m_Columns.erase(ColIt);
                        }
                    }
                }
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                std::uint64_t SpineId = 0, ColumnId = 0, NewColumnId = 0, NeighborColumnId = 0; float Y = 0.0f; bool bNewColumn = false; char Side = 'R';
                const bool bOk = ResolveArgs(Ctx, SpineId, Y, ColumnId, bNewColumn, NewColumnId, NeighborColumnId, Side).empty();
                auto SpineIt = bOk ? std::find_if(Ctx.m_Spines.begin(), Ctx.m_Spines.end(), [&](auto& Sp) { return Sp.m_Id == SpineId; }) : Ctx.m_Spines.end();
                if (!bOk || SpineIt == Ctx.m_Spines.end()) { File.Write(std::uint8_t{0}); return; }

                File.Write(std::uint8_t{1});
                File.Write(*SpineIt); // spine is a plain POD-ish struct - trivially copyable snapshot

                // Everything needed to reverse the splice, if -NewColumn (same fields as CreateSpine's
                // own undo needs).
                File.Write(bNewColumn ? std::uint8_t{1} : std::uint8_t{0});
                File.Write(NeighborColumnId);
                File.Write(Side == 'R' ? std::uint8_t{1} : std::uint8_t{0});
                std::uint64_t OldFarNeighborId = 0;
                if (bNewColumn)
                    for (auto& Co : Ctx.m_Columns)
                        if (Co.m_Id == NeighborColumnId) { OldFarNeighborId = (Side == 'R') ? Co.m_RightId : Co.m_LeftId; break; }
                File.Write(OldFarNeighborId);
                const auto DestColumnId = bNewColumn ? NewColumnId : ColumnId;
                File.Write(DestColumnId);

                const auto OldColumnId = SpineIt->m_ColumnId;
                const bool bOldColumnWillBeRemoved = WillRemoveOldColumn(Ctx, SpineId, OldColumnId, DestColumnId);
                File.Write(bOldColumnWillBeRemoved ? std::uint8_t{1} : std::uint8_t{0});
                if (bOldColumnWillBeRemoved)
                {
                    auto ColIt = std::find_if(Ctx.m_Columns.begin(), Ctx.m_Columns.end(), [&](auto& Co) { return Co.m_Id == OldColumnId; });
                    if (ColIt != Ctx.m_Columns.end()) File.Write(*ColIt);
                }
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                std::uint8_t bFound = 0; File.Read(bFound);
                if (!bFound) return;
                spine OldSpine{}; File.Read(OldSpine);

                std::uint8_t bNewColumn = 0; File.Read(bNewColumn);
                std::uint64_t NeighborColumnId = 0; File.Read(NeighborColumnId);
                std::uint8_t bSideR = 0; File.Read(bSideR);
                std::uint64_t OldFarNeighborId = 0; File.Read(OldFarNeighborId);
                std::uint64_t DestColumnId = 0; File.Read(DestColumnId);

                std::uint8_t bColumnRemoved = 0; File.Read(bColumnRemoved);
                column OldColumn{}; bool bHaveColumn = false;
                if (bColumnRemoved) { File.Read(OldColumn); bHaveColumn = true; }

                auto& Ctx = get<node_os_command_context>();

                if (bNewColumn)
                {
                    // Reverse the splice Redo() performed, exactly like CreateSpine's own undo.
                    std::erase_if(Ctx.m_Columns, [&](auto& Co) { return Co.m_Id == DestColumnId; });
                    for (auto& Co : Ctx.m_Columns)
                        if (Co.m_Id == NeighborColumnId)
                        {
                            if (bSideR) Co.m_RightId = OldFarNeighborId; else Co.m_LeftId = OldFarNeighborId;
                            break;
                        }
                    if (OldFarNeighborId != 0)
                        for (auto& Co : Ctx.m_Columns)
                            if (Co.m_Id == OldFarNeighborId)
                            {
                                if (bSideR) Co.m_LeftId = NeighborColumnId; else Co.m_RightId = NeighborColumnId;
                                break;
                            }
                }

                for (auto& Sp : Ctx.m_Spines) if (Sp.m_Id == OldSpine.m_Id) { Sp = OldSpine; break; }

                if (bHaveColumn)
                {
                    // If Redo() transferred the root flag onto the destination column, hand it back -
                    // exactly one column stays flagged root at all times. A no-op if the destination was
                    // itself a -NewColumn splice already unwound above.
                    if (OldColumn.m_bIsRoot)
                        for (auto& Co : Ctx.m_Columns) if (Co.m_Id == DestColumnId) { Co.m_bIsRoot = false; break; }
                    Ctx.m_Columns.push_back(OldColumn);
                    for (auto& Co : Ctx.m_Columns)
                    {
                        if (OldColumn.m_LeftId  != 0 && Co.m_Id == OldColumn.m_LeftId)  Co.m_RightId = OldColumn.m_Id;
                        if (OldColumn.m_RightId != 0 && Co.m_Id == OldColumn.m_RightId) Co.m_LeftId  = OldColumn.m_Id;
                    }
                }
            }

            xcmdline::parser::handle m_hId, m_hY, m_hColumn, m_hNewColumn, m_hNewColumnId, m_hNeighborColumn, m_hSide;
        };

        //================================================================================================
        // DeleteSpine - legal only when the spine currently has zero member nodes (deleting a populated
        // one is two user actions: delete its nodes, then delete the now-empty placeholder - same
        // single-responsibility shape as DeleteLink). Cascades to remove the column too if this was its
        // last spine, bridging its own Left/Right neighbors together (a column with zero spines never
        // persists) - except the one column flagged m_bIsRoot, which Pass C's layout walk always needs
        // to exist as its anchor.
        //================================================================================================
        struct delete_spine_cmd : xundo::command_base
        {
            delete_spine_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "DeleteSpine", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Deletes an empty spine (and its column, if it was the column's last one). Usage: DeleteSpine -Id spineid"; }
            void RegisterArguments() noexcept override { m_hId = m_Parser.addOption("Id", "Spine id", true, 1); }

            std::string Redo() noexcept override
            {
                auto IdArg = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                if (std::holds_alternative<xerr>(IdArg)) return "DeleteSpine: bad arguments";
                const auto SpineId = ParseGuid(std::get<std::string>(IdArg));

                auto& Ctx = get<node_os_command_context>();
                auto SpineIt = std::find_if(Ctx.m_Spines.begin(), Ctx.m_Spines.end(), [&](auto& Sp) { return Sp.m_Id == SpineId; });
                if (SpineIt == Ctx.m_Spines.end()) return "DeleteSpine: spine no longer exists";
                if (SpineIt->m_bIsRoot) return "DeleteSpine: cannot delete the root spine";
                for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == SpineId) return "DeleteSpine: spine still has nodes";

                const auto ColumnId = SpineIt->m_ColumnId;
                bool bOtherSpineInColumn = false;
                for (auto& Sp : Ctx.m_Spines) if (Sp.m_Id != SpineId && Sp.m_ColumnId == ColumnId) { bOtherSpineInColumn = true; break; }

                Ctx.m_Spines.erase(SpineIt);
                if (!bOtherSpineInColumn)
                {
                    auto ColIt = std::find_if(Ctx.m_Columns.begin(), Ctx.m_Columns.end(), [&](auto& Co) { return Co.m_Id == ColumnId; });
                    if (ColIt != Ctx.m_Columns.end() && !ColIt->m_bIsRoot)
                    {
                        const auto LeftId = ColIt->m_LeftId, RightId = ColIt->m_RightId;
                        for (auto& Co : Ctx.m_Columns)
                        {
                            if (LeftId  != 0 && Co.m_Id == LeftId)  Co.m_RightId = RightId;
                            if (RightId != 0 && Co.m_Id == RightId) Co.m_LeftId  = LeftId;
                        }
                        Ctx.m_Columns.erase(ColIt);
                    }
                }
                if (Ctx.m_Selection.m_SelectedGapSpineId == SpineId) { Ctx.m_Selection.m_SelectedGapSpineId = 0; Ctx.m_Selection.m_SelectedGapIndex = -1; }
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                auto IdArg = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                const auto SpineId = std::holds_alternative<xerr>(IdArg) ? std::uint64_t{0} : ParseGuid(std::get<std::string>(IdArg));

                auto SpineIt = std::find_if(Ctx.m_Spines.begin(), Ctx.m_Spines.end(), [&](auto& Sp) { return Sp.m_Id == SpineId; });
                const bool bFound = SpineIt != Ctx.m_Spines.end();
                File.Write(bFound ? std::uint8_t{1} : std::uint8_t{0});
                if (!bFound) return;

                File.Write(*SpineIt); // spine is a plain POD-ish struct - trivially copyable snapshot

                bool bOtherSpineInColumn = false;
                for (auto& Sp : Ctx.m_Spines) if (Sp.m_Id != SpineId && Sp.m_ColumnId == SpineIt->m_ColumnId) { bOtherSpineInColumn = true; break; }
                auto ColIt = std::find_if(Ctx.m_Columns.begin(), Ctx.m_Columns.end(), [&](auto& Co) { return Co.m_Id == SpineIt->m_ColumnId; });
                const bool bColumnWillBeRemoved = !bOtherSpineInColumn && ColIt != Ctx.m_Columns.end() && !ColIt->m_bIsRoot;
                File.Write(bColumnWillBeRemoved ? std::uint8_t{1} : std::uint8_t{0}); // "will the column also be removed"
                if (bColumnWillBeRemoved) File.Write(*ColIt);
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                std::uint8_t bFound = 0; File.Read(bFound);
                if (!bFound) return;
                spine Spine{}; File.Read(Spine);
                std::uint8_t bColumnRemoved = 0; File.Read(bColumnRemoved);

                auto& Ctx = get<node_os_command_context>();
                Ctx.m_Spines.push_back(Spine);
                if (bColumnRemoved)
                {
                    column Column{}; File.Read(Column);
                    Ctx.m_Columns.push_back(Column);
                    for (auto& Co : Ctx.m_Columns)
                    {
                        if (Column.m_LeftId  != 0 && Co.m_Id == Column.m_LeftId)  Co.m_RightId = Column.m_Id;
                        if (Column.m_RightId != 0 && Co.m_Id == Column.m_RightId) Co.m_LeftId  = Column.m_Id;
                    }
                }
            }

            xcmdline::parser::handle m_hId;
        };

        //================================================================================================
        // ReorderNodes - carries the FULL new id-order sequence (matching how MoveNodesTo/InsertNodeAt
        // already reassign every node's m_Order densely, not just the moved ones' positions).
        //================================================================================================
        struct reorder_nodes_cmd : xundo::command_base
        {
            reorder_nodes_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "ReorderNodes", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Sets every node's stacking order. Usage: ReorderNodes -Ids id[,id...] (the full new order)"; }
            void RegisterArguments() noexcept override { m_hIds = m_Parser.addOption("Ids", "Full new order, comma-separated node ids", true, 1); }

            std::string Redo() noexcept override
            {
                auto IdsArg = m_Parser.getOptionArgAs<std::string>(m_hIds, 0);
                if (std::holds_alternative<xerr>(IdsArg)) return "ReorderNodes: bad arguments";
                const auto NewOrder = SplitIds(std::get<std::string>(IdsArg));
                auto& Ctx = get<node_os_command_context>();
                for (int i = 0; i < (int)NewOrder.size(); ++i)
                    for (auto& N : Ctx.m_Nodes) if (N.m_Id == NewOrder[i]) { N.m_Order = i; break; }
                return {}; // pure reorder - doesn't change what's connected to what, no bDirty (matches existing MoveNodesTo)
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                File.Write(static_cast<std::uint32_t>(Ctx.m_Nodes.size()));
                for (auto& N : Ctx.m_Nodes) { File.Write(N.m_Id); File.Write(N.m_Order); }
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                std::uint32_t Count = 0; File.Read(Count);
                for (std::uint32_t i = 0; i < Count; ++i)
                {
                    std::uint64_t Id = 0; int Order = 0; File.Read(Id); File.Read(Order);
                    for (auto& N : Ctx.m_Nodes) if (N.m_Id == Id) { N.m_Order = Order; break; }
                }
            }

            xcmdline::parser::handle m_hIds;
        };

        //================================================================================================
        // MoveNodesToSpine - moves node(s) into a DIFFERENT spine (dragging a node onto another
        // spine's own marker), renumbering every spine it touches - each source spine's own remainder
        // and the destination spine's new dense order - densely to 0..N-1, same reasoning as
        // CreateNode/ReorderNodes: deleting/removing leaves gaps that get closed here, never patched
        // with arithmetic on the existing m_Order values. Addressed the same way CreateNode addresses
        // insertion (-After/-Before an existing node in the destination, or -InSpine to append
        // regardless of that spine's current size).
        //================================================================================================
        struct move_nodes_to_spine_cmd : xundo::command_base
        {
            move_nodes_to_spine_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "MoveNodesToSpine", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override
            {
                return "Moves node(s) into a different spine. Usage: MoveNodesToSpine -Ids id[,id...] (-After id | -Before id | -InSpine spineid)";
            }
            void RegisterArguments() noexcept override
            {
                m_hIds     = m_Parser.addOption("Ids",     "Node ids to move, comma-separated",                          true,  1);
                m_hAfter   = m_Parser.addOption("After",   "Insert right after this node id in the destination spine",  false, 1);
                m_hBefore  = m_Parser.addOption("Before",  "Insert right before this node id in the destination spine", false, 1);
                m_hInSpine = m_Parser.addOption("InSpine", "Append to this spine, whatever its current size",           false, 1);
            }

            // Shared by Redo and BackupCurrenState - resolves -After/-Before/-InSpine into a target
            // spine + dense order index, exactly like create_node_cmd's own ResolveTargetOrder.
            std::string ResolveTarget(node_os_command_context& Ctx, std::uint64_t& OutSpineId, int& OutOrder) const noexcept
            {
                const bool bHasAfter   = m_Parser.hasOption(m_hAfter);
                const bool bHasBefore  = m_Parser.hasOption(m_hBefore);
                const bool bHasInSpine = m_Parser.hasOption(m_hInSpine);
                if ((bHasAfter ? 1 : 0) + (bHasBefore ? 1 : 0) + (bHasInSpine ? 1 : 0) != 1)
                    return "MoveNodesToSpine: exactly one of -After/-Before/-InSpine is required";

                if (bHasInSpine)
                {
                    auto RefArg = m_Parser.getOptionArgAs<std::string>(m_hInSpine, 0);
                    if (std::holds_alternative<xerr>(RefArg)) return "MoveNodesToSpine: bad arguments";
                    const auto SpineId = ParseGuid(std::get<std::string>(RefArg));
                    bool bFound = false;
                    for (auto& S : Ctx.m_Spines) if (S.m_Id == SpineId) { bFound = true; break; }
                    if (!bFound) return "MoveNodesToSpine: -InSpine spine no longer exists";
                    int Count = 0;
                    for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == SpineId) ++Count;
                    OutSpineId = SpineId; OutOrder = Count; return {};
                }

                auto RefArg = m_Parser.getOptionArgAs<std::string>(bHasAfter ? m_hAfter : m_hBefore, 0);
                if (std::holds_alternative<xerr>(RefArg)) return "MoveNodesToSpine: bad arguments";
                const auto RefId = ParseGuid(std::get<std::string>(RefArg));
                std::uint64_t RefSpineId = 0; int RefOrder = 0;
                if (!ResolveNodeSpineAndOrder(Ctx.m_Nodes, RefId, RefSpineId, RefOrder)) return "MoveNodesToSpine: -After/-Before node no longer exists";
                OutSpineId = RefSpineId; OutOrder = bHasAfter ? RefOrder + 1 : RefOrder;
                return {};
            }

            std::string Redo() noexcept override
            {
                auto IdsArg = m_Parser.getOptionArgAs<std::string>(m_hIds, 0);
                if (std::holds_alternative<xerr>(IdsArg)) return "MoveNodesToSpine: bad arguments";
                const auto MovingIds = SplitIds(std::get<std::string>(IdsArg));
                if (MovingIds.empty()) return "MoveNodesToSpine: no ids given";

                auto& Ctx = get<node_os_command_context>();
                std::uint64_t TargetSpineId = 0; int TargetOrder = 0;
                if (auto Err = ResolveTarget(Ctx, TargetSpineId, TargetOrder); !Err.empty()) return Err;

                auto IsMoving = [&](std::uint64_t Id) { return std::find(MovingIds.begin(), MovingIds.end(), Id) != MovingIds.end(); };

                auto OrderOf = [&](std::uint64_t Id) { for (auto& N : Ctx.m_Nodes) if (N.m_Id == Id) return N.m_Order; return 0; };

                // The target's CURRENT dense order, snapshotted before any renumbering below touches it
                // - TargetOrder (resolved above) is expressed against this exact snapshot.
                std::vector<std::uint64_t> TargetOrderBefore;
                for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == TargetSpineId) TargetOrderBefore.push_back(N.m_Id);
                std::sort(TargetOrderBefore.begin(), TargetOrderBefore.end(), [&](std::uint64_t A, std::uint64_t B) { return OrderOf(A) < OrderOf(B); });

                // How many movers already sitting in the TARGET spine were before TargetOrder -
                // removing them shifts the insertion point left by that many (same adjustment the UI's
                // own same-spine MoveNodesTo already makes).
                int Adjust = 0;
                for (int i = 0; i < TargetOrder && i < (int)TargetOrderBefore.size(); ++i)
                    if (IsMoving(TargetOrderBefore[i])) ++Adjust;

                // Every distinct spine this touches: every mover's OWN current spine, plus the target -
                // each gets its own remainder (or, for the target, remainder-plus-movers) renumbered
                // densely to 0..N-1.
                std::set<std::uint64_t> TouchedSpineIds{ TargetSpineId };
                for (auto Id : MovingIds)
                    for (auto& N : Ctx.m_Nodes) if (N.m_Id == Id) { TouchedSpineIds.insert(N.m_SpineId); break; }

                for (auto SpineId : TouchedSpineIds)
                {
                    std::vector<std::uint64_t> Remaining;
                    for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == SpineId && !IsMoving(N.m_Id)) Remaining.push_back(N.m_Id);
                    std::sort(Remaining.begin(), Remaining.end(), [&](std::uint64_t A, std::uint64_t B) { return OrderOf(A) < OrderOf(B); });
                    if (SpineId == TargetSpineId)
                    {
                        const int InsertAt = std::clamp(TargetOrder - Adjust, 0, (int)Remaining.size());
                        Remaining.insert(Remaining.begin() + InsertAt, MovingIds.begin(), MovingIds.end());
                    }
                    for (int i = 0; i < (int)Remaining.size(); ++i)
                        for (auto& N : Ctx.m_Nodes) if (N.m_Id == Remaining[i]) { N.m_Order = i; break; }
                }
                for (auto Id : MovingIds)
                    for (auto& N : Ctx.m_Nodes) if (N.m_Id == Id) N.m_SpineId = TargetSpineId;

                return {}; // pure reassignment - doesn't change what's connected to what, no bDirty (matches ReorderNodes)
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                File.Write(static_cast<std::uint32_t>(Ctx.m_Nodes.size()));
                for (auto& N : Ctx.m_Nodes) { File.Write(N.m_Id); File.Write(N.m_Order); File.Write(N.m_SpineId); }
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                std::uint32_t Count = 0; File.Read(Count);
                for (std::uint32_t i = 0; i < Count; ++i)
                {
                    std::uint64_t Id = 0; int Order = 0; std::uint64_t SpineId = 0;
                    File.Read(Id); File.Read(Order); File.Read(SpineId);
                    for (auto& N : Ctx.m_Nodes) if (N.m_Id == Id) { N.m_Order = Order; N.m_SpineId = SpineId; break; }
                }
            }

            xcmdline::parser::handle m_hIds, m_hAfter, m_hBefore, m_hInSpine;
        };

        //================================================================================================
        // SetProperties - unlike every other command, the mutation has ALREADY happened by the time
        // this is issued (ImGui already wrote the live property bytes this frame, including whatever
        // an arbitrary plugin-drawn custom button did). So both snapshots travel in the command string
        // itself, base64-encoded: BackupCurrenState never touches live state, it just pulls -Before out
        // of the already-parsed args; Redo (re-)applies -After; Undo applies -Before. One command
        // covers scalar edits, list resizes, and custom-button mutations uniformly.
        //================================================================================================
        struct set_properties_cmd : xundo::command_base
        {
            set_properties_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "SetProperties", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Applies a property snapshot to a node. Usage: SetProperties -NodeId N -Before base64 -After base64"; }
            void RegisterArguments() noexcept override
            {
                m_hNodeId = m_Parser.addOption("NodeId", "Node id", true, 1);
                m_hBefore = m_Parser.addOption("Before", "Base64 property snapshot, pre-edit",  true, 1);
                m_hAfter  = m_Parser.addOption("After",  "Base64 property snapshot, post-edit", true, 1);
            }

            static xnode_os_node* GetNodeFor(node_os_command_context& Ctx, std::uint64_t NodeId)
            {
                for (auto& N : Ctx.m_Nodes)
                    if (N.m_Id == NodeId && HasSerializableProperties(N.m_pNode))
                        return N.m_pNode;
                return nullptr;
            }

            std::string Redo() noexcept override
            {
                auto NodeId = m_Parser.getOptionArgAs<std::string>(m_hNodeId, 0);
                auto After  = m_Parser.getOptionArgAs<std::string>(m_hAfter, 0);
                if (std::holds_alternative<xerr>(NodeId) || std::holds_alternative<xerr>(After)) return "SetProperties: bad arguments";
                auto& Ctx = get<node_os_command_context>();
                if (auto* pNode = GetNodeFor(Ctx, ParseGuid(std::get<std::string>(NodeId))))
                {
                    ApplyPropertiesFromString(pNode, Base64Decode(std::get<std::string>(After)));
                    Ctx.m_bDirty = true;
                }
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto NodeId = m_Parser.getOptionArgAs<std::string>(m_hNodeId, 0);
                auto Before = m_Parser.getOptionArgAs<std::string>(m_hBefore, 0);
                File.Write(std::holds_alternative<xerr>(NodeId) ? std::uint64_t{0} : ParseGuid(std::get<std::string>(NodeId)));
                WriteString(File, std::holds_alternative<xerr>(Before) ? std::string{} : std::get<std::string>(Before));
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                std::uint64_t NodeId = 0; File.Read(NodeId);
                const std::string BeforeB64 = ReadString(File);
                auto& Ctx = get<node_os_command_context>();
                if (auto* pNode = GetNodeFor(Ctx, NodeId))
                {
                    ApplyPropertiesFromString(pNode, Base64Decode(BeforeB64));
                    Ctx.m_bDirty = true;
                }
            }

            xcmdline::parser::handle m_hNodeId, m_hBefore, m_hAfter;
        };

        //================================================================================================
        // Select - one command covers all three selection fields at once (SelectedNodes/SelectedLink/
        // SelectedGap), matching how every existing interaction site already sets all three together.
        //================================================================================================
        struct select_cmd : xundo::command_base
        {
            select_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "Select", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override
            {
                return "Sets the current selection - every flag is optional, omitted means \"none of this kind\"."
                       " Usage: Select [-Nodes id[,id...]] [-Link id] [-MarkerAfter id | -MarkerBefore id | -MarkerSpine spineid]";
            }
            void RegisterArguments() noexcept override
            {
                m_hNodes        = m_Parser.addOption("Nodes",        "Selected node ids, comma-separated",                  false, 1);
                m_hLink         = m_Parser.addOption("Link",         "Selected link id",                                    false, 1);
                m_hMarkerAfter  = m_Parser.addOption("MarkerAfter",  "Select the insert marker right after this node id",   false, 1);
                m_hMarkerBefore = m_Parser.addOption("MarkerBefore", "Select the insert marker right before this node id",  false, 1);
                m_hMarkerSpine  = m_Parser.addOption("MarkerSpine",  "Select an empty spine's own placeholder marker",       false, 1);
            }

            std::string Redo() noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                auto& S   = Ctx.m_Selection;

                S.m_SelectedNodes.clear();
                if (m_Parser.hasOption(m_hNodes))
                {
                    auto NodesArg = m_Parser.getOptionArgAs<std::string>(m_hNodes, 0);
                    if (std::holds_alternative<xerr>(NodesArg)) return "Select: bad arguments";
                    const auto Ids = SplitIds(std::get<std::string>(NodesArg));
                    S.m_SelectedNodes = std::set<std::uint64_t>(Ids.begin(), Ids.end());
                }

                S.m_SelectedLink = 0;
                if (m_Parser.hasOption(m_hLink))
                {
                    auto LinkArg = m_Parser.getOptionArgAs<std::string>(m_hLink, 0);
                    if (std::holds_alternative<xerr>(LinkArg)) return "Select: bad arguments";
                    S.m_SelectedLink = ParseGuid(std::get<std::string>(LinkArg));
                }

                S.m_SelectedGapSpineId = 0;
                S.m_SelectedGapIndex   = -1;
                const bool bHasAfter  = m_Parser.hasOption(m_hMarkerAfter);
                const bool bHasBefore = m_Parser.hasOption(m_hMarkerBefore);
                const bool bHasSpine  = m_Parser.hasOption(m_hMarkerSpine);
                if ((bHasAfter ? 1 : 0) + (bHasBefore ? 1 : 0) + (bHasSpine ? 1 : 0) > 1)
                    return "Select: -MarkerAfter, -MarkerBefore and -MarkerSpine are mutually exclusive";
                if (bHasAfter || bHasBefore)
                {
                    auto RefArg = m_Parser.getOptionArgAs<std::string>(bHasAfter ? m_hMarkerAfter : m_hMarkerBefore, 0);
                    if (std::holds_alternative<xerr>(RefArg)) return "Select: bad arguments";
                    const auto RefId = ParseGuid(std::get<std::string>(RefArg));
                    std::uint64_t RefSpineId = 0; int RefOrder = 0;
                    if (!ResolveNodeSpineAndOrder(Ctx.m_Nodes, RefId, RefSpineId, RefOrder)) return "Select: -MarkerAfter/-MarkerBefore node no longer exists";
                    S.m_SelectedGapSpineId = RefSpineId; S.m_SelectedGapIndex = bHasAfter ? RefOrder + 1 : RefOrder;
                }
                else if (bHasSpine)
                {
                    // Legal only for an empty spine - a non-empty one already has -MarkerBefore <its
                    // first node> to select the very same visual slot.
                    auto RefArg = m_Parser.getOptionArgAs<std::string>(m_hMarkerSpine, 0);
                    if (std::holds_alternative<xerr>(RefArg)) return "Select: bad arguments";
                    const auto RefSpineId = ParseGuid(std::get<std::string>(RefArg));
                    bool bFound = false;
                    for (auto& Sp : Ctx.m_Spines) if (Sp.m_Id == RefSpineId) { bFound = true; break; }
                    if (!bFound) return "Select: -MarkerSpine spine no longer exists";
                    for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == RefSpineId) return "Select: -MarkerSpine is only for an empty spine";
                    S.m_SelectedGapSpineId = RefSpineId; S.m_SelectedGapIndex = 0;
                }
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override { BackupSelection(get<node_os_command_context>(), File); }
            void Undo(xundo::undo_file& File) noexcept override { RestoreSelection(get<node_os_command_context>(), File); }

            xcmdline::parser::handle m_hNodes, m_hLink, m_hMarkerAfter, m_hMarkerBefore, m_hMarkerSpine;
        };

        //================================================================================================
        // ClearSelection - a dedicated, self-describing command name for "select nothing", rather than
        // Select with every flag omitted: a bare "Select" with nothing after it in the history log still
        // makes a reader (human or agent) work out what it did; "ClearSelection" says it outright.
        //================================================================================================
        struct clear_selection_cmd : xundo::command_base
        {
            clear_selection_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "ClearSelection", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Deselects everything (nodes, link, insert marker). Usage: ClearSelection"; }
            void RegisterArguments() noexcept override {} // takes no arguments at all

            std::string Redo() noexcept override
            {
                auto& S = get<node_os_command_context>().m_Selection;
                S.m_SelectedNodes.clear();
                S.m_SelectedLink = 0;
                S.m_SelectedGapSpineId = 0;
                S.m_SelectedGapIndex   = -1;
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override { BackupSelection(get<node_os_command_context>(), File); }
            void Undo(xundo::undo_file& File) noexcept override { RestoreSelection(get<node_os_command_context>(), File); }
        };

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
