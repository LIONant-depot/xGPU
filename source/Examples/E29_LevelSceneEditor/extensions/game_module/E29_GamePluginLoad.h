#ifndef E29_GAME_PLUGIN_LOAD_H
#define E29_GAME_PLUGIN_LOAD_H
#pragma once

// Extracted from E29_GamePlugin.h (mechanical move, phase 3 of the kit split - see the umbrella
// file's own top comment). The shadow-copy + LoadLibrary/GetProcAddress mechanics: copying the
// compiler's real output to a generation-suffixed file that's actually loaded
// (CopyGamePluginForLoad), the two-call RegisterComponents/RegisterSystems sequence
// xecs_plugin_api.h's own comment requires (LoadGamePluginComponents, RegisterGamePluginSystems),
// and detach/unload (UnloadGamePlugin). Meant to be included via the umbrella (E29_GamePlugin.h)
// only, after E29_GamePluginLog.h and E29_GamePluginBuild.h (game_plugin_state).
#include "source/Examples/E29_LevelSceneEditor/GameProject/E29_GameRegistration.h"

namespace e29
{
    // Called from RegisterGamePluginSystems below (the one fixed choke point every reload already
    // goes through) rather than duplicated at each of ITS OWN call sites - guarantees this can never
    // be forgotten at some future new reload trigger. GetProcAddress returning null (an
    // older-generation DLL built before this export existed) just means an empty map - every
    // component then falls back to "uncategorized", exactly like it already does today.
    inline void LoadGameComponentDisplayInfo( game_plugin_state& Plugin ) noexcept
    {
        xscene::g_ComponentDisplayInfo.clear();
        if (!Plugin.isLoaded()) return;

        auto* pGetInfo = reinterpret_cast<e29_game_registration::pfn_get_component_display_info>(GetProcAddress(Plugin.m_hModule, e29_game_registration::kGetComponentDisplayInfoName));
        if (pGetInfo == nullptr) return;

        pGetInfo([](void* pUserData, std::uint64_t /*Guid*/, const char* pName, const char* pCategory, int Priority) noexcept
        {
            auto& Map = *reinterpret_cast<std::unordered_map<std::string, xscene::component_display_info>*>(pUserData);
            Map[pName] = { pCategory, Priority };
        }, &xscene::g_ComponentDisplayInfo);
    }

    //---------------------------------------------------------------------------
    // Copies Plugin.m_CompiledDllPath (+ its matching .pdb, if present - direct user requirement:
    // "the job of the editor is to copy the new version of the dll with any symbols it may need
    // for debugging") to a fresh, generation-suffixed filename in the same directory - the file
    // that actually gets LoadLibrary'd. Never overwrites a previous generation's copy (each
    // Generation value names a distinct file) - by the time this runs, UnloadGamePlugin has already
    // deleted the previous one anyway (safe then: nothing still has it mapped). Returns the new
    // copy's path, or empty on failure (nothing compiled yet, or the copy itself failed).
    //---------------------------------------------------------------------------
    inline std::wstring CopyGamePluginForLoad( const std::wstring& CompiledDllPath, std::uint32_t Generation ) noexcept
    {
        std::error_code Ec;
        const std::filesystem::path Compiled = CompiledDllPath;
        if (!std::filesystem::exists(Compiled, Ec))
        {
            LogGamePlugin(std::format("Game.dll: nothing compiled yet at {}", Compiled.string()));
            return {};
        }

        const auto Dir     = Compiled.parent_path();
        const auto Stem    = Compiled.stem().wstring();
        const auto NewDll  = Dir / std::format(L"{}_loaded_{}.dll", Stem, Generation);
        // The COMPILED pdb lives in its own separate "GamePdb/<Config>" directory now (sibling to
        // Dir) AND is linked with /PDBALTPATH set to just its own bare filename ("E29_Game.pdb", no
        // directory) instead of the real compile-time absolute path - see CMakeLists.txt's own
        // comment on the E29_Game target for the full story of why (relocating the directory ALONE
        // was tried first and empirically disproven - the debugger resolves symbols via the
        // EMBEDDED path regardless of where the file physically sits, confirmed live via Restart
        // Manager). Because the embedded path is just a bare filename, NewPdb below must be that
        // SAME bare name ("E29_Game.pdb", not generation-suffixed like NewDll) for the debugger's
        // own resolution (starting with the loaded module's own directory) to actually find it.
        const auto SrcPdb  = Dir.parent_path() / L"GamePdb" / Dir.filename() / (Stem + L".pdb");
        const auto NewPdb  = Dir / (Stem + L".pdb");

        std::filesystem::copy_file(Compiled, NewDll, std::filesystem::copy_options::overwrite_existing, Ec);
        if (Ec)
        {
            LogGamePlugin(std::format("Game.dll: failed to copy {} -> {}", Compiled.string(), NewDll.string()));
            return {};
        }

        // Best-effort - a missing/failed .pdb copy only degrades debugging, it's not a load failure.
        if (std::filesystem::exists(SrcPdb, Ec))
        {
            std::error_code PdbEc;
            std::filesystem::copy_file(SrcPdb, NewPdb, std::filesystem::copy_options::overwrite_existing, PdbEc);
        }

        return NewDll.wstring();
    }

    //---------------------------------------------------------------------------
    // Component-registry compatibility plan (Build/RELOAD_CRASH_REPORT.md's own follow-up design) -
    // "ping-pong" pre-flight: the shadow-copy step below was ALWAYS mandatory before any load, so
    // copying+loading a candidate DLL costs nothing extra done early, before touching the OLD
    // generation - it's a pure reorder, not added work. A game_plugin_candidate is an
    // ownership-holding intermediate: either it gets Commit'ed (ownership transfers to
    // game_plugin_state, exactly like the old single-shot LoadGamePluginComponents did) or Discard'ed
    // (FreeLibrary + delete the shadow copy, old generation never touched) - never both, never
    // neither.
    //---------------------------------------------------------------------------
    struct game_plugin_candidate
    {
        HMODULE      m_hModule = nullptr;
        std::wstring m_LoadedPath;
    };

    // Step 1: copy the already-known-good compiled DLL into a fresh generation-suffixed shadow file
    // and LoadLibrary it - no registry mutation at all, safe to call while an OLD generation is still
    // fully loaded and running. Returns an invalid candidate (m_hModule==nullptr) on any failure -
    // nothing was allocated, nothing to Discard.
    inline game_plugin_candidate PrepareGamePluginCandidate( const std::wstring& CompiledDllPath, std::uint32_t Generation ) noexcept
    {
        game_plugin_candidate Candidate;

        const std::wstring LoadedPath = CopyGamePluginForLoad(CompiledDllPath, Generation);
        if (LoadedPath.empty())
        {
            LogGamePlugin("Game.dll: nothing to load");
            return Candidate;
        }

        // Plain printf, deliberately NOT xeditor::NotifyError() - xeditor::NotifyError() also arms the modal error-
        // popup (RenderErrorPopup, checked once per frame from the main loop's own top-level
        // scope). This function's own first caller (E29_LevelScene_Editor.cpp's own startup code,
        // via LoadGamePluginComponents below) calls it BEFORE the main loop has rendered even one
        // frame - confirmed empirically (a deterministic, 100%-reproducible "Missing EndChild()"
        // ImGui assertion on frame 1) that arming the popup flag that early breaks ImGui's
        // window-stack bookkeeping. A missing/failed-to-load Game.dll is an expected, benign
        // condition anyway (nothing has been built yet on a fresh checkout) - a plain log line is
        // the right amount of ceremony for it, not a modal.
        HMODULE hModule = LoadLibraryW(LoadedPath.c_str());
        if (hModule == nullptr)
        {
            LogGamePlugin(std::format("Game.dll: LoadLibrary failed for {}", std::filesystem::path(LoadedPath).filename().string()));
            return Candidate;
        }

        Candidate.m_hModule    = hModule;
        Candidate.m_LoadedPath = LoadedPath;
        return Candidate;
    }

    // Discards a Prepare'd-but-never-Commit'ed candidate - the compatibility check (Step 2's own
    // consumer) found it incompatible, or the caller otherwise changed its mind. FreeLibrary + delete
    // the shadow copy, leaving nothing behind; the OLD generation (if any) is completely unaffected,
    // since nothing about it was ever touched. Safe to call on an already-invalid candidate (no-op).
    inline void DiscardGamePluginCandidate( game_plugin_candidate& Candidate ) noexcept
    {
        if (Candidate.m_hModule) FreeLibrary(Candidate.m_hModule);
        if (!Candidate.m_LoadedPath.empty())
        {
            std::error_code Ec;
            std::filesystem::remove(std::filesystem::path(Candidate.m_LoadedPath), Ec);
        }
        Candidate = {};
    }

    // Step 2: the candidate's own full component manifest, by stable guid - available immediately,
    // zero registry mutation, since E29_GetComponentDisplayInfo's data comes from a self-registration
    // list populated at LoadLibrary/static-init time, well before XecsPlugin_RegisterComponents is
    // ever called. Empty (not a failure) for an older-generation DLL built before this export existed
    // - every component then just can't be cross-checked, same "best-effort" posture as everywhere
    // else optional metadata is missing in this project.
    inline std::vector<xecs::scene::component_dependency> ProbeCandidateComponents( const game_plugin_candidate& Candidate ) noexcept
    {
        std::vector<xecs::scene::component_dependency> Result;
        if (!Candidate.m_hModule) return Result;

        auto* pGetInfo = reinterpret_cast<e29_game_registration::pfn_get_component_display_info>(GetProcAddress(Candidate.m_hModule, e29_game_registration::kGetComponentDisplayInfoName));
        if (!pGetInfo) return Result;

        pGetInfo([](void* pUserData, std::uint64_t Guid, const char* pName, const char*, int) noexcept
        {
            auto& Out = *reinterpret_cast<std::vector<xecs::scene::component_dependency>*>(pUserData);
            Out.push_back({ xecs::component::type::guid{Guid}, pName });
        }, &Result);

        return Result;
    }

    // Step 3: the actual registry-mutating half - calls the candidate's own
    // XecsPlugin_RegisterComponents (the FIRST of the two-call sequence xecs_plugin_api.h's own
    // comment requires: every RegisterComponents call, host's and the plugin's, must happen before
    // ANY RegisterSystems call) and transfers ownership of the loaded module to Plugin. Only ever
    // meaningful once the OLD generation (if any) has already been fully torn down/unregistered - see
    // ReloadGameModule's own call site for the ordering this depends on. On failure, Discards the
    // candidate itself (nothing left dangling) and returns false with Plugin untouched.
    inline bool CommitGamePluginCandidate( xecs::game_mgr::instance& GameMgr, game_plugin_state& Plugin, game_plugin_candidate& Candidate, std::uint32_t Generation ) noexcept
    {
        assert( Plugin.isLoaded() == false );
        if (!Candidate.m_hModule)
        {
            Plugin.m_LastStatus += " | nothing to load";
            return false;
        }

        auto* pRegisterComponents = reinterpret_cast<xecs_plugin_pfn_register_components*>(GetProcAddress(Candidate.m_hModule, XECS_PLUGIN_REGISTER_COMPONENTS_NAME));
        if (pRegisterComponents == nullptr)
        {
            Plugin.m_LastStatus = std::format("Game.dll: missing export {}", XECS_PLUGIN_REGISTER_COMPONENTS_NAME);
            LogGamePlugin(Plugin.m_LastStatus);
            DiscardGamePluginCandidate(Candidate);
            return false;
        }

        Plugin.m_hModule       = Candidate.m_hModule;
        Plugin.m_LoadedDllPath = Candidate.m_LoadedPath;
        Plugin.m_Token         = { .m_Slot = 1, .m_Generation = Generation };
        pRegisterComponents(GameMgr, Plugin.m_Token);

        Plugin.m_LastStatus = std::format("Game.dll: loaded generation {} ({})", Generation, std::filesystem::path(Candidate.m_LoadedPath).filename().string());
        Candidate = {}; // ownership transferred to Plugin - Discard must never also free what Plugin now owns
        return true;
    }

    // The ONE remaining synchronous, single-shot caller (startup, generation 1) - nothing is running
    // yet to be incompatible with, so no candidate/compatibility dance is needed there. Thin
    // Prepare+Commit wrapper, byte-for-byte the same external behavior the old single-function version
    // had.
    inline bool LoadGamePluginComponents( xecs::game_mgr::instance& GameMgr, game_plugin_state& Plugin, std::uint32_t Generation ) noexcept
    {
        auto Candidate = PrepareGamePluginCandidate(Plugin.m_CompiledDllPath, Generation);
        return CommitGamePluginCandidate(GameMgr, Plugin, Candidate, Generation);
    }

    // The SECOND call of the two-call sequence - only meaningful once every RegisterComponents
    // call (host's own, done by the caller, and the plugin's, done by
    // LoadGamePluginComponents above) has already happened. A no-op if no plugin is loaded.
    inline void RegisterGamePluginSystems( xecs::game_mgr::instance& GameMgr, game_plugin_state& Plugin ) noexcept
    {
        if (!Plugin.isLoaded()) return;

        if (auto* pRegisterSystems = reinterpret_cast<xecs_plugin_pfn_register_systems*>(GetProcAddress(Plugin.m_hModule, XECS_PLUGIN_REGISTER_SYSTEMS_NAME)))
            pRegisterSystems(GameMgr);

        LoadGameComponentDisplayInfo(Plugin);
    }

    //---------------------------------------------------------------------------
    // Detach/quiesce + unload - called with the OLD world already destroyed (see PollGameReload
    // below), so xecs::component::mgr::UnregisterPlugin's own full-reset is exactly the correct,
    // sufficient operation (see its own comment for why). A no-op if nothing is loaded.
    //---------------------------------------------------------------------------
    inline void UnloadGamePlugin( game_plugin_state& Plugin ) noexcept
    {
        if (!Plugin.isLoaded()) return;

        xscene::g_ComponentDisplayInfo.clear();

        if (auto* pUnregister = reinterpret_cast<xecs_plugin_pfn_unregister*>(GetProcAddress(Plugin.m_hModule, XECS_PLUGIN_UNREGISTER_NAME)))
            pUnregister(Plugin.m_Token);

        xecs::component::mgr::UnregisterPlugin(Plugin.m_Token);

        FreeLibrary(Plugin.m_hModule);
        Plugin.m_hModule = nullptr;
        Plugin.m_Token   = {};

        // The shadow copy (see game_plugin_state's own comment for why it exists) is safe to
        // delete now that nothing has it mapped - best-effort; a leftover file here would be
        // cosmetic, never a correctness problem. The .pdb is NOT
        // Plugin.m_LoadedDllPath-with-a-different-extension anymore - CopyGamePluginForLoad's own
        // comment explains why it's always the fixed bare name "E29_Game.pdb" (matching this
        // target's compiled PDB_NAME), never generation-suffixed like the .dll itself.
        if (!Plugin.m_LoadedDllPath.empty())
        {
            std::error_code Ec;
            const std::filesystem::path LoadedDll = Plugin.m_LoadedDllPath;
            std::filesystem::remove(LoadedDll, Ec);
            std::filesystem::remove(LoadedDll.parent_path() / L"E29_Game.pdb", Ec);
            Plugin.m_LoadedDllPath.clear();
        }
    }

} // namespace e29

#endif // E29_GAME_PLUGIN_LOAD_H
