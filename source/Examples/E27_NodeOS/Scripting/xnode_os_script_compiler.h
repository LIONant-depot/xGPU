#pragma once
//-----------------------------------------------------------------------------------
// Third-version compiler: one flat spine, recursively parsed into real nested C++.
// (NODE_SCRIPTING_DESIGN.md, section 4.) There is no lambda/call mechanism anymore - a
// spine is a plain ordered list of node instances, and "next thing to execute" is simply
// the next node in that list (Order+1), all the way through. `If`/`ForEachLoop` don't
// redirect to a different spine at all; instead each one OWNS a closing marker node
// (`End`, or `End`+`End-Else` for an if/else) placed later in the SAME list. The
// compiler does recursive-descent: when it reaches an owner node, it consumes nodes up
// to its own closing marker as that construct's nested body, emits a real braced C++
// block, and continues from right after the marker - never a jump/label in the output.
//
// A node's own template is just its "header" text plus exactly one `$body` placeholder
// where the recursively-compiled nested content goes - e.g. ForEachLoop's template is
// `"for (...) {\n$body}\n"`. A plain statement node (no owned marker) has no `$body` and
// is just emitted as-is. `If`'s else clause, when present (End-Else used), is appended by
// the compiler itself, not by the node's own template - the presence of an else is a
// graph-structural fact, not something a template should need to spell out.
//
// Because this is real nested C++, a local declared by one node (e.g. `Element_$id`) is
// visible to any node compiled into its OWN nested body, via completely ordinary C++
// scoping - no capture, no parameters, nothing extra needed. Also because it's real
// nesting, a missing or mismatched closing marker is a plain, catchable parse error, not
// something that can silently produce broken output - see the design doc for why this
// replaced both the earlier flat label/goto model and the spine-as-lambda model.
//
// The compiler never synthesizes braces itself - `{`/`}` are plain text the node author
// writes directly in their own template, exactly like every other character in it. A
// control node's template covers everything up to and including its own opening `{` and
// prologue statements (e.g. ForEachLoop declaring `Element_$id` before its body), then
// `$body`, then its own closing `}` - the compiler only ever splices the recursively-
// compiled nested content in at that one marker. The one thing the compiler DOES own
// itself is the literal `else { ... }` wrapper when an End-Else is present, since no node
// contributes that text (End-Else is a pure position marker, like End).
//-----------------------------------------------------------------------------------

#include "xnode_os_node_type_definition.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace nodeos::scripting
{
    struct pin_ref
    {
        std::uint64_t m_NodeInstanceId = 0;
        std::string   m_PinId;
    };

    // A DataIn pin's value comes from exactly one DataOut pin elsewhere in the SAME spine.
    struct data_link
    {
        pin_ref m_Source; // DataOut
        pin_ref m_Target; // DataIn
    };

    // OwnerId -> CloserId: an If/ForEachLoop's own closing marker, OR (when OwnerId is
    // itself an End-Else) the End that closes the else block it opened. This one shape
    // covers both cases - see the file header.
    struct owned_end_link
    {
        std::uint64_t m_OwnerId  = 0;
        std::uint64_t m_CloserId = 0;
    };

    struct node_instance
    {
        std::uint64_t         m_InstanceId = 0;
        xresource::type_guid  m_TypeGuid;
        int                   m_Order      = 0;
    };

    // One flat spine - the whole unit of compilation for this version. Multiple
    // independent spines (e.g. separate event entry points) are just separate
    // flat_script_graphs for now; whether/how they interact via column position is an
    // open question deliberately deferred (design doc).
    struct flat_script_graph
    {
        std::vector<node_instance>   m_Nodes;
        std::vector<data_link>       m_DataLinks;
        std::vector<owned_end_link>  m_OwnedEnds;
    };

    struct compile_result
    {
        bool        m_bSuccess = false;
        std::string m_Source;
        std::string m_Error;
    };

    using node_type_by_guid = std::unordered_map<std::uint64_t, const node_type_definition*>;

    namespace details
    {
        // Substitutes $id/$input[PinId] only - no $body, no $call. Used for the "header"
        // text of every node (a plain statement's whole template, or a control node's
        // text up to its $body placeholder).
        inline bool SubstituteHeader
        (
            const std::string&          T
        ,   const node_type_definition& Def
        ,   const node_instance&        Inst
        ,   const flat_script_graph&    Graph
        ,   std::string&                Out
        ,   std::string&                Error
        ) noexcept
        {
            for (std::size_t i = 0; i < T.size(); )
            {
                if (T[i] != '$') { Out += T[i]; ++i; continue; }

                if (T.compare(i, 3, "$id") == 0) { Out += std::to_string(Inst.m_InstanceId); i += 3; continue; }

                if (T.compare(i, 6, "$input") == 0)
                {
                    std::size_t j = i + 6;
                    if (j >= T.size() || T[j] != '[') { Error = "malformed $input in template of " + Def.m_DisplayName; return false; }
                    std::size_t Close = T.find(']', j);
                    if (Close == std::string::npos) { Error = "unterminated $input in template of " + Def.m_DisplayName; return false; }
                    std::string PinId = T.substr(j + 1, Close - j - 1);

                    const data_link* Found = nullptr;
                    for (auto& L : Graph.m_DataLinks)
                        if (L.m_Target.m_NodeInstanceId == Inst.m_InstanceId && L.m_Target.m_PinId == PinId) { Found = &L; break; }
                    if (!Found) { Error = "node " + std::to_string(Inst.m_InstanceId) + " (" + Def.m_DisplayName + "): unwired DataIn pin '" + PinId + "'"; return false; }
                    Out += Found->m_Source.m_PinId + "_" + std::to_string(Found->m_Source.m_NodeInstanceId);
                    i = Close + 1;
                    continue;
                }

                Out += T[i];
                ++i;
            }
            return true;
        }

        inline bool CompileRange
        (
            const std::vector<const node_instance*>&        Sorted
        ,   std::size_t                                      StartIdx
        ,   std::size_t                                      EndIdxExclusive
        ,   const flat_script_graph&                          Graph
        ,   const node_type_by_guid&                          TypeByGuid
        ,   const std::unordered_map<std::uint64_t, std::uint64_t>& OwnerToCloser
        ,   const std::unordered_map<std::uint64_t, std::size_t>&   IndexById
        ,   std::string&                                      Out
        ,   std::string&                                      Error
        ) noexcept
        {
            std::size_t i = StartIdx;
            while (i < EndIdxExclusive)
            {
                const node_instance* N = Sorted[i];
                auto TypeIt = TypeByGuid.find(N->m_TypeGuid.m_Value);
                if (TypeIt == TypeByGuid.end()) { Error = "node " + std::to_string(N->m_InstanceId) + ": unknown node type"; return false; }
                const node_type_definition& Def = *TypeIt->second;

                auto OwnerIt = OwnerToCloser.find(N->m_InstanceId);
                if (OwnerIt == OwnerToCloser.end())
                {
                    // Plain statement node - substitute its whole template, emit, move on.
                    std::string Body;
                    if (!SubstituteHeader(Def.m_Template, Def, *N, Graph, Body, Error)) return false;
                    Out += "  // GRAPH_NODE:" + std::to_string(N->m_InstanceId) + "\n";
                    Out += Body + "\n";
                    ++i;
                    continue;
                }

                // Owner node (If/ForEachLoop): find its closer's index, recursively compile
                // the range strictly between them as $body.
                std::uint64_t CloserId = OwnerIt->second;
                auto CloserIdxIt = IndexById.find(CloserId);
                if (CloserIdxIt == IndexById.end()) { Error = "node " + std::to_string(N->m_InstanceId) + ": its closing marker doesn't exist in this spine"; return false; }
                std::size_t CloserIdx = CloserIdxIt->second;
                if (!(CloserIdx > i && CloserIdx < EndIdxExclusive)) { Error = "node " + std::to_string(N->m_InstanceId) + ": its closing marker is out of range - malformed/crossed nesting"; return false; }

                std::string Header;
                std::size_t BodyPos = Def.m_Template.find("$body");
                if (BodyPos == std::string::npos) { Error = "node " + std::to_string(N->m_InstanceId) + " (" + Def.m_DisplayName + "): owns a closing marker but its template has no $body"; return false; }
                if (!SubstituteHeader(Def.m_Template.substr(0, BodyPos), Def, *N, Graph, Header, Error)) return false;
                std::string Trailer;
                if (!SubstituteHeader(Def.m_Template.substr(BodyPos + 5), Def, *N, Graph, Trailer, Error)) return false;

                std::string Inner;
                if (!CompileRange(Sorted, i + 1, CloserIdx, Graph, TypeByGuid, OwnerToCloser, IndexById, Inner, Error)) return false;

                // Is the closer itself an End-Else (i.e. does IT also own a further closer)?
                auto ElseIt = OwnerToCloser.find(CloserId);
                if (ElseIt != OwnerToCloser.end())
                {
                    std::uint64_t FinalCloserId = ElseIt->second;
                    auto FinalIdxIt = IndexById.find(FinalCloserId);
                    if (FinalIdxIt == IndexById.end()) { Error = "End-Else node " + std::to_string(CloserId) + ": its own closing End doesn't exist in this spine"; return false; }
                    std::size_t FinalIdx = FinalIdxIt->second;
                    if (!(FinalIdx > CloserIdx && FinalIdx < EndIdxExclusive)) { Error = "End-Else node " + std::to_string(CloserId) + ": its closing End is out of range - malformed/crossed nesting"; return false; }

                    std::string ElseInner;
                    if (!CompileRange(Sorted, CloserIdx + 1, FinalIdx, Graph, TypeByGuid, OwnerToCloser, IndexById, ElseInner, Error)) return false;

                    Out += "  // GRAPH_NODE:" + std::to_string(N->m_InstanceId) + "\n";
                    Out += Header + Inner + Trailer + " else {\n" + ElseInner + "}\n";
                    i = FinalIdx + 1;
                }
                else
                {
                    Out += "  // GRAPH_NODE:" + std::to_string(N->m_InstanceId) + "\n";
                    Out += Header + Inner + Trailer + "\n";
                    i = CloserIdx + 1;
                }
            }
            return true;
        }
    }

    inline compile_result CompileFlatScriptGraph(const flat_script_graph& Graph, const node_type_by_guid& TypeByGuid) noexcept
    {
        compile_result R;

        std::vector<const node_instance*> Sorted;
        for (auto& N : Graph.m_Nodes) Sorted.push_back(&N);
        std::sort(Sorted.begin(), Sorted.end(), [](const node_instance* A, const node_instance* B) { return A->m_Order < B->m_Order; });

        std::unordered_map<std::uint64_t, std::size_t> IndexById;
        for (std::size_t i = 0; i < Sorted.size(); ++i) IndexById[Sorted[i]->m_InstanceId] = i;

        std::unordered_map<std::uint64_t, std::uint64_t> OwnerToCloser;
        for (auto& L : Graph.m_OwnedEnds) OwnerToCloser[L.m_OwnerId] = L.m_CloserId;

        std::string Body;
        if (!details::CompileRange(Sorted, 0, Sorted.size(), Graph, TypeByGuid, OwnerToCloser, IndexById, Body, R.m_Error))
            return R;

        R.m_bSuccess = true;
        R.m_Source   = std::move(Body);
        return R;
    }
}
