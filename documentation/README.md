# xGPU documentation

Design notes and hard-won findings, organised by the depot/area they belong to. Documentation for a
dependency lives in that dependency's own depot (`dependencies/<depot>/documentation`, or `doc` where
the depot already used that name); this folder holds what belongs to xGPU itself.

| Folder | Contents |
|---|---|
| [E29_LevelSceneEditor](E29_LevelSceneEditor) | Command/undo system, Asset Browser command layer, Play-mode property tweaks, save gating, Level Tree source-control column |
| [E27_NodeOS](E27_NodeOS) | Plugin DLL hot-reload sequence, thread_local pointer aliasing, screenshot capture |
| [ImGui](ImGui) | Dear ImGui behaviours that repeatedly caused real bugs here (hover ownership, table indent) |

Related documentation in other depots:

- `dependencies/xECSV2/doc` - scene save hardening, pool reallocation hazard, cross-DLL component lookup, scratch buffers
- `dependencies/xcontainer/documentation` - `FindAsReadOnly` and `noexcept` callbacks

Source comments reference these files by path. Do not reference private/working notes from source.
