#ifndef _XGPU
#define _XGPU
#pragma once

//
// System Headers
//
#include<array>
#include<memory>
#include<string>
#include<span>
#include<vector>
#include<variant>
#include<cassert>
#include<mutex>
#include<optional>

//
// Utility macros
//
#define XGPU_INLINE __forceinline

#ifdef _DEBUG
    #define XGPU_DEBUG_CMD(A) A
#else
    #define XGPU_DEBUG_CMD(A) 
#endif

//
// Predefinitions 
//
namespace xgpu::details
{
    struct window_handle;
    struct instance_handle;
    struct device_handle;
    struct keyboard_handle;
    struct mouse_handle;
    struct pipeline_handle;
    struct pipeline_instance_handle;
    struct shader_handle;
    struct vertex_descriptor_handle;
    struct texture_handle;
    struct texture_instance_handle;
    struct buffer_handle;
    struct renderpass_handle;
}

namespace xgpu
{
    struct device;
    struct instance;
};

//
// Helpful classes
//
namespace xgpu::details
{
    template< auto T_ERR, std::size_t T_SIZE >
    consteval auto GenerateError(const char(&Message)[T_SIZE]) noexcept
    {
        using err_t = decltype(T_ERR);
        static_assert(sizeof(err_t) == 1);
        std::array<char, T_SIZE + 1> NewString{};
        NewString[0] = static_cast<char>(T_ERR);
        for (int i = 0; i < T_SIZE; ++i) NewString[i + 1] = Message[i];
        return NewString;
    }
}

namespace xgpu
{
    template<typename T> XGPU_INLINE
    const char* getErrorMsg(T pErr) noexcept
    {
        assert( pErr != nullptr );
        return reinterpret_cast<const char*>(&pErr[1]);
    }

    template<typename T> XGPU_INLINE
    int getErrorInt(T pErr) noexcept
    {
        if(pErr == nullptr) return 0;
        return - ( 1 + static_cast<int>(*pErr) );
    }

    template<typename ... Args>
    std::string FormatString( const char* pFmt, Args ... args )
    {
        // Extra space for '\0' so + 1
        int size_s = std::snprintf(nullptr, 0, pFmt, args ...) + 1; 
        if (size_s <= 0) { return "Error formatting string...."; }
        auto size = static_cast<size_t>(size_s);
        auto buf = std::make_unique<char[]>(size);
        std::snprintf(buf.get(), size, pFmt, args ...);
        // We don't want the '\0' inside so size -1
        return std::string(buf.get(), buf.get() + size - 1); 
    }

    template< typename T >
    class lock_object
    {
    public:

        auto&   get         ( void ) noexcept       { assert(m_Locked);  return m_Value;               }
        auto&   get         ( void ) const noexcept { assert(m_Locked);  return m_Value;               }
        void    lock        ( void ) noexcept       { m_Mutex.lock(); assert(!m_Locked); XGPU_DEBUG_CMD(m_Locked = true);     }
        void    unlock      ( void ) noexcept       { assert(m_Locked);  XGPU_DEBUG_CMD(m_Locked = false);  m_Mutex.unlock(); }
        bool    try_lock    ( void ) noexcept       { if( m_Mutex.try_lock() ) { assert(!m_Locked); XGPU_DEBUG_CMD(m_Locked = true); return true; } return false; }

    protected:

        std::mutex  m_Mutex     {};
        T           m_Value     {};
        XGPU_DEBUG_CMD( bool m_Locked = false );
    };

    #define VGPU_ERROR(CODE,STR) [&]{ static constexpr auto StrError = xgpu::details::GenerateError<CODE>( STR ); return const_cast<std::decay_t<decltype(CODE)>*>(reinterpret_cast<const decltype(CODE)*>(StrError.data())); }()
};

//
// Public headers
//
#include "xgpu_mouse.h"
#include "xgpu_keyboard.h"
#include "xgpu_vertex_descriptor.h"
#include "xgpu_texture.h"
#include "xgpu_buffer.h"
#include "xgpu_shader.h"
#include "xgpu_pipeline.h"
#include "xgpu_pipeline_instance.h"
#include "xgpu_cmd_buffer.h"
#include "xgpu_renderpass.h"
#include "xgpu_window.h"
#include "xgpu_device.h"
#include "xgpu_instance.h"

//
// Private headers
//
#include "details/xgpu_keyboard_inline.h"
#include "details/xgpu_mouse_inline.h"
#include "details/xgpu_window_inline.h"
#include "details/xgpu_cmd_buffer_inline.h"
#include "details/xgpu_instance_inline.h"
#include "details/xgpu_pipeline_inline.h"
#include "details/xgpu_pipeline_instance_inline.h"
#include "details/xgpu_vertex_descriptor_inline.h"
#include "details/xgpu_buffer_inline.h"
#include "details/xgpu_device_inline.h"
#include "details/xgpu_shader_inline.h"
#include "details/xgpu_texture_inline.h"
#include "details/xgpu_renderpass_inline.h"

#endif