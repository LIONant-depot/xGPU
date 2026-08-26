#pragma once
// Canvas-support cluster (mesh preview, port/geometry/type-color helpers, wildcard resolution,
// drag/selection/view state), extracted from the monolithic E27_NodeOS_Editor.cpp (header #7). This
// is the header NodeOS_Interpreter.h (header #4) and NodeOS_Codegen.h (header #5) forward-declared
// ResolveUnconnectedLiteral/FindMemberByName against - both real definitions live here. Also carries
// ExecuteGraph (not explicitly named by the split plan; placed here since it needs GetInputValue/
// mesh_preview_system from this same cluster plus RunProgram from NodeOS_Interpreter.h, and nothing
// later in the file depends on it).
#include "NodeOS_Common.h"
#include "NodeOS_Types.h"
#include "NodeOS_PropertySerialize.h"
#include "NodeOS_Interpreter.h"

namespace nodeos
{
    //------------------------------------------------------------------------------------------------
    static const char* PortTypeToPreview(const char* pTypeName, void* pValue)
    {
        static thread_local std::string s_Scratch;
        // No value to show is not itself information worth a line of text - especially now that
        // most scalar inputs show either a live wire or an inline constant instead of ever reaching
        // this at all. Blank, not "(none)".
        if (!pValue) { return ""; }
        if (std::strcmp(pTypeName, "Text") == 0) { return static_cast<const char*>(pValue); }
        if (std::strcmp(pTypeName, "Mesh") == 0)
        {
            auto* pMesh = static_cast<xnode_os_mesh_data*>(pValue);
            s_Scratch = std::format("{} verts / {} tris", pMesh->m_VertexCount, pMesh->m_IndexCount / 3);
            return s_Scratch.c_str();
        }
        // Float/Int/Short - now that RunProgram (NODE_SCRIPTING_DESIGN.md §12.6) actually produces
        // real values instead of every Execute() being a no-op, this path is finally reachable with
        // a genuine numeric pValue - dereference it properly instead of falling through to the raw-
        // pointer placeholder below, which used to be harmless only because nothing ever got here
        // with a real value to show.
        if (std::strcmp(pTypeName, "Float") == 0) { s_Scratch = std::format("{:.3f}", *static_cast<float*>(pValue)); return s_Scratch.c_str(); }
        if (std::strcmp(pTypeName, "Int")   == 0) { s_Scratch = std::to_string(*static_cast<std::int32_t*>(pValue)); return s_Scratch.c_str(); }
        if (std::strcmp(pTypeName, "Short") == 0) { s_Scratch = std::to_string(*static_cast<std::int16_t*>(pValue)); return s_Scratch.c_str(); }
        // Anything else genuinely unrecognized - the raw pointer as a placeholder, same as before.
        s_Scratch = std::format("<{:#x}>", (std::uintptr_t)pValue);
        return s_Scratch.c_str();
    }

    //------------------------------------------------------------------------------------------------
    // A tiny, dedicated render pipeline (position+normal, normal-as-color, no lighting/textures) so
    // any "Mesh"-typed pin can show a REAL rendered preview inline in its node - not just a vertex
    // count. Deliberately separate from the rest of the engine's material system: this is here to
    // prove wiring nodes together produces something that visibly does work, not to be a real
    // material. One shared static camera angle is reused for every preview square.
    //
    // Renders into a small offscreen texture per pin (same render-to-texture pattern as
    // E22_FramebufferTarget), then displays that texture via a plain ImGui::Image() - NOT a raw
    // viewport-rect draw callback. imgui-node-editor defers/transforms node content for pan+zoom in
    // its own Begin/End pass, so anything positioned via a manually-captured screen rect (as an
    // earlier version of this code did) ends up drawn in the wrong place the moment the canvas is
    // panned or the node dragged; ImGui::Image() is a normal widget the library repositions
    // correctly right along with everything else inside the node, same as the Text() calls above.
    //------------------------------------------------------------------------------------------------
    struct mesh_preview_vert { float m_X, m_Y, m_Z, m_NX, m_NY, m_NZ; };
    struct mesh_preview_push_constants { xmath::fmat4 m_L2C; };

    struct mesh_preview_entry
    {
        xgpu::texture     m_Texture;
        xgpu::renderpass  m_RenderPass;
        xgpu::buffer      m_VertexBuffer;
        // Draw() is always an indexed draw in this engine (see E22_FramebufferTarget's own usage) -
        // this preview data is already a plain, non-indexed triangle list (RebuildIfMesh expands each
        // triangle into 3 raw vertices), so this is a trivial identity mapping (index i = i), just to
        // satisfy Draw()'s requirement that SOME index buffer is bound. Sized to the largest vertex
        // count this entry has ever needed - recreated only when a bigger mesh comes through.
        xgpu::buffer      m_IndexBuffer;
        int                m_IndexCapacity = 0;
        int                m_VertexCount = 0;
        bool               m_bTextureReady = false;
    };

    struct mesh_preview_system
    {
        xgpu::vertex_descriptor                                    m_VertexDescriptor;
        xgpu::pipeline                                               m_Pipeline;
        xgpu::pipeline_instance                                      m_PipelineInstance;
        xgpu::tools::view                                            m_View;
        std::unordered_map<std::uint64_t, mesh_preview_entry>        m_Entries;

        static constexpr int s_PreviewSize = 110;

        bool Init(xgpu::device& Device)
        {
            auto Attributes = std::array
            { xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(mesh_preview_vert, m_X),  .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D }
            , xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(mesh_preview_vert, m_NX), .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D }
            };
            if (auto Err = Device.Create(m_VertexDescriptor, { .m_VertexSize = sizeof(mesh_preview_vert), .m_Attributes = Attributes }); Err)
                return false;

            xgpu::shader FragShader, VertShader;
            if (auto Err = Device.Create(FragShader, { .m_Type = xgpu::shader::type::bit::FRAGMENT, .m_Sharer = xgpu::shader::setup::raw_data{ std::array{
                #include "E27_normal_frag.h"
            } } }); Err) return false;
            if (auto Err = Device.Create(VertShader, { .m_Type = xgpu::shader::type::bit::VERTEX, .m_Sharer = xgpu::shader::setup::raw_data{ std::array{
                #include "E27_normal_vert.h"
            } } }); Err) return false;

            auto Shaders = std::array<const xgpu::shader*, 2>{ &FragShader, &VertShader };
            if (auto Err = Device.Create(m_Pipeline, { .m_VertexDescriptor = m_VertexDescriptor, .m_Shaders = Shaders, .m_PushConstantsSize = sizeof(mesh_preview_push_constants)
                // NONE, not the pipeline default of BACK - Cube's index data was written only caring
                // about triangle *count* (see cube_node.cpp's own comment), never checked for
                // consistent CCW winding, so half its faces would otherwise get culled as "backfacing".
                , .m_Primitive     = { .m_Cull = xgpu::pipeline::primitive::cull::NONE }
                // The preview's render pass has a color attachment only, no depth texture - leaving
                // the pipeline's own depth-test-enabled default would compare against a nonexistent
                // depth buffer.
                , .m_DepthStencil  = { .m_bDepthTestEnable = false, .m_bDepthWriteEnable = false }
                }); Err)
                return false;
            if (auto Err = Device.Create(m_PipelineInstance, { .m_PipeLine = m_Pipeline }); Err)
                return false;

            m_View.setViewport({ 0, 0, s_PreviewSize, s_PreviewSize });
            m_View.LookAt(3.0f, xmath::radian3(30_xdeg, 45_xdeg, 0_xdeg), xmath::fvec3::fromZero());
            return true;
        }

        // Flattens the mesh into a plain triangle list with a computed flat (per-face) normal per
        // vertex - the plugin ABI only carries positions+indices (xnode_os_mesh_data), so the host
        // is the one that derives shading data, same way it's the host (not the plugin) that owns
        // the actual GPU rendering. Creates the pin's offscreen texture/render pass once, lazily.
        void RebuildIfMesh(xgpu::device& Device, std::uint64_t PinId, const char* pTypeName, void* pValue)
        {
            // Deliberately never erase/destroy an existing entry here, even though a null/typeless
            // value means "nothing to show any more" - its xgpu::texture may already be referenced by
            // an ImGui::Image() draw command queued earlier this same frame (or, since there's no fence
            // sync on this path, even a frame or two back), so destroying the GPU objects synchronously
            // risks a use-after-free/destroy-while-in-flight crash. This is exactly what reproduced by
            // deleting a node or a connection with a live mesh flowing through it. Just stop rendering
            // into it (m_VertexCount = 0 makes DrawPreviewSquare fall back to the placeholder) and leave
            // the GPU objects alone for the rest of the program - a deliberately accepted small leak on
            // disconnect, not a correctness risk.
            auto StopRendering = [&] { if (auto It = m_Entries.find(PinId); It != m_Entries.end()) It->second.m_VertexCount = 0; };

            if (!pValue || std::strcmp(pTypeName, "Mesh") != 0) { StopRendering(); return; }
            auto* pMesh = static_cast<xnode_os_mesh_data*>(pValue);

            std::vector<mesh_preview_vert> Verts;
            Verts.reserve(pMesh->m_IndexCount);
            for (unsigned int i = 0; i + 2 < pMesh->m_IndexCount; i += 3)
            {
                const auto GetPos = [&](unsigned int Idx) { return xmath::fvec3{ pMesh->m_pPositions[Idx*3+0], pMesh->m_pPositions[Idx*3+1], pMesh->m_pPositions[Idx*3+2] }; };
                const auto A = GetPos(pMesh->m_pIndices[i+0]), B = GetPos(pMesh->m_pIndices[i+1]), C = GetPos(pMesh->m_pIndices[i+2]);
                const auto N = (B - A).Cross(C - A).Normalize();
                for (auto& P : { A, B, C })
                    Verts.push_back({ P.m_X, P.m_Y, P.m_Z, N.m_X, N.m_Y, N.m_Z });
            }
            if (Verts.empty()) { StopRendering(); return; }

            auto& Entry = m_Entries[PinId];
            if (!Entry.m_bTextureReady)
            {
                if (Device.Create(Entry.m_Texture, { .m_Format = xgpu::texture::format::R8G8B8A8_UNORM, .m_Width = s_PreviewSize, .m_Height = s_PreviewSize, .m_isGamma = false })) return;
                std::array<xgpu::renderpass::attachment, 1> Attachments{ Entry.m_Texture };
                if (Device.Create(Entry.m_RenderPass, { .m_Attachments = Attachments })) return;
                Entry.m_bTextureReady = true;
            }

            if (Device.Create(Entry.m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(mesh_preview_vert), .m_EntryCount = (int)Verts.size() })) return;
            (void)Entry.m_VertexBuffer.MemoryMap(0, (int)Verts.size(), [&](void* pData) { std::memcpy(pData, Verts.data(), Verts.size() * sizeof(mesh_preview_vert)); });

            if ((int)Verts.size() > Entry.m_IndexCapacity)
            {
                if (Device.Create(Entry.m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = (int)Verts.size() })) return;
                (void)Entry.m_IndexBuffer.MemoryMap(0, (int)Verts.size(), [&](void* pData)
                {
                    auto* pIndex = static_cast<std::uint32_t*>(pData);
                    for (std::uint32_t i = 0; i < Verts.size(); ++i) pIndex[i] = i;
                });
                Entry.m_IndexCapacity = (int)Verts.size();
            }

            Entry.m_VertexCount = (int)Verts.size();
        }

        // Actually draws every ready pin's cube into its own offscreen texture - must run once per
        // frame, before any ImGui:: calls (same ordering E22_FramebufferTarget uses), since it opens
        // its own render pass(es) on the window's current frame.
        void RenderAll(xgpu::window& MainWindow)
        {
            for (auto& [PinId, Entry] : m_Entries)
            {
                if (!Entry.m_bTextureReady || Entry.m_VertexCount == 0) continue;
                auto CmdBuffer = MainWindow.StartRenderPass(Entry.m_RenderPass);
                CmdBuffer.setPipelineInstance(m_PipelineInstance);
                CmdBuffer.setBuffer(Entry.m_VertexBuffer);
                CmdBuffer.setBuffer(Entry.m_IndexBuffer);
                CmdBuffer.setPushConstants(mesh_preview_push_constants{ .m_L2C = m_View.getW2C() });
                CmdBuffer.Draw(Entry.m_VertexCount);
            }
        }

        // Shows the pin's rendered texture inline - a normal ImGui widget, so it moves/scales
        // correctly with its node under the canvas's own pan and zoom. Always draws a visible
        // bordered placeholder even before the graph has ever executed - the render canvas is part
        // of the node's shape from the moment it exists, not something that pops into existence
        // only once a mesh happens to flow through it.
        void DrawPreviewSquare(std::uint64_t PinId, float Scale)
        {
            const float Size = s_PreviewSize * Scale;
            const ImVec2 P0 = ImGui::GetCursorScreenPos();
            const ImVec2 P1{ P0.x + Size, P0.y + Size };
            auto It = m_Entries.find(PinId);
            if (It == m_Entries.end() || !It->second.m_bTextureReady || It->second.m_VertexCount == 0)
            {
                ImGui::GetWindowDrawList()->AddRectFilled(P0, P1, theme::CanvasDark, 0.0f);
                ImGui::GetWindowDrawList()->AddRect(P0, P1, theme::NodeBorder, 0.0f);
                ImGui::Dummy(ImVec2(Size, Size));
                return;
            }
            ImGui::Image((ImTextureRef)((void*)&It->second.m_Texture), ImVec2(Size, Size), ImVec2(0, 1), ImVec2(1, 0));
        }
    };

    //------------------------------------------------------------------------------------------------
    // Canvas geometry - ported from rslgraph-ui's own constants
    // (_ai_programming/ai_programming/rslgraph-ui/apps/rslgraph-ui/src/canvas/geometry.ts). Pixel
    // values differ from the original (that was tuned for its own 13px web font) but every formula
    // shape is the same: nodes measured from their own port-label text, stacked with a fixed gap,
    // wires routed via a highway line offset from the widest node plus a per-lane step.
    //------------------------------------------------------------------------------------------------
    namespace geo
    {
        constexpr float HEADER_H        = 26.0f;
        constexpr float ROW_H           = 20.0f;
        constexpr float VALUE_LINE_H    = 14.0f;
        constexpr float NODE_PAD_BOTTOM = 12.0f;
        constexpr float NODE_GAP        = 28.0f;
        constexpr float TOP             = 10.0f;
        constexpr float PORT_PAD        = 14.0f;
        // The "[Type]" label next to a pin reads as an annotation, not a name - smaller than the
        // row/title text and tucked in close to its own pin, rather than sharing the row-name's
        // full size and PORT_PAD's wider inset (which still governs how much column width a port
        // reserves overall - a smaller, closer label needs less of it, hence *_FONT_SCALE feeding
        // back into NodeWidth's own PortColW measurement too).
        constexpr float PIN_TYPE_FONT_SCALE = 0.72f;
        constexpr float PIN_TYPE_INSET      = 6.0f;
        // The node title is the one piece of text meant to read first at a glance - a bit bigger
        // than every other label on the node (row names, pin types, category). NodeWidth's own
        // TitleW measurement scales by the same factor so the box reserves exactly enough room for
        // the bigger rendered title, never less (see MinForHeader).
        constexpr float TITLE_FONT_SCALE = 1.3f;
        constexpr float GLYPH           = 9.0f;
        constexpr float ICON_CLEARANCE  = 16.0f;
        constexpr float LANE_GAP        = 14.0f;
        constexpr float PORT_HIT_RADIUS = 16.0f;
        constexpr float LINK_HIT_DIST   = 6.0f;
        constexpr float PREVIEW_GAP     = 10.0f;
        constexpr float SECTION_GAP     = 24.0f; // extra breathing room (line + "locals" caption) where a node's ports switch from external (caller-facing) to local-scope (body-facing) - see LocalSectionGapTotal
        constexpr float COLUMN_MARGIN     = 60.0f; // world-space gap between two adjacent columns' own highway extents
        constexpr float COLUMN_CLEAR_GAP  = 24.0f; // extra world-space distance, past a column's own extent, a spine-control drag must clear before a new-column drop target appears
        constexpr float SPINE_CIRCLE_R    = 7.0f;  // the two spine-control circles' own radius (screen-space, scales with zoom like everything else here)
        constexpr float SPINE_CIRCLE_GAP  = 4.0f;  // gap between the insert-marker box's edge and each circle, so they never overlap its own hit area
    }

    // Every port on a node in one flat, row-ordered list (inputs then outputs) - rslgraph-ui's own
    // NodeDef::ports is a single flat array regardless of direction; this is the equivalent view
    // over our ABI's separate input/output arrays.
    struct port_ref { bool m_bIsOutput; int m_Index; const xnode_os_port_desc* m_pDesc; };
    static std::vector<port_ref> FlatPorts(const xnode_os_node* pNode)
    {
        std::vector<port_ref> Out;
        const auto Inputs  = pNode->getInputs();
        const auto Outputs = pNode->getOutputs();
        for (int i = 0; i < (int)Inputs.size();  ++i) Out.push_back({ false, i, &Inputs[i] });
        for (int i = 0; i < (int)Outputs.size(); ++i) Out.push_back({ true,  i, &Outputs[i] });
        // Visual/anchor row order groups by scope-locality FIRST (every external/caller-facing pin,
        // then every local/body-facing one), direction second within each group - a stable partition,
        // so a mixed node like Function reads as one coherent "signature" block followed by one
        // "body view" block, needing exactly one divider (see the draw loop's SECTION_GAP handling)
        // instead of one per direction. The trailing ownership "End" pin (always last - see
        // for_each_loop_node.cpp's own comment on why) is excluded from the partition and
        // re-appended after: it's structural, not part of either interface, and must stay last
        // regardless of its own m_bLocalScope value (false, same as every external pin, which would
        // otherwise pull it up into the external group).
        const bool bHasTrailingEnd = !Out.empty() && Out.back().m_bIsOutput && std::strcmp(Out.back().m_pDesc->m_pTypeName, "Scope") == 0;
        port_ref EndPort{};
        if (bHasTrailingEnd) { EndPort = Out.back(); Out.pop_back(); }
        std::stable_partition(Out.begin(), Out.end(), [](const port_ref& P) { return !P.m_pDesc->m_bLocalScope; });
        if (bHasTrailingEnd) Out.push_back(EndPort);
        return Out;
    }
    static std::uint64_t PinOf(const port_ref& P, std::uint64_t NodeId) { return P.m_bIsOutput ? OutPinOf(NodeId, P.m_Index) : InPinOf(NodeId, P.m_Index); }
    static bool IsMeshType(const char* pType) noexcept { return std::strcmp(pType, "Mesh") == 0; }
    // IsScopeType/IsExecType moved to Editor/NodeOS_Types.h (header #1) - the interpreter
    // (Editor/NodeOS_Interpreter.h, header #4)'s IsRealDataPort needs both, and Types.h is the only
    // header guaranteed to already be included at that point.
    // An "Any" pin (Compare/Math Expression's A/B/Result, Print's Value) is a scalar wildcard - it
    // resolves DIRECTLY to whatever's wired to it. A "Span<Any>" pin (ForEachLoop's own Span input -
    // named for std::span, since that's what actually accepts a std::vector, std::array, C array, or
    // any other contiguous container of any element type T) is a CONTAINER wildcard - see
    // IsSpanWildcardType/ResolveContainerWildcardType below for how those differ: a container's
    // element type is what actually varies, not its own shape.
    static bool IsWildcardType(const char* pType) noexcept { return std::strcmp(pType, "Any") == 0; }
    static bool IsSpanWildcardType(const char* pType) noexcept { return std::strcmp(pType, "Span<Any>") == 0; }
    // Any container-shaped type name, resolved or not ("Span<Any>", "Span<Float>", ...) - never has
    // an inline literal or a preview line to show (there's no sensible "type a container" UI), same
    // no-value-line treatment RowHeight already gives Mesh/Scope/Bool below.
    static bool IsContainerType(const char* pType) noexcept { return std::string_view(pType).substr(0, 5) == "Span<"; }
    // If pType looks like "Span<X>", returns X; otherwise returns pType unchanged (nothing to
    // unwrap). Plain string surgery, not a real generic/template system - this corpus only ever
    // nests one level deep (NODE_SCRIPTING_DESIGN.md never called for more).
    static std::string_view UnwrapSpanElementType(std::string_view Type) noexcept
    {
        if (Type.size() > 6 && Type.substr(0, 5) == "Span<" && Type.back() == '>')
            return Type.substr(5, Type.size() - 6);
        return Type;
    }
    // What container type (e.g. "Span<Float>") is wired to NodeId's own "Span<Any>" input, if
    // anything - unwrapped or not, this is just "what's actually connected there" (used both to
    // display the Span pin's own effective type, and as the raw material ResolveNodeWildcardType
    // unwraps for Element/Index below). Returns nullptr while nothing is wired yet.
    static const char* ResolveContainerWildcardType(std::uint64_t NodeId, const xnode_os_node* pDesc, const std::vector<node_instance>& Nodes, const std::vector<link_instance>& Links) noexcept
    {
        int Index = 0;
        for (auto& P : pDesc->getInputs())
        {
            if (IsSpanWildcardType(P.m_pTypeName))
                for (auto& L : Links)
                    if (L.m_TargetNode == NodeId && L.m_TargetInput == Index)
                        for (auto& N : Nodes)
                            if (N.m_Id == L.m_SourceNode && N.m_pNode)
                            {
                                auto Outs = N.m_pNode->getOutputs();
                                if (L.m_SourceOutput < (int)Outs.size()) return Outs[L.m_SourceOutput].m_pTypeName;
                            }
            ++Index;
        }
        return nullptr;
    }
    // What has NodeId's own scalar "Any" wildcard(s) resolved to, if anything is wired yet - either
    // directly (a plain Any input wired straight to a source, Compare/Math Expression's A/B) or
    // indirectly, unwrapped from a "Span<Any>" input's own resolved container type (ForEachLoop's
    // Element/Index, once its Span input is wired) - purely derived from the CURRENT Links every
    // frame, never stored anywhere, same "never cache stale geometry" rule as everything else in
    // this file: disconnecting the wire that resolved it reverts it to unresolved for free, with no
    // explicit cleanup needed. Returns nullptr while nothing is wired yet (any type is still
    // acceptable in that case). A free function, not a DrawGraphCanvas-local lambda, specifically so
    // RowHeight/NodeHeight (also free functions - they run during layout, before/outside the
    // interactive draw loop) can call it too; DrawGraphCanvas's own call sites just pass their
    // already-in-scope Nodes/Links along.
    //
    // Forward-declared: EffectiveTypeName (below) calls this to resolve a plain "Any" pin, and this
    // now calls EffectiveTypeName right back for a two-hop chain (see the Outs[...] handling inside) -
    // a real mutual recursion, not a leftover.
    static const char* EffectiveTypeName(std::uint64_t NodeId, const xnode_os_node* pDesc, const char* pRawType, const std::vector<node_instance>& Nodes, const std::vector<link_instance>& Links) noexcept;
    static const char* ResolveNodeWildcardType(std::uint64_t NodeId, const xnode_os_node* pDesc, const std::vector<node_instance>& Nodes, const std::vector<link_instance>& Links) noexcept
    {
        int Index = 0;
        for (auto& P : pDesc->getInputs())
        {
            if (IsWildcardType(P.m_pTypeName))
                for (auto& L : Links)
                    if (L.m_TargetNode == NodeId && L.m_TargetInput == Index)
                        for (auto& N : Nodes)
                            if (N.m_Id == L.m_SourceNode && N.m_pNode)
                            {
                                auto Outs = N.m_pNode->getOutputs();
                                if (L.m_SourceOutput >= (int)Outs.size()) continue;
                                const char* pSourceType = Outs[L.m_SourceOutput].m_pTypeName;
                                // The source's own declared output type can ITSELF be an unresolved
                                // wildcard - wiring straight from a Compare/Math Expression's own
                                // "Any" Result rather than from a concrete-typed producer like
                                // Constant. Returning that raw, still-open "Any" text here is exactly
                                // the bug that left Print's Value pin showing "[Any]" and a raw
                                // pointer preview instead of "[Float]" and the real number once Print
                                // got wired two hops downstream of Constant instead of one - resolve
                                // it one level further instead of assuming the immediate source is
                                // already concrete.
                                if (IsWildcardType(pSourceType))
                                    return EffectiveTypeName(N.m_Id, N.m_pNode, pSourceType, Nodes, Links);
                                return pSourceType;
                            }
            ++Index;
        }
        if (const char* pContainerType = ResolveContainerWildcardType(NodeId, pDesc, Nodes, Links))
        {
            static thread_local std::string s_Elem;
            s_Elem.assign(UnwrapSpanElementType(pContainerType));
            return s_Elem.c_str();
        }
        return nullptr;
    }
    // The effective type name for display/matching purposes: a concrete pin's own declared type
    // unchanged; an Any pin's resolved type once something has locked it in (or the raw "Any" text
    // itself while still fully open); a Span<Any> pin's own resolved container type, NOT unwrapped
    // (or the raw "Span<Any>" text while still open) - Span itself displays/matches as the whole
    // container, only Element/Index (plain Any pins) see the unwrapped element type. Never nullptr -
    // always safe to print/compare directly.
    static const char* EffectiveTypeName(std::uint64_t NodeId, const xnode_os_node* pDesc, const char* pRawType, const std::vector<node_instance>& Nodes, const std::vector<link_instance>& Links) noexcept
    {
        if (IsSpanWildcardType(pRawType))
        {
            if (const char* pResolved = ResolveContainerWildcardType(NodeId, pDesc, Nodes, Links)) return pResolved;
            return pRawType;
        }
        if (!IsWildcardType(pRawType)) return pRawType;
        if (const char* pResolved = ResolveNodeWildcardType(NodeId, pDesc, Nodes, Links)) return pResolved;
        return pRawType;
    }

    // literal_slot/literal_storage now live in Editor/NodeOS_Types.h (see its own comment) -
    // moved there since every downstream section already depends on that header anyway.

    // Generic, node-type-agnostic: does this node reflect a property with the SAME NAME as one of
    // its own pins (e.g. Compare/Math Expression's "A"/"B", same idea as Constant's Value* fields)?
    // If so, THAT property is the pin's real, typed, undoable, saved literal. A node opts into this
    // just by declaring a same-named property; the host never needs to know which node types did -
    // every node type with a literal-editable pin now does (see SetVariable's own "Value"), so this
    // is the ONLY mechanism, not a fallback path.
    static const xproperty::type::members* FindMemberByName(const xproperty::type::object* pObj, const char* pName) noexcept
    {
        for (auto& M : pObj->m_Members)
            if (std::strcmp(M.m_pName, pName) == 0) return &M;
        return nullptr;
    }
    // Shared tail for GetInputValue/PullInputValue: what a pin resolves to when NO wire targets it
    // at all - a same-named reflected property (see FindMemberByName), read directly and already
    // correctly typed, or nullptr if the node doesn't declare one for this pin.
    static void* ResolveUnconnectedLiteral(std::uint64_t NodeId, int InputIndex, const std::vector<node_instance>& Nodes, const std::vector<link_instance>& Links, literal_storage& Scratch)
    {
        auto NodeIt = std::find_if(Nodes.begin(), Nodes.end(), [&](auto& N) { return N.m_Id == NodeId; });
        if (NodeIt == Nodes.end() || !NodeIt->m_pNode) return nullptr;
        const auto NodeInputs = NodeIt->m_pNode->getInputs();
        if (InputIndex >= (int)NodeInputs.size()) return nullptr;

        auto* pMember = FindMemberByName(NodeIt->m_pNode->getProperties(), NodeInputs[InputIndex].m_pName);
        if (!pMember) return nullptr;

        xproperty::any Out; xproperty::settings::context Ctx;
        if (!pMember->TryRead(NodeIt->m_pNode, Out, Ctx)) return nullptr;

        Scratch.emplace_back();
        void* pSlot = &Scratch.back();
        if (Out.is<float>())        { *static_cast<float*>(pSlot)        = Out.get<float>();        return pSlot; }
        if (Out.is<bool>())         { *static_cast<bool*>(pSlot)         = Out.get<bool>();         return pSlot; }
        if (Out.is<std::int32_t>()) { *static_cast<std::int32_t*>(pSlot) = Out.get<std::int32_t>(); return pSlot; }
        if (Out.is<std::int16_t>()) { *static_cast<std::int16_t*>(pSlot) = Out.get<std::int16_t>(); return pSlot; }
        return nullptr;
    }
    // Whatever cached output feeds a given node's input pin right now - nullptr if unconnected (and
    // no literal is typed in), or if a wire IS there but its source simply hasn't run (yet, or ever -
    // this never PULLS a source into running; see PullInputValue below, used by real execution, for
    // that). Read-only, side-effect-free - this is what the canvas's own live pin preview and the
    // mesh-preview pass use, since triggering real Execute() calls (with their real side effects,
    // e.g. Print writing to the console) merely because a frame got drawn would be a much bigger
    // surprise than a preview showing "nothing yet."
    static void* GetInputValue(std::uint64_t NodeId, int InputIndex, const std::vector<node_instance>& Nodes, const std::vector<link_instance>& Links, literal_storage& Scratch)
    {
        for (auto& Link : Links)
        {
            if (Link.m_TargetNode != NodeId || Link.m_TargetInput != InputIndex) continue;
            auto SourceIt = std::find_if(Nodes.begin(), Nodes.end(), [&](auto& N) { return N.m_Id == Link.m_SourceNode; });
            if (SourceIt == Nodes.end() || !SourceIt->m_bHasRun) return nullptr;
            return (Link.m_SourceOutput < (int)SourceIt->m_CachedOutputs.size()) ? SourceIt->m_CachedOutputs[Link.m_SourceOutput] : nullptr;
        }
        return ResolveUnconnectedLiteral(NodeId, InputIndex, Nodes, Links, Scratch);
    }

    // For connection-matching only: is this (already-effective) type name STILL an open wildcard of
    // either kind - a bare "Any" that's never been wired at all, or a "Span<Any>" whose own Span
    // input hasn't been wired yet? Either one accepts any type on the other end of a new connection.
    // A RESOLVED wildcard (EffectiveTypeName already returned the real type, e.g. "Float" or
    // "Span<Float>") is intentionally NOT considered open here - it must match exactly like any
    // ordinary concrete pin from that point on.
    static bool IsAnyKindOfWildcard(const char* pType) noexcept { return IsWildcardType(pType) || IsSpanWildcardType(pType); }
    // ReadBoolProperty (used by DisplayTypeText, right below) is defined in
    // Editor/NodeOS_PropertySerialize.h, included ahead of this point - no forward declaration needed.
    // ForEachLoop's own Element output additionally shows const/& based on its own "ReadOnlyElement"
    // checkbox (defaults to read-only, i.e. "const T&") - the node should visibly reflect that choice
    // rather than have it be an invisible side-panel-only setting. A name-based special case, the
    // display-only counterpart to Compare's operator-filtering/Constant's inline Value widget above.
    // Every other port's display text is just its own effective type name, unchanged.
    static std::string DisplayTypeText(const xnode_os_node* pDesc, const port_ref& P, const char* pEffType)
    {
        if (P.m_bIsOutput && pDesc->m_pFactory->getName() == "ForEachLoop" && std::strcmp(P.m_pDesc->m_pName, "Element") == 0)
        {
            const bool bReadOnly = ReadBoolProperty(pDesc, "ReadOnlyElement", true);
            // "RO Float", not "const Float&" - this label is user-facing, not a C++ declaration; the
            // read-only/mutable distinction still matters here (it's the whole point of the checkbox
            // this reflects), but nothing about reference syntax should leak into the canvas.
            return (bReadOnly ? std::string("RO ") : std::string()) + pEffType;
        }
        // A Function's local-mirrored outputs (the function body's own view of its parameters) carry
        // Required/ReadOnly directly on the port descriptor itself (see function_node.cpp), unlike
        // ForEachLoop's bespoke per-instance property above - same const/& treatment, but deliberately
        // NOT keyed off m_bLocalScope alone: ForEachLoop's Index is also flagged m_bLocalScope (for
        // the scope-containment check) but is a plain value, never a reference - showing it as
        // "const Int&" would misdescribe the eventual codegen shape. Scoped to Function specifically,
        // where a mirrored output genuinely is a reference into the caller's own argument.
        if (P.m_bIsOutput && P.m_pDesc->m_bLocalScope && pDesc->m_pFactory->getName() == "Function")
            // "RO Float", not "const Float&" - same reasoning as ForEachLoop's Element just above:
            // this is a user-facing canvas label, not a C++ declaration.
            return (P.m_pDesc->m_bReadOnly ? std::string("RO ") : std::string()) + pEffType;
        // "?" for Optional - every pre-existing static port_desc defaults m_bRequired to true (see
        // xnode_os_plugin_api.h), so this is a no-op everywhere except a Function's user-configured
        // pins, the only place m_bRequired can actually be false today.
        return P.m_pDesc->m_bRequired ? std::string(pEffType) : std::string(pEffType) + "?";
    }
    // Per-TYPE color, not per-pin - every Float pin/wire in the graph reads the same color at a
    // glance, same convention Unity's own node-based editors (Shader Graph, Visual Scripting) use:
    // a value's TYPE is what the color encodes, not which node or which side of a wire it's on.
    // Approximate hand-picked values, not a pixel-exact Unity palette - the numeric family (Bool/
    // Float/Int/Short) shares one recognizable hue family the way Unity's own "number" types do,
    // while staying distinguishable from each other; refine on request if exact parity matters.
    static ImU32 TypeColor(const char* pType) noexcept
    {
        if (IsMeshType(pType))                    return IM_COL32(167, 139, 250, 255); // purple
        if (std::strcmp(pType, "Text") == 0)      return IM_COL32(74, 222, 128, 255);  // green
        // A Scope pin (owner<->End ownership, NODE_SCRIPTING_DESIGN.md section 4.1) matches the box's
        // own BACKGROUND fill, not the generic default and not the border either - it's part of the
        // box's own structure, not a value, and both the pin-glyph code and the link-drawing loop
        // already darken this per scope depth (DarkenForDepth) to keep matching the actual box at
        // whatever nesting level it sits at - this is the one shared base color both read from.
        if (IsScopeType(pType))                   return theme::NodeBg;
        // Exec pins get their own distinct color (a plain white, matching the long-established
        // convention for control-flow pins elsewhere) so a glance at the glyph tells data from
        // control flow apart, same as Scope already does for ownership.
        if (IsExecType(pType))                    return IM_COL32(241, 245, 249, 255);
        // NOT red - this editor already uses red for its own "scope-invalid link" warning
        // (bScopeInvalid in the link-drawing loop), and a near-identical red for Bool would make a
        // perfectly valid Bool wire indistinguishable from a broken one at a glance. A deeper,
        // more saturated green than "Text"'s own minty green just above - close enough in hue to
        // read as "green" on request, far enough in value/saturation not to recreate that same
        // collision one type over.
        if (std::strcmp(pType, "Bool") == 0)      return IM_COL32(22, 163, 74, 255);   // green (deep, distinct from Text's mint green)
        if (std::strcmp(pType, "Float") == 0)     return IM_COL32(101, 210, 235, 255); // cyan
        if (std::strcmp(pType, "Int") == 0)       return IM_COL32(66, 153, 225, 255);  // blue
        if (std::strcmp(pType, "Short") == 0)     return IM_COL32(56, 178, 165, 255);  // teal
        // A container type (Span<Any>/Span<Float>/...) is a distinct SHAPE, not a value of the type
        // it holds - its own amber/orange marks it apart from a plain scalar pin of the same element
        // type, same spirit as Scope getting its own color rather than inheriting from whatever it
        // wraps. IsContainerType matches on the "Span<" prefix regardless of what's inside.
        if (IsContainerType(pType))               return IM_COL32(245, 158, 11, 255);  // amber
        // "Any" (never wired, still fully open) and anything else unrecognized share this neutral
        // gray - an unresolved wildcard genuinely has no type yet to color by.
        return IM_COL32(148, 163, 184, 255);
    }
    // A node's header strip tints by its own factory category (Flow Control/Math/Logic/...), same
    // spirit as Unity's own category-colored node headers. The title text drawn on top is always
    // near-white (see the title draw call) - every color here is kept dark and roughly matched in
    // luminance to the plain theme::NodeHeader gray it replaces, so white text stays legible and no
    // one category's box reads as jarringly brighter than its neighbors on the same canvas. New
    // categories not listed here just fall back to the old neutral header gray - nothing breaks if
    // a future plugin introduces one.
    static ImU32 CategoryColor(std::string_view Category) noexcept
    {
        if (Category == "Flow Control") return IM_COL32(52, 71, 94, 255);  // slate blue
        if (Category == "Logic")        return IM_COL32(38, 82, 74, 255);  // teal green
        if (Category == "Math")         return IM_COL32(92, 68, 32, 255);  // amber brown
        if (Category == "Debug")        return IM_COL32(94, 46, 46, 255);  // muted red
        if (Category == "Variables")    return IM_COL32(70, 50, 92, 255);  // purple
        if (Category == "Output")       return IM_COL32(30, 76, 86, 255);  // cyan teal
        if (Category == "Geometry")     return IM_COL32(66, 68, 32, 255);  // olive
        return theme::NodeHeader;
    }
    // A Bool pin never has anything to preview - it dropped the inline-constant checkbox (a
    // hardcoded true/false doesn't fit how Condition/And/Or/Not are meant to be used - they're wired
    // from Compare, not typed directly). No point reserving a value line it will never use. An Exec
    // pin is the same - pure control flow, never a value. (The original flat-spine design removed
    // Blueprint-style exec pins entirely, since a plain node's "next" is just whatever follows it in
    // the same spine - still true today. Exec pins came back narrowly for OnEvent/Execute/Call/
    // Function's own new input, to let a SPINE be triggered by an event or invoked from elsewhere;
    // ordinary nodes still have none. There's also deliberately no separate "Int"/"Short" scalar pin
    // type: minimizing node/type proliferation matters more than nominal precision here, so every
    // scalar numeric value in this corpus - including ForEachLoop's own Element/index - is just
    // "Float", the same principle already applied to Compare (one enum-driven node instead of a
    // GreaterThan/LessThan/Equals box each).)
    static bool IsNoPreviewType(const char* pType) noexcept { return std::strcmp(pType, "Bool") == 0 || IsExecType(pType); }
    // A Mesh-typed port's live render lives in one shared preview block at the TOP of the node (right
    // under the header), not inline per-row - so its row never prints a value-preview line below the
    // glyph and doesn't need the VALUE_LINE_H space reserved for one (leaving it in produced a visible
    // gap between a Mesh row and whatever row follows it). Scope/Bool pins get the same
    // treatment for the same underlying reason - none of them ever have a value to preview.
    // An output pin never shows anything either, right now - nothing in this corpus actually
    // executes yet, so pValue is always null and the row would sit empty regardless of type. Revisit
    // once real execution is wired up and an output can genuinely have a live value to preview.
    static bool IsPinConnected(std::uint64_t NodeId, int PinIndex, bool bIsOutput, const std::vector<link_instance>& Links) noexcept
    {
        for (auto& L : Links)
            if (bIsOutput ? (L.m_SourceNode == NodeId && L.m_SourceOutput == PinIndex)
                          : (L.m_TargetNode == NodeId && L.m_TargetInput  == PinIndex))
                return true;
        return false;
    }
    static float RowHeight(const port_ref& P, std::uint64_t NodeId, const std::vector<link_instance>& Links, const char* pEffType) noexcept
    {
        if (IsMeshType(pEffType) || IsScopeType(pEffType) || IsNoPreviewType(pEffType) || IsContainerType(pEffType) || P.m_bIsOutput)
            return geo::ROW_H;
        // A still-unresolved Any pin (Compare/Math Expression's A/B before either one is wired - see
        // ResolveNodeWildcardType) has nothing to preview or enter yet either - same no-line
        // treatment as Mesh/Scope/Bool above, until it resolves to something.
        if (IsWildcardType(pEffType)) return geo::ROW_H;
        // A numeric input's extra line holds either its own inline literal-constant widget (while
        // unconnected) or, once a wire connects it AND the graph has actually run (RunProgram -
        // NODE_SCRIPTING_DESIGN.md §12.6), the real resolved value via PortTypeToPreview - either
        // way something wants that line, so it's reserved unconditionally rather than trying to
        // predict "will there be a value to show this frame," which would need Nodes threaded all
        // the way through here just to ask GetInputValue. The only cost is a occasionally-blank line
        // under a connected-but-not-yet-run pin - far better than the line disappearing and letting
        // the row below it collide with real preview text, which is what happened before RunProgram
        // ever produced a real value to show here (this path used to be dead: nothing ever executed,
        // so a connected numeric pin never actually needed the space it was skipping). Checked
        // against the EFFECTIVE type so an Any pin resolved to one of these gets the exact same
        // treatment an ordinarily-typed pin of that type would.
        return geo::ROW_H + geo::VALUE_LINE_H;
    }
    static int MeshPortCount(const xnode_os_node* pNode)
    {
        int Count = 0;
        for (auto& P : FlatPorts(pNode)) if (IsMeshType(P.m_pDesc->m_pTypeName)) ++Count;
        return Count;
    }
    static float PreviewAreaHeight(const xnode_os_node* pNode)
    {
        const int Count = MeshPortCount(pNode);
        return Count > 0 ? Count * (mesh_preview_system::s_PreviewSize + geo::PREVIEW_GAP) + geo::PREVIEW_GAP : 0.0f;
    }
    // An End marker (NODE_SCRIPTING_DESIGN.md section 4.1/4.2) isn't really a node in its own right -
    // more an extension of whichever If/ForEachLoop owns it - so it renders as a title-only pill:
    // no body, no port rows, no visible pin glyph at all (the wire just anchors to the title bar's
    // own edge). Its actual displayed title is computed contextually elsewhere (the node-header draw
    // loop) since that needs the ownership graph this function doesn't have access to; a fixed,
    // generously-wide box here is a reasonable trade against threading that context through here too.
    static bool IsEndMarkerType(const xnode_os_node* pNode) noexcept { return pNode->m_pFactory->getName() == "End"; }
    static float NodeWidth(const xnode_os_node* pNode, std::uint64_t NodeId, const std::vector<node_instance>& Nodes, const std::vector<link_instance>& Links)
    {
        // Scaled by TITLE_FONT_SCALE too - an End marker's title renders at the same bigger size as
        // every other node's, so its fixed width needs the same proportional headroom.
        if (IsEndMarkerType(pNode)) return 190.0f * geo::TITLE_FONT_SCALE;
        const auto NodeName = pNode->m_pFactory->getName();
        const float TitleW = ImGui::CalcTextSize(NodeName.data(), NodeName.data() + NodeName.size()).x;
        float NameW = TitleW;
        float PortColW = 40.0f;
        for (auto& P : FlatPorts(pNode))
        {
            NameW = std::max(NameW, ImGui::CalcTextSize(P.m_pDesc->m_pName).x);
            // Sized off the EFFECTIVE (and, for ForEachLoop's Element, const/&-decorated - see
            // DisplayTypeText) type, so a resolved Any pin gets room for its real type name, not just
            // the shorter placeholder "Any" text.
            const char* pEffType = EffectiveTypeName(NodeId, pNode, P.m_pDesc->m_pTypeName, Nodes, Links);
            const std::string TypeLabel = std::string("[") + DisplayTypeText(pNode, P, pEffType) + "]";
            PortColW = std::max(PortColW, ImGui::CalcTextSize(TypeLabel.c_str()).x * geo::PIN_TYPE_FONT_SCALE + geo::PIN_TYPE_INSET);
        }
        const float MinForPreview = MeshPortCount(pNode) > 0 ? mesh_preview_system::s_PreviewSize + 24.0f : 0.0f;
        // The header row carries BOTH the title (left-aligned) and the category label (right-
        // aligned, e.g. "Flow Control") - neither the port-column formula above nor MinForPreview
        // ever reserved room for the two of them to coexist, which is exactly the bug behind
        // "Function"/"If"'s title colliding with their own "Flow Control" category text. TitleW
        // (NOT the port-loop-mutated NameW) is the true title width; End markers never reach this
        // point at all (early return above), so DisplayName == NodeName always holds here - no
        // string-mismatch risk between what's measured and what's drawn.
        const auto NodeCategory = pNode->m_pFactory->getCategory();
        const float CategoryW = ImGui::CalcTextSize(NodeCategory.data(), NodeCategory.data() + NodeCategory.size()).x;
        // TitleW scaled by TITLE_FONT_SCALE to match the actual bigger rendered title (see the
        // title draw call) - reserving room at the OLD, smaller size here would silently reopen the
        // title/category overlap bug the bigger title was supposed to have no part in.
        const float MinForHeader = 10.0f + TitleW * geo::TITLE_FONT_SCALE + 16.0f + CategoryW + 10.0f;
        // The inline-enum-widget block (draw loop) sets the combo's own width to pRow->m_W - 20.0f
        // and shows the CURRENTLY SELECTED item's full display name inside it - never previously
        // measured here, so a node with a long enum choice (Compare's "A Greater Or Equal To B")
        // could end up in a box too narrow to read its own dropdown without opening it. +50.0f covers
        // the combo's own internal frame padding/dropdown arrow, on top of the same -20.0f the widget
        // itself already reserves.
        float MinForEnum = 0.0f;
        if (const xproperty::type::object* pObj = pNode->getProperties())
            for (auto& M : pObj->m_Members)
                if (auto* pVar = std::get_if<xproperty::type::members::var>(&M.m_Variant); pVar && pVar->m_AtomicType.m_IsEnum)
                    for (auto& Item : pVar->m_AtomicType.m_RegisteredEnumSpan)
                        MinForEnum = std::max(MinForEnum, ImGui::CalcTextSize(Item.m_pName).x + 50.0f);
        const float Result = std::max({ NameW + 2.0f * PortColW + 40.0f, MinForPreview, MinForHeader, MinForEnum });
        return Result;
    }
    // Extra vertical space at the ONE point a node's port list switches from external (caller-
    // facing) to local-scope (body-facing) - FlatPorts already groups every external pin first, then
    // every local one (direction-agnostic), so there is exactly one such boundary total, not one per
    // direction. "End" is also flagged !m_bLocalScope (it's a structural ownership marker, not part
    // of either interface) but FlatPorts keeps it trailing after the local group, so checking only
    // the false->true direction (never true->false) never mistakes local-into-End for a second
    // boundary. Shared between NodeHeight (so the box is sized to fit it) and the draw loop (so RowY
    // actually leaves the gap) - see function_node.cpp for the only node type with a mixed port list
    // today.
    static float LocalSectionGapTotal(const xnode_os_node* pNode)
    {
        float Total = 0.0f;
        bool bHavePrev = false, bPrevLocal = false;
        for (auto& P : FlatPorts(pNode))
        {
            if (bHavePrev && P.m_pDesc->m_bLocalScope && !bPrevLocal)
                Total += geo::SECTION_GAP;
            bPrevLocal = P.m_pDesc->m_bLocalScope;
            bHavePrev = true;
        }
        return Total;
    }
    static float NodeHeight(const xnode_os_node* pNode, std::uint64_t NodeId, const std::vector<node_instance>& Nodes, const std::vector<link_instance>& Links)
    {
        if (IsEndMarkerType(pNode)) return geo::HEADER_H;
        float H = geo::HEADER_H + PreviewAreaHeight(pNode) + LocalSectionGapTotal(pNode);
        for (auto& P : FlatPorts(pNode)) H += RowHeight(P, NodeId, Links, EffectiveTypeName(NodeId, pNode, P.m_pDesc->m_pTypeName, Nodes, Links));
        // Reserve room for each enum property's own inline dropdown, drawn directly in the node body
        // (not just the side properties panel) - see the inline-enum-widget block in the draw loop.
        if (const xproperty::type::object* pObj = pNode->getProperties())
            for (auto& M : pObj->m_Members)
                if (auto* pVar = std::get_if<xproperty::type::members::var>(&M.m_Variant); pVar && pVar->m_AtomicType.m_IsEnum)
                    H += geo::ROW_H + 4.0f;
        // Constant's own numeric Value gets a second inline row too, same as its Type dropdown just
        // above - see the Constant-specific block right after the inline-enum-widget loop.
        if (pNode->m_pFactory->getName() == "Constant")
            H += geo::ROW_H + 4.0f;
        return H + geo::NODE_PAD_BOTTOM;
    }
    // How many scopes currently enclose each node, in place of visual indentation (which would fight
    // the spine layout's own X positions) - walk each spine in Order, push a node's own
    // m_OwnedEndId when it owns one, pop on reaching it. An End-Else (which owns a further End)
    // pops its own pairing and immediately pushes the next one, so the false-branch content lands
    // back at the same depth the true branch was at - traced by hand for both plain If and If/Else
    // before trusting this.
    static std::unordered_map<std::uint64_t, int> ComputeScopeDepths(const std::vector<node_instance>& Nodes)
    {
        std::unordered_map<std::uint64_t, int> Depth;
        std::map<std::uint64_t, std::vector<const node_instance*>> BySpine;
        for (auto& N : Nodes) BySpine[N.m_SpineId].push_back(&N);
        for (auto& [SpineId, SpineNodes] : BySpine)
        {
            std::vector<const node_instance*> Sorted = SpineNodes;
            std::sort(Sorted.begin(), Sorted.end(), [](const node_instance* A, const node_instance* B) { return A->m_Order < B->m_Order; });
            std::vector<std::uint64_t> Stack;
            for (auto* N : Sorted)
            {
                if (!Stack.empty() && Stack.back() == N->m_Id) Stack.pop_back();
                Depth[N->m_Id] = (int)Stack.size();
                if (N->m_OwnedEndId != 0) Stack.push_back(N->m_OwnedEndId);
            }
        }
        return Depth;
    }
    // The ordered chain of enclosing owner ids (If/ForEachLoop) for each node, outermost first - e.g.
    // {ForEachLoopId, IfId} for a node nested inside an If nested inside a ForEachLoop. Exact same
    // per-spine stack walk as ComputeScopeDepths just above, only keeping the stack's actual contents
    // instead of collapsing it to a count - used by IsDataLinkScopeValid below to check whether a
    // data link's source is something the target could actually reference in real nested C++.
    static std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> ComputeEnclosingChains(const std::vector<node_instance>& Nodes)
    {
        std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> Chains;
        std::map<std::uint64_t, std::vector<const node_instance*>> BySpine;
        for (auto& N : Nodes) BySpine[N.m_SpineId].push_back(&N);
        for (auto& [SpineId, SpineNodes] : BySpine)
        {
            std::vector<const node_instance*> Sorted = SpineNodes;
            std::sort(Sorted.begin(), Sorted.end(), [](const node_instance* A, const node_instance* B) { return A->m_Order < B->m_Order; });
            std::vector<std::uint64_t> Stack;
            for (auto* N : Sorted)
            {
                if (!Stack.empty() && Stack.back() == N->m_Id) Stack.pop_back();
                Chains[N->m_Id] = Stack;
                if (N->m_OwnedEndId != 0) Stack.push_back(N->m_OwnedEndId);
            }
        }
        return Chains;
    }
    // Every node id from an owner (If/ForEachLoop) through its own End marker, inclusive, by walking
    // its spine in Order - used to highlight "what does this scope actually contain" when its
    // ownership link is selected. A visual highlight only, deliberately not an actual selection (see
    // the call site) - inspecting a scope shouldn't make its boxes eligible for Delete/drag. Also
    // used by IsDataLinkScopeValid below for any m_bLocalScope-flagged pin's containment check.
    // Walks m_OwnedEndId repeatedly (Owner -> its marker -> that marker's OWN marker -> ...) until
    // reaching a node that doesn't own anything further - handles an owner with a multi-hop marker
    // chain (create_owned_pair_cmd's optional 2nd hop) exactly like a plain single-hop owner (If/
    // ForEachLoop/Function all resolve to their direct marker today, nothing currently uses 2 hops).
    static std::uint64_t ResolveTerminalMarker(const std::vector<node_instance>& Nodes, std::uint64_t OwnerId)
    {
        std::uint64_t Cur = OwnerId;
        for (int Guard = 0; Guard < (int)Nodes.size() + 1; ++Guard)
        {
            const node_instance* pCur = nullptr;
            for (auto& N : Nodes) if (N.m_Id == Cur) { pCur = &N; break; }
            if (!pCur || pCur->m_OwnedEndId == 0) return Cur;
            Cur = pCur->m_OwnedEndId;
        }
        return Cur; // malformed cycle - shouldn't happen, bail out rather than loop forever
    }
    static std::vector<std::uint64_t> ComputeScopeSpan(const std::vector<node_instance>& Nodes, std::uint64_t OwnerId)
    {
        const node_instance* pOwner = nullptr;
        for (auto& N : Nodes) if (N.m_Id == OwnerId) { pOwner = &N; break; }
        if (!pOwner || pOwner->m_OwnedEndId == 0) return {};
        const std::uint64_t TerminalId = ResolveTerminalMarker(Nodes, OwnerId);

        std::vector<const node_instance*> Sorted;
        for (auto& N : Nodes) if (N.m_SpineId == pOwner->m_SpineId) Sorted.push_back(&N);
        std::sort(Sorted.begin(), Sorted.end(), [](const node_instance* A, const node_instance* B) { return A->m_Order < B->m_Order; });

        std::vector<std::uint64_t> Span;
        bool bInside = false;
        for (auto* N : Sorted)
        {
            if (N->m_Id == OwnerId) bInside = true;
            if (bInside) Span.push_back(N->m_Id);
            if (bInside && N->m_Id == TerminalId) break;
        }
        return Span;
    }
    // Whether a data link's SOURCE is something the TARGET could actually reference once this
    // compiles to real nested C++ (NODE_SCRIPTING_DESIGN.md section 4.4/11.6): the boundary is SCOPE
    // depth, not spine identity. A source sitting at a spine's own TOP level (an empty enclosing
    // chain - never nested inside any If/ForEachLoop body) is "world scope": conceptually shared/
    // global state any node anywhere can read, the same role a Blueprint Variable plays for cross-
    // Event-Graph communication in Unreal (you can't wire one Event Graph's local pin into a
    // different Event Graph at all, but both can read/write a shared class member). A source nested
    // inside a local scope, by contrast, is trapped in whatever function/block its OWN spine compiles
    // to - readable only from the same or a more deeply nested scope in that SAME spine, never from a
    // different spine at all, and never from a sibling or already-exited scope even in the same
    // spine (the exact §4.4 gap: a value from inside one branch isn't visible after it, or inside an
    // unrelated one).
    //
    // Blueprint/Unity Visual Scripting don't block drawing an invalid wire either - both let you draw
    // it, then refuse to compile it (Blueprint's own diagnostic: "X is not in scope due to a network
    // of execution and data flow errors"). There's no compiler wired up to this editor yet, so this
    // is surfaced as an immediate visual flag (see the link-drawing loop below) instead of a deferred
    // compile error - strictly more helpful than staying silent about it until a compiler exists.
    //
    // Left deliberately unaddressed here (a compiler-design question, not an editor-validation one):
    // a same-spine read is always guaranteed fresh (flat sequential/nested execution order makes
    // "already computed by the time this reads it" automatic), but a cross-spine world-scope read
    // has no such guarantee - it's a persisted "last value written" read, with real questions about
    // which spine runs first/how often. Worth resolving before compilation is actually wired up.
    static bool IsDataLinkScopeValid(std::uint64_t SourceNode, int SourceOutputIndex, std::uint64_t TargetNode, int TargetInputIndex, const std::vector<node_instance>& Nodes, const std::unordered_map<std::uint64_t, std::vector<std::uint64_t>>& Chains)
    {
        const node_instance* pSrc = nullptr; const node_instance* pTgt = nullptr;
        for (auto& N : Nodes) { if (N.m_Id == SourceNode) pSrc = &N; if (N.m_Id == TargetNode) pTgt = &N; }
        if (!pSrc || !pTgt) return true; // dangling reference - not this check's concern

        // A pin flagged m_bLocalScope (a Function's mirrored parameter/return pins; ForEachLoop's
        // Element/Index) only has meaning strictly INSIDE the scope its OWN node opens - regardless
        // of whether the flagged pin is this link's source (the body READS it) or target (the body
        // WRITES it), the OTHER endpoint must be physically within the flagged pin's owning node's
        // own scope span. Same containment question either direction, so one rule covers both -
        // replaces the old node-position-based "mid-chain" test now that the flag is per-pin.
        if (pSrc->m_pNode)
        {
            const auto Outs = pSrc->m_pNode->getOutputs();
            if (SourceOutputIndex >= 0 && SourceOutputIndex < (int)Outs.size() && Outs[SourceOutputIndex].m_bLocalScope)
            {
                const auto Span = ComputeScopeSpan(Nodes, SourceNode);
                return std::find(Span.begin(), Span.end(), TargetNode) != Span.end();
            }
        }
        if (pTgt->m_pNode)
        {
            const auto Ins = pTgt->m_pNode->getInputs();
            if (TargetInputIndex >= 0 && TargetInputIndex < (int)Ins.size() && Ins[TargetInputIndex].m_bLocalScope)
            {
                const auto Span = ComputeScopeSpan(Nodes, TargetNode);
                return std::find(Span.begin(), Span.end(), SourceNode) != Span.end();
            }
        }

        // Reaching here means the source port is NOT itself local-scope-flagged (that returned
        // above) - but if the source node owns a scope at all, its own EXTERNAL output is still that
        // scope's finished result, never available until the ENTIRE body has run. The ordinary
        // chain-prefix check below would otherwise treat it like any other enclosing-scope value
        // available from entry (e.g. a ForEachLoop's Span, an If's Condition) - correct for those,
        // wrong for a return value. So nothing PHYSICALLY INSIDE the owner's own span may read it,
        // regardless of nesting depth elsewhere; anything at or above the owner's own scope still can
        // (checked generically off m_OwnedEndId - only Function has an external output shaped this
        // way today, but this isn't specific to it by name).
        if (pSrc->m_OwnedEndId != 0)
        {
            const auto OwnSpan = ComputeScopeSpan(Nodes, SourceNode);
            if (std::find(OwnSpan.begin(), OwnSpan.end(), TargetNode) != OwnSpan.end()) return false;
        }

        auto SrcIt = Chains.find(SourceNode); auto TgtIt = Chains.find(TargetNode);
        if (SrcIt == Chains.end() || TgtIt == Chains.end()) return true;
        const auto& SrcChain = SrcIt->second; const auto& TgtChain = TgtIt->second;
        if (pSrc->m_SpineId != pTgt->m_SpineId) return SrcChain.empty() || TgtChain.empty(); // local-to-local across spines is invalid; either end being world scope is fine
        if (SrcChain.size() > TgtChain.size()) return false;
        for (std::size_t i = 0; i < SrcChain.size(); ++i) if (SrcChain[i] != TgtChain[i]) return false;
        // Chain compatibility alone isn't enough - two nodes at the SAME nesting depth (including
        // both unnested, "world scope") have IDENTICAL chains regardless of which one was actually
        // placed first, so the check above can't tell a forward link from a backward one. The spine's
        // own Order IS execution order in this flat model - a source that comes AFTER its target
        // would be read before it's ever computed. Cross-spine order isn't comparable this way (each
        // spine numbers its own Order independently), which is why this sits after the cross-spine
        // branch above rather than folded into it.
        if (pSrc->m_Order > pTgt->m_Order) return false;
        return true;
    }
    // Diminishing-returns darken, never marching to pure black - the fill is already very dark
    // (17,24,39) against a near-black canvas, so an unbounded per-level darken would make deeply
    // nested boxes blend into the background, defeating the point.
    static ImU32 DarkenForDepth(ImU32 BaseCol, int Depth) noexcept
    {
        if (Depth <= 0) return BaseCol;
        const float Factor = 0.45f + 0.55f / (1.0f + Depth * 0.6f);
        const int R = (int)(((BaseCol >> IM_COL32_R_SHIFT) & 0xFF) * Factor);
        const int G = (int)(((BaseCol >> IM_COL32_G_SHIFT) & 0xFF) * Factor);
        const int B = (int)(((BaseCol >> IM_COL32_B_SHIFT) & 0xFF) * Factor);
        return IM_COL32(R, G, B, 255);
    }
    // Scales just the alpha channel, leaving hue/brightness untouched - used to dim a single
    // ineligible PIN during a drag without touching the whole node's own DarkenForDepth-based fill
    // (a node can carry both eligible and ineligible pins at once, e.g. a Function's external vs
    // local-mirrored ports - see the per-pin dim in the port-row loop below).
    static ImU32 WithAlpha(ImU32 BaseCol, float Factor) noexcept
    {
        const int A = (int)(((BaseCol >> IM_COL32_A_SHIFT) & 0xFF) * Factor);
        return (BaseCol & ~((ImU32)0xFF << IM_COL32_A_SHIFT)) | ((ImU32)A << IM_COL32_A_SHIFT);
    }
    // An If/ForEachLoop and everything in its scope - including its own End (or End/End-Else pair)
    // and anything nested inside, else-branch content included - is one piece, never separable by an
    // ordinary drag. Expands an initial moving-set so that touching ANY part of a scope (the owner,
    // its marker, or something dragged from inside it via a multi-select) pulls the whole thing along
    // - reuses ComputeScopeSpan, which already walks through further nesting on its own.
    static std::vector<std::uint64_t> ExpandMoveSetForScopes(const std::vector<node_instance>& Nodes, std::vector<std::uint64_t> Ids)
    {
        std::set<std::uint64_t> Result(Ids.begin(), Ids.end());
        bool bChanged = true;
        while (bChanged)
        {
            bChanged = false;
            for (auto Id : std::vector<std::uint64_t>(Result.begin(), Result.end()))
            {
                const node_instance* pN = nullptr;
                for (auto& N : Nodes) if (N.m_Id == Id) { pN = &N; break; }
                if (!pN) continue;

                std::uint64_t OwnerId = 0;
                if (pN->m_OwnedEndId != 0) OwnerId = Id; // Id is itself an owner
                else for (auto& N : Nodes) if (N.m_OwnedEndId == Id) { OwnerId = N.m_Id; break; } // Id is someone's marker
                if (OwnerId == 0) continue;

                for (auto SpanId : ComputeScopeSpan(Nodes, OwnerId))
                    if (Result.insert(SpanId).second) bChanged = true;
            }
        }
        // Result is a std::set, so it's currently sorted by raw GUID value - meaningless for
        // ordering. Re-sort by each node's actual (SpineId, Order) so whatever consumes this list to
        // rebuild a sequence (ReorderNodes/MoveNodesTo) gets the nodes back in their real, current
        // relative order, not scrambled by id value.
        std::vector<std::uint64_t> Out(Result.begin(), Result.end());
        std::sort(Out.begin(), Out.end(), [&](std::uint64_t A, std::uint64_t B)
        {
            const node_instance *pA = nullptr, *pB = nullptr;
            for (auto& N : Nodes) { if (N.m_Id == A) pA = &N; if (N.m_Id == B) pB = &N; }
            if (!pA || !pB) return false;
            if (pA->m_SpineId != pB->m_SpineId) return pA->m_SpineId < pB->m_SpineId;
            return pA->m_Order < pB->m_Order;
        });
        return Out;
    }
    static float DistPointSegment(ImVec2 P, ImVec2 A, ImVec2 B) noexcept
    {
        const ImVec2 AB{ B.x - A.x, B.y - A.y };
        const float Len2 = AB.x * AB.x + AB.y * AB.y;
        float T = Len2 > 0.0f ? ((P.x - A.x) * AB.x + (P.y - A.y) * AB.y) / Len2 : 0.0f;
        T = std::clamp(T, 0.0f, 1.0f);
        const ImVec2 Closest{ A.x + AB.x * T, A.y + AB.y * T };
        return std::hypot(P.x - Closest.x, P.y - Closest.y);
    }

    // Drag-to-connect and selection state - persisted by the caller across frames, same role as
    // Canvas.tsx's own `drag` React state and the node/link selection rslgraph-ui itself never had.
    struct canvas_drag
    {
        bool           m_bActive = false;
        std::uint64_t  m_FromNode = 0;
        bool            m_bFromIsOutput = false;
        int             m_FromIndex = 0;
        ImVec2          m_FromPos{};
        char            m_FromSide = 'R'; // which of the (possibly two) rendered glyphs for this pin was grabbed
    };
    struct canvas_selection
    {
        std::set<std::uint64_t> m_SelectedNodes; // Ctrl/Shift-click toggles membership, matching this
                                                   // codebase's own multi-select convention (E23's bone
                                                   // tree/viewport)
        std::uint64_t            m_SelectedLink = 0;
        std::uint64_t            m_SelectedGapSpineId = 0;  // 0 means none selected
        int                      m_SelectedGapIndex   = -1; // a gap slot within m_SelectedGapSpineId,
                                                              // selected the same way a node is.
                                                              // Future copy/paste targets this.
    };

    // Drag-to-reorder: picking up a node (or, if it's part of the current selection, the whole
    // selection) and dropping it on a spine "+" marker moves it to that stacking position - separate
    // from canvas_drag above, which is pin-to-pin wiring.
    struct canvas_node_drag
    {
        bool                        m_bActive = false;
        std::vector<std::uint64_t> m_MovingIds;
    };

    // Dragging one of the two circles on a spine-control marker - either to grow a new column/spine
    // off of it, or (staying inside the dragged spine's own column) to freely reposition that spine's
    // own Y. No "which circle was grabbed" field on purpose - direction is recomputed live every frame
    // from the current mouse position relative to the grab point, never locked in at grab-time (see
    // the design conversation this came out of: "we do not care... we snap the origin... and move on").
    struct canvas_spine_drag
    {
        bool           m_bActive  = false;
        std::uint64_t  m_SpineId  = 0; // the spine the grabbed marker belongs to
        float          m_GrabY    = 0.0f; // world Y of the grabbed marker itself, at grab time
    };

    // Delete key on a selected (non-empty) spine's own gap-marker can't just call DeleteSpine - it
    // refuses a spine that still has nodes on it. Instead this holds which spine is waiting on the
    // user's "delete it and everything on it?" answer, between the frame Delete was pressed and the
    // frame the confirm popup gets one.
    struct canvas_delete_spine_confirm
    {
        std::uint64_t m_SpineId = 0; // 0 = no confirmation pending
    };

    // Zoom (mouse wheel, anchored under the cursor) and pan (right-drag on empty canvas space, both
    // axes) - the canvas has no native ImGui scrollbar, so this is the only way to navigate a graph
    // wider/taller than the window. No bounds on either axis - the graph can grow arbitrarily far in
    // any direction as columns/spines are added, so there's no fixed content extent to clamp against.
    struct canvas_view
    {
        float m_Zoom = 1.0f;
        float m_PanX = 0.0f;
        float m_PanY = 0.0f;
        bool  m_bPanDragActive = false; // true from a right-press starting inside the canvas until the
                                         // right button releases - tracked by hand (see DrawGraphCanvas)
                                         // rather than via ImGui's own item/active-id system, since that
                                         // slot is already claimed while a pin-to-pin/node-reorder drag
                                         // (left button) is in progress, and panning needs to keep
                                         // working through that.
    };

    // Runs the graph for real (RunProgram), then rebuilds the GPU mesh preview for every pin
    // currently carrying a "Mesh" value. Not itself named by the split plan's header list; placed
    // here since it's the natural home for "run the interpreter and refresh canvas-support state"
    // and needs both RunProgram (NodeOS_Interpreter.h) and GetInputValue/mesh_preview_system (this
    // header) - see this file's own note on this being a plan gap, resolved conservatively.
    static void ExecuteGraph(xgpu::device& Device, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::vector<spine>& Spines, mesh_preview_system& MeshPreview)
    {
        for (auto& Node : Nodes)
        {
            if (Node.m_bHasRun && Node.m_pNode)
                Node.m_pNode->FreeOutputs(Node.m_CachedOutputs.data());
            Node.m_bHasRun = false;
            Node.m_LastError.clear();
            Node.m_CachedOutputs.clear();
        }

        // Lives for the rest of this call, including the mesh-preview pass below (which resolves
        // input values again via its own GetInputValue calls) - see literal_storage's own comment.
        literal_storage LiteralScratch;
        RunProgram(Nodes, Links, Spines, LiteralScratch);

        // "End" markers are deliberately never marked m_bHasRun by RunSpineRange (there's nothing to
        // run - they're a pure boundary) - excluded here so a working, correctly-skipped marker
        // doesn't get flagged as an error alongside genuinely unreached content. A NodeBuilder itself
        // is also never m_bHasRun unless something actually pulled its output this run (see
        // RunNodeBuilderBody) - "not reached" is an accurate, not misleading, message in that case:
        // nothing tested this node's body this run.
        for (auto& Node : Nodes)
            if (!Node.m_bHasRun && Node.m_pNode && Node.m_pNode->m_pFactory->getName() != "End")
                Node.m_LastError = "not reached this run - unconnected to the main exec/spine flow";

        // Rebuild the GPU mesh preview for every pin currently carrying a "Mesh" value - both a
        // producer's output (Cube) and a consumer's input (Inspect Mesh) get a live render.
        for (auto& Node : Nodes)
        {
            if (!Node.m_pNode) continue;
            const auto NodeOutputs = Node.m_pNode->getOutputs();
            const auto NodeInputs  = Node.m_pNode->getInputs();
            for (int i = 0; i < (int)NodeOutputs.size(); ++i)
            {
                void* pValue = (Node.m_bHasRun && i < (int)Node.m_CachedOutputs.size()) ? Node.m_CachedOutputs[i] : nullptr;
                MeshPreview.RebuildIfMesh(Device, OutPinOf(Node.m_Id, i), NodeOutputs[i].m_pTypeName, pValue);
            }
            for (int i = 0; i < (int)NodeInputs.size(); ++i)
            {
                MeshPreview.RebuildIfMesh(Device, InPinOf(Node.m_Id, i), NodeInputs[i].m_pTypeName, GetInputValue(Node.m_Id, i, Nodes, Links, LiteralScratch));
            }
        }
    }
}
