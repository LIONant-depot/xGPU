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

namespace e29
{
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
    // Copies the (already known-good - see StartGameReload/PollGameReload, which call
    // BuildGamePluginIfStale BEFORE this) compiled DLL into a new generation-suffixed shadow file
    // (CopyGamePluginForLoad), loads THAT, and calls its XecsPlugin_RegisterComponents - the FIRST
    // of the two-call sequence xecs_plugin_api.h's own comment requires (every RegisterComponents
    // call, host's and the plugin's, must happen before ANY RegisterSystems call). Returns false
    // (Plugin left untouched, exactly as before the call) on any failure - missing file, missing
    // export, nothing else to do differently: the caller keeps running without a game loaded, same
    // as if this was never called.
    //---------------------------------------------------------------------------
    inline bool LoadGamePluginComponents( xecs::game_mgr::instance& GameMgr, game_plugin_state& Plugin, std::uint32_t Generation ) noexcept
    {
        assert( Plugin.isLoaded() == false );

        const std::wstring LoadedPath = CopyGamePluginForLoad(Plugin.m_CompiledDllPath, Generation);
        if (LoadedPath.empty())
        {
            Plugin.m_LastStatus += " | nothing to load";
            LogGamePlugin("Game.dll: nothing to load");
            return false;
        }

        // Plain printf, deliberately NOT e29::Debugger() - Debugger() also arms the modal error-
        // popup (RenderErrorPopup, checked once per frame from the main loop's own top-level
        // scope). This function's very first caller (E29_LevelScene_Editor.cpp's own startup code)
        // calls it BEFORE the main loop has rendered even one frame - confirmed empirically (a
        // deterministic, 100%-reproducible "Missing EndChild()" ImGui assertion on frame 1,
        // isolated by bisecting against the pre-Phase-8 file and adding per-call-site printf
        // checkpoints) that arming the popup flag that early breaks ImGui's window-stack
        // bookkeeping - every OTHER Debugger() call site in this codebase fires from inside an
        // already-running frame, which this startup call path is not. A missing/failed-to-load
        // Game.dll is an expected, benign condition anyway (nothing has been built yet on a fresh
        // checkout) - a plain log line is the right amount of ceremony for it, not a modal.
        HMODULE hModule = LoadLibraryW(LoadedPath.c_str());
        if (hModule == nullptr)
        {
            Plugin.m_LastStatus = std::format("Game.dll: LoadLibrary failed for {}", std::filesystem::path(LoadedPath).filename().string());
            LogGamePlugin(Plugin.m_LastStatus);
            return false;
        }

        auto* pRegisterComponents = reinterpret_cast<xecs_plugin_pfn_register_components*>(GetProcAddress(hModule, XECS_PLUGIN_REGISTER_COMPONENTS_NAME));
        if (pRegisterComponents == nullptr)
        {
            Plugin.m_LastStatus = std::format("Game.dll: missing export {}", XECS_PLUGIN_REGISTER_COMPONENTS_NAME);
            LogGamePlugin(Plugin.m_LastStatus);
            FreeLibrary(hModule);
            return false;
        }

        Plugin.m_hModule       = hModule;
        Plugin.m_LoadedDllPath = LoadedPath;
        Plugin.m_Token         = { .m_Slot = 1, .m_Generation = Generation };
        pRegisterComponents(GameMgr, Plugin.m_Token);

        Plugin.m_LastStatus = std::format("Game.dll: loaded generation {} ({})", Generation, std::filesystem::path(LoadedPath).filename().string());
        return true;
    }

    // The SECOND call of the two-call sequence - only meaningful once every RegisterComponents
    // call (host's own, done by the caller, and the plugin's, done by
    // LoadGamePluginComponents above) has already happened. A no-op if no plugin is loaded.
    inline void RegisterGamePluginSystems( xecs::game_mgr::instance& GameMgr, game_plugin_state& Plugin ) noexcept
    {
        if (!Plugin.isLoaded()) return;

        if (auto* pRegisterSystems = reinterpret_cast<xecs_plugin_pfn_register_systems*>(GetProcAddress(Plugin.m_hModule, XECS_PLUGIN_REGISTER_SYSTEMS_NAME)))
            pRegisterSystems(GameMgr);
    }

    //---------------------------------------------------------------------------
    // Detach/quiesce + unload - called with the OLD world already destroyed (see PollGameReload
    // below), so xecs::component::mgr::UnregisterPlugin's own full-reset is exactly the correct,
    // sufficient operation (see its own comment for why). A no-op if nothing is loaded.
    //---------------------------------------------------------------------------
    inline void UnloadGamePlugin( game_plugin_state& Plugin ) noexcept
    {
        if (!Plugin.isLoaded()) return;

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
