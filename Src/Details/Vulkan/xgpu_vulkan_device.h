namespace xgpu::vulkan
{
    struct device final : xgpu::details::device_handle
    {
        xgpu::device::error* Initialize
        (   std::shared_ptr<xgpu::vulkan::instance>&    Instance
        ,   const xgpu::device::setup&                  Setup
        ,   std::uint32_t                               MainQueueIndex
        ,   VkPhysicalDevice                            PhysicalDevice
        ,   std::vector<VkQueueFamilyProperties>        Properties 
        ) noexcept;

                bool                    getMemoryType       ( std::uint32_t     TypeBits
                                                            , VkFlags           Properties
                                                            , std::uint32_t&    TypeIndex 
                                                            ) const noexcept;

                xgpu::device::error* FlushLocalStorageCommandBuffer( void
                                                            ) noexcept;

        void                            FlushCommands       ( void 
                                                            );
        virtual void                    getInstance         ( xgpu::instance& Instance 
                                                            ) const noexcept override;
        virtual xgpu::device::error*    Create              ( xgpu::window&                             Window
                                                            , const xgpu::window::setup&                Setup
                                                            , std::shared_ptr<device_handle>&           SharedDevice
                                                            ) noexcept override;

        virtual xgpu::device::error*    Create              ( xgpu::renderpass&                         Renderpass
                                                            , const xgpu::renderpass::setup&            Setup
                                                            , std::shared_ptr<device_handle>&           SharedDevice
                                                            ) noexcept override;

        virtual xgpu::device::error*    Create              ( xgpu::pipeline&                           Pipeline
                                                            , const xgpu::pipeline::setup&              Setup
                                                            , std::shared_ptr<device_handle>&           SharedDevice
                                                            ) noexcept override;

        virtual xgpu::device::error*    Create              ( xgpu::pipeline_instance&                  PipelineInstance
                                                            , const xgpu::pipeline_instance::setup&     Setup
                                                            , std::shared_ptr<device_handle>&           SharedDevice
                                                            ) noexcept override;

        virtual xgpu::device::error*    Create              ( xgpu::shader&                             Shader
                                                            , const xgpu::shader::setup&                Setup
                                                            , std::shared_ptr<device_handle>&           SharedDevice
                                                            ) noexcept override;

        virtual xgpu::device::error*    Create              ( xgpu::vertex_descriptor&                  VDescriptor
                                                            , const xgpu::vertex_descriptor::setup&     Setup
                                                            , std::shared_ptr<device_handle>&           SharedDevice
                                                            ) noexcept override;

        virtual xgpu::device::error*    Create              ( xgpu::texture&                            Texture
                                                            , const xgpu::texture::setup&               Setup
                                                            , std::shared_ptr<device_handle>&           SharedDevice
                                                            ) noexcept override;

        virtual xgpu::device::error*    Create              ( xgpu::buffer&                             Buffer
                                                            , const xgpu::buffer::setup&                Setup
                                                            , std::shared_ptr<device_handle>&           SharedDevice
                                                            ) noexcept override;



        std::shared_ptr<xgpu::vulkan::instance>         m_Instance                  {};
        VkPhysicalDevice                                m_VKPhysicalDevice          {};
        VkDevice                                        m_VKDevice                  {};
        std::uint32_t                                   m_MainQueueIndex            {0xffffffff};
        std::uint32_t                                   m_TransferQueueIndex        {0xffffffff};
        xgpu::lock_object<VkQueue>                      m_VKMainQueue               {};
        xgpu::lock_object<VkQueue>                      m_VKTransferQueue           {};
        VkPhysicalDeviceMemoryProperties                m_VKDeviceMemoryProperties  {};
        VkPipelineCache                                 m_VKPipelineCache           {};  // ??
        VkDeviceSize                                    m_BufferMemoryAlignment     {256};
        VkPhysicalDeviceProperties                      m_VKPhysicalDeviceProperties{};
        lock_object<VkDescriptorPool>                   m_LockedVKDescriptorPool    {};
    };
}