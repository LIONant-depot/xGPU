#include "dependencies/xbmp_tools/src/xbmp_tools.h"
#include "source/tools/xgpu_xcore_bitmap_helpers.h"

namespace e06
{
    struct load_textures
    {
        int Initialize( xgpu::device& Device, const std::wstring_view SkinFilePath, std::span<const std::wstring> TexturePaths, std::span<const import3d::material_instance> MaterialInstances ) noexcept
        {
            //
            // Load the textures
            //
            m_Textures.resize(TexturePaths.size());
            std::wstring TexturePath;
            {
                auto S = xstrtool::findI(SkinFilePath, L"Source");
                if (S == std::string::npos)
                {
                    auto I = xstrtool::rfindI(SkinFilePath, L"/");
                    assert(I != std::string::npos);
                    I++;
                    TexturePath = xstrtool::CopyN(SkinFilePath, I);
                }
                else
                {
                    TexturePath = xstrtool::CopyN(SkinFilePath, S);
                }
                TexturePath = std::format( L"{}Textures/", TexturePath );
            }

            if (auto Err = xgpu::tools::bitmap::Create(m_DefaultTexture, Device, xbitmap::getDefaultBitmap()); Err)
                return xgpu::getErrorInt(Err);

            {
                int i=0;
                for( auto& S : TexturePaths )
                {
                    auto I = xstrtool::rfindI(S.c_str(), L"/");

                    if (I == -1) I = 0;
                    else         I++;

                    auto FinalTexturePath = std::format( L"{}{}", TexturePath, S.substr(I) );
                    xbitmap Bitmap;
                    if( xstrtool::rfindI(S.c_str(), L".dds") != std::string::npos )
                    {
                        if (auto Err = xbmp::tools::loader::LoadDSS(Bitmap, FinalTexturePath); Err)
                        {
                            printf("ERROR: Failed to load the texture [%s], assigning the default texture\n", xstrtool::To(FinalTexturePath).c_str());
                            m_Textures[i++] = m_DefaultTexture;
                            continue;
                        }
                    }
                    else if (auto Err = xbmp::tools::loader::LoadSTDImage(Bitmap, FinalTexturePath); Err)
                    {
                        printf("ERROR: Failed to load the texture [%s], assigning the default texture\n", xstrtool::To(FinalTexturePath).c_str());
                        m_Textures[i++] = m_DefaultTexture;
                        continue;
                    }

                    //
                    // Try to set the right color space of the texture base on its usage...
                    //
                    Bitmap.setColorSpace( xbitmap::color_space::LINEAR );
                    for(auto& E : MaterialInstances )
                    {
                        if( E.m_DiffuseSampler.m_iTexture == i )
                        {
                            Bitmap.setColorSpace(xbitmap::color_space::SRGB);
                            break;
                        }
                    }

                    if (auto Err = xgpu::tools::bitmap::Create(m_Textures[i], Device, Bitmap); Err)
                    {
                        printf("ERROR: I was able to load this texture [%s], however vulkan had an error so I will set the default texture\n", xstrtool::To(FinalTexturePath).c_str());
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