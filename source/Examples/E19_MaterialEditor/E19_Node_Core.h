#pragma once

#include "source/xGPU.h"
#include "plugins/xmaterial.plugin/source/graph/xmaterial_graph.h"
#include "source/tools/xgpu_imgui_breach.h"
#include "dependencies/xresource_mgr/source/xresource_mgr.h"
#include "dependencies/imgui-node-editor/imgui_node_editor.h"
#include "dependencies/imgui/imgui_internal.h"

namespace e19 
{
    // -------- Helpers Functions--------
    inline bool IsPinCompatible(const xmaterial_compiler::pin& outPin, const xmaterial_compiler::pin& inPin, const xmaterial_compiler::graph& g)
    {
        if(g.FindNodeByPin(inPin.m_PinGUID) == g.FindNodeByPin(outPin.m_PinGUID)) return false;

        auto* tOut	= g.GetType(outPin.m_TypeGUID);
        auto* tIn	= g.GetType(inPin.m_TypeGUID);
        if (!tOut || !tIn) return false;

        // strict type match; extend here to allow implicit casts
        return tOut->m_GUID == tIn->m_GUID;
    }

    //---check if a pin is connected---
    bool IsPinConnected(const xmaterial_compiler::pin_guid& pinId, const xmaterial_compiler::graph& g);
}

//UI related
namespace e19
{
    inline ImColor ToImColor(const xmaterial_compiler::ColorRGBA& c)
    {
        return ImColor(c.r, c.g, c.b, c.a);
    }

    //color of the pin icon
    ImColor GetIconColor(const xmaterial_compiler::type_guid& type, const xmaterial_compiler::graph& g);

    // --- draw a pin circle (hollow/filled) ---
    void DrawPinCircle(const xmaterial_compiler::type_guid& type, const xmaterial_compiler::pin_guid& pinId, const xmaterial_compiler::graph& g, ImVec2 size = ImVec2{ 10,10 });
}