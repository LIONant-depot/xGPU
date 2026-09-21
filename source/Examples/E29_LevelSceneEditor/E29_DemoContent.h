#pragma once

namespace e29

{

    struct transform

    {

        constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "Transform" };



        // Position/Rotation/Scale via xmath::fvec3 - same shape Unity/Unreal/Godot's own Transform

        // uses, and the same convention this codebase's own descriptors already follow (see

        // xskeleton_desc::transform in xskeleton_descriptor.h: Scale/Rotation/Translation as

        // xmath::fvec3, Rotation stored in radians as Euler angles).

        xmath::fvec3 m_Position = xmath::fvec3::fromZero();

        xmath::fvec3 m_Rotation = xmath::fvec3::fromZero(); // radians

        xmath::fvec3 m_Scale    = xmath::fvec3::fromOne();



        XPROPERTY_DEF

        ( "Transform", transform

        , obj_member<"Position", &transform::m_Position>

        , obj_member<"Rotation", &transform::m_Rotation>

        , obj_member<"Scale",    &transform::m_Scale>

        )

    };

    XPROPERTY_REG(transform)



    // Two trivial demo Update systems - E29 otherwise registers ZERO systems (RegisterSystems<>() is

    // called empty, purely to lock component bit IDs), so the new System Registry panel/Play-Stop

    // toggle would have nothing real to list/reorder/enable/observe without these. Each just

    // printf's once per tick (flushed unconditionally, per this project's persistent-diagnostic-

    // logging preference) since E29 has no viewport to observe a "real" effect through - reordering

    // them in the System Registry panel changes which line prints first; disabling one stops its own

    // line, which is the whole verification surface for that feature.

    //

    // typedef_v deliberately leaves m_Guid at its default (xecs::system::type::details::CreateInfo's

    // own type::guid{__FUNCSIG__} fallback) rather than hand-typing an explicit guid string per

    // system - that automatic, zero-boilerplate identity is worth keeping for the common case. It IS

    // technically less durable than an explicit guid (SystemOrder.config.txt persists it, and a

    // rename or compiler-formatting change could shift it) - but the actual blast radius is small:

    // Load() already treats an unmatched saved guid as "not registered any more" and just skips it

    // (see its own comment), so the worst case is a silently-reset reorder/enable preference, never a

    // crash or corrupted state. Worth switching to an explicit guid on a case-by-case basis for

    // anything where that reset would actually matter (a real gameplay system whose saved order

    // matters for correctness, not a demo).

    struct tick_logger_a : xecs::system::instance

    {

        constexpr static auto typedef_v = xecs::system::type::update{ .m_pName = "Tick Logger A" };



        tick_logger_a(xecs::game_mgr::instance& GameMgr) noexcept : xecs::system::instance(GameMgr) {}



        void OnUpdate(void) noexcept

        {

            std::printf("[System] Tick Logger A\n");

            std::fflush(stdout);

        }

    };



    struct tick_logger_b : xecs::system::instance

    {

        constexpr static auto typedef_v = xecs::system::type::update{ .m_pName = "Tick Logger B" };



        tick_logger_b(xecs::game_mgr::instance& GameMgr) noexcept : xecs::system::instance(GameMgr) {}



        void OnUpdate(void) noexcept

        {

            std::printf("[System] Tick Logger B\n");

            std::fflush(stdout);

        }

    };

}
