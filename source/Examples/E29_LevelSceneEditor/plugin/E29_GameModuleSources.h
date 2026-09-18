#ifndef E29_GAME_MODULE_SOURCES_H
#define E29_GAME_MODULE_SOURCES_H
#pragma once

// Generates GameProject\E29_Game_Modules.cmake from the project's own Script-Module build-membership
// list (Project.config\Script.config.txt, e29::g_ScriptConfig.m_ModuleRefs - see
// E29_ProjectScriptConfig.h) - the CMake fragment the top-level CMakeLists.txt's own E29_Game target
// include()'s for its extra sources (see that file's own comment at its add_library(E29_Game SHARED
// ...) call). Regenerated on every AddProjectModuleReference/RemoveProjectModuleReference
// (E29_Commands_Scripting.h) and once at project load (LoadScriptConfig's own call site,
// E29_LevelScene_Editor.cpp) so a fresh checkout's fragment is never stale relative to what's
// actually persisted. The actual cmake reconfigure this fragment change requires rides Game.dll's
// EXISTING reload triggers (window focus regained, Play pressed, etc.) - direct user decision, no
// new trigger mechanism ("what we had was based on unity experience... we keep what we have").
#include <fstream>

namespace e29
{
    // Same derivation game_plugin_state's own m_CompiledDllPath already uses (this process's own exe
    // path, parent_path() up to the repo root) - fully self-contained, doesn't reach into
    // game_plugin_state/g_pGamePlugin at all. "Where is the xGPU repo" and "which project is open"
    // are two different paths in general - they only happen to nest in this dev environment.
    inline std::filesystem::path GetRepoRoot() noexcept
    {
        TCHAR szModulePath[MAX_PATH];
        GetModuleFileName(NULL, szModulePath, MAX_PATH);
        // .../Build/xGPUExamples.vs2022/Debug/xGPU_unit_test.exe -> four parent_path() calls to repo root.
        return std::filesystem::path(szModulePath).parent_path().parent_path().parent_path().parent_path();
    }

    inline std::filesystem::path GameModuleFragmentPath() noexcept
    {
        return GetRepoRoot() / L"source" / L"Examples" / L"E29_LevelSceneEditor" / L"GameProject" / L"E29_Game_Modules.cmake";
    }

    // Where generated compile-unit stubs live for header-only module files (see RegenerateGameModuleSources's
    // own comment on why these exist) - pure build scratch, never inside a resource's own tracked
    // source_db, never committed.
    inline std::filesystem::path GameModuleGeneratedStubDir() noexcept
    {
        return GetRepoRoot() / L"Build" / L"GeneratedModuleStubs";
    }

    // Every referenced Script-Module resource's own source_db files, plus a source_group(TREE ...)
    // call per module so Visual Studio's Solution Explorer shows one folder per module - direct user
    // request ("at least from their tree perspective... create a root folder with the name of the
    // script-module and dump its tree there"). A module that doesn't resolve (e.g. stale guid) or has
    // an empty/missing source_db is silently skipped, not treated as an error - matches this
    // project's own "best-effort, never block on an admittedly-incomplete reference" posture.
    //
    // REAL BUG FOUND LIVE (2026-09-19): a module consisting ONLY of a header (the common case, since
    // E29_REGISTER_COMPONENT/SYSTEM's whole self-registration mechanism is header-safe/inline by
    // design) was listed in E29_GAME_MODULE_SOURCES as a plain .h - CMake tracks a .h in a target's
    // source list for IDE display only, it does NOT compile it as its own translation unit. With
    // nothing left to #include it (the earlier hand-wired bridge in E29_Game.cpp is gone on purpose -
    // that was the whole point of building self-registration), the header's own self-registration
    // globals simply never ran, and the component silently never showed up as registered - confirmed
    // via ListComponentTypes missing it entirely. Fixed here, not by asking module authors to always
    // pair a header with a hand-written .cpp: for every .h/.hpp found, ALSO generate a trivial
    // companion .cpp (just one #include line) in the scratch stub dir above, and compile THAT instead
    // of relying on the header alone - the header itself still gets listed too, purely for the
    // source_group/IDE-display side, but the STUB is what actually makes it a real compile unit.
    inline void RegenerateGameModuleSources() noexcept
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

        // Forward slashes throughout - CMake's own list/string syntax treats backslash as an escape
        // character (same real bug already found+fixed once this session for the now-deleted Phase 2
        // per-resource compiler's own generated CMakeLists.txt).
        auto ToForward = [](std::wstring Path) noexcept { std::ranges::replace(Path, L'\\', L'/'); return Path; };

        std::error_code Ec;
        const auto StubDir = GameModuleGeneratedStubDir();
        std::filesystem::create_directories(StubDir, Ec);

        std::wstring Content = L"# Auto-generated by AddProjectModuleReference/RemoveProjectModuleReference - do not edit by hand.\n";
        Content += L"set(E29_GAME_MODULE_SOURCES\n";
        for (auto& M : Modules)
        {
            for (auto& F : M.m_Files)
            {
                Content += L"  \"" + ToForward(F) + L"\"\n";

                const auto Ext = std::filesystem::path(F).extension().wstring();
                if (Ext == L".h" || Ext == L".hpp")
                {
                    // Deterministic, unique stub name - module name + header stem, so two different
                    // modules each having e.g. a "Component.h" don't collide.
                    const auto StubName = M.m_Name + L"_" + std::filesystem::path(F).stem().wstring() + L".generated.cpp";
                    const auto StubPath = StubDir / StubName;
                    std::wofstream StubOut(StubPath, std::ios::trunc);
                    StubOut << L"// Auto-generated - do not edit by hand. Exists purely to give \"" << std::filesystem::path(F).filename().wstring()
                            << L"\" a real translation unit, since a header-only module's own self-registration globals never run otherwise.\n";
                    StubOut << L"#include \"" << ToForward(F) << L"\"\n";
                    Content += L"  \"" + ToForward(StubPath.wstring()) + L"\"\n";
                }
            }
        }
        Content += L")\n\n";

        for (auto& M : Modules)
        {
            Content += std::format(L"source_group(TREE \"{}\" PREFIX \"{}\" FILES\n", ToForward(M.m_Folder), M.m_Name);
            for (auto& F : M.m_Files)
                Content += L"  \"" + ToForward(F) + L"\"\n";
            Content += L")\n";
        }

        const auto FragmentPath = GameModuleFragmentPath();
        std::filesystem::create_directories(FragmentPath.parent_path(), Ec);
        std::wofstream Out(FragmentPath, std::ios::trunc);
        Out << Content;
    }
}

#endif // E29_GAME_MODULE_SOURCES_H
