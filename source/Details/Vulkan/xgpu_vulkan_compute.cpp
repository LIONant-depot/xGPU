#include "../xgpu_env.h"
namespace xgpu::vulkan
{
    namespace
    {
        // What a dispatch outside an explicit-sync section waits for (draws, copies, the host's writes, other dispatches; DRAW_INDIRECT: a
        // buffer of draw commands it rewrites may still be read by earlier indirect draws, and later ones read what it wrote; the attachment
        // stages: a texture it reads was rendered by an earlier pass, and a later pass may render into it again), and makes its writes visible to
        constexpr VkPipelineStageFlags dispatch_neighbours_v = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                                                             | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT
                                                             | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        constexpr VkMemoryBarrier dispatch_in_v
        { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER
        , .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT
                         | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
        , .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT };
        constexpr VkMemoryBarrier dispatch_out_v
        { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT
        , .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_HOST_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT };

        // A storage image from where it rests between operations (sampled, or nothing yet) to the general layout
        VkImageMemoryBarrier ToGeneralBarrier( const vulkan::texture& T ) noexcept
        {
            return { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
                   , .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
                   , .oldLayout = T.m_bContentReady || T.m_bStorageAfterPass ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL
                   , .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = T.m_VKImage
                   , .subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 } };
        }
    }

    // (R1a) The explicit-sync section: one barrier in (all earlier work; the section's storage images to general), none per dispatch
    void window::BeginExplicitSync( std::span<const xgpu::texture* const> Images ) noexcept
    {
        assert(!m_bExplicitSync);
        m_bExplicitSync = true; m_GeneralImages.clear();
        std::array<VkImageMemoryBarrier, 16> To{};
        std::uint32_t n = 0;
        for( auto* pI : Images ) if( pI && pI->m_Private && n < To.size() )
        {
            auto& T = *const_cast<vulkan::texture*>(static_cast<const vulkan::texture*>(pI->m_Private.get()));
            if( std::find(m_GeneralImages.begin(), m_GeneralImages.end(), &T) != m_GeneralImages.end() ) continue;
            To[n++] = ToGeneralBarrier(T); m_GeneralImages.push_back(&T); T.m_bContentReady = true;
        }
        vkCmdPipelineBarrier(RecordingCmd(), dispatch_neighbours_v, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &dispatch_in_v, 0, nullptr, n, To.data());
        ++m_PendingBarriers;
    }
    // A dispatch reads or overwrites what an earlier one in the section touched
    void window::ComputeBarrier( void ) noexcept
    {
        assert(m_bExplicitSync);
        constexpr VkMemoryBarrier B{ .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT };
        vkCmdPipelineBarrier(RecordingCmd(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &B, 0, nullptr, 0, nullptr);
        ++m_PendingBarriers;
    }
    // Out: the section's writes visible to everything after, its images sampled again
    void window::EndExplicitSync( void ) noexcept
    {
        assert(m_bExplicitSync);
        std::vector<VkImageMemoryBarrier> Back;
        for( auto* pT : m_GeneralImages )
        {
            auto B = ToGeneralBarrier(*pT);
            B.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; B.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
            B.oldLayout = VK_IMAGE_LAYOUT_GENERAL; B.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            Back.push_back(B);
        }
        vkCmdPipelineBarrier(RecordingCmd(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, dispatch_neighbours_v, 0, 1, &dispatch_out_v, 0, nullptr, static_cast<std::uint32_t>(Back.size()), Back.data());
        if( m_FrameMap.m_bOn && !m_FrameMap.m_Entries.empty() ) m_FrameMap.m_Entries.back().m_Barriers += 1;
        m_bExplicitSync = false; m_GeneralImages.clear(); m_PendingBarriers = 0;
    }
    // XGPU_PIPELINE_STATS=<file>: what the driver says about a compute pipeline, appended (a measuring tool: a pipeline is named only by its bindings and its order)
    // (also the graphics pipelines': window::setPipelineInstance, labelled by their pass and state)
    void WritePipelineStats( device& D, VkPipeline Pipeline, const std::string& Label ) noexcept
    {
        const auto pProps = reinterpret_cast<PFN_vkGetPipelineExecutablePropertiesKHR>(vkGetDeviceProcAddr(D.m_VKDevice, "vkGetPipelineExecutablePropertiesKHR"));
        const auto pStats = reinterpret_cast<PFN_vkGetPipelineExecutableStatisticsKHR>(vkGetDeviceProcAddr(D.m_VKDevice, "vkGetPipelineExecutableStatisticsKHR"));
        const auto pIR    = reinterpret_cast<PFN_vkGetPipelineExecutableInternalRepresentationsKHR>(vkGetDeviceProcAddr(D.m_VKDevice, "vkGetPipelineExecutableInternalRepresentationsKHR"));
        FILE* pFile = pProps && pStats && pIR ? xgpu::details::Open(xgpu::details::Env("XGPU_PIPELINE_STATS"), "a") : nullptr;
        if( !pFile ) return;
        static int s_Index = 0;
        std::fprintf(pFile, "pipeline %d: %s\n", s_Index++, Label.c_str());
        const VkPipelineInfoKHR Info{ .sType = VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR, .pipeline = Pipeline };
        std::uint32_t n = 0;
        pProps(D.m_VKDevice, &Info, &n, nullptr);
        std::vector<VkPipelineExecutablePropertiesKHR> Props(n, { .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR });
        pProps(D.m_VKDevice, &Info, &n, Props.data());
        for( std::uint32_t e = 0; e < n; ++e )
        {
            std::fprintf(pFile, "  executable %s (subgroup %u)\n", Props[e].name, Props[e].subgroupSize);
            const VkPipelineExecutableInfoKHR Exe{ .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR, .pipeline = Pipeline, .executableIndex = e };
            std::uint32_t m = 0;
            pStats(D.m_VKDevice, &Exe, &m, nullptr);
            std::vector<VkPipelineExecutableStatisticKHR> Stats(m, { .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR });
            pStats(D.m_VKDevice, &Exe, &m, Stats.data());
            for( const auto& S : Stats )
            {
                if( std::strcmp(S.name, "Binary Size") == 0 ) { std::lock_guard Lk(xgpu_vkcount::g_CodeLock); xgpu_vkcount::g_Code[Pipeline] += S.format == VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR ? std::uint64_t(S.value.i64) : S.value.u64; xgpu_vkcount::g_Label[Pipeline] = Label; }
                std::fprintf(pFile, "    %s: ", S.name);
                switch( S.format )
                {
                case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR:  std::fprintf(pFile, "%u\n", S.value.b32); break;
                case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:   std::fprintf(pFile, "%lld\n", static_cast<long long>(S.value.i64)); break;
                case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:  std::fprintf(pFile, "%llu\n", static_cast<unsigned long long>(S.value.u64)); break;
                default:                                                  std::fprintf(pFile, "%f\n", S.value.f64); break;
                }
            }
            std::uint32_t r = 0;
            pIR(D.m_VKDevice, &Exe, &r, nullptr);
            std::vector<VkPipelineExecutableInternalRepresentationKHR> IR(r, { .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INTERNAL_REPRESENTATION_KHR });
            pIR(D.m_VKDevice, &Exe, &r, IR.data());
            for( const auto& I : IR ) std::fprintf(pFile, "    representation %s: %zu bytes\n", I.name, I.dataSize);
        }
        std::fclose(pFile);
    }

    xgpu::device::error* compute::Initialize( std::shared_ptr<device>&& Device, const xgpu::compute::setup& Setup ) noexcept
    {
        m_pName = Setup.m_pName;
        m_Device   = std::move(Device);
        m_nBuffers  = Setup.m_nBuffers;
        if( Setup.m_pShader && Setup.m_pShader->m_Private ) m_ReadOnlyBuffers = static_cast<const vulkan::shader*>(Setup.m_pShader->m_Private.get())->m_ReadOnlyBuffers;
        m_nTextures = Setup.m_nTextures;
        m_nImages   = Setup.m_nImages;
        const VkFilter Filter = Setup.m_bLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        m_PushSize = static_cast<std::uint32_t>(Setup.m_PushConstantsSize);
        if( Setup.m_pShader == nullptr || Setup.m_pShader->m_Private == nullptr ) return VGPU_ERROR(xgpu::device::error::FAILURE, "compute: no shader");
        if( m_nBuffers < 0 || m_nBuffers > max_buffers_v )                        return VGPU_ERROR(xgpu::device::error::FAILURE, "compute: 0 to 16 storage buffers");
        if( m_PushSize > 128 )                                                    return VGPU_ERROR(xgpu::device::error::FAILURE, "compute: more than 128 bytes of push constants");
        if( m_nTextures < 0 || m_nTextures > max_textures_v )                     return VGPU_ERROR(xgpu::device::error::FAILURE, "compute: 0 to 8 textures");
        if( m_nImages < 0 || m_nImages > max_textures_v )                         return VGPU_ERROR(xgpu::device::error::FAILURE, "compute: 0 to 8 storage images");
        const auto& Shader = *static_cast<const vulkan::shader*>(Setup.m_pShader->m_Private.get());

        std::array<VkDescriptorSetLayoutBinding, max_buffers_v> Bindings{};
        for( int i = 0; i < m_nBuffers; ++i )
            Bindings[i] = { .binding = static_cast<std::uint32_t>(i), .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT };
        const VkDescriptorSetLayoutCreateInfo LayoutInfo
        { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = static_cast<std::uint32_t>(m_nBuffers), .pBindings = Bindings.data() };
        if( vkCreateDescriptorSetLayout(m_Device->m_VKDevice, &LayoutInfo, m_Device->m_Instance->m_pVKAllocator, &m_VKSetLayout) )
            return VGPU_ERROR(xgpu::device::error::FAILURE, "compute: vkCreateDescriptorSetLayout failed");
        if( m_nTextures )
        {
            std::array<VkDescriptorSetLayoutBinding, max_textures_v> TextureBindings{};
            for( int i = 0; i < m_nTextures; ++i )
                TextureBindings[i] = { .binding = static_cast<std::uint32_t>(i), .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT };
            const VkDescriptorSetLayoutCreateInfo TextureInfo
            { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = static_cast<std::uint32_t>(m_nTextures), .pBindings = TextureBindings.data() };
            if( vkCreateDescriptorSetLayout(m_Device->m_VKDevice, &TextureInfo, m_Device->m_Instance->m_pVKAllocator, &m_VKTextureLayout) )
                return VGPU_ERROR(xgpu::device::error::FAILURE, "compute: vkCreateDescriptorSetLayout (textures) failed");
            const VkSamplerCreateInfo SamplerInfo
            { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = Filter, .minFilter = Filter, .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST
            , .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
            , .maxLod = VK_LOD_CLAMP_NONE };
            if( vkCreateSampler(m_Device->m_VKDevice, &SamplerInfo, m_Device->m_Instance->m_pVKAllocator, &m_VKSampler) )
                return VGPU_ERROR(xgpu::device::error::FAILURE, "compute: vkCreateSampler failed");
        }

        if( m_nImages )
        {
            std::array<VkDescriptorSetLayoutBinding, max_textures_v> ImageBindings{};
            for( int i = 0; i < m_nImages; ++i )
                ImageBindings[i] = { .binding = static_cast<std::uint32_t>(i), .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT };
            const VkDescriptorSetLayoutCreateInfo ImageInfo
            { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = static_cast<std::uint32_t>(m_nImages), .pBindings = ImageBindings.data() };
            if( vkCreateDescriptorSetLayout(m_Device->m_VKDevice, &ImageInfo, m_Device->m_Instance->m_pVKAllocator, &m_VKImageLayout) )
                return VGPU_ERROR(xgpu::device::error::FAILURE, "compute: vkCreateDescriptorSetLayout (images) failed");
            if( !m_nTextures )          // (set 1 still needs a layout: an empty one)
            {
                const VkDescriptorSetLayoutCreateInfo Empty{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
                if( vkCreateDescriptorSetLayout(m_Device->m_VKDevice, &Empty, m_Device->m_Instance->m_pVKAllocator, &m_VKTextureLayout) )
                    return VGPU_ERROR(xgpu::device::error::FAILURE, "compute: vkCreateDescriptorSetLayout (no textures) failed");
            }
        }

        const VkPushConstantRange Push{ .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = m_PushSize };
        const std::array SetLayouts{ m_VKSetLayout, m_VKTextureLayout, m_VKImageLayout };
        const VkPipelineLayoutCreateInfo PipelineLayoutInfo
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = m_nImages ? 3u : m_nTextures ? 2u : 1u, .pSetLayouts = SetLayouts.data()
        , .pushConstantRangeCount = m_PushSize ? 1u : 0u, .pPushConstantRanges = m_PushSize ? &Push : nullptr };
        if( vkCreatePipelineLayout(m_Device->m_VKDevice, &PipelineLayoutInfo, m_Device->m_Instance->m_pVKAllocator, &m_VKPipelineLayout) )
            return VGPU_ERROR(xgpu::device::error::FAILURE, "compute: vkCreatePipelineLayout failed");

        VkPipelineShaderStageCreateInfo Stage = Shader.m_ShaderStageCreateInfo;
        if( Stage.stage != VK_SHADER_STAGE_COMPUTE_BIT ) return VGPU_ERROR(xgpu::device::error::FAILURE, "compute: the shader is not a compute shader");
        const VkComputePipelineCreateInfo PipelineInfo{ .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO
        , .flags = m_Device->m_bPipelineStats ? VkPipelineCreateFlags(VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR | VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR) : VkPipelineCreateFlags(0)
        , .stage = Stage, .layout = m_VKPipelineLayout };
        XPROFILE_SCOPE("xGPU: make a compute pipeline");
        if( vkCreateComputePipelines(m_Device->m_VKDevice, m_Device->m_bPipelineStats ? VK_NULL_HANDLE : m_Device->m_VKPipelineCache, 1, &PipelineInfo, m_Device->m_Instance->m_pVKAllocator, &m_VKPipeline) )
            return VGPU_ERROR(xgpu::device::error::FAILURE, "compute: vkCreateComputePipelines failed");
        if( m_Device->m_bPipelineStats ) WritePipelineStats( *m_Device, m_VKPipeline, std::format("compute {} (buffers {} textures {} images {} push {})", m_pName ? m_pName : "(no name)", m_nBuffers, m_nTextures, m_nImages, m_PushSize) );
        return nullptr;
    }

    compute::~compute( void ) noexcept
    {
        if( !m_Device ) return;
        vkDeviceWaitIdle(m_Device->m_VKDevice);         // ponytail: a compute object lives as long as its renderer; wait instead of a death march
        if( m_VKPipeline )       vkDestroyPipeline(m_Device->m_VKDevice, m_VKPipeline, m_Device->m_Instance->m_pVKAllocator);
        if( m_VKPipelineLayout ) vkDestroyPipelineLayout(m_Device->m_VKDevice, m_VKPipelineLayout, m_Device->m_Instance->m_pVKAllocator);
        if( m_VKSetLayout )      vkDestroyDescriptorSetLayout(m_Device->m_VKDevice, m_VKSetLayout, m_Device->m_Instance->m_pVKAllocator);
        if( m_VKTextureLayout )  vkDestroyDescriptorSetLayout(m_Device->m_VKDevice, m_VKTextureLayout, m_Device->m_Instance->m_pVKAllocator);
        if( m_VKImageLayout )    vkDestroyDescriptorSetLayout(m_Device->m_VKDevice, m_VKImageLayout, m_Device->m_Instance->m_pVKAllocator);
        if( m_VKSampler )        vkDestroySampler(m_Device->m_VKDevice, m_VKSampler, m_Device->m_Instance->m_pVKAllocator);
    }

    //----------------------------------------------------------------------------------------------------------

    void window::Dispatch( xgpu::compute& Compute, std::span<xgpu::buffer* const> Buffers, std::span<const xgpu::texture* const> Textures, std::span<const xgpu::texture* const> Images, const void* pPush, std::size_t PushSize, int X, int Y, int Z ) noexcept
    {
        auto& C = *static_cast<vulkan::compute*>(Compute.m_Private.get());
        xgpu::buffer* const pIndirect = m_pIndirectArgs; const std::uint32_t IndirectOffset = m_IndirectOffset; m_pIndirectArgs = nullptr;      // (DispatchIndirect: this dispatch only)
        // A grid past the device's maxComputeWorkGroupCount (llvmpipe and many GPUs: 65535 in X) is invalid: the dispatch is skipped, said once
        // (device::getMaxComputeGroupsX: the caller splits it, with an offset of its own)
        if( const auto& Max = m_Device->m_VKPhysicalDeviceProperties.limits.maxComputeWorkGroupCount
          ; std::uint32_t(X) > Max[0] || std::uint32_t(Y) > Max[1] || std::uint32_t(Z) > Max[2] )
        {
            if( !m_bReportedGridOverflow ) { m_bReportedGridOverflow = true; m_Device->m_Instance->ReportError(std::format("xGPU: a dispatch of {} x {} x {} work groups is past this device's maxComputeWorkGroupCount ({} x {} x {}): skipped", X, Y, Z, Max[0], Max[1], Max[2])); }
            return;
        }
        assert(static_cast<int>(Buffers.size()) == C.m_nBuffers && static_cast<int>(Textures.size()) == C.m_nTextures && static_cast<int>(Images.size()) == C.m_nImages && PushSize <= C.m_PushSize);
        const VkCommandBuffer Cmd = RecordingCmd();       // (the frame's, or its async section's)
        if( m_Device->ReuseOn() ) { for( auto* pB : Buffers ) if( pB && pB->m_Private ) static_cast<vulkan::buffer*>(pB->m_Private.get())->m_BoundSubmit = m_Device->ReuseNow();
                                    if( pIndirect && pIndirect->m_Private ) static_cast<vulkan::buffer*>(pIndirect->m_Private.get())->m_BoundSubmit = m_Device->ReuseNow(); }
        m_Device->Checkpoint(Cmd, "dispatch", C.m_pName);

        // (the frame map: this dispatch, its buffers (read or written: not known here), textures read, images written)
        bool bMap = false;
        if( m_FrameMap.m_bOn )
        {
            auto& E = m_FrameMap.Add("dispatch"); bMap = true; E.m_Barriers = m_bExplicitSync ? m_PendingBarriers : 2;      // (In and Out, below; in a section: the caller's since the last dispatch)
            E.m_Name = frame_map::Name(C.m_pName);
            for( std::size_t b = 0; b < Buffers.size(); ++b ) if( auto* pB = Buffers[b] )      // (read only: the shader's readonly buffers)
                ((C.m_ReadOnlyBuffers >> b) & 1u ? E.m_Reads : E.m_RW).push_back(frame_map::Name(static_cast<const vulkan::buffer*>(pB->m_Private.get())->m_pName));
            for( auto* pT : Textures ) if( pT ) E.m_Reads.push_back(frame_map::Name(static_cast<const vulkan::texture*>(pT->m_Private.get())->m_pName));
            for( auto* pI : Images )   if( pI ) E.m_Writes.push_back(frame_map::Name(static_cast<const vulkan::texture*>(pI->m_Private.get())->m_pName));
        }

        // (measurement, XGPU_BARRIER_LOG: dispatches a device frame, each with its two full barriers, and the storage images' layout flips)
        static const bool s_bBarrierLog = xgpu::details::Env("XGPU_BARRIER_LOG") != nullptr;
        if( s_bBarrierLog )
        {
            static std::uint64_t s_Frame = 0, s_First = 0, s_Dispatches = 0, s_Images = 0;
            const std::uint64_t F = m_Device->getFrameIndex();
            if( F != s_Frame ) { s_Frame = F; if( s_First == 0 ) s_First = F; else if( F - s_First >= 120 ) { std::printf("XGPU_BARRIERS a frame: %.1f dispatches (%.1f full barriers), %.1f storage-image layout flips\n", double(s_Dispatches) / double(F - s_First), 2.0 * double(s_Dispatches) / double(F - s_First), 2.0 * double(s_Images) / double(F - s_First)); std::fflush(stdout); s_First = F; s_Dispatches = s_Images = 0; } }
            ++s_Dispatches; s_Images += std::uint64_t(C.m_nImages);
        }

        // (C30 diagnostic, XGPU_DESC_WAIT="<compute name part>[:t]": the frame in flight waited for before this dispatch's descriptor writes - ":t"
        // before its texture set's only)
        static const char* s_pDescWait = xgpu::details::Env("XGPU_DESC_WAIT");
        // ("name#k": only the k-th such dispatch of the device frame, from 0)
        static const std::string s_Name = s_pDescWait ? std::string(s_pDescWait).substr(0, std::string(s_pDescWait).find_first_of(":#")) : std::string();
        static const int s_K = s_pDescWait && std::strchr(s_pDescWait, '#') ? std::atoi(std::strchr(s_pDescWait, '#') + 1) : -1;
        static std::uint64_t s_KFrame = ~0ull; static int s_KCount = 0;
        bool bDescWait = s_pDescWait && C.m_pName && std::strstr(C.m_pName, s_Name.c_str());
        if( bDescWait && s_K >= 0 ) { if( s_KFrame != m_Device->getFrameIndex() ) { s_KFrame = m_Device->getFrameIndex(); s_KCount = 0; } bDescWait = (s_KCount++ == s_K); }
        const bool bDescWaitTex = bDescWait && std::strchr(s_pDescWait, ':') != nullptr;
        if( bDescWait && !bDescWaitTex ) m_Device->WaitInFlight();
        VkDescriptorSet Set = VK_NULL_HANDLE;
        if( C.m_nBuffers )
        {
            if( (Set = Rings()[VK_DESCRIPTOR_TYPE_STORAGE_BUFFER].alloc(m_Device->m_VKDevice, C.m_VKSetLayout)) == VK_NULL_HANDLE )
            {
                if( !m_bReportedSetOverflow ) { m_bReportedSetOverflow = true; m_Device->m_Instance->ReportError("xGPU: a frame ran out of descriptor sets (compute); the dispatch is skipped"); }
                return;
            }
            std::array<VkDescriptorBufferInfo, compute::max_buffers_v> Infos{};
            std::array<VkWriteDescriptorSet, compute::max_buffers_v>   Writes{};
            for( int i = 0; i < C.m_nBuffers; ++i )
            {
                Infos[i]  = { .buffer = static_cast<vulkan::buffer*>(Buffers[i]->m_Private.get())->m_VKBuffer, .offset = 0, .range = static_cast<vulkan::buffer*>(Buffers[i]->m_Private.get())->StorageRange() };
                Writes[i] = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = Set, .dstBinding = static_cast<std::uint32_t>(i), .descriptorCount = 1
                            , .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &Infos[i] };
            }
            vkUpdateDescriptorSets(m_Device->m_VKDevice, static_cast<std::uint32_t>(C.m_nBuffers), Writes.data(), 0, nullptr);
        }
        if( bDescWaitTex ) m_Device->WaitInFlight();
        VkDescriptorSet TextureSet = VK_NULL_HANDLE;
        if( C.m_nTextures )
        {
            if( (TextureSet = Rings()[VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER].alloc(m_Device->m_VKDevice, C.m_VKTextureLayout)) == VK_NULL_HANDLE )
            {
                if( !m_bReportedSetOverflow ) { m_bReportedSetOverflow = true; m_Device->m_Instance->ReportError("xGPU: a frame ran out of descriptor sets (compute textures); the dispatch is skipped"); }
                return;
            }
            std::array<VkDescriptorImageInfo, compute::max_textures_v> Infos{};
            std::array<VkWriteDescriptorSet, compute::max_textures_v> Writes{};
            for( int i = 0; i < C.m_nTextures; ++i )
            {
                const auto& T = *static_cast<const vulkan::texture*>(Textures[i]->m_Private.get());
                const bool bGeneral = m_bExplicitSync && std::find(m_GeneralImages.begin(), m_GeneralImages.end(), &T) != m_GeneralImages.end();      // (a section's image: still general)
                Infos[i]  = { .sampler = C.m_VKSampler, .imageView = T.m_VKDescriptorImageInfo.imageView, .imageLayout = bGeneral ? VK_IMAGE_LAYOUT_GENERAL : T.m_VKDescriptorImageInfo.imageLayout };
                Writes[i] = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = TextureSet, .dstBinding = static_cast<std::uint32_t>(i), .descriptorCount = 1
                            , .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &Infos[i] };
            }
            vkUpdateDescriptorSets(m_Device->m_VKDevice, static_cast<std::uint32_t>(C.m_nTextures), Writes.data(), 0, nullptr);
        }

        VkDescriptorSet ImageSet = VK_NULL_HANDLE;
        std::array<VkImageMemoryBarrier, compute::max_textures_v> ToGeneral{}, ToSampled{};
        std::uint32_t nToGeneral = 0;
        if( C.m_nImages )
        {
            if( (ImageSet = Rings()[VK_DESCRIPTOR_TYPE_STORAGE_IMAGE].alloc(m_Device->m_VKDevice, C.m_VKImageLayout)) == VK_NULL_HANDLE )
            {
                if( !m_bReportedSetOverflow ) { m_bReportedSetOverflow = true; m_Device->m_Instance->ReportError("xGPU: a frame ran out of descriptor sets (compute images); the dispatch is skipped"); }
                return;
            }
            std::array<VkDescriptorImageInfo, compute::max_textures_v> Infos{};
            std::array<VkWriteDescriptorSet, compute::max_textures_v> Writes{};
            for( int i = 0; i < C.m_nImages; ++i )
            {
                auto& T = *const_cast<vulkan::texture*>(static_cast<const vulkan::texture*>(Images[i]->m_Private.get()));
                assert(T.m_bStorage);
                Infos[i]  = { .imageView = T.m_VKDescriptorImageInfo.imageView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL };
                Writes[i] = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ImageSet, .dstBinding = static_cast<std::uint32_t>(i), .descriptorCount = 1
                            , .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &Infos[i] };
                if( m_bExplicitSync )      // (a section: an image Begin did not list goes general now and stays so until End)
                {
                    if( std::find(m_GeneralImages.begin(), m_GeneralImages.end(), &T) == m_GeneralImages.end() ) { ToGeneral[nToGeneral++] = ToGeneralBarrier(T); m_GeneralImages.push_back(&T); T.m_bContentReady = true; }
                    continue;
                }
                ToGeneral[i] = ToGeneralBarrier(T); ++nToGeneral;
                ToSampled[i] = ToGeneral[i];
                ToSampled[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; ToSampled[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
                ToSampled[i].oldLayout = VK_IMAGE_LAYOUT_GENERAL; ToSampled[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                T.m_bContentReady = true;
            }
            vkUpdateDescriptorSets(m_Device->m_VKDevice, static_cast<std::uint32_t>(C.m_nImages), Writes.data(), 0, nullptr);
        }

        // Earlier work is done with the buffers and images before this one touches them (dispatch_in_v); in a section: only an image's
        // first move to general (the caller's ComputeBarrier orders the rest)
        if( !m_bExplicitSync ) vkCmdPipelineBarrier(Cmd, dispatch_neighbours_v, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &dispatch_in_v, 0, nullptr, static_cast<std::uint32_t>(C.m_nImages), ToGeneral.data());
        else { if( nToGeneral ) { vkCmdPipelineBarrier(Cmd, dispatch_neighbours_v, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &dispatch_in_v, 0, nullptr, nToGeneral, ToGeneral.data()); if( bMap ) ++m_FrameMap.m_Entries.back().m_Barriers; } m_PendingBarriers = 0; }

        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, C.m_VKPipeline);
        if( Set != VK_NULL_HANDLE ) vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, C.m_VKPipelineLayout, 0, 1, &Set, 0, nullptr);
        if( TextureSet != VK_NULL_HANDLE ) vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, C.m_VKPipelineLayout, 1, 1, &TextureSet, 0, nullptr);
        if( ImageSet != VK_NULL_HANDLE ) vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, C.m_VKPipelineLayout, 2, 1, &ImageSet, 0, nullptr);
        if( PushSize ) vkCmdPushConstants(Cmd, C.m_VKPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<std::uint32_t>(PushSize), pPush);
        if( bMap ) m_FrameMap.m_Entries.back().m_Q0 = m_FrameMap.Stamp(Cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        if( m_Device->ProgressOn() )
        {
            std::string N = std::string(Cmd == m_Frames[m_FrameIndex].m_VKAsyncCmd ? "async dispatch begins " : "dispatch begins ") + (C.m_pName ? C.m_pName : "?") + std::format(" {}x{}x{}{} push", X, Y, Z, pIndirect ? " indirect" : "");
            for( std::size_t i = 0; i + 4 <= std::min<std::size_t>(PushSize, 32); i += 4 ) { std::int32_t v; std::memcpy(&v, static_cast<const char*>(pPush) + i, 4); N += " " + std::to_string(v); }
            m_Device->ProgressStamp(Cmd, ProgressKey(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, std::move(N));
        }
        if( pIndirect ) { vkCmdDispatchIndirect(Cmd, static_cast<const vulkan::buffer*>(pIndirect->m_Private.get())->m_VKBuffer, IndirectOffset); if( bMap ) ++m_FrameMap.m_Entries.back().m_Indirect; }
        else            vkCmdDispatch(Cmd, static_cast<std::uint32_t>(X), static_cast<std::uint32_t>(Y), static_cast<std::uint32_t>(Z));
        if( bMap ) m_FrameMap.m_Entries.back().m_Q1 = m_FrameMap.Stamp(Cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
        if( m_Device->ProgressOn() ) m_Device->ProgressStamp(Cmd, ProgressKey(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, "dispatch ended");

        // Later shaders, copies and the host (a readback after the frame) see what it wrote (a section: at its End)
        if( !m_bExplicitSync ) vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, dispatch_neighbours_v, 0, 1, &dispatch_out_v, 0, nullptr, static_cast<std::uint32_t>(C.m_nImages), ToSampled.data());
    }
}
