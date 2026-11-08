namespace xgpu::vulkan
{
    //----------------------------------------------------------------------------------
    // vkFlushMappedMemoryRanges/vkInvalidateMappedMemoryRanges require offset and size to
    // be multiples of nonCoherentAtomSize, unless size == VK_WHOLE_SIZE or offset+size ==
    // the allocation size. A partial Lock/Unlock (Count != m_nEntries) can request a byte
    // range that isn't aligned to that (e.g. 64 on most desktop GPUs), so round it out to
    // an aligned range instead of using the raw entry-based offset/size.
    static VkMappedMemoryRange ComputeAlignedRange( VkDeviceMemory Memory, VkDeviceSize AtomSize, VkDeviceSize AllocationSize, VkDeviceSize RawOffset, VkDeviceSize RawSize ) noexcept
    {
        const VkDeviceSize AlignedOffset = (RawOffset / AtomSize) * AtomSize;
        VkDeviceSize        AlignedEnd   = ((RawOffset + RawSize + AtomSize - 1) / AtomSize) * AtomSize;
        if (AlignedEnd > AllocationSize) AlignedEnd = AllocationSize;

        return VkMappedMemoryRange
        { .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE
        , .memory = Memory
        , .offset = AlignedOffset
        , .size   = AlignedEnd - AlignedOffset
        };
    }

    //----------------------------------------------------------------------------------

    xgpu::device::error* buffer::Initialize( std::shared_ptr<device>&& Device, const xgpu::buffer::setup& Setup ) noexcept
    {
        m_Device = Device;
        return Create(Setup);
    }

    //----------------------------------------------------------------------------------

    xgpu::device::error* buffer::Create(const xgpu::buffer::setup& Setup) noexcept
    {
        assert(Setup.m_EntryCount >= 1 );
        assert(Setup.m_EntryByteSize > 0);

        // Uniform entries are bound at dynamic offsets: each one is rounded up to the device's offset alignment (64 on most
        // desktop GPUs, up to 256 elsewhere), so the user describes its data as is and pays only the padding this GPU needs
        auto EntryByteSize = static_cast<VkDeviceSize>(Setup.m_EntryByteSize);
        if( Setup.m_Type == xgpu::buffer::type::UNIFORM )
        {
            const VkDeviceSize Align = m_Device->m_VKPhysicalDeviceProperties.limits.minUniformBufferOffsetAlignment;
            EntryByteSize = (EntryByteSize + Align - 1) / Align * Align;
        }

        m_Type   = Setup.m_Type;
        m_pName  = Setup.m_pName;
        m_Usage  = Setup.m_Usage;
        m_bWriteOnly = Setup.m_bWriteOnly;

        VkDeviceSize ByteSize = EntryByteSize * Setup.m_EntryCount;

        // A storage buffer is bound whole (window: the draws' and the dispatches' descriptors): one past the device's maxStorageBufferRange (llvmpipe:
        // 128 MB; most desktop GPUs: 4 GB) could not be seen by a shader past that range - it is refused here, by name, instead of reading out of bounds
        if( Setup.m_Type == xgpu::buffer::type::STORAGE && ByteSize > m_Device->m_VKPhysicalDeviceProperties.limits.maxStorageBufferRange )
        {
            m_Device->m_Instance->ReportError(std::format("xGPU: the storage buffer '{}' is {} bytes, past this device's maxStorageBufferRange ({} bytes)"
                                                         , Setup.m_pName ? Setup.m_pName : "(no name)", std::uint64_t(ByteSize), m_Device->m_VKPhysicalDeviceProperties.limits.maxStorageBufferRange));
            return VGPU_ERROR(xgpu::device::error::FAILURE, "A storage buffer past the device's maxStorageBufferRange");
        }

        ByteSize = ((ByteSize - 1) / m_Device->m_BufferMemoryAlignment + 1) * m_Device->m_BufferMemoryAlignment;
        VkBufferCreateInfo BufferInfo
        { .sType         = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO
        , .size          = ByteSize
        , .usage         = m_Usage == xgpu::buffer::setup::usage::GPU_READ
                            ? VK_BUFFER_USAGE_TRANSFER_SRC_BIT        // Use as staging buffer
                            : static_cast<VkBufferUsageFlags>
                                ( Setup.m_Type == xgpu::buffer::type::INDEX 
                                ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                                : Setup.m_Type == xgpu::buffer::type::VERTEX 
                                    ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                                    : Setup.m_Type == xgpu::buffer::type::UNIFORM 
                                        ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
                                        : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT     // (it may hold draw commands: DrawIndexedIndirect)
                                ) | VK_BUFFER_USAGE_TRANSFER_SRC_BIT     // a source of window::CopyBuffer
                                  | VK_BUFFER_USAGE_TRANSFER_DST_BIT     // and a target (a device-local buffer read back through it)
         , .sharingMode   = VK_SHARING_MODE_EXCLUSIVE
        };

        if( auto VKErr = vkCreateBuffer(m_Device->m_VKDevice, &BufferInfo, m_Device->m_Instance->m_pVKAllocator, &m_VKBuffer ); VKErr )
        {
            m_Device->m_Instance->ReportError(VKErr, "Fail to create a Buffer");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create a Buffer");
        }

        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(m_Device->m_VKDevice, m_VKBuffer, &req );

        m_Device->m_BufferMemoryAlignment = (m_Device->m_BufferMemoryAlignment > req.alignment) ? m_Device->m_BufferMemoryAlignment : req.alignment;
        VkMemoryAllocateInfo AllocInfo
        {   .sType              = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
        ,   .allocationSize     = req.size
        ,   .memoryTypeIndex    = 0
        };
        // HOST_COHERENT, not just HOST_VISIBLE: without it, a memory type is allowed to be host-
        // cached, meaning a GPU write isn't guaranteed visible to a later CPU MapLock without an
        // explicit vkInvalidateMappedMemoryRanges (which nothing in this engine ever called) - CPU
        // readback of GPU-written data (e.g. a compute/fragment-shader result read back through
        // MemoryMap) could silently see stale bytes. The Vulkan spec guarantees at least one memory
        // type exposes both bits together, so this can't fail to find one where HOST_VISIBLE alone
        // would have succeeded.
        m_Device->getMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, AllocInfo.memoryTypeIndex );

        // write-only: device-local where the CPU can map it (the GPU's reads stay on the card - the light cells read 7x faster); the BAR
        // heap full or none: host memory as above
        const std::uint32_t HostType = AllocInfo.memoryTypeIndex;
        const bool bDevice = Setup.m_bWriteOnly && m_Usage == xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ
            && m_Device->getMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, AllocInfo.memoryTypeIndex);
        VkResult VKErr = bDevice ? m_Device->AllocMemory(AllocInfo, m_VKBufferMemory, m_pName ? m_pName : "buffer (own allocation)") : VK_ERROR_OUT_OF_DEVICE_MEMORY;
        if( VKErr != VK_SUCCESS )
        {
            AllocInfo.memoryTypeIndex = HostType;
            VKErr = m_Device->AllocMemory(AllocInfo, m_VKBufferMemory, m_pName ? m_pName : "buffer (own allocation)");
        }
        if( VKErr )
        {
            m_Device->m_Instance->ReportError(VKErr, "Fail to Allocate Memory for Buffer");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to Allocate Memory for Buffer");
        }

        if( auto VKErr = vkBindBufferMemory( m_Device->m_VKDevice, m_VKBuffer, m_VKBufferMemory, 0); VKErr )
        {
            m_Device->m_Instance->ReportError(VKErr, "Fail to Bind Memory for Buffer");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to Bind Memory for Buffer");
        }

        m_ByteSize       = static_cast<int>(req.size);
        m_nEntries       = Setup.m_EntryCount;
        m_EntrySizeBytes = static_cast<int>(EntryByteSize);

        //
        // If the user wants us to copy the data right away we can do that
        //
        if(Setup.m_pData)
        {
            assert(*Setup.m_pData);
            assert(m_EntrySizeBytes == Setup.m_EntryByteSize);     // initial data comes packed: its entries must not need padding
            void* pDst;
            if( auto Err = MapLock(pDst, 0, m_nEntries); Err ) return Err;
            std::memcpy( pDst, *Setup.m_pData, m_nEntries*m_EntrySizeBytes );
            if (auto Err = MapUnlock(0, m_nEntries); Err ) return Err;
        }

        return nullptr;
    }

    //----------------------------------------------------------------------------------

    void buffer::WaitFramesUpTo( std::int64_t Frame ) noexcept { m_Device->WaitFramesUpTo(Frame); }

    xgpu::device::error* buffer::MapLock( void*& pMemory, int StartIndex, int Count, bool bWaitInFlight ) noexcept
    {
        // MemoryMapFrameRegion: a slot of a ring of 3 slots a device frame (xLIONRender's ring_frames_v): the frame 3 back used it last.
        // One window: done already. Several (a frame in flight each): wait for it - no more (2026-11-08: Play's device lost without this)
        if( !bWaitInFlight ) m_Device->WaitFramesUpTo( static_cast<std::int64_t>(m_Device->m_FrameIndex) - 3 );
        static const char* s_pForceWait = xgpu::details::Env("XGPU_FORCE_MAP_WAIT");      // (measurement: maps wait as before 2026-11-08 - every one, or those whose name holds the value)
        if( s_pForceWait && (s_pForceWait[0] == '1' || (m_pName && std::strstr(m_pName, s_pForceWait))) ) bWaitInFlight = true;
        if( bWaitInFlight )          // the frame in flight may still read what is about to be written (MemoryMapFrameRegion: not)
        {
            static const bool s_bLog = xgpu::details::Env("XGPU_WAIT_LOG") != nullptr;
            const auto T0 = std::chrono::steady_clock::now();
            m_Device->WaitInFlight();
            if( s_bLog ) if( const double Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count(); Ms >= 1.0 ) { std::printf("[xGPU] MAPWAIT %.2f ms: %s\n", Ms, m_pName ? m_pName : "(no name)"); std::fflush(stdout); }
        }
        assert(StartIndex < m_nEntries);
        assert((StartIndex+Count) <= m_nEntries);
        assert(m_ByteSize);

        const VkDeviceSize RawOffset = static_cast<VkDeviceSize>(StartIndex) * m_EntrySizeBytes;
        const VkDeviceSize RawSize   = static_cast<VkDeviceSize>(Count)      * m_EntrySizeBytes;

        // The range vkMapMemory is given here must be the EXACT same range MapUnlock later
        // flushes (ComputeAlignedRange is a pure function of these same inputs, so both calls
        // agree independently) - vkFlushMappedMemoryRanges/vkInvalidateMappedMemoryRanges are
        // only allowed to touch bytes within the currently-mapped range, not just within the
        // memory object as a whole, so mapping the raw (unaligned) entry range while flushing
        // an atom-size-rounded one would ask to flush bytes that were never actually mapped.
        const VkMappedMemoryRange Range = Count == m_nEntries
            ? VkMappedMemoryRange
              { .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE
              , .memory = m_VKBufferMemory
              , .offset = 0
              , .size   = VK_WHOLE_SIZE
              }
            : ComputeAlignedRange( m_VKBufferMemory, m_Device->m_VKPhysicalDeviceProperties.limits.nonCoherentAtomSize, static_cast<VkDeviceSize>(m_ByteSize), RawOffset, RawSize );

        // A buffer the CPU writes or reads (not GPU_READ's staging copy) is mapped once, the first time, and stays mapped until Destroy:
        // its memory is host-visible and coherent, so a pointer into it is all a write or a read needs - no map, unmap or transfer each call
        // (Tomas 2026-11-08). GPU_READ maps its staging memory for the one copy and unmaps it (MapUnlock)
        if( m_Usage != xgpu::buffer::setup::usage::GPU_READ )
        {
            if( m_pVMapMemory == nullptr )
            {
                void* pWhole;
                if( auto VKErr = vkMapMemory( m_Device->m_VKDevice, m_VKBufferMemory, 0, VK_WHOLE_SIZE, 0, &pWhole ); VKErr )
                {
                    m_Device->m_Instance->ReportError(VKErr, "Fail to Map Memory from Buffer");
                    return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to Map Memory from Buffer");
                }
                m_pVMapMemory = reinterpret_cast<std::byte*>(pWhole);
            }
            pMemory = m_pVMapMemory + RawOffset;
        }
        else
        {
            void* pMappedBase;
            if( auto VKErr = vkMapMemory
            ( m_Device->m_VKDevice
            , m_VKBufferMemory
            , Range.offset
            , Range.size
            , 0
            , &pMappedBase
            ); VKErr )
            {
                m_Device->m_Instance->ReportError(VKErr, "Fail to Map Memory from Buffer");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to Map Memory from Buffer");
            }

            // The caller expects a pointer to their logical StartIndex, which can differ from
            // Range.offset once that offset gets rounded down to a nonCoherentAtomSize boundary.
            pMemory = reinterpret_cast<std::byte*>(pMappedBase) + (RawOffset - Range.offset);
        }

        // Belt-and-suspenders on top of always allocating HOST_COHERENT memory now (see Create):
        // guarantees this map sees whatever the GPU most recently wrote, rather than relying
        // exclusively on the memory type choice. A no-op on coherent memory per the Vulkan spec, so
        // this is free insurance, not a workaround for a specific known-non-coherent case.
        auto InvalidateRanges = std::array{ Range };
        if( auto VKErr = vkInvalidateMappedMemoryRanges(m_Device->m_VKDevice, static_cast<std::uint32_t>(InvalidateRanges.size()), InvalidateRanges.data()); VKErr )
        {
            m_Device->m_Instance->ReportError(VKErr, "Fail to Invalidate Mapped Memory for Buffer");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to Invalidate Mapped Memory for Buffer");
        }

        return nullptr;
    }

    //----------------------------------------------------------------------------------

    xgpu::device::error* buffer::TransferToDestination( void ) noexcept
    {
        VkDeviceSize ByteSize = m_EntrySizeBytes * m_nEntries;
        ByteSize = ((ByteSize - 1) / m_Device->m_BufferMemoryAlignment + 1) * m_Device->m_BufferMemoryAlignment;
        VkBufferCreateInfo BufferInfo
        { .sType        = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO
        , .size         = ByteSize
        , .usage        = static_cast<VkBufferUsageFlags>( ( m_Type == xgpu::buffer::type::INDEX 
                                                            ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT 
                                                            : m_Type == xgpu::buffer::type::VERTEX 
                                                                ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                                                                : m_Type == xgpu::buffer::type::UNIFORM 
                                                                    ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
                                                                    : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT
                                                               )
                                                               | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                                                               | VK_BUFFER_USAGE_TRANSFER_SRC_BIT     // (a source of window::CopyBuffer too: one device-local buffer into another)
                                                             )
        , .sharingMode  = VK_SHARING_MODE_EXCLUSIVE
        };

        VkBuffer VKFinalBuffer;
        if (auto VKErr = vkCreateBuffer(m_Device->m_VKDevice, &BufferInfo, m_Device->m_Instance->m_pVKAllocator, &VKFinalBuffer); VKErr)
        {
            m_Device->m_Instance->ReportError(VKErr, "Fail to create a local Buffer");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create a local Buffer");
        }

        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(m_Device->m_VKDevice, VKFinalBuffer, &req);

        m_Device->m_BufferMemoryAlignment = (m_Device->m_BufferMemoryAlignment > req.alignment) ? m_Device->m_BufferMemoryAlignment : req.alignment;
        // A range of a device-local block (device::AllocVRAM), or its own allocation when it is large
        m_VRAM = m_Device->AllocVRAM(req, false, m_pName);
        if (!m_VRAM.isValid())
        {
            vkDestroyBuffer(m_Device->m_VKDevice, VKFinalBuffer, m_Device->m_Instance->m_pVKAllocator);
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to Allocate Memory for local Buffer");
        }

        if (auto VKErr = vkBindBufferMemory(m_Device->m_VKDevice, VKFinalBuffer, m_VRAM.m_Memory, m_VRAM.m_Offset); VKErr)
        {
            m_Device->m_Instance->ReportError(VKErr, "Fail to Bind Memory for local Buffer");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to Bind Memory for local Buffer");
        }

        //
        // Transfer the memory: copied on the transfer queue (the upload queue frees the host buffer when its batch is done); the frames
        // wait on the GPU, nothing waits here
        //
        if( auto Err = m_Device->m_Upload.UploadBuffer(m_VKBuffer, m_VKBufferMemory, VKFinalBuffer, ByteSize); Err ) return Err;

        m_VKBufferMemory = VK_NULL_HANDLE;      // (the host one is the upload queue's to free now)
        m_VKBuffer       = VKFinalBuffer;

        // By setting this guy to zero we are saying that we should not need to touch this memory again
        m_ByteSize       = 0;

        return nullptr;
    }

    //----------------------------------------------------------------------------------

    xgpu::device::error* buffer::MapUnlock(int StartIndex, int Count) noexcept
    {
        assert(StartIndex < m_nEntries);
        assert((StartIndex + Count) <= m_nEntries);

        auto Ranges = std::array
        {
            Count == m_nEntries
            ? VkMappedMemoryRange
              { .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE
              , .memory = m_VKBufferMemory
              , .offset = 0
              , .size   = VK_WHOLE_SIZE
              }
            : ComputeAlignedRange
              ( m_VKBufferMemory
              , m_Device->m_VKPhysicalDeviceProperties.limits.nonCoherentAtomSize
              , static_cast<VkDeviceSize>(m_ByteSize)
              , static_cast<VkDeviceSize>(StartIndex * m_EntrySizeBytes)
              , static_cast<VkDeviceSize>(Count * m_EntrySizeBytes)
              )
        };

        if (auto VKErr = vkFlushMappedMemoryRanges(m_Device->m_VKDevice, static_cast<std::uint32_t>(Ranges.size()), Ranges.data()); VKErr)
        {
            m_Device->m_Instance->ReportError(VKErr, "Fail to Map Memory from Buffer");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to Map Memory from Buffer");
        }

        if( m_pVMapMemory == nullptr ) vkUnmapMemory(m_Device->m_VKDevice, m_VKBufferMemory);      // (a persistent mapping stays: Destroy)

        if (m_Usage == xgpu::buffer::setup::usage::GPU_READ && m_ByteSize)
        {
            return TransferToDestination();
        }

        return nullptr;
    }

    //----------------------------------------------------------------------------------

    int buffer::getEntryCount(void) const noexcept
    {
        return m_nEntries;
    }

    //----------------------------------------------------------------------------------

    VkDeviceSize buffer::StorageRange() const noexcept
    {
        const VkDeviceSize Max = m_Device->m_VKPhysicalDeviceProperties.limits.maxStorageBufferRange;
        return static_cast<VkDeviceSize>(m_ByteSize) > Max ? Max : VK_WHOLE_SIZE;
    }

    //----------------------------------------------------------------------------------

    xgpu::buffer::error* buffer::Resize( int NewEntryCount ) noexcept
    {
        m_Device->WaitInFlight();          // (the old buffer is destroyed here: the frame in flight may still use it)
        assert( m_Usage != xgpu::buffer::setup::usage::GPU_READ );

        const xgpu::buffer::setup Setup
        { .m_Type           = m_Type
        , .m_Usage          = m_Usage
        , .m_EntryByteSize  = m_EntrySizeBytes
        , .m_EntryCount     = NewEntryCount
        , .m_pName          = m_pName
        , .m_bWriteOnly     = m_bWriteOnly
        };

        Destroy();

        return reinterpret_cast<xgpu::buffer::error*>(Create(Setup));
    }

    //----------------------------------------------------------------------------------

    // The Vulkan buffer and its memory go with the object - the deferred destroy's second release (DeathMarch) ends here. It was empty: every
    // buffer ever destroyed kept its memory (a closed Level's whole renderer: ~460 MB of VRAM and shared RAM, found with the profiler's memory map,
    // 2026-11-01); only Resize freed one
    buffer::~buffer(void) noexcept
    {
        if( m_Device ) Destroy();
    }

    //----------------------------------------------------------------------------------

    int buffer::getEntrySize(void) const noexcept
    {
        return m_EntrySizeBytes;
    }

    //----------------------------------------------------------------------------------

    void* buffer::allocEntry(void) noexcept
    {
        assert(m_Type == xgpu::buffer::type::UNIFORM);  // Only for dynamic UBOs

        // get the next value in our circular queue
        auto V = m_CurrentOffset.load();
        while (true)
        {
            auto N = V + 1;
            if (N == m_nEntries) N = 0;
            if (m_CurrentOffset.compare_exchange_weak(V, N))
            {
                V = N;
                break;
            }
        }
        // The ring comes back to the first entry the frame before wrote (the frame in flight's: one at most, window::EndFrame): wait for it
        if (m_AllocFrame != m_Device->m_FrameIndex) { m_PrevFrameStart = m_FrameStart; m_FrameStart = V; m_AllocFrame = m_Device->m_FrameIndex; }
        if (V == m_PrevFrameStart) { static const bool s_bLog = xgpu::details::Env("XGPU_WAIT_LOG") != nullptr; const auto T0 = std::chrono::steady_clock::now(); m_Device->WaitInFlight(); if( s_bLog ) if( const double Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count(); Ms >= 1.0 ) { std::printf("[xGPU] RINGWAIT %.2f ms: %s (the dynamic ring came round to the frame in flight's entries)\n", Ms, m_pName ? m_pName : "(no name)"); std::fflush(stdout); } }

        m_LastOffset = V * m_EntrySizeBytes;

        //
        // Make sure the memory is mapped
        //
        if (m_pVMapMemory == nullptr)
        {
            assert(m_Usage == xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ);

            void* pData;
            if (auto Err = MapLock(pData, 0, m_nEntries); Err)
                return nullptr;

            m_pVMapMemory = reinterpret_cast<std::byte*>(pData);
        }

        return &m_pVMapMemory[m_LastOffset];
    }

    //----------------------------------------------------------------------------------

    void buffer::Destroy( void ) noexcept
    {
        if(m_pVMapMemory)
        {
            vkUnmapMemory(m_Device->m_VKDevice, m_VKBufferMemory);
            m_pVMapMemory = nullptr;
        }

        if(m_VKBuffer)
        {
            vkDestroyBuffer( m_Device->m_VKDevice, m_VKBuffer, m_Device->m_Instance->m_pVKAllocator );
            m_VKBuffer = VK_NULL_HANDLE;
        }

        if(m_VKBufferMemory)
        {
            m_Device->FreeMemory(m_VKBufferMemory);
            m_VKBufferMemory = VK_NULL_HANDLE;
        }
        m_Device->FreeVRAM(m_VRAM);
    }

    //----------------------------------------------------------------------------------

    void buffer::DeathMarch(xgpu::buffer&& Buffer) noexcept
    {
        if (m_bQueued) return;              // (the deferred destroy releasing it: the destructor frees it)
        m_bQueued = true;
        m_Device->Destroy(std::move(Buffer));
    }
}