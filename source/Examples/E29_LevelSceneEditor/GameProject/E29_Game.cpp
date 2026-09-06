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
    XPROPERTY_REG(spin_component)

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
}

extern "C" __declspec(dllexport)
void XecsPlugin_RegisterComponents( xecs::game_mgr::instance& GameMgr, xecs::plugin::token Token ) noexcept
{
    e29_game::s_Generation = Token.m_Generation;
    GameMgr.RegisterComponents<e29_game::spin_component>(Token);
}

extern "C" __declspec(dllexport)
void XecsPlugin_RegisterSystems( xecs::game_mgr::instance& GameMgr ) noexcept
{
    GameMgr.RegisterSystems<e29_game::spin_system>();
}

extern "C" __declspec(dllexport)
void XecsPlugin_Unregister( xecs::plugin::token /*Token*/ ) noexcept
{
    // Nothing privately allocated outside the ECS world to release - see xecs_plugin_api.h's own
    // comment on why this is usually a no-op.
}
