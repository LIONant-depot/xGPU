#ifndef E16_ANIM_CHARACTER_H
#define E16_ANIM_CHARACTER_H
#pragma once

#include <vector>
#include "xcore.h"

#include "../../tools/Import3D/import3d.h"
#include "../../Src/Examples/E06_AssimLoader/E06_LoadTextureHelper.h"

namespace e16
{
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

        const import3d::skeleton&       m_Skeleton;
        const import3d::anim_package&   m_AnimPackage;
        int                             m_iCurAnim      {};
        float                           m_Time          {};
    };

    struct anim_character
    {
        import3d::geom            m_SkinGeom;
        import3d::skeleton        m_Skeleton;
        import3d::anim_package    m_AnimPackage;
        anim_player               m_AnimPlayer{ m_Skeleton, m_AnimPackage };
    };
}


#endif