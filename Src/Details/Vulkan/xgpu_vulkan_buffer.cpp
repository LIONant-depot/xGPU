namespace xgpu::vulkan
{
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

        if( Setup.m_Type == xgpu::buffer::type::UNIFORM )
        {
            if( auto x = (Setup.m_EntryByteSize % m_Device->m_VKPhysicalDeviceProperties.limits.minUniformBufferOffsetAlignment); x != 0)
            {
                m_Device->m_Instance->ReportError(std::format("Uniform Buffer entry size does not meet the minimum Alignment requirements of {}", m_Device->m_VKPhysicalDeviceProperties.limits.minUniformBufferOffsetAlignment));
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Uniform Buffer entry size does not meet the minimum Alignment requirements");
            }
        }

        m_Type   = Setup.m_Type;
        m_Usage  = Setup.m_Usage;

        VkDeviceSize ByteSize = Setup.m_EntryByteSize * Setup.m_EntryCount;
        ByteSize = ((ByteSize - 1) / m_Device->m_BufferMemoryAlignment + 1) * m_Device->m_BufferMemoryAlignment;
        VkBufferCreateInfo BufferInfo
        {    .sType         = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO
         ,   .size          = ByteSize
         ,   .usage         = m_Usage == xgpu::buffer::setup::usage::GPU_READ
                              ? VK_BUFFER_USAGE_TRANSFER_SRC_BIT        // Use as staging buffer
                              : static_cast<VkBufferUsageFlags>(Setup.m_Type == xgpu::buffer::type::INDEX 
                                                                ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                                                                : Setup.m_Type == xgpu::buffer::type::VERTEX 
                                                                    ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                                                                    : VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT 
                                                               )
         ,   .sharingMode   = VK_SHARING_MODE_EXCLUSIVE
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
        m_Device->getMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, AllocInfo.memoryTypeIndex );

        if( auto VKErr = vkAllocateMemory(m_Device->m_VKDevice, &AllocInfo, m_Device->m_Instance->m_pVKAllocator, &m_VKBufferMemory); VKErr )
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
        m_EntrySizeBytes = Setup.m_EntryByteSize;

        //
        // If the user wants us to copy the data right away we can do that
        //
        if(Setup.m_pData)
        {
            assert(*Setup.m_pData);
            void* pDst;
            if( auto Err = MapLock(pDst, 0, m_nEntries); Err ) return Err;
            std::memcpy( pDst, *Setup.m_pData, m_nEntries*m_EntrySizeBytes );
            if (auto Err = MapUnlock(0, m_nEntries); Err ) return Err;
        }

        return nullptr;
    }

    //----------------------------------------------------------------------------------

    xgpu::device::error* buffer::MapLock( void*& pMemory, int StartIndex, int Count ) noexcept
    {
        assert(StartIndex < m_nEntries);
        assert((StartIndex+Count) <= m_nEntries);
        assert(m_ByteSize);

        if( auto VKErr = vkMapMemory
        ( m_Device->m_VKDevice
        , m_VKBufferMemory
        , StartIndex*m_EntrySizeBytes
        , Count == m_nEntries ? VK_WHOLE_SIZE : static_cast<VkDeviceSize>(m_EntrySizeBytes * Count)
        , 0
        , &pMemory 
        ); VKErr )
        {
            m_Device->m_Instance->ReportError(VKErr, "Fail to Map Memory from Buffer");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to Map Memory from Buffer");
        }

        return nullptr;
    }

    //----------------------------------------------------------------------------------

    xgpu::device::error* buffer::TransferToDestination( void ) noexcept
    {
        VkDeviceSize ByteSize = m_EntrySizeBytes * m_nEntries;
        ByteSize = ((ByteSize - 1) / m_Device->m_BufferMemoryAlignment + 1) * m_Device->m_BufferMemoryAlignment;
        VkBufferCreateInfo BufferInfo
        {    .sType         = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO
         ,   .size          = ByteSize
         ,   .usage         = static_cast<VkBufferUsageFlags>( ( m_Type == xgpu::buffer::type::INDEX 
                                                                    ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT 
                                                                    : m_Type == xgpu::buffer::type::VERTEX 
                                                                        ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                                                                        : VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
                                                               )
                                                               | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                                                             )
         ,   .sharingMode   = VK_SHARING_MODE_EXCLUSIVE
        };

        VkBuffer VKFinalBuffer;
        if (auto VKErr = vkCreateBuffer(m_Device->m_VKDevice, &BufferInfo, m_Device->m_Instance->m_pVKAllocator, &VKFinalBuffer); VKErr)
        {
            m_Device->m_Instance->ReportError(VKErr, "Fail to create a local Buffer");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create a local Buffer");
        }

        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(m_Device->m_VKDevice, m_VKBuffer, &req);

        m_Device->m_BufferMemoryAlignment = (m_Device->m_BufferMemoryAlignment > req.alignment) ? m_Device->m_BufferMemoryAlignment : req.alignment;
        VkMemoryAllocateInfo AllocInfo
        { .sType            = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
        , .allocationSize   = req.size
        , .memoryTypeIndex  = 0
        };
        m_Device->getMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, AllocInfo.memoryTypeIndex);

        VkDeviceMemory VKFinalBufferMemory;
        if (auto VKErr = vkAllocateMemory(m_Device->m_VKDevice, &AllocInfo, m_Device->m_Instance->m_pVKAllocator, &VKFinalBufferMemory); VKErr)
        {
            m_Device->m_Instance->ReportError(VKErr, "Fail to Allocate Memory for local Buffer");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to Allocate Memory for local Buffer");
        }

        if (auto VKErr = vkBindBufferMemory(m_Device->m_VKDevice, VKFinalBuffer, VKFinalBufferMemory, 0); VKErr)
        {
            m_Device->m_Instance->ReportError(VKErr, "Fail to Bind Memory for local Buffer");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to Bind Memory for local Buffer");
        }

        //
        // Transfer the memory
        //
        if (auto [PerDevice, Error] = m_Device->m_Instance->getLocalStorage().getOrCreatePerDevice(*m_Device); Error) return Error;
        else
        {
            auto CopyRegions = std::array
            {   VkBufferCopy
                {   .srcOffset  = 0
                ,   .dstOffset  = 0
                ,   .size       = ByteSize
                }
            };

            vkCmdCopyBuffer
            ( PerDevice.m_VKSetupCmdBuffer
            , m_VKBuffer
            , VKFinalBuffer
            , static_cast<std::uint32_t>(CopyRegions.size())
            , CopyRegions.data()
            );

            PerDevice.FlushVKSetupCommandBuffer();
        }

        //
        // Free the staging buffers and set the final ones
        //
        vkDestroyBuffer( m_Device->m_VKDevice, m_VKBuffer, m_Device->m_Instance->m_pVKAllocator );
        vkFreeMemory( m_Device->m_VKDevice, m_VKBufferMemory, m_Device->m_Instance->m_pVKAllocator );

        m_VKBufferMemory = VKFinalBufferMemory;
        m_VKBuffer       = VKFinalBuffer;

        // By setting this guy to zero we are saying that we should not need to touch this memory again
        m_ByteSize       = 0;

        return nullptr;
    }

    //----------------------------------------------------------------------------------

    xgpu::device::error* buffer::MapUnlock( int StartIndex, int Count ) noexcept
    {
        assert(StartIndex < m_nEntries);
        assert((StartIndex + Count) <= m_nEntries);

        auto Ranges = std::array
        {
            VkMappedMemoryRange
            {   .sType      = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE
            ,   .memory     = m_VKBufferMemory
            ,   .offset     = static_cast<VkDeviceSize>(StartIndex * m_EntrySizeBytes)
            ,   .size       = Count == m_nEntries ? VK_WHOLE_SIZE : static_cast<VkDeviceSize>(m_EntrySizeBytes * Count)
            }
        };

        if (auto VKErr = vkFlushMappedMemoryRanges(m_Device->m_VKDevice, static_cast<std::uint32_t>(Ranges.size()), Ranges.data() ); VKErr )
        {
            m_Device->m_Instance->ReportError(VKErr, "Fail to Map Memory from Buffer");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to Map Memory from Buffer");
        }

        vkUnmapMemory( m_Device->m_VKDevice, m_VKBufferMemory);

        if( m_Usage == xgpu::buffer::setup::usage::GPU_READ && m_ByteSize )
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

    xgpu::buffer::error* buffer::Resize( int NewEntryCount ) noexcept
    {
        assert( m_Usage != xgpu::buffer::setup::usage::GPU_READ );

        const xgpu::buffer::setup Setup
        { .m_Type           = m_Type
        , .m_Usage          = m_Usage
        , .m_EntryByteSize  = m_EntrySizeBytes
        , .m_EntryCount     = NewEntryCount
        };

        Destroy();

        return reinterpret_cast<xgpu::buffer::error*>(Create(Setup));
    }

    //----------------------------------------------------------------------------------

    buffer::~buffer(void) noexcept
    {
        Destroy();
    }

    //-------------------------------------------------------------------------------

    void* buffer::getUniformBufferVMem(std::uint32_t& DynamicOffset) noexcept
    {
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

        DynamicOffset = V * m_EntrySizeBytes;

        //
        // Make sure the memory is mapped
        //
        if(m_pVMapMemory==nullptr)
        {
            assert(m_Usage == xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ);

            void* pData;
            if (auto Err = MapLock(pData, 0, m_nEntries); Err)
                return Err;
            
            m_pVMapMemory = reinterpret_cast<std::byte*>(pData);
        }

        return &m_pVMapMemory[DynamicOffset];
    }


    //----------------------------------------------------------------------------------

    void buffer::Destroy(void) noexcept
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
            vkFreeMemory(m_Device->m_VKDevice, m_VKBufferMemory, m_Device->m_Instance->m_pVKAllocator );
            m_VKBufferMemory = VK_NULL_HANDLE;
        }
    }
}