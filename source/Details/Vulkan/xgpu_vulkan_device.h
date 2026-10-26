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

                                       ~device              (void) noexcept override;
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

        virtual xgpu::device::error*    UpdateTexture       ( xgpu::texture&                            Texture
                                                            , int                                        OffsetX
                                                            , int                                        OffsetY
                                                            , int                                        Width
                                                            , int                                        Height
                                                            , std::span<const std::byte>                Source
                                                            ) noexcept override;

        virtual xgpu::device::error*    ReadTexture         ( const xgpu::texture&                      Texture
                                                            , std::vector<std::uint32_t>&               Dest
                                                            , int&                                       Width
                                                            , int&                                       Height
                                                            ) noexcept override;

        virtual xgpu::device::error*    ReadTexture         ( const xgpu::texture&                      Texture
                                                            , std::vector<std::byte>&                   Dest
                                                            , int&                                       Width
                                                            , int&                                       Height
                                                            ) noexcept override;
        virtual xgpu::device::error*    Create              ( xgpu::timestamps&                         Timestamps
                                                            , const xgpu::timestamps::setup&            Setup
                                                            , std::shared_ptr<device_handle>&           SharedDevice
                                                            ) noexcept override;
        virtual xgpu::device::error*    Create              ( xgpu::compute&                            Compute
                                                            , const xgpu::compute::setup&               Setup
                                                            , std::shared_ptr<device_handle>&           SharedDevice
                                                            ) noexcept override;
        void                            PageFlipNotification( void ) noexcept;
        void                            DeathMarch          ( void ) noexcept;
        virtual void                    Shutdown            ( void ) noexcept override;
        virtual std::uint64_t           getFrameIndex       ( void ) const noexcept override { return static_cast<std::uint64_t>(m_FrameIndex); }
        virtual bool                    isBindless          ( void ) const noexcept override { return m_bBindless; }
        virtual bool                    hasDrawIndirectCount( void ) const noexcept override { return m_bDrawIndirectCount; }

        virtual void                    Destroy             ( xgpu::texture&& Texture )                     noexcept override;
        virtual void                    Destroy             ( xgpu::pipeline_instance&& PipelineInstance )  noexcept override;
        virtual void                    Destroy             ( xgpu::pipeline&& Pipeline )                   noexcept override;
        virtual void                    Destroy             ( xgpu::renderpass&& RenderPass )               noexcept override;
        virtual void                    Destroy             ( xgpu::buffer&& Buffer )                       noexcept override;
        virtual void                    Destroy             ( xgpu::window&& Window )                       noexcept override;
        virtual void                    Destroy             ( xgpu::texture_table&& Table )                 noexcept override;
        virtual xgpu::device::error*    Create              ( xgpu::texture_table&                      Table
                                                            , const xgpu::texture_table::setup&         Setup
                                                            , std::shared_ptr<device_handle>&           SharedDevice
                                                            ) noexcept override;
        virtual xgpu::sampler_id        getSampler          ( const xgpu::pipeline::sampler& Desc )        noexcept override;

        // The textures' ids and, on a bindless device, the global heaps every texture-table pipeline reads at set 3: binding 0 the 2D
        // textures and 1 the cube ones (by texture_id), 2 the samplers (by sampler_id). The registry is kept on every device: a
        // non-bindless table builds its set from it (xgpu_vulkan_texture_table.cpp)
        struct texture_heap
        {
            std::mutex                                          m_Lock;
            std::vector<std::weak_ptr<vulkan::texture>>         m_Textures;         // by texture_id (0 never valid)
            std::vector<std::uint32_t>                          m_Free;
            std::vector<VkSampler>                              m_Samplers;         // by sampler_id (0 never valid)
            std::unordered_map<std::uint64_t, std::uint32_t>    m_SamplerIDs;       // the description's key -> id
            std::unordered_map<int, VkDescriptorSetLayout>      m_TableLayouts;     // non-bindless: per slot count
            std::vector<VkDescriptorPool>                       m_TablePools;       // non-bindless: the tables' sets
            VkDescriptorSetLayout                               m_Layout    {};     // bindless: the heaps
            VkDescriptorPool                                    m_Pool      {};
            VkDescriptorSet                                     m_Set       {};
            std::uint32_t                                       m_nTextures = 0, m_nSamplers = 0;
            xgpu::texture                                       m_White;            // non-bindless: an invalid slot's texture
        };
        texture_heap                    m_Heap;
        upload_queue                    m_Upload;                       // the streaming lane (xgpu_vulkan_upload.h)
        // Device-local memory in blocks (xgpu_vulkan_vram.cpp): textures with data, render targets, GPU_READ buffers
        std::mutex                      m_VRAMLock;
        std::deque<vram_block>          m_VRAMBlocks;
        std::atomic<int>                m_nVRAMDedicated    { 0 };
        vram_allocation                 AllocVRAM           ( const VkMemoryRequirements& Req, bool bImage ) noexcept;
        void                            FreeVRAM            ( vram_allocation& A ) noexcept;
        virtual xgpu::device::memory_stats getMemoryStats   ( void ) noexcept override;
        std::atomic<std::uint64_t>      m_nMainQueueWaits   { 0 };     // vkQueueWaitIdle on the main queue (the setup command buffer: readbacks, region updates)
        virtual xgpu::timeline_point    FlushUploads        ( void ) noexcept override { return { m_Upload.Flush() }; }
        virtual bool                    isDone              ( xgpu::timeline_point P ) noexcept override { return m_Upload.isDone(P.m_Value); }
        virtual void                    Wait                ( xgpu::timeline_point P ) noexcept override { m_Upload.Wait(P.m_Value); }
        virtual std::uint64_t           getMainQueueWaits   ( void ) const noexcept override { return m_nMainQueueWaits.load(); }
        // For a main-queue submission (EndFrame, the setup command buffer): the uploads' acquires to run first (CmdBuffers), the waits
        // and signals to add. Call before taking the main queue's lock
        void                            UploadSubmitInfo    ( std::vector<VkCommandBufferSubmitInfo>& CmdBuffers, std::vector<VkSemaphoreSubmitInfo>& Waits, std::vector<VkSemaphoreSubmitInfo>& Signals ) noexcept;
        xgpu::device::error*            InitHeap            ( void ) noexcept;
        std::uint32_t                   AllocTextureID      ( const std::shared_ptr<vulkan::texture>& T ) noexcept;
        void                            FreeTextureID       ( std::uint32_t ID ) noexcept;
        std::shared_ptr<vulkan::texture> getTexture         ( std::uint32_t ID ) noexcept;
        VkSampler                       getVKSampler        ( std::uint32_t ID ) noexcept;
        VkDescriptorSetLayout           TextureTableLayout  ( int nSlots ) noexcept;
        VkDescriptorSet                 AllocTableSet       ( int nSlots, VkDescriptorPool& Pool ) noexcept;
        void                            FreeTableSet        ( VkDescriptorPool Pool, VkDescriptorSet Set ) noexcept;


        struct death_march
        {
            std::vector<xgpu::texture>            m_Texture;
            std::vector<xgpu::pipeline_instance>  m_PipelineInstance;
            std::vector<xgpu::pipeline>           m_Pipeline;
            std::vector<xgpu::renderpass>         m_RenderPass;
            std::vector<xgpu::buffer>             m_Buffer;
            std::vector<xgpu::window>             m_Window;
            std::vector<xgpu::texture_table>      m_TextureTable;
            std::vector<std::pair<VkDescriptorPool, VkDescriptorSet>> m_TableSets;      // a texture table's set it replaced (texture_table::BuildSet)
        };

        using mati_per_renderpass_map = std::unordered_map<std::uint64_t, pipeline_instance::cached>;
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
        bool                                            m_bHostQueryReset           = false;    // VkPhysicalDeviceHostQueryResetFeatures, needed by vulkan::timestamps
        bool                                            m_bDrawIndirectCount        = false;    // VkPhysicalDeviceVulkan12Features::drawIndirectCount: xgpu::device::hasDrawIndirectCount
        bool                                            m_bBindless                 = false;    // descriptor indexing on (CreateGraphicsDevice): xgpu::device::isBindless
        std::array<death_march,2>                       m_DeathMarchList            {};
        // The frames submitted and not waited yet (every window's): the CPU's first write into a GPU buffer waits them (WaitInFlight)
        std::mutex                                      m_InFlightLock              {};
        std::vector<VkFence>                            m_InFlight                  {};
        std::atomic<int>                                m_LastDoneFrame             {-1};      // the newest m_FrameIndex known complete (its fence waited)
        void                                            AddInFlight                 ( VkFence Fence ) noexcept;
        void                                            RemoveInFlight              ( VkFence Fence ) noexcept;
        void                                            WaitInFlight                ( void ) noexcept;
        mati_per_renderpass_map                         m_PipeLineInstanceMap       {};
        mat_per_renderpass_map                          m_PipeLineMap               {};
    };
}