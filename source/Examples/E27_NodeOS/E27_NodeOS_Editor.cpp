#include "source/xGPU.h"
#include "source/tools/xgpu_imgui_breach.h"
#include "source/tools/xgpu_view.h"

#include <atomic> // std::atomic<int> compile counter, see CompilePluginWorker
#include <mutex>  // guards the shared plugin PCH rebuild, see EnsurePluginPchFresh

#include "source/Examples/E27_NodeOS/SDK/xnode_os_plugin_api.h"
#include "source/Examples/E27_NodeOS/SDK/xnode_os_host_interface.h"
#include "source/Examples/E27_NodeOS/SDK/xnode_os_shared_types.h"

// Reusing E19_MaterialEditor's syntax-highlighting TextEditor widget (already compiled into this
// same xGPU_unit_test target) to show the codegen backend's generated C++ as a real docked window,
// same pattern as E19's own GLSLEditor - a persistent instance, SetText() only when new source is
// generated, Render() called every frame.
#include "source/Examples/E19_MaterialEditor/E19_TextEditor.h"

// The real, official property inspector - the host draws every plugin's properties uniformly with
// this over the node's own real getProperties() object (see xnode_os_plugin_api.h's top comment for
// why that's safe across the DLL boundary now). Only the header: the real .cpp implementation is
// already compiled once, into this same executable, by E04_Properties.cpp - including the .cpp here
// too would double-define every symbol in it.
#include "dependencies/xproperty/source/examples/imgui/xPropertyImGuiInspector.h"

// For the whole graph file (Nodes/Links/xProperties records) - see SaveGraph/LoadGraph/
// SerializeReflectedMembers below.
#include "dependencies/xtextfile/source/xtextfile.h"

// The command/undo layer: every graph mutation (add/delete node, connect, reorder, edit a property,
// change selection) becomes a string command executed through xundo::system::Execute(), which has
// zero ImGui/xgpu dependency - the ImGui interaction code below builds a command string and calls
// the same entry point a future headless runner or driver plugin would call. See the Commands
// section near the bottom of this file, right before E27_Example().
#include "dependencies/xundo/source/xundo_system.h"

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

//-----------------------------------------------------------------------------------
//
// E27 - Node OS: the actual point of the RTCS/ai_programming research was never "fill out a form
// and get a manifest" (see E26) - it's a composable OS where wiring nodes together produces a real
// program that executes and does something. And critically: a node type must not require stopping
// the whole system and rebuilding it in Visual Studio to exist - it has to be creatable from
// *inside* the running tool.
//
// So this example proves the actual load-bearing claim: a node's behavior lives in its own .cpp
// file (source/Examples/E27_NodeOS/Plugins/<Name>/*.cpp), completely absent from this executable's own build -
// grep the CMakeLists.txt, it is not there. Pressing "Compile & Load" in the Node Library panel
// below shells out to the local MSVC toolchain (vcvarsall.bat + cl.exe /LD) right now, while this
// program is running, turns that .cpp into a DLL, and LoadLibrary's it - the resulting node type
// appears in the canvas's Add Node palette with zero CMake reconfigure and zero Visual Studio IDE
// involvement. Wire two such nodes together and press Execute: the host calls straight into code
// it was never compiled with, in dependency order, and shows the real result.
//
// The canvas itself is hand-rolled (plain ImDrawList calls), not a third-party node-editor library -
// the graph view is the single most important piece of a node-based system, and depending on someone
// else's library for it means never fully owning it. Its design (auto vertical stacking by order, no
// free dragging, orthogonal "highway" wire routing with per-side lane packing, a port's rendered side
// chosen by wire direction so no wire ever crosses over its own destination node, shape/color/fill
// visual encoding) is a direct port of _ai_programming/ai_programming/rslgraph-ui's own SVG canvas
// (apps/rslgraph-ui/src/canvas/{Canvas,NodeView,geometry}.tsx) - the original prototype's design for
// exactly this problem, translated from React+SVG to ImGui draw-list calls. rslgraph-ui itself never
// implemented node/link selection or deletion; this port adds both.
//
//-----------------------------------------------------------------------------------

namespace nodeos
{
    // The editor's own chrome palette - neutral Unity-Editor-style dark grays instead of the
    // previous navy/slate-blue scheme, so TypeColor's own much more saturated per-type pin/wire
    // colors have a quiet, recessive background to actually stand out against, the same
    // relationship Unity's own node editors (Shader Graph, Visual Scripting) keep between chrome
    // and content. Named and centralized here - rather than the scattered inline IM_COL32 literals
    // every other color in this file still uses - specifically because "give the editor a real
    // theme" is the point of this block; approximate hand-picked values, not a pixel-exact Unity
    // palette, refine on request if exact parity matters. Declared at the very top of the namespace,
    // ahead of everything else, so every function below (including small early ones like mesh_
    // preview_system's own border color) can reference it regardless of where it's defined.
    namespace theme
    {
        constexpr ImU32 NodeBg     = IM_COL32(58, 58, 58, 255);    // node body fill
        constexpr ImU32 NodeHeader = IM_COL32(85, 85, 85, 255);    // node title-row strip - brighter than the body, so the name/category line reads as its own header at a glance
        constexpr ImU32 NodeBorder = IM_COL32(8, 8, 8, 255);       // node outline / Scope-pin color - near-black, a crisp line against the body rather than a soft slate blend
        constexpr ImU32 CanvasDark = IM_COL32(28, 28, 28, 255);    // unconnected-pin fill / darkest recesses
        constexpr ImU32 Rail       = IM_COL32(46, 46, 46, 255);    // per-column background rail lines
        constexpr ImU32 Selected   = IM_COL32(58, 121, 187, 255);  // Unity's own selection-outline blue
        constexpr ImU32 Canvas     = IM_COL32(16, 16, 16, 255);    // the graph window's own backdrop, behind the dot grid and every node
        constexpr ImU32 Grid       = IM_COL32(32, 32, 32, 255);    // grid dots - a shade lighter than Canvas, subtle, never competing with node/wire content
    }

    // printf() alone is not reliable here: this is a GUI-subsystem executable, and stdout redirection
    // from a shell does not reliably reach it (confirmed empirically - every captured log this session
    // was silently empty regardless of what actually ran). Appending to a real file on disk is the only
    // diagnostic channel that's actually verifiable after the fact.
    static void Debugger(std::string_view View)
    {
        printf("%s\n", View.data());
        if (FILE* pFile = std::fopen("D:/LIONant/xGPU/source/Examples/E27_NodeOS/nodeos_debug.log", "a"))
        {
            std::fwrite(View.data(), 1, View.size(), pFile);
            std::fputc('\n', pFile);
            std::fclose(pFile);
        }
    }

    //------------------------------------------------------------------------------------------------
    // A node type currently available to place on the canvas - always the direct result of a
    // successful compile+load, never hardcoded here.
    //------------------------------------------------------------------------------------------------
    struct available_node_type
    {
        std::string                        m_DisplayName;   // "<plugin display name> :: <node name>"
        HMODULE                             m_Module  = nullptr;
        xnode_os_node_factory*              m_pFactory = nullptr;
        std::string                         m_SourcePath;    // the plugin_source_entry this came from - only for recompiling; not an identity
        std::string                         m_DirName;       // the plugin's Plugins/<DirName>/ folder name - the actual identity (see plugin_source_entry)
    };

    //------------------------------------------------------------------------------------------------
    // One instance of a node type dropped on the canvas. m_pNode IS the node - its own property
    // members, ports, and Execute all live on the polymorphic object itself (see
    // xnode_os_plugin_api.h), not a separate opaque blob. The module its factory lives in is never
    // FreeLibrary'd, even across a plugin recompile (see MergeCompileResult), so m_pNode stays valid
    // for this instance's whole life.
    //------------------------------------------------------------------------------------------------
    struct node_instance
    {
        std::uint64_t                    m_Id = 0;
        xnode_os_node*                   m_pNode = nullptr;
        std::uint64_t                    m_SpineId = 0;       // which spine (see `spine` below) this box belongs to
        int                              m_Order = 0;         // stacking rank, dense WITHIN m_SpineId only (never across the whole graph) - reorder with the header's up/down buttons, never freely dragged
        std::vector<void*>                m_CachedOutputs;      // filled after a successful Execute
        bool                              m_bHasRun = false;
        std::string                       m_LastError;
        std::uint64_t                    m_OwnedEndId = 0;    // 0 = doesn't own a marker. A control-flow node (If/ForEachLoop, see NODE_SCRIPTING_DESIGN.md section 4.1) owns a paired End/End-Else marker node, created and destroyed together with it - never an ordinary, independently-editable link.
    };

    struct link_instance
    {
        std::uint64_t m_Id = 0;
        std::uint64_t m_SourceNode = 0; int m_SourceOutput = 0;
        std::uint64_t m_TargetNode = 0; int m_TargetInput  = 0;
        bool          m_bReadOnly  = false; // an owner<->End ownership link (NODE_SCRIPTING_DESIGN.md section 4.1) - can never be dragged loose or deleted independently; only removed when one of its two nodes is deleted (which removes both, via DeleteNodes' cascade)
    };

    //------------------------------------------------------------------------------------------------
    // The horizontal container a spine (or several) lives in. Columns form a plain doubly-linked
    // list (never a tree) with exactly one root - today's original, always-there column. A column's
    // width is its own boxes + its own highway lane extent, recomputed fresh every frame in
    // DrawGraphCanvas like everything else here - nothing about live geometry is ever stored here.
    //------------------------------------------------------------------------------------------------
    struct column
    {
        std::uint64_t m_Id      = 0;
        std::uint64_t m_LeftId  = 0;  // 0 = no neighbor yet
        std::uint64_t m_RightId = 0;  // 0 = no neighbor yet
        bool          m_bIsRoot = false; // exactly one column ever has this set
    };

    //------------------------------------------------------------------------------------------------
    // A vertical chain of boxes connected up/down - pure connectivity, doesn't know or care about
    // highways/lanes at all (that's the column's concern). A column can host more than one spine, as
    // long as their vertical Y-ranges don't overlap. A spine's position is just (m_Y, m_ColumnId) -
    // plain, absolute, directly settable world-space coordinates, not derived from anything else. It
    // doesn't remember where it came from, only where it currently is, so it can be freely dragged to
    // a new Y (same column) or a new column later without carrying any stale history. Root ignores
    // m_Y (always geo::TOP) and never changes m_ColumnId.
    //------------------------------------------------------------------------------------------------
    struct spine
    {
        std::uint64_t m_Id       = 0;
        std::uint64_t m_ColumnId = 0;
        bool          m_bIsRoot  = false; // exactly one spine
        float         m_Y        = 0.0f;  // absolute world Y of this spine's own top slot (root ignores this - always geo::TOP)
    };

    //------------------------------------------------------------------------------------------------
    // The result of compiling+loading one plugin - a plain value (no shared state touched while
    // building it) so it can be produced on a background thread via std::async and handed back to the
    // main thread to merge, for the Node Library panel's "Compile & Load"/"Recompile & Reload" button.
    // Defined here, ahead of plugin_source_entry, since std::future<T> needs T complete at the point
    // plugin_source_entry declares its m_Future member below.
    //------------------------------------------------------------------------------------------------
    struct plugin_compile_result
    {
        bool                     m_bSuccess = false;
        std::string               m_Log;
        HMODULE                   m_Module   = nullptr;
        xnode_os_node_factory*    m_pFactory = nullptr;
    };

    // A source file the Node Library panel offers to compile - just a display name + path to a .cpp
    // implementing the plugin ABI. Adding a new native node kind to this demo means dropping another
    // folder under Plugins/ (see ScanPluginSources) - never touching this file's own build.
    struct plugin_source_entry
    {
        std::string m_DisplayName;
        std::string m_SourcePath;
        // The plugin's folder name under Plugins/ (e.g. "CubeNode") - the actual identity used in
        // AddNode commands and the saved graph file's "Source" field, NOT m_SourcePath. A folder name
        // is guaranteed unique within this Plugins/ tree (ScanPluginSources walks it), unlike a full
        // path, which is absolute and machine/checkout-specific - a saved graph referencing a folder
        // name stays meaningful if the repo ever moves, and an AI agent constructing an AddNode command
        // doesn't need to know or guess this machine's absolute path layout.
        std::string m_DirName;
        std::string m_CompileLog;
        bool         m_bLoaded = false;
        HMODULE      m_Module  = nullptr; // this source's currently-registered module, if any - lets a
                                            // recompile find and prune its OWN previous AvailableTypes
                                            // entries before adding the fresh ones (see CompileAndLoadPlugin)
        bool                                  m_bCompiling = false; // true while the "Compile & Load"/
                                                                     // "Recompile & Reload" button's async
                                                                     // compile (below) is in flight
        std::future<plugin_compile_result>    m_Future;
    };

    // A pin id used to be NodeId*10000+offset+PortIndex, which relied on NodeId being a small
    // sequential counter so multiplying it out couldn't wrap. Now that ids are random 64-bit guids
    // (xresource::guid_generator::Instance64()), that scheme breaks structurally, not just
    // probabilistically: 10000 shares a factor of 16 with 2^64, so the multiply is 16-to-1, not
    // bijective - distinct NodeIds can fold onto the exact same encoded value. A proper bit-mixing
    // combine (the splitmix64/MurmurHash3 finalizer) avalanches instead, so collisions drop to
    // ordinary 64-bit-hash odds rather than being guaranteed for a real subset of inputs.
    static std::uint64_t MixPinId(std::uint64_t NodeId, int PortIndex, std::uint64_t Salt) noexcept
    {
        std::uint64_t X = NodeId + 0x9E3779B97F4A7C15ull * (std::uint64_t)(PortIndex + 1) + Salt;
        X = (X ^ (X >> 30)) * 0xBF58476D1CE4E5B9ull;
        X = (X ^ (X >> 27)) * 0x94D049BB133111EBull;
        X = X ^ (X >> 31);
        return X;
    }
    static std::uint64_t OutPinOf(std::uint64_t NodeId, int PortIndex) noexcept { return MixPinId(NodeId, PortIndex, 0x1ull); }
    static std::uint64_t InPinOf (std::uint64_t NodeId, int PortIndex) noexcept { return MixPinId(NodeId, PortIndex, 0x2ull); }

    //------------------------------------------------------------------------------------------------
    // The host's own implementation of the interface handed to a plugin's NodeOS_CreateFactory. Truly
    // stateless (just routes Log() to the same Debugger() channel everything else here uses), so one
    // shared instance safely serves every plugin's CreateFactory call - there is nothing here a second
    // caller could ever clobber.
    //------------------------------------------------------------------------------------------------
    // A plain, growing line buffer for whatever a running program logs (Print's real Execute(),
    // eventually anything else) - the ONLY user-visible surface for ixnode_os_host::Log today; the
    // OS debug output Debugger() also writes to isn't visible to the user at all. Cleared once at
    // the start of each RunProgram (see below) so every run starts from a clean trace, not appended
    // across runs.
    static std::vector<std::string>& GetRuntimeLog()
    {
        static std::vector<std::string> s_Log;
        return s_Log;
    }

    class host_bridge final : public ixnode_os_host
    {
    public:
        void Log(const char* pMessage) noexcept override
        {
            if (!pMessage) return;
            Debugger(pMessage);
            GetRuntimeLog().emplace_back(pMessage);
        }
    };

    static host_bridge& GetHostBridge()
    {
        static host_bridge s_Bridge;
        return s_Bridge;
    }

    // Every std::system() call this file ever makes to invoke the MSVC toolchain shares this ONE lock -
    // concurrent cl.exe/link.exe invocations from the same parent process (e.g. the PCH build racing a
    // different plugin's own compile, both firing during startup's auto-add-node burst) turned out to
    // silently kill one of them: confirmed empirically - a standalone test program calling system() on
    // the exact same generated .bat, in isolation, always succeeded, while the same .bat launched from
    // inside this app while ANOTHER compile was also in flight sometimes died ~45ms in, before ever
    // reaching its own cl.exe line, with system() still reporting exit code 0. Serializing every
    // compiler invocation (this one included) removes the race entirely.
    static std::mutex& CompilerInvocationMutex()
    {
        static std::mutex s_Mutex;
        return s_Mutex;
    }

    //------------------------------------------------------------------------------------------------
    // vcvarsall.bat's own vswhere.exe lookup and full INCLUDE/LIB/PATH setup is identical every time
    // it runs for this host process - re-running it from scratch on every single plugin compile is
    // pure avoidable latency. Run it exactly ONCE (the first time any plugin compiles), capture the
    // resulting environment, and hand back a block of "set VAR=value" lines any later compile .bat can
    // splice in directly instead of paying for vcvarsall.bat's own process + vswhere lookup again.
    //------------------------------------------------------------------------------------------------
    static const std::string& GetOrBuildVsEnvSetup()
    {
        static const std::string s_Cached = []() -> std::string
        {
            namespace fs = std::filesystem;
            const fs::path OutputDir = fs::path("D:/LIONant/xGPU/source/Examples/E27_NodeOS/CompiledPlugins");
            std::error_code Ec;
            fs::create_directories(OutputDir, Ec);
            const fs::path BatPath = OutputDir / "_vsenv_capture.bat";
            const fs::path LogPath = OutputDir / "_vsenv_capture.log";

            {
                std::ofstream Bat(BatPath);
                Bat << "@echo off\r\n";
                // vcvarsall.bat locates the VS install via vswhere.exe (bare name, relies on PATH) -
                // not guaranteed to be on PATH for whatever environment xGPU_unit_test.exe itself was
                // launched from, so it's added explicitly rather than assumed.
                Bat << "set \"PATH=%PATH%;C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\"\r\n";
                Bat << "call \"D:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat\" x64 >nul\r\n";
                Bat << "set > \"" << LogPath.string() << "\"\r\n"; // bare `set`: dumps every current env var as NAME=VALUE
            }
            // Not guarded by CompilerInvocationMutex() - this only ever runs vswhere.exe/vcvarsall.bat
            // (never cl.exe/link.exe, so it can't race one), and it's already exactly-once-safe via
            // this function's own static-local-initializer guarantee. Guarding it WOULD be actively
            // wrong: EnsurePluginPchFresh() calls this function while already holding that mutex, so
            // acquiring it again here (same thread, non-reentrant mutex) would deadlock on the very
            // first call - confirmed the hard way, this exact bug shipped once already.
            std::system(std::format("\"{}\"", BatPath.string()).c_str());

            std::ifstream LogFile(LogPath);
            std::string Line, Out;
            while (std::getline(LogFile, Line))
            {
                if (!Line.empty() && Line.back() == '\r') Line.pop_back();
                if (Line.find('=') == std::string::npos) continue;
                Out += "set \"" + Line + "\"\r\n";
            }
            return Out;
        }();
        return s_Cached;
    }

    //------------------------------------------------------------------------------------------------
    // A plugin's own include chain (xnode_os_plugin_api.h -> xproperty core -> xmath/xresource_guid)
    // costs ~1.5s of template-heavy parsing PER COMPILE, measured empirically (a truly empty .cpp with
    // the same flags compiles in ~40ms - link itself is ~65ms, so that 1.5s is neither process startup
    // nor linking, it's parsing these exact headers). A precompiled header covering that chain, built
    // once and reused via /Yu + /FI, turns that into a one-time cost. Rebuilt automatically whenever
    // any covered header's write-time is newer than the existing .pch, so editing the SDK while the
    // app is running is always picked up on the very next plugin compile - never silently stale.
    // Mutex-guarded since plugin compiles can run concurrently on background threads (std::async).
    // bPchReady's fallback in CompilePluginWorker means a build failure here (any cause) never breaks
    // a plugin compile - it just misses the speedup that one time.
    //------------------------------------------------------------------------------------------------
    static const std::filesystem::path& PluginPchOutputDir()
    {
        static const std::filesystem::path s_Dir = "D:/LIONant/xGPU/source/Examples/E27_NodeOS/CompiledPlugins";
        return s_Dir;
    }
    static const std::filesystem::path& PluginPchHeaderName()
    {
        // Deliberately a bare filename, not an absolute path - /Yc, /Yu and /FI must all name it
        // identically for MSVC to recognize the reuse, and /FI needs a bare name resolvable via /I
        // (added to every plugin's own compile line below) since the plugin's source file lives in a
        // different folder than this header.
        static const std::filesystem::path s_Name = "_plugin_pch.h";
        return s_Name;
    }
    static std::filesystem::path PluginPchFile() { return PluginPchOutputDir() / "_plugin_pch.pch"; }
    // The .obj produced alongside the .pch when it's (re)built - creating a PCH still really compiles
    // its pch.cpp, so every non-inline global that chain drags in (xproperty's static registration
    // objects, xresource::g_Mgr, ...) ends up defined in THIS .obj. /Yu alone doesn't pull it in - it
    // has to be passed as an extra input to every plugin's own link step below, or those symbols come
    // back as LNK2001 unresolved externals (confirmed empirically - this is exactly what happened on
    // the first attempt at wiring this up).
    static std::filesystem::path PluginPchObjFile() { return PluginPchOutputDir() / "_plugin_pch.obj"; }

    // Returns whether the PCH is actually usable right now - a plugin compile should NEVER fail just
    // because the speed optimization didn't pan out; the caller falls back to compiling without it.
    static bool EnsurePluginPchFresh()
    {
        std::lock_guard Lock(CompilerInvocationMutex());

        namespace fs = std::filesystem;
        const fs::path OutputDir  = PluginPchOutputDir();
        const fs::path PchHeader  = OutputDir / PluginPchHeaderName();
        const fs::path PchFile    = PluginPchFile();
        std::error_code Ec;
        fs::create_directories(OutputDir, Ec);

        // Every header this PCH transitively covers - traced by hand from xnode_os_plugin_api.h's own
        // #include chain. If any of these is newer than the existing .pch, the cache is stale.
        const fs::path CoveredHeaders[] =
        {
            "D:/LIONant/xGPU/source/Examples/E27_NodeOS/SDK/xnode_os_plugin_api.h",
            "D:/LIONant/xGPU/source/Examples/E27_NodeOS/SDK/xnode_os_host_interface.h",
            "D:/LIONant/xGPU/source/Examples/E27_NodeOS/SDK/xnode_os_shared_types.h",
            "D:/LIONant/xGPU/dependencies/xproperty/source/xcore/my_properties.h",
            "D:/LIONant/xGPU/dependencies/xproperty/source/xproperty.h",
            "D:/LIONant/xGPU/dependencies/xproperty/source/sprop/property_sprop_container.h",
            "D:/LIONant/xGPU/dependencies/xproperty/source/examples/imgui/my_property_ui.h",
            "D:/LIONant/xGPU/dependencies/xresource_guid/source/xresource_guid.h",
            "D:/LIONant/xGPU/dependencies/xmath/source/xmath.h",
        };

        const fs::path PchObjPath = OutputDir / "_plugin_pch.obj";
        bool bNeedsBuild = !fs::exists(PchFile) || !fs::exists(PchObjPath);
        if (!bNeedsBuild)
        {
            const auto PchTime = fs::last_write_time(PchFile, Ec);
            for (auto& H : CoveredHeaders)
                if (fs::exists(H) && fs::last_write_time(H, Ec) > PchTime) { bNeedsBuild = true; break; }
        }
        if (!bNeedsBuild) return true;

        const fs::path PchSource = OutputDir / "_plugin_pch.cpp";
        const fs::path PchLog    = OutputDir / "_plugin_pch_build.log";
        const fs::path PchBat    = OutputDir / "_plugin_pch_build.bat";

        // The two headers a plugin's own SDK include actually needs - everything else (xproperty,
        // xmath, xresource_guid) comes in transitively through these.
        { std::ofstream Header(PchHeader);
          Header << "#pragma once\r\n"
                 << "#include \"" << CoveredHeaders[0].string() << "\"\r\n"
                 << "#include \"" << CoveredHeaders[2].string() << "\"\r\n"; }
        { std::ofstream Source(PchSource); Source << "#include \"" << PluginPchHeaderName().string() << "\"\r\n"; }

        // The .ofstream MUST be closed (flushed to disk) before system() launches cmd.exe to read this
        // exact file - the other two .bat writers in this file (GetOrBuildVsEnvSetup,
        // CompilePluginWorker) already scope their own std::ofstream inside a nested { } block for
        // exactly this reason. Skipping that scoping here once caused a real, hard-to-diagnose bug:
        // cmd.exe would read whatever prefix of the file had already been flushed, hit a premature
        // EOF partway through, and exit "successfully" having never reached the cl.exe line at all.
        {
            std::ofstream Bat(PchBat);
            Bat << "@echo off\r\n";
            Bat << GetOrBuildVsEnvSetup();
            Bat << "cl.exe /nologo /c /EHsc /std:c++20 /MDd /DWIN32 /D_WINDOWS /D_DEBUG /DUNICODE /D_UNICODE "
                   "/I\"D:\\LIONant\\xGPU\\source\\Examples\\E27_NodeOS\\SDK\" /I\"D:\\LIONant\\xGPU\" "
                   "/Yc\"" << PluginPchHeaderName().string() << "\" /Fp\"" << PchFile.string() << "\" \""
                << PchSource.string() << "\" /Fo\"" << PchObjPath.string() << "\" > \"" << PchLog.string() << "\" 2>&1\r\n";
        }

        std::system(std::format("\"{}\"", PchBat.string()).c_str());

        // Verify it actually landed - if the build silently failed for any reason, every subsequent
        // plugin compile must fall back to not using the PCH rather than hard-failing on a missing
        // .pch/.obj.
        return fs::exists(PchFile) && fs::exists(PchObjPath);
    }

    //------------------------------------------------------------------------------------------------
    // Shells out to the local MSVC toolchain to turn one plugin .cpp into a DLL, right now, while this
    // program is running - then LoadLibrary's it and calls its NodeOS_CreateFactory, handing it a
    // host_bridge and getting back the one factory it exports. Pure: touches no shared state, safe to
    // run on a background thread (see the async path below) as well as inline.
    //------------------------------------------------------------------------------------------------
    static plugin_compile_result CompilePluginWorker(std::string SourcePath)
    {
        const bool bPchReady = EnsurePluginPchFresh();
        plugin_compile_result Result;
        namespace fs = std::filesystem;
        const fs::path Src        = SourcePath;
        const fs::path OutputDir  = fs::path("D:/LIONant/xGPU/source/Examples/E27_NodeOS/CompiledPlugins");

        // A recompile's own DLL is never FreeLibrary'd (MergeCompileResult keeps the old module alive
        // indefinitely so any already-placed node instance's m_pFactory keeps working) - which means the
        // OS still has the PREVIOUS build's exact .dll file locked. Reusing the same output filename
        // therefore made the linker fail with LNK1104 ("cannot open file") on every recompile past the
        // first. Each compile gets its own never-reused filename instead, so it's always writing
        // somewhere fresh - old DLLs just accumulate on disk for the life of the process, same as their
        // in-memory modules already do.
        static std::atomic<int> s_CompileCounter{ 0 };
        const std::string Unique = Src.stem().string() + "_" + std::to_string(++s_CompileCounter);
        const fs::path DllPath    = OutputDir / (Unique + ".dll");
        const fs::path BatPath    = OutputDir / (Unique + "_compile.bat");
        const fs::path LogPath    = OutputDir / (Unique + "_compile.log");

        std::error_code Ec;
        fs::create_directories(OutputDir, Ec);

        // A .bat is far more robust than trying to hand-escape a single system() command line with
        // spaces in "Program Files" and nested quotes. Two /I's always: the SDK folder (so a plugin
        // can write bare `#include "xnode_os_plugin_api.h"`) and the repo root (so a plugin wanting
        // properties can write the exact same `#include "dependencies/xproperty/source/xcore/
        // my_properties.h"` line every other example in this engine already uses). When the shared PCH
        // is actually ready, /Yu+/FI+/Fp force-inject and reuse it (skipping the ~1.5s of template-
        // heavy header parsing that chain costs otherwise - see EnsurePluginPchFresh's own comment for
        // the measurement) and its own .obj rides along as an extra link input (creating a PCH still
        // really compiles its own translation unit, so every non-inline global that chain drags in has
        // to be linked from somewhere). If the PCH build ever failed, this whole clause is skipped and
        // the plugin just compiles the plain way - correctness always wins over speed here.
        {
            std::ofstream Bat(BatPath);
            Bat << "@echo off\r\n";
            Bat << GetOrBuildVsEnvSetup(); // cached - no vcvarsall.bat/vswhere re-run on every compile
            // /MDd links the DYNAMIC debug CRT (shared across every plugin DLL and the host), not the
            // static one - same WIN32/_WINDOWS/_DEBUG/UNICODE defines as xGPU_unit_test's own Debug
            // config, so the SDK/xproperty headers see an identical build environment either side of
            // the DLL boundary.
            Bat << "cl.exe /nologo /LD /EHsc /std:c++20 /MDd /DWIN32 /D_WINDOWS /D_DEBUG /DUNICODE /D_UNICODE"
                   " /I\"D:\\LIONant\\xGPU\\source\\Examples\\E27_NodeOS\\SDK\" /I\"D:\\LIONant\\xGPU\"";
            if (bPchReady)
                Bat << " /I\"" << PluginPchOutputDir().string() << "\""
                       " /Yu\"" << PluginPchHeaderName().string() << "\" /FI\"" << PluginPchHeaderName().string() << "\""
                       " /Fp\"" << PluginPchFile().string() << "\"";
            Bat << " \"" << Src.string() << "\"";
            if (bPchReady)
                Bat << " \"" << PluginPchObjFile().string() << "\"";
            Bat << " /Fe:\"" << DllPath.string() << "\" /Fo:\"" << (OutputDir / (Unique + ".obj")).string() << "\" > \"" << LogPath.string() << "\" 2>&1\r\n";
        }

        const std::string Command = std::format("\"{}\"", BatPath.string());
        int ExitCode;
        { std::lock_guard Lock(CompilerInvocationMutex()); ExitCode = std::system(Command.c_str()); }

        std::ifstream LogFile(LogPath);
        std::stringstream LogStream;
        LogStream << LogFile.rdbuf();
        Result.m_Log = LogStream.str();

        if (ExitCode != 0 || !fs::exists(DllPath))
        {
            Result.m_Log += std::format("\n[compile failed, exit code {}]", ExitCode);
            return Result;
        }

        HMODULE Module = LoadLibraryA(DllPath.string().c_str());
        if (!Module)
        {
            Result.m_Log += "\n[LoadLibrary failed after a successful compile]";
            return Result;
        }

        auto pCreateFactory = (xnode_os_pfn_create_factory*)GetProcAddress(Module, XNODE_OS_CREATE_FACTORY_NAME);
        if (!pCreateFactory)
        {
            Result.m_Log += "\n[DLL loaded but does not export " XNODE_OS_CREATE_FACTORY_NAME "]";
            FreeLibrary(Module);
            return Result;
        }

        xnode_os_node_factory& Factory = pCreateFactory(GetHostBridge());

        Result.m_Log += std::format("\n[compiled and loaded successfully - '{}' node type registered]", Factory.getName());
        Result.m_bSuccess = true;
        Result.m_Module   = Module;
        Result.m_pFactory = &Factory;
        return Result;
    }

    // Applies a (possibly backgrounded) compile result to the shared, main-thread-only state - the
    // one point both the synchronous (Add Node menu's lazy first-compile) and async (Node Library
    // panel's button) paths funnel through, so the "prune this source's stale entries" rule never
    // has to be written twice.
    static bool MergeCompileResult(plugin_source_entry& Entry, plugin_compile_result& Result, std::vector<available_node_type>& OutTypes)
    {
        Entry.m_CompileLog = Result.m_Log;
        if (!Result.m_bSuccess) return false;

        // Recompiling an already-loaded plugin: prune whatever THIS source registered last time before
        // adding its fresh registration. Neither the old module NOR its factory is ever destroyed -
        // anything still referencing it (an already-placed node instance's m_pFactory included) keeps
        // working against it indefinitely; this only stops the stale entry from being offered again in
        // the Add Node menu.
        if (Entry.m_Module)
            std::erase_if(OutTypes, [&](auto& T) { return T.m_Module == Entry.m_Module; });

        OutTypes.push_back({ std::format("{} :: {}", Entry.m_DisplayName, Result.m_pFactory->getName()), Result.m_Module, Result.m_pFactory, Entry.m_SourcePath, Entry.m_DirName });

        Entry.m_bLoaded = true;
        Entry.m_Module  = Result.m_Module;
        return true;
    }

    // Synchronous convenience wrapper - used only by the Add Node menu's lazy first-compile
    // (EnsureLoadedAndGetType), where a type is needed immediately to place the node being clicked
    // right now. The Node Library panel's button uses the async path (CompilePluginWorker run via
    // std::async) instead, since it doesn't need an instant result.
    static bool CompileAndLoadPlugin(plugin_source_entry& Entry, std::vector<available_node_type>& OutTypes)
    {
        auto Result = CompilePluginWorker(Entry.m_SourcePath);
        return MergeCompileResult(Entry, Result, OutTypes);
    }

    // "CubeNode" -> "Cube Node" - a plain word-boundary split on a folder name, so a freshly-dropped
    // plugin folder gets a readable display name with zero manifest/registration file of its own.
    static std::string PrettifyFolderName(const std::string& Raw)
    {
        std::string Out;
        for (std::size_t i = 0; i < Raw.size(); ++i)
        {
            if (i > 0 && std::isupper((unsigned char)Raw[i]) && std::islower((unsigned char)Raw[i - 1])) Out += ' ';
            Out += Raw[i];
        }
        return Out;
    }

    //------------------------------------------------------------------------------------------------
    // Discovers every plugin the host can offer, by walking Plugins/<PluginFolder>/*.cpp - no manifest,
    // no registration call, no editing this file to add a new native node kind. Dropping a new
    // "MyThingNode/my_thing_node.cpp" here is the entire integration step; it shows up in the Add Node
    // menu the next time the program launches, in its NOT-yet-compiled state (compilation itself still
    // only happens lazily, the first time someone actually places one - see EnsureLoadedAndGetType).
    //------------------------------------------------------------------------------------------------
    static std::vector<plugin_source_entry> ScanPluginSources(const std::string& RootDir)
    {
        namespace fs = std::filesystem;
        std::vector<plugin_source_entry> Out;
        std::error_code Ec;
        if (!fs::exists(RootDir, Ec)) return Out;
        for (auto& Entry : fs::recursive_directory_iterator(RootDir, Ec))
        {
            if (Ec) break;
            if (!Entry.is_regular_file() || Entry.path().extension() != ".cpp") continue;
            const std::string DirName = Entry.path().parent_path().filename().string();
            Out.push_back({ PrettifyFolderName(DirName), Entry.path().string(), DirName });
        }
        std::sort(Out.begin(), Out.end(), [](auto& A, auto& B) { return A.m_DisplayName < B.m_DisplayName; });
        return Out;
    }

    // The Add Node menu offers plugin SOURCES, not compiled types (so an as-yet-unbuilt plugin can
    // still be listed and picked) - this resolves a source to its first exported type descriptor,
    // compiling+loading it right now if this is the very first time it's been placed, or just finding
    // the already-loaded entry (by its "<source display name> :: <node name>" prefix, the same string
    // CompileAndLoadPlugin builds) so picking a second instance never recompiles.
    static xnode_os_node_factory* EnsureLoadedAndGetType(plugin_source_entry& Source, std::vector<available_node_type>& AvailableTypes)
    {
        if (!Source.m_bLoaded)
        {
            const std::size_t Before = AvailableTypes.size();
            if (!CompileAndLoadPlugin(Source, AvailableTypes) || AvailableTypes.size() <= Before) return nullptr;
            return AvailableTypes[Before].m_pFactory;
        }
        const std::string Prefix = Source.m_DisplayName + " :: ";
        for (auto& T : AvailableTypes)
            if (T.m_DisplayName.rfind(Prefix, 0) == 0) return T.m_pFactory;
        return nullptr;
    }

    //------------------------------------------------------------------------------------------------
    // Every node-add path (right-click canvas, spine insert marker, the empty-canvas "+") funnels
    // through here so instance creation never gets forgotten at one of them - and its mirror,
    // destroying that same instance when a node is removed.
    //------------------------------------------------------------------------------------------------
    static node_instance CreateNodeInstance(std::uint64_t Id, xnode_os_node_factory* pFactory, int Order, std::uint64_t SpineId)
    {
        node_instance NewNode;
        NewNode.m_Id      = Id;
        NewNode.m_Order   = Order;
        NewNode.m_SpineId = SpineId;
        if (pFactory) NewNode.m_pNode = &pFactory->CreateNodeInstance();
        return NewNode;
    }

    static void DestroyNodeInstance(node_instance& Node)
    {
        if (Node.m_pNode && Node.m_pNode->m_pFactory)
            Node.m_pNode->m_pFactory->DestroyNodeInstance(*Node.m_pNode);
        Node.m_pNode = nullptr;
    }

    //================================================================================================
    // Commands - pure helpers (command-string building + xundo::system::Execute dispatch), split out
    // here (ahead of DrawGraphCanvas/DrawNodePropertiesPanel, which call them) from the actual
    // xundo::command_base-derived classes further down this file (which need
    // SerializePropertiesToString/ApplyPropertiesFromString, not defined until later) - see that
    // later "namespace commands" block's own comment for the full explanation. Every graph mutation
    // becomes a string command executed through System.Execute(), which has zero ImGui/xgpu
    // dependency: this is the same entry point a future headless runner or "command source" driver
    // plugin would call.
    //================================================================================================
    namespace commands
    {
        // Base64 - used ONLY for SetProperties' Before/After payloads (arbitrary property text that
        // could contain characters awkward for a space/tab-delimited command line, e.g. a file path).
        // Every other command's arguments are plain ids/csv-of-ids, which need no encoding at all.
        inline std::string Base64Encode(const std::string& In)
        {
            static constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string Out;
            Out.reserve(((In.size() + 2) / 3) * 4);
            std::size_t i = 0;
            for (; i + 2 < In.size(); i += 3)
            {
                const std::uint32_t N = (std::uint32_t(std::uint8_t(In[i])) << 16) | (std::uint32_t(std::uint8_t(In[i + 1])) << 8) | std::uint8_t(In[i + 2]);
                Out += Alphabet[(N >> 18) & 0x3F]; Out += Alphabet[(N >> 12) & 0x3F];
                Out += Alphabet[(N >> 6) & 0x3F];  Out += Alphabet[N & 0x3F];
            }
            const std::size_t Rem = In.size() - i;
            if (Rem == 1)
            {
                const std::uint32_t N = std::uint32_t(std::uint8_t(In[i])) << 16;
                Out += Alphabet[(N >> 18) & 0x3F]; Out += Alphabet[(N >> 12) & 0x3F]; Out += "==";
            }
            else if (Rem == 2)
            {
                const std::uint32_t N = (std::uint32_t(std::uint8_t(In[i])) << 16) | (std::uint32_t(std::uint8_t(In[i + 1])) << 8);
                Out += Alphabet[(N >> 18) & 0x3F]; Out += Alphabet[(N >> 12) & 0x3F]; Out += Alphabet[(N >> 6) & 0x3F]; Out += '=';
            }
            return Out;
        }

        inline std::string Base64Decode(const std::string& In)
        {
            auto DecodeChar = [](char C) -> int
            {
                if (C >= 'A' && C <= 'Z') return C - 'A';
                if (C >= 'a' && C <= 'z') return C - 'a' + 26;
                if (C >= '0' && C <= '9') return C - '0' + 52;
                if (C == '+') return 62;
                if (C == '/') return 63;
                return -1; // padding ('=') or terminator
            };
            std::string Out;
            Out.reserve((In.size() / 4) * 3);
            int Bits = 0, NumBits = 0;
            for (char C : In)
            {
                const int V = DecodeChar(C);
                if (V < 0) break;
                Bits = (Bits << 6) | V;
                NumBits += 6;
                if (NumBits >= 8)
                {
                    NumBits -= 8;
                    Out += static_cast<char>((Bits >> NumBits) & 0xFF);
                }
            }
            return Out;
        }

        // Node/link ids are guids (xresource::guid_generator::Instance64()), not small counting
        // numbers, so they're always WRITTEN as hex (0x-prefixed, matching this engine's own
        // Plugin.config convention for a u64 guid field - e.g. "#8B3C028882EA813D") rather than a huge,
        // unreadable decimal string. Parsing accepts EITHER form, in case a hand-typed or
        // agent-generated command uses plain decimal instead.
        inline std::string FormatGuid(std::uint64_t Id) { return std::format("0x{:016x}", Id); }
        inline std::uint64_t ParseGuid(const std::string& S)
        {
            if (S.size() > 2 && S[0] == '0' && (S[1] == 'x' || S[1] == 'X'))
                return std::stoull(S.substr(2), nullptr, 16);
            return std::stoull(S, nullptr, 10);
        }

        // Comma-separated guid lists - used by DeleteNodes/ReorderNodes/Select for "a set of node ids"
        // without needing a separate -flag per id. xcmdline requires a required option to have at
        // least 1 argument, so an empty set is encoded as the literal "-" rather than an empty string.
        inline std::string JoinIds(const std::vector<std::uint64_t>& Ids)
        {
            std::string Out;
            for (std::size_t i = 0; i < Ids.size(); ++i) { if (i) Out += ','; Out += FormatGuid(Ids[i]); }
            return Out.empty() ? std::string("-") : Out;
        }
        inline std::vector<std::uint64_t> SplitIds(const std::string& Csv)
        {
            std::vector<std::uint64_t> Out;
            if (Csv.empty() || Csv == "-") return Out;
            std::size_t Pos = 0;
            while (Pos < Csv.size())
            {
                const std::size_t Comma = Csv.find(',', Pos);
                Out.push_back(ParseGuid(Csv.substr(Pos, Comma == std::string::npos ? std::string::npos : Comma - Pos)));
                Pos = (Comma == std::string::npos) ? Csv.size() : Comma + 1;
            }
            return Out;
        }


        // Plugins are identified by their Plugins/<DirName>/ folder name, not by an absolute source
        // path - a folder name is guaranteed unique within this scan (see plugin_source_entry's own
        // comment) and stays meaningful across machines/checkouts, in a saved graph file, or in a
        // command an AI agent constructs without knowing this machine's absolute path layout.
        inline plugin_source_entry* FindSourceByDirName(std::vector<plugin_source_entry>& Sources, const std::string& DirName)
        {
            for (auto& S : Sources) if (S.m_DirName == DirName) return &S;
            return nullptr;
        }

        // Shared by create_node_cmd's own -After/-Before resolution and select_cmd's -MarkerAfter/
        // -MarkerBefore resolution (and now create_spine_cmd's -AnchorNode) - all three need the exact
        // same "which spine, and which dense order within it, does this already-known node id sit at"
        // lookup against the current node list.
        inline bool ResolveNodeSpineAndOrder(const std::vector<node_instance>& Nodes, std::uint64_t NodeId, std::uint64_t& OutSpineId, int& OutOrder) noexcept
        {
            for (auto& N : Nodes)
                if (N.m_Id == NodeId) { OutSpineId = N.m_SpineId; OutOrder = N.m_Order; return true; }
            return false;
        }

        // A control node and its owned End/End-Else marker are never independently deletable - the
        // pair is one unit (NODE_SCRIPTING_DESIGN.md section 4.1). Deleting either one pulls the other
        // in too. Used by DeleteNodes' Redo AND BackupCurrenState, so Undo restores exactly what was
        // actually removed, including anything cascade-added.
        inline std::vector<std::uint64_t> ExpandOwnershipCascade(const std::vector<node_instance>& Nodes, std::vector<std::uint64_t> Ids) noexcept
        {
            bool bChanged = true;
            while (bChanged)
            {
                bChanged = false;
                for (auto& N : Nodes)
                {
                    const bool bOwnerInSet = std::find(Ids.begin(), Ids.end(), N.m_Id) != Ids.end();
                    if (bOwnerInSet && N.m_OwnedEndId != 0 && std::find(Ids.begin(), Ids.end(), N.m_OwnedEndId) == Ids.end())
                    {
                        Ids.push_back(N.m_OwnedEndId);
                        bChanged = true;
                    }
                    const bool bMarkerInSet = N.m_OwnedEndId != 0 && std::find(Ids.begin(), Ids.end(), N.m_OwnedEndId) != Ids.end();
                    if (bMarkerInSet && !bOwnerInSet)
                    {
                        Ids.push_back(N.m_Id);
                        bChanged = true;
                    }
                }
            }
            return Ids;
        }

        inline void WriteString(xundo::undo_file& File, const std::string& S)
        {
            const std::uint32_t Len = static_cast<std::uint32_t>(S.size());
            File.Write(Len);
            if (Len) File.Write(S.data(), Len);
        }
        inline std::string ReadString(xundo::undo_file& File)
        {
            std::uint32_t Len = 0; File.Read(Len);
            std::string S; S.resize(Len);
            if (Len) File.Read(S.data(), Len);
            return S;
        }

        // Command-string builders - one per registered command name (see the xundo::command_base
        // classes further down this file). Pure string formatting, no xundo dependency at all.
        // CreateNode is addressed relative to an EXISTING node's id (-After/-Before), not a raw order
        // index or an invented "gap" identity - the id is something a caller (human or agent) already
        // has from having just created or observed that node, so no separate discovery/query step is
        // ever needed to place a new node relative to one that's already there. Neither flag given
        // means "append at the end" (or "the only node", if the graph is empty).
        inline std::string MakeCreateNodeAppend(std::uint64_t Id, const std::string& PluginDir)
        {
            return std::format("CreateNode -Id {} -PluginDir {}", FormatGuid(Id), PluginDir);
        }
        inline std::string MakeCreateNodeAfter(std::uint64_t Id, const std::string& PluginDir, std::uint64_t AfterNodeId)
        {
            return std::format("CreateNode -Id {} -PluginDir {} -After {}", FormatGuid(Id), PluginDir, FormatGuid(AfterNodeId));
        }
        inline std::string MakeCreateNodeBefore(std::uint64_t Id, const std::string& PluginDir, std::uint64_t BeforeNodeId)
        {
            return std::format("CreateNode -Id {} -PluginDir {} -Before {}", FormatGuid(Id), PluginDir, FormatGuid(BeforeNodeId));
        }
        // Same four placements as CreateNode, for a control node (If/ForEachLoop) that owns a
        // paired End/End-Else marker (NODE_SCRIPTING_DESIGN.md section 4.1) - the marker is always
        // created right after the owner, in the same spine, in the same command, along with the
        // read-only link between them (LinkId is caller-minted, same "Redo never invents an id" rule
        // as everything else - see connect_cmd's own MakeConnect for the existing precedent). This
        // link is never something the user drags into existence - the system wires it automatically,
        // and it's rendered read-only (Connect/DeleteLink both refuse to touch it) specifically
        // because it isn't an ordinary, user-editable connection.
        // Optional 2nd hop (Owner -> Mid -> End2) - see create_owned_pair_cmd's own comment. Nothing
        // uses this today (Function used to, before it merged its owned marker into itself), kept as
        // generic plumbing for a future owner type that genuinely needs a 2-level marker chain.
        // End2Id == 0 means "no second hop", the common (currently the only) case.
        struct owned_pair_2nd_hop { std::uint64_t m_End2Id = 0; std::string m_End2PluginDir; std::uint64_t m_Link2Id = 0; };
        inline std::string Opt2ndHopSuffix(const owned_pair_2nd_hop& Hop2)
        {
            if (Hop2.m_End2Id == 0) return {};
            return std::format(" -End2Id {} -End2PluginDir {} -Link2Id {}", FormatGuid(Hop2.m_End2Id), Hop2.m_End2PluginDir, FormatGuid(Hop2.m_Link2Id));
        }
        inline std::string MakeCreateOwnedPairAppend(std::uint64_t Id, const std::string& PluginDir, std::uint64_t EndId, const std::string& EndPluginDir, std::uint64_t LinkId, const owned_pair_2nd_hop& Hop2 = {})
        {
            return std::format("CreateOwnedPair -Id {} -PluginDir {} -EndId {} -EndPluginDir {} -LinkId {}", FormatGuid(Id), PluginDir, FormatGuid(EndId), EndPluginDir, FormatGuid(LinkId)) + Opt2ndHopSuffix(Hop2);
        }
        inline std::string MakeCreateOwnedPairAfter(std::uint64_t Id, const std::string& PluginDir, std::uint64_t EndId, const std::string& EndPluginDir, std::uint64_t LinkId, std::uint64_t AfterNodeId, const owned_pair_2nd_hop& Hop2 = {})
        {
            return std::format("CreateOwnedPair -Id {} -PluginDir {} -EndId {} -EndPluginDir {} -LinkId {} -After {}", FormatGuid(Id), PluginDir, FormatGuid(EndId), EndPluginDir, FormatGuid(LinkId), FormatGuid(AfterNodeId)) + Opt2ndHopSuffix(Hop2);
        }
        inline std::string MakeCreateOwnedPairBefore(std::uint64_t Id, const std::string& PluginDir, std::uint64_t EndId, const std::string& EndPluginDir, std::uint64_t LinkId, std::uint64_t BeforeNodeId, const owned_pair_2nd_hop& Hop2 = {})
        {
            return std::format("CreateOwnedPair -Id {} -PluginDir {} -EndId {} -EndPluginDir {} -LinkId {} -Before {}", FormatGuid(Id), PluginDir, FormatGuid(EndId), EndPluginDir, FormatGuid(LinkId), FormatGuid(BeforeNodeId)) + Opt2ndHopSuffix(Hop2);
        }
        inline std::string MakeCreateOwnedPairInSpine(std::uint64_t Id, const std::string& PluginDir, std::uint64_t EndId, const std::string& EndPluginDir, std::uint64_t LinkId, std::uint64_t SpineId, const owned_pair_2nd_hop& Hop2 = {})
        {
            return std::format("CreateOwnedPair -Id {} -PluginDir {} -EndId {} -EndPluginDir {} -LinkId {} -InSpine {}", FormatGuid(Id), PluginDir, FormatGuid(EndId), EndPluginDir, FormatGuid(LinkId), FormatGuid(SpineId)) + Opt2ndHopSuffix(Hop2);
        }

        // -InSpine is the only way to place a node into a currently-empty spine - there's no existing
        // node in it yet to address -After/-Before relative to.
        inline std::string MakeCreateNodeInSpine(std::uint64_t Id, const std::string& PluginDir, std::uint64_t SpineId)
        {
            return std::format("CreateNode -Id {} -PluginDir {} -InSpine {}", FormatGuid(Id), PluginDir, FormatGuid(SpineId));
        }

        enum class node_placement_kind { Append, After, Before, InSpine };

        // Every "add a node" UI path funnels through here to decide plain CreateNode vs.
        // CreateOwnedPair - the one place that reads xnode_os_node_factory::needsOwnedEndMarker(), so
        // no call site has to know "If"/"ForEachLoop" by name. RefId is the -After/-Before node id or
        // the -InSpine spine id; ignored for Append.
        inline std::string BuildCreateNodeCommand
        (
            std::vector<plugin_source_entry>& Sources
        ,   std::vector<available_node_type>& AvailableTypes
        ,   plugin_source_entry&               OwnerSrc
        ,   xnode_os_node_factory*              pOwnerType
        ,   node_placement_kind                 Kind
        ,   std::uint64_t                        RefId
        ) noexcept
        {
            const auto NewId = xresource::guid_generator::Instance64();
            if (pOwnerType->needsOwnedEndMarker())
            {
                const std::string EndDir(pOwnerType->getOwnedEndMarkerPluginDir());
                auto* pEndSrc = FindSourceByDirName(Sources, EndDir);
                if (pEndSrc)
                {
                    auto* pEndType = EnsureLoadedAndGetType(*pEndSrc, AvailableTypes);
                    if (pEndType)
                    {
                        const auto EndId  = xresource::guid_generator::Instance64();
                        const auto LinkId = xresource::guid_generator::Instance64();

                        // Discover an optional 2nd hop the same generic way the 1st hop was found -
                        // no hardcoded node-name check here (see create_owned_pair_cmd's own comment
                        // for why this stays a 2-hop special case rather than a general N-way chain;
                        // nothing currently needs it, Function included, now that its owned marker is
                        // merged into itself).
                        owned_pair_2nd_hop Hop2;
                        if (pEndType->needsOwnedEndMarker())
                        {
                            const std::string End2Dir(pEndType->getOwnedEndMarkerPluginDir());
                            auto* pEnd2Src = FindSourceByDirName(Sources, End2Dir);
                            if (pEnd2Src && EnsureLoadedAndGetType(*pEnd2Src, AvailableTypes))
                            {
                                Hop2.m_End2Id        = xresource::guid_generator::Instance64();
                                Hop2.m_End2PluginDir = End2Dir;
                                Hop2.m_Link2Id       = xresource::guid_generator::Instance64();
                            }
                            // Second-level plugin missing/failed to compile - fall through with
                            // Hop2 empty rather than failing the whole placement; the user still gets
                            // the owner + its immediate marker, same "never do nothing" policy as the
                            // 1st-hop fallback below.
                        }

                        switch (Kind)
                        {
                        case node_placement_kind::Append:  return MakeCreateOwnedPairAppend (NewId, OwnerSrc.m_DirName, EndId, EndDir, LinkId, Hop2);
                        case node_placement_kind::After:   return MakeCreateOwnedPairAfter  (NewId, OwnerSrc.m_DirName, EndId, EndDir, LinkId, RefId, Hop2);
                        case node_placement_kind::Before:  return MakeCreateOwnedPairBefore (NewId, OwnerSrc.m_DirName, EndId, EndDir, LinkId, RefId, Hop2);
                        case node_placement_kind::InSpine: return MakeCreateOwnedPairInSpine(NewId, OwnerSrc.m_DirName, EndId, EndDir, LinkId, RefId, Hop2);
                        }
                    }
                }
                // Marker plugin missing/failed to compile - fall through to a plain create rather than
                // silently doing nothing, so the user sees the node and a compile error in the log
                // instead of the "+" click appearing to do nothing at all.
            }
            switch (Kind)
            {
            case node_placement_kind::Append:  return MakeCreateNodeAppend (NewId, OwnerSrc.m_DirName);
            case node_placement_kind::After:   return MakeCreateNodeAfter  (NewId, OwnerSrc.m_DirName, RefId);
            case node_placement_kind::Before:  return MakeCreateNodeBefore (NewId, OwnerSrc.m_DirName, RefId);
            case node_placement_kind::InSpine: return MakeCreateNodeInSpine(NewId, OwnerSrc.m_DirName, RefId);
            }
            return {};
        }

        inline std::string MakeDeleteNodes(const std::vector<std::uint64_t>& Ids) { return std::format("DeleteNodes -Ids {}", JoinIds(Ids)); }
        inline std::string MakeDeleteLink(std::uint64_t Id) { return std::format("DeleteLink -Id {}", FormatGuid(Id)); }
        inline std::string MakeConnect(std::uint64_t Id, std::uint64_t SourceNode, int SourceOutput, std::uint64_t TargetNode, int TargetInput)
        {
            return std::format("Connect -Id {} -SourceNode {} -SourceOutput {} -TargetNode {} -TargetInput {}"
                               , FormatGuid(Id), FormatGuid(SourceNode), SourceOutput, FormatGuid(TargetNode), TargetInput);
        }
        // Value is a plain scalar token (a Float/Int/Bool literal), never containing spaces - no
        // Base64 needed here, unlike SetProperties' own arbitrary multi-line blobs.
        inline std::string MakeSetLiteralValue(std::uint64_t PinId, const std::string& Value)
        {
            return std::format("SetLiteralValue -Pin {} -Value {}", FormatGuid(PinId), Value);
        }
        inline std::string MakeReorderNodes(const std::vector<std::uint64_t>& NewOrder) { return std::format("ReorderNodes -Ids {}", JoinIds(NewOrder)); }
        // Moves node(s) into a DIFFERENT spine at a given position - addressed the same way CreateNode
        // addresses insertion (-After/-Before an existing node, or -InSpine to append to a spine
        // regardless of its current size).
        inline std::string MakeMoveNodesToSpineAfter(const std::vector<std::uint64_t>& Ids, std::uint64_t AfterNodeId)
        {
            return std::format("MoveNodesToSpine -Ids {} -After {}", JoinIds(Ids), FormatGuid(AfterNodeId));
        }
        inline std::string MakeMoveNodesToSpineBefore(const std::vector<std::uint64_t>& Ids, std::uint64_t BeforeNodeId)
        {
            return std::format("MoveNodesToSpine -Ids {} -Before {}", JoinIds(Ids), FormatGuid(BeforeNodeId));
        }
        inline std::string MakeMoveNodesToSpineIn(const std::vector<std::uint64_t>& Ids, std::uint64_t SpineId)
        {
            return std::format("MoveNodesToSpine -Ids {} -InSpine {}", JoinIds(Ids), FormatGuid(SpineId));
        }
        inline std::string MakeSetProperties(std::uint64_t NodeId, const std::string& Before, const std::string& After)
        {
            return std::format("SetProperties -NodeId {} -Before {} -After {}", FormatGuid(NodeId), Base64Encode(Before), Base64Encode(After));
        }
        // Disabling (-Enable 0) needs none of EndId/EndPluginDir/LinkId - use MakeSetEndElseDisable.
        inline std::string MakeSetEndElseEnable(std::uint64_t OwnerId, std::uint64_t EndId, const std::string& EndPluginDir, std::uint64_t LinkId)
        {
            return std::format("SetEndElseState -OwnerId {} -Enable 1 -EndId {} -EndPluginDir {} -LinkId {}", FormatGuid(OwnerId), FormatGuid(EndId), EndPluginDir, FormatGuid(LinkId));
        }
        inline std::string MakeSetEndElseDisable(std::uint64_t OwnerId)
        {
            return std::format("SetEndElseState -OwnerId {} -Enable 0", FormatGuid(OwnerId));
        }
        // Every field is OPTIONAL and simply omitted when not selected - "nothing selected" is the bare
        // command "Select" with no flags at all, not a "-Nodes -" placeholder or a "-Link 0x000...0"
        // sentinel next to a real-looking id that could be mistaken for an actual (nonexistent) link.
        // The insert-marker selection is addressed the same way CreateNode addresses insertion -
        // relative to an existing node (-MarkerAfter/-MarkerBefore <nodeid>), not a raw, shifting index.
        inline std::string MakeSelectNodes(const std::vector<std::uint64_t>& Nodes)
        {
            return Nodes.empty() ? std::string("Select") : std::format("Select -Nodes {}", JoinIds(Nodes));
        }
        inline std::string MakeSelectLink(std::uint64_t Link) { return std::format("Select -Link {}", FormatGuid(Link)); }
        inline std::string MakeSelectMarkerAfter(std::uint64_t NodeId)  { return std::format("Select -MarkerAfter {}",  FormatGuid(NodeId)); }
        inline std::string MakeSelectMarkerBefore(std::uint64_t NodeId) { return std::format("Select -MarkerBefore {}", FormatGuid(NodeId)); }
        inline std::string MakeSelectMarkerSpine(std::uint64_t SpineId) { return std::format("Select -MarkerSpine {}", FormatGuid(SpineId)); }
        inline std::string MakeClearSelection() { return "ClearSelection"; }

        // CreateSpine - places a new, empty spine directly at an absolute -Y. -Column/-NewColumn fold
        // "attach to an existing column" vs. "synthesize a new one" into one command, same pattern
        // Select already uses for its own several mutually exclusive concerns. -NewColumnId is minted
        // by the CALLER (never inside Redo()), matching this codebase's standing rule for every id this
        // command system ever creates. -NewColumn carries a dummy "1" argument rather than appearing
        // bare - xcmdline::parser::hasOption() only reports an option present if it actually collected
        // an argument, so a bare flag immediately followed by another flag (nothing non-flag trailing
        // it) leaves its own args empty and fails minArgs, erroring the WHOLE command before Redo()
        // ever runs (the exact bug already hit and fixed once for -AnchorAfter/-AnchorBefore).
        inline std::string MakeCreateSpineNewColumn(std::uint64_t SpineId, float Y, std::uint64_t NeighborColumnId, char Side, std::uint64_t NewColumnId)
        {
            return std::format("CreateSpine -Id {} -Y {:.3f} -NewColumn 1 -NewColumnId {} -NeighborColumn {} -Side {}"
                               , FormatGuid(SpineId), Y, FormatGuid(NewColumnId), FormatGuid(NeighborColumnId), Side);
        }
        inline std::string MakeCreateSpineExistingColumn(std::uint64_t SpineId, float Y, std::uint64_t ColumnId)
        {
            return std::format("CreateSpine -Id {} -Y {:.3f} -Column {}", FormatGuid(SpineId), Y, FormatGuid(ColumnId));
        }
        inline std::string MakeDeleteSpine(std::uint64_t SpineId) { return std::format("DeleteSpine -Id {}", FormatGuid(SpineId)); }

        // SetSpinePosition - sets a spine's absolute (Y, Column) directly - drag it anywhere within
        // its own column (any pixel), or straight into a different already-existing column.
        inline std::string MakeSetSpinePosition(std::uint64_t SpineId, float Y, std::uint64_t ColumnId)
        {
            return std::format("SetSpinePosition -Id {} -Y {:.3f} -Column {}", FormatGuid(SpineId), Y, FormatGuid(ColumnId));
        }
        // Same, but the destination column doesn't exist yet - splice a new one in beside -NeighborColumn
        // first. Mirrors MakeCreateSpineNewColumn's own dummy "1" on -NewColumn for the same xcmdline
        // bare-flag reason.
        inline std::string MakeSetSpinePositionNewColumn(std::uint64_t SpineId, float Y, std::uint64_t NeighborColumnId, char Side, std::uint64_t NewColumnId)
        {
            return std::format("SetSpinePosition -Id {} -Y {:.3f} -NewColumn 1 -NewColumnId {} -NeighborColumn {} -Side {}"
                               , FormatGuid(SpineId), Y, FormatGuid(NewColumnId), FormatGuid(NeighborColumnId), Side);
        }

        // Wraps system::Execute with logging - EVERY command, not just failures, so nodeos_debug.log
        // is a genuine audit trail of everything that happened to the graph (the same log an AI agent
        // driving this through a future "command source" plugin would read).
        inline void Run(xundo::system& System, const std::string& Cmd)
        {
            if (auto Err = System.Execute(Cmd); !Err.empty())
                Debugger(std::format("Node OS: command failed: '{}' ({})", Cmd, Err));
            else
                Debugger(std::format("Node OS: {}", Cmd));
        }
    }

    //------------------------------------------------------------------------------------------------
    static void DrawNodeLibraryPanel(std::vector<plugin_source_entry>& Sources, std::vector<available_node_type>& AvailableTypes, bool& bDirty)
    {
        // Pick up any background compile (started by the button below) that finished since last frame,
        // before drawing anything - so this frame's log/"loaded" text already reflects it. Runs off the
        // UI thread (std::async) so the editor never freezes while a plugin recompiles.
        for (auto& Src : Sources)
        {
            if (Src.m_bCompiling && Src.m_Future.valid() && Src.m_Future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                auto Result = Src.m_Future.get();
                Src.m_bCompiling = false;
                if (MergeCompileResult(Src, Result, AvailableTypes))
                    bDirty = true;
            }
        }

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(430, 500), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Node Library"))
        {
            ImGui::TextWrapped("Auto-discovered from Plugins/ - NOT part of this program's own build. Placing one "
                                "from the Add Node menu compiles+loads it automatically the first time; the button "
                                "below is only for forcing a recompile after editing a plugin's source.");
            ImGui::Separator();

            for (auto& Src : Sources)
            {
                ImGui::PushID(Src.m_SourcePath.c_str());
                ImGui::BulletText("%s", Src.m_DisplayName.c_str());
                ImGui::TextWrapped("%s", Src.m_SourcePath.c_str());

                if (Src.m_bCompiling)
                {
                    ImGui::BeginDisabled();
                    ImGui::Button("Compiling...");
                    ImGui::EndDisabled();
                }
                else if (ImGui::Button(Src.m_bLoaded ? "Recompile & Reload" : "Compile & Load"))
                {
                    Src.m_bCompiling = true;
                    Src.m_Future = std::async(std::launch::async, CompilePluginWorker, Src.m_SourcePath);
                }

                if (Src.m_bLoaded)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "loaded");
                }

                if (!Src.m_CompileLog.empty())
                {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0.3f));
                    ImGui::BeginChild(("log" + Src.m_SourcePath).c_str(), ImVec2(0, 60), true);
                    ImGui::TextUnformatted(Src.m_CompileLog.c_str());
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                }
                ImGui::Separator();
                ImGui::PopID();
            }
        }
        ImGui::End();
    }

    //------------------------------------------------------------------------------------------------
    static const char* PortTypeToPreview(const char* pTypeName, void* pValue)
    {
        static thread_local std::string s_Scratch;
        // No value to show is not itself information worth a line of text - especially now that
        // most scalar inputs show either a live wire or an inline constant instead of ever reaching
        // this at all. Blank, not "(none)".
        if (!pValue) { return ""; }
        if (std::strcmp(pTypeName, "Text") == 0) { return static_cast<const char*>(pValue); }
        if (std::strcmp(pTypeName, "Mesh") == 0)
        {
            auto* pMesh = static_cast<xnode_os_mesh_data*>(pValue);
            s_Scratch = std::format("{} verts / {} tris", pMesh->m_VertexCount, pMesh->m_IndexCount / 3);
            return s_Scratch.c_str();
        }
        // Float/Int/Short - now that RunProgram (NODE_SCRIPTING_DESIGN.md §12.6) actually produces
        // real values instead of every Execute() being a no-op, this path is finally reachable with
        // a genuine numeric pValue - dereference it properly instead of falling through to the raw-
        // pointer placeholder below, which used to be harmless only because nothing ever got here
        // with a real value to show.
        if (std::strcmp(pTypeName, "Float") == 0) { s_Scratch = std::format("{:.3f}", *static_cast<float*>(pValue)); return s_Scratch.c_str(); }
        if (std::strcmp(pTypeName, "Int")   == 0) { s_Scratch = std::to_string(*static_cast<std::int32_t*>(pValue)); return s_Scratch.c_str(); }
        if (std::strcmp(pTypeName, "Short") == 0) { s_Scratch = std::to_string(*static_cast<std::int16_t*>(pValue)); return s_Scratch.c_str(); }
        // Anything else genuinely unrecognized - the raw pointer as a placeholder, same as before.
        s_Scratch = std::format("<{:#x}>", (std::uintptr_t)pValue);
        return s_Scratch.c_str();
    }

    //------------------------------------------------------------------------------------------------
    // A tiny, dedicated render pipeline (position+normal, normal-as-color, no lighting/textures) so
    // any "Mesh"-typed pin can show a REAL rendered preview inline in its node - not just a vertex
    // count. Deliberately separate from the rest of the engine's material system: this is here to
    // prove wiring nodes together produces something that visibly does work, not to be a real
    // material. One shared static camera angle is reused for every preview square.
    //
    // Renders into a small offscreen texture per pin (same render-to-texture pattern as
    // E22_FramebufferTarget), then displays that texture via a plain ImGui::Image() - NOT a raw
    // viewport-rect draw callback. imgui-node-editor defers/transforms node content for pan+zoom in
    // its own Begin/End pass, so anything positioned via a manually-captured screen rect (as an
    // earlier version of this code did) ends up drawn in the wrong place the moment the canvas is
    // panned or the node dragged; ImGui::Image() is a normal widget the library repositions
    // correctly right along with everything else inside the node, same as the Text() calls above.
    //------------------------------------------------------------------------------------------------
    struct mesh_preview_vert { float m_X, m_Y, m_Z, m_NX, m_NY, m_NZ; };
    struct mesh_preview_push_constants { xmath::fmat4 m_L2C; };

    struct mesh_preview_entry
    {
        xgpu::texture     m_Texture;
        xgpu::renderpass  m_RenderPass;
        xgpu::buffer      m_VertexBuffer;
        // Draw() is always an indexed draw in this engine (see E22_FramebufferTarget's own usage) -
        // this preview data is already a plain, non-indexed triangle list (RebuildIfMesh expands each
        // triangle into 3 raw vertices), so this is a trivial identity mapping (index i = i), just to
        // satisfy Draw()'s requirement that SOME index buffer is bound. Sized to the largest vertex
        // count this entry has ever needed - recreated only when a bigger mesh comes through.
        xgpu::buffer      m_IndexBuffer;
        int                m_IndexCapacity = 0;
        int                m_VertexCount = 0;
        bool               m_bTextureReady = false;
    };

    struct mesh_preview_system
    {
        xgpu::vertex_descriptor                                    m_VertexDescriptor;
        xgpu::pipeline                                               m_Pipeline;
        xgpu::pipeline_instance                                      m_PipelineInstance;
        xgpu::tools::view                                            m_View;
        std::unordered_map<std::uint64_t, mesh_preview_entry>        m_Entries;

        static constexpr int s_PreviewSize = 110;

        bool Init(xgpu::device& Device)
        {
            auto Attributes = std::array
            { xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(mesh_preview_vert, m_X),  .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D }
            , xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(mesh_preview_vert, m_NX), .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D }
            };
            if (auto Err = Device.Create(m_VertexDescriptor, { .m_VertexSize = sizeof(mesh_preview_vert), .m_Attributes = Attributes }); Err)
                return false;

            xgpu::shader FragShader, VertShader;
            if (auto Err = Device.Create(FragShader, { .m_Type = xgpu::shader::type::bit::FRAGMENT, .m_Sharer = xgpu::shader::setup::raw_data{ std::array{
                #include "E27_normal_frag.h"
            } } }); Err) return false;
            if (auto Err = Device.Create(VertShader, { .m_Type = xgpu::shader::type::bit::VERTEX, .m_Sharer = xgpu::shader::setup::raw_data{ std::array{
                #include "E27_normal_vert.h"
            } } }); Err) return false;

            auto Shaders = std::array<const xgpu::shader*, 2>{ &FragShader, &VertShader };
            if (auto Err = Device.Create(m_Pipeline, { .m_VertexDescriptor = m_VertexDescriptor, .m_Shaders = Shaders, .m_PushConstantsSize = sizeof(mesh_preview_push_constants)
                // NONE, not the pipeline default of BACK - Cube's index data was written only caring
                // about triangle *count* (see cube_node.cpp's own comment), never checked for
                // consistent CCW winding, so half its faces would otherwise get culled as "backfacing".
                , .m_Primitive     = { .m_Cull = xgpu::pipeline::primitive::cull::NONE }
                // The preview's render pass has a color attachment only, no depth texture - leaving
                // the pipeline's own depth-test-enabled default would compare against a nonexistent
                // depth buffer.
                , .m_DepthStencil  = { .m_bDepthTestEnable = false, .m_bDepthWriteEnable = false }
                }); Err)
                return false;
            if (auto Err = Device.Create(m_PipelineInstance, { .m_PipeLine = m_Pipeline }); Err)
                return false;

            m_View.setViewport({ 0, 0, s_PreviewSize, s_PreviewSize });
            m_View.LookAt(3.0f, xmath::radian3(30_xdeg, 45_xdeg, 0_xdeg), xmath::fvec3::fromZero());
            return true;
        }

        // Flattens the mesh into a plain triangle list with a computed flat (per-face) normal per
        // vertex - the plugin ABI only carries positions+indices (xnode_os_mesh_data), so the host
        // is the one that derives shading data, same way it's the host (not the plugin) that owns
        // the actual GPU rendering. Creates the pin's offscreen texture/render pass once, lazily.
        void RebuildIfMesh(xgpu::device& Device, std::uint64_t PinId, const char* pTypeName, void* pValue)
        {
            // Deliberately never erase/destroy an existing entry here, even though a null/typeless
            // value means "nothing to show any more" - its xgpu::texture may already be referenced by
            // an ImGui::Image() draw command queued earlier this same frame (or, since there's no fence
            // sync on this path, even a frame or two back), so destroying the GPU objects synchronously
            // risks a use-after-free/destroy-while-in-flight crash. This is exactly what reproduced by
            // deleting a node or a connection with a live mesh flowing through it. Just stop rendering
            // into it (m_VertexCount = 0 makes DrawPreviewSquare fall back to the placeholder) and leave
            // the GPU objects alone for the rest of the program - a deliberately accepted small leak on
            // disconnect, not a correctness risk.
            auto StopRendering = [&] { if (auto It = m_Entries.find(PinId); It != m_Entries.end()) It->second.m_VertexCount = 0; };

            if (!pValue || std::strcmp(pTypeName, "Mesh") != 0) { StopRendering(); return; }
            auto* pMesh = static_cast<xnode_os_mesh_data*>(pValue);

            std::vector<mesh_preview_vert> Verts;
            Verts.reserve(pMesh->m_IndexCount);
            for (unsigned int i = 0; i + 2 < pMesh->m_IndexCount; i += 3)
            {
                const auto GetPos = [&](unsigned int Idx) { return xmath::fvec3{ pMesh->m_pPositions[Idx*3+0], pMesh->m_pPositions[Idx*3+1], pMesh->m_pPositions[Idx*3+2] }; };
                const auto A = GetPos(pMesh->m_pIndices[i+0]), B = GetPos(pMesh->m_pIndices[i+1]), C = GetPos(pMesh->m_pIndices[i+2]);
                const auto N = (B - A).Cross(C - A).Normalize();
                for (auto& P : { A, B, C })
                    Verts.push_back({ P.m_X, P.m_Y, P.m_Z, N.m_X, N.m_Y, N.m_Z });
            }
            if (Verts.empty()) { StopRendering(); return; }

            auto& Entry = m_Entries[PinId];
            if (!Entry.m_bTextureReady)
            {
                if (Device.Create(Entry.m_Texture, { .m_Format = xgpu::texture::format::R8G8B8A8_UNORM, .m_Width = s_PreviewSize, .m_Height = s_PreviewSize, .m_isGamma = false })) return;
                std::array<xgpu::renderpass::attachment, 1> Attachments{ Entry.m_Texture };
                if (Device.Create(Entry.m_RenderPass, { .m_Attachments = Attachments })) return;
                Entry.m_bTextureReady = true;
            }

            if (Device.Create(Entry.m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(mesh_preview_vert), .m_EntryCount = (int)Verts.size() })) return;
            (void)Entry.m_VertexBuffer.MemoryMap(0, (int)Verts.size(), [&](void* pData) { std::memcpy(pData, Verts.data(), Verts.size() * sizeof(mesh_preview_vert)); });

            if ((int)Verts.size() > Entry.m_IndexCapacity)
            {
                if (Device.Create(Entry.m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = (int)Verts.size() })) return;
                (void)Entry.m_IndexBuffer.MemoryMap(0, (int)Verts.size(), [&](void* pData)
                {
                    auto* pIndex = static_cast<std::uint32_t*>(pData);
                    for (std::uint32_t i = 0; i < Verts.size(); ++i) pIndex[i] = i;
                });
                Entry.m_IndexCapacity = (int)Verts.size();
            }

            Entry.m_VertexCount = (int)Verts.size();
        }

        // Actually draws every ready pin's cube into its own offscreen texture - must run once per
        // frame, before any ImGui:: calls (same ordering E22_FramebufferTarget uses), since it opens
        // its own render pass(es) on the window's current frame.
        void RenderAll(xgpu::window& MainWindow)
        {
            for (auto& [PinId, Entry] : m_Entries)
            {
                if (!Entry.m_bTextureReady || Entry.m_VertexCount == 0) continue;
                auto CmdBuffer = MainWindow.StartRenderPass(Entry.m_RenderPass);
                CmdBuffer.setPipelineInstance(m_PipelineInstance);
                CmdBuffer.setBuffer(Entry.m_VertexBuffer);
                CmdBuffer.setBuffer(Entry.m_IndexBuffer);
                CmdBuffer.setPushConstants(mesh_preview_push_constants{ .m_L2C = m_View.getW2C() });
                CmdBuffer.Draw(Entry.m_VertexCount);
            }
        }

        // Shows the pin's rendered texture inline - a normal ImGui widget, so it moves/scales
        // correctly with its node under the canvas's own pan and zoom. Always draws a visible
        // bordered placeholder even before the graph has ever executed - the render canvas is part
        // of the node's shape from the moment it exists, not something that pops into existence
        // only once a mesh happens to flow through it.
        void DrawPreviewSquare(std::uint64_t PinId, float Scale)
        {
            const float Size = s_PreviewSize * Scale;
            const ImVec2 P0 = ImGui::GetCursorScreenPos();
            const ImVec2 P1{ P0.x + Size, P0.y + Size };
            auto It = m_Entries.find(PinId);
            if (It == m_Entries.end() || !It->second.m_bTextureReady || It->second.m_VertexCount == 0)
            {
                ImGui::GetWindowDrawList()->AddRectFilled(P0, P1, theme::CanvasDark, 0.0f);
                ImGui::GetWindowDrawList()->AddRect(P0, P1, theme::NodeBorder, 0.0f);
                ImGui::Dummy(ImVec2(Size, Size));
                return;
            }
            ImGui::Image((ImTextureRef)((void*)&It->second.m_Texture), ImVec2(Size, Size), ImVec2(0, 1), ImVec2(1, 0));
        }
    };

    //------------------------------------------------------------------------------------------------
    // Canvas geometry - ported from rslgraph-ui's own constants
    // (_ai_programming/ai_programming/rslgraph-ui/apps/rslgraph-ui/src/canvas/geometry.ts). Pixel
    // values differ from the original (that was tuned for its own 13px web font) but every formula
    // shape is the same: nodes measured from their own port-label text, stacked with a fixed gap,
    // wires routed via a highway line offset from the widest node plus a per-lane step.
    //------------------------------------------------------------------------------------------------
    namespace geo
    {
        constexpr float HEADER_H        = 26.0f;
        constexpr float ROW_H           = 20.0f;
        constexpr float VALUE_LINE_H    = 14.0f;
        constexpr float NODE_PAD_BOTTOM = 12.0f;
        constexpr float NODE_GAP        = 28.0f;
        constexpr float TOP             = 10.0f;
        constexpr float PORT_PAD        = 14.0f;
        // The "[Type]" label next to a pin reads as an annotation, not a name - smaller than the
        // row/title text and tucked in close to its own pin, rather than sharing the row-name's
        // full size and PORT_PAD's wider inset (which still governs how much column width a port
        // reserves overall - a smaller, closer label needs less of it, hence *_FONT_SCALE feeding
        // back into NodeWidth's own PortColW measurement too).
        constexpr float PIN_TYPE_FONT_SCALE = 0.72f;
        constexpr float PIN_TYPE_INSET      = 6.0f;
        // The node title is the one piece of text meant to read first at a glance - a bit bigger
        // than every other label on the node (row names, pin types, category). NodeWidth's own
        // TitleW measurement scales by the same factor so the box reserves exactly enough room for
        // the bigger rendered title, never less (see MinForHeader).
        constexpr float TITLE_FONT_SCALE = 1.3f;
        constexpr float GLYPH           = 9.0f;
        constexpr float ICON_CLEARANCE  = 16.0f;
        constexpr float LANE_GAP        = 14.0f;
        constexpr float PORT_HIT_RADIUS = 16.0f;
        constexpr float LINK_HIT_DIST   = 6.0f;
        constexpr float PREVIEW_GAP     = 10.0f;
        constexpr float SECTION_GAP     = 24.0f; // extra breathing room (line + "locals" caption) where a node's ports switch from external (caller-facing) to local-scope (body-facing) - see LocalSectionGapTotal
        constexpr float COLUMN_MARGIN     = 60.0f; // world-space gap between two adjacent columns' own highway extents
        constexpr float COLUMN_CLEAR_GAP  = 24.0f; // extra world-space distance, past a column's own extent, a spine-control drag must clear before a new-column drop target appears
        constexpr float SPINE_CIRCLE_R    = 7.0f;  // the two spine-control circles' own radius (screen-space, scales with zoom like everything else here)
        constexpr float SPINE_CIRCLE_GAP  = 4.0f;  // gap between the insert-marker box's edge and each circle, so they never overlap its own hit area
    }

    // Every port on a node in one flat, row-ordered list (inputs then outputs) - rslgraph-ui's own
    // NodeDef::ports is a single flat array regardless of direction; this is the equivalent view
    // over our ABI's separate input/output arrays.
    struct port_ref { bool m_bIsOutput; int m_Index; const xnode_os_port_desc* m_pDesc; };
    static std::vector<port_ref> FlatPorts(const xnode_os_node* pNode)
    {
        std::vector<port_ref> Out;
        const auto Inputs  = pNode->getInputs();
        const auto Outputs = pNode->getOutputs();
        for (int i = 0; i < (int)Inputs.size();  ++i) Out.push_back({ false, i, &Inputs[i] });
        for (int i = 0; i < (int)Outputs.size(); ++i) Out.push_back({ true,  i, &Outputs[i] });
        // Visual/anchor row order groups by scope-locality FIRST (every external/caller-facing pin,
        // then every local/body-facing one), direction second within each group - a stable partition,
        // so a mixed node like Function reads as one coherent "signature" block followed by one
        // "body view" block, needing exactly one divider (see the draw loop's SECTION_GAP handling)
        // instead of one per direction. The trailing ownership "End" pin (always last - see
        // for_each_loop_node.cpp's own comment on why) is excluded from the partition and
        // re-appended after: it's structural, not part of either interface, and must stay last
        // regardless of its own m_bLocalScope value (false, same as every external pin, which would
        // otherwise pull it up into the external group).
        const bool bHasTrailingEnd = !Out.empty() && Out.back().m_bIsOutput && std::strcmp(Out.back().m_pDesc->m_pTypeName, "Scope") == 0;
        port_ref EndPort{};
        if (bHasTrailingEnd) { EndPort = Out.back(); Out.pop_back(); }
        std::stable_partition(Out.begin(), Out.end(), [](const port_ref& P) { return !P.m_pDesc->m_bLocalScope; });
        if (bHasTrailingEnd) Out.push_back(EndPort);
        return Out;
    }
    static std::uint64_t PinOf(const port_ref& P, std::uint64_t NodeId) { return P.m_bIsOutput ? OutPinOf(NodeId, P.m_Index) : InPinOf(NodeId, P.m_Index); }
    static bool IsMeshType(const char* pType) noexcept { return std::strcmp(pType, "Mesh") == 0; }
    // A "Scope" pin (NODE_SCRIPTING_DESIGN.md section 4.1's owner<->End ownership pins) never
    // carries a runtime value at all - it exists purely to draw the read-only ownership wire, so it
    // gets the same no-value-line treatment as Mesh below, never a "(none)" placeholder.
    static bool IsScopeType(const char* pType) noexcept { return std::strcmp(pType, "Scope") == 0; }
    // An "Exec" pin (OnEvent's output, Execute/Function/Call's input, Call's output -
    // NODE_SCRIPTING_DESIGN.md's exec-flow addition) carries no data at all, same as Scope - it's a
    // pure control-flow trigger, never wired to a Float/Bool/etc. pin and never assigned an inline
    // literal.
    static bool IsExecType(const char* pType) noexcept { return std::strcmp(pType, "Exec") == 0; }
    // An "Any" pin (Compare/Math Expression's A/B/Result, Print's Value) is a scalar wildcard - it
    // resolves DIRECTLY to whatever's wired to it. A "Span<Any>" pin (ForEachLoop's own Span input -
    // named for std::span, since that's what actually accepts a std::vector, std::array, C array, or
    // any other contiguous container of any element type T) is a CONTAINER wildcard - see
    // IsSpanWildcardType/ResolveContainerWildcardType below for how those differ: a container's
    // element type is what actually varies, not its own shape.
    static bool IsWildcardType(const char* pType) noexcept { return std::strcmp(pType, "Any") == 0; }
    static bool IsSpanWildcardType(const char* pType) noexcept { return std::strcmp(pType, "Span<Any>") == 0; }
    // Any container-shaped type name, resolved or not ("Span<Any>", "Span<Float>", ...) - never has
    // an inline literal or a preview line to show (there's no sensible "type a container" UI), same
    // no-value-line treatment RowHeight already gives Mesh/Scope/Bool below.
    static bool IsContainerType(const char* pType) noexcept { return std::string_view(pType).substr(0, 5) == "Span<"; }
    // If pType looks like "Span<X>", returns X; otherwise returns pType unchanged (nothing to
    // unwrap). Plain string surgery, not a real generic/template system - this corpus only ever
    // nests one level deep (NODE_SCRIPTING_DESIGN.md never called for more).
    static std::string_view UnwrapSpanElementType(std::string_view Type) noexcept
    {
        if (Type.size() > 6 && Type.substr(0, 5) == "Span<" && Type.back() == '>')
            return Type.substr(5, Type.size() - 6);
        return Type;
    }
    // What container type (e.g. "Span<Float>") is wired to NodeId's own "Span<Any>" input, if
    // anything - unwrapped or not, this is just "what's actually connected there" (used both to
    // display the Span pin's own effective type, and as the raw material ResolveNodeWildcardType
    // unwraps for Element/Index below). Returns nullptr while nothing is wired yet.
    static const char* ResolveContainerWildcardType(std::uint64_t NodeId, const xnode_os_node* pDesc, const std::vector<node_instance>& Nodes, const std::vector<link_instance>& Links) noexcept
    {
        int Index = 0;
        for (auto& P : pDesc->getInputs())
        {
            if (IsSpanWildcardType(P.m_pTypeName))
                for (auto& L : Links)
                    if (L.m_TargetNode == NodeId && L.m_TargetInput == Index)
                        for (auto& N : Nodes)
                            if (N.m_Id == L.m_SourceNode && N.m_pNode)
                            {
                                auto Outs = N.m_pNode->getOutputs();
                                if (L.m_SourceOutput < (int)Outs.size()) return Outs[L.m_SourceOutput].m_pTypeName;
                            }
            ++Index;
        }
        return nullptr;
    }
    // What has NodeId's own scalar "Any" wildcard(s) resolved to, if anything is wired yet - either
    // directly (a plain Any input wired straight to a source, Compare/Math Expression's A/B) or
    // indirectly, unwrapped from a "Span<Any>" input's own resolved container type (ForEachLoop's
    // Element/Index, once its Span input is wired) - purely derived from the CURRENT Links every
    // frame, never stored anywhere, same "never cache stale geometry" rule as everything else in
    // this file: disconnecting the wire that resolved it reverts it to unresolved for free, with no
    // explicit cleanup needed. Returns nullptr while nothing is wired yet (any type is still
    // acceptable in that case). A free function, not a DrawGraphCanvas-local lambda, specifically so
    // RowHeight/NodeHeight (also free functions - they run during layout, before/outside the
    // interactive draw loop) can call it too; DrawGraphCanvas's own call sites just pass their
    // already-in-scope Nodes/Links along.
    //
    // Forward-declared: EffectiveTypeName (below) calls this to resolve a plain "Any" pin, and this
    // now calls EffectiveTypeName right back for a two-hop chain (see the Outs[...] handling inside) -
    // a real mutual recursion, not a leftover.
    static const char* EffectiveTypeName(std::uint64_t NodeId, const xnode_os_node* pDesc, const char* pRawType, const std::vector<node_instance>& Nodes, const std::vector<link_instance>& Links) noexcept;
    static const char* ResolveNodeWildcardType(std::uint64_t NodeId, const xnode_os_node* pDesc, const std::vector<node_instance>& Nodes, const std::vector<link_instance>& Links) noexcept
    {
        int Index = 0;
        for (auto& P : pDesc->getInputs())
        {
            if (IsWildcardType(P.m_pTypeName))
                for (auto& L : Links)
                    if (L.m_TargetNode == NodeId && L.m_TargetInput == Index)
                        for (auto& N : Nodes)
                            if (N.m_Id == L.m_SourceNode && N.m_pNode)
                            {
                                auto Outs = N.m_pNode->getOutputs();
                                if (L.m_SourceOutput >= (int)Outs.size()) continue;
                                const char* pSourceType = Outs[L.m_SourceOutput].m_pTypeName;
                                // The source's own declared output type can ITSELF be an unresolved
                                // wildcard - wiring straight from a Compare/Math Expression's own
                                // "Any" Result rather than from a concrete-typed producer like
                                // Constant. Returning that raw, still-open "Any" text here is exactly
                                // the bug that left Print's Value pin showing "[Any]" and a raw
                                // pointer preview instead of "[Float]" and the real number once Print
                                // got wired two hops downstream of Constant instead of one - resolve
                                // it one level further instead of assuming the immediate source is
                                // already concrete.
                                if (IsWildcardType(pSourceType))
                                    return EffectiveTypeName(N.m_Id, N.m_pNode, pSourceType, Nodes, Links);
                                return pSourceType;
                            }
            ++Index;
        }
        if (const char* pContainerType = ResolveContainerWildcardType(NodeId, pDesc, Nodes, Links))
        {
            static thread_local std::string s_Elem;
            s_Elem.assign(UnwrapSpanElementType(pContainerType));
            return s_Elem.c_str();
        }
        return nullptr;
    }
    // The effective type name for display/matching purposes: a concrete pin's own declared type
    // unchanged; an Any pin's resolved type once something has locked it in (or the raw "Any" text
    // itself while still fully open); a Span<Any> pin's own resolved container type, NOT unwrapped
    // (or the raw "Span<Any>" text while still open) - Span itself displays/matches as the whole
    // container, only Element/Index (plain Any pins) see the unwrapped element type. Never nullptr -
    // always safe to print/compare directly.
    static const char* EffectiveTypeName(std::uint64_t NodeId, const xnode_os_node* pDesc, const char* pRawType, const std::vector<node_instance>& Nodes, const std::vector<link_instance>& Links) noexcept
    {
        if (IsSpanWildcardType(pRawType))
        {
            if (const char* pResolved = ResolveContainerWildcardType(NodeId, pDesc, Nodes, Links)) return pResolved;
            return pRawType;
        }
        if (!IsWildcardType(pRawType)) return pRawType;
        if (const char* pResolved = ResolveNodeWildcardType(NodeId, pDesc, Nodes, Links)) return pResolved;
        return pRawType;
    }

    // A literal's resolved runtime bytes live here, not in a shared static/thread_local buffer - see
    // GetInputValue's own comment for why a single shared slot would be wrong the moment a node has
    // more than one unconnected literal input. std::deque (not vector) specifically: appending never
    // invalidates an already-returned pointer into an earlier element, since nothing here ever needs
    // random-access indexing, only stable addresses for as long as the deque itself is alive.
    struct literal_slot { unsigned char m_Bytes[8]; };
    using literal_storage = std::deque<literal_slot>;

    // Shared tail for GetInputValue/PullInputValue: what a pin resolves to when NO wire targets it
    // at all - whatever's typed into its inline-literal box (LiteralValues, keyed by InPinOf - the
    // same value the canvas already shows next to the pin), or nullptr if nothing was ever typed in.
    static void* ResolveUnconnectedLiteral(std::uint64_t NodeId, int InputIndex, const std::vector<node_instance>& Nodes, const std::vector<link_instance>& Links, const std::unordered_map<std::uint64_t, std::string>& LiteralValues, literal_storage& Scratch)
    {
        auto LitIt = LiteralValues.find(InPinOf(NodeId, InputIndex));
        if (LitIt == LiteralValues.end() || LitIt->second.empty()) return nullptr;
        auto NodeIt = std::find_if(Nodes.begin(), Nodes.end(), [&](auto& N) { return N.m_Id == NodeId; });
        if (NodeIt == Nodes.end() || !NodeIt->m_pNode) return nullptr;
        const auto NodeInputs = NodeIt->m_pNode->getInputs();
        if (InputIndex >= (int)NodeInputs.size()) return nullptr;
        const char* pEffType = EffectiveTypeName(NodeId, NodeIt->m_pNode, NodeInputs[InputIndex].m_pTypeName, Nodes, Links);

        Scratch.emplace_back();
        void* pSlot = &Scratch.back();
        if (std::strcmp(pEffType, "Bool") == 0)  { *static_cast<bool*>(pSlot)         = (LitIt->second == "1" || LitIt->second == "true"); return pSlot; }
        if (std::strcmp(pEffType, "Int") == 0)   { *static_cast<std::int32_t*>(pSlot) = std::atoi(LitIt->second.c_str()); return pSlot; }
        if (std::strcmp(pEffType, "Short") == 0) { *static_cast<std::int16_t*>(pSlot) = (std::int16_t)std::atoi(LitIt->second.c_str()); return pSlot; }
        *static_cast<float*>(pSlot) = std::strtof(LitIt->second.c_str(), nullptr); // Float, and the default for anything else
        return pSlot;
    }
    // Whatever cached output feeds a given node's input pin right now - nullptr if unconnected (and
    // no literal is typed in), or if a wire IS there but its source simply hasn't run (yet, or ever -
    // this never PULLS a source into running; see PullInputValue below, used by real execution, for
    // that). Read-only, side-effect-free - this is what the canvas's own live pin preview and the
    // mesh-preview pass use, since triggering real Execute() calls (with their real side effects,
    // e.g. Print writing to the console) merely because a frame got drawn would be a much bigger
    // surprise than a preview showing "nothing yet."
    static void* GetInputValue(std::uint64_t NodeId, int InputIndex, const std::vector<node_instance>& Nodes, const std::vector<link_instance>& Links, const std::unordered_map<std::uint64_t, std::string>& LiteralValues, literal_storage& Scratch)
    {
        for (auto& Link : Links)
        {
            if (Link.m_TargetNode != NodeId || Link.m_TargetInput != InputIndex) continue;
            auto SourceIt = std::find_if(Nodes.begin(), Nodes.end(), [&](auto& N) { return N.m_Id == Link.m_SourceNode; });
            if (SourceIt == Nodes.end() || !SourceIt->m_bHasRun) return nullptr;
            return (Link.m_SourceOutput < (int)SourceIt->m_CachedOutputs.size()) ? SourceIt->m_CachedOutputs[Link.m_SourceOutput] : nullptr;
        }
        return ResolveUnconnectedLiteral(NodeId, InputIndex, Nodes, Links, LiteralValues, Scratch);
    }

    // For connection-matching only: is this (already-effective) type name STILL an open wildcard of
    // either kind - a bare "Any" that's never been wired at all, or a "Span<Any>" whose own Span
    // input hasn't been wired yet? Either one accepts any type on the other end of a new connection.
    // A RESOLVED wildcard (EffectiveTypeName already returned the real type, e.g. "Float" or
    // "Span<Float>") is intentionally NOT considered open here - it must match exactly like any
    // ordinary concrete pin from that point on.
    static bool IsAnyKindOfWildcard(const char* pType) noexcept { return IsWildcardType(pType) || IsSpanWildcardType(pType); }
    // Defined further down (needs xproperty::any/settings::context machinery not declared yet at
    // this point in the file) - forward-declared here so DisplayTypeText, right below, can call it.
    static bool ReadBoolProperty(const xnode_os_node* pNode, const char* pName, bool Default) noexcept;
    // ForEachLoop's own Element output additionally shows const/& based on its own "ReadOnlyElement"
    // checkbox (defaults to read-only, i.e. "const T&") - the node should visibly reflect that choice
    // rather than have it be an invisible side-panel-only setting. A name-based special case, the
    // display-only counterpart to Compare's operator-filtering/Constant's inline Value widget above.
    // Every other port's display text is just its own effective type name, unchanged.
    static std::string DisplayTypeText(const xnode_os_node* pDesc, const port_ref& P, const char* pEffType)
    {
        if (P.m_bIsOutput && pDesc->m_pFactory->getName() == "ForEachLoop" && std::strcmp(P.m_pDesc->m_pName, "Element") == 0)
        {
            const bool bReadOnly = ReadBoolProperty(pDesc, "ReadOnlyElement", true);
            // "RO Float", not "const Float&" - this label is user-facing, not a C++ declaration; the
            // read-only/mutable distinction still matters here (it's the whole point of the checkbox
            // this reflects), but nothing about reference syntax should leak into the canvas.
            return (bReadOnly ? std::string("RO ") : std::string()) + pEffType;
        }
        // A Function's local-mirrored outputs (the function body's own view of its parameters) carry
        // Required/ReadOnly directly on the port descriptor itself (see function_node.cpp), unlike
        // ForEachLoop's bespoke per-instance property above - same const/& treatment, but deliberately
        // NOT keyed off m_bLocalScope alone: ForEachLoop's Index is also flagged m_bLocalScope (for
        // the scope-containment check) but is a plain value, never a reference - showing it as
        // "const Int&" would misdescribe the eventual codegen shape. Scoped to Function specifically,
        // where a mirrored output genuinely is a reference into the caller's own argument.
        if (P.m_bIsOutput && P.m_pDesc->m_bLocalScope && pDesc->m_pFactory->getName() == "Function")
            // "RO Float", not "const Float&" - same reasoning as ForEachLoop's Element just above:
            // this is a user-facing canvas label, not a C++ declaration.
            return (P.m_pDesc->m_bReadOnly ? std::string("RO ") : std::string()) + pEffType;
        // "?" for Optional - every pre-existing static port_desc defaults m_bRequired to true (see
        // xnode_os_plugin_api.h), so this is a no-op everywhere except a Function's user-configured
        // pins, the only place m_bRequired can actually be false today.
        return P.m_pDesc->m_bRequired ? std::string(pEffType) : std::string(pEffType) + "?";
    }
    // Per-TYPE color, not per-pin - every Float pin/wire in the graph reads the same color at a
    // glance, same convention Unity's own node-based editors (Shader Graph, Visual Scripting) use:
    // a value's TYPE is what the color encodes, not which node or which side of a wire it's on.
    // Approximate hand-picked values, not a pixel-exact Unity palette - the numeric family (Bool/
    // Float/Int/Short) shares one recognizable hue family the way Unity's own "number" types do,
    // while staying distinguishable from each other; refine on request if exact parity matters.
    static ImU32 TypeColor(const char* pType) noexcept
    {
        if (IsMeshType(pType))                    return IM_COL32(167, 139, 250, 255); // purple
        if (std::strcmp(pType, "Text") == 0)      return IM_COL32(74, 222, 128, 255);  // green
        // A Scope pin (owner<->End ownership, NODE_SCRIPTING_DESIGN.md section 4.1) matches the box's
        // own BACKGROUND fill, not the generic default and not the border either - it's part of the
        // box's own structure, not a value, and both the pin-glyph code and the link-drawing loop
        // already darken this per scope depth (DarkenForDepth) to keep matching the actual box at
        // whatever nesting level it sits at - this is the one shared base color both read from.
        if (IsScopeType(pType))                   return theme::NodeBg;
        // Exec pins get their own distinct color (a plain white, matching the long-established
        // convention for control-flow pins elsewhere) so a glance at the glyph tells data from
        // control flow apart, same as Scope already does for ownership.
        if (IsExecType(pType))                    return IM_COL32(241, 245, 249, 255);
        // NOT red - this editor already uses red for its own "scope-invalid link" warning
        // (bScopeInvalid in the link-drawing loop), and a near-identical red for Bool would make a
        // perfectly valid Bool wire indistinguishable from a broken one at a glance. A deeper,
        // more saturated green than "Text"'s own minty green just above - close enough in hue to
        // read as "green" on request, far enough in value/saturation not to recreate that same
        // collision one type over.
        if (std::strcmp(pType, "Bool") == 0)      return IM_COL32(22, 163, 74, 255);   // green (deep, distinct from Text's mint green)
        if (std::strcmp(pType, "Float") == 0)     return IM_COL32(101, 210, 235, 255); // cyan
        if (std::strcmp(pType, "Int") == 0)       return IM_COL32(66, 153, 225, 255);  // blue
        if (std::strcmp(pType, "Short") == 0)     return IM_COL32(56, 178, 165, 255);  // teal
        // A container type (Span<Any>/Span<Float>/...) is a distinct SHAPE, not a value of the type
        // it holds - its own amber/orange marks it apart from a plain scalar pin of the same element
        // type, same spirit as Scope getting its own color rather than inheriting from whatever it
        // wraps. IsContainerType matches on the "Span<" prefix regardless of what's inside.
        if (IsContainerType(pType))               return IM_COL32(245, 158, 11, 255);  // amber
        // "Any" (never wired, still fully open) and anything else unrecognized share this neutral
        // gray - an unresolved wildcard genuinely has no type yet to color by.
        return IM_COL32(148, 163, 184, 255);
    }
    // A node's header strip tints by its own factory category (Flow Control/Math/Logic/...), same
    // spirit as Unity's own category-colored node headers. The title text drawn on top is always
    // near-white (see the title draw call) - every color here is kept dark and roughly matched in
    // luminance to the plain theme::NodeHeader gray it replaces, so white text stays legible and no
    // one category's box reads as jarringly brighter than its neighbors on the same canvas. New
    // categories not listed here just fall back to the old neutral header gray - nothing breaks if
    // a future plugin introduces one.
    static ImU32 CategoryColor(std::string_view Category) noexcept
    {
        if (Category == "Flow Control") return IM_COL32(52, 71, 94, 255);  // slate blue
        if (Category == "Logic")        return IM_COL32(38, 82, 74, 255);  // teal green
        if (Category == "Math")         return IM_COL32(92, 68, 32, 255);  // amber brown
        if (Category == "Debug")        return IM_COL32(94, 46, 46, 255);  // muted red
        if (Category == "Variables")    return IM_COL32(70, 50, 92, 255);  // purple
        if (Category == "Output")       return IM_COL32(30, 76, 86, 255);  // cyan teal
        if (Category == "Geometry")     return IM_COL32(66, 68, 32, 255);  // olive
        return theme::NodeHeader;
    }
    // A Bool pin never has anything to preview - it dropped the inline-constant checkbox (a
    // hardcoded true/false doesn't fit how Condition/And/Or/Not are meant to be used - they're wired
    // from Compare, not typed directly). No point reserving a value line it will never use. An Exec
    // pin is the same - pure control flow, never a value. (The original flat-spine design removed
    // Blueprint-style exec pins entirely, since a plain node's "next" is just whatever follows it in
    // the same spine - still true today. Exec pins came back narrowly for OnEvent/Execute/Call/
    // Function's own new input, to let a SPINE be triggered by an event or invoked from elsewhere;
    // ordinary nodes still have none. There's also deliberately no separate "Int"/"Short" scalar pin
    // type: minimizing node/type proliferation matters more than nominal precision here, so every
    // scalar numeric value in this corpus - including ForEachLoop's own Element/index - is just
    // "Float", the same principle already applied to Compare (one enum-driven node instead of a
    // GreaterThan/LessThan/Equals box each).)
    static bool IsNoPreviewType(const char* pType) noexcept { return std::strcmp(pType, "Bool") == 0 || IsExecType(pType); }
    // A Mesh-typed port's live render lives in one shared preview block at the TOP of the node (right
    // under the header), not inline per-row - so its row never prints a value-preview line below the
    // glyph and doesn't need the VALUE_LINE_H space reserved for one (leaving it in produced a visible
    // gap between a Mesh row and whatever row follows it). Scope/Bool pins get the same
    // treatment for the same underlying reason - none of them ever have a value to preview.
    // An output pin never shows anything either, right now - nothing in this corpus actually
    // executes yet, so pValue is always null and the row would sit empty regardless of type. Revisit
    // once real execution is wired up and an output can genuinely have a live value to preview.
    static bool IsPinConnected(std::uint64_t NodeId, int PinIndex, bool bIsOutput, const std::vector<link_instance>& Links) noexcept
    {
        for (auto& L : Links)
            if (bIsOutput ? (L.m_SourceNode == NodeId && L.m_SourceOutput == PinIndex)
                          : (L.m_TargetNode == NodeId && L.m_TargetInput  == PinIndex))
                return true;
        return false;
    }
    static float RowHeight(const port_ref& P, std::uint64_t NodeId, const std::vector<link_instance>& Links, const char* pEffType) noexcept
    {
        if (IsMeshType(pEffType) || IsScopeType(pEffType) || IsNoPreviewType(pEffType) || IsContainerType(pEffType) || P.m_bIsOutput)
            return geo::ROW_H;
        // A still-unresolved Any pin (Compare/Math Expression's A/B before either one is wired - see
        // ResolveNodeWildcardType) has nothing to preview or enter yet either - same no-line
        // treatment as Mesh/Scope/Bool above, until it resolves to something.
        if (IsWildcardType(pEffType)) return geo::ROW_H;
        // A numeric input's extra line holds either its own inline literal-constant widget (while
        // unconnected) or, once a wire connects it AND the graph has actually run (RunProgram -
        // NODE_SCRIPTING_DESIGN.md §12.6), the real resolved value via PortTypeToPreview - either
        // way something wants that line, so it's reserved unconditionally rather than trying to
        // predict "will there be a value to show this frame," which would need Nodes threaded all
        // the way through here just to ask GetInputValue. The only cost is a occasionally-blank line
        // under a connected-but-not-yet-run pin - far better than the line disappearing and letting
        // the row below it collide with real preview text, which is what happened before RunProgram
        // ever produced a real value to show here (this path used to be dead: nothing ever executed,
        // so a connected numeric pin never actually needed the space it was skipping). Checked
        // against the EFFECTIVE type so an Any pin resolved to one of these gets the exact same
        // treatment an ordinarily-typed pin of that type would.
        return geo::ROW_H + geo::VALUE_LINE_H;
    }
    static int MeshPortCount(const xnode_os_node* pNode)
    {
        int Count = 0;
        for (auto& P : FlatPorts(pNode)) if (IsMeshType(P.m_pDesc->m_pTypeName)) ++Count;
        return Count;
    }
    static float PreviewAreaHeight(const xnode_os_node* pNode)
    {
        const int Count = MeshPortCount(pNode);
        return Count > 0 ? Count * (mesh_preview_system::s_PreviewSize + geo::PREVIEW_GAP) + geo::PREVIEW_GAP : 0.0f;
    }
    // An End marker (NODE_SCRIPTING_DESIGN.md section 4.1/4.2) isn't really a node in its own right -
    // more an extension of whichever If/ForEachLoop owns it - so it renders as a title-only pill:
    // no body, no port rows, no visible pin glyph at all (the wire just anchors to the title bar's
    // own edge). Its actual displayed title is computed contextually elsewhere (the node-header draw
    // loop) since that needs the ownership graph this function doesn't have access to; a fixed,
    // generously-wide box here is a reasonable trade against threading that context through here too.
    static bool IsEndMarkerType(const xnode_os_node* pNode) noexcept { return pNode->m_pFactory->getName() == "End"; }
    static float NodeWidth(const xnode_os_node* pNode, std::uint64_t NodeId, const std::vector<node_instance>& Nodes, const std::vector<link_instance>& Links)
    {
        // Scaled by TITLE_FONT_SCALE too - an End marker's title renders at the same bigger size as
        // every other node's, so its fixed width needs the same proportional headroom.
        if (IsEndMarkerType(pNode)) return 190.0f * geo::TITLE_FONT_SCALE;
        const auto NodeName = pNode->m_pFactory->getName();
        const float TitleW = ImGui::CalcTextSize(NodeName.data(), NodeName.data() + NodeName.size()).x;
        float NameW = TitleW;
        float PortColW = 40.0f;
        for (auto& P : FlatPorts(pNode))
        {
            NameW = std::max(NameW, ImGui::CalcTextSize(P.m_pDesc->m_pName).x);
            // Sized off the EFFECTIVE (and, for ForEachLoop's Element, const/&-decorated - see
            // DisplayTypeText) type, so a resolved Any pin gets room for its real type name, not just
            // the shorter placeholder "Any" text.
            const char* pEffType = EffectiveTypeName(NodeId, pNode, P.m_pDesc->m_pTypeName, Nodes, Links);
            const std::string TypeLabel = std::string("[") + DisplayTypeText(pNode, P, pEffType) + "]";
            PortColW = std::max(PortColW, ImGui::CalcTextSize(TypeLabel.c_str()).x * geo::PIN_TYPE_FONT_SCALE + geo::PIN_TYPE_INSET);
        }
        const float MinForPreview = MeshPortCount(pNode) > 0 ? mesh_preview_system::s_PreviewSize + 24.0f : 0.0f;
        // The header row carries BOTH the title (left-aligned) and the category label (right-
        // aligned, e.g. "Flow Control") - neither the port-column formula above nor MinForPreview
        // ever reserved room for the two of them to coexist, which is exactly the bug behind
        // "Function"/"If"'s title colliding with their own "Flow Control" category text. TitleW
        // (NOT the port-loop-mutated NameW) is the true title width; End markers never reach this
        // point at all (early return above), so DisplayName == NodeName always holds here - no
        // string-mismatch risk between what's measured and what's drawn.
        const auto NodeCategory = pNode->m_pFactory->getCategory();
        const float CategoryW = ImGui::CalcTextSize(NodeCategory.data(), NodeCategory.data() + NodeCategory.size()).x;
        // TitleW scaled by TITLE_FONT_SCALE to match the actual bigger rendered title (see the
        // title draw call) - reserving room at the OLD, smaller size here would silently reopen the
        // title/category overlap bug the bigger title was supposed to have no part in.
        const float MinForHeader = 10.0f + TitleW * geo::TITLE_FONT_SCALE + 16.0f + CategoryW + 10.0f;
        const float Result = std::max({ NameW + 2.0f * PortColW + 40.0f, MinForPreview, MinForHeader });
        return Result;
    }
    // Extra vertical space at the ONE point a node's port list switches from external (caller-
    // facing) to local-scope (body-facing) - FlatPorts already groups every external pin first, then
    // every local one (direction-agnostic), so there is exactly one such boundary total, not one per
    // direction. "End" is also flagged !m_bLocalScope (it's a structural ownership marker, not part
    // of either interface) but FlatPorts keeps it trailing after the local group, so checking only
    // the false->true direction (never true->false) never mistakes local-into-End for a second
    // boundary. Shared between NodeHeight (so the box is sized to fit it) and the draw loop (so RowY
    // actually leaves the gap) - see function_node.cpp for the only node type with a mixed port list
    // today.
    static float LocalSectionGapTotal(const xnode_os_node* pNode)
    {
        float Total = 0.0f;
        bool bHavePrev = false, bPrevLocal = false;
        for (auto& P : FlatPorts(pNode))
        {
            if (bHavePrev && P.m_pDesc->m_bLocalScope && !bPrevLocal)
                Total += geo::SECTION_GAP;
            bPrevLocal = P.m_pDesc->m_bLocalScope;
            bHavePrev = true;
        }
        return Total;
    }
    static float NodeHeight(const xnode_os_node* pNode, std::uint64_t NodeId, const std::vector<node_instance>& Nodes, const std::vector<link_instance>& Links)
    {
        if (IsEndMarkerType(pNode)) return geo::HEADER_H;
        float H = geo::HEADER_H + PreviewAreaHeight(pNode) + LocalSectionGapTotal(pNode);
        for (auto& P : FlatPorts(pNode)) H += RowHeight(P, NodeId, Links, EffectiveTypeName(NodeId, pNode, P.m_pDesc->m_pTypeName, Nodes, Links));
        // Reserve room for each enum property's own inline dropdown, drawn directly in the node body
        // (not just the side properties panel) - see the inline-enum-widget block in the draw loop.
        if (const xproperty::type::object* pObj = pNode->getProperties())
            for (auto& M : pObj->m_Members)
                if (auto* pVar = std::get_if<xproperty::type::members::var>(&M.m_Variant); pVar && pVar->m_AtomicType.m_IsEnum)
                    H += geo::ROW_H + 4.0f;
        // Constant's own numeric Value gets a second inline row too, same as its Type dropdown just
        // above - see the Constant-specific block right after the inline-enum-widget loop.
        if (pNode->m_pFactory->getName() == "Constant")
            H += geo::ROW_H + 4.0f;
        return H + geo::NODE_PAD_BOTTOM;
    }
    // How many scopes currently enclose each node, in place of visual indentation (which would fight
    // the spine layout's own X positions) - walk each spine in Order, push a node's own
    // m_OwnedEndId when it owns one, pop on reaching it. An End-Else (which owns a further End)
    // pops its own pairing and immediately pushes the next one, so the false-branch content lands
    // back at the same depth the true branch was at - traced by hand for both plain If and If/Else
    // before trusting this.
    static std::unordered_map<std::uint64_t, int> ComputeScopeDepths(const std::vector<node_instance>& Nodes)
    {
        std::unordered_map<std::uint64_t, int> Depth;
        std::map<std::uint64_t, std::vector<const node_instance*>> BySpine;
        for (auto& N : Nodes) BySpine[N.m_SpineId].push_back(&N);
        for (auto& [SpineId, SpineNodes] : BySpine)
        {
            std::vector<const node_instance*> Sorted = SpineNodes;
            std::sort(Sorted.begin(), Sorted.end(), [](const node_instance* A, const node_instance* B) { return A->m_Order < B->m_Order; });
            std::vector<std::uint64_t> Stack;
            for (auto* N : Sorted)
            {
                if (!Stack.empty() && Stack.back() == N->m_Id) Stack.pop_back();
                Depth[N->m_Id] = (int)Stack.size();
                if (N->m_OwnedEndId != 0) Stack.push_back(N->m_OwnedEndId);
            }
        }
        return Depth;
    }
    // The ordered chain of enclosing owner ids (If/ForEachLoop) for each node, outermost first - e.g.
    // {ForEachLoopId, IfId} for a node nested inside an If nested inside a ForEachLoop. Exact same
    // per-spine stack walk as ComputeScopeDepths just above, only keeping the stack's actual contents
    // instead of collapsing it to a count - used by IsDataLinkScopeValid below to check whether a
    // data link's source is something the target could actually reference in real nested C++.
    static std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> ComputeEnclosingChains(const std::vector<node_instance>& Nodes)
    {
        std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> Chains;
        std::map<std::uint64_t, std::vector<const node_instance*>> BySpine;
        for (auto& N : Nodes) BySpine[N.m_SpineId].push_back(&N);
        for (auto& [SpineId, SpineNodes] : BySpine)
        {
            std::vector<const node_instance*> Sorted = SpineNodes;
            std::sort(Sorted.begin(), Sorted.end(), [](const node_instance* A, const node_instance* B) { return A->m_Order < B->m_Order; });
            std::vector<std::uint64_t> Stack;
            for (auto* N : Sorted)
            {
                if (!Stack.empty() && Stack.back() == N->m_Id) Stack.pop_back();
                Chains[N->m_Id] = Stack;
                if (N->m_OwnedEndId != 0) Stack.push_back(N->m_OwnedEndId);
            }
        }
        return Chains;
    }
    // Every node id from an owner (If/ForEachLoop) through its own End marker, inclusive, by walking
    // its spine in Order - used to highlight "what does this scope actually contain" when its
    // ownership link is selected. A visual highlight only, deliberately not an actual selection (see
    // the call site) - inspecting a scope shouldn't make its boxes eligible for Delete/drag. Also
    // used by IsDataLinkScopeValid below for any m_bLocalScope-flagged pin's containment check.
    // Walks m_OwnedEndId repeatedly (Owner -> its marker -> that marker's OWN marker -> ...) until
    // reaching a node that doesn't own anything further - handles an owner with a multi-hop marker
    // chain (create_owned_pair_cmd's optional 2nd hop) exactly like a plain single-hop owner (If/
    // ForEachLoop/Function all resolve to their direct marker today, nothing currently uses 2 hops).
    static std::uint64_t ResolveTerminalMarker(const std::vector<node_instance>& Nodes, std::uint64_t OwnerId)
    {
        std::uint64_t Cur = OwnerId;
        for (int Guard = 0; Guard < (int)Nodes.size() + 1; ++Guard)
        {
            const node_instance* pCur = nullptr;
            for (auto& N : Nodes) if (N.m_Id == Cur) { pCur = &N; break; }
            if (!pCur || pCur->m_OwnedEndId == 0) return Cur;
            Cur = pCur->m_OwnedEndId;
        }
        return Cur; // malformed cycle - shouldn't happen, bail out rather than loop forever
    }
    static std::vector<std::uint64_t> ComputeScopeSpan(const std::vector<node_instance>& Nodes, std::uint64_t OwnerId)
    {
        const node_instance* pOwner = nullptr;
        for (auto& N : Nodes) if (N.m_Id == OwnerId) { pOwner = &N; break; }
        if (!pOwner || pOwner->m_OwnedEndId == 0) return {};
        const std::uint64_t TerminalId = ResolveTerminalMarker(Nodes, OwnerId);

        std::vector<const node_instance*> Sorted;
        for (auto& N : Nodes) if (N.m_SpineId == pOwner->m_SpineId) Sorted.push_back(&N);
        std::sort(Sorted.begin(), Sorted.end(), [](const node_instance* A, const node_instance* B) { return A->m_Order < B->m_Order; });

        std::vector<std::uint64_t> Span;
        bool bInside = false;
        for (auto* N : Sorted)
        {
            if (N->m_Id == OwnerId) bInside = true;
            if (bInside) Span.push_back(N->m_Id);
            if (bInside && N->m_Id == TerminalId) break;
        }
        return Span;
    }
    // Whether a data link's SOURCE is something the TARGET could actually reference once this
    // compiles to real nested C++ (NODE_SCRIPTING_DESIGN.md section 4.4/11.6): the boundary is SCOPE
    // depth, not spine identity. A source sitting at a spine's own TOP level (an empty enclosing
    // chain - never nested inside any If/ForEachLoop body) is "world scope": conceptually shared/
    // global state any node anywhere can read, the same role a Blueprint Variable plays for cross-
    // Event-Graph communication in Unreal (you can't wire one Event Graph's local pin into a
    // different Event Graph at all, but both can read/write a shared class member). A source nested
    // inside a local scope, by contrast, is trapped in whatever function/block its OWN spine compiles
    // to - readable only from the same or a more deeply nested scope in that SAME spine, never from a
    // different spine at all, and never from a sibling or already-exited scope even in the same
    // spine (the exact §4.4 gap: a value from inside one branch isn't visible after it, or inside an
    // unrelated one).
    //
    // Blueprint/Unity Visual Scripting don't block drawing an invalid wire either - both let you draw
    // it, then refuse to compile it (Blueprint's own diagnostic: "X is not in scope due to a network
    // of execution and data flow errors"). There's no compiler wired up to this editor yet, so this
    // is surfaced as an immediate visual flag (see the link-drawing loop below) instead of a deferred
    // compile error - strictly more helpful than staying silent about it until a compiler exists.
    //
    // Left deliberately unaddressed here (a compiler-design question, not an editor-validation one):
    // a same-spine read is always guaranteed fresh (flat sequential/nested execution order makes
    // "already computed by the time this reads it" automatic), but a cross-spine world-scope read
    // has no such guarantee - it's a persisted "last value written" read, with real questions about
    // which spine runs first/how often. Worth resolving before compilation is actually wired up.
    static bool IsDataLinkScopeValid(std::uint64_t SourceNode, int SourceOutputIndex, std::uint64_t TargetNode, int TargetInputIndex, const std::vector<node_instance>& Nodes, const std::unordered_map<std::uint64_t, std::vector<std::uint64_t>>& Chains)
    {
        const node_instance* pSrc = nullptr; const node_instance* pTgt = nullptr;
        for (auto& N : Nodes) { if (N.m_Id == SourceNode) pSrc = &N; if (N.m_Id == TargetNode) pTgt = &N; }
        if (!pSrc || !pTgt) return true; // dangling reference - not this check's concern

        // A pin flagged m_bLocalScope (a Function's mirrored parameter/return pins; ForEachLoop's
        // Element/Index) only has meaning strictly INSIDE the scope its OWN node opens - regardless
        // of whether the flagged pin is this link's source (the body READS it) or target (the body
        // WRITES it), the OTHER endpoint must be physically within the flagged pin's owning node's
        // own scope span. Same containment question either direction, so one rule covers both -
        // replaces the old node-position-based "mid-chain" test now that the flag is per-pin.
        if (pSrc->m_pNode)
        {
            const auto Outs = pSrc->m_pNode->getOutputs();
            if (SourceOutputIndex >= 0 && SourceOutputIndex < (int)Outs.size() && Outs[SourceOutputIndex].m_bLocalScope)
            {
                const auto Span = ComputeScopeSpan(Nodes, SourceNode);
                return std::find(Span.begin(), Span.end(), TargetNode) != Span.end();
            }
        }
        if (pTgt->m_pNode)
        {
            const auto Ins = pTgt->m_pNode->getInputs();
            if (TargetInputIndex >= 0 && TargetInputIndex < (int)Ins.size() && Ins[TargetInputIndex].m_bLocalScope)
            {
                const auto Span = ComputeScopeSpan(Nodes, TargetNode);
                return std::find(Span.begin(), Span.end(), SourceNode) != Span.end();
            }
        }

        // Reaching here means the source port is NOT itself local-scope-flagged (that returned
        // above) - but if the source node owns a scope at all, its own EXTERNAL output is still that
        // scope's finished result, never available until the ENTIRE body has run. The ordinary
        // chain-prefix check below would otherwise treat it like any other enclosing-scope value
        // available from entry (e.g. a ForEachLoop's Span, an If's Condition) - correct for those,
        // wrong for a return value. So nothing PHYSICALLY INSIDE the owner's own span may read it,
        // regardless of nesting depth elsewhere; anything at or above the owner's own scope still can
        // (checked generically off m_OwnedEndId - only Function has an external output shaped this
        // way today, but this isn't specific to it by name).
        if (pSrc->m_OwnedEndId != 0)
        {
            const auto OwnSpan = ComputeScopeSpan(Nodes, SourceNode);
            if (std::find(OwnSpan.begin(), OwnSpan.end(), TargetNode) != OwnSpan.end()) return false;
        }

        auto SrcIt = Chains.find(SourceNode); auto TgtIt = Chains.find(TargetNode);
        if (SrcIt == Chains.end() || TgtIt == Chains.end()) return true;
        const auto& SrcChain = SrcIt->second; const auto& TgtChain = TgtIt->second;
        if (pSrc->m_SpineId != pTgt->m_SpineId) return SrcChain.empty() || TgtChain.empty(); // local-to-local across spines is invalid; either end being world scope is fine
        if (SrcChain.size() > TgtChain.size()) return false;
        for (std::size_t i = 0; i < SrcChain.size(); ++i) if (SrcChain[i] != TgtChain[i]) return false;
        // Chain compatibility alone isn't enough - two nodes at the SAME nesting depth (including
        // both unnested, "world scope") have IDENTICAL chains regardless of which one was actually
        // placed first, so the check above can't tell a forward link from a backward one. The spine's
        // own Order IS execution order in this flat model - a source that comes AFTER its target
        // would be read before it's ever computed. Cross-spine order isn't comparable this way (each
        // spine numbers its own Order independently), which is why this sits after the cross-spine
        // branch above rather than folded into it.
        if (pSrc->m_Order > pTgt->m_Order) return false;
        return true;
    }
    // Diminishing-returns darken, never marching to pure black - the fill is already very dark
    // (17,24,39) against a near-black canvas, so an unbounded per-level darken would make deeply
    // nested boxes blend into the background, defeating the point.
    static ImU32 DarkenForDepth(ImU32 BaseCol, int Depth) noexcept
    {
        if (Depth <= 0) return BaseCol;
        const float Factor = 0.45f + 0.55f / (1.0f + Depth * 0.6f);
        const int R = (int)(((BaseCol >> IM_COL32_R_SHIFT) & 0xFF) * Factor);
        const int G = (int)(((BaseCol >> IM_COL32_G_SHIFT) & 0xFF) * Factor);
        const int B = (int)(((BaseCol >> IM_COL32_B_SHIFT) & 0xFF) * Factor);
        return IM_COL32(R, G, B, 255);
    }
    // Scales just the alpha channel, leaving hue/brightness untouched - used to dim a single
    // ineligible PIN during a drag without touching the whole node's own DarkenForDepth-based fill
    // (a node can carry both eligible and ineligible pins at once, e.g. a Function's external vs
    // local-mirrored ports - see the per-pin dim in the port-row loop below).
    static ImU32 WithAlpha(ImU32 BaseCol, float Factor) noexcept
    {
        const int A = (int)(((BaseCol >> IM_COL32_A_SHIFT) & 0xFF) * Factor);
        return (BaseCol & ~((ImU32)0xFF << IM_COL32_A_SHIFT)) | ((ImU32)A << IM_COL32_A_SHIFT);
    }
    // An If/ForEachLoop and everything in its scope - including its own End (or End/End-Else pair)
    // and anything nested inside, else-branch content included - is one piece, never separable by an
    // ordinary drag. Expands an initial moving-set so that touching ANY part of a scope (the owner,
    // its marker, or something dragged from inside it via a multi-select) pulls the whole thing along
    // - reuses ComputeScopeSpan, which already walks through further nesting on its own.
    static std::vector<std::uint64_t> ExpandMoveSetForScopes(const std::vector<node_instance>& Nodes, std::vector<std::uint64_t> Ids)
    {
        std::set<std::uint64_t> Result(Ids.begin(), Ids.end());
        bool bChanged = true;
        while (bChanged)
        {
            bChanged = false;
            for (auto Id : std::vector<std::uint64_t>(Result.begin(), Result.end()))
            {
                const node_instance* pN = nullptr;
                for (auto& N : Nodes) if (N.m_Id == Id) { pN = &N; break; }
                if (!pN) continue;

                std::uint64_t OwnerId = 0;
                if (pN->m_OwnedEndId != 0) OwnerId = Id; // Id is itself an owner
                else for (auto& N : Nodes) if (N.m_OwnedEndId == Id) { OwnerId = N.m_Id; break; } // Id is someone's marker
                if (OwnerId == 0) continue;

                for (auto SpanId : ComputeScopeSpan(Nodes, OwnerId))
                    if (Result.insert(SpanId).second) bChanged = true;
            }
        }
        // Result is a std::set, so it's currently sorted by raw GUID value - meaningless for
        // ordering. Re-sort by each node's actual (SpineId, Order) so whatever consumes this list to
        // rebuild a sequence (ReorderNodes/MoveNodesTo) gets the nodes back in their real, current
        // relative order, not scrambled by id value.
        std::vector<std::uint64_t> Out(Result.begin(), Result.end());
        std::sort(Out.begin(), Out.end(), [&](std::uint64_t A, std::uint64_t B)
        {
            const node_instance *pA = nullptr, *pB = nullptr;
            for (auto& N : Nodes) { if (N.m_Id == A) pA = &N; if (N.m_Id == B) pB = &N; }
            if (!pA || !pB) return false;
            if (pA->m_SpineId != pB->m_SpineId) return pA->m_SpineId < pB->m_SpineId;
            return pA->m_Order < pB->m_Order;
        });
        return Out;
    }
    static float DistPointSegment(ImVec2 P, ImVec2 A, ImVec2 B) noexcept
    {
        const ImVec2 AB{ B.x - A.x, B.y - A.y };
        const float Len2 = AB.x * AB.x + AB.y * AB.y;
        float T = Len2 > 0.0f ? ((P.x - A.x) * AB.x + (P.y - A.y) * AB.y) / Len2 : 0.0f;
        T = std::clamp(T, 0.0f, 1.0f);
        const ImVec2 Closest{ A.x + AB.x * T, A.y + AB.y * T };
        return std::hypot(P.x - Closest.x, P.y - Closest.y);
    }

    // Drag-to-connect and selection state - persisted by the caller across frames, same role as
    // Canvas.tsx's own `drag` React state and the node/link selection rslgraph-ui itself never had.
    struct canvas_drag
    {
        bool           m_bActive = false;
        std::uint64_t  m_FromNode = 0;
        bool            m_bFromIsOutput = false;
        int             m_FromIndex = 0;
        ImVec2          m_FromPos{};
        char            m_FromSide = 'R'; // which of the (possibly two) rendered glyphs for this pin was grabbed
    };
    struct canvas_selection
    {
        std::set<std::uint64_t> m_SelectedNodes; // Ctrl/Shift-click toggles membership, matching this
                                                   // codebase's own multi-select convention (E23's bone
                                                   // tree/viewport)
        std::uint64_t            m_SelectedLink = 0;
        std::uint64_t            m_SelectedGapSpineId = 0;  // 0 means none selected
        int                      m_SelectedGapIndex   = -1; // a gap slot within m_SelectedGapSpineId,
                                                              // selected the same way a node is.
                                                              // Future copy/paste targets this.
    };

    // Drag-to-reorder: picking up a node (or, if it's part of the current selection, the whole
    // selection) and dropping it on a spine "+" marker moves it to that stacking position - separate
    // from canvas_drag above, which is pin-to-pin wiring.
    struct canvas_node_drag
    {
        bool                        m_bActive = false;
        std::vector<std::uint64_t> m_MovingIds;
    };

    // Dragging one of the two circles on a spine-control marker - either to grow a new column/spine
    // off of it, or (staying inside the dragged spine's own column) to freely reposition that spine's
    // own Y. No "which circle was grabbed" field on purpose - direction is recomputed live every frame
    // from the current mouse position relative to the grab point, never locked in at grab-time (see
    // the design conversation this came out of: "we do not care... we snap the origin... and move on").
    struct canvas_spine_drag
    {
        bool           m_bActive  = false;
        std::uint64_t  m_SpineId  = 0; // the spine the grabbed marker belongs to
        float          m_GrabY    = 0.0f; // world Y of the grabbed marker itself, at grab time
    };

    // Delete key on a selected (non-empty) spine's own gap-marker can't just call DeleteSpine - it
    // refuses a spine that still has nodes on it. Instead this holds which spine is waiting on the
    // user's "delete it and everything on it?" answer, between the frame Delete was pressed and the
    // frame the confirm popup gets one.
    struct canvas_delete_spine_confirm
    {
        std::uint64_t m_SpineId = 0; // 0 = no confirmation pending
    };

    // Zoom (mouse wheel, anchored under the cursor) and pan (right-drag on empty canvas space, both
    // axes) - the canvas has no native ImGui scrollbar, so this is the only way to navigate a graph
    // wider/taller than the window. No bounds on either axis - the graph can grow arbitrarily far in
    // any direction as columns/spines are added, so there's no fixed content extent to clamp against.
    struct canvas_view
    {
        float m_Zoom = 1.0f;
        float m_PanX = 0.0f;
        float m_PanY = 0.0f;
        bool  m_bPanDragActive = false; // true from a right-press starting inside the canvas until the
                                         // right button releases - tracked by hand (see DrawGraphCanvas)
                                         // rather than via ImGui's own item/active-id system, since that
                                         // slot is already claimed while a pin-to-pin/node-reorder drag
                                         // (left button) is in progress, and panning needs to keep
                                         // working through that.
    };

    // Forward-declared: defined later alongside the rest of the property-serialization helpers, but
    // needed here too, to compute an End marker's contextual display name (see the node-header draw
    // loop below), and to draw an enum property's dropdown directly in the node body rather than only
    // in the side properties panel (see the inline-enum-widget block further down).
    static std::string SerializePropertiesToString(xnode_os_node* pNode);
    static bool ReadBoolPropertyFromSnapshot(const std::string& Snapshot, std::string_view Name) noexcept;
    static bool HasSerializableProperties(xnode_os_node* pNode);
    static int ReadEnumAsInt(const xproperty::type::members& Member, void* pInstance) noexcept;
    static void WriteEnumFromInt(const xproperty::type::members& Member, void* pInstance, int Value) noexcept;
    static bool ReadBoolProperty(const xnode_os_node* pNode, const char* pName, bool Default) noexcept;

    //------------------------------------------------------------------------------------------------
    static void DrawGraphCanvas(std::vector<plugin_source_entry>& Sources, std::vector<available_node_type>& AvailableTypes, std::vector<node_instance>& Nodes
                               , std::vector<link_instance>& Links, mesh_preview_system& MeshPreview
                               , canvas_drag& Drag, canvas_selection& Selection, canvas_view& View
                               , canvas_node_drag& NodeDrag, canvas_spine_drag& SpineDrag
                               , canvas_delete_spine_confirm& DeleteSpineConfirm
                               , std::vector<spine>& Spines, std::vector<column>& Columns
                               , std::unordered_map<std::uint64_t, std::string>& LiteralValues
                               , bool& bDirty, xundo::system& System)
    {
        // Lives for this whole draw call only - every pin-preview GetInputValue() call below that
        // resolves an inline literal gets its own stable slot in here (see literal_storage's own
        // comment for why a shared single slot would be wrong).
        literal_storage LiteralScratch;

        ImGui::SetNextWindowPos(ImVec2(440, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(820, 560), ImGuiCond_FirstUseEver);
        // No native scrollbar - left-drag pan + wheel zoom (below) are the only way to navigate a
        // graph taller than the window, so the two don't fight over what "scrolling" means.
        if (!ImGui::Begin("Graph", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            ImGui::End();
            return;
        }

        // Nothing to lay out, wire, pan or zoom yet - a single big "+" centered in the window is the
        // whole UI until the very first node exists. No pan/zoom state applies here at all (the canvas
        // "can not be moved" while empty, per design), so this bypasses the entire rest of the function.
        if (Nodes.empty())
        {
            ImGui::TextDisabled("Click the + to add your first node.");
            const ImVec2 Origin = ImGui::GetCursorScreenPos();
            const ImVec2 Avail  = ImGui::GetContentRegionAvail();
            const ImVec2 Center{ Origin.x + Avail.x * 0.5f, Origin.y + Avail.y * 0.5f };
            const float  Radius = 22.0f;
            ImDrawList*  pDraw  = ImGui::GetWindowDrawList();
            const ImVec2 HitMin{ Center.x - Radius, Center.y - Radius }, HitMax{ Center.x + Radius, Center.y + Radius };
            const bool   bHovered = ImGui::IsMouseHoveringRect(HitMin, HitMax);

            pDraw->AddCircleFilled(Center, Radius, bHovered ? IM_COL32(56, 130, 246, 255) : IM_COL32(30, 41, 59, 255));
            pDraw->AddCircle(Center, Radius, IM_COL32(100, 116, 139, 255), 0, 2.0f);
            const float Arm = Radius * 0.45f;
            pDraw->AddLine({ Center.x - Arm, Center.y }, { Center.x + Arm, Center.y }, IM_COL32(226, 232, 240, 255), 3.0f);
            pDraw->AddLine({ Center.x, Center.y - Arm }, { Center.x, Center.y + Arm }, IM_COL32(226, 232, 240, 255), 3.0f);

            ImGui::SetCursorScreenPos(HitMin);
            ImGui::InvisibleButton("empty_add", ImVec2(Radius * 2, Radius * 2));
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                ImGui::OpenPopup("NodeOS_AddNodePopup");
            if (ImGui::BeginPopup("NodeOS_AddNodePopup"))
            {
                if (Sources.empty())
                    ImGui::TextDisabled("No plugin sources found under Plugins/.");
                for (auto& Src : Sources)
                    if (ImGui::MenuItem(Src.m_DisplayName.c_str()))
                    {
                        // Lazy compile: the very first placement of this type compiles+loads it right
                        // now (Sources is populated from a folder scan at startup, not from a manual
                        // "Compile & Load" click) - every later placement just reuses the loaded type.
                        if (auto* pType = EnsureLoadedAndGetType(Src, AvailableTypes))
                            commands::Run(System, commands::BuildCreateNodeCommand(Sources, AvailableTypes, Src, pType, commands::node_placement_kind::Append, 0));
                    }
                ImGui::EndPopup();
            }
            ImGui::End();
            return;
        }

        ImGui::TextDisabled("Click a + to add/insert a node | drag pin-to-pin to wire | drag a node onto a + to move it | right-drag empty space to pan, wheel to zoom | Delete to remove");
        const float AvailWidth = ImGui::GetContentRegionAvail().x;

        auto FindNode = [&](std::uint64_t Id) -> node_instance* { auto It = std::find_if(Nodes.begin(), Nodes.end(), [&](auto& N) { return N.m_Id == Id; }); return It == Nodes.end() ? nullptr : &*It; };
        auto DescOf   = [&](node_instance* pN) -> const xnode_os_node* { return pN ? pN->m_pNode : nullptr; };
        // EffectiveTypeName(Id, pDesc, RawType) - see the top-level definition above RowHeight for why
        // this needs to be a free function rather than a local lambda (RowHeight/NodeHeight need it too,
        // and can't see anything declared in here).

        auto FindColumn = [&](std::uint64_t Id) -> column* { for (auto& Co : Columns) if (Co.m_Id == Id) return &Co; return nullptr; };
        std::unordered_map<std::uint64_t, std::uint64_t> ColumnOfSpine;
        for (auto& S : Spines) ColumnOfSpine[S.m_Id] = S.m_ColumnId;
        auto ColumnOfNode = [&](std::uint64_t NodeId) -> std::uint64_t { auto* pN = FindNode(NodeId); return pN ? ColumnOfSpine[pN->m_SpineId] : 0; };

        // ---- Pass A: per-spine relative layout - each spine's own dense node order + sizes, Y
        // relative to that spine's own local origin (not yet placed in world/column space). Spine
        // (pure up/down connectivity, blind to highways) vs Column (the horizontal container that owns
        // them) is a deliberate split - see this file's own design-conversation history. ----
        struct spine_layout
        {
            std::vector<std::uint64_t> m_Order;               // this spine's own dense node order
            std::vector<float>          m_RelY, m_W, m_H;       // parallel to m_Order
            float                        m_RelBottom  = geo::NODE_GAP; // relative Y of the trailing gap
            float                        m_WidestNode = 120.0f;
        };
        std::unordered_map<std::uint64_t, spine_layout> SpineLayout;
        for (auto& S : Spines)
        {
            spine_layout SL;
            std::vector<std::uint64_t> SpineNodeIds;
            for (auto& N : Nodes) if (N.m_SpineId == S.m_Id) SpineNodeIds.push_back(N.m_Id);
            std::sort(SpineNodeIds.begin(), SpineNodeIds.end(), [&](std::uint64_t A, std::uint64_t B) { return FindNode(A)->m_Order < FindNode(B)->m_Order; });

            // A leading NODE_GAP (instead of starting flush at relative 0) reserves room for the
            // "insert before the first node" spine marker below, so it sits in a real gap identical in
            // size to every between-node gap rather than being crammed against the spine's own origin.
            float CursorY = geo::NODE_GAP;
            for (auto Id : SpineNodeIds)
            {
                auto* pDesc = DescOf(FindNode(Id));
                if (!pDesc) continue;
                const float W = NodeWidth(pDesc, Id, Nodes, Links), H = NodeHeight(pDesc, Id, Nodes, Links);
                SL.m_WidestNode = std::max(SL.m_WidestNode, W);
                SL.m_Order.push_back(Id); SL.m_RelY.push_back(CursorY); SL.m_W.push_back(W); SL.m_H.push_back(H);
                CursorY += H + geo::NODE_GAP;
            }
            SL.m_RelBottom = CursorY;
            SpineLayout.emplace(S.m_Id, std::move(SL));
        }

        // Relative Y of gap GapIndex (0 = before everything, m_Order.size() = after everything) within
        // a spine's own local layout - the exact formula every DrawInsertMarker call site already used
        // for the single-spine case, factored out so both the anchor walk (Pass B) and the marker-
        // drawing loop further below can share it. An empty spine's own single placeholder slot sits at
        // the same relative Y its first real node would (CursorY's own starting value above).
        auto GapRelY = [&](const spine_layout& SL, int GapIndex) -> float
        {
            if (SL.m_Order.empty()) return geo::NODE_GAP;
            if (GapIndex <= 0) return SL.m_RelY.front() - geo::NODE_GAP * 0.5f;
            if (GapIndex >= (int)SL.m_Order.size()) return SL.m_RelY.back() + SL.m_H.back() + geo::NODE_GAP * 0.5f;
            return SL.m_RelY[GapIndex - 1] + SL.m_H[GapIndex - 1] + geo::NODE_GAP * 0.5f;
        };

        // ---- Pass B: per-spine absolute Y. A spine's own m_Y IS its absolute world Y directly - no
        // derivation needed, root included (it just starts seeded at geo::TOP, like any other spine
        // starts wherever it was dropped). ----
        std::unordered_map<std::uint64_t, float> SpineAbsY;
        for (auto& S : Spines) SpineAbsY[S.m_Id] = S.m_Y;

        // Every node, in no particular meaningful order - only used where "for every node" is all that
        // matters (the render loop, the drag-to-connect port hit-test), never for relative ordering.
        std::vector<std::uint64_t> Order;
        for (auto& S : Spines) { auto& SL = SpineLayout[S.m_Id]; Order.insert(Order.end(), SL.m_Order.begin(), SL.m_Order.end()); }

        struct row_layout { std::uint64_t m_NodeId; float m_X, m_Y, m_W, m_H; };
        std::vector<row_layout> Layout;
        for (auto& S : Spines)
        {
            auto& SL = SpineLayout[S.m_Id];
            for (std::size_t i = 0; i < SL.m_Order.size(); ++i)
                Layout.push_back({ SL.m_Order[i], 0.0f, SpineAbsY[S.m_Id] + SL.m_RelY[i], SL.m_W[i], SL.m_H[i] });
        }
        auto FindRow = [&](std::uint64_t Id) -> row_layout* { auto It = std::find_if(Layout.begin(), Layout.end(), [&](auto& R) { return R.m_NodeId == Id; }); return It == Layout.end() ? nullptr : &*It; };

        // Is ColId's column to the right of OfColId's, walking the column chain structurally (never by
        // comparing X positions - those aren't known yet this early, and don't need to be: the chain's
        // own Left/Right links already say which way is which).
        auto IsColumnRightOf = [&](std::uint64_t ColId, std::uint64_t OfColId) -> bool
        {
            for (auto* pCo = FindColumn(OfColId); pCo && pCo->m_RightId; pCo = FindColumn(pCo->m_RightId))
                if (pCo->m_RightId == ColId) return true;
            return false;
        };

        // Cross-column connection ownership (the user's own spec): draw the direct line from source to
        // target and look at its diagonal direction - "down-right" or "up-left" (the horizontal and
        // vertical steps agree in sign) means the SOURCE's own column carries the vertical run;
        // "up-right" or "down-left" (they disagree) means the TARGET's column does. Same column (even
        // a different spine sharing it) is unambiguous - always that one column, no diagonal to reason
        // about.
        auto OwnerColumnOf = [&](const link_instance& L) -> std::uint64_t
        {
            const auto SrcCol = ColumnOfNode(L.m_SourceNode), DstCol = ColumnOfNode(L.m_TargetNode);
            if (SrcCol == DstCol) return SrcCol;
            auto* pSrcRow = FindRow(L.m_SourceNode); auto* pDstRow = FindRow(L.m_TargetNode);
            if (!pSrcRow || !pDstRow) return SrcCol;
            const bool bRight = IsColumnRightOf(DstCol, SrcCol);
            const bool bDown  = pDstRow->m_Y >= pSrcRow->m_Y;
            return (bRight == bDown) ? SrcCol : DstCol;
        };

        float TotalH = 0.0f;
        for (auto& S : Spines) TotalH = std::max(TotalH, SpineAbsY[S.m_Id] + SpineLayout[S.m_Id].m_RelBottom);
        TotalH += 20.0f;

        // Insert a new node instance at stacking position GapIndex within SpineId (0 = before
        // everything in that spine, that spine's own node count = after everything) - renumbers every
        // node's m_Order (spine-local, not global any more) to its dense index in the resulting stack
        // rather than doing arithmetic on the existing m_Order values, since deleting a node can leave
        // those with gaps. Takes the plugin SOURCE, not a type descriptor directly, so the very first
        // placement of a not-yet-compiled type can still compile+load it lazily.
        auto InsertNodeAt = [&](std::uint64_t SpineId, int GapIndex, plugin_source_entry& Src)
        {
            auto* pType = EnsureLoadedAndGetType(Src, AvailableTypes);
            if (!pType) return;
            auto& SL = SpineLayout[SpineId];
            if (SL.m_Order.empty()) { commands::Run(System, commands::BuildCreateNodeCommand(Sources, AvailableTypes, Src, pType, commands::node_placement_kind::InSpine, SpineId)); return; }

            const int Clamped = std::clamp(GapIndex, 0, (int)SL.m_Order.size());

            // Growth-blocking is a UI-LAYER GATE, never inside create_node_cmd::Redo() (which stays
            // 100% ImGui-free/headless-safe) - appending past the bottom must not be allowed to grow
            // into a sibling spine sharing this column. Measured via a throwaway instance, since a
            // not-yet-created node's own height depends on its real port list, not just its type; for
            // now, a collision here just silently rejects the append (same as an unresolved drag-drop)
            // - "fracturing" a spine to make room is deliberately left for later.
            if (Clamped >= (int)SL.m_Order.size())
            {
                auto& TempInstance = pType->CreateNodeInstance();
                // Sentinel id 0: this throwaway instance owns no real links, so every pin must size
                // as "disconnected" - Instance64() never mints 0, so no real link can match it.
                const float NewBottom = SpineAbsY[SpineId] + SL.m_RelBottom + NodeHeight(&TempInstance, 0, Nodes, Links);
                pType->DestroyNodeInstance(TempInstance);
                for (auto& S2 : Spines)
                {
                    if (S2.m_Id == SpineId || S2.m_ColumnId != ColumnOfSpine[SpineId]) continue;
                    auto& SL2 = SpineLayout[S2.m_Id];
                    if (SpineAbsY[SpineId] <= SpineAbsY[S2.m_Id] + SL2.m_RelBottom && SpineAbsY[S2.m_Id] <= NewBottom) return;
                }
            }

            // Addressed relative to whichever EXISTING node currently sits at this gap - see
            // create_node_cmd's own comment for why (node ids are already known/observable, an
            // invented "gap id" would need its own discovery step).
            const std::string Cmd = (Clamped < (int)SL.m_Order.size())
                ? commands::BuildCreateNodeCommand(Sources, AvailableTypes, Src, pType, commands::node_placement_kind::Before, SL.m_Order[Clamped])
                : commands::BuildCreateNodeCommand(Sources, AvailableTypes, Src, pType, commands::node_placement_kind::After, SL.m_Order.back());
            commands::Run(System, Cmd);
        };

        // Move an already-existing set of nodes (a drag-and-drop reorder or, now, a drag onto a
        // DIFFERENT spine's own marker) to stacking position GapIndex WITHIN SpineId. Same-spine drop:
        // a pure dense-renumber reorder (ReorderNodes), unchanged from before spines existed.
        // Cross-spine drop: issues MoveNodesToSpine instead, addressed the same way InsertNodeAt
        // addresses a new node (-After/-Before an existing node, or -InSpine for an empty target).
        auto MoveNodesTo = [&](std::uint64_t SpineId, const std::vector<std::uint64_t>& MovingIds, int GapIndex)
        {
            if (MovingIds.empty()) return;
            const bool bAllAlreadyHere = std::all_of(MovingIds.begin(), MovingIds.end(), [&](std::uint64_t Id) { auto* pN = FindNode(Id); return pN && pN->m_SpineId == SpineId; });
            auto& SL = SpineLayout[SpineId];

            if (bAllAlreadyHere)
            {
                std::vector<std::uint64_t> MovingInOrder, Remaining;
                for (auto Id : SL.m_Order)
                {
                    if (std::find(MovingIds.begin(), MovingIds.end(), Id) != MovingIds.end()) MovingInOrder.push_back(Id);
                    else Remaining.push_back(Id);
                }
                if (MovingInOrder.empty()) return;
                int Adjust = 0;
                for (int i = 0; i < GapIndex && i < (int)SL.m_Order.size(); ++i)
                    if (std::find(MovingIds.begin(), MovingIds.end(), SL.m_Order[i]) != MovingIds.end()) ++Adjust;
                const int NewGapIndex = std::clamp(GapIndex - Adjust, 0, (int)Remaining.size());
                Remaining.insert(Remaining.begin() + NewGapIndex, MovingInOrder.begin(), MovingInOrder.end());
                commands::Run(System, commands::MakeReorderNodes(Remaining));
                return;
            }

            // Cross-spine move: the same growth-blocking gate InsertNodeAt uses, checked only for an
            // append (past the end) - inserting into the middle is left unguarded for now, same
            // limitation InsertNodeAt already accepts. These are real (not throwaway) instances, so
            // their height is already known without needing to create/destroy a temporary one.
            const int Clamped = std::clamp(GapIndex, 0, (int)SL.m_Order.size());
            if (Clamped >= (int)SL.m_Order.size())
            {
                // A sibling that this exact move is about to drain completely (e.g. merging a whole
                // spine into another one sitting in the same column) isn't a real obstacle - by the time
                // the append actually lands there, it'll be gone.
                auto WouldBecomeEmpty = [&](std::uint64_t OtherSpineId)
                {
                    bool bHasAny = false;
                    for (auto& N : Nodes)
                        if (N.m_SpineId == OtherSpineId)
                        {
                            bHasAny = true;
                            if (std::find(MovingIds.begin(), MovingIds.end(), N.m_Id) == MovingIds.end()) return false;
                        }
                    return bHasAny;
                };
                float AddedHeight = 0.0f;
                for (auto Id : MovingIds) { auto* pN = FindNode(Id); if (pN && pN->m_pNode) AddedHeight += NodeHeight(pN->m_pNode, Id, Nodes, Links) + geo::NODE_GAP; }
                const float NewBottom = SpineAbsY[SpineId] + SL.m_RelBottom + AddedHeight;
                for (auto& S2 : Spines)
                {
                    if (S2.m_Id == SpineId || S2.m_ColumnId != ColumnOfSpine[SpineId] || WouldBecomeEmpty(S2.m_Id)) continue;
                    auto& SL2 = SpineLayout[S2.m_Id];
                    if (SpineAbsY[SpineId] <= SpineAbsY[S2.m_Id] + SL2.m_RelBottom && SpineAbsY[S2.m_Id] <= NewBottom) return;
                }
            }

            const std::string Cmd = SL.m_Order.empty() ? commands::MakeMoveNodesToSpineIn(MovingIds, SpineId)
                : (Clamped < (int)SL.m_Order.size() ? commands::MakeMoveNodesToSpineBefore(MovingIds, SL.m_Order[Clamped])
                                                     : commands::MakeMoveNodesToSpineAfter(MovingIds, SL.m_Order.back()));
            commands::Run(System, Cmd);
        };

        // ---- per-port side (L/R): chosen by wire direction, so a wire never crosses over its own
        // destination node - the source's output and the target's input both take the side that
        // matches whether the wire travels down (R) or up (L) in absolute world space. A link's
        // highway side is a pure per-link fact (which way it travels) - computing it fresh wherever
        // needed, rather than caching it keyed only by pin, is what fixes the "a pin with wires going
        // both up and down only ever renders on one side" bug. Compared by each node's own ABSOLUTE Y
        // (not a per-spine stacking index) specifically so this stays correct for a link between two
        // DIFFERENT spines that share a column - the highway/lane packing belongs to the column, not
        // the spine, and connect_cmd only forbids a link from leaving its column, not its spine.
        auto LinkSide = [&](const link_instance& L) -> char
        {
            auto* pSrcRow = FindRow(L.m_SourceNode); auto* pDstRow = FindRow(L.m_TargetNode);
            if (!pSrcRow || !pDstRow) return 'R';
            return (pDstRow->m_Y >= pSrcRow->m_Y) ? 'R' : 'L';
        };

        // Same column: the ORIGINAL rule above - both ends share one side, chosen purely by up/down
        // travel. Cross column: each end's pin instead renders on whichever of its own two edges faces
        // the OTHER column (so neither end ever has to route back around its own node to reach the
        // highway) - and the rail (within whichever column OwnerColumnOf resolves to) sits on that
        // same owning end's own facing side, so the vertical run starts already pointed the right way.
        auto LinkSides = [&](const link_instance& L, char& OutSourceSide, char& OutTargetSide, char& OutRailSide)
        {
            const auto SrcCol = ColumnOfNode(L.m_SourceNode), DstCol = ColumnOfNode(L.m_TargetNode);
            if (SrcCol == DstCol) { OutSourceSide = OutTargetSide = OutRailSide = LinkSide(L); return; }
            const bool bTargetRight = IsColumnRightOf(DstCol, SrcCol);
            OutSourceSide = bTargetRight ? 'R' : 'L';
            OutTargetSide = bTargetRight ? 'L' : 'R';
            OutRailSide   = (OwnerColumnOf(L) == SrcCol) ? OutSourceSide : OutTargetSide;
        };

        // Which side(s) a given pin actually needs a glyph rendered on - a set, not a single side, so a
        // pin used by links going in different directions (up vs down, or toward different columns)
        // gets one glyph per side actually in use.
        std::unordered_map<std::uint64_t, std::set<char>> PortSides;
        for (auto& Link : Links)
        {
            char SourceSide = 'R', TargetSide = 'R', RailSide = 'R';
            LinkSides(Link, SourceSide, TargetSide, RailSide);
            PortSides[OutPinOf(Link.m_SourceNode, Link.m_SourceOutput)].insert(SourceSide);
            PortSides[InPinOf(Link.m_TargetNode, Link.m_TargetInput)].insert(TargetSide);
        }
        // An unconnected pin defaults to the conventional side for its direction - input on the left,
        // output on the right - same as every other node-graph editor. A connected pin instead renders
        // wherever its actual link(s) go (PortSides above), which may not be the default side at all.
        auto SidesOf = [&](std::uint64_t PinId, bool bIsOutput) -> std::set<char>
        {
            auto It = PortSides.find(PinId);
            return It == PortSides.end() ? std::set<char>{ bIsOutput ? 'R' : 'L' } : It->second;
        };

        // Takes the side explicitly now - a port can have two valid anchor points (one per side it's
        // rendered on), so "the" anchor no longer makes sense without saying which one.
        auto PortAnchor = [&](std::uint64_t NodeId, const port_ref& P, char S) -> ImVec2
        {
            auto* pRow = FindRow(NodeId); auto* pDesc = DescOf(FindNode(NodeId));
            if (!pRow || !pDesc) return {};
            // An End marker has no body/rows at all - its pin (never drawn as a glyph) anchors
            // straight to the title bar's own edge instead.
            if (IsEndMarkerType(pDesc))
                return { (S == 'L') ? pRow->m_X : pRow->m_X + pRow->m_W, pRow->m_Y + geo::HEADER_H * 0.5f };
            float Y = pRow->m_Y + geo::HEADER_H + PreviewAreaHeight(pDesc);
            bool bHavePrevQ = false, bPrevQLocal = false;
            for (auto& Q : FlatPorts(pDesc))
            {
                // Same SECTION_GAP insertion as the draw loop (see LocalSectionGapTotal) - the anchor
                // walk has to reproduce every bit of the draw loop's own RowY accumulation, or a wire
                // ends up terminating above/below the glyph it's actually meant to touch, worse the
                // further down the port list the target sits.
                if (bHavePrevQ && Q.m_pDesc->m_bLocalScope && !bPrevQLocal) Y += geo::SECTION_GAP;
                bPrevQLocal = Q.m_pDesc->m_bLocalScope;
                bHavePrevQ = true;
                // Half of ROW_H specifically, matching the drawing loop's own CenterY (RowY +
                // ROW_H*0.5) - NOT half of RowHeight(), which also counts the value-line space below
                // the glyph and would anchor wires visibly below the actual drawn pin.
                if (Q.m_bIsOutput == P.m_bIsOutput && Q.m_Index == P.m_Index) { Y += geo::ROW_H * 0.5f; break; }
                Y += RowHeight(Q, NodeId, Links, EffectiveTypeName(NodeId, pDesc, Q.m_pDesc->m_pTypeName, Nodes, Links));
            }
            return { (S == 'L') ? pRow->m_X : pRow->m_X + pRow->m_W, Y };
        };

        // ---- lane packing: greedy interval partitioning, NOT rslgraph-ui's own laneOf (which is just
        // a stateless per-side counter - order of appearance, nothing more; verified directly in
        // Canvas.tsx). Sorting by Y-span length before assigning means a short/local hop always gets
        // first pick of the innermost lane, and only actually claims a new lane when it truly overlaps
        // something already there. Pooled PER COLUMN, keyed by OwnerColumnOf(Link) - a connection can
        // now span any two columns, but its vertical run only ever lives in ONE of them at a time (see
        // OwnerColumnOf's own comment) - no two columns' highways ever interact.
        //
        // Two spans that share the same SOURCE pin (one output fanning out to several inputs, e.g. a
        // Cube's Mesh feeding both Export Mesh and Inspect Mesh) are never considered to "overlap"
        // each other, even when their Y-ranges do - they're the same trunk visually, just splitting
        // off toward different destinations, not independent wires that happen to cross. Without this,
        // every fan-out target claimed its own parallel lane for the full length of the shared run,
        // which is exactly the "too many highways" the fan-out case was producing.
        struct link_lane_interval { float m_Lo, m_Hi; std::uint64_t m_SourcePin; };
        std::unordered_map<std::uint64_t, std::vector<std::vector<link_lane_interval>>> LaneIntervalsBySide[2]; // [0]=L, [1]=R, keyed by column id
        std::unordered_map<std::uint64_t, int> LaneOfLink;
        {
            struct link_span { std::uint64_t m_LinkId, m_ColumnId; int m_Side; float m_Lo, m_Hi; std::uint64_t m_SourcePin; };
            std::vector<link_span> Spans;
            for (auto& Link : Links)
            {
                auto* pSrcDesc = DescOf(FindNode(Link.m_SourceNode)); auto* pDstDesc = DescOf(FindNode(Link.m_TargetNode));
                if (!pSrcDesc || !pDstDesc) continue;
                const auto SrcOutputs = pSrcDesc->getOutputs(); const auto DstInputs = pDstDesc->getInputs();
                if (Link.m_SourceOutput >= (int)SrcOutputs.size() || Link.m_TargetInput >= (int)DstInputs.size()) continue;
                const port_ref OutP{ true, Link.m_SourceOutput, &SrcOutputs[Link.m_SourceOutput] };
                const port_ref InP { false, Link.m_TargetInput,  &DstInputs[Link.m_TargetInput] };
                char SourceSide = 'R', TargetSide = 'R', RailSide = 'R';
                LinkSides(Link, SourceSide, TargetSide, RailSide);
                const float FromY = PortAnchor(Link.m_SourceNode, OutP, SourceSide).y, ToY = PortAnchor(Link.m_TargetNode, InP, TargetSide).y;
                const int Side2 = (RailSide == 'R') ? 1 : 0;
                Spans.push_back({ Link.m_Id, OwnerColumnOf(Link), Side2, std::min(FromY, ToY), std::max(FromY, ToY), PinOf(OutP, Link.m_SourceNode) });
            }
            // Fan-out from one source pin (e.g. a Cube's Mesh feeding both Export Mesh and Inspect
            // Mesh) is one trunk, not several independent wires - a lane it claims has to be reserved
            // across the FULL combined range of every branch, not just whichever branch is being
            // placed at the moment. Without grouping branches together BEFORE lane assignment, a
            // short branch (the same source feeding a nearby target) can let an unrelated, different-
            // source link slot into the gap right after it, only for a LONGER branch of the same
            // trunk to collide with that unrelated link later and get bounced to its own lane anyway
            // - the short branch reused the lane fine, but the long one couldn't, because something
            // else had already moved into the space it needed too.
            struct span_group { std::uint64_t m_ColumnId; int m_Side; std::uint64_t m_SourcePin; float m_Lo, m_Hi; std::vector<std::uint64_t> m_LinkIds; };
            std::vector<span_group> Groups;
            for (auto& S : Spans)
            {
                auto It = std::find_if(Groups.begin(), Groups.end(), [&](auto& G) { return G.m_ColumnId == S.m_ColumnId && G.m_Side == S.m_Side && G.m_SourcePin == S.m_SourcePin; });
                if (It == Groups.end())
                    Groups.push_back({ S.m_ColumnId, S.m_Side, S.m_SourcePin, S.m_Lo, S.m_Hi, { S.m_LinkId } });
                else
                {
                    It->m_Lo = std::min(It->m_Lo, S.m_Lo);
                    It->m_Hi = std::max(It->m_Hi, S.m_Hi);
                    It->m_LinkIds.push_back(S.m_LinkId);
                }
            }
            std::sort(Groups.begin(), Groups.end(), [](auto& A, auto& B) { return (A.m_Hi - A.m_Lo) < (B.m_Hi - B.m_Lo); });
            for (auto& G : Groups)
            {
                auto& Lanes = LaneIntervalsBySide[G.m_Side][G.m_ColumnId];
                int ChosenLane = -1;
                for (int L = 0; L < (int)Lanes.size(); ++L)
                {
                    bool bOverlaps = false;
                    for (auto& Iv : Lanes[L])
                    {
                        if (Iv.m_SourcePin == G.m_SourcePin) continue; // same trunk - never blocks sharing a lane (groups are already merged by source pin, kept as a safety net)
                        if (G.m_Lo <= Iv.m_Hi && G.m_Hi >= Iv.m_Lo) { bOverlaps = true; break; }
                    }
                    if (!bOverlaps) { ChosenLane = L; break; }
                }
                if (ChosenLane < 0) { ChosenLane = (int)Lanes.size(); Lanes.push_back({}); }
                Lanes[ChosenLane].push_back({ G.m_Lo, G.m_Hi, G.m_SourcePin });
                for (auto LinkId : G.m_LinkIds) LaneOfLink[LinkId] = ChosenLane;
            }
        }
        auto LaneCountOf = [&](std::uint64_t ColId, int Side01) -> int
        {
            auto It = LaneIntervalsBySide[Side01].find(ColId);
            return It == LaneIntervalsBySide[Side01].end() ? 0 : (int)It->second.size();
        };

        // ---- Pass C: per-column X, root first then walking Left/Right outward - required order, not
        // a choice: each column's X depends on the previous column's own live highway extent on that
        // side. A column's width is its own boxes + its own highway lane extent - no two columns'
        // highways ever interact. ----
        std::unordered_map<std::uint64_t, float> ColumnWidestNode;
        for (auto& S : Spines)
        {
            auto& W = ColumnWidestNode[S.m_ColumnId];
            W = std::max(W, SpineLayout[S.m_Id].m_WidestNode);
        }
        auto HighwayBaseOf = [&](std::uint64_t ColId) -> float
        {
            auto It = ColumnWidestNode.find(ColId);
            return (It == ColumnWidestNode.end() ? 120.0f : It->second) * 0.5f + geo::ICON_CLEARANCE;
        };
        auto Extent = [&](std::uint64_t ColId, char Side) -> float
        {
            const int Count = LaneCountOf(ColId, Side == 'R' ? 1 : 0);
            return HighwayBaseOf(ColId) + std::max(0, Count - 1) * geo::LANE_GAP;
        };

        std::unordered_map<std::uint64_t, float> ColumnX;
        std::uint64_t RootColumnId = 0;
        for (auto& Co : Columns) if (Co.m_bIsRoot) { RootColumnId = Co.m_Id; break; }
        ColumnX[RootColumnId] = std::max(AvailWidth * 0.5f, 260.0f); // centered on the window, never so narrow the highways collide
        for (auto* pCo = FindColumn(RootColumnId); pCo && pCo->m_LeftId; )
        {
            auto* pNext = FindColumn(pCo->m_LeftId);
            if (!pNext) break;
            ColumnX[pNext->m_Id] = ColumnX[pCo->m_Id] - (Extent(pCo->m_Id, 'L') + geo::COLUMN_MARGIN + Extent(pNext->m_Id, 'R'));
            pCo = pNext;
        }
        for (auto* pCo = FindColumn(RootColumnId); pCo && pCo->m_RightId; )
        {
            auto* pNext = FindColumn(pCo->m_RightId);
            if (!pNext) break;
            ColumnX[pNext->m_Id] = ColumnX[pCo->m_Id] + (Extent(pCo->m_Id, 'R') + geo::COLUMN_MARGIN + Extent(pNext->m_Id, 'L'));
            pCo = pNext;
        }
        for (auto& R : Layout) R.m_X = ColumnX[ColumnOfNode(R.m_NodeId)] - R.m_W * 0.5f;

        const float SpineX = ColumnX[RootColumnId]; // the window always centers on the ROOT column - see ToScreen below, unchanged from before spines existed
        auto HighwayX = [&](std::uint64_t ColId, char S, int Lane) { const float D = HighwayBaseOf(ColId) + Lane * geo::LANE_GAP; return S == 'L' ? ColumnX[ColId] - D : ColumnX[ColId] + D; };

        const ImVec2 WindowOrigin = ImGui::GetCursorScreenPos();
        const float  AvailHeight  = ImGui::GetContentRegionAvail().y; // captured before canvas_bg (below) advances the cursor and shrinks it
        const bool bWindowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        // A PURE geometric "is the mouse over the canvas" test, deliberately not IsWindowHovered -
        // used only for wheel-zoom and right-drag-pan below, both of which need to keep working while
        // a pin-to-pin or node-reorder drag (left button) is in progress: only one item can ever be
        // ImGui's "active" item at a time, so once that left-button drag claims it, IsWindowHovered's
        // default "blocked by active item elsewhere" gating starves anything gated on it, exactly the
        // same class of bug this file has already hit (and fixed the same way) for port hover/node-drag
        // drop resolution. bWindowHovered itself stays as-is for the Delete-key check below, which
        // SHOULD respect normal window-focus/Z-order semantics.
        const bool bMouseInCanvasRect = ImGui::IsMouseHoveringRect(WindowOrigin, { WindowOrigin.x + AvailWidth, WindowOrigin.y + AvailHeight });
        const float WindowCenterX = WindowOrigin.x + AvailWidth * 0.5f;

        // Wheel zoom, anchored under the cursor on both axes. The canvas has no native scrollbar (see
        // ImGuiWindowFlags above); right-drag on empty canvas space (below) handles panning, both axes.
        if (bMouseInCanvasRect)
        {
            const float Wheel = ImGui::GetIO().MouseWheel;
            if (Wheel != 0.0f)
            {
                const float LocalXAtMouse = (ImGui::GetIO().MousePos.x - WindowCenterX - View.m_PanX) / View.m_Zoom + SpineX;
                const float LocalYAtMouse = (ImGui::GetIO().MousePos.y - WindowOrigin.y - View.m_PanY) / View.m_Zoom;
                View.m_Zoom = std::clamp(View.m_Zoom + Wheel * 0.1f, 0.3f, 2.5f);
                View.m_PanX = ImGui::GetIO().MousePos.x - WindowCenterX - (LocalXAtMouse - SpineX) * View.m_Zoom;
                View.m_PanY = ImGui::GetIO().MousePos.y - WindowOrigin.y - LocalYAtMouse * View.m_Zoom;
            }
        }

        auto ToScreen    = [&](ImVec2 P) { return ImVec2(WindowCenterX + View.m_PanX + (P.x - SpineX) * View.m_Zoom, WindowOrigin.y + View.m_PanY + P.y * View.m_Zoom); };
        auto ToScreenLen = [&](float L) { return L * View.m_Zoom; };
        ImDrawList* pDraw = ImGui::GetWindowDrawList();
        const ImVec2 MouseLocal{ SpineX + (ImGui::GetIO().MousePos.x - WindowCenterX - View.m_PanX) / View.m_Zoom, (ImGui::GetIO().MousePos.y - WindowOrigin.y - View.m_PanY) / View.m_Zoom };

        // Backdrop + dot grid, drawn before anything else so every node/wire sits on top of it - the
        // same visual signature Unity's own node editors (Shader Graph, Visual Scripting) use: a dark
        // charcoal canvas with a faint, evenly-spaced dot grid, never a plain flat fill. Dots are laid
        // out in WORLD space (inverting ToScreen for the window's own visible rect) so they pan and
        // zoom together with the graph instead of scrolling independently of it, but drawn at a fixed
        // on-screen size regardless of zoom - the same reason grid lines in most editors stay a
        // constant weight rather than getting thicker/thinner as you zoom.
        {
            const ImVec2 WinMax{ WindowOrigin.x + AvailWidth, WindowOrigin.y + AvailHeight };
            pDraw->AddRectFilled(WindowOrigin, WinMax, theme::Canvas);

            constexpr float GridStep = 32.0f; // world units between dots
            const float WorldXMin = (WindowOrigin.x - WindowCenterX - View.m_PanX) / View.m_Zoom + SpineX;
            const float WorldXMax = (WinMax.x - WindowCenterX - View.m_PanX) / View.m_Zoom + SpineX;
            const float WorldYMin = (WindowOrigin.y - WindowOrigin.y - View.m_PanY) / View.m_Zoom;
            const float WorldYMax = (WinMax.y - WindowOrigin.y - View.m_PanY) / View.m_Zoom;
            const int FirstCol = (int)std::floor(WorldXMin / GridStep), LastCol = (int)std::ceil(WorldXMax / GridStep);
            const int FirstRow = (int)std::floor(WorldYMin / GridStep), LastRow = (int)std::ceil(WorldYMax / GridStep);
            // Zoomed out far enough to need more than this many dots in either direction, skip the
            // grid entirely rather than looping tens of thousands of times - it would be so dense at
            // that point it'd just read as a gray wash anyway.
            if (LastCol - FirstCol < 400 && LastRow - FirstRow < 400)
            {
                pDraw->PushClipRect(WindowOrigin, WinMax, true);
                for (int Row = FirstRow; Row <= LastRow; ++Row)
                    for (int Col = FirstCol; Col <= LastCol; ++Col)
                        pDraw->AddCircleFilled(ToScreen({ Col * GridStep, Row * GridStep }), 1.5f, theme::Grid, 6);
                pDraw->PopClipRect();
            }
        }

        // Background catcher, submitted first so it sits "under" every node/pin/button widget
        // (each marked AllowOverlap below to win hover/clicks over this) - gives click-on-empty-space
        // (deselect), left-drag-to-pan, and right-click-for-Add-Node without needing per-region
        // invisible buttons. Sized to the window itself (not the full zoomed content) since panning,
        // not scrolling, covers the rest - a click anywhere in the visible window should count as a
        // background click.
        ImGui::SetNextItemAllowOverlap(); // first-submitted covering the whole window - without this it
                                           // permanently owns hover for the frame and blocks every node/
                                           // pin/button drawn after it (xgpu_imgui_overlapping_invisible_buttons).
        ImGui::SetCursorScreenPos(WindowOrigin);
        // InvisibleButton only reacts to the left mouse button unless told otherwise - panning needs
        // the right button explicitly enabled here, or IsItemActive()/IsMouseDragging(Right) below never
        // fires at all.
        ImGui::InvisibleButton("canvas_bg", ImGui::GetContentRegionAvail(), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

        // Right-drag pans, leaving left click free for pure selection (click a link, click a node) with
        // no drag-vs-click ambiguity to resolve on that button anymore. Tracked by hand (geometric
        // press test, then just follow the raw button state to release) rather than canvas_bg's own
        // IsItemActive(), for the same reason wheel-zoom above switched to bMouseInCanvasRect: a
        // concurrent left-button pin-to-pin or node-reorder drag already holds ImGui's one "active
        // item" slot, so gating panning on canvas_bg becoming active too would silently never fire
        // while such a drag is in progress - and panning/zooming while dragging a connection is
        // exactly the case this needs to keep working for.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && bMouseInCanvasRect)
            View.m_bPanDragActive = true;
        if (View.m_bPanDragActive)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
            {
                View.m_PanX += ImGui::GetIO().MouseDelta.x;
                View.m_PanY += ImGui::GetIO().MouseDelta.y;
            }
            else
                View.m_bPanDragActive = false;
        }
        const bool bBackgroundClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

        auto DrawHighwayPath = [&](std::uint64_t ColId, ImVec2 From, ImVec2 To, char S, int Lane, ImU32 Col, float Thickness, const float* pDash)
        {
            const float HX = HighwayX(ColId, S, Lane);
            (void)pDash; // no native dashed-line primitive - dashing omitted, solid preview line is distinguished by color instead
            pDraw->AddLine(ToScreen(From), ToScreen({ HX, From.y }), Col, Thickness);
            pDraw->AddLine(ToScreen({ HX, From.y }), ToScreen({ HX, To.y }), Col, Thickness);
            pDraw->AddLine(ToScreen({ HX, To.y }), ToScreen(To), Col, Thickness);
        };

        // One rail pair per column, each spanning the full graph height - harmless to draw past a
        // column's own actual content, and simpler than tracking each column's own vertical extent.
        // Drawn at the OUTERMOST lane actually in use on each side, not lane 0 - a column carrying
        // more than one parallel wire on a side needs its rail to reach as far out as its farthest
        // lane, or the backdrop line would cut through wires instead of framing all of them.
        for (auto& Co : Columns)
        {
            const int LOuter = std::max(0, LaneCountOf(Co.m_Id, 0) - 1);
            const int ROuter = std::max(0, LaneCountOf(Co.m_Id, 1) - 1);
            pDraw->AddLine(ToScreen({ HighwayX(Co.m_Id, 'L', LOuter), 0 }), ToScreen({ HighwayX(Co.m_Id, 'L', LOuter), TotalH }), theme::Rail);
            pDraw->AddLine(ToScreen({ HighwayX(Co.m_Id, 'R', ROuter), 0 }), ToScreen({ HighwayX(Co.m_Id, 'R', ROuter), TotalH }), theme::Rail);
        }

        const auto ScopeDepths     = ComputeScopeDepths(Nodes);
        const auto EnclosingChains = ComputeEnclosingChains(Nodes);

        for (auto& Link : Links)
        {
            auto* pSrcDesc = DescOf(FindNode(Link.m_SourceNode)); auto* pDstDesc = DescOf(FindNode(Link.m_TargetNode));
            if (!pSrcDesc || !pDstDesc) continue;
            const auto SrcOutputs = pSrcDesc->getOutputs(); const auto DstInputs = pDstDesc->getInputs();
            if (Link.m_SourceOutput >= (int)SrcOutputs.size() || Link.m_TargetInput >= (int)DstInputs.size()) continue;
            const port_ref OutP{ true, Link.m_SourceOutput, &SrcOutputs[Link.m_SourceOutput] };
            const port_ref InP { false, Link.m_TargetInput,  &DstInputs[Link.m_TargetInput] };
            const bool bSelected = (Selection.m_SelectedLink == Link.m_Id);
            // TypeColor("Scope") already matches the box border color, so an ordinary owner<->End
            // link (always a Scope pin) picks up the right color for free here - only its extra
            // thickness needs a read-only-specific branch. A Scope link's own two ends always share
            // the same depth by construction (an owner and its End marker both resolve to the same
            // stack size in ComputeScopeDepths), so either endpoint's depth darkens the whole wire,
            // matching the boxes it connects.
            // EffectiveTypeName, not the source pin's raw declared type - a wire out of a wildcard
            // output (Math Expression/Compare's own "Any" Result) must show the type it actually
            // RESOLVED to, matching what the pin glyph on either end already colors itself by, not
            // the perpetually-neutral "Any" gray every such wire would otherwise be stuck showing
            // even once it's carrying, say, a real Float.
            ImU32 Col = bSelected ? theme::Selected : TypeColor(EffectiveTypeName(Link.m_SourceNode, pSrcDesc, OutP.m_pDesc->m_pTypeName, Nodes, Links));
            if (!bSelected && Link.m_bReadOnly)
            {
                auto DepthIt = ScopeDepths.find(Link.m_SourceNode);
                Col = DarkenForDepth(Col, DepthIt == ScopeDepths.end() ? 0 : DepthIt->second);
            }
            // A data link (never a read-only ownership wire, which is always structurally valid by
            // construction) whose source the target could never actually reference in real nested
            // C++ - see IsDataLinkScopeValid - flags in a clear warning color regardless of type or
            // selection state. There's no compiler to catch this yet; this is the honest stand-in for
            // the diagnostic one would eventually give (matching how Unreal Blueprint/Unity Visual
            // Scripting both let the wire get drawn and only flag it later - just surfaced immediately
            // here instead of deferred, since there's nothing else to defer it to today).
            const bool bScopeInvalid = !Link.m_bReadOnly && !IsDataLinkScopeValid(Link.m_SourceNode, Link.m_SourceOutput, Link.m_TargetNode, Link.m_TargetInput, Nodes, EnclosingChains);
            if (bScopeInvalid) Col = IM_COL32(239, 68, 68, 255);
            const float Thickness = bScopeInvalid ? 3.0f : bSelected ? 3.0f : (Link.m_bReadOnly ? 4.0f : 2.0f);
            char SourceSide = 'R', TargetSide = 'R', RailSide = 'R';
            LinkSides(Link, SourceSide, TargetSide, RailSide);
            DrawHighwayPath(OwnerColumnOf(Link), PortAnchor(Link.m_SourceNode, OutP, SourceSide), PortAnchor(Link.m_TargetNode, InP, TargetSide)
                           , RailSide, LaneOfLink[Link.m_Id], Col, Thickness, nullptr);
        }

        if (Drag.m_bActive)
        {
            const char S = Drag.m_FromSide;
            const auto DragColumnId = ColumnOfNode(Drag.m_FromNode);
            DrawHighwayPath(DragColumnId, Drag.m_FromPos, MouseLocal, S, LaneCountOf(DragColumnId, S == 'R' ? 1 : 0), IM_COL32(125, 211, 252, 255), 2.0f, nullptr);
        }

        // Selecting a scope's ownership link highlights every box the scope actually contains - a
        // visual-only highlight (see ComputeScopeSpan), never added to Selection.m_SelectedNodes, so
        // inspecting a scope this way never makes its contents eligible for Delete or drag.
        std::set<std::uint64_t> HighlightedScopeSpan;
        if (Selection.m_SelectedLink != 0)
            for (auto& L : Links)
                if (L.m_Id == Selection.m_SelectedLink && L.m_bReadOnly)
                {
                    auto Span = ComputeScopeSpan(Nodes, L.m_SourceNode);
                    HighlightedScopeSpan.insert(Span.begin(), Span.end());
                    break;
                }

        const float FontSize = ImGui::GetFontSize() * View.m_Zoom;
        auto DrawText = [&](ImVec2 Pos, ImU32 Col, const char* pText) { pDraw->AddText(nullptr, FontSize, ToScreen(Pos), Col, pText); };

        // The drag origin's own effective type, fixed for the whole drag - computed once per frame
        // rather than once per candidate pin.
        auto* pFromDescForDrag = Drag.m_bActive ? DescOf(FindNode(Drag.m_FromNode)) : nullptr;
        std::string FromEffForDrag;
        if (pFromDescForDrag)
        {
            const char* pFromType = Drag.m_bFromIsOutput ? pFromDescForDrag->getOutputs()[Drag.m_FromIndex].m_pTypeName : pFromDescForDrag->getInputs()[Drag.m_FromIndex].m_pTypeName;
            FromEffForDrag = EffectiveTypeName(Drag.m_FromNode, pFromDescForDrag, pFromType, Nodes, Links);
        }

        // Whether a SPECIFIC candidate pin (opposite direction from the drag's own origin, PortIndex
        // into that direction's own port list) would actually accept the drop - by type AND scope
        // (IsDataLinkScopeValid). A node can carry both eligible and ineligible pins at once (a
        // Function's external vs. local-mirrored ports; ForEachLoop's Span vs. Element/Index), so
        // this is checked per-pin - both to dim one ineligible pin without fading its whole node (see
        // the port-row loop below) and, aggregated across a node's full port list in
        // NodeAcceptsDrag, to fade a node that has NO eligible pin anywhere on it.
        auto PortAcceptsDrag = [&](std::uint64_t CandidateId, int PortIndex) -> bool
        {
            if (!Drag.m_bActive || CandidateId == Drag.m_FromNode || !pFromDescForDrag) return true;
            auto* pCandDesc = DescOf(FindNode(CandidateId));
            if (!pCandDesc) return true;
            const auto CandPorts = Drag.m_bFromIsOutput ? pCandDesc->getInputs() : pCandDesc->getOutputs();
            if (PortIndex < 0 || PortIndex >= (int)CandPorts.size()) return true;
            const std::string ToEff = EffectiveTypeName(CandidateId, pCandDesc, CandPorts[PortIndex].m_pTypeName, Nodes, Links);
            if (!IsAnyKindOfWildcard(FromEffForDrag.c_str()) && !IsAnyKindOfWildcard(ToEff.c_str()) && FromEffForDrag != ToEff) return false;
            const std::uint64_t SrcForScope    = Drag.m_bFromIsOutput ? Drag.m_FromNode  : CandidateId;
            const std::uint64_t TgtForScope    = Drag.m_bFromIsOutput ? CandidateId      : Drag.m_FromNode;
            const int           SrcOutForScope = Drag.m_bFromIsOutput ? Drag.m_FromIndex : PortIndex;
            const int           TgtInForScope  = Drag.m_bFromIsOutput ? PortIndex        : Drag.m_FromIndex;
            return IsDataLinkScopeValid(SrcForScope, SrcOutForScope, TgtForScope, TgtInForScope, Nodes, EnclosingChains);
        };

        // A node fades out (in the draw loop below) only when NOT ONE of its ports would accept the
        // drop - if even one pin qualifies, the node stays visible and the per-pin dim below marks
        // its other, ineligible pins instead.
        auto NodeAcceptsDrag = [&](std::uint64_t CandidateId) -> bool
        {
            if (!Drag.m_bActive || CandidateId == Drag.m_FromNode) return true;
            auto* pCandDesc = DescOf(FindNode(CandidateId));
            if (!pCandDesc) return true;
            const auto CandPorts = Drag.m_bFromIsOutput ? pCandDesc->getInputs() : pCandDesc->getOutputs();
            for (int i = 0; i < (int)CandPorts.size(); ++i)
                if (PortAcceptsDrag(CandidateId, i)) return true;
            return false;
        };

        // Native ImGui widgets (the inline-literal InputFloat/InputInt boxes, the enum Combo dropdown)
        // have no notion of this canvas's own pan/zoom transform - left alone, their text stays
        // pinned at the base font size no matter how far the graph is zoomed, drifting out of step
        // with every hand-drawn label around them (row names, titles, pin types) the instant Zoom
        // isn't exactly 1.0. SetWindowFontScale is ImGui's own mechanism for exactly this: scales
        // every widget's text drawn while it's active, for the rest of this window. Reset back to
        // 1.0 right after the node loop, before any popup menu below it - a right-click "Add Node"
        // menu should stay a fixed, comfortable size regardless of how zoomed-in the canvas is.
        ImGui::SetWindowFontScale(View.m_Zoom);
        for (size_t oi = 0; oi < Order.size(); ++oi)
        {
            const auto Id = Order[oi];
            auto* pRow  = FindRow(Id);
            auto* pNode = FindNode(Id);
            auto* pDesc = DescOf(pNode);
            if (!pRow || !pDesc) continue;

            const ImVec2 P0 = ToScreen({ pRow->m_X, pRow->m_Y });
            const ImVec2 P1 = ToScreen({ pRow->m_X + pRow->m_W, pRow->m_Y + pRow->m_H });
            const bool bSelected      = Selection.m_SelectedNodes.contains(Id);
            const bool bBeingDragged  = NodeDrag.m_bActive && std::find(NodeDrag.m_MovingIds.begin(), NodeDrag.m_MovingIds.end(), Id) != NodeDrag.m_MovingIds.end();

            // Depth stands in for indentation (see ComputeScopeDepths) - the deeper a node's own
            // enclosing scope, the darker its fill; the border stays untouched so the box outline is
            // always visible no matter how deep.
            const auto DepthIt = ScopeDepths.find(Id);
            const int  Depth   = DepthIt == ScopeDepths.end() ? 0 : DepthIt->second;
            const bool bInHighlightedSpan = HighlightedScopeSpan.contains(Id);
            const bool bIsEndMarker = IsEndMarkerType(pDesc);
            pDraw->AddRectFilled(P0, P1, DarkenForDepth(theme::NodeBg, Depth), 0.0f);
            // Title-row strip, brighter than the body beneath it - drawn as its own rect ON TOP of
            // the body fill (not a separate widget), so the node's name/category line reads as a
            // real header at a glance instead of just floating text over the same flat body color.
            // Tinted by the node's own category (CategoryColor) rather than one flat gray for every
            // node, Unity-style - falls back to the old neutral gray for any category not listed
            // there. An End marker draws no header at all - it's deliberately just a plain title bar
            // with no body/ports beneath it, nothing to visually separate a "header" from, and no
            // category of its own to tint by.
            const auto NodeCategory = bIsEndMarker ? std::string_view{} : pDesc->m_pFactory->getCategory();
            if (!bIsEndMarker)
                pDraw->AddRectFilled(P0, { P1.x, P0.y + ToScreenLen(geo::HEADER_H) }, DarkenForDepth(CategoryColor(NodeCategory), Depth), 0.0f);
            pDraw->AddRect(P0, P1, bBeingDragged ? IM_COL32(56, 189, 248, 255) : (bSelected ? theme::Selected : (bInHighlightedSpan ? IM_COL32(125, 211, 252, 255) : theme::NodeBorder))
                          , 0.0f, 0, ToScreenLen((bSelected || bBeingDragged || bInHighlightedSpan) ? 2.5f : 1.5f));
            // Screen-space: title/category text is measured and positioned entirely in screen
            // pixels at the EXACT font size passed to AddText below. Measuring via unscaled
            // ImGui::CalcTextSize/GetFontSize here would silently pick up SetWindowFontScale's
            // zoom-scaled font (it's active for this whole loop, for native widgets' benefit) and
            // then get scaled AGAIN by ToScreen - a double-scale that's invisible at Zoom=1.0 (a
            // no-op) but drifts/overlaps at any other zoom. Same fix as the pin-type label below.
            // The title reads first at a glance, so it renders a bit bigger than every other label
            // on the node (row names, pin types, category) - TITLE_FONT_SCALE, matched by
            // NodeWidth's own TitleW reservation so a bigger title never re-collides with the
            // category text the way the original bug did.
            const float TitleFontSize = FontSize * geo::TITLE_FONT_SCALE;
            const float TitleYPx = P0.y + (ToScreenLen(geo::HEADER_H) - TitleFontSize) * 0.5f;
            const auto NodeName = pDesc->m_pFactory->getName();
            // An End marker's own factory name is just "End" for every instance - not clear enough
            // on its own (NODE_SCRIPTING_DESIGN.md section 4.2). Its actual displayed title is
            // computed contextually: "<Owner>-End" for a plain marker, "<Owner>-End-Else" once its
            // own IsElse checkbox is on, or "Else-End" for the further marker THAT one owns in turn
            // (recognized by its owner also being an End, not a real control node).
            std::string DisplayName(NodeName);
            if (NodeName == "End")
            {
                node_instance* pOwnerNode = nullptr;
                for (auto& N : Nodes) if (N.m_OwnedEndId == Id) { pOwnerNode = &N; break; }
                if (pOwnerNode && pOwnerNode->m_pNode)
                {
                    const auto OwnerName = pOwnerNode->m_pNode->m_pFactory->getName();
                    if (OwnerName == "End") DisplayName = "Else-End";
                    else
                    {
                        const bool bIsElse = pNode->m_pNode && ReadBoolPropertyFromSnapshot(SerializePropertiesToString(pNode->m_pNode), "IsElse");
                        DisplayName = std::string(OwnerName) + (bIsElse ? "-End-Else" : "-End");
                    }
                }
            }
            const ImVec2 NameSizePx = ImGui::GetFont()->CalcTextSizeA(TitleFontSize, FLT_MAX, 0.0f, DisplayName.c_str());
            const float TitleXPx = bIsEndMarker
                ? P0.x + (P1.x - P0.x) * 0.5f - NameSizePx.x * 0.5f
                : P0.x + ToScreenLen(10.0f);
            pDraw->AddText(nullptr, TitleFontSize, ImVec2(TitleXPx, TitleYPx), IM_COL32(226, 232, 240, 255), DisplayName.c_str());
            // An End marker's title bar shows only its name - no category label, no header/body
            // divider line (it has no body to divide from). The category label stays at the normal
            // (non-title) font size - it's an annotation next to the title, not a second title.
            if (!bIsEndMarker)
            {
                const ImVec2 CatSizePx = ImGui::GetFont()->CalcTextSizeA(FontSize, FLT_MAX, 0.0f, NodeCategory.data(), NodeCategory.data() + NodeCategory.size());
                const float CatXPx = P1.x - CatSizePx.x - ToScreenLen(10.0f);
                pDraw->AddText(nullptr, FontSize, ImVec2(CatXPx, P0.y + (ToScreenLen(geo::HEADER_H) - FontSize) * 0.5f), IM_COL32(100, 116, 139, 255), NodeCategory.data());
                pDraw->AddLine(ToScreen({ pRow->m_X, pRow->m_Y + geo::HEADER_H }), ToScreen({ pRow->m_X + pRow->m_W, pRow->m_Y + geo::HEADER_H }), theme::NodeBorder);
            }

            ImGui::PushID((int)Id);
            ImGui::SetNextItemAllowOverlap();
            ImGui::SetCursorScreenPos(P0);
            ImGui::InvisibleButton("body", ImVec2(ToScreenLen(pRow->m_W), ToScreenLen(pRow->m_H)));

            // Press decides the moving set (this node's whole multi-selection, if it's part of one)
            // before any drag distance is known, so a plain click still agrees with what a drag would
            // have moved. Only an actual drag (past the threshold) acts on it, marked via
            // NodeDrag.m_bActive - a click that never turns into a drag falls through to selection
            // below instead.
            if (ImGui::IsItemActivated())
            {
                NodeDrag.m_bActive = false;
                auto InitialIds = Selection.m_SelectedNodes.contains(Id)
                    ? std::vector<std::uint64_t>(Selection.m_SelectedNodes.begin(), Selection.m_SelectedNodes.end())
                    : std::vector<std::uint64_t>{ Id };
                NodeDrag.m_MovingIds = ExpandMoveSetForScopes(Nodes, std::move(InitialIds));
            }
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f))
                NodeDrag.m_bActive = true;

            if (ImGui::IsItemClicked() && !NodeDrag.m_bActive)
            {
                // A control node and its owned End/End-Else marker(s) are one compound node (NODE_
                // SCRIPTING_DESIGN.md section 4.1) - clicking any one piece selects the whole group,
                // same cascade DeleteNodes already uses so the two notions of "one unit" agree.
                const auto GroupIds = commands::ExpandOwnershipCascade(Nodes, { Id });

                // Toggle decided here, against the CURRENT selection, then issued as the full desired
                // end-state - Select's Redo() just sets exactly what's in the command string (so replay
                // during a later Redo() stays deterministic regardless of what's currently selected).
                const bool bToggle = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
                std::set<std::uint64_t> NewSelectedNodes = bToggle ? Selection.m_SelectedNodes : std::set<std::uint64_t>{};
                if (bToggle)
                {
                    if (NewSelectedNodes.contains(Id)) for (auto Gid : GroupIds) NewSelectedNodes.erase(Gid);
                    else                                 for (auto Gid : GroupIds) NewSelectedNodes.insert(Gid);
                }
                else
                    NewSelectedNodes = { GroupIds.begin(), GroupIds.end() };
                commands::Run(System, commands::MakeSelectNodes({ NewSelectedNodes.begin(), NewSelectedNodes.end() }));
            }

            // Reorder (drag a node onto a spine "+" marker) and delete (Delete key) both already exist
            // elsewhere, so the header no longer needs its own ^/v/x buttons for them.

            // Preview block - every Mesh-typed port's live render, grouped right under the header,
            // ABOVE the port rows: "what this node shows" first, "what it's wired to" underneath.
            float RowY = pRow->m_Y + geo::HEADER_H;
            if (MeshPortCount(pDesc) > 0)
            {
                RowY += geo::PREVIEW_GAP;
                for (auto& P : FlatPorts(pDesc))
                {
                    if (!IsMeshType(P.m_pDesc->m_pTypeName)) continue;
                    ImGui::SetNextItemAllowOverlap();
                    ImGui::SetCursorScreenPos(ToScreen({ pRow->m_X + pRow->m_W * 0.5f - mesh_preview_system::s_PreviewSize * 0.5f, RowY }));
                    MeshPreview.DrawPreviewSquare(PinOf(P, Id), View.m_Zoom);
                    RowY += mesh_preview_system::s_PreviewSize + geo::PREVIEW_GAP;
                }
            }

            // An End marker draws no port rows at all - no glyph, no label, nothing to drag - its
            // pin is purely the title-bar anchor point PortAnchor already computes above.
            const auto PortsForRow = FlatPorts(pDesc);
            bool bHavePrevPort = false, bPrevPortLocal = false;
            if (!IsEndMarkerType(pDesc))
            for (std::size_t PortIdx = 0; PortIdx < PortsForRow.size(); ++PortIdx)
            {
                auto& P = PortsForRow[PortIdx];
                // At the ONE point this node's ports switch from external to local-scope: a visible
                // gap (not just a color/line cue - see LocalSectionGapTotal, which reserves the
                // matching space in NodeHeight), a background tint matching what a node physically
                // INSIDE this scope would get from DarkenForDepth (same visual language as the rest
                // of this file's depth-based dimming), and a small caption so the transition reads
                // without having to infer it from the const/& type annotations alone. The tint runs
                // all the way to the node's own bottom edge (through the End row) rather than
                // stopping at the local ports' own extent, so there's no hard edge partway down.
                if (bHavePrevPort && P.m_pDesc->m_bLocalScope && !bPrevPortLocal)
                {
                    const float LineY = RowY + geo::SECTION_GAP * 0.5f;
                    // Inset from the node's own left/right border by a hair, and stop a hair short of
                    // the bottom border, so the tint sits INSIDE the node's outline rather than
                    // painting over it - reaches the very bottom (through the End row) rather than
                    // stopping at the local ports' own extent, so there's no hard edge partway down.
                    const float BorderInset = ToScreenLen(1.5f);
                    const ImVec2 TintMin = { ToScreen({ pRow->m_X, LineY }).x + BorderInset, ToScreen({ pRow->m_X, LineY }).y };
                    const ImVec2 TintMax = { P1.x - BorderInset, P1.y - ToScreenLen(1.0f) };
                    pDraw->AddRectFilled(TintMin, TintMax, DarkenForDepth(theme::NodeBg, Depth + 1), 0.0f);
                    pDraw->AddLine(ToScreen({ pRow->m_X + 6.0f, LineY }), ToScreen({ pRow->m_X + pRow->m_W - 6.0f, LineY }), IM_COL32(100, 116, 139, 255), ToScreenLen(1.5f));
                    DrawText({ pRow->m_X + 8.0f, LineY + 2.0f }, IM_COL32(100, 116, 139, 255), "locals");
                    RowY += geo::SECTION_GAP;
                }
                bPrevPortLocal = P.m_pDesc->m_bLocalScope;
                bHavePrevPort = true;

                // An Any pin (Compare's A/B) shows and colors as whatever it's currently resolved to
                // (see ResolveNodeWildcardType) - "Any" itself only while nothing has locked it in yet.
                const char* pEffType = EffectiveTypeName(Id, pDesc, P.m_pDesc->m_pTypeName, Nodes, Links);
                const float RH = RowHeight(P, Id, Links, pEffType);
                const float CenterY = RowY + geo::ROW_H * 0.5f;
                // A Scope pin darkens with its own node's depth too, matching the box and the wire.
                ImU32 Col = IsScopeType(pEffType) ? DarkenForDepth(TypeColor(pEffType), Depth) : TypeColor(pEffType);
                const bool bConnected = PortSides.contains(PinOf(P, Id));
                ImU32 Fill = bConnected ? Col : theme::CanvasDark;

                // Fade the SMALLEST thing that's actually invalid: a candidate pin (opposite
                // direction from the drag, different node) that fails type or scope dims on its own,
                // rather than only ever fading the whole node - a node can mix eligible and
                // ineligible pins on the same box (a Function's external vs. local-mirrored ports;
                // ForEachLoop's Span vs. its own Element/Index), and the whole-node overlay below
                // only fires when NOTHING on the node qualifies.
                ImU32 NameCol = IM_COL32(203, 213, 225, 255);
                if (Drag.m_bActive && Id != Drag.m_FromNode && P.m_bIsOutput != Drag.m_bFromIsOutput && !PortAcceptsDrag(Id, P.m_Index))
                {
                    Col     = WithAlpha(Col, 0.35f);
                    Fill    = WithAlpha(Fill, 0.35f);
                    NameCol = WithAlpha(NameCol, 0.35f);
                }

                // "End" gets no name label at all - it's the one port name that's the same on every
                // owner type, adds no information ("[Scope]" already says what it is), and dropping
                // it lets every OTHER port's own name stand out more.
                if (std::strcmp(P.m_pDesc->m_pName, "End") != 0)
                {
                    // Screen-space, same reasoning as the title/category fix above.
                    const ImVec2 PinNameSizePx = ImGui::GetFont()->CalcTextSizeA(FontSize, FLT_MAX, 0.0f, P.m_pDesc->m_pName);
                    const ImVec2 RowCenterPx = ToScreen({ pRow->m_X + pRow->m_W * 0.5f, CenterY });
                    pDraw->AddText(nullptr, FontSize, ImVec2(RowCenterPx.x - PinNameSizePx.x * 0.5f, RowCenterPx.y - PinNameSizePx.y * 0.5f), NameCol, P.m_pDesc->m_pName);
                }

                // A pin used by links going both up and down the stack needs a glyph on BOTH sides -
                // one per side actually in use (SidesOf), not just whichever link happened to be
                // processed last into a single shared "the" side.
                for (char S : SidesOf(PinOf(P, Id), P.m_bIsOutput))
                {
                    const float CX = (S == 'L') ? pRow->m_X : pRow->m_X + pRow->m_W;

                    // Glyph: a triangle pointing INTO the node for an input, OUT toward the highway for
                    // an output - matches rslgraph-ui NodeView.tsx's Glyph: pointRight = (side=='R') == isOutput.
                    const bool bPointRight = (S == 'R') == P.m_bIsOutput;
                    const float R = geo::GLYPH * 0.5f;
                    const ImVec2 Tip = bPointRight ? ToScreen({ CX + R, CenterY }) : ToScreen({ CX - R, CenterY });
                    const ImVec2 B1  = bPointRight ? ToScreen({ CX - R, CenterY - R }) : ToScreen({ CX + R, CenterY - R });
                    const ImVec2 B2  = bPointRight ? ToScreen({ CX - R, CenterY + R }) : ToScreen({ CX + R, CenterY + R });
                    pDraw->AddTriangleFilled(Tip, B1, B2, Fill);
                    pDraw->AddTriangle(Tip, B1, B2, Col, ToScreenLen(1.5f));

                    // Screen-space anchoring throughout, not a world-space position built from a
                    // screen-space width and then re-transformed - measure the label at the EXACT
                    // font size AddText will use, anchor from the row edge AFTER ToScreen (not
                    // before), and do the whole left/right offset in already-zoomed pixels. Round-
                    // tripping a width through world space and back was the earlier (unreliable)
                    // approach; this is the one that can't drift as View.m_Zoom changes, since
                    // nothing here mixes the two coordinate spaces.
                    // Reads as an annotation, not a name - smaller than the row/title text and
                    // tucked in close to its own pin (PIN_TYPE_FONT_SCALE/PIN_TYPE_INSET, matched by
                    // NodeWidth's own PortColW reservation so the box stays exactly as wide as this
                    // smaller, closer label actually needs).
                    const std::string TypeLabel = std::string("[") + DisplayTypeText(pDesc, P, pEffType) + "]";
                    const float PinTypeFontSize = FontSize * geo::PIN_TYPE_FONT_SCALE;
                    const ImVec2 TypeSizePx = ImGui::GetFont()->CalcTextSizeA(PinTypeFontSize, FLT_MAX, 0.0f, TypeLabel.c_str());
                    const ImVec2 RowEdgePx = ToScreen({ S == 'L' ? pRow->m_X : pRow->m_X + pRow->m_W, CenterY });
                    const float InsetPx = ToScreenLen(geo::PIN_TYPE_INSET);
                    const ImVec2 TypePosPx = (S == 'L')
                        ? ImVec2(RowEdgePx.x + InsetPx, RowEdgePx.y - TypeSizePx.y * 0.5f)
                        : ImVec2(RowEdgePx.x - InsetPx - TypeSizePx.x, RowEdgePx.y - TypeSizePx.y * 0.5f);
                    pDraw->AddText(nullptr, PinTypeFontSize, TypePosPx, Col, TypeLabel.c_str());

                    // Drag-to-connect hit target - generous, bigger than the visible glyph (rslgraph-ui's
                    // own NodeView.tsx uses a 13px-radius invisible circle over each port; this is wider
                    // still since the whole point is that grabbing it should be easy, not precise).
                    ImGui::PushID((int)PinOf(P, Id));
                    ImGui::PushID(S);
                    ImGui::SetNextItemAllowOverlap();
                    const ImVec2 PinMin = ToScreen({ CX - geo::PORT_HIT_RADIUS, CenterY - geo::PORT_HIT_RADIUS });
                    const ImVec2 PinMax = ToScreen({ CX + geo::PORT_HIT_RADIUS, CenterY + geo::PORT_HIT_RADIUS });
                    ImGui::SetCursorScreenPos(PinMin);
                    ImGui::InvisibleButton("pin", ImVec2(PinMax.x - PinMin.x, PinMax.y - PinMin.y));
                    // Direct geometric hit-test against the mouse position, not ImGui's own hover/active-id
                    // bookkeeping: while dragging, the ORIGIN pin's own InvisibleButton is still "active"
                    // (mouse held since that press), and IsItemHovered() silently returns false for every
                    // OTHER item while some item is active - exactly the case for every port dragged over.
                    // This codebase already hit this same class of bug once for the spine's node-drag drop
                    // (see MarkerPositions/MoveNodesTo below) and settled on hit-testing the cursor directly
                    // instead of chasing the right AllowWhenBlockedByActiveItem/AllowOverlap flag combination.
                    const bool bPinHovered = ImGui::IsMouseHoveringRect(PinMin, PinMax);
                    if (!Drag.m_bActive && bPinHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        Drag = { true, Id, P.m_bIsOutput, P.m_Index, { CX, CenterY }, S };
                    ImGui::PopID();
                    ImGui::PopID();

                    // While actively dragging a connection, the hover ring only shows over a port that
                    // would actually accept the drop - opposite direction, a different node, and a matching
                    // type name (the exact same validity check the drop-resolution below commits with) - so
                    // a wrong-type or same-direction port simply never lights up. Outside of a drag, hovering
                    // any port still rings it, as a plain "you can start a connection here" affordance.
                    bool bShowRing = bPinHovered;
                    if (bPinHovered && Drag.m_bActive)
                    {
                        const bool bSamePort      = (Id == Drag.m_FromNode && P.m_bIsOutput == Drag.m_bFromIsOutput && P.m_Index == Drag.m_FromIndex);
                        const bool bOppositeDir   = (P.m_bIsOutput != Drag.m_bFromIsOutput);
                        const bool bDifferentNode = (Id != Drag.m_FromNode);
                        // PortAcceptsDrag checks type AND scope now - a scope-forbidden pin (e.g. a
                        // Function's local-mirrored input reached from outside its own body) used to
                        // still ring here on type alone, inviting a drop the commit logic would then
                        // silently refuse; folded into the one shared check so the ring and the
                        // eventual drop-resolution never disagree.
                        bShowRing = !bSamePort && bOppositeDir && bDifferentNode && PortAcceptsDrag(Id, P.m_Index);
                    }
                    if (bShowRing)
                    {
                        const float HR = geo::PORT_HIT_RADIUS * 1.4f;
                        pDraw->AddCircle(ToScreen({ CX, CenterY }), ToScreenLen(HR), IM_COL32(125, 211, 252, 255), 0, ToScreenLen(1.5f));
                    }
                }

                if (!IsMeshType(pEffType) && !IsScopeType(pEffType) && !IsNoPreviewType(pEffType) && !P.m_bIsOutput)
                {
                    void* pValue = P.m_bIsOutput
                        ? ((pNode->m_bHasRun && P.m_Index < (int)pNode->m_CachedOutputs.size()) ? pNode->m_CachedOutputs[P.m_Index] : nullptr)
                        : GetInputValue(Id, P.m_Index, Nodes, Links, LiteralValues, LiteralScratch);

                    // Float/Int/Short are this corpus's numeric scalar types (see IsNoPreviewType's
                    // own comment for why Bool is excluded here - it's filtered out already, above,
                    // regardless of whether it's a fixed Bool pin or a wildcard resolved to Bool).
                    // Checked against the EFFECTIVE type, not the raw declared one, so an Any pin
                    // (Compare/Math Expression's A/B) resolved to one of these gets the same inline
                    // literal an ordinarily-typed pin of that type would - this is what "the compare
                    // node still lets you enter a value based on the known type" actually means.
                    const bool bIsFloat = std::strcmp(pEffType, "Float") == 0;
                    const bool bIsInt   = std::strcmp(pEffType, "Int") == 0 || std::strcmp(pEffType, "Short") == 0;

                    // Gated on bConnected (an actual wire in Links), NOT on pValue - pValue only
                    // reflects a produced runtime value, which none of these no-op-Execute() node
                    // types ever populate, so checking it would leave the widget showing forever even
                    // after a wire is attached. bConnected is exactly "is a wire here right now",
                    // which is the only thing that should ever hide the inline constant.
                    if (!P.m_bIsOutput && !bConnected && (bIsFloat || bIsInt))
                    {
                        // Inline constant, Unity-style: an unconnected scalar input isn't "no
                        // value", it's "whatever's typed right here" - stored in LiteralValues,
                        // keyed by this exact pin, independent of any wire (a literal is simply
                        // ignored once a wire exists - see the pValue check just above).
                        const std::uint64_t PinId = PinOf(P, Id);
                        auto LitIt = LiteralValues.find(PinId);
                        const std::string CurrentText = LitIt != LiteralValues.end() ? LitIt->second : "0";

                        ImGui::PushID((int)PinId);
                        ImGui::SetNextItemAllowOverlap();
                        ImGui::SetCursorScreenPos(ToScreen({ pRow->m_X + 12.0f, RowY + geo::ROW_H }));
                        ImGui::SetNextItemWidth(ToScreenLen(pRow->m_W - 24.0f));

                        if (bIsInt)
                        {
                            int Val = 0; try { Val = std::stoi(CurrentText); } catch (...) {}
                            if (ImGui::InputInt("##lit", &Val, 0, 0))
                                commands::Run(System, commands::MakeSetLiteralValue(PinId, std::to_string(Val)));
                        }
                        else
                        {
                            float Val = 0.0f; try { Val = std::stof(CurrentText); } catch (...) {}
                            if (ImGui::InputFloat("##lit", &Val, 0.0f, 0.0f, "%.3f"))
                                commands::Run(System, commands::MakeSetLiteralValue(PinId, std::format("{}", Val)));
                        }
                        ImGui::PopID();
                    }
                    else
                    {
                        const char* pPreview = PortTypeToPreview(pEffType, pValue);
                        if (pPreview[0] != '\0')
                        {
                            // Screen-space, same reasoning as the title/category fix above.
                            const ImVec2 ValSizePx = ImGui::GetFont()->CalcTextSizeA(FontSize, FLT_MAX, 0.0f, pPreview);
                            const ImVec2 AnchorPx = ToScreen({ pRow->m_X + pRow->m_W * 0.5f, RowY + geo::ROW_H });
                            pDraw->AddText(nullptr, FontSize, ImVec2(AnchorPx.x - ValSizePx.x * 0.5f, AnchorPx.y), IM_COL32(148, 163, 184, 255), pPreview);
                        }
                    }
                }

                RowY += RH;
            }

            // Inline enum-property widgets, directly in the node body - e.g. Compare's own Operator
            // choice, not only reachable through the side properties panel. Reuses the exact same
            // xproperty reflection every other property path already uses; a change goes through the
            // same undo-safe SetProperties command the side panel issues (see DrawNodePropertiesPanel).
            if (pNode && pNode->m_pNode && HasSerializableProperties(pNode->m_pNode))
            {
                xnode_os_node* pRealNode = pNode->m_pNode;
                const xproperty::type::object* pObj = pRealNode->getProperties();
                for (auto& M : pObj->m_Members)
                {
                    auto* pVar = std::get_if<xproperty::type::members::var>(&M.m_Variant);
                    if (!pVar || !pVar->m_AtomicType.m_IsEnum) continue;

                    const int CurrentVal = ReadEnumAsInt(M, pRealNode);
                    const char* pCurrentName = "?";
                    for (auto& Item : pVar->m_AtomicType.m_RegisteredEnumSpan)
                        if ((int)Item.m_Value == CurrentVal) { pCurrentName = Item.m_pName; break; }

                    // Compare's own Operator choice narrows to Equal/Not Equal whenever its A/B pins
                    // (see ResolveNodeWildcardType) haven't resolved to an orderable atomic type -
                    // Float is the only one today (E27_NodeOS_Editor.cpp's IsNoPreviewType comment) -
                    // a struct-like comparison has no meaningful </<=/>/>=. This is UI-only filtering
                    // of the dropdown's own choices, not a change to what's stored. Matched by the
                    // enum's own underlying VALUE (2 = EQUAL, 3 = NOT_EQUAL in compare_node.cpp's own
                    // declaration order), not by display-name text - the host already has to know this
                    // plugin's specific 0..5 ordering elsewhere (EmitOrdinaryNode's own "Compare" case
                    // maps the same numbers to operator tokens), and a value match survives a display-
                    // text rename for free, unlike the name-based check this replaced.
                    const bool bIsCompareOperator = pRealNode->m_pFactory->getName() == "Compare" && std::strcmp(M.m_pName, "Operator") == 0;
                    const char* pResolvedCompareType = bIsCompareOperator ? ResolveNodeWildcardType(Id, pDesc, Nodes, Links) : nullptr;
                    const bool bOrderable = !bIsCompareOperator || (pResolvedCompareType && std::strcmp(pResolvedCompareType, "Float") == 0);

                    ImGui::PushID((int)Id);
                    ImGui::PushID(M.m_pName);
                    ImGui::SetNextItemAllowOverlap();
                    ImGui::SetCursorScreenPos(ToScreen({ pRow->m_X + 10.0f, RowY }));
                    ImGui::SetNextItemWidth(ToScreenLen(pRow->m_W - 20.0f));
                    if (ImGui::BeginCombo("##enum", pCurrentName))
                    {
                        for (auto& Item : pVar->m_AtomicType.m_RegisteredEnumSpan)
                        {
                            if (!bOrderable && (int)Item.m_Value != 2 && (int)Item.m_Value != 3)
                                continue;
                            const bool bIsSel = ((int)Item.m_Value == CurrentVal);
                            if (ImGui::Selectable(Item.m_pName, bIsSel))
                            {
                                const std::string Before = SerializePropertiesToString(pRealNode);
                                WriteEnumFromInt(M, pRealNode, (int)Item.m_Value);
                                const std::string After = SerializePropertiesToString(pRealNode);
                                if (After != Before)
                                    commands::Run(System, commands::MakeSetProperties(Id, Before, After));
                            }
                            if (bIsSel) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                    ImGui::PopID();
                    RowY += geo::ROW_H + 4.0f;
                }

                // Constant's own numeric Value, directly in the node body too - not just the side
                // panel - same look as Compare/Math Expression's inline literal on an unconnected
                // Any pin. A name-based special case, same as Compare's operator-filtering above:
                // Value is stored as plain text regardless of Type (see constant_node.cpp), so
                // picking the right WIDGET for it - a numeric spinner vs. a checkbox - needs to know
                // this node is specifically a Constant and read its own sibling Type member; a fully
                // generic "any string property" inline editor wouldn't know to do that.
                if (pRealNode->m_pFactory->getName() == "Constant")
                {
                    const xproperty::type::members* pTypeMember  = nullptr;
                    const xproperty::type::members* pValueMember = nullptr;
                    for (auto& M : pObj->m_Members)
                    {
                        if      (std::strcmp(M.m_pName, "Type")  == 0) pTypeMember  = &M;
                        else if (std::strcmp(M.m_pName, "Value") == 0) pValueMember = &M;
                    }
                    if (pTypeMember && pValueMember)
                    {
                        const char* pTypeName = "Float";
                        if (auto* pTypeVar = std::get_if<xproperty::type::members::var>(&pTypeMember->m_Variant))
                        {
                            const int TypeVal = ReadEnumAsInt(*pTypeMember, pRealNode);
                            for (auto& Item : pTypeVar->m_AtomicType.m_RegisteredEnumSpan)
                                if ((int)Item.m_Value == TypeVal) { pTypeName = Item.m_pName; break; }
                        }

                        xproperty::any ValueOut; xproperty::settings::context ReadCtx;
                        std::string CurrentText;
                        if (pValueMember->TryRead(pRealNode, ValueOut, ReadCtx) && ValueOut.is<std::string>())
                            CurrentText = ValueOut.get<std::string>();

                        auto CommitValue = [&](const std::string& NewText)
                        {
                            const std::string Before = SerializePropertiesToString(pRealNode);
                            xproperty::any In{ NewText }; xproperty::settings::context WriteCtx;
                            (void)pValueMember->TryWrite(pRealNode, In, WriteCtx);
                            const std::string After = SerializePropertiesToString(pRealNode);
                            if (After != Before)
                                commands::Run(System, commands::MakeSetProperties(Id, Before, After));
                        };

                        ImGui::PushID((int)Id);
                        ImGui::PushID("ConstValue");
                        ImGui::SetNextItemAllowOverlap();
                        ImGui::SetCursorScreenPos(ToScreen({ pRow->m_X + 10.0f, RowY }));
                        ImGui::SetNextItemWidth(ToScreenLen(pRow->m_W - 20.0f));
                        if (std::strcmp(pTypeName, "Bool") == 0)
                        {
                            bool Val = (CurrentText == "1");
                            if (ImGui::Checkbox("##constval", &Val))
                                CommitValue(Val ? "1" : "0");
                        }
                        else if (std::strcmp(pTypeName, "Int") == 0 || std::strcmp(pTypeName, "Short") == 0)
                        {
                            int Val = 0; try { Val = std::stoi(CurrentText); } catch (...) {}
                            if (ImGui::InputInt("##constval", &Val, 0, 0))
                                CommitValue(std::to_string(Val));
                        }
                        else
                        {
                            float Val = 0.0f; try { Val = std::stof(CurrentText); } catch (...) {}
                            if (ImGui::InputFloat("##constval", &Val, 0.0f, 0.0f, "%.3f"))
                                CommitValue(std::format("{}", Val));
                        }
                        ImGui::PopID();
                        ImGui::PopID();
                        RowY += geo::ROW_H + 4.0f;
                    }
                }
            }

            if (!pNode->m_LastError.empty())
            {
                ImGui::SetNextItemAllowOverlap();
                ImGui::SetCursorScreenPos(ToScreen({ pRow->m_X + 8.0f, pRow->m_Y + pRow->m_H - 16.0f }));
                ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "%s", pNode->m_LastError.c_str());
            }
            // Fades the WHOLE node - drawn last, on top of everything else just rendered for it
            // (header, ports, inline widgets, error text) - when it has no port that could legally
            // accept the connection currently being dragged (see NodeAcceptsDrag). A dark, mostly-
            // opaque overlay matching the canvas background rather than touching every individual
            // color computed above - far less invasive than threading an alpha factor through this
            // whole block, and just as effective visually.
            if (Drag.m_bActive && !NodeAcceptsDrag(Id))
                pDraw->AddRectFilled(P0, P1, WithAlpha(theme::CanvasDark, 195.0f / 255.0f), 0.0f);

            ImGui::PopID();
        }

        // ---- the spine control: a "+" marker (with its box, and now two circles) in every gap of
        // every spine (before its first node, between each consecutive pair, after its last - or just
        // one, for a currently-empty spine) - left-click one to insert a node at that exact position;
        // drag a circle to grow a whole new spine off it. This is the "skeleton" the user asked to make
        // the stacking order itself directly editable, now extended to grow sideways too.
        struct marker_pos { std::uint64_t m_SpineId; int m_GapIndex; float m_X, m_Y; };
        std::vector<marker_pos> MarkerPositions;
        for (auto& Sp : Spines)
        {
            auto& SL = SpineLayout[Sp.m_Id];
            const float MarkerX = ColumnX[Sp.m_ColumnId];

            auto DrawInsertMarker = [&](int GapIndex)
            {
                const float Y = SpineAbsY[Sp.m_Id] + GapRelY(SL, GapIndex);
                MarkerPositions.push_back({ Sp.m_Id, GapIndex, MarkerX, Y });
                const bool   bSelected = (Selection.m_SelectedGapSpineId == Sp.m_Id && Selection.m_SelectedGapIndex == GapIndex);
                const ImVec2 Center = ToScreen({ MarkerX, Y });
                const float  HalfW = ToScreenLen(28.0f), HalfH = ToScreenLen(13.0f);
                const ImVec2 BoxMin{ Center.x - HalfW, Center.y - HalfH }, BoxMax{ Center.x + HalfW, Center.y + HalfH };
                const float  PlusR = ToScreenLen(10.0f);
                const ImVec2 PlusMin{ Center.x - PlusR, Center.y - PlusR }, PlusMax{ Center.x + PlusR, Center.y + PlusR };
                const bool bPlusHovered = ImGui::IsMouseHoveringRect(PlusMin, PlusMax);

                // Two independent things sharing this slot: a selectable box (clicking anywhere in it,
                // outside the + itself, selects it the same way clicking a node does - a future Ctrl+V
                // paste will target the current selection) and the + button (always opens the insert
                // popup, regardless of selection state - pressing it never itself selects the box).
                pDraw->AddRectFilled(BoxMin, BoxMax, theme::NodeBg, 0.0f);
                pDraw->AddRect(BoxMin, BoxMax, bSelected ? theme::Selected : theme::NodeBorder
                              , 0.0f, 0, ToScreenLen(bSelected ? 2.0f : 1.2f));

                pDraw->AddCircleFilled(Center, PlusR, bPlusHovered ? IM_COL32(56, 130, 246, 255) : IM_COL32(30, 41, 59, 255));
                pDraw->AddCircle(Center, PlusR, IM_COL32(100, 116, 139, 255), 0, ToScreenLen(1.2f));
                const float Arm = PlusR * 0.45f;
                pDraw->AddLine({ Center.x - Arm, Center.y }, { Center.x + Arm, Center.y }, IM_COL32(226, 232, 240, 255), ToScreenLen(1.5f));
                pDraw->AddLine({ Center.x, Center.y - Arm }, { Center.x, Center.y + Arm }, IM_COL32(226, 232, 240, 255), ToScreenLen(1.5f));

                ImGui::PushID("spine_insert");
                ImGui::PushID((int)Sp.m_Id); ImGui::PushID((int)(Sp.m_Id >> 32));
                ImGui::PushID(GapIndex);

                // Box hit region, submitted first (bigger, covers the + button's area too).
                ImGui::SetNextItemAllowOverlap();
                ImGui::SetCursorScreenPos(BoxMin);
                ImGui::InvisibleButton("box", ImVec2(HalfW * 2, HalfH * 2));
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                {
                    // Addressed relative to the node currently on one side of this gap, same reasoning
                    // as CreateNode's own -After/-Before (a raw, shifting GapIndex isn't something
                    // worth serializing into a durable command) - or, for a currently-empty spine, the
                    // spine itself (-MarkerSpine, legal only in that case).
                    const std::string Cmd = SL.m_Order.empty() ? commands::MakeSelectMarkerSpine(Sp.m_Id)
                        : (GapIndex < (int)SL.m_Order.size() ? commands::MakeSelectMarkerBefore(SL.m_Order[GapIndex]) : commands::MakeSelectMarkerAfter(SL.m_Order.back()));
                    commands::Run(System, Cmd);
                }

                // + button, submitted after with AllowOverlap - wins the click over the box beneath it
                // for its own (smaller) area, so clicking the + specifically never also selects the box.
                ImGui::SetNextItemAllowOverlap();
                ImGui::SetCursorScreenPos(PlusMin);
                ImGui::InvisibleButton("plus", ImVec2(PlusR * 2, PlusR * 2));
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                    ImGui::OpenPopup("NodeOS_SpineInsertPopup");

                if (ImGui::BeginPopup("NodeOS_SpineInsertPopup"))
                {
                    if (Sources.empty())
                        ImGui::TextDisabled("No plugin sources found under Plugins/.");
                    for (auto& Src : Sources)
                        if (ImGui::MenuItem(Src.m_DisplayName.c_str()))
                            InsertNodeAt(Sp.m_Id, GapIndex, Src);

                    // Only a real split when there's something on both sides of this exact gap - splitting
                    // at the very top (nothing above) or the very bottom (nothing below) wouldn't actually
                    // separate anything. The new spine lands in the SAME column, anchored at this gap's own
                    // current absolute Y, so its nodes stay exactly where they visually already are.
                    if (GapIndex > 0 && GapIndex < (int)SL.m_Order.size())
                    {
                        ImGui::Separator();
                        if (ImGui::MenuItem("Split spine here"))
                        {
                            const auto NewSpineId = xresource::guid_generator::Instance64();
                            const std::vector<std::uint64_t> Trailing(SL.m_Order.begin() + GapIndex, SL.m_Order.end());
                            commands::Run(System, commands::MakeCreateSpineExistingColumn(NewSpineId, Y, Sp.m_ColumnId));
                            commands::Run(System, commands::MakeMoveNodesToSpineIn(Trailing, NewSpineId));
                        }
                    }
                    ImGui::EndPopup();
                }

                // The two spine-control circles, positioned OUTSIDE the box's own edges so they never
                // overlap its click area (the same overlapping-InvisibleButton lesson this file already
                // learned once). Always present, even on a currently-empty spine's own lone placeholder -
                // that's the only way to grab and drag a still-empty spine anywhere.
                {
                    const float CircleR   = ToScreenLen(geo::SPINE_CIRCLE_R);
                    const float CircleGap = ToScreenLen(geo::SPINE_CIRCLE_GAP);
                    const ImVec2 CircleCenters[2] = { { BoxMin.x - CircleGap - CircleR, Center.y }, { BoxMax.x + CircleGap + CircleR, Center.y } };
                    for (int Side = 0; Side < 2; ++Side)
                    {
                        ImGui::PushID(Side);
                        ImGui::SetNextItemAllowOverlap();
                        ImGui::SetCursorScreenPos({ CircleCenters[Side].x - CircleR, CircleCenters[Side].y - CircleR });
                        ImGui::InvisibleButton("circle", ImVec2(CircleR * 2, CircleR * 2));
                        const bool bHovered = ImGui::IsItemHovered();
                        if (ImGui::IsItemActivated())
                        {
                            SpineDrag.m_bActive = false; // only flips true past the drag threshold below
                            SpineDrag.m_SpineId = Sp.m_Id;
                            SpineDrag.m_GrabY   = Y;
                        }
                        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f))
                            SpineDrag.m_bActive = true;
                        pDraw->AddCircleFilled(CircleCenters[Side], CircleR, bHovered ? IM_COL32(94, 234, 212, 255) : theme::NodeBorder);
                        pDraw->AddCircle(CircleCenters[Side], CircleR, IM_COL32(148, 163, 184, 255), 0, ToScreenLen(1.2f));
                        ImGui::PopID();
                    }
                }

                ImGui::PopID();
                ImGui::PopID(); ImGui::PopID();
                ImGui::PopID();
            };

            for (int GapIndex = 0; GapIndex <= (int)SL.m_Order.size(); ++GapIndex)
                DrawInsertMarker(GapIndex);
        }
        ImGui::SetWindowFontScale(1.0f); // matches the SetWindowFontScale(View.m_Zoom) set before this loop began

        // Resolve a node-drag drop by direct distance to MouseLocal, same pattern as the pin-to-pin
        // drag-to-connect resolution below - NOT the marker's own ImGui hover state. A marker sitting
        // under an ACTIVE (held-down) different widget (the dragged node's own body) is exactly the
        // overlapping-item scenario this codebase has already been burned by once
        // (xgpu_imgui_overlapping_invisible_buttons); hit-testing the cursor position directly sidesteps
        // it entirely instead of relying on getting every AllowOverlap/ActiveId interaction exactly right.
        if (NodeDrag.m_bActive && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            std::uint64_t BestSpineId = 0; int BestGap = -1; float Best = 40.0f;
            for (auto& M : MarkerPositions)
            {
                const float D = std::hypot(M.m_X - MouseLocal.x, M.m_Y - MouseLocal.y);
                if (D < Best) { Best = D; BestSpineId = M.m_SpineId; BestGap = M.m_GapIndex; }
            }
            // MoveNodesTo silently cancels on its own if the moving set doesn't already live entirely
            // in BestSpineId - cross-spine drag-reorder isn't supported yet.
            if (BestGap >= 0) MoveNodesTo(BestSpineId, NodeDrag.m_MovingIds, BestGap);
            NodeDrag.m_bActive = false;
        }

        // A dragged node that isn't dropped on a marker just cancels - it never had a floating ghost
        // position to snap back from. A thin line from each moving node's own slot to the cursor is the
        // only in-flight feedback (drawn here, once the markers above already had first chance to
        // consume the drop this frame).
        if (NodeDrag.m_bActive)
        {
            for (auto MovingId : NodeDrag.m_MovingIds)
            {
                auto* pRow = FindRow(MovingId);
                if (!pRow) continue;
                pDraw->AddLine(ToScreen({ pRow->m_X + pRow->m_W * 0.5f, pRow->m_Y + pRow->m_H * 0.5f }), ImGui::GetIO().MousePos
                              , IM_COL32(56, 189, 248, 180), ToScreenLen(1.5f));
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                NodeDrag.m_bActive = false;
        }

        // ---- spine-control drag: dropping on empty space within the dragged spine's own column
        // relocates it there (Step 1, unchanged); dropping on one of the "+" targets shown for every
        // OTHER existing column, or in the gaps between/around columns, attaches a new empty spine to
        // that column or splices in a brand-new one (Step 2). Every target is visible for the whole
        // drag, not just once some threshold is crossed - "whenever we drag the circle."
        if (SpineDrag.m_bActive)
        {
            const std::uint64_t OwnColumnId = ColumnOfSpine[SpineDrag.m_SpineId];
            const ImVec2 GrabWorldPos{ ColumnX[OwnColumnId], SpineDrag.m_GrabY };

            pDraw->AddLine(ToScreen(GrabWorldPos), ImGui::GetIO().MousePos, IM_COL32(94, 234, 212, 220), ToScreenLen(2.0f));

            // Every column, left to right, walking outward from the dragged spine's own.
            std::vector<column*> Ordered;
            {
                auto* pLeftmost = FindColumn(OwnColumnId);
                while (pLeftmost && pLeftmost->m_LeftId) pLeftmost = FindColumn(pLeftmost->m_LeftId);
                for (auto* p = pLeftmost; p; p = FindColumn(p->m_RightId)) Ordered.push_back(p);
            }

            // An existing-column "+" sits at that column's own center X, MouseLocal.y for its own Y -
            // exactly where every one of that column's own spine-control markers already lives too,
            // since they share the same X. When the mouse is close enough to one of those markers that
            // dropping there should merge into it instead (see the merge resolution below), the "+"
            // itself steps out of the way for the frame rather than winning the pick purely on X and
            // blocking the more specific target underneath it.
            auto NearOtherMarker = [&](std::uint64_t ColId)
            {
                for (auto& M : MarkerPositions)
                    if (M.m_SpineId != SpineDrag.m_SpineId && ColumnOfSpine[M.m_SpineId] == ColId && std::abs(M.m_Y - MouseLocal.y) < 40.0f)
                        return true;
                return false;
            };

            // One "+" per EXISTING column, including the dragged spine's own (drop -> attach a new,
            // empty spine there - the own-column one lands right in the dragged spine's own column
            // without disturbing it, same as any other column's), plus one in every gap between/around
            // columns (drop -> splice in a brand-new column).
            struct drop_target { std::uint64_t m_ColumnId; bool m_bNewColumn; bool m_bBetween; std::uint64_t m_NeighborColumnId; char m_Side; float m_X; };
            std::vector<drop_target> Targets;
            for (std::size_t i = 0; i < Ordered.size(); ++i)
            {
                auto* pCol = Ordered[i];
                if (!NearOtherMarker(pCol->m_Id))
                    Targets.push_back({ pCol->m_Id, false, false, 0, 'R', ColumnX[pCol->m_Id] });

                if (i == 0)
                {
                    const float GhostX = ColumnX[pCol->m_Id] - (Extent(pCol->m_Id, 'L') + geo::COLUMN_MARGIN + HighwayBaseOf(0));
                    Targets.push_back({ 0, true, false, pCol->m_Id, 'L', GhostX });
                }
                if (i + 1 < Ordered.size())
                {
                    // Midpoint of the actual GAP between the two columns' own facing highway edges -
                    // NOT the midpoint of their centers, which drifts toward whichever column is
                    // narrower whenever the two have different highway extents.
                    auto* pNext = Ordered[i + 1];
                    const float MidX = (ColumnX[pCol->m_Id] + Extent(pCol->m_Id, 'R') + ColumnX[pNext->m_Id] - Extent(pNext->m_Id, 'L')) * 0.5f;
                    Targets.push_back({ 0, true, true, pCol->m_Id, 'R', MidX });
                }
                else
                {
                    const float GhostX = ColumnX[pCol->m_Id] + (Extent(pCol->m_Id, 'R') + geo::COLUMN_MARGIN + HighwayBaseOf(0));
                    Targets.push_back({ 0, true, false, pCol->m_Id, 'R', GhostX });
                }
            }

            // The "+" targets follow the mouse's own Y continuously (not the fixed grab point) - the
            // user aims each one exactly where the new/attached spine should land. Drawn bigger than
            // the spine-control marker's own "+" (radius 10) since there can be many of these at once,
            // scattered across the whole canvas - they need to read clearly at a glance while dragging.
            // The BETWEEN-column ones are the exception: kept smaller than that so there's still room
            // in a narrow gap to drop past the "+" and land the spine directly in one of the two
            // flanking columns instead. PickRadiusFor is the ONE source of truth for "is the mouse over
            // this target" - shared by the highlight below and the actual drop resolution on release, so
            // a "+" never lights up as armed for a wider area than what will really register the drop.
            auto PickRadiusFor = [&](const drop_target& T) { return ToScreenLen(T.m_bBetween ? 9.0f : 20.0f); };
            for (auto& T : Targets)
            {
                const ImVec2 TCenter = ToScreen({ T.m_X, MouseLocal.y });
                const float  TR = ToScreenLen(T.m_bBetween ? 9.0f : 16.0f);
                const bool   bHere = std::abs(ImGui::GetIO().MousePos.x - TCenter.x) < PickRadiusFor(T);
                pDraw->AddCircleFilled(TCenter, TR, bHere ? IM_COL32(56, 130, 246, 255) : (T.m_bNewColumn ? IM_COL32(30, 41, 59, 180) : IM_COL32(30, 41, 59, 255)));
                pDraw->AddCircle(TCenter, TR, IM_COL32(100, 116, 139, 255), 0, ToScreenLen(1.2f));
                const float TArm = TR * 0.45f;
                pDraw->AddLine({ TCenter.x - TArm, TCenter.y }, { TCenter.x + TArm, TCenter.y }, IM_COL32(226, 232, 240, 220), ToScreenLen(1.5f));
                pDraw->AddLine({ TCenter.x, TCenter.y - TArm }, { TCenter.x, TCenter.y + TArm }, IM_COL32(226, 232, 240, 220), ToScreenLen(1.5f));
            }

            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                drop_target* pBest = nullptr; float BestD = 0.0f;
                for (auto& T : Targets)
                {
                    const float PickR = PickRadiusFor(T);
                    const float D = std::abs(ImGui::GetIO().MousePos.x - ToScreen({ T.m_X, MouseLocal.y }).x);
                    if (D < PickR && (!pBest || D < BestD)) { BestD = D; pBest = &T; }
                }

                if (pBest)
                {
                    const auto NewSpineId = xresource::guid_generator::Instance64();
                    const std::string Cmd = pBest->m_bNewColumn
                        ? commands::MakeCreateSpineNewColumn(NewSpineId, MouseLocal.y, pBest->m_NeighborColumnId, pBest->m_Side, xresource::guid_generator::Instance64())
                        : commands::MakeCreateSpineExistingColumn(NewSpineId, MouseLocal.y, pBest->m_ColumnId);
                    commands::Run(System, Cmd);
                }
                else
                {
                    // Dropped on a DIFFERENT spine's own node-gap marker (not the floating column "+"
                    // above, a stationary one belonging to an already-populated spine) - merge the whole
                    // dragged spine's nodes in at that exact gap, same as dragging a set of nodes onto
                    // another spine already does, and the old nodes at/after that gap shift down to make
                    // room (MoveNodesToSpine's own renumbering). The dragged spine ends up empty either
                    // way, so remove it the same way any empty spine goes - but only if the move actually
                    // happened; MoveNodesTo silently no-ops on a blocked append, and an empty spine has
                    // nothing to merge in the first place.
                    std::uint64_t MergeTargetSpineId = 0; int MergeTargetGap = -1; float MergeBestD = 40.0f;
                    for (auto& M : MarkerPositions)
                    {
                        if (M.m_SpineId == SpineDrag.m_SpineId) continue;
                        const float D = std::hypot(M.m_X - MouseLocal.x, M.m_Y - MouseLocal.y);
                        if (D < MergeBestD) { MergeBestD = D; MergeTargetSpineId = M.m_SpineId; MergeTargetGap = M.m_GapIndex; }
                    }

                    std::vector<std::uint64_t> MergingIds;
                    if (MergeTargetGap >= 0)
                        for (auto& N : Nodes) if (N.m_SpineId == SpineDrag.m_SpineId) MergingIds.push_back(N.m_Id);

                    if (!MergingIds.empty())
                    {
                        std::sort(MergingIds.begin(), MergingIds.end(), [&](std::uint64_t A, std::uint64_t B)
                                 { auto* pA = FindNode(A); auto* pB = FindNode(B); return (pA ? pA->m_Order : 0) < (pB ? pB->m_Order : 0); });
                        MoveNodesTo(MergeTargetSpineId, MergingIds, MergeTargetGap);
                        bool bStillHasNodes = false;
                        for (auto& N : Nodes) if (N.m_SpineId == SpineDrag.m_SpineId) { bStillHasNodes = true; break; }
                        if (!bStillHasNodes)
                            commands::Run(System, commands::MakeDeleteSpine(SpineDrag.m_SpineId));
                    }
                    else
                    {
                        // Not on a "+" or another spine's marker either - dropped somewhere in a column's
                        // own space instead (own column or a different one), so move the dragged spine
                        // there, preserving the exact offset between the grabbed marker and the spine's
                        // own top so the drag feels WYSIWYG regardless of which one of its gaps was
                        // grabbed. Dropped in genuinely empty space (past every column and every "+" too)
                        // - splice a brand-new column in at the nearest edge and move it there instead.
                        auto SpineIt = std::find_if(Spines.begin(), Spines.end(), [&](auto& S) { return S.m_Id == SpineDrag.m_SpineId; });
                        if (SpineIt != Spines.end())
                        {
                            const float GrabOffsetFromTop = SpineDrag.m_GrabY - SpineAbsY[SpineDrag.m_SpineId];
                            const float NewTopY = MouseLocal.y - GrabOffsetFromTop;

                            column* pHit = nullptr;
                            for (auto* pCol : Ordered)
                                if (MouseLocal.x >= ColumnX[pCol->m_Id] - Extent(pCol->m_Id, 'L')
                                 && MouseLocal.x <= ColumnX[pCol->m_Id] + Extent(pCol->m_Id, 'R')) { pHit = pCol; break; }

                            if (pHit)
                            {
                                commands::Run(System, commands::MakeSetSpinePosition(SpineDrag.m_SpineId, NewTopY, pHit->m_Id));
                            }
                            else if (MouseLocal.x < ColumnX[Ordered.front()->m_Id] - Extent(Ordered.front()->m_Id, 'L'))
                            {
                                const auto NewColumnId = xresource::guid_generator::Instance64();
                                commands::Run(System, commands::MakeSetSpinePositionNewColumn(SpineDrag.m_SpineId, NewTopY, Ordered.front()->m_Id, 'L', NewColumnId));
                            }
                            else if (MouseLocal.x > ColumnX[Ordered.back()->m_Id] + Extent(Ordered.back()->m_Id, 'R'))
                            {
                                const auto NewColumnId = xresource::guid_generator::Instance64();
                                commands::Run(System, commands::MakeSetSpinePositionNewColumn(SpineDrag.m_SpineId, NewTopY, Ordered.back()->m_Id, 'R', NewColumnId));
                            }
                            else
                            {
                                for (std::size_t i = 0; i + 1 < Ordered.size(); ++i)
                                {
                                    auto* pA = Ordered[i]; auto* pB = Ordered[i + 1];
                                    const float AEdge = ColumnX[pA->m_Id] + Extent(pA->m_Id, 'R');
                                    const float BEdge = ColumnX[pB->m_Id] - Extent(pB->m_Id, 'L');
                                    if (MouseLocal.x >= AEdge && MouseLocal.x <= BEdge)
                                    {
                                        const bool bCloserToA = (MouseLocal.x - AEdge) <= (BEdge - MouseLocal.x);
                                        auto* pNeighbor = bCloserToA ? pA : pB;
                                        const char Side = bCloserToA ? 'R' : 'L';
                                        const auto NewColumnId = xresource::guid_generator::Instance64();
                                        commands::Run(System, commands::MakeSetSpinePositionNewColumn(SpineDrag.m_SpineId, NewTopY, pNeighbor->m_Id, Side, NewColumnId));
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                SpineDrag.m_bActive = false;
            }
        }

        // --- resolve a drag-to-connect drop: hit-test every port, validate direction+type, single-
        // connection-per-input eviction (Canvas.tsx's onUp + connect.ts's evictionCandidate) ---
        if (Drag.m_bActive && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            std::uint64_t TargetNode = 0; bool bTargetIsOutput = false; int TargetIndex = 0;
            float Best = geo::PORT_HIT_RADIUS;
            for (auto Id : Order)
            {
                auto* pDesc = DescOf(FindNode(Id));
                if (!pDesc) continue;
                for (auto& P : FlatPorts(pDesc))
                {
                    // A pin rendered on both sides has two valid drop anchors now - check whichever one
                    // the mouse is actually closest to.
                    for (char S : SidesOf(PinOf(P, Id), P.m_bIsOutput))
                    {
                        const ImVec2 A = PortAnchor(Id, P, S);
                        const float D = std::hypot(A.x - MouseLocal.x, A.y - MouseLocal.y);
                        if (D <= Best) { Best = D; TargetNode = Id; bTargetIsOutput = P.m_bIsOutput; TargetIndex = P.m_Index; }
                    }
                }
            }
            if (TargetNode && !(TargetNode == Drag.m_FromNode && bTargetIsOutput == Drag.m_bFromIsOutput && TargetIndex == Drag.m_FromIndex) && bTargetIsOutput != Drag.m_bFromIsOutput)
            {
                const std::uint64_t OutNode = Drag.m_bFromIsOutput ? Drag.m_FromNode : TargetNode;
                const int            OutIdx  = Drag.m_bFromIsOutput ? Drag.m_FromIndex : TargetIndex;
                const std::uint64_t InNode  = Drag.m_bFromIsOutput ? TargetNode : Drag.m_FromNode;
                const int            InIdx   = Drag.m_bFromIsOutput ? TargetIndex : Drag.m_FromIndex;
                auto* pOutDesc = DescOf(FindNode(OutNode)); auto* pInDesc = DescOf(FindNode(InNode));
                const auto OutOutputs = pOutDesc ? pOutDesc->getOutputs() : std::span<const xnode_os_port_desc>{};
                const auto InInputs   = pInDesc   ? pInDesc->getInputs()   : std::span<const xnode_os_port_desc>{};
                const bool bBoundsOk = pOutDesc && pInDesc && OutNode != InNode && OutIdx < (int)OutOutputs.size() && InIdx < (int)InInputs.size();
                // A still-open wildcard on either end (Compare/Math Expression's Any pins, ForEachLoop's
                // Span<Any> - see IsAnyKindOfWildcard) accepts whatever the other end is - the
                // connection is what resolves it - rather than requiring an exact strcmp match the way
                // every ordinary, concretely-typed pin still does.
                const bool bTypesCompatible = bBoundsOk && [&]
                {
                    // Copied into a real std::string IMMEDIATELY - see the identical hazard/fix note
                    // on the drag-preview ring check above (ResolveNodeWildcardType's container-
                    // unwrap path returns a pointer into a shared thread_local buffer the second call
                    // below could otherwise silently overwrite before this comparison runs).
                    const std::string OutEff = EffectiveTypeName(OutNode, pOutDesc, OutOutputs[OutIdx].m_pTypeName, Nodes, Links);
                    const char* pInEff = EffectiveTypeName(InNode, pInDesc, InInputs[InIdx].m_pTypeName, Nodes, Links);
                    return IsAnyKindOfWildcard(OutEff.c_str()) || IsAnyKindOfWildcard(pInEff) || OutEff == pInEff;
                }();
                if (bBoundsOk && bTypesCompatible)
                {
                    // Eviction of any prior link into the same target input happens inside Connect's
                    // own Redo() now (and its Undo() restores whatever got evicted) - see connect_cmd.
                    commands::Run(System, commands::MakeConnect(xresource::guid_generator::Instance64(), OutNode, OutIdx, InNode, InIdx));
                }
            }
            Drag.m_bActive = false;
        }

        if (bBackgroundClicked)
        {
            std::uint64_t HitLink = 0; float Best = geo::LINK_HIT_DIST;
            for (auto& Link : Links)
            {
                auto* pSrcDesc = DescOf(FindNode(Link.m_SourceNode)); auto* pDstDesc = DescOf(FindNode(Link.m_TargetNode));
                if (!pSrcDesc || !pDstDesc) continue;
                const auto SrcOutputs = pSrcDesc->getOutputs(); const auto DstInputs = pDstDesc->getInputs();
                const port_ref OutP{ true, Link.m_SourceOutput, &SrcOutputs[Link.m_SourceOutput] };
                const port_ref InP { false, Link.m_TargetInput,  &DstInputs[Link.m_TargetInput] };
                char SourceSide = 'R', TargetSide = 'R', RailSide = 'R';
                LinkSides(Link, SourceSide, TargetSide, RailSide);
                const ImVec2 From = PortAnchor(Link.m_SourceNode, OutP, SourceSide), To = PortAnchor(Link.m_TargetNode, InP, TargetSide);
                const float HX = HighwayX(OwnerColumnOf(Link), RailSide, LaneOfLink[Link.m_Id]);
                const ImVec2 Pts[4] = { From, { HX, From.y }, { HX, To.y }, To };
                for (int s = 0; s < 3; ++s) { const float D = DistPointSegment(MouseLocal, Pts[s], Pts[s + 1]); if (D < Best) { Best = D; HitLink = Link.m_Id; } }
            }
            commands::Run(System, HitLink ? commands::MakeSelectLink(HitLink) : commands::MakeClearSelection());
        }

        // No separate "background click/right-click for Add Node" anymore - right-click is pan-only now,
        // and the trailing spine marker (GapIndex == Layout.size(), drawn below) already covers
        // "add a node after the last one", so nothing is lost.

        if (bWindowHovered && ImGui::IsKeyPressed(ImGuiKey_Delete))
        {
            if (Selection.m_SelectedLink)
                commands::Run(System, commands::MakeDeleteLink(Selection.m_SelectedLink));
            else if (!Selection.m_SelectedNodes.empty())
                commands::Run(System, commands::MakeDeleteNodes({ Selection.m_SelectedNodes.begin(), Selection.m_SelectedNodes.end() }));
            else if (Selection.m_SelectedGapSpineId)
            {
                bool bHasNodes = false;
                for (auto& N : Nodes) if (N.m_SpineId == Selection.m_SelectedGapSpineId) { bHasNodes = true; break; }
                if (bHasNodes)
                {
                    // DeleteSpine itself would just refuse this - ask first, since it means taking every
                    // node on the spine (and every link touching them) with it.
                    DeleteSpineConfirm.m_SpineId = Selection.m_SelectedGapSpineId;
                    ImGui::OpenPopup("NodeOS_DeleteSpineConfirm");
                }
                else
                {
                    commands::Run(System, commands::MakeDeleteSpine(Selection.m_SelectedGapSpineId));
                }
            }
        }

        if (ImGui::BeginPopupModal("NodeOS_DeleteSpineConfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            int NodeCount = 0;
            for (auto& N : Nodes) if (N.m_SpineId == DeleteSpineConfirm.m_SpineId) ++NodeCount;
            ImGui::Text("Delete this spine and its %d node%s?", NodeCount, NodeCount == 1 ? "" : "s");
            ImGui::Separator();
            if (ImGui::Button("Delete", ImVec2(120, 0)))
            {
                std::vector<std::uint64_t> Ids;
                for (auto& N : Nodes) if (N.m_SpineId == DeleteSpineConfirm.m_SpineId) Ids.push_back(N.m_Id);
                if (!Ids.empty()) commands::Run(System, commands::MakeDeleteNodes(Ids));
                commands::Run(System, commands::MakeDeleteSpine(DeleteSpineConfirm.m_SpineId));
                DeleteSpineConfirm.m_SpineId = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                DeleteSpineConfirm.m_SpineId = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::End();
    }

    //------------------------------------------------------------------------------------------------
    // Dependency-respecting evaluation: repeatedly execute any not-yet-run node whose every
    // connected input already has a producer that has run, until nothing changes. Good enough for
    // the small acyclic graphs this proof of concept cares about. No longer needs AvailableTypes at
    // all - each node instance carries its own behavior directly (node_instance::m_pNode).
    //------------------------------------------------------------------------------------------------
    // ---- Real spine/exec-flow interpreter (NODE_SCRIPTING_DESIGN.md's exec-flow addition) ----
    // This is what actually RUNS the program, replacing the older pure-dataflow "run whatever's
    // ready" fixed point that used to live in ExecuteGraph (still fine for graphs with zero exec
    // involvement, but wrong once OnEvent/ExecutionCall/Function/Execute exist - that model has no
    // concept of spine order, scope, or "only run if actually triggered," and would call a node's
    // Execute() the moment its inputs looked ready regardless of whether anything ever invokes it).
    // A node runs if it's reachable from the root spine by walking ordinary spine Order and Exec
    // wires (the rule settled early this session) - OR, since a later session, if some node that
    // WAS reached that way reads one of its outputs: an ordinary/"pure" node (no Exec pins of its
    // own, doesn't own a scope) is a data dependency, not a position on a spine - moving it to a
    // different spine, or ahead of/behind whoever reads it, was never meant to change whether it
    // runs, any more than it would in Blueprints/Shader Graph/any other pull-based node graph. See
    // PullInputValue/EnsureNodeRun below for the actual pull; anything else genuinely unreached by
    // either rule is still inert, "commented code," never executed.
    static node_instance* FindNodeById(std::uint64_t Id, std::vector<node_instance>& Nodes)
    {
        auto It = std::find_if(Nodes.begin(), Nodes.end(), [&](auto& N) { return N.m_Id == Id; });
        return It == Nodes.end() ? nullptr : &*It;
    }
    // A port that carries a real, resolvable value at runtime - excludes Exec (pure control-flow
    // trigger, never a value) and Scope (the owner<->End ownership pin, likewise never a value).
    static bool IsRealDataPort(const xnode_os_port_desc& P) noexcept
    {
        return !IsExecType(P.m_pTypeName) && !IsScopeType(P.m_pTypeName);
    }
    static void RunOrdinaryNode(node_instance& Node, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::unordered_map<std::uint64_t, std::string>& LiteralValues, literal_storage& Scratch, int PullDepth = 0);
    // Only a genuinely PURE/ordinary data node is eligible to be pulled - anything with a real Exec
    // pin (OnEvent/ExecutionCall/Execute/Function), or that owns a scope of its own content
    // (If/ForEachLoop), must still go through its own explicit trigger. A data read must never
    // silently invoke a Function call, fire an ExecutionCall fan-out, or run a conditional scope's
    // body as a side effect of some unrelated node just wanting to read a value - only leaf/data
    // nodes (Constant, Compare, Math Expression, ...) are safe to evaluate lazily, on demand,
    // regardless of where they happen to sit.
    static bool IsPullableNodeType(std::string_view Name) noexcept
    {
        return Name != "OnEvent" && Name != "ExecutionCall" && Name != "Execute" && Name != "Function"
            && Name != "If" && Name != "ForEachLoop" && Name != "End";
    }
    static void EnsureNodeRun(std::uint64_t NodeId, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::unordered_map<std::uint64_t, std::string>& LiteralValues, literal_storage& Scratch, int PullDepth)
    {
        // A real cyclic data dependency (A needs B needs A) would otherwise recurse forever - bail
        // rather than stack-overflow; no ordinary graph legitimately nests pulls this deep. Left
        // unrun, exactly like any other node the walk never reaches - PullInputValue's own caller
        // sees nullptr, same as an honestly-unconnected pin.
        if (PullDepth > 64) return;
        node_instance* pNode = FindNodeById(NodeId, Nodes);
        if (!pNode || !pNode->m_pNode || pNode->m_bHasRun) return;
        if (!IsPullableNodeType(pNode->m_pNode->m_pFactory->getName())) return;
        RunOrdinaryNode(*pNode, Nodes, Links, LiteralValues, Scratch, PullDepth + 1);
    }
    // Like GetInputValue, but for real execution: if a wire's source hasn't run yet, PULLS it (runs
    // it right now, recursively resolving its own inputs the same way) instead of just reporting
    // nullptr - see the pull-based-execution comment above FindNodeById for why. GetInputValue itself
    // stays read-only/non-pulling, for the canvas preview and mesh-preview passes that must never
    // trigger a real Execute() (with its real side effects) merely because a frame got drawn.
    static void* PullInputValue(std::uint64_t NodeId, int InputIndex, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::unordered_map<std::uint64_t, std::string>& LiteralValues, literal_storage& Scratch, int PullDepth)
    {
        for (auto& Link : Links)
        {
            if (Link.m_TargetNode != NodeId || Link.m_TargetInput != InputIndex) continue;
            auto SourceIt = std::find_if(Nodes.begin(), Nodes.end(), [&](auto& N) { return N.m_Id == Link.m_SourceNode; });
            if (SourceIt == Nodes.end()) return nullptr;
            if (!SourceIt->m_bHasRun)
                EnsureNodeRun(SourceIt->m_Id, Nodes, Links, LiteralValues, Scratch, PullDepth);
            if (!SourceIt->m_bHasRun) return nullptr; // still didn't run - an Exec-gated/scope-owning source, or a cycle bailout
            return (Link.m_SourceOutput < (int)SourceIt->m_CachedOutputs.size()) ? SourceIt->m_CachedOutputs[Link.m_SourceOutput] : nullptr;
        }
        return ResolveUnconnectedLiteral(NodeId, InputIndex, Nodes, Links, LiteralValues, Scratch);
    }
    static void RunOrdinaryNode(node_instance& Node, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::unordered_map<std::uint64_t, std::string>& LiteralValues, literal_storage& Scratch, int PullDepth)
    {
        if (!Node.m_pNode || Node.m_bHasRun) return;
        const auto NodeInputs  = Node.m_pNode->getInputs();
        const auto NodeOutputs = Node.m_pNode->getOutputs();
        std::vector<void*> Inputs(NodeInputs.size(), nullptr);
        for (int i = 0; i < (int)NodeInputs.size(); ++i)
            Inputs[i] = PullInputValue(Node.m_Id, i, Nodes, Links, LiteralValues, Scratch, PullDepth);
        Node.m_CachedOutputs.assign(NodeOutputs.size(), nullptr);
        Node.m_pNode->Execute(Inputs.data(), Node.m_CachedOutputs.data());
        Node.m_bHasRun = true;
    }
    static void RunSpineRange(std::uint64_t SpineId, int FromOrderInclusive, int ToOrderExclusive, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::unordered_map<std::uint64_t, std::string>& LiteralValues, literal_storage& Scratch);
    static void RunExecTarget(std::uint64_t TargetNodeId, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::unordered_map<std::uint64_t, std::string>& LiteralValues, literal_storage& Scratch);
    // ExecutionCall's Exec output fans out to every Exec-typed link off it - fork, run each target to
    // completion (RunExecTarget is fully synchronous, so this doubles as the join: nothing after this
    // call returns until every fanned-out target has finished). Order between multiple targets is
    // deliberately unspecified (settled this session); a plain left-to-right pass over Links is as
    // good as any other order today.
    static void RunExecutionCall(node_instance& Caller, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::unordered_map<std::uint64_t, std::string>& LiteralValues, literal_storage& Scratch)
    {
        if (Caller.m_bHasRun) return;
        Caller.m_bHasRun = true;
        for (auto& L : Links)
            if (L.m_SourceNode == Caller.m_Id)
                RunExecTarget(L.m_TargetNode, Nodes, Links, LiteralValues, Scratch);
    }
    // Entering Function or Execute via an incoming Exec trigger - the only way either ever runs (see
    // RunSpineRange, which deliberately skips both during ordinary positional walking).
    static void RunExecTarget(std::uint64_t TargetNodeId, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::unordered_map<std::uint64_t, std::string>& LiteralValues, literal_storage& Scratch)
    {
        node_instance* pTarget = FindNodeById(TargetNodeId, Nodes);
        if (!pTarget || !pTarget->m_pNode || pTarget->m_bHasRun) return;
        const auto Name = pTarget->m_pNode->m_pFactory->getName();

        if (Name == "Function")
        {
            // Real subroutine call: resolve its own declared (external, non-local, non-Exec) inputs
            // from wherever they're wired, mirror each one into the matching local-scope OUTPUT slot
            // (the body's own view of its parameters - function_node.cpp's Rebuild always places the
            // K-th declared input's mirror at output index [ExternalOutputCount + K]), run the body
            // (everything between this node and its own End, in Order), then mirror whatever the
            // body wrote into the local Result-mirror INPUT back out to the matching declared
            // external OUTPUT (the reverse direction, same indexing scheme).
            const auto Inputs  = pTarget->m_pNode->getInputs();
            const auto Outputs = pTarget->m_pNode->getOutputs();
            std::vector<void*> InVals(Inputs.size(), nullptr);
            for (int i = 0; i < (int)Inputs.size(); ++i)
                if (!Inputs[i].m_bLocalScope && IsRealDataPort(Inputs[i]))
                    InVals[i] = PullInputValue(pTarget->m_Id, i, Nodes, Links, LiteralValues, Scratch, 0);
            pTarget->m_CachedOutputs.assign(Outputs.size(), nullptr);
            pTarget->m_pNode->Execute(InVals.data(), pTarget->m_CachedOutputs.data()); // no-op today, kept for a real ABI

            // Where the local-mirror GROUP actually starts in each direction - found by scanning for
            // the real boundary, not by counting type-filtered pins and assuming the mirror group
            // sits immediately after them. That assumption held while Exec was appended LAST (so
            // every non-local input was also a "real data" input), but broke the moment Exec moved
            // to be first: counting only "external, real-data" inputs then undercounts by exactly
            // one (Exec occupies a non-local slot the count skips), pointing every mirror lookup one
            // pin too early. Scanning for the actual first-local-pin index is correct regardless of
            // how many/which non-local pins precede the local group, in either direction.
            int FirstLocalOutputIdx = (int)Outputs.size();
            for (int i = 0; i < (int)Outputs.size(); ++i) if (Outputs[i].m_bLocalScope) { FirstLocalOutputIdx = i; break; }
            int FirstLocalInputIdx = (int)Inputs.size();
            for (int i = 0; i < (int)Inputs.size(); ++i) if (Inputs[i].m_bLocalScope) { FirstLocalInputIdx = i; break; }

            for (int i = 0, K = 0; i < (int)Inputs.size(); ++i)
            {
                if (Inputs[i].m_bLocalScope || !IsRealDataPort(Inputs[i])) continue;
                const int MirrorIdx = FirstLocalOutputIdx + K;
                if (MirrorIdx < (int)pTarget->m_CachedOutputs.size()) pTarget->m_CachedOutputs[MirrorIdx] = InVals[i];
                ++K;
            }
            pTarget->m_bHasRun = true;

            auto* pEnd = FindNodeById(pTarget->m_OwnedEndId, Nodes);
            const int EndOrder = pEnd ? pEnd->m_Order : INT_MAX;
            RunSpineRange(pTarget->m_SpineId, pTarget->m_Order + 1, EndOrder, Nodes, Links, LiteralValues, Scratch);

            for (int i = 0, L2 = 0; i < (int)Outputs.size(); ++i)
            {
                if (Outputs[i].m_bLocalScope || !IsRealDataPort(Outputs[i])) continue;
                const int MirrorInputIdx = FirstLocalInputIdx + L2;
                pTarget->m_CachedOutputs[i] = (MirrorInputIdx < (int)Inputs.size()) ? PullInputValue(pTarget->m_Id, MirrorInputIdx, Nodes, Links, LiteralValues, Scratch, 0) : nullptr;
                ++L2;
            }
        }
        else if (Name == "Execute")
        {
            pTarget->m_CachedOutputs.assign(pTarget->m_pNode->getOutputs().size(), nullptr);
            pTarget->m_bHasRun = true;
            // No owned scope - "body" is simply everything positionally after it in its own spine,
            // all the way to the spine's own end (NODE_SCRIPTING_DESIGN.md's Execute/lambda-capture
            // analogy) - nothing bounds it the way Function's own End does.
            RunSpineRange(pTarget->m_SpineId, pTarget->m_Order + 1, INT_MAX, Nodes, Links, LiteralValues, Scratch);
        }
    }
    // The flat-spine model's own base case: run every node positioned in [FromOrderInclusive,
    // ToOrderExclusive) of one spine, in Order. "End" is a pure boundary marker, never run. Function
    // and Execute are deliberately SKIPPED here even if positionally reached - both declare a real
    // Exec input specifically so they only ever run via an incoming trigger (RunExecTarget), never
    // just because ordinary spine order got to them. ForEachLoop isn't given real loop semantics
    // yet - nothing saved exercises it; RunOrdinaryNode's generic "resolve inputs, call Execute()
    // once" is what it'd fall through to today, same as any other node type not specifically
    // recognized here - a real next step once something actually needs it.
    static void RunSpineRange(std::uint64_t SpineId, int FromOrderInclusive, int ToOrderExclusive, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::unordered_map<std::uint64_t, std::string>& LiteralValues, literal_storage& Scratch)
    {
        std::vector<node_instance*> Members;
        for (auto& N : Nodes)
            if (N.m_SpineId == SpineId && N.m_Order >= FromOrderInclusive && N.m_Order < ToOrderExclusive)
                Members.push_back(&N);
        std::sort(Members.begin(), Members.end(), [](auto* A, auto* B) { return A->m_Order < B->m_Order; });
        for (std::size_t i = 0; i < Members.size(); ++i)
        {
            auto* pN = Members[i];
            if (!pN->m_pNode || pN->m_bHasRun) continue;
            const auto Name = pN->m_pNode->m_pFactory->getName();
            if (Name == "End") continue;
            if (Name == "ExecutionCall") { RunExecutionCall(*pN, Nodes, Links, LiteralValues, Scratch); continue; }
            if (Name == "Function" || Name == "Execute") continue;
            if (Name == "If" && pN->m_OwnedEndId != 0)
            {
                // If has no Exec pins at all (if_node.cpp) - purely positional, its true-branch body
                // is just whatever physically follows it in this same spine up to its owned End. Runs
                // itself first (a no-op Execute(), but this is what resolves+marks Condition's source
                // as read) then decides whether to recurse into the body at all - an untaken branch's
                // nodes are deliberately left m_bHasRun == false, so the existing "not reached this
                // run" flagging (ExecuteGraph's epilogue) shows exactly which path didn't execute,
                // same meaning it already carries for any other unreached node. Condition is resolved
                // via PullInputValue, not GetInputValue - Compare (or whatever feeds it) is a data
                // dependency, not something that has to happen to sit somewhere the flat walk already
                // reaches; If wiring TO it is exactly what should pull it in, wherever it lives.
                RunOrdinaryNode(*pN, Nodes, Links, LiteralValues, Scratch);
                const bool* pCond = static_cast<const bool*>(PullInputValue(pN->m_Id, 0, Nodes, Links, LiteralValues, Scratch, 0));
                auto* pEnd = FindNodeById(pN->m_OwnedEndId, Nodes);
                const int EndOrder = pEnd ? pEnd->m_Order : INT_MAX;
                if (pCond && *pCond)
                    RunSpineRange(SpineId, pN->m_Order + 1, EndOrder, Nodes, Links, LiteralValues, Scratch);
                // Either way, the OUTER walk must not also treat the body as ordinary members once
                // this returns - skip past it (the recursive call above already ran+marked it when
                // taken; when not taken, this is what keeps it from running unconditionally, which
                // was the whole bug this block exists to fix).
                while (i + 1 < Members.size() && Members[i + 1]->m_Order < EndOrder) ++i;
                continue;
            }
            RunOrdinaryNode(*pN, Nodes, Links, LiteralValues, Scratch);
        }
    }
    // Runs the whole PROGRAM: starts at the root spine's own beginning and walks forward - OnEvent
    // is a pure label (zero pins, does nothing on its own); ExecutionCall is what actually fires.
    // Once the root spine runs off its own end, the program is done, independent of anything else
    // that may or may not have been triggered along the way ("main spine governs program lifetime,"
    // settled this session).
    static void RunProgram(std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::vector<spine>& Spines, const std::unordered_map<std::uint64_t, std::string>& LiteralValues, literal_storage& Scratch)
    {
        GetRuntimeLog().clear();
        std::uint64_t RootSpineId = 0;
        for (auto& S : Spines) if (S.m_bIsRoot) { RootSpineId = S.m_Id; break; }
        if (RootSpineId == 0) return;
        RunSpineRange(RootSpineId, 0, INT_MAX, Nodes, Links, LiteralValues, Scratch);
    }

    // ---- Real C++ codegen (tests whether ordinary nodes' own logic can be REUSED rather than
    // reimplemented by generated code - the question this whole block exists to answer). Mirrors
    // RunSpineRange/RunExecTarget/RunExecutionCall's exact structure - same reachability walk, same
    // recognized node-type names - but EMITS TEXT instead of executing. Handles exactly what the
    // current saved test graph needs (OnEvent, Constant, ExecutionCall, Function, Execute, Print) -
    // anything else emits a visible "not implemented" comment rather than silently producing nothing
    // or something wrong.
    //
    // The interpreter (RunExecTarget's "Function" branch) needed real bookkeeping at runtime to
    // mirror a resolved parameter value into the local-scope output slot the body reads through -
    // codegen needs NONE of that: a Function's parameter and its own local-mirror output share the
    // exact same generated variable name (CppVar(FunctionId, MirrorOutIdx) for both), so the body's
    // ordinary "read my local mirror" link resolves, via the same CppInputExpr every other link
    // uses, straight to the C++ parameter itself. No separate mirroring step exists in the emitted
    // code at all - this is the concrete "codegen is simpler here than the interpreter was" case,
    // not just a claim.
    static std::string ReadStringPropertyFromSnapshot(const std::string& Snapshot, std::string_view Name)
    {
        std::size_t Pos = 0;
        while (Pos < Snapshot.size())
        {
            const std::size_t LineEnd = Snapshot.find('\n', Pos);
            const std::string Line = Snapshot.substr(Pos, (LineEnd == std::string::npos ? Snapshot.size() : LineEnd) - Pos);
            Pos = (LineEnd == std::string::npos) ? Snapshot.size() : LineEnd + 1;
            const std::size_t Tab1 = Line.find('\t');
            if (Tab1 != std::string::npos && std::string_view(Line).substr(0, Tab1) == Name)
            {
                const std::size_t Tab2 = Line.find('\t', Tab1 + 1);
                if (Tab2 != std::string::npos) return Line.substr(Tab2 + 1);
            }
        }
        return {};
    }
    // A stable, deterministic C++ variable name for a given (NodeId, OutputIndex) pin - masked to
    // 24 bits purely for readability in the generated source, collisions are not a real concern for
    // a single small test graph.
    static std::string CppVar(std::uint64_t NodeId, int OutputIndex)
    {
        return std::format("v{:x}_{}", NodeId & 0xffffff, OutputIndex);
    }
    static void EmitOrdinaryNode(node_instance& Node, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::unordered_map<std::uint64_t, std::string>& LiteralValues, std::set<std::uint64_t>& EmittedNodeIds, std::string& Out);
    // Codegen's own mirror of the interpreter's EnsureNodeRun - see IsPullableNodeType's own comment
    // for exactly which node types are (and are never) safe to pull. Emits the source's declaration
    // directly into Out, the SAME accumulator the caller is about to append its own line into - since
    // this runs to completion before that caller's own `Out += ...` executes, the pulled dependency's
    // declaration always lands immediately BEFORE the statement that needed it, which is the only
    // place C++'s declare-before-use rule allows it to go.
    static void EnsureNodeEmitted(std::uint64_t NodeId, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::unordered_map<std::uint64_t, std::string>& LiteralValues, std::set<std::uint64_t>& EmittedNodeIds, std::string& Out, int PullDepth)
    {
        if (PullDepth > 64 || EmittedNodeIds.count(NodeId)) return; // cycle bailout, or already done
        node_instance* pNode = FindNodeById(NodeId, Nodes);
        if (!pNode || !pNode->m_pNode) return;
        if (!IsPullableNodeType(pNode->m_pNode->m_pFactory->getName())) return;
        EmitOrdinaryNode(*pNode, Nodes, Links, LiteralValues, EmittedNodeIds, Out);
    }
    // Whatever C++ expression currently feeds an input pin - the variable name for whatever's wired
    // to it, or, if nothing's wired, the same inline-literal-on-unconnected-pin value the interpreter
    // now also honors (see GetInputValue's own comment) - formatted as a real C++ literal token
    // matching the pin's effective type, rather than always silently falling back to 0.0f regardless
    // of what's typed in.
    //
    // A wire's source that hasn't been emitted yet gets PULLED in (EnsureNodeEmitted) rather than
    // treated as a hard "0.0f" miss - mirrors PullInputValue's own interpreter-side behavior: a data
    // wire is a real dependency, not a requirement that the producer happen to sit somewhere the flat
    // spine walk already reaches. Only a genuinely unpullable source (Exec-gated, scope-owning, or a
    // real cyclic dependency) still falls back to "0.0f", matching that same source's interpreter-
    // side null result.
    static std::string CppInputExpr(std::uint64_t NodeId, int InputIndex, std::vector<link_instance>& Links, std::vector<node_instance>& Nodes, const std::unordered_map<std::uint64_t, std::string>& LiteralValues, std::set<std::uint64_t>& EmittedNodeIds, std::string& Out, int PullDepth = 0)
    {
        for (auto& L : Links)
            if (L.m_TargetNode == NodeId && L.m_TargetInput == InputIndex)
            {
                if (!EmittedNodeIds.count(L.m_SourceNode))
                    EnsureNodeEmitted(L.m_SourceNode, Nodes, Links, LiteralValues, EmittedNodeIds, Out, PullDepth);
                if (EmittedNodeIds.count(L.m_SourceNode)) return CppVar(L.m_SourceNode, L.m_SourceOutput);
                return "0.0f";
            }

        auto LitIt = LiteralValues.find(InPinOf(NodeId, InputIndex));
        if (LitIt == LiteralValues.end() || LitIt->second.empty()) return "0.0f";
        node_instance* pNode = FindNodeById(NodeId, Nodes);
        const char* pEffType = "Float";
        if (pNode && pNode->m_pNode)
        {
            const auto Inputs = pNode->m_pNode->getInputs();
            if (InputIndex < (int)Inputs.size())
                pEffType = EffectiveTypeName(NodeId, pNode->m_pNode, Inputs[InputIndex].m_pTypeName, Nodes, Links);
        }
        if (std::strcmp(pEffType, "Bool") == 0) return (LitIt->second == "1" || LitIt->second == "true") ? "true" : "false";
        if (std::strcmp(pEffType, "Int") == 0 || std::strcmp(pEffType, "Short") == 0) return LitIt->second;
        // Float, and the default for anything else - same "{:#}" round-trip Constant's own literal
        // emission uses, so a whole number typed in ("1") doesn't produce the invalid literal "1f".
        return std::format("{:#}f", std::strtof(LitIt->second.c_str(), nullptr));
    }
    static void EmitOrdinaryNode(node_instance& Node, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::unordered_map<std::uint64_t, std::string>& LiteralValues, std::set<std::uint64_t>& EmittedNodeIds, std::string& Out)
    {
        const auto Name = Node.m_pNode->m_pFactory->getName();
        if (Name == "Constant")
        {
            // A literal's value is known at codegen time - nothing to call, just substitute the
            // number directly. The one ordinary node type in this test graph with no real logic to
            // share (there's no logic, only a value).
            const std::string ValueText = ReadStringPropertyFromSnapshot(SerializePropertiesToString(Node.m_pNode), "Value");
            // Round-tripped through a real float parse/format rather than appending an "f" suffix
            // straight onto whatever text was stored - a whole-number value like "0" has no decimal
            // point, so a bare textual "f" suffix produces the syntactically invalid literal "0f".
            // "{:#}" forces a decimal point even for a whole number (std::format's float '#' flag,
            // same meaning as printf's) - plain "{}" formats 0.0f as "0", which a bare "f" suffix
            // would turn into the invalid literal "0f" instead of the valid "0.f".
            const float Value = std::strtof(ValueText.empty() ? "0" : ValueText.c_str(), nullptr);
            Out += std::format("    float {} = {:#}f;\n", CppVar(Node.m_Id, 0), Value);
        }
        else if (Name == "Print")
        {
            Out += std::format("    std::printf(\"%.2f\\n\", {});\n", CppInputExpr(Node.m_Id, 0, Links, Nodes, LiteralValues, EmittedNodeIds, Out));
        }
        else if (Name == "Compare")
        {
            // Mirrors compare_node.cpp's own Execute() switch, but working from the Operator
            // property's raw serialized form - ReflectedMemberToRow stores an enum as
            // ReadEnumAsInt's numeric value ("0".."5"), never the display name, so this indexes the
            // same compare_op_v ordering by number rather than matching against enum item text.
            const std::string Op = ReadStringPropertyFromSnapshot(SerializePropertiesToString(Node.m_pNode), "Operator");
            const char* pToken = "!=";
            switch (Op.empty() ? 0 : std::atoi(Op.c_str()))
            {
                case 0: pToken = ">";  break; // GREATER
                case 1: pToken = "<";  break; // LESS
                case 2: pToken = "=="; break; // EQUAL
                case 3: pToken = "!="; break; // NOT_EQUAL
                case 4: pToken = ">="; break; // GREATER_OR_EQUAL
                case 5: pToken = "<="; break; // LESS_OR_EQUAL
            }
            Out += std::format("    bool {} = ({} {} {});\n", CppVar(Node.m_Id, 0), CppInputExpr(Node.m_Id, 0, Links, Nodes, LiteralValues, EmittedNodeIds, Out), pToken, CppInputExpr(Node.m_Id, 1, Links, Nodes, LiteralValues, EmittedNodeIds, Out));
        }
        else if (Name == "Math Expression")
        {
            // Mirrors math_expression_node.cpp's own Execute() switch (same raw-serialized-enum
            // indexing Compare uses above) - the REVERSE variants swap which operand prints on which
            // side of the operator rather than just picking a different token, so this builds the
            // whole expression per case instead of substituting one shared token into a fixed shape.
            const std::string Op = ReadStringPropertyFromSnapshot(SerializePropertiesToString(Node.m_pNode), "Operator");
            const std::string A = CppInputExpr(Node.m_Id, 0, Links, Nodes, LiteralValues, EmittedNodeIds, Out);
            const std::string B = CppInputExpr(Node.m_Id, 1, Links, Nodes, LiteralValues, EmittedNodeIds, Out);
            std::string Expr;
            switch (Op.empty() ? 0 : std::atoi(Op.c_str()))
            {
                case 0: Expr = std::format("({} + {})", A, B); break; // ADD
                case 1: Expr = std::format("({} - {})", A, B); break; // SUBTRACT
                case 2: Expr = std::format("({} - {})", B, A); break; // SUBTRACT_REVERSE
                case 3: Expr = std::format("({} * {})", A, B); break; // MULTIPLY
                case 4: Expr = std::format("({} / {})", A, B); break; // DIVIDE
                case 5: Expr = std::format("({} / {})", B, A); break; // DIVIDE_REVERSE
                default: Expr = "0.0f"; break;
            }
            Out += std::format("    float {} = {};\n", CppVar(Node.m_Id, 0), Expr);
        }
        else
        {
            Out += std::format("    // codegen for '{}' not implemented yet\n", Name);
        }
        // Marks this node reachable/emitted for CppInputExpr's own "is my wired source real"
        // check - mirrors the interpreter's m_bHasRun, but kept as a SEPARATE set rather than
        // reusing that field: codegen and the interpreter can both run in the same process (the
        // self-test does exactly this, back to back, on the same Nodes), and m_bHasRun already has
        // its own real meaning there - overloading it here would make each pass corrupt the other's
        // bookkeeping.
        EmittedNodeIds.insert(Node.m_Id);
    }
    static void EmitSpineRange(std::uint64_t SpineId, int FromOrderInclusive, int ToOrderExclusive, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::unordered_map<std::uint64_t, std::string>& LiteralValues, std::set<std::uint64_t>& EmittedNodeIds, std::string& Out, std::string& FunctionDefs);
    static void EmitExecTarget(std::uint64_t TargetNodeId, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::unordered_map<std::uint64_t, std::string>& LiteralValues, std::set<std::uint64_t>& EmittedNodeIds, std::string& Out, std::string& FunctionDefs);
    static void EmitExecutionCall(node_instance& Caller, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::unordered_map<std::uint64_t, std::string>& LiteralValues, std::set<std::uint64_t>& EmittedNodeIds, std::string& Out, std::string& FunctionDefs)
    {
        // No "already emitted" guard the way RunExecutionCall has m_bHasRun - a node reached by more
        // than one path would get emitted (and its variable re-declared) more than once, a real
        // limitation this first pass doesn't handle; the current test graph is tree-shaped so it
        // never comes up.
        for (auto& L : Links)
            if (L.m_SourceNode == Caller.m_Id)
                EmitExecTarget(L.m_TargetNode, Nodes, Links, LiteralValues, EmittedNodeIds, Out, FunctionDefs);
    }
    static void EmitExecTarget(std::uint64_t TargetNodeId, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::unordered_map<std::uint64_t, std::string>& LiteralValues, std::set<std::uint64_t>& EmittedNodeIds, std::string& Out, std::string& FunctionDefs)
    {
        node_instance* pTarget = FindNodeById(TargetNodeId, Nodes);
        if (!pTarget || !pTarget->m_pNode) return;
        const auto Name = pTarget->m_pNode->m_pFactory->getName();

        if (Name == "Function")
        {
            const auto Inputs  = pTarget->m_pNode->getInputs();
            const auto Outputs = pTarget->m_pNode->getOutputs();
            int ExternalOutputCount = 0;
            for (auto& O : Outputs) if (!O.m_bLocalScope && IsRealDataPort(O)) ++ExternalOutputCount;

            // Declared (external, non-local, non-Exec) inputs become real C++ parameters - named
            // the SAME as the matching local-mirror OUTPUT (see this block's own top comment for why
            // that one naming choice is what makes the body's "read my parameter" links just work
            // with no special-casing at all).
            std::string Params;
            int ParamCount = 0;
            for (int i = 0; i < (int)Inputs.size(); ++i)
            {
                if (Inputs[i].m_bLocalScope || !IsRealDataPort(Inputs[i])) continue;
                if (!Params.empty()) Params += ", ";
                Params += std::format("float {}", CppVar(pTarget->m_Id, ExternalOutputCount + ParamCount));
                ++ParamCount;
            }

            // Marked emitted BEFORE the body below, not after - the body reads its own parameters
            // through Function's own local-mirror OUTPUT slots (same node id, different output
            // index), so CppInputExpr's "is my wired source real" check needs pTarget->m_Id already
            // in the set by the time the body's own Print/etc. resolve those links. Mirrors the
            // interpreter's RunExecTarget, which sets pTarget->m_bHasRun = true before its own
            // RunSpineRange(body) call for the exact same reason.
            EmittedNodeIds.insert(pTarget->m_Id);

            std::string Body;
            auto* pEnd = FindNodeById(pTarget->m_OwnedEndId, Nodes);
            const int EndOrder = pEnd ? pEnd->m_Order : INT_MAX;
            EmitSpineRange(pTarget->m_SpineId, pTarget->m_Order + 1, EndOrder, Nodes, Links, LiteralValues, EmittedNodeIds, Body, FunctionDefs);

            // Whatever's wired into the local Result-mirror INPUT becomes the return expression -
            // only the first declared output is handled today, matching the interpreter's own scope.
            // Found by SCANNING for the first local-scope Input, not by deriving an offset from a
            // count - Inputs is [Exec][external params...][local Result-mirror...], so the local
            // group's start shifted by one the moment Exec became the first declared input (see
            // RunExecTarget's own identical fix, same root cause).
            int FirstLocalInputIdx = (int)Inputs.size();
            for (int i = 0; i < (int)Inputs.size(); ++i) if (Inputs[i].m_bLocalScope) { FirstLocalInputIdx = i; break; }
            std::string ReturnExpr = "0.0f";
            for (int i = 0; i < (int)Outputs.size(); ++i)
            {
                if (Outputs[i].m_bLocalScope || !IsRealDataPort(Outputs[i])) continue;
                ReturnExpr = CppInputExpr(pTarget->m_Id, FirstLocalInputIdx, Links, Nodes, LiteralValues, EmittedNodeIds, Body);
                break;
            }

            const std::string FnName = std::format("Fn_{:x}", pTarget->m_Id & 0xffffff);
            FunctionDefs += std::format("static float {}({})\n{{\n{}    return {};\n}}\n\n", FnName, Params, Body, ReturnExpr);

            std::string Args;
            for (int i = 0; i < (int)Inputs.size(); ++i)
            {
                if (Inputs[i].m_bLocalScope || !IsRealDataPort(Inputs[i])) continue;
                if (!Args.empty()) Args += ", ";
                Args += CppInputExpr(pTarget->m_Id, i, Links, Nodes, LiteralValues, EmittedNodeIds, Out);
            }
            Out += std::format("    float {} = {}({});\n", CppVar(pTarget->m_Id, 0), FnName, Args);
        }
        else if (Name == "Execute")
        {
            // No owned scope - maps directly onto a C++ lambda captured by reference, exactly the
            // "invoking a lambda, not calling a real function" distinction NODE_SCRIPTING_DESIGN.md
            // §12.2 draws - its body just runs inline, reading/writing whatever's already in scope.
            std::string Body, Unused;
            EmitSpineRange(pTarget->m_SpineId, pTarget->m_Order + 1, INT_MAX, Nodes, Links, LiteralValues, EmittedNodeIds, Body, Unused);
            Out += std::format("    [&]() {{\n{}    }}();\n", Body);
        }
    }
    static void EmitSpineRange(std::uint64_t SpineId, int FromOrderInclusive, int ToOrderExclusive, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::unordered_map<std::uint64_t, std::string>& LiteralValues, std::set<std::uint64_t>& EmittedNodeIds, std::string& Out, std::string& FunctionDefs)
    {
        std::vector<node_instance*> Members;
        for (auto& N : Nodes)
            if (N.m_SpineId == SpineId && N.m_Order >= FromOrderInclusive && N.m_Order < ToOrderExclusive)
                Members.push_back(&N);
        std::sort(Members.begin(), Members.end(), [](auto* A, auto* B) { return A->m_Order < B->m_Order; });
        for (std::size_t i = 0; i < Members.size(); ++i)
        {
            auto* pN = Members[i];
            if (!pN->m_pNode) continue;
            const auto Name = pN->m_pNode->m_pFactory->getName();
            if (Name == "End" || Name == "OnEvent") continue;
            if (Name == "ExecutionCall") { EmitExecutionCall(*pN, Nodes, Links, LiteralValues, EmittedNodeIds, Out, FunctionDefs); continue; }
            if (Name == "Function" || Name == "Execute") continue;
            if (Name == "If" && pN->m_OwnedEndId != 0)
            {
                // Mirrors RunSpineRange's own "If" handling: a real C++ if(){} block, body bounded
                // by the same Order range the interpreter uses (this node's Order+1 up to its owned
                // End's Order) - not the flat, unconditional walk that used to emit this body's code
                // regardless of the (also unimplemented, until now) condition.
                auto* pEnd = FindNodeById(pN->m_OwnedEndId, Nodes);
                const int EndOrder = pEnd ? pEnd->m_Order : INT_MAX;
                std::string Body;
                EmitSpineRange(SpineId, pN->m_Order + 1, EndOrder, Nodes, Links, LiteralValues, EmittedNodeIds, Body, FunctionDefs);
                Out += std::format("    if ({}) {{\n{}    }}\n", CppInputExpr(pN->m_Id, 0, Links, Nodes, LiteralValues, EmittedNodeIds, Out), Body);
                while (i + 1 < Members.size() && Members[i + 1]->m_Order < EndOrder) ++i;
                continue;
            }
            EmitOrdinaryNode(*pN, Nodes, Links, LiteralValues, EmittedNodeIds, Out);
        }
    }
    // The one entry point: same signature as RunProgram (no ImGui/xgpu dependency at all), so this
    // is directly testable without touching the GUI.
    static std::string GenerateCpp(std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::vector<spine>& Spines, const std::unordered_map<std::uint64_t, std::string>& LiteralValues)
    {
        std::uint64_t RootSpineId = 0;
        for (auto& S : Spines) if (S.m_bIsRoot) { RootSpineId = S.m_Id; break; }
        std::string FunctionDefs, MainBody;
        // Tracks which nodes actually got emitted as the walk proceeds - a node physically moved
        // onto a spine nothing ever triggers (not the root spine, not jumped to by any Exec target)
        // never gets visited here, exactly like the interpreter's own m_bHasRun would never get set
        // for it - CppInputExpr checks this before referencing a wired source's variable, instead of
        // assuming every link's source is guaranteed to have been declared somewhere.
        std::set<std::uint64_t> EmittedNodeIds;
        if (RootSpineId != 0)
            EmitSpineRange(RootSpineId, 0, INT_MAX, Nodes, Links, LiteralValues, EmittedNodeIds, MainBody, FunctionDefs);
        return "// Auto-generated by Node OS codegen - do not hand-edit\n#include <cstdio>\n\n"
             + FunctionDefs
             + "static void RunMain()\n{\n" + MainBody + "}\n\nint main()\n{\n    RunMain();\n    return 0;\n}\n";
    }

    // Writes the generated source to disk, compiles it into a genuinely standalone .exe (no /LD,
    // no PCH, no SDK include paths at all - unlike a plugin, generated code has zero dependency on
    // xnode_os_plugin_api.h or anything else in this project, which is the whole point of "real
    // native codegen"), then runs it and captures its actual stdout - the concrete, checkable proof
    // that the pipeline produces a real program with the expected behavior, not just plausible-
    // looking text. Reuses GetOrBuildVsEnvSetup/CompilerInvocationMutex, the exact same toolchain
    // plumbing plugin compiles already use.
    struct codegen_run_result { bool m_bCompileOk = false; bool m_bRanOk = false; std::string m_CompileLog; std::string m_RunOutput; std::string m_SourcePath; };
    static codegen_run_result CompileAndRunGeneratedCpp(const std::string& Source)
    {
        codegen_run_result Result;
        namespace fs = std::filesystem;
        const fs::path OutputDir = "D:/LIONant/xGPU/source/Examples/E27_NodeOS/CompiledPlugins";
        std::error_code Ec;
        fs::create_directories(OutputDir, Ec);

        const fs::path SrcPath = OutputDir / "_generated_program.cpp";
        const fs::path ExePath = OutputDir / "_generated_program.exe";
        const fs::path BatPath = OutputDir / "_generated_program_compile.bat";
        const fs::path LogPath = OutputDir / "_generated_program_compile.log";
        const fs::path RunOutPath = OutputDir / "_generated_program_run.log";
        Result.m_SourcePath = SrcPath.string();

        { std::ofstream Src(SrcPath); Src << Source; }
        {
            std::ofstream Bat(BatPath);
            Bat << "@echo off\r\n";
            Bat << GetOrBuildVsEnvSetup();
            // No /LD (a real EXE, not a DLL), no /Yu/FI/Fp (no PCH - generated code only ever
            // includes plain standard headers), no /I at all (no SDK/xproperty dependency whatsoever
            // - the entire point of this being "real native codegen" rather than "another plugin").
            Bat << "cl.exe /nologo /EHsc /std:c++20 /MDd \"" << SrcPath.string() << "\""
                   " /Fe:\"" << ExePath.string() << "\" /Fo:\"" << (OutputDir / "_generated_program.obj").string()
                << "\" > \"" << LogPath.string() << "\" 2>&1\r\n";
        }

        int ExitCode;
        { std::lock_guard Lock(CompilerInvocationMutex()); ExitCode = std::system(std::format("\"{}\"", BatPath.string()).c_str()); }

        { std::ifstream LogFile(LogPath); std::stringstream S; S << LogFile.rdbuf(); Result.m_CompileLog = S.str(); }
        Result.m_bCompileOk = (ExitCode == 0) && fs::exists(ExePath);
        if (!Result.m_bCompileOk)
        {
            Result.m_CompileLog += std::format("\n[compile failed, exit code {}]", ExitCode);
            return Result;
        }

        // Run it for real and capture its actual stdout to a file, rather than just trusting that a
        // clean compile means correct behavior. Wrapped in one EXTRA outer quote pair (on top of the
        // two inner pairs around each path) - cmd.exe's "strip the outer quotes" rule only fires when
        // the whole command line is a single quoted token; with two separately-quoted paths plus a
        // redirection in between, an unwrapped line gets misparsed ("The filename, directory name, or
        // volume label syntax is incorrect") even though the command is well-formed and the actual
        // .exe runs fine standalone - this is the standard fix for cmd.exe /c with 2+ quoted paths.
        const std::string RunCommand = std::format("\"\"{}\" > \"{}\" 2>&1\"", ExePath.string(), RunOutPath.string());
        const int RunExitCode = std::system(RunCommand.c_str());
        { std::ifstream RunFile(RunOutPath); std::stringstream S; S << RunFile.rdbuf(); Result.m_RunOutput = S.str(); }
        Result.m_bRanOk = (RunExitCode == 0);
        return Result;
    }

    // The one on-screen surface for whatever a running program logs (see GetRuntimeLog/host_bridge
    // above) - Print's real output lands here. Cleared automatically at the start of every run
    // (RunProgram), not accumulated across runs, so each click shows exactly that run's own trace.
    static void DrawRuntimeLogPanel()
    {
        ImGui::SetNextWindowPos(ImVec2(1265, 440), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(200, 150), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Console"))
        {
            if (ImGui::SmallButton("Clear")) GetRuntimeLog().clear();
            ImGui::Separator();
            for (auto& Line : GetRuntimeLog())
                ImGui::TextUnformatted(Line.c_str());
        }
        ImGui::End();
    }

    static void ExecuteGraph(xgpu::device& Device, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, const std::vector<spine>& Spines, mesh_preview_system& MeshPreview, const std::unordered_map<std::uint64_t, std::string>& LiteralValues)
    {
        for (auto& Node : Nodes)
        {
            if (Node.m_bHasRun && Node.m_pNode)
                Node.m_pNode->FreeOutputs(Node.m_CachedOutputs.data());
            Node.m_bHasRun = false;
            Node.m_LastError.clear();
            Node.m_CachedOutputs.clear();
        }

        // Lives for the rest of this call, including the mesh-preview pass below (which resolves
        // input values again via its own GetInputValue calls) - see literal_storage's own comment.
        literal_storage LiteralScratch;
        RunProgram(Nodes, Links, Spines, LiteralValues, LiteralScratch);

        // "End" markers are deliberately never marked m_bHasRun by RunSpineRange (there's nothing to
        // run - they're a pure boundary) - excluded here so a working, correctly-skipped marker
        // doesn't get flagged as an error alongside genuinely unreached content.
        for (auto& Node : Nodes)
            if (!Node.m_bHasRun && Node.m_pNode && Node.m_pNode->m_pFactory->getName() != "End")
                Node.m_LastError = "not reached this run - unconnected to the main exec/spine flow";

        // Rebuild the GPU mesh preview for every pin currently carrying a "Mesh" value - both a
        // producer's output (Cube) and a consumer's input (Inspect Mesh) get a live render.
        for (auto& Node : Nodes)
        {
            if (!Node.m_pNode) continue;
            const auto NodeOutputs = Node.m_pNode->getOutputs();
            const auto NodeInputs  = Node.m_pNode->getInputs();
            for (int i = 0; i < (int)NodeOutputs.size(); ++i)
            {
                void* pValue = (Node.m_bHasRun && i < (int)Node.m_CachedOutputs.size()) ? Node.m_CachedOutputs[i] : nullptr;
                MeshPreview.RebuildIfMesh(Device, OutPinOf(Node.m_Id, i), NodeOutputs[i].m_pTypeName, pValue);
            }
            for (int i = 0; i < (int)NodeInputs.size(); ++i)
            {
                MeshPreview.RebuildIfMesh(Device, InPinOf(Node.m_Id, i), NodeInputs[i].m_pTypeName, GetInputValue(Node.m_Id, i, Nodes, Links, LiteralValues, LiteralScratch));
            }
        }
    }

    //------------------------------------------------------------------------------------------------
    // Generic, host-owned property serialization - reads/writes a (Name, Kind, Value) row per member,
    // directly against the node's own REAL xproperty::type::object (getProperties(), inherited from
    // xproperty::base - see xnode_os_plugin_api.h's top comment for why a real object crossing the DLL
    // boundary is safe now). xtextfile only ever needs to know about the same fixed 5 atomic kinds
    // (FLOAT/INT/BOOL/STRING/ENUM) the property panel already draws with, and that vocabulary belongs
    // to the host ("the OS"), not to any individual plugin. Values round-trip as text (this engine's
    // text files are already documented as lossy for floats - see xtextfile.h's own top comment - an
    // accepted, existing tradeoff, not a new one). ENUM stores the underlying int value. Only atomic
    // ("var") members are serialized - COMPOUND/LIST members (e.g. Inspect Mesh's nested "Mesh" scope)
    // are skipped, matching this project's current scope; they're still drawn fine by the real
    // inspector, just not round-tripped through save/load or undo snapshots yet.
    //------------------------------------------------------------------------------------------------
    enum class property_kind : int { FLOAT = 0, INT = 1, BOOL = 2, STRING = 3, ENUM = 4 };

    // A single property reduced to its plain-text row shape - Name/Kind/Value, no storage-backend
    // opinion at all. This is the ONLY place that knows how to turn one xproperty member into text and
    // back; every serialization backend (the xtextfile-based graph file below, and the in-memory one
    // used for xundo snapshots) is a thin wrapper around these two functions.
    struct property_row { std::string m_Name; int m_Kind; std::string m_Value; };

    static void NarrowInto(const std::wstring& W, std::string& Out) noexcept
    {
        Out.clear();
        if (W.empty()) return;
        const int Needed = WideCharToMultiByte(CP_UTF8, 0, W.c_str(), (int)W.size(), nullptr, 0, nullptr, nullptr);
        if (Needed <= 0) return;
        Out.resize((std::size_t)Needed);
        WideCharToMultiByte(CP_UTF8, 0, W.c_str(), (int)W.size(), Out.data(), Needed, nullptr, nullptr);
    }
    static std::wstring WidenFromUtf8(const std::string& S) noexcept
    {
        if (S.empty()) return {};
        const int Needed = MultiByteToWideChar(CP_UTF8, 0, S.c_str(), (int)S.size(), nullptr, 0);
        if (Needed <= 0) return {};
        std::wstring W((std::size_t)Needed, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, S.c_str(), (int)S.size(), W.data(), Needed);
        return W;
    }

    static int ReadEnumAsInt(const xproperty::type::members& Member, void* pInstance) noexcept
    {
        xproperty::any Out; xproperty::settings::context Ctx;
        if (!Member.TryRead(pInstance, Out, Ctx) || !Out.m_pType) return 0;
        // xproperty::any stores an enum member as its EXACT declared enum type (atomic_type = T,
        // per xproperty.h's var_type<T>), never converted to a plain int/unsigned/int64/uint64 - so
        // probing tryGet<those>() always misses regardless of the real value, silently falling
        // through to 0 (which then always displays as the enum's first registered item). Read the
        // raw bytes by width instead, exactly like the official xproperty inspector's own generic
        // enum reader does (xPropertyImGuiInspector.cpp's draw_enums) - it doesn't know the concrete
        // enum type either, only its size.
        switch (Out.m_pType->m_Size)
        {
            case 1: return *reinterpret_cast<const std::uint8_t*>(&Out.m_Data);
            case 2: return *reinterpret_cast<const std::uint16_t*>(&Out.m_Data);
            case 4: return *reinterpret_cast<const std::uint32_t*>(&Out.m_Data);
            case 8: return (int)*reinterpret_cast<const std::uint64_t*>(&Out.m_Data);
        }
        return 0;
    }
    static void WriteEnumFromInt(const xproperty::type::members& Member, void* pInstance, int Value) noexcept
    {
        auto* pVar = std::get_if<xproperty::type::members::var>(&Member.m_Variant);
        if (!pVar) return;
        const auto Found = pVar->m_AtomicType.TryFindEnumByValue((std::uint64_t)Value);
        if (!Found) return; // not one of the enum's registered values - reject rather than write garbage
        xproperty::any In{ std::string(Found.value()->m_pName) }; xproperty::settings::context Ctx;
        (void)Member.TryWrite(pInstance, In, Ctx);
    }

    // A plain, by-name reflected bool read - e.g. ForEachLoop's own "ReadOnlyElement" checkbox,
    // consulted purely for DISPLAY (decorating the Element port's type label with const/& - see the
    // per-port draw loop) rather than through the general property-snapshot machinery, since this
    // isn't an edit/undo path, just a read.
    static bool ReadBoolProperty(const xnode_os_node* pNode, const char* pName, bool Default) noexcept
    {
        const xproperty::type::object* pObj = pNode->getProperties();
        if (!pObj) return Default;
        for (auto& M : pObj->m_Members)
            if (std::strcmp(M.m_pName, pName) == 0)
            {
                xproperty::any Out; xproperty::settings::context Ctx;
                if (M.TryRead(pNode, Out, Ctx) && Out.is<bool>()) return Out.get<bool>();
            }
        return Default;
    }

    static bool ReflectedMemberToRow(const xproperty::type::members& Member, void* pInstance, property_row& OutRow)
    {
        auto* pVar = std::get_if<xproperty::type::members::var>(&Member.m_Variant);
        if (!pVar) return false; // COMPOUND/LIST - not serialized generically, see comment above

        OutRow.m_Name = Member.m_pName;
        if (pVar->m_AtomicType.m_IsEnum)
        {
            OutRow.m_Kind  = (int)property_kind::ENUM;
            OutRow.m_Value = std::format("{}", ReadEnumAsInt(Member, pInstance));
            return true;
        }

        xproperty::any Out; xproperty::settings::context Ctx;
        if (!Member.TryRead(pInstance, Out, Ctx)) return false;

        if      (Out.is<float>())        { OutRow.m_Kind = (int)property_kind::FLOAT;  OutRow.m_Value = std::format("{}", Out.get<float>()); }
        else if (Out.is<int>())          { OutRow.m_Kind = (int)property_kind::INT;    OutRow.m_Value = std::format("{}", Out.get<int>()); }
        else if (Out.is<bool>())         { OutRow.m_Kind = (int)property_kind::BOOL;   OutRow.m_Value = Out.get<bool>() ? "1" : "0"; }
        else if (Out.is<std::string>())  { OutRow.m_Kind = (int)property_kind::STRING; OutRow.m_Value = Out.get<std::string>(); }
        else if (Out.is<std::wstring>()) { OutRow.m_Kind = (int)property_kind::STRING; NarrowInto(Out.get<std::wstring>(), OutRow.m_Value); }
        else return false; // an atomic type outside the fixed vocabulary - skip, not an error

        return true;
    }

    // Looks the member up BY NAME (not by the row's original index) so a property added, removed, or
    // reordered on the plugin's struct since the row was produced doesn't silently misassign a value.
    static void ApplyRowToMember(const xproperty::type::object* pObj, void* pInstance, const property_row& Row)
    {
        const xproperty::type::members* pFound = nullptr;
        for (auto& M : pObj->m_Members) if (Row.m_Name == M.m_pName) { pFound = &M; break; }
        if (!pFound) return; // property no longer exists on this type - skip, don't fail the whole caller

        xproperty::settings::context Ctx;
        switch (static_cast<property_kind>(Row.m_Kind))
        {
            case property_kind::ENUM:  WriteEnumFromInt(*pFound, pInstance, std::stoi(Row.m_Value)); return;
            case property_kind::FLOAT: { xproperty::any In{ std::stof(Row.m_Value) };  (void)pFound->TryWrite(pInstance, In, Ctx); return; }
            case property_kind::INT:   { xproperty::any In{ std::stoi(Row.m_Value) };  (void)pFound->TryWrite(pInstance, In, Ctx); return; }
            case property_kind::BOOL:  { xproperty::any In{ Row.m_Value == "1" };      (void)pFound->TryWrite(pInstance, In, Ctx); return; }
            case property_kind::STRING:
            {
                auto* pVar = std::get_if<xproperty::type::members::var>(&pFound->m_Variant);
                if (pVar && pVar->m_AtomicType.m_GUID == xproperty::type::atomic_v<std::wstring>.m_GUID)
                {
                    xproperty::any In{ WidenFromUtf8(Row.m_Value) }; (void)pFound->TryWrite(pInstance, In, Ctx);
                }
                else
                {
                    xproperty::any In{ Row.m_Value }; (void)pFound->TryWrite(pInstance, In, Ctx);
                }
                return;
            }
        }
    }

    // Only atomic ("var") members are serializable - see this block's top comment.
    static std::vector<std::uint32_t> SerializableMemberIndices(const xproperty::type::object* pObj)
    {
        std::vector<std::uint32_t> Out;
        for (std::uint32_t i = 0; i < pObj->m_Members.size(); ++i)
            if (std::get_if<xproperty::type::members::var>(&pObj->m_Members[i].m_Variant)) Out.push_back(i);
        return Out;
    }

    static bool HasSerializableProperties(xnode_os_node* pNode)
    {
        return pNode && !SerializableMemberIndices(pNode->getProperties()).empty();
    }

    // Thin xtextfile-backed wrapper around the row conversion above.
    static bool SerializeReflectedMembers(xtextfile::stream& Stream, xnode_os_node* pNode)
    {
        const xproperty::type::object* pObj = pNode->getProperties();
        const auto Indices = SerializableMemberIndices(pObj);
        if (auto Err = Stream.Record("xProperties"
            , [&](std::size_t& C, xerr&) { if (!Stream.isReading()) C = Indices.size(); }
            , [&](std::size_t i, xerr& Error)
            {
                property_row Row;

                // On write, i indexes Indices (the filtered, serializable subset) in order. On read,
                // i is just this row's position in the file - ApplyRowToMember looks the member up by
                // name instead.
                if (!Stream.isReading())
                    ReflectedMemberToRow(pObj->m_Members[Indices[i]], pNode, Row);

                0
                || (Error = Stream.Field("Name",  Row.m_Name))
                || (Error = Stream.Field("Kind",  Row.m_Kind))
                || (Error = Stream.Field("Value", Row.m_Value));
                if (Error) return;

                if (Stream.isReading())
                    ApplyRowToMember(pObj, pNode, Row);
            }
        ); Err)
        {
            return false;
        }
        return true;
    }

    // In-memory, xtextfile-free snapshot of a whole properties block - for xundo command payloads,
    // where a plain string is exactly what's wanted (xundo commands ARE strings) and pulling in a
    // file-stream abstraction for something that never touches disk would be the wrong tool. One line
    // per member, tab-separated (property names/values here are plain identifiers/numbers/paths, never
    // tabs or newlines, so no escaping is needed - unlike the general text-file format).
    static std::string SerializePropertiesToString(xnode_os_node* pNode)
    {
        const xproperty::type::object* pObj = pNode->getProperties();
        std::string Out;
        for (auto Idx : SerializableMemberIndices(pObj))
        {
            property_row Row;
            if (!ReflectedMemberToRow(pObj->m_Members[Idx], pNode, Row)) continue;
            Out += std::format("{}\t{}\t{}\n", Row.m_Name, Row.m_Kind, Row.m_Value);
        }
        return Out;
    }

    static void ApplyPropertiesFromString(xnode_os_node* pNode, const std::string& Snapshot)
    {
        const xproperty::type::object* pObj = pNode->getProperties();
        std::size_t Pos = 0;
        while (Pos < Snapshot.size())
        {
            const std::size_t LineEnd = Snapshot.find('\n', Pos);
            const std::string Line    = Snapshot.substr(Pos, (LineEnd == std::string::npos ? Snapshot.size() : LineEnd) - Pos);
            Pos = (LineEnd == std::string::npos) ? Snapshot.size() : LineEnd + 1;
            if (Line.empty()) continue;

            const std::size_t Tab1 = Line.find('\t');
            const std::size_t Tab2 = Line.find('\t', Tab1 == std::string::npos ? std::string::npos : Tab1 + 1);
            if (Tab1 == std::string::npos || Tab2 == std::string::npos) continue; // malformed row - skip

            ApplyRowToMember(pObj, pNode, property_row
            { .m_Name  = Line.substr(0, Tab1)
            , .m_Kind  = std::stoi(Line.substr(Tab1 + 1, Tab2 - Tab1 - 1))
            , .m_Value = Line.substr(Tab2 + 1)
            });
        }
    }

    //------------------------------------------------------------------------------------------------
    // Save/load the whole graph (nodes + their properties + links) as a plain xtextfile - the same
    // text-file convention every other engine tool uses.
    //
    // File shape - each Record() call is its own separate, sequential top-level record (xtextfile has
    // no notion of nesting one record inside another's per-row callback), so a node's property block is
    // written/read as its own "xProperties" record (via SerializeReflectedMembers, above) immediately
    // after the "Nodes" record, once per node that has one, in the same Nodes-array order on both sides:
    //   Record "Nodes": Id, Source (plugin's Plugins/<DirName>/ folder name, not a full path), Type (name), Order, HasProperties
    //   [ one "xProperties" record per node with HasProperties==true, in Nodes order ]
    //   Record "Links" : Id, SourceNode, SourceOutput, TargetNode, TargetInput
    //
    // HasProperties is recorded explicitly (not re-derived from the type on load) because it must stay
    // correct even when a node's plugin source can no longer be resolved at load time - otherwise the
    // reader would have no way to know whether to expect (and thus stay aligned past) that node's
    // "xProperties" record, and every property record after it in the file would silently desync.
    //------------------------------------------------------------------------------------------------
    static bool SaveGraph(const std::string& Utf8Path, const std::vector<node_instance>& Nodes, const std::vector<link_instance>& Links, const std::vector<available_node_type>& AvailableTypes
                         , const std::vector<spine>& Spines, const std::vector<column>& Columns
                         , const std::unordered_map<std::uint64_t, std::string>& LiteralValues)
    {
        const std::wstring WPath(Utf8Path.begin(), Utf8Path.end()); // ASCII-safe path is all this demo needs

        xtextfile::stream Stream;
        if (auto Err = Stream.Open(false, WPath, xtextfile::file_type::TEXT); Err)
        {
            Debugger(std::format("Node OS: failed to open '{}' for saving", Utf8Path));
            return false;
        }

        const auto FindSourcePath = [&](const xnode_os_node_factory* pFactory) -> std::string
        {
            // The plugin's DIRECTORY NAME, not its absolute .cpp path (kept as "Source" in the field
            // name/comment for continuity, but see plugin_source_entry's own comment on why a folder
            // name is the actual identity) - stays meaningful if the repo ever moves and matches what
            // AddNode/DeleteNodes commands already use.
            for (auto& T : AvailableTypes) if (T.m_pFactory == pFactory) return T.m_DirName;
            return {};
        };

        if (auto Err = Stream.Record("Nodes"
            , [&](std::size_t& C, xerr&) { C = Nodes.size(); }
            , [&](std::size_t i, xerr& Error)
            {
                auto&         N             = Nodes[i];
                std::uint64_t Id            = N.m_Id;
                std::string   Source        = N.m_pNode ? FindSourcePath(N.m_pNode->m_pFactory) : std::string{};
                std::string   TypeName      = N.m_pNode ? std::string(N.m_pNode->m_pFactory->getName()) : "";
                int           Order         = N.m_Order;
                std::uint64_t SpineId       = N.m_SpineId;
                // The owner->marker relationship (If/ForEachLoop -> its own End/End-Else) - 0 if this
                // node doesn't own one. Without this, every save/load round-trip silently flattened
                // the whole graph's nesting: a reloaded owner would show as owning nothing, which
                // desyncs cascading delete/drag/select AND (once IsDataLinkScopeValid existed) makes
                // every node look like unnested "world scope", since ComputeEnclosingChains/
                // ComputeScopeDepths both derive nesting purely from this one field.
                std::uint64_t OwnedEndId    = N.m_OwnedEndId;
                // Not merely "does the node have properties at all" - a property struct whose every
                // member falls outside the serializable atomic vocabulary would exist but reflect zero
                // rows, and SerializeReflectedMembers would then write no "xProperties" record at all,
                // desyncing the reader if HasProperties still claimed one was coming.
                bool          HasProperties = HasSerializableProperties(N.m_pNode);

                0
                || (Error = Stream.Field("Id",            Id))
                || (Error = Stream.Field("Source",        Source))
                || (Error = Stream.Field("Type",          TypeName))
                || (Error = Stream.Field("Order",         Order))
                || (Error = Stream.Field("SpineId",       SpineId))
                || (Error = Stream.Field("OwnedEndId",    OwnedEndId))
                || (Error = Stream.Field("HasProperties", HasProperties));
            }
        ); Err)
        {
            Debugger("Node OS: failed writing Nodes record");
            return false;
        }

        for (auto& N : Nodes)
        {
            if (HasSerializableProperties(N.m_pNode))
            {
                if (!SerializeReflectedMembers(Stream, N.m_pNode))
                {
                    Debugger(std::format("Node OS: failed writing properties for node {}", N.m_Id));
                    return false;
                }
            }
        }

        if (auto Err = Stream.Record("Links"
            , [&](std::size_t& C, xerr&) { C = Links.size(); }
            , [&](std::size_t i, xerr& Error)
            {
                auto&         L            = Links[i];
                std::uint64_t Id           = L.m_Id;
                std::uint64_t SourceNode   = L.m_SourceNode;
                int           SourceOutput = L.m_SourceOutput;
                std::uint64_t TargetNode   = L.m_TargetNode;
                int           TargetInput  = L.m_TargetInput;

                0
                || (Error = Stream.Field("Id",           Id))
                || (Error = Stream.Field("SourceNode",   SourceNode))
                || (Error = Stream.Field("SourceOutput", SourceOutput))
                || (Error = Stream.Field("TargetNode",   TargetNode))
                || (Error = Stream.Field("TargetInput",  TargetInput));
            }
        ); Err)
        {
            Debugger("Node OS: failed writing Links record");
            return false;
        }

        if (auto Err = Stream.Record("Columns"
            , [&](std::size_t& C, xerr&) { C = Columns.size(); }
            , [&](std::size_t i, xerr& Error)
            {
                auto&         Co      = Columns[i];
                std::uint64_t Id      = Co.m_Id;
                std::uint64_t LeftId  = Co.m_LeftId;
                std::uint64_t RightId = Co.m_RightId;
                bool          IsRoot  = Co.m_bIsRoot;

                0
                || (Error = Stream.Field("Id",      Id))
                || (Error = Stream.Field("LeftId",  LeftId))
                || (Error = Stream.Field("RightId", RightId))
                || (Error = Stream.Field("IsRoot",  IsRoot));
            }
        ); Err)
        {
            Debugger("Node OS: failed writing Columns record");
            return false;
        }

        if (auto Err = Stream.Record("Spines"
            , [&](std::size_t& C, xerr&) { C = Spines.size(); }
            , [&](std::size_t i, xerr& Error)
            {
                auto&         Sp       = Spines[i];
                std::uint64_t Id       = Sp.m_Id;
                std::uint64_t ColumnId = Sp.m_ColumnId;
                bool          IsRoot   = Sp.m_bIsRoot;
                float         Y        = Sp.m_Y;

                0
                || (Error = Stream.Field("Id",       Id))
                || (Error = Stream.Field("ColumnId", ColumnId))
                || (Error = Stream.Field("IsRoot",   IsRoot))
                || (Error = Stream.Field("Y",        Y));
            }
        ); Err)
        {
            Debugger("Node OS: failed writing Spines record");
            return false;
        }

        // Inline literal values typed directly into unconnected Float/Int/Short pins (Unity-style
        // "no wire isn't no value") - previously never persisted at all despite being a real per-pin
        // setting, so every save/load round-trip silently discarded whatever was typed in. Stored
        // exactly as typed, keyed by the same PinId (InPinOf) the live editing session already uses.
        std::vector<std::pair<std::uint64_t, std::string>> LiteralVec(LiteralValues.begin(), LiteralValues.end());
        if (auto Err = Stream.Record("LiteralValues"
            , [&](std::size_t& C, xerr&) { C = LiteralVec.size(); }
            , [&](std::size_t i, xerr& Error)
            {
                std::uint64_t PinId = LiteralVec[i].first;
                std::string   Value = LiteralVec[i].second;
                0
                || (Error = Stream.Field("PinId", PinId))
                || (Error = Stream.Field("Value", Value));
            }
        ); Err)
        {
            Debugger("Node OS: failed writing LiteralValues record");
            return false;
        }

        return true;
    }

    // Mirrors SaveGraph. A node whose recorded Source/Type can no longer be resolved fails the WHOLE
    // load rather than silently skipping just that node: skipping it would still leave its
    // "xProperties" record (if HasProperties was true) sitting unread in the file, desyncing every
    // property record after it - a loud, whole-file failure beats a quietly corrupted partial load.
    static bool LoadGraph(const std::string& Utf8Path, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links
                         , std::vector<plugin_source_entry>& Sources, std::vector<available_node_type>& AvailableTypes
                         , std::vector<spine>& Spines, std::vector<column>& Columns
                         , std::unordered_map<std::uint64_t, std::string>& LiteralValues)
    {
        const std::wstring WPath(Utf8Path.begin(), Utf8Path.end());

        xtextfile::stream Stream;
        if (auto Err = Stream.Open(true, WPath, xtextfile::file_type::TEXT); Err)
        {
            Debugger(std::format("Node OS: failed to open '{}' for loading", Utf8Path));
            return false;
        }

        std::vector<node_instance> NewNodes;
        std::vector<link_instance> NewLinks;

        if (auto Err = Stream.Record("Nodes"
            , [&](std::size_t& C, xerr&) { NewNodes.reserve(C); }
            , [&](std::size_t, xerr& Error)
            {
                std::uint64_t Id = 0;
                std::string   Source, TypeName;
                int           Order = 0;
                std::uint64_t SpineId = 0;
                std::uint64_t OwnedEndId = 0;
                bool          HasProperties = false;

                if (0
                 || (Error = Stream.Field("Id",            Id))
                 || (Error = Stream.Field("Source",        Source))
                 || (Error = Stream.Field("Type",          TypeName))
                 || (Error = Stream.Field("Order",         Order))
                 || (Error = Stream.Field("SpineId",       SpineId)))
                    return;

                // Added after some graphs were already saved without it - tolerate its absence
                // (defaulting to "doesn't own a marker") instead of failing the whole load, since an
                // older file's header simply won't have this column yet.
                if (Error = Stream.Field("OwnedEndId", OwnedEndId); Error)
                {
                    if (Error.getState<xtextfile::state>() != xtextfile::state::FIELD_NOT_FOUND) return;
                    Error.clear();
                    OwnedEndId = 0;
                }

                if (Error = Stream.Field("HasProperties", HasProperties); Error)
                    return;

                auto SrcIt = std::find_if(Sources.begin(), Sources.end(), [&](auto& S) { return S.m_DirName == Source; });
                if (SrcIt == Sources.end())
                {
                    Error = xerr::create<xtextfile::state::FIELD_NOT_FOUND, "Node OS: a saved node's plugin source no longer exists">();
                    return;
                }

                auto* pFactory = EnsureLoadedAndGetType(*SrcIt, AvailableTypes);
                if (!pFactory || TypeName != pFactory->getName())
                {
                    Error = xerr::create<xtextfile::state::FIELD_NOT_FOUND, "Node OS: a saved node's type no longer matches its plugin source">();
                    return;
                }

                NewNodes.push_back(CreateNodeInstance(Id, pFactory, Order, SpineId));
                NewNodes.back().m_OwnedEndId = OwnedEndId;
            }
        ); Err)
        {
            Debugger("Node OS: failed reading Nodes record");
            for (auto& N : NewNodes) DestroyNodeInstance(N);
            return false;
        }

        for (auto& N : NewNodes)
        {
            if (HasSerializableProperties(N.m_pNode))
            {
                if (!SerializeReflectedMembers(Stream, N.m_pNode))
                {
                    Debugger(std::format("Node OS: failed reading properties for node {}", N.m_Id));
                    for (auto& M : NewNodes) DestroyNodeInstance(M);
                    return false;
                }
            }
        }

        if (auto Err = Stream.Record("Links"
            , [&](std::size_t& C, xerr&) { NewLinks.reserve(C); }
            , [&](std::size_t, xerr& Error)
            {
                std::uint64_t Id = 0, SourceNode = 0, TargetNode = 0;
                int           SourceOutput = 0, TargetInput = 0;

                if (0
                 || (Error = Stream.Field("Id",           Id))
                 || (Error = Stream.Field("SourceNode",   SourceNode))
                 || (Error = Stream.Field("SourceOutput", SourceOutput))
                 || (Error = Stream.Field("TargetNode",   TargetNode))
                 || (Error = Stream.Field("TargetInput",  TargetInput)))
                    return;

                NewLinks.push_back({ Id, SourceNode, SourceOutput, TargetNode, TargetInput });
            }
        ); Err)
        {
            Debugger("Node OS: failed reading Links record");
            for (auto& N : NewNodes) DestroyNodeInstance(N);
            return false;
        }

        // m_bReadOnly isn't persisted at all (not even as a column) - an ownership link's read-only-
        // ness is fully implied by whether some node's own m_OwnedEndId matches it, so re-deriving
        // here can never drift out of sync with the Nodes record the way storing it a second time
        // could. Without this, every save/load round-trip silently downgraded every owner<->End
        // ownership link to an ordinary, user-editable/deletable one - and, since bScopeInvalid's red
        // coloring explicitly skips read-only links, made them wrongly subject to that check too
        // (an End marker is always inside its owner's own ComputeScopeSpan by construction).
        for (auto& L : NewLinks)
            for (auto& N : NewNodes)
                if (N.m_Id == L.m_SourceNode && N.m_OwnedEndId == L.m_TargetNode) { L.m_bReadOnly = true; break; }

        std::vector<column> NewColumns;
        if (auto Err = Stream.Record("Columns"
            , [&](std::size_t& C, xerr&) { NewColumns.reserve(C); }
            , [&](std::size_t, xerr& Error)
            {
                column Co{};
                if (0
                 || (Error = Stream.Field("Id",      Co.m_Id))
                 || (Error = Stream.Field("LeftId",  Co.m_LeftId))
                 || (Error = Stream.Field("RightId", Co.m_RightId))
                 || (Error = Stream.Field("IsRoot",  Co.m_bIsRoot)))
                    return;
                NewColumns.push_back(Co);
            }
        ); Err)
        {
            Debugger("Node OS: failed reading Columns record");
            for (auto& N : NewNodes) DestroyNodeInstance(N);
            return false;
        }

        std::vector<spine> NewSpines;
        if (auto Err = Stream.Record("Spines"
            , [&](std::size_t& C, xerr&) { NewSpines.reserve(C); }
            , [&](std::size_t, xerr& Error)
            {
                spine Sp{};
                if (0
                 || (Error = Stream.Field("Id",       Sp.m_Id))
                 || (Error = Stream.Field("ColumnId", Sp.m_ColumnId))
                 || (Error = Stream.Field("IsRoot",   Sp.m_bIsRoot))
                 || (Error = Stream.Field("Y",        Sp.m_Y)))
                    return;
                NewSpines.push_back(Sp);
            }
        ); Err)
        {
            Debugger("Node OS: failed reading Spines record");
            for (auto& N : NewNodes) DestroyNodeInstance(N);
            return false;
        }

        // Cross-reference validation: every SpineId a node claims, every ColumnId a spine claims, and
        // every Left/RightId a column claims must resolve among the freshly-loaded sets - a dangling
        // reference fails the WHOLE load, same policy as the "plugin source no longer exists" check
        // above (a loud failure beats a quietly corrupted graph).
        {
            auto HasSpine  = [&](std::uint64_t Id) { return std::any_of(NewSpines.begin(),  NewSpines.end(),  [&](auto& Sp) { return Sp.m_Id == Id; }); };
            auto HasColumn = [&](std::uint64_t Id) { return std::any_of(NewColumns.begin(), NewColumns.end(), [&](auto& Co) { return Co.m_Id == Id; }); };

            bool bValid = true;
            for (auto& N : NewNodes) if (!HasSpine(N.m_SpineId)) { bValid = false; break; }
            if (bValid) for (auto& Sp : NewSpines)
                if (!HasColumn(Sp.m_ColumnId)) { bValid = false; break; }
            if (bValid) for (auto& Co : NewColumns)
            {
                if (Co.m_LeftId  != 0 && !HasColumn(Co.m_LeftId))  { bValid = false; break; }
                if (Co.m_RightId != 0 && !HasColumn(Co.m_RightId)) { bValid = false; break; }
            }
            if (!bValid)
            {
                Debugger("Node OS: failed loading - a Spine/Column reference does not resolve");
                for (auto& N : NewNodes) DestroyNodeInstance(N);
                return false;
            }
        }

        // Optional, trailing, purely-additive record - an older graph.txt saved before inline
        // literals were persisted simply won't have it at all (EOF the moment Stream.Record looks for
        // it), which is a legitimate, expected case here, not corruption: unlike every record above,
        // nothing else references a PinId, so there's no dangling-reference risk in just defaulting to
        // "no literals" and moving on rather than failing the whole load.
        std::unordered_map<std::uint64_t, std::string> NewLiteralValues;
        Stream.Record("LiteralValues"
            , [&](std::size_t& C, xerr&) { }
            , [&](std::size_t, xerr& Error)
            {
                std::uint64_t PinId = 0;
                std::string   Value;
                if (0
                 || (Error = Stream.Field("PinId", PinId))
                 || (Error = Stream.Field("Value", Value)))
                    return;
                NewLiteralValues[PinId] = Value;
            }
        );

        for (auto& N : Nodes) DestroyNodeInstance(N);
        Nodes          = std::move(NewNodes);
        Links          = std::move(NewLinks);
        Spines         = std::move(NewSpines);
        Columns        = std::move(NewColumns);
        LiteralValues  = std::move(NewLiteralValues);

        return true;
    }

    static void DrawNodePropertiesEmptyState(const char* pMessage)
    {
        ImGui::SetNextWindowPos(ImVec2(1265, 90), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Node Properties"))
            ImGui::TextDisabled("%s", pMessage);
        ImGui::End();
    }

    //------------------------------------------------------------------------------------------------
    // Dockable panel for the currently-selected node's properties - active only when exactly one node
    // is selected (multi-select property editing is out of scope for now). Draws with the HOST's own
    // real xproperty::inspector over the node's own real getProperties() object, uniformly for every
    // node type - no plugin ever needs to compile its own copy of ImGui/xPropertyImGuiInspector.cpp or
    // implement its own drawing function just to show a property panel (see xnode_os_plugin_api.h's
    // top comment for why a real xproperty::type::object crossing the DLL boundary is safe now).
    //
    // The Inspector instance itself MUST persist across frames rather than being rebuilt from scratch
    // on every call - see cube_node.cpp's old DrawProperties comment (from before this became the
    // host's job) for the full explanation: Show() seeds each row's ImGui id partly from the address
    // of its component-list slot, so a fresh AppendEntity()/AppendEntityComponent() call every frame
    // makes every widget's id unstable, which looks exactly like "nothing happens when I click." Only
    // rebuild when a different node gets selected.
    //------------------------------------------------------------------------------------------------
    // A bool property serializes as "1"/"0" (see ReflectedMemberToRow) - a plain, generic scrape
    // over the same Name\tKind\tValue\n rows SerializePropertiesToString already produces, so this
    // needs no knowledge of any specific node type's own C++ layout (the host never casts a plugin's
    // xnode_os_node* to its own concrete type - see xnode_os_plugin_api.h's cross-DLL note).
    static bool ReadBoolPropertyFromSnapshot(const std::string& Snapshot, std::string_view Name) noexcept
    {
        std::size_t Pos = 0;
        while (Pos < Snapshot.size())
        {
            const std::size_t LineEnd = Snapshot.find('\n', Pos);
            const std::string Line = Snapshot.substr(Pos, (LineEnd == std::string::npos ? Snapshot.size() : LineEnd) - Pos);
            Pos = (LineEnd == std::string::npos) ? Snapshot.size() : LineEnd + 1;
            const std::size_t Tab1 = Line.find('\t');
            if (Tab1 != std::string::npos && std::string_view(Line).substr(0, Tab1) == Name)
            {
                const std::size_t Tab2 = Line.find('\t', Tab1 + 1);
                if (Tab2 != std::string::npos) return Line.substr(Tab2 + 1) == "1";
            }
        }
        return false;
    }

    // Function's own pin-list encoding, decoded/re-encoded HOST-SIDE for the pin editor below - a
    // duplicate of function_node.cpp's identical private DecodePins, since plugin internals never
    // cross the DLL boundary (xnode_os_plugin_api.h's top comment); the host only ever reads/writes
    // the InputsSpec/OutputsSpec STRING PROPERTY through reflection, same as every other inline
    // widget in this file touches a node's properties.
    struct host_pin_spec { std::string m_Name, m_Type; bool m_bRequired = true; bool m_bReadOnly = true; };
    static std::vector<host_pin_spec> DecodeHostPinSpec(const std::string& Spec)
    {
        std::vector<host_pin_spec> Out;
        std::size_t Pos = 0;
        while (Pos < Spec.size())
        {
            const std::size_t Bar = Spec.find('|', Pos);
            const std::string Entry = Spec.substr(Pos, Bar == std::string::npos ? std::string::npos : Bar - Pos);
            const std::size_t C1 = Entry.find(':');
            const std::size_t C2 = (C1 == std::string::npos) ? std::string::npos : Entry.find(':', C1 + 1);
            const std::size_t C3 = (C2 == std::string::npos) ? std::string::npos : Entry.find(':', C2 + 1);
            if (C1 != std::string::npos && C2 != std::string::npos && C3 != std::string::npos)
            {
                host_pin_spec Pin;
                Pin.m_Name      = Entry.substr(0, C1);
                Pin.m_Type      = Entry.substr(C1 + 1, C2 - C1 - 1);
                Pin.m_bRequired = Entry[C2 + 1] == '1';
                Pin.m_bReadOnly = Entry[C3 + 1] == '1';
                Out.push_back(std::move(Pin));
            }
            if (Bar == std::string::npos) break;
            Pos = Bar + 1;
        }
        return Out;
    }
    static std::string EncodeHostPinSpec(const std::vector<host_pin_spec>& Pins)
    {
        std::string Out;
        for (auto& P : Pins)
        {
            if (!Out.empty()) Out += '|';
            Out += P.m_Name; Out += ':'; Out += P.m_Type; Out += ':';
            Out += (P.m_bRequired ? '1' : '0'); Out += ':'; Out += (P.m_bReadOnly ? '1' : '0');
        }
        return Out;
    }
    static constexpr const char* s_FunctionPinTypes[] = { "Float", "Int", "Short", "Bool", "Any", "Span<Any>" };

    // Draws one Add/Remove/edit pin table (Inputs or Outputs) for a Function node, directly in the
    // properties panel - Function's port COUNT is user-editable, unlike every other node type here,
    // so it needs real table UI rather than the single inline widget Constant/Compare use for their
    // one fixed slot. Every edit re-encodes the whole spec string and commits it through the same
    // undo-safe SetProperties command the rest of this panel uses - the node's own getInputs()/
    // getOutputs() derive the local-mirrored pins from this same spec directly (see
    // function_node.cpp), so there's no separate instance to keep in sync anymore.
    static void DrawFunctionPinEditor(xundo::system& System, xnode_os_node* pFnNode, std::uint64_t FnNodeId, const char* pSpecMemberName, const char* pLabel)
    {
        const xproperty::type::object* pObj = pFnNode->getProperties();
        const xproperty::type::members* pSpecMember = nullptr;
        for (auto& M : pObj->m_Members) if (std::strcmp(M.m_pName, pSpecMemberName) == 0) { pSpecMember = &M; break; }
        if (!pSpecMember) return;

        xproperty::any SpecOut; xproperty::settings::context ReadCtx;
        std::string SpecText;
        if (pSpecMember->TryRead(pFnNode, SpecOut, ReadCtx) && SpecOut.is<std::string>())
            SpecText = SpecOut.get<std::string>();
        auto Pins = DecodeHostPinSpec(SpecText);

        auto Commit = [&](std::vector<host_pin_spec>& NewPins)
        {
            const std::string Before = SerializePropertiesToString(pFnNode);
            xproperty::any In{ EncodeHostPinSpec(NewPins) }; xproperty::settings::context WriteCtx;
            (void)pSpecMember->TryWrite(pFnNode, In, WriteCtx);
            const std::string After = SerializePropertiesToString(pFnNode);
            if (After != Before)
                commands::Run(System, commands::MakeSetProperties(FnNodeId, Before, After));
        };

        ImGui::TextUnformatted(pLabel);
        int RemoveIndex = -1;
        for (int i = 0; i < (int)Pins.size(); ++i)
        {
            ImGui::PushID(i);
            auto& Pin = Pins[i];
            char NameBuf[64]; strncpy_s(NameBuf, Pin.m_Name.c_str(), _TRUNCATE);
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::InputText("##name", NameBuf, sizeof(NameBuf)))
            {
                Pin.m_Name = NameBuf;
                Commit(Pins);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::BeginCombo("##type", Pin.m_Type.c_str()))
            {
                for (auto* pTypeName : s_FunctionPinTypes)
                {
                    const bool bSel = Pin.m_Type == pTypeName;
                    if (ImGui::Selectable(pTypeName, bSel)) { Pin.m_Type = pTypeName; Commit(Pins); }
                    if (bSel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            bool bReq = Pin.m_bRequired;
            if (ImGui::Checkbox("Req", &bReq)) { Pin.m_bRequired = bReq; Commit(Pins); }
            ImGui::SameLine();
            bool bRO = Pin.m_bReadOnly;
            if (ImGui::Checkbox("RO", &bRO)) { Pin.m_bReadOnly = bRO; Commit(Pins); }
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) RemoveIndex = i;
            ImGui::PopID();
        }
        if (RemoveIndex >= 0)
        {
            Pins.erase(Pins.begin() + RemoveIndex);
            Commit(Pins);
        }
        if (ImGui::SmallButton(std::format("+ Add {}", pLabel).c_str()))
        {
            Pins.push_back({ std::format("{}{}", pLabel[0], Pins.size() + 1), "Float", true, true });
            Commit(Pins);
        }
    }

    static void DrawNodePropertiesPanel(std::vector<node_instance>& Nodes, const std::set<std::uint64_t>& SelectedNodes, xundo::system& System, std::vector<plugin_source_entry>& Sources, std::vector<available_node_type>& AvailableTypes)
    {
        // A control node and its owned End/End-Else marker(s) select as one compound group now (see
        // the click-selection cascade in DrawGraphCanvas) - so "exactly one node selected" no longer
        // holds for them. If this selection is exactly one such group, resolve its root (the one
        // member nobody ELSE in the group owns - rules out an EndElse marker, which is itself pointed
        // to by the owner, in favor of the actual If/ForEachLoop) and show properties for THAT,
        // merged with its owned marker's own properties (just "IsElse" today) - the whole group reads
        // as one node to the user (NODE_SCRIPTING_DESIGN.md section 4.1), so an edit like the Else
        // toggle belongs on the same panel as the owner, even though it's still physically stored on
        // the End marker itself (getOutputs()'s dynamic "ElseEnd" pin has to read its own instance's
        // field - see end_marker_node.cpp).
        std::uint64_t PrimaryId = 0;
        if (SelectedNodes.size() == 1)
        {
            PrimaryId = *SelectedNodes.begin();
        }
        else
        {
            for (auto Id : SelectedNodes)
            {
                const auto Group = commands::ExpandOwnershipCascade(Nodes, { Id });
                if (std::set<std::uint64_t>(Group.begin(), Group.end()) != SelectedNodes) continue;
                const bool bOwnedByOther = std::any_of(Nodes.begin(), Nodes.end(), [&](auto& N) { return N.m_OwnedEndId == Id; });
                if (!bOwnedByOther) { PrimaryId = Id; break; }
            }
        }
        if (PrimaryId == 0)
        {
            DrawNodePropertiesEmptyState(SelectedNodes.empty() ? "Select a node to see its properties." : "Select a single node to see its properties.");
            return;
        }

        auto It = std::find_if(Nodes.begin(), Nodes.end(), [&](auto& N) { return N.m_Id == PrimaryId; });
        if (It == Nodes.end() || !It->m_pNode) { DrawNodePropertiesEmptyState("(node no longer exists)"); return; }

        xnode_os_node* pNode = It->m_pNode;
        const std::uint64_t NodeId = It->m_Id;

        xnode_os_node* pMarkerNode = nullptr;
        std::uint64_t  MarkerNodeId = 0;
        // The End/End-Else marker type is shared by every owner (If, ForEachLoop, ...), but "Else" -
        // a second branch - is only a meaningful concept for If. Merging it into a ForEachLoop's own
        // panel would show a checkbox that does nothing sensible there, so this stays scoped to the
        // one owner type it actually applies to (same name-based special-case this function already
        // used before the merge existed, for exactly the same reason).
        if (It->m_OwnedEndId != 0 && pNode->m_pFactory->getName() == "If")
        {
            auto MarkerIt = std::find_if(Nodes.begin(), Nodes.end(), [&](auto& N) { return N.m_Id == It->m_OwnedEndId; });
            if (MarkerIt != Nodes.end() && MarkerIt->m_pNode && HasSerializableProperties(MarkerIt->m_pNode))
            {
                pMarkerNode  = MarkerIt->m_pNode;
                MarkerNodeId = MarkerIt->m_Id;
            }
        }

        const bool bOwnHasProps = HasSerializableProperties(pNode);
        if (!bOwnHasProps && !pMarkerNode)
        {
            DrawNodePropertiesEmptyState(std::format("{} has no properties.", pNode->m_pFactory->getName()).c_str());
            return;
        }

        static xproperty::inspector s_Inspector("Node Properties");
        static void* s_pBoundNode       = nullptr;
        static void* s_pBoundMarkerNode = nullptr;
        if (s_pBoundNode != pNode || s_pBoundMarkerNode != pMarkerNode)
        {
            s_Inspector.clear();
            s_Inspector.AppendEntity();
            if (bOwnHasProps) s_Inspector.AppendEntityComponent(*pNode->getProperties(), pNode);
            if (pMarkerNode)  s_Inspector.AppendEntityComponent(*pMarkerNode->getProperties(), pMarkerNode);
            s_pBoundNode       = pNode;
            s_pBoundMarkerNode = pMarkerNode;
        }

        const std::string Before       = bOwnHasProps ? SerializePropertiesToString(pNode)       : std::string{};
        const std::string MarkerBefore = pMarkerNode  ? SerializePropertiesToString(pMarkerNode)  : std::string{};
        xproperty::settings::context Context;
        ImGui::SetNextWindowPos(ImVec2(1265, 90), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);
        s_Inspector.Show(Context, [] {});
        const std::string After       = bOwnHasProps ? SerializePropertiesToString(pNode)      : std::string{};
        const std::string MarkerAfter = pMarkerNode  ? SerializePropertiesToString(pMarkerNode) : std::string{};

        if (bOwnHasProps && After != Before)
            commands::Run(System, commands::MakeSetProperties(NodeId, Before, After));

        if (pMarkerNode && MarkerAfter != MarkerBefore)
        {
            // The End node's own "IsElse" checkbox (NODE_SCRIPTING_DESIGN.md section 4.2) is the one
            // property edit that means more than "just store this value" - it also creates or
            // removes a further, paired End marker. Detected generically off the serialized snapshot
            // rather than casting pMarkerNode to any concrete plugin type (never safe across the
            // plugin DLL boundary - see xnode_os_plugin_api.h).
            // SetProperties always runs FIRST: SetEndElseState's own Redo() reads the marker's live
            // getOutputs() to find its new "ElseEnd" pin index, which only reliably reflects
            // IsElse==true if SetProperties (whose Redo() actually applies the After snapshot via
            // ApplyPropertiesFromString) has already run - true during this initial edit only because
            // the ImGui widget mutated pMarkerNode directly, but not guaranteed during a later redo
            // replay unless the ordering is explicit here too.
            commands::Run(System, commands::MakeSetProperties(MarkerNodeId, MarkerBefore, MarkerAfter));

            const bool bWasElse = ReadBoolPropertyFromSnapshot(MarkerBefore, "IsElse");
            const bool bIsElse  = ReadBoolPropertyFromSnapshot(MarkerAfter,  "IsElse");
            if (bIsElse && !bWasElse)
            {
                auto* pEndSrc = commands::FindSourceByDirName(Sources, "End");
                if (pEndSrc && EnsureLoadedAndGetType(*pEndSrc, AvailableTypes))
                    commands::Run(System, commands::MakeSetEndElseEnable(MarkerNodeId, xresource::guid_generator::Instance64(), pEndSrc->m_DirName, xresource::guid_generator::Instance64()));
            }
            else if (!bIsElse && bWasElse)
                commands::Run(System, commands::MakeSetEndElseDisable(MarkerNodeId));
        }

        // Function's signature is user-editable (add/remove/rename/retype pins, toggle Required/
        // ReadOnly) - the raw InputsSpec/OutputsSpec text fields above are a harmless power-user
        // escape hatch (same "the encoded string is still just an ordinary property" spirit as
        // Constant's Value), but this table is the real, intended editing surface.
        if (pNode->m_pFactory->getName() == "Function")
        {
            ImGui::Separator();
            DrawFunctionPinEditor(System, pNode, NodeId, "InputsSpec",  "Inputs");
            ImGui::Separator();
            DrawFunctionPinEditor(System, pNode, NodeId, "OutputsSpec", "Outputs");
        }
    }

    //================================================================================================
    // Commands - every graph mutation becomes a string command executed through xundo::system::
    // Execute(), which has zero ImGui/xgpu dependency: the ImGui interaction code above builds a
    // command string and calls the exact same entry point a future headless runner or "command
    // source" driver plugin would call (see this file's top comment). Selection changes go through
    // this SAME history as data commands (explicit choice - Ctrl+Z steps back through selection
    // changes too, not just data edits).
    //================================================================================================
    namespace commands
    {
        // Base64Encode/Decode, JoinIds/SplitIds, FindSourceByDirName, WriteString/ReadString, and the
        // free Make*/Run helpers live EARLIER in this file (right after DestroyNodeInstance) - they
        // need to be visible to DrawGraphCanvas/DrawNodePropertiesPanel, which are defined before this
        // point, and ordinary single-pass C++ lookup means a name has to already be declared above the
        // point that uses it. The actual xundo::command_base-derived classes below stay here because
        // THEY need SerializePropertiesToString/ApplyPropertiesFromString, which aren't defined until
        // just above this point.

        //--------------------------------------------------------------------------------------------
        // The one "database" every command mutates, retrieved via command_base::get<node_os_command_
        // context>() - plain references into E27_Example()'s own locals, not owned here.
        //--------------------------------------------------------------------------------------------
        struct node_os_command_context
        {
            std::vector<node_instance>&        m_Nodes;
            std::vector<link_instance>&        m_Links;
            canvas_selection&                  m_Selection;
            std::vector<plugin_source_entry>&  m_Sources;
            std::vector<available_node_type>&  m_AvailableTypes;
            bool&                              m_bDirty;
            std::vector<spine>&                 m_Spines;
            std::vector<column>&                m_Columns;
            // Inline constants on an unconnected scalar input pin (Float/Int/Bool), Unity-style - "no
            // wire" no longer means "no value", it means "whatever's typed right here". Keyed by
            // InPinOf(NodeId, PinIndex); stored as plain text (same convention property_row already
            // uses) so one map covers every scalar type without a tagged union. Rendering-only for
            // now - not read by ExecuteGraph/the live data pipeline yet, only by the canvas widget.
            std::unordered_map<std::uint64_t, std::string>& m_LiteralValues;
        };

        // Shared by select_cmd and clear_selection_cmd - both snapshot/restore the exact same set.
        inline void BackupSelection(node_os_command_context& Ctx, xundo::undo_file& File) noexcept
        {
            auto& S = Ctx.m_Selection;
            File.Write(static_cast<std::uint32_t>(S.m_SelectedNodes.size()));
            for (auto Id : S.m_SelectedNodes) File.Write(Id);
            File.Write(S.m_SelectedLink);
            File.Write(S.m_SelectedGapSpineId);
            File.Write(S.m_SelectedGapIndex);
        }
        inline void RestoreSelection(node_os_command_context& Ctx, xundo::undo_file& File) noexcept
        {
            auto& S = Ctx.m_Selection;
            std::uint32_t Count = 0; File.Read(Count);
            S.m_SelectedNodes.clear();
            for (std::uint32_t i = 0; i < Count; ++i) { std::uint64_t Id = 0; File.Read(Id); S.m_SelectedNodes.insert(Id); }
            File.Read(S.m_SelectedLink);
            File.Read(S.m_SelectedGapSpineId);
            File.Read(S.m_SelectedGapIndex);
        }

        //================================================================================================
        // CreateNode - addressed relative to an EXISTING node's id (-After/-Before), never a raw order
        // index or an invented "gap" identity: see the design discussion this replaced (a two-command
        // "InsertNode" group keyed by a shifting numeric GapIndex) for why. Resolving -After/-Before
        // against the CURRENT node list happens once, right here, at Redo() time - so a stale reference
        // (the node no longer exists by the time this runs) fails cleanly instead of guessing.
        //================================================================================================
        struct create_node_cmd : xundo::command_base
        {
            create_node_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "CreateNode", pDataBase) { RegisterArguments(); }

            const char* getCommandHelp() const noexcept override { return "Creates a node. Usage: CreateNode -Id N -PluginDir dirname [-After id | -Before id | -InSpine spineid]"; }
            void RegisterArguments() noexcept override
            {
                m_hId        = m_Parser.addOption("Id",        "Node id",                                           true,  1);
                m_hPluginDir = m_Parser.addOption("PluginDir", "Plugin folder name under Plugins/ (e.g. CubeNode)",  true,  1);
                m_hAfter     = m_Parser.addOption("After",     "Insert right after this node id",                   false, 1);
                m_hBefore    = m_Parser.addOption("Before",    "Insert right before this node id - neither -After nor -Before means append at the end", false, 1);
                m_hInSpine   = m_Parser.addOption("InSpine",   "Append to this (currently empty) spine id - mutually exclusive with -After/-Before, the only way to place a node into a spine with no nodes yet", false, 1);
            }

            // Resolves -After/-Before/-InSpine (if given) against the CURRENT node/spine list into a
            // target spine id + a dense order index WITHIN THAT SPINE - shared by Redo (which needs it
            // to place the new node) and BackupCurrenState (which needs it to know the full pre-insert
            // layout for Undo).
            std::string ResolveTargetOrder(node_os_command_context& Ctx, int& OutTargetOrder, std::uint64_t& OutTargetSpineId) const noexcept
            {
                const bool bHasAfter   = m_Parser.hasOption(m_hAfter);
                const bool bHasBefore  = m_Parser.hasOption(m_hBefore);
                const bool bHasInSpine = m_Parser.hasOption(m_hInSpine);
                if ((bHasAfter ? 1 : 0) + (bHasBefore ? 1 : 0) + (bHasInSpine ? 1 : 0) > 1)
                    return "CreateNode: -After, -Before and -InSpine are mutually exclusive";

                if (bHasInSpine)
                {
                    auto RefArg = m_Parser.getOptionArgAs<std::string>(m_hInSpine, 0);
                    if (std::holds_alternative<xerr>(RefArg)) return "CreateNode: bad arguments";
                    const auto SpineId = ParseGuid(std::get<std::string>(RefArg));
                    bool bFound = false;
                    for (auto& S : Ctx.m_Spines) if (S.m_Id == SpineId) { bFound = true; break; }
                    if (!bFound) return "CreateNode: -InSpine spine no longer exists";
                    int Count = 0;
                    for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == SpineId) ++Count;
                    OutTargetOrder = Count; OutTargetSpineId = SpineId; return {};
                }

                if (!bHasAfter && !bHasBefore)
                {
                    // No placement given at all - append to the root spine, same as this command's
                    // behavior before spines existed.
                    for (auto& S : Ctx.m_Spines)
                        if (S.m_bIsRoot)
                        {
                            int Count = 0;
                            for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == S.m_Id) ++Count;
                            OutTargetOrder = Count; OutTargetSpineId = S.m_Id; return {};
                        }
                    return "CreateNode: no root spine exists";
                }

                auto RefArg = m_Parser.getOptionArgAs<std::string>(bHasAfter ? m_hAfter : m_hBefore, 0);
                if (std::holds_alternative<xerr>(RefArg)) return "CreateNode: bad arguments";
                const auto RefId = ParseGuid(std::get<std::string>(RefArg));

                std::uint64_t RefSpineId = 0; int RefOrder = 0;
                if (!ResolveNodeSpineAndOrder(Ctx.m_Nodes, RefId, RefSpineId, RefOrder)) return "CreateNode: -After/-Before node no longer exists";
                OutTargetSpineId = RefSpineId; OutTargetOrder = bHasAfter ? RefOrder + 1 : RefOrder; return {};
            }

            std::string Redo() noexcept override
            {
                auto Id        = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                auto PluginDir = m_Parser.getOptionArgAs<std::string>(m_hPluginDir, 0);
                if (std::holds_alternative<xerr>(Id) || std::holds_alternative<xerr>(PluginDir))
                    return "CreateNode: bad arguments";

                auto& Ctx = get<node_os_command_context>();
                int TargetOrder = 0; std::uint64_t TargetSpineId = 0;
                if (auto Err = ResolveTargetOrder(Ctx, TargetOrder, TargetSpineId); !Err.empty()) return Err;

                auto* pSrc = FindSourceByDirName(Ctx.m_Sources, std::get<std::string>(PluginDir));
                if (!pSrc) return "CreateNode: unknown plugin directory";
                auto* pType = EnsureLoadedAndGetType(*pSrc, Ctx.m_AvailableTypes);
                if (!pType) return "CreateNode: failed to compile/load plugin";

                for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == TargetSpineId && N.m_Order >= TargetOrder) ++N.m_Order;
                Ctx.m_Nodes.push_back(CreateNodeInstance(ParseGuid(std::get<std::string>(Id)), pType, TargetOrder, TargetSpineId));
                Ctx.m_bDirty = true;
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto Id = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                File.Write(std::holds_alternative<xerr>(Id) ? std::uint64_t{0} : ParseGuid(std::get<std::string>(Id)));

                auto& Ctx = get<node_os_command_context>();
                File.Write(static_cast<std::uint32_t>(Ctx.m_Nodes.size()));
                for (auto& N : Ctx.m_Nodes) { File.Write(N.m_Id); File.Write(N.m_Order); }
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                std::uint64_t Id = 0; File.Read(Id);
                auto& Ctx = get<node_os_command_context>();
                std::erase_if(Ctx.m_Links, [&](auto& L) { return L.m_SourceNode == Id || L.m_TargetNode == Id; });
                for (auto& N : Ctx.m_Nodes) if (N.m_Id == Id) DestroyNodeInstance(N);
                std::erase_if(Ctx.m_Nodes, [&](auto& N) { return N.m_Id == Id; });
                Ctx.m_Selection.m_SelectedNodes.erase(Id);

                std::uint32_t Count = 0; File.Read(Count);
                for (std::uint32_t i = 0; i < Count; ++i)
                {
                    std::uint64_t NId = 0; int Order = 0; File.Read(NId); File.Read(Order);
                    for (auto& N : Ctx.m_Nodes) if (N.m_Id == NId) { N.m_Order = Order; break; }
                }
                Ctx.m_bDirty = true;
            }

            xcmdline::parser::handle m_hId, m_hPluginDir, m_hAfter, m_hBefore, m_hInSpine;
        };

        //================================================================================================
        // CreateOwnedPair - creates a control node (If/ForEachLoop) together with its owned End/
        // End-Else marker in one command, the marker always landing right after the owner in the same
        // spine. Placement (-After/-Before/-InSpine) addresses the OWNER, exactly like CreateNode -
        // the marker's own position is never independently specified, since it isn't independently
        // meaningful (NODE_SCRIPTING_DESIGN.md section 4.1: the marker is non-detachable, created and
        // destroyed with its owner - see DeleteNodes' cascade for the other half of that invariant).
        //================================================================================================
        struct create_owned_pair_cmd : xundo::command_base
        {
            create_owned_pair_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "CreateOwnedPair", pDataBase) { RegisterArguments(); }

            const char* getCommandHelp() const noexcept override { return "Creates a control node with its owned End marker. Usage: CreateOwnedPair -Id N -PluginDir dirname -EndId N -EndPluginDir dirname [-After id | -Before id | -InSpine spineid]"; }
            void RegisterArguments() noexcept override
            {
                m_hId           = m_Parser.addOption("Id",           "Owner node id",                                     true,  1);
                m_hPluginDir    = m_Parser.addOption("PluginDir",    "Owner's plugin folder name",                        true,  1);
                m_hEndId        = m_Parser.addOption("EndId",        "Marker node id",                                    true,  1);
                m_hEndPluginDir = m_Parser.addOption("EndPluginDir", "Marker's plugin folder name",                       true,  1);
                m_hLinkId       = m_Parser.addOption("LinkId",       "Read-only owner<->End link id",                     true,  1);
                m_hAfter        = m_Parser.addOption("After",        "Insert the owner right after this node id",         false, 1);
                m_hBefore       = m_Parser.addOption("Before",       "Insert the owner right before this node id",        false, 1);
                m_hInSpine      = m_Parser.addOption("InSpine",      "Append the owner to this (currently empty) spine",  false, 1);
                // Optional 2nd hop - a minimal, non-generic extension rather than a full N-way chain,
                // since nothing needs more than 2 hops today (Function used to, before its owned
                // marker merged into itself). When given, EndId/EndPluginDir describe the MIDDLE node (owned by the owner,
                // itself owning End2Id) instead of the terminal marker.
                m_hEnd2Id        = m_Parser.addOption("End2Id",        "Second-level marker id, owned by the first marker - only when the first marker itself needs one", false, 1);
                m_hEnd2PluginDir = m_Parser.addOption("End2PluginDir", "Second-level marker's plugin folder name",                                                        false, 1);
                m_hLink2Id       = m_Parser.addOption("Link2Id",       "Read-only first-marker<->second-marker link id",                                                  false, 1);
            }

            // Identical placement logic to create_node_cmd::ResolveTargetOrder - duplicated rather than
            // shared, since the two commands' parser handles are distinct members.
            std::string ResolveTargetOrder(node_os_command_context& Ctx, int& OutTargetOrder, std::uint64_t& OutTargetSpineId) const noexcept
            {
                const bool bHasAfter   = m_Parser.hasOption(m_hAfter);
                const bool bHasBefore  = m_Parser.hasOption(m_hBefore);
                const bool bHasInSpine = m_Parser.hasOption(m_hInSpine);
                if ((bHasAfter ? 1 : 0) + (bHasBefore ? 1 : 0) + (bHasInSpine ? 1 : 0) > 1)
                    return "CreateOwnedPair: -After, -Before and -InSpine are mutually exclusive";

                if (bHasInSpine)
                {
                    auto RefArg = m_Parser.getOptionArgAs<std::string>(m_hInSpine, 0);
                    if (std::holds_alternative<xerr>(RefArg)) return "CreateOwnedPair: bad arguments";
                    const auto SpineId = ParseGuid(std::get<std::string>(RefArg));
                    bool bFound = false;
                    for (auto& S : Ctx.m_Spines) if (S.m_Id == SpineId) { bFound = true; break; }
                    if (!bFound) return "CreateOwnedPair: -InSpine spine no longer exists";
                    int Count = 0;
                    for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == SpineId) ++Count;
                    OutTargetOrder = Count; OutTargetSpineId = SpineId; return {};
                }

                if (!bHasAfter && !bHasBefore)
                {
                    for (auto& S : Ctx.m_Spines)
                        if (S.m_bIsRoot)
                        {
                            int Count = 0;
                            for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == S.m_Id) ++Count;
                            OutTargetOrder = Count; OutTargetSpineId = S.m_Id; return {};
                        }
                    return "CreateOwnedPair: no root spine exists";
                }

                auto RefArg = m_Parser.getOptionArgAs<std::string>(bHasAfter ? m_hAfter : m_hBefore, 0);
                if (std::holds_alternative<xerr>(RefArg)) return "CreateOwnedPair: bad arguments";
                const auto RefId = ParseGuid(std::get<std::string>(RefArg));

                std::uint64_t RefSpineId = 0; int RefOrder = 0;
                if (!ResolveNodeSpineAndOrder(Ctx.m_Nodes, RefId, RefSpineId, RefOrder)) return "CreateOwnedPair: -After/-Before node no longer exists";
                OutTargetSpineId = RefSpineId; OutTargetOrder = bHasAfter ? RefOrder + 1 : RefOrder; return {};
            }

            std::string Redo() noexcept override
            {
                auto Id           = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                auto PluginDir    = m_Parser.getOptionArgAs<std::string>(m_hPluginDir, 0);
                auto EndId        = m_Parser.getOptionArgAs<std::string>(m_hEndId, 0);
                auto EndPluginDir = m_Parser.getOptionArgAs<std::string>(m_hEndPluginDir, 0);
                auto LinkId       = m_Parser.getOptionArgAs<std::string>(m_hLinkId, 0);
                if (std::holds_alternative<xerr>(Id) || std::holds_alternative<xerr>(PluginDir) || std::holds_alternative<xerr>(EndId) || std::holds_alternative<xerr>(EndPluginDir) || std::holds_alternative<xerr>(LinkId))
                    return "CreateOwnedPair: bad arguments";

                auto& Ctx = get<node_os_command_context>();
                int TargetOrder = 0; std::uint64_t TargetSpineId = 0;
                if (auto Err = ResolveTargetOrder(Ctx, TargetOrder, TargetSpineId); !Err.empty()) return Err;

                auto* pOwnerSrc = FindSourceByDirName(Ctx.m_Sources, std::get<std::string>(PluginDir));
                if (!pOwnerSrc) return "CreateOwnedPair: unknown owner plugin directory";
                auto* pOwnerType = EnsureLoadedAndGetType(*pOwnerSrc, Ctx.m_AvailableTypes);
                if (!pOwnerType) return "CreateOwnedPair: failed to compile/load owner plugin";

                auto* pEndSrc = FindSourceByDirName(Ctx.m_Sources, std::get<std::string>(EndPluginDir));
                if (!pEndSrc) return "CreateOwnedPair: unknown marker plugin directory";
                auto* pEndType = EnsureLoadedAndGetType(*pEndSrc, Ctx.m_AvailableTypes);
                if (!pEndType) return "CreateOwnedPair: failed to compile/load marker plugin";

                const bool bHasEnd2 = m_Parser.hasOption(m_hEnd2Id);
                xnode_os_node_factory* pEnd2Type = nullptr;
                std::string End2PluginDirStr;
                if (bHasEnd2)
                {
                    auto End2PluginDirArg = m_Parser.getOptionArgAs<std::string>(m_hEnd2PluginDir, 0);
                    if (std::holds_alternative<xerr>(End2PluginDirArg)) return "CreateOwnedPair: bad arguments";
                    End2PluginDirStr = std::get<std::string>(End2PluginDirArg);
                    auto* pEnd2Src = FindSourceByDirName(Ctx.m_Sources, End2PluginDirStr);
                    if (!pEnd2Src) return "CreateOwnedPair: unknown second-level marker plugin directory";
                    pEnd2Type = EnsureLoadedAndGetType(*pEnd2Src, Ctx.m_AvailableTypes);
                    if (!pEnd2Type) return "CreateOwnedPair: failed to compile/load second-level marker plugin";
                }

                // All nodes land together - shift everything at/after TargetOrder by however many
                // we're inserting (2, or 3 when a second hop is present).
                const int NodeCount = bHasEnd2 ? 3 : 2;
                for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == TargetSpineId && N.m_Order >= TargetOrder) N.m_Order += NodeCount;

                const auto OwnerId  = ParseGuid(std::get<std::string>(Id));
                const auto MarkerId = ParseGuid(std::get<std::string>(EndId));
                const auto LinkIdVal = ParseGuid(std::get<std::string>(LinkId));

                Ctx.m_Nodes.push_back(CreateNodeInstance(OwnerId, pOwnerType, TargetOrder, TargetSpineId));
                Ctx.m_Nodes.back().m_OwnedEndId = MarkerId;
                // The read-only ownership link - always the owner's LAST output pin (its dedicated
                // "End" pin, appended after any real data outputs it declares) to the marker's own
                // first (and only) input pin. Read the count off the just-created real instance
                // (captured now, before the next push_back can reallocate Ctx.m_Nodes and invalidate
                // this reference) rather than a throwaway instance that would need its own cleanup.
                const int OwnerOutputIdx = Ctx.m_Nodes.back().m_pNode ? (int)Ctx.m_Nodes.back().m_pNode->getOutputs().size() - 1 : 0;

                Ctx.m_Nodes.push_back(CreateNodeInstance(MarkerId, pEndType, TargetOrder + 1, TargetSpineId));
                Ctx.m_Links.push_back(link_instance{ LinkIdVal, OwnerId, std::max(OwnerOutputIdx, 0), MarkerId, 0, true });

                if (bHasEnd2)
                {
                    const auto End2IdVal  = ParseGuid(std::get<std::string>(m_Parser.getOptionArgAs<std::string>(m_hEnd2Id, 0)));
                    const auto Link2IdVal = ParseGuid(std::get<std::string>(m_Parser.getOptionArgAs<std::string>(m_hLink2Id, 0)));
                    Ctx.m_Nodes.back().m_OwnedEndId = End2IdVal; // the just-created middle marker owns the terminal one
                    const int MidOutputIdx = Ctx.m_Nodes.back().m_pNode ? (int)Ctx.m_Nodes.back().m_pNode->getOutputs().size() - 1 : 0;
                    Ctx.m_Nodes.push_back(CreateNodeInstance(End2IdVal, pEnd2Type, TargetOrder + 2, TargetSpineId));
                    Ctx.m_Links.push_back(link_instance{ Link2IdVal, MarkerId, std::max(MidOutputIdx, 0), End2IdVal, 0, true });
                }

                Ctx.m_bDirty = true;
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto Id     = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                auto EndId  = m_Parser.getOptionArgAs<std::string>(m_hEndId, 0);
                auto End2Id = m_Parser.getOptionArgAs<std::string>(m_hEnd2Id, 0);
                File.Write(std::holds_alternative<xerr>(Id)     ? std::uint64_t{0} : ParseGuid(std::get<std::string>(Id)));
                File.Write(std::holds_alternative<xerr>(EndId)  ? std::uint64_t{0} : ParseGuid(std::get<std::string>(EndId)));
                File.Write(std::holds_alternative<xerr>(End2Id) ? std::uint64_t{0} : ParseGuid(std::get<std::string>(End2Id)));

                auto& Ctx = get<node_os_command_context>();
                File.Write(static_cast<std::uint32_t>(Ctx.m_Nodes.size()));
                for (auto& N : Ctx.m_Nodes) { File.Write(N.m_Id); File.Write(N.m_Order); }
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                std::uint64_t Id = 0, EndId = 0, End2Id = 0; File.Read(Id); File.Read(EndId); File.Read(End2Id);
                auto& Ctx = get<node_os_command_context>();
                auto IsDoomed = [&](std::uint64_t X) { return X == Id || X == EndId || X == End2Id; };
                std::erase_if(Ctx.m_Links, [&](auto& L) { return IsDoomed(L.m_SourceNode) || IsDoomed(L.m_TargetNode); });
                for (auto& N : Ctx.m_Nodes) if (IsDoomed(N.m_Id)) DestroyNodeInstance(N);
                std::erase_if(Ctx.m_Nodes, [&](auto& N) { return IsDoomed(N.m_Id); });
                Ctx.m_Selection.m_SelectedNodes.erase(Id);
                Ctx.m_Selection.m_SelectedNodes.erase(EndId);
                Ctx.m_Selection.m_SelectedNodes.erase(End2Id);

                std::uint32_t Count = 0; File.Read(Count);
                for (std::uint32_t i = 0; i < Count; ++i)
                {
                    std::uint64_t NId = 0; int Order = 0; File.Read(NId); File.Read(Order);
                    for (auto& N : Ctx.m_Nodes) if (N.m_Id == NId) { N.m_Order = Order; break; }
                }
                Ctx.m_bDirty = true;
            }

            xcmdline::parser::handle m_hId, m_hPluginDir, m_hEndId, m_hEndPluginDir, m_hLinkId, m_hAfter, m_hBefore, m_hInSpine;
            xcmdline::parser::handle m_hEnd2Id, m_hEnd2PluginDir, m_hLink2Id;
        };

        //================================================================================================
        // SetEndElseState - the one bespoke command behind an End node's own "IsElse" checkbox
        // (NODE_SCRIPTING_DESIGN.md section 4.2). Enabling it creates a further, plain End marker
        // right after this node plus a read-only link from this node's now-appearing "ElseEnd" pin
        // to it (mirroring CreateOwnedPair, but the "owner" here already exists rather than being
        // created by this same command); disabling it removes that paired End again. The two arms
        // share one command because they're two faces of the exact same user action - one checkbox.
        //================================================================================================
        struct set_end_else_state_cmd : xundo::command_base
        {
            set_end_else_state_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "SetEndElseState", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Toggles an End node's else-pairing. Usage: SetEndElseState -OwnerId N -Enable 0|1 [-EndId N -EndPluginDir dirname -LinkId N]"; }
            void RegisterArguments() noexcept override
            {
                m_hOwnerId      = m_Parser.addOption("OwnerId",      "The End node whose else-pairing is changing",           true,  1);
                m_hEnable       = m_Parser.addOption("Enable",       "1 = create the paired End, 0 = remove it",              true,  1);
                m_hEndId        = m_Parser.addOption("EndId",        "New marker node id - only used when -Enable 1",         false, 1);
                m_hEndPluginDir = m_Parser.addOption("EndPluginDir", "New marker's plugin folder - only used when -Enable 1", false, 1);
                m_hLinkId       = m_Parser.addOption("LinkId",       "New read-only link id - only used when -Enable 1",      false, 1);
            }

            std::string Redo() noexcept override
            {
                auto OwnerArg  = m_Parser.getOptionArgAs<std::string>(m_hOwnerId, 0);
                auto EnableArg = m_Parser.getOptionArgAs<std::int64_t>(m_hEnable, 0);
                if (std::holds_alternative<xerr>(OwnerArg) || std::holds_alternative<xerr>(EnableArg)) return "SetEndElseState: bad arguments";
                auto& Ctx = get<node_os_command_context>();
                const auto OwnerId = ParseGuid(std::get<std::string>(OwnerArg));
                const bool bEnable = std::get<std::int64_t>(EnableArg) != 0;

                node_instance* pOwnerNode = nullptr;
                for (auto& N : Ctx.m_Nodes) if (N.m_Id == OwnerId) { pOwnerNode = &N; break; }
                if (!pOwnerNode) return "SetEndElseState: owner node no longer exists";

                if (bEnable)
                {
                    if (pOwnerNode->m_OwnedEndId != 0) return {}; // already paired - idempotent no-op

                    auto EndIdArg        = m_Parser.getOptionArgAs<std::string>(m_hEndId, 0);
                    auto EndPluginDirArg = m_Parser.getOptionArgAs<std::string>(m_hEndPluginDir, 0);
                    auto LinkIdArg       = m_Parser.getOptionArgAs<std::string>(m_hLinkId, 0);
                    if (std::holds_alternative<xerr>(EndIdArg) || std::holds_alternative<xerr>(EndPluginDirArg) || std::holds_alternative<xerr>(LinkIdArg))
                        return "SetEndElseState: -EndId/-EndPluginDir/-LinkId required when -Enable 1";

                    auto* pEndSrc = FindSourceByDirName(Ctx.m_Sources, std::get<std::string>(EndPluginDirArg));
                    if (!pEndSrc) return "SetEndElseState: unknown marker plugin directory";
                    auto* pEndType = EnsureLoadedAndGetType(*pEndSrc, Ctx.m_AvailableTypes);
                    if (!pEndType) return "SetEndElseState: failed to compile/load marker plugin";

                    const auto EndId       = ParseGuid(std::get<std::string>(EndIdArg));
                    const auto LinkIdVal   = ParseGuid(std::get<std::string>(LinkIdArg));
                    const auto TargetSpineId = pOwnerNode->m_SpineId;
                    const int  TargetOrder   = pOwnerNode->m_Order + 1;
                    for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == TargetSpineId && N.m_Order >= TargetOrder) ++N.m_Order;

                    // pOwnerNode is re-resolved after this push_back, since it may reallocate Ctx.m_Nodes.
                    Ctx.m_Nodes.push_back(CreateNodeInstance(EndId, pEndType, TargetOrder, TargetSpineId));
                    for (auto& N : Ctx.m_Nodes) if (N.m_Id == OwnerId) { pOwnerNode = &N; break; }
                    pOwnerNode->m_OwnedEndId = EndId;

                    // By the time this command runs, the owner's own IsElse property (a plain
                    // reflected bool on its own node type - see Plugins/End) has already been set by
                    // the SetProperties command issued alongside this one, so getOutputs() already
                    // reports its extra "ElseEnd" pin - always the last one.
                    const int OwnerOutputIdx = pOwnerNode->m_pNode ? (int)pOwnerNode->m_pNode->getOutputs().size() - 1 : 0;
                    Ctx.m_Links.push_back(link_instance{ LinkIdVal, OwnerId, std::max(OwnerOutputIdx, 0), EndId, 0, true });
                }
                else
                {
                    const auto OldEndId = pOwnerNode->m_OwnedEndId;
                    if (OldEndId == 0) return {}; // nothing paired - idempotent no-op

                    std::erase_if(Ctx.m_Links, [&](auto& L) { return L.m_SourceNode == OldEndId || L.m_TargetNode == OldEndId; });
                    std::uint64_t RemovedSpineId = 0; int RemovedOrder = 0;
                    for (auto& N : Ctx.m_Nodes) if (N.m_Id == OldEndId) { RemovedSpineId = N.m_SpineId; RemovedOrder = N.m_Order; DestroyNodeInstance(N); break; }
                    std::erase_if(Ctx.m_Nodes, [&](auto& N) { return N.m_Id == OldEndId; });
                    for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == RemovedSpineId && N.m_Order > RemovedOrder) --N.m_Order;
                    for (auto& N : Ctx.m_Nodes) if (N.m_Id == OwnerId) { N.m_OwnedEndId = 0; break; }
                }
                Ctx.m_bDirty = true;
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                auto OwnerArg = m_Parser.getOptionArgAs<std::string>(m_hOwnerId, 0);
                const auto OwnerId = std::holds_alternative<xerr>(OwnerArg) ? std::uint64_t{0} : ParseGuid(std::get<std::string>(OwnerArg));

                File.Write(static_cast<std::uint32_t>(Ctx.m_Nodes.size()));
                for (auto& N : Ctx.m_Nodes) { File.Write(N.m_Id); File.Write(N.m_Order); File.Write(N.m_OwnedEndId); }

                // Snapshot the currently-paired End (if any) in full, so Undo can recreate it if
                // Redo's -Enable 0 arm went on to delete it.
                std::uint64_t OldEndId = 0;
                for (auto& N : Ctx.m_Nodes) if (N.m_Id == OwnerId) { OldEndId = N.m_OwnedEndId; break; }
                if (!OldEndId) { File.Write(std::uint8_t{0}); return; }

                for (auto& N : Ctx.m_Nodes)
                {
                    if (N.m_Id != OldEndId) continue;
                    std::string PluginDir;
                    for (auto& T : Ctx.m_AvailableTypes) if (N.m_pNode && T.m_pFactory == N.m_pNode->m_pFactory) { PluginDir = T.m_DirName; break; }
                    File.Write(std::uint8_t{1});
                    File.Write(N.m_Id); WriteString(File, PluginDir); File.Write(N.m_Order); File.Write(N.m_SpineId);
                    link_instance FoundLink{}; bool bHasLink = false;
                    for (auto& L : Ctx.m_Links) if (L.m_TargetNode == OldEndId) { FoundLink = L; bHasLink = true; break; }
                    File.Write(bHasLink ? std::uint8_t{1} : std::uint8_t{0});
                    if (bHasLink) File.Write(FoundLink);
                    return;
                }
                File.Write(std::uint8_t{0});
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                auto& Ctx = get<node_os_command_context>();

                struct prior_row { std::uint64_t m_Id; int m_Order; std::uint64_t m_OwnedEndId; };
                std::uint32_t Count = 0; File.Read(Count);
                std::vector<prior_row> PriorState(Count);
                for (auto& R : PriorState) { File.Read(R.m_Id); File.Read(R.m_Order); File.Read(R.m_OwnedEndId); }

                auto WasThereBefore = [&](std::uint64_t Id) { for (auto& R : PriorState) if (R.m_Id == Id) return true; return false; };

                // Remove whatever the -Enable 1 arm might have added (an id absent from PriorState).
                std::erase_if(Ctx.m_Nodes, [&](auto& N) { if (WasThereBefore(N.m_Id)) return false; DestroyNodeInstance(N); return true; });
                std::erase_if(Ctx.m_Links, [&](auto& L) { return !WasThereBefore(L.m_SourceNode) || !WasThereBefore(L.m_TargetNode); });

                std::uint8_t bHadOldEnd = 0; File.Read(bHadOldEnd);
                if (bHadOldEnd)
                {
                    std::uint64_t EndId = 0; File.Read(EndId);
                    const std::string PluginDir = ReadString(File);
                    int Order = 0; File.Read(Order);
                    std::uint64_t SpineId = 0; File.Read(SpineId);
                    std::uint8_t bHasLink = 0; File.Read(bHasLink);
                    link_instance L{};
                    if (bHasLink) File.Read(L);

                    const bool bAlreadyThere = std::any_of(Ctx.m_Nodes.begin(), Ctx.m_Nodes.end(), [&](auto& N) { return N.m_Id == EndId; });
                    if (!bAlreadyThere)
                    {
                        auto* pSrc = FindSourceByDirName(Ctx.m_Sources, PluginDir);
                        auto* pFactory = pSrc ? EnsureLoadedAndGetType(*pSrc, Ctx.m_AvailableTypes) : nullptr;
                        if (pFactory)
                        {
                            Ctx.m_Nodes.push_back(CreateNodeInstance(EndId, pFactory, Order, SpineId));
                            if (bHasLink) Ctx.m_Links.push_back(L);
                        }
                    }
                }

                for (auto& R : PriorState)
                    for (auto& N : Ctx.m_Nodes)
                        if (N.m_Id == R.m_Id) { N.m_Order = R.m_Order; N.m_OwnedEndId = R.m_OwnedEndId; break; }

                Ctx.m_bDirty = true;
            }

            xcmdline::parser::handle m_hOwnerId, m_hEnable, m_hEndId, m_hEndPluginDir, m_hLinkId;
        };

        //================================================================================================
        // DeleteNodes - the heaviest command: must fully snapshot each deleted node's identity, order,
        // and complete property block (via SerializePropertiesToString) plus every cascade-deleted
        // link, so Undo can reconstruct all of it exactly - this is the "resize 10 entries down to 3"
        // case the earlier design discussion settled on.
        //================================================================================================
        struct delete_nodes_cmd : xundo::command_base
        {
            delete_nodes_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "DeleteNodes", pDataBase) { RegisterArguments(); }

            const char* getCommandHelp() const noexcept override { return "Deletes node(s) and any links touching them. Usage: DeleteNodes -Ids id[,id...]"; }
            void RegisterArguments() noexcept override { m_hIds = m_Parser.addOption("Ids", "Node ids, comma-separated", true, 1); }

            std::string Redo() noexcept override
            {
                auto IdsArg = m_Parser.getOptionArgAs<std::string>(m_hIds, 0);
                if (std::holds_alternative<xerr>(IdsArg)) return "DeleteNodes: bad arguments";
                auto& Ctx = get<node_os_command_context>();
                const auto Ids = ExpandOwnershipCascade(Ctx.m_Nodes, SplitIds(std::get<std::string>(IdsArg)));

                auto IsDoomed = [&](std::uint64_t Id) { return std::find(Ids.begin(), Ids.end(), Id) != Ids.end(); };
                std::erase_if(Ctx.m_Links, [&](auto& L) { return IsDoomed(L.m_SourceNode) || IsDoomed(L.m_TargetNode); });
                for (auto& N : Ctx.m_Nodes) if (IsDoomed(N.m_Id)) DestroyNodeInstance(N);
                std::erase_if(Ctx.m_Nodes, [&](auto& N) { return IsDoomed(N.m_Id); });
                for (auto Id : Ids) Ctx.m_Selection.m_SelectedNodes.erase(Id);
                Ctx.m_bDirty = true;
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto IdsArg = m_Parser.getOptionArgAs<std::string>(m_hIds, 0);
                auto& Ctx = get<node_os_command_context>();
                const auto Ids = std::holds_alternative<xerr>(IdsArg) ? std::vector<std::uint64_t>{} : ExpandOwnershipCascade(Ctx.m_Nodes, SplitIds(std::get<std::string>(IdsArg)));
                auto IsDoomed = [&](std::uint64_t Id) { return std::find(Ids.begin(), Ids.end(), Id) != Ids.end(); };

                struct node_snap { std::uint64_t m_Id; std::string m_PluginDir; int m_Order; std::uint64_t m_SpineId; std::string m_Properties; std::uint64_t m_OwnedEndId; };
                std::vector<node_snap> NodeSnaps;
                for (auto& N : Ctx.m_Nodes)
                {
                    if (!IsDoomed(N.m_Id)) continue;
                    std::string PluginDir;
                    for (auto& T : Ctx.m_AvailableTypes) if (N.m_pNode && T.m_pFactory == N.m_pNode->m_pFactory) { PluginDir = T.m_DirName; break; }
                    std::string Properties;
                    if (HasSerializableProperties(N.m_pNode))
                        Properties = SerializePropertiesToString(N.m_pNode);
                    NodeSnaps.push_back({ N.m_Id, PluginDir, N.m_Order, N.m_SpineId, Properties, N.m_OwnedEndId });
                }
                std::vector<link_instance> LinkSnaps;
                for (auto& L : Ctx.m_Links)
                    if (IsDoomed(L.m_SourceNode) || IsDoomed(L.m_TargetNode))
                        LinkSnaps.push_back(L);

                File.Write(static_cast<std::uint32_t>(NodeSnaps.size()));
                for (auto& S : NodeSnaps) { File.Write(S.m_Id); WriteString(File, S.m_PluginDir); File.Write(S.m_Order); File.Write(S.m_SpineId); WriteString(File, S.m_Properties); File.Write(S.m_OwnedEndId); }
                File.Write(static_cast<std::uint32_t>(LinkSnaps.size()));
                for (auto& L : LinkSnaps) File.Write(L);
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                std::uint32_t NodeCount = 0; File.Read(NodeCount);
                for (std::uint32_t i = 0; i < NodeCount; ++i)
                {
                    std::uint64_t Id = 0; File.Read(Id);
                    const std::string PluginDir = ReadString(File);
                    int Order = 0; File.Read(Order);
                    std::uint64_t SpineId = 0; File.Read(SpineId);
                    const std::string Properties = ReadString(File);
                    std::uint64_t OwnedEndId = 0; File.Read(OwnedEndId);

                    auto* pSrc = FindSourceByDirName(Ctx.m_Sources, PluginDir);
                    auto* pFactory = pSrc ? EnsureLoadedAndGetType(*pSrc, Ctx.m_AvailableTypes) : nullptr;
                    if (!pFactory) continue; // plugin source no longer resolvable - best effort, matching LoadGraph's own tolerance
                    Ctx.m_Nodes.push_back(CreateNodeInstance(Id, pFactory, Order, SpineId));
                    Ctx.m_Nodes.back().m_OwnedEndId = OwnedEndId;
                    if (!Properties.empty())
                        ApplyPropertiesFromString(Ctx.m_Nodes.back().m_pNode, Properties);
                }
                std::uint32_t LinkCount = 0; File.Read(LinkCount);
                for (std::uint32_t i = 0; i < LinkCount; ++i)
                {
                    link_instance L{}; File.Read(L);
                    Ctx.m_Links.push_back(L);
                }
                Ctx.m_bDirty = true;
            }

            xcmdline::parser::handle m_hIds;
        };

        //================================================================================================
        // DeleteLink
        //================================================================================================
        struct delete_link_cmd : xundo::command_base
        {
            delete_link_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "DeleteLink", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Deletes a link. Usage: DeleteLink -Id N"; }
            void RegisterArguments() noexcept override { m_hId = m_Parser.addOption("Id", "Link id", true, 1); }

            std::string Redo() noexcept override
            {
                auto Id = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                if (std::holds_alternative<xerr>(Id)) return "DeleteLink: bad arguments";
                auto& Ctx = get<node_os_command_context>();
                const auto IdVal = ParseGuid(std::get<std::string>(Id));
                for (auto& L : Ctx.m_Links)
                    if (L.m_Id == IdVal && L.m_bReadOnly) return "DeleteLink: this is an owner<->End ownership link - it can't be removed on its own, only by deleting one of the two nodes";
                std::erase_if(Ctx.m_Links, [&](auto& L) { return L.m_Id == IdVal; });
                if (Ctx.m_Selection.m_SelectedLink == IdVal) Ctx.m_Selection.m_SelectedLink = 0;
                Ctx.m_bDirty = true;
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto Id = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                const auto IdVal = std::holds_alternative<xerr>(Id) ? std::uint64_t{0} : ParseGuid(std::get<std::string>(Id));
                auto& Ctx = get<node_os_command_context>();
                for (auto& L : Ctx.m_Links)
                    if (L.m_Id == IdVal) { File.Write(std::uint8_t{1}); File.Write(L); return; }
                File.Write(std::uint8_t{0});
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                std::uint8_t bHad = 0; File.Read(bHad);
                if (!bHad) return;
                link_instance L{}; File.Read(L);
                auto& Ctx = get<node_os_command_context>();
                Ctx.m_Links.push_back(L);
                Ctx.m_bDirty = true;
            }

            xcmdline::parser::handle m_hId;
        };

        //================================================================================================
        // Connect - evicts any existing link into the same target input first (matching the existing
        // "single connection per input" rule), so Undo must be able to restore whichever link (if any)
        // that eviction removed.
        //================================================================================================
        struct connect_cmd : xundo::command_base
        {
            connect_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "Connect", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Connects two ports. Usage: Connect -Id N -SourceNode N -SourceOutput N -TargetNode N -TargetInput N"; }
            void RegisterArguments() noexcept override
            {
                m_hId           = m_Parser.addOption("Id",           "Link id",              true, 1);
                m_hSourceNode   = m_Parser.addOption("SourceNode",   "Source node id",       true, 1);
                m_hSourceOutput = m_Parser.addOption("SourceOutput", "Source output index",  true, 1);
                m_hTargetNode   = m_Parser.addOption("TargetNode",   "Target node id",       true, 1);
                m_hTargetInput  = m_Parser.addOption("TargetInput",  "Target input index",   true, 1);
            }

            // Shared by Redo and BackupCurrenState - both need the same 5 fields off m_Parser.
            bool ParseAll(link_instance& L) const noexcept
            {
                auto Id = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                auto SN = m_Parser.getOptionArgAs<std::string>(m_hSourceNode, 0);
                auto SO = m_Parser.getOptionArgAs<std::int64_t>(m_hSourceOutput, 0);
                auto TN = m_Parser.getOptionArgAs<std::string>(m_hTargetNode, 0);
                auto TI = m_Parser.getOptionArgAs<std::int64_t>(m_hTargetInput, 0);
                if (std::holds_alternative<xerr>(Id) || std::holds_alternative<xerr>(SN) || std::holds_alternative<xerr>(SO) || std::holds_alternative<xerr>(TN) || std::holds_alternative<xerr>(TI))
                    return false;
                L.m_Id           = ParseGuid(std::get<std::string>(Id));
                L.m_SourceNode   = ParseGuid(std::get<std::string>(SN));
                L.m_SourceOutput = static_cast<int>(std::get<std::int64_t>(SO));
                L.m_TargetNode   = ParseGuid(std::get<std::string>(TN));
                L.m_TargetInput  = static_cast<int>(std::get<std::int64_t>(TI));
                return true;
            }

            std::string Redo() noexcept override
            {
                link_instance L{};
                if (!ParseAll(L)) return "Connect: bad arguments";
                auto& Ctx = get<node_os_command_context>();
                // Any two nodes anywhere in the graph can connect, regardless of spine or column - the
                // highway belongs to the SOURCE node's own column: the wire travels that column's own
                // rail up/down to the target's Y, then jogs however far sideways it needs to reach the
                // target, crossing intervening columns if the target lives in a different one (see
                // DrawHighwayPath/its ColumnOfNode(Link.m_SourceNode) call in DrawGraphCanvas).
                auto SourceIt = std::find_if(Ctx.m_Nodes.begin(), Ctx.m_Nodes.end(), [&](auto& N) { return N.m_Id == L.m_SourceNode; });
                auto TargetIt = std::find_if(Ctx.m_Nodes.begin(), Ctx.m_Nodes.end(), [&](auto& N) { return N.m_Id == L.m_TargetNode; });
                if (SourceIt == Ctx.m_Nodes.end() || TargetIt == Ctx.m_Nodes.end()) return "Connect: source/target node no longer exists";
                // An owner<->End ownership link is read-only - dragging a new wire onto that same
                // target input must not silently evict it the way an ordinary rewire would.
                for (auto& X : Ctx.m_Links)
                    if (X.m_TargetNode == L.m_TargetNode && X.m_TargetInput == L.m_TargetInput && X.m_bReadOnly)
                        return "Connect: that input is a read-only owner<->End ownership pin - it can't be rewired";
                std::erase_if(Ctx.m_Links, [&](auto& X) { return X.m_TargetNode == L.m_TargetNode && X.m_TargetInput == L.m_TargetInput; });
                Ctx.m_Links.push_back(L);
                Ctx.m_bDirty = true;
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                link_instance L{};
                const bool bOk = ParseAll(L);
                File.Write(bOk ? L.m_TargetNode : std::uint64_t{0});
                File.Write(bOk ? L.m_TargetInput : 0);
                auto& Ctx = get<node_os_command_context>();
                for (auto& X : Ctx.m_Links)
                    if (bOk && X.m_TargetNode == L.m_TargetNode && X.m_TargetInput == L.m_TargetInput) { File.Write(std::uint8_t{1}); File.Write(X); return; }
                File.Write(std::uint8_t{0});
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                std::uint64_t TargetNode = 0; int TargetInput = 0;
                File.Read(TargetNode); File.Read(TargetInput);
                std::uint8_t bHadExisting = 0; File.Read(bHadExisting);
                link_instance Existing{};
                if (bHadExisting) File.Read(Existing);

                auto& Ctx = get<node_os_command_context>();
                // Unconditionally remove whatever currently sits in that slot - that's always exactly
                // the link Redo() added, regardless of whether an eviction happened too.
                std::erase_if(Ctx.m_Links, [&](auto& X) { return X.m_TargetNode == TargetNode && X.m_TargetInput == TargetInput; });
                if (bHadExisting) Ctx.m_Links.push_back(Existing);
                Ctx.m_bDirty = true;
            }

            xcmdline::parser::handle m_hId, m_hSourceNode, m_hSourceOutput, m_hTargetNode, m_hTargetInput;
        };

        //================================================================================================
        // SetLiteralValue - the inline constant on an unconnected scalar input pin (Float/Int/Bool),
        // Unity-style: "no wire" no longer means "no value", just "whatever's typed right here".
        // Stored as plain text keyed by the pin id (InPinOf), same convention property_row already
        // uses, so one command/map covers every scalar type. Never touches m_Links - a literal and a
        // wire are independent; the pin just prefers the wire's value when one exists (see
        // GetInputValue's own caller in the canvas draw loop).
        //================================================================================================
        struct set_literal_value_cmd : xundo::command_base
        {
            set_literal_value_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "SetLiteralValue", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Sets an unconnected input pin's inline constant. Usage: SetLiteralValue -Pin N -Value text"; }
            void RegisterArguments() noexcept override
            {
                m_hPin   = m_Parser.addOption("Pin",   "Pin id (InPinOf(NodeId, Index))", true, 1);
                m_hValue = m_Parser.addOption("Value",  "New literal text",               true, 1);
            }

            std::string Redo() noexcept override
            {
                auto PinArg   = m_Parser.getOptionArgAs<std::string>(m_hPin, 0);
                auto ValueArg = m_Parser.getOptionArgAs<std::string>(m_hValue, 0);
                if (std::holds_alternative<xerr>(PinArg) || std::holds_alternative<xerr>(ValueArg)) return "SetLiteralValue: bad arguments";
                auto& Ctx = get<node_os_command_context>();
                Ctx.m_LiteralValues[ParseGuid(std::get<std::string>(PinArg))] = std::get<std::string>(ValueArg);
                Ctx.m_bDirty = true;
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto PinArg = m_Parser.getOptionArgAs<std::string>(m_hPin, 0);
                const auto PinId = std::holds_alternative<xerr>(PinArg) ? std::uint64_t{0} : ParseGuid(std::get<std::string>(PinArg));
                auto& Ctx = get<node_os_command_context>();
                auto It = Ctx.m_LiteralValues.find(PinId);
                File.Write(It != Ctx.m_LiteralValues.end() ? std::uint8_t{1} : std::uint8_t{0});
                if (It != Ctx.m_LiteralValues.end()) WriteString(File, It->second);
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                auto PinArg = m_Parser.getOptionArgAs<std::string>(m_hPin, 0);
                if (std::holds_alternative<xerr>(PinArg)) return;
                const auto PinId = ParseGuid(std::get<std::string>(PinArg));
                auto& Ctx = get<node_os_command_context>();
                std::uint8_t bHadPrior = 0; File.Read(bHadPrior);
                if (bHadPrior) Ctx.m_LiteralValues[PinId] = ReadString(File);
                else            Ctx.m_LiteralValues.erase(PinId);
                Ctx.m_bDirty = true;
            }

            xcmdline::parser::handle m_hPin, m_hValue;
        };

        //================================================================================================
        // CreateSpine - a genuinely new mutation shape: creates zero nodes, only the structural
        // containers (a spine, and optionally the column that houses it), placed directly at an
        // absolute world -Y - a spine's position is just (Y, ColumnId), nothing derived. -Column/
        // -NewColumn fold "attach to an already-existing column" vs. "synthesize a brand-new one right
        // on -Side of -NeighborColumn" into one command, the same way Select already folds its several
        // mutually exclusive concerns into one. -NewColumnId is minted by the CALLER (never inside
        // Redo()), matching this codebase's standing rule that Redo() never invents an id. No bDirty -
        // this never touches node/link data (matches reorder_nodes_cmd not setting it either).
        //================================================================================================
        struct create_spine_cmd : xundo::command_base
        {
            create_spine_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "CreateSpine", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override
            {
                return "Creates a new, empty spine. Usage: CreateSpine -Id spineid -Y yvalue "
                       "(-Column columnid | -NewColumn -NewColumnId id -NeighborColumn columnid -Side L|R)";
            }
            void RegisterArguments() noexcept override
            {
                m_hId             = m_Parser.addOption("Id",             "New spine id",                                    true,  1);
                m_hY              = m_Parser.addOption("Y",              "Absolute world Y for this spine's own top slot",  true,  1);
                m_hColumn         = m_Parser.addOption("Column",         "Attach to this already-existing column id",       false, 1);
                m_hNewColumn      = m_Parser.addOption("NewColumn",      "Synthesize a new column (value ignored)",         false, 1);
                m_hNewColumnId    = m_Parser.addOption("NewColumnId",    "Id for the new column",                           false, 1);
                m_hNeighborColumn = m_Parser.addOption("NeighborColumn", "The new column's own neighbor column id",         false, 1);
                m_hSide           = m_Parser.addOption("Side",           "Which side of -NeighborColumn the new one sits on: L or R", false, 1);
            }

            // Shared by Redo and BackupCurrenState - resolves and validates every argument without
            // mutating anything (the actual column creation only ever happens once, inside Redo()).
            std::string ResolveArgs(node_os_command_context& Ctx, std::uint64_t& OutSpineId, float& OutY, std::uint64_t& OutColumnId, bool& OutNewColumn
                                   , std::uint64_t& OutNewColumnId, std::uint64_t& OutNeighborColumnId, char& OutSide) const noexcept
            {
                auto IdArg = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                auto YArg  = m_Parser.getOptionArgAs<double>(m_hY, 0);
                if (std::holds_alternative<xerr>(IdArg) || std::holds_alternative<xerr>(YArg)) return "CreateSpine: bad arguments";
                OutSpineId = ParseGuid(std::get<std::string>(IdArg));
                OutY       = static_cast<float>(std::get<double>(YArg));

                const bool bHasColumn    = m_Parser.hasOption(m_hColumn);
                const bool bHasNewColumn = m_Parser.hasOption(m_hNewColumn);
                if (bHasColumn == bHasNewColumn) return "CreateSpine: exactly one of -Column/-NewColumn is required";

                if (bHasColumn)
                {
                    auto A = m_Parser.getOptionArgAs<std::string>(m_hColumn, 0);
                    if (std::holds_alternative<xerr>(A)) return "CreateSpine: bad arguments";
                    OutColumnId = ParseGuid(std::get<std::string>(A));
                    bool bFound = false; for (auto& Co : Ctx.m_Columns) if (Co.m_Id == OutColumnId) { bFound = true; break; }
                    if (!bFound) return "CreateSpine: -Column no longer exists";
                    OutNewColumn = false;
                    return {};
                }

                auto NCId = m_Parser.getOptionArgAs<std::string>(m_hNewColumnId, 0);
                auto NB   = m_Parser.getOptionArgAs<std::string>(m_hNeighborColumn, 0);
                auto Sd   = m_Parser.getOptionArgAs<std::string>(m_hSide, 0);
                if (std::holds_alternative<xerr>(NCId) || std::holds_alternative<xerr>(NB) || std::holds_alternative<xerr>(Sd)) return "CreateSpine: bad arguments";
                OutNewColumnId      = ParseGuid(std::get<std::string>(NCId));
                OutNeighborColumnId = ParseGuid(std::get<std::string>(NB));
                const auto& SideStr = std::get<std::string>(Sd);
                if (SideStr.empty() || (SideStr[0] != 'L' && SideStr[0] != 'R')) return "CreateSpine: -Side must be L or R";
                OutSide = SideStr[0];

                bool bNeighborFound = false;
                for (auto& Co : Ctx.m_Columns) if (Co.m_Id == OutNeighborColumnId) { bNeighborFound = true; break; }
                if (!bNeighborFound) return "CreateSpine: -NeighborColumn no longer exists";
                OutNewColumn = true;
                return {};
            }

            std::string Redo() noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                std::uint64_t SpineId = 0, ColumnId = 0, NewColumnId = 0, NeighborColumnId = 0; float Y = 0.0f; bool bNewColumn = false; char Side = 'R';
                if (auto Err = ResolveArgs(Ctx, SpineId, Y, ColumnId, bNewColumn, NewColumnId, NeighborColumnId, Side); !Err.empty()) return Err;

                if (bNewColumn)
                {
                    // Splices the new column in on -Side of -NeighborColumn - if the neighbor already
                    // had a column there (inserting BETWEEN two existing columns, not just past the
                    // outermost one), that far column is relinked to the new one instead, same as
                    // inserting into any doubly-linked list.
                    column NewCol{ NewColumnId, 0, 0, false };
                    for (auto& Co : Ctx.m_Columns)
                        if (Co.m_Id == NeighborColumnId)
                        {
                            std::uint64_t& NearPtr = (Side == 'R') ? Co.m_RightId : Co.m_LeftId;
                            const std::uint64_t OldFarNeighborId = NearPtr;
                            NearPtr = NewColumnId;
                            if (Side == 'R') { NewCol.m_LeftId = NeighborColumnId; NewCol.m_RightId = OldFarNeighborId; }
                            else             { NewCol.m_RightId = NeighborColumnId; NewCol.m_LeftId = OldFarNeighborId; }
                            if (OldFarNeighborId != 0)
                                for (auto& Co2 : Ctx.m_Columns)
                                    if (Co2.m_Id == OldFarNeighborId)
                                    {
                                        std::uint64_t& FarPtr = (Side == 'R') ? Co2.m_LeftId : Co2.m_RightId;
                                        FarPtr = NewColumnId;
                                        break;
                                    }
                            break;
                        }
                    Ctx.m_Columns.push_back(NewCol);
                    ColumnId = NewColumnId;
                }

                Ctx.m_Spines.push_back(spine{ SpineId, ColumnId, false, Y });
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                std::uint64_t SpineId = 0, ColumnId = 0, NewColumnId = 0, NeighborColumnId = 0; float Y = 0.0f; bool bNewColumn = false; char Side = 'R';
                const bool bOk = ResolveArgs(Ctx, SpineId, Y, ColumnId, bNewColumn, NewColumnId, NeighborColumnId, Side).empty();
                File.Write(bOk ? std::uint8_t{1} : std::uint8_t{0});
                File.Write(SpineId);
                File.Write(bNewColumn ? std::uint8_t{1} : std::uint8_t{0});
                File.Write(bNewColumn ? NewColumnId : ColumnId);
                File.Write(NeighborColumnId);
                File.Write(Side == 'R' ? std::uint8_t{1} : std::uint8_t{0});

                // The far neighbor (if any) that will need relinking on undo - whichever column
                // currently sits past -NeighborColumn on -Side, before the splice happens.
                std::uint64_t OldFarNeighborId = 0;
                if (bNewColumn)
                    for (auto& Co : Ctx.m_Columns)
                        if (Co.m_Id == NeighborColumnId) { OldFarNeighborId = (Side == 'R') ? Co.m_RightId : Co.m_LeftId; break; }
                File.Write(OldFarNeighborId);
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                std::uint8_t bOk = 0; File.Read(bOk);
                if (!bOk) return;
                std::uint64_t SpineId = 0; File.Read(SpineId);
                std::uint8_t bNewColumn = 0; File.Read(bNewColumn);
                std::uint64_t ColumnId = 0; File.Read(ColumnId);
                std::uint64_t NeighborColumnId = 0; File.Read(NeighborColumnId);
                std::uint8_t bSideR = 0; File.Read(bSideR);
                std::uint64_t OldFarNeighborId = 0; File.Read(OldFarNeighborId);

                auto& Ctx = get<node_os_command_context>();
                std::erase_if(Ctx.m_Spines, [&](auto& Sp) { return Sp.m_Id == SpineId; });
                if (bNewColumn)
                {
                    std::erase_if(Ctx.m_Columns, [&](auto& Co) { return Co.m_Id == ColumnId; });
                    for (auto& Co : Ctx.m_Columns)
                        if (Co.m_Id == NeighborColumnId)
                        {
                            if (bSideR) Co.m_RightId = OldFarNeighborId; else Co.m_LeftId = OldFarNeighborId;
                            break;
                        }
                    if (OldFarNeighborId != 0)
                        for (auto& Co : Ctx.m_Columns)
                            if (Co.m_Id == OldFarNeighborId)
                            {
                                if (bSideR) Co.m_LeftId = NeighborColumnId; else Co.m_RightId = NeighborColumnId;
                                break;
                            }
                }
            }

            xcmdline::parser::handle m_hId, m_hY, m_hColumn, m_hNewColumn, m_hNewColumnId, m_hNeighborColumn, m_hSide;
        };

        //================================================================================================
        // SetSpinePosition - sets a spine's absolute position directly: which column it lives in, and
        // its own world Y within it. No anchor/offset indirection at all - a spine's position IS
        // (Y, ColumnId), plain and settable. -NewColumn mirrors CreateSpine's own dual addressing, so a
        // spine can be dropped straight into a brand-new column spliced in beside an existing one.
        // Cascades to remove the OLD column too if the move empties it, bridging its own Left/Right
        // neighbors together, same as DeleteSpine's own cascade - no exceptions, even for the root
        // column: its m_bIsRoot flag transfers onto the destination column first, since Pass C's layout
        // walk needs exactly one root column to exist as its anchor, but doesn't care which one it is.
        // No bDirty - repositioning a spine never changes what's connected to what.
        //================================================================================================
        struct set_spine_position_cmd : xundo::command_base
        {
            set_spine_position_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "SetSpinePosition", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override
            {
                return "Moves a spine. Usage: SetSpinePosition -Id spineid -Y yvalue "
                       "(-Column columnid | -NewColumn -NewColumnId id -NeighborColumn columnid -Side L|R)";
            }
            void RegisterArguments() noexcept override
            {
                m_hId             = m_Parser.addOption("Id",             "Spine id to move",                                true,  1);
                m_hY              = m_Parser.addOption("Y",              "New absolute world Y",                            true,  1);
                m_hColumn         = m_Parser.addOption("Column",         "Move into this already-existing column id",       false, 1);
                m_hNewColumn      = m_Parser.addOption("NewColumn",      "Synthesize a new column (value ignored)",         false, 1);
                m_hNewColumnId    = m_Parser.addOption("NewColumnId",    "Id for the new column",                           false, 1);
                m_hNeighborColumn = m_Parser.addOption("NeighborColumn", "The new column's own neighbor column id",         false, 1);
                m_hSide           = m_Parser.addOption("Side",           "Which side of -NeighborColumn the new one sits on: L or R", false, 1);
            }

            // Shared by Redo and BackupCurrenState - resolves and validates every argument without
            // mutating anything (the actual column creation only ever happens once, inside Redo()).
            std::string ResolveArgs(node_os_command_context& Ctx, std::uint64_t& OutSpineId, float& OutY, std::uint64_t& OutColumnId, bool& OutNewColumn
                                   , std::uint64_t& OutNewColumnId, std::uint64_t& OutNeighborColumnId, char& OutSide) const noexcept
            {
                auto IdArg = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                auto YArg  = m_Parser.getOptionArgAs<double>(m_hY, 0);
                if (std::holds_alternative<xerr>(IdArg) || std::holds_alternative<xerr>(YArg)) return "SetSpinePosition: bad arguments";
                OutSpineId = ParseGuid(std::get<std::string>(IdArg));
                OutY       = static_cast<float>(std::get<double>(YArg));

                const bool bHasColumn    = m_Parser.hasOption(m_hColumn);
                const bool bHasNewColumn = m_Parser.hasOption(m_hNewColumn);
                if (bHasColumn == bHasNewColumn) return "SetSpinePosition: exactly one of -Column/-NewColumn is required";

                if (bHasColumn)
                {
                    auto A = m_Parser.getOptionArgAs<std::string>(m_hColumn, 0);
                    if (std::holds_alternative<xerr>(A)) return "SetSpinePosition: bad arguments";
                    OutColumnId = ParseGuid(std::get<std::string>(A));
                    bool bFound = false; for (auto& Co : Ctx.m_Columns) if (Co.m_Id == OutColumnId) { bFound = true; break; }
                    if (!bFound) return "SetSpinePosition: -Column no longer exists";
                    OutNewColumn = false;
                    return {};
                }

                auto NCId = m_Parser.getOptionArgAs<std::string>(m_hNewColumnId, 0);
                auto NB   = m_Parser.getOptionArgAs<std::string>(m_hNeighborColumn, 0);
                auto Sd   = m_Parser.getOptionArgAs<std::string>(m_hSide, 0);
                if (std::holds_alternative<xerr>(NCId) || std::holds_alternative<xerr>(NB) || std::holds_alternative<xerr>(Sd)) return "SetSpinePosition: bad arguments";
                OutNewColumnId      = ParseGuid(std::get<std::string>(NCId));
                OutNeighborColumnId = ParseGuid(std::get<std::string>(NB));
                const auto& SideStr = std::get<std::string>(Sd);
                if (SideStr.empty() || (SideStr[0] != 'L' && SideStr[0] != 'R')) return "SetSpinePosition: -Side must be L or R";
                OutSide = SideStr[0];

                bool bNeighborFound = false;
                for (auto& Co : Ctx.m_Columns) if (Co.m_Id == OutNeighborColumnId) { bNeighborFound = true; break; }
                if (!bNeighborFound) return "SetSpinePosition: -NeighborColumn no longer exists";
                OutNewColumn = true;
                return {};
            }

            // Shared by Redo and BackupCurrenState - a column with zero spines never persists, no
            // exceptions: if it's the one flagged m_bIsRoot, Redo() transfers that flag onto the
            // destination column first (Pass C's layout walk always needs exactly one root column to
            // exist as its anchor, it doesn't care which one).
            static bool WillRemoveOldColumn(node_os_command_context& Ctx, std::uint64_t SpineId, std::uint64_t OldColumnId, std::uint64_t DestColumnId) noexcept
            {
                if (OldColumnId == DestColumnId) return false;
                for (auto& Sp : Ctx.m_Spines) if (Sp.m_Id != SpineId && Sp.m_ColumnId == OldColumnId) return false;
                for (auto& Co : Ctx.m_Columns) if (Co.m_Id == OldColumnId) return true;
                return false;
            }

            std::string Redo() noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                std::uint64_t SpineId = 0, ColumnId = 0, NewColumnId = 0, NeighborColumnId = 0; float Y = 0.0f; bool bNewColumn = false; char Side = 'R';
                if (auto Err = ResolveArgs(Ctx, SpineId, Y, ColumnId, bNewColumn, NewColumnId, NeighborColumnId, Side); !Err.empty()) return Err;

                auto SpineIt = std::find_if(Ctx.m_Spines.begin(), Ctx.m_Spines.end(), [&](auto& Sp) { return Sp.m_Id == SpineId; });
                if (SpineIt == Ctx.m_Spines.end()) return "SetSpinePosition: spine no longer exists";
                const auto OldColumnId = SpineIt->m_ColumnId;

                if (bNewColumn)
                {
                    // Splices the new column in on -Side of -NeighborColumn, exactly like CreateSpine.
                    column NewCol{ NewColumnId, 0, 0, false };
                    for (auto& Co : Ctx.m_Columns)
                        if (Co.m_Id == NeighborColumnId)
                        {
                            std::uint64_t& NearPtr = (Side == 'R') ? Co.m_RightId : Co.m_LeftId;
                            const std::uint64_t OldFarNeighborId = NearPtr;
                            NearPtr = NewColumnId;
                            if (Side == 'R') { NewCol.m_LeftId = NeighborColumnId; NewCol.m_RightId = OldFarNeighborId; }
                            else             { NewCol.m_RightId = NeighborColumnId; NewCol.m_LeftId = OldFarNeighborId; }
                            if (OldFarNeighborId != 0)
                                for (auto& Co2 : Ctx.m_Columns)
                                    if (Co2.m_Id == OldFarNeighborId)
                                    {
                                        std::uint64_t& FarPtr = (Side == 'R') ? Co2.m_LeftId : Co2.m_RightId;
                                        FarPtr = NewColumnId;
                                        break;
                                    }
                            break;
                        }
                    Ctx.m_Columns.push_back(NewCol);
                    ColumnId = NewColumnId;
                }

                SpineIt->m_ColumnId = ColumnId;
                SpineIt->m_Y        = Y;

                if (OldColumnId != ColumnId)
                {
                    bool bOtherSpineInOldColumn = false;
                    for (auto& Sp : Ctx.m_Spines) if (Sp.m_ColumnId == OldColumnId) { bOtherSpineInOldColumn = true; break; }
                    if (!bOtherSpineInOldColumn)
                    {
                        auto ColIt = std::find_if(Ctx.m_Columns.begin(), Ctx.m_Columns.end(), [&](auto& Co) { return Co.m_Id == OldColumnId; });
                        // A column with zero spines never persists, no exceptions - bridge its own
                        // Left/Right neighbors together, same as DeleteSpine's own cascade. If this was
                        // the root column, transfer that flag onto where the spine is moving TO first, so
                        // exactly one column always stays flagged root.
                        if (ColIt != Ctx.m_Columns.end())
                        {
                            if (ColIt->m_bIsRoot)
                                for (auto& Co : Ctx.m_Columns) if (Co.m_Id == ColumnId) { Co.m_bIsRoot = true; break; }
                            const auto LeftId = ColIt->m_LeftId, RightId = ColIt->m_RightId;
                            for (auto& Co : Ctx.m_Columns)
                            {
                                if (LeftId  != 0 && Co.m_Id == LeftId)  Co.m_RightId = RightId;
                                if (RightId != 0 && Co.m_Id == RightId) Co.m_LeftId  = LeftId;
                            }
                            Ctx.m_Columns.erase(ColIt);
                        }
                    }
                }
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                std::uint64_t SpineId = 0, ColumnId = 0, NewColumnId = 0, NeighborColumnId = 0; float Y = 0.0f; bool bNewColumn = false; char Side = 'R';
                const bool bOk = ResolveArgs(Ctx, SpineId, Y, ColumnId, bNewColumn, NewColumnId, NeighborColumnId, Side).empty();
                auto SpineIt = bOk ? std::find_if(Ctx.m_Spines.begin(), Ctx.m_Spines.end(), [&](auto& Sp) { return Sp.m_Id == SpineId; }) : Ctx.m_Spines.end();
                if (!bOk || SpineIt == Ctx.m_Spines.end()) { File.Write(std::uint8_t{0}); return; }

                File.Write(std::uint8_t{1});
                File.Write(*SpineIt); // spine is a plain POD-ish struct - trivially copyable snapshot

                // Everything needed to reverse the splice, if -NewColumn (same fields as CreateSpine's
                // own undo needs).
                File.Write(bNewColumn ? std::uint8_t{1} : std::uint8_t{0});
                File.Write(NeighborColumnId);
                File.Write(Side == 'R' ? std::uint8_t{1} : std::uint8_t{0});
                std::uint64_t OldFarNeighborId = 0;
                if (bNewColumn)
                    for (auto& Co : Ctx.m_Columns)
                        if (Co.m_Id == NeighborColumnId) { OldFarNeighborId = (Side == 'R') ? Co.m_RightId : Co.m_LeftId; break; }
                File.Write(OldFarNeighborId);
                const auto DestColumnId = bNewColumn ? NewColumnId : ColumnId;
                File.Write(DestColumnId);

                const auto OldColumnId = SpineIt->m_ColumnId;
                const bool bOldColumnWillBeRemoved = WillRemoveOldColumn(Ctx, SpineId, OldColumnId, DestColumnId);
                File.Write(bOldColumnWillBeRemoved ? std::uint8_t{1} : std::uint8_t{0});
                if (bOldColumnWillBeRemoved)
                {
                    auto ColIt = std::find_if(Ctx.m_Columns.begin(), Ctx.m_Columns.end(), [&](auto& Co) { return Co.m_Id == OldColumnId; });
                    if (ColIt != Ctx.m_Columns.end()) File.Write(*ColIt);
                }
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                std::uint8_t bFound = 0; File.Read(bFound);
                if (!bFound) return;
                spine OldSpine{}; File.Read(OldSpine);

                std::uint8_t bNewColumn = 0; File.Read(bNewColumn);
                std::uint64_t NeighborColumnId = 0; File.Read(NeighborColumnId);
                std::uint8_t bSideR = 0; File.Read(bSideR);
                std::uint64_t OldFarNeighborId = 0; File.Read(OldFarNeighborId);
                std::uint64_t DestColumnId = 0; File.Read(DestColumnId);

                std::uint8_t bColumnRemoved = 0; File.Read(bColumnRemoved);
                column OldColumn{}; bool bHaveColumn = false;
                if (bColumnRemoved) { File.Read(OldColumn); bHaveColumn = true; }

                auto& Ctx = get<node_os_command_context>();

                if (bNewColumn)
                {
                    // Reverse the splice Redo() performed, exactly like CreateSpine's own undo.
                    std::erase_if(Ctx.m_Columns, [&](auto& Co) { return Co.m_Id == DestColumnId; });
                    for (auto& Co : Ctx.m_Columns)
                        if (Co.m_Id == NeighborColumnId)
                        {
                            if (bSideR) Co.m_RightId = OldFarNeighborId; else Co.m_LeftId = OldFarNeighborId;
                            break;
                        }
                    if (OldFarNeighborId != 0)
                        for (auto& Co : Ctx.m_Columns)
                            if (Co.m_Id == OldFarNeighborId)
                            {
                                if (bSideR) Co.m_LeftId = NeighborColumnId; else Co.m_RightId = NeighborColumnId;
                                break;
                            }
                }

                for (auto& Sp : Ctx.m_Spines) if (Sp.m_Id == OldSpine.m_Id) { Sp = OldSpine; break; }

                if (bHaveColumn)
                {
                    // If Redo() transferred the root flag onto the destination column, hand it back -
                    // exactly one column stays flagged root at all times. A no-op if the destination was
                    // itself a -NewColumn splice already unwound above.
                    if (OldColumn.m_bIsRoot)
                        for (auto& Co : Ctx.m_Columns) if (Co.m_Id == DestColumnId) { Co.m_bIsRoot = false; break; }
                    Ctx.m_Columns.push_back(OldColumn);
                    for (auto& Co : Ctx.m_Columns)
                    {
                        if (OldColumn.m_LeftId  != 0 && Co.m_Id == OldColumn.m_LeftId)  Co.m_RightId = OldColumn.m_Id;
                        if (OldColumn.m_RightId != 0 && Co.m_Id == OldColumn.m_RightId) Co.m_LeftId  = OldColumn.m_Id;
                    }
                }
            }

            xcmdline::parser::handle m_hId, m_hY, m_hColumn, m_hNewColumn, m_hNewColumnId, m_hNeighborColumn, m_hSide;
        };

        //================================================================================================
        // DeleteSpine - legal only when the spine currently has zero member nodes (deleting a populated
        // one is two user actions: delete its nodes, then delete the now-empty placeholder - same
        // single-responsibility shape as DeleteLink). Cascades to remove the column too if this was its
        // last spine, bridging its own Left/Right neighbors together (a column with zero spines never
        // persists) - except the one column flagged m_bIsRoot, which Pass C's layout walk always needs
        // to exist as its anchor.
        //================================================================================================
        struct delete_spine_cmd : xundo::command_base
        {
            delete_spine_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "DeleteSpine", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Deletes an empty spine (and its column, if it was the column's last one). Usage: DeleteSpine -Id spineid"; }
            void RegisterArguments() noexcept override { m_hId = m_Parser.addOption("Id", "Spine id", true, 1); }

            std::string Redo() noexcept override
            {
                auto IdArg = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                if (std::holds_alternative<xerr>(IdArg)) return "DeleteSpine: bad arguments";
                const auto SpineId = ParseGuid(std::get<std::string>(IdArg));

                auto& Ctx = get<node_os_command_context>();
                auto SpineIt = std::find_if(Ctx.m_Spines.begin(), Ctx.m_Spines.end(), [&](auto& Sp) { return Sp.m_Id == SpineId; });
                if (SpineIt == Ctx.m_Spines.end()) return "DeleteSpine: spine no longer exists";
                if (SpineIt->m_bIsRoot) return "DeleteSpine: cannot delete the root spine";
                for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == SpineId) return "DeleteSpine: spine still has nodes";

                const auto ColumnId = SpineIt->m_ColumnId;
                bool bOtherSpineInColumn = false;
                for (auto& Sp : Ctx.m_Spines) if (Sp.m_Id != SpineId && Sp.m_ColumnId == ColumnId) { bOtherSpineInColumn = true; break; }

                Ctx.m_Spines.erase(SpineIt);
                if (!bOtherSpineInColumn)
                {
                    auto ColIt = std::find_if(Ctx.m_Columns.begin(), Ctx.m_Columns.end(), [&](auto& Co) { return Co.m_Id == ColumnId; });
                    if (ColIt != Ctx.m_Columns.end() && !ColIt->m_bIsRoot)
                    {
                        const auto LeftId = ColIt->m_LeftId, RightId = ColIt->m_RightId;
                        for (auto& Co : Ctx.m_Columns)
                        {
                            if (LeftId  != 0 && Co.m_Id == LeftId)  Co.m_RightId = RightId;
                            if (RightId != 0 && Co.m_Id == RightId) Co.m_LeftId  = LeftId;
                        }
                        Ctx.m_Columns.erase(ColIt);
                    }
                }
                if (Ctx.m_Selection.m_SelectedGapSpineId == SpineId) { Ctx.m_Selection.m_SelectedGapSpineId = 0; Ctx.m_Selection.m_SelectedGapIndex = -1; }
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                auto IdArg = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                const auto SpineId = std::holds_alternative<xerr>(IdArg) ? std::uint64_t{0} : ParseGuid(std::get<std::string>(IdArg));

                auto SpineIt = std::find_if(Ctx.m_Spines.begin(), Ctx.m_Spines.end(), [&](auto& Sp) { return Sp.m_Id == SpineId; });
                const bool bFound = SpineIt != Ctx.m_Spines.end();
                File.Write(bFound ? std::uint8_t{1} : std::uint8_t{0});
                if (!bFound) return;

                File.Write(*SpineIt); // spine is a plain POD-ish struct - trivially copyable snapshot

                bool bOtherSpineInColumn = false;
                for (auto& Sp : Ctx.m_Spines) if (Sp.m_Id != SpineId && Sp.m_ColumnId == SpineIt->m_ColumnId) { bOtherSpineInColumn = true; break; }
                auto ColIt = std::find_if(Ctx.m_Columns.begin(), Ctx.m_Columns.end(), [&](auto& Co) { return Co.m_Id == SpineIt->m_ColumnId; });
                const bool bColumnWillBeRemoved = !bOtherSpineInColumn && ColIt != Ctx.m_Columns.end() && !ColIt->m_bIsRoot;
                File.Write(bColumnWillBeRemoved ? std::uint8_t{1} : std::uint8_t{0}); // "will the column also be removed"
                if (bColumnWillBeRemoved) File.Write(*ColIt);
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                std::uint8_t bFound = 0; File.Read(bFound);
                if (!bFound) return;
                spine Spine{}; File.Read(Spine);
                std::uint8_t bColumnRemoved = 0; File.Read(bColumnRemoved);

                auto& Ctx = get<node_os_command_context>();
                Ctx.m_Spines.push_back(Spine);
                if (bColumnRemoved)
                {
                    column Column{}; File.Read(Column);
                    Ctx.m_Columns.push_back(Column);
                    for (auto& Co : Ctx.m_Columns)
                    {
                        if (Column.m_LeftId  != 0 && Co.m_Id == Column.m_LeftId)  Co.m_RightId = Column.m_Id;
                        if (Column.m_RightId != 0 && Co.m_Id == Column.m_RightId) Co.m_LeftId  = Column.m_Id;
                    }
                }
            }

            xcmdline::parser::handle m_hId;
        };

        //================================================================================================
        // ReorderNodes - carries the FULL new id-order sequence (matching how MoveNodesTo/InsertNodeAt
        // already reassign every node's m_Order densely, not just the moved ones' positions).
        //================================================================================================
        struct reorder_nodes_cmd : xundo::command_base
        {
            reorder_nodes_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "ReorderNodes", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Sets every node's stacking order. Usage: ReorderNodes -Ids id[,id...] (the full new order)"; }
            void RegisterArguments() noexcept override { m_hIds = m_Parser.addOption("Ids", "Full new order, comma-separated node ids", true, 1); }

            std::string Redo() noexcept override
            {
                auto IdsArg = m_Parser.getOptionArgAs<std::string>(m_hIds, 0);
                if (std::holds_alternative<xerr>(IdsArg)) return "ReorderNodes: bad arguments";
                const auto NewOrder = SplitIds(std::get<std::string>(IdsArg));
                auto& Ctx = get<node_os_command_context>();
                for (int i = 0; i < (int)NewOrder.size(); ++i)
                    for (auto& N : Ctx.m_Nodes) if (N.m_Id == NewOrder[i]) { N.m_Order = i; break; }
                return {}; // pure reorder - doesn't change what's connected to what, no bDirty (matches existing MoveNodesTo)
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                File.Write(static_cast<std::uint32_t>(Ctx.m_Nodes.size()));
                for (auto& N : Ctx.m_Nodes) { File.Write(N.m_Id); File.Write(N.m_Order); }
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                std::uint32_t Count = 0; File.Read(Count);
                for (std::uint32_t i = 0; i < Count; ++i)
                {
                    std::uint64_t Id = 0; int Order = 0; File.Read(Id); File.Read(Order);
                    for (auto& N : Ctx.m_Nodes) if (N.m_Id == Id) { N.m_Order = Order; break; }
                }
            }

            xcmdline::parser::handle m_hIds;
        };

        //================================================================================================
        // MoveNodesToSpine - moves node(s) into a DIFFERENT spine (dragging a node onto another
        // spine's own marker), renumbering every spine it touches - each source spine's own remainder
        // and the destination spine's new dense order - densely to 0..N-1, same reasoning as
        // CreateNode/ReorderNodes: deleting/removing leaves gaps that get closed here, never patched
        // with arithmetic on the existing m_Order values. Addressed the same way CreateNode addresses
        // insertion (-After/-Before an existing node in the destination, or -InSpine to append
        // regardless of that spine's current size).
        //================================================================================================
        struct move_nodes_to_spine_cmd : xundo::command_base
        {
            move_nodes_to_spine_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "MoveNodesToSpine", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override
            {
                return "Moves node(s) into a different spine. Usage: MoveNodesToSpine -Ids id[,id...] (-After id | -Before id | -InSpine spineid)";
            }
            void RegisterArguments() noexcept override
            {
                m_hIds     = m_Parser.addOption("Ids",     "Node ids to move, comma-separated",                          true,  1);
                m_hAfter   = m_Parser.addOption("After",   "Insert right after this node id in the destination spine",  false, 1);
                m_hBefore  = m_Parser.addOption("Before",  "Insert right before this node id in the destination spine", false, 1);
                m_hInSpine = m_Parser.addOption("InSpine", "Append to this spine, whatever its current size",           false, 1);
            }

            // Shared by Redo and BackupCurrenState - resolves -After/-Before/-InSpine into a target
            // spine + dense order index, exactly like create_node_cmd's own ResolveTargetOrder.
            std::string ResolveTarget(node_os_command_context& Ctx, std::uint64_t& OutSpineId, int& OutOrder) const noexcept
            {
                const bool bHasAfter   = m_Parser.hasOption(m_hAfter);
                const bool bHasBefore  = m_Parser.hasOption(m_hBefore);
                const bool bHasInSpine = m_Parser.hasOption(m_hInSpine);
                if ((bHasAfter ? 1 : 0) + (bHasBefore ? 1 : 0) + (bHasInSpine ? 1 : 0) != 1)
                    return "MoveNodesToSpine: exactly one of -After/-Before/-InSpine is required";

                if (bHasInSpine)
                {
                    auto RefArg = m_Parser.getOptionArgAs<std::string>(m_hInSpine, 0);
                    if (std::holds_alternative<xerr>(RefArg)) return "MoveNodesToSpine: bad arguments";
                    const auto SpineId = ParseGuid(std::get<std::string>(RefArg));
                    bool bFound = false;
                    for (auto& S : Ctx.m_Spines) if (S.m_Id == SpineId) { bFound = true; break; }
                    if (!bFound) return "MoveNodesToSpine: -InSpine spine no longer exists";
                    int Count = 0;
                    for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == SpineId) ++Count;
                    OutSpineId = SpineId; OutOrder = Count; return {};
                }

                auto RefArg = m_Parser.getOptionArgAs<std::string>(bHasAfter ? m_hAfter : m_hBefore, 0);
                if (std::holds_alternative<xerr>(RefArg)) return "MoveNodesToSpine: bad arguments";
                const auto RefId = ParseGuid(std::get<std::string>(RefArg));
                std::uint64_t RefSpineId = 0; int RefOrder = 0;
                if (!ResolveNodeSpineAndOrder(Ctx.m_Nodes, RefId, RefSpineId, RefOrder)) return "MoveNodesToSpine: -After/-Before node no longer exists";
                OutSpineId = RefSpineId; OutOrder = bHasAfter ? RefOrder + 1 : RefOrder;
                return {};
            }

            std::string Redo() noexcept override
            {
                auto IdsArg = m_Parser.getOptionArgAs<std::string>(m_hIds, 0);
                if (std::holds_alternative<xerr>(IdsArg)) return "MoveNodesToSpine: bad arguments";
                const auto MovingIds = SplitIds(std::get<std::string>(IdsArg));
                if (MovingIds.empty()) return "MoveNodesToSpine: no ids given";

                auto& Ctx = get<node_os_command_context>();
                std::uint64_t TargetSpineId = 0; int TargetOrder = 0;
                if (auto Err = ResolveTarget(Ctx, TargetSpineId, TargetOrder); !Err.empty()) return Err;

                auto IsMoving = [&](std::uint64_t Id) { return std::find(MovingIds.begin(), MovingIds.end(), Id) != MovingIds.end(); };

                auto OrderOf = [&](std::uint64_t Id) { for (auto& N : Ctx.m_Nodes) if (N.m_Id == Id) return N.m_Order; return 0; };

                // The target's CURRENT dense order, snapshotted before any renumbering below touches it
                // - TargetOrder (resolved above) is expressed against this exact snapshot.
                std::vector<std::uint64_t> TargetOrderBefore;
                for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == TargetSpineId) TargetOrderBefore.push_back(N.m_Id);
                std::sort(TargetOrderBefore.begin(), TargetOrderBefore.end(), [&](std::uint64_t A, std::uint64_t B) { return OrderOf(A) < OrderOf(B); });

                // How many movers already sitting in the TARGET spine were before TargetOrder -
                // removing them shifts the insertion point left by that many (same adjustment the UI's
                // own same-spine MoveNodesTo already makes).
                int Adjust = 0;
                for (int i = 0; i < TargetOrder && i < (int)TargetOrderBefore.size(); ++i)
                    if (IsMoving(TargetOrderBefore[i])) ++Adjust;

                // Every distinct spine this touches: every mover's OWN current spine, plus the target -
                // each gets its own remainder (or, for the target, remainder-plus-movers) renumbered
                // densely to 0..N-1.
                std::set<std::uint64_t> TouchedSpineIds{ TargetSpineId };
                for (auto Id : MovingIds)
                    for (auto& N : Ctx.m_Nodes) if (N.m_Id == Id) { TouchedSpineIds.insert(N.m_SpineId); break; }

                for (auto SpineId : TouchedSpineIds)
                {
                    std::vector<std::uint64_t> Remaining;
                    for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == SpineId && !IsMoving(N.m_Id)) Remaining.push_back(N.m_Id);
                    std::sort(Remaining.begin(), Remaining.end(), [&](std::uint64_t A, std::uint64_t B) { return OrderOf(A) < OrderOf(B); });
                    if (SpineId == TargetSpineId)
                    {
                        const int InsertAt = std::clamp(TargetOrder - Adjust, 0, (int)Remaining.size());
                        Remaining.insert(Remaining.begin() + InsertAt, MovingIds.begin(), MovingIds.end());
                    }
                    for (int i = 0; i < (int)Remaining.size(); ++i)
                        for (auto& N : Ctx.m_Nodes) if (N.m_Id == Remaining[i]) { N.m_Order = i; break; }
                }
                for (auto Id : MovingIds)
                    for (auto& N : Ctx.m_Nodes) if (N.m_Id == Id) N.m_SpineId = TargetSpineId;

                return {}; // pure reassignment - doesn't change what's connected to what, no bDirty (matches ReorderNodes)
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                File.Write(static_cast<std::uint32_t>(Ctx.m_Nodes.size()));
                for (auto& N : Ctx.m_Nodes) { File.Write(N.m_Id); File.Write(N.m_Order); File.Write(N.m_SpineId); }
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                std::uint32_t Count = 0; File.Read(Count);
                for (std::uint32_t i = 0; i < Count; ++i)
                {
                    std::uint64_t Id = 0; int Order = 0; std::uint64_t SpineId = 0;
                    File.Read(Id); File.Read(Order); File.Read(SpineId);
                    for (auto& N : Ctx.m_Nodes) if (N.m_Id == Id) { N.m_Order = Order; N.m_SpineId = SpineId; break; }
                }
            }

            xcmdline::parser::handle m_hIds, m_hAfter, m_hBefore, m_hInSpine;
        };

        //================================================================================================
        // SetProperties - unlike every other command, the mutation has ALREADY happened by the time
        // this is issued (ImGui already wrote the live property bytes this frame, including whatever
        // an arbitrary plugin-drawn custom button did). So both snapshots travel in the command string
        // itself, base64-encoded: BackupCurrenState never touches live state, it just pulls -Before out
        // of the already-parsed args; Redo (re-)applies -After; Undo applies -Before. One command
        // covers scalar edits, list resizes, and custom-button mutations uniformly.
        //================================================================================================
        struct set_properties_cmd : xundo::command_base
        {
            set_properties_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "SetProperties", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Applies a property snapshot to a node. Usage: SetProperties -NodeId N -Before base64 -After base64"; }
            void RegisterArguments() noexcept override
            {
                m_hNodeId = m_Parser.addOption("NodeId", "Node id", true, 1);
                m_hBefore = m_Parser.addOption("Before", "Base64 property snapshot, pre-edit",  true, 1);
                m_hAfter  = m_Parser.addOption("After",  "Base64 property snapshot, post-edit", true, 1);
            }

            static xnode_os_node* GetNodeFor(node_os_command_context& Ctx, std::uint64_t NodeId)
            {
                for (auto& N : Ctx.m_Nodes)
                    if (N.m_Id == NodeId && HasSerializableProperties(N.m_pNode))
                        return N.m_pNode;
                return nullptr;
            }

            std::string Redo() noexcept override
            {
                auto NodeId = m_Parser.getOptionArgAs<std::string>(m_hNodeId, 0);
                auto After  = m_Parser.getOptionArgAs<std::string>(m_hAfter, 0);
                if (std::holds_alternative<xerr>(NodeId) || std::holds_alternative<xerr>(After)) return "SetProperties: bad arguments";
                auto& Ctx = get<node_os_command_context>();
                if (auto* pNode = GetNodeFor(Ctx, ParseGuid(std::get<std::string>(NodeId))))
                {
                    ApplyPropertiesFromString(pNode, Base64Decode(std::get<std::string>(After)));
                    Ctx.m_bDirty = true;
                }
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto NodeId = m_Parser.getOptionArgAs<std::string>(m_hNodeId, 0);
                auto Before = m_Parser.getOptionArgAs<std::string>(m_hBefore, 0);
                File.Write(std::holds_alternative<xerr>(NodeId) ? std::uint64_t{0} : ParseGuid(std::get<std::string>(NodeId)));
                WriteString(File, std::holds_alternative<xerr>(Before) ? std::string{} : std::get<std::string>(Before));
            }

            void Undo(xundo::undo_file& File) noexcept override
            {
                std::uint64_t NodeId = 0; File.Read(NodeId);
                const std::string BeforeB64 = ReadString(File);
                auto& Ctx = get<node_os_command_context>();
                if (auto* pNode = GetNodeFor(Ctx, NodeId))
                {
                    ApplyPropertiesFromString(pNode, Base64Decode(BeforeB64));
                    Ctx.m_bDirty = true;
                }
            }

            xcmdline::parser::handle m_hNodeId, m_hBefore, m_hAfter;
        };

        //================================================================================================
        // Select - one command covers all three selection fields at once (SelectedNodes/SelectedLink/
        // SelectedGap), matching how every existing interaction site already sets all three together.
        //================================================================================================
        struct select_cmd : xundo::command_base
        {
            select_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "Select", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override
            {
                return "Sets the current selection - every flag is optional, omitted means \"none of this kind\"."
                       " Usage: Select [-Nodes id[,id...]] [-Link id] [-MarkerAfter id | -MarkerBefore id | -MarkerSpine spineid]";
            }
            void RegisterArguments() noexcept override
            {
                m_hNodes        = m_Parser.addOption("Nodes",        "Selected node ids, comma-separated",                  false, 1);
                m_hLink         = m_Parser.addOption("Link",         "Selected link id",                                    false, 1);
                m_hMarkerAfter  = m_Parser.addOption("MarkerAfter",  "Select the insert marker right after this node id",   false, 1);
                m_hMarkerBefore = m_Parser.addOption("MarkerBefore", "Select the insert marker right before this node id",  false, 1);
                m_hMarkerSpine  = m_Parser.addOption("MarkerSpine",  "Select an empty spine's own placeholder marker",       false, 1);
            }

            std::string Redo() noexcept override
            {
                auto& Ctx = get<node_os_command_context>();
                auto& S   = Ctx.m_Selection;

                S.m_SelectedNodes.clear();
                if (m_Parser.hasOption(m_hNodes))
                {
                    auto NodesArg = m_Parser.getOptionArgAs<std::string>(m_hNodes, 0);
                    if (std::holds_alternative<xerr>(NodesArg)) return "Select: bad arguments";
                    const auto Ids = SplitIds(std::get<std::string>(NodesArg));
                    S.m_SelectedNodes = std::set<std::uint64_t>(Ids.begin(), Ids.end());
                }

                S.m_SelectedLink = 0;
                if (m_Parser.hasOption(m_hLink))
                {
                    auto LinkArg = m_Parser.getOptionArgAs<std::string>(m_hLink, 0);
                    if (std::holds_alternative<xerr>(LinkArg)) return "Select: bad arguments";
                    S.m_SelectedLink = ParseGuid(std::get<std::string>(LinkArg));
                }

                S.m_SelectedGapSpineId = 0;
                S.m_SelectedGapIndex   = -1;
                const bool bHasAfter  = m_Parser.hasOption(m_hMarkerAfter);
                const bool bHasBefore = m_Parser.hasOption(m_hMarkerBefore);
                const bool bHasSpine  = m_Parser.hasOption(m_hMarkerSpine);
                if ((bHasAfter ? 1 : 0) + (bHasBefore ? 1 : 0) + (bHasSpine ? 1 : 0) > 1)
                    return "Select: -MarkerAfter, -MarkerBefore and -MarkerSpine are mutually exclusive";
                if (bHasAfter || bHasBefore)
                {
                    auto RefArg = m_Parser.getOptionArgAs<std::string>(bHasAfter ? m_hMarkerAfter : m_hMarkerBefore, 0);
                    if (std::holds_alternative<xerr>(RefArg)) return "Select: bad arguments";
                    const auto RefId = ParseGuid(std::get<std::string>(RefArg));
                    std::uint64_t RefSpineId = 0; int RefOrder = 0;
                    if (!ResolveNodeSpineAndOrder(Ctx.m_Nodes, RefId, RefSpineId, RefOrder)) return "Select: -MarkerAfter/-MarkerBefore node no longer exists";
                    S.m_SelectedGapSpineId = RefSpineId; S.m_SelectedGapIndex = bHasAfter ? RefOrder + 1 : RefOrder;
                }
                else if (bHasSpine)
                {
                    // Legal only for an empty spine - a non-empty one already has -MarkerBefore <its
                    // first node> to select the very same visual slot.
                    auto RefArg = m_Parser.getOptionArgAs<std::string>(m_hMarkerSpine, 0);
                    if (std::holds_alternative<xerr>(RefArg)) return "Select: bad arguments";
                    const auto RefSpineId = ParseGuid(std::get<std::string>(RefArg));
                    bool bFound = false;
                    for (auto& Sp : Ctx.m_Spines) if (Sp.m_Id == RefSpineId) { bFound = true; break; }
                    if (!bFound) return "Select: -MarkerSpine spine no longer exists";
                    for (auto& N : Ctx.m_Nodes) if (N.m_SpineId == RefSpineId) return "Select: -MarkerSpine is only for an empty spine";
                    S.m_SelectedGapSpineId = RefSpineId; S.m_SelectedGapIndex = 0;
                }
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override { BackupSelection(get<node_os_command_context>(), File); }
            void Undo(xundo::undo_file& File) noexcept override { RestoreSelection(get<node_os_command_context>(), File); }

            xcmdline::parser::handle m_hNodes, m_hLink, m_hMarkerAfter, m_hMarkerBefore, m_hMarkerSpine;
        };

        //================================================================================================
        // ClearSelection - a dedicated, self-describing command name for "select nothing", rather than
        // Select with every flag omitted: a bare "Select" with nothing after it in the history log still
        // makes a reader (human or agent) work out what it did; "ClearSelection" says it outright.
        //================================================================================================
        struct clear_selection_cmd : xundo::command_base
        {
            clear_selection_cmd(xundo::system& System, void* pDataBase) noexcept : command_base(System, "ClearSelection", pDataBase) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Deselects everything (nodes, link, insert marker). Usage: ClearSelection"; }
            void RegisterArguments() noexcept override {} // takes no arguments at all

            std::string Redo() noexcept override
            {
                auto& S = get<node_os_command_context>().m_Selection;
                S.m_SelectedNodes.clear();
                S.m_SelectedLink = 0;
                S.m_SelectedGapSpineId = 0;
                S.m_SelectedGapIndex   = -1;
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override { BackupSelection(get<node_os_command_context>(), File); }
            void Undo(xundo::undo_file& File) noexcept override { RestoreSelection(get<node_os_command_context>(), File); }
        };
    }
}

//------------------------------------------------------------------------------------------------

int E27_Example()
{
    // TEMPORARY, non-interactive self-test for the codegen backend - loads the real saved graph,
    // generates C++, compiles and runs it, and writes the result to a file. Deliberately BEFORE any
    // xgpu instance/device/window/ImGui exists - none of LoadGraph/GenerateCpp/
    // CompileAndRunGeneratedCpp need a GPU at all, and an earlier version of this hook placed after
    // that setup hit a heap-corruption crash on exit (a real, pre-existing xgpu/window teardown
    // fragility when the app exits before ever entering its normal render loop, not anything to do
    // with the codegen work itself - avoided entirely by never creating that stack in the first
    // place for this path). Only fires with -CodegenSelfTest on the command line, so ordinary
    // launches (no flag) are completely unaffected. Exists purely so this can be verified from
    // outside the app (no click-driven testing) - remove once the codegen pipeline itself is done
    // being validated.
    if (std::strstr(GetCommandLineA(), "-CodegenSelfTest"))
    {
        std::vector<nodeos::plugin_source_entry> Sources = nodeos::ScanPluginSources("D:/LIONant/xGPU/source/Examples/E27_NodeOS/Plugins");
        std::vector<nodeos::available_node_type> AvailableTypes;
        std::vector<nodeos::node_instance>       Nodes;
        std::vector<nodeos::link_instance>       Links;
        std::vector<nodeos::spine>  Spines  { nodeos::spine {  xresource::guid_generator::Instance64(), 0, true, nodeos::geo::TOP } };
        std::vector<nodeos::column> Columns { nodeos::column { xresource::guid_generator::Instance64(), 0, 0, true } };
        Spines.front().m_ColumnId = Columns.front().m_Id;
        std::unordered_map<std::uint64_t, std::string> LiteralValues;

        std::string Report;
        if (!nodeos::LoadGraph("D:/LIONant/xGPU/source/Examples/E27_NodeOS/graph.txt", Nodes, Links, Sources, AvailableTypes, Spines, Columns, LiteralValues))
            Report = "[self-test] LoadGraph FAILED\n";
        else
        {
            const std::string GeneratedSource = nodeos::GenerateCpp(Nodes, Links, Spines, LiteralValues);
            const auto Result = nodeos::CompileAndRunGeneratedCpp(GeneratedSource);
            Report += "=== GENERATED SOURCE ===\n" + GeneratedSource + "\n";
            Report += std::format("=== COMPILE {} ===\n{}\n", Result.m_bCompileOk ? "OK" : "FAILED", Result.m_CompileLog);
            if (Result.m_bCompileOk)
                Report += std::format("=== RUN {} - OUTPUT ===\n{}\n", Result.m_bRanOk ? "OK" : "FAILED", Result.m_RunOutput);

            // TEMPORARY - the interpreter (RunProgram/RunSpineRange's "If" handling and GetInputValue's
            // literal fallback) is never exercised by codegen at all; running it here too, on the exact
            // same loaded Nodes/Links/LiteralValues, proves the interpreter's own conditional-branch and
            // literal-value fixes independently rather than trusting they match codegen by inspection
            // alone. Harmless to run after codegen above - RunProgram only touches m_bHasRun/
            // m_CachedOutputs, which GenerateCpp/CompileAndRunGeneratedCpp never read.
            nodeos::literal_storage InterpScratch;
            nodeos::RunProgram(Nodes, Links, Spines, LiteralValues, InterpScratch);
            Report += "=== INTERPRETER (Execute Graph) OUTPUT ===\n";
            for (auto& Line : nodeos::GetRuntimeLog()) Report += Line + "\n";
            for (auto& N : Nodes)
                if (N.m_pNode && !N.m_bHasRun && N.m_pNode->m_pFactory->getName() != "End")
                    Report += std::format("[not reached: {} #{:x}]\n", N.m_pNode->m_pFactory->getName(), N.m_Id & 0xffffff);
        }
        std::ofstream Out("D:/LIONant/xGPU/source/Examples/E27_NodeOS/CompiledPlugins/_codegen_selftest_report.txt");
        Out << Report;
        Out.close();
        for (auto& N : Nodes) nodeos::DestroyNodeInstance(N);

        // Bisected empirically (TerminateProcess checkpoints after every step above, one rebuild):
        // nothing in this self-test's own code corrupts the heap - every checkpoint up through here
        // is clean. The "not allocated by _aligned routines" Debug Error only appears during the
        // process's NORMAL exit teardown (global/static destructors, DLL_PROCESS_DETACH for plugin
        // DLLs loaded above) - a pre-existing fragility unrelated to codegen, most likely a Debug
        // host CRT heap disagreeing with a Release-built plugin DLL's CRT heap (plugins are compiled
        // by a separate Release-by-default tool - see xgpu_plugin_compiler_debug_release memory) once
        // that DLL is unloaded. Terminating here instead of falling through to that teardown sidesteps
        // it entirely for this self-test's own purpose (verifying the codegen pipeline itself).
        TerminateProcess(GetCurrentProcess(), 0);
    }

    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = false, .m_bEnableRenderDoc = false, .m_pLogErrorFunc = nodeos::Debugger, .m_pLogWarning = nodeos::Debugger }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err)
        return xgpu::getErrorInt(Err);

    xgpu::window MainWindow;
    if (auto Err = Device.Create(MainWindow, {}); Err)
        return xgpu::getErrorInt(Err);

    xgpu::tools::imgui::CreateInstance(MainWindow);

    // Overrides ImGui's own default dark theme's blue accent (Button/Header/FrameBg/Tab/CheckMark/
    // SliderGrab/ResizeGrip/ScrollbarGrab all default to a saturated blue) with neutral dark grays,
    // matching the rest of this editor's own Unity-inspired chrome (theme::* above) - a real style
    // EDIT, not a PushStyleColor scope, since this is meant to hold for the app's entire lifetime,
    // not one widget/frame. E27 is the only example this build actually runs (see main.cpp), so
    // there's no other example's own look to preserve by scoping this more narrowly.
    {
        ImGuiStyle& Style = ImGui::GetStyle();
        Style.Colors[ImGuiCol_Button]              = ImVec4(0.24f, 0.24f, 0.24f, 1.0f);
        Style.Colors[ImGuiCol_ButtonHovered]       = ImVec4(0.32f, 0.32f, 0.32f, 1.0f);
        Style.Colors[ImGuiCol_ButtonActive]        = ImVec4(0.40f, 0.40f, 0.40f, 1.0f);
        Style.Colors[ImGuiCol_FrameBg]             = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
        Style.Colors[ImGuiCol_FrameBgHovered]      = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
        Style.Colors[ImGuiCol_FrameBgActive]       = ImVec4(0.24f, 0.24f, 0.24f, 1.0f);
        // A combo box's own closed button uses FrameBg, but the dropdown LIST it opens is a
        // separate ImGui color (PopupBg) - left at ImGui's own default (a different near-black,
        // slightly-transparent shade) it made every open dropdown visibly mismatch every other edit
        // box's background. Pinned to the exact same opaque color as FrameBg so every edit
        // surface - closed or open - reads as one consistent background.
        Style.Colors[ImGuiCol_PopupBg]             = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
        Style.Colors[ImGuiCol_Header]              = ImVec4(0.24f, 0.24f, 0.24f, 1.0f);
        Style.Colors[ImGuiCol_HeaderHovered]       = ImVec4(0.32f, 0.32f, 0.32f, 1.0f);
        Style.Colors[ImGuiCol_HeaderActive]        = ImVec4(0.40f, 0.40f, 0.40f, 1.0f);
        Style.Colors[ImGuiCol_CheckMark]           = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
        Style.Colors[ImGuiCol_SliderGrab]          = ImVec4(0.45f, 0.45f, 0.45f, 1.0f);
        Style.Colors[ImGuiCol_SliderGrabActive]    = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
        Style.Colors[ImGuiCol_Tab]                 = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
        Style.Colors[ImGuiCol_TabHovered]          = ImVec4(0.32f, 0.32f, 0.32f, 1.0f);
        Style.Colors[ImGuiCol_TabActive]           = ImVec4(0.33f, 0.33f, 0.33f, 1.0f);
        Style.Colors[ImGuiCol_TabUnfocused]        = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
        Style.Colors[ImGuiCol_TabUnfocusedActive]  = ImVec4(0.24f, 0.24f, 0.24f, 1.0f);
        Style.Colors[ImGuiCol_ResizeGrip]          = ImVec4(0.35f, 0.35f, 0.35f, 0.5f);
        Style.Colors[ImGuiCol_ResizeGripHovered]   = ImVec4(0.45f, 0.45f, 0.45f, 0.7f);
        Style.Colors[ImGuiCol_ResizeGripActive]    = ImVec4(0.55f, 0.55f, 0.55f, 0.9f);
        Style.Colors[ImGuiCol_ScrollbarGrab]       = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
        Style.Colors[ImGuiCol_ScrollbarGrabHovered]= ImVec4(0.45f, 0.45f, 0.45f, 1.0f);
        Style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
    }

    // Auto-discovered, not hardcoded: every Plugins/<Folder>/*.cpp here becomes an Add Node menu entry
    // immediately, in its not-yet-compiled state - dropping a new plugin folder in is the entire
    // integration step for a new native node kind.
    std::vector<nodeos::plugin_source_entry> Sources = nodeos::ScanPluginSources("D:/LIONant/xGPU/source/Examples/E27_NodeOS/Plugins");
    std::vector<nodeos::available_node_type> AvailableTypes;
    std::vector<nodeos::node_instance>       Nodes;
    std::vector<nodeos::link_instance>       Links;
    std::unordered_map<std::uint64_t, std::string> LiteralValues; // inline constants on unconnected scalar pins

    // There is always exactly one root spine living in exactly one root column - every other spine/
    // column this session ever creates starts out attached next to one of the existing ones via
    // CreateSpine. m_Y seeds at geo::TOP, same starting point as before any spine was ever dragged.
    std::vector<nodeos::spine>  Spines  { nodeos::spine {  xresource::guid_generator::Instance64(), 0, true, nodeos::geo::TOP } };
    std::vector<nodeos::column> Columns { nodeos::column { xresource::guid_generator::Instance64(), 0, 0, true } };
    Spines.front().m_ColumnId = Columns.front().m_Id;

    nodeos::mesh_preview_system MeshPreview;
    if (!MeshPreview.Init(Device))
        return 1;

    nodeos::canvas_drag       Drag;
    nodeos::canvas_selection  Selection;
    nodeos::canvas_view       View;
    nodeos::canvas_node_drag  NodeDrag;
    nodeos::canvas_spine_drag SpineDrag;
    nodeos::canvas_delete_spine_confirm DeleteSpineConfirm;

    bool bDirty = false; // persists across frames - see the deferred-execute comment below
    char GraphPathBuffer[260] = "D:/LIONant/xGPU/source/Examples/E27_NodeOS/graph.txt";
    std::string GraphStatus;

    // Read-only - this is generated output ("do not hand-edit" is right there in the file's own
    // first line), not something the user edits back into the graph. SetText() only happens right
    // after a "Compile to C++" click; the widget otherwise just keeps showing whatever it last held.
    TextEditor GeneratedCodeEditor;
    GeneratedCodeEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
    GeneratedCodeEditor.SetReadOnly(true);
    GeneratedCodeEditor.SetText("// Click \"Compile to C++\" to generate source here.\n");

    // Every graph mutation (add/delete node, connect, reorder, edit a property, change selection)
    // goes through this System - see the "Commands" sections above for why: it's the one entry point
    // with zero ImGui/xgpu dependency that a future headless runner or driver plugin could call
    // identically to how the ImGui code below calls it. bAutoLoadSave=false - a fresh undo stack each
    // run, since a stale on-disk history from a previous, differently-shaped graph would be more
    // confusing than useful for this example.
    nodeos::commands::node_os_command_context CmdContext{ Nodes, Links, Selection, Sources, AvailableTypes, bDirty, Spines, Columns, LiteralValues };
    xundo::system NodeOsUndo;
    if (auto Err = NodeOsUndo.Init("D:/LIONant/xGPU/source/Examples/E27_NodeOS/UndoHistory", false); !Err.empty())
        nodeos::Debugger(std::format("Node OS: xundo Init failed: {}", Err));
    nodeos::commands::create_node_cmd     CmdCreateNode(NodeOsUndo, &CmdContext);
    nodeos::commands::create_owned_pair_cmd CmdCreateOwnedPair(NodeOsUndo, &CmdContext);
    nodeos::commands::set_end_else_state_cmd CmdSetEndElseState(NodeOsUndo, &CmdContext);
    nodeos::commands::delete_nodes_cmd    CmdDeleteNodes(NodeOsUndo, &CmdContext);
    nodeos::commands::delete_link_cmd     CmdDeleteLink(NodeOsUndo, &CmdContext);
    nodeos::commands::connect_cmd         CmdConnect(NodeOsUndo, &CmdContext);
    nodeos::commands::set_literal_value_cmd CmdSetLiteralValue(NodeOsUndo, &CmdContext);
    nodeos::commands::reorder_nodes_cmd   CmdReorderNodes(NodeOsUndo, &CmdContext);
    nodeos::commands::move_nodes_to_spine_cmd CmdMoveNodesToSpine(NodeOsUndo, &CmdContext);
    nodeos::commands::set_properties_cmd  CmdSetProperties(NodeOsUndo, &CmdContext);
    nodeos::commands::select_cmd          CmdSelect(NodeOsUndo, &CmdContext);
    nodeos::commands::clear_selection_cmd CmdClearSelection(NodeOsUndo, &CmdContext);
    nodeos::commands::create_spine_cmd    CmdCreateSpine(NodeOsUndo, &CmdContext);
    nodeos::commands::delete_spine_cmd    CmdDeleteSpine(NodeOsUndo, &CmdContext);
    nodeos::commands::set_spine_position_cmd CmdSetSpinePosition(NodeOsUndo, &CmdContext);

    while (Instance.ProcessInputEvents())
    {
        if (xgpu::tools::imgui::BeginRendering(true))
            continue;

        // Ctrl+Z / Ctrl+Y (also Ctrl+Shift+Z for Redo) - guarded by WantTextInput so typing "z" into a
        // property text field never gets mistaken for an undo shortcut.
        if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Z) && !ImGui::GetIO().KeyShift) { NodeOsUndo.Undo(); bDirty = true; }
            else if (ImGui::IsKeyPressed(ImGuiKey_Y) || (ImGui::IsKeyPressed(ImGuiKey_Z) && ImGui::GetIO().KeyShift)) { NodeOsUndo.Redo(); bDirty = true; }
        }

        // Deferred to the TOP of the frame, before anything else touches MeshPreview: ExecuteGraph can
        // erase mesh_preview_system entries (RebuildIfMesh's null-value branch, e.g. when a link that
        // used to carry a mesh gets removed by a node/link deletion), which destroys the xgpu::texture
        // an ImGui::Image() call captured a raw pointer to. If ExecuteGraph ran AFTER DrawGraphCanvas
        // in the SAME frame that made the change, that pointer would already be sitting in this frame's
        // ImGui draw list, and Render() below would dereference it after it was freed - a real crash
        // reproduced by deleting a node with a live mesh flowing out of (or into) it. Running it here
        // instead means any erase happens before DrawPreviewSquare/ImGui::Image are ever called again,
        // so a pruned entry is simply never captured in the first place.
        if (bDirty)
        {
            nodeos::ExecuteGraph(Device, Nodes, Links, Spines, MeshPreview, LiteralValues);
            bDirty = false;
        }

        MeshPreview.RenderAll(MainWindow);

        // A fresh compile, a new/inserted node, a new/removed connection, a deletion, or a property
        // edit all mark this dirty so the graph re-runs (at the top of the NEXT frame, per above) and
        // every mesh preview reflects it - no manual "Execute Graph" click required for the common
        // case; the button below remains for a manual force-rerun.
        nodeos::DrawNodeLibraryPanel(Sources, AvailableTypes, bDirty);
        nodeos::DrawGraphCanvas(Sources, AvailableTypes, Nodes, Links, MeshPreview, Drag, Selection, View, NodeDrag, SpineDrag, DeleteSpineConfirm, Spines, Columns, LiteralValues, bDirty, NodeOsUndo);
        nodeos::DrawNodePropertiesPanel(Nodes, Selection.m_SelectedNodes, NodeOsUndo, Sources, AvailableTypes);
        nodeos::DrawRuntimeLogPanel();

        ImGui::SetNextWindowPos(ImVec2(300, 620), ImGuiCond_FirstUseEver);
        // Passing an explicit empty callback rather than relying on Render()'s own defaulted one -
        // MSVC independently re-evaluates a defaulted decltype([](){}) template default argument at
        // each call site, producing two DIFFERENT closure types for the same call and a hard error.
        GeneratedCodeEditor.Render("Generated C++##codegen", ImVec2(600, 300), true, [](){});

        ImGui::SetNextWindowPos(ImVec2(1265, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(200, 80), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Run"))
        {
            if (ImGui::Button("Execute Graph"))
                bDirty = true; // same deferred path, not an immediate call - see the comment above

            // Generates real C++ from the current graph, compiles it into a genuinely standalone
            // .exe (NODE_SCRIPTING_DESIGN.md's stated end goal, as opposed to Execute Graph's own
            // in-editor interpreter), runs it, and reports the actual captured output - not just
            // "it compiled." Immediate, not deferred through bDirty, since codegen never touches
            // MeshPreview/GPU textures the way ExecuteGraph does.
            if (ImGui::Button("Compile to C++"))
            {
                const std::string GeneratedSource = nodeos::GenerateCpp(Nodes, Links, Spines, LiteralValues);
                GeneratedCodeEditor.SetText(GeneratedSource);
                const auto CodegenResult = nodeos::CompileAndRunGeneratedCpp(GeneratedSource);
                nodeos::GetRuntimeLog().clear();
                nodeos::GetRuntimeLog().push_back(std::format("[codegen] source: {}", CodegenResult.m_SourcePath));
                if (!CodegenResult.m_bCompileOk)
                {
                    nodeos::GetRuntimeLog().push_back("[codegen] COMPILE FAILED:");
                    nodeos::GetRuntimeLog().push_back(CodegenResult.m_CompileLog);
                }
                else
                {
                    nodeos::GetRuntimeLog().push_back("[codegen] compiled OK - actual program output:");
                    nodeos::GetRuntimeLog().push_back(CodegenResult.m_RunOutput);
                }
            }

            ImGui::Separator();

            // Undo/Redo, plus a dropdown over the FULL history (not just one step at a time) - every
            // entry is the exact command string that was executed (the same one an AI agent driving
            // this through a future "command source" plugin would see/issue), so this doubles as a
            // plain-text audit trail of the session, not just an undo control.
            {
                const int UndoIndex = NodeOsUndo.GetUndoIndex();
                const std::size_t HistoryCount = NodeOsUndo.GetHistoryCount();

                ImGui::BeginDisabled(UndoIndex == 0);
                if (ImGui::Button("Undo")) { NodeOsUndo.Undo(); bDirty = true; }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(UndoIndex >= (int)HistoryCount);
                if (ImGui::Button("Redo")) { NodeOsUndo.Redo(); bDirty = true; }
                ImGui::EndDisabled();

                ImGui::SetNextItemWidth(-1);
                const std::string Preview = (UndoIndex > 0) ? NodeOsUndo.GetHistoryDisplayString((std::size_t)UndoIndex - 1) : std::string("(nothing to undo)");
                if (ImGui::BeginCombo("##History", Preview.c_str()))
                {
                    if (HistoryCount == 0)
                        ImGui::TextDisabled("No commands yet.");
                    for (std::size_t i = 0; i < HistoryCount; ++i)
                    {
                        // Selecting an entry jumps the WHOLE timeline to "everything through this
                        // command has been applied" - i.e. this command becomes the new top of the
                        // undo stack, matching what clicking a step in a history panel means in most
                        // editors (Photoshop/Word's undo dropdown, etc). Only top-level entries are
                        // selectable this way - a GROUP command (System.Execute(name, {sub-commands}),
                        // none of Node OS's own commands currently use one, but the tree rendering below
                        // supports it generically) is one atomic undo step, so its sub-commands are shown
                        // as an expandable tree underneath purely for visibility, never as their own
                        // jump targets.
                        const bool bApplied  = (int)i < UndoIndex; // still in effect vs already undone
                        const bool bIsCurrent = ((int)i == UndoIndex - 1);
                        if (!bApplied) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

                        const std::string Label = std::format("[{:03}] {}", i, NodeOsUndo.GetHistoryCommandString(i));
                        if (NodeOsUndo.IsHistoryGroup(i))
                        {
                            ImGui::PushID((int)i);
                            const bool bOpen = ImGui::TreeNodeEx(Label.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | (bIsCurrent ? ImGuiTreeNodeFlags_Selected : 0));
                            // OpenOnArrow means clicking the arrow toggles open/closed without also
                            // counting as "clicked" here - IsItemToggledOpen() tells the two apart, so
                            // expanding the tree to look at it never jumps the undo position by accident.
                            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) { NodeOsUndo.JumpTo((int)i + 1); bDirty = true; }
                            if (bOpen)
                            {
                                for (std::size_t j = 0; j < NodeOsUndo.GetHistorySubCommandCount(i); ++j)
                                    ImGui::BulletText("%s", NodeOsUndo.GetHistorySubCommandString(i, j).c_str());
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }
                        else if (ImGui::Selectable(Label.c_str(), bIsCurrent))
                        {
                            NodeOsUndo.JumpTo((int)i + 1);
                            bDirty = true;
                        }
                        if (!bApplied) ImGui::PopStyleColor();
                    }
                    ImGui::EndCombo();
                }
            }

            ImGui::Separator();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##GraphPath", GraphPathBuffer, sizeof(GraphPathBuffer));
            if (ImGui::Button("Save"))
                GraphStatus = nodeos::SaveGraph(GraphPathBuffer, Nodes, Links, AvailableTypes, Spines, Columns, LiteralValues) ? "Saved." : "Save failed - see log.";
            ImGui::SameLine();
            if (ImGui::Button("Load"))
            {
                Selection.m_SelectedNodes.clear();
                Selection.m_SelectedLink = 0;
                Selection.m_SelectedGapSpineId = 0;
                Selection.m_SelectedGapIndex   = -1;
                GraphStatus = nodeos::LoadGraph(GraphPathBuffer, Nodes, Links, Sources, AvailableTypes, Spines, Columns, LiteralValues) ? "Loaded." : "Load failed - see log.";
                bDirty = true; // re-run the freshly loaded graph, same deferred path as everything else
                // Load replaces Nodes/Links wholesale (not through commands), so any existing undo
                // history refers to node/link ids that may no longer mean anything in the new graph -
                // clear it rather than let Ctrl+Z do something confusing against unrelated state.
                NodeOsUndo.Reset();
            }
            if (!GraphStatus.empty())
                ImGui::TextDisabled("%s", GraphStatus.c_str());
        }
        ImGui::End();

        xgpu::tools::imgui::Render();
        MainWindow.PageFlip();
    }

    return 0;
}
