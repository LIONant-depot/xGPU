namespace xgpu::vulkan
{
    //-----------------------------------------------------------------------------------------

    xgpu::device::error* local_storage::per_device::FlushVKSetupCommandBuffer(void) noexcept
    {
        if( m_VKSetupCmdBuffer == VK_NULL_HANDLE)
            return nullptr;

        if( auto VKErr = vkEndCommandBuffer(m_VKSetupCmdBuffer); VKErr )
        {
            m_pDevice->m_Instance->ReportError( VKErr, "Failed to end the per thread command buffer" );
            return VGPU_ERROR( xgpu::device::error::FAILURE, "Failed to end the per thread command buffer" );
        }

        VkSubmitInfo SubmitInfo
        {
            .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO
        ,   .commandBufferCount = 1
        ,   .pCommandBuffers    = &m_VKSetupCmdBuffer
        };        

        //
        // Submit to queue and wait (note that only one thread at a time can do this)
        //
        {
            std::lock_guard Lk(m_pDevice->m_VKMainQueue);
            if( auto VKErr = vkQueueSubmit(m_pDevice->m_VKMainQueue.get(), 1, &SubmitInfo, VK_NULL_HANDLE); VKErr )
            {
                m_pDevice->m_Instance->ReportError(VKErr, "Fail to submit the per thread command buffer");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to submit the per thread command buffer");
            }

            // TODO: This may waste a bit of CPU cycles...
            if( auto VKErr = vkQueueWaitIdle(m_pDevice->m_VKMainQueue.get()); VKErr )
            {
                m_pDevice->m_Instance->ReportError(VKErr, "Fail to wait for idle state in queue, location per thread command buffer" );
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to wait for idle state in queue, location per thread command buffer" );
            }
        }

        return CreateVKSetupCommandBuffer();
    }

    //-----------------------------------------------------------------------------------------

    xgpu::device::error* local_storage::per_device::CreateVKSetupCommandBuffer( void ) noexcept
    {
        if( m_VKSetupCmdBuffer != VK_NULL_HANDLE )
        {
            vkFreeCommandBuffers( m_pDevice->m_VKDevice
                                , m_VKCmdPool
                                , 1
                                , &m_VKSetupCmdBuffer
                                );
            m_VKSetupCmdBuffer = VK_NULL_HANDLE;
        }

        VkCommandBufferAllocateInfo CommandBufferAllocateInfo
        {
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
        ,   .commandPool        = m_VKCmdPool
        ,   .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY
        ,   .commandBufferCount = 1
        };

        if( auto VKErr = vkAllocateCommandBuffers( m_pDevice->m_VKDevice
                                                 , &CommandBufferAllocateInfo
                                                 , &m_VKSetupCmdBuffer
                                                 ); VKErr )
        {
            m_pDevice->m_Instance->ReportError(VKErr, "Fail to allocate the setup command buffer, location per thread command buffer");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to allocate the setup command buffer, location per thread command buffer");
        }

        // todo : Command buffer is also started here, better put somewhere else
        // todo : Check if necessary at all...
        VkCommandBufferBeginInfo CommandBufferBeginInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        
        if( auto VKErr = vkBeginCommandBuffer(m_VKSetupCmdBuffer, &CommandBufferBeginInfo);  VKErr )
        {
            m_pDevice->m_Instance->ReportError(VKErr, "Fail to Beging the setup command buffer, location per thread command buffer");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to Beging the setup command buffer, location per thread command buffer");
        }

        return nullptr;
    }

    //-----------------------------------------------------------------------------------------

    std::tuple<local_storage::per_device&, xgpu::device::error*> local_storage::getOrCreatePerDevice(xgpu::vulkan::device& Device) noexcept
    {
        for( int i=0; i< m_nDevices; ++i )
        {
            if(m_PerDevice[i].m_pDevice == &Device ) return { m_PerDevice[i], nullptr };
        }

        auto& Entry     = m_PerDevice[m_nDevices++];
        Entry.m_pDevice = &Device;

        VkCommandPoolCreateInfo CreateInfo
        {
            .sType              = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO
        ,   .flags              = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
        ,   .queueFamilyIndex   = Device.m_MainQueueIndex
        };
        if (auto VKErr = vkCreateCommandPool( Device.m_VKDevice
                                            , &CreateInfo
                                            , Device.m_Instance->m_pVKAllocator
                                            , &Entry.m_VKCmdPool
                                            ); VKErr )
        {
            Device.m_Instance->ReportError(VKErr, "Unable to create a Frame's Command Pool");
            return { Entry, VGPU_ERROR(xgpu::device::error::FAILURE, "Unable to create a Frame's Command Pool") };
        }

        return { Entry, Entry.CreateVKSetupCommandBuffer() };
    }

}
