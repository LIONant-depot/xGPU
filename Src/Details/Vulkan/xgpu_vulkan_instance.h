namespace xgpu::vulkan
{
    struct instance final : xgpu::system::instance
    {
        VkInstance                          m_VKInstance            { VK_NULL_HANDLE };
        HINSTANCE                           m_hInstance             { 0 };
        bool                                m_bValidation           { false };
        std::shared_ptr<instance>           m_Self;
        std::string                         m_AppName;
        VkAllocationCallbacks*              m_pVKAllocator          { nullptr };

        std::vector<std::unique_ptr<local_storage>> m_LocalStorageCleanup   {};
        std::mutex                                  m_LocalStorageMutex     {};
                    
                void                        ReportError             ( std::string_view Message                    , const std::source_location& location = std::source_location::current() ) noexcept;
                void                        ReportError             ( VkResult ErrorCode, std::string_view Message, const std::source_location& location = std::source_location::current() ) noexcept;
                void                        ReportWarning           ( std::string_view Message                    , const std::source_location& location = std::source_location::current() ) noexcept;
                void                        ReportWarning           ( VkResult ErrorCode, std::string_view Message, const std::source_location& location = std::source_location::current() ) noexcept;

                local_storage&              getLocalStorage         ( void )                                                                noexcept;
        virtual                            ~instance                ( void )                                                                noexcept;
                xgpu::instance::error*      CreateVKInstance        ( const xgpu::instance::setup& Setup )                                  noexcept;
                xgpu::device::error*        CollectPhysicalDevices  ( std::vector<VkPhysicalDevice>&    PhysicalDevices
                                                                    , xgpu::device::discreate           DecreateType
                                                                    ) noexcept;


        virtual xgpu::device::error*        CreateDevice            ( xgpu::device&     Device,     const xgpu::device::setup&      Setup ) noexcept override;
        virtual xgpu::keyboard::error*      CreateKeyboard          ( xgpu::keyboard&   Keyboard,   const xgpu::keyboard::setup&    Setup ) noexcept override;
        virtual xgpu::mouse::error*         CreateMouse             ( xgpu::mouse&      Mouse,      const xgpu::mouse::setup&       Setup ) noexcept override;
        virtual bool                        ProcessInputEvents      ( void )                                                                noexcept override;
    };
}