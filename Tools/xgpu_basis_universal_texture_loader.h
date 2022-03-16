#define BASISU_NO_ITERATOR_DEBUG_LEVEL
#include "../../dependencies/basis_universal/transcoder/basisu_transcoder.h"

namespace xgpu::tools::basis_universal
{
  //  inline static basist::etc1_global_selector_codebook g_BasisGlobalSelectorCodebook(basist::g_global_selector_cb_size, basist::g_global_selector_cb);
    inline static bool init = false;

    //------------------------------------------------------------------------------------------------

    xgpu::texture vtk2_Loader( xgpu::device& Device, const char* pFileName )
    {
        if(false == init ) 
        {
            basist::basisu_transcoder_init();
            init = true;
        }

        //
        // Read the file
        //
        long                         FileSize;
        std::unique_ptr<std::byte[]> FileMemory;
        {
            FILE* fp;
            if( fopen_s(&fp, pFileName, "rb" ) ) return {};
            fseek( fp, 0, SEEK_END);
            FileSize = ftell(fp);
            fseek(fp, 0, SEEK_SET );
            FileMemory = std::make_unique<std::byte[]>( FileSize );
            fread(FileMemory.get(), FileSize, 1, fp );
            fclose(fp);
        }

        //
        // Decode
        //
        basist::ktx2_transcoder Decoder;//&g_BasisGlobalSelectorCodebook);
        if(!Decoder.init( FileMemory.get(), static_cast<uint32_t>(FileSize))) return {};

        std::array< xgpu::texture::setup::mip, 32 > Mips;   // Decoder.get_levels();
        xgpu::texture::setup                        Setup;

        Setup.m_Width       = Decoder.get_width();
        Setup.m_Height      = Decoder.get_height();
        Setup.m_ArrayCount  = std::max(Setup.m_ArrayCount, static_cast<int>(Decoder.get_layers() * Decoder.get_faces()));
        Setup.m_Type        = xgpu::texture::type::GAMMA;

        if (Decoder.get_faces() == 6)
        {
            // This is a cube map
        }

        //
        // Get the desired texture format
        //
        const auto Format = [&]
        {
            if (Decoder.get_has_alpha())
            {
                Setup.m_Format = xgpu::texture::format::BC3_8RGBA;
                return basist::transcoder_texture_format::cTFBC3_RGBA;
            }

            Setup.m_Format = xgpu::texture::format::BC1_4RGB;
            return basist::transcoder_texture_format::cTFBC1_RGB;
        }();
        const auto BytesPerBlock = basis_get_bytes_per_block_or_pixel(Format);

        //
        // Ready to decode now...
        //
        if (!Decoder.start_transcoding()) return {};

        //
        // Compute the size of mips and face size
        //
        const auto MipCount   = Decoder.get_levels();
        const auto LayerCount = std::max(1u, Decoder.get_layers());
        const auto FaceCount  = Decoder.get_faces();

        
        std::uint32_t                   FaceSize = 0;
        for (auto iMip = 0u; iMip < MipCount; ++iMip)
        {
            basist::ktx2_image_level_info   LevelInfo;
            if (!Decoder.get_image_level_info(LevelInfo, iMip, 0, 0)) continue;

            Mips[iMip].m_Size = LevelInfo.m_total_blocks * BytesPerBlock;
            FaceSize += static_cast<std::uint32_t>(Mips[iMip].m_Size);
        }
        Setup.m_MipChain = { Mips.data(), MipCount };

        //
        // Transcode the data out
        //
        const size_t TextureDataSize = FaceSize * LayerCount * FaceCount;
        auto TextureData = std::make_unique<std::byte[]>(TextureDataSize);

        std::size_t DataOffset = 0;
        for( auto iLayer = 0u; iLayer < LayerCount; ++iLayer )
        for( auto iFace  = 0u; iFace  < FaceCount;  ++iFace  )
        for( auto iMip   = 0u; iMip   < MipCount;   ++iMip   )
        {
            basist::ktx2_image_level_info   LevelInfo;
            if (!Decoder.get_image_level_info(LevelInfo, iMip, iLayer, iFace)) continue;

            void* pData = TextureData.get() + DataOffset;
            DataOffset += LevelInfo.m_total_blocks * BytesPerBlock;
            if( !Decoder.transcode_image_level(iMip, iLayer, iFace, pData, LevelInfo.m_total_blocks, Format) ) continue;
        }
        Setup.m_Data = { TextureData.get(), TextureDataSize };

        //
        // Create Texture
        //
        xgpu::texture Texture;
        if( auto Err = Device.Create( Texture, Setup ); Err ) return {};

        return Texture;
    }

    //------------------------------------------------------------------------------------------------

    xgpu::texture basis_Loader(xgpu::device& Device, const char* pFileName)
    {
        if (false == init)
        {
            basist::basisu_transcoder_init();
            init = true;
        }

        //
        // Read the file
        //
        long                         FileSize;
        std::unique_ptr<std::byte[]> FileMemory;
        {
            FILE* fp;
            if (fopen_s(&fp, pFileName, "rb")) return {};
            fseek(fp, 0, SEEK_END);
            FileSize = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            FileMemory = std::make_unique<std::byte[]>(FileSize);
            fread(FileMemory.get(), FileSize, 1, fp);
            fclose(fp);
        }

        //
        // Decode 
        //
        basist::basisu_transcoder Decoder;//(&g_BasisGlobalSelectorCodebook);
        if ( !Decoder.validate_header(FileMemory.get(), static_cast<uint32_t>(FileSize) )) return {};

        basist::basisu_file_info FileInfo;
        if( !Decoder.get_file_info(FileMemory.get(), static_cast<uint32_t>(FileSize), FileInfo)) return {};

        const uint32_t ImageIndex = 0;
        basist::basisu_image_info ImageInfo;
        if( !Decoder.get_image_info(FileMemory.get(), static_cast<uint32_t>(FileSize), ImageInfo, ImageIndex )) return {};

        //
        // Collect info
        //
        std::array< xgpu::texture::setup::mip, 32 > Mips;   // Decoder.get_levels();
        xgpu::texture::setup                        Setup;

        Setup.m_Width       = ImageInfo.m_width;
        Setup.m_Height      = ImageInfo.m_height;
        Setup.m_ArrayCount  = 1;
        Setup.m_Type        = xgpu::texture::type::GAMMA;

        const basist::transcoder_texture_format Format = [&]
        {
            if (ImageInfo.m_alpha_flag)
            {
                Setup.m_Format = xgpu::texture::format::BC3_8RGBA;
                return basist::transcoder_texture_format::cTFBC3_RGBA;;
            }
            else
            {
                Setup.m_Format = xgpu::texture::format::BC1_4RGB;
                return basist::transcoder_texture_format::cTFBC1_RGB;
            }
        }();

        //
        // Start decoding
        //
        if( !Decoder.start_transcoding(FileMemory.get(), static_cast<uint32_t>(FileSize) )) return {};

        const auto      MipCount                = ImageInfo.m_total_levels;
        const uint32_t  BytesPerBlock           = basis_get_bytes_per_block_or_pixel(Format);
        std::size_t     DataSize = 0;

        for( auto iMip = 0u; iMip < MipCount; ++iMip )
        {
            basist::basisu_image_level_info LevelInfo;
            if(!Decoder.get_image_level_info(FileMemory.get(), static_cast<uint32_t>(FileSize), LevelInfo, ImageIndex, iMip )) return {};
            Mips[iMip].m_Size = LevelInfo.m_total_blocks * BytesPerBlock;
            DataSize += Mips[iMip].m_Size;
        }
        Setup.m_MipChain = { Mips.data(), MipCount };

        auto ImageData = std::make_unique<std::byte[]>(DataSize);
        Setup.m_Data = { ImageData.get(), DataSize };

        std::size_t DataOffset = 0;
        for (auto iMip = 0u; iMip < MipCount; ++iMip)
        {
            basist::basisu_image_level_info LevelInfo;
            if (!Decoder.get_image_level_info( FileMemory.get(), static_cast<uint32_t>(FileSize), LevelInfo, ImageIndex, iMip)) continue;

            void* pData = ImageData.get() + DataOffset;
            DataOffset += LevelInfo.m_total_blocks * BytesPerBlock;
            if ( !Decoder.transcode_image_level(FileMemory.get(), static_cast<uint32_t>(FileSize), ImageIndex, iMip, pData, LevelInfo.m_total_blocks, Format ) ) continue;
        }

        //
        // Create Texture
        //
        xgpu::texture Texture;
        if (auto Err = Device.Create(Texture, Setup); Err) return {};

        return Texture;
    }
}