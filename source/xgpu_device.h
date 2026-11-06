namespace xgpu
{
    // A point on the upload timeline (device::FlushUploads): done once the GPU passed it
    struct timeline_point
    {
        std::uint64_t   m_Value = 0;
    };

    struct device
    {
        enum class error : std::uint8_t
        {
            FAILURE
        };

        enum class type : std::uint8_t
        {
            RENDER_AND_SWAP
        ,   RENDER_ONLY
        ,   COMPUTE
        ,   COPY
        ,   ENUM_COUNT
        };

        enum class discreate : std::uint8_t
        {
            ANY_GPU
        ,   DISCREATE_ONLY
        ,   NON_DISCREATE_ONLY
        };

        struct setup
        {
            type        m_Type              { type::RENDER_AND_SWAP };
            discreate   m_Discreate         { discreate::DISCREATE_ONLY };
        };

        XGPU_INLINE               void           getInstance    ( xgpu::instance& Instance 
                                                                ) const noexcept;
        [[nodiscard]] XGPU_INLINE device::error* Create         ( window&                           Window
                                                                , const window::setup&              Setup 
                                                                ) noexcept;

        [[nodiscard]] XGPU_INLINE device::error* Create         ( renderpass&                       Renderpass
                                                                , const renderpass::setup&          Setup 
                                                                ) noexcept;

        [[nodiscard]] XGPU_INLINE device::error* Create         ( pipeline&                         Pipeline
                                                                , const pipeline::setup&            Setup 
                                                                ) noexcept;

        [[nodiscard]] XGPU_INLINE device::error* Create         ( pipeline_instance&                PipelineInstance
                                                                , const pipeline_instance::setup&   Setup 
                                                                ) noexcept;

        [[nodiscard]] XGPU_INLINE device::error* Create         ( shader&                           Shader
                                                                , const shader::setup&              Setup 
                                                                ) noexcept;

        [[nodiscard]] XGPU_INLINE device::error* Create         ( vertex_descriptor&                VDescriptor
                                                                , const vertex_descriptor::setup&   Setup 
                                                                ) noexcept;

        [[nodiscard]] XGPU_INLINE device::error* Create         ( texture&                          Texture
                                                                , const texture::setup&             Setup 
                                                                ) noexcept;

        [[nodiscard]] XGPU_INLINE device::error* Create         ( buffer&                           Buffer
                                                                , const buffer::setup&              Setup
                                                                ) noexcept;
        [[nodiscard]] XGPU_INLINE device::error* Create         ( timestamps&                       Timestamps
                                                                , const timestamps::setup&          Setup
                                                                ) noexcept;
        [[nodiscard]] XGPU_INLINE device::error* Create         ( compute&                          Compute
                                                                , const compute::setup&             Setup
                                                                ) noexcept;
        [[nodiscard]] XGPU_INLINE device::error* Create         ( texture_table&                    Table
                                                                , const texture_table::setup&       Setup
                                                                ) noexcept;

        // The sampler table's entry for Desc: one per distinct description, for the device's lifetime (an invalid id: the table is full)
        [[nodiscard]] XGPU_INLINE sampler_id     getSampler     ( const pipeline::sampler&          Desc
                                                                ) noexcept;

        // Writes Source into the [OffsetX,OffsetY]..[+Width,+Height] sub-rectangle of Texture's mip 0 (only -
        // no mips/array layers/cubemaps), leaving the rest of the texture untouched. Texture must already be
        // created (with the same format as Source implies) and not currently in use by an in-flight command
        // buffer. General-purpose - any atlas or streaming/dynamic texture content, not tied to one feature.
        // Src was created with setup::m_FirstMip > 0 (its image holds every mip, only those from m_FirstMip are in and shown): uploads the
        // mips above (MipFaceData: per face, mips 0..m_FirstMip-1, face-major) into the same image and makes Out a texture of all its mips -
        // its own view and texture_id, the image shared (it lives while either texture does). Out is usable at once: the frames wait for
        // the upload on the GPU, as for any created texture; Src keeps showing its mips meanwhile (no other mip of the image is touched).
        [[nodiscard]] XGPU_INLINE device::error* ExtendTexture  ( texture&                          Out
                                                                , texture&                          Src
                                                                , std::span<const std::span<const std::byte>> MipFaceData
                                                                ) noexcept;

        // The pipelines made so far, kept between runs: Load merges a file saved by Save into the device's pipeline cache (none yet, or
        // one from another GPU or driver: nothing loaded, not an error), so a pipeline made before is not compiled again on first use.
        // XGPU_NO_PIPELINE_CACHE=1: Load loads nothing (to measure without it).
        XGPU_INLINE void                         LoadPipelineCache  ( std::wstring_view Path ) noexcept;
        [[nodiscard]] XGPU_INLINE device::error* SavePipelineCache  ( std::wstring_view Path ) noexcept;

        [[nodiscard]] XGPU_INLINE device::error* UpdateTexture  ( texture&                          Texture
                                                                , int                                OffsetX
                                                                , int                                OffsetY
                                                                , int                                Width
                                                                , int                                Height
                                                                , std::span<const std::byte>        Source
                                                                ) noexcept;

        // Reads back Texture's mip 0 as packed 32-bit pixels, one uint32 per texel in whatever byte order
        // Texture's own format actually uses (no conversion - e.g. R8G8B8A8_UNORM comes back R in the low
        // byte; the swapchain's own format, which window::Screenshot reads, happens to be B8G8R8A8, but that
        // is not true of every texture format). Only meaningful for an uncompressed 8-bit-per-channel format.
        // Synchronous: Texture's GPU work must already be complete (e.g. a fence already waited on) before
        // calling this, there is no internal wait.
        [[nodiscard]] XGPU_INLINE device::error* ReadTexture    ( const texture&                    Texture
                                                                , std::vector<std::uint32_t>&       Dest
                                                                , int&                               Width
                                                                , int&                               Height
                                                                ) noexcept;

        // Same as above but returns mip 0's texels exactly as stored, in the texture's own format (e.g. 8 bytes per
        // texel of half floats for R16G16B16A16_SFLOAT) - for numerical comparisons of HDR render targets.
        // Uncompressed color formats only.
        [[nodiscard]] XGPU_INLINE device::error* ReadTexture    ( const texture&                    Texture
                                                                , std::vector<std::byte>&           Dest
                                                                , int&                               Width
                                                                , int&                               Height
                                                                ) noexcept;

        XGPU_INLINE void                         Destroy        ( pipeline_instance&&               PipelineInstance ) noexcept;
        XGPU_INLINE void                         Destroy        ( pipeline&&                        Pipeline )         noexcept;
        XGPU_INLINE void                         Destroy        ( texture&&                         Texture )          noexcept;
        XGPU_INLINE void                         Destroy        ( buffer&&                          Buffer )           noexcept;
        XGPU_INLINE void                         Destroy        ( window&&                          Window )           noexcept;
        XGPU_INLINE void                         Destroy        ( renderpass&&                      Renderpass )       noexcept;
        XGPU_INLINE void                         Destroy        ( texture_table&&                   Table )            noexcept;
        XGPU_INLINE void                         Shutdown       ( void ) noexcept;
        // Advances once per page flip: for data a user keeps one copy of per frame in flight (index % frames in flight)
        [[nodiscard]] XGPU_INLINE std::uint64_t  getFrameIndex  ( void ) const noexcept;
        // Whether textures and samplers are read by index from the global tables (descriptor indexing) or through per-material
        // sets built from the same ids. Callers write the same code for both: diagnostics and tests only
        [[nodiscard]] XGPU_INLINE bool           isBindless     ( void ) const noexcept;
        // True only under WSL on Mesa Dozen while it reports Vulkan 1.2 (no core 1.3, no bind-time vertex strides).
        // Native Linux and Windows are always false. Remove when Dozen advertises Vulkan 1.3.
        [[nodiscard]] XGPU_INLINE bool           isDozenCompat  ( void ) const noexcept;
        // Whether cmd_buffer::DrawIndexedIndirectCount can be used (the count read from a buffer: Vulkan 1.2's drawIndirectCount)
        [[nodiscard]] XGPU_INLINE bool           hasDrawIndirectCount( void ) const noexcept;
        // Whether shaders may use GroupNonUniformClustered (subgroupClusteredAdd/Min): VK_SUBGROUP_FEATURE_CLUSTERED_BIT
        [[nodiscard]] XGPU_INLINE bool           hasSubgroupClustered( void ) const noexcept;
        // The largest storage buffer a shader can see whole (VkPhysicalDeviceLimits::maxStorageBufferRange: llvmpipe 128 MB, most desktop GPUs 4 GB):
        // buffer creation refuses a storage buffer past it; a user that grows a buffer with slack keeps the slack under it
        [[nodiscard]] XGPU_INLINE std::uint64_t  getMaxStorageBufferBytes( void ) const noexcept;
        // The most work groups one window::Dispatch can have along X (VkPhysicalDeviceLimits::maxComputeWorkGroupCount[0]: llvmpipe and many GPUs 65535):
        // a 1D grid past it is the caller's to split (an offset of its own in the push constants); Dispatch refuses it
        [[nodiscard]] XGPU_INLINE std::uint32_t  getMaxComputeGroupsX( void ) const noexcept;

        // The streaming lane: a texture or buffer created with data is copied on the transfer queue, never by stalling the main queue; every
        // frame (and anything else submitted to the main queue) waits on the GPU for the uploads flushed before it. FlushUploads sends what
        // is recorded now and returns its point (the window's frames flush by themselves); isDone / Wait: for tools and tests
        [[nodiscard]] XGPU_INLINE timeline_point FlushUploads   ( void ) noexcept;
        [[nodiscard]] XGPU_INLINE bool           isDone         ( timeline_point Point ) noexcept;
        XGPU_INLINE void                         Wait           ( timeline_point Point ) noexcept;
        // Diagnostics: the device-local blocks (64 MB, OffsetAllocator ranges), the ranges in them, the allocations of their own (past 16 MB)
        struct memory_stats
        {
            int             m_nBlocks       = 0;
            int             m_nRanges       = 0;
            int             m_nDedicated    = 0;
            std::uint64_t   m_FreeBytes     = 0;
        };
        [[nodiscard]] XGPU_INLINE memory_stats   getMemoryStats ( void ) noexcept;
        // Diagnostics: how many times the main queue was waited idle (readbacks and region updates still do; uploads never)
        [[nodiscard]] XGPU_INLINE std::uint64_t  getMainQueueWaits( void ) const noexcept;

        std::shared_ptr<details::device_handle>   m_Private;
    };
}