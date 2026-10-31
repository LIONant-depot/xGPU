namespace xgpu::vulkan
{
    //------------------------------------------------------------------------------------------------------------------------
    // Device-local memory: few large allocations (drivers cap them around 4096) instead of one per resource. The blocks of a memory type
    // are tried in order; none with room: a new one (in a released block's slot when there is one). Freed ranges merge (OffsetAllocator);
    // a block that empties goes back to the driver unless it is the only empty one of its kind (kept for the next ones) - textures moving
    // to fewer mips (xtexture residency) must give the memory back, not only free it inside xGPU.
    // ponytail: one lock for every block; a lock per block when loader threads contend
    //------------------------------------------------------------------------------------------------------------------------

    vram_allocation device::AllocVRAM( const VkMemoryRequirements& Req, bool bImage ) noexcept
    {
        vram_allocation A;
        std::uint32_t   Type = 0;
        if( !getMemoryType(Req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Type) ) return A;

        const auto Dedicated = [&]() -> vram_allocation
        {
            const VkMemoryAllocateInfo Info{ .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = Req.size, .memoryTypeIndex = Type };
            vram_allocation D;
            if( auto VKErr = vkAllocateMemory(m_VKDevice, &Info, m_Instance->m_pVKAllocator, &D.m_Memory); VKErr ) m_Instance->ReportError(VKErr, "Fail to allocate device memory");
            else
            {
                ++m_nVRAMDedicated;
                const auto Id = reinterpret_cast<std::uint64_t>(D.m_Memory);     // (the profiler's memory map: a region of its own, all of it one allocation)
                XPROFILE_MEM_REGION("VRAM", Id, Req.size);
                XPROFILE_MEM_ALLOC("VRAM", Id, 0, Req.size, bImage ? "image (own allocation)" : "buffer (own allocation)");
            }
            return D;
        };
        if( Req.size > vram_dedicated_v ) return Dedicated();

        // Units for the size, and the alignment past one unit (the range's start rounded up to it)
        const VkDeviceSize      Extra = Req.alignment > vram_unit_v ? Req.alignment - vram_unit_v : 0;
        const std::uint32_t     Units = static_cast<std::uint32_t>((Req.size + Extra + vram_unit_v - 1) / vram_unit_v);
        const auto Take = [&](int i) -> bool
        {
            auto& B = m_VRAMBlocks[i];
            if( !B.m_Memory || B.m_Type != Type || B.m_bImage != bImage ) return false;
            const auto R = B.m_pAlloc->allocate(Units);
            if( R.offset == OffsetAllocator::Allocation::NO_SPACE ) return false;
            A.m_Memory = B.m_Memory;
            A.m_Offset = (VkDeviceSize(R.offset) * vram_unit_v + Req.alignment - 1) / Req.alignment * Req.alignment;
            A.m_iBlock = i;
            A.m_Range  = R;
            ++B.m_nLive;
            XPROFILE_MEM_ALLOC("VRAM", std::uint64_t(i) + 1, A.m_Offset, Req.size, bImage ? "image" : "buffer");
            return true;
        };

        std::lock_guard Lk(m_VRAMLock);
        for( int i = 0; i < static_cast<int>(m_VRAMBlocks.size()); ++i ) if( Take(i) ) return A;

        const VkMemoryAllocateInfo Info{ .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = vram_block_v, .memoryTypeIndex = Type };
        VkDeviceMemory Memory;
        if( vkAllocateMemory(m_VKDevice, &Info, m_Instance->m_pVKAllocator, &Memory) ) return Dedicated();     // (no room for a block: the range alone)
        int iNew = 0;
        while( iNew < static_cast<int>(m_VRAMBlocks.size()) && m_VRAMBlocks[iNew].m_Memory ) ++iNew;
        if( iNew == static_cast<int>(m_VRAMBlocks.size()) ) m_VRAMBlocks.emplace_back();
        auto& B     = m_VRAMBlocks[iNew];
        B.m_Memory  = Memory;
        B.m_Type    = Type;
        B.m_bImage  = bImage;
        B.m_nLive   = 0;
        B.m_pAlloc  = std::make_unique<OffsetAllocator::Allocator>(static_cast<std::uint32_t>(vram_block_v / vram_unit_v));
        XPROFILE_MEM_REGION("VRAM", std::uint64_t(iNew) + 1, vram_block_v);      // (the profiler's memory map: block i is region i + 1)
        Take(iNew);
        return A;
    }

    //------------------------------------------------------------------------------------------------------------------------

    void device::FreeVRAM( vram_allocation& A ) noexcept
    {
        if( !A.isValid() ) return;
        if( A.m_iBlock < 0 )
        {
            XPROFILE_MEM_REGION("VRAM", reinterpret_cast<std::uint64_t>(A.m_Memory), 0);
            vkFreeMemory(m_VKDevice, A.m_Memory, m_Instance->m_pVKAllocator);
            --m_nVRAMDedicated;
        }
        else
        {
            std::lock_guard Lk(m_VRAMLock);
            auto& B = m_VRAMBlocks[A.m_iBlock];
            B.m_pAlloc->free(A.m_Range);
            XPROFILE_MEM_FREE("VRAM", std::uint64_t(A.m_iBlock) + 1, A.m_Offset);
            if( --B.m_nLive == 0 )
            {
                bool bOtherEmpty = false;
                for( const auto& O : m_VRAMBlocks ) bOtherEmpty |= &O != &B && O.m_Memory && O.m_nLive == 0 && O.m_Type == B.m_Type && O.m_bImage == B.m_bImage;
                if( bOtherEmpty )
                {
                    vkFreeMemory(m_VKDevice, B.m_Memory, m_Instance->m_pVKAllocator);
                    XPROFILE_MEM_REGION("VRAM", std::uint64_t(A.m_iBlock) + 1, 0);
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
