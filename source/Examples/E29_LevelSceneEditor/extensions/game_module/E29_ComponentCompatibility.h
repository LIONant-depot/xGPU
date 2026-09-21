#ifndef E29_COMPONENT_COMPATIBILITY_H
#define E29_COMPONENT_COMPATIBILITY_H
#pragma once

// Phase 2 of the component-registry compatibility plan (Build/RELOAD_CRASH_REPORT.md's own follow-up
// design - see the "E29 component-registry compatibility" section appended to the shared plan doc).
// One shared check reused by four trigger points: live Game.dll hot-reload (a candidate DLL's own
// E29_GetComponentDisplayInfo manifest), normal scene Open (the already-loaded live registry),
// removing a Script-Module project reference (a trial-build candidate's manifest), and a source-control
// pull (routes through the same hot-reload gate as an ordinary edit - no separate check needed there).
// Deliberately just the pure diff here - no UI yet. The confirm-modal shape (A: strip missing
// components and continue / B: cancel) gets built alongside its first real call site (Phase 3) rather
// than designed in isolation, since guessing the interaction shape before a real caller exists risks
// getting it wrong and having to redo it.

namespace e29
{
    // Cross-references every entry in Required against whatever "is this type currently available"
    // predicate the caller supplies - IsAvailable is deliberately a predicate, not a fixed source, so
    // the SAME function serves both "check against the live registry" (xecs::component::mgr::
    // findComponentTypeInfo, already-loaded DLL) and "check against a not-yet-committed candidate
    // DLL's own manifest" (E29_GetComponentDisplayInfo output, pre-flight probe) without either caller
    // needing to know which. Returns only the entries NOT available, deduplicated by guid (a scene can
    // list the same component more than once if this is ever called with several scenes' manifests
    // merged together upstream).
    inline std::vector<xecs::scene::component_dependency> CheckComponentCompatibility
    ( const std::vector<xecs::scene::component_dependency>& Required
    , const std::function<bool(xecs::component::type::guid)>& IsAvailable
    ) noexcept
    {
        std::vector<xecs::scene::component_dependency> Missing;
        for (auto& Dep : Required)
        {
            if (IsAvailable(Dep.m_Guid)) continue;
            if (std::find_if(Missing.begin(), Missing.end(), [&](auto& M) noexcept { return M.m_Guid == Dep.m_Guid; }) != Missing.end())
                continue;
            Missing.push_back(Dep);
        }
        return Missing;
    }
}

#endif // E29_COMPONENT_COMPATIBILITY_H
