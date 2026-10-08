#ifndef VGPU_LINUX_LOCALSTORAGE_H
#define VGPU_LINUX_LOCALSTORAGE_H
#pragma once
//
// Linux OS services shared by every Linux window backend (X11 today, Wayland later): nothing in here
// knows about a window system. Namespace is linux_os because `linux` is a predefined macro in GNU mode.
//
#include <pthread.h>
#include <cassert>

namespace xgpu::linux_os
{
    struct local_storage
    {
        local_storage ( void ) noexcept { [[maybe_unused]] int r = pthread_key_create(&m_Key, nullptr); assert(r == 0); }
       ~local_storage ( void ) noexcept { pthread_key_delete(m_Key); }
        void  setRaw  ( void* pPtr ) noexcept { pthread_setspecific(m_Key, pPtr); }
        void* getRaw  ( void ) noexcept { return pthread_getspecific(m_Key); }
        pthread_key_t m_Key {};
    };
}
#endif
