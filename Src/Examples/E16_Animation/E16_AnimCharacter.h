#ifndef E16_ANIM_CHARACTER_H
#define E16_ANIM_CHARACTER_H
#pragma once

#include <vector>
#include "xcore.h"

#include "E16_Skeleton.h"
#include "E16_AnimationPackage.h"
#include "E16_SkinGeom.h"

#include "../../dependencies/xbmp_tools/src/xbmp_tools.h"
#include "../../tools/xgpu_xcore_bitmap_helpers.h"

namespace e16
{
    struct load_textures
    {
        int Initialize( xgpu::device& Device, const std::string_view SkinFilePath, std::span<const std::string> TexturePaths, std::span<const skin_geom::material_instance> MaterialInstances ) noexcept
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
                    if (auto Err = xbmp::tools::loader::LoadSTDImage(Bitmap, FinalTexturePath); Err)
                    {
                        printf ("ERROR: Failed to load the texture [%s], assinging the default texture\n", FinalTexturePath.data() );
                        m_Textures[i] = m_DefaultTexture;
                    }
                    else
                    {
                        // Try to set the right color space of the texture base on its usage...
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

    struct anim_player
    {
        //-----------------------------------------------------------------------------------------------------------

        void PlayAnim(int i) noexcept
        {
            assert(i < m_AnimPackage.m_Animations.size());
            m_iCurAnim = i;
            m_Time = 0;
        }

        //-----------------------------------------------------------------------------------------------------------

        void Update(float DT) noexcept
        {
            auto& Anim = m_AnimPackage.m_Animations[m_iCurAnim];

            // advance time
            m_Time += DT;
            while (m_Time >= Anim.m_TimeLength) m_Time -= Anim.m_TimeLength;
        }

        //-----------------------------------------------------------------------------------------------------------

        void ComputeMatrices( std::span<xcore::matrix4> FinalL2W, const xcore::matrix4& L2W ) const noexcept
        {
            auto&       Anim      = m_AnimPackage.m_Animations[m_iCurAnim];
            const float FrameTime = m_Time * Anim.m_FPS;
            const int   iFrameT0  = static_cast<int>(FrameTime);
            const int   iFrameT1  = static_cast<int>((iFrameT0+1)%Anim.m_BoneKeyFrames[0].m_Transfoms.size());

            // compute hierarchy matrices
            for (int i = 0; i < Anim.m_BoneKeyFrames.size(); ++i )
            {
                auto& KeyFrame = Anim.m_BoneKeyFrames[i];

                // Setup the L2W matrix for this bone, (two versions) one with inner-frame blending, the other version without it
                if constexpr (true)
                {
                    FinalL2W[i].setup(xcore::math::transform3::Blend(KeyFrame.m_Transfoms[iFrameT0], FrameTime - iFrameT0, KeyFrame.m_Transfoms[iFrameT1]));
                }
                else
                {
                    FinalL2W[i].setup(KeyFrame.m_Transfoms[iFrameT0]);
                }
                
                if (-1 != m_Skeleton.m_Bones[i].m_iParent)
                {
                    assert(m_Skeleton.m_Bones[i].m_iParent <= i);
                    FinalL2W[i] = FinalL2W[m_Skeleton.m_Bones[i].m_iParent] * FinalL2W[i];
                }
                else
                {
                    assert(i == 0);
                    FinalL2W[i] = L2W * FinalL2W[i];
                }
            }

            // add the bind matrices into the final hierarchy
            for (int i = 0; i < Anim.m_BoneKeyFrames.size(); ++i)
            {
                FinalL2W[i] *= m_Skeleton.m_Bones[i].m_InvBind;
            }
        }

        const skeleton&             m_Skeleton;
        const anim_package&         m_AnimPackage;
        int                         m_iCurAnim      {};
        float                       m_Time          {};
    };

    struct anim_character
    {
        skin_geom       m_SkinGeom;
        skeleton        m_Skeleton;
        anim_package    m_AnimPackage;
        anim_player     m_AnimPlayer{ m_Skeleton, m_AnimPackage };
    };
}


#endif