namespace xgpu::vulkan
{
    //----------------------------------------------------------------------------------

    xgpu::device::error* timestamps::Initialize( std::shared_ptr<device>&& Device, const xgpu::timestamps::setup& Setup ) noexcept
    {
        assert(Setup.m_Count > 0);
        m_Device = Device;
        m_Count  = Setup.m_Count;

        if( m_Device->m_bHostQueryReset == false )
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Timestamps need the hostQueryReset feature, which this device does not support");

        std::uint32_t nFamilies = 0;
        vkGetPhysicalDeviceQueueFamilyProperties( m_Device->m_VKPhysicalDevice, &nFamilies, nullptr );
        std::vector<VkQueueFamilyProperties> Families(nFamilies);
        vkGetPhysicalDeviceQueueFamilyProperties( m_Device->m_VKPhysicalDevice, &nFamilies, Families.data() );

        const std::uint32_t ValidBits = Families[m_Device->m_MainQueueIndex].timestampValidBits;
        if( ValidBits == 0 )
            return VGPU_ERROR(xgpu::device::error::FAILURE, "The main queue does not support timestamps");

        m_TickMask  = ValidBits >= 64 ? ~0ull : ((1ull << ValidBits) - 1);
        m_NsPerTick = m_Device->m_VKPhysicalDeviceProperties.limits.timestampPeriod;

        VkQueryPoolCreateInfo CreateInfo
        { .sType        = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO
        , .queryType    = VK_QUERY_TYPE_TIMESTAMP
        , .queryCount   = static_cast<std::uint32_t>(m_Count * ring_size_v)
        };
        if( auto VKErr = vkCreateQueryPool( m_Device->m_VKDevice, &CreateInfo, m_Device->m_Instance->m_pVKAllocator, &m_VKQueryPool ); VKErr )
        {
            m_Device->m_Instance->ReportError(VKErr, "vkCreateQueryPool");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create the timestamp query pool");
        }

        // Queries must be reset before their first use
        vkResetQueryPool( m_Device->m_VKDevice, m_VKQueryPool, 0, CreateInfo.queryCount );
        m_Written.assign( CreateInfo.queryCount, 0 );
        m_iCurrent = 0;

        return nullptr;
    }

    //----------------------------------------------------------------------------------

    timestamps::~timestamps( void ) noexcept
    {
        // ponytail: destroyed immediately, safe because EndFrame waits for the GPU; route through the device's
        // DeathMarch once xGPU keeps frames in flight
        if( m_VKQueryPool ) vkDestroyQueryPool( m_Device->m_VKDevice, m_VKQueryPool, m_Device->m_Instance->m_pVKAllocator );
    }

    //----------------------------------------------------------------------------------

    void timestamps::Write( VkCommandBuffer CmdBuffer, int Index ) noexcept
    {
        assert( Index >= 0 && Index < m_Count );

        // A new frame takes the next set; unread results left in it are dropped
        const int Frame = m_Device->m_FrameIndex;
        if( m_Sets[m_iCurrent].m_Frame != Frame )
        {
            m_iCurrent = (m_iCurrent + 1) % ring_size_v;
            auto& Set  = m_Sets[m_iCurrent];
            if( Set.m_Frame != -1 )
            {
                vkResetQueryPool( m_Device->m_VKDevice, m_VKQueryPool, m_iCurrent * m_Count, m_Count );
                std::fill_n( &m_Written[m_iCurrent * m_Count], m_Count, std::uint8_t{0} );
            }
            Set.m_Frame = Frame;
        }

        const int Query = m_iCurrent * m_Count + Index;
        assert( m_Written[Query] == 0 && "Each timestamp index can be written once per frame" );
        m_Written[Query] = 1;

        vkCmdWriteTimestamp( CmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_VKQueryPool, static_cast<std::uint32_t>(Query) );
    }

    //----------------------------------------------------------------------------------

    bool timestamps::getResults( std::vector<std::uint64_t>& NanoSeconds ) noexcept
    {
        struct result { std::uint64_t m_Ticks, m_Available; };
        std::vector<result> Results(m_Count);
        bool                bFound = false;

        // Oldest set first, so the newest finished frame is what is left in NanoSeconds
        for( int k = 1; k <= ring_size_v; ++k )
        {
            const int i   = (m_iCurrent + k) % ring_size_v;
            auto&     Set = m_Sets[i];

            // Free, or still being recorded (its frame has not been flipped yet)
            if( Set.m_Frame == -1 || Set.m_Frame == m_Device->m_FrameIndex ) continue;

            // VK_NOT_READY is expected when part of the set is not done; availability says which
            (void)vkGetQueryPoolResults( m_Device->m_VKDevice, m_VKQueryPool, i * m_Count, m_Count
                                       , sizeof(result) * m_Count, Results.data(), sizeof(result)
                                       , VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT );

            const std::uint8_t* pWritten = &m_Written[i * m_Count];
            bool bReady = true;
            for( int q = 0; q < m_Count; ++q ) if( pWritten[q] && Results[q].m_Available == 0 ) { bReady = false; break; }
            if( bReady == false ) break;        // newer sets can't be done either

            NanoSeconds.resize(m_Count);
            for( int q = 0; q < m_Count; ++q )
                NanoSeconds[q] = pWritten[q] ? static_cast<std::uint64_t>( static_cast<double>(Results[q].m_Ticks & m_TickMask) * m_NsPerTick ) : ~0ull;

            vkResetQueryPool( m_Device->m_VKDevice, m_VKQueryPool, i * m_Count, m_Count );
            std::fill_n( &m_Written[i * m_Count], m_Count, std::uint8_t{0} );
            Set.m_Frame = -1;
            bFound      = true;
        }

        return bFound;
    }
}
