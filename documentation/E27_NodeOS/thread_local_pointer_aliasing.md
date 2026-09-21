# thread_local buffer pointer aliasing

> A function returning const char* backed by a shared thread_local buffer corrupts an earlier live result when called twice before the first is consumed
>
> Migrated from the working notes on 2026-09-21 (last edited 2026-08-22).

In `E27_NodeOS_Editor.cpp`, `ResolveNodeWildcardType`'s container-unwrap path backs its result in
`static thread_local std::string s_Elem` and returns `s_Elem.c_str()`. Any call site that computes
TWO results from this function (or `EffectiveTypeName`, which calls it) before comparing them is at
risk: the second call silently overwrites `s_Elem`, corrupting the first call's still-live pointer
before the comparison runs.

**Found via**: self-review while committing overnight-unsupervised work, independently confirmed by
a background review agent given the same code with no context of the fix already made — both landed
on the exact same finding, which is a good sign this class of bug is genuinely findable by careful
tracing, not a fluke.

**Why it didn't already misbehave**: only one pin type in the whole corpus (`ForEachLoop`'s
`Element` output) ever takes the container-unwrap path, and it's always an output, so role-
separation (output vs input) happened to prevent two such pins from ever landing on opposite sides
of one comparison. This is exactly the kind of bug that stays silent until a second node type
exercises the same path — see `e23_gpu_id_picking` (note pending migration) and `e23_gpu_pick_ssbo_race` (note pending migration) for other cases
in this same project of "correct-looking code that happens not to fire yet."

**Fix applied**: at the two call sites that compare two resolved types (drag-preview ring check,
drop-commit connection validation), copy the FIRST result into its own `std::string` immediately,
before making the second call. Left the shared function's return type (`const char*`) unchanged
everywhere else, since every other call site uses its result immediately/singly - no aliasing risk
there.

**How to apply**: before adding a second node type whose wildcard pin can resolve through the
container-unwrap path (i.e., a second producer of `Span<T>` besides `ForEachLoop`, or a second
`Any`-typed *output* fed by unwrapping), re-audit every `EffectiveTypeName`/`ResolveNodeWildcardType`
call site for this same pattern - grep for `EffectiveTypeName(` and check whether more than one
result from it is alive at the same time before use.
