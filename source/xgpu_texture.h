namespace xgpu
{
    // What a shader reads a texture and a sampler by (texture_table, XGPU_TEXTURE / XGPU_SAMPLER): small values, 0 never valid. A
    // texture gets its id when it is created and keeps it until it is destroyed (the id is reused only after that has retired)
    struct texture_id
    {
        std::uint32_t   m_Value = 0;
        constexpr bool  isValid     ( void ) const noexcept { return m_Value != 0; }
        constexpr bool  operator==  ( const texture_id& ) const noexcept = default;
    };
    struct sampler_id
    {
        std::uint32_t   m_Value = 0;
        constexpr bool  isValid     ( void ) const noexcept { return m_Value != 0; }
        constexpr bool  operator==  ( const sampler_id& ) const noexcept = default;
    };

    struct texture
    {
        enum class address_mode : std::uint8_t
        { CLAMP
        , CLAMP_BORDER
        , TILE
        , MIRROR
        , MIRROR_CLAMP
        , ENUM_COUNT
        };

        enum class format : std::uint8_t
        {   INVALID
        ,   B8G8R8A8_UNORM
        ,   B8G8R8U8_UNORM
        ,   A8R8G8B8_UNORM
        ,   U8R8G8B8_UNORM
        ,   R8G8B8U8_UNORM
        ,   R8G8B8A8_UNORM
        ,   XCOLOR = R8G8B8A8_UNORM
        ,   R8G8B8_UNORM
        ,   R8_UNORM                // single-channel, uncompressed - e.g. a raw SDF font atlas, no block compression at all
        ,   R4G4B4A4_UNORM = 10
        ,   R5G6B5_UNORM
        ,   B5G5R5A1_UNORM
        ,   XCOLOR_END              // end of the range of xcolor
        ,   BC1_4RGB_UNORM
        ,   BC1_4RGBA1_UNORM
        ,   BC2_8RGBA_UNORM
        ,   BC3_8RGBA_UNORM
        ,   BC4_4R_UNORM
        ,   BC5_8RG_UNORM
        ,   BC6H_8RGB_UFLOAT
        ,   BC6H_8RGB_SFLOAT
        ,   BC7_8RGBA_UNORM
        ,   ETC2_4RGB_UNORM
        ,   ETC2_4RGBA1_UNORM
        ,   ETC2_8RGBA_UNORM
        ,   R32G32B32A32_SFLOAT
        ,   R32G32B32_SFLOAT
        ,   R32G32_SFLOAT
        ,   R32_SFLOAT
        ,   R16G16B16A16_SFLOAT
        ,   R16G16_SFLOAT
        ,   R16_SFLOAT
        ,   DEPTH_U16
        ,   DEPTH_U24_STENCIL_U8
        ,   DEPTH_F32
        ,   DEPTH_F32_STENCIL_U8
        ,   A2B10G10R10_UNORM       // packed 32 bits (appended: the values above are stored in resources)
        ,   B10G11R11_UFLOAT        // packed 32 bits, unsigned floats (HDR color without alpha)
        ,   R32_UINT                // one 32 bit unsigned integer (packed data a shader reads with texelFetch: usampler2D)
        ,   ENUM_COUNT
        ,   DEFAULT = R8G8B8A8_UNORM
        };

        inline static bool isDepth  ( const format Fmt ) noexcept { return Fmt >= format::DEPTH_U16 && Fmt <= format::DEPTH_F32_STENCIL_U8; }
        inline static bool isStencil( const format Fmt ) noexcept { return Fmt == format::DEPTH_U24_STENCIL_U8 || Fmt == format::DEPTH_F32_STENCIL_U8; }

        enum class transfer_mode : std::uint8_t
        { SYNC
        , ASYNC
        };

        struct setup
        {
            struct mip_size
            {
                std::uint32_t   m_SizeInBytes;
            };

            format                                  m_Format                { format::DEFAULT };
            transfer_mode                           m_TransferMode          { transfer_mode::SYNC };
            std::array<address_mode,3>              m_AdressModes           { address_mode::TILE, address_mode::TILE, address_mode::TILE };
            int                                     m_Width                 { -1 };
            int                                     m_Height                { -1 };
            std::span<const mip_size>               m_MipSizes              {};
            std::span<const std::byte>              m_AllFacesData          {};
            std::span<const std::span<const std::byte>> m_MipFaceData       {};     // instead of m_AllFacesData: the same bytes in pieces, one per face and mip
                                                                                    // (face * mips + mip), copied to the staging ring one after another
            std::uint32_t                           m_FaceCount             {1};
            // >0: the image holds every mip of m_MipSizes but gets (and shows) only the mips from this one - the data's pieces start here
            // (per face, mips m_FirstMip..); device::ExtendTexture uploads the ones above later, into the same image (a streamed texture)
            std::uint32_t                           m_FirstMip              {0};
            bool                                    m_isGamma               { true };
            bool                                    m_isCubemap             { false };
            const char*                             m_pName                 { nullptr };   // what it is - "Category: detail", a literal (the profiler's memory map; null: image)
        };

        inline
        std::array<int,3>           getTextureDimensions                ( void ) const noexcept;

        inline
        int                         getMipCount                         ( void ) const noexcept;

        inline
        format                      getFormat                           ( void ) const noexcept;

        inline
        bool                        isCubemap                           ( void ) const noexcept;

        inline
        std::array<address_mode,3>  getAdressModes                      ( void ) const noexcept;
        // Its index in the device's texture heap (texture_table, the material's parameter block)
        inline
        texture_id                  getID                               ( void ) const noexcept;
        // Its own sampler: the device's sampler table entry for its address modes (device::getSampler), a material slot's default
        inline
        sampler_id                  getSampler                          ( void ) const noexcept;

                                    texture                             ( void ) noexcept = default;
                                    texture                             ( const texture& ) noexcept = default;
                                    texture                             ( texture&& ) noexcept = default;
        // Assigning over a handle lets go of the old image as the destructor does - through the deferred destroy (as xgpu::buffer)
        inline texture&             operator=                           ( const texture& Other ) noexcept;
        inline texture&             operator=                           ( texture&& Other ) noexcept;
        inline                     ~texture                             ( void ) noexcept;
        inline void                 Release                             ( void ) noexcept;      // the last handle: into the device's deferred destroy

        std::shared_ptr<details::texture_handle> m_Private;
    };
}