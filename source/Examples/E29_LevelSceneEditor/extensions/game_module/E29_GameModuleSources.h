#ifndef E29_GAME_MODULE_SOURCES_H
#define E29_GAME_MODULE_SOURCES_H
#pragma once

// The script project. A script module is only source files (its resource descriptor's source_db folder); the editor turns the
// project's referenced modules into a CMake project it generates under <project>\Cache\Script (never checked in, rebuilt from
// the modules), builds it with Visual Studio's generator, and gets Game.dll in the root of the project's compiled resources
// (<project>\Cache\Resources\Platforms\WINDOWS). The generated project links the game entry points of xscript_module.plugin
// and the xECSV2 import library of this editor's own build, so the DLL shares the editor's one component registry.
//
// The project is regenerated when the module list changes (AddProjectModuleReference and friends) and once when the project
// loads. The cmake reconfigure and build it needs ride the Game.dll reload triggers (window focus regained, Play pressed).
#include <fstream>

namespace e29
{
    // Everything about where the script project's files are. Computed once on the main thread: the build runs on a background
    // thread and must not reach into the library manager.
    struct script_project_paths
    {
        std::filesystem::path m_Project;      // the project root (the folder that holds Descriptors and Project.config)
        std::filesystem::path m_Root;         // <project>\Cache\Script
        std::filesystem::path m_BuildDir;     // <Root>\Build: the Visual Studio solution and its intermediate files
        std::filesystem::path m_CMakeLists;   // <Root>\CMakeLists.txt, generated
        std::filesystem::path m_Dll;          // <project>\Cache\Resources\Platforms\WINDOWS\Game.dll, the compiled resource
        std::filesystem::path m_PdbDir;       // where the linker writes Game.pdb for the running configuration
        std::filesystem::path m_LoadedDir;    // <Root>\Loaded: the copies of Game.dll that are actually loaded
        std::filesystem::path m_XGpuRoot;     // the xGPU checkout this editor was built from
        std::filesystem::path m_XGpuBinDir;   // its build directory: <Config>\xECSV2.lib is under it
        std::wstring          m_Config;       // "Debug" or "Release": the configuration of the running editor

        std::filesystem::path RuntimeDir() const { return m_XGpuRoot / L"plugins" / L"xscript_module.plugin" / L"source" / L"Runtime"; }
    };

    inline script_project_paths MakeScriptProjectPaths(const std::filesystem::path& Project) noexcept
    {
        TCHAR szModulePath[MAX_PATH];
        GetModuleFileName(NULL, szModulePath, MAX_PATH);
        const std::filesystem::path ExeDir = std::filesystem::path(szModulePath).parent_path();   // .../Build/<BuildDirName>/<Config>

        script_project_paths P;
        P.m_Project    = Project;
        P.m_Config     = ExeDir.filename().wstring();
        P.m_XGpuBinDir = ExeDir.parent_path();
        P.m_XGpuRoot   = P.m_XGpuBinDir.parent_path().parent_path();
        P.m_Root       = Project / L"Cache" / L"Script";
        P.m_BuildDir   = P.m_Root / L"Build";
        P.m_CMakeLists = P.m_Root / L"CMakeLists.txt";
        P.m_Dll        = Project / L"Cache" / L"Resources" / L"Platforms" / L"WINDOWS" / L"Game.dll";
        P.m_PdbDir     = P.m_BuildDir / L"GamePdb" / P.m_Config;
        P.m_LoadedDir  = P.m_Root / L"Loaded";
        return P;
    }

    // The project this example edits sits next to the xGPU checkout.
    inline script_project_paths MakeScriptProjectPaths() noexcept
    {
        auto P = MakeScriptProjectPaths(std::filesystem::path{});
        return MakeScriptProjectPaths(P.m_XGpuRoot / L"example.lionprj");
    }

    // The newest time at which anything the DLL is built from changed: the game entry files, the generated project (a module
    // added, removed or renamed) and every referenced module's own source files (a plain content edit changes none of the
    // others). It reads the script config and the library manager, so it is called on the main thread and its result is
    // handed to the build as a value. A module that does not resolve is skipped, like a stale reference everywhere else.
    inline std::filesystem::file_time_type GetLatestModuleSourceWriteTime(const script_project_paths& P) noexcept
    {
        std::error_code Ec;
        auto Latest = std::filesystem::file_time_type::min();
        auto Consider = [&](const std::filesystem::path& File) noexcept
        {
            if (const auto T = std::filesystem::last_write_time(File, Ec); !Ec && T > Latest) Latest = T;
        };

        Consider(P.m_CMakeLists);
        Consider(P.RuntimeDir() / L"xscript_game_entry.cpp");
        Consider(P.RuntimeDir() / L"xscript_registration.h");

        for (auto& Ref : g_ScriptConfig.m_ModuleRefs)
        {
            std::wstring DescFolder;
            e10::g_LibMgr.getNodeInfo(Ref, [&](e10::library_db::info_node& Node)
            {
                const auto SlashPos = Node.m_Path.find_last_of(L'\\');
                DescFolder = (SlashPos == std::wstring::npos) ? Node.m_Path : Node.m_Path.substr(0, SlashPos);
            });
            if (DescFolder.empty()) continue;

            const auto SourceDb = DescFolder + L"\\source_db";
            if (!std::filesystem::exists(SourceDb, Ec)) continue;

            for (auto& Fs : std::filesystem::directory_iterator(SourceDb, Ec))
            {
                if (Ec || !Fs.is_regular_file()) continue;
                const auto Ext = Fs.path().extension().wstring();
                if (Ext == L".cpp" || Ext == L".h" || Ext == L".hpp") Consider(Fs.path());
            }
        }
        return Latest;
    }

    // Writes <Root>\CMakeLists.txt from the referenced modules: their source files, one source_group per module so Visual
    // Studio's Solution Explorer shows a folder per module, and their headers force-included into the precompiled header.
    // A header is only listed in a target's sources for display; CMake does not compile it, so a header-only module (the
    // common case, since self-registration is inline by design) would never run its registration. Force-including it into the
    // PCH, which CMake does compile, guarantees it is compiled, with no generated stub.
    //
    // The file is only written when its content changes (and then this returns true): its time is one of the things the DLL is
    // compared against, and rewriting it at every launch would force a full rebuild every time the editor opens.
    inline bool RegenerateGameModuleSources(const script_project_paths& P) noexcept
    {
        struct module_entry { std::wstring m_Folder; std::wstring m_Name; std::vector<std::wstring> m_Files; };
        std::vector<module_entry> Modules;

        for (auto& Ref : g_ScriptConfig.m_ModuleRefs)
        {
            std::wstring DescFolder;
            std::string  Name;
            e10::g_LibMgr.getNodeInfo(Ref, [&](e10::library_db::info_node& Node)
            {
                Name = Node.m_Info.m_Name;
                const auto SlashPos = Node.m_Path.find_last_of(L'\\');
                DescFolder = (SlashPos == std::wstring::npos) ? Node.m_Path : Node.m_Path.substr(0, SlashPos);
            });
            if (DescFolder.empty()) continue;

            const auto SourceDb = DescFolder + L"\\source_db";
            std::error_code Ec;
            if (!std::filesystem::exists(SourceDb, Ec)) continue;

            module_entry Entry;
            Entry.m_Folder = SourceDb;
            Entry.m_Name   = Name.empty() ? L"Module" : xstrtool::To(Name);
            for (auto& Fs : std::filesystem::directory_iterator(SourceDb, Ec))
            {
                if (Ec || !Fs.is_regular_file()) continue;
                const auto Ext = Fs.path().extension().wstring();
                if (Ext == L".cpp" || Ext == L".h" || Ext == L".hpp")
                    Entry.m_Files.push_back(Fs.path().wstring());
            }
            if (!Entry.m_Files.empty()) Modules.push_back(std::move(Entry));
        }

        // Forward slashes throughout: CMake treats a backslash as an escape character.
        auto Fwd = [](const std::filesystem::path& Path) noexcept { std::wstring S = Path.wstring(); std::ranges::replace(S, L'\\', L'/'); return S; };
        const auto Root = Fwd(P.m_XGpuRoot);

        std::wstring C = L"# Generated by the editor from the project's script modules - do not edit; it is rewritten when the module list changes.\n";
        C += L"cmake_minimum_required(VERSION 3.10)\nproject(Script LANGUAGES CXX)\n";
        C += L"add_definitions(-DUNICODE -D_UNICODE)\nset(CMAKE_CXX_STANDARD 20)\nset(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
        C += L"set(CMAKE_SUPPRESS_REGENERATION true)\nset(CMAKE_CONFIGURATION_TYPES \"Debug;Release\" CACHE STRING \"\" FORCE)\n\n";
        C += std::format(L"set(XGPU_ROOT \"{}\")\n\n", Root);

        C += L"set(MODULE_SOURCES\n";
        for (auto& M : Modules) for (auto& F : M.m_Files) C += L"  \"" + Fwd(F) + L"\"\n";
        C += L")\n\nset(MODULE_PCH_HEADERS\n";
        for (auto& M : Modules)
            for (auto& F : M.m_Files)
            {
                const auto Ext = std::filesystem::path(F).extension().wstring();
                if (Ext == L".h" || Ext == L".hpp") C += L"  \"$<$<COMPILE_LANGUAGE:CXX>:" + Fwd(F) + L">\"\n";
            }
        C += L")\n\n";

        // Each consumer needs its own copy of xtextfile's and xproperty's backend (no shared state), which is why they are compiled
        // into the DLL and, like the entry point, kept out of the precompiled header.
        C += L"set(BACKEND_SOURCES \"${XGPU_ROOT}/dependencies/xtextfile/source/xtextfile.cpp\" \"${XGPU_ROOT}/dependencies/xproperty/source/xcore/my_properties.cpp\")\n";
        C += L"add_library(Game SHARED \"${XGPU_ROOT}/plugins/xscript_module.plugin/source/Runtime/xscript_game_entry.cpp\" ${MODULE_SOURCES} ${BACKEND_SOURCES})\n";
        C += L"target_include_directories(Game PRIVATE \"${XGPU_ROOT}\"";
        for (const wchar_t* Dep : { L"xECSV2/src", L"xerr", L"xresource_guid", L"xtextfile", L"xproperty", L"xresource_pipeline_v2", L"xstrtool", L"xcontainer", L"xdelegate", L"xscheduler", L"xmath", L"xbits" })
            C += std::format(L" \"${{XGPU_ROOT}}/dependencies/{}\"", Dep);
        C += L")\ntarget_compile_definitions(Game PRIVATE XECS_BUILD_SHARED)\n";
        C += std::format(L"target_link_libraries(Game PRIVATE \"{}/$<CONFIG>/xECSV2.lib\")\n", Fwd(P.m_XGpuBinDir));
        C += L"target_precompile_headers(Game PRIVATE \"$<$<COMPILE_LANGUAGE:CXX>:${XGPU_ROOT}/dependencies/xECSV2/src/xecs.h>\" ${MODULE_PCH_HEADERS})\n";
        C += L"set_source_files_properties(${BACKEND_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)\n";

        // The DLL goes to the compiled resources; the debugger finds symbols through the path embedded at link time, so the PDB
        // is linked with only its bare name (/PDBALTPATH) and the editor copies it next to the copy of the DLL it loads. That
        // leaves the compiler's own PDB free to be rewritten while a debugger is attached. /FS lets concurrent compiles share
        // one PDB, which /MP makes likely.
        const auto DllDir = Fwd(P.m_Dll.parent_path());
        C += std::format(L"set_target_properties(Game PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG \"{0}\" RUNTIME_OUTPUT_DIRECTORY_RELEASE \"{0}\" PDB_OUTPUT_DIRECTORY \"${{CMAKE_BINARY_DIR}}/GamePdb\")\n", DllDir);
        C += L"set_property(TARGET Game APPEND_STRING PROPERTY LINK_FLAGS \" /PDBALTPATH:Game.pdb\")\n";
        C += L"target_compile_options(Game PRIVATE /FS /MP)\n\n";

        for (auto& M : Modules)
        {
            C += std::format(L"source_group(TREE \"{}\" PREFIX \"{}\" FILES\n", Fwd(M.m_Folder), M.m_Name);
            for (auto& F : M.m_Files) C += L"  \"" + Fwd(F) + L"\"\n";
            C += L")\n";
        }

        {
            std::wifstream ExistingIn(P.m_CMakeLists, std::ios::binary);
            if (ExistingIn.is_open())
            {
                std::wstring Existing((std::istreambuf_iterator<wchar_t>(ExistingIn)), std::istreambuf_iterator<wchar_t>());
                if (Existing == C) return false;
            }
        }

        std::error_code Ec;
        std::filesystem::create_directories(P.m_CMakeLists.parent_path(), Ec);
        std::wofstream Out(P.m_CMakeLists, std::ios::trunc);
        Out << C;
        return true;
    }
}

#endif // E29_GAME_MODULE_SOURCES_H
