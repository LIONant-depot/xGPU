namespace xgpu::vulkan
{
    //------------------------------------------------------------------------------------------------------------------------

    xgpu::device::error* upload_queue::Init( device& Device ) noexcept
    {
        m_pDevice    = &Device;
        m_bOwnFamily = Device.m_TransferQueueIndex != Device.m_MainQueueIndex;
        auto* pAlloc = Device.m_Instance->m_pVKAllocator;

        for( auto* pSem : { &m_Timeline, &m_AcquireTimeline } )
        {
            const VkSemaphoreTypeCreateInfo Type{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE, .initialValue = 0 };
            const VkSemaphoreCreateInfo     Info{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &Type };
            if( auto VKErr = vkCreateSemaphore(Device.m_VKDevice, &Info, pAlloc, pSem); VKErr )
            {
                Device.m_Instance->ReportError(VKErr, "Fail to create the upload queue's timeline semaphores");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create the upload queue's timeline semaphores");
            }
        }

        for( auto [pPool, Family] : { std::pair{ &m_TransferPool, Device.m_TransferQueueIndex }, std::pair{ &m_AcquirePool, Device.m_MainQueueIndex } } )
        {
            const VkCommandPoolCreateInfo Info{ .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = Family };
            if( auto VKErr = vkCreateCommandPool(Device.m_VKDevice, &Info, pAlloc, pPool); VKErr )
            {
                Device.m_Instance->ReportError(VKErr, "Fail to create the upload queue's command pools");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create the upload queue's command pools");
            }
        }
        for( auto& B : m_Batches )
        {
            const VkCommandBufferAllocateInfo T{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = m_TransferPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
            const VkCommandBufferAllocateInfo A{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = m_AcquirePool,  .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
            if( vkAllocateCommandBuffers(Device.m_VKDevice, &T, &B.m_Transfer) || vkAllocateCommandBuffers(Device.m_VKDevice, &A, &B.m_Acquire) )
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to allocate the upload queue's command buffers");
        }

        // The staging ring: host visible and coherent, mapped for good (XGPU_UPLOAD_RING_MB: its size, 64 MB by default)
        const char* pMB = std::getenv("XGPU_UPLOAD_RING_MB");
        m_RingSize = std::uint64_t(pMB ? std::max(1, std::atoi(pMB)) : 64) << 20;
        const VkBufferCreateInfo BufferInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = m_RingSize, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
        if( auto VKErr = vkCreateBuffer(Device.m_VKDevice, &BufferInfo, pAlloc, &m_Ring); VKErr )
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create the upload ring");
        VkMemoryRequirements Req;
        vkGetBufferMemoryRequirements(Device.m_VKDevice, m_Ring, &Req);
        VkMemoryAllocateInfo AllocInfo{ .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = Req.size };
        Device.getMemoryType(Req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, AllocInfo.memoryTypeIndex);
        if( vkAllocateMemory(Device.m_VKDevice, &AllocInfo, pAlloc, &m_RingMemory) || vkBindBufferMemory(Device.m_VKDevice, m_Ring, m_RingMemory, 0)
         || vkMapMemory(Device.m_VKDevice, m_RingMemory, 0, m_RingSize, 0, reinterpret_cast<void**>(&m_pRing)) )
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to allocate the upload ring");
        return nullptr;
    }

    //------------------------------------------------------------------------------------------------------------------------

    void upload_queue::Destroy( void ) noexcept
    {
        if( m_pDevice == nullptr ) return;
        auto  VKDevice = m_pDevice->m_VKDevice;
        auto* pAlloc   = m_pDevice->m_Instance->m_pVKAllocator;
        for( auto& B : m_Batches ) for( auto& [Buf, Mem] : B.m_Free ) { vkDestroyBuffer(VKDevice, Buf, pAlloc); vkFreeMemory(VKDevice, Mem, pAlloc); }
        vkDestroyBuffer(VKDevice, m_Ring, pAlloc);
        vkFreeMemory(VKDevice, m_RingMemory, pAlloc);
        vkDestroyCommandPool(VKDevice, m_TransferPool, pAlloc);
        vkDestroyCommandPool(VKDevice, m_AcquirePool, pAlloc);
        vkDestroySemaphore(VKDevice, m_Timeline, pAlloc);
        vkDestroySemaphore(VKDevice, m_AcquireTimeline, pAlloc);
        m_pDevice = nullptr;
    }

    //------------------------------------------------------------------------------------------------------------------------

    // The finished batches, oldest first: their transfer done and, when they release to the main family, their acquire run too
    void upload_queue::Retire( void ) noexcept
    {
        std::uint64_t Done = 0, AcquireDone = 0;
        vkGetSemaphoreCounterValue(m_pDevice->m_VKDevice, m_Timeline, &Done);
        vkGetSemaphoreCounterValue(m_pDevice->m_VKDevice, m_AcquireTimeline, &AcquireDone);
        while( !m_InFlight.empty() )
        {
            auto& B = m_Batches[m_InFlight.front()];
            if( B.m_Value > Done ) break;
            if( B.m_bAcquires && (B.m_AcquireValue == 0 || B.m_AcquireValue > AcquireDone) ) break;
            for( auto& [Buf, Mem] : B.m_Free ) { vkDestroyBuffer(m_pDevice->m_VKDevice, Buf, m_pDevice->m_Instance->m_pVKAllocator); vkFreeMemory(m_pDevice->m_VKDevice, Mem, m_pDevice->m_Instance->m_pVKAllocator); }
            B.m_Free.clear();
            m_Tail          = B.m_RingEnd;
            B.m_Value       = 0;
            B.m_AcquireValue= 0;
            B.m_bAcquires   = false;
            B.m_nCommands   = 0;
            m_InFlight.pop_front();
        }
    }

    //------------------------------------------------------------------------------------------------------------------------

    // The oldest batch finished (a wait on the transfer side, never on the main queue); its acquire submitted on its own if nothing took it
    static void WaitOldest( upload_queue& Q ) noexcept
    {
        auto& B = Q.m_Batches[Q.m_InFlight.front()];
        const VkSemaphoreWaitInfo Wait{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, .semaphoreCount = 1, .pSemaphores = &Q.m_Timeline, .pValues = &B.m_Value };
        vkWaitSemaphores(Q.m_pDevice->m_VKDevice, &Wait, UINT64_MAX);
        if( B.m_bAcquires )
        {
            if( B.m_AcquireValue == 0 ) Q.SubmitAcquiresAlone();
            const VkSemaphoreWaitInfo AWait{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, .semaphoreCount = 1, .pSemaphores = &Q.m_AcquireTimeline, .pValues = &B.m_AcquireValue };
            vkWaitSemaphores(Q.m_pDevice->m_VKDevice, &AWait, UINT64_MAX);
        }
        Q.Retire();
    }

    //------------------------------------------------------------------------------------------------------------------------

    upload_queue::batch* upload_queue::Open( void ) noexcept
    {
        if( m_iOpen >= 0 ) return &m_Batches[m_iOpen];
        for(;;)
        {
            Retire();
            for( int i = 0; i < batches_v; ++i )
                if( m_Batches[i].m_Value == 0 && std::ranges::find(m_InFlight, i) == m_InFlight.end() ) { m_iOpen = i; break; }
            if( m_iOpen >= 0 ) break;
            WaitOldest(*this);
        }
        auto& B = m_Batches[m_iOpen];
        const VkCommandBufferBeginInfo Begin{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
        vkResetCommandBuffer(B.m_Transfer, 0);
        vkBeginCommandBuffer(B.m_Transfer, &Begin);
        if( m_bOwnFamily ) { vkResetCommandBuffer(B.m_Acquire, 0); vkBeginCommandBuffer(B.m_Acquire, &Begin); }
        B.m_bOpen = true;
        return &B;
    }

    //------------------------------------------------------------------------------------------------------------------------

    // Bytes of staging: the ring (wrapping to its start when they do not fit before its end; the oldest batches waited for when it is
    // full), or a buffer of their own past half of it
    xgpu::device::error* upload_queue::Reserve( std::size_t Bytes, staging& Out ) noexcept
    {
        const std::uint64_t Size = (std::uint64_t(Bytes) + 15) & ~std::uint64_t(15);
        if( Size > m_RingSize / 2 )
        {
            auto* pAlloc = m_pDevice->m_Instance->m_pVKAllocator;
            const VkBufferCreateInfo Info{ .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = Size, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
            VkBuffer Buffer;
            if( vkCreateBuffer(m_pDevice->m_VKDevice, &Info, pAlloc, &Buffer) ) return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create an upload's own staging buffer");
            VkMemoryRequirements Req;
            vkGetBufferMemoryRequirements(m_pDevice->m_VKDevice, Buffer, &Req);
            VkMemoryAllocateInfo Alloc{ .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = Req.size };
            m_pDevice->getMemoryType(Req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, Alloc.memoryTypeIndex);
            VkDeviceMemory Memory;
            if( vkAllocateMemory(m_pDevice->m_VKDevice, &Alloc, pAlloc, &Memory) ) { vkDestroyBuffer(m_pDevice->m_VKDevice, Buffer, pAlloc); return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to allocate an upload's own staging buffer"); }
            vkBindBufferMemory(m_pDevice->m_VKDevice, Buffer, Memory, 0);
            vkMapMemory(m_pDevice->m_VKDevice, Memory, 0, Size, 0, reinterpret_cast<void**>(&Out.m_pData));
            Out.m_Buffer = Buffer;
            Out.m_Offset = 0;
            Open()->m_Free.push_back({ Buffer, Memory });
            ++m_nOversized;
            return nullptr;
        }
        for(;;)
        {
            const std::uint64_t At  = m_Head % m_RingSize;
            const std::uint64_t Pad = At + Size > m_RingSize ? m_RingSize - At : 0;
            if( (m_Head - m_Tail) + Pad + Size <= m_RingSize )
            {
                m_Head      += Pad;
                m_nWraps    += Pad ? 1 : 0;
                Out.m_Offset = m_Head % m_RingSize;
                Out.m_pData  = m_pRing + Out.m_Offset;
                Out.m_Buffer = m_Ring;
                m_Head      += Size;
                return nullptr;
            }
            // Full: what is recorded goes, then the oldest batch is waited for (its range comes back)
            if( m_iOpen >= 0 && m_Batches[m_iOpen].m_nCommands ) FlushLocked();
            Retire();
            if( m_InFlight.empty() ) { m_Tail = m_Head; continue; }     // (nothing holds the ring: it is all free)
            WaitOldest(*this);
        }
    }

    //------------------------------------------------------------------------------------------------------------------------

    xgpu::device::error* upload_queue::UploadImage( VkImage Image, VkImageSubresourceRange Range, std::span<const std::byte> Data, std::span<VkBufferImageCopy> Regions ) noexcept
    {
        std::lock_guard Lk(m_Lock);
        staging S;
        if( auto Err = Reserve(Data.size(), S); Err ) return Err;
        std::memcpy(S.m_pData, Data.data(), Data.size());
        for( auto& R : Regions ) R.bufferOffset += S.m_Offset;

        auto& B = *Open();
        const auto Barrier = [&](VkCommandBuffer Cmd, const VkImageMemoryBarrier2& IB)
        {
            const VkDependencyInfo Dep{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &IB };
            vkCmdPipelineBarrier2(Cmd, &Dep);
        };
        Barrier(B.m_Transfer, { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_NONE, .srcAccessMask = VK_ACCESS_2_NONE
                              , .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT, .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT
                              , .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                              , .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = Image, .subresourceRange = Range });
        vkCmdCopyBufferToImage(B.m_Transfer, S.m_Buffer, Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<std::uint32_t>(Regions.size()), Regions.data());

        // To SHADER_READ_ONLY for the main family: released here and acquired by the main queue (another family), or in place (one family)
        const std::uint32_t Src = m_bOwnFamily ? m_pDevice->m_TransferQueueIndex : VK_QUEUE_FAMILY_IGNORED;
        const std::uint32_t Dst = m_bOwnFamily ? m_pDevice->m_MainQueueIndex     : VK_QUEUE_FAMILY_IGNORED;
        Barrier(B.m_Transfer, { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT, .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT
                              , .dstStageMask = m_bOwnFamily ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .dstAccessMask = m_bOwnFamily ? VK_ACCESS_2_NONE : VK_ACCESS_2_MEMORY_READ_BIT
                              , .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                              , .srcQueueFamilyIndex = Src, .dstQueueFamilyIndex = Dst, .image = Image, .subresourceRange = Range });
        if( m_bOwnFamily )
        {
            Barrier(B.m_Acquire, { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_NONE, .srcAccessMask = VK_ACCESS_2_NONE
                                 , .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT
                                 , .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                 , .srcQueueFamilyIndex = Src, .dstQueueFamilyIndex = Dst, .image = Image, .subresourceRange = Range });
            B.m_bAcquires = true;
        }
        ++B.m_nCommands;
        ++m_nUploads;
        return nullptr;
    }

    //------------------------------------------------------------------------------------------------------------------------

    xgpu::device::error* upload_queue::UploadBuffer( VkBuffer Staging, VkDeviceMemory StagingMemory, VkBuffer Dst, VkDeviceSize Bytes ) noexcept
    {
        std::lock_guard Lk(m_Lock);
        auto& B = *Open();
        const VkBufferCopy Region{ .size = Bytes };
        vkCmdCopyBuffer(B.m_Transfer, Staging, Dst, 1, &Region);
        B.m_Free.push_back({ Staging, StagingMemory });

        const std::uint32_t Src = m_bOwnFamily ? m_pDevice->m_TransferQueueIndex : VK_QUEUE_FAMILY_IGNORED;
        const std::uint32_t DstQ= m_bOwnFamily ? m_pDevice->m_MainQueueIndex     : VK_QUEUE_FAMILY_IGNORED;
        const auto Barrier = [&](VkCommandBuffer Cmd, const VkBufferMemoryBarrier2& BB)
        {
            const VkDependencyInfo Dep{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &BB };
            vkCmdPipelineBarrier2(Cmd, &Dep);
        };
        if( m_bOwnFamily )
        {
            Barrier(B.m_Transfer, { .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT, .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT
                                  , .srcQueueFamilyIndex = Src, .dstQueueFamilyIndex = DstQ, .buffer = Dst, .size = VK_WHOLE_SIZE });
            Barrier(B.m_Acquire,  { .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2, .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT
                                  , .srcQueueFamilyIndex = Src, .dstQueueFamilyIndex = DstQ, .buffer = Dst, .size = VK_WHOLE_SIZE });
            B.m_bAcquires = true;
        }
        ++B.m_nCommands;
        ++m_nUploads;
        return nullptr;
    }

    //------------------------------------------------------------------------------------------------------------------------

    std::uint64_t upload_queue::FlushLocked( void ) noexcept
    {
        if( m_iOpen < 0 || m_Batches[m_iOpen].m_nCommands == 0 ) return m_Flushed;
        auto& B = m_Batches[m_iOpen];
        vkEndCommandBuffer(B.m_Transfer);
        if( m_bOwnFamily ) vkEndCommandBuffer(B.m_Acquire);
        B.m_bOpen   = false;
        B.m_Value   = ++m_Flushed;
        B.m_RingEnd = m_Head;

        const VkCommandBufferSubmitInfo Cmd   { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = B.m_Transfer };
        const VkSemaphoreSubmitInfo     Signal{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = m_Timeline, .value = B.m_Value, .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT };
        const VkSubmitInfo2             Submit{ .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2, .commandBufferInfoCount = 1, .pCommandBufferInfos = &Cmd, .signalSemaphoreInfoCount = 1, .pSignalSemaphoreInfos = &Signal };
        auto& Queue = m_bOwnFamily ? m_pDevice->m_VKTransferQueue : m_pDevice->m_VKMainQueue;      // (one family: the same VkQueue, under the main queue's lock)
        {
            std::lock_guard Lk(Queue);
            if( auto VKErr = vkQueueSubmit2(Queue.get(), 1, &Submit, VK_NULL_HANDLE); VKErr ) m_pDevice->m_Instance->ReportError(VKErr, "Fail to submit an upload batch");
        }
        m_InFlight.push_back(m_iOpen);
        if( B.m_bAcquires ) m_Pending.push_back(m_iOpen);
        m_iOpen = -1;
        return m_Flushed;
    }

    std::uint64_t upload_queue::Flush( void ) noexcept
    {
        std::lock_guard Lk(m_Lock);
        return FlushLocked();
    }

    //------------------------------------------------------------------------------------------------------------------------

    void upload_queue::TakeAcquires( std::vector<VkCommandBuffer>& CmdBuffers, std::uint64_t& WaitValue, std::uint64_t& AcquireSignal ) noexcept
    {
        std::lock_guard Lk(m_Lock);
        WaitValue     = FlushLocked();
        AcquireSignal = 0;
        if( m_Pending.empty() ) return;
        AcquireSignal = ++m_Acquired;
        for( int i : m_Pending ) { CmdBuffers.push_back(m_Batches[i].m_Acquire); m_Batches[i].m_AcquireValue = AcquireSignal; }
        m_Pending.clear();
    }

    //------------------------------------------------------------------------------------------------------------------------

    void upload_queue::SubmitAcquiresAlone( void ) noexcept
    {
        std::vector<VkCommandBufferSubmitInfo> Cmds;
        const std::uint64_t A = ++m_Acquired;
        for( int i : m_Pending ) { Cmds.push_back({ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = m_Batches[i].m_Acquire }); m_Batches[i].m_AcquireValue = A; }
        m_Pending.clear();
        const VkSemaphoreSubmitInfo Wait  { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = m_Timeline, .value = m_Flushed, .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT };
        const VkSemaphoreSubmitInfo Signal{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = m_AcquireTimeline, .value = A, .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT };
        const VkSubmitInfo2 Submit{ .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2, .waitSemaphoreInfoCount = 1, .pWaitSemaphoreInfos = &Wait
                                  , .commandBufferInfoCount = static_cast<std::uint32_t>(Cmds.size()), .pCommandBufferInfos = Cmds.data()
                                  , .signalSemaphoreInfoCount = 1, .pSignalSemaphoreInfos = &Signal };
        std::lock_guard Lk(m_pDevice->m_VKMainQueue);
        if( auto VKErr = vkQueueSubmit2(m_pDevice->m_VKMainQueue.get(), 1, &Submit, VK_NULL_HANDLE); VKErr ) m_pDevice->m_Instance->ReportError(VKErr, "Fail to submit the uploads' acquires");
    }

    //------------------------------------------------------------------------------------------------------------------------

    bool upload_queue::isDone( std::uint64_t Value ) noexcept
    {
        std::uint64_t Done = 0;
        vkGetSemaphoreCounterValue(m_pDevice->m_VKDevice, m_Timeline, &Done);
        return Done >= Value;
    }

    void upload_queue::Wait( std::uint64_t Value ) noexcept
    {
        const VkSemaphoreWaitInfo Info{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, .semaphoreCount = 1, .pSemaphores = &m_Timeline, .pValues = &Value };
        vkWaitSemaphores(m_pDevice->m_VKDevice, &Info, UINT64_MAX);
    }
}
