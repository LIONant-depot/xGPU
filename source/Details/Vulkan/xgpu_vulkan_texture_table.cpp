namespace xgpu::vulkan
{
    //----------------------------------------------------------------------------------------------------------
    // The device's texture heap (xgpu_vulkan_device.h texture_heap)
    //----------------------------------------------------------------------------------------------------------

    xgpu::device::error* device::InitHeap( void ) noexcept
    {
        auto& H = m_Heap;
        H.m_Textures.resize(1);         // id 0 is never valid
        H.m_Samplers.resize(1, VK_NULL_HANDLE);
        if( m_bBindless == false ) return nullptr;

        // As many as the device lets one stage see, at most 64K textures (two bindings share the ids: 2D and cube) and 1K samplers
        VkPhysicalDeviceVulkan12Properties P12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES };
        VkPhysicalDeviceProperties2        P{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &P12 };
        vkGetPhysicalDeviceProperties2(m_VKPhysicalDevice, &P);
        H.m_nTextures = std::min({ 65536u, P12.maxPerStageDescriptorUpdateAfterBindSampledImages / 2, P12.maxDescriptorSetUpdateAfterBindSampledImages / 2 });
        H.m_nSamplers = std::min({ 1024u, P12.maxPerStageDescriptorUpdateAfterBindSamplers, P12.maxDescriptorSetUpdateAfterBindSamplers });

        constexpr VkShaderStageFlags Stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        const auto Bindings = std::array
        { VkDescriptorSetLayoutBinding{ .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = H.m_nTextures, .stageFlags = Stages }
        , VkDescriptorSetLayoutBinding{ .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = H.m_nTextures, .stageFlags = Stages }
        , VkDescriptorSetLayoutBinding{ .binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,       .descriptorCount = H.m_nSamplers, .stageFlags = Stages }
        };
        constexpr VkDescriptorBindingFlags Flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
        const auto BindingFlags = std::array{ Flags, Flags, Flags };
        const VkDescriptorSetLayoutBindingFlagsCreateInfo FlagsInfo{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO
                                                                    , .bindingCount = static_cast<std::uint32_t>(BindingFlags.size()), .pBindingFlags = BindingFlags.data() };
        const VkDescriptorSetLayoutCreateInfo LayoutInfo{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .pNext = &FlagsInfo
                                                        , .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT
                                                        , .bindingCount = static_cast<std::uint32_t>(Bindings.size()), .pBindings = Bindings.data() };
        if( auto VKErr = vkCreateDescriptorSetLayout(m_VKDevice, &LayoutInfo, m_Instance->m_pVKAllocator, &H.m_Layout); VKErr )
        {
            m_Instance->ReportError(VKErr, "Fail to create the bindless heap's set layout");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create the bindless heap's set layout");
        }

        const auto PoolSizes = std::array
        { VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2 * H.m_nTextures }
        , VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER,       H.m_nSamplers }
        };
        const VkDescriptorPoolCreateInfo PoolInfo{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT
                                                 , .maxSets = 1, .poolSizeCount = static_cast<std::uint32_t>(PoolSizes.size()), .pPoolSizes = PoolSizes.data() };
        if( auto VKErr = vkCreateDescriptorPool(m_VKDevice, &PoolInfo, m_Instance->m_pVKAllocator, &H.m_Pool); VKErr )
        {
            m_Instance->ReportError(VKErr, "Fail to create the bindless heap's pool");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create the bindless heap's pool");
        }
        const VkDescriptorSetAllocateInfo Alloc{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = H.m_Pool, .descriptorSetCount = 1, .pSetLayouts = &H.m_Layout };
        if( auto VKErr = vkAllocateDescriptorSets(m_VKDevice, &Alloc, &H.m_Set); VKErr )
        {
            m_Instance->ReportError(VKErr, "Fail to allocate the bindless heap's set");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to allocate the bindless heap's set");
        }
        return nullptr;
    }

    //----------------------------------------------------------------------------------------------------------

    // A new texture's id (its slot in the heap's arrays, written now on a bindless device). A freed id is taken again only after the
    // texture that had it was destroyed, which the device's death march already delays past the frames that could read it
    std::uint32_t device::AllocTextureID( const std::shared_ptr<vulkan::texture>& T ) noexcept
    {
        auto& H = m_Heap;
        std::uint32_t ID;
        {
            std::lock_guard Lk(H.m_Lock);
            if( H.m_Free.empty() == false ) { ID = H.m_Free.back(); H.m_Free.pop_back(); }
            else
            {
                ID = static_cast<std::uint32_t>(H.m_Textures.size());
                if( m_bBindless && ID >= H.m_nTextures )
                {
                    m_Instance->ReportError("xGPU: the bindless texture heap is full; the texture gets no id");
                    return 0;
                }
                H.m_Textures.emplace_back();
            }
            H.m_Textures[ID] = T;
        }
        if( m_bBindless )
        {
            const VkWriteDescriptorSet Write{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = H.m_Set, .dstBinding = T->m_bCubeMap ? 1u : 0u
                                            , .dstArrayElement = ID, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                            , .pImageInfo = &T->m_VKDescriptorImageInfo };
            vkUpdateDescriptorSets(m_VKDevice, 1, &Write, 0, nullptr);
        }
        return ID;
    }

    //----------------------------------------------------------------------------------------------------------

    void device::FreeTextureID( std::uint32_t ID ) noexcept
    {
        if( ID == 0 ) return;
        std::lock_guard Lk(m_Heap.m_Lock);
        m_Heap.m_Textures[ID].reset();
        m_Heap.m_Free.push_back(ID);        // the heap keeps the old descriptor until the id is written again: partially bound, never read
    }

    //----------------------------------------------------------------------------------------------------------

    std::shared_ptr<vulkan::texture> device::getTexture( std::uint32_t ID ) noexcept
    {
        std::lock_guard Lk(m_Heap.m_Lock);
        return ID < m_Heap.m_Textures.size() ? m_Heap.m_Textures[ID].lock() : nullptr;
    }

    //----------------------------------------------------------------------------------------------------------

    // One sampler per distinct description, for the device's lifetime (a handful in practice)
    xgpu::sampler_id device::getSampler( const xgpu::pipeline::sampler& D ) noexcept
    {
        const float Aniso = D.m_bDisableAnisotropy || D.m_MaxAnisotropy <= 0 ? 0.0f : std::min(D.m_MaxAnisotropy, m_VKPhysicalDeviceProperties.limits.maxSamplerAnisotropy);
        const std::uint64_t Key = (std::uint64_t(std::bit_cast<std::uint32_t>(Aniso)) << 32)
                                | (std::uint64_t(D.m_AddressMode[0]) << 16) | (std::uint64_t(D.m_AddressMode[1]) << 20) | (std::uint64_t(D.m_AddressMode[2]) << 24)
                                | (std::uint64_t(D.m_MipmapMag) << 8) | (std::uint64_t(D.m_MipmapMin) << 10) | (std::uint64_t(D.m_MipmapMode) << 12);
        auto& H = m_Heap;
        std::lock_guard Lk(H.m_Lock);
        if( auto It = H.m_SamplerIDs.find(Key); It != H.m_SamplerIDs.end() ) return { It->second };

        const auto ID = static_cast<std::uint32_t>(H.m_Samplers.size());
        if( m_bBindless && ID >= H.m_nSamplers )
        {
            m_Instance->ReportError("xGPU: the bindless sampler table is full");
            return {};
        }
        const VkSamplerCreateInfo Info
        { .sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO
        , .magFilter        = ConvertSampler(D.m_MipmapMag)
        , .minFilter        = ConvertSampler(D.m_MipmapMin)
        , .mipmapMode       = ConvertMipmapMode(D.m_MipmapMode)
        , .addressModeU     = ConvertAddressMode(D.m_AddressMode[0])
        , .addressModeV     = ConvertAddressMode(D.m_AddressMode[1])
        , .addressModeW     = ConvertAddressMode(D.m_AddressMode[2])
        , .anisotropyEnable = Aniso > 0 ? VK_TRUE : VK_FALSE
        , .maxAnisotropy    = Aniso > 0 ? Aniso : 1.0f
        , .compareOp        = VK_COMPARE_OP_NEVER
        , .maxLod           = VK_LOD_CLAMP_NONE
        , .borderColor      = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
        };
        VkSampler Sampler;
        if( auto VKErr = vkCreateSampler(m_VKDevice, &Info, m_Instance->m_pVKAllocator, &Sampler); VKErr )
        {
            m_Instance->ReportError(VKErr, "Fail to create a sampler of the sampler table");
            return {};
        }
        H.m_Samplers.push_back(Sampler);
        H.m_SamplerIDs.emplace(Key, ID);
        if( m_bBindless )
        {
            const VkDescriptorImageInfo Image{ .sampler = Sampler };
            const VkWriteDescriptorSet  Write{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = H.m_Set, .dstBinding = 2, .dstArrayElement = ID
                                             , .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .pImageInfo = &Image };
            vkUpdateDescriptorSets(m_VKDevice, 1, &Write, 0, nullptr);
        }
        return { ID };
    }

    //----------------------------------------------------------------------------------------------------------

    VkSampler device::getVKSampler( std::uint32_t ID ) noexcept
    {
        std::lock_guard Lk(m_Heap.m_Lock);
        return ID < m_Heap.m_Samplers.size() ? m_Heap.m_Samplers[ID] : VK_NULL_HANDLE;
    }

    //----------------------------------------------------------------------------------------------------------

    // Set 3 of a pipeline made with m_nTextureSlots: the heaps (bindless), else nSlots combined image samplers (one layout per count,
    // shared by the pipelines and the tables: their sets are compatible)
    VkDescriptorSetLayout device::TextureTableLayout( int nSlots ) noexcept
    {
        if( m_bBindless ) return m_Heap.m_Layout;
        std::lock_guard Lk(m_Heap.m_Lock);
        if( auto It = m_Heap.m_TableLayouts.find(nSlots); It != m_Heap.m_TableLayouts.end() ) return It->second;
        std::vector<VkDescriptorSetLayoutBinding> Bindings(nSlots);
        for( int i = 0; i < nSlots; ++i )
            Bindings[i] = { .binding = static_cast<std::uint32_t>(i), .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1
                          , .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT };
        const VkDescriptorSetLayoutCreateInfo Info{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = static_cast<std::uint32_t>(nSlots), .pBindings = Bindings.data() };
        VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
        if( auto VKErr = vkCreateDescriptorSetLayout(m_VKDevice, &Info, m_Instance->m_pVKAllocator, &Layout); VKErr )
        {
            m_Instance->ReportError(VKErr, "Fail to create a texture table's set layout");
            return VK_NULL_HANDLE;
        }
        m_Heap.m_TableLayouts.emplace(nSlots, Layout);
        return Layout;
    }

    //----------------------------------------------------------------------------------------------------------

    // A non-bindless table's set, from the last table pool (a new pool when it is full)
    VkDescriptorSet device::AllocTableSet( int nSlots, VkDescriptorPool& Pool ) noexcept
    {
        constexpr std::uint32_t sets_per_pool_v = 256, images_per_pool_v = 256 * 8;
        const VkDescriptorSetLayout Layout = TextureTableLayout(nSlots);
        std::lock_guard Lk(m_Heap.m_Lock);
        for( int Try = 0; Try < 2; ++Try )
        {
            if( Try == 1 || m_Heap.m_TablePools.empty() )
            {
                const VkDescriptorPoolSize      Size{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, images_per_pool_v };
                const VkDescriptorPoolCreateInfo Info{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
                                                     , .maxSets = sets_per_pool_v, .poolSizeCount = 1, .pPoolSizes = &Size };
                VkDescriptorPool NewPool;
                if( auto VKErr = vkCreateDescriptorPool(m_VKDevice, &Info, m_Instance->m_pVKAllocator, &NewPool); VKErr )
                {
                    m_Instance->ReportError(VKErr, "Fail to create a texture table pool");
                    return VK_NULL_HANDLE;
                }
                m_Heap.m_TablePools.push_back(NewPool);
            }
            Pool = m_Heap.m_TablePools.back();
            const VkDescriptorSetAllocateInfo Alloc{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = Pool, .descriptorSetCount = 1, .pSetLayouts = &Layout };
            VkDescriptorSet Set;
            if( vkAllocateDescriptorSets(m_VKDevice, &Alloc, &Set) == VK_SUCCESS ) return Set;
        }
        m_Instance->ReportError("Fail to allocate a texture table's set");
        return VK_NULL_HANDLE;
    }

    //----------------------------------------------------------------------------------------------------------

    void device::FreeTableSet( VkDescriptorPool Pool, VkDescriptorSet Set ) noexcept
    {
        if( Set == VK_NULL_HANDLE ) return;
        std::lock_guard Lk(m_Heap.m_Lock);
        vkFreeDescriptorSets(m_VKDevice, Pool, 1, &Set);
    }

    //----------------------------------------------------------------------------------------------------------

    xgpu::device::error* device::Create
    ( xgpu::texture_table&                  Table
    , const xgpu::texture_table::setup&     Setup
    , std::shared_ptr<device_handle>&       SharedDevice
    ) noexcept
    {
        // An invalid slot's texture (white), made the first time a table needs it
        if( m_bBindless == false && m_Heap.m_White.m_Private == nullptr )
        {
            const std::uint32_t White = 0xffffffffu;
            const auto          Mips  = std::array{ xgpu::texture::setup::mip_size{ sizeof(White) } };
            if( auto Err = Create(m_Heap.m_White, { .m_Format = xgpu::texture::format::R8G8B8A8_UNORM, .m_Width = 1, .m_Height = 1, .m_MipSizes = Mips
                                                  , .m_AllFacesData = { reinterpret_cast<const std::byte*>(&White), sizeof(White) }, .m_isGamma = false }, SharedDevice); Err )
                return Err;
        }
        auto T = std::make_shared<xgpu::vulkan::texture_table>();
        if( auto Err = T->Initialize(std::reinterpret_pointer_cast<xgpu::vulkan::device>(SharedDevice), Setup); Err )
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create a texture table");
        Table.m_Private = T;
        return nullptr;
    }

    //----------------------------------------------------------------------------------------------------------

    void device::Destroy( xgpu::texture_table&& Table ) noexcept
    {
        m_DeathMarchList[m_FrameIndex % m_DeathMarchList.size()].m_TextureTable.push_back(std::move(Table));
    }

    //----------------------------------------------------------------------------------------------------------
    // vulkan::texture_table
    //----------------------------------------------------------------------------------------------------------

    xgpu::texture_table::error* texture_table::Initialize( std::shared_ptr<vulkan::device>&& Device, const xgpu::texture_table::setup& Setup ) noexcept
    {
        m_Device = std::move(Device);
        m_Slots.assign(Setup.m_Slots.begin(), Setup.m_Slots.end());
        m_Textures.resize(m_Slots.size());
        for( std::size_t i = 0; i < m_Slots.size(); ++i ) m_Textures[i] = m_Device->getTexture(m_Slots[i].m_Texture.m_Value);
        return BuildSet();
    }

    //----------------------------------------------------------------------------------------------------------

    texture_table::~texture_table( void ) noexcept
    {
        if( m_Device ) m_Device->FreeTableSet(m_VKPool, m_VKSet);
    }

    //----------------------------------------------------------------------------------------------------------

    xgpu::texture_table::error* texture_table::Update( int iSlot, const xgpu::texture_table::slot& Slot ) noexcept
    {
        assert(iSlot >= 0 && iSlot < static_cast<int>(m_Slots.size()));
        m_Slots[iSlot]    = Slot;
        m_Textures[iSlot] = m_Device->getTexture(Slot.m_Texture.m_Value);
        return BuildSet();
    }

    //----------------------------------------------------------------------------------------------------------

    xgpu::texture_table::error* texture_table::BuildSet( void ) noexcept
    {
        if( m_Device->m_bBindless || m_Slots.empty() ) return nullptr;

        VkDescriptorPool Pool = VK_NULL_HANDLE;
        const VkDescriptorSet Set = m_Device->AllocTableSet(static_cast<int>(m_Slots.size()), Pool);
        if( Set == VK_NULL_HANDLE ) return VGPU_ERROR(xgpu::texture_table::error::FAILURE, "No room for a texture table's set");

        const auto& White = *std::reinterpret_pointer_cast<vulkan::texture>(m_Device->m_Heap.m_White.m_Private);
        std::vector<VkDescriptorImageInfo> Images(m_Slots.size());
        std::vector<VkWriteDescriptorSet>  Writes(m_Slots.size());
        for( std::size_t i = 0; i < m_Slots.size(); ++i )
        {
            const auto& T       = m_Textures[i] ? *m_Textures[i] : White;
            auto        Sampler = m_Slots[i].m_Sampler.isValid() ? m_Slots[i].m_Sampler : T.m_SamplerID;
            Images[i]           = T.m_VKDescriptorImageInfo;
            Images[i].sampler   = m_Device->getVKSampler(Sampler.m_Value);
            Writes[i] = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = Set, .dstBinding = static_cast<std::uint32_t>(i)
                        , .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &Images[i] };
        }
        vkUpdateDescriptorSets(m_Device->m_VKDevice, static_cast<std::uint32_t>(Writes.size()), Writes.data(), 0, nullptr);

        // The old set may still be read by the frame in flight: freed by the death march
        if( m_VKSet != VK_NULL_HANDLE )
            m_Device->m_DeathMarchList[m_Device->m_FrameIndex % m_Device->m_DeathMarchList.size()].m_TableSets.push_back({ m_VKPool, m_VKSet });
        m_VKSet  = Set;
        m_VKPool = Pool;
        return nullptr;
    }

    //----------------------------------------------------------------------------------------------------------

    void texture_table::DeathMarch( xgpu::texture_table&& Table ) noexcept
    {
        if( !m_Device ) return;
        m_Device->Destroy(std::move(Table));      // (m_Device stays: the destructor frees the set through it)
    }
}
