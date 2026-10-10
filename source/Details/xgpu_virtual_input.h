#pragma once

// Virtual input: a mouse and a keyboard that are not the machine's. While it is active every xgpu::mouse and xgpu::keyboard answers from the state here and the operating system's
// mouse and keys are ignored, so whatever sits above xGPU (ImGui, the editor, a game) believes someone is pressing keys and moving a mouse, while the person at the computer keeps their
// own mouse for other things. Used by the editor's input commands (VirtualInput, MouseMove, MouseButton, Key, Text): tests and tools drive the editor with it, without fighting the
// user for the one real mouse.
//
// One state for the whole process (there is one virtual person). The consumer of the input (the ImGui backend) calls NextFrame() once per frame after reading it: what was pressed this
// frame becomes "was pressed" for the next, and the one-frame values (wheel, the latest character) are cleared.
#include <array>
#include <cstdint>
#include <string>

namespace xgpu::virtual_input
{
    struct state
    {
        bool                                m_bActive       = false;
        std::array<bool,  8>                m_ButtonDown    {};       // xgpu::mouse::digital
        std::array<bool,  8>                m_ButtonWasDown {};
        std::array<float, 2>                m_Position      {};       // xgpu::mouse::analog::POS_ABS: in client pixels of the main window
        std::array<float, 2>                m_Relative      {};       // POS_REL: the move since the last frame
        float                               m_Wheel         = 0.0f;   // WHEEL_REL: this frame's turn of the wheel
        std::array<bool,  256>              m_KeyDown       {};       // xgpu::keyboard::digital
        std::array<bool,  256>              m_KeyWasDown    {};
        int                                 m_LatestChar    = 0;      // the character of this frame (0: none)
        std::string                         m_Typing;                 // text still to be typed, one character per frame
        std::uint64_t                       m_Frames        = 0;      // frames the consumer has read: a client that sends an input and wants to see its effect waits for this to move
    };

    inline state& State() noexcept { static state s_State; return s_State; }

    inline bool Active() noexcept { return State().m_bActive; }

    inline void SetActive(bool bOn) noexcept
    {
        auto& S = State();
        if (S.m_bActive == bOn) return;
        S = state{};                                                   // a clean slate on both edges: nothing stays pressed
        S.m_bActive = bOn;
    }

    inline void MoveTo(float X, float Y) noexcept
    {
        auto& S = State();
        S.m_Relative = { S.m_Relative[0] + X - S.m_Position[0], S.m_Relative[1] + Y - S.m_Position[1] };
        S.m_Position = { X, Y };
    }

    inline void SetButton(int Button, bool bDown) noexcept { auto& S = State(); if (Button >= 0 && Button < static_cast<int>(S.m_ButtonDown.size())) S.m_ButtonDown[Button] = bDown; }
    inline void AddWheel(float Delta) noexcept             { State().m_Wheel += Delta; }
    inline void SetKey(int Key, bool bDown) noexcept       { auto& S = State(); if (Key > 0 && Key < static_cast<int>(S.m_KeyDown.size())) S.m_KeyDown[Key] = bDown; }
    inline void SetChar(int Character) noexcept            { State().m_LatestChar = Character; }
    inline void Type(const std::string& Text)  noexcept    { auto& S = State(); S.m_Typing += Text; if (S.m_LatestChar == 0 && !S.m_Typing.empty()) { S.m_LatestChar = static_cast<unsigned char>(S.m_Typing.front()); S.m_Typing.erase(0, 1); } }

    // Called by the consumer once per frame, after it read the input.
    inline void NextFrame() noexcept
    {
        auto& S = State();
        S.m_ButtonWasDown = S.m_ButtonDown;
        S.m_KeyWasDown    = S.m_KeyDown;
        S.m_Relative      = { 0.0f, 0.0f };
        S.m_Wheel         = 0.0f;
        S.m_LatestChar    = 0;
        if (!S.m_Typing.empty()) { S.m_LatestChar = static_cast<unsigned char>(S.m_Typing.front()); S.m_Typing.erase(0, 1); }       // the next character of the text being typed
        ++S.m_Frames;
    }
}
