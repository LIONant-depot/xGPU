#pragma once
// Shared top-of-file includes, factored out of the original monolithic E27_NodeOS_Editor.cpp so
// every extracted Editor/NodeOS_*.h header can pull in the same, already-verified include set with
// one line instead of each guessing its own minimal subset. #pragma once makes repeated inclusion
// (every one of the 13 headers includes this first) free after the first hit.
#include "source/xGPU.h"
#include "source/tools/xgpu_imgui_breach.h"
#include "source/tools/xgpu_view.h"

#include <atomic>              // std::atomic<int> compile counter, see CompilePluginWorker
#include <mutex>               // guards the shared plugin PCH rebuild, see EnsurePluginPchFresh; also command_console_pipe_bridge
#include <thread>              // CommandConsolePipeThreadMain's own background thread
#include <condition_variable>  // command_console_pipe_bridge's request/response handoff

#include "source/Examples/E27_NodeOS/SDK/xnode_os_plugin_api.h"
#include "source/Examples/E27_NodeOS/SDK/xnode_os_host_interface.h"
#include "source/Examples/E27_NodeOS/SDK/xnode_os_shared_types.h"

// Reusing E19_MaterialEditor's syntax-highlighting TextEditor widget (already compiled into this
// same xGPU_unit_test target) to show the codegen backend's generated C++ as a real docked window,
// same pattern as E19's own GLSLEditor - a persistent instance, SetText() only when new source is
// generated, Render() called every frame.
#include "source/Examples/E19_MaterialEditor/E19_TextEditor.h"

// For WriteScreenshotImage - already compiled into this same xGPU_unit_test target (E05's own
// bitmap inspector uses it identically) - real PNG output via xbmp::tools::writers::SaveSTDImage,
// not a hand-rolled TGA writer.
#include "dependencies/xbmp_tools/src/xbmp_tools.h"

// The real, official property inspector - the host draws every plugin's properties uniformly with
// this over the node's own real getProperties() object (see xnode_os_plugin_api.h's top comment for
// why that's safe across the DLL boundary now). Only the header: the real .cpp implementation is
// already compiled once, into this same executable, by E04_Properties.cpp - including the .cpp here
// too would double-define every symbol in it.
#include "dependencies/xproperty/source/examples/imgui/xPropertyImGuiInspector.h"

// For the whole graph file (Nodes/Links/xProperties records) - see SaveGraph/LoadGraph/
// SerializeReflectedMembers below.
#include "dependencies/xtextfile/source/xtextfile.h"
// The official reflected-object <-> xtextfile serializer (xproperty::sprop::serializer::Stream) -
// SerializeReflectedMembers below delegates straight to this rather than hand-walking properties,
// same as every other editor in this codebase (E10/E20/E21/E23/E24/E25) already does for its own
// descriptors.
#include "dependencies/xproperty/source/sprop/property_sprop_xtextfile_serializer.h"

// The command/undo layer: every graph mutation (add/delete node, connect, reorder, edit a property,
// change selection) becomes a string command executed through xundo::system::Execute(), which has
// zero ImGui/xgpu dependency - the ImGui interaction code below builds a command string and calls
// the same entry point a future headless runner or driver plugin would call. See the Commands
// section near the bottom of this file, right before E27_Example().
#include "dependencies/xundo/source/xundo_system.h"
#include "dependencies/xundo/source/xundo_history.h"
#include "dependencies/xstrtool/source/xstrtool.h"

// Node/link ids are random 64-bit instance guids (xresource::guid_generator::Instance64()), not a
// sequential counter - this engine's own established identity convention (see e.g. xmaterial.plugin's
// resource_type_guid_v). A monotonic counter starting fresh per process is a real collision risk the
// moment more than one actor can mint ids - a future headless runner, an AI agent issuing its own
// AddNode commands, or just two saved graphs ever getting merged - since every one of those would
// restart from 1. A random 64-bit value carries the same std::uint64_t type everywhere an id already
// flows (xtextfile fields, std::set<uint64_t> selection, command strings), so nothing else changes.
#include "dependencies/xresource_guid/source/xresource_guid.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifdef ERROR
    #undef ERROR
#endif

#include <string>
#include <vector>
#include <format>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <climits>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <unordered_map>
#include <deque>
#include <set>
#include <future>
#include <chrono>
