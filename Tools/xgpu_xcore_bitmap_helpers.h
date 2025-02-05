#ifndef XGPU_XCORE_BITMAP_HELPERS_H
#define XGPU_XCORE_BITMAP_HELPERS_H
#pragma once

namespace xgpu::tools::bitmap
{
    inline static constexpr auto table_xbmp_to_xgpu_v = []() consteval
    {
        auto Table = std::array<xgpu::texture::format, static_cast<std::size_t>(xcore::bitmap::format::ENUM_COUNT) >
        { xgpu::texture::format::INVALID };

        Table[static_cast<std::size_t>(xcore::bitmap::format::B8G8R8A8)]        = xgpu::texture::format::B8G8R8A8        ;
        Table[static_cast<std::size_t>(xcore::bitmap::format::B8G8R8U8)]        = xgpu::texture::format::B8G8R8U8        ;
        Table[static_cast<std::size_t>(xcore::bitmap::format::A8R8G8B8)]        = xgpu::texture::format::A8R8G8B8        ;
        Table[static_cast<std::size_t>(xcore::bitmap::format::U8R8G8B8)]        = xgpu::texture::format::U8R8G8B8        ;
        Table[static_cast<std::size_t>(xcore::bitmap::format::R8G8B8U8)]        = xgpu::texture::format::R8G8B8U8        ;
        Table[static_cast<std::size_t>(xcore::bitmap::format::R8G8B8A8)]        = xgpu::texture::format::R8G8B8A8        ;
        Table[static_cast<std::size_t>(xcore::bitmap::format::R4G4B4A4)]        = xgpu::texture::format::R4G4B4A4        ;
        Table[static_cast<std::size_t>(xcore::bitmap::format::R5G6B5)]          = xgpu::texture::format::R5G6B5          ;
        Table[static_cast<std::size_t>(xcore::bitmap::format::B5G5R5A1)]        = xgpu::texture::format::B5G5R5A1        ;
        Table[static_cast<std::size_t>(xcore::bitmap::format::BC1_4RGB)]        = xgpu::texture::format::BC1_4RGB        ;
        Table[static_cast<std::size_t>(xcore::bitmap::format::BC1_4RGBA1)]      = xgpu::texture::format::BC1_4RGBA1      ;
        Table[static_cast<std::size_t>(xcore::bitmap::format::BC2_8RGBA)]       = xgpu::texture::format::BC2_8RGBA       ;
        Table[static_cast<std::size_t>(xcore::bitmap::format::BC3_8RGBA)]       = xgpu::texture::format::BC3_8RGBA       ;
        Table[static_cast<std::size_t>(xcore::bitmap::format::BC3_81Y0X_NORMAL)]= xgpu::texture::format::BC3_8RGBA       ;
        Table[static_cast<std::size_t>(xcore::bitmap::format::BC4_4R)]          = xgpu::texture::format::BC4_4R          ;
        Table[static_cast<std::size_t>(xcore::bitmap::format::BC5_8RG)]         = xgpu::texture::format::BC5_8RG         ;
        Table[static_cast<std::size_t>(xcore::bitmap::format::BC5_8YX_NORMAL)]  = xgpu::texture::format::BC5_8RG         ;
        Table[static_cast<std::size_t>(xcore::bitmap::format::BC6H_8RGB_SFLOAT)] = xgpu::texture::format::BC6H_8RGB_SFLOAT ;
        Table[static_cast<std::size_t>(xcore::bitmap::format::BC6H_8RGB_UFLOAT)] = xgpu::texture::format::BC6H_8RGB_UFLOAT ;
        Table[static_cast<std::size_t>(xcore::bitmap::format::BC7_8RGBA)]       = xgpu::texture::format::BC7_8RGBA       ;
        Table[static_cast<std::size_t>(xcore::bitmap::format::ETC2_4RGB)]       = xgpu::texture::format::ETC2_4RGB       ;
        Table[static_cast<std::size_t>(xcore::bitmap::format::ETC2_4RGBA1)]     = xgpu::texture::format::ETC2_4RGBA1     ;
        Table[static_cast<std::size_t>(xcore::bitmap::format::ETC2_8RGBA)]      = xgpu::texture::format::ETC2_8RGBA      ;

        Table[static_cast<std::size_t>(xcore::bitmap::format::R16G16B16A16_FLOAT)] = xgpu::texture::format::R16G16B16A16_FLOAT;
        Table[static_cast<std::size_t>(xcore::bitmap::format::R32G32B32A32_FLOAT)] = xgpu::texture::format::R32G32B32A32_FLOAT;

        return Table;
    }();

    //---------------------------------------------------------------------------------------

    constexpr
    xgpu::texture::format getFormat( xcore::bitmap::format Fmt ) noexcept
    {
        assert(table_xbmp_to_xgpu_v[static_cast<std::size_t>(Fmt)] != xgpu::texture::format::INVALID );
        return table_xbmp_to_xgpu_v[static_cast<std::size_t>(Fmt)];
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
            Setup.m_hasSignedChannels = Bitmap.isSigned();

            if (auto Err = Device.Create(Texture, Setup); Err)
                return Err;

            return nullptr;
        };

        if( Bitmap.getFormat() == xcore::bitmap::format::R8G8B8 )
        {
            xcore::bitmap Temp;
            Temp.CreateBitmap(Bitmap.getWidth(), Bitmap.getHeight());
            Temp.setColorSpace(Bitmap.getColorSpace());
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

#endif