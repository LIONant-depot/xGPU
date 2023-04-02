#include "../../dependencies/xbmp_tools/src/xbmp_tools.h"
#include "../../tools/xgpu_xcore_bitmap_helpers.h"

namespace e06
{
    struct load_textures
    {
        int Initialize( xgpu::device& Device, const std::string_view SkinFilePath, std::span<const std::string> TexturePaths, std::span<const import3d::material_instance> MaterialInstances ) noexcept
        {
            //
            // Load the textures
            //
            m_Textures.resize(TexturePaths.size());
            xcore::cstring TexturePath;
            {
                auto S = xcore::string::FindStrI(SkinFilePath.data(), "Source");
                if (S == -1)
                {
                    auto I = xcore::string::findLastInstance(SkinFilePath.data(), '/');
                    assert(I != -1);
                    I++;
                    xcore::string::CopyN(TexturePath, SkinFilePath, xcore::cstring::units{ static_cast<std::uint32_t>(I) });
                }
                else
                {
                    xcore::string::CopyN(TexturePath, SkinFilePath, xcore::cstring::units{ static_cast<std::uint32_t>(S) });
                }
                TexturePath = xcore::string::Fmt("%sTextures/", TexturePath.data() );
            }

            if (auto Err = xgpu::tools::bitmap::Create(m_DefaultTexture, Device, xcore::bitmap::getDefaultBitmap()); Err)
                return xgpu::getErrorInt(Err);

            {
                int i=0;
                for( auto& S : TexturePaths )
                {
                    int I = xcore::string::findLastInstance(S.c_str(), '/');

                    if (I == -1) I = 0;
                    else         I++;

                    auto FinalTexturePath = xcore::string::Fmt( "%s%s", TexturePath.data(), &S.c_str()[I] );
                    xcore::bitmap Bitmap;
                    if( xcore::string::FindStrI(S.c_str(), ".dds") != -1 )
                    {
                        if (auto Err = xbmp::tools::loader::LoadDSS(Bitmap, FinalTexturePath); Err)
                        {
                            printf("ERROR: Failed to load the texture [%s], assinging the default texture\n", FinalTexturePath.data());
                            m_Textures[i++] = m_DefaultTexture;
                            continue;
                        }
                    }
                    else if (auto Err = xbmp::tools::loader::LoadSTDImage(Bitmap, FinalTexturePath); Err)
                    {
                        printf("ERROR: Failed to load the texture [%s], assinging the default texture\n", FinalTexturePath.data());
                        m_Textures[i++] = m_DefaultTexture;
                        continue;
                    }

                    //
                    // Try to set the right color space of the texture base on its usage...
                    //
                    Bitmap.setColorSpace( xcore::bitmap::color_space::LINEAR );
                    for(auto& E : MaterialInstances )
                    {
                        if( E.m_DiffuseSampler.m_iTexture == i )
                        {
                            Bitmap.setColorSpace(xcore::bitmap::color_space::SRGB);
                            break;
                        }
                    }

                    if (auto Err = xgpu::tools::bitmap::Create(m_Textures[i], Device, Bitmap); Err)
                    {
                        printf("ERROR: I was able to load this textuure [%s], however vulkan had an error so I will set the default texture\n", FinalTexturePath.data());
                        m_Textures[i] = m_DefaultTexture;
                    }

                    i++;
                }
            }

            // Done
            return 0;
        }

        xgpu::texture              m_DefaultTexture;
        std::vector<xgpu::texture> m_Textures;
    };
}