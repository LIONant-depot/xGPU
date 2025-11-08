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

        void                            PageFlipNotification( void ) noexcept;
        void                            DeathMarch          ( void ) noexcept;
        virtual void                    Shutdown            ( void ) noexcept override;

        virtual void                    Destroy             ( xgpu::texture&& Texture )                     noexcept override;
        virtual void                    Destroy             ( xgpu::pipeline_instance&& PipelineInstance )  noexcept override;
        virtual void                    Destroy             ( xgpu::pipeline&& Pipeline )                   noexcept override;
        virtual void                    Destroy             ( xgpu::renderpass&& RenderPass )               noexcept override;
        virtual void                    Destroy             ( xgpu::buffer&& Buffer )                       noexcept override;
        virtual void                    Destroy             ( xgpu::window&& Window )                       noexcept override;


        struct death_march
        {
            std::vector<xgpu::texture>            m_Texture;
            std::vector<xgpu::pipeline_instance>  m_PipelineInstance;
            std::vector<xgpu::pipeline>           m_Pipeline;
            std::vector<xgpu::renderpass>         m_RenderPass;
            std::vector<xgpu::buffer>             m_Buffer;
            std::vector<xgpu::window>             m_Window;
        };

        using mati_per_renderpass_map = std::unordered_map<std::uint64_t, pipeline_instance::per_renderpass>;
        using mat_per_renderpass_map = std::unordered_map<std::uint64_t, pipeline::per_renderpass>;


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
        int                                             m_FrameIndex                = 0;
        std::array<death_march,2>                       m_DeathMarchList            {};
        mati_per_renderpass_map                         m_PipeLineInstanceMap       {};
        mat_per_renderpass_map                          m_PipeLineMap               {};
    };
}