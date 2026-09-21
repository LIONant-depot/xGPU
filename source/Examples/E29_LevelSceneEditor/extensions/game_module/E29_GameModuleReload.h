#pragma once

namespace e29
{
    //---------------------------------------------------------------------------
    // Pending reload-compatibility confirmation (component-registry compatibility plan, Phase 3) -
    // set by ReloadGameModule's own pre-flight gate below when a candidate DLL doesn't cover every
    // component type the currently-open scenes' own ComponentDeps.txt manifests declare they need.
    // Rendered by RenderReloadCompatibilityModal (called once per frame from the main loop, same
    // shape as RenderGamePluginLogPanel) - Cancel just clears this (the old generation was never
    // touched, kept running exactly as it was); "Strip and Continue" removes the missing components
    // from every affected entity in the open scenes via the normal, undo-tracked RemoveComponent
    // command, then re-requests a reload - the retry finds nothing missing this time, since the
    // components are already gone before the next candidate probe even runs.
    //---------------------------------------------------------------------------
    struct pending_reload_compatibility
    {
        std::vector<xecs::scene::component_dependency> m_Missing;
    };
    inline std::optional<pending_reload_compatibility> g_PendingReloadCompatibility;

    // Reloads Game.dll for the whole host: refuse if the open scenes need components the new module lacks; have every
    // editor snapshot and destroy its world; swap the module and register its components; have every editor recreate its
    // world. Returns whether the new module loaded. Nothing is touched if it is refused.
    template< typename T_REGISTER_HOST_COMPONENTS_FN >
    bool ReloadGameModule( game_plugin_state& Plugin, T_REGISTER_HOST_COMPONENTS_FN&& RegisterHostComponents ) noexcept
    {
        // Ping-pong pre-flight compatibility gate (component-registry compatibility plan, Phase 3) -
        // only for an actual DLL swap; a non-swap reload can't introduce a NEW missing-component
        // scenario the open scenes weren't already tolerating. Prepares (copies+LoadLibrary's) the
        // candidate DLL FIRST, before touching anything else - the shadow-copy step was always
        // mandatory regardless of when it happens, so doing it this early costs nothing extra. If the
        // candidate is missing a component type any currently-open scene's own ComponentDeps.txt
        // declares it needs, the candidate is discarded (FreeLibrary'd) and this function returns
        // immediately - NOTHING else below has run yet, so the OLD generation (if any) is completely
        // untouched, still fully loaded and running. See RenderReloadCompatibilityModal (below) for
        // what happens next (a confirm choice the user makes on a later frame).
        //
        // NextGeneration is captured HERE, before UnloadGamePlugin (below) resets Plugin.m_Token -
        // incidental fix to what was previously always effectively "generation 1" every single reload
        // (the old single-step LoadGamePluginComponents call computed Plugin.m_Token.m_Generation + 1
        // AFTER Unload had already zeroed m_Token) - moving the candidate-prep earlier means this now
        // has to be captured earlier too, and capturing it against the REAL current generation is the
        // more obviously correct behavior, not a deliberate design change in its own right.
        const std::uint32_t NextGeneration = Plugin.m_Token.m_Generation + 1;
        game_plugin_candidate Candidate;
        Candidate = PrepareGamePluginCandidate(Plugin.m_Paths, NextGeneration);
        if (Candidate.m_hModule)
        {
            const auto CandidateManifest = ProbeCandidateComponents(Candidate);
            // An older-generation DLL missing the XScript_GetComponentDisplayInfo export can't be
            // checked at all - proceed exactly as before this feature existed, best-effort.
            if (!CandidateManifest.empty())
            {
                std::unordered_set<std::uint64_t> Available;
                for (auto& D : CandidateManifest) Available.insert(D.m_Guid.m_Value);

                // Scope the check to ONLY components the plugin itself is responsible for - a
                // scene's own manifest also lists host built-ins (Name, Transform, ...), which
                // XScript_GetComponentDisplayInfo's self-registration list never claims at all (they're
                // registered directly by RegisterHostComponents in the HOST binary, not by any
                // Game.dll generation) - checking THOSE against a candidate's manifest would always
                // show them "missing" regardless of whether anything actually changed. The OLD
                // generation (still fully loaded here - nothing has been touched yet) is asked the
                // SAME question a candidate is: which guids does IT self-register - that's the
                // authoritative "this guid is the plugin's responsibility, not the host's" set,
                // confirmed live (this exact gap was caught live: an unfiltered check flagged 7-8
                // "missing" components after removing just one).
                game_plugin_candidate OldGenerationView{ Plugin.m_hModule, {} };
                std::unordered_set<std::uint64_t> PluginOwned;
                for (auto& D : ProbeCandidateComponents(OldGenerationView)) PluginOwned.insert(D.m_Guid.m_Value);

                std::vector<xecs::scene::component_dependency> Needed, Required;
                Plugin.m_Events.m_OnCollectRequiredComponents.NotifyAll(Needed);
                for (auto& Dep : Needed)
                    if (PluginOwned.contains(Dep.m_Guid.m_Value)) Required.push_back(Dep);

                auto Missing = CheckComponentCompatibility(Required, [&](xecs::component::type::guid Guid) noexcept
                {
                    return Available.contains(Guid.m_Value);
                });

                if (!Missing.empty())
                {
                    LogGamePlugin(std::format("Game.dll: reload blocked - {} component type(s) referenced by open scenes are missing from the new build", Missing.size()));
                    DiscardGamePluginCandidate(Candidate);
                    g_PendingReloadCompatibility = pending_reload_compatibility{ std::move(Missing) };
                    return false;
                }
            }
        }

        Plugin.m_Events.m_OnBeforeReload.NotifyAll();

        // Every world is gone, so the shared component registry can be reset (see UnregisterPlugin's own comment).
        if (Plugin.isLoaded()) UnloadGamePlugin(Plugin);
        else                   xecs::component::mgr::resetRegistrations();

        // Components are registered through an instance of the game manager, though the registry they land in is the
        // process's own; a short-lived one does it, and every editor's fresh world then finds them already registered.
        bool bLoaded = false;
        {
            auto Registrar = std::make_unique<xecs::game_mgr::instance>();
            RegisterHostComponents(*Registrar);
            bLoaded = CommitGamePluginCandidate(*Registrar, Plugin, Candidate, NextGeneration);
        }

        Plugin.m_Events.m_OnAfterReload.NotifyAll();
        return bLoaded;
    }
}
