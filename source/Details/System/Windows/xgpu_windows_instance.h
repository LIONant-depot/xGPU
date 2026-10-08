namespace xgpu::windows
{
    struct instance : xgpu::details::instance_handle
    {
        // Vulkan instance extensions this backend's windows need (spelled out: vulkan.h is not included yet at this point)
        static std::span<const char* const> getSurfaceExtensions( void ) noexcept
        {
            static constexpr const char* s_List[] = { "VK_KHR_win32_surface" };     // VK_KHR_WIN32_SURFACE_EXTENSION_NAME
            return s_List;
        }

        bool ProcessInputEvents( void ) noexcept;

        local_storage               m_LocalStorage  {};
        std::shared_ptr<keyboard>   m_Keyboard      = std::make_shared<keyboard>();
        std::shared_ptr<mouse>      m_Mouse         = std::make_shared<mouse>();
    };

}
