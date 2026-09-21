# ImGui: overlapping InvisibleButtons and hover ownership

> Dear ImGui gives permanent hover priority to whichever overlapping item is submitted first each frame - a later item (e.g. a splitter/divider) placed on top via SetCursorScreenPos can never be hovered/dragged unless the earlier item opts out
>
> Migrated from the working notes on 2026-09-21 (last edited 2026-08-09).

When two ImGui items occupy the same screen region in the same frame (e.g. a big `InvisibleButton` canvas plus a thin splitter/divider `InvisibleButton` placed on top of it via `SetCursorScreenPos`), Dear ImGui's `ItemHoverable` gives permanent hover ownership to whichever item is **submitted first** for that mouse position. A later-submitted overlapping item is silently blocked from ever becoming hovered/active, no matter how large you make its hit region.

**Why:** Discovered while building a draggable gutter/ruler divider in [xgpu_imgui_timeline.h](../../../xGPU/source/tools/xgpu_imgui_timeline.h) — the divider's `InvisibleButton`, submitted after the main canvas `InvisibleButton`, could not be dragged even after repeatedly widening its hit region (6px → 10px → 14px). The real cause wasn't size, it was submission order: the canvas button (submitted first) always owned hover for the shared strip.

**Fix:** Call `ImGui::SetNextItemAllowOverlap()` immediately **before** the earlier/larger item (the one that should let something on top of it steal hover) — not before the later item. This is the modern (ImGui 1.92+) API; the old equivalent was `SetItemAllowOverlap()` called *after* the earlier item.

**How to apply:** Any time a widget is deliberately layered on top of another via `SetCursorScreenPos` (splitters, resize handles, drag handles over a canvas) and hover/click isn't registering on the top item, check submission order first before assuming it's a hit-region-size problem. Related: [xgpu_imgui_selectable_spanallcolumns_overlap](xgpu_imgui_selectable_spanallcolumns_overlap.md) is the converse problem (a single item's own hit-region swallowing clicks meant for sibling widgets).
