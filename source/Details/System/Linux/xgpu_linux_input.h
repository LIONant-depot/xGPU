#ifndef VGPU_LINUX_INPUT_H
#define VGPU_LINUX_INPUT_H
#pragma once
//
// Linux input state shared by every Linux window backend. A backend (Xlib, Wayland, ...) only translates its own events
// into writes on these two objects; the per-frame protocol (ButtonIndex / KeyWasDownIndex flip, WasDown cleared) is the
// same one the Windows backend uses.
//
#include <array>
#include <cstdint>

namespace xgpu::linux_os
{
    struct keyboard final : xgpu::details::keyboard_handle
    {
        using full_keyboard = std::array< bool, (int)xgpu::keyboard::digital::ENUM_COUNT >;

        virtual bool isPressedGeneric ( int GadgetID ) const noexcept override { return m_KeyIsDown[GadgetID]; }
        virtual bool wasPressedGeneric( int GadgetID ) const noexcept override { return m_KeyWasDown[m_KeyWasDownIndex][GadgetID]; }
        virtual int  getLatestChar    ( void )         const noexcept override { return m_MostRecentChar; }

        int                             m_KeyWasDownIndex   { 0 };
        full_keyboard                   m_KeyIsDown         {};     // what the user reads (left/right modifiers folded into the left one, as on Windows)
        full_keyboard                   m_KeyPhysical       {};     // what is physically down (left and right modifiers kept apart)
        std::array<full_keyboard, 2>    m_KeyWasDown        {};
        int                             m_MostRecentChar    { 0 };
        xgpu::keyboard::digital         m_MostRecentKey     { xgpu::keyboard::digital::KEY_NULL };
    };

    struct mouse final : xgpu::details::mouse_handle
    {
        using full_digital_mouse = std::array< bool,                 (int)xgpu::mouse::digital::ENUM_COUNT >;
        using full_analog_mouse  = std::array< std::array<float, 2>, (int)xgpu::mouse::analog::ENUM_COUNT  >;

        virtual bool                 isPressedGeneric ( int GadgetID ) const noexcept override { return m_ButtonIsDown[GadgetID]; }
        virtual bool                 wasPressedGeneric( int GadgetID ) const noexcept override { return m_ButtonWasDown[m_ButtonIndex][GadgetID]; }
        virtual std::array<float, 2> getValueGeneric  ( int GadgetID ) const noexcept override { return m_Analog[GadgetID]; }

        int                               m_ButtonIndex   { 0 };
        full_digital_mouse                m_ButtonIsDown  {};
        std::array<full_digital_mouse, 2> m_ButtonWasDown {};
        full_analog_mouse                 m_Analog        {};
    };

    // Linux evdev key code (linux/input-event-codes.h) -> xgpu key. X11 reports evdev+8 and Wayland reports evdev
    // directly, so this one table serves both. The first 88 codes are the same PC scan codes xgpu::keyboard::digital
    // uses (it is the DirectInput table); only the extended keys need a lookup.
    constexpr xgpu::keyboard::digital EvdevToKey( int Evdev ) noexcept
    {
        using k = xgpu::keyboard::digital;
        if( Evdev >= 1 && Evdev <= 88 ) return static_cast<k>(Evdev);
        switch( Evdev )
        {
        case  89: return k::KEY_ABNT_C1;
        case  96: return k::KEY_NUMPADENTER;
        case  97: return k::KEY_RCONTROL;
        case  98: return k::KEY_DIVIDE;
        case  99: return k::KEY_SYSRQ;
        case 100: return k::KEY_RALT;
        case 102: return k::KEY_HOME;
        case 103: return k::KEY_UP;
        case 104: return k::KEY_PAGEUP;
        case 105: return k::KEY_LEFT;
        case 106: return k::KEY_RIGHT;
        case 107: return k::KEY_END;
        case 108: return k::KEY_DOWN;
        case 109: return k::KEY_PAGEDOWN;
        case 110: return k::KEY_INSERT;
        case 111: return k::KEY_DELETE;
        case 113: return k::KEY_MUTE;
        case 114: return k::KEY_VOLUMEDOWN;
        case 115: return k::KEY_VOLUMEUP;
        case 116: return k::KEY_POWER;
        case 117: return k::KEY_NUMPADEQUALS;
        case 119: return k::KEY_PAUSE;
        case 121: return k::KEY_NUMPADCOMMA;
        case 125: return k::KEY_LWIN;
        case 126: return k::KEY_RWIN;
        case 127: return k::KEY_APPS;
        case 183: return k::KEY_F13;
        case 184: return k::KEY_F14;
        case 185: return k::KEY_F15;
        }
        return k::KEY_NULL;
    }
}
#endif
