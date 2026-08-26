#pragma once
#include "NodeOS_Common.h"

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
        // A muted, dark steel blue, not gray - once the grid (below) filled the canvas with its own
        // thin gray lines, the rail's old flat gray made it blend into the grid instead of reading
        // as a structural column boundary. Kept dark enough to stay well behind the bright per-type
        // wire colors (cyan/green/etc.) rather than competing with them for attention. Also drawn
        // thicker than a grid line (see the AddLine call) for the same reason.
        constexpr ImU32 Rail       = IM_COL32(24, 42, 64, 255);   // per-column background rail lines
        constexpr ImU32 Selected   = IM_COL32(58, 121, 187, 255);  // Unity's own selection-outline blue
        constexpr ImU32 Canvas     = IM_COL32(16, 16, 16, 255);    // the graph window's own backdrop, behind the dot grid and every node
        constexpr ImU32 Grid       = IM_COL32(20, 20, 20, 255);    // grid lines (minor) - a shade lighter than Canvas, subtle, never competing with node/wire content
        // TEST: minor/major line-grid look borrowed from E25/E21's own ground-grid shader
        // (E21_GridShader_frag.glsl - BaseColor/LineColor/MajorLineWidth) - a bit brighter than the
        // minor lines above, still well below node/wire brightness.
        constexpr ImU32 GridMajor  = IM_COL32(27, 27, 27, 255);
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

    // Both are plain std::uint64_t underneath (xresource::guid_generator::Instance64's own return
    // type) - these don't add compile-time type safety, a node_guid still converts freely to/from a
    // raw uint64_t or a link_guid. What they buy is self-documentation at the call site: a function
    // signature or struct field written in terms of node_guid/link_guid says what KIND of id it is
    // without a reader having to trace back to whichever generator produced it.
    using node_guid   = std::uint64_t;
    using link_guid   = std::uint64_t;
    using spine_guid  = std::uint64_t;
    using column_guid = std::uint64_t;

    //------------------------------------------------------------------------------------------------
    // One instance of a node type dropped on the canvas. m_pNode IS the node - its own property
    // members, ports, and Execute all live on the polymorphic object itself (see
    // xnode_os_plugin_api.h), not a separate opaque blob. The module its factory lives in is never
    // FreeLibrary'd, even across a plugin recompile (see MergeCompileResult), so m_pNode stays valid
    // for this instance's whole life.
    //------------------------------------------------------------------------------------------------
    struct node_instance
    {
        node_guid                        m_Id = 0;
        xnode_os_node*                   m_pNode = nullptr;
        spine_guid                       m_SpineId = 0;       // which spine (see `spine` below) this box belongs to
        int                              m_Order = 0;         // stacking rank, dense WITHIN m_SpineId only (never across the whole graph) - reorder with the header's up/down buttons, never freely dragged
        std::vector<void*>                m_CachedOutputs;      // filled after a successful Execute
        bool                              m_bHasRun = false;
        std::string                       m_LastError;
        node_guid                        m_OwnedEndId = 0;    // 0 = doesn't own a marker. A control-flow node (If/ForEachLoop, see NODE_SCRIPTING_DESIGN.md section 4.1) owns a paired End/End-Else marker node, created and destroyed together with it - never an ordinary, independently-editable link.
    };

    // The host-owned half of a node_instance (Id/Source/Type/Order/SpineId/OwnedEndId) as an ordinary
    // reflected type, plain (no xproperty::base, same XPROPERTY_DEF/XPROPERTY_REG pattern
    // InspectMeshNode's own mesh_stats already proves in this codebase) rather than hand-rolled
    // Stream.Field calls. Each node now serializes ITSELF as one self-contained, self-describing unit
    // - this record, immediately followed by the plugin's own "xProperties" record if it has one -
    // instead of one shared "Nodes" table (fixed columns, one row per node) kept in lockstep-by-array-
    // order with a separately-counted sequence of per-node property blocks. That two-sequence design
    // is what actually caused the real bug this replaces: a node's reflected shape gaining a new
    // DONT_SAVE-only member made the OLD HasAnyProperties predicate ("has any member at all") say a
    // property block would follow when the real serializer's collector (which skips DONT_SAVE) wrote
    // zero bytes for it - so the reader consumed the NEXT node's own block instead, silently cascading
    // a misalignment through the rest of the file. A plain per-node reflected record sidesteps the
    // whole class of bug: xproperty's own "Name"/"Value" row format means a missing/renamed field is
    // just a missing row, not a positional-column desync - no more hand-rolled tolerant-missing-field
    // checks needed either (see OwnedEndId's old FIELD_NOT_FOUND tolerance in LoadGraph, now moot).
    struct node_topology
    {
        std::uint64_t Id         = 0;
        std::string   Source;    // plugin's Plugins/<DirName>/ folder name, not a full path
        std::string   Type;      // factory's getName()
        std::int32_t  Order      = 0;
        std::uint64_t SpineId    = 0;
        std::uint64_t OwnedEndId = 0; // 0 = doesn't own a marker - see node_instance::m_OwnedEndId

        XPROPERTY_DEF
        ( "node_topology", node_topology
        , obj_member<"Id",         &node_topology::Id>
        , obj_member<"Source",     &node_topology::Source>
        , obj_member<"Type",       &node_topology::Type>
        , obj_member<"Order",      &node_topology::Order>
        , obj_member<"SpineId",    &node_topology::SpineId>
        , obj_member<"OwnedEndId", &node_topology::OwnedEndId>
        )
    };
    XPROPERTY_REG(node_topology)

    // m_bReadOnly is deliberately NOT an obj_member below (so it's never reflected/serialized) - it's
    // always re-derived on load from whether some node's own m_OwnedEndId matches this link (see
    // LoadGraph), never stored a second time.
    struct link_instance
    {
        link_guid m_Id = 0;
        node_guid m_SourceNode = 0; int m_SourceOutput = 0;
        node_guid m_TargetNode = 0; int m_TargetInput  = 0;
        bool      m_bReadOnly  = false; // an owner<->End ownership link (NODE_SCRIPTING_DESIGN.md section 4.1) - can never be dragged loose or deleted independently; only removed when one of its two nodes is deleted (which removes both, via DeleteNodes' cascade)

        XPROPERTY_DEF
        ( "link_instance", link_instance
        , obj_member<"Id",           &link_instance::m_Id>
        , obj_member<"SourceNode",   &link_instance::m_SourceNode>
        , obj_member<"SourceOutput", &link_instance::m_SourceOutput>
        , obj_member<"TargetNode",   &link_instance::m_TargetNode>
        , obj_member<"TargetInput",  &link_instance::m_TargetInput>
        )
    };
    XPROPERTY_REG(link_instance)

    //------------------------------------------------------------------------------------------------
    // The horizontal container a spine (or several) lives in. Columns form a plain doubly-linked
    // list (never a tree) with exactly one root - today's original, always-there column. A column's
    // width is its own boxes + its own highway lane extent, recomputed fresh every frame in
    // DrawGraphCanvas like everything else here - nothing about live geometry is ever stored here.
    //------------------------------------------------------------------------------------------------
    struct column
    {
        column_guid m_Id      = 0;
        column_guid m_LeftId  = 0;  // 0 = no neighbor yet
        column_guid m_RightId = 0;  // 0 = no neighbor yet
        bool        m_bIsRoot = false; // exactly one column ever has this set - the layout walk's fixed anchor (RootColumnId, used throughout DrawGraphCanvas/command Redo()s); not vestigial, see the many m_bIsRoot call sites elsewhere in this file

        XPROPERTY_DEF
        ( "column", column
        , obj_member<"Id",      &column::m_Id>
        , obj_member<"LeftId",  &column::m_LeftId>
        , obj_member<"RightId", &column::m_RightId>
        , obj_member<"IsRoot",  &column::m_bIsRoot>
        )
    };
    XPROPERTY_REG(column)

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
        spine_guid  m_Id       = 0;
        column_guid m_ColumnId = 0;
        bool        m_bIsRoot  = false; // exactly one spine
        float       m_Y        = 0.0f;  // absolute world Y of this spine's own top slot (root ignores this - always geo::TOP)

        XPROPERTY_DEF
        ( "spine", spine
        , obj_member<"Id",       &spine::m_Id>
        , obj_member<"ColumnId", &spine::m_ColumnId>
        , obj_member<"IsRoot",   &spine::m_bIsRoot>
        , obj_member<"Y",        &spine::m_Y>
        )
    };
    XPROPERTY_REG(spine)

    // The one record in SaveGraph/LoadGraph that was still hand-rolled Stream.Record/Field calls -
    // now the same official xproperty::sprop::serializer::Stream() every other record here already
    // uses (node_topology/link_instance/column/spine), so the ENTIRE file goes through one
    // serialization path, not "every record except this one".
    struct graph_header
    {
        std::int32_t ColumnCount = 0;
        std::int32_t SpineCount  = 0;
        std::int32_t NodeCount   = 0;
        std::int32_t LinkCount   = 0;

        XPROPERTY_DEF
        ( "graph_header", graph_header
        , obj_member<"ColumnCount", &graph_header::ColumnCount>
        , obj_member<"SpineCount",  &graph_header::SpineCount>
        , obj_member<"NodeCount",   &graph_header::NodeCount>
        , obj_member<"LinkCount",   &graph_header::LinkCount>
        )
    };
    XPROPERTY_REG(graph_header)

    //------------------------------------------------------------------------------------------------
    // The result of compiling+loading one plugin - a plain value (no shared state touched while
    // building it) so it can be produced on a background thread via std::async and handed back to the
    // main thread to merge, for the Node Library panel's "Compile & Load"/"Recompile & Reload" button.
    // Defined here, ahead of plugin_source_entry, since std::future<T> needs T complete at the point
    // plugin_source_entry declares its m_Future member below.
    //------------------------------------------------------------------------------------------------
    struct plugin_compile_result
    {
        bool                                  m_bSuccess = false;
        std::string                            m_Log;
        HMODULE                                m_Module   = nullptr;
        std::vector<xnode_os_node_factory*>    m_Factories; // one or more - see NodeOS_CreateFactories in xnode_os_plugin_api.h
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

        // Tried FIRST, purely additive - see xnode_os_plugin_api.h's own comment. A plugin that
        // doesn't export this (every existing single-type plugin) falls straight through to the
        // original NodeOS_CreateFactory path below, unchanged.
        if (auto* pCreateFactories = (xnode_os_pfn_create_factories*)GetProcAddress(Module, XNODE_OS_CREATE_FACTORIES_NAME))
        {
            auto RegisterFn = +[](void* pUserData, xnode_os_node_factory& F) { static_cast<std::vector<xnode_os_node_factory*>*>(pUserData)->push_back(&F); };
            pCreateFactories(GetHostBridge(), &Result.m_Factories, RegisterFn);
            if (Result.m_Factories.empty())
            {
                Result.m_Log += "\n[DLL exports " XNODE_OS_CREATE_FACTORIES_NAME " but registered zero node types]";
                FreeLibrary(Module);
                return Result;
            }

            std::string Names;
            for (auto* pF : Result.m_Factories) Names += (Names.empty() ? "" : ", ") + std::string(pF->getName());
            Result.m_Log += std::format("\n[compiled and loaded successfully - {} node type(s) registered: {}]", Result.m_Factories.size(), Names);
            Result.m_bSuccess = true;
            Result.m_Module   = Module;
            return Result;
        }

        auto pCreateFactory = (xnode_os_pfn_create_factory*)GetProcAddress(Module, XNODE_OS_CREATE_FACTORY_NAME);
        if (!pCreateFactory)
        {
            Result.m_Log += "\n[DLL loaded but does not export " XNODE_OS_CREATE_FACTORY_NAME " or " XNODE_OS_CREATE_FACTORIES_NAME "]";
            FreeLibrary(Module);
            return Result;
        }

        xnode_os_node_factory& Factory = pCreateFactory(GetHostBridge());

        Result.m_Log += std::format("\n[compiled and loaded successfully - '{}' node type registered]", Factory.getName());
        Result.m_bSuccess = true;
        Result.m_Module   = Module;
        Result.m_Factories.push_back(&Factory);
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

        // One or more - see xnode_os_plugin_api.h's NodeOS_CreateFactories - all sharing this same
        // Entry (m_DirName/m_Module/m_SourcePath), distinguished only by their own factory's getName().
        for (auto* pFactory : Result.m_Factories)
            OutTypes.push_back({ std::format("{} :: {}", Entry.m_DisplayName, pFactory->getName()), Result.m_Module, pFactory, Entry.m_SourcePath, Entry.m_DirName });

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

    //------------------------------------------------------------------------------------------------
    // Wraps the captured backbuffer as an xbitmap (via setup()) and hands it to
    // xbmp::tools::writers::SaveSTDImage, which dispatches to stb_image_write by the path's own
    // extension (.png/.bmp/.tga/.jpg). Captured format is B8G8R8A8 packed uint32
    // (xgpu_vulkan_window.cpp's CaptureBackbuffer) - xbitmap::format::B8G8R8A8 names that exact byte
    // order, so no channel-swap/conversion is needed.
    //
    // xbitmap::setup() (see xbitmap.cpp) treats the VERY FIRST slot of the data span as a
    // mip-offset-table entry (an xbitmap::mip{ m_Offset }), not pixel payload - the same trap the
    // raw-span CONSTRUCTOR has (see [[xgpu_screenshot_capture]] memory), just less obvious since
    // setup() looks like a plain "here's my buffer" call. Confirmed by hand against setup()'s own
    // asserts and getMipPtr's offset math: one extra uint32 slot at the front holding
    // sizeof(xbitmap::mip) (the byte offset to skip past this one-entry table) is what a correct
    // 1-mip/1-frame image needs - skipping it is what actually crashed (abort(), not a silent
    // wrong-image) the first time this was written without it.
    //------------------------------------------------------------------------------------------------
    static bool WriteScreenshotImage(const std::string& Path, const std::vector<std::uint32_t>& Pixels, int Width, int Height)
    {
        if (Width <= 0 || Height <= 0 || Pixels.empty()) return false;

        // Every writer path SaveSTDImage(xbitmap&) can take ends up emitting all 4 captured channels
        // (see xbmp_tools_std_image_writer.cpp - B8G8R8A8 isn't one of the 3 fast-pathed formats, so
        // it always goes through the generic per-pixel xcolor conversion, which is 32bpp/RGBA
        // unconditionally). Unlike the hand-rolled TGA writer this replaced, there's no "write 24bpp
        // instead" option here - so the swapchain's real-but-compositor-ignored alpha (see this
        // memory's own gotcha 2) has to be forced opaque explicitly, or PNG viewers that DO honor
        // alpha render exactly the "corrupted text" artifact that gotcha describes. B8G8R8A8 means A
        // is the top byte of the packed uint32.
        std::vector<std::uint32_t> Padded(1 + static_cast<std::size_t>(Width) * Height);
        Padded[0] = sizeof(xbitmap::mip);
        std::transform(Pixels.begin(), Pixels.end(), Padded.begin() + 1, [](std::uint32_t Px) { return Px | 0xFF000000u; });

        xbitmap Bitmap;
        Bitmap.setup(static_cast<std::uint32_t>(Width), static_cast<std::uint32_t>(Height)
            , xbitmap::format::B8G8R8A8
            , static_cast<std::uint64_t>(Width) * Height * sizeof(std::uint32_t) // FaceSize - pixel payload only, excludes the header slot
            , std::as_writable_bytes(std::span(Padded))
            , false // bFreeMemoryOnDestruction - Padded stays vector-owned
            , 1, 1  // nMips, nFrames
            );

        const std::wstring WPath(Path.begin(), Path.end());
        return !xbmp::tools::writers::SaveSTDImage(WPath, Bitmap);
    }

    // A literal's resolved runtime bytes live here, not in a shared static/thread_local buffer - see
    // GetInputValue's own comment for why a single shared slot would be wrong the moment a node has
    // more than one unconnected literal input. std::deque (not vector) specifically: appending never
    // invalidates an already-returned pointer into an earlier element, since nothing here ever needs
    // random-access indexing, only stable addresses for as long as the deque itself is alive.
    // (Moved here from its original home in the canvas-support section - a trivial POD pair with no
    // real coupling to interpreter/canvas logic, and every downstream section already depends on this
    // header anyway.)
    struct literal_slot { unsigned char m_Bytes[8]; };
    using literal_storage = std::deque<literal_slot>;
}
