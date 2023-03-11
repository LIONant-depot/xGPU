#ifndef E16_ANIM_CHARACTER_H
#define E16_ANIM_CHARACTER_H
#pragma once

#include <vector>
#include "xcore.h"

#include "E16_Skeleton.h"
#include "E16_AnimationPackage.h"
#include "E16_SkinGeom.h"

namespace e16
{
    struct anim_player
    {
        void PlayAnim(int i)
        {
            assert(i < m_AnimPackage.m_Animations.size());
            m_iCurAnim = i;
            m_Time = 0;
        }

        void Update(float DT)
        {
            auto& Anim = m_AnimPackage.m_Animations[m_iCurAnim];

            // advance time
            m_Time += DT;
            while (m_Time >= Anim.m_TimeLength) m_Time -= Anim.m_TimeLength;
        }

        void ComputeMatrices( const xcore::matrix4& L2W, std::span<xcore::matrix4> FinalL2W )
        {
            auto&       Anim    = m_AnimPackage.m_Animations[m_iCurAnim];
            const int   iFrame  = static_cast<int>(m_Time * Anim.m_FPS);

            // compute hirarchy matrices
            for (int i = 0; i < Anim.m_BoneKeyFrames.size(); ++i )
            {
                auto& KeyFrame = Anim.m_BoneKeyFrames[i];

                FinalL2W[i].setup(KeyFrame.m_Transfoms[iFrame]);
                
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

            // add the bind matrices into the final hirarchy
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