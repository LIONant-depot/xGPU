#pragma once

// The shared `name` component every entity/prefab/tree label relies on.
// Split out of E29_LevelSceneEditorKit.h; included from there at the position this code used to occupy.
namespace e29
{
    //---------------------------------------------------------------------------
    // Shared starter component - every entity the Level tree/prefab machinery below labels, names,
    // or searches by relies on THIS component being present (see e.g. ResolveEntityReference,
    // CreatePrefabFromGroupRoot, DetermineGroupRoot's synthetic-root naming, the tree row label
    // logic) - promoted from "just E29's own demo content" into the kit itself for that reason. A
    // future editor built on this kit registers it exactly like any other component
    // (GameMgr.RegisterComponents<e29::name, ...>()).
    //---------------------------------------------------------------------------

    struct name
    {
        constexpr static auto typedef_v = xecs::component::type::data{ .m_pName = "Name" };

        std::string m_Value = "Entity";

        XPROPERTY_DEF
        ( "Name", name
        , obj_member<"Value", &name::m_Value>
        )
    };
    XPROPERTY_REG(name)

}
