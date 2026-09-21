#pragma once
#include "dependencies/xdelegate/source/xdelegate.h"

namespace e29
{
    // Reloading Game.dll is a host-wide transaction: every editor that owns a world takes part, through these events.
    // The process-wide component registry may only be reset while NO world exists, so each editor must snapshot and
    // destroy its own world before the module is swapped and recreate it afterwards.
    struct game_module_events
    {
        // Each editor appends the components its open scenes need. The reload is refused if the new module lacks any.
        xdelegate::thread_unsafe<std::vector<xecs::scene::component_dependency>&> m_OnCollectRequiredComponents;

        // Each editor snapshots whatever the reload would lose (open scenes, unsaved edits) and destroys its world.
        xdelegate::thread_unsafe<> m_OnBeforeReload;

        // The new module is loaded and its components registered: each editor recreates its world and restores from its snapshot.
        xdelegate::thread_unsafe<> m_OnAfterReload;
    };
}
