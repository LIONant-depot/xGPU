#include "xcore.h"
#include "xGPU.h"
#include <iostream>
#include <fstream>

#include "../../tools/xgpu_xcore_bitmap_helpers.h"
#include "../../dependencies/xtexture.plugin/dependencies/xbmp_tools/src/xbmp_tools.h"
#include "../../tools/xgpu_basis_universal_texture_loader.h"
#include "../../dependencies/xtexture.plugin/dependencies/xresource_pipeline_v2/dependencies/xproperty/source/examples/imgui/xPropertyImGuiInspector.h"

namespace e05
{
    //------------------------------------------------------------------------------------------------

    struct ivec2
    {
        std::uint32_t x, y;
        XPROPERTY_DEF
        ("ivec2", ivec2, xproperty::settings::vector2_group
        , obj_member_ro<"Width", &ivec2::x >
        , obj_member_ro<"Height", &ivec2::y >
        )
    };
    XPROPERTY_REG(ivec2)

    //------------------------------------------------------------------------------------------------
    
    inline void DebugMessage(std::string_view View)
    {
        printf("%s\n", View.data());
    }

    //------------------------------------------------------------------------------------------------

    struct bitmap_inspector
    {
        void clear() noexcept
        {
            m_FileName.clear();
            m_MipMapSizes.clear();
            if (m_pBitmap && m_pBitmap != &m_BitmapData)
            {
                xcore::memory::AlignedFree(m_pBitmap);
            }
            m_pBitmap = &m_BitmapData;
        }

        ~bitmap_inspector()
        {
            clear();
        }

        void Load( const char* pFileName, bool isXBMP = false ) noexcept
        {
            clear();

            //
            // Get the file name
            //
            m_FileName = pFileName;
            m_FileName = m_FileName.substr(m_FileName.find_last_of("/\\") + 1);
            m_pBitmap  = &m_BitmapData;

            //
            // Load file
            //
            if (isXBMP || m_FileName.find(".xbmp") != (~0ull) )
            {
                if (auto Err = xcore::bitmap::SerializeLoad(m_pBitmap, xcore::string::To<wchar_t>(pFileName)); Err)
                {
                    e05::DebugMessage(Err.getCode().m_pString);
                    std::exit(Err.getCode().m_RawState);
                }
            }
            else if( m_FileName.find(".basis") != (~0ull) )
            {
                assert(false);
                /*
                Texture = xgpu::tools::basis_universal::basis_Loader(Device, pFileName);

                auto Size = Texture.getTextureDimensions();
                auto FrameSize = (Size[0] * Size[1]) / 4;
                m_pBitmap->setup
                ( Size[0]
                , Size[1]
                , xcore::bitmap::format::BC1_4RGB
                , FrameSize
                , std::span( new std::byte [FrameSize], FrameSize + (Texture.getMipCount()*sizeof(int)))
                , true
                , Texture.getMipCount()
                , 1
                );
                */
            }
            else if (m_FileName.find(".dds") != (~0ull))
            {
                if (auto Err = xbmp::tools::loader::LoadDSS(*m_pBitmap, pFileName); Err)
                {
                    e05::DebugMessage(xbmp::tools::getErrorMsg(Err));
                    std::exit(xbmp::tools::getErrorInt(Err));
                }
            }
            else if (auto Err = xbmp::tools::loader::LoadSTDImage(*m_pBitmap, pFileName); Err)
            {
                e05::DebugMessage(xbmp::tools::getErrorMsg(Err));
                std::exit(xbmp::tools::getErrorInt(Err));
            }

            //
            // Determine the size of the fiel
            //
            {
                std::ifstream file(pFileName, std::ios::binary | std::ios::ate);
                m_FileSize = file.tellg();
            }

            //
            // Set the mipmap sizes
            //
            m_MipMapSizes.resize(m_pBitmap->getMipCount());
            for (int i = 0; i < m_pBitmap->getMipCount(); ++i)
            {
                m_MipMapSizes[i].x = std::max(1u, m_pBitmap->getWidth() >> i);
                m_MipMapSizes[i].y = std::max(1u, m_pBitmap->getHeight() >> i);
            }
        }

        std::string             m_FileName;
        std::size_t             m_FileSize;
        xcore::bitmap           m_BitmapData;
        xcore::bitmap*          m_pBitmap = &m_BitmapData;
        std::vector<ivec2>      m_MipMapSizes;

        constexpr static bool isNotValid(const e05::bitmap_inspector& O) noexcept
        {
            return O.m_pBitmap == nullptr || O.m_pBitmap->getFormat() == xcore::bitmap::format::INVALID;
        }

        inline static std::string ConvertToCommas( std::size_t Value ) noexcept
        {
            std::array<char, 50> buffer;
            snprintf(buffer.data(), buffer.size(), "%10.0zu", Value);
            const auto len      = static_cast<int>(strlen(buffer.data()));
            std::array<char, 50> result;
            int new_len = static_cast<int>(result.size() - 1);

            result[new_len] = '\0';
            int  j = new_len - 1;
            auto k = 0;
            for (int i = len - 1; i >= 0; i--) 
            {
                if (k == 3) 
                {
                    if (buffer[i] != ' ') result[j--] = ',';
                    k = 0;
                }
                result[j--] = buffer[i];
                k++;
            }
            return std::format( "{} bytes", &result[j+1]);
        }

        XPROPERTY_DEF
        ( "Bitmap Info", bitmap_inspector
        , obj_member_ro
            < "FileName"
            , &bitmap_inspector::m_FileName
            , member_help<"The file name of the image"
            >>
        , obj_member_ro
            < "Size"
            , +[](bitmap_inspector& I) ->ivec2&
            {
                static ivec2 Out;
                {}
                Out.x = I.m_pBitmap->getWidth();
                Out.y = I.m_pBitmap->getHeight();
                return Out;
            }
            , member_help<"Image Size in pixels" 
            >>
        , obj_member_ro
            < "Format"
            ,  +[](bitmap_inspector& I, bool bRead, std::string& Out )
            {
                assert(bRead);
                switch (I.m_pBitmap->getFormat())
                {
                    case xcore::bitmap::format::R4G4B4A4:   Out = "R4G4B4A4"; break;
                    case xcore::bitmap::format::R5G6B5:     Out = "R5G6B5"; break;
                    case xcore::bitmap::format::B5G5R5A1:   Out = "B5G5R5A1"; break;
                    case xcore::bitmap::format::R8G8B8:     Out = "R8G8B8"; break;
                    case xcore::bitmap::format::R8G8B8U8:   Out = "R8G8B8U8"; break;
                    case xcore::bitmap::format::R8G8B8A8:   Out = "R8G8B8A8"; break;
                    case xcore::bitmap::format::B8G8R8A8:   Out = "B8G8R8A8"; break;
                    case xcore::bitmap::format::B8G8R8U8:   Out = "B8G8R8U8"; break;
                    case xcore::bitmap::format::U8R8G8B8:   Out = "U8R8G8B8"; break;

                    case xcore::bitmap::format::R32G32B32A32_FLOAT: Out = "R32G32B32A32_FLOAT"; break;
                    case xcore::bitmap::format::R16G16B16A16_FLOAT: Out = "R16G16B16A16_FLOAT"; break;

                    case xcore::bitmap::format::BC1_4RGB:           Out = "BC1_4RGB / DXT1"; break;
                    case xcore::bitmap::format::BC1_4RGBA1:         Out = "BC1_4RGBA1 / DXT1"; break;
                    case xcore::bitmap::format::BC2_8RGBA:          Out = "BC2_8RGBA / DXT3"; break;
                    case xcore::bitmap::format::BC3_8RGBA:          Out = "BC3_8RGBA / DXT5"; break;
                    case xcore::bitmap::format::BC3_81Y0X_NORMAL:   Out = "BC3_81Y0X_NORMAL"; break;
                    case xcore::bitmap::format::BC4_4R:             Out = "BC4_4R"; break;
                    case xcore::bitmap::format::BC5_8RG:            Out = "BC5_8RG"; break;
                    case xcore::bitmap::format::BC5_8YX_NORMAL:     Out = "BC5_8YX_NORMAL"; break;
                    case xcore::bitmap::format::BC6H_8RGB_SFLOAT:   Out = "BC6H_8RGB_SFLOAT"; break;
                    case xcore::bitmap::format::BC6H_8RGB_UFLOAT:   Out = "BC6H_8RGB_UFLOAT"; break;
                    case xcore::bitmap::format::BC7_8RGBA:          Out = "BC7_8RGBA"; break;
                    default: Out = "Unexpected format"; break;
                }
            }
            , member_help<"Format from xcore bitmap of the image" 
            >>
        , obj_member_ro
            < "isSigned"
            ,  +[](bitmap_inspector& I, bool bRead, bool& Out )
            {
                Out = I.m_pBitmap->isSigned();
            }
            , member_help < "If the pixel format contains signed data"
            >>
        , obj_member_ro
            < "UAdress Mode"
            ,  +[](bitmap_inspector& I, bool bRead, std::string& Out )
            {
                assert(bRead);
                switch (I.m_pBitmap->getUWrapMode())
                {
                case xcore::bitmap::wrap_mode::CLAMP_TO_EDGE: Out = "CLAMP_TO_EDGE"; break;
                case xcore::bitmap::wrap_mode::MIRROR:        Out = "MIRROR"; break;
                case xcore::bitmap::wrap_mode::WRAP:          Out = "WRAP"; break;
                default: Out = "Unexpected wrap"; break;
                }
            }
            , member_help<"Size in bytes of the file"
            >>
        , obj_member_ro
            < "VAdress Mode"
            ,  +[](bitmap_inspector& I, bool bRead, std::string& Out )
            {
                assert(bRead);
                switch (I.m_pBitmap->getVWrapMode())
                {
                case xcore::bitmap::wrap_mode::CLAMP_TO_EDGE: Out = "CLAMP_TO_EDGE"; break;
                case xcore::bitmap::wrap_mode::MIRROR:        Out = "MIRROR"; break;
                case xcore::bitmap::wrap_mode::WRAP:          Out = "WRAP"; break;
                default: Out = "Unexpected wrap"; break;
                }
            }
            , member_help<"Size in bytes of the file"
            >>
        , obj_member_ro
            < "DataSize"
            , +[](bitmap_inspector& I, bool, std::string& Out)
            {
                if (I.m_pBitmap->getDataSize() == 0) 
                {
                    Out = "No Bitmap loaded";
                    return;
                }
                Out = ConvertToCommas(I.m_pBitmap->getDataSize());
            }
            , member_help<"Size in bytes of the image in memory" 
            >>
        , obj_member_ro
            < "FileSize"
            , +[](bitmap_inspector& I, bool, std::string& Out)
            {
                if (I.m_pBitmap->getDataSize() == 0) 
                {
                    Out = "No Bitmap loaded";
                    return;
                }
                Out = ConvertToCommas(I.m_FileSize);
            }
            , member_help<"Size in bytes of the file"
            >>
        , obj_member_ro
            < "Disk Compression"
            , +[](bitmap_inspector& I, bool, float& Out)
            {
                if (I.m_pBitmap->getDataSize()) Out = (1.0f - (I.m_FileSize / (float)I.m_pBitmap->getDataSize())) * 100.0f;
                else Out = 0;
            }
            , member_ui<float>::drag_bar<0, 0, 100, "%.2f%%">
            , member_help<"How much the file is compressed in disk"
            >>
        , obj_member_ro
            < "HasAlphaChannel"
            , +[](bitmap_inspector& I, bool, bool& Out)
            {
                if (isNotValid(I))
                {
                    Out = "No Bitmap loaded";
                    return;
                }
                Out = I.m_pBitmap->hasAlphaChannel();
            }
            , member_help<"Checks if the data has an alpha channel" 
            >>
        , obj_member_ro
            < "SRGB"
            , +[](bitmap_inspector& I, bool, bool& Out)
            {
                Out = !I.m_pBitmap->isLinearSpace();
            }
            , member_help<"Check if the data is in Gamma space (if true) or Linear Space (if false)" 
            >>
        , obj_member_ro
            < "FrameCount"
            , +[](bitmap_inspector& I, bool, int& Out)
            {
                if (isNotValid(I))
                {
                    Out = 0;
                    return;
                }
                Out = static_cast<int>(I.m_pBitmap->getFrameCount());
            }
            , member_help<"Tells how many frames there is in this file" 
            >>
        , obj_member_ro
            < "FrameSize"
            , +[](bitmap_inspector& I, bool, std::string& Out)
            {
                if (I.m_pBitmap->getDataSize() == 0)
                {
                    Out = "No Bitmap loaded";
                    return;
                }
                Out = ConvertToCommas(I.m_pBitmap->getFrameSize());
            }
            , member_help<"How big a frame in the file is" 
            >>
        , obj_member_ro
            < "isCubeMap"
            , +[](bitmap_inspector& I, bool, bool& Out)
            {
                Out = I.m_pBitmap->isCubemap();
            }
            , member_help<"Check if the image is a cube map" 
            >>
        , obj_member_ro
            < "NumFaces"
            , +[](bitmap_inspector& I, bool, int& Out)
                {
                    Out = static_cast<int>(I.m_pBitmap->getFaceCount());
                }
                , member_help<"If we have loaded a cubemap how many faces the file contains" 
                >>
        , obj_member_ro
            < "FaceSize"
            , +[](bitmap_inspector& I, bool, std::string& Out)
            {
                if (I.m_pBitmap->getDataSize() == 0)
                {
                    Out = "No Bitmap loaded";
                    return;
                }
                Out = ConvertToCommas(I.m_pBitmap->getFaceSize());
            }
            , member_help<"How many bytes does a face of the cubemap / texture array is in this file" 
            >>
        , obj_member_ro
            < "Mips"
            , &bitmap_inspector::m_MipMapSizes
            , member_help<"Tells how many mip map the image has" 
            >>
        );
    };
    XPROPERTY_REG2(bitmap_inspector_props, e05::bitmap_inspector)
}