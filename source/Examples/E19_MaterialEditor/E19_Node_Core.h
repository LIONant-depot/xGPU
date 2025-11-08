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
    inline bool IsPinCompatible(const xmaterial_graph::pin& outPin, const xmaterial_graph::pin& inPin, const xmaterial_graph::graph& g)
    {
        if(g.findNodeByPin(inPin.m_PinGUID) == g.findNodeByPin(outPin.m_PinGUID)) return false;

        auto* tOut	= g.findType(outPin.m_TypeGUID);
        auto* tIn	= g.findType(inPin.m_TypeGUID);
        if (!tOut || !tIn) return false;

        // strict type match; extend here to allow implicit casts
        return tOut->m_GUID == tIn->m_GUID;
    }

    //---check if a pin is connected---
    bool IsPinConnected(const xmaterial_graph::pin_guid& pinId, const xmaterial_graph::graph& g);
}

//UI related
namespace e19
{
    inline ImColor ToImColor(const xmaterial_graph::ColorRGBA& c)
    {
        return ImColor(c.r, c.g, c.b, c.a);
    }

    //color of the pin icon
    ImColor GetIconColor(const xmaterial_graph::type_guid& type, const xmaterial_graph::graph& g);

    // --- draw a pin circle (hollow/filled) ---
    void DrawPinCircle(const xmaterial_graph::type_guid& type, const xmaterial_graph::pin_guid& pinId, const xmaterial_graph::graph& g, ImVec2 size = ImVec2{ 10,10 });
}