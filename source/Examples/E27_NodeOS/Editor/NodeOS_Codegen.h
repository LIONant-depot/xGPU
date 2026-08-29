#pragma once
// Real C++ codegen backend, extracted from the monolithic E27_NodeOS_Editor.cpp (header #5). Needs
// NodeOS_Interpreter.h (header #4) for IsPullableNodeType/FindNodeById/IsRealDataPort - the one real
// cross-backend-section dependency in the file, per the split plan.
#include "NodeOS_Common.h"
#include "NodeOS_Types.h"
#include "NodeOS_PropertySerialize.h"
#include "NodeOS_SaveLoad.h"
#include "NodeOS_Interpreter.h"

namespace nodeos
{
    // Real definition lives in Editor/NodeOS_CanvasSupport.h (header #7, included later) - forward
    // declared here exactly like ResolveUnconnectedLiteral is in NodeOS_Interpreter.h. CppInputExpr
    // and BuildNodeFromFunction's own ReadStringProp lambda both need it.
    static const xproperty::type::members* FindMemberByName(const xproperty::type::object* pObj, const char* pName) noexcept;

    // ---- Real C++ codegen (tests whether ordinary nodes' own logic can be REUSED rather than
    // reimplemented by generated code - the question this whole block exists to answer). Mirrors
    // RunSpineRange/RunExecTarget/RunExecutionCall's exact structure - same reachability walk, same
    // recognized node-type names - but EMITS TEXT instead of executing. Handles exactly what the
    // current saved test graph needs (OnEvent, Constant, ExecutionCall, Function, Execute, Print) -
    // anything else emits a visible "not implemented" comment rather than silently producing nothing
    // or something wrong.
    //
    // The interpreter (RunExecTarget's "Function" branch) needed real bookkeeping at runtime to
    // mirror a resolved parameter value into the local-scope output slot the body reads through -
    // codegen needs NONE of that: a Function's parameter and its own local-mirror output share the
    // exact same generated variable name (CppVar(FunctionId, MirrorOutIdx) for both), so the body's
    // ordinary "read my local mirror" link resolves, via the same CppInputExpr every other link
    // uses, straight to the C++ parameter itself. No separate mirroring step exists in the emitted
    // code at all - this is the concrete "codegen is simpler here than the interpreter was" case,
    // not just a claim.
    static std::string ReadStringPropertyFromSnapshot(const std::string& Snapshot, std::string_view Name)
    {
        std::size_t Pos = 0;
        while (Pos < Snapshot.size())
        {
            const std::size_t LineEnd = Snapshot.find('\n', Pos);
            const std::string Line = Snapshot.substr(Pos, (LineEnd == std::string::npos ? Snapshot.size() : LineEnd) - Pos);
            Pos = (LineEnd == std::string::npos) ? Snapshot.size() : LineEnd + 1;
            const std::size_t Tab1 = Line.find('\t');
            if (Tab1 != std::string::npos && std::string_view(Line).substr(0, Tab1) == Name)
            {
                const std::size_t Tab2 = Line.find('\t', Tab1 + 1);
                if (Tab2 != std::string::npos) return Line.substr(Tab2 + 1);
            }
        }
        return {};
    }
    // A stable, deterministic C++ variable name for a given (NodeId, OutputIndex) pin - masked to
    // 24 bits purely for readability in the generated source, collisions are not a real concern for
    // a single small test graph.
    static std::string CppVar(std::uint64_t NodeId, int OutputIndex)
    {
        return std::format("v{:x}_{}", NodeId & 0xffffff, OutputIndex);
    }
    static void EmitOrdinaryNode(node_instance& Node, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, std::set<std::uint64_t>& EmittedNodeIds, std::string& Out);
    // Codegen's own mirror of the interpreter's EnsureNodeRun - see IsPullableNodeType's own comment
    // for exactly which node types are (and are never) safe to pull. Emits the source's declaration
    // directly into Out, the SAME accumulator the caller is about to append its own line into - since
    // this runs to completion before that caller's own `Out += ...` executes, the pulled dependency's
    // declaration always lands immediately BEFORE the statement that needed it, which is the only
    // place C++'s declare-before-use rule allows it to go.
    static void EnsureNodeEmitted(std::uint64_t NodeId, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, std::set<std::uint64_t>& EmittedNodeIds, std::string& Out, int PullDepth)
    {
        if (PullDepth > 64 || EmittedNodeIds.count(NodeId)) return; // cycle bailout, or already done
        node_instance* pNode = FindNodeById(NodeId, Nodes);
        if (!pNode || !pNode->m_pNode) return;
        if (!IsPullableNodeType(pNode->m_pNode->m_pFactory->getName())) return;
        EmitOrdinaryNode(*pNode, Nodes, Links, EmittedNodeIds, Out);
    }
    // Whatever C++ expression currently feeds an input pin - the variable name for whatever's wired
    // to it, or, if nothing's wired, the same inline-literal-on-unconnected-pin value the interpreter
    // now also honors (see GetInputValue's own comment) - formatted as a real C++ literal token
    // matching the pin's effective type, rather than always silently falling back to 0.0f regardless
    // of what's typed in.
    //
    // A wire's source that hasn't been emitted yet gets PULLED in (EnsureNodeEmitted) rather than
    // treated as a hard "0.0f" miss - mirrors PullInputValue's own interpreter-side behavior: a data
    // wire is a real dependency, not a requirement that the producer happen to sit somewhere the flat
    // spine walk already reaches. Only a genuinely unpullable source (Exec-gated, scope-owning, or a
    // real cyclic dependency) still falls back to "0.0f", matching that same source's interpreter-
    // side null result.
    static std::string CppInputExpr(std::uint64_t NodeId, int InputIndex, std::vector<link_instance>& Links, std::vector<node_instance>& Nodes, std::set<std::uint64_t>& EmittedNodeIds, std::string& Out, int PullDepth = 0)
    {
        node_instance* pNode = FindNodeById(NodeId, Nodes);
        const auto Inputs = (pNode && pNode->m_pNode) ? pNode->m_pNode->getInputs() : std::span<const xnode_os_port_desc>{};

        // Guid-aware resolution (see ResolvePortIndex's own comment) - otherwise a pin reordered in a
        // variable-arity node's Inputs (Function/NodeBuilder) after this link was made would generate
        // code that reads the wrong upstream variable, same failure mode as the interpreter's
        // PullInputValue before it got the same fix.
        for (auto& L : Links)
            if (L.m_TargetNode == NodeId && ResolveTargetIndex(L, Inputs) == InputIndex)
            {
                if (!EmittedNodeIds.count(L.m_SourceNode))
                    EnsureNodeEmitted(L.m_SourceNode, Nodes, Links, EmittedNodeIds, Out, PullDepth);
                if (EmittedNodeIds.count(L.m_SourceNode))
                {
                    node_instance* pSrcNode = FindNodeById(L.m_SourceNode, Nodes);
                    const auto SourceOutputs = (pSrcNode && pSrcNode->m_pNode) ? pSrcNode->m_pNode->getOutputs() : std::span<const xnode_os_port_desc>{};
                    return CppVar(L.m_SourceNode, ResolveSourceIndex(L, SourceOutputs));
                }
                return "0.0f";
            }

        // Same-named reflected property (see FindMemberByName/ResolveUnconnectedLiteral's own
        // comment) - read directly and already correctly typed. Every node type with a literal-
        // editable pin declares one, so a miss here just means genuinely nothing is there.
        if (pNode && pNode->m_pNode)
        {
            if (InputIndex < (int)Inputs.size())
            {
                if (auto* pMember = FindMemberByName(pNode->m_pNode->getProperties(), Inputs[InputIndex].m_pName))
                {
                    xproperty::any Out; xproperty::settings::context Ctx;
                    if (pMember->TryRead(pNode->m_pNode, Out, Ctx))
                    {
                        if (Out.is<bool>())         return Out.get<bool>() ? "true" : "false";
                        if (Out.is<std::int32_t>()) return std::to_string(Out.get<std::int32_t>());
                        if (Out.is<std::int16_t>()) return std::to_string(Out.get<std::int16_t>());
                        // Float, and the default for anything else - same "{:#}" round-trip Constant's
                        // own literal emission uses, so a whole number ("1") doesn't produce "1f".
                        if (Out.is<float>())        return std::format("{:#}f", Out.get<float>());
                    }
                }
            }
        }

        return "0.0f";
    }
    static void EmitOrdinaryNode(node_instance& Node, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, std::set<std::uint64_t>& EmittedNodeIds, std::string& Out)
    {
        const auto Name = Node.m_pNode->m_pFactory->getName();
        if (Name == "Constant")
        {
            // A literal's value is known at codegen time - nothing to call, just substitute the
            // number directly. The one ordinary node type in this test graph with no real logic to
            // share (there's no logic, only a value).
            const std::string Snapshot = SerializePropertiesToString(Node.m_pNode);
            const std::string TypeText = ReadStringPropertyFromSnapshot(Snapshot, "Type");
            const int TypeVal = TypeText.empty() ? 0 : std::atoi(TypeText.c_str()); // const_type: 0=Float,1=Int,2=Short,3=Bool
            // constant_node.cpp reflects one properly-typed member per Type (Value Float/Value Int/
            // Value Short/Value Bool - see that file) rather than one shared string, so which
            // property to read now depends on Type too, same as the canvas's own inline editor does.
            const char* pValueName = TypeVal == 3 ? "Value Bool" : TypeVal == 1 ? "Value Int" : TypeVal == 2 ? "Value Short" : "Value Float";
            const std::string ValueText = ReadStringPropertyFromSnapshot(Snapshot, pValueName);
            // Round-tripped through a real float parse/format rather than appending an "f" suffix
            // straight onto whatever text was stored - a whole-number value like "0" has no decimal
            // point, so a bare textual "f" suffix produces the syntactically invalid literal "0f".
            // "{:#}" forces a decimal point even for a whole number (std::format's float '#' flag,
            // same meaning as printf's) - plain "{}" formats 0.0f as "0", which a bare "f" suffix
            // would turn into the invalid literal "0f" instead of the valid "0.f". Codegen only ever
            // emits a float literal regardless of Type - a pre-existing limitation shared with every
            // other scalar pin in this corpus (see DisplayTypeText's own note on why there's no
            // separate Int/Short pin type) - so Bool reads back as 0.0f/1.0f rather than `false`/`true`.
            const float Value = (TypeVal == 3)
                ? ((ValueText == "1" || ValueText == "true") ? 1.0f : 0.0f)
                : std::strtof(ValueText.empty() ? "0" : ValueText.c_str(), nullptr);
            Out += std::format("    const float {} = {:#}f;\n", CppVar(Node.m_Id, 0), Value);
        }
        else if (Name == "Print")
        {
            Out += std::format("    std::printf(\"%.2f\\n\", {});\n", CppInputExpr(Node.m_Id, 0, Links, Nodes, EmittedNodeIds, Out));
        }
        else if (Name == "Compare")
        {
            // Mirrors compare_node.cpp's own Execute() switch, but working from the Operator
            // property's raw serialized form - ReflectedMemberToRow stores an enum as
            // ReadEnumAsInt's numeric value ("0".."5"), never the display name, so this indexes the
            // same compare_op_v ordering by number rather than matching against enum item text.
            const std::string Op = ReadStringPropertyFromSnapshot(SerializePropertiesToString(Node.m_pNode), "Operator");
            const char* pToken = "!=";
            switch (Op.empty() ? 0 : std::atoi(Op.c_str()))
            {
                case 0: pToken = ">";  break; // GREATER
                case 1: pToken = "<";  break; // LESS
                case 2: pToken = "=="; break; // EQUAL
                case 3: pToken = "!="; break; // NOT_EQUAL
                case 4: pToken = ">="; break; // GREATER_OR_EQUAL
                case 5: pToken = "<="; break; // LESS_OR_EQUAL
            }
            Out += std::format("    const bool {} = ({} {} {});\n", CppVar(Node.m_Id, 0), CppInputExpr(Node.m_Id, 0, Links, Nodes, EmittedNodeIds, Out), pToken, CppInputExpr(Node.m_Id, 1, Links, Nodes, EmittedNodeIds, Out));
        }
        else if (Name == "Math Expression")
        {
            // Mirrors math_expression_node.cpp's own Execute() switch (same raw-serialized-enum
            // indexing Compare uses above) - the REVERSE variants swap which operand prints on which
            // side of the operator rather than just picking a different token, so this builds the
            // whole expression per case instead of substituting one shared token into a fixed shape.
            const std::string Op = ReadStringPropertyFromSnapshot(SerializePropertiesToString(Node.m_pNode), "Operator");
            const std::string A = CppInputExpr(Node.m_Id, 0, Links, Nodes, EmittedNodeIds, Out);
            const std::string B = CppInputExpr(Node.m_Id, 1, Links, Nodes, EmittedNodeIds, Out);
            std::string Expr;
            switch (Op.empty() ? 0 : std::atoi(Op.c_str()))
            {
                case 0: Expr = std::format("({} + {})", A, B); break; // ADD
                case 1: Expr = std::format("({} - {})", A, B); break; // SUBTRACT
                case 2: Expr = std::format("({} - {})", B, A); break; // SUBTRACT_REVERSE
                case 3: Expr = std::format("({} * {})", A, B); break; // MULTIPLY
                case 4: Expr = std::format("({} / {})", A, B); break; // DIVIDE
                case 5: Expr = std::format("({} / {})", B, A); break; // DIVIDE_REVERSE
                default: Expr = "0.0f"; break;
            }
            Out += std::format("    const float {} = {};\n", CppVar(Node.m_Id, 0), Expr);
        }
        else if (Name == "Sin" || Name == "Cos" || Name == "Tan")
        {
            // Mirrors {sin,cos,tan}_node.cpp's own Execute() exactly - a single std::<fn> call, no
            // enum/operator to switch on the way Compare/Math Expression need (trig_node.cpp's three
            // types are otherwise identical in shape, see that file's own comment on why they're not
            // one templated node).
            const char* pFn = Name == "Sin" ? "sin" : Name == "Cos" ? "cos" : "tan";
            Out += std::format("    const float {} = std::{}({});\n", CppVar(Node.m_Id, 0), pFn, CppInputExpr(Node.m_Id, 0, Links, Nodes, EmittedNodeIds, Out));
        }
        else if (Name == "Random")
        {
            // Mirrors random_node.cpp's own Execute() - Min/Max order-independent (a std::rand()-based
            // std::uniform_real_distribution stand-in, not literally the same RNG algorithm or stream
            // as the interpreter's per-instance std::mt19937; Random is non-deterministic either way,
            // so bit-identical output between codegen and the interpreter was never a meaningful bar,
            // just "a real random value in the right range").
            const std::string Min = CppInputExpr(Node.m_Id, 0, Links, Nodes, EmittedNodeIds, Out);
            const std::string Max = CppInputExpr(Node.m_Id, 1, Links, Nodes, EmittedNodeIds, Out);
            Out += std::format("    const float {0}_lo = std::min({1}, {2}), {0}_hi = std::max({1}, {2});\n", CppVar(Node.m_Id, 0), Min, Max);
            Out += std::format("    const float {0} = {0}_lo + ({0}_hi - {0}_lo) * (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX));\n", CppVar(Node.m_Id, 0));
        }
        else if (Name == "Clamp")
        {
            // Mirrors clamp_node.cpp's own Execute() - Min/Max order-independent by construction
            // (std::clamp itself is UB when Min > Max, exactly why that file does this by hand too).
            const std::string Value = CppInputExpr(Node.m_Id, 0, Links, Nodes, EmittedNodeIds, Out);
            const std::string Min   = CppInputExpr(Node.m_Id, 1, Links, Nodes, EmittedNodeIds, Out);
            const std::string Max   = CppInputExpr(Node.m_Id, 2, Links, Nodes, EmittedNodeIds, Out);
            Out += std::format("    const float {0}_lo = std::min({1}, {2}), {0}_hi = std::max({1}, {2});\n", CppVar(Node.m_Id, 0), Min, Max);
            Out += std::format("    const float {0} = std::min(std::max({1}, {0}_lo), {0}_hi);\n", CppVar(Node.m_Id, 0), Value);
        }
        else
        {
            Out += std::format("    // codegen for '{}' not implemented yet\n", Name);
        }
        // Marks this node reachable/emitted for CppInputExpr's own "is my wired source real"
        // check - mirrors the interpreter's m_bHasRun, but kept as a SEPARATE set rather than
        // reusing that field: codegen and the interpreter can both run in the same process (the
        // self-test does exactly this, back to back, on the same Nodes), and m_bHasRun already has
        // its own real meaning there - overloading it here would make each pass corrupt the other's
        // bookkeeping.
        EmittedNodeIds.insert(Node.m_Id);
    }
    static void EmitSpineRange(std::uint64_t SpineId, int FromOrderInclusive, int ToOrderExclusive, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, std::set<std::uint64_t>& EmittedNodeIds, std::string& Out, std::string& FunctionDefs);
    static void EmitExecTarget(std::uint64_t TargetNodeId, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, std::set<std::uint64_t>& EmittedNodeIds, std::string& Out, std::string& FunctionDefs);
    static void EmitExecutionCall(node_instance& Caller, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, std::set<std::uint64_t>& EmittedNodeIds, std::string& Out, std::string& FunctionDefs)
    {
        // No "already emitted" guard the way RunExecutionCall has m_bHasRun - a node reached by more
        // than one path would get emitted (and its variable re-declared) more than once, a real
        // limitation this first pass doesn't handle; the current test graph is tree-shaped so it
        // never comes up.
        for (auto& L : Links)
            if (L.m_SourceNode == Caller.m_Id)
                EmitExecTarget(L.m_TargetNode, Nodes, Links, EmittedNodeIds, Out, FunctionDefs);
    }
    static void EmitExecTarget(std::uint64_t TargetNodeId, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, std::set<std::uint64_t>& EmittedNodeIds, std::string& Out, std::string& FunctionDefs)
    {
        node_instance* pTarget = FindNodeById(TargetNodeId, Nodes);
        if (!pTarget || !pTarget->m_pNode) return;
        const auto Name = pTarget->m_pNode->m_pFactory->getName();

        if (Name == "Function")
        {
            const auto Inputs  = pTarget->m_pNode->getInputs();
            const auto Outputs = pTarget->m_pNode->getOutputs();
            int ExternalOutputCount = 0;
            for (auto& O : Outputs) if (!O.m_bLocalScope && IsRealDataPort(O)) ++ExternalOutputCount;

            // Declared (external, non-local, non-Exec) inputs become real C++ parameters - named
            // the SAME as the matching local-mirror OUTPUT (see this block's own top comment for why
            // that one naming choice is what makes the body's "read my parameter" links just work
            // with no special-casing at all).
            std::string Params;
            int ParamCount = 0;
            for (int i = 0; i < (int)Inputs.size(); ++i)
            {
                if (Inputs[i].m_bLocalScope || !IsRealDataPort(Inputs[i])) continue;
                if (!Params.empty()) Params += ", ";
                Params += std::format("float {}", CppVar(pTarget->m_Id, ExternalOutputCount + ParamCount));
                ++ParamCount;
            }

            // Marked emitted BEFORE the body below, not after - the body reads its own parameters
            // through Function's own local-mirror OUTPUT slots (same node id, different output
            // index), so CppInputExpr's "is my wired source real" check needs pTarget->m_Id already
            // in the set by the time the body's own Print/etc. resolve those links. Mirrors the
            // interpreter's RunExecTarget, which sets pTarget->m_bHasRun = true before its own
            // RunSpineRange(body) call for the exact same reason.
            EmittedNodeIds.insert(pTarget->m_Id);

            std::string Body;
            auto* pEnd = FindNodeById(pTarget->m_OwnedEndId, Nodes);
            const int EndOrder = pEnd ? pEnd->m_Order : INT_MAX;
            EmitSpineRange(pTarget->m_SpineId, pTarget->m_Order + 1, EndOrder, Nodes, Links, EmittedNodeIds, Body, FunctionDefs);

            // Whatever's wired into the local Result-mirror INPUT becomes the return expression -
            // only the first declared output is handled today, matching the interpreter's own scope.
            // Found by SCANNING for the first local-scope Input, not by deriving an offset from a
            // count - Inputs is [Exec][external params...][local Result-mirror...], so the local
            // group's start shifted by one the moment Exec became the first declared input (see
            // RunExecTarget's own identical fix, same root cause).
            int FirstLocalInputIdx = (int)Inputs.size();
            for (int i = 0; i < (int)Inputs.size(); ++i) if (Inputs[i].m_bLocalScope) { FirstLocalInputIdx = i; break; }
            std::string ReturnExpr = "0.0f";
            for (int i = 0; i < (int)Outputs.size(); ++i)
            {
                if (Outputs[i].m_bLocalScope || !IsRealDataPort(Outputs[i])) continue;
                ReturnExpr = CppInputExpr(pTarget->m_Id, FirstLocalInputIdx, Links, Nodes, EmittedNodeIds, Body);
                break;
            }

            const std::string FnName = std::format("Fn_{:x}", pTarget->m_Id & 0xffffff);
            FunctionDefs += std::format("static float {}({})\n{{\n{}    return {};\n}}\n\n", FnName, Params, Body, ReturnExpr);

            std::string Args;
            for (int i = 0; i < (int)Inputs.size(); ++i)
            {
                if (Inputs[i].m_bLocalScope || !IsRealDataPort(Inputs[i])) continue;
                if (!Args.empty()) Args += ", ";
                Args += CppInputExpr(pTarget->m_Id, i, Links, Nodes, EmittedNodeIds, Out);
            }
            Out += std::format("    const float {} = {}({});\n", CppVar(pTarget->m_Id, 0), FnName, Args);
        }
        else if (Name == "Execute")
        {
            // No owned scope - maps directly onto a C++ lambda captured by reference, exactly the
            // "invoking a lambda, not calling a real function" distinction NODE_SCRIPTING_DESIGN.md
            // §12.2 draws - its body just runs inline, reading/writing whatever's already in scope.
            std::string Body, Unused;
            EmitSpineRange(pTarget->m_SpineId, pTarget->m_Order + 1, INT_MAX, Nodes, Links, EmittedNodeIds, Body, Unused);
            Out += std::format("    [&]() {{\n{}    }}();\n", Body);
        }
    }
    static void EmitSpineRange(std::uint64_t SpineId, int FromOrderInclusive, int ToOrderExclusive, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, std::set<std::uint64_t>& EmittedNodeIds, std::string& Out, std::string& FunctionDefs)
    {
        std::vector<node_instance*> Members;
        for (auto& N : Nodes)
            if (N.m_SpineId == SpineId && N.m_Order >= FromOrderInclusive && N.m_Order < ToOrderExclusive)
                Members.push_back(&N);
        std::sort(Members.begin(), Members.end(), [](auto* A, auto* B) { return A->m_Order < B->m_Order; });
        for (std::size_t i = 0; i < Members.size(); ++i)
        {
            auto* pN = Members[i];
            // Skip anything already emitted - either pulled in early via EnsureNodeEmitted (a real,
            // if previously unexercised, gap: nothing stopped this positional walk from re-emitting
            // the same node's declaration a second time once it reached it) or pre-marked by
            // GenerateNodePluginCpp to exclude test-rig content wired to a NodeBuilder's own external
            // pins (see FindNodesWiredToExternalPins) - mirrors RunSpineRange's own m_bHasRun guard.
            if (!pN->m_pNode || EmittedNodeIds.count(pN->m_Id)) continue;
            const auto Name = pN->m_pNode->m_pFactory->getName();
            // NodeBuilder itself can't reach this walk at all - GenerateCpp refuses upfront if the
            // graph contains one (see its own top-of-function check) - so no case for it is needed
            // here, unlike OnEvent.
            if (Name == "End" || Name == "OnEvent") continue;
            if (Name == "ExecutionCall") { EmitExecutionCall(*pN, Nodes, Links, EmittedNodeIds, Out, FunctionDefs); continue; }
            if (Name == "Function" || Name == "Execute") continue;
            if (Name == "If" && pN->m_OwnedEndId != 0)
            {
                // Mirrors RunSpineRange's own "If" handling: a real C++ if(){} block, body bounded
                // by the same Order range the interpreter uses (this node's Order+1 up to its owned
                // End's Order) - not the flat, unconditional walk that used to emit this body's code
                // regardless of the (also unimplemented, until now) condition.
                auto* pEnd = FindNodeById(pN->m_OwnedEndId, Nodes);
                const int EndOrder = pEnd ? pEnd->m_Order : INT_MAX;
                std::string Body;
                EmitSpineRange(SpineId, pN->m_Order + 1, EndOrder, Nodes, Links, EmittedNodeIds, Body, FunctionDefs);
                Out += std::format("    if ({}) {{\n{}    }}\n", CppInputExpr(pN->m_Id, 0, Links, Nodes, EmittedNodeIds, Out), Body);
                while (i + 1 < Members.size() && Members[i + 1]->m_Order < EndOrder) ++i;
                continue;
            }
            EmitOrdinaryNode(*pN, Nodes, Links, EmittedNodeIds, Out);
        }
    }
    // The one entry point: same signature as RunProgram (no ImGui/xgpu dependency at all), so this
    // is directly testable without touching the GUI.
    static std::string GenerateCpp(std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::vector<spine>& Spines)
    {
        std::uint64_t RootSpineId = 0;
        for (auto& S : Spines) if (S.m_bIsRoot) { RootSpineId = S.m_Id; break; }
        std::string FunctionDefs, MainBody;
        // Tracks which nodes actually got emitted as the walk proceeds - a node physically moved
        // onto a spine nothing ever triggers (not the root spine, not jumped to by any Exec target)
        // never gets visited here, exactly like the interpreter's own m_bHasRun would never get set
        // for it - CppInputExpr checks this before referencing a wired source's variable, instead of
        // assuming every link's source is guaranteed to have been declared somewhere.
        std::set<std::uint64_t> EmittedNodeIds;
        if (RootSpineId != 0)
            EmitSpineRange(RootSpineId, 0, INT_MAX, Nodes, Links, EmittedNodeIds, MainBody, FunctionDefs);
        return "// Auto-generated by Node OS codegen - do not hand-edit\n#include <cstdio>\n#include <cstdlib>\n#include <cmath>\n#include <algorithm>\n\n"
             + FunctionDefs
             + "static void RunMain()\n{\n" + MainBody + "}\n\nint main()\n{\n    RunMain();\n    return 0;\n}\n";
    }

    //------------------------------------------------------------------------------------------------
    // NodeBuilder v1 - "compile a Node": a NodeBuilder node IS the definition (own InputsSpec/
    // OutputsSpec, own owned scope/body between itself and its own End - see node_builder_node.cpp),
    // never a reference to some other node elsewhere. This lowers that same node's own signature +
    // owned-scope body into a genuine plugin .cpp (the same ABI every hand-written
    // Plugins/<X>/x_node.cpp implements) - structurally the same body-lowering call EmitExecTarget's
    // "Function" branch already uses (EmitSpineRange over an owned scope), just wrapped in a plugin-
    // ABI harness (Inputs[i] casts up front, Outputs[i] writes at the end - multiple declared outputs
    // all written directly) instead of a free-function-pasted-into-someone-else's-program shape.
    //
    // v1 type support is Float/Bool only (the only two types anything in this codegen backend has
    // ever actually cast) - a pin declared "Int"/"Short"/"Any"/"Span<Any>" (all legal choices in
    // NodeBuilder's own pin editor - it reuses the exact same spec format Function's does) fails
    // generation with a clear, named diagnostic rather than emitting a cast that doesn't compile.
    //------------------------------------------------------------------------------------------------
    struct node_plugin_gen_result { bool m_bOk = false; std::string m_SourceOrError; };
    static node_plugin_gen_result GenerateNodePluginCpp(node_instance& Builder, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::string& ArtifactName)
    {
        node_plugin_gen_result Result;
        if (!Builder.m_pNode) { Result.m_SourceOrError = "NodeBuilder instance has no resolved plugin"; return Result; }

        const auto Inputs  = Builder.m_pNode->getInputs();  // external inputs + local-mirror-of-outputs
        const auto Outputs = Builder.m_pNode->getOutputs(); // external outputs + local-mirror-of-inputs

        // Only real, externally-wireable data ports become the generated node's OWN ports - every
        // local-scope mirror (body-internal only) is never part of what a CALLER of the generated
        // node sees.
        struct ext_port { std::string m_Name; std::string m_Type; bool m_bRequired; bool m_bReadOnly; };
        std::vector<ext_port> ExtInputs, ExtOutputs;
        for (auto& P : Inputs)  if (!P.m_bLocalScope && IsRealDataPort(P)) ExtInputs.push_back({ P.m_pName, P.m_pTypeName, P.m_bRequired, P.m_bReadOnly });
        for (auto& P : Outputs) if (!P.m_bLocalScope && IsRealDataPort(P)) ExtOutputs.push_back({ P.m_pName, P.m_pTypeName, P.m_bRequired, P.m_bReadOnly });

        const auto CppTypeOf = [](const std::string& T) -> const char*
        {
            if (T == "Float") return "float";
            if (T == "Bool")  return "bool";
            return nullptr;
        };
        for (auto& P : ExtInputs)  if (!CppTypeOf(P.m_Type)) { Result.m_SourceOrError = std::format("input '{}' has type '{}' - NodeBuilder v1 only supports Float/Bool", P.m_Name, P.m_Type); return Result; }
        for (auto& P : ExtOutputs) if (!CppTypeOf(P.m_Type)) { Result.m_SourceOrError = std::format("output '{}' has type '{}' - NodeBuilder v1 only supports Float/Bool", P.m_Name, P.m_Type); return Result; }
        if (ExtOutputs.empty()) { Result.m_SourceOrError = "NodeBuilder declares zero outputs - nothing for the generated node to produce"; return Result; }

        // Owns a scope (needsOwnedEndMarker, node_builder_node.cpp) - the body is [Order+1, End's
        // Order), same shape EmitExecTarget's own "Function" branch already uses. Test-rig content
        // (a Print reading the external Sum output for live testing, see RunNodeBuilderBody's own
        // comment for the full story) lives in the SAME spine but AFTER the End marker, so it's simply
        // outside this range - never swept into the compiled node, no exclusion list needed. This is
        // what makes "the interpreter always runs in test mode, the C++ version compiles the final
        // node without the test cases" structurally true, not true by accident because no test rig
        // happened to exist yet.
        std::set<std::uint64_t> EmittedNodeIds;
        EmittedNodeIds.insert(Builder.m_Id); // body may read Builder's own local-mirror outputs
        auto* pEnd = FindNodeById(Builder.m_OwnedEndId, Nodes);
        const int EndOrder = pEnd ? pEnd->m_Order : INT_MAX;
        std::string Body, UnusedFunctionDefs;
        EmitSpineRange(Builder.m_SpineId, Builder.m_Order + 1, EndOrder, Nodes, Links, EmittedNodeIds, Body, UnusedFunctionDefs);

        // Prologue: bind each external input's local name (the SAME CppVar(Builder.m_Id, ...) name the
        // body above already references) to a cast read from this node's OWN Inputs[i].
        std::string Prologue;
        for (int i = 0; i < (int)ExtInputs.size(); ++i)
            Prologue += std::format("    const {0} {1} = *static_cast<const {0}*>(Inputs[{2}]);\n", CppTypeOf(ExtInputs[i].m_Type), CppVar(Builder.m_Id, (int)ExtOutputs.size() + i), i);

        // Epilogue: every declared output gets its own Outputs[i] write - the J-th external output
        // (from Builder's own m_OutputsSpec) and the J-th local-mirror INPUT (decoded from that same
        // spec, in the same order - see node_builder_node.cpp's Rebuild) are the same pin, so the
        // return expression for output J is just Builder's own local-mirror input at that same
        // relative index. No leading Exec pin to skip - Builder.getInputs() is [external inputs...]
        // [local mirrors...] directly (see node_builder_node.cpp's own top comment for why NodeBuilder
        // has no Exec pin at all). Matches the ABI's actual convention (Random/MathExpression, not
        // something this generator invents): Outputs[i] starts null, Execute() mallocs its own storage
        // and assigns the pointer, FreeOutputs() frees it - writing straight through a caller-owned
        // Outputs[i] would assume storage nothing in this ABI ever pre-allocates (confirmed the hard
        // way: a null-pointer write segfaulted the very first end-to-end test of this code).
        const int FirstLocalInputIdx = (int)ExtInputs.size();
        std::string Epilogue, FreeOutputsBody;
        for (int j = 0; j < (int)ExtOutputs.size(); ++j)
        {
            const std::string Expr = CppInputExpr(Builder.m_Id, FirstLocalInputIdx + j, Links, Nodes, EmittedNodeIds, Body);
            const char* T = CppTypeOf(ExtOutputs[j].m_Type);
            Epilogue += std::format("    {{ auto* p = static_cast<{0}*>(std::malloc(sizeof({0}))); *p = {1}; Outputs[{2}] = p; }}\n", T, Expr, j);
            FreeOutputsBody += std::format("            std::free(Outputs[{}]);\n", j);
        }

        // Each entry gets its own xresource::guid_generator::Instance64() call, evaluated once per
        // NODE INSTANCE the moment its m_Inputs/m_Outputs member is default-constructed - matching
        // every other node type's own per-instance port guid (see xnode_os_port_desc::m_Guid's own
        // comment). A generated node's pin list is fixed for life, but link_instance no longer stores
        // an index at all, so even a fixed pin needs a real, non-zero guid for a link to reference.
        std::string InputDescs, OutputDescs;
        for (auto& P : ExtInputs)  InputDescs  += std::format("{{ \"{}\", \"{}\", true, true, false, xresource::guid_generator::Instance64() }}, ", P.m_Name, P.m_Type);
        for (auto& P : ExtOutputs) OutputDescs += std::format("{{ \"{}\", \"{}\", true, true, false, xresource::guid_generator::Instance64() }}, ", P.m_Name, P.m_Type);

        // One always-saved, DONT_SHOW-only dummy property is deliberately NOT needed here, unlike
        // Random/Clamp/Trig - a GENERATED node has zero user-facing properties by construction, but
        // that's fine: nothing about the zero-properties adjacency bug (see
        // xgpu_zero_properties_adjacency_bug) depended on a node HAVING properties, only on whether
        // HasAnyProperties() is stable for a type across saves - a type that always, unconditionally
        // has zero properties is exactly as stable/distinguishable as one that always has some; the
        // bug was specifically about a type whose HasAnyProperties() answer could flip between saves
        // (DONT_SAVE-only members hidden by wiring state). A generated node's port list is frozen at
        // generation time, so this can never happen.
        const std::string StructName = ArtifactName + "_node";
        Result.m_SourceOrError = std::format(
            "// Auto-generated by NodeBuilder '{0}' - do not hand-edit; re-run NodeBuilder instead\n"
            "#include \"../../SDK/xnode_os_plugin_api.h\"\n"
            "#include \"../../SDK/xnode_os_shared_types.h\"\n"
            "#include \"dependencies/xresource_guid/source/xresource_guid.h\"\n"
            "#include <cmath>\n#include <algorithm>\n#include <cstdlib>\n\n"
            "namespace\n{{\n"
            "    struct {1} : xnode_os_node\n"
            "    {{\n"
            "        XPROPERTY_VDEF(\"{1}\", {1})\n\n"
            "        xnode_os_port_desc m_Inputs[]  = {{ {2} }};\n"
            "        xnode_os_port_desc m_Outputs[] = {{ {3} }};\n"
            "        std::span<const xnode_os_port_desc> getInputs()  const noexcept override {{ return m_Inputs; }}\n"
            "        std::span<const xnode_os_port_desc> getOutputs() const noexcept override {{ return m_Outputs; }}\n"
            "        void Execute(void** Inputs, void** Outputs) noexcept override\n"
            "        {{\n{4}{5}{6}        }}\n"
            "        void FreeOutputs(void** Outputs) noexcept override\n"
            "        {{\n{8}        }}\n"
            "    }};\n}}\n"
            "XPROPERTY_VREG({1})\n\n"
            "namespace\n{{\n"
            "    struct {1}_factory : xnode_os_node_factory\n"
            "    {{\n"
            "        XPROPERTY_VDEF(\"{1}_factory\", {1}_factory)\n"
            "        std::string_view getVersion()  const noexcept override {{ return \"1.0\"; }}\n"
            "        std::string_view getName()     const noexcept override {{ return \"{7}\"; }}\n"
            "        std::string_view getCategory() const noexcept override {{ return \"Generated\"; }}\n"
            "        xnode_os_node& CreateNodeInstance() override {{ auto* p = new {1}(); p->m_pFactory = this; return *p; }}\n"
            "        void DestroyNodeInstance(xnode_os_node& N) override {{ delete static_cast<{1}*>(&N); }}\n"
            "    }};\n}}\n"
            "XPROPERTY_VREG({1}_factory)\n\n"
            "extern \"C\" XNODE_OS_EXPORT xnode_os_node_factory& NodeOS_CreateFactory(ixnode_os_host&) noexcept {{ return *new {1}_factory(); }}\n"
            "extern \"C\" XNODE_OS_EXPORT void NodeOS_DestroyFactory(xnode_os_node_factory& F) noexcept {{ delete static_cast<{1}_factory*>(&F); }}\n"
            , ArtifactName, StructName, InputDescs, OutputDescs, Prologue, Body, Epilogue, ArtifactName, FreeOutputsBody);
        Result.m_bOk = true;
        return Result;
    }

    // A-Z/a-z/0-9/_ only, and never starting with a digit - ArtifactName becomes a C++ struct name
    // (via GenerateNodePluginCpp's own "_node"/"_factory" suffixing) AND a Plugins/<...>/ folder name,
    // so it has to be valid as both at once.
    static std::string SanitizeArtifactName(const std::string& Raw)
    {
        std::string Out;
        for (char C : Raw) if (std::isalnum((unsigned char)C) || C == '_') Out += C;
        if (Out.empty() || std::isdigit((unsigned char)Out[0])) Out = "Generated" + Out;
        return Out;
    }

    // Orchestrates the whole "compile a Node" pipeline for one NodeBuilder instance: NodeBuilder IS
    // the definition (no separate node to look up), lower+generate (GenerateNodePluginCpp above),
    // write the result under a new Plugins/<Name>/ folder, and run it through the exact same
    // compile-and-load path a hand-written plugin's "Compile & Load" button already uses
    // (CompileAndLoadPlugin) - so a NodeBuilder-produced type is, from that point on, indistinguishable
    // from one a human wrote by hand. Re-running against an already-published Name reuses the same
    // Sources entry (found by DirName) rather than creating a duplicate - MergeCompileResult already
    // knows how to prune a re-registering module's stale entries first.
    //
    // Enforces the "a graph has exactly one purpose" rule: a graph mixing NodeBuilder with OnEvent (a
    // program-purpose marker), or declaring more than one NodeBuilder, can't say what it's FOR - see
    // node_builder_node.cpp's own top comment. Checked here, not at Load/Save time, since an in-
    // progress graph mid-transition between purposes is not itself an error - only actually trying to
    // USE it (build a node, or run it as a program - see RunProgram/GenerateCpp's matching check) is.
    static std::string BuildNodeFromFunction(node_instance& Builder, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, std::vector<plugin_source_entry>& Sources, std::vector<available_node_type>& AvailableTypes, const std::vector<spine>& Spines, const std::vector<column>& Columns)
    {
        if (!Builder.m_pNode) return "BuildNode: NodeBuilder instance has no resolved plugin";

        const bool bHasOnEvent = std::any_of(Nodes.begin(), Nodes.end(), [](auto& N) { return N.m_pNode && N.m_pNode->m_pFactory->getName() == "OnEvent"; });
        if (bHasOnEvent) return "BuildNode: graph also contains an OnEvent node - a graph is either a program (OnEvent) or a node definition (NodeBuilder), never both";

        const int NodeBuilderCount = (int)std::count_if(Nodes.begin(), Nodes.end(), [](auto& N) { return N.m_pNode && N.m_pNode->m_pFactory->getName() == "NodeBuilder"; });
        if (NodeBuilderCount > 1) return std::format("BuildNode: graph contains {} NodeBuilder nodes - a graph defines exactly one node", NodeBuilderCount);

        if (Builder.m_OwnedEndId == 0) return "BuildNode: NodeBuilder has no owned End marker (malformed)";

        const auto ReadStringProp = [](xnode_os_node* pNode, const char* pName) -> std::string
        {
            auto* pMember = FindMemberByName(pNode->getProperties(), pName);
            if (!pMember) return {};
            xproperty::any Out; xproperty::settings::context Ctx;
            if (pMember->TryRead(pNode, Out, Ctx) && Out.is<std::string>()) return Out.get<std::string>();
            return {};
        };

        const std::string ArtifactName = SanitizeArtifactName(ReadStringProp(Builder.m_pNode, "Name"));

        auto Gen = GenerateNodePluginCpp(Builder, Nodes, Links, ArtifactName);
        if (!Gen.m_bOk) return "BuildNode: generation failed - " + Gen.m_SourceOrError;

        namespace fs = std::filesystem;
        const fs::path Dir  = fs::path("D:/LIONant/xGPU/source/Examples/E27_NodeOS/Plugins") / ArtifactName;
        const fs::path Path = Dir / (ArtifactName + "_node.cpp");
        std::error_code Ec;
        fs::create_directories(Dir, Ec);
        { std::ofstream Out(Path, std::ios::trunc); Out << Gen.m_SourceOrError; }

        // The graph source lives right next to the artifact it produced - not a separate provenance-
        // tracking system (no new state to keep in sync, no risk of it drifting from what actually
        // got compiled), just SaveGraph writing to a name-addressable location, same as everything
        // else NodeBuilder already does by convention. This is what a future "open this node, see (and
        // recompile) its own defining graph" feature would key off - saved even if the compile below
        // fails, so a broken attempt is still inspectable/fixable from exactly where it was published.
        SaveGraph((Dir / (ArtifactName + "_graph.txt")).string(), Nodes, Links, AvailableTypes, Spines, Columns);

        // Reuse an existing Sources entry for this Name if NodeBuilder already published it once
        // before (so a second run recompiles/replaces the same plugin, rather than accumulating
        // duplicate entries every time) - otherwise this is a brand-new, first-time publish.
        plugin_source_entry* pEntry = nullptr;
        for (auto& S : Sources) if (S.m_DirName == ArtifactName) { pEntry = &S; break; }
        if (!pEntry)
        {
            Sources.push_back({ .m_DisplayName = ArtifactName, .m_SourcePath = Path.string(), .m_DirName = ArtifactName });
            pEntry = &Sources.back();
        }
        pEntry->m_SourcePath = Path.string();

        if (!CompileAndLoadPlugin(*pEntry, AvailableTypes))
            return std::format("BuildNode: compile failed for '{}' - see log:\n{}", ArtifactName, pEntry->m_CompileLog);

        return std::format("BuildNode: published '{}' - {}", ArtifactName, pEntry->m_CompileLog);
    }

    // Writes the generated source to disk, compiles it into a genuinely standalone .exe (no /LD,
    // no PCH, no SDK include paths at all - unlike a plugin, generated code has zero dependency on
    // xnode_os_plugin_api.h or anything else in this project, which is the whole point of "real
    // native codegen"), then runs it and captures its actual stdout - the concrete, checkable proof
    // that the pipeline produces a real program with the expected behavior, not just plausible-
    // looking text. Reuses GetOrBuildVsEnvSetup/CompilerInvocationMutex, the exact same toolchain
    // plumbing plugin compiles already use.
    struct codegen_run_result { bool m_bCompileOk = false; bool m_bRanOk = false; std::string m_CompileLog; std::string m_RunOutput; std::string m_SourcePath; };
    static codegen_run_result CompileAndRunGeneratedCpp(const std::string& Source)
    {
        codegen_run_result Result;
        namespace fs = std::filesystem;
        const fs::path OutputDir = "D:/LIONant/xGPU/source/Examples/E27_NodeOS/CompiledPlugins";
        std::error_code Ec;
        fs::create_directories(OutputDir, Ec);

        const fs::path SrcPath = OutputDir / "_generated_program.cpp";
        const fs::path ExePath = OutputDir / "_generated_program.exe";
        const fs::path BatPath = OutputDir / "_generated_program_compile.bat";
        const fs::path LogPath = OutputDir / "_generated_program_compile.log";
        const fs::path RunOutPath = OutputDir / "_generated_program_run.log";
        Result.m_SourcePath = SrcPath.string();

        { std::ofstream Src(SrcPath); Src << Source; }
        {
            std::ofstream Bat(BatPath);
            Bat << "@echo off\r\n";
            Bat << GetOrBuildVsEnvSetup();
            // No /LD (a real EXE, not a DLL), no /Yu/FI/Fp (no PCH - generated code only ever
            // includes plain standard headers), no /I at all (no SDK/xproperty dependency whatsoever
            // - the entire point of this being "real native codegen" rather than "another plugin").
            Bat << "cl.exe /nologo /EHsc /std:c++20 /MDd \"" << SrcPath.string() << "\""
                   " /Fe:\"" << ExePath.string() << "\" /Fo:\"" << (OutputDir / "_generated_program.obj").string()
                << "\" > \"" << LogPath.string() << "\" 2>&1\r\n";
        }

        int ExitCode;
        { std::lock_guard Lock(CompilerInvocationMutex()); ExitCode = std::system(std::format("\"{}\"", BatPath.string()).c_str()); }

        { std::ifstream LogFile(LogPath); std::stringstream S; S << LogFile.rdbuf(); Result.m_CompileLog = S.str(); }
        Result.m_bCompileOk = (ExitCode == 0) && fs::exists(ExePath);
        if (!Result.m_bCompileOk)
        {
            Result.m_CompileLog += std::format("\n[compile failed, exit code {}]", ExitCode);
            return Result;
        }

        // Run it for real and capture its actual stdout to a file, rather than just trusting that a
        // clean compile means correct behavior. Wrapped in one EXTRA outer quote pair (on top of the
        // two inner pairs around each path) - cmd.exe's "strip the outer quotes" rule only fires when
        // the whole command line is a single quoted token; with two separately-quoted paths plus a
        // redirection in between, an unwrapped line gets misparsed ("The filename, directory name, or
        // volume label syntax is incorrect") even though the command is well-formed and the actual
        // .exe runs fine standalone - this is the standard fix for cmd.exe /c with 2+ quoted paths.
        const std::string RunCommand = std::format("\"\"{}\" > \"{}\" 2>&1\"", ExePath.string(), RunOutPath.string());
        const int RunExitCode = std::system(RunCommand.c_str());
        { std::ifstream RunFile(RunOutPath); std::stringstream S; S << RunFile.rdbuf(); Result.m_RunOutput = S.str(); }
        Result.m_bRanOk = (RunExitCode == 0);
        return Result;
    }
}
