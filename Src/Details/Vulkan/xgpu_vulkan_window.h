namespace xgpu::vulkan
{
    struct cmdbuffer
    {
        VkCommandBuffer                     m_VKCommandBuffer;
        VkRenderPass                        m_VKActiveRenderPass;
        vulkan::renderpass*                 m_pActiveRenderPass;
        pipeline_instance::per_renderpass   m_ActivePipelineInstance;
        VkPipelineLayout                    m_VKPipelineLayout;

    };

    static_assert( sizeof(xgpu::cmd_buffer) == (sizeof(cmdbuffer) + sizeof(std::size_t)) );

    struct window final : xgpu::system::window
    {
        [[nodiscard]]   xgpu::device::error*    Initialize                  ( std::shared_ptr<vulkan::device>&& Device
                                                                            , const xgpu::window::setup&        Setup 
                                                                            ) noexcept;
        [[nodiscard]]   xgpu::device::error*    CreateWindowSwapChain       ( int Width
                                                                            , int Height 
                                                                            ) noexcept;
        [[nodiscard]]   xgpu::device::error*    CreateOrResizeWindow        ( int Width
                                                                            , int Height 
                                                                            ) noexcept;
        [[nodiscard]]   xgpu::device::error*    CreateWindowCommandBuffers  ( void 
                                                                            ) noexcept;
        [[nodiscard]]   xgpu::device::error*    CreateRenderPass            ( VkSurfaceFormatKHR& VKSurfaceFormat
                                                                            , VkFormat&           VKDepthSurfaceFormat
                                                                            ) noexcept;
        [[nodiscard]]   xgpu::device::error*    DestroyRenderPass           ( void 
                                                                            ) noexcept;
        [[nodiscard]]   xgpu::device::error*    CreateDepthResources        ( VkExtent2D Extents
                                                                            ) noexcept;
        [[nodiscard]]   xgpu::device::error*    EndFrame                    ( void
                                                                            ) noexcept;
        virtual
        bool                                    BegingRendering             ( void
                                                                            ) noexcept override;
        virtual
        void                                    CmdRenderBegin              ( xgpu::cmd_buffer& CmdBuffer
                                                                            ) noexcept override;
        virtual     
        void                                    CmdRenderBegin              ( xgpu::cmd_buffer& CmdBuffer
                                                                            , const xgpu::renderpass& Renderpass
                                                                            ) noexcept override;
        virtual
        void                                    CmdRenderEnd                ( xgpu::cmd_buffer& CmdBuffer
                                                                            ) noexcept override;
        virtual
        void                                    setPipelineInstance         ( xgpu::cmd_buffer&        CmdBuffer
                                                                            , xgpu::pipeline_instance& Instance 
                                                                            ) noexcept override;
        virtual
        void                                    setBuffer                   ( xgpu::cmd_buffer& CmdBuffer
                                                                            , xgpu::buffer&     Buffer
                                                                            , int               StartingElementIndex
                                                                            ) noexcept override;
        virtual     
        void                                    setStreamingBuffers         ( xgpu::cmd_buffer&         CmdBuffer
                                                                            , std::span<xgpu::buffer>   Buffers
                                                                            , int                       StartingElementIndex
                                                                            ) noexcept;
        virtual
        void                                    DrawInstance                ( xgpu::cmd_buffer& CmdBuffer
                                                                            , int               InstanceCount
                                                                            , int               IndexCount
                                                                            , int               FirstInstance
                                                                            , int               FirstIndex
                                                                            , int               VertexOffset 
                                                                            ) noexcept override;
        virtual
        void                                    setViewport                 ( xgpu::cmd_buffer& CmdBuffer
                                                                            , float x
                                                                            , float y
                                                                            , float w
                                                                            , float h
                                                                            , float minDepth
                                                                            , float maxDepth 
                                                                            ) noexcept override;
        virtual
        void                                    setScissor                  ( xgpu::cmd_buffer& CmdBuffer
                                                                            , int x
                                                                            , int y
                                                                            , int w
                                                                            , int h 
                                                                            ) noexcept override;
        virtual
        void                                    setPushConstants            ( xgpu::cmd_buffer& CmdBuffer
                                                                            , const void*       pData
                                                                            , std::size_t       Size 
                                                                            ) noexcept override;
        virtual     
        void*                                   getUniformBufferVMem        ( xgpu::cmd_buffer&         CmdBuffer
                                                                            , xgpu::shader::type::bit   ShaderType
                                                                            , std::size_t               Size
                                                                            ) noexcept override;
        virtual     
        void                                    setDepthBias                ( xgpu::cmd_buffer& CmdBuffer
                                                                            , float             ConstantFactor
                                                                            , float             DepthBiasClamp
                                                                            , float             DepthBiasSlope
                                                                            ) noexcept override;
        virtual
        void                                    setClearColor               ( float             R
                                                                            , float             G
                                                                            , float             B
                                                                            , float             A 
                                                                            ) noexcept override;
        virtual
        void                                    PageFlip                    ( void 
                                                                            ) noexcept override;
        virtual     
        xgpu::device                            getDevice                   ( void
                                                                            ) const noexcept override;

        using mati_per_renderpass_map = std::unordered_map<std::uint64_t, pipeline_instance::per_renderpass>;
        using mat_per_renderpass_map  = std::unordered_map<std::uint64_t, pipeline::per_renderpass>;

        std::shared_ptr<vulkan::device>         m_Device                {};
        VkSurfaceKHR                            m_VKSurface             {};
        std::array<VkClearValue,2>              m_VKClearValue          { VkClearValue{ .color = {.float32 = {0,0,0,1}} }, VkClearValue{ .depthStencil{ 1.0f, 0 } }};
        bool                                    m_bClearOnRender        {};
        VkSwapchainKHR                          m_VKSwapchain           {};
        std::uint32_t                           m_ImageCount            {2}; // 2 for a double buffer
        std::unique_ptr<frame[]>                m_Frames                {};
        std::unique_ptr<frame_semaphores[]>     m_FrameSemaphores       {};
        VkImage                                 m_VKDepthbuffer         {};
        VkImageView                             m_VKDepthbufferView     {};
        VkDeviceMemory                          m_VKDepthbufferMemory   {};
        VkRenderPass                            m_VKRenderPass          {};
        VkPipeline                              m_VKPipeline            {};
        VkSurfaceFormatKHR                      m_VKSurfaceFormat       {};
        VkFormat                                m_VKDepthFormat         {};
        VkPresentModeKHR                        m_VKPresentMode         {};
        std::uint32_t                           m_SemaphoreIndex        {0};
        std::uint32_t                           m_FrameIndex            {0};
        bool                                    m_bRebuildSwapChain     {false};
        int                                     m_BeginState            {0};
        int                                     m_nCmds                 {0};
        VkViewport                              m_DefaultViewport       {};
        VkRect2D                                m_DefaultScissor        {};
        mati_per_renderpass_map                 m_PipeLineInstanceMap   {};
        mat_per_renderpass_map                  m_PipeLineMap           {};
    };
}