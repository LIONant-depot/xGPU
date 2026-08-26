# Problem Statement: `NodeBuilder` — a graph node that compiles a graph into a new native node

Status: **open problem, seeking outside design input** — this is not a design doc, it is a brief
written to get independent ideas from other AI models before committing to an approach. If you are
an AI reading this: do not assume the "baseline approach" sketched in §4 is the intended answer —
it is included only so you have a concrete existing mechanism to react to, extend, or reject.
Original, structurally different proposals are explicitly welcome.

## 1. What this system is

E27 Node OS is a visual node-graph editor (a worked example inside a larger C++/Vulkan engine
called xGPU). Two things distinguish it from a typical Blueprint/Shader-Graph clone:

- **It compiles to real, native C++**, not an interpreted bytecode/VM. A graph either runs through
  a tree-walking interpreter (for fast iteration inside the editor) or is lowered to an actual
  `.cpp` file, compiled with `cl.exe`, and executed as a real binary — both paths exist today and
  are kept behaviorally in sync.
- **Node *types themselves* are native plugins.** Every node kind the palette offers (`Random`,
  `Compare`, `ForEachLoop`, `Print`, ...) is a hand-written `.cpp` file living in its own
  `Plugins/<Name>/` folder, compiled into a small DLL by the *exact same* `cl.exe`-shelling pipeline
  the graph-to-C++ path uses, then loaded back into the running editor and hot-swapped in place. A
  new node type today = a human writes one `.cpp` file implementing a small ABI; nothing else in the
  host needs to change.

Both of these already work and are exercised routinely (there's a live hot-reload command: save the
graph, unload the old DLL, recompile the plugin `.cpp`, reload the DLL, reload the graph — all one
step).

## 2. The goal

**Make the graph able to write and compile its own new node types**, using the exact same plugin
pipeline that already turns a hand-written `.cpp` into a loadable node kind — closing the loop so
that a user (or an AI agent driving this editor) can define a reusable piece of graph logic once,
"compile" it, and have it show up in the node palette as a first-class native node, indistinguishable
from a hand-written one, usable (and re-placeable, many times) anywhere else in the graph or in
future graphs.

Concretely: introduce a node called **`NodeBuilder`** whose job is to take an existing in-graph
definition of some reusable logic and produce a genuinely new, independently-compiled, independently
-loadable node type from it.

This is *not* asking for a subroutine-call mechanism inside one graph run (that already exists,
described in §3) — it's asking for **graph-authored extension of the node vocabulary itself**: the
set of things you can drag out of the palette should be able to grow *from inside the tool*, not
only from a human editing source files outside it.

## 3. What already exists that's directly relevant

### 3.1 `Function` — an in-graph, reusable, scoped subroutine

There is already a node type called `Function`. Placing one creates a paired "owned scope": the
`Function` node itself, plus an automatically-created, non-detachable `End` marker later in the same
list of nodes. Everything positioned between them is the function's *body*. A `Function` instance:

- Declares its own signature as two small encoded strings — one for its inputs, one for its outputs
  — each entry carrying a name, a type name, and two independent flags (required-vs-optional,
  read-only-vs-writable). This spec is user-editable through the node's own property panel (an
  add/remove-row pin editor), and it's what the node's `getInputs()`/`getOutputs()` decode into
  actual typed pins, freshly, every time they're queried.
- Exposes those declared inputs as real pins *external* callers wire into, **and** simultaneously
  mirrors them, role-flipped, as pins only the function's *own body* can see (declared inputs become
  body-visible outputs the body reads from; declared outputs become body-visible inputs the body
  writes into). One node, four logical pin groups, entirely derived from the same two spec strings —
  no separate node type needed for "the inside view" vs "the outside view."
- Requires an explicit trigger pin (`Exec`) to run at all — it does nothing just by being reachable
  in spine order, unlike an ordinary data node.

Today, a `Function`'s *body* only ever gets lowered as an **inline free function pasted directly into
the same generated `.cpp`** as whatever program is being compiled (a `static float Fn_<id>(params) {
...; return expr; }`, called from wherever the `Function` node was invoked). It is real, working
codegen — nested control flow inside the body, real typed C++ parameters, a real return value — but
it only exists *inside one specific generated program*. It is not, itself, a reusable, independently
-loadable thing. Two different generated programs that both want the same logic would each get their
own private copy of that free function; nothing packages it as a standalone artifact.

### 3.2 The plugin ABI a hand-written node type must implement

A node type is a small `struct` deriving from a base with:
- `getInputs()` / `getOutputs()` — returning a list of `{ name, type name, required?, read-only?,
  local-scope-only? }` descriptors, queried fresh per call (so even a *dynamic*-arity node type is
  representable, though most hand-written types return a fixed, constant list).
- `Execute(void** Inputs, void** Outputs)` — one untyped pointer per pin, in declared order; no
  compile-time type information crosses this boundary at all, casting is the node's own job.
- A matching factory `struct` (`getName()`, `getCategory()`, `CreateNodeInstance()`,
  `DestroyNodeInstance()`, plus two optional hooks a control-flow node type uses to declare that it
  owns a paired end-marker, the same mechanism `Function` itself uses for its own `End`).
- Two plain exported C functions the host `GetProcAddress`s after `LoadLibrary`-ing the DLL.

### 3.3 The compile pipeline this would reuse

A worker function takes **any** `.cpp` path, shells out to `cl.exe` with `/LD` (produces a DLL, not
an EXE), loads the result, and pulls out however many factories it registers. It has no dependency
on the source file having been "discovered" any particular way first — it's a pure function of a
path on disk. A second, separate step is what makes a freshly-compiled type *addressable* by the
rest of the editor (placeable from the palette, resolvable by name when a saved graph references it,
survivable across a save/reload) — that step needs the new type associated with a stable folder
identity under `Plugins/<SomeName>/`.

A full hot-reload sequence already exists and is exercised today for the human-edits-a-`.cpp`-by-hand
case: save the current graph → destroy every live node instance → unload the old DLL → recompile →
load the new DLL → reload the saved graph (which re-resolves every node against whatever's loaded
now). Whatever `NodeBuilder` produces would presumably need to go through some version of this same
sequence to become live without restarting the whole editor.

## 4. A baseline mechanism (existing building blocks, not a prescribed answer)

The most direct extension of what's already built: reuse the *same* body-lowering logic that today
turns a `Function`'s owned scope into a free function's body, but change the **harness** wrapped
around that body — instead of `static float Fn_<id>(float p0, float p1) { body; return expr; }`
pasted into someone else's translation unit, emit a full, standalone `.cpp`: a `struct` implementing
the plugin ABI, whose `Execute(void** Inputs, void** Outputs)` body is that same lowered logic
rewritten to read/write through the `Inputs[i]`/`Outputs[i]` pointers instead of named parameters,
plus `getInputs()`/`getOutputs()` built from the same signature spec `Function` already parses,
plus the factory boilerplate and DLL exports. Write that file under a new `Plugins/<Generated>/`
folder, run it through the existing compile-any-`.cpp` worker, and register it the same way a
freshly-compiled hand-written plugin gets registered today.

This is a real, mostly-mechanical extension of existing code, not a leap — but it is very likely not
the *best* answer, and several real design questions don't have an obvious resolution just from
"extend the existing thing." That's what this brief is actually asking for help with.

## 5. Open questions — genuinely undecided, looking for outside perspectives on all of these

1. **What triggers `NodeBuilder`, and what does it look like as a node?** Is it a node sitting in
   the canvas with a property referencing which `Function` to compile and an `Exec` pin to fire the
   compile, the same interaction shape as everything else in this graph? A one-shot button in a
   `Function`'s own property panel would be far less code — but the user explicitly wants this to be
   a *node*. Is there a reason a node is actually the right shape here (vs. a command), e.g. so the
   *compiling itself* can be sequenced, gated, or driven by other graph logic, or triggered remotely
   by an external agent through the same pipe/command-console mechanism everything else in this
   editor is already scriptable through?
2. **Identity and collisions.** What names the generated plugin folder / node type? What happens the
   second time `NodeBuilder` runs against a `Function` whose signature has since changed — same
   folder, full hot-reload cycle, and now every *existing placed instance* of the old generated type
   elsewhere in the graph needs to reconcile against a new signature? Is this a versioned artifact
   (`MyFn_v2`) or a mutable one (`MyFn`, always latest)?
3. **Type erasure at the boundary.** `Function`'s declared pin types are just strings today
   (`"Float"`, `"Int"`, ...), and the only type actually proven end-to-end through this codegen path
   is `Float`. Real plugin `Execute()` casts `void*` by hand per type. What's the right amount of
   type-table machinery to build now vs. defer — should v1 just refuse to compile a `Function` with
   any non-`Float` pin, or is there a smaller-than-expected amount of work to genuinely generalize
   the cast logic?
4. **Static vs. dynamic ports on the output.** `Function` deliberately has *dynamic*, editable
   per-instance ports (that's what makes it useful while you're still designing the logic). Should
   the node `NodeBuilder` produces freeze that into a fixed, compiled-in port list (the normal shape
   for a hand-written plugin) — meaning further signature edits require re-running `NodeBuilder`,
   not live-editing the compiled node? That seems obviously right but is worth stating explicitly and
   testing against a real signature-change scenario.
5. **What body content is actually liftable this way?** Nested `if` already composes through the
   existing recursive lowering. Does everything else in the node vocabulary (loops, other subroutine
   calls, nodes that read/write outside their own scope) already lower cleanly inside a body destined
   to become a real plugin `Execute()`, or does packaging-as-a-standalone-DLL introduce new
   restrictions (e.g. can a generated node's body legally call *another* generated node? Can it call
   itself, i.e. recursion)?
6. **Multiple outputs.** The existing free-function lowering only ever produces one return value.
   `Function` already supports multiple declared outputs. `Execute(void**, void** Outputs)` can
   obviously write more than one slot — but the *existing* body-lowering code doesn't do that yet for
   the free-function case, so this is real, not-yet-solved work regardless of which outer approach
   wins.
7. **Debuggability and trust.** If the generated `.cpp` fails to compile, or compiles but misbehaves,
   how does that map back to something a graph author can act on? Is the generated source meant to
   be inspectable/readable from inside the editor? Should a compile failure block placement of the
   new type entirely, or produce some kind of "broken node type" placeholder?
8. **Does this ever need to work for the *interpreter* path too**, or is "compile a Node" inherently
   a `cl.exe`-only, codegen-only feature, with the interpreter simply treating a `NodeBuilder`-
   produced node exactly like any other hand-written plugin once it's loaded (which it should, by
   construction, since it implements the same ABI)?
9. **Anything not covered above.** This system's existing design record (a separate, longer document
   in this same project) has already gone through several complete pivots on adjacent questions
   (execution model, typing, nesting/scope) — if a proposal here rhymes with one of those earlier,
   rejected shapes, say so and explain the difference; if it's genuinely new, so much the better.

## 6. What a good answer looks like

Not full working code. A clear description of: what `NodeBuilder` consumes as input, what artifact(s)
it produces and where they live, how identity/versioning/collision is handled, how much of §5's type
question it resolves vs. explicitly defers, and — most importantly — *why* that shape is better than
the baseline in §4 along whichever axis matters most to the proposal (simplicity, safety, how much of
the existing pipeline it reuses vs. replaces, how naturally it extends to loops/recursion/multiple
outputs, or something not listed here at all).
