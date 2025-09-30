namespace xgpu
{
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
        ,   R16G16B16A16_SFLOAT
        ,   DEPTH_U16
        ,   DEPTH_U24_STENCIL_U8
        ,   DEPTH_F32
        ,   DEPTH_F32_STENCIL_U8
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
            std::uint32_t                           m_FaceCount             {1};
            bool                                    m_isGamma               { true };
            bool                                    m_isCubemap             { false };
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

        inline                     ~texture                             ( void ) noexcept;

        std::shared_ptr<details::texture_handle> m_Private;
    };
}