

#include "xGPU.h"
#include "Details/System/xgpu_system.cpp"
#include "Details/Vulkan/xgpu_vulkan.cpp"

xgpu::instance::error* xgpu::CreateInstance( xgpu::instance& Instance, const xgpu::instance::setup& Setup ) noexcept
{
    // Make sure we free up any previously created instance 
    Instance.m_Private.reset();

    // Choose the right driver
    switch( Setup.m_Driver )
    {
        case xgpu::instance::setup::driver::DIRECTX12: return VGPU_ERROR(xgpu::instance::error::FAILURE, "There is not support for DirectX 12 SDK");
        case xgpu::instance::setup::driver::METAL:     return VGPU_ERROR(xgpu::instance::error::FAILURE, "There is not support for Apple Metal SDK");
        case xgpu::instance::setup::driver::VULKAN:    if( auto Err = CreateInstanceVulkan(Instance, Setup); Err ) return Err;
    }

    return nullptr;
}
