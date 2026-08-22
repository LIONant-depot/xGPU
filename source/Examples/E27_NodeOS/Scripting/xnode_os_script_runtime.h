#pragma once
//-----------------------------------------------------------------------------------
// Runtime support for generated script code (NODE_SCRIPTING_DESIGN.md, section 4). Every
// generated .cpp includes this. script_exit is what an End node throws to unwind the whole
// script in one shot, regardless of how many spine-calls deep execution currently is - see
// section 9.3 for why this replaced a spine-local `return;`.
//-----------------------------------------------------------------------------------

namespace nodeos::scripting
{
    struct script_exit
    {
        int m_Code = 0;
    };
}
