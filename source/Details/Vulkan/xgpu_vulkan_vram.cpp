namespace xgpu::vulkan
{
    //------------------------------------------------------------------------------------------------------------------------
    // Device-local memory: few large allocations (drivers cap them around 4096) instead of one per resource. Blocks of a kind: a memory type,
    // small (< 1 MB, in 8 MB blocks) or not (64 MB blocks), images or buffers (bufferImageGranularity never applies) - an empty block takes either. An allocation goes into
    // the fullest block of its kind that has room (they pack, the others can empty); none: a new block (in a released block's slot when there
    // is one). Freed ranges merge (OffsetAllocator); a block that empties goes back to the driver unless it is the only empty one of its
    // memory type and size class (kept for the next ones) - textures moving to fewer mips (xtexture residency) must give the memory back.
    // Small ones apart: long-lived small images (texture tails) kept big blocks from emptying (a Level closed: 300 MB free inside blocks -
    // two empty ones kept, two pinned by 1-3 small allocations; the profiler's memory map, 2026-11-01).
    // ponytail: one lock for every block; a lock per block when loader threads contend
    //------------------------------------------------------------------------------------------------------------------------

    vram_allocation device::AllocVRAM( const VkMemoryRequirements& Req, bool bImage, const char* pName ) noexcept
    {
        vram_allocation A;
        std::uint32_t   Type = 0;
        if( !getMemoryType(Req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Type) ) return A;

        const auto Dedicated = [&]() -> vram_allocation
        {
            const VkMemoryAllocateInfo Info{ .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = Req.size, .memoryTypeIndex = Type };
            vram_allocation D;
            if( auto VKErr = AllocMemory(Info, D.m_Memory, pName ? pName : bImage ? "image (own allocation)" : "buffer (own allocation)"); VKErr ) m_Instance->ReportError(VKErr, "Fail to allocate device memory");
            else ++m_nVRAMDedicated;
            return D;
        };
        if( Req.size > vram_dedicated_v || (bImage && Req.size >= vram_dedicated_image_v) ) return Dedicated();

        // Units for the size, and the alignment past one unit (the range's start rounded up to it)
        const VkDeviceSize      Extra = Req.alignment > vram_unit_v ? Req.alignment - vram_unit_v : 0;
        const std::uint32_t     Units = static_cast<std::uint32_t>((Req.size + Extra + vram_unit_v - 1) / vram_unit_v);
        const bool bSmall = Req.size < vram_small_v;
        const auto Fits = [&](const vram_block& B) { return B.m_Memory && B.m_Type == Type && B.m_bSmall == bSmall && (B.m_bImage == bImage || B.m_nLive == 0); };
        const auto Take = [&](int i) -> bool
        {
            auto& B = m_VRAMBlocks[i];
            if( !Fits(B) ) return false;
            const auto R = B.m_pAlloc->allocate(Units);
            if( R.offset == OffsetAllocator::Allocation::NO_SPACE ) return false;
            B.m_bImage = bImage;                    // (an empty block was either kind)
            A.m_Memory = B.m_Memory;
            A.m_Offset = (VkDeviceSize(R.offset) * vram_unit_v + Req.alignment - 1) / Req.alignment * Req.alignment;
            A.m_iBlock = i;
            A.m_Range  = R;
            ++B.m_nLive;
            XPROFILE_MEM_ALLOC(MemoryKind(B.m_Type), std::uint64_t(i) + 1, A.m_Offset, Req.size, pName ? pName : bImage ? "image" : "buffer");
            return true;
        };

        std::lock_guard Lk(m_VRAMLock);
        {
            // the fullest first: the least free space (an empty block last)
            std::vector<std::pair<std::uint32_t, int>> Order;
            for( int i = 0; i < static_cast<int>(m_VRAMBlocks.size()); ++i ) if( Fits(m_VRAMBlocks[i]) ) Order.push_back({ m_VRAMBlocks[i].m_pAlloc->storageReport().totalFreeSpace, i });
            std::sort(Order.begin(), Order.end());
            for( const auto& [Free, i] : Order ) if( Take(i) ) return A;
        }

        const VkDeviceSize         BlockSize = bSmall ? vram_small_block_v : vram_block_v;
        const VkMemoryAllocateInfo Info{ .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = BlockSize, .memoryTypeIndex = Type };
        VkDeviceMemory Memory;
        if( vkAllocateMemory(m_VKDevice, &Info, m_Instance->m_pVKAllocator, &Memory) ) return Dedicated();     // (no room for a block: the range alone)
        int iNew = 0;
        while( iNew < static_cast<int>(m_VRAMBlocks.size()) && m_VRAMBlocks[iNew].m_Memory ) ++iNew;
        if( iNew == static_cast<int>(m_VRAMBlocks.size()) ) m_VRAMBlocks.emplace_back();
        auto& B     = m_VRAMBlocks[iNew];
        B.m_Memory  = Memory;
        B.m_Type    = Type;
        B.m_bImage  = bImage;
        B.m_bSmall  = bSmall;
        B.m_nLive   = 0;
        B.m_pAlloc  = std::make_unique<OffsetAllocator::Allocator>(static_cast<std::uint32_t>(BlockSize / vram_unit_v));
        XPROFILE_MEM_REGION(MemoryKind(Type), std::uint64_t(iNew) + 1, BlockSize);      // (the profiler's memory map: block i is region i + 1)
        Take(iNew);
        return A;
    }

    //------------------------------------------------------------------------------------------------------------------------
    // Every device memory allocation of xGPU but the blocks' goes through these two: the profiler's memory map gets it as a region of its own
    // (one allocation, tagged), in the pool of its memory's kind

    VkResult device::AllocMemory( const VkMemoryAllocateInfo& Info, VkDeviceMemory& Out, const char* pTag ) noexcept
    {
        const VkResult R = vkAllocateMemory(m_VKDevice, &Info, m_Instance->m_pVKAllocator, &Out);
        if( R == VK_SUCCESS )
        {
            const auto Id = reinterpret_cast<std::uint64_t>(Out);
            XPROFILE_MEM_REGION(MemoryKind(Info.memoryTypeIndex), Id, Info.allocationSize);
            XPROFILE_MEM_ALLOC(MemoryKind(Info.memoryTypeIndex), Id, 0, Info.allocationSize, pTag);
        }
        return R;
    }

    void device::FreeMemory( VkDeviceMemory Memory ) noexcept
    {
        if( Memory == VK_NULL_HANDLE ) return;
        XPROFILE_MEM_REGION(nullptr, reinterpret_cast<std::uint64_t>(Memory), 0);
        vkFreeMemory(m_VKDevice, Memory, m_Instance->m_pVKAllocator);
    }

    // Where a memory type is, by what it is: the card's own memory (device local), the part of it the CPU can write (ReBAR / the BAR
    // window: device local and host visible), or system memory the GPU reads over the bus (host visible: shared; host cached: read back)
    const char* device::MemoryKind( std::uint32_t Type ) const noexcept
    {
        const auto F = m_VKDeviceMemoryProperties.memoryTypes[Type].propertyFlags;
        const bool bDevice = F & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, bHost = F & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, bCached = F & VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        if( bDevice && bHost ) return "VRAM, CPU visible (ReBAR)";
        if( bDevice )          return "VRAM (device local)";
        if( bHost && bCached ) return "Shared system RAM (cached)";
        if( bHost )            return "Shared system RAM";
        return "Other memory";
    }

    //------------------------------------------------------------------------------------------------------------------------

    void device::FreeVRAM( vram_allocation& A ) noexcept
    {
        if( !A.isValid() ) return;
        if( A.m_iBlock < 0 )
        {
            FreeMemory(A.m_Memory);
            --m_nVRAMDedicated;
        }
        else
        {
            std::lock_guard Lk(m_VRAMLock);
            auto& B = m_VRAMBlocks[A.m_iBlock];
            B.m_pAlloc->free(A.m_Range);
            XPROFILE_MEM_FREE(MemoryKind(B.m_Type), std::uint64_t(A.m_iBlock) + 1, A.m_Offset);
            if( --B.m_nLive == 0 )
            {
                bool bOtherEmpty = false;
                for( const auto& O : m_VRAMBlocks ) bOtherEmpty |= &O != &B && O.m_Memory && O.m_nLive == 0 && O.m_Type == B.m_Type && O.m_bSmall == B.m_bSmall;
                if( bOtherEmpty )
                {
                    vkFreeMemory(m_VKDevice, B.m_Memory, m_Instance->m_pVKAllocator);
                    XPROFILE_MEM_REGION(MemoryKind(B.m_Type), std::uint64_t(A.m_iBlock) + 1, 0);
                    B.m_Memory = VK_NULL_HANDLE;
                    B.m_pAlloc.reset();
                }
            }
        }
        A = {};
    }

    //------------------------------------------------------------------------------------------------------------------------

    xgpu::device::memory_stats device::getMemoryStats( void ) noexcept
    {
        std::lock_guard Lk(m_VRAMLock);
        xgpu::device::memory_stats S{ .m_nDedicated = m_nVRAMDedicated.load() };
        for( const auto& B : m_VRAMBlocks ) if( B.m_Memory ) { ++S.m_nBlocks; S.m_nRanges += B.m_nLive; S.m_FreeBytes += std::uint64_t(B.m_pAlloc->storageReport().totalFreeSpace) * vram_unit_v; }
        return S;
    }
}
