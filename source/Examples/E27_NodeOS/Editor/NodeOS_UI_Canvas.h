#pragma once
// DrawGraphCanvas alone, extracted from the monolithic E27_NodeOS_Editor.cpp (header #9) - the
// single largest function in the file (~1830 lines), kept isolated on its own per the split plan.
#include "NodeOS_Common.h"
#include "NodeOS_Types.h"
#include "NodeOS_PropertySerialize.h"
#include "NodeOS_CommandBuilders.h"
#include "NodeOS_CanvasSupport.h"

namespace nodeos
{
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
            // Resolved (guid-aware) indices, not the link's raw stored ones - OutPinOf/InPinOf's key is
            // keyed by CURRENT index (matching how the per-row draw loop elsewhere computes the same
            // key for the actual pin it's drawing), so a stale index here would tag the wrong pin's
            // side-affinity entirely, not just render a wire from the wrong anchor.
            auto* pSrcDesc = DescOf(FindNode(Link.m_SourceNode)); auto* pDstDesc = DescOf(FindNode(Link.m_TargetNode));
            const int SrcIdx = pSrcDesc ? ResolveSourceIndex(Link, pSrcDesc->getOutputs()) : -1;
            const int DstIdx = pDstDesc ? ResolveTargetIndex(Link, pDstDesc->getInputs())  : -1;
            PortSides[OutPinOf(Link.m_SourceNode, SrcIdx)].insert(SourceSide);
            PortSides[InPinOf(Link.m_TargetNode, DstIdx)].insert(TargetSide);
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
                const int SrcIdx = ResolveSourceIndex(Link, SrcOutputs), DstIdx = ResolveTargetIndex(Link, DstInputs);
                if (SrcIdx < 0 || DstIdx < 0) continue;
                const port_ref OutP{ true, SrcIdx, &SrcOutputs[SrcIdx] };
                const port_ref InP { false, DstIdx, &DstInputs[DstIdx] };
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
            const int SrcIdx = ResolveSourceIndex(Link, SrcOutputs), DstIdx = ResolveTargetIndex(Link, DstInputs);
            if (SrcIdx < 0 || DstIdx < 0) continue;
            const port_ref OutP{ true, SrcIdx, &SrcOutputs[SrcIdx] };
            const port_ref InP { false, DstIdx, &DstInputs[DstIdx] };
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
            const bool bScopeInvalid = !Link.m_bReadOnly && !IsDataLinkScopeValid(Link.m_SourceNode, SrcIdx, Link.m_TargetNode, DstIdx, Nodes, EnclosingChains);
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
                    // OutIdx/InIdx were only ever needed to find WHICH port the drag landed on right
                    // now - the link itself stores each port's own m_Guid, never the index (see
                    // link_instance's own comment).
                    commands::Run(System, commands::MakeConnect(xresource::guid_generator::Instance64(), OutNode, InNode, OutOutputs[OutIdx].m_Guid, InInputs[InIdx].m_Guid));
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
                const int SrcIdx = ResolveSourceIndex(Link, SrcOutputs), DstIdx = ResolveTargetIndex(Link, DstInputs);
                if (SrcIdx < 0 || DstIdx < 0) continue;
                const port_ref OutP{ true, SrcIdx, &SrcOutputs[SrcIdx] };
                const port_ref InP { false, DstIdx, &DstInputs[DstIdx] };
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
}
