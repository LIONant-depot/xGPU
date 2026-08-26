#pragma once
// The graph interpreter, extracted from the monolithic E27_NodeOS_Editor.cpp (header #4).
#include "NodeOS_Common.h"
#include "NodeOS_Types.h"

namespace nodeos
{
    // Real definition lives in Editor/NodeOS_CanvasSupport.h (header #7, included later) - forward
    // declared here exactly like the original monolith already forward-declared things defined later
    // in the same file. PullInputValue below is the only interpreter function that needs it (as the
    // "nothing wired - use whatever literal the property panel shows" fallback).
    static void* ResolveUnconnectedLiteral(std::uint64_t NodeId, int InputIndex, const std::vector<node_instance>& Nodes, const std::vector<link_instance>& Links, literal_storage& Scratch);

    // A node runs if it's reachable from the root spine by walking ordinary spine Order and Exec
    // wires (the rule settled early this session) - OR, since a later session, if some node that
    // WAS reached that way reads one of its outputs: an ordinary/"pure" node (no Exec pins of its
    // own, doesn't own a scope) is a data dependency, not a position on a spine - moving it to a
    // different spine, or ahead of/behind whoever reads it, was never meant to change whether it
    // runs, any more than it would in Blueprints/Shader Graph/any other pull-based node graph. See
    // PullInputValue/EnsureNodeRun below for the actual pull; anything else genuinely unreached by
    // either rule is still inert, "commented code," never executed.
    static node_instance* FindNodeById(std::uint64_t Id, std::vector<node_instance>& Nodes)
    {
        auto It = std::find_if(Nodes.begin(), Nodes.end(), [&](auto& N) { return N.m_Id == Id; });
        return It == Nodes.end() ? nullptr : &*It;
    }
    // A port that carries a real, resolvable value at runtime - excludes Exec (pure control-flow
    // trigger, never a value) and Scope (the owner<->End ownership pin, likewise never a value).
    static bool IsRealDataPort(const xnode_os_port_desc& P) noexcept
    {
        return !IsExecType(P.m_pTypeName) && !IsScopeType(P.m_pTypeName);
    }
    static void RunOrdinaryNode(node_instance& Node, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, literal_storage& Scratch, int PullDepth = 0);
    // Only a genuinely PURE/ordinary data node is eligible to be pulled - anything with a real Exec
    // pin (OnEvent/ExecutionCall/Execute/Function), or that owns a scope of its own content
    // (If/ForEachLoop), must still go through its own explicit trigger. A data read must never
    // silently invoke a Function call, fire an ExecutionCall fan-out, or run a conditional scope's
    // body as a side effect of some unrelated node just wanting to read a value - only leaf/data
    // nodes (Constant, Compare, Math Expression, ...) are safe to evaluate lazily, on demand,
    // regardless of where they happen to sit.
    static bool IsPullableNodeType(std::string_view Name) noexcept
    {
        return Name != "OnEvent" && Name != "ExecutionCall" && Name != "Execute" && Name != "Function"
            && Name != "If" && Name != "ForEachLoop" && Name != "End";
    }
    // Pull-triggered "run this NodeBuilder's own body, using whatever test rig is wired to its
    // external pins" - see this file's own RunProgram comment for the full "interpreter always runs
    // in test mode" story. Defined after RunSpineRange/PullInputValue (needs both); declared here so
    // EnsureNodeRun can dispatch to it.
    static void RunNodeBuilderBody(node_instance& Builder, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, literal_storage& Scratch, int PullDepth);

    static void EnsureNodeRun(std::uint64_t NodeId, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, literal_storage& Scratch, int PullDepth)
    {
        // A real cyclic data dependency (A needs B needs A) would otherwise recurse forever - bail
        // rather than stack-overflow; no ordinary graph legitimately nests pulls this deep. Left
        // unrun, exactly like any other node the walk never reaches - PullInputValue's own caller
        // sees nullptr, same as an honestly-unconnected pin.
        if (PullDepth > 64) return;
        node_instance* pNode = FindNodeById(NodeId, Nodes);
        if (!pNode || !pNode->m_pNode || pNode->m_bHasRun) return;
        if (!IsPullableNodeType(pNode->m_pNode->m_pFactory->getName())) return;
        if (pNode->m_pNode->m_pFactory->getName() == "NodeBuilder")
            RunNodeBuilderBody(*pNode, Nodes, Links, Scratch, PullDepth + 1);
        else
            RunOrdinaryNode(*pNode, Nodes, Links, Scratch, PullDepth + 1);
    }
    // Like GetInputValue, but for real execution: if a wire's source hasn't run yet, PULLS it (runs
    // it right now, recursively resolving its own inputs the same way) instead of just reporting
    // nullptr - see the pull-based-execution comment above FindNodeById for why. GetInputValue itself
    // stays read-only/non-pulling, for the canvas preview and mesh-preview passes that must never
    // trigger a real Execute() (with its real side effects) merely because a frame got drawn.
    static void* PullInputValue(std::uint64_t NodeId, int InputIndex, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, literal_storage& Scratch, int PullDepth)
    {
        for (auto& Link : Links)
        {
            if (Link.m_TargetNode != NodeId || Link.m_TargetInput != InputIndex) continue;
            auto SourceIt = std::find_if(Nodes.begin(), Nodes.end(), [&](auto& N) { return N.m_Id == Link.m_SourceNode; });
            if (SourceIt == Nodes.end()) return nullptr;
            if (!SourceIt->m_bHasRun)
                EnsureNodeRun(SourceIt->m_Id, Nodes, Links, Scratch, PullDepth);
            if (!SourceIt->m_bHasRun) return nullptr; // still didn't run - an Exec-gated/scope-owning source, or a cycle bailout
            return (Link.m_SourceOutput < (int)SourceIt->m_CachedOutputs.size()) ? SourceIt->m_CachedOutputs[Link.m_SourceOutput] : nullptr;
        }
        return ResolveUnconnectedLiteral(NodeId, InputIndex, Nodes, Links, Scratch);
    }
    // A dedicated, minimal function - no local C++ objects requiring unwinding (MSVC's C2712 forbids
    // mixing __try with those in the same function) - just the raw call and SEH's own catch-and-
    // report. Catches hardware faults (access violation, divide-by-zero, etc.) a buggy or still-
    // being-developed plugin's Execute() might trigger. NOT a sandbox: per Microsoft's own guidance,
    // the process's heap/global state may already be corrupted by the time this returns - this is
    // "log it and skip this node," never "guaranteed safe to keep running normally" - but it's
    // strictly better than the whole editor going down over one bad node, which is the actual goal
    // (see [[xgpu_plugin_dll_hotreload]] for the companion "don't need to restart to fix it" half).
    static unsigned long SEH_CallExecute(xnode_os_node* pNode, void** Inputs, void** Outputs) noexcept
    {
        __try
        {
            pNode->Execute(Inputs, Outputs);
            return 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return GetExceptionCode();
        }
    }

    static void RunOrdinaryNode(node_instance& Node, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, literal_storage& Scratch, int PullDepth)
    {
        if (!Node.m_pNode || Node.m_bHasRun) return;
        const auto NodeInputs  = Node.m_pNode->getInputs();
        const auto NodeOutputs = Node.m_pNode->getOutputs();
        std::vector<void*> Inputs(NodeInputs.size(), nullptr);
        for (int i = 0; i < (int)NodeInputs.size(); ++i)
            Inputs[i] = PullInputValue(Node.m_Id, i, Nodes, Links, Scratch, PullDepth);

        // Re-check, not just the guard at entry: resolving an input above can recurse into a NodeBuilder
        // pull (RunNodeBuilderBody) whose own body-walk can, in a graph shaped like this node's inputs
        // being wired through a NodeBuilder positioned BEFORE it in the same spine, reach and run THIS
        // node reentrantly before this call ever gets here - confirmed the hard way (a Math Expression
        // wired to a NodeBuilder's local-mirror outputs, with the NodeBuilder itself pulled by something
        // still earlier in the same PullInputValue chain, executed twice and leaked its first malloc'd
        // output). Bailing here, after resolving inputs, is what makes this call a no-op instead of a
        // real double-Execute() when that's already happened.
        if (Node.m_bHasRun) return;

        Node.m_CachedOutputs.assign(NodeOutputs.size(), nullptr);

        // __except alone (SEH) does NOT catch a plain C++ throw under this project's /EHsc - only
        // hardware faults. Wrapping the SEH call in an ordinary try/catch here (not inside
        // SEH_CallExecute itself, where it would trip C2712) covers both failure modes a plugin's
        // Execute() could hit, each reported through the same m_LastError the UI already renders in
        // red on the node itself (see line ~3655) - a crashing node is visible at a glance, not a
        // silent gap in the graph.
        try
        {
            if (const unsigned long ExCode = SEH_CallExecute(Node.m_pNode, Inputs.data(), Node.m_CachedOutputs.data()); ExCode != 0)
                Node.m_LastError = std::format("Execute() crashed (exception code {:#x}) - node skipped this run", ExCode);
        }
        catch (const std::exception& Ex) { Node.m_LastError = std::format("Execute() threw: {}", Ex.what()); }
        catch (...)                      { Node.m_LastError = "Execute() threw a non-standard exception"; }

        Node.m_bHasRun = true;
    }
    static void RunSpineRange(std::uint64_t SpineId, int FromOrderInclusive, int ToOrderExclusive, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, literal_storage& Scratch);
    static void RunExecTarget(std::uint64_t TargetNodeId, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, literal_storage& Scratch);
    // ExecutionCall's Exec output fans out to every Exec-typed link off it - fork, run each target to
    // completion (RunExecTarget is fully synchronous, so this doubles as the join: nothing after this
    // call returns until every fanned-out target has finished). Order between multiple targets is
    // deliberately unspecified (settled this session); a plain left-to-right pass over Links is as
    // good as any other order today.
    static void RunExecutionCall(node_instance& Caller, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, literal_storage& Scratch)
    {
        if (Caller.m_bHasRun) return;
        Caller.m_bHasRun = true;
        for (auto& L : Links)
            if (L.m_SourceNode == Caller.m_Id)
                RunExecTarget(L.m_TargetNode, Nodes, Links, Scratch);
    }
    // Entering Function or Execute via an incoming Exec trigger - the only way either ever runs (see
    // RunSpineRange, which deliberately skips both during ordinary positional walking).
    static void RunExecTarget(std::uint64_t TargetNodeId, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, literal_storage& Scratch)
    {
        node_instance* pTarget = FindNodeById(TargetNodeId, Nodes);
        if (!pTarget || !pTarget->m_pNode || pTarget->m_bHasRun) return;
        const auto Name = pTarget->m_pNode->m_pFactory->getName();

        if (Name == "Function")
        {
            // Real subroutine call: resolve its own declared (external, non-local, non-Exec) inputs
            // from wherever they're wired, mirror each one into the matching local-scope OUTPUT slot
            // (the body's own view of its parameters - function_node.cpp's Rebuild always places the
            // K-th declared input's mirror at output index [ExternalOutputCount + K]), run the body
            // (everything between this node and its own End, in Order), then mirror whatever the
            // body wrote into the local Result-mirror INPUT back out to the matching declared
            // external OUTPUT (the reverse direction, same indexing scheme).
            const auto Inputs  = pTarget->m_pNode->getInputs();
            const auto Outputs = pTarget->m_pNode->getOutputs();
            std::vector<void*> InVals(Inputs.size(), nullptr);
            for (int i = 0; i < (int)Inputs.size(); ++i)
                if (!Inputs[i].m_bLocalScope && IsRealDataPort(Inputs[i]))
                    InVals[i] = PullInputValue(pTarget->m_Id, i, Nodes, Links, Scratch, 0);
            pTarget->m_CachedOutputs.assign(Outputs.size(), nullptr);
            pTarget->m_pNode->Execute(InVals.data(), pTarget->m_CachedOutputs.data()); // no-op today, kept for a real ABI

            // Where the local-mirror GROUP actually starts in each direction - found by scanning for
            // the real boundary, not by counting type-filtered pins and assuming the mirror group
            // sits immediately after them. That assumption held while Exec was appended LAST (so
            // every non-local input was also a "real data" input), but broke the moment Exec moved
            // to be first: counting only "external, real-data" inputs then undercounts by exactly
            // one (Exec occupies a non-local slot the count skips), pointing every mirror lookup one
            // pin too early. Scanning for the actual first-local-pin index is correct regardless of
            // how many/which non-local pins precede the local group, in either direction.
            int FirstLocalOutputIdx = (int)Outputs.size();
            for (int i = 0; i < (int)Outputs.size(); ++i) if (Outputs[i].m_bLocalScope) { FirstLocalOutputIdx = i; break; }
            int FirstLocalInputIdx = (int)Inputs.size();
            for (int i = 0; i < (int)Inputs.size(); ++i) if (Inputs[i].m_bLocalScope) { FirstLocalInputIdx = i; break; }

            for (int i = 0, K = 0; i < (int)Inputs.size(); ++i)
            {
                if (Inputs[i].m_bLocalScope || !IsRealDataPort(Inputs[i])) continue;
                const int MirrorIdx = FirstLocalOutputIdx + K;
                if (MirrorIdx < (int)pTarget->m_CachedOutputs.size()) pTarget->m_CachedOutputs[MirrorIdx] = InVals[i];
                ++K;
            }
            pTarget->m_bHasRun = true;

            auto* pEnd = FindNodeById(pTarget->m_OwnedEndId, Nodes);
            const int EndOrder = pEnd ? pEnd->m_Order : INT_MAX;
            RunSpineRange(pTarget->m_SpineId, pTarget->m_Order + 1, EndOrder, Nodes, Links, Scratch);

            for (int i = 0, L2 = 0; i < (int)Outputs.size(); ++i)
            {
                if (Outputs[i].m_bLocalScope || !IsRealDataPort(Outputs[i])) continue;
                const int MirrorInputIdx = FirstLocalInputIdx + L2;
                pTarget->m_CachedOutputs[i] = (MirrorInputIdx < (int)Inputs.size()) ? PullInputValue(pTarget->m_Id, MirrorInputIdx, Nodes, Links, Scratch, 0) : nullptr;
                ++L2;
            }
        }
        else if (Name == "Execute")
        {
            pTarget->m_CachedOutputs.assign(pTarget->m_pNode->getOutputs().size(), nullptr);
            pTarget->m_bHasRun = true;
            // No owned scope - "body" is simply everything positionally after it in its own spine,
            // all the way to the spine's own end (NODE_SCRIPTING_DESIGN.md's Execute/lambda-capture
            // analogy) - nothing bounds it the way Function's own End does.
            RunSpineRange(pTarget->m_SpineId, pTarget->m_Order + 1, INT_MAX, Nodes, Links, Scratch);
        }
    }
    // The flat-spine model's own base case: run every node positioned in [FromOrderInclusive,
    // ToOrderExclusive) of one spine, in Order. "End" is a pure boundary marker, never run. Function
    // and Execute are deliberately SKIPPED here even if positionally reached - both declare a real
    // Exec input specifically so they only ever run via an incoming trigger (RunExecTarget), never
    // just because ordinary spine order got to them. ForEachLoop isn't given real loop semantics
    // yet - nothing saved exercises it; RunOrdinaryNode's generic "resolve inputs, call Execute()
    // once" is what it'd fall through to today, same as any other node type not specifically
    // recognized here - a real next step once something actually needs it.
    static void RunSpineRange(std::uint64_t SpineId, int FromOrderInclusive, int ToOrderExclusive, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, literal_storage& Scratch)
    {
        std::vector<node_instance*> Members;
        for (auto& N : Nodes)
            if (N.m_SpineId == SpineId && N.m_Order >= FromOrderInclusive && N.m_Order < ToOrderExclusive)
                Members.push_back(&N);
        std::sort(Members.begin(), Members.end(), [](auto* A, auto* B) { return A->m_Order < B->m_Order; });
        for (std::size_t i = 0; i < Members.size(); ++i)
        {
            auto* pN = Members[i];
            if (!pN->m_pNode || pN->m_bHasRun) continue;
            const auto Name = pN->m_pNode->m_pFactory->getName();
            if (Name == "End") continue;
            if (Name == "ExecutionCall") { RunExecutionCall(*pN, Nodes, Links, Scratch); continue; }
            // NodeBuilder is skipped positionally, same as Function/Execute, but for a different
            // reason: those two need an explicit Exec trigger; NodeBuilder has no Exec pin at all and
            // only ever runs PULL-triggered (RunNodeBuilderBody, via EnsureNodeRun) - exactly like
            // Constant/Math Expression already do - when something downstream needs its output. If
            // nothing pulls it, it correctly stays !m_bHasRun ("not reached this run").
            if (Name == "Function" || Name == "Execute" || Name == "NodeBuilder") continue;
            if (Name == "If" && pN->m_OwnedEndId != 0)
            {
                // If has no Exec pins at all (if_node.cpp) - purely positional, its true-branch body
                // is just whatever physically follows it in this same spine up to its owned End. Runs
                // itself first (a no-op Execute(), but this is what resolves+marks Condition's source
                // as read) then decides whether to recurse into the body at all - an untaken branch's
                // nodes are deliberately left m_bHasRun == false, so the existing "not reached this
                // run" flagging (ExecuteGraph's epilogue) shows exactly which path didn't execute,
                // same meaning it already carries for any other unreached node. Condition is resolved
                // via PullInputValue, not GetInputValue - Compare (or whatever feeds it) is a data
                // dependency, not something that has to happen to sit somewhere the flat walk already
                // reaches; If wiring TO it is exactly what should pull it in, wherever it lives.
                RunOrdinaryNode(*pN, Nodes, Links, Scratch);
                const bool* pCond = static_cast<const bool*>(PullInputValue(pN->m_Id, 0, Nodes, Links, Scratch, 0));
                auto* pEnd = FindNodeById(pN->m_OwnedEndId, Nodes);
                const int EndOrder = pEnd ? pEnd->m_Order : INT_MAX;
                if (pCond && *pCond)
                    RunSpineRange(SpineId, pN->m_Order + 1, EndOrder, Nodes, Links, Scratch);
                // Either way, the OUTER walk must not also treat the body as ordinary members once
                // this returns - skip past it (the recursive call above already ran+marked it when
                // taken; when not taken, this is what keeps it from running unconditionally, which
                // was the whole bug this block exists to fix).
                while (i + 1 < Members.size() && Members[i + 1]->m_Order < EndOrder) ++i;
                continue;
            }
            RunOrdinaryNode(*pN, Nodes, Links, Scratch);
        }
    }

    // Pull-triggered: something downstream (e.g. a Print wired to Builder's external Sum output)
    // needed a value, EnsureNodeRun routed here instead of the generic RunOrdinaryNode. Same two-phase
    // mirroring dance RunExecTarget's own "Function" branch uses, just PULL-triggered instead of Exec-
    // triggered (Builder has no Exec pin), and bounded by Builder's own owned End marker exactly like
    // Function's body is - test-rig content (a Constant feeding Builder's external inputs, a Print
    // reading its external output) lives in the SAME spine, positioned AFTER that End marker, and is
    // therefore never part of this walk's range at all - no exclusion list needed. An earlier version
    // of this node had no End marker at all ("the whole graph IS the node") and instead tried to
    // exclude test-rig nodes by which pins they were wired to; that broke the moment test content sat
    // in the same spine as the body (positionally "inside" by range, wired-external "outside" by
    // convention - a real contradiction, not just an edge case) - seeing node_builder_node.cpp's own
    // top comment for the full story.
    //   1. Resolve Builder's own EXTERNAL inputs (pulls from whatever test rig is wired in, e.g. a
    //      Constant) and mirror each into the matching LOCAL-scope OUTPUT slot - the body's own nodes
    //      read their "parameters" from there, same convention codegen already uses.
    //   2. Mark m_bHasRun BEFORE running the body - the body reads through Builder's own output slots
    //      (same node, different index), so PullInputValue's "is my source already run" check needs
    //      this true already, exactly like RunExecTarget's own comment on the same requirement.
    //   3. Run the body (RunSpineRange bounded to [Order+1, End's Order)) - already-run members are a
    //      no-op the second time the OUTER walk reaches them positionally (RunOrdinaryNode's own
    //      re-check guard, added specifically because this reentrant path exists).
    //   4. Mirror the LOCAL-scope INPUT slots the body just wrote into (via ordinary wires) back out
    //      to Builder's own declared EXTERNAL output slots, so the pulling caller (Print) gets a value.
    static void RunNodeBuilderBody(node_instance& Builder, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, literal_storage& Scratch, int PullDepth)
    {
        if (!Builder.m_pNode || Builder.m_bHasRun) return;
        const auto Inputs  = Builder.m_pNode->getInputs();  // external inputs + local-mirror-of-outputs
        const auto Outputs = Builder.m_pNode->getOutputs(); // external outputs + local-mirror-of-inputs + [End]

        int ExtInputCount = 0, ExtOutputCount = 0;
        for (auto& P : Inputs)  if (!P.m_bLocalScope && IsRealDataPort(P)) ++ExtInputCount;
        for (auto& P : Outputs) if (!P.m_bLocalScope && IsRealDataPort(P)) ++ExtOutputCount;

        Builder.m_CachedOutputs.assign(Outputs.size(), nullptr);

        int ExtInputSlot = 0;
        for (int i = 0; i < (int)Inputs.size(); ++i)
        {
            if (Inputs[i].m_bLocalScope || !IsRealDataPort(Inputs[i])) continue;
            Builder.m_CachedOutputs[ExtOutputCount + ExtInputSlot] = PullInputValue(Builder.m_Id, i, Nodes, Links, Scratch, PullDepth);
            ++ExtInputSlot;
        }

        Builder.m_bHasRun = true;

        auto* pEnd = FindNodeById(Builder.m_OwnedEndId, Nodes);
        const int EndOrder = pEnd ? pEnd->m_Order : INT_MAX;
        RunSpineRange(Builder.m_SpineId, Builder.m_Order + 1, EndOrder, Nodes, Links, Scratch);

        const int FirstLocalInputIdx = ExtInputCount;
        for (int j = 0; j < ExtOutputCount; ++j)
            Builder.m_CachedOutputs[j] = PullInputValue(Builder.m_Id, FirstLocalInputIdx + j, Nodes, Links, Scratch, PullDepth);
    }

    // Runs the whole PROGRAM: starts at the root spine's own beginning and walks forward - OnEvent
    // is a pure label (zero pins, does nothing on its own); ExecutionCall is what actually fires.
    // Once the root spine runs off its own end, the program is done, independent of anything else
    // that may or may not have been triggered along the way ("main spine governs program lifetime,"
    // settled this session).
    // Used by the two CODEGEN paths only (CompileToCpp/"Compile to C++" and the -CodegenSelfTest
    // hook) - both produce a standalone RunMain()-driven PROGRAM, which still isn't the right shape
    // for a node-definition graph even now that the interpreter runs it in test mode (a "does this
    // become a working program" check would just bake whatever test rig happens to be wired in as if
    // it were the real thing). Refuse at the CALL SITE, before GenerateCpp/CompileAndRunGeneratedCpp
    // ever runs - GenerateCpp itself once tried returning a one-line comment in place of source and
    // letting the caller compile it anyway, which turned a clear refusal into a confusing LNK1561
    // "entry point must be defined", confirmed the hard way.
    //
    // RunProgram (the live interpreter, "Execute Graph") does NOT use this check - see its own
    // comment for why a NodeBuilder-declaring graph runs perfectly well there, just in test mode.
    static bool HasNodeBuilder(const std::vector<node_instance>& Nodes) noexcept
    {
        return std::any_of(Nodes.begin(), Nodes.end(), [](auto& N) { return N.m_pNode && N.m_pNode->m_pFactory->getName() == "NodeBuilder"; });
    }

    // The graph itself already says what it's for - finds its own NodeBuilder so "compile this graph"
    // (the button, the pipe command) can dispatch to the right backend automatically instead of
    // making the caller separately check HasNodeBuilder and hunt down its id first. If more than one
    // exists, returns the first found - BuildNodeFromFunction's own count check still catches and
    // reports that as the real error it is; this is purely a convenience lookup, not a second place
    // that validates graph purpose.
    static node_instance* FindTheNodeBuilder(std::vector<node_instance>& Nodes) noexcept
    {
        for (auto& N : Nodes) if (N.m_pNode && N.m_pNode->m_pFactory->getName() == "NodeBuilder") return &N;
        return nullptr;
    }

    // A NodeBuilder-declaring graph runs here in TEST MODE: NodeBuilder itself is skipped during
    // ordinary positional walking (RunSpineRange's own dispatch) and only ever runs pull-triggered
    // (see RunNodeBuilderBody) - exactly like Constant/Math Expression already do, not like Function/
    // Execute's Exec-triggered model, since NodeBuilder has no Exec pin. Whatever's wired to its own
    // EXTERNAL pins in THIS graph (e.g. a Constant feeding A/B, a Print reading Sum) drives that test
    // run - and is completely invisible to BuildNode's own codegen (GenerateNodePluginCpp only ever
    // reads NodeBuilder's declared InputsSpec/OutputsSpec and its body's own internal wiring, never
    // what's wired to the external pins) - "the interpreter always runs in test mode, the C++ version
    // compiles the final node without the test cases," exactly as confirmed.
    static void RunProgram(std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::vector<spine>& Spines, literal_storage& Scratch)
    {
        GetRuntimeLog().clear();
        std::uint64_t RootSpineId = 0;
        for (auto& S : Spines) if (S.m_bIsRoot) { RootSpineId = S.m_Id; break; }
        if (RootSpineId == 0) return;
        RunSpineRange(RootSpineId, 0, INT_MAX, Nodes, Links, Scratch);
    }
}
