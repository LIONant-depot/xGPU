#ifndef XGPU_IMGUI_BREACH_H
#define XGPU_IMGUI_BREACH_H
#pragma once
#include "imgui.h"

namespace xgpu::tools::imgui
{
    // Templated function to wrap a lambda for ImGui callback
    // BE CAREFULL ABOUT HOW YOU CAPTURE VARIABLES!!!
    // If you do not know how this works make sure you do not capture by reference
    template<typename T_CALLBACK>
    void AddCustomRenderCallback(T_CALLBACK&& callback)
    {
        // Note: Lambda should use [=] or [] to avoid dangling reference captures
        struct CallbackWrapper
        {
            static void Render(const ImDrawList* parent_list, const ImDrawCmd* pCmd)
            {
                auto& Wrapper = *static_cast<CallbackWrapper*>(pCmd->UserCallbackData);
                (Wrapper.m_Callback)(Wrapper.m_Pos, Wrapper.m_Size); //pCmd->ClipRect
                delete &Wrapper;
            }

            T_CALLBACK  m_Callback;
            ImVec2      m_Pos;
            ImVec2      m_Size;
        };

        auto p = new CallbackWrapper( std::forward<T_CALLBACK>(callback)
                                    , ImGui::GetWindowPos()
                                    , ImGui::GetWindowSize()
                                    );

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddCallback(CallbackWrapper::Render, p);
        draw_list->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }


    xgpu::device::error*    CreateContext   ( void ) noexcept;
    xgpu::device::error*    CreateInstance  ( xgpu::window& MainWindow ) noexcept;
    bool                    BeginRendering  ( bool bEnableDocking = false ) noexcept;
    bool                    isInputSleeping ( void ) noexcept;
    void                    Render          ( void ) noexcept;
    void                    Shutdown        ( void ) noexcept;
    ImFont*&                getFont         ( int Index=0) noexcept;
}
#endif // XGPU_IMGUI_BREACH_H