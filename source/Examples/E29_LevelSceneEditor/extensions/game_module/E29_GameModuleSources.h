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

    // REAL BUG FOUND LIVE (2026-09-19), while testing the PCH-header fix above:
    // BuildGamePluginIfStale's own staleness check (E29_GamePluginBuild.h) only ever compared
    // GameProject/E29_Game.cpp's own mtime against the compiled DLL's - a module's own source_db
    // files (and the generated fragment listing them) were never consulted at all, so editing an
    // existing module file's CONTENT (SetScriptSourceFileContent deliberately does NOT call
    // RegenerateGameModuleSources for exactly this case - a content-only edit needs no reconfigure)
    // would never be seen as stale and would silently never trigger a rebuild through any of the
    // normal reload triggers. Confirmed by touching TestModule.h and observing Play still reported
    // "up to date" against the un-rebuilt DLL. Fixed by having BuildGamePluginIfStale also compare
    // against the newest of: the generated fragment's own mtime (covers add/remove/rename, which DO
    // regenerate it) and every currently-referenced module's own source_db file mtimes (covers a
    // pure content edit, which doesn't touch the fragment at all) - this walks the exact same
    // g_ScriptConfig.m_ModuleRefs list RegenerateGameModuleSources already does, so a module that
    // fails to resolve is silently skipped here too, matching this codebase's own established
    // best-effort posture for a stale/incomplete reference.
    inline std::filesystem::file_time_type GetLatestModuleSourceWriteTime() noexcept
    {
        std::error_code Ec;
        auto Latest = std::filesystem::file_time_type::min();

        if (const auto FragTime = std::filesystem::last_write_time(GameModuleFragmentPath(), Ec); !Ec && FragTime > Latest)
            Latest = FragTime;

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
                if (Ext != L".cpp" && Ext != L".h" && Ext != L".hpp") continue;

                if (const auto FileTime = Fs.last_write_time(Ec); !Ec && FileTime > Latest)
                    Latest = FileTime;
            }
        }
        return Latest;
    }

    // Every referenced Script-Module resource's own source_db files, plus a source_group(TREE ...)
    // call per module so Visual Studio's Solution Explorer shows one folder per module - direct user
    // request ("at least from their tree perspective... create a root folder with the name of the
    // script-module and dump its tree there"). A module that doesn't resolve (e.g. stale guid) or has
    // an empty/missing source_db is silently skipped, not treated as an error - matches this
    // project's own "best-effort, never block on an admittedly-incomplete reference" posture.
    //
    // REAL BUG FOUND LIVE (2026-09-19), fixed via the PCH rather than generated stubs: a module
    // consisting ONLY of a header (the common case, since E29_REGISTER_COMPONENT/SYSTEM's whole
    // self-registration mechanism is header-safe/inline by design) was listed in
    // E29_GAME_MODULE_SOURCES as a plain .h - CMake tracks a .h in a target's source list for IDE
    // display only, it does NOT compile it as its own translation unit, so its self-registration
    // globals never ran (confirmed via ListComponentTypes silently missing it). The first fix
    // generated a throwaway companion .cpp per header - direct user correction: that's solving the
    // wrong layer. E29_Game already has its own real, always-compiled .cpp for exactly this purpose -
    // the synthetic translation unit CMake's own target_precompile_headers() generates to build the
    // PCH (CMakeLists.txt's "$<$<COMPILE_LANGUAGE:CXX>:.../xecs.h>" entry). Every .h/.hpp a module
    // contributes now goes into E29_GAME_MODULE_PCH_HEADERS instead, appended onto that SAME
    // target_precompile_headers() call - it gets force-#include'd into the PCH's own compile unit
    // (guaranteeing real compilation) AND into every other source in the target (module .cpp files
    // included), with no new generated .cpp anywhere. This also matches how these modules are
    // expected to actually be authored: a header's declarations are normally #include'd by whichever
    // .cpp in the same module actually uses them - the stub was only ever needed for the degenerate
    // header-with-no-consumer case a minimal test fixture happens to hit. Component definitions are
    // the natural fit for this (declarative, rarely edited once stable); systems are expected to live
    // in .cpp files instead (no shared declarations to expose, most of their own iteration is in
    // OnUpdate logic), so PCH invalidation from a module edit should be the exception, not the norm.
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

        std::wstring Content = L"# Auto-generated by AddProjectModuleReference/RemoveProjectModuleReference - do not edit by hand.\n";
        Content += L"set(E29_GAME_MODULE_SOURCES\n";
        for (auto& M : Modules)
            for (auto& F : M.m_Files)
                Content += L"  \"" + ToForward(F) + L"\"\n";
        Content += L")\n\n";

        // Every module header, force-included into E29_Game's own PCH (see this function's own top
        // comment) instead of getting a generated compile-unit stub - pre-wrapped in the same
        // COMPILE_LANGUAGE:CXX guard CMakeLists.txt's own xecs.h entry uses, so the CMakeLists.txt
        // call site is just "target_precompile_headers(E29_Game PRIVATE <xecs.h entry>
        // ${E29_GAME_MODULE_PCH_HEADERS})" - no per-entry wrapping needed there.
        Content += L"set(E29_GAME_MODULE_PCH_HEADERS\n";
        for (auto& M : Modules)
            for (auto& F : M.m_Files)
            {
                const auto Ext = std::filesystem::path(F).extension().wstring();
                if (Ext == L".h" || Ext == L".hpp")
                    Content += L"  \"$<$<COMPILE_LANGUAGE:CXX>:" + ToForward(F) + L">\"\n";
            }
        Content += L")\n\n";

        for (auto& M : Modules)
        {
            Content += std::format(L"source_group(TREE \"{}\" PREFIX \"{}\" FILES\n", ToForward(M.m_Folder), M.m_Name);
            for (auto& F : M.m_Files)
                Content += L"  \"" + ToForward(F) + L"\"\n";
            Content += L")\n";
        }

        // REAL BUG FOUND LIVE (2026-09-19): this function runs unconditionally at every project
        // load (LoadScriptConfig's own call site) as well as on every Add/RemoveProjectModuleReference
        // - previously harmless (nothing consumed the fragment's own mtime), but now that
        // BuildGamePluginIfStale's staleness check also looks at GameModuleFragmentPath()'s mtime
        // (GetLatestModuleSourceWriteTime, above) an unconditional rewrite makes the fragment look
        // "just modified" on every single launch even when nothing actually changed, forcing a full
        // cmake reconfigure + PCH rebuild every time the editor opens - confirmed live (rebuild fired
        // on startup with no edits made). Fixed by only writing when the generated content actually
        // differs from what's already on disk, so the fragment's mtime - and therefore the staleness
        // check - only moves on a real add/remove/rename, exactly like every other module source file
        // already does for a real content edit.
        std::error_code Ec;
        const auto FragmentPath = GameModuleFragmentPath();
        {
            std::wifstream ExistingIn(FragmentPath, std::ios::binary);
            if (ExistingIn.is_open())
            {
                std::wstring Existing((std::istreambuf_iterator<wchar_t>(ExistingIn)), std::istreambuf_iterator<wchar_t>());
                if (Existing == Content) return;
            }
        }

        std::filesystem::create_directories(FragmentPath.parent_path(), Ec);
        std::wofstream Out(FragmentPath, std::ios::trunc);
        Out << Content;
    }
}

#endif // E29_GAME_MODULE_SOURCES_H
