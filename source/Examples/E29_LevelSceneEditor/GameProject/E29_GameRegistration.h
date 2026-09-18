#ifndef E29_GAME_REGISTRATION_H
#define E29_GAME_REGISTRATION_H
#pragma once

// Self-registering component/system list for E29_Game.dll's own Script-Module pipeline - lets each
// module announce its own components/systems just by being LINKED INTO the DLL, without
// E29_Game.cpp needing to know about it at compile time (no per-module #include/RegisterComponents
// call to hand-edit as modules are added/removed). Direct user design: an intrusive self-
// registering linked list (each entry's constructor runs at static-init time, before main - and
// crucially, before the host ever calls XecsPlugin_RegisterComponents/RegisterSystems, since those
// only run AFTER this DLL has finished loading - so by the time the list is walked, every entry
// across every module's own translation unit has already linked itself in). Safe here specifically
// because every module compiles into this SAME DLL as the reader - no cross-DLL boundary crossing
// (unlike xproperty's own reflection registry, which genuinely can't cross a DLL boundary - see
// xproperty_no_cross_dll_real_objects).
//
// Component/system DISPLAY ORDER in the Entity Properties panel (E29_Panel_EntityProperties.h) is a
// SEPARATE concern from registration order (registration order is unspecified across translation
// units and doesn't matter functionally). There is no existing ordering mechanism anywhere in this
// engine to reuse - xecs::component::type::data has no priority/category field, and the panel's own
// "Transform/Name always show first" is a pure accident of built-in engine components always
// registering before any game/module component, not a deliberate mechanism. Rather than add a field
// to the shared xECSV2 engine type (a bigger, cross-example change), category+priority live here,
// E29-side only, captured on each component's own registration record. Uncategorized components
// (i.e. every built-in engine component, which never goes through E29_REGISTER_COMPONENT at all)
// sort before every categorized one, by construction - so Transform/Name stay pinned at the top
// exactly as they are today, without needing to touch their own definitions.
#include "dependencies/xECSV2/src/xecs.h"

namespace e29_game_registration
{
    struct component_entry
    {
        void (*m_pRegisterFn)(xecs::game_mgr::instance&, xecs::plugin::token) = nullptr;
        const char*   m_pName     = "";
        const char*   m_pCategory = "";
        int           m_Priority  = 0;
        // The type's own compile-time guid (xecs::component::type::info_v<TYPE>.m_Guid.m_Value) -
        // deterministic, identical across every binary the type is compiled into (see
        // xecs_component_type_inline.h's own guid_v - name-hash-derived unless a type sets one
        // explicitly), available immediately at DLL LoadLibrary time via this self-registration list,
        // well before XecsPlugin_RegisterComponents ever runs. This is what lets a candidate DLL's own
        // manifest (E29_GetComponentDisplayInfo below) be compared against a scene's ComponentDeps.txt
        // by stable identity instead of by display name.
        std::uint64_t m_Guid      = 0;
    };

    struct system_entry
    {
        void (*m_pRegisterFn)(xecs::game_mgr::instance&) = nullptr;
    };

    template<typename T>
    struct self_registration
    {
        inline static self_registration<T>* s_pHead = nullptr;

        self_registration<T>* m_pNext;
        T                     m_Value;

        explicit self_registration(T Value) noexcept : m_pNext(s_pHead), m_Value(Value) { s_pHead = this; }
    };

    // Finds a component's own registration entry by name (the same TYPE::typedef_v.m_pName the
    // engine's own xecs::component::type::info::m_pName carries) - a plain linear scan, never a hot
    // path (once per Entity Properties rebuild, over a handful of registered component types, not
    // once per frame).
    inline const component_entry* FindComponentEntry(const char* pName) noexcept
    {
        for (auto* p = self_registration<component_entry>::s_pHead; p; p = p->m_pNext)
            if (std::strcmp(p->m_Value.m_pName, pName) == 0) return &p->m_Value;
        return nullptr;
    }

    // Export contract for handing category/priority across the DLL boundary to the editor - an
    // E29-ONLY, additive export, NOT part of xecs_plugin_api.h's own core plugin contract (which
    // lives in the shared xECSV2 engine repo). GetProcAddress needs only a name string and a
    // function-pointer type to cast to; neither requires registering anywhere shared, so this stays
    // entirely inside E29 - deliberate, after weighing (and rejecting) adding a field to the shared
    // xecs::component::type::data instead. Callback-with-userdata shape (not a returned container)
    // so the exported function itself stays a trivial, ABI-stable extern "C" signature - no
    // std::vector/std::function crossing the DLL boundary.
    inline constexpr const char* kGetComponentDisplayInfoName = "E29_GetComponentDisplayInfo";
    using pfn_component_display_visitor  = void(__cdecl*)(void* pUserData, std::uint64_t Guid, const char* pName, const char* pCategory, int Priority);
    using pfn_get_component_display_info = void(__cdecl*)(pfn_component_display_visitor pVisitor, void* pUserData);
}

namespace e29
{
    // Editor-side copy of each currently-loaded component's category/priority - repopulated by
    // LoadGameComponentDisplayInfo (E29_GamePluginLoad.h) every time a new generation loads, read by
    // the Entity Properties panel for filtering/ordering. Declared here (not in E29_GamePluginLoad.h
    // itself) since this header has no heavy include-order prerequisites - safe for the panel file to
    // include directly without pulling in game_plugin_state/LogGamePlugin's own ordering constraints.
    struct component_display_info { std::string m_Category; int m_Priority = 0; };
    inline std::unordered_map<std::string, component_display_info> g_ComponentDisplayInfo;
}

// A module writes ONE of these per component/system struct, right where XPROPERTY_REG (for
// components) would otherwise go - it now does both. CATEGORY/PRIORITY are required, not defaulted
// (C preprocessor macros can't cleanly default arguments, and Unreal's own UPROPERTY(Category=...)
// convention this mirrors doesn't default it either) - group name (e.g. "Rendering") + a plain int
// sorting ascending within that group.
#define E29_REGISTER_COMPONENT(TYPE, CATEGORY, PRIORITY) \
    XPROPERTY_REG(TYPE) \
    inline e29_game_registration::self_registration<e29_game_registration::component_entry> g_AutoReg_##TYPE \
    { e29_game_registration::component_entry \
        { [](xecs::game_mgr::instance& GameMgr, xecs::plugin::token Token) noexcept { GameMgr.RegisterComponents<TYPE>(Token); } \
        , TYPE::typedef_v.m_pName, CATEGORY, PRIORITY \
        , xecs::component::type::info_v<TYPE>.m_Guid.m_Value \
        } \
    };

// Systems in this codebase are never XPROPERTY_REG'd (no existing system - spin_system/spin2_system -
// does this either), so this only ever does the self-registration half.
#define E29_REGISTER_SYSTEM(TYPE) \
    inline e29_game_registration::self_registration<e29_game_registration::system_entry> g_AutoReg_##TYPE \
    { e29_game_registration::system_entry \
        { [](xecs::game_mgr::instance& GameMgr) noexcept { GameMgr.RegisterSystems<TYPE>(); } } \
    };

#endif // E29_GAME_REGISTRATION_H
