#ifndef VGPU_NULL_SYSTEM_H
#define VGPU_NULL_SYSTEM_H
#pragma once
//
// Linux port (linux-headless branch): "null" OS/window system backend for xGPU.
// Used on non-Windows platforms where xGPU has no native windowing backend yet.
// It provides the same interface as xgpu::windows::{instance,window,...} so the
// Vulkan backend compiles; window creation always fails cleanly, which is what
// the headless editor expects (it never opens a window). Windows builds never
// see this file (see xgpu_system.h).
//
#include <pthread.h>
#include <array>
#include <memory>
#include <cassert>
#include <cstdint>

namespace xgpu::nullsys
{
    struct local_storage
    {
        local_storage ( void ) noexcept { [[maybe_unused]] int r = pthread_key_create(&m_Key, nullptr); assert(r == 0); }
       ~local_storage ( void ) noexcept { pthread_key_delete(m_Key); }
        void  setRaw  ( void* pPtr ) noexcept { pthread_setspecific(m_Key, pPtr); }
        void* getRaw  ( void ) noexcept { return pthread_getspecific(m_Key); }
        pthread_key_t m_Key {};
    };

    struct keyboard final : xgpu::details::keyboard_handle
    {
        using full_keyboard = std::array< bool, (int)xgpu::keyboard::digital::ENUM_COUNT >;
        virtual bool isPressedGeneric ( int GadgetID ) const noexcept override { return m_KeyIsDown[GadgetID]; }
        virtual bool wasPressedGeneric( int GadgetID ) const noexcept override { return m_KeyWasDown[m_KeyWasDownIndex][GadgetID]; }
        virtual int  getLatestChar    ( void )         const noexcept override { return 0; }
        int                          m_KeyWasDownIndex { 0 };
        full_keyboard                m_KeyIsDown       {};
        std::array<full_keyboard, 2> m_KeyWasDown      {};
    };

    struct mouse final : xgpu::details::mouse_handle
    {
        using full_digital_mouse = std::array< bool, (int)xgpu::mouse::digital::ENUM_COUNT >;
        using full_analog_mouse  = std::array< std::array<float, 2>, (int)xgpu::mouse::analog::ENUM_COUNT >;
        virtual bool                 isPressedGeneric ( int GadgetID ) const noexcept override { return m_ButtonIsDown[GadgetID]; }
        virtual bool                 wasPressedGeneric( int GadgetID ) const noexcept override { return m_ButtonWasDown[m_ButtonIndex][GadgetID]; }
        virtual std::array<float, 2> getValueGeneric  ( int GadgetID ) const noexcept override { return m_Analog[GadgetID]; }
        int                               m_ButtonIndex   { 0 };
        full_digital_mouse                m_ButtonIsDown  {};
        std::array<full_digital_mouse, 2> m_ButtonWasDown {};
        full_analog_mouse                 m_Analog        {};
    };

    struct instance : xgpu::details::instance_handle
    {
        static std::span<const char* const> getSurfaceExtensions( void ) noexcept { return {}; }   // no windows, no surfaces
        bool ProcessInputEvents( void ) noexcept { return true; }   // no OS message pump
        local_storage               m_LocalStorage  {};
        std::shared_ptr<keyboard>   m_Keyboard      = std::make_shared<keyboard>();
        std::shared_ptr<mouse>      m_Mouse         = std::make_shared<mouse>();
    };

    struct window : xgpu::details::window_handle
    {
        xgpu::device::error* Initialize( const xgpu::window::setup&, instance& ) noexcept
        {
            return VGPU_ERROR(xgpu::device::error::FAILURE, "xGPU: no window system backend on this platform (headless only)");
        }
        virtual int                 getWidth             ( void ) const noexcept override { return m_Width; }
        virtual int                 getHeight            ( void ) const noexcept override { return m_Height; }
        virtual bool                BegingRendering      ( void )       noexcept override { return m_isMinimize; }
        virtual std::size_t         getSystemWindowHandle( void ) const noexcept override { return 0; }
        virtual bool                isFocused            ( void ) const noexcept override { return false; }
        virtual bool                isCapturing          ( void ) const noexcept override { return false; }
        virtual bool                isHovered            ( void ) const noexcept override { return false; }
        virtual void                setFocus             ( void ) const noexcept override {}
        virtual std::pair<int, int> getPosition          ( void ) const noexcept override { return { 0, 0 }; }
        virtual void                setPosition          ( int, int )   noexcept override {}
        virtual void                setSize              ( int W, int H ) noexcept override { m_Width = W; m_Height = H; }
        bool getResizedAndReset ( void ) noexcept { auto b = m_isResized; m_isResized = false; return b; }
        bool isMinimized        ( void ) const noexcept override { return false; }
        void setMousePosition   ( int, int ) noexcept override {}
        void setFrameless       ( bool f ) noexcept { m_isFrameless = f; }
        virtual ~window() = default;

        int                 m_Width       { 0 };
        int                 m_Height      { 0 };
        bool                m_isMinimize  { false };
        bool                m_isResized   { false };
        bool                m_isFrameless { false };
        bool                m_isHovered   { false };
        std::pair<int, int> m_TruePosition{};
    };
}
#endif