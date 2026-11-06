#pragma once
// The environment's switches (XGPU_..., XLION_...) read in one place: std::getenv and std::fopen are the standard's and safe as used here (read once,
// a diagnostics file); MSVC deprecates them (C4996), so the warning is answered once, here
#include <cstdlib>
#include <cstdio>

namespace xgpu::details
{
    inline const char* Env( const char* pName ) noexcept
    {
#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable: 4996)
#endif
        return std::getenv(pName);
#if defined(_MSC_VER)
    #pragma warning(pop)
#endif
    }
    inline std::FILE* Open( const char* pPath, const char* pMode ) noexcept
    {
#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable: 4996)
#endif
        return std::fopen(pPath, pMode);
#if defined(_MSC_VER)
    #pragma warning(pop)
#endif
    }
}
