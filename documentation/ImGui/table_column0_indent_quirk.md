# ImGui: table tree-indent applies to column 0 only

> Dear ImGui tables apply tree-indent ONLY to column 0 by default (IndentEnable/IndentDisable per column) - a real bug source for any table with a tree in a non-zero column
>
> Migrated from the working notes on 2026-09-21 (last edited 2026-09-17).

Confirmed by reading the vendored `imgui_tables.cpp` itself (not from memory/guessing) while fixing
the E29 Level Tree's new SC badge column, see [E29 Level Tree source-control column](../E29_LevelSceneEditor/level_tree_source_control_column.md):

`imgui_tables.cpp` (`TableSetupColumn`, ~line 772-773):
```cpp
if ((flags & ImGuiTableColumnFlags_IndentMask_) == 0)
    flags |= (table->Columns.index_from_ptr(column) == 0) ? ImGuiTableColumnFlags_IndentEnable : ImGuiTableColumnFlags_IndentDisable;
```

Dear ImGui tables give column 0 `IndentEnable` and every other column `IndentDisable` BY DEFAULT,
regardless of which column's content actually calls `TreeNodeEx`/`TreePush`. If a table puts its
tree in column 1 (as this codebase's own convention would do when column 0 is reserved for an
icon/status column, e.g. a leftmost "##SC" badge), the real symptoms are:
- The Name column (where the tree actually lives) shows NO visual indentation at all - "the
  indentation for the tree seems to be missing" (direct user report).
- The icon column (column 0) DOES receive the current indent, drifting right with tree depth and
  clipping past a narrow fixed-width column at even one level of nesting - "some of them seems
  missing, and others look like they are too far on the left" (direct user report, same root cause).

**Fix**: explicit `ImGuiTableColumnFlags_IndentDisable` on the icon column and
`ImGuiTableColumnFlags_IndentEnable` on the Name column in `TableSetupColumn` - overrides the
default cleanly, no manual `window->DC.Indent.x` poking needed (an earlier attempt at that
hand-rolled workaround was replaced once the real, documented column-flag fix was found).

**Related, separate bug found in the same pass**: a `ImGuiTreeNodeFlags_SpanFullWidth` tree row
paints its Selected/Hover background across every column in the row, not just the column the
TreeNodeEx call is in. If an icon in another column is drawn BEFORE that TreeNodeEx call in
submission order, the background paints over it on any highlighted row (later draw = on top). Fix:
draw the icon column's content AFTER the Name column's TreeNodeEx, not before - still lands in the
same column position, just wins the paint order.

**Also watch**: a table's `ImGuiStyleVar_CellPadding` is whole-table, not per-column - shrinking a
narrow fixed-width icon column without also shrinking (or zeroing) CellPadding.x can make the
padding alone exceed the column width (negative available content room), pushing content past the
column and even past the window's own edge. Scope a `Push/PopStyleVar(ImGuiStyleVar_CellPadding, ...)`
around just that `BeginTable`/`EndTable` if a column needs to be narrower than the theme's default
padding allows.
