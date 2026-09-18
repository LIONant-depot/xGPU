// E29's sample "Game.dll" - Phase 8 of the xECSV2 type-registration architecture plan.
//
// Deliberately self-contained and trivial: one component, one system, both printf-verified (same
// persistent-diagnostic-logging convention as E29_LevelScene_Editor.cpp's own tick_logger_a/b) -
// this file exists to prove the hot-reload plumbing works end to end (register, tick, unregister,
// reload), not to be a real game. A real game would replace this file's content; the exported
// entry points at the bottom are the only part the host actually depends on.
//
// Built by this project's own CMakeLists.txt as add_library(E29_Game SHARED ...), gated behind
// XECS_BUILD_SHARED_LIBRARY (a Game.dll only makes sense when xECSV2 itself is a shared library -
// see xecs_plugin_api.h's own top comment for why).
#include "dependencies/xECSV2/src/xecs.h"
#include "dependencies/xECSV2/src/xecs_plugin_api.h"

// Self-registration infrastructure (E29_GameRegistration.h's own top comment has the full design) -
// every Script-Module's own component/system announces itself via E29_REGISTER_COMPONENT/
// E29_REGISTER_SYSTEM just by being compiled into this DLL. This file's own exported entry points
// below are now a fixed loop, never edited per module again - the earlier version of this file
// hand-wired TestScript's own Glow component/system here as a deliberate, temporary proof; that
// bridge is gone now that the real mechanism exists.
#include "E29_GameRegistration.h"

namespace e29_game
{
    // A generation counter, stamped into every spin_component at registration time (see
    // RegisterComponents below) purely so the tick log can show it changed after a reload - the
    // whole point of this sample is to make hot-reload OBSERVABLE, not just structurally correct.
    static std::uint32_t s_Generation = 0;

    struct spin_component
    {
        constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "Spin" };

        float m_DegreesPerTick = 1.0f;
        XPROPERTY_DEF
        ("Spin", spin_component
        , obj_member<"DegreesPerTick", &spin_component::m_DegreesPerTick>
        )
    };
    E29_REGISTER_COMPONENT(spin_component, "Gameplay", 0)

    struct spin_system : xecs::system::instance
    {
        constexpr static auto typedef_v = xecs::system::type::update{ .m_pName = "Game: Spin System" };

        spin_system(xecs::game_mgr::instance& GameMgr) noexcept : xecs::system::instance(GameMgr) {}

        // No component parameter - matches E29_LevelScene_Editor.cpp's own tick_logger_a/b
        // convention exactly (a pure "tick once globally" Update system, not a per-entity query).
        // This sample exists to prove the hot-reload/registration plumbing, not to demonstrate
        // xECS's per-entity query authoring model.
        void OnUpdate(void) noexcept
        {
            std::printf("[Game.dll gen=%u] Spin tick (testing async build + disabled-button reload)\n", s_Generation);
            std::fflush(stdout);
        }
    };
    E29_REGISTER_SYSTEM(spin_system)

    struct spin2_system : xecs::system::instance
    {
        constexpr static auto typedef_v = xecs::system::type::update{ .m_pName = "Game: Spin2 System" };

        spin2_system(xecs::game_mgr::instance& GameMgr) noexcept : xecs::system::instance(GameMgr) {}

        // No component parameter - matches E29_LevelScene_Editor.cpp's own tick_logger_a/b
        // convention exactly (a pure "tick once globally" Update system, not a per-entity query).
        // This sample exists to prove the hot-reload/registration plumbing, not to demonstrate
        // xECS's per-entity query authoring model.
        void OnUpdate(void) noexcept
        {
            std::printf("[Game.dll gen=%u] Spin 2 tick (testing async build + disabled-button reload)\n", s_Generation);
            std::fflush(stdout);
        }
    };
    E29_REGISTER_SYSTEM(spin2_system)

}

extern "C" __declspec(dllexport)
void XecsPlugin_RegisterComponents( xecs::game_mgr::instance& GameMgr, xecs::plugin::token Token ) noexcept
{
    e29_game::s_Generation = Token.m_Generation;
    for (auto* p = e29_game_registration::self_registration<e29_game_registration::component_entry>::s_pHead; p; p = p->m_pNext)
        p->m_Value.m_pRegisterFn(GameMgr, Token);
}

extern "C" __declspec(dllexport)
void XecsPlugin_RegisterSystems( xecs::game_mgr::instance& GameMgr ) noexcept
{
    for (auto* p = e29_game_registration::self_registration<e29_game_registration::system_entry>::s_pHead; p; p = p->m_pNext)
        p->m_Value.m_pRegisterFn(GameMgr);
}

extern "C" __declspec(dllexport)
void XecsPlugin_Unregister( xecs::plugin::token /*Token*/ ) noexcept
{
    // Nothing privately allocated outside the ECS world to release - see xecs_plugin_api.h's own
    // comment on why this is usually a no-op.
}

// E29-only export (E29_GameRegistration.h's own top comment has the full reasoning) - hands every
// registered component's own category/priority to the editor so it can filter/order the Entity
// Properties panel's component list. Optional: an older-generation DLL or a hypothetical non-E29
// consumer of xecs_plugin_api.h simply won't have this export, and the editor's own GetProcAddress
// call already treats that as "no display info available" rather than a load failure.
extern "C" __declspec(dllexport)
void E29_GetComponentDisplayInfo( e29_game_registration::pfn_component_display_visitor pVisitor, void* pUserData ) noexcept
{
    for (auto* p = e29_game_registration::self_registration<e29_game_registration::component_entry>::s_pHead; p; p = p->m_pNext)
        pVisitor(pUserData, p->m_Value.m_Guid, p->m_Value.m_pName, p->m_Value.m_pCategory, p->m_Value.m_Priority);
}
