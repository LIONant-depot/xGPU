#ifndef XGPU_IMGUI_BREACH_H
#define XGPU_IMGUI_BREACH_H
#pragma once
#include "imgui.h"

namespace xgpu::tools::imgui
{
    namespace details
    {
        struct call_back_public_access
        {
            cmd_buffer* m_pCmdBuffer{ nullptr };
        };
    }

    // Templated function to wrap a lambda for ImGui callback
    // BE CAREFULL ABOUT HOW YOU CAPTURE VARIABLES!!!
    // If you do not know how this works make sure you do not capture by reference
    template<typename T_CALLBACK>
    void AddCustomRenderCallback(T_CALLBACK&& callback)
    {
        // Note: Lambda should use [=] or [] to avoid dangling reference captures
        struct CallbackWrapper : details::call_back_public_access
        {
            inline static void Render(const ImDrawList* parent_list, const ImDrawCmd* pCmd)
            {
                auto& Wrapper = *static_cast<CallbackWrapper*>(pCmd->UserCallbackData);
                (Wrapper.m_Callback)(*Wrapper.m_pCmdBuffer, Wrapper.m_Pos, Wrapper.m_Size);
                delete &Wrapper;
            }

            T_CALLBACK  m_Callback;
            ImVec2      m_Pos;
            ImVec2      m_Size;
        };

         CallbackWrapper* pWrapper = new CallbackWrapper{ {}, std::forward<T_CALLBACK>(callback), ImGui::GetWindowPos(), ImGui::GetWindowSize() };
 
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddCallback(CallbackWrapper::Render, pWrapper);
        draw_list->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }


    xgpu::device::error*    CreateContext   ( void ) noexcept;
    xgpu::device::error*    CreateInstance  ( xgpu::window& MainWindow ) noexcept;
    bool                    BeginRendering  ( bool bEnableDocking = false ) noexcept;
    bool                    isInputSleeping ( void ) noexcept;
    void                    Render          ( void ) noexcept;
    void                    Shutdown        ( void ) noexcept;
    ImFont*&                getFont         ( int Index=0) noexcept;

    // Returns the "bold/emphasized" companion of whatever font is CURRENTLY io.FontDefault, instead of
    // a hardcoded index - shared tree/breadcrumb rendering code (e.g. virtual_tree_tab.h's "selected
    // folder"/"last path segment" emphasis) uses this so an example that overrides io.FontDefault away
    // from the original Fonts[0] Consolas (E29's own Segoe UI theme, see E29_Theme.h) still gets a
    // matching-family bold variant instead of a jarring font-family mismatch. Every example that never
    // touches io.FontDefault gets Fonts[1] exactly as before - zero behavior change for them.
    ImFont*                 getEmphasisFont ( void ) noexcept;
    ImFont*                 getLargeEmphasisFont ( void ) noexcept;  // getEmphasisFont's "genuinely larger" companion (Fonts[3]/[6]) - see xgpu_imgui_breach.cpp

    // True exactly once on the frame the app window transitions from unfocused to focused (e.g. the
    // user alt-tabbed back in after editing code in an external IDE - the same edge Unity uses to
    // decide "check if scripts need recompiling"). Edge-triggered and self-consuming: calling this
    // returns the pending event (if any) and clears it, so at most one caller per gained-focus event
    // ever sees `true` - matches BeginRendering's own StartNewFrame, which already computes this
    // transition every frame for ImGui's own io.AddFocusEvent and previously discarded it afterward.
    bool                    ConsumeWindowFocusGained( void ) noexcept;


    void                    ClearTexture(xgpu::texture& Texture);
}
#endif // XGPU_IMGUI_BREACH_H