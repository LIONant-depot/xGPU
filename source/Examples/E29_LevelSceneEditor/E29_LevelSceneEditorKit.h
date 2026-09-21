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
#include "dependencies/xeditor/include/xeditor/commands.h"
#include "dependencies/xeditor/include/xeditor/widgets.h"

#define XRESOURCE_PIPELINE_NO_COMPILER
#include "dependencies/xresource_pipeline_v2/source/xresource_pipeline.h"
#include "dependencies/xresource_pipeline_v2/source/editor/E10_Resources.h"
#include "dependencies/xresource_pipeline_v2/source/editor/E10_AssetMgr.h"
#include "dependencies/xresource_pipeline_v2/source/editor/E10_AssetBrowser.h"
#include "source/Examples/E29_LevelSceneEditor/E29_EditorTabs.h"

//-----------------------------------------------------------------------------------
// The include order of the editor's translation unit: the scene editor (the xscene plugin), then the Level editor's state
// and code, the resource commands and browser hooks, and the panels. The headers are not standalone: order matters.
//-----------------------------------------------------------------------------------

#include "dependencies/xresource_pipeline_v2/source/editor/E10_InspectorPickers.h"
#include "plugins/xscene.plugin/source/Editor/xscene_editor.h"

#include "source/Examples/E29_LevelSceneEditor/core/E29_EditorState.h"
#include "source/Examples/E29_LevelSceneEditor/commands/E29_CommandContext.h"
#include "source/Examples/E29_LevelSceneEditor/level/E29_LevelOps.h"
#include "source/Examples/E29_LevelSceneEditor/level/E29_SaveEverything.h"
#include "source/Examples/E29_LevelSceneEditor/level/E29_DocumentSession.h"

#include "dependencies/xresource_pipeline_v2/source/editor/E10_Commands_Assets.h"
#include "dependencies/xresource_pipeline_v2/source/editor/E10_Commands_AssetFiles.h"
#include "dependencies/xresource_pipeline_v2/source/editor/E10_AssetBrowserCallbacks.h"

#include "source/Examples/E29_LevelSceneEditor/level/E29_Panel_LevelTree.h"
#include "source/Examples/E29_LevelSceneEditor/extensions/game_module/E29_Panel_SystemRegistry.h"
#include "source/Examples/E29_LevelSceneEditor/extensions/command_console/E29_Panel_CommandConsole.h"
#include "dependencies/xresource_pipeline_v2/source/editor/E10_Panel_SourceControl.h"

#endif // E29_LEVEL_SCENE_EDITOR_KIT_H
