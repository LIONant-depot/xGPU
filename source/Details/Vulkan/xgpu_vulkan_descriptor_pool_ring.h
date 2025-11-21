namespace xgpu
{
    struct descriptor_pool_ring
    {
        inline
        void Init(VkDevice VKDevice, VkDescriptorType VKType, const VkAllocationCallbacks* pCallbacks, uint32_t InitialSize = 1024) noexcept
        {
            m_VKDescriptorType  = VKType;
            m_MaxCapacity       = InitialSize;
            m_pCallBacks        = pCallbacks;
            m_Pools.push_back(createPool(VKDevice));
        }

        inline
        VkDescriptorSet alloc(VkDevice VKDevice, VkDescriptorSetLayout& Layout) noexcept
        {
            VkDescriptorSet set = VK_NULL_HANDLE;
            VkDescriptorSetAllocateInfo allocInfo
            { .sType                = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
            , .descriptorPool       = m_Pools[m_CurrentIndex].m_VKPool
            , .descriptorSetCount   = 1
            , .pSetLayouts          = &Layout
            };

            if (vkAllocateDescriptorSets(VKDevice, &allocInfo, &set) == VK_SUCCESS)
            {
                m_Pools[m_CurrentIndex].m_Used++;
                return set;
            }

            // Grow 1.5× instead of 2×
            m_MaxCapacity += m_MaxCapacity / 2;   // 1.5× growth
            m_Pools.push_back(createPool(VKDevice));
            m_CurrentIndex = static_cast<int>(m_Pools.size() - 1);

            // Retry on the new larger pool
            allocInfo.descriptorPool = m_Pools[m_CurrentIndex].m_VKPool;
            vkAllocateDescriptorSets(VKDevice, &allocInfo, &set);
            m_Pools[m_CurrentIndex].m_Used++;
            return set;
        }

        inline
        void AdvanceAndReset(VkDevice VKDevice) noexcept
        {
            vkResetDescriptorPool(VKDevice, m_Pools[m_CurrentIndex].m_VKPool, 0);
            m_Pools[m_CurrentIndex].m_Used = 0;
            m_CurrentIndex = (m_CurrentIndex + 1) % static_cast<int>(m_Pools.size());

            // Shrink old empty small m_Pools while we have >=3 total
            while (m_Pools.size() >= 3)
            {
                int Next = m_CurrentIndex;
                if (m_Pools[Next].m_Used == 0 && m_Pools[Next].m_Capacity < m_MaxCapacity)
                {
                    vkDestroyDescriptorPool(VKDevice, m_Pools[Next].m_VKPool, nullptr);
                    m_Pools.erase(m_Pools.begin() + Next);
                    if (Next < m_CurrentIndex) --m_CurrentIndex;
                }
                else break;
            }

            // Keep at least 3 m_Pools
            if (m_Pools.size() < 3)
            {
                int insertPos = (m_CurrentIndex + 1) % static_cast<int>(m_Pools.size() + 1);
                do{ m_Pools.insert(m_Pools.begin() + insertPos, createPool(VKDevice)); } while (m_Pools.size() < 3);
            }
        }

        inline
        void Destroy(VkDevice VKDevice) noexcept
        {
            for (auto& Entry : m_Pools)
            {
                if (Entry.m_VKPool != VK_NULL_HANDLE)
                {
                    vkDestroyDescriptorPool(VKDevice, Entry.m_VKPool, m_pCallBacks);
                    Entry.m_VKPool = VK_NULL_HANDLE;
                }
            }
            m_Pools.clear();
        }

    //protected:

        struct entry
        {
            VkDescriptorPool m_VKPool   = VK_NULL_HANDLE;
            std::uint32_t    m_Used     = 0;
            std::uint32_t    m_Capacity = 0;
        };

    //protected:

        entry createPool(VkDevice VKDevice) noexcept
        {
            VkDescriptorPoolSize PoolSize{ m_VKDescriptorType, m_MaxCapacity };
            VkDescriptorPoolCreateInfo Info
            { .sType            = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
            //, .flags            = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
            , .maxSets          = m_MaxCapacity
            , .poolSizeCount    = 1
            , .pPoolSizes       = &PoolSize
            };

            VkDescriptorPool pool = VK_NULL_HANDLE;
            vkCreateDescriptorPool(VKDevice, &Info, m_pCallBacks, &pool);

            return { pool, 0, m_MaxCapacity };
        }

    //protected:

        std::vector<entry>              m_Pools;
        const VkAllocationCallbacks*    m_pCallBacks     = nullptr;
        VkDescriptorType                m_VKDescriptorType;
        int                             m_CurrentIndex   = 0;
        std::uint32_t                   m_MaxCapacity    = 1024;
    };
}