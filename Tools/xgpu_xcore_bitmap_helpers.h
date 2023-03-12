namespace xgpu::tools::bitmap
{
    //---------------------------------------------------------------------------------------

    constexpr
    xgpu::texture::format getFormat( xcore::bitmap::format Fmt ) noexcept
    {
        /*
        switch(Fmt)
        {
            case xcore::bitmap::format::INVALID             : return xgpu::texture::format::INVALID                   ;
            case xcore::bitmap::format::B8G8R8A8            : return xgpu::texture::format::B8G8R8A8                  ;
            case xcore::bitmap::format::B8G8R8U8            : return xgpu::texture::format::B8G8R8U8                  ;
            case xcore::bitmap::format::A8R8G8B8            : return xgpu::texture::format::A8R8G8B8                  ;
            case xcore::bitmap::format::U8R8G8B8            : return xgpu::texture::format::U8R8G8B8                  ;
            case xcore::bitmap::format::R8G8B8U8            : return xgpu::texture::format::R8G8B8U8                  ;
            case xcore::bitmap::format::R8G8B8A8            : return xgpu::texture::format::R8G8B8A8                  ;
            case xcore::bitmap::format::R8G8B8              : return xgpu::texture::format::R8G8B8                    ;
            case xcore::bitmap::format::R4G4B4A4            : return xgpu::texture::format::R4G4B4A4                  ;
            case xcore::bitmap::format::R5G6B5              : return xgpu::texture::format::R5G6B5                    ;
            case xcore::bitmap::format::B5G5R5A1            : return xgpu::texture::format::B5G5R5A1                  ;
            case xcore::bitmap::format::BC1_4RGB            : return xgpu::texture::format::BC1_4RGB                  ;
            case xcore::bitmap::format::BC1_4RGBA1          : return xgpu::texture::format::BC1_4RGBA1                ;
            case xcore::bitmap::format::BC2_8RGBA           : return xgpu::texture::format::BC2_8RGBA                 ;
            case xcore::bitmap::format::BC3_8RGBA           : return xgpu::texture::format::BC3_8RGBA                 ;
            case xcore::bitmap::format::BC4_4R              : return xgpu::texture::format::BC4_4R                    ;
            case xcore::bitmap::format::BC5_8RG             : return xgpu::texture::format::BC5_8RG                   ;
            case xcore::bitmap::format::BC6H_8RGB_FLOAT     : return xgpu::texture::format::BC6H_8RGB_FLOAT           ;
            case xcore::bitmap::format::BC7_8RGBA           : return xgpu::texture::format::BC7_8RGBA                 ;
            case xcore::bitmap::format::ETC2_4RGB           : return xgpu::texture::format::ETC2_4RGB                 ;
            case xcore::bitmap::format::ETC2_4RGBA1         : return xgpu::texture::format::ETC2_4RGBA1               ;
            case xcore::bitmap::format::ETC2_8RGBA          : return xgpu::texture::format::ETC2_8RGBA                ;
        }
        */
        static_assert( (int)xcore::bitmap::format::INVALID                  ==          (int)xgpu::texture::format::INVALID                     );
        static_assert( (int)xcore::bitmap::format::B8G8R8A8                 ==          (int)xgpu::texture::format::B8G8R8A8                    );
        static_assert( (int)xcore::bitmap::format::B8G8R8U8                 ==          (int)xgpu::texture::format::B8G8R8U8                    );
        static_assert( (int)xcore::bitmap::format::A8R8G8B8                 ==          (int)xgpu::texture::format::A8R8G8B8                    );
        static_assert( (int)xcore::bitmap::format::U8R8G8B8                 ==          (int)xgpu::texture::format::U8R8G8B8                    );
        static_assert( (int)xcore::bitmap::format::R8G8B8U8                 ==          (int)xgpu::texture::format::R8G8B8U8                    );
        static_assert( (int)xcore::bitmap::format::R8G8B8A8                 ==          (int)xgpu::texture::format::R8G8B8A8                    );
        static_assert( (int)xcore::bitmap::format::R4G4B4A4                 ==          (int)xgpu::texture::format::R4G4B4A4                    );
        static_assert( (int)xcore::bitmap::format::R5G6B5                   ==          (int)xgpu::texture::format::R5G6B5                      );
        static_assert( (int)xcore::bitmap::format::B5G5R5A1                 ==          (int)xgpu::texture::format::B5G5R5A1                    );
        static_assert( (int)xcore::bitmap::format::BC1_4RGB                 ==          (int)xgpu::texture::format::BC1_4RGB                    );
        static_assert( (int)xcore::bitmap::format::BC1_4RGBA1               ==          (int)xgpu::texture::format::BC1_4RGBA1                  );
        static_assert( (int)xcore::bitmap::format::BC2_8RGBA                ==          (int)xgpu::texture::format::BC2_8RGBA                   );
        static_assert( (int)xcore::bitmap::format::BC3_8RGBA                ==          (int)xgpu::texture::format::BC3_8RGBA                   );
        static_assert( (int)xcore::bitmap::format::BC4_4R                   ==          (int)xgpu::texture::format::BC4_4R                      );
        static_assert( (int)xcore::bitmap::format::BC5_8RG                  ==          (int)xgpu::texture::format::BC5_8RG                     );
        static_assert( (int)xcore::bitmap::format::BC6H_8RGB_FLOAT          ==          (int)xgpu::texture::format::BC6H_8RGB_FLOAT             );
        static_assert( (int)xcore::bitmap::format::BC7_8RGBA                ==          (int)xgpu::texture::format::BC7_8RGBA                   );
        static_assert( (int)xcore::bitmap::format::ETC2_4RGB                ==          (int)xgpu::texture::format::ETC2_4RGB                   );
        static_assert( (int)xcore::bitmap::format::ETC2_4RGBA1              ==          (int)xgpu::texture::format::ETC2_4RGBA1                 );
        static_assert( (int)xcore::bitmap::format::ETC2_8RGBA               ==          (int)xgpu::texture::format::ETC2_8RGBA                  );

        return static_cast<xgpu::texture::format>(Fmt);
    }

    //---------------------------------------------------------------------------------------

    inline
    xgpu::device::error* Create( xgpu::texture& Texture, xgpu::device& Device, const xcore::bitmap& Bitmap ) noexcept
    {
        static constexpr auto Do = [](xgpu::texture& Texture, xgpu::device& Device, const xcore::bitmap& Bitmap) -> xgpu::device::error*
        {
            xgpu::texture::setup                    Setup;
            std::vector<xgpu::texture::setup::mip>  Mips;

            for (int i = 0, end = Bitmap.getMipCount(); i < end; ++i)
            {
                Mips.push_back({ Bitmap.getMipSize(i) });
            }

            Setup.m_Format      = getFormat(Bitmap.getFormat());
            Setup.m_Height      = Bitmap.getHeight();
            Setup.m_Width       = Bitmap.getWidth();
            Setup.m_MipChain    = Mips;
            Setup.m_Data        = std::span{ Bitmap.getMip<std::byte>(0).data(), Bitmap.getFrameSize() };
            Setup.m_Type        = Bitmap.isLinearSpace() ? xgpu::texture::type::LINEAR : xgpu::texture::type::GAMMA;

            if (auto Err = Device.Create(Texture, Setup); Err)
                return Err;

            return nullptr;
        };

        if( Bitmap.getFormat() == xcore::bitmap::format::R8G8B8 )
        {
            xcore::bitmap Temp;
            Temp.CreateBitmap(Bitmap.getWidth(), Bitmap.getHeight());

            int iC=0;
            auto Src       = std::span{ Bitmap.getMip<std::uint8_t>(0).data(), Bitmap.getFrameSize() };
            auto NewPixels = std::span{ Temp.getMip<xcore::icolor>(0).data(), static_cast<std::size_t>(Bitmap.getWidth()* Bitmap.getHeight()) };
            for ( auto y=0u; y < Bitmap.getHeight(); ++y)
            for ( auto x=0u; x < Bitmap.getWidth();  ++x)
            {
                auto& C = NewPixels[ x + y * Bitmap.getWidth()];
                C.m_R = Src[iC++];
                C.m_G = Src[iC++];
                C.m_B = Src[iC++];
                C.m_A = 0xff;
            }

            return Do(Texture, Device, Temp);
        }
        else
        {
            return Do(Texture, Device, Bitmap);
        }

        return nullptr;
    }

}