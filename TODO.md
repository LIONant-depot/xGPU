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
- [x] Fixed 2026-09-22: `example.lionprj/Cache/Plugins/xscript_module.plugin` (the E29 game-module scripting
      core) had no git remote of its own - it was a plain local folder on this dev machine, fully covered by
      `example.lionprj`'s own `Cache/` gitignore rule, so it had never been pushed anywhere, and a fresh
      clone hit `error C1083: Cannot open include file 'plugins/xscript_module.plugin/...'`. Pushed as its
      own repo (`LIONant-depot/xscript_module.plugin`, same header-only shape as xscene/xlevel) and added to
      the same CMakeLists.txt clone-on-demand `foreach`.
- [x] Fixed 2026-09-22: `xgeom_skin.plugin`'s local clone had two remotes - `origin` (a personal fork,
      `nickreal03/xgeom_skin.plugin`) and `lionant` (the canonical `LIONant-depot/xgeom_skin.plugin`, which
      every other plugin's `origin` points straight at). Tonight's "push it all" pushed to `origin` only, so
      the canonical repo Install.bat actually clones from stayed on an old commit - a fresh clone built an
      xgeom_skin.plugin with no Editor/ at all. Pushed the same commits to `lionant` too. No other plugin has
      this second remote; checked all of tonight's other pushes and they only have one `origin`, already
      correct.
- [x] Verified 2026-09-22: with all of the above, a genuine fresh `git clone` of xGPU from GitHub (not the
      dev tree, not the stale scratch clone) configures, generates and builds `xGPU_unit_test.exe` (Release)
      with zero compile errors - the flow the user's original report was blocked on.
