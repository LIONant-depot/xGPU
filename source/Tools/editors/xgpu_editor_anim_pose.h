#ifndef XGPU_EDITOR_ANIM_POSE_H
#define XGPU_EDITOR_ANIM_POSE_H
#pragma once

#include "plugins/xskeleton.plugin/source/xskeleton.h"
#include "plugins/xanim_package.plugin/source/xanim_package.h"

// Pose-evaluation + playback-advance code pulled out of E24_AnimPackage_Editor.cpp, where it lived as
// file-local statics with zero editor dependency (only xskeleton/xanim_package runtime types) - E23
// had no use for it, but E25 (skin preview) needs the exact same pose evaluation to drive its
// SkinMatrix array, so it moves here rather than being re-derived a third time.
namespace xgpu::tools::editors
{
    //---------------------------------------------------------------------------
    // Pose evaluation.
    //---------------------------------------------------------------------------

    // Forward kinematics through each bone's local rest transform - always valid, used both as the
    // "nothing selected yet" preview and to compute the framing radius/center at load time. Bones are
    // topologically sorted (parent index < child index), so one forward pass is enough.
    inline void ComputeRestBoneWorlds(const xskeleton::skeleton& Skeleton, std::vector<xmath::fmat4>& OutWorlds)
    {
        const auto Bones = Skeleton.getBones();
        const auto Rests = Skeleton.getBoneRests();
        OutWorlds.resize(Bones.size());
        for (std::size_t i = 0; i < Bones.size(); ++i)
        {
            const xmath::fmat4 LocalMat = Rests[i].m_RestPose.toMatrix();
            const int          iParent  = Bones[i].m_iParent;
            OutWorlds[i] = (iParent < 0) ? LocalMat : (OutWorlds[iParent] * LocalMat);
        }
    }

    // Same shape as E16_AnimCharacter's ComputeMatrices for the time->frame/blend arithmetic (frame
    // index + fractional blend, wraparound for a looping clip vs clamping for a non-looping one), but
    // reading anim_package's own skeleton-order curve layout directly - getClipFrame(iClip, iFrame)[i]
    // IS bone i of the bound skeleton, no per-frame name-hash lookup needed (see xanim_package.h's own
    // comment on why the compiler guarantees this).
    inline void ComputeAnimatedBoneWorlds
    ( const xskeleton::skeleton&         Skel
    , const xanim_package::anim_package& Pkg
    , int                                 iClip
    , float                               TimeSeconds
    , std::vector<xmath::fmat4>&          OutWorlds
    )
    {
        auto&     Clip    = Pkg.getClips()[iClip];
        const int nFrames = Clip.m_nFrames;
        if (nFrames <= 0) { ComputeRestBoneWorlds(Skel, OutWorlds); return; }

        const float FrameTime = TimeSeconds * Clip.m_FPS;
        int iF0 = static_cast<int>(FrameTime);
        int iF1 = iF0 + 1;
        if (Clip.m_bLoop) { iF0 = ((iF0 % nFrames) + nFrames) % nFrames; iF1 = ((iF1 % nFrames) + nFrames) % nFrames; }
        else               { iF0 = std::clamp(iF0, 0, nFrames - 1); iF1 = std::clamp(iF1, 0, nFrames - 1); }
        const float T = FrameTime - std::floor(FrameTime);

        auto FrameA = Pkg.getClipFrame(iClip, iF0);
        auto FrameB = Pkg.getClipFrame(iClip, iF1);
        auto Bones  = Skel.getBones();

        OutWorlds.resize(Bones.size());
        for (int i = 0; i < static_cast<int>(Bones.size()); ++i)
        {
            const auto Local    = xmath::transform3::fromBlend(FrameA[i], FrameB[i], T);
            const auto LocalMat = Local.toMatrix();
            OutWorlds[i] = (Bones[i].m_iParent < 0) ? LocalMat : (OutWorlds[Bones[i].m_iParent] * LocalMat);
        }
    }

    // Display-only root motion: the accumulated delta is added as a single extra world-space offset
    // to every bone, purely so a clip authored with root motion doesn't just play in place. Loop
    // count comes from the CALLER's own wrap-tracking (see AdvancePlayback's LoopsElapsed out-param)
    // rather than from an ever-growing time value - the scrub slider needs TimeSeconds to stay bounded
    // to [0, ClipLength) for the UI, so the loop count is tracked as a side channel instead.
    inline xmath::fvec3 ComputeRootMotionOffset(const xanim_package::clip& Clip, std::span<const xmath::fvec3> RootMotion, float WrappedTimeSeconds, int LoopsElapsed)
    {
        if (RootMotion.empty() || Clip.m_nFrames <= 0) return {};

        const int   nFrames   = Clip.m_nFrames;
        const float FrameTime = WrappedTimeSeconds * Clip.m_FPS;
        int iF0 = static_cast<int>(FrameTime);
        int iF1 = iF0 + 1;
        if (Clip.m_bLoop) { iF0 = ((iF0 % nFrames) + nFrames) % nFrames; iF1 = ((iF1 % nFrames) + nFrames) % nFrames; }
        else               { iF0 = std::clamp(iF0, 0, nFrames - 1); iF1 = std::clamp(iF1, 0, nFrames - 1); }
        const float T = FrameTime - std::floor(FrameTime);

        const xmath::fvec3 Blended = RootMotion[iF0] + (RootMotion[iF1] - RootMotion[iF0]) * T;
        return Clip.m_LoopDisplacement * float(LoopsElapsed) + Blended;
    }

    inline void ApplyWorldOffset(std::vector<xmath::fmat4>& Worlds, const xmath::fvec3& Offset)
    {
        xmath::fmat4 T;
        T.setupSRT(xmath::fvec3(1.0f, 1.0f, 1.0f), xmath::radian3(0_xdeg, 0_xdeg, 0_xdeg), Offset);
        for (auto& M : Worlds) M = T * M;
    }

    //---------------------------------------------------------------------------
    // Playback advance.
    //---------------------------------------------------------------------------

    // Discrete playback-speed steps - a slider snapped to these (rather than a continuous float) makes
    // landing exactly back on 1x trivial.
    inline constexpr float       g_PlaybackSpeeds[]      = { 0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 3.0f };
    inline constexpr const char* g_PlaybackSpeedLabels[] = { "0.25x", "0.5x", "0.75x", "1x", "1.25x", "1.5x", "2x", "3x" };  // avoids %g's significant-digit truncation (1.25 -> "1.2")
    inline constexpr int         g_NumPlaybackSpeeds     = static_cast<int>(std::size(g_PlaybackSpeeds));
    inline constexpr int         g_DefaultSpeedIndex     = 3;   // g_PlaybackSpeeds[3] == 1.0f

    // Advances TimeSeconds by Speed*DeltaTime and applies clip-length wrap (looping, incrementing
    // LoopsElapsed) or clamp-and-stop (non-looping, clearing bPlaying) semantics. Caller is expected
    // to have already checked bPlaying and ClipLengthSeconds>0 before calling - this only does the
    // arithmetic once both hold, so it drops straight into an existing `if (bPlaying && Length>0)`.
    inline void AdvancePlayback(float& TimeSeconds, int& LoopsElapsed, bool& bPlaying, float ClipLengthSeconds, bool bLoop, float DeltaTime, float Speed) noexcept
    {
        TimeSeconds += DeltaTime * Speed;
        if (bLoop)
        {
            while (TimeSeconds >= ClipLengthSeconds) { TimeSeconds -= ClipLengthSeconds; ++LoopsElapsed; }
        }
        else if (TimeSeconds >= ClipLengthSeconds)
        {
            TimeSeconds = ClipLengthSeconds;
            bPlaying    = false;
        }
    }

    // Minimal reusable playback state for a NEW editor (E25) that doesn't already have its own
    // bespoke animation-state struct the way E24 does. E24 itself keeps its existing fields (this
    // would be a wide, risk-for-no-reward rename there) and just calls AdvancePlayback() directly.
    struct playback_state
    {
        float   m_TimeSeconds  = 0.0f;
        int     m_LoopsElapsed = 0;
        bool    m_bPlaying     = false;
        int     m_iSpeedIndex  = g_DefaultSpeedIndex;
    };
}

#endif
