namespace xgpu::vulkan
{
    struct texture_format
    {
        VkFormat            m_VKFormatLinear;
        VkFormat            m_VKFormatSRGB;
    };

    //---------------------------------------------------------------------------------------
    texture::~texture(void) noexcept
    {
        if (m_VKView)         vkDestroyImageView    (m_Device->m_VKDevice, m_VKView, m_Device->m_Instance->m_pVKAllocator);
        if (m_VKImage)        vkDestroyImage        (m_Device->m_VKDevice, m_VKImage, m_Device->m_Instance->m_pVKAllocator);
        if (m_VKDeviceMemory) vkFreeMemory          (m_Device->m_VKDevice, m_VKDeviceMemory, m_Device->m_Instance->m_pVKAllocator );
    }


    //---------------------------------------------------------------------------------------
    static
    VkFormat getVKTextureFormat
    ( const xgpu::texture::format   Fmt
    , const bool                    bGamma
    ) noexcept
    {
        // https://www.khronos.org/registry/vulkan/specs/1.0/xhtml/vkspec.html 
        // VK Interpretation of Numeric Format
        // UNORM   - The components are unsigned normalized values in the range [0,1]
        // SNORM   - The components are signed normalized values in the range [-1,1]
        // USCALED - The components are unsigned integer values that get converted to floating-point in the range [0,2n-1]
        // SSCALED - The components are signed integer values that get converted to floating-point in the range [-2n-1,2n-1-1]
        // UINT    - The components are unsigned integer values in the range [0,2n-1]
        // SINT    - The components are signed integer values in the range [-2n-1,2n-1-1]
        // UFLOAT  - The components are unsigned floating-point numbers (used by packed, shared exponent, and some compressed formats)
        // SFLOAT  - The components are signed floating-point numbers
        // SRGB    - The R, G, and B components are unsigned normalized values that represent values using sRGB nonlinear encoding, 
        //           while the A component (if one exists) is a regular unsigned normalized value
        constexpr static auto FormatTable = []() constexpr noexcept -> auto
        {
            auto FormatTable = std::array<texture_format, (int)xgpu::texture::format::ENUM_COUNT>{ texture_format{VK_FORMAT_UNDEFINED,VK_FORMAT_UNDEFINED} };

            //             xgpu::texture::format                                                    VKFormat_LinearUnsigned                 VKFormat_SRGB
            //           ---------------------------------                                          ----------------------------------    ------------------------------------
            FormatTable[(int)xgpu::texture::format::B8G8R8A8_UNORM            ] = texture_format  { VK_FORMAT_B8G8R8A8_UNORM            , VK_FORMAT_B8G8R8A8_SRGB              };
            FormatTable[(int)xgpu::texture::format::B8G8R8U8_UNORM            ] = texture_format  { VK_FORMAT_B8G8R8A8_UNORM            , VK_FORMAT_B8G8R8A8_SRGB              };
            FormatTable[(int)xgpu::texture::format::A8R8G8B8_UNORM            ] = texture_format  { VK_FORMAT_UNDEFINED                 , VK_FORMAT_UNDEFINED                  };
            FormatTable[(int)xgpu::texture::format::U8R8G8B8_UNORM            ] = texture_format  { VK_FORMAT_UNDEFINED                 , VK_FORMAT_UNDEFINED                  };
            FormatTable[(int)xgpu::texture::format::R8G8B8U8_UNORM            ] = texture_format  { VK_FORMAT_R8G8B8A8_UNORM            , VK_FORMAT_R8G8B8A8_SRGB              };
            FormatTable[(int)xgpu::texture::format::R8G8B8A8_UNORM            ] = texture_format  { VK_FORMAT_R8G8B8A8_UNORM            , VK_FORMAT_R8G8B8A8_SRGB              };
            FormatTable[(int)xgpu::texture::format::R8G8B8_UNORM              ] = texture_format  { VK_FORMAT_R8G8B8_UNORM              , VK_FORMAT_R8G8B8_SRGB                };
            FormatTable[(int)xgpu::texture::format::R8_UNORM                  ] = texture_format  { VK_FORMAT_R8_UNORM                  , VK_FORMAT_R8_SRGB                    };
            FormatTable[(int)xgpu::texture::format::R4G4B4A4_UNORM            ] = texture_format  { VK_FORMAT_R4G4B4A4_UNORM_PACK16     , VK_FORMAT_R4G4B4A4_UNORM_PACK16      };

            FormatTable[(int)xgpu::texture::format::R5G6B5_UNORM              ] = texture_format  { VK_FORMAT_R5G6B5_UNORM_PACK16       , VK_FORMAT_R5G6B5_UNORM_PACK16        };
            FormatTable[(int)xgpu::texture::format::B5G5R5A1_UNORM            ] = texture_format  { VK_FORMAT_B5G5R5A1_UNORM_PACK16     , VK_FORMAT_B5G5R5A1_UNORM_PACK16      };

            FormatTable[(int)xgpu::texture::format::BC1_4RGB_UNORM            ] = texture_format  { VK_FORMAT_BC1_RGB_UNORM_BLOCK       , VK_FORMAT_BC1_RGB_SRGB_BLOCK         };
            FormatTable[(int)xgpu::texture::format::BC1_4RGBA1_UNORM          ] = texture_format  { VK_FORMAT_BC1_RGBA_UNORM_BLOCK      , VK_FORMAT_BC1_RGBA_SRGB_BLOCK        };
            FormatTable[(int)xgpu::texture::format::BC2_8RGBA_UNORM           ] = texture_format  { VK_FORMAT_BC2_UNORM_BLOCK           , VK_FORMAT_BC2_SRGB_BLOCK             };
            FormatTable[(int)xgpu::texture::format::BC3_8RGBA_UNORM           ] = texture_format  { VK_FORMAT_BC3_UNORM_BLOCK           , VK_FORMAT_BC3_SRGB_BLOCK             };
            FormatTable[(int)xgpu::texture::format::BC4_4R_UNORM              ] = texture_format  { VK_FORMAT_BC4_UNORM_BLOCK           , VK_FORMAT_UNDEFINED                  };
            FormatTable[(int)xgpu::texture::format::BC5_8RG_UNORM             ] = texture_format  { VK_FORMAT_BC5_UNORM_BLOCK           , VK_FORMAT_UNDEFINED                  };
            FormatTable[(int)xgpu::texture::format::BC6H_8RGB_SFLOAT          ] = texture_format  { VK_FORMAT_BC6H_SFLOAT_BLOCK         , VK_FORMAT_UNDEFINED                  };
            FormatTable[(int)xgpu::texture::format::BC6H_8RGB_UFLOAT          ] = texture_format  { VK_FORMAT_BC6H_UFLOAT_BLOCK         , VK_FORMAT_UNDEFINED                  };
            FormatTable[(int)xgpu::texture::format::BC7_8RGBA_UNORM           ] = texture_format  { VK_FORMAT_BC7_UNORM_BLOCK           , VK_FORMAT_BC7_SRGB_BLOCK             };

            FormatTable[(int)xgpu::texture::format::ETC2_4RGB_UNORM           ] = texture_format  { VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK   , VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK     };
            FormatTable[(int)xgpu::texture::format::ETC2_4RGBA1_UNORM         ] = texture_format  { VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK , VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK   };
            FormatTable[(int)xgpu::texture::format::ETC2_8RGBA_UNORM          ] = texture_format  { VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK , VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK   };

            FormatTable[(int)xgpu::texture::format::R32G32B32A32_SFLOAT       ] = texture_format  { VK_FORMAT_R32G32B32A32_SFLOAT       , VK_FORMAT_R32G32B32A32_SFLOAT        };
            FormatTable[(int)xgpu::texture::format::R32G32B32_SFLOAT          ] = texture_format  { VK_FORMAT_R32G32B32_SFLOAT          , VK_FORMAT_R32G32B32_SFLOAT           };
            FormatTable[(int)xgpu::texture::format::R32G32_SFLOAT             ] = texture_format  { VK_FORMAT_R32G32_SFLOAT             , VK_FORMAT_R32G32_SFLOAT              };
            FormatTable[(int)xgpu::texture::format::R32_SFLOAT                ] = texture_format  { VK_FORMAT_R32_SFLOAT                , VK_FORMAT_R32_SFLOAT                 };

            FormatTable[(int)xgpu::texture::format::R16G16B16A16_SFLOAT       ] = texture_format  { VK_FORMAT_R16G16B16A16_SFLOAT       , VK_FORMAT_R16G16B16A16_SFLOAT        };
            FormatTable[(int)xgpu::texture::format::R16G16_SFLOAT             ] = texture_format  { VK_FORMAT_R16G16_SFLOAT             , VK_FORMAT_R16G16_SFLOAT              };
            FormatTable[(int)xgpu::texture::format::R16_SFLOAT                ] = texture_format  { VK_FORMAT_R16_SFLOAT                , VK_FORMAT_R16_SFLOAT                 };

            FormatTable[(int)xgpu::texture::format::DEPTH_U16           ]       = texture_format  { VK_FORMAT_D16_UNORM                 , VK_FORMAT_UNDEFINED                  };
            FormatTable[(int)xgpu::texture::format::DEPTH_U24_STENCIL_U8]       = texture_format  { VK_FORMAT_D24_UNORM_S8_UINT         , VK_FORMAT_UNDEFINED                  };
            FormatTable[(int)xgpu::texture::format::DEPTH_F32           ]       = texture_format  { VK_FORMAT_D32_SFLOAT                , VK_FORMAT_UNDEFINED                  };
            FormatTable[(int)xgpu::texture::format::DEPTH_F32_STENCIL_U8]       = texture_format  { VK_FORMAT_D32_SFLOAT_S8_UINT        , VK_FORMAT_UNDEFINED                  };

            return FormatTable;
        }();  

        const auto& Entry = FormatTable[(int)Fmt];
        if (bGamma) return Entry.m_VKFormatSRGB;
        return Entry.m_VKFormatLinear;
    }

    //-------------------------------------------------------------------------------                        
    // Create an image memory barrier for changing the layout of
    // an image and put it into an active command buffer
    static
    void setImageLayout
    (
        VkCommandBuffer                 CmdBuffer
    ,   VkImage                         Image
    ,   VkImageAspectFlags              AspectMask
    ,   VkImageLayout                   OldImageLayout
    ,   VkImageLayout                   NewImageLayout
    ,   const VkImageSubresourceRange&  SubresourceRange
    ) noexcept
    {
        // Create an image barrier object
        VkImageMemoryBarrier ImageMemoryBarrier
        { .sType                  = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER
        , .pNext                  = nullptr
        , .oldLayout              = OldImageLayout
        , .newLayout              = NewImageLayout
        , .srcQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED
        , .dstQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED
        , .image                  = Image
        , .subresourceRange       = SubresourceRange
        };

        // Put barrier on top of pipeline
        VkPipelineStageFlags SrcStageFlags  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags DestStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

        // Only sets masks for layouts used in this example
        // For a more complete version that can be used with other layouts see vkTools::setImageLayout

        if (OldImageLayout == VK_IMAGE_LAYOUT_UNDEFINED && NewImageLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) 
        {
            ImageMemoryBarrier.srcAccessMask = 0;
            ImageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            SrcStageFlags  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            DestStageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (OldImageLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && NewImageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) 
        {
            ImageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            ImageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            SrcStageFlags  = VK_PIPELINE_STAGE_TRANSFER_BIT;
            DestStageFlags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else 
        {
            assert(false);
        }


        // Put barrier inside setup command buffer
        vkCmdPipelineBarrier( CmdBuffer
                            , SrcStageFlags
                            , DestStageFlags
                            , 0
                            , 0
                            , nullptr
                            , 0
                            , nullptr
                            , 1
                            , &ImageMemoryBarrier
                            );
    }

    //---------------------------------------------------------------------------------------

    std::array<int, 3>  texture::getTextureDimensions(void) const noexcept
    {
        return { m_Width, m_Height, m_ArrayCount };
    }

    //---------------------------------------------------------------------------------------

    int texture::getMipCount(void) const noexcept
    {
        return m_nMips;
    }

    //---------------------------------------------------------------------------------------
    
    xgpu::texture::format texture::getFormat(void) const noexcept
    {
        return m_Format;
    }

    //---------------------------------------------------------------------------------------

    bool texture::isCubemap(void) const noexcept
    {
        return m_bCubeMap;
    }

    //---------------------------------------------------------------------------------------

    std::array<xgpu::texture::address_mode, 3> texture::getAdressModes(void) const noexcept
    {
        return m_AdressModes;
    }

    //---------------------------------------------------------------------------------------

    xgpu::device::error* texture::Initialize
    ( std::shared_ptr<vulkan::device>&& Device
    , const xgpu::texture::setup&       Setup
    ) noexcept
    {
        m_Device = Device;
        m_bCubeMap = Setup.m_isCubemap;
        m_AdressModes = Setup.m_AdressModes;

        // Get texture format        
        if(m_VKFormat = getVKTextureFormat(Setup.m_Format, Setup.m_isGamma); m_VKFormat == VK_FORMAT_UNDEFINED )
        {
            m_Device->m_Instance->ReportError( "Unsorported texture format with the specific LINEAR, and SIGNED Flags");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Unsorported texture format with the specific LINEAR, and SIGNED Flags");
        }

        m_nMips      = static_cast<std::uint8_t>(Setup.m_MipSizes.size());
        m_Width      = static_cast<std::uint16_t>(Setup.m_Width);
        m_Height     = static_cast<std::uint16_t>(Setup.m_Height);
        m_ArrayCount = static_cast<std::uint16_t>(Setup.m_FaceCount);
        m_Format     = Setup.m_Format;
        
        // Get device properites for the requested texture format
        VkFormatProperties  VKFormatProps{};
        vkGetPhysicalDeviceFormatProperties(m_Device->m_VKPhysicalDevice, m_VKFormat, &VKFormatProps );

        // Only use linear tiling if requested (and supported by the device)
        // Support for linear tiling is mostly limited, so prefer to use
        // optimal tiling instead
        // On most implementations linear tiling will only support a very
        // limited amount of formats and features (mip maps, cubemaps, arrays, etc.)
        const auto bUseStaging = [&]( bool forceLinearTiling )
        {
            // Only use linear tiling if forced
            if( forceLinearTiling )
            {
                // Don't use linear if format is not supported for (linear) shader sampling
                return !(VKFormatProps.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
            }

            return true;
        }( false );

        VkMemoryAllocateInfo MemoryAllocInfo
        {
            .sType              = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
        ,   .pNext              = nullptr
        ,   .allocationSize     = 0
        ,   .memoryTypeIndex    = 0
        };

        VkMemoryRequirements MemoryRequirements = {};
        VkImageLayout        OutImageLayout{ VK_IMAGE_LAYOUT_UNDEFINED };

        //
        // If we do not have anything to copy then we assume the user wants to simply create the texture
        //
        const bool isDepth = Setup.m_Format >= xgpu::texture::format::DEPTH_U16 && Setup.m_Format <= xgpu::texture::format::DEPTH_F32_STENCIL_U8;
        if( m_nMips == 0 )
        {
            m_nMips = 1;
            VkImageCreateInfo ImageCreateInfo
            {
                .sType          = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO
            ,   .pNext          = nullptr
            ,   .imageType      = VK_IMAGE_TYPE_2D
            ,   .format         = m_VKFormat
            ,   .extent
                {
                    .width      = static_cast<std::uint32_t>(Setup.m_Width)
                ,   .height     = static_cast<std::uint32_t>(Setup.m_Height)
                ,   .depth      = 1
                }
            ,   .mipLevels      = m_nMips
            ,   .arrayLayers    = 1
            ,   .samples        = VK_SAMPLE_COUNT_1_BIT
            ,   .tiling         = VK_IMAGE_TILING_OPTIMAL
            ,   .usage          = std::uint32_t(isDepth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
            ,   .sharingMode    = VK_SHARING_MODE_EXCLUSIVE
            ,   .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED
            };

            if( auto VKErr = vkCreateImage( m_Device->m_VKDevice
                                          , &ImageCreateInfo
                                          , m_Device->m_Instance->m_pVKAllocator
                                          , &m_VKImage ); VKErr )
            {
                m_Device->m_Instance->ReportError(VKErr, "Fail to create the vulkan Image for a texture with zero mips");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create the vulkan Image for a texture with zero mips");
            }

            //
            // Let allocate space for the image in vram
            //
            vkGetImageMemoryRequirements( m_Device->m_VKDevice, m_VKImage, &MemoryRequirements );

            MemoryAllocInfo.allocationSize = MemoryRequirements.size;

            m_Device->getMemoryType( MemoryRequirements.memoryTypeBits
                                , VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                                , MemoryAllocInfo.memoryTypeIndex );

            // TODO: THIS SHOULD BE THREAD SAFE??
            if( auto VKErr = vkAllocateMemory( m_Device->m_VKDevice
                                             , &MemoryAllocInfo
                                             , m_Device->m_Instance->m_pVKAllocator
                                             , &m_VKDeviceMemory ); VKErr )
            {
                m_Device->m_Instance->ReportError(VKErr, "Fail to allocate the necesary memory for the texture in vram with zero mips");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to allocate the necesary memory for the texture in vram with zero mips");
            }

            if( auto VKErr = vkBindImageMemory  ( m_Device->m_VKDevice
                                                , m_VKImage
                                                , m_VKDeviceMemory
                                                , 0 ); VKErr )
            {
                m_Device->m_Instance->ReportError(VKErr, "Fail to bind with the vram memory from the texture with zero mips");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to bind with the vram memory from the texture with zero mips" );
            }
        }
        //
        // If we have a normal texture with data
        //
        else if( bUseStaging )
        {
            // Create a host-visible staging buffer that contains the raw image data
            // This buffer is used as a transfer source for the buffer copy
            VkBuffer           StagingBuffer;
            VkBufferCreateInfo BufferCreateInfo
            {
                .sType          = VkStructureType::VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO
            ,   .pNext          = nullptr
            ,   .flags          = 0
            ,   .size           = Setup.m_AllFacesData.size()
            ,   .usage          = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
            ,   .sharingMode    = VK_SHARING_MODE_EXCLUSIVE
            };
                
            if( auto VKErr = vkCreateBuffer( m_Device->m_VKDevice
                                           , &BufferCreateInfo
                                           , m_Device->m_Instance->m_pVKAllocator
                                           , &StagingBuffer); VKErr )
            {
                m_Device->m_Instance->ReportError( VKErr, "Fail to create a staging buffer while creating a texture" );
                return VGPU_ERROR( xgpu::device::error::FAILURE, "Fail to create a staging buffer while creating a texture" );
            }

            // Get memory requirements for the staging buffer (alignment, memory type bits)
            vkGetBufferMemoryRequirements( m_Device->m_VKDevice, StagingBuffer, &MemoryRequirements );
            MemoryAllocInfo.allocationSize = MemoryRequirements.size;
            assert(MemoryAllocInfo.allocationSize >= MemoryRequirements.size);

            // Get memory type index for a host visible buffer
            m_Device->getMemoryType( MemoryRequirements.memoryTypeBits
                                   , VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                   , MemoryAllocInfo.memoryTypeIndex );

            VkDeviceMemory  StagingMemory{};
            if( auto VKErr = vkAllocateMemory( m_Device->m_VKDevice
                                             , &MemoryAllocInfo
                                             , m_Device->m_Instance->m_pVKAllocator
                                             , &StagingMemory ); VKErr )
            {
                m_Device->m_Instance->ReportError( VKErr, "Fail to allocated memory for the texture" );
                return VGPU_ERROR( xgpu::device::error::FAILURE, "Fail to allocated memory for the texture" );
            }

            if( auto VKErr = vkBindBufferMemory( m_Device->m_VKDevice
                                               , StagingBuffer
                                               , StagingMemory
                                               , 0 ); VKErr )
            {
                m_Device->m_Instance->ReportError(VKErr, "Fail to bind with the allocated memory for the texture");
                return VGPU_ERROR( xgpu::device::error::FAILURE, "Fail to bind with the allocated memory for the texture");
            }

            //
            // Copy texture data into staging buffer
            //

            // Get System pointer 
            std::byte* pData{ nullptr };
            if( auto VKErr = vkMapMemory( m_Device->m_VKDevice
                                        , StagingMemory
                                        , 0
                                        , MemoryRequirements.size
                                        , 0
                                        , (void **)&pData ); VKErr )
            {
                m_Device->m_Instance->ReportError(VKErr, "Fail to map with the allocated memory for the texture");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to map with the allocated memory for the texture");
            }
            
            assert(MemoryRequirements.size >= Setup.m_AllFacesData.size() );
            memcpy(pData, Setup.m_AllFacesData.data(), Setup.m_AllFacesData.size());
            vkUnmapMemory( m_Device->m_VKDevice, StagingMemory );

            // Setup buffer copy regions for each mip level
            std::vector<VkBufferImageCopy>  BufferCopyRegions{};

            // Load mip levels into linear textures that are used to copy from
            std::uint32_t Offset{ 0 };
            for (std::uint32_t iFace = 0; iFace < Setup.m_FaceCount; iFace++)
            {
                auto Width  = Setup.m_Width;
                auto Height = Setup.m_Height;

                for (std::uint32_t i = 0; i < m_nMips; i++)
                {
                    BufferCopyRegions.emplace_back
                    (
                        VkBufferImageCopy
                        {
                            .bufferOffset           = Offset
                        ,   .imageSubresource
                            {
                                .aspectMask         = VK_IMAGE_ASPECT_COLOR_BIT
                            ,   .mipLevel           = i
                            ,   .baseArrayLayer     = iFace
                            ,   .layerCount         = 1//Setup.m_FaceCount
                            }
                        ,   .imageExtent
                            {
                                .width              = static_cast<std::uint32_t>(Width)
                            ,   .height             = static_cast<std::uint32_t>(Height)
                            ,   .depth              = 1
                            }
                        }
                    );

                    // Get ready for next mip
                    Width   = std::max(1, Width >> 1);
                    Height  = std::max(1, Height >> 1);
                    Offset += static_cast<uint32_t>(Setup.m_MipSizes[i].m_SizeInBytes);
                }
            }

            // Create optimal tiled target image
            // TRANSFER_SRC_BIT (alongside the DST_BIT this path already needed for its own upload) lets
            // Readback/ReadTexture read this image back later - the same VUID (-00186, "usage flag") that
            // caught its absence here also caught it on the m_nMips==0/render-target branch above, so every
            // texture xGPU creates can be read back with UpdateRegion/Readback, not just the ones it happens
            // to be convenient for.
            VkImageCreateInfo ImageCreateInfo
            {
                .sType          = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO
            ,   .pNext          = nullptr
            ,   .flags          = Setup.m_isCubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0u
            ,   .imageType      = VK_IMAGE_TYPE_2D
            ,   .format         = m_VKFormat
            ,   .extent
                {
                    .width      = static_cast<std::uint32_t>(Setup.m_Width)
                ,   .height     = static_cast<std::uint32_t>(Setup.m_Height)
                ,   .depth      = 1
                }
            ,   .mipLevels      = m_nMips
            ,   .arrayLayers    = Setup.m_FaceCount
            ,   .samples        = VK_SAMPLE_COUNT_1_BIT
            ,   .tiling         = VK_IMAGE_TILING_OPTIMAL
            ,   .usage          = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            ,   .sharingMode    = VK_SHARING_MODE_EXCLUSIVE
            ,   .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED
            };

            if( auto VKErr = vkCreateImage( m_Device->m_VKDevice
                                          , &ImageCreateInfo
                                          , m_Device->m_Instance->m_pVKAllocator
                                          , &m_VKImage ); VKErr )
            {
                m_Device->m_Instance->ReportError(VKErr, "Fail to create the vulkan Image for a texture");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create the vulkan Image for a texture");
            }

            //
            // Let allocate space for the image in vram
            //
            vkGetImageMemoryRequirements( m_Device->m_VKDevice, m_VKImage, &MemoryRequirements );

            MemoryAllocInfo.allocationSize = MemoryRequirements.size;

            m_Device->getMemoryType( MemoryRequirements.memoryTypeBits
                                , VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                                , MemoryAllocInfo.memoryTypeIndex );

            // TODO: THIS SHOULD BE THREAD SAFE??
            if( auto VKErr = vkAllocateMemory( m_Device->m_VKDevice
                                             , &MemoryAllocInfo
                                             , m_Device->m_Instance->m_pVKAllocator
                                             , &m_VKDeviceMemory ); VKErr )
            {
                m_Device->m_Instance->ReportError(VKErr, "Fail to allocate the necesary memory for the texture in vram");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to allocate the necesary memory for the texture in vram");
            }

            if( auto VKErr = vkBindImageMemory  ( m_Device->m_VKDevice
                                                , m_VKImage
                                                , m_VKDeviceMemory
                                                , 0 ); VKErr )
            {
                m_Device->m_Instance->ReportError(VKErr, "Fail to bind with the vram memory from the texture");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to bind with the vram memory from the texture" );
            }

            // Image barrier for optimal image
            // The sub resource range describes the regions of the image we will be transition
            VkImageSubresourceRange SubresourceRange
            {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT
            ,   .baseMipLevel   = 0
            ,   .levelCount     = m_nMips
            ,   .layerCount     = Setup.m_FaceCount
            };

            // Optimal image will be used as destination for the copy, so we must transfer from our
            // initial undefined image layout to the transfer destination layout
            if (auto [PerDevice, Error] = m_Device->m_Instance->getLocalStorage().getOrCreatePerDevice(*m_Device); Error) return Error;
            else
            {
                setImageLayout(PerDevice.m_VKSetupCmdBuffer
                              , m_VKImage
                              , VK_IMAGE_ASPECT_COLOR_BIT
                              , VK_IMAGE_LAYOUT_UNDEFINED
                              , VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                              , SubresourceRange );

                // Copy mip levels from staging buffer
                vkCmdCopyBufferToImage(PerDevice.m_VKSetupCmdBuffer
                                      , StagingBuffer
                                      , m_VKImage
                                      , VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                                      , static_cast<uint32_t>(BufferCopyRegions.size())
                                      , BufferCopyRegions.data() );

                OutImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                // Change texture image layout to shader read after all mip levels have been copied
                setImageLayout(PerDevice.m_VKSetupCmdBuffer
                              , m_VKImage
                              , VK_IMAGE_ASPECT_COLOR_BIT
                              , VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                              , OutImageLayout
                              , SubresourceRange );

                // Clean up staging resources
                PerDevice.FlushVKSetupCommandBuffer();

                // Clean up the staging buffer
                vkDestroyBuffer( m_Device->m_VKDevice, StagingBuffer, m_Device->m_Instance->m_pVKAllocator );
                vkFreeMemory( m_Device->m_VKDevice, StagingMemory, m_Device->m_Instance->m_pVKAllocator);
            }
        }
        else
        {
            // Prefer using optimal tiling, as linear tiling 
            // may support only a small set of features 
            // depending on implementation (e.g. no mip maps, only one layer, etc.)

            // Load mip map level 0 to linear tiling image
            VkImageCreateInfo ImageCreateInfo
            {
                .sType              = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO
            ,   .pNext              = nullptr
            ,   .imageType          = VK_IMAGE_TYPE_2D
            ,   .format             = m_VKFormat
            ,   .extent
                {
                    .width  = static_cast<uint32_t>(Setup.m_Width)
                ,   .height = static_cast<uint32_t>(Setup.m_Height)
                ,   .depth  = 1
                }
            ,   .mipLevels          = 1
            ,   .arrayLayers        = 1
            ,   .samples            = VK_SAMPLE_COUNT_1_BIT
            ,   .tiling             = VK_IMAGE_TILING_LINEAR
            ,   .usage              = VK_IMAGE_USAGE_SAMPLED_BIT
            ,   .sharingMode        = VK_SHARING_MODE_EXCLUSIVE
            ,   .initialLayout      = VK_IMAGE_LAYOUT_PREINITIALIZED
            };
            VkImage MappableImage{};

            if( auto VKErr = vkCreateImage( m_Device->m_VKDevice
                                          , &ImageCreateInfo
                                          , m_Device->m_Instance->m_pVKAllocator
                                          , &MappableImage
                                          ); VKErr )
            {
                m_Device->m_Instance->ReportError(VKErr, "Fail to Create a Mappable Image for the texture");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to Create a Mappable Image for the texture");
            }

            // Get memory requirements for this image 
            // like size and alignment
            vkGetImageMemoryRequirements( m_Device->m_VKDevice, MappableImage, &MemoryRequirements );

            // Set memory allocation size to required memory size
            MemoryAllocInfo.allocationSize = MemoryRequirements.size;

            // Get memory type that can be mapped to host memory
            m_Device->getMemoryType( MemoryRequirements.memoryTypeBits
                                , VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                , MemoryAllocInfo.memoryTypeIndex );

            // Allocate host memory
            VkDeviceMemory MappableMemory{};
            if( auto VKErr = vkAllocateMemory( m_Device->m_VKDevice
                                             , &MemoryAllocInfo
                                             , m_Device->m_Instance->m_pVKAllocator
                                             , &MappableMemory
                                             ); VKErr )
            {
                m_Device->m_Instance->ReportError(VKErr, "Fail to allocate memory for the mappable Image for the texture");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to allocate memory for the mappable Image for the texture");
            }

            // Bind allocated image for use
            if( auto VKErr = vkBindImageMemory  ( m_Device->m_VKDevice
                                                , MappableImage
                                                , MappableMemory
                                                , 0
                                                ); VKErr )
            {
                m_Device->m_Instance->ReportError(VKErr, "Fail to bind the mappable Image for the texture");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to bind the mappable Image for the texture");
            }

            // Get sub resource layout
            // Mip map count, array layer, etc.
            VkImageSubresource ImageSubresource
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT
            };

            // Get sub resources layout 
            // Includes row pitch, size offsets, etc.
            VkSubresourceLayout SubresourceLayout;
            vkGetImageSubresourceLayout( m_Device->m_VKDevice
                                       , MappableImage
                                       , &ImageSubresource
                                       , &SubresourceLayout
                                       );

            // Map image memory
            void* pData{};
            if( auto VKErr = vkMapMemory( m_Device->m_VKDevice
                                        , MappableMemory
                                        , 0
                                        , MemoryRequirements.size
                                        , 0
                                        , &pData
                                        ); VKErr )
            {
                m_Device->m_Instance->ReportError(VKErr, "Fail to Map memory between the CPU and the GPU while uploading a texture");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to Map memory between the CPU and the GPU while uploading a texture");
            }

            // Copy image data into memory
            memcpy( pData
                  , Setup.m_AllFacesData.data()
                  , Setup.m_MipSizes.data() ? Setup.m_MipSizes[ ImageSubresource.mipLevel ].m_SizeInBytes : Setup.m_AllFacesData.size()
                  );

            vkUnmapMemory(m_Device->m_VKDevice, MappableMemory );

            // Linear tiled images don't need to be staged
            // and can be directly used as textures
            m_VKImage         = MappableImage;
            m_VKDeviceMemory  = MappableMemory;
            OutImageLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                
            // Setup image memory barrier transfer image to shader read layout

            // The sub resource range describes the regions of the image we will be transition
            VkImageSubresourceRange ImageSubresourceRange
            {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT     // Image only contains color data
            ,   .baseMipLevel   = 0                             // Start at first mip level
            ,   .levelCount     = 1                             // Only one mip level, most implementations won't support more for linear tiled images
            ,   .layerCount     = 1                             // The 2D texture only has one layer 
            };

            if (auto [PerDevice, Error] = m_Device->m_Instance->getLocalStorage().getOrCreatePerDevice(*m_Device); Error) return Error;
            else
            {
                setImageLayout( PerDevice.m_VKSetupCmdBuffer
                              , m_VKImage
                              , VK_IMAGE_ASPECT_COLOR_BIT
                              , VK_IMAGE_LAYOUT_PREINITIALIZED
                              , OutImageLayout
                              , ImageSubresourceRange );

                // Clean up staging resources
                PerDevice.FlushVKSetupCommandBuffer();
            }
        }

        // Create image view
        // Textures are not directly accessed by the shaders and
        // are abstracted by image views containing additional
        // information and sub resource ranges
        VkImageViewCreateInfo ImageViewCreateInfo
        {
            .sType              = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
        ,   .pNext              = nullptr
        ,   .image              = m_VKImage
        ,   .viewType           = Setup.m_isCubemap ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D
        ,   .format             = m_VKFormat
        ,   .components         = 
            {
                .r              = VK_COMPONENT_SWIZZLE_R
            ,   .g              = VK_COMPONENT_SWIZZLE_G
            ,   .b              = VK_COMPONENT_SWIZZLE_B
            ,   .a              = VK_COMPONENT_SWIZZLE_A
            }
        ,   .subresourceRange
            {
                .aspectMask     = isDepth ? std::uint32_t(VK_IMAGE_ASPECT_DEPTH_BIT) | (xgpu::texture::isStencil(m_Format)?VK_IMAGE_ASPECT_STENCIL_BIT:0) : VK_IMAGE_ASPECT_COLOR_BIT
            ,   .baseMipLevel   = 0
                // Linear tiling usually won't support mip maps
                // Only set mip map count if optimal tiling is used
            ,   .levelCount     = (bUseStaging) ? static_cast<std::uint32_t>(m_nMips) : 1u
            ,   .baseArrayLayer = 0
            ,   .layerCount     = Setup.m_FaceCount
            }
        };
        if( auto VKErr = vkCreateImageView( m_Device->m_VKDevice
                                          , &ImageViewCreateInfo
                                          , m_Device->m_Instance->m_pVKAllocator
                                          , &m_VKView 
                                          ); VKErr )
        {
            m_Device->m_Instance->ReportError( VKErr, "Fail to create an Image view of the texture" );
            return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create an Image view of the texture");
        }


        //
        // The Descriptor Image Info the sampler will be set later
        //
        m_VKDescriptorImageInfo.imageView   = m_VKView;
        m_VKDescriptorImageInfo.imageLayout = isDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // True only for the "with data" staging path above (OutImageLayout was set there, right after its
        // own real UNDEFINED->TRANSFER_DST_OPTIMAL->SHADER_READ_ONLY_OPTIMAL transitions) - the "empty"
        // (m_nMips==0) path's image genuinely stays at UNDEFINED despite the descriptor above, see this
        // field's own comment in the header.
        m_bContentReady = (OutImageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        return nullptr;
    }

    //---------------------------------------------------------------------------------------
    // Same staging-buffer + vkCmdCopyBufferToImage idiom as Initialize's own staging path above, just a
    // sub-rectangle of an already-created image instead of the whole thing at creation time. The image is
    // expected to be resting at SHADER_READ_ONLY_OPTIMAL (where Initialize always leaves it, and where a
    // finished render-pass color attachment also ends up - xgpu_vulkan_renderpass.cpp's finalLayout), and is
    // left there afterward.
    xgpu::device::error* texture::UpdateRegion
    ( int                           OffsetX
    , int                           OffsetY
    , int                           Width
    , int                           Height
    , std::span<const std::byte>   Source
    ) noexcept
    {
        if (OffsetX < 0 || OffsetY < 0 || Width <= 0 || Height <= 0 || OffsetX + Width > m_Width || OffsetY + Height > m_Height)
        {
            m_Device->m_Instance->ReportError("UpdateTexture: region is out of bounds for this texture");
            return VGPU_ERROR(xgpu::device::error::FAILURE, "UpdateTexture: region is out of bounds for this texture");
        }

        VkBuffer        StagingBuffer{};
        VkDeviceMemory  StagingMemory{};
        {
            VkBufferCreateInfo BufferCreateInfo
            { .sType        = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO
            , .size         = static_cast<VkDeviceSize>(Source.size())
            , .usage        = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
            , .sharingMode  = VK_SHARING_MODE_EXCLUSIVE
            };
            if (auto VKErr = vkCreateBuffer(m_Device->m_VKDevice, &BufferCreateInfo, m_Device->m_Instance->m_pVKAllocator, &StagingBuffer); VKErr)
            {
                m_Device->m_Instance->ReportError(VKErr, "Fail to create a staging buffer while updating a texture region");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create a staging buffer while updating a texture region");
            }

            VkMemoryRequirements MemoryRequirements;
            vkGetBufferMemoryRequirements(m_Device->m_VKDevice, StagingBuffer, &MemoryRequirements);

            VkMemoryAllocateInfo MemoryAllocInfo{ .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = MemoryRequirements.size };
            m_Device->getMemoryType(MemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, MemoryAllocInfo.memoryTypeIndex);

            if (auto VKErr = vkAllocateMemory(m_Device->m_VKDevice, &MemoryAllocInfo, m_Device->m_Instance->m_pVKAllocator, &StagingMemory); VKErr)
            {
                m_Device->m_Instance->ReportError(VKErr, "Fail to allocate memory for the texture-region staging buffer");
                vkDestroyBuffer(m_Device->m_VKDevice, StagingBuffer, m_Device->m_Instance->m_pVKAllocator);
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to allocate memory for the texture-region staging buffer");
            }

            if (auto VKErr = vkBindBufferMemory(m_Device->m_VKDevice, StagingBuffer, StagingMemory, 0); VKErr)
            {
                m_Device->m_Instance->ReportError(VKErr, "Fail to bind memory for the texture-region staging buffer");
                vkFreeMemory(m_Device->m_VKDevice, StagingMemory, m_Device->m_Instance->m_pVKAllocator);
                vkDestroyBuffer(m_Device->m_VKDevice, StagingBuffer, m_Device->m_Instance->m_pVKAllocator);
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to bind memory for the texture-region staging buffer");
            }

            void* pData{};
            if (auto VKErr = vkMapMemory(m_Device->m_VKDevice, StagingMemory, 0, MemoryRequirements.size, 0, &pData); VKErr)
            {
                m_Device->m_Instance->ReportError(VKErr, "Fail to map the texture-region staging buffer");
                vkFreeMemory(m_Device->m_VKDevice, StagingMemory, m_Device->m_Instance->m_pVKAllocator);
                vkDestroyBuffer(m_Device->m_VKDevice, StagingBuffer, m_Device->m_Instance->m_pVKAllocator);
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to map the texture-region staging buffer");
            }
            std::memcpy(pData, Source.data(), Source.size());
            vkUnmapMemory(m_Device->m_VKDevice, StagingMemory);
        }

        if (auto [PerDevice, Error] = m_Device->m_Instance->getLocalStorage().getOrCreatePerDevice(*m_Device); Error)
        {
            vkFreeMemory(m_Device->m_VKDevice, StagingMemory, m_Device->m_Instance->m_pVKAllocator);
            vkDestroyBuffer(m_Device->m_VKDevice, StagingBuffer, m_Device->m_Instance->m_pVKAllocator);
            return Error;
        }
        else
        {
            const VkImageSubresourceRange SubresourceRange{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 };

            auto Barrier = [&](VkImageLayout OldLayout, VkImageLayout NewLayout, VkAccessFlags SrcAccess, VkAccessFlags DstAccess, VkPipelineStageFlags SrcStage, VkPipelineStageFlags DstStage)
            {
                VkImageMemoryBarrier ImageMemoryBarrier
                { .sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER
                , .srcAccessMask         = SrcAccess
                , .dstAccessMask         = DstAccess
                , .oldLayout             = OldLayout
                , .newLayout             = NewLayout
                , .srcQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED
                , .dstQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED
                , .image                 = m_VKImage
                , .subresourceRange      = SubresourceRange
                };
                vkCmdPipelineBarrier(PerDevice.m_VKSetupCmdBuffer, SrcStage, DstStage, 0, 0, nullptr, 0, nullptr, 1, &ImageMemoryBarrier);
            };

            // The "empty" (m_nMips==0, render-target-shaped) Initialize path leaves the real image at
            // UNDEFINED despite m_VKDescriptorImageInfo claiming SHADER_READ_ONLY_OPTIMAL (see
            // m_bContentReady's own comment in the header) - the very first UpdateRegion on such a texture
            // (e.g. a freshly created atlas, before anything has ever been rendered into or written to it)
            // must transition FROM its real starting layout, not the descriptor's aspirational one.
            if (m_bContentReady)
            {
                Barrier(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                       , VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT
                       , VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            }
            else
            {
                Barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                       , 0, VK_ACCESS_TRANSFER_WRITE_BIT
                       , VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            }

            VkBufferImageCopy Region
            { .bufferOffset      = 0
            , .bufferRowLength   = 0
            , .bufferImageHeight = 0
            , .imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }
            , .imageOffset       = { OffsetX, OffsetY, 0 }
            , .imageExtent       = { static_cast<std::uint32_t>(Width), static_cast<std::uint32_t>(Height), 1 }
            };
            vkCmdCopyBufferToImage(PerDevice.m_VKSetupCmdBuffer, StagingBuffer, m_VKImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);

            Barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                   , VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT
                   , VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

            PerDevice.FlushVKSetupCommandBuffer();
            m_bContentReady = true;   // the image genuinely rests at SHADER_READ_ONLY_OPTIMAL now, either way
        }

        vkFreeMemory(m_Device->m_VKDevice, StagingMemory, m_Device->m_Instance->m_pVKAllocator);
        vkDestroyBuffer(m_Device->m_VKDevice, StagingBuffer, m_Device->m_Instance->m_pVKAllocator);
        return nullptr;
    }

    //---------------------------------------------------------------------------------------
    // Mirrors xgpu_vulkan_window.cpp's CaptureBackbuffer (transition -> vkCmdCopyImageToBuffer -> transition
    // back, map/memcpy/unmap), generalized to this texture's own VkImage/SHADER_READ_ONLY_OPTIMAL resting
    // layout instead of the swapchain backbuffer/PRESENT_SRC_KHR. No format conversion - each uint32 in Dest
    // is this texture's own m_VKFormat byte order (e.g. R8G8B8A8_UNORM comes back R in the low byte, not the
    // B8G8R8A8 the swapchain's own Screenshot happens to read). Synchronous - the caller is responsible for
    // making sure this texture's GPU work (a render into it, an UpdateRegion) is already complete.
    xgpu::device::error* texture::Readback
    ( std::vector<std::uint32_t>&  Dest
    , int&                          Width
    , int&                          Height
    ) noexcept
    {
        const std::uint32_t W        = m_Width;
        const std::uint32_t H        = m_Height;
        const VkDeviceSize  DataSize = VkDeviceSize(W) * VkDeviceSize(H) * 4;

        VkBuffer        ReadbackBuffer{};
        VkDeviceMemory  ReadbackMemory{};
        {
            VkBufferCreateInfo BufferCreateInfo
            { .sType        = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO
            , .size         = DataSize
            , .usage        = VK_BUFFER_USAGE_TRANSFER_DST_BIT
            , .sharingMode  = VK_SHARING_MODE_EXCLUSIVE
            };
            if (auto VKErr = vkCreateBuffer(m_Device->m_VKDevice, &BufferCreateInfo, m_Device->m_Instance->m_pVKAllocator, &ReadbackBuffer); VKErr)
            {
                m_Device->m_Instance->ReportError(VKErr, "Fail to create the texture readback buffer");
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to create the texture readback buffer");
            }

            VkMemoryRequirements MemoryRequirements;
            vkGetBufferMemoryRequirements(m_Device->m_VKDevice, ReadbackBuffer, &MemoryRequirements);

            VkMemoryAllocateInfo MemoryAllocInfo{ .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = MemoryRequirements.size };
            m_Device->getMemoryType(MemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, MemoryAllocInfo.memoryTypeIndex);

            if (auto VKErr = vkAllocateMemory(m_Device->m_VKDevice, &MemoryAllocInfo, m_Device->m_Instance->m_pVKAllocator, &ReadbackMemory); VKErr)
            {
                m_Device->m_Instance->ReportError(VKErr, "Fail to allocate memory for the texture readback buffer");
                vkDestroyBuffer(m_Device->m_VKDevice, ReadbackBuffer, m_Device->m_Instance->m_pVKAllocator);
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to allocate memory for the texture readback buffer");
            }

            if (auto VKErr = vkBindBufferMemory(m_Device->m_VKDevice, ReadbackBuffer, ReadbackMemory, 0); VKErr)
            {
                m_Device->m_Instance->ReportError(VKErr, "Fail to bind memory for the texture readback buffer");
                vkFreeMemory(m_Device->m_VKDevice, ReadbackMemory, m_Device->m_Instance->m_pVKAllocator);
                vkDestroyBuffer(m_Device->m_VKDevice, ReadbackBuffer, m_Device->m_Instance->m_pVKAllocator);
                return VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to bind memory for the texture readback buffer");
            }
        }

        if (auto [PerDevice, Error] = m_Device->m_Instance->getLocalStorage().getOrCreatePerDevice(*m_Device); Error)
        {
            vkFreeMemory(m_Device->m_VKDevice, ReadbackMemory, m_Device->m_Instance->m_pVKAllocator);
            vkDestroyBuffer(m_Device->m_VKDevice, ReadbackBuffer, m_Device->m_Instance->m_pVKAllocator);
            return Error;
        }
        else
        {
            const VkImageSubresourceRange SubresourceRange{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 };

            auto Barrier = [&](VkImageLayout OldLayout, VkImageLayout NewLayout, VkAccessFlags SrcAccess, VkAccessFlags DstAccess, VkPipelineStageFlags SrcStage, VkPipelineStageFlags DstStage)
            {
                VkImageMemoryBarrier ImageMemoryBarrier
                { .sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER
                , .srcAccessMask         = SrcAccess
                , .dstAccessMask         = DstAccess
                , .oldLayout             = OldLayout
                , .newLayout             = NewLayout
                , .srcQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED
                , .dstQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED
                , .image                 = m_VKImage
                , .subresourceRange      = SubresourceRange
                };
                vkCmdPipelineBarrier(PerDevice.m_VKSetupCmdBuffer, SrcStage, DstStage, 0, 0, nullptr, 0, nullptr, 1, &ImageMemoryBarrier);
            };

            Barrier(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                   , VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT
                   , VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

            VkBufferImageCopy Region
            { .bufferOffset      = 0
            , .bufferRowLength   = 0
            , .bufferImageHeight = 0
            , .imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }
            , .imageOffset       = { 0, 0, 0 }
            , .imageExtent       = { W, H, 1 }
            };
            vkCmdCopyImageToBuffer(PerDevice.m_VKSetupCmdBuffer, m_VKImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ReadbackBuffer, 1, &Region);

            Barrier(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                   , VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT
                   , VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

            PerDevice.FlushVKSetupCommandBuffer();
        }

        xgpu::device::error* Result = nullptr;
        void* pMapped{};
        if (auto VKErr = vkMapMemory(m_Device->m_VKDevice, ReadbackMemory, 0, DataSize, 0, &pMapped); VKErr)
        {
            m_Device->m_Instance->ReportError(VKErr, "Fail to map the texture readback buffer");
            Result = VGPU_ERROR(xgpu::device::error::FAILURE, "Fail to map the texture readback buffer");
            Width = Height = 0;
            Dest.clear();
        }
        else
        {
            Width  = static_cast<int>(W);
            Height = static_cast<int>(H);
            Dest.resize(static_cast<std::size_t>(W) * static_cast<std::size_t>(H));
            std::memcpy(Dest.data(), pMapped, static_cast<std::size_t>(DataSize));
            vkUnmapMemory(m_Device->m_VKDevice, ReadbackMemory);
        }

        vkFreeMemory(m_Device->m_VKDevice, ReadbackMemory, m_Device->m_Instance->m_pVKAllocator);
        vkDestroyBuffer(m_Device->m_VKDevice, ReadbackBuffer, m_Device->m_Instance->m_pVKAllocator);
        return Result;
    }

    void texture::DeathMarch(xgpu::texture&& Texture) noexcept
    {
        if (!m_Device) return;
        m_Device->Destroy(std::move(Texture));
        m_Device.reset();
    }

}

