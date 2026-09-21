#pragma once

// What the scene editing code works on: which scenes are open, which entity is selected, and the world and undo it edits.
// Nothing here knows about levels or Play, so the scene code can be used by any editor that opens scenes.
namespace e29
{
    struct scene_state
    {
        // Every open scene stays resident until the editor removes it; opening one never closes another.
        std::vector<xecs::scene::guid> m_OpenScenes;

        xecs::scene::permanent_id  m_SelectedEntityId    = xecs::scene::invalid_permanent_id_v;
        xecs::component::entity    m_SelectedEntity      = {};    // the live handle: a cache, re-resolved from the id and scene after any world change
        xecs::scene::guid          m_SelectedEntityScene = {};    // which open scene the selection belongs to

        bool m_bEntityInspectorDirty = true;

        // Ctrl-click set, separate from the primary selection above (which alone drives the Properties panel); a plain click
        // clears it. It is scoped to ONE scene at a time, because a prefab's members must all come from the same live scene.
        std::unordered_set<xecs::scene::permanent_id>  m_MultiSelectedEntityIds;
        // The same members in click order (an unordered_set has none): the first one clicked donates its folder and parent to
        // the synthetic root of a group. Kept in lockstep with the set at every mutation.
        std::vector<xecs::scene::permanent_id>         m_MultiSelectOrder;
        xecs::scene::guid                              m_MultiSelectScene;

        // Entity Properties: the category filter (empty = every component) and the Add Component popup's search text and
        // per-category open state (key = category, empty = "Uncategorized").
        std::string                            m_ComponentCategoryFilter;
        std::string                            m_ComponentSelectorSearchString;
        std::unordered_map<std::string, bool>  m_ComponentSelectorCategoryOpen;
    };

    // One editor's working set for scenes: its state, the owner of its world and the undo of its document. Commands reach it
    // through scene_command (World(), State(), SceneContext()); panels and helpers take it explicitly. The world is
    // destroyed and recreated on every Game.dll reload, so the context holds the unique_ptr that owns it and reads through
    // it each time (World()), never a pointer to the world itself.
    struct scene_context
    {
        scene_state&                               m_State;
        std::unique_ptr<xecs::game_mgr::instance>& m_pWorld;
        xundo::system&                             m_Undo;      // every edit of this editor's document goes through it

        xecs::game_mgr::instance& World() noexcept { return *m_pWorld; }
    };

    // The active editor's scene context, provided to the host at startup. For code that has no session of its own, such as
    // a static drag-drop handler.
    inline scene_context* FindSceneContext() noexcept
    {
        auto* pHost = xeditor::host::current();
        return pHost ? pHost->find<scene_context>() : nullptr;
    }
}
