namespace xgpu
{
    // Descriptor sets for one frame, of one descriptor type: fixed-size pools allocated in order, all of them reset together
    // once the frame's fence has passed (AdvanceAndReset). A frame that needs more sets takes the next pool (created the first
    // time it is needed, then kept for the following frames); the number of pools is capped, so a frame that asks for more
    // sets than that gets VK_NULL_HANDLE and an error report - never unbounded growth.
    // (It used to grow each new pool by 1.5x and reset one pool per frame: the pools a big frame filled were never reset, so
    // every frame made a larger pool than the last - with ~100k sets per frame the driver stalled the whole machine.)
    struct descriptor_pool_ring
    {
        static constexpr std::uint32_t sets_per_pool_v         = 4096;
        static constexpr std::uint32_t descriptors_per_set_v   = 8;       // a set of the static / dynamic layouts holds at most this many
        static constexpr std::size_t   max_pools_v             = 64;      // 262144 sets per frame and type

        inline
        void Init(VkDevice VKDevice, VkDescriptorType VKType, const VkAllocationCallbacks* pCallbacks, std::uint32_t = 0) noexcept
        {
            m_VKDescriptorType  = VKType;
            m_pCallBacks        = pCallbacks;
            m_Pools.push_back(createPool(VKDevice));
        }

        // VK_NULL_HANDLE when the frame used every pool (the caller reports it)
        inline
        VkDescriptorSet alloc(VkDevice VKDevice, VkDescriptorSetLayout& Layout) noexcept
        {
            while (true)
            {
                if (m_Current == m_Pools.size())
                {
                    if (m_Pools.size() >= max_pools_v) return VK_NULL_HANDLE;
                    m_Pools.push_back(createPool(VKDevice));
                    if (m_Pools.back() == VK_NULL_HANDLE) { m_Pools.pop_back(); return VK_NULL_HANDLE; }
                }

                VkDescriptorSet             Set = VK_NULL_HANDLE;
                VkDescriptorSetAllocateInfo AllocInfo
                { .sType                = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
                , .descriptorPool       = m_Pools[m_Current]
                , .descriptorSetCount   = 1
                , .pSetLayouts          = &Layout
                };
                if (vkAllocateDescriptorSets(VKDevice, &AllocInfo, &Set) == VK_SUCCESS) return Set;
                ++m_Current;            // this pool is full: the next one
            }
        }

        // After the frame's fence: every pool this frame used is free again
        inline
        void AdvanceAndReset(VkDevice VKDevice) noexcept
        {
            for (std::size_t i = 0; i <= m_Current && i < m_Pools.size(); ++i) vkResetDescriptorPool(VKDevice, m_Pools[i], 0);
            m_Current = 0;
        }

        inline
        void Destroy(VkDevice VKDevice) noexcept
        {
            for (auto& Pool : m_Pools) if (Pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(VKDevice, Pool, m_pCallBacks);
            m_Pools.clear();
            m_Current = 0;
        }

        inline
        VkDescriptorPool createPool(VkDevice VKDevice) noexcept
        {
            VkDescriptorPoolSize PoolSize{ m_VKDescriptorType, sets_per_pool_v * descriptors_per_set_v };
            VkDescriptorPoolCreateInfo Info
            { .sType            = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
            , .maxSets          = sets_per_pool_v
            , .poolSizeCount    = 1
            , .pPoolSizes       = &PoolSize
            };
            VkDescriptorPool Pool = VK_NULL_HANDLE;
            if (vkCreateDescriptorPool(VKDevice, &Info, m_pCallBacks, &Pool) != VK_SUCCESS) return VK_NULL_HANDLE;
            return Pool;
        }

        std::vector<VkDescriptorPool>   m_Pools;
        const VkAllocationCallbacks*    m_pCallBacks        = nullptr;
        VkDescriptorType                m_VKDescriptorType  {};
        std::size_t                     m_Current           = 0;
    };
}
