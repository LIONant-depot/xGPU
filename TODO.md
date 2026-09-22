# Open items

Tracks work the editor sessions have flagged but not closed. Add to this file instead of leaving a "handoff"
note in Build/; delete a line once it's actually fixed and verified.

## Editor command coverage (from the "set any setting/property" pass, 2026-09-22)

- [ ] Anim Package editor has no `ListPreview` (its viewport settings aren't exposed as a preview struct yet).
- [ ] Material graph editor: canvas zoom/pan and node selection have no commands (everything else - create/delete/
      connect/move/properties/shader text - does).
- [ ] No command to insert, delete or move an element in the middle of a list property; only append/resize via a
      path ending in `[]` plus `SetProperty`, or the inspector's `SnapshotEdit`.
- [ ] `SetPreview`/`SetProperty` on a bogus enum item name is now refused (fixed) - keep an eye out for the same
      "silently accepted" shape elsewhere if a new enum-backed command is added.

## E29 smoke suite

- [ ] 13 of 84 tests fail on a clean run (`source/Examples/E29_LevelSceneEditor/smoke`), the same 13 every time:
      `test_components.py` (3), `test_folders.py` (1), `test_play_more.py` (2), `test_read_only_queries.py` (1),
      `test_robustness.py` (1), `test_scenes_and_levels.py` (5). Spot-checked three: they expect old reply text
      (`ClearSelection` should answer with a message but gets `""`; `CompileStatus` text format changed;
      `CloseScene` reply wording changed). Likely stale expectations, not regressions, but not confirmed against
      a pre-change baseline - do that before rewriting the assertions.

## Known gaps, longer-standing

- [ ] Material editor's PBR preview logs `vkCmdDrawIndexed(): ... set 2 not bound` Vulkan validation errors; the
      E19 example the preview was copied from does the same, so it's pre-existing, not a regression.
- [ ] E23 GPU id-buffer picking and packed edge labels were not ported to the plugin Skeleton editor; it uses CPU
      ray-picking and plain name labels instead. Revisit if picking accuracy on dense skeletons becomes an issue.
- [ ] Static Geom and Skin Geom editors duplicate their node-hierarchy command code; candidate to unify into one
      template once a third consumer shows up.
- [ ] [Command Console pipe intermittent hang](xgpu_command_console_pipe_intermittent_hang.md) - UNRESOLVED,
      E27_NodeOS only so far, not reproduced under heavier stress on E29's own pipe. Pace pipe calls as a
      workaround until root-caused.

## Build

- [ ] `D:\xgpu_test\xGPU` is a stale scratch clone (no `dependencies/`, no `plugins` symlink) - never build or
      edit there; use `D:\LIONant\xGPU`, the live tree, with `Build/xGPUExamples.vs2022`.
- [x] Fixed 2026-09-22: a fresh clone's CMake configure failed at the Generate step ("Cannot find source
      file plugins/xscene.plugin/source/Editor/xscene_prefab_overrides.h") because `xscene.plugin` and
      `xlevel.plugin` were never cloned anywhere - `Install.bat`'s `PLUGINS` list only covers the
      resource-pipeline plugins with a `build/CreateAndBuildProject.bat`, which these two header-only
      editor plugins don't have. Fixed by giving them the same direct `git clone` CMake already did for
      `xrtcs.plugin` for the same reason. Verified end-to-end against a genuine fresh clone. Also fixed
      four dead `include_directories` calls with a stray extra `dependencies/` in the path (xgeom_static,
      xgeom_skin, xtexture, xmaterial) - harmless (CMake doesn't error on a missing include dir) but wrong.
- [x] Fixed 2026-09-22, same pass: `xeditor` (the shared editor framework - `dependencies/xeditor`) was never
      fetched anywhere either; only present on this dev machine by a past manual clone. Added it to the
      `FetchAndPopulate` list alongside the other ~20 dependencies there.
- [ ] BLOCKS a truly fresh clone from finishing (found verifying the two fixes above end-to-end):
      `example.lionprj/Cache/Plugins/xscript_module.plugin` (the E29 game-module scripting core) has no git
      remote of its own - it's a plain local folder on this dev machine, fully covered by `example.lionprj`'s
      own `Cache/` gitignore rule, so it has never been pushed anywhere. `E29_GamePluginLoad.h` `#include`s
      `plugins/xscript_module.plugin/source/Runtime/xscript_registration.h`, so a fresh clone hits
      `error C1083: Cannot open include file` there once the xscene/xlevel/xeditor gaps above are fixed.
      Needs a decision, not just a fix: push it as its own `xscript_module.plugin` repo (matching every
      other plugin) and wire it into `Install.bat`/CMakeLists.txt the same way, or is it not meant to ship
      in a fresh clone yet?
