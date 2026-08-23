# Node OS Scripting Language — Design Reference

Status: **frozen for implementation** (converged 2026-08-22, now on its third execution model — see
§9 for the full history of what changed and why, across all three pivots; §11, added 2026-08-23,
reconciles §2's typing rule with the live editor's wildcard-pin mechanism built since; §12, added
2026-08-23, brings exec pins back narrowly for spine/scope invocation and describes the first real
interpreter that runs a saved graph). This is the
authoritative reference for building the native-codegen visual scripting layer on top of E27 Node
OS. Update it in place as decisions change during implementation — do not let it drift out of sync
with the code, and do not re-litigate a frozen decision without recording why here.

## 0. Goal

Give E27 Node OS Blueprint/Shader-Graph-level *functionality* (control flow, variables, functions,
structs, math) using Unreal and Unity as reference bars for "what a node needs to do" — but with a
different execution model than either: **the graph generates real, compilable C++**, compiled via
the exact `cl.exe`-shelling pipeline this editor already uses for node-factory plugins, producing a
native DLL loaded back into the running editor. Not an interpreted VM, not a live type-inference
engine.

**Acceptance test for the whole design** (the thing that must remain true forever): *adding a new
command should require adding only a data-type/definition file — it should just work after that.*
Concretely: adding any new node type — a new math op, a new native call, a new control-flow shape
— must be pure data/asset work, zero compiler code changes. This turned out to be achievable for
**every** node type, including control flow itself (§4) — there is no closed set of "structural"
node categories the compiler hardcodes at all.

## 1. Two separate problems (do not conflate them)

1. **Node-type behavior**: how a node type declares its own pins and type rules. Solved with
   strict monomorphic typing — no wildcards, no live unification solver. If generics are ever
   wanted, model them as compile-time monomorphization (a generic façade resolving to a concrete
   node once connected types are known), never as an interactive constraint solver.
2. **Program execution**: how a graph of wired nodes becomes running code. Solved by the
   flat-spine, recursive-descent model in §4 — a spine (§3) is a single ordered list of nodes; "next
   thing to execute" is always just the next node in that list; a control-flow node (`If`,
   `ForEachLoop`) doesn't redirect to a *different* spine at all — it owns a closing marker node
   placed later in the *same* list, and the compiler recovers real nested C++ blocks (`if`/`for`,
   actual braces) by recursively consuming everything between a node and its own marker. No calls,
   no lambdas, no labels or jumps anywhere in the generated output.

## 2. Type system

- Every data pin has exactly one concrete type, identified by an `xresource::type_guid` — this
  engine's own existing strongly-typed GUID (a hashed `std::uint64_t`, see
  `dependencies/xresource_guid/source/xresource_guid.h`). Do not invent a parallel string-based
  type identity; reuse this.
- `Array<Float>` is itself a registered concrete type with its own `type_guid`, not a notation
  resolved elsewhere. Two "Array of Float" pins on unrelated node types must compare equal by that
  GUID.
- Data-edge validity is total and trivial:
  `src.Kind==DataOutput && dst.Kind==DataInput && src.TypeGuid==dst.TypeGuid && dst has no existing source`.
- No implicit coercion. A user who needs `Int -> Float` wires an explicit `IntToFloat` node.
- A semantic/content hash (§9.4) is a number (`std::uint64_t`), never a string like `"sha256:..."`.
  Reach for the concrete type a value actually is; do not default to `std::string` for identity or
  hash fields just because it is the path of least resistance.
- **This section's "no wildcards" is a compiler-boundary rule, not an editor rule** — the live
  editor built since this document was frozen has a generic/wildcard pin mechanism (`Any`,
  `Span<Any>`) for real UX reasons. See §11 for how that's reconciled: the wildcard never reaches
  the compiler, only the concrete type it resolved to.

## 3. Spines: the one thing that determines execution order

A spine (E27's own existing UI concept: a vertical, densely ordered chain of nodes) is now the
*entire* unit of sequencing. There is no separate exec-pin address space and no per-node "run this
next" wire for the ordinary case — a node's "next" is simply whatever's next in the same spine
(`Order + 1`). This is not a bounded exception to some larger rule anymore; it is the rule.

What stays *unchanged* from the earlier, more general "layout never determines semantics" framing:
**column** identity — which spine visually shares a column, and where — has zero effect on anything
generated. Only a spine's own internal `Order` matters, and it matters completely, on purpose:
that's what lets a plain sequence of statements read top-to-bottom with no wire between every
consecutive pair, the same way an ordinary function body needs no annotation between one line and
the next.

**Deliberately left open**: whether *multiple* spines (e.g. two spines stacked in one column) ever
have any relationship to each other at the compiler level — whether reaching the end of one could
ever mean "continue into whatever's below it in the same column" — is not decided. For now, a spine
is a self-contained unit; see §10.

## 4. Execution model: one flat spine, recursive-descent into real nested C++

This is the third execution model this document has gone through — see §9 for the full history,
including the flat label/`goto` draft and the spine-as-lambda/`$call` draft this replaces. Both
earlier drafts are worth reading once, specifically so their mistakes aren't quietly reintroduced.

**There is still no fixed vocabulary of node categories.** No `PureExpr`/`Statement`/`Branch`/`Loop`/
`Sequence` enum. Every node type is exactly the same shape: a fixed pin list plus **one codegen
template string**. What makes one node "a loop" and another "a plain statement" is entirely what its
own template text does — nothing the compiler needs to know about node types in advance. That
property has survived all three pivots unchanged.

### 4.1 The mechanism

A spine compiles by walking its nodes in `Order` and, for each one, checking whether it *owns* a
closing marker:

- **A plain node** (no owned marker) has its template substituted (`$id`, `$input[PinId]`) and
  emitted as-is, one statement, then the compiler moves to the next node.
- **An owner node** (`If`, `ForEachLoop`) has a template containing exactly one `$body` placeholder.
  The compiler finds the node's own closing marker — a dedicated `End` node placed later in the same
  spine — recursively compiles *everything strictly between* the owner and its marker as nested
  content, and splices that in at `$body`. Compilation then continues from right after the marker.
- **`If` specifically** may own an `End-Else` instead of a plain `End`. `End-Else` is itself an
  owner: it owns a further `End` of its own. Content between `If` and `End-Else` is the true branch;
  content between `End-Else` and its own `End` is the false branch; the compiler appends the literal
  `else { ... }` wrapper itself (there is no third node contributing that text — `End-Else` marks a
  position, nothing else).
- `End`/`End-Else` markers are **pure position markers** — they carry no template text at all and are
  never substituted or emitted; the compiler only ever reads *where* they sit in the spine.

**Braces are plain template text, not something the compiler synthesizes.** A node's template
already contains its own opening brace, any prologue statements, and its own closing brace — `$body`
just marks where the recursively-compiled nested content gets spliced in. This is deliberate: the
compiler never needs to know what kind of block a node is opening (a `for`, an `if`, anything else a
future node type invents) — it only ever needs to find one placeholder and one marker.

**Ownership is not an ordinary, freely-rewireable pin.** An owner's marker is created and destroyed
together with the owner — delete the `ForEachLoop`, its `End` goes with it, the same way deleting an
`If` deletes its own `End`/`End-Else`. This is the piece of the design closest to Scratch/Snap!'s
C-shaped "wraps its own contents" blocks, grafted onto an otherwise ordinary node-and-wire graph for
data — a deliberate hybrid, not an accident. Exactly how this ownership is represented in the on-disk
graph format and enforced by the editor (so a user can't detach or dangle a marker) is implementation
work still to be done — see §10.

### 4.2 Worked examples

`ForEachLoop` — two plain data pins, nothing else. No `In`, no `Body`, no `Completed`:

```ini
[NodeType]
    Guid  <hashed from "ForEachLoop">
[Pins]
    Pin { Id "Array"   Kind DataIn   Type <Array<Float> guid> }
    Pin { Id "Element" Kind DataOut  Type <Float guid>        }
[Template]
    "for (std::size_t __idx_$id = 0; __idx_$id < $input[Array].size(); ++__idx_$id) {\n"
    "  float Element_$id = $input[Array][__idx_$id];\n"
    "$body"
    "}\n"
```

`If` — one data pin, `Condition`. No `Then`/`Else`/`In` exec pins at all:

```ini
[NodeType]
    Guid  <hashed from "If">
[Pins]
    Pin { Id "Condition" Kind DataIn  Type <Bool guid> }
[Template]
    "if ($input[Condition]) {\n"
    "$body"
    "}\n"
```

`End` — the plain position marker. No pins, no template text (never emitted):

```ini
[NodeType]
    Guid  <hashed from "End">
[Pins]
[Template]
    ""
```

`End-Else` has the same shape as `End` (no pins, no template) — the compiler distinguishes it purely
by the graph fact that it is *itself* an owner (it owns a further `End`), not by anything on the node
type definition. Whether `End`/`End-Else` need to be genuinely different node types or the same type
used two different ways is an open implementation question (§10).

A hand-traced example — `ForEachLoop` over an array, summing elements greater than a threshold,
still incrementing a counter every iteration regardless, then doing something once after the whole
loop — compiles to (and this was verified by an actual compile-and-run test, not just reasoned
through):

```cpp
auto Array_1 = std::span<float>(g_TestArray, (size_t)g_TestArrayCount);
for (std::size_t __idx_2 = 0; __idx_2 < Array_1.size(); ++__idx_2) {
  float Element_2 = Array_1[__idx_2];
  bool Result_3 = (Element_2 > 2.5f);
  if (Result_3) {
    g_Sum += Element_2;
  }
  g_Count++;
}
g_Done = true;
```

Note `g_Count++` sits *outside* the `if` but *inside* the `for` — it's the node placed after the
`If`'s own `End` but before the `ForEachLoop`'s own `End` — and `g_Done = true` sits outside both,
after the loop's own `End`. Nesting, sequencing, and "what runs unconditionally vs. conditionally"
all fall directly out of node position plus marker ownership, with zero jump instructions or lambda
calls anywhere in the output.

### 4.3 Why this is safe

The original, more hardcoded draft (§9) protected against one node's malformed template corrupting
another's by keeping a closed, compiler-owned set of block-emitting `Pattern`s. This model gets an
equivalent guarantee for a different, simpler reason: **a missing, misplaced, or crossed-nesting
marker is a plain parse error**, caught by the compiler's own recursive descent (an owner whose
marker doesn't exist in the spine, or whose marker's index falls outside the range being compiled,
fails cleanly with a diagnostic naming the offending node) — never a case where the compiler silently
emits something that merely *looks* plausible. There is no shared, ambiguous jump-target space for a
bad graph to corrupt, because there's no jump-target space at all.

A node's own template may still freely use real C++ structure (braces, `for`, `if`, anything else) —
that was already true and remains true — but now that's the *entire* extent of what a template
controls; nesting between different nodes is the compiler's own recursive structure, not something
any node's template text can accidentally interfere with.

### 4.4 Locals and scope

A local declared by a plain node (`float Element_$id = ...;`) or by an owner's own prologue is an
ordinary C++ local, properly block-scoped by whatever real brace it's nested inside — a value
computed inside an `If`'s true branch is *not* visible to whatever runs after the whole `if`, exactly
like real C++, because it genuinely is real C++ block scope. This is stricter than both earlier
drafts: the flat label/goto model had no scoping at all (§9); the spine-as-lambda model gave scoping
per spine-call boundary. This one gives it at every single brace, which is both the simplest possible
answer and the one an author familiar with any C-family language already has the right intuition for.

The corresponding rough edge (§10): if a node *after* an `If`'s own `End` needs a value that was only
conditionally computed inside one branch, there's no automatic mechanism for that — exactly like
real C++, the author would need a node declaring/defaulting that value *before* the `If`, with the
branches only assigning into it. Not solved here; flagged as a real but well-understood gap.

## 5. Node-factory plugins and native bindings — unchanged in spirit

Existing node-factory plugins (`CubeNode`, `ExportMeshNode`) need not know scripting exists to keep
working exactly as today. To additionally become callable from generated script code, a plugin
ships one more data-file section — never a new compiled export, consistent with §0's data-only
acceptance test:

```ini
[NodeType]
    Guid  "3c91e2f0-...-CubeNode"

[ScriptBinding]
    Symbol      "CubeNode_Compute"
    Convention  "xnode_script_v1"
    Header      "Plugins/CubeNode/CubeNode_ScriptExports.h"
    Library     "$self"
    Return      "Status"
    Param       { Name "Width"  Type "Float" }
    Param       { Name "Height" Type "Float" }
    Param       { Name "Depth"  Type "Float" }
    OutParam    { Name "Mesh"   Type "MeshHandle" }
```

- No raw, arbitrary `CallExpr` string. The binding is structured; the compiler generates the actual
  call expression itself, so it can validate every parameter's type against the declared `AbiClass`
  (below) before ever emitting code.
- `Library` is not necessarily this plugin's own artifact — a node might wrap a shared engine
  utility library, a third-party SDK, or a system DLL (`"user32.lib"`). Accept a list, de-duplicate
  across every node touched by one script graph, pass the union as extra `cl.exe` link inputs (the
  same trick already proven for linking the shared plugin PCH's own `.obj` into every plugin
  compile).
- `"$self"` is a resolved-at-compile-time token, not a literal path — every plugin compile in this
  codebase produces a uniquely-named `.dll`/`.lib` pair on every recompile (the DLL-lock fix).
  Resolve it dynamically, at script-compile time, from whatever this plugin's currently-loaded
  module actually is.
- `extern "C"` alone is not sufficient ABI safety — it avoids name mangling but says nothing about
  struct packing, calling convention, ownership, pointer/span lifetime, exceptions, or version
  compatibility. Define a small, closed `AbiClass` set per type registry entry: `Scalar` /
  `FixedLayout` (explicitly packed vec2/vec3/color) / `OpaqueHandle` (uint64_t or pointer owned by a
  declared module/context) / `HostOnly` (never crosses a DLL boundary — a real `xproperty` object,
  `std::string`, or anything with virtual state must be classified this way, per this codebase's own
  already-learned cross-DLL lessons). `[ScriptBinding]` may only reference types whose `AbiClass`
  permits the crossing, **validated at registration time**, not discovered later via a linker or
  runtime failure. No exceptions may cross the boundary.

## 6. Plugin architecture

```text
Host (thin, plugin-agnostic substrate)
 |- TypeRegistry        single source of truth for type_guid + AbiClass (§2, §5)
 |- NodeTypeCatalog      TypeGuid -> whichever extension registered it
 |- Generic renderer     draws any node from its render hints; knows nothing about what it does
 `- Compile dispatcher   recursive-descent over one flat spine, splices $body, substitutes $input - see §4

Node UI Extension plugins (many; host hardcodes none of them)
 |- "Scripting" extension  - registers If/ForEachLoop/End/SetFloat/... AND owns the graph->C++ compiler
 `- future extensions      - same contract, possibly a different node family, possibly no compiler

Node Factory plugins (existing, entirely unchanged)
 |- CubeNode, ExportMeshNode, InspectMeshNode - one native runtime node type each, data-flow only
 `- OPTIONALLY also ship a [ScriptBinding] section (§5) to become script-callable
```

```cpp
class xnode_os_ui_extension
{
public:
    virtual const char*                              getExtensionName() const noexcept = 0;
    virtual std::span<const xnode_os_node_type_desc>  getNodeTypes()     const noexcept = 0;
    virtual xnode_os_compiler* getCompiler() const noexcept { return nullptr; } // null if this extension owns no compiler
    virtual ~xnode_os_ui_extension() = default;
};
```

An extension's "compiler" contribution stays small under this model too — mostly template strings
per node type, plus whichever node types declare ownership of a marker. A compiled escape hatch may
still be worth keeping available for a node whose codegen genuinely can't be expressed as one
template — but as with the earlier drafts, it is not a load-bearing safety mechanism, since there is
no structural corruption risk left for it to guard against (§4.3).

Deliberately **no rendering callback anywhere in this interface** — presentation stays entirely
render-hint-data-driven, for the same reason the existing `xproperty` hardening redesign moved
styled-property rendering out of plugins and into a host-side registry: a plugin that could inject
raw draw calls would have to link ImGui, reopening an already-solved cross-DLL-linkage problem.

## 7. Nesting and scope — resolved by §4, not deferred

Both earlier drafts of this section are now moot in different ways. The flat label/goto draft
deferred "scopes" to v2 as a purely visual layer. The spine-as-lambda draft resolved nesting via
separate spines called as functions. Neither is what actually shipped: nesting is now handled
*without leaving the spine at all* — an `If` nested inside a `ForEachLoop`'s body is just more nodes
between the loop and its own `End`, closed by the inner `If`'s own `End`/`End-Else` before the outer
`End` is reached. The multi-column/multi-spine UI work stays exactly as useful as it already was for
*visual* organization (packing unrelated sequences side by side) — it is simply no longer where
nesting semantics live.

## 8. First implementation milestone

**Positive corpus**: `AddFloat`, `SetFloat`, `GetFloat`, `If`, `ForEachLoop`, `End`, `End-Else`,
`Exit` (§9.6 — the whole-script-stop node, renamed from the earlier draft's `End` to avoid colliding
with this draft's own, unrelated `End` marker). Every one of these is pure data (pins + one template
string, or no pins/template at all for the two marker types) — there is no "build the fixed patterns
into the host first" step, because there is no closed pattern set.

**Success criteria**:
1. A hand-built graph using this corpus lowers to compilable, structured C++ — real nested
   `if`/`for` blocks, no jumps or lambdas anywhere — and `cl.exe` compiles it via the existing
   plugin-compile pipeline. **Done and verified**: a loop with a nested `if` (summing elements past
   a threshold while unconditionally counting every iteration, then running one more step after the
   whole loop) was compiled, linked into a DLL, loaded, and run — every value came out correct,
   including the case that only works if nesting/scoping/continuation are all genuinely right.
2. A deliberately introduced error (a missing/misplaced marker) maps back to the originating node
   and fails with a clear diagnostic, not a plausible-looking wrong result (§4.3, §8.1).
3. Adding one more node type (a second native `Call` binding via `[ScriptBinding]`, §5) requires
   zero compiler code changes — this is the actual test of the acceptance criterion in §0.

### 8.1 Diagnostics

Since `cl.exe` errors cite line/column in the generated `.cpp`, emit a plain comment before each
node's own template output:

```cpp
// GRAPH_NODE:b2e9-inst-0031
```

On a `cl.exe` diagnostic, walk backward in the generated source to the nearest preceding
`GRAPH_NODE` comment to find which node to highlight. No `#line` directives or sidecar source-map
format needed for v1.

## 9. What changed across each pivot, and why

### 9.1 First draft: a closed `lowering_kind` enum and region/dominance tracking

The first version of this design converged, across several rounds of independent review, on a
materially more complex model: a closed `lowering_kind` enum (`PureExpr`/`Statement`/`Call`/
`Declaration`/`Branch`/`Loop`/`Sequence`/`Return`/`Break`/`Continue`/`Entry`), a formal region/
scope-dominance system for values produced inside a loop body, propagated "required scope token"
tracking through pure-expression trees, and a hard rule that only a closed set of compiler-owned
`Pattern` handlers could ever emit a block, brace, or jump — on the grounds that letting node
templates control structural C++ text was an unenforced safety hole.

That version was internally consistent and each individual refinement was correct *given its own
premise*. The premise itself was the thing worth challenging, across all three pivots since: nothing
about "generate real C++" requires a closed, compiler-owned taxonomy of what a node is allowed to be.

**Kept unchanged across every pivot since**: native C++ codegen over an interpreted VM; strict
monomorphic typing (§2); `xresource::type_guid` for all type identity; structured,
registration-time-validated `[ScriptBinding]` for native calls (§5); schema versioning for node
type definitions (§9.4); the three-tier plugin architecture (§6); comment-based diagnostic mapping
(§8.1); deferring wildcard/generic pins to possible future compile-time monomorphization.

### 9.2 Second draft: flat labels and `goto`

Rejecting the closed taxonomy led to the opposite extreme: every node gets a compiler-generated
label, every exec-out pin resolves to a `goto` target, and there is no nesting left to protect
because there is no nesting at all — closer to BASIC/assembly than to C. This correctly eliminated
the region/dominance machinery, but gave up all lexical scoping (a loop body's locals could leak past
where the loop logically ended) and produced `goto`-laden output no human would enjoy reading.

### 9.3 Third draft: spine-as-function, then spine-as-lambda

Reusing E27's own spine (already a real, ordered, named sequential unit, built for an unrelated UI
reason — packing independent chains of nodes side by side) as the unit of execution: each spine
compiled to its own C++ function, and a redirect (`$call[PinId]`) was a plain function call into
another spine, recovering real structured `for`/`if` output with none of the labels.

This had a real, verified gap: a called spine couldn't see a value its caller had already computed
(a loop's `Body` couldn't read the loop's own `Element`), since each was a separate function with no
shared scope. The fix — making a spine an immediately-invoked, `[&]`-capturing lambda inlined at its
own call site rather than a standalone named function — solved that specific problem and was
confirmed by an actual compile-and-run test. But it left two things unresolved: how a *called* spine
could hand a value back to its caller (never designed), and whether a spine could have more than one
caller without duplicating its content. Both are now moot — see below.

### 9.4 Schema versioning (unchanged since the first draft, still required)

A saved graph cannot safely mean only `TypeGuid + PinId` — the definition file behind that GUID can
change over a project's life while retaining the same GUID. Two different failure policies, frozen
as normative:

```text
Dangling graph reference (corrupt/invalid record)      -> fail the whole graph load (existing E27 policy)
Missing/changed node schema (expected project evolution) -> preserve the node as UNRESOLVED,
                                                              show an actionable diagnostic,
                                                              never silently reinterpret meaning
```

An unresolved node must preserve: instance id, original `TypeGuid`, saved schema version/hash, raw
property payload, raw pin/link records (including now-unknown pin ids), and layout data, so
migration tooling can repair it later instead of the load silently discarding user intent.

### 9.5 Fourth (current) draft: one flat spine, recursive descent, owned markers

The spine-as-lambda model was rejected not because it was wrong, but because it was still solving a
harder problem than necessary: it kept "redirect to different content" as a *cross-spine* concept
(a call), when the actual complaint was simpler — control-flow nodes were carrying too many exec pins
(`In`, `Body`/`Then`/`Else`, `Completed`) for what they conceptually do. The realization: a spine
doesn't need to call *anywhere* to express a loop body or a branch arm — it can just keep going, in
the same list, with a lightweight, non-detachable marker (owned by the control node, created and
destroyed with it) saying where that span of influence ends. The compiler recovers real nested
blocks from that by ordinary recursive descent, exactly as if it were parsing a small structured
language, because in effect it is one.

This is explicitly a hybrid of two different visual-programming traditions: Scratch/Snap!'s
C-shaped, block-owns-its-contents model (for control flow specifically) grafted onto an otherwise
ordinary Unreal/Unity-style node-and-wire graph (for data). Verified the same way every prior pivot
was: a real nested `if`-inside-`for` graph was compiled and run, confirming correct nesting, correct
scoping, and correct continuation after each marker in a single pass — not merely reasoned about.

### 9.6 Naming: `End` needs to mean only one thing

An earlier revision of this draft used `End` for "throw `script_exit`, halt the entire script
immediately" (a small script-internal exception, not a real process `exit()`, since the generated
script is a DLL loaded into the live editor — see the git history of this document for that
version's worked example). This draft's own `End`/`End-Else` markers are a completely different,
unrelated concept (pure position markers, no runtime behavior at all), so the two cannot share a
name. The halt-everything node is renamed `Exit` here; confirm this naming before implementation, but
do not let a future pass silently reuse `End` for both meanings.

## 10. Open items

- [x] **Ownership representation — resolved, see §11.1.** A control node's owned `End`/`End-Else` is
      a plain field on the owner instance (`m_OwnedEndId`), not something inferred from a wire; the
      editor enforces non-detachability, cascading delete/drag/select, and a read-only *rendering*
      of that relationship (never the other way around). Built and verified in the live editor.
- [x] **`End` vs `End-Else` as node types — resolved, see §11.2.** One node type (`End`), used two
      ways: a plain `bool IsElse` property whose toggle creates/destroys a second, chained `End`
      live. Deliberately NOT split into two node-type GUIDs (a fuller proposal suggested this; see
      §11.2 for why it was rejected as unnecessary complexity).
- [ ] **Value flow out of a conditional branch.** §4.4's real-C++-scoping trade-off: a node after an
      `If`'s own `End` cannot see a value only computed inside one branch, without an explicit
      "declare before, assign inside" pattern the author has to build themselves. Worth a worked
      example before this is trusted at scale.
- [x] **Do multiple spines interact at all? — resolved, see §11.5.** Not by reaching a spine's own
      end (that's still not a thing - column/Y position never implies "continue into a sibling
      spine"), but through data: a spine's own TOP-LEVEL content (never nested inside a local scope)
      is shared/global state any other spine can read too, the same role a Blueprint Variable plays
      across Event Graphs. Content inside a local scope stays trapped in its own spine, same as
      before.
- [ ] Whether a script can have more than one top-level entry point (e.g. one per lifecycle event) —
      presumably yes, one designated spine and one generated entry function per event, but not
      spelled out in detail yet.
- [ ] `Exit`'s `script_exit` unwinds through ordinary C++ destructors like any exception — confirm no
      generated local in the §8 corpus needs cleanup that would matter if skipped this way (none do
      yet). Revisit once a node type introduces an owning handle.
- [ ] Whether a compiled escape hatch (§6) is worth keeping for v1 at all, or whether the corpus in
      §8 can be fully served by templates alone.
- [ ] `[ReflectionBinding]`/`[ValueBinding]` section shape for a struct-field-access node
      referencing an `xproperty`-reflected type directly.

## 11. Reconciling live wildcard resolution with §2 (added after the live editor's scripting UI shipped)

Everything in §§1–10 above predates a large stretch of live-editor implementation work: the actual
ownership enforcement (cascading delete/drag/select), a consolidated enum-driven node style
(`Compare`/`Bool Expression`/`Math Expression`, one node with an operator dropdown instead of a box
per variant), and — the one genuinely new idea not anticipated above — a **generic/wildcard pin
mechanism** (`Any`, `Span<Any>`) so `Compare`/`Math Expression`/`Print`/`ForEachLoop` work over
whatever concrete type gets wired in, instead of being hardcoded to `Float` or needing a
`CompareFloat`/`CompareInt`/... family. This section records how that reconciles with §2's frozen
"no wildcards" rule, and closes two of §10's open items. A fuller reconciliation proposal was
sourced from an external review pass and evaluated against this codebase directly — most of it held
up; two pieces (§11.4, and splitting `End` into two node types) were deliberately rejected as
solving already-solved or non-existent problems. Recording the reasoning for both keeps this
document from re-litigating them later.

### 11.1 Ownership is already canonical data, not inferred from a wire

§10 used to list ownership representation as undesigned. It is designed now, and simpler than that
item implied: an owner node instance carries its owned marker's id directly
(`node_instance::m_OwnedEndId`), set once when the pair is created and cleared when it's removed.
The read-only "ownership wire" the canvas draws between an owner and its marker is a *rendering* of
that field — cascading delete, cascading drag, cascading selection, and the wire's own
undetachability all derive from `m_OwnedEndId`, never the other way around. A future graph-file
format should serialize this field directly (an explicit owner→marker record, not something a
loader has to re-infer by noticing a read-only-flagged link) — but the *design* question §10 flagged
is already settled; only the serialization encoding remains open.

### 11.2 Why `End` stays one node type, not two

The reconciliation proposal suggested splitting `End` into distinct `EndMarker`/
`ElseBoundaryMarker` node types, on the grounds that a node type whose own port list changes shape
based on one of its properties (`End`'s `IsElse` toggle adds a second, chained `End` and an
`ElseEnd` output when true) is awkward for schema validation and migration.

That concern is real in the abstract, but weighed against actually rebuilding a mechanic that's
already built, tested, and simple, it isn't worth it: `If`/`else` is an extremely well-understood,
fixed C++ shape, and one `End` type with a boolean is materially less machinery than two node types
the editor has to swap between live (destroy one instance, create a different-typed one, rewire
everything) every time a user toggles a checkbox. **Decision: keep the single `End` type.** This
closes §10's "`End` vs `End-Else` as node types" item outright, rather than leaving it open for a
descriptor format to re-decide later.

### 11.3 The actual reconciliation: resolve live, persist concrete, compile concrete

§2 says data pins have exactly one concrete type and rejects "live unification" wildcards. The
editor's `Any`/`Span<Any>` mechanism *is* a form of live resolution — just a much smaller one than
what §2 was actually rejecting (a real constraint solver): it's first-wire-wins, recomputed fresh
from the current wires every frame, with no stored state and no propagation across more than one
hop. The reconciliation is not to remove it (it's a real, working, tested UX improvement — it's
what let `Compare`/`Math Expression`/`Print`/`ForEachLoop` exist as ONE node each instead of a
family per concrete type) and not to treat it as satisfying §2 as originally written either.
Instead:

- **The editor resolves wildcards live, for editing UX only.** This is unchanged — `Any` and
  `Span<Any>` keep working exactly as built, purely as canvas/property-panel presentation, computed
  fresh from `Links` every frame (see `E27_NodeOS_Editor.cpp`'s `ResolveNodeWildcardType`/
  `EffectiveTypeName` — these names and their behavior are correct and stay).
- **A saved graph records the resolved concrete type per wildcard pin, not `"Any"`/`"Span<Any>"`.**
  Whatever format eventually replaces the current ad-hoc save format needs a field for this — a
  wildcard pin with nothing wired to it (and therefore nothing to resolve) is a legitimate saved
  state too (an incomplete graph, not an error), and should round-trip back to "unresolved," not
  silently default to some placeholder concrete type.
- **The compiler never receives a pin typed `"Any"` or `"Span<Any>"`, and never performs resolution
  itself.** By the time a graph reaches compilation, `Compare`'s `A`/`B` are just two `Float` pins
  (or whatever they resolved to) as far as the compiler is concerned — it has no wildcard concept,
  no unifier, nothing that could be mistaken for the "interactive constraint solver" §2 rejects. A
  wildcard pin that's still unresolved at a point the compiler would need to emit code for it is a
  **pre-compilation graph-validation failure** (a clear diagnostic naming the unresolved node/pin),
  never something the compiler tries to guess or defer.

This is deliberately *not* full monomorphization in the template-instantiation sense (no
`CompareFloat` node type ever gets materialized) — it's closer to "the editor's own live resolution
already did the equivalent of monomorphizing this specific instance; save that outcome, and let the
compiler treat it as if it had never been generic at all." Whatever descriptor/schema work happens
later (§10's still-open items about ownership *encoding* and `[ReflectionBinding]`) should treat a
wildcard pin as an editor/serialization-layer concept only — the compiler-facing side of a
descriptor (§4's template + `$input[PinId]` substitution) never needs to know a pin was ever
generic.

### 11.4 `Span<T>` needs no adapter nodes — rejected as unnecessary

The same reconciliation proposal suggested the graph would need explicit `VectorFloatToSpan`/
`ArrayFloatToSpan`/`NativeArrayToSpan` producer nodes, plus splitting `Span<T>` into distinct
`SpanConst<T>`/`SpanMutable<T>` registered types, on the grounds that `std::vector`/`std::array`/C
arrays/`std::span` "are not interchangeable C++ types."

**Rejected.** `std::span<T>` already has implicit converting constructors from `std::vector<T>`,
`std::array<T,N>`, and any other contiguous range — this is exactly what `std::span` is *for*.
Wherever a real producer feeding a `Span<T>` pin is (say) a `std::vector<float>` under the hood,
codegen wraps it at the point of use (`std::span<float>(TheVector)`), same as any other ordinary
implicit conversion the generated C++ already relies on elsewhere — this needs zero graph-visible
adapter nodes. The const-vs-mutable question (`ForEachLoop`'s `ReadOnlyElement` property) is a
**compiler-side validation check**, not a reason for a second family of registered types: if
`ReadOnlyElement` is false but the actual wired-in source only exposes a `const`-qualified
container, that's a graph-validation error at compile time ("this loop wants to write through
`Element`, but `X` only provides read-only access") — resolved the same way any other
type-compatibility check in §2 already works, not by inventing `SpanConst<T>`/`SpanMutable<T>` as
separate concrete types.

### 11.5 Cross-spine data: the boundary is scope depth, not spine identity

§3 left "do multiple spines interact at all" deliberately open and defaulted to "no, keep them
independent." Building against a real graph with two spines side by side (two `ForEachLoop`s in
separate columns) surfaced the actual question immediately: a first pass at validating this flagged
*every* cross-spine data link as invalid, unconditionally — too strict, and not what "independent
units" was actually meant to rule out.

**Resolved**: the boundary is scope depth, not spine identity. A node sitting at a spine's own TOP
level — its enclosing-scope chain is empty, meaning it's never nested inside any `If`/`ForEachLoop`
body — is **world scope**: conceptually shared/global state, valid as a data source for *any* node
anywhere, in any spine, at any depth. A node nested inside a local scope, by contrast, produces a
value trapped in whatever block its own spine compiles to - readable only from the same or a more
deeply nested scope in that *same* spine, never from a different spine at all, and (§4.4, unchanged)
never from a sibling or already-exited scope even within its own spine.

This is precisely the shape of the split Unreal Blueprint already draws between a **Blueprint
Variable** (class member state — any Event Graph can read or write it) and an ordinary **local pin
value** inside one Event Graph (Blueprint doesn't even let you attempt a wire between two different
Event Graphs at all - there's no shared canvas space to drag one on). "World scope" here plays the
Blueprint Variable's role without needing a distinct node/variable concept of its own - it falls
straight out of "not nested in anything," which the editor already tracks for every node.

**Left open, and worth resolving before compilation is wired up**: a same-spine read is always
guaranteed fresh, because flat sequential/nested execution order makes "already computed by the time
this reads it" automatic. A cross-spine world-scope read has no equivalent guarantee - it's really a
"last value written" read against some kind of persisted/shared storage, and if spines become
separate generated functions (one per entry point, per §10's still-open "more than one top-level
entry point" item), *which* function actually owns that storage, and what "fresh" even means when
the reading spine might run before the writing one ever has, isn't designed yet. The editor's own
validation (`IsDataLinkScopeValid` in `E27_NodeOS_Editor.cpp`) only decides whether a link is
*legal* to draw without becoming a dangling reference in generated code — it says nothing about
timing, and shouldn't be read as having settled that question.

### 11.6 What's still genuinely open after this section

This section resolves the *conceptual* tension, not the implementation. Still undesigned:

- The actual descriptor/schema format unifying node-type description across the live editor's C++
  plugin DLLs (`SDK/xnode_os_plugin_api.h`) and the standalone compiler prototype's own
  `.nodetype`/`node_type_definition` format (`Scripting/xnode_os_node_type_definition.h`) — today
  these are two independent representations of "what a node type is," and neither can describe the
  other. Migrating pin identity from this file's `xresource::type_guid` design to what the live
  editor actually uses (plain `const char*` type names) is part of the same unification.
- The concrete on-disk encoding for a resolved wildcard binding (§11.3) and for an owner→marker
  ownership record (§11.1) — both now have a clear *design* answer above, just not a chosen file
  format yet.
- Whether/how `[ScriptBinding]` (§5) actually gets attached to the three existing, real,
  currently-executing native plugins (`CubeNode`, `ExportMeshNode`, `InspectMeshNode`) — none of
  them have one yet, and none of this section's reconciliation changes that design, only confirms
  it's still the right mechanism to use when that work happens.

## 12. Exec pins, revived narrowly — triggering spines and scopes explicitly

§4's flat-spine model already answers "what runs next" for ordinary content: whatever's positionally
next in the same spine. It never answered "what makes a *different* spine start running at all" —
every spine was an island, and nothing could invoke one from an event or from another spine's own
logic. This section adds exactly that capability back, using the same "Exec" pin concept §4.4 and
§9.2's history describe as removed — but bound to a small, fixed set of node types instead of
threaded through every node the way the old Blueprint-style model (and this project's own first
execution-model draft, §9.2) did.

### 12.1 Why this doesn't reopen §9's decision to remove exec pins

§9.2 removed exec pins because *ordinary* sequencing doesn't need them — a plain node's "next" is
just whatever follows it in the same spine. That's still true and unchanged: `Compare`,
`MathExpression`, `Print`, `Constant`, and every other ordinary node type has zero exec pins, exactly
as before. What's new is a *different* problem exec pins turn out to be the right tool for: crossing
a spine or scope boundary on purpose. Four new/changed node types carry the entire mechanism; nothing
else does.

### 12.2 The four node types

- **`OnEvent`** (`Plugins/OnEvent/on_event_node.cpp`) — zero inputs, zero outputs. A pure
  documentation marker: it labels which event a nearby spine conceptually responds to, with no
  wiring implications at all. It is never reached, never runs, and carries no data — purely for a
  human reading the graph.
- **`ExecutionCall`** (`Plugins/ExecutionCall/execution_call_node.cpp`) — zero inputs, one `Exec`
  output. The thing that actually fires: a manually/externally triggerable spine entry point. (This
  was originally named `OnEvent`, before the empty pure-label node above needed the name instead.)
- **`Execute`** (`Plugins/Execute/execute_node.cpp`) — one `Exec` input, zero outputs, and
  deliberately owns **no scope** (`needsOwnedEndMarker()` is the inherited default, `false`). Its
  "body" is simply everything positionally after it in its own spine, running via the spine's
  ordinary Order-based sequencing, all the way to the spine's own end — there is no paired `End`
  marker bounding it the way `If`/`ForEachLoop`/`Function` have. This is the key distinction a
  `Call` into an `Execute` vs. a `Call` into a `Function` will draw: `Execute` behaves like invoking
  a C++ lambda captured by reference (`[&]`) — it reads/writes whatever's already reachable from
  where it sits, with no isolated parameter/local scope of its own — while a `Function` call is a
  real subroutine call with its own encapsulated scope. Two `Execute`s (or unrelated content) sharing
  one column/spine is not something the model disambiguates for you — same as leaving unrelated
  functions back to back in one file — it's the graph author's responsibility to keep them
  organized, not the runtime's.
- **`Function`'s new `Exec` input** (`Plugins/Function/function_node.cpp`) — `Function` already
  owns a real scope (§11's reconciliation); it now also requires an explicit `Exec` pulse to run its
  body, appended as the last input so an already-wired declared input's index never shifts. Input
  only, no matching output: **the caller** (a `Call` node, once built — see §12.7) is what regains
  control once the function returns; `Function` itself has no notion of "what comes after."

`Call` — the node that actually invokes a `Function` or an `Execute` from elsewhere in the graph — is
deliberately **not built yet**. See §12.7.

### 12.3 Fork-join: one Exec output, several wires

An `Exec` output pin can fan out to more than one target (`ExecutionCall`'s single output wired to
both a `Function`'s and an `Execute`'s `Exec` input is the worked example this session actually
built and ran). The settled semantics: **every fanned-out target must finish before the caller's own
spine continues past the triggering node** — a join, not fire-and-forget. The *order* in which
multiple targets run relative to each other is deliberately left unspecified; nothing in this design
depends on it, and the current interpreter (§12.6) just runs them left-to-right over `Links` for lack
of any reason to do otherwise.

### 12.4 Program lifetime: the root spine governs it

Once the **root spine** — the one spine flagged `m_bIsRoot` in the `Spines` table, the one `OnEvent`
conventionally labels — runs off its own end, **the program is done**, independent of whether every
other spine or `Function` it triggered along the way ever actually got reached. This mirrors a plain
`main()`: once it returns, the program ends regardless of what else was ever defined but never
called.

### 12.5 Columns remain pure organization; reachability is what actually matters

Exec wiring (§12.3) and each spine's own Order (§3) are now the **only** two things that determine
execution — not which column something happens to sit in. The entire worked example in this section
could be redrawn in a single column and behave identically; splitting it across columns is purely for
human readability, the same reason real code gets split across files even though the compiler
wouldn't care if it weren't. A direct consequence, also settled this session: **anything not
reachable from the root spine by walking Exec wires and spine Order is inert** — present in the
graph, never executed, exactly like commented-out code. This is not an error case. Remove the wire
from `ExecutionCall` to `Execute` in the worked example and `Execute` (and everything after it in its
own spine) simply never runs — nothing else changes.

### 12.6 A first real interpreter — implemented

`E27_NodeOS_Editor.cpp` now contains an actual implementation of §12.1–§12.5, replacing the older
`ExecuteGraph` body that used to run everything with a naive "run whatever's data-ready" fixed point
(a model with no concept of spine order, scope, or "only run if actually triggered" — it would have
called `Function`'s own `Execute()` the moment its declared inputs looked resolvable, regardless of
whether anything ever invoked it). The replacement:

- `RunProgram` finds the root spine and calls `RunSpineRange` on it from Order 0 — the single entry
  point for running the whole program.
- `RunSpineRange` walks one spine's nodes in `[FromOrder, ToOrder)`, in Order — the flat-spine base
  case. `End` is skipped (pure boundary marker). `ExecutionCall` triggers `RunExecutionCall`.
  `Function` and `Execute` are deliberately **skipped** here even when positionally reached — both
  declare a real `Exec` input specifically so they only ever run via an incoming trigger, never
  merely because spine order got to them. Everything else falls through to `RunOrdinaryNode` (resolve
  declared inputs from wherever they're wired, call `Execute()` once, cache outputs) — the same
  one-shot model `If`/`ForEachLoop` would also get today, since neither has been given real
  branch/loop semantics yet (see §12.7).
- `RunExecutionCall` implements §12.3: fires every Exec-typed link off its own output, synchronously
  (so the loop itself is the join), then lets its own spine continue.
- `RunExecTarget` is where `Function` and `Execute` actually run once triggered. For `Function`: it
  resolves the declared (external, non-local, non-`Exec`) inputs, mirrors each one into the matching
  local-scope *output* slot (`function_node.cpp`'s `Rebuild` always places the K-th declared input's
  mirror at output index `[ExternalOutputCount + K]` — the body's own view of its parameters), runs
  the body between itself and its own `End` via `RunSpineRange`, then mirrors whatever the body wrote
  into the local Result-mirror *input* back out to the matching declared external output (the reverse
  direction, same indexing scheme). For `Execute`: no scope to set up at all — it just runs
  `RunSpineRange` from the node right after it to the spine's own end.
- `Constant` and `Print` got real (non-stub) `Execute()` bodies to make the worked example actually
  observable: `Constant` allocates and returns a real value matching its `Type`/`Value` properties
  (same `malloc`/`FreeOutputs` convention `CubeNode` already established); `Print` reads its `Value`
  input as a `float` and routes it through `ixnode_os_host::Log()` — the one sanctioned host callback
  a plugin can reach (stored from the reference `NodeOS_CreateFactory` receives once at load, handed
  to each instance in `CreateNodeInstance()`). `host_bridge::Log()` now also appends to a small
  on-screen "Console" panel (`GetRuntimeLog()`), cleared at the start of every run — previously
  `Log()` only reached the OS debugger, invisible to the user.

### 12.7 What's still genuinely open after this section

- **`Call` itself is not built.** It needs to invoke a `Function` or `Execute` node from elsewhere in
  the graph, which means dynamically mirroring a *different* node's live signature onto itself — a
  real new mechanism, since the plugin-isolation rule (§6/`xnode_os_plugin_api.h`'s own top comment)
  means `Call` can't just reach into another instance's live state. This needs host-side
  synchronization analogous to what `ResyncLocalConnections` used to do for `Function`/
  `LocalConnections` before those merged into one instance (§11) — except `Call`'s target is a loose,
  user-changeable reference rather than an owned pair, so the sync can't be designed away by merging
  instances this time. Expected shape, not yet built: mirrors the target's declared inputs/outputs as
  real data pins when targeting a `Function`, plus its own `Exec` in/out pair; mirrors nothing but
  `Exec` in/out when targeting an `Execute` (nothing else to mirror — `Execute` has no scope, no
  parameters).
- **`If`/`ForEachLoop` have no real branch/loop semantics in the interpreter yet.** `RunOrdinaryNode`
  gives them the same generic one-shot treatment as any other node type not specifically recognized —
  correct for nothing that actually branches or repeats. Nothing saved so far exercises either; this
  is the next concrete extension once something does.
- **`Print` (and anything else with an `Any`-typed pin) assumes Float.** The resolved wildcard type
  isn't threaded into `Execute()`'s type-erased `void**` signature at all — genuinely dispatching on
  it would mean adding that plumbing to the plugin ABI itself (`xnode_os_plugin_api.h`), which is a
  more invasive change than anything else in this section. Deferred until a real second value type
  actually needs printing.
- **Inline literals typed into an unconnected pin are UI-only, never fed into real execution.** They
  live in `DrawGraphCanvas`'s own `LiteralValues` map (keyed by pin, populated as the user types),
  not in `node_instance`/the saved graph at all — `GetInputValue` only ever resolves from an actual
  wire. No saved test graph currently depends on an unconnected pin's typed literal mattering for its
  computed result, so this has been deferred rather than solved; it will need resolving before an
  unconnected numeric input can mean anything at runtime.
- **Cross-spine data freshness (§11.5's own left-open item) is unaffected by any of this.** The
  interpreter above only ever reads a value that's already been computed earlier in the *same*
  synchronous call chain (Constant runs before `ExecutionCall` triggers anything that reads it,
  because it's positioned earlier in the same spine) — it does not yet address what "fresh" means for
  a spine that reads world-scope state written by a *different* spine that may or may not have run
  yet.
