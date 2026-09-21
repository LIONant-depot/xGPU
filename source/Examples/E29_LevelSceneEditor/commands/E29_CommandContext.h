#ifndef E29_COMMAND_CONTEXT_H
#define E29_COMMAND_CONTEXT_H
#pragma once

// The Level editor's commands (levels, Play, the game module, ...) get the editor context through editor_command.
// Which pointer a command receives is decided where the command set is built (E29_CommandSet.h).
//
// Meant to be included after the contexts are defined, via the kit umbrella (E29_LevelSceneEditorKit.h).
#include "dependencies/xundo/source/xundo_system.h"
#include "dependencies/xeditor/include/xeditor/commands.h"
#include "dependencies/xeditor/include/xeditor/serialize.h"

namespace e29::commands
{
    // The same for the commands of the Level editor: World(), State() (with the Level fields), EditorContext().
    template<typename T_BASE>
    struct editor_command_mixin : T_BASE
    {
        using T_BASE::T_BASE;
        xecs::game_mgr::instance& World() noexcept { return this->template get<editor_context>().World(); }
        editor_state&             State() noexcept { return this->template get<editor_context>().State(); }
        editor_context&      EditorContext() noexcept { return this->template get<editor_context>(); }
    };
    using editor_command       = editor_command_mixin<xundo::command_base>;
    using editor_query_command = editor_command_mixin<xundo::query_command_base>;
}

#endif // E29_COMMAND_CONTEXT_H
