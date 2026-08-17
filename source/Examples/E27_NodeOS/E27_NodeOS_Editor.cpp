#include "source/xGPU.h"
#include "source/tools/xgpu_imgui_breach.h"
#include "source/tools/xgpu_view.h"

#include "source/Examples/E27_NodeOS/SDK/xnode_os_plugin_api.h"
#include "source/Examples/E27_NodeOS/SDK/xnode_os_host_interface.h"
#include "source/Examples/E27_NodeOS/SDK/xnode_os_reflected_object.h"
#include "source/Examples/E27_NodeOS/SDK/xnode_os_shared_types.h"

// For the whole graph file (Nodes/Links/xProperties records) - see SaveGraph/LoadGraph/
// SerializeReflectedMembers below. The host never needs real xproperty types itself: property
// serialization walks a plugin's ixnode_os_reflected_object view generically, the same ABI-safe
// primitives the property panel already draws with - xtextfile only needs to know about that fixed
// atomic vocabulary, which belongs to the host, not to any individual plugin.
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
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <unordered_map>
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
        HMODULE                             m_Module = nullptr;
        const xnode_os_node_type_desc*      m_pDesc  = nullptr;
        std::string                         m_SourcePath;    // the plugin_source_entry this came from - only for recompiling; not an identity
        std::string                         m_DirName;       // the plugin's Plugins/<DirName>/ folder name - the actual identity (see plugin_source_entry)
    };

    //------------------------------------------------------------------------------------------------
    // One instance of a node type dropped on the canvas. Holds the type descriptor DIRECTLY (not an
    // index into AvailableTypes): the module it points into is never FreeLibrary'd, even across a
    // plugin recompile (see CompileAndLoadPlugin), so the pointer stays valid for this instance's
    // whole life - and unlike a positional index, pruning AvailableTypes on recompile can never
    // silently repoint an existing node at the wrong descriptor.
    //------------------------------------------------------------------------------------------------
    struct node_instance
    {
        std::uint64_t                    m_Id = 0;
        const xnode_os_node_type_desc*   m_pType = nullptr;
        void*                            m_pProperties = nullptr; // this instance's own property block - see xnode_os_node_type_desc's comment
        int                              m_Order = 0;         // stacking rank (rslgraph-ui's NodeDef::order) - reorder with the header's up/down buttons, never freely dragged
        std::vector<void*>                m_CachedOutputs;      // filled after a successful Execute
        bool                              m_bHasRun = false;
        std::string                       m_LastError;
    };

    struct link_instance
    {
        std::uint64_t m_Id = 0;
        std::uint64_t m_SourceNode = 0; int m_SourceOutput = 0;
        std::uint64_t m_TargetNode = 0; int m_TargetInput  = 0;
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
        bool                                          m_bSuccess = false;
        std::string                                    m_Log;
        HMODULE                                        m_Module = nullptr;
        std::vector<const xnode_os_node_type_desc*>    m_Types;
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
    // The host's own implementation of the interface handed to a plugin's NodeOS_OnLoad. One fresh
    // instance per compile/load call: RegisterNodeType just collects into m_Collected, rejecting any
    // descriptor whose own ABI version doesn't match this host build's - the cheap insurance
    // xnode_os_plugin_api.h describes, so a stale-header plugin's registration is refused outright
    // instead of the host reading past the end of a struct shape it doesn't actually match.
    //------------------------------------------------------------------------------------------------
    class host_bridge final : public ixnode_os_host
    {
    public:
        std::vector<const xnode_os_node_type_desc*> m_Collected;

        int GetAbiVersion() const noexcept override { return XNODE_OS_ABI_VERSION; }

        void RegisterNodeType(const xnode_os_node_type_desc* pDesc) noexcept override
        {
            if (!pDesc) return;
            if (pDesc->m_AbiVersion != XNODE_OS_ABI_VERSION)
            {
                Debugger(std::format("Node OS: rejected a node type registered with ABI version {} (host expects {})", pDesc->m_AbiVersion, XNODE_OS_ABI_VERSION));
                return;
            }
            m_Collected.push_back(pDesc);
        }

        void Log(const char* pMessage) noexcept override
        {
            if (pMessage) Debugger(pMessage);
        }

        void* GetImGuiContext() const noexcept override
        {
            return ImGui::GetCurrentContext();
        }

        void GetImGuiAllocatorFunctions(void** ppAllocFunc, void** ppFreeFunc, void** ppUserData) const noexcept override
        {
            ImGuiMemAllocFunc AllocFunc; ImGuiMemFreeFunc FreeFunc; void* pUserData;
            ImGui::GetAllocatorFunctions(&AllocFunc, &FreeFunc, &pUserData);
            *ppAllocFunc = reinterpret_cast<void*>(AllocFunc);
            *ppFreeFunc  = reinterpret_cast<void*>(FreeFunc);
            *ppUserData  = pUserData;
        }
    };

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
    // Shells out to the local MSVC toolchain to turn one plugin .cpp into a DLL, right now, while this
    // program is running - then LoadLibrary's it and calls its NodeOS_OnLoad, handing it a host_bridge
    // to register whatever node types it exports through. Pure: touches no shared state, safe to run
    // on a background thread (see the async path below) as well as inline.
    //------------------------------------------------------------------------------------------------
    static plugin_compile_result CompilePluginWorker(std::string SourcePath)
    {
        plugin_compile_result Result;
        namespace fs = std::filesystem;
        const fs::path Src        = SourcePath;
        const fs::path OutputDir  = fs::path("D:/LIONant/xGPU/source/Examples/E27_NodeOS/CompiledPlugins");
        const fs::path DllPath    = OutputDir / (Src.stem().string() + ".dll");
        const fs::path BatPath    = OutputDir / (Src.stem().string() + "_compile.bat");
        const fs::path LogPath    = OutputDir / (Src.stem().string() + "_compile.log");

        std::error_code Ec;
        fs::create_directories(OutputDir, Ec);

        // A .bat is far more robust than trying to hand-escape a single system() command line with
        // spaces in "Program Files" and nested quotes. Two /I's: the SDK folder (so a plugin can write
        // bare `#include "xnode_os_plugin_api.h"`) and the repo root (so a plugin wanting properties
        // can write the exact same `#include "dependencies/xproperty/source/xcore/my_properties.h"`
        // line every other example in this engine already uses).
        {
            std::ofstream Bat(BatPath);
            Bat << "@echo off\r\n";
            Bat << GetOrBuildVsEnvSetup(); // cached - no vcvarsall.bat/vswhere re-run on every compile
            // /MDd + the same WIN32/_WINDOWS/_DEBUG/UNICODE defines as xGPU_unit_test's own Debug
            // config: cheap insurance for any plugin that opts into m_pDrawProperties (see
            // xnode_os_plugin_api.h) and compiles its own copy of imgui.cpp/xPropertyImGuiInspector.cpp
            // - keeping the build environment identical to the host's own is what makes the shared
            // ImGuiContext this enables safe to dereference from the plugin's own compiled code.
            // The imgui/ folder itself (not just the repo root) is on this line because
            // xPropertyImGuiInspector.cpp does a bare #include "imgui_internal.h" - it expects to be
            // compiled with the same include-path shape the host's own project gives it. Same reason
            // for dependencies/xerr: xtextfile.h does a bare #include "source/xerr.h", resolving
            // relative to xerr's OWN folder being on the include path, exactly like the host's project.
            Bat << "cl.exe /nologo /LD /EHsc /std:c++20 /MDd /DWIN32 /D_WINDOWS /D_DEBUG /DUNICODE /D_UNICODE /I\"D:\\LIONant\\xGPU\\source\\Examples\\E27_NodeOS\\SDK\" /I\"D:\\LIONant\\xGPU\\dependencies\\imgui\" /I\"D:\\LIONant\\xGPU\\dependencies\\xerr\" /I\"D:\\LIONant\\xGPU\" \"" << Src.string() << "\" /Fe:\"" << DllPath.string() << "\" /Fo:\"" << (OutputDir / (Src.stem().string() + ".obj")).string() << "\" > \"" << LogPath.string() << "\" 2>&1\r\n";
        }

        const std::string Command = std::format("\"{}\"", BatPath.string());
        const int ExitCode = std::system(Command.c_str());

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

        auto pOnLoad = (xnode_os_pfn_on_load)GetProcAddress(Module, XNODE_OS_ON_LOAD_NAME);
        if (!pOnLoad)
        {
            Result.m_Log += "\n[DLL loaded but does not export " XNODE_OS_ON_LOAD_NAME "]";
            FreeLibrary(Module);
            return Result;
        }

        host_bridge Bridge;
        if (!pOnLoad(&Bridge))
        {
            Result.m_Log += "\n[NodeOS_OnLoad returned false - plugin declined to initialize]";
            FreeLibrary(Module);
            return Result;
        }

        Result.m_Log += std::format("\n[compiled and loaded successfully - {} node type(s) registered]", Bridge.m_Collected.size());
        Result.m_bSuccess = true;
        Result.m_Module   = Module;
        Result.m_Types    = std::move(Bridge.m_Collected);
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
        // adding its fresh registrations. The old module is never FreeLibrary'd - anything still
        // referencing it (an already-placed node instance's raw xnode_os_node_type_desc* included)
        // keeps working against it indefinitely; this only stops the stale entry from being offered
        // again in the Add Node menu.
        if (Entry.m_Module)
            std::erase_if(OutTypes, [&](auto& T) { return T.m_Module == Entry.m_Module; });

        for (auto* pDesc : Result.m_Types)
            OutTypes.push_back({ std::format("{} :: {}", Entry.m_DisplayName, pDesc->m_pName), Result.m_Module, pDesc, Entry.m_SourcePath, Entry.m_DirName });

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
    static const xnode_os_node_type_desc* EnsureLoadedAndGetType(plugin_source_entry& Source, std::vector<available_node_type>& AvailableTypes)
    {
        if (!Source.m_bLoaded)
        {
            const std::size_t Before = AvailableTypes.size();
            if (!CompileAndLoadPlugin(Source, AvailableTypes) || AvailableTypes.size() <= Before) return nullptr;
            return AvailableTypes[Before].m_pDesc;
        }
        const std::string Prefix = Source.m_DisplayName + " :: ";
        for (auto& T : AvailableTypes)
            if (T.m_DisplayName.rfind(Prefix, 0) == 0) return T.m_pDesc;
        return nullptr;
    }

    //------------------------------------------------------------------------------------------------
    // Every node-add path (right-click canvas, spine insert marker, the empty-canvas "+") funnels
    // through here so property allocation never gets forgotten at one of them - and its mirror,
    // destroying that same block when a node is removed.
    //------------------------------------------------------------------------------------------------
    static node_instance CreateNodeInstance(std::uint64_t Id, const xnode_os_node_type_desc* pType, int Order)
    {
        node_instance NewNode;
        NewNode.m_Id    = Id;
        NewNode.m_pType = pType;
        NewNode.m_Order = Order;
        if (pType && pType->m_PropertyStructSize > 0 && pType->m_pCreateDefaultProperties)
            NewNode.m_pProperties = pType->m_pCreateDefaultProperties();
        return NewNode;
    }

    static void DestroyNodeProperties(node_instance& Node)
    {
        if (Node.m_pType && Node.m_pProperties && Node.m_pType->m_pDestroyProperties)
            Node.m_pType->m_pDestroyProperties(Node.m_pProperties);
        Node.m_pProperties = nullptr;
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
        inline std::string MakeDeleteNodes(const std::vector<std::uint64_t>& Ids) { return std::format("DeleteNodes -Ids {}", JoinIds(Ids)); }
        inline std::string MakeDeleteLink(std::uint64_t Id) { return std::format("DeleteLink -Id {}", FormatGuid(Id)); }
        inline std::string MakeConnect(std::uint64_t Id, std::uint64_t SourceNode, int SourceOutput, std::uint64_t TargetNode, int TargetInput)
        {
            return std::format("Connect -Id {} -SourceNode {} -SourceOutput {} -TargetNode {} -TargetInput {}"
                               , FormatGuid(Id), FormatGuid(SourceNode), SourceOutput, FormatGuid(TargetNode), TargetInput);
        }
        inline std::string MakeReorderNodes(const std::vector<std::uint64_t>& NewOrder) { return std::format("ReorderNodes -Ids {}", JoinIds(NewOrder)); }
        inline std::string MakeSetProperties(std::uint64_t NodeId, const std::string& Before, const std::string& After)
        {
            return std::format("SetProperties -NodeId {} -Before {} -After {}", FormatGuid(NodeId), Base64Encode(Before), Base64Encode(After));
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
        inline std::string MakeClearSelection() { return "ClearSelection"; }

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
        if (!pValue) { return "(none)"; }
        if (std::strcmp(pTypeName, "Text") == 0) { return static_cast<const char*>(pValue); }
        if (std::strcmp(pTypeName, "Mesh") == 0)
        {
            auto* pMesh = static_cast<xnode_os_mesh_data*>(pValue);
            s_Scratch = std::format("{} verts / {} tris", pMesh->m_VertexCount, pMesh->m_IndexCount / 3);
            return s_Scratch.c_str();
        }
        s_Scratch = std::format("<{:#x}>", (std::uintptr_t)pValue);
        return s_Scratch.c_str();
    }

    // Whatever cached output feeds a given node's input pin right now (nullptr if unconnected or
    // the source hasn't run) - used both by the text preview and the mesh render preview below.
    static void* GetInputValue(std::uint64_t NodeId, int InputIndex, const std::vector<node_instance>& Nodes, const std::vector<link_instance>& Links)
    {
        for (auto& Link : Links)
        {
            if (Link.m_TargetNode != NodeId || Link.m_TargetInput != InputIndex) continue;
            auto SourceIt = std::find_if(Nodes.begin(), Nodes.end(), [&](auto& N) { return N.m_Id == Link.m_SourceNode; });
            if (SourceIt == Nodes.end() || !SourceIt->m_bHasRun) return nullptr;
            return (Link.m_SourceOutput < (int)SourceIt->m_CachedOutputs.size()) ? SourceIt->m_CachedOutputs[Link.m_SourceOutput] : nullptr;
        }
        return nullptr;
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
                ImGui::GetWindowDrawList()->AddRectFilled(P0, P1, IM_COL32(6, 9, 20, 255), 4.0f * Scale);
                ImGui::GetWindowDrawList()->AddRect(P0, P1, IM_COL32(51, 65, 85, 255), 4.0f * Scale);
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
        constexpr float GLYPH           = 9.0f;
        constexpr float ICON_CLEARANCE  = 16.0f;
        constexpr float LANE_GAP        = 14.0f;
        constexpr float PORT_HIT_RADIUS = 16.0f;
        constexpr float LINK_HIT_DIST   = 6.0f;
        constexpr float PREVIEW_GAP     = 10.0f;
    }

    // Every port on a node in one flat, row-ordered list (inputs then outputs) - rslgraph-ui's own
    // NodeDef::ports is a single flat array regardless of direction; this is the equivalent view
    // over our ABI's separate input/output arrays.
    struct port_ref { bool m_bIsOutput; int m_Index; const xnode_os_port_desc* m_pDesc; };
    static std::vector<port_ref> FlatPorts(const xnode_os_node_type_desc* pDesc)
    {
        std::vector<port_ref> Out;
        for (int i = 0; i < pDesc->m_InputCount;  ++i) Out.push_back({ false, i, &pDesc->m_pInputs[i] });
        for (int i = 0; i < pDesc->m_OutputCount; ++i) Out.push_back({ true,  i, &pDesc->m_pOutputs[i] });
        return Out;
    }
    static std::uint64_t PinOf(const port_ref& P, std::uint64_t NodeId) { return P.m_bIsOutput ? OutPinOf(NodeId, P.m_Index) : InPinOf(NodeId, P.m_Index); }
    static bool IsMeshType(const char* pType) noexcept { return std::strcmp(pType, "Mesh") == 0; }
    static ImU32 TypeColor(const char* pType) noexcept
    {
        if (IsMeshType(pType))                    return IM_COL32(167, 139, 250, 255);
        if (std::strcmp(pType, "Text") == 0)      return IM_COL32(74, 222, 128, 255);
        return IM_COL32(148, 163, 184, 255);
    }
    // A Mesh-typed port's live render lives in one shared preview block at the TOP of the node (right
    // under the header), not inline per-row - so its row never prints a value-preview line below the
    // glyph and doesn't need the VALUE_LINE_H space reserved for one (leaving it in produced a visible
    // gap between a Mesh row and whatever row follows it).
    static float RowHeight(const port_ref& P) noexcept { return geo::ROW_H + (IsMeshType(P.m_pDesc->m_pTypeName) ? 0.0f : geo::VALUE_LINE_H); }
    static int MeshPortCount(const xnode_os_node_type_desc* pDesc)
    {
        int Count = 0;
        for (auto& P : FlatPorts(pDesc)) if (IsMeshType(P.m_pDesc->m_pTypeName)) ++Count;
        return Count;
    }
    static float PreviewAreaHeight(const xnode_os_node_type_desc* pDesc)
    {
        const int Count = MeshPortCount(pDesc);
        return Count > 0 ? Count * (mesh_preview_system::s_PreviewSize + geo::PREVIEW_GAP) + geo::PREVIEW_GAP : 0.0f;
    }
    static float NodeWidth(const xnode_os_node_type_desc* pDesc)
    {
        float NameW = ImGui::CalcTextSize(pDesc->m_pName).x;
        float PortColW = 40.0f;
        for (auto& P : FlatPorts(pDesc))
        {
            NameW = std::max(NameW, ImGui::CalcTextSize(P.m_pDesc->m_pName).x);
            const std::string TypeLabel = std::string("[") + P.m_pDesc->m_pTypeName + "]";
            PortColW = std::max(PortColW, ImGui::CalcTextSize(TypeLabel.c_str()).x + geo::PORT_PAD);
        }
        const float MinForPreview = MeshPortCount(pDesc) > 0 ? mesh_preview_system::s_PreviewSize + 24.0f : 0.0f;
        return std::max(NameW + 2.0f * PortColW + 40.0f, MinForPreview);
    }
    static float NodeHeight(const xnode_os_node_type_desc* pDesc)
    {
        float H = geo::HEADER_H + PreviewAreaHeight(pDesc);
        for (auto& P : FlatPorts(pDesc)) H += RowHeight(P);
        return H + geo::NODE_PAD_BOTTOM;
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
        int                      m_SelectedGap  = -1; // a spine slot, selected the same way a node is -
                                                        // -1 means none. Future copy/paste targets this.
    };

    // Drag-to-reorder: picking up a node (or, if it's part of the current selection, the whole
    // selection) and dropping it on a spine "+" marker moves it to that stacking position - separate
    // from canvas_drag above, which is pin-to-pin wiring.
    struct canvas_node_drag
    {
        bool                        m_bActive = false;
        std::vector<std::uint64_t> m_MovingIds;
    };

    // Zoom (mouse wheel, anchored under the cursor) and vertical pan (left-drag on empty canvas space)
    // - the canvas has no native ImGui scrollbar, so this is the only way to navigate a graph taller
    // than the window. Deliberately no horizontal pan/zoom-anchor: the stack always centers on the
    // window (point 4 of the review that shaped this) - only vertical position and scale are ever
    // adjustable.
    struct canvas_view
    {
        float m_Zoom = 1.0f;
        float m_PanY = 0.0f;
        bool  m_bPanDragActive = false; // true from a right-press starting inside the canvas until the
                                         // right button releases - tracked by hand (see DrawGraphCanvas)
                                         // rather than via ImGui's own item/active-id system, since that
                                         // slot is already claimed while a pin-to-pin/node-reorder drag
                                         // (left button) is in progress, and panning needs to keep
                                         // working through that.
    };

    //------------------------------------------------------------------------------------------------
    static void DrawGraphCanvas(std::vector<plugin_source_entry>& Sources, std::vector<available_node_type>& AvailableTypes, std::vector<node_instance>& Nodes
                               , std::vector<link_instance>& Links, mesh_preview_system& MeshPreview
                               , canvas_drag& Drag, canvas_selection& Selection, canvas_view& View
                               , canvas_node_drag& NodeDrag, bool& bDirty, xundo::system& System)
    {
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
                        if (EnsureLoadedAndGetType(Src, AvailableTypes))
                            commands::Run(System, commands::MakeCreateNodeAppend(xresource::guid_generator::Instance64(), Src.m_DirName));
                    }
                ImGui::EndPopup();
            }
            ImGui::End();
            return;
        }

        ImGui::TextDisabled("Click a + to add/insert a node | drag pin-to-pin to wire | drag a node onto a + to move it | right-drag empty space to pan, wheel to zoom | Delete to remove");
        const float AvailWidth = ImGui::GetContentRegionAvail().x;

        auto FindNode = [&](std::uint64_t Id) -> node_instance* { auto It = std::find_if(Nodes.begin(), Nodes.end(), [&](auto& N) { return N.m_Id == Id; }); return It == Nodes.end() ? nullptr : &*It; };
        auto DescOf   = [&](node_instance* pN) -> const xnode_os_node_type_desc* { return pN ? pN->m_pType : nullptr; };

        // ---- stacking order: sorted by m_Order, never freely dragged (rslgraph-ui's layout.ts) ----
        std::vector<std::uint64_t> Order;
        for (auto& N : Nodes) Order.push_back(N.m_Id);
        std::sort(Order.begin(), Order.end(), [&](std::uint64_t A, std::uint64_t B) { return FindNode(A)->m_Order < FindNode(B)->m_Order; });
        auto OrderIndex = [&](std::uint64_t Id) -> int { auto It = std::find(Order.begin(), Order.end(), Id); return It == Order.end() ? 0 : (int)(It - Order.begin()); };

        struct row_layout { std::uint64_t m_NodeId; float m_X, m_Y, m_W, m_H; };
        std::vector<row_layout> Layout;
        float WidestNode = 120.0f;
        // A leading NODE_GAP (instead of starting flush at TOP) reserves room for the "insert before
        // the first node" spine marker below, so it sits in a real gap identical in size to every
        // between-node gap rather than being crammed against the window edge.
        float CursorY = geo::TOP + geo::NODE_GAP;
        for (auto Id : Order)
        {
            auto* pDesc = DescOf(FindNode(Id));
            if (!pDesc) continue;
            const float W = NodeWidth(pDesc), H = NodeHeight(pDesc);
            WidestNode = std::max(WidestNode, W);
            Layout.push_back({ Id, 0.0f, CursorY, W, H });
            CursorY += H + geo::NODE_GAP;
        }
        const float SpineX = std::max(AvailWidth * 0.5f, 260.0f); // centered on the window, never so narrow the highways collide
        for (auto& R : Layout) R.m_X = SpineX - R.m_W * 0.5f;
        const float TotalH       = CursorY + 20.0f;
        const float HighwayBase  = WidestNode * 0.5f + geo::ICON_CLEARANCE;
        auto FindRow = [&](std::uint64_t Id) -> row_layout* { auto It = std::find_if(Layout.begin(), Layout.end(), [&](auto& R) { return R.m_NodeId == Id; }); return It == Layout.end() ? nullptr : &*It; };

        // Insert a new node instance at stacking position GapIndex (0 = before everything, Order.size()
        // = after everything, i = between Order[i-1] and Order[i]) - renumbers every node's m_Order to
        // its dense index in the resulting stack rather than doing arithmetic on the existing m_Order
        // values, since deleting a node can leave those with gaps. Takes the plugin SOURCE, not a type
        // descriptor directly, so the very first placement of a not-yet-compiled type can still
        // compile+load it lazily (EnsureLoadedAndGetType).
        auto InsertNodeAt = [&](int GapIndex, plugin_source_entry& Src)
        {
            if (!EnsureLoadedAndGetType(Src, AvailableTypes)) return;
            const auto NewId = xresource::guid_generator::Instance64();
            // Addressed relative to whichever EXISTING node currently sits at this gap - see
            // create_node_cmd's own comment for why (node ids are already known/observable, an
            // invented "gap id" would need its own discovery step). Order is non-empty here (this
            // lambda is only reachable once Layout is non-empty).
            const int Clamped = std::clamp(GapIndex, 0, (int)Order.size());
            const std::string Cmd = (Clamped < (int)Order.size())
                ? commands::MakeCreateNodeBefore(NewId, Src.m_DirName, Order[Clamped])
                : commands::MakeCreateNodeAfter(NewId, Src.m_DirName, Order.back());
            commands::Run(System, Cmd);
        };

        // Move an already-existing set of nodes (a drag-and-drop reorder) to stacking position
        // GapIndex, same dense-renumber approach as InsertNodeAt - GapIndex is expressed against the
        // ORIGINAL Order, so it's adjusted by however many of the moving nodes sat before it there.
        // Pure reorder: no bDirty, since it doesn't change what's connected to what.
        auto MoveNodesTo = [&](const std::vector<std::uint64_t>& MovingIds, int GapIndex)
        {
            std::vector<std::uint64_t> MovingInOrder, Remaining;
            for (auto Id : Order)
            {
                if (std::find(MovingIds.begin(), MovingIds.end(), Id) != MovingIds.end()) MovingInOrder.push_back(Id);
                else Remaining.push_back(Id);
            }
            if (MovingInOrder.empty()) return;
            int Adjust = 0;
            for (int i = 0; i < GapIndex && i < (int)Order.size(); ++i)
                if (std::find(MovingIds.begin(), MovingIds.end(), Order[i]) != MovingIds.end()) ++Adjust;
            const int NewGapIndex = std::clamp(GapIndex - Adjust, 0, (int)Remaining.size());
            Remaining.insert(Remaining.begin() + NewGapIndex, MovingInOrder.begin(), MovingInOrder.end());
            commands::Run(System, commands::MakeReorderNodes(Remaining));
        };

        // ---- per-port side (L/R): chosen by wire direction, so a wire never crosses over its own
        // destination node - the source's output and the target's input both take the side that
        // matches whether the wire travels down (R) or up (L) the stack (Canvas.tsx's `state` memo).
        // A link's highway side is a pure per-link fact (which way it travels in the stack) - computing
        // it fresh wherever needed, rather than caching it keyed only by pin, is what fixes the "a pin
        // with wires going both up and down only ever renders on one side" bug: a single pin can need
        // BOTH sides at once (one link outgoing up, another down), so anything keyed purely by pin id
        // can only ever remember the last link that touched it.
        auto LinkSide = [&](const link_instance& L) -> char { return (OrderIndex(L.m_TargetNode) >= OrderIndex(L.m_SourceNode)) ? 'R' : 'L'; };

        // Which side(s) a given pin actually needs a glyph rendered on - a set, not a single side, so a
        // pin used by links going both directions gets two glyphs, one per side actually in use.
        std::unordered_map<std::uint64_t, std::set<char>> PortSides;
        for (auto& Link : Links)
        {
            const char S = LinkSide(Link);
            PortSides[OutPinOf(Link.m_SourceNode, Link.m_SourceOutput)].insert(S);
            PortSides[InPinOf(Link.m_TargetNode, Link.m_TargetInput)].insert(S);
        }
        // Every rendered side for an unconnected pin defaults to 'R', matching this codebase's existing
        // convention for a pin nothing has touched yet.
        auto SidesOf = [&](std::uint64_t PinId) -> std::set<char>
        {
            auto It = PortSides.find(PinId);
            return It == PortSides.end() ? std::set<char>{'R'} : It->second;
        };

        auto HighwayX = [&](char S, int Lane) { const float D = HighwayBase + Lane * geo::LANE_GAP; return S == 'L' ? SpineX - D : SpineX + D; };
        // Takes the side explicitly now - a port can have two valid anchor points (one per side it's
        // rendered on), so "the" anchor no longer makes sense without saying which one.
        auto PortAnchor = [&](std::uint64_t NodeId, const port_ref& P, char S) -> ImVec2
        {
            auto* pRow = FindRow(NodeId); auto* pDesc = DescOf(FindNode(NodeId));
            if (!pRow || !pDesc) return {};
            float Y = pRow->m_Y + geo::HEADER_H + PreviewAreaHeight(pDesc);
            for (auto& Q : FlatPorts(pDesc))
            {
                // Half of ROW_H specifically, matching the drawing loop's own CenterY (RowY +
                // ROW_H*0.5) - NOT half of RowHeight(), which also counts the value-line space below
                // the glyph and would anchor wires visibly below the actual drawn pin.
                if (Q.m_bIsOutput == P.m_bIsOutput && Q.m_Index == P.m_Index) { Y += geo::ROW_H * 0.5f; break; }
                Y += RowHeight(Q);
            }
            return { (S == 'L') ? pRow->m_X : pRow->m_X + pRow->m_W, Y };
        };

        // ---- lane packing per side: greedy interval partitioning, NOT rslgraph-ui's own laneOf (which
        // is just a stateless per-side counter - order of appearance, nothing more; verified directly
        // in Canvas.tsx). Sorting by Y-span length before assigning means a short/local hop always gets
        // first pick of the innermost lane, and only actually claims a new lane when it truly overlaps
        // something already there - so nearby connections hug the spine and far-traveling ones are the
        // only thing pushed outward, and any lane that's genuinely free gets reused instead of growing
        // the lane count.
        struct link_lane_interval { float m_Lo, m_Hi; };
        std::vector<std::vector<link_lane_interval>> LaneIntervals[2]; // [0]=L, [1]=R
        std::unordered_map<std::uint64_t, int> LaneOfLink;
        {
            struct link_span { std::uint64_t m_LinkId; int m_Side; float m_Lo, m_Hi; };
            std::vector<link_span> Spans;
            for (auto& Link : Links)
            {
                auto* pSrcDesc = DescOf(FindNode(Link.m_SourceNode)); auto* pDstDesc = DescOf(FindNode(Link.m_TargetNode));
                if (!pSrcDesc || !pDstDesc || Link.m_SourceOutput >= pSrcDesc->m_OutputCount || Link.m_TargetInput >= pDstDesc->m_InputCount) continue;
                const port_ref OutP{ true, Link.m_SourceOutput, &pSrcDesc->m_pOutputs[Link.m_SourceOutput] };
                const port_ref InP { false, Link.m_TargetInput,  &pDstDesc->m_pInputs[Link.m_TargetInput] };
                const char LSide = LinkSide(Link);
                const float FromY = PortAnchor(Link.m_SourceNode, OutP, LSide).y, ToY = PortAnchor(Link.m_TargetNode, InP, LSide).y;
                const int Side2 = (LSide == 'R') ? 1 : 0;
                Spans.push_back({ Link.m_Id, Side2, std::min(FromY, ToY), std::max(FromY, ToY) });
            }
            std::sort(Spans.begin(), Spans.end(), [](auto& A, auto& B) { return (A.m_Hi - A.m_Lo) < (B.m_Hi - B.m_Lo); });
            for (auto& S : Spans)
            {
                auto& Lanes = LaneIntervals[S.m_Side];
                int ChosenLane = -1;
                for (int L = 0; L < (int)Lanes.size(); ++L)
                {
                    bool bOverlaps = false;
                    for (auto& Iv : Lanes[L]) if (S.m_Lo <= Iv.m_Hi && S.m_Hi >= Iv.m_Lo) { bOverlaps = true; break; }
                    if (!bOverlaps) { ChosenLane = L; break; }
                }
                if (ChosenLane < 0) { ChosenLane = (int)Lanes.size(); Lanes.push_back({}); }
                Lanes[ChosenLane].push_back({ S.m_Lo, S.m_Hi });
                LaneOfLink[S.m_LinkId] = ChosenLane;
            }
        }
        const int LaneCount[2] = { (int)LaneIntervals[0].size(), (int)LaneIntervals[1].size() };

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

        // Wheel zoom, anchored under the cursor, vertically only - horizontally the stack is always
        // window-centered, see canvas_view's own comment. The canvas has no native scrollbar (see
        // ImGuiWindowFlags above); right-drag on empty canvas space (below) handles vertical pan.
        if (bMouseInCanvasRect)
        {
            const float Wheel = ImGui::GetIO().MouseWheel;
            if (Wheel != 0.0f)
            {
                const float LocalYAtMouse = (ImGui::GetIO().MousePos.y - WindowOrigin.y - View.m_PanY) / View.m_Zoom;
                View.m_Zoom = std::clamp(View.m_Zoom + Wheel * 0.1f, 0.3f, 2.5f);
                View.m_PanY = ImGui::GetIO().MousePos.y - WindowOrigin.y - LocalYAtMouse * View.m_Zoom;
            }
        }

        auto ToScreen    = [&](ImVec2 P) { return ImVec2(WindowCenterX + (P.x - SpineX) * View.m_Zoom, WindowOrigin.y + View.m_PanY + P.y * View.m_Zoom); };
        auto ToScreenLen = [&](float L) { return L * View.m_Zoom; };
        ImDrawList* pDraw = ImGui::GetWindowDrawList();
        const ImVec2 MouseLocal{ SpineX + (ImGui::GetIO().MousePos.x - WindowCenterX) / View.m_Zoom, (ImGui::GetIO().MousePos.y - WindowOrigin.y - View.m_PanY) / View.m_Zoom };

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
                View.m_PanY += ImGui::GetIO().MouseDelta.y;
            else
                View.m_bPanDragActive = false;
        }
        const bool bBackgroundClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

        // Clamp panning to the actual content bounds - the first node's leading gap (local Y 0) and the
        // last node's trailing gap (TotalH) should never be pushed off-screen unless the stack is
        // genuinely taller than the window; even then, panning stops exactly at the real ends instead of
        // overscrolling into a gap the nodes themselves don't justify. Applied after both the wheel-zoom
        // and the drag-pan above so neither can leave PanY out of range.
        {
            const float ContentHeightScreen = TotalH * View.m_Zoom;
            if (ContentHeightScreen <= AvailHeight)
                View.m_PanY = 0.0f;
            else
                View.m_PanY = std::clamp(View.m_PanY, AvailHeight - ContentHeightScreen, 0.0f);
        }

        auto DrawHighwayPath = [&](ImVec2 From, ImVec2 To, char S, int Lane, ImU32 Col, float Thickness, const float* pDash)
        {
            const float HX = HighwayX(S, Lane);
            (void)pDash; // no native dashed-line primitive - dashing omitted, solid preview line is distinguished by color instead
            pDraw->AddLine(ToScreen(From), ToScreen({ HX, From.y }), Col, Thickness);
            pDraw->AddLine(ToScreen({ HX, From.y }), ToScreen({ HX, To.y }), Col, Thickness);
            pDraw->AddLine(ToScreen({ HX, To.y }), ToScreen(To), Col, Thickness);
        };

        pDraw->AddLine(ToScreen({ HighwayX('L', 0), 0 }), ToScreen({ HighwayX('L', 0), TotalH }), IM_COL32(30, 38, 58, 255));
        pDraw->AddLine(ToScreen({ HighwayX('R', 0), 0 }), ToScreen({ HighwayX('R', 0), TotalH }), IM_COL32(30, 38, 58, 255));

        for (auto& Link : Links)
        {
            auto* pSrcDesc = DescOf(FindNode(Link.m_SourceNode)); auto* pDstDesc = DescOf(FindNode(Link.m_TargetNode));
            if (!pSrcDesc || !pDstDesc || Link.m_SourceOutput >= pSrcDesc->m_OutputCount || Link.m_TargetInput >= pDstDesc->m_InputCount) continue;
            const port_ref OutP{ true, Link.m_SourceOutput, &pSrcDesc->m_pOutputs[Link.m_SourceOutput] };
            const port_ref InP { false, Link.m_TargetInput,  &pDstDesc->m_pInputs[Link.m_TargetInput] };
            const bool bSelected = (Selection.m_SelectedLink == Link.m_Id);
            const ImU32 Col = bSelected ? IM_COL32(253, 224, 71, 255) : TypeColor(OutP.m_pDesc->m_pTypeName);
            const char LSide = LinkSide(Link);
            DrawHighwayPath(PortAnchor(Link.m_SourceNode, OutP, LSide), PortAnchor(Link.m_TargetNode, InP, LSide)
                           , LSide, LaneOfLink[Link.m_Id], Col, bSelected ? 3.0f : 2.0f, nullptr);
        }

        if (Drag.m_bActive)
        {
            const char S = Drag.m_FromSide;
            DrawHighwayPath(Drag.m_FromPos, MouseLocal, S, LaneCount[S == 'R' ? 1 : 0], IM_COL32(125, 211, 252, 255), 2.0f, nullptr);
        }

        const float FontSize = ImGui::GetFontSize() * View.m_Zoom;
        auto DrawText = [&](ImVec2 Pos, ImU32 Col, const char* pText) { pDraw->AddText(nullptr, FontSize, ToScreen(Pos), Col, pText); };

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

            pDraw->AddRectFilled(P0, P1, IM_COL32(17, 24, 39, 255), ToScreenLen(10.0f));
            pDraw->AddRect(P0, P1, bBeingDragged ? IM_COL32(56, 189, 248, 255) : (bSelected ? IM_COL32(253, 224, 71, 255) : IM_COL32(51, 65, 85, 255))
                          , ToScreenLen(10.0f), 0, ToScreenLen((bSelected || bBeingDragged) ? 2.5f : 1.5f));
            const float TitleY = pRow->m_Y + (geo::HEADER_H - ImGui::GetFontSize()) * 0.5f;
            DrawText({ pRow->m_X + 10, TitleY }, IM_COL32(226, 232, 240, 255), pDesc->m_pName);
            {
                const ImVec2 CatSize = ImGui::CalcTextSize(pDesc->m_pCategory);
                DrawText({ pRow->m_X + pRow->m_W - CatSize.x - 10, TitleY }, IM_COL32(100, 116, 139, 255), pDesc->m_pCategory);
            }
            pDraw->AddLine(ToScreen({ pRow->m_X, pRow->m_Y + geo::HEADER_H }), ToScreen({ pRow->m_X + pRow->m_W, pRow->m_Y + geo::HEADER_H }), IM_COL32(51, 65, 85, 255));

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
                NodeDrag.m_MovingIds = Selection.m_SelectedNodes.contains(Id)
                    ? std::vector<std::uint64_t>(Selection.m_SelectedNodes.begin(), Selection.m_SelectedNodes.end())
                    : std::vector<std::uint64_t>{ Id };
            }
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f))
                NodeDrag.m_bActive = true;

            if (ImGui::IsItemClicked() && !NodeDrag.m_bActive)
            {
                // Toggle decided here, against the CURRENT selection, then issued as the full desired
                // end-state - Select's Redo() just sets exactly what's in the command string (so replay
                // during a later Redo() stays deterministic regardless of what's currently selected).
                const bool bToggle = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
                std::set<std::uint64_t> NewSelectedNodes = bToggle ? Selection.m_SelectedNodes : std::set<std::uint64_t>{};
                if (bToggle)
                {
                    if (NewSelectedNodes.contains(Id)) NewSelectedNodes.erase(Id);
                    else                                 NewSelectedNodes.insert(Id);
                }
                else
                    NewSelectedNodes = { Id };
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

            for (auto& P : FlatPorts(pDesc))
            {
                const float RH = RowHeight(P);
                const float CenterY = RowY + geo::ROW_H * 0.5f;
                const ImU32 Col = TypeColor(P.m_pDesc->m_pTypeName);
                const bool bConnected = PortSides.contains(PinOf(P, Id));
                const ImU32 Fill = bConnected ? Col : IM_COL32(11, 16, 33, 255);

                const ImVec2 NameSize = ImGui::CalcTextSize(P.m_pDesc->m_pName);
                DrawText({ pRow->m_X + pRow->m_W * 0.5f - NameSize.x * 0.5f, CenterY - NameSize.y * 0.5f }, IM_COL32(203, 213, 225, 255), P.m_pDesc->m_pName);

                // A pin used by links going both up and down the stack needs a glyph on BOTH sides -
                // one per side actually in use (SidesOf), not just whichever link happened to be
                // processed last into a single shared "the" side.
                for (char S : SidesOf(PinOf(P, Id)))
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

                    const std::string TypeLabel = std::string("[") + P.m_pDesc->m_pTypeName + "]";
                    const ImVec2 TypeSize = ImGui::CalcTextSize(TypeLabel.c_str());
                    const float TypeX = (S == 'L') ? pRow->m_X + 14.0f : pRow->m_X + pRow->m_W - 14.0f - TypeSize.x;
                    DrawText({ TypeX, CenterY - TypeSize.y * 0.5f }, Col, TypeLabel.c_str());

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
                        const bool bSamePort   = (Id == Drag.m_FromNode && P.m_bIsOutput == Drag.m_bFromIsOutput && P.m_Index == Drag.m_FromIndex);
                        const bool bOppositeDir = (P.m_bIsOutput != Drag.m_bFromIsOutput);
                        const bool bDifferentNode = (Id != Drag.m_FromNode);
                        bool bTypeMatches = false;
                        if (!bSamePort && bOppositeDir && bDifferentNode)
                        {
                            if (auto* pFromDesc = DescOf(FindNode(Drag.m_FromNode)))
                            {
                                const char* pFromType = Drag.m_bFromIsOutput ? pFromDesc->m_pOutputs[Drag.m_FromIndex].m_pTypeName : pFromDesc->m_pInputs[Drag.m_FromIndex].m_pTypeName;
                                bTypeMatches = std::strcmp(pFromType, P.m_pDesc->m_pTypeName) == 0;
                            }
                        }
                        bShowRing = !bSamePort && bOppositeDir && bDifferentNode && bTypeMatches;
                    }
                    if (bShowRing)
                    {
                        const float HR = geo::PORT_HIT_RADIUS * 1.4f;
                        pDraw->AddCircle(ToScreen({ CX, CenterY }), ToScreenLen(HR), IM_COL32(125, 211, 252, 255), 0, ToScreenLen(1.5f));
                    }
                }

                if (!IsMeshType(P.m_pDesc->m_pTypeName))
                {
                    void* pValue = P.m_bIsOutput
                        ? ((pNode->m_bHasRun && P.m_Index < (int)pNode->m_CachedOutputs.size()) ? pNode->m_CachedOutputs[P.m_Index] : nullptr)
                        : GetInputValue(Id, P.m_Index, Nodes, Links);
                    const char* pPreview = PortTypeToPreview(P.m_pDesc->m_pTypeName, pValue);
                    const ImVec2 ValSize = ImGui::CalcTextSize(pPreview);
                    DrawText({ pRow->m_X + pRow->m_W * 0.5f - ValSize.x * 0.5f, RowY + geo::ROW_H }, IM_COL32(148, 163, 184, 255), pPreview);
                }

                RowY += RH;
            }

            if (!pNode->m_LastError.empty())
            {
                ImGui::SetNextItemAllowOverlap();
                ImGui::SetCursorScreenPos(ToScreen({ pRow->m_X + 8.0f, pRow->m_Y + pRow->m_H - 16.0f }));
                ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "%s", pNode->m_LastError.c_str());
            }
            ImGui::PopID();
        }

        // ---- the spine: a "+" marker in every gap of the stack (before the first node, between each
        // consecutive pair, after the last) - left-click one to insert a node at that exact position.
        // This is the "skeleton" the user asked to make the stacking order itself directly editable.
        // (GapIndex, local Y) of every marker drawn below - collected so the node-drag drop below can
        // hit-test against MouseLocal directly (see the comment there for why, instead of ImGui hover).
        std::vector<std::pair<int, float>> MarkerPositions;
        if (!Layout.empty())
        {
            auto DrawInsertMarker = [&](int GapIndex, float Y)
            {
                MarkerPositions.push_back({ GapIndex, Y });
                const bool   bSelected = (Selection.m_SelectedGap == GapIndex);
                const ImVec2 Center = ToScreen({ SpineX, Y });
                const float  HalfW = ToScreenLen(28.0f), HalfH = ToScreenLen(13.0f);
                const ImVec2 BoxMin{ Center.x - HalfW, Center.y - HalfH }, BoxMax{ Center.x + HalfW, Center.y + HalfH };
                const float  PlusR = ToScreenLen(10.0f);
                const ImVec2 PlusMin{ Center.x - PlusR, Center.y - PlusR }, PlusMax{ Center.x + PlusR, Center.y + PlusR };
                const bool bPlusHovered = ImGui::IsMouseHoveringRect(PlusMin, PlusMax);

                // Two independent things sharing this slot: a selectable box (clicking anywhere in it,
                // outside the + itself, selects it the same way clicking a node does - a future Ctrl+V
                // paste will target the current selection) and the + button (always opens the insert
                // popup, regardless of selection state - pressing it never itself selects the box).
                pDraw->AddRectFilled(BoxMin, BoxMax, IM_COL32(17, 24, 39, 255), ToScreenLen(4.0f));
                pDraw->AddRect(BoxMin, BoxMax, bSelected ? IM_COL32(253, 224, 71, 255) : IM_COL32(51, 65, 85, 255)
                              , ToScreenLen(4.0f), 0, ToScreenLen(bSelected ? 2.0f : 1.2f));

                pDraw->AddCircleFilled(Center, PlusR, bPlusHovered ? IM_COL32(56, 130, 246, 255) : IM_COL32(30, 41, 59, 255));
                pDraw->AddCircle(Center, PlusR, IM_COL32(100, 116, 139, 255), 0, ToScreenLen(1.2f));
                const float Arm = PlusR * 0.45f;
                pDraw->AddLine({ Center.x - Arm, Center.y }, { Center.x + Arm, Center.y }, IM_COL32(226, 232, 240, 255), ToScreenLen(1.5f));
                pDraw->AddLine({ Center.x, Center.y - Arm }, { Center.x, Center.y + Arm }, IM_COL32(226, 232, 240, 255), ToScreenLen(1.5f));

                ImGui::PushID("spine_insert");
                ImGui::PushID(GapIndex);

                // Box hit region, submitted first (bigger, covers the + button's area too).
                ImGui::SetNextItemAllowOverlap();
                ImGui::SetCursorScreenPos(BoxMin);
                ImGui::InvisibleButton("box", ImVec2(HalfW * 2, HalfH * 2));
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                {
                    // Addressed relative to the node currently on one side of this gap, same reasoning
                    // as CreateNode's own -After/-Before (a raw, shifting GapIndex isn't something
                    // worth serializing into a durable command). Order is non-empty here (only reached
                    // once Layout is non-empty).
                    const std::string Cmd = (GapIndex < (int)Order.size())
                        ? commands::MakeSelectMarkerBefore(Order[GapIndex])
                        : commands::MakeSelectMarkerAfter(Order.back());
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
                            InsertNodeAt(GapIndex, Src);
                    ImGui::EndPopup();
                }
                ImGui::PopID();
                ImGui::PopID();
            };

            DrawInsertMarker(0, Layout.front().m_Y - geo::NODE_GAP * 0.5f);
            for (int i = 0; i + 1 < (int)Layout.size(); ++i)
                DrawInsertMarker(i + 1, Layout[i].m_Y + Layout[i].m_H + geo::NODE_GAP * 0.5f);
            DrawInsertMarker((int)Layout.size(), Layout.back().m_Y + Layout.back().m_H + geo::NODE_GAP * 0.5f);
        }

        // Resolve a node-drag drop by direct distance to MouseLocal, same pattern as the pin-to-pin
        // drag-to-connect resolution below - NOT the marker's own ImGui hover state. A marker sitting
        // under an ACTIVE (held-down) different widget (the dragged node's own body) is exactly the
        // overlapping-item scenario this codebase has already been burned by once
        // (xgpu_imgui_overlapping_invisible_buttons); hit-testing the cursor position directly sidesteps
        // it entirely instead of relying on getting every AllowOverlap/ActiveId interaction exactly right.
        if (NodeDrag.m_bActive && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            int BestGap = -1; float Best = 40.0f;
            for (auto& [GapIndex, Y] : MarkerPositions)
            {
                const float D = std::hypot(SpineX - MouseLocal.x, Y - MouseLocal.y);
                if (D < Best) { Best = D; BestGap = GapIndex; }
            }
            if (BestGap >= 0) MoveNodesTo(NodeDrag.m_MovingIds, BestGap);
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
                    for (char S : SidesOf(PinOf(P, Id)))
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
                if (pOutDesc && pInDesc && OutNode != InNode && OutIdx < pOutDesc->m_OutputCount && InIdx < pInDesc->m_InputCount
                    && std::strcmp(pOutDesc->m_pOutputs[OutIdx].m_pTypeName, pInDesc->m_pInputs[InIdx].m_pTypeName) == 0)
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
                const port_ref OutP{ true, Link.m_SourceOutput, &pSrcDesc->m_pOutputs[Link.m_SourceOutput] };
                const port_ref InP { false, Link.m_TargetInput,  &pDstDesc->m_pInputs[Link.m_TargetInput] };
                const char LSide = LinkSide(Link);
                const ImVec2 From = PortAnchor(Link.m_SourceNode, OutP, LSide), To = PortAnchor(Link.m_TargetNode, InP, LSide);
                const float HX = HighwayX(LSide, LaneOfLink[Link.m_Id]);
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
        }

        ImGui::End();
    }

    //------------------------------------------------------------------------------------------------
    // Dependency-respecting evaluation: repeatedly execute any not-yet-run node whose every
    // connected input already has a producer that has run, until nothing changes. Good enough for
    // the small acyclic graphs this proof of concept cares about. No longer needs AvailableTypes at
    // all - each node holds its own type descriptor directly (node_instance::m_pType).
    //------------------------------------------------------------------------------------------------
    static void ExecuteGraph(xgpu::device& Device, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links, mesh_preview_system& MeshPreview)
    {
        for (auto& Node : Nodes)
        {
            if (Node.m_bHasRun && Node.m_pType && Node.m_pType->m_pFreeOutputs)
                Node.m_pType->m_pFreeOutputs(Node.m_CachedOutputs.data(), (int)Node.m_CachedOutputs.size());
            Node.m_bHasRun = false;
            Node.m_LastError.clear();
            Node.m_CachedOutputs.clear();
        }

        bool bProgress = true;
        int  Guard = 0;
        while (bProgress && Guard++ < (int)Nodes.size() + 1)
        {
            bProgress = false;
            for (auto& Node : Nodes)
            {
                if (Node.m_bHasRun || !Node.m_pType) continue;
                auto* pDesc = Node.m_pType;

                std::vector<void*> Inputs(pDesc->m_InputCount, nullptr);
                bool bReady = true;
                for (auto& Link : Links)
                {
                    if (Link.m_TargetNode != Node.m_Id) continue;
                    auto SourceIt = std::find_if(Nodes.begin(), Nodes.end(), [&](auto& N) { return N.m_Id == Link.m_SourceNode; });
                    if (SourceIt == Nodes.end() || !SourceIt->m_bHasRun) { bReady = false; break; }
                    if (Link.m_TargetInput < (int)Inputs.size())
                        Inputs[Link.m_TargetInput] = (Link.m_SourceOutput < (int)SourceIt->m_CachedOutputs.size()) ? SourceIt->m_CachedOutputs[Link.m_SourceOutput] : nullptr;
                }
                if (!bReady) continue;

                Node.m_CachedOutputs.assign(pDesc->m_OutputCount, nullptr);
                pDesc->m_pExecute(Node.m_pProperties, Inputs.data(), Node.m_CachedOutputs.data());
                Node.m_bHasRun = true;
                bProgress = true;
            }
        }

        for (auto& Node : Nodes)
            if (!Node.m_bHasRun)
                Node.m_LastError = "not executed - missing/cyclic input";

        // Rebuild the GPU mesh preview for every pin currently carrying a "Mesh" value - both a
        // producer's output (Cube) and a consumer's input (Inspect Mesh) get a live render.
        for (auto& Node : Nodes)
        {
            if (!Node.m_pType) continue;
            auto* pDesc = Node.m_pType;
            for (int i = 0; i < pDesc->m_OutputCount; ++i)
            {
                void* pValue = (Node.m_bHasRun && i < (int)Node.m_CachedOutputs.size()) ? Node.m_CachedOutputs[i] : nullptr;
                MeshPreview.RebuildIfMesh(Device, OutPinOf(Node.m_Id, i), pDesc->m_pOutputs[i].m_pTypeName, pValue);
            }
            for (int i = 0; i < pDesc->m_InputCount; ++i)
                MeshPreview.RebuildIfMesh(Device, InPinOf(Node.m_Id, i), pDesc->m_pInputs[i].m_pTypeName, GetInputValue(Node.m_Id, i, Nodes, Links));
        }
    }

    //------------------------------------------------------------------------------------------------
    // Generic, host-owned property serialization - reads/writes a (Name, Kind, Value) row per member,
    // via nothing but the ABI-safe ixnode_os_reflected_object primitives every plugin with properties
    // already provides. This is deliberately NOT delegated to a plugin: xtextfile only ever needs to
    // know about the same fixed 5 atomic kinds (FLOAT/INT/BOOL/STRING/ENUM) the property panel already
    // draws with, and that vocabulary belongs to the host ("the OS"), not to any individual plugin - a
    // plugin needs to expose nothing beyond m_pGetReflectedObject for this to work, no separate
    // serialization opt-in required. Values round-trip as text (this engine's text files are already
    // documented as lossy for floats - see xtextfile.h's own top comment - which is an accepted,
    // existing tradeoff, not a new one introduced here). ENUM stores the underlying int value, matching
    // how DrawReflectedMembers already treats it (SetInt) rather than round-tripping through the name
    // table. COMPOUND/LIST members are skipped for now (same "not yet handled generically" scope
    // xnode_os_property_adapter.h already documents for the drawing side).
    //------------------------------------------------------------------------------------------------
    // A single property reduced to its plain-text row shape - Name/Kind/Value, no storage-backend
    // opinion at all. This is the ONLY place that knows how to turn one ixnode_os_reflected_object
    // member into text and back; every serialization backend (the xtextfile-based graph file below,
    // and any future in-memory one - e.g. xundo snapshots) is a thin wrapper around these two
    // functions, so the atomic-kind switch logic exists exactly once regardless of how many backends
    // eventually want a text view of a property block.
    struct property_row { std::string m_Name; int m_Kind; std::string m_Value; };

    static property_row ReflectedMemberToRow(ixnode_os_reflected_object* pObj, int Idx)
    {
        property_row Row{ pObj->GetMemberName(Idx), static_cast<int>(pObj->GetMemberKind(Idx)), {} };
        switch (pObj->GetMemberKind(Idx))
        {
            case xnode_os_member_kind::FLOAT:  Row.m_Value = std::format("{}", pObj->GetFloat(Idx));  break;
            case xnode_os_member_kind::INT:    Row.m_Value = std::format("{}", pObj->GetInt(Idx));    break;
            case xnode_os_member_kind::BOOL:   Row.m_Value = pObj->GetBool(Idx) ? "1" : "0";           break;
            case xnode_os_member_kind::STRING: Row.m_Value = pObj->GetString(Idx);                     break;
            case xnode_os_member_kind::ENUM:   Row.m_Value = std::format("{}", pObj->GetInt(Idx));    break;
            default: break; // COMPOUND/LIST - not serialized generically yet, see comment above
        }
        return Row;
    }

    // Looks the member up BY NAME (not by the row's original index) so a property added, removed, or
    // reordered on the plugin's struct since the row was produced doesn't silently misassign a value -
    // the same robustness the old inline xtextfile version had, now shared by every backend.
    static void ApplyRowToReflectedObject(ixnode_os_reflected_object* pObj, const property_row& Row)
    {
        int FoundIdx = -1;
        for (int m = 0; m < pObj->GetMemberCount(); ++m)
            if (Row.m_Name == pObj->GetMemberName(m)) { FoundIdx = m; break; }
        if (FoundIdx < 0) return; // property no longer exists on this type - skip, don't fail the whole caller

        switch (static_cast<xnode_os_member_kind>(Row.m_Kind))
        {
            case xnode_os_member_kind::FLOAT:  pObj->SetFloat(FoundIdx, std::stof(Row.m_Value));        break;
            case xnode_os_member_kind::INT:    pObj->SetInt(FoundIdx, std::stoi(Row.m_Value));          break;
            case xnode_os_member_kind::BOOL:   pObj->SetBool(FoundIdx, Row.m_Value == "1");             break;
            case xnode_os_member_kind::STRING: pObj->SetString(FoundIdx, Row.m_Value.c_str());          break;
            case xnode_os_member_kind::ENUM:   pObj->SetInt(FoundIdx, std::stoi(Row.m_Value));          break;
            default: break;
        }
    }

    // Thin xtextfile-backed wrapper around the row conversion above - identical wire format and
    // behavior to before the refactor (Name/Kind/Value fields, by-name lookup on read), just no longer
    // inlining the atomic-kind switch logic itself.
    static bool SerializeReflectedMembers(xtextfile::stream& Stream, ixnode_os_reflected_object* pObj)
    {
        if (auto Err = Stream.Record("xProperties"
            , [&](std::size_t& C, xerr&) { if (!Stream.isReading()) C = static_cast<std::size_t>(pObj->GetMemberCount()); }
            , [&](std::size_t i, xerr& Error)
            {
                property_row Row;

                // On write, 'i' IS the member index (we told Record our own member count above, and
                // we're the ones choosing to walk 0..Count-1 in that same order). On read, 'i' is just
                // this row's position in the file - ApplyRowToReflectedObject looks the member up by
                // name instead.
                if (!Stream.isReading())
                    Row = ReflectedMemberToRow(pObj, static_cast<int>(i));

                0
                || (Error = Stream.Field("Name",  Row.m_Name))
                || (Error = Stream.Field("Kind",  Row.m_Kind))
                || (Error = Stream.Field("Value", Row.m_Value));
                if (Error) return;

                if (Stream.isReading())
                    ApplyRowToReflectedObject(pObj, Row);
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
    static std::string SerializePropertiesToString(ixnode_os_reflected_object* pObj)
    {
        std::string Out;
        for (int i = 0; i < pObj->GetMemberCount(); ++i)
        {
            const auto Row = ReflectedMemberToRow(pObj, i);
            Out += std::format("{}\t{}\t{}\n", Row.m_Name, Row.m_Kind, Row.m_Value);
        }
        return Out;
    }

    static void ApplyPropertiesFromString(ixnode_os_reflected_object* pObj, const std::string& Snapshot)
    {
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

            ApplyRowToReflectedObject(pObj, property_row
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
    static bool SaveGraph(const std::string& Utf8Path, const std::vector<node_instance>& Nodes, const std::vector<link_instance>& Links, const std::vector<available_node_type>& AvailableTypes)
    {
        const std::wstring WPath(Utf8Path.begin(), Utf8Path.end()); // ASCII-safe path is all this demo needs

        xtextfile::stream Stream;
        if (auto Err = Stream.Open(false, WPath, xtextfile::file_type::TEXT); Err)
        {
            Debugger(std::format("Node OS: failed to open '{}' for saving", Utf8Path));
            return false;
        }

        const auto FindSourcePath = [&](const xnode_os_node_type_desc* pType) -> std::string
        {
            // The plugin's DIRECTORY NAME, not its absolute .cpp path (kept as "Source" in the field
            // name/comment for continuity, but see plugin_source_entry's own comment on why a folder
            // name is the actual identity) - stays meaningful if the repo ever moves and matches what
            // AddNode/DeleteNodes commands already use.
            for (auto& T : AvailableTypes) if (T.m_pDesc == pType) return T.m_DirName;
            return {};
        };

        if (auto Err = Stream.Record("Nodes"
            , [&](std::size_t& C, xerr&) { C = Nodes.size(); }
            , [&](std::size_t i, xerr& Error)
            {
                auto&         N             = Nodes[i];
                std::uint64_t Id            = N.m_Id;
                std::string   Source        = FindSourcePath(N.m_pType);
                std::string   TypeName      = N.m_pType ? N.m_pType->m_pName : "";
                int           Order         = N.m_Order;
                // Not merely "does m_pGetReflectedObject exist" - a property struct whose every member
                // the ABI-safe adapter can't represent (yet) would exist but reflect zero members, and
                // SerializeReflectedMembers would then write no "xProperties" record at all, desyncing
                // the reader if HasProperties still claimed one was coming.
                bool          HasProperties = false;
                if (N.m_pType && N.m_pType->m_pGetReflectedObject && N.m_pProperties)
                {
                    if (auto* pObj = N.m_pType->m_pGetReflectedObject(N.m_pProperties))
                    {
                        HasProperties = pObj->GetMemberCount() > 0;
                        pObj->Destroy();
                    }
                }

                0
                || (Error = Stream.Field("Id",            Id))
                || (Error = Stream.Field("Source",        Source))
                || (Error = Stream.Field("Type",          TypeName))
                || (Error = Stream.Field("Order",         Order))
                || (Error = Stream.Field("HasProperties", HasProperties));
            }
        ); Err)
        {
            Debugger("Node OS: failed writing Nodes record");
            return false;
        }

        for (auto& N : Nodes)
        {
            if (N.m_pType && N.m_pType->m_pGetReflectedObject && N.m_pProperties)
            {
                auto* pObj = N.m_pType->m_pGetReflectedObject(N.m_pProperties);
                if (!pObj) continue;
                const bool bOk = SerializeReflectedMembers(Stream, pObj);
                pObj->Destroy();
                if (!bOk)
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

        return true;
    }

    // Mirrors SaveGraph. A node whose recorded Source/Type can no longer be resolved fails the WHOLE
    // load rather than silently skipping just that node: skipping it would still leave its
    // "xProperties" record (if HasProperties was true) sitting unread in the file, desyncing every
    // property record after it - a loud, whole-file failure beats a quietly corrupted partial load.
    static bool LoadGraph(const std::string& Utf8Path, std::vector<node_instance>& Nodes, std::vector<link_instance>& Links
                         , std::vector<plugin_source_entry>& Sources, std::vector<available_node_type>& AvailableTypes)
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
                bool          HasProperties = false;

                if (0
                 || (Error = Stream.Field("Id",            Id))
                 || (Error = Stream.Field("Source",        Source))
                 || (Error = Stream.Field("Type",          TypeName))
                 || (Error = Stream.Field("Order",         Order))
                 || (Error = Stream.Field("HasProperties", HasProperties)))
                    return;

                auto SrcIt = std::find_if(Sources.begin(), Sources.end(), [&](auto& S) { return S.m_DirName == Source; });
                if (SrcIt == Sources.end())
                {
                    Error = xerr::create<xtextfile::state::FIELD_NOT_FOUND, "Node OS: a saved node's plugin source no longer exists">();
                    return;
                }

                const auto* pType = EnsureLoadedAndGetType(*SrcIt, AvailableTypes);
                if (!pType || TypeName != pType->m_pName || (HasProperties && !pType->m_pGetReflectedObject))
                {
                    Error = xerr::create<xtextfile::state::FIELD_NOT_FOUND, "Node OS: a saved node's type no longer matches its plugin source">();
                    return;
                }

                NewNodes.push_back(CreateNodeInstance(Id, pType, Order));
            }
        ); Err)
        {
            Debugger("Node OS: failed reading Nodes record");
            for (auto& N : NewNodes) DestroyNodeProperties(N);
            return false;
        }

        for (auto& N : NewNodes)
        {
            if (N.m_pType && N.m_pType->m_pGetReflectedObject && N.m_pProperties)
            {
                auto* pObj = N.m_pType->m_pGetReflectedObject(N.m_pProperties);
                if (!pObj) continue;
                const bool bOk = SerializeReflectedMembers(Stream, pObj);
                pObj->Destroy();
                if (!bOk)
                {
                    Debugger(std::format("Node OS: failed reading properties for node {}", N.m_Id));
                    for (auto& M : NewNodes) DestroyNodeProperties(M);
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
            for (auto& N : NewNodes) DestroyNodeProperties(N);
            return false;
        }

        for (auto& N : Nodes) DestroyNodeProperties(N);
        Nodes = std::move(NewNodes);
        Links = std::move(NewLinks);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    // Dockable panel for the currently-selected node's properties - active only when exactly one node
    // is selected (multi-select property editing is out of scope for now). Walks the node type's
    // ixnode_os_reflected_object view of its property block, drawing the 5 fixed atomic kinds with
    // ordinary ImGui widgets and recursing generically into COMPOUND members - a plugin never needs
    // to teach this panel about a new type, only ever compose the fixed five. The reflected view is
    // deliberately cheap and re-requested every frame (created here, Destroy()'d at the end of the
    // same call) rather than cached, since it's just a thin view over the property block, not the
    // property block itself.
    //------------------------------------------------------------------------------------------------
    static void DrawReflectedMembers(ixnode_os_reflected_object* pObj, bool& bDirty)
    {
        for (int i = 0; i < pObj->GetMemberCount(); ++i)
        {
            ImGui::PushID(i);
            const char* pName = pObj->GetMemberName(i);
            switch (pObj->GetMemberKind(i))
            {
                case xnode_os_member_kind::FLOAT:
                {
                    float V = pObj->GetFloat(i);
                    if (ImGui::DragFloat(pName, &V, 0.01f)) { pObj->SetFloat(i, V); bDirty = true; }
                    break;
                }
                case xnode_os_member_kind::INT:
                {
                    int V = pObj->GetInt(i);
                    if (ImGui::DragInt(pName, &V)) { pObj->SetInt(i, V); bDirty = true; }
                    break;
                }
                case xnode_os_member_kind::BOOL:
                {
                    bool V = pObj->GetBool(i);
                    if (ImGui::Checkbox(pName, &V)) { pObj->SetBool(i, V); bDirty = true; }
                    break;
                }
                case xnode_os_member_kind::STRING:
                {
                    char Buffer[256];
                    strncpy_s(Buffer, pObj->GetString(i), _TRUNCATE);
                    if (ImGui::InputText(pName, Buffer, sizeof(Buffer))) { pObj->SetString(i, Buffer); bDirty = true; }
                    break;
                }
                case xnode_os_member_kind::ENUM:
                {
                    const int Current = pObj->GetInt(i);
                    const char* pCurrentName = "";
                    for (int e = 0; e < pObj->GetEnumValueCount(i); ++e)
                        if (auto V = pObj->GetEnumValueAt(i, e); V.m_Value == Current) pCurrentName = V.m_pName;
                    if (ImGui::BeginCombo(pName, pCurrentName))
                    {
                        for (int e = 0; e < pObj->GetEnumValueCount(i); ++e)
                        {
                            auto V = pObj->GetEnumValueAt(i, e);
                            if (ImGui::Selectable(V.m_pName, V.m_Value == Current)) { pObj->SetInt(i, V.m_Value); bDirty = true; }
                        }
                        ImGui::EndCombo();
                    }
                    break;
                }
                case xnode_os_member_kind::COMPOUND:
                {
                    if (ImGui::TreeNode(pName))
                    {
                        if (auto* pChild = pObj->GetCompoundMember(i)) DrawReflectedMembers(pChild, bDirty);
                        ImGui::TreePop();
                    }
                    break;
                }
                case xnode_os_member_kind::LIST:
                    ImGui::Text("%s: (%d entries)", pName, pObj->GetListCount(i)); // v1: count only, see xnode_os_property_adapter.h
                    break;
            }
            ImGui::PopID();
        }
    }

    static void DrawNodePropertiesEmptyState(const char* pMessage)
    {
        ImGui::SetNextWindowPos(ImVec2(1265, 90), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Node Properties"))
            ImGui::TextDisabled("%s", pMessage);
        ImGui::End();
    }

    static void DrawNodePropertiesPanel(std::vector<node_instance>& Nodes, const std::set<std::uint64_t>& SelectedNodes, xundo::system& System)
    {
        if (SelectedNodes.size() != 1)
        {
            DrawNodePropertiesEmptyState(SelectedNodes.empty() ? "Select a node to see its properties." : "Select a single node to see its properties.");
            return;
        }

        auto It = std::find_if(Nodes.begin(), Nodes.end(), [&](auto& N) { return N.m_Id == *SelectedNodes.begin(); });
        if (It == Nodes.end() || !It->m_pType) { DrawNodePropertiesEmptyState("(node no longer exists)"); return; }

        auto* pType = It->m_pType;
        if ((!pType->m_pGetReflectedObject && !pType->m_pDrawProperties) || !It->m_pProperties)
        {
            DrawNodePropertiesEmptyState(std::format("{} has no properties.", pType->m_pName).c_str());
            return;
        }
        void* pProperties = It->m_pProperties;
        const std::uint64_t NodeId = It->m_Id;

        // m_pDrawProperties, when a plugin offers it, draws with the plugin's OWN compiled
        // xproperty::inspector over its OWN reflection data - see xnode_os_plugin_api.h's comment for
        // why that's safe where an earlier, abandoned attempt (the host inspecting the plugin's real
        // xproperty object directly) was not. It has no "did anything change" signal of its own (and
        // may include arbitrary custom buttons doing arbitrary things), so a before/after snapshot of
        // the WHOLE properties block - via the same ixnode_os_reflected_object primitives the ABI-safe
        // path below uses, never a raw byte compare (a property can own a resource, e.g. Export Mesh's
        // Path is a std::wstring) - is the one thing that covers every kind of mutation uniformly.
        if (pType->m_pDrawProperties)
        {
            std::string Before;
            if (pType->m_pGetReflectedObject)
                if (auto* pObj = pType->m_pGetReflectedObject(pProperties)) { Before = SerializePropertiesToString(pObj); pObj->Destroy(); }

            pType->m_pDrawProperties(pProperties);

            if (pType->m_pGetReflectedObject)
            {
                std::string After;
                if (auto* pObj = pType->m_pGetReflectedObject(pProperties)) { After = SerializePropertiesToString(pObj); pObj->Destroy(); }
                if (After != Before)
                    commands::Run(System, commands::MakeSetProperties(NodeId, Before, After));
            }
            return;
        }

        ImGui::SetNextWindowPos(ImVec2(1265, 90), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Node Properties"))
        {
            ImGui::TextDisabled("%s", pType->m_pName);
            ImGui::Separator();
            if (auto* pObj = pType->m_pGetReflectedObject(pProperties))
            {
                const std::string Before = SerializePropertiesToString(pObj);
                bool bUnusedDirty = false;
                DrawReflectedMembers(pObj, bUnusedDirty);
                const std::string After = SerializePropertiesToString(pObj);
                if (After != Before)
                    commands::Run(System, commands::MakeSetProperties(NodeId, Before, After));
                pObj->Destroy();
            }
        }
        ImGui::End();
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
        // free Make*/Run helpers live EARLIER in this file (right after DestroyNodeProperties) - they
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
        };

        // Shared by select_cmd and clear_selection_cmd - both snapshot/restore the exact same triple.
        inline void BackupSelection(node_os_command_context& Ctx, xundo::undo_file& File) noexcept
        {
            auto& S = Ctx.m_Selection;
            File.Write(static_cast<std::uint32_t>(S.m_SelectedNodes.size()));
            for (auto Id : S.m_SelectedNodes) File.Write(Id);
            File.Write(S.m_SelectedLink);
            File.Write(S.m_SelectedGap);
        }
        inline void RestoreSelection(node_os_command_context& Ctx, xundo::undo_file& File) noexcept
        {
            auto& S = Ctx.m_Selection;
            std::uint32_t Count = 0; File.Read(Count);
            S.m_SelectedNodes.clear();
            for (std::uint32_t i = 0; i < Count; ++i) { std::uint64_t Id = 0; File.Read(Id); S.m_SelectedNodes.insert(Id); }
            File.Read(S.m_SelectedLink);
            File.Read(S.m_SelectedGap);
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

            const char* getCommandHelp() const noexcept override { return "Creates a node. Usage: CreateNode -Id N -PluginDir dirname [-After id | -Before id]"; }
            void RegisterArguments() noexcept override
            {
                m_hId        = m_Parser.addOption("Id",        "Node id",                                           true,  1);
                m_hPluginDir = m_Parser.addOption("PluginDir", "Plugin folder name under Plugins/ (e.g. CubeNode)",  true,  1);
                m_hAfter     = m_Parser.addOption("After",     "Insert right after this node id",                   false, 1);
                m_hBefore    = m_Parser.addOption("Before",    "Insert right before this node id - neither -After nor -Before means append at the end", false, 1);
            }

            // Resolves -After/-Before (if given) against the CURRENT node list into a target dense
            // order index - shared by Redo (which needs it to place the new node) and BackupCurrenState
            // (which needs it to know the full pre-insert layout for Undo).
            std::string ResolveTargetOrder(node_os_command_context& Ctx, int& OutTargetOrder) const noexcept
            {
                const bool bHasAfter  = m_Parser.hasOption(m_hAfter);
                const bool bHasBefore = m_Parser.hasOption(m_hBefore);
                if (bHasAfter && bHasBefore) return "CreateNode: -After and -Before are mutually exclusive";

                if (!bHasAfter && !bHasBefore) { OutTargetOrder = static_cast<int>(Ctx.m_Nodes.size()); return {}; }

                auto RefArg = m_Parser.getOptionArgAs<std::string>(bHasAfter ? m_hAfter : m_hBefore, 0);
                if (std::holds_alternative<xerr>(RefArg)) return "CreateNode: bad arguments";
                const auto RefId = ParseGuid(std::get<std::string>(RefArg));

                for (auto& N : Ctx.m_Nodes)
                    if (N.m_Id == RefId) { OutTargetOrder = bHasAfter ? N.m_Order + 1 : N.m_Order; return {}; }
                return "CreateNode: -After/-Before node no longer exists";
            }

            std::string Redo() noexcept override
            {
                auto Id        = m_Parser.getOptionArgAs<std::string>(m_hId, 0);
                auto PluginDir = m_Parser.getOptionArgAs<std::string>(m_hPluginDir, 0);
                if (std::holds_alternative<xerr>(Id) || std::holds_alternative<xerr>(PluginDir))
                    return "CreateNode: bad arguments";

                auto& Ctx = get<node_os_command_context>();
                int TargetOrder = 0;
                if (auto Err = ResolveTargetOrder(Ctx, TargetOrder); !Err.empty()) return Err;

                auto* pSrc = FindSourceByDirName(Ctx.m_Sources, std::get<std::string>(PluginDir));
                if (!pSrc) return "CreateNode: unknown plugin directory";
                const auto* pType = EnsureLoadedAndGetType(*pSrc, Ctx.m_AvailableTypes);
                if (!pType) return "CreateNode: failed to compile/load plugin";

                for (auto& N : Ctx.m_Nodes) if (N.m_Order >= TargetOrder) ++N.m_Order;
                Ctx.m_Nodes.push_back(CreateNodeInstance(ParseGuid(std::get<std::string>(Id)), pType, TargetOrder));
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
                for (auto& N : Ctx.m_Nodes) if (N.m_Id == Id) DestroyNodeProperties(N);
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

            xcmdline::parser::handle m_hId, m_hPluginDir, m_hAfter, m_hBefore;
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
                const auto Ids = SplitIds(std::get<std::string>(IdsArg));

                auto& Ctx = get<node_os_command_context>();
                auto IsDoomed = [&](std::uint64_t Id) { return std::find(Ids.begin(), Ids.end(), Id) != Ids.end(); };
                std::erase_if(Ctx.m_Links, [&](auto& L) { return IsDoomed(L.m_SourceNode) || IsDoomed(L.m_TargetNode); });
                for (auto& N : Ctx.m_Nodes) if (IsDoomed(N.m_Id)) DestroyNodeProperties(N);
                std::erase_if(Ctx.m_Nodes, [&](auto& N) { return IsDoomed(N.m_Id); });
                for (auto Id : Ids) Ctx.m_Selection.m_SelectedNodes.erase(Id);
                Ctx.m_bDirty = true;
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override
            {
                auto IdsArg = m_Parser.getOptionArgAs<std::string>(m_hIds, 0);
                const auto Ids = std::holds_alternative<xerr>(IdsArg) ? std::vector<std::uint64_t>{} : SplitIds(std::get<std::string>(IdsArg));
                auto& Ctx = get<node_os_command_context>();
                auto IsDoomed = [&](std::uint64_t Id) { return std::find(Ids.begin(), Ids.end(), Id) != Ids.end(); };

                struct node_snap { std::uint64_t m_Id; std::string m_PluginDir; int m_Order; std::string m_Properties; };
                std::vector<node_snap> NodeSnaps;
                for (auto& N : Ctx.m_Nodes)
                {
                    if (!IsDoomed(N.m_Id)) continue;
                    std::string PluginDir;
                    for (auto& T : Ctx.m_AvailableTypes) if (T.m_pDesc == N.m_pType) { PluginDir = T.m_DirName; break; }
                    std::string Properties;
                    if (N.m_pType && N.m_pType->m_pGetReflectedObject && N.m_pProperties)
                        if (auto* pObj = N.m_pType->m_pGetReflectedObject(N.m_pProperties)) { Properties = SerializePropertiesToString(pObj); pObj->Destroy(); }
                    NodeSnaps.push_back({ N.m_Id, PluginDir, N.m_Order, Properties });
                }
                std::vector<link_instance> LinkSnaps;
                for (auto& L : Ctx.m_Links)
                    if (IsDoomed(L.m_SourceNode) || IsDoomed(L.m_TargetNode))
                        LinkSnaps.push_back(L);

                File.Write(static_cast<std::uint32_t>(NodeSnaps.size()));
                for (auto& S : NodeSnaps) { File.Write(S.m_Id); WriteString(File, S.m_PluginDir); File.Write(S.m_Order); WriteString(File, S.m_Properties); }
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
                    const std::string Properties = ReadString(File);

                    auto* pSrc = FindSourceByDirName(Ctx.m_Sources, PluginDir);
                    const auto* pType = pSrc ? EnsureLoadedAndGetType(*pSrc, Ctx.m_AvailableTypes) : nullptr;
                    if (!pType) continue; // plugin source no longer resolvable - best effort, matching LoadGraph's own tolerance
                    Ctx.m_Nodes.push_back(CreateNodeInstance(Id, pType, Order));
                    if (!Properties.empty() && pType->m_pGetReflectedObject)
                        if (auto* pObj = pType->m_pGetReflectedObject(Ctx.m_Nodes.back().m_pProperties))
                        { ApplyPropertiesFromString(pObj, Properties); pObj->Destroy(); }
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

            static ixnode_os_reflected_object* GetObjFor(node_os_command_context& Ctx, std::uint64_t NodeId)
            {
                for (auto& N : Ctx.m_Nodes)
                    if (N.m_Id == NodeId && N.m_pType && N.m_pType->m_pGetReflectedObject && N.m_pProperties)
                        return N.m_pType->m_pGetReflectedObject(N.m_pProperties);
                return nullptr;
            }

            std::string Redo() noexcept override
            {
                auto NodeId = m_Parser.getOptionArgAs<std::string>(m_hNodeId, 0);
                auto After  = m_Parser.getOptionArgAs<std::string>(m_hAfter, 0);
                if (std::holds_alternative<xerr>(NodeId) || std::holds_alternative<xerr>(After)) return "SetProperties: bad arguments";
                auto& Ctx = get<node_os_command_context>();
                if (auto* pObj = GetObjFor(Ctx, ParseGuid(std::get<std::string>(NodeId))))
                {
                    ApplyPropertiesFromString(pObj, Base64Decode(std::get<std::string>(After)));
                    pObj->Destroy();
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
                if (auto* pObj = GetObjFor(Ctx, NodeId))
                {
                    ApplyPropertiesFromString(pObj, Base64Decode(BeforeB64));
                    pObj->Destroy();
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
                       " Usage: Select [-Nodes id[,id...]] [-Link id] [-MarkerAfter id | -MarkerBefore id]";
            }
            void RegisterArguments() noexcept override
            {
                m_hNodes        = m_Parser.addOption("Nodes",        "Selected node ids, comma-separated",                  false, 1);
                m_hLink         = m_Parser.addOption("Link",         "Selected link id",                                    false, 1);
                m_hMarkerAfter  = m_Parser.addOption("MarkerAfter",  "Select the insert marker right after this node id",   false, 1);
                m_hMarkerBefore = m_Parser.addOption("MarkerBefore", "Select the insert marker right before this node id",  false, 1);
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

                S.m_SelectedGap = -1;
                const bool bHasAfter  = m_Parser.hasOption(m_hMarkerAfter);
                const bool bHasBefore = m_Parser.hasOption(m_hMarkerBefore);
                if (bHasAfter && bHasBefore) return "Select: -MarkerAfter and -MarkerBefore are mutually exclusive";
                if (bHasAfter || bHasBefore)
                {
                    auto RefArg = m_Parser.getOptionArgAs<std::string>(bHasAfter ? m_hMarkerAfter : m_hMarkerBefore, 0);
                    if (std::holds_alternative<xerr>(RefArg)) return "Select: bad arguments";
                    const auto RefId = ParseGuid(std::get<std::string>(RefArg));
                    bool bFound = false;
                    for (auto& N : Ctx.m_Nodes)
                        if (N.m_Id == RefId) { S.m_SelectedGap = bHasAfter ? N.m_Order + 1 : N.m_Order; bFound = true; break; }
                    if (!bFound) return "Select: -MarkerAfter/-MarkerBefore node no longer exists";
                }
                return {};
            }

            void BackupCurrenState(xundo::undo_file& File) noexcept override { BackupSelection(get<node_os_command_context>(), File); }
            void Undo(xundo::undo_file& File) noexcept override { RestoreSelection(get<node_os_command_context>(), File); }

            xcmdline::parser::handle m_hNodes, m_hLink, m_hMarkerAfter, m_hMarkerBefore;
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
                S.m_SelectedGap  = -1;
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

    // Auto-discovered, not hardcoded: every Plugins/<Folder>/*.cpp here becomes an Add Node menu entry
    // immediately, in its not-yet-compiled state - dropping a new plugin folder in is the entire
    // integration step for a new native node kind.
    std::vector<nodeos::plugin_source_entry> Sources = nodeos::ScanPluginSources("D:/LIONant/xGPU/source/Examples/E27_NodeOS/Plugins");
    std::vector<nodeos::available_node_type> AvailableTypes;
    std::vector<nodeos::node_instance>       Nodes;
    std::vector<nodeos::link_instance>       Links;

    nodeos::mesh_preview_system MeshPreview;
    if (!MeshPreview.Init(Device))
        return 1;

    nodeos::canvas_drag      Drag;
    nodeos::canvas_selection Selection;
    nodeos::canvas_view      View;
    nodeos::canvas_node_drag NodeDrag;

    bool bDirty = false; // persists across frames - see the deferred-execute comment below
    char GraphPathBuffer[260] = "D:/LIONant/xGPU/source/Examples/E27_NodeOS/graph.txt";
    std::string GraphStatus;

    // Every graph mutation (add/delete node, connect, reorder, edit a property, change selection)
    // goes through this System - see the "Commands" sections above for why: it's the one entry point
    // with zero ImGui/xgpu dependency that a future headless runner or driver plugin could call
    // identically to how the ImGui code below calls it. bAutoLoadSave=false - a fresh undo stack each
    // run, since a stale on-disk history from a previous, differently-shaped graph would be more
    // confusing than useful for this example.
    nodeos::commands::node_os_command_context CmdContext{ Nodes, Links, Selection, Sources, AvailableTypes, bDirty };
    xundo::system NodeOsUndo;
    if (auto Err = NodeOsUndo.Init("D:/LIONant/xGPU/source/Examples/E27_NodeOS/UndoHistory", false); !Err.empty())
        nodeos::Debugger(std::format("Node OS: xundo Init failed: {}", Err));
    nodeos::commands::create_node_cmd     CmdCreateNode(NodeOsUndo, &CmdContext);
    nodeos::commands::delete_nodes_cmd    CmdDeleteNodes(NodeOsUndo, &CmdContext);
    nodeos::commands::delete_link_cmd     CmdDeleteLink(NodeOsUndo, &CmdContext);
    nodeos::commands::connect_cmd         CmdConnect(NodeOsUndo, &CmdContext);
    nodeos::commands::reorder_nodes_cmd   CmdReorderNodes(NodeOsUndo, &CmdContext);
    nodeos::commands::set_properties_cmd  CmdSetProperties(NodeOsUndo, &CmdContext);
    nodeos::commands::select_cmd          CmdSelect(NodeOsUndo, &CmdContext);
    nodeos::commands::clear_selection_cmd CmdClearSelection(NodeOsUndo, &CmdContext);

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
            nodeos::ExecuteGraph(Device, Nodes, Links, MeshPreview);
            bDirty = false;
        }

        MeshPreview.RenderAll(MainWindow);

        // A fresh compile, a new/inserted node, a new/removed connection, a deletion, or a property
        // edit all mark this dirty so the graph re-runs (at the top of the NEXT frame, per above) and
        // every mesh preview reflects it - no manual "Execute Graph" click required for the common
        // case; the button below remains for a manual force-rerun.
        nodeos::DrawNodeLibraryPanel(Sources, AvailableTypes, bDirty);
        nodeos::DrawGraphCanvas(Sources, AvailableTypes, Nodes, Links, MeshPreview, Drag, Selection, View, NodeDrag, bDirty, NodeOsUndo);
        nodeos::DrawNodePropertiesPanel(Nodes, Selection.m_SelectedNodes, NodeOsUndo);

        ImGui::SetNextWindowPos(ImVec2(1265, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(200, 80), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Run"))
        {
            if (ImGui::Button("Execute Graph"))
                bDirty = true; // same deferred path, not an immediate call - see the comment above

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
                GraphStatus = nodeos::SaveGraph(GraphPathBuffer, Nodes, Links, AvailableTypes) ? "Saved." : "Save failed - see log.";
            ImGui::SameLine();
            if (ImGui::Button("Load"))
            {
                Selection.m_SelectedNodes.clear();
                GraphStatus = nodeos::LoadGraph(GraphPathBuffer, Nodes, Links, Sources, AvailableTypes) ? "Loaded." : "Load failed - see log.";
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
