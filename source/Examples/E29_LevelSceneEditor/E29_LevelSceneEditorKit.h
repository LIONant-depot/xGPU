#ifndef E29_LEVEL_SCENE_EDITOR_KIT_H
#define E29_LEVEL_SCENE_EDITOR_KIT_H
#pragma once

#include "source/xGPU.h"

#include "dependencies/xmath/source/xmath.h"
#include "dependencies/xproperty/source/xcore/my_properties.h"
#include "dependencies/xproperty/source/examples/imgui/xPropertyImGuiInspector.h"
#include "dependencies/xstrtool/source/xstrtool.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <functional>
#include <unordered_set>
#include <cctype>
#include <cstring>

// xECSV2 - real Level/Scene resource types (see dependencies/xECSV2/src/xecs_level*.h,
// xecs_scene*.h) and the entity/component machinery this editor edits. Included BEFORE the
// resource-pipeline/asset-browser headers below so xecs.h's own narrow xresource_pipeline_v2
// includes (descriptor_base/info/factory/version - all #pragma once) are the ones that win; the
// asset-browser's own xresource_pipeline.h umbrella pull of the same four headers becomes a no-op.
#include "dependencies/xECSV2/src/xecs.h"

#define XRESOURCE_PIPELINE_NO_COMPILER
#include "dependencies/xresource_pipeline_v2/source/xresource_pipeline.h"
#include "dependencies/xresource_pipeline_v2/source/editor/E10_Resources.h"
#include "dependencies/xresource_pipeline_v2/source/editor/E10_AssetMgr.h"
#include "dependencies/xresource_pipeline_v2/source/editor/E10_AssetBrowser.h"
#include "source/Examples/E29_LevelSceneEditor/E29_EditorTabs.h"

//-----------------------------------------------------------------------------------
//
// E29 Level/Scene Editor KIT - everything from the original, single-file E29 example that isn't
// specific to ITS particular demo content (its own `transform` starter component, its own
// window/device bring-up in E29_Example()). Split out so a FUTURE editor wanting the same Level
// tree UI, prefab authoring/instancing, or entity-reference/override inspector wiring can reuse it
// directly instead of re-deriving it - matching this codebase's own existing convention of a
// reusable, header-only "kit" per concern (E10_AssetBrowser.h/E10_asset_browser_virtual_tree_tab.h
// are the model this follows: namespace named after the example that first grew the feature, reused
// by many later examples despite the number in the namespace name).
//
// Layout, in order: error-popup mechanism, the shared `name` component (the tree/prefab machinery
// below needs SOME component to label entities with - promoted here from "just E29's own demo
// content" to "part of the kit" for exactly that reason), resource-picker glue, editor_state,
// id-minting, folder bookkeeping, scene open/close + dependency-cycle guard, SaveEverything, and the
// entity_inspector_bridge (prefab-override + entity-reference inspector callback wiring).
// Two clusters that used to live inline in this same file now live under kit/, pulled in via
// #include at the exact point they used to appear (this file remains the one umbrella header a
// caller includes - see each moved file's own top comment for why it isn't meant to stand alone):
// prefab lookup/override bookkeeping + prefab creation/instancing/deletion/drag-drop
// (E29_PrefabOverrides.h, E29_PrefabAuthoring.h - phase 2 of the kit split), and the three big UI
// panels, Level tree/Entity Properties/System Registry (E29_Panel_*.h - phase 1).
//
//-----------------------------------------------------------------------------------

#include "source/Examples/E29_LevelSceneEditor/core/E29_Notify.h"
#include "source/Examples/E29_LevelSceneEditor/scene/E29_NameComponent.h"
#include "source/Examples/E29_LevelSceneEditor/scene/E29_InspectorPickers.h"
#include "source/Examples/E29_LevelSceneEditor/core/E29_EditorState.h"
#include "source/Examples/E29_LevelSceneEditor/level/E29_LevelOps.h"
#include "source/Examples/E29_LevelSceneEditor/scene/E29_SceneDependencies.h"

#include "scene/E29_PrefabOverrides.h"
#include "scene/E29_PrefabAuthoring.h"

// e29::commands::Run/FormatSceneGuid (E29_CommandContext.h, lightweight - no dependency on
// DeleteEntitySubtree itself, but needs e29::g_pGameMgr/g_pState, which E29_PrefabAuthoring.h just
// declared above) needed by ShowCreateMenuItems' own "New Entity" branch, right below - closed/
// reopened around this include for the same ODR-nesting reason E29_Commands_PropertyEdit.h's own
// include comment explains (this file declares its own `namespace e29::commands { ... }` at file
// scope).
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"

#include "source/Examples/E29_LevelSceneEditor/scene/E29_CreateMenu.h"
#include "source/Examples/E29_LevelSceneEditor/level/E29_SaveEverything.h"

// Level document Close / Save-before-open (File menu + double-click/drop). Own namespace e29
// block - included AFTER the kit's namespace closes (same ODR-nesting rule as PropertyEdit).
#include "level/E29_DocumentSession.h"

// Property-edit command (phase 2 of the kit split's own follow-on, documentation/E29_LevelSceneEditor/command_undo_system_plan.md
// memory) included directly here, not relying on E29_LevelScene_Editor.cpp's own later include -
// entity_inspector_bridge, right below, needs it. Same self-sufficiency reasoning as
// level/E29_Panel_LevelTree.h's own top comment for why. Closed/reopened around this include (rather
// than included mid-namespace like the earlier, WRONG version of this edit was) because
// E29_Commands_PropertyEdit.h declares its own `namespace e29::commands { ... }` at file scope - if
// this #include ran while namespace e29 was already open, that would nest into e29::e29::commands
// instead, exactly the ODR-nesting bug this comment is here to prevent regressing.
#include "source/Examples/E29_LevelSceneEditor/scene/commands/E29_Commands_PropertyEdit.h"
#include "source/Examples/E29_LevelSceneEditor/scene/commands/E29_Commands_EntityReference.h"
#include "source/Examples/E29_LevelSceneEditor/extensions/asset_browser/E29_Commands_AssetBrowser.h"
// Needed here (not just from E29_LevelScene_Editor.cpp's own later include) because
// RegisterAssetBrowserCallbacks, just below, now also wires the raw-file hooks and needs
// e29::commands::EncodeAssetPath - include guards make the .cpp's own separate include harmless.
#include "source/Examples/E29_LevelSceneEditor/extensions/asset_browser/E29_Commands_AssetFiles.h"

#include "source/Examples/E29_LevelSceneEditor/scene/E29_EntityInspectorBridge.h"
#include "source/Examples/E29_LevelSceneEditor/extensions/asset_browser/E29_AssetBrowserCallbacks.h"

#include "level/E29_Panel_LevelTree.h"
#include "scene/E29_Panel_ComponentSelector.h"
#include "scene/E29_Panel_EntityProperties.h"
#include "extensions/game_module/E29_Panel_SystemRegistry.h"
#include "extensions/command_console/E29_Panel_CommandConsole.h"
#include "extensions/source_control/E29_Panel_SourceControl.h"

#endif // E29_LEVEL_SCENE_EDITOR_KIT_H
