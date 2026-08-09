#include "source/xGPU.h"

#include "dependencies/xproperty/source/xcore/my_properties.h"
#include "dependencies/xproperty/source/examples/imgui/xPropertyImGuiInspector.h"
#include "dependencies/xstrtool/source/xstrtool.h"

#include "source/tools/xgpu_xcore_bitmap_helpers.h"
#include "source/tools/xgpu_view.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <numbers>
#include <vector>

#define XRESOURCE_PIPELINE_NO_COMPILER
#include "dependencies/xresource_pipeline_v2/source/xresource_pipeline.h"
#include "source/xstrtool.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_Resources.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_AssetMgr.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_AssetBrowser.h"

#include "plugins/xtexture.plugin/source/xtexture_xgpu_rsc_loader.h"

#include "../E19_MaterialEditor/E19_mesh_manager.h"

// Skeleton: only the compiled-resource loader (.h) is needed here, NOT xskeleton_xgpu_rsc_loader.cpp -
// E23_Skeleton_Editor.cpp already includes that .cpp (once, into its own translation unit) and both
// examples link into the same xGPU_unit_test executable, so xresource::loader<xrsc::skeleton_type_guid_v>
// ::Load/Destroy are already defined there. Including the .cpp again here would be a duplicate-symbol
// link error - this file only needs the declaration (this header) to call xresource::g_Mgr.getResource
// on an xrsc::skeleton ref. No descriptor/details headers either - this viewer never resolves bone
// names, only the compiled hierarchy/rest-pose the wedge renderer needs.
#include "plugins/xskeleton.plugin/source/xskeleton.h"
#include "plugins/xskeleton.plugin/source/xskeleton_xgpu_rsc_loader.h"

// AnimPackage: nothing else in the executable defines this loader yet, so both the declaration (.h)
// and the definition (.cpp) are included here, same pattern xskeleton used the one time IT was new.
#include "plugins/xanim_package.plugin/source/xanim_package.h"
#include "plugins/xanim_package.plugin/source/xanim_package_descriptor.h"
#include "plugins/xanim_package.plugin/source/xanim_package_xgpu_rsc_loader.h"
#include "plugins/xanim_package.plugin/source/xanim_package_xgpu_rsc_loader.cpp"

#include "source/tools/xgpu_imgui_timeline.h"

//-----------------------------------------------------------------------------------
//
// E24 - AnimPackage viewer (load + preview only).
//
// Opens a compiled xanim_package asset, resolves the xskeleton it's bound to, lists its clips, and
// plays/scrubs one clip's pose on the same octahedral "wedge" bone gizmo E23_SkeletonEditor draws -
// same shaders (draw_vert/draw_frag, already compiled project-wide), same LINE_LIST outline pipeline
// shape, just fed animated per-bone world matrices instead of rest-pose ones. No authoring: no
// descriptor editing, no Save/Compile, no bone-hierarchy UI, no GPU picking - open an asset, see it
// play. Unlike E23's outline+translucent-fill pair, every bone here is drawn as a closed 12-edge
// wireframe (4 ring + 4 to each tip) so it still reads as a solid gizmo with only one draw pass -
// there's no shadow/fill pipeline pair to reuse for that here.
//
//-----------------------------------------------------------------------------------

namespace e24
{
    //---------------------------------------------------------------------------
    // Shaders - reused verbatim from the project-wide compiled set (Bin/draw_vert.glsl /
    // Bin/draw_frag.glsl are already in CMakeLists' global shader list, same ones E23's own wedge
    // outline pipeline uses) - no new .glsl files, so no CMakeLists shader-list changes needed.
    //---------------------------------------------------------------------------

    constexpr static std::uint32_t g_OutlineVertShader[] =
    {
        #include "draw_vert.h"
    };
    constexpr static std::uint32_t g_OutlineFragShader[] =
    {
        #include "draw_frag.h"
    };

    // Same ground grid as E23_SkeletonEditor/E21_StaticGeomEditor - already compiled project-wide
    // (E21_GridShader_vert/frag.glsl are in CMakeLists' global shader list), no new .glsl needed.
    constexpr static std::uint32_t g_GridVertShader[] =
    {
        #include "E21_GridShader_vert.h"
    };
    constexpr static std::uint32_t g_GridFragShader[] =
    {
        #include "E21_GridShader_frag.h"
    };

    static void Debugger(std::string_view View)
    {
        printf("%s\n", View.data());
    }

    // A single local-to-clip matrix - wedge vertices are already built in world space, so this is
    // really just View.getW2C(). Matches E23's own push_constants shape.
    struct push_constants
    {
        xmath::fmat4    m_L2C;
    };

    // E21_GridShader's own uniform block - a real UBO (not push constants), matching E21/E23's exact
    // shape and alignas(256) (see E23_Skeleton_Editor.cpp's identical comment: this shader's uniform
    // block is too large for push constants and silently corrupts past m_MajorGridDiv if you try).
    // This viewer has no shadow-casting pass, so m_L2CTShadow is always fed a zero matrix - the
    // fragment shader only samples its shadow map when inShadowPos.w > 0, which a zero matrix never
    // produces, so the grid always renders fully lit.
    struct alignas(256) grid_uniform
    {
        xmath::fmat4    m_L2W;
        xmath::fmat4    m_W2C;
        xmath::fmat4    m_L2CTShadow;
        xmath::fvec3    m_WorldSpaceCameraPos = xmath::fvec3(0.0f, 10.0f, 0.0f);
        float           m_MajorGridDiv = 10.0f;
    };

    //---------------------------------------------------------------------------
    // Per-bone world-space info, forward-kinematics only (no BIND-pose concept here - this viewer
    // isn't concerned with mesh skinning, only with playing curve data back through the hierarchy).
    //---------------------------------------------------------------------------

    struct bone_world
    {
        xmath::fvec3    m_Position  {};
        xmath::fvec3    m_Right     {};
        xmath::fvec3    m_Up        {};
    };

    //---------------------------------------------------------------------------
    // Wedge geometry - ported from E23_SkeletonEditor's wedge builder. Every bone gets a CLOSED
    // wireframe (4 ring edges at its widest cross-section, plus 4 edges from the parent tip to each
    // ring corner and 4 more from each ring corner to the child tip) rather than E23's ring-only
    // outline backed by a separate translucent fill pass - reading clearly as a solid gizmo without
    // needing a second pipeline/shader pair.
    //---------------------------------------------------------------------------

    struct wedge_shape
    {
        std::array<xmath::fvec3, 4> m_Ring;
    };

    bool ComputeWedgeShape(const xmath::fvec3& A, const xmath::fvec3& B, const xmath::fvec3& Right, const xmath::fvec3& Up, wedge_shape& Out)
    {
        xmath::fvec3 Dir = B - A;
        const float  Len = Dir.Length();
        if (Len < 1.0e-5f) return false;
        Dir = Dir / Len;

        // Re-orthogonalize against Dir (Gram-Schmidt) - the bone's own Right/Up can carry non-uniform
        // scale/shear from the source rig, which can otherwise collapse the ring into a line. See
        // E23_SkeletonEditor's identical comment for the full reasoning.
        xmath::fvec3 UpOrtho = Up - Dir * Dir.Dot(Up);
        if (UpOrtho.Length() < 1.0e-4f)
            UpOrtho = (std::abs(Dir.Dot(xmath::fvec3(0, 1, 0))) < 0.99f) ? xmath::fvec3(0, 1, 0) : xmath::fvec3(1, 0, 0);
        UpOrtho.Normalize();
        const xmath::fvec3 RightOrtho = Dir.Cross(UpOrtho).Normalize();
        UpOrtho = RightOrtho.Cross(Dir).Normalize();

        const float HeadWidth = std::max(Len * 0.10f, 1.0e-4f);
        const float HeadDist  = std::min(Len * 0.22f, HeadWidth * 2.4f);
        const xmath::fvec3 Center = A + Dir * HeadDist;

        Out.m_Ring =
        { Center + RightOrtho * HeadWidth
        , Center + UpOrtho    * HeadWidth
        , Center - RightOrtho * HeadWidth
        , Center - UpOrtho    * HeadWidth
        };
        return true;
    }

    //---------------------------------------------------------------------------
    // Root marker - a wireframe sphere for any parentless bone, same as E23's.
    //---------------------------------------------------------------------------

    struct sphere_frame
    {
        xmath::fvec3    m_Right;
        xmath::fvec3    m_Up;
        xmath::fvec3    m_Forward;
    };

    bool ComputeOrthoFrame(const xmath::fvec3& Right, const xmath::fvec3& Up, sphere_frame& Out)
    {
        xmath::fvec3 R = Right;
        xmath::fvec3 U = Up;
        if (R.Length() < 1.0e-5f || U.Length() < 1.0e-5f) return false;
        R.Normalize();
        U.Normalize();

        xmath::fvec3 F = R.Cross(U);
        if (F.Length() < 1.0e-4f) return false;
        F.Normalize();
        U = F.Cross(R).Normalize();

        Out = { R, U, F };
        return true;
    }

    constexpr int g_RootSphereWireSegs = 28; // per great circle

    // Sized off the distance to the root's nearest child so the marker reads at the same scale as the
    // wedges hanging off it - see E23_SkeletonEditor::RootSphereRadius's own comment for why a fixed
    // absolute floor doesn't work across wildly different asset scales.
    float RootSphereRadius(const xskeleton::skeleton& Skeleton, const std::vector<bone_world>& World, int iRoot, float OverallRadius)
    {
        const auto Bones = Skeleton.getBones();
        float      MinDist = -1.0f;
        for (int i = 0; i < int(Bones.size()); ++i)
        {
            if (Bones[i].m_iParent != iRoot) continue;
            const float D = (World[i].m_Position - World[iRoot].m_Position).Length();
            if (MinDist < 0.0f || D < MinDist) MinDist = D;
        }
        const float Floor = std::max(OverallRadius * 0.02f, 1.0e-4f);
        return (MinDist > 1.0e-5f) ? std::max(MinDist * 0.18f, Floor) : Floor * 1.5f;
    }

    //---------------------------------------------------------------------------
    // Depth tint - CPU-computed per vertex every frame (camera-distance based), lerped toward the
    // background clear color, same as E23. No selection/hover concept here (out of scope), so the
    // style is simpler.
    //---------------------------------------------------------------------------

    struct wedge_style
    {
        xmath::fvec3    m_CameraPos         {};
        float           m_NearDepth         = 1.0f;
        float           m_FarDepth          = 15.0f;
        std::uint32_t   m_BackgroundColor   = IM_COL32(115, 115, 115, 255); // matches the viewport's own 0.45 gray
        std::uint32_t   m_NormalColor       = IM_COL32(255, 255, 255, 255);
        std::uint32_t   m_VirtualColor      = IM_COL32(255, 180, 84, 255);
        std::uint32_t   m_RootColor         = IM_COL32(255, 220, 40, 255); // yellow - unconditional on bone type
        float           m_VirtualLineBoost  = 1.6f; // virtual bones are dashed and thin - see E23's identical field for why this needs a permanent boost
    };

    std::uint32_t DepthTint(const xmath::fvec3& P, const xmath::fvec3& CameraPos, float NearD, float FarD, std::uint32_t BaseColor, std::uint32_t BgColor)
    {
        const float D     = (P - CameraPos).Length();
        const float Range = std::max(FarD - NearD, 0.001f);
        const float T     = std::clamp((D - NearD) / Range, 0.0f, 1.0f) * 0.35f; // capped well under 1.0 - see E23's identical comment

        auto Channel = [](std::uint32_t C, int Shift) -> int { return int((C >> Shift) & 0xFFu); };

        const int Br = Channel(BaseColor, 0),  Bg_ = Channel(BaseColor, 8),  Bb = Channel(BaseColor, 16), Ba = Channel(BaseColor, 24);
        const int Gr = Channel(BgColor,   0),  Gg  = Channel(BgColor,   8),  Gb = Channel(BgColor,   16);

        const int R = int(float(Br) + float(Gr - Br) * T);
        const int G = int(float(Bg_) + float(Gg - Bg_) * T);
        const int B = int(float(Bb) + float(Gb - Bb) * T);

        return IM_COL32(R, G, B, Ba);
    }

    std::uint32_t ScaleColorRGB(std::uint32_t Color, float Factor)
    {
        auto Channel = [](std::uint32_t C, int Shift) -> int { return int((C >> Shift) & 0xFFu); };
        const int R = std::clamp(int(Channel(Color, 0)  * Factor), 0, 255);
        const int G = std::clamp(int(Channel(Color, 8)  * Factor), 0, 255);
        const int B = std::clamp(int(Channel(Color, 16) * Factor), 0, 255);
        return (Color & 0xFF000000u) | (std::uint32_t(B) << 16) | (std::uint32_t(G) << 8) | std::uint32_t(R);
    }

    std::uint32_t VertexColor(const wedge_style& Style, const xmath::fvec3& P, std::uint32_t BaseColor)
    {
        return DepthTint(P, Style.m_CameraPos, Style.m_NearDepth, Style.m_FarDepth, BaseColor, Style.m_BackgroundColor);
    }

    inline constexpr int g_MaxWedgeVertices = 65536;

    void EmitSegment(std::vector<e19::draw_vert>& Verts, const xmath::fvec3& A, const xmath::fvec3& B, std::uint32_t ColorA, std::uint32_t ColorB)
    {
        e19::draw_vert VA{}; VA.m_X = A.m_X; VA.m_Y = A.m_Y; VA.m_Z = A.m_Z; VA.m_U = 0.0f; VA.m_V = 0.0f; VA.m_Color = ColorA;
        e19::draw_vert VB{}; VB.m_X = B.m_X; VB.m_Y = B.m_Y; VB.m_Z = B.m_Z; VB.m_U = 0.0f; VB.m_V = 0.0f; VB.m_Color = ColorB;
        Verts.push_back(VA);
        Verts.push_back(VB);
    }

    // Virtual bones are dashed by only emitting alternating sub-segments along the edge - same
    // approximation E23 uses (LINE_LIST has no native dash support).
    void EmitEdge(std::vector<e19::draw_vert>& Verts, const xmath::fvec3& A, const xmath::fvec3& B, bool bDashed, const wedge_style& Style, std::uint32_t BaseColor)
    {
        auto ColorAt = [&](const xmath::fvec3& P) { return VertexColor(Style, P, BaseColor); };

        if (!bDashed)
        {
            EmitSegment(Verts, A, B, ColorAt(A), ColorAt(B));
            return;
        }

        constexpr int Splits = 6;
        for (int i = 0; i < Splits; i += 2)
        {
            const float t0 = float(i)     / float(Splits);
            const float t1 = float(i + 1) / float(Splits);
            const xmath::fvec3 P0 = A + (B - A) * t0;
            const xmath::fvec3 P1 = A + (B - A) * t1;
            EmitSegment(Verts, P0, P1, ColorAt(P0), ColorAt(P1));
        }
    }

    void BuildRootSphereWireframe(const xmath::fvec3& Center, const sphere_frame& Frame, float Radius, const wedge_style& Style, std::uint32_t Color, std::vector<e19::draw_vert>& Verts)
    {
        auto Circle = [&](const xmath::fvec3& AxisA, const xmath::fvec3& AxisB)
        {
            xmath::fvec3 Prev = Center + AxisA * Radius;
            for (int i = 1; i <= g_RootSphereWireSegs; ++i)
            {
                const float Angle = (2.0f * std::numbers::pi_v<float>) * (float(i) / float(g_RootSphereWireSegs));
                const xmath::fvec3 Cur = Center + AxisA * (Radius * std::cos(Angle)) + AxisB * (Radius * std::sin(Angle));
                EmitEdge(Verts, Prev, Cur, false, Style, Color);
                Prev = Cur;
            }
        };
        Circle(Frame.m_Right,   Frame.m_Up);
        Circle(Frame.m_Up,      Frame.m_Forward);
        Circle(Frame.m_Forward, Frame.m_Right);
    }

    void BuildWedgeGeometry(const xskeleton::skeleton& Skeleton, const std::vector<bone_world>& World, const wedge_style& Style, float OverallRadius, std::vector<e19::draw_vert>& Verts)
    {
        Verts.clear();

        const auto Bones = Skeleton.getBones();
        if (World.size() != Bones.size()) return;

        for (int i = 0; i < int(Bones.size()); ++i)
        {
            const int iParent = Bones[i].m_iParent;
            if (iParent < 0) continue; // root has no edge to draw

            const xmath::fvec3& A = World[iParent].m_Position;
            const xmath::fvec3& B = World[i].m_Position;

            wedge_shape Shape;
            if (!ComputeWedgeShape(A, B, World[i].m_Right, World[i].m_Up, Shape)) continue;

            const bool          bVirtual     = Bones[i].m_Flags.m_bVirtual;
            const std::uint32_t BaseColor    = bVirtual ? Style.m_VirtualColor : Style.m_NormalColor;
            const std::uint32_t OutlineColor = bVirtual ? ScaleColorRGB(BaseColor, Style.m_VirtualLineBoost) : BaseColor;

            for (int k = 0; k < 4; ++k)
                EmitEdge(Verts, Shape.m_Ring[k], Shape.m_Ring[(k + 1) & 3], bVirtual, Style, OutlineColor);

            for (int k = 0; k < 4; ++k)
            {
                EmitEdge(Verts, A,               Shape.m_Ring[k], bVirtual, Style, OutlineColor);
                EmitEdge(Verts, Shape.m_Ring[k], B,                bVirtual, Style, OutlineColor);
            }

            if (Verts.size() > std::size_t(g_MaxWedgeVertices - 96))
                break; // stay comfortably under the buffer's capacity
        }

        for (int i = 0; i < int(Bones.size()); ++i)
        {
            if (Bones[i].m_iParent >= 0) continue; // only parentless bones get a marker

            sphere_frame Frame;
            if (!ComputeOrthoFrame(World[i].m_Right, World[i].m_Up, Frame)) continue;

            BuildRootSphereWireframe(World[i].m_Position, Frame, RootSphereRadius(Skeleton, World, i, OverallRadius), Style, Style.m_RootColor, Verts);
        }
    }

    //---------------------------------------------------------------------------
    // Pose evaluation.
    //---------------------------------------------------------------------------

    // Forward kinematics through each bone's local rest transform - always valid, used both as the
    // "nothing selected yet" preview and to compute the framing radius/center at load time. Bones are
    // topologically sorted (parent index < child index), so one forward pass is enough.
    void ComputeRestBoneWorlds(const xskeleton::skeleton& Skeleton, std::vector<xmath::fmat4>& OutWorlds)
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
    static void ComputeAnimatedBoneWorlds
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
    // count comes from the CALLER's own wrap-tracking (see anim_state::m_LoopsElapsed) rather than
    // from an ever-growing time value - the scrub slider needs m_TimeSeconds to stay bounded to
    // [0, ClipLength) for the UI, so the loop count is tracked as a side channel instead.
    xmath::fvec3 ComputeRootMotionOffset(const xanim_package::clip& Clip, std::span<const xmath::fvec3> RootMotion, float WrappedTimeSeconds, int LoopsElapsed)
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

    void ApplyWorldOffset(std::vector<xmath::fmat4>& Worlds, const xmath::fvec3& Offset)
    {
        xmath::fmat4 T;
        T.setupSRT(xmath::fvec3(1.0f, 1.0f, 1.0f), xmath::radian3(0_xdeg, 0_xdeg, 0_xdeg), Offset);
        for (auto& M : Worlds) M = T * M;
    }

    void ToBoneWorldArray(const std::vector<xmath::fmat4>& Worlds, std::vector<bone_world>& Out)
    {
        Out.resize(Worlds.size());
        for (std::size_t i = 0; i < Worlds.size(); ++i)
        {
            Out[i].m_Position = Worlds[i].ExtractPosition();
            Out[i].m_Right    = Worlds[i].Right();
            Out[i].m_Up       = Worlds[i].Up();
        }
    }

    const char* RootMotionModeName(xanim_package::root_motion_mode Mode)
    {
        switch (Mode)
        {
        case xanim_package::root_motion_mode::NONE:    return "None";
        case xanim_package::root_motion_mode::XZ_ONLY: return "XZ Only";
        case xanim_package::root_motion_mode::XYZ:     return "XYZ";
        }
        return "?";
    }

    // Same trash-can glyph as E23_SkeletonEditor::g_DeleteIcon (Segoe MDL2-style icon font) - reused
    // verbatim, not re-guessed, so it's guaranteed to actually exist in the bundled font.
    constexpr const char* g_DeleteIcon = "\xEE\x9D\x8D";

    // Transport bar glyphs, cross-checked against the authoritative community codepoint table
    // (github.com/scottdorman/mdl2-icons) rather than guessed. GoToStart/GoToEnd reuse the standard
    // Previous/Next "skip track" pair (U+E892/U+E893) - visually symmetric and the conventional choice
    // for single-clip transport controls; there's no dedicated "go to end" glyph in the font to pair
    // with the dedicated "go to start" one, so Previous/Next keeps both ends visually consistent.
    constexpr const char* g_PlayIcon      = "\xEE\x9D\xA8";  // U+E768 Play
    constexpr const char* g_PauseIcon     = "\xEE\x9D\xA9";  // U+E769 Pause
    constexpr const char* g_GoToStartIcon = "\xEE\xA2\x92";  // U+E892 Previous
    constexpr const char* g_GoToEndIcon   = "\xEE\xA2\x93";  // U+E893 Next

    // Discrete playback-speed steps - a slider snapped to these (rather than a continuous float) makes
    // landing exactly back on 1x trivial, per the user's own request.
    constexpr float       g_PlaybackSpeeds[]    = { 0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 3.0f };
    constexpr const char* g_PlaybackSpeedLabels[] = { "0.25x", "0.5x", "0.75x", "1x", "1.25x", "1.5x", "2x", "3x" };  // avoids %g's significant-digit truncation (1.25 -> "1.2")
    constexpr int         g_NumPlaybackSpeeds   = static_cast<int>(std::size(g_PlaybackSpeeds));
    constexpr int         g_DefaultSpeedIndex   = 3;   // g_PlaybackSpeeds[3] == 1.0f

    // A descriptor clip override may or may not have a compiled counterpart to preview: it won't if
    // it's marked for Delete, or if the descriptor was edited since the last Compile. Resolved the
    // same way the compiler itself identifies a clip at runtime - CRC32 of the compiled name.
    int FindCompiledClipIndex(const xanim_package::anim_package& Package, const xanim_package_desc::clip& Override)
    {
        return Package.findClipIndex(xstrtool::CRC32(Override.m_Name));
    }

    //---------------------------------------------------------------------------

    xrsc::texture_ref CreateBackgroundTexture(xgpu::device& Device, const xbitmap& Bitmap)
    {
        xrsc::texture_ref Ref;
        Ref.m_Instance = xresource::guid_generator::Instance64();

        auto Texture = std::make_unique<xgpu::texture>();
        if (auto Err = xgpu::tools::bitmap::Create(*Texture, Device, Bitmap); Err)
        {
            assert(false);
            e24::Debugger(xgpu::getErrorMsg(Err));
            std::exit(xgpu::getErrorInt(Err));
        }

        xresource::g_Mgr.RegisterResource(Ref, Texture.release());
        return Ref;
    }

    //---------------------------------------------------------------------------
    // Mirrors E23_SkeletonEditor::GenerateDescriptorPath - the library's info.txt path -> the
    // editor Descriptor.txt path next to it. Generic across resource types, not skeleton-specific.
    //---------------------------------------------------------------------------

    struct anim_state
    {
        xrsc::anim_package                  m_Ref            = {};   // compiled anim package resource
        xrsc::skeleton                      m_SkeletonRef    = {};   // compiled skeleton resource, resolved from the descriptor
        e10::library::guid                  m_LibraryGUID    = {};
        xresource::full_guid                m_InfoGUID       = {};
        std::wstring                        m_DescriptorPath = {};
        xanim_package_desc::descriptor      m_Descriptor     = {};
        xanim_package_desc::details         m_Details        = {}; // compiler's last-seen raw import list (Details.txt) - see LoadDetails/MergeWithDetails
        std::string                         m_ErrorMessage   = {}; // non-empty -> show in the UI instead of crashing

        std::vector<xmath::fmat4>           m_RestWorldMats  = {}; // rest pose, computed once at load - also backs the framing radius/center
        xmath::fvec3                        m_Center         = xmath::fvec3(0.0f, 0.0f, 0.0f);
        float                               m_Radius         = 1.0f;

        int                                  m_iSelectedClip           = -1;   // index into the COMPILED package's clips - drives playback
        int                                  m_iSelectedImportSource   = -1;   // index into m_Descriptor.m_ImportSources
        int                                  m_iSelectedDescriptorClip = -1;   // index into m_ImportSources[m_iSelectedImportSource].m_Clips - drives the editable Clips table's row highlight

        // Inline rename - double-click to enter, matches E23_SkeletonEditor's bone-tree rename
        // exactly (see its own m_RenamingBoneName/m_RenameBuf comment): a single click on the Name
        // field needs to just select/preview the row, so the field can't be an always-editable
        // InputText - only the one (source, clip) pair being renamed shows an editable box.
        int                                  m_iRenamingImportSource = -1;
        int                                  m_iRenamingClip         = -1;
        bool                                 m_bRenameJustStarted    = false;
        std::string                          m_RenameBuf             = {};

        float                                m_TimeSeconds   = 0.0f;
        int                                  m_LoopsElapsed  = 0;    // see ComputeRootMotionOffset's own comment
        bool                                 m_bPlaying      = false;
        int                                  m_iSpeedIndex   = e24::g_DefaultSpeedIndex;  // index into e24::g_PlaybackSpeeds
        xgpu::tools::imgui::timeline::state  m_Timeline      = {};   // scrub widget's own zoom/pan - reset whenever the selected clip changes

        bool m_bNeedsReframe = true;

        // Compile/save tracking - mirrors E23_SkeletonEditor::skeleton_state exactly. Saving the
        // descriptor (SaveDescriptor) is what actually triggers a recompile - a background
        // file-watcher in the library manager picks up the change and runs the plugin's compiler,
        // broadcasting progress via e10::g_LibMgr.m_OnCompilationState (see the registration in
        // E24_Example) to whichever shared_ptr<log> this GUID's compile currently owns.
        std::shared_ptr<e10::compilation::historical_entry::log> m_Log = {};
        bool                                 m_bReload       = false;
        bool                                 m_bErrors       = false;

        bool empty() const noexcept { return m_InfoGUID.empty(); }

        void clear()
        {
            m_LibraryGUID.clear();
            m_InfoGUID.clear();
            m_DescriptorPath.clear();
            m_Descriptor    = {};
            m_Details       = {};
            m_ErrorMessage.clear();
            m_RestWorldMats.clear();
            m_Center        = xmath::fvec3(0.0f, 0.0f, 0.0f);
            m_Radius        = 1.0f;
            m_iSelectedClip           = -1;
            m_iSelectedImportSource   = -1;
            m_iSelectedDescriptorClip = -1;
            m_iRenamingImportSource   = -1;
            m_iRenamingClip           = -1;
            m_bRenameJustStarted      = false;
            m_RenameBuf.clear();
            m_TimeSeconds   = 0.0f;
            m_LoopsElapsed  = 0;
            m_bPlaying      = false;
            m_bNeedsReframe = true;
            m_Log           = std::make_shared<e10::compilation::historical_entry::log>(e10::compilation::historical_entry::communication{ .m_Result = e10::compilation::historical_entry::result::SUCCESS });
            m_bReload       = false;
            m_bErrors       = false;
        }

        // Writing the descriptor to disk is what actually kicks off a recompile (see m_Log's comment
        // above) - this is the "Compile" button's entire action.
        void SaveDescriptor()
        {
            xproperty::settings::context Context;
            if (auto Err = m_Descriptor.Serialize(false, m_DescriptorPath, Context); Err)
                assert(false);
        }
    };

    void GenerateDescriptorPath(anim_state& State, const std::wstring& InfoPath)
    {
        State.m_DescriptorPath = InfoPath;
        if (auto Pos = InfoPath.find(L"info.txt"); Pos != std::wstring::npos)
            State.m_DescriptorPath.replace(Pos, std::wstring_view(L"info.txt").length(), L"Descriptor.txt");
    }

    // Mirrors E23_SkeletonEditor::GenerateDetailsLogPath - generic across resource types, not
    // skeleton-specific: Descriptors/{Type}/{shard}/{GUID}.desc/Descriptor.txt ->
    // Cache/Resources/Logs/{Type}/{shard}/{GUID}.log/Details.txt.
    std::wstring GenerateDetailsLogPath(const std::wstring& DescriptorPath)
    {
        std::wstring Path = DescriptorPath;
        if (auto Pos = Path.find(L"Descriptors"); Pos != std::wstring::npos)
            Path.replace(Pos, std::wstring_view(L"Descriptors").length(), L"Cache\\Resources\\Logs");
        if (auto Pos = Path.find(L".desc"); Pos != std::wstring::npos)
            Path.replace(Pos, std::wstring_view(L".desc").length(), L".log");
        if (auto Pos = Path.find(L"Descriptor.txt"); Pos != std::wstring::npos)
            Path.replace(Pos, std::wstring_view(L"Descriptor.txt").length(), L"Details.txt");
        return Path;
    }

    // Details.txt is the compiler's log of what it actually saw on the last import - the source of
    // truth for raw clip names, used to reconcile the descriptor's sparse per-clip overrides (see
    // MergeWithDetails below).
    xanim_package_desc::details LoadDetails(const std::wstring& DescriptorPath)
    {
        xanim_package_desc::details Details;
        if (const auto DetailsPath = GenerateDetailsLogPath(DescriptorPath); std::filesystem::exists(DetailsPath))
        {
            xtextfile::stream TextFile;
            if (auto Err = TextFile.Open(true, DetailsPath, xtextfile::file_type::TEXT); !Err)
            {
                xproperty::settings::context Context;
                xproperty::sprop::serializer::Stream(TextFile, Details, Context);
            }
        }
        return Details;
    }

    //---------------------------------------------------------------------------

    void LoadAnimPackage(anim_state& State, e10::library::guid LibraryGUID, xresource::full_guid InfoGUID)
    {
        xresource::g_Mgr.ReleaseRef(State.m_Ref);
        xresource::g_Mgr.ReleaseRef(State.m_SkeletonRef);
        State.clear();
        State.m_Ref.clear();
        State.m_SkeletonRef.clear();

        State.m_LibraryGUID = LibraryGUID;
        State.m_InfoGUID    = InfoGUID;

        e10::g_LibMgr.getNodeInfo(State.m_LibraryGUID, State.m_InfoGUID, [&](e10::library_db::info_node& NodeInfo)
        {
            GenerateDescriptorPath(State, NodeInfo.m_Path);
        });

        if (!State.m_DescriptorPath.empty() && std::filesystem::exists(State.m_DescriptorPath))
        {
            xproperty::settings::context Context;
            // descriptor::Serialize also cross-checks the referenced skeleton's compiled bone
            // manifest (see xanim_package_desc::descriptor::Serialize) - that part can legitimately
            // fail (e.g. the skeleton hasn't been compiled yet) even though m_SkeletonRef/m_Clips/etc
            // already deserialized fine before that point, so only treat this as fatal if the
            // skeleton reference itself never came through.
            auto Err = State.m_Descriptor.Serialize(true, State.m_DescriptorPath, Context);
            if (Err && State.m_Descriptor.m_SkeletonRef.empty())
            {
                State.m_ErrorMessage = std::format("Failed to read the AnimPackage descriptor: {}", Err.getMessage());
                return;
            }

            // Reconcile the descriptor's sparse per-clip overrides against the compiler's last-seen
            // raw import list - same job xskeleton_desc::descriptor::MergeWithDetails does for E23's
            // bone tree, so the "Clips" editor shows every currently-imported clip immediately, not
            // just whichever ones someone already curated. In-memory only - Compile is what persists
            // any resulting new entries to disk.
            State.m_Details = LoadDetails(State.m_DescriptorPath);
            State.m_Descriptor.MergeWithDetails(State.m_Details);
        }

        State.m_Ref.m_Instance = InfoGUID.m_Instance;
        auto* pPackage = xresource::g_Mgr.getResource(State.m_Ref);
        if (pPackage == nullptr)
        {
            State.m_ErrorMessage = "Failed to load the compiled AnimPackage resource.";
            return;
        }

        if (State.m_Descriptor.m_SkeletonRef.empty())
        {
            State.m_ErrorMessage = "This AnimPackage's descriptor has no Skeleton reference.";
            return;
        }

        State.m_SkeletonRef.m_Instance = State.m_Descriptor.m_SkeletonRef.m_Instance;
        auto* pSkeleton = xresource::g_Mgr.getResource(State.m_SkeletonRef);
        if (pSkeleton == nullptr)
        {
            State.m_ErrorMessage = "Failed to resolve the referenced Skeleton resource.";
            return;
        }

        if (pPackage->m_nBones != pSkeleton->getBones().size())
        {
            State.m_ErrorMessage = std::format("AnimPackage/Skeleton bone-count mismatch ({} vs {}) - was the skeleton recompiled after this package?", pPackage->m_nBones, pSkeleton->getBones().size());
            return;
        }

        ComputeRestBoneWorlds(*pSkeleton, State.m_RestWorldMats);

        xmath::fvec3 Center(0.0f, 0.0f, 0.0f);
        for (auto& M : State.m_RestWorldMats) Center += M.ExtractPosition();
        if (!State.m_RestWorldMats.empty()) Center /= float(State.m_RestWorldMats.size());

        float Radius = 0.5f;
        for (auto& M : State.m_RestWorldMats) Radius = std::max(Radius, (M.ExtractPosition() - Center).Length());

        State.m_Center        = Center;
        State.m_Radius        = Radius;
        State.m_bNeedsReframe = true;
    }
}

//-----------------------------------------------------------------------------------

int E24_Example()
{
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = true, .m_pLogErrorFunc = e24::Debugger, .m_pLogWarning = e24::Debugger }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err)
        return xgpu::getErrorInt(Err);

    xgpu::window MainWindow;
    if (auto Err = Device.Create(MainWindow, {}); Err)
        return xgpu::getErrorInt(Err);

    xresource::g_Mgr.Initiallize(20000);

    //
    // Default (white, 1x1) texture - used by the wedge outline pipeline's sampler.
    //
    xrsc::texture_ref DefaultTextureRef = e24::CreateBackgroundTexture(Device, xbitmap::getDefaultBitmap());
    xgpu::texture*    pDefaultTexture   = xresource::g_Mgr.getResource(DefaultTextureRef);
    if (pDefaultTexture == nullptr)
    {
        assert(false);
        return 1;
    }

    //
    // Ground grid, for spatial context - same E21_GridShader as E23_SkeletonEditor. Triangle
    // topology (default), unlike the wedge outline's LINE_LIST, so it needs its own vertex
    // descriptor even though the attribute layout is identical.
    //
    xgpu::vertex_descriptor Primitive3DVertexDescriptor;
    {
        auto Attributes = std::array
        { xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(e19::draw_vert, m_X),     .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D }
        , xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(e19::draw_vert, m_U),     .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D }
        , xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(e19::draw_vert, m_Color), .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED }
        };
        auto Setup = xgpu::vertex_descriptor::setup{ .m_VertexSize = sizeof(e19::draw_vert), .m_Attributes = Attributes };
        if (auto Err = Device.Create(Primitive3DVertexDescriptor, Setup); Err)
            return xgpu::getErrorInt(Err);
    }

    xgpu::buffer GridDynamicUBO;
    if (auto Err = Device.Create(GridDynamicUBO, { .m_Type = xgpu::buffer::type::UNIFORM, .m_Usage = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ, .m_EntryByteSize = sizeof(e24::grid_uniform), .m_EntryCount = 10 }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::pipeline          Grid3dMaterial;
    xgpu::pipeline_instance Grid3dMaterialInstance;
    {
        xgpu::shader VertexShader;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::VERTEX
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e24::g_GridVertShader, sizeof(e24::g_GridVertShader) / sizeof(int)}}
            };
            if (auto Err = Device.Create(VertexShader, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader FragShader;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::FRAGMENT
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e24::g_GridFragShader, sizeof(e24::g_GridFragShader) / sizeof(int)}}
            };
            if (auto Err = Device.Create(FragShader, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        auto UBuffersUsage = std::array{ xgpu::pipeline::uniform_binds{ .m_BindIndex = 0, .m_Usage = { .m_bVertex = true, .m_bFragment = true }, .m_Type = xgpu::pipeline::uniform_binds::type::UBO_DYNAMIC } };
        auto Samplers = std::array{ xgpu::pipeline::sampler{.m_AddressMode = std::array{ xgpu::pipeline::sampler::address_mode::CLAMP, xgpu::pipeline::sampler::address_mode::CLAMP, xgpu::pipeline::sampler::address_mode::CLAMP}} };
        auto Shaders  = std::array<const xgpu::shader*, 2>{ &FragShader, &VertexShader };
        auto Setup    = xgpu::pipeline::setup
        { .m_VertexDescriptor   = Primitive3DVertexDescriptor
        , .m_Shaders            = Shaders
        , .m_UniformBinds       = UBuffersUsage
        , .m_Samplers           = Samplers
        // Same fix as E23_SkeletonEditor: AddCustomRenderCallback's viewport convention makes the
        // grid quad's winding read as back-facing, and a ground plane looks the same from both
        // sides anyway, so disabling culling is simpler than chasing the exact winding order.
        , .m_Primitive          = { .m_Cull = xgpu::pipeline::primitive::cull::NONE }
        , .m_Blend              = xgpu::pipeline::blend::getAlphaOriginal()
        };

        if (auto Err = Device.Create(Grid3dMaterial, Setup); Err)
            return xgpu::getErrorInt(Err);

        // No real shadow map in this preview-only viewer (see grid_uniform's comment) - the white
        // default texture is bound purely to satisfy the sampler slot; it's never actually sampled.
        auto Bindings  = std::array{ xgpu::pipeline_instance::sampler_binding{*pDefaultTexture} };
        auto InstSetup = xgpu::pipeline_instance::setup{ .m_PipeLine = Grid3dMaterial, .m_SamplersBindings = Bindings };
        if (auto Err = Device.Create(Grid3dMaterialInstance, InstSetup); Err)
            return xgpu::getErrorInt(Err);
    }

    //
    // Wedge outline pipeline - LINE_LIST octahedral bone gizmos, reusing the already-compiled
    // draw_vert/draw_frag shaders exactly like E23_SkeletonEditor's own wedge outline pipeline.
    //
    xgpu::vertex_descriptor WedgeOutlineVertexDescriptor;
    {
        auto Attributes = std::array
        { xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(e19::draw_vert, m_X),     .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D }
        , xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(e19::draw_vert, m_U),     .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D }
        , xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(e19::draw_vert, m_Color), .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED }
        };
        auto Setup = xgpu::vertex_descriptor::setup
        { .m_Topology   = xgpu::vertex_descriptor::topology::LINE_LIST
        , .m_VertexSize = sizeof(e19::draw_vert)
        , .m_Attributes = Attributes
        };
        if (auto Err = Device.Create(WedgeOutlineVertexDescriptor, Setup); Err)
            return xgpu::getErrorInt(Err);
    }

    xgpu::pipeline          WedgeOutlinePipeline;
    xgpu::pipeline_instance WedgeOutlinePipelineInstance;
    {
        xgpu::shader VertexShader;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::VERTEX
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e24::g_OutlineVertShader, sizeof(e24::g_OutlineVertShader) / sizeof(int)}}
            };
            if (auto Err = Device.Create(VertexShader, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader FragShader;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::FRAGMENT
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e24::g_OutlineFragShader, sizeof(e24::g_OutlineFragShader) / sizeof(int)}}
            };
            if (auto Err = Device.Create(FragShader, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        auto Samplers = std::array{ xgpu::pipeline::sampler{} };
        auto Shaders  = std::array<const xgpu::shader*, 2>{ &FragShader, &VertexShader };
        auto Setup    = xgpu::pipeline::setup
        { .m_VertexDescriptor   = WedgeOutlineVertexDescriptor
        , .m_Shaders            = Shaders
        , .m_PushConstantsSize  = sizeof(e24::push_constants)
        , .m_Samplers           = Samplers
        };

        if (auto Err = Device.Create(WedgeOutlinePipeline, Setup); Err)
            return xgpu::getErrorInt(Err);

        auto Bindings  = std::array{ xgpu::pipeline_instance::sampler_binding{*pDefaultTexture} };
        auto InstSetup = xgpu::pipeline_instance::setup{ .m_PipeLine = WedgeOutlinePipeline, .m_SamplersBindings = Bindings };
        if (auto Err = Device.Create(WedgeOutlinePipelineInstance, InstSetup); Err)
            return xgpu::getErrorInt(Err);
    }

    //
    // Dynamic wedge geometry buffers - the index buffer is a static 0..N-1 identity ramp; only the
    // vertex buffer changes every frame (depth tint depends on the live camera, geometry on the
    // current pose).
    //
    xgpu::buffer WedgeIndexBuffer;
    if (auto Err = Device.Create(WedgeIndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = e24::g_MaxWedgeVertices }); Err)
        return xgpu::getErrorInt(Err);

    (void)WedgeIndexBuffer.MemoryMap(0, e24::g_MaxWedgeVertices, [&](void* pData)
    {
        auto* pIndex = static_cast<std::uint32_t*>(pData);
        for (int i = 0; i < e24::g_MaxWedgeVertices; ++i) pIndex[i] = static_cast<std::uint32_t>(i);
    });

    xgpu::buffer WedgeVertexBuffer;
    if (auto Err = Device.Create(WedgeVertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_Usage = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ, .m_EntryByteSize = sizeof(e19::draw_vert), .m_EntryCount = e24::g_MaxWedgeVertices }); Err)
        return xgpu::getErrorInt(Err);

    //
    // Grid plane mesh (E19's mesh manager, used only for its PLANE3D primitive)
    //
    e19::mesh_manager MeshManager = {};
    MeshManager.Init(Device);

    //
    // Asset Mgr
    //
    resource_mgr_user_data ResourceMgrUserData;

    e10::assert_browser  AsserBrowser;
    e24::anim_state       AnimState;

    // Compile-progress subscriber - the library manager broadcasts every resource's compile state
    // (any type, any selection) through this one delegate; filter to whichever AnimPackage is
    // currently loaded and mirror its log/result locally, matching E23_SkeletonEditor's own
    // CallBackForCompilation exactly.
    auto CallBackForCompilation = [&](e10::library_mgr&, e10::library::guid, xresource::full_guid gCompilingEntry, std::shared_ptr<e10::compilation::historical_entry::log>& LogInformation)
    {
        if (AnimState.m_InfoGUID != gCompilingEntry) return;

        if (AnimState.m_Log.get() != LogInformation.get())
            AnimState.m_Log = LogInformation;

        e10::compilation::historical_entry::result Result;
        {
            xcontainer::lock::scope lk(*AnimState.m_Log);
            Result = AnimState.m_Log->get().m_Result;
        }

        if (Result == e10::compilation::historical_entry::result::SUCCESS || Result == e10::compilation::historical_entry::result::SUCCESS_WARNINGS)
        {
            AnimState.m_bReload = true;
            AnimState.m_bErrors = false;
        }
        else if (Result == e10::compilation::historical_entry::result::FAILURE)
        {
            AnimState.m_bErrors = true;
        }
    };
    e10::g_LibMgr.m_OnCompilationState.Register(CallBackForCompilation);

    //
    // Property inspector - the selected AnimPackage descriptor's own properties (import sources,
    // skeleton reference, per-clip overrides), driven by xproperty rather than hardcoded ImGui
    // widgets, matching E23_SkeletonEditor's "Skeleton Properties" pattern. Bound once: AnimState and
    // its m_Descriptor member never change address, only their contents do (LoadAnimPackage
    // overwrites m_Descriptor in place via Serialize), so the inspector stays valid across loads.
    //
    xproperty::inspector Inspector("AnimPackage Properties");
    Inspector.m_Settings.m_ColorVScalar1 = 0.270f * 1.4f;
    Inspector.m_Settings.m_ColorVScalar2 = 0.305f * 1.4f;
    Inspector.m_Settings.m_ColorSScalar  = 0.26f * 1.4f;
    Inspector.AppendEntity();
    Inspector.AppendEntityComponent(*AnimState.m_Descriptor.getProperties(), &AnimState.m_Descriptor);

    //
    // Setup Imgui interface
    //
    xgpu::tools::imgui::CreateInstance(MainWindow);
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 0.5f;

    //
    // Set the project path (same lookup every editor example uses)
    //
    {
        TCHAR szFileName[MAX_PATH];
        GetModuleFileName(NULL, szFileName, MAX_PATH);

        std::wcout << L"Full path: " << szFileName << L"\n";
        if (auto I = xstrtool::findI(std::wstring{ szFileName }, { L"xGPU" }); I != std::string::npos)
        {
            I += 4; // Skip the xGPU part
            szFileName[I] = 0;
            std::wcout << L"Found xGPU at: " << szFileName << L"\n";

            TCHAR LIONantProject[] = L"\\example.lionprj";
            for (int i = 0; szFileName[I++] = LIONantProject[i]; ++i);

            std::wcout << "Project Path: " << szFileName << "\n";

            if (auto Err = e10::g_LibMgr.OpenProject(szFileName); Err)
            {
                e24::Debugger(Err.getMessage());
                return 1;
            }

            ImGuiIO& io = ImGui::GetIO();
            static std::string IniSave = std::format("{}/Assets/imgui.ini", xstrtool::To(szFileName));
            io.IniFilename = IniSave.c_str();

            ResourceMgrUserData.m_Device = Device;
            xresource::g_Mgr.setUserData(&ResourceMgrUserData, false);
            xresource::g_Mgr.setRootPath(std::format(L"{}//Cache//Resources//Platforms//Windows", e10::g_LibMgr.m_ProjectPath));
        }
    }

    //
    // Setup the view (camera) - same orbit/pan/zoom controls as every other example.
    //
    xgpu::tools::view  View        = {};
    xmath::radian3     Angles      = {};
    float              Distance    = -1;   // let it auto-compute once an asset loads
    xmath::fvec3       CameraTarget(0, 0, 0);
    View.setFov(60_xdeg);

    xgpu::mouse    Mouse;
    xgpu::keyboard Keyboard;
    Instance.Create(Mouse, {});
    Instance.Create(Keyboard, {});

    std::vector<e19::draw_vert>    WedgeVerts;
    std::vector<xmath::fmat4>      PoseWorldMats;
    std::vector<e24::bone_world>   PoseBoneWorld;

    //
    // Main Loop
    //
    while (Instance.ProcessInputEvents())
    {
        if (xgpu::tools::imgui::BeginRendering(true)) continue;

        // Space bar toggles Play/Pause - same action as the transport bar's own button, just not
        // gated on that panel having focus (matches every video player's own convention). Guarded by
        // WantTextInput so it still types a literal space while renaming a clip instead of hijacking it.
        if (!AnimState.empty() && AnimState.m_iSelectedClip >= 0 && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Space))
            AnimState.m_bPlaying = !AnimState.m_bPlaying;

        //
        // Advance playback time before evaluating the pose this frame.
        //
        if (auto* pPackage = AnimState.empty() ? nullptr : xresource::g_Mgr.getResource(AnimState.m_Ref);
            pPackage && AnimState.m_iSelectedClip >= 0 && AnimState.m_iSelectedClip < int(pPackage->getClips().size()))
        {
            auto&       Clip       = pPackage->getClips()[AnimState.m_iSelectedClip];
            const float ClipLength = (Clip.m_FPS > 0 && Clip.m_nFrames > 0) ? float(Clip.m_nFrames) / float(Clip.m_FPS) : 0.0f;

            if (AnimState.m_bPlaying && ClipLength > 0.0f)
            {
                AnimState.m_TimeSeconds += ImGui::GetIO().DeltaTime * e24::g_PlaybackSpeeds[AnimState.m_iSpeedIndex];
                if (Clip.m_bLoop)
                {
                    while (AnimState.m_TimeSeconds >= ClipLength) { AnimState.m_TimeSeconds -= ClipLength; ++AnimState.m_LoopsElapsed; }
                }
                else if (AnimState.m_TimeSeconds >= ClipLength)
                {
                    AnimState.m_TimeSeconds = ClipLength;
                    AnimState.m_bPlaying    = false;
                }
            }
        }

        //
        // AnimPackage viewport - a plain, dockable ImGui::Begin(...) window hosting the 3D scene via
        // AddCustomRenderCallback, matching every other example's own preview panel.
        //
        if (!AnimState.empty())
        {
            auto* pSkeleton = xresource::g_Mgr.getResource(AnimState.m_SkeletonRef);
            auto* pPackage  = xresource::g_Mgr.getResource(AnimState.m_Ref);

            if (pSkeleton)
            {
                ImGui::SetNextWindowSize(ImVec2(900, 620), ImGuiCond_FirstUseEver);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::Begin("AnimPackage Viewport");
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();

                const ImVec2 WindowPos  = ImGui::GetCursorScreenPos();
                const ImVec2 WindowSize = ImGui::GetContentRegionAvail();
                const bool   bViewportHovered = ImGui::IsWindowHovered();

                //
                // Camera controls
                //
                if (bViewportHovered)
                {
                    if (Mouse.isPressed(xgpu::mouse::digital::BTN_RIGHT))
                    {
                        auto MousePos = Mouse.getValue(xgpu::mouse::analog::POS_REL);
                        Angles.m_Pitch.m_Value -= 0.01f * MousePos[1];
                        Angles.m_Yaw.m_Value   -= 0.01f * MousePos[0];
                    }

                    if (Mouse.isPressed(xgpu::mouse::digital::BTN_MIDDLE))
                    {
                        auto MousePos = Mouse.getValue(xgpu::mouse::analog::POS_REL);
                        CameraTarget += View.getWorldYVector() * (0.005f * MousePos[1]);
                        CameraTarget += View.getWorldXVector() * (0.005f * MousePos[0]);
                    }

                    if (Distance != -1)
                    {
                        Distance += Distance * -0.2f * Mouse.getValue(xgpu::mouse::analog::WHEEL_REL)[0];
                        if (Distance < 0.5f)
                        {
                            CameraTarget += View.getWorldZVector() * (0.5f * (0.5f - Distance));
                            Distance = 0.5f;
                        }
                    }
                }

                View.setViewport({ static_cast<int>(WindowPos.x), static_cast<int>(WindowPos.y)
                                 , static_cast<int>(WindowPos.x + WindowSize.x), static_cast<int>(WindowPos.y + WindowSize.y) });

                if (AnimState.m_bNeedsReframe)
                {
                    AnimState.m_bNeedsReframe = false;

                    const float VerticalFov = View.getFov().m_Value;
                    const float Aspect      = View.getAspect();
                    const float HFov        = 2.0f * std::atan(Aspect * std::tan(VerticalFov * 0.5f));
                    const float MinFov      = std::min(VerticalFov, HFov);

                    Distance     = AnimState.m_Radius / std::tan(MinFov * 0.5f);
                    CameraTarget = AnimState.m_Center;
                }

                View.LookAt(Distance, Angles, CameraTarget);

                //
                // Pose - animated (when a valid clip is selected and bone counts agree with the
                // bound skeleton) or rest pose (fallback, so the viewport is never empty).
                //
                bool bUsedAnimatedPose = false;
                if (pPackage && AnimState.m_iSelectedClip >= 0 && AnimState.m_iSelectedClip < int(pPackage->getClips().size())
                    && pPackage->m_nBones == pSkeleton->getBones().size())
                {
                    auto& Clip = pPackage->getClips()[AnimState.m_iSelectedClip];
                    e24::ComputeAnimatedBoneWorlds(*pSkeleton, *pPackage, AnimState.m_iSelectedClip, AnimState.m_TimeSeconds, PoseWorldMats);

                    if (Clip.m_RootMotionMode != xanim_package::root_motion_mode::NONE)
                    {
                        const auto RootMotion = pPackage->getClipRootMotion(AnimState.m_iSelectedClip);
                        const auto Offset     = e24::ComputeRootMotionOffset(Clip, RootMotion, AnimState.m_TimeSeconds, AnimState.m_LoopsElapsed);
                        e24::ApplyWorldOffset(PoseWorldMats, Offset);
                    }
                    bUsedAnimatedPose = true;
                }
                else
                {
                    PoseWorldMats = AnimState.m_RestWorldMats;
                }
                (void)bUsedAnimatedPose;

                e24::ToBoneWorldArray(PoseWorldMats, PoseBoneWorld);

                e24::wedge_style Style;
                Style.m_CameraPos = View.getPosition();
                Style.m_NearDepth = std::max(0.01f, Distance * 0.25f);
                Style.m_FarDepth  = Distance * 1.6f + AnimState.m_Radius;

                e24::BuildWedgeGeometry(*pSkeleton, PoseBoneWorld, Style, AnimState.m_Radius, WedgeVerts);

                if (!WedgeVerts.empty())
                {
                    (void)WedgeVertexBuffer.MemoryMap(0, static_cast<int>(WedgeVerts.size()), [&](void* pData)
                    {
                        std::memcpy(pData, WedgeVerts.data(), WedgeVerts.size() * sizeof(e19::draw_vert));
                    });
                }

                const std::size_t nWedgeVerts = WedgeVerts.size();
                const xmath::fmat4 W2C        = View.getW2C();

                xgpu::tools::imgui::AddCustomRenderCallback([&, nWedgeVerts, W2C](xgpu::cmd_buffer& CmdBuffer, const ImVec2&, const ImVec2&)
                {
                    //
                    // Ground grid, for spatial context - same as E23_SkeletonEditor's. Fixed at world
                    // (0,0,0) rather than tracking the skeleton's own center, so it always shows the
                    // user where true zero is instead of hiding any actual world-space offset.
                    //
                    {
                        CmdBuffer.setPipelineInstance(Grid3dMaterialInstance);
                        auto& Uniform = GridDynamicUBO.allocEntry<e24::grid_uniform>();
                        Uniform.m_WorldSpaceCameraPos = View.getPosition();
                        Uniform.m_L2W          = xmath::fmat4(xmath::fvec3(100.f, 100.0f, 1.f), xmath::radian3(-90_xdeg, 0_xdeg, 0_xdeg), xmath::fvec3(0, 0, 0));
                        Uniform.m_W2C          = W2C;
                        Uniform.m_L2CTShadow   = xmath::fmat4::fromZero(); // no shadow pass here - see grid_uniform's comment
                        Uniform.m_MajorGridDiv = 10.0f;
                        CmdBuffer.setDynamicUBO(GridDynamicUBO, 0);
                        MeshManager.Rendering(CmdBuffer, e19::mesh_manager::model::PLANE3D);
                    }

                    if (nWedgeVerts)
                    {
                        CmdBuffer.setPipelineInstance(WedgeOutlinePipelineInstance);
                        CmdBuffer.setBuffer(WedgeIndexBuffer);
                        CmdBuffer.setBuffer(WedgeVertexBuffer);
                        CmdBuffer.setPushConstants(e24::push_constants{ .m_L2C = W2C });
                        CmdBuffer.Draw(static_cast<int>(nWedgeVerts));
                    }
                });

                ImGui::End();
            }
        }

        //
        // Main menu bar - Save/Compile/Feedback mirror E23_SkeletonEditor's so this editor doesn't
        // feel like a different tool. Saving the descriptor is what actually kicks off a recompile
        // (see the m_OnCompilationState subscriber above); this is just the button + status readout.
        //
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open AnimPackage..."))
                    AsserBrowser.Show(true);

                ImGui::Separator();
                {
                    const bool bDisableSave = !e10::g_LibMgr.isReadyToSave() && AnimState.empty();
                    if (bDisableSave) ImGui::BeginDisabled();
                    if (ImGui::MenuItem("Save", "Ctrl+S"))
                    {
                        xproperty::settings::context Context;
                        e10::g_LibMgr.Save(Context);
                    }
                    if (bDisableSave) ImGui::EndDisabled();
                }
                ImGui::EndMenu();
            }

            ImGui::SameLine(200);

            if (!AnimState.empty())
            {
                xcontainer::lock::scope lk(*AnimState.m_Log);
                auto& Log = AnimState.m_Log->get();

                bool bDisable = Log.m_Result == e10::compilation::historical_entry::result::COMPILING
                             || Log.m_Result == e10::compilation::historical_entry::result::COMPILING_WARNINGS;

                std::vector<std::string> ValidationErrors;
                if (!bDisable)
                {
                    AnimState.m_Descriptor.Validate(ValidationErrors);
                    if (!ValidationErrors.empty()) bDisable = true;
                }

                if (bDisable) ImGui::BeginDisabled();
                if (ImGui::Button("Compile"))
                    AnimState.SaveDescriptor();
                if (bDisable) ImGui::EndDisabled();

                std::uint32_t Color = IM_COL32(255, 255, 255, 255);
                switch (Log.m_Result)
                {
                case e10::compilation::historical_entry::result::COMPILING_WARNINGS: Color = IM_COL32(255, 255, 0,   255); break;
                case e10::compilation::historical_entry::result::COMPILING:          Color = IM_COL32(0,   255, 0,   255); break;
                case e10::compilation::historical_entry::result::FAILURE:            Color = IM_COL32(255, 170, 140, 255); break;
                case e10::compilation::historical_entry::result::SUCCESS_WARNINGS:   Color = IM_COL32(255, 255, 0,   255); break;
                case e10::compilation::historical_entry::result::SUCCESS:            Color = IM_COL32(255, 255, 255, 255); break;
                }

                ImGui::PushStyleColor(ImGuiCol_Text, Color);
                if (ImGui::Button("Feedback"))
                {
                    const ImVec2 ButtonPos  = ImGui::GetItemRectMin();
                    const ImVec2 ButtonSize = ImGui::GetItemRectSize();
                    ImGui::SetNextWindowPos(ImVec2(ButtonPos.x, ButtonPos.y + ButtonSize.y));
                    ImGui::OpenPopup("Feedback");
                }
                ImGui::PopStyleColor();

                if (ImGui::BeginPopup("Feedback"))
                {
                    ImGui::BeginChild("###Feedback-Child", ImVec2(600, 300));
                    ImGui::PushTextWrapPos(600);

                    for (auto& S : ValidationErrors)
                        ImGui::TextUnformatted(S.data(), S.data() + S.size());

                    if (!Log.m_Log.empty())
                    {
                        std::vector<std::size_t> LineOffsets{ 0 };
                        for (std::size_t Pos = 0; (Pos = Log.m_Log.find('\n', Pos)) != std::string::npos; ++Pos)
                            LineOffsets.push_back(Pos + 1);

                        const int nLines = static_cast<int>(LineOffsets.size());
                        ImGuiListClipper Clipper;
                        Clipper.Begin(nLines);
                        while (Clipper.Step())
                        {
                            for (int Row = Clipper.DisplayStart; Row < Clipper.DisplayEnd; ++Row)
                            {
                                const std::size_t Start = LineOffsets[Row];
                                const std::size_t End   = (Row + 1 < nLines) ? LineOffsets[Row + 1] - 1 : Log.m_Log.size();
                                const std::string_view Line(Log.m_Log.data() + Start, End - Start);

                                if (xstrtool::findI(Line, "ERROR:") != std::string::npos)
                                {
                                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
                                    ImGui::TextUnformatted(Line.data(), Line.data() + Line.size());
                                    ImGui::PopStyleColor();
                                }
                                else
                                {
                                    ImGui::TextUnformatted(Line.data(), Line.data() + Line.size());
                                }
                            }
                        }
                    }
                    ImGui::PopTextWrapPos();
                    ImGui::EndChild();
                    ImGui::EndPopup();
                }

                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gives information about the compilation process");

                if (!Log.m_Log.empty())
                    ImGui::Text("%s", std::string(xstrtool::getLastLine(Log.m_Log)).c_str());
            }

            ImGui::EndMainMenuBar();
        }

        // A successful recompile means the runtime resource changed under us - reload everything
        // (descriptor, compiled resource, bone worlds) the same way a fresh selection would, matching
        // E23_SkeletonEditor's own m_bReload handling. Preserve m_Log across the reload (clear()
        // would otherwise wipe it back to a blank SUCCESS entry, discarding the very compile output
        // the Feedback popup is about to show).
        if (AnimState.m_bReload)
        {
            AnimState.m_bReload = false;
            const auto SavedLog = AnimState.m_Log;
            e24::LoadAnimPackage(AnimState, AnimState.m_LibraryGUID, AnimState.m_InfoGUID);
            AnimState.m_Log = SavedLog;
        }

        {
            xproperty::settings::context Context;
            // Stacked below Clips/Playback in the same x=915 column rather than off to the right -
            // a window opened further right risks landing outside the actual client area depending
            // on the app's window size, where it would render but never be visible or reachable.
            ImGui::SetNextWindowPos(ImVec2(915, 522), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(500, 420), ImGuiCond_FirstUseEver);
            Inspector.Show(Context, []{});
        }

        AsserBrowser.Render(e10::g_LibMgr, xresource::g_Mgr);

        if (auto SelAsset = AsserBrowser.getSelectedAsset(); SelAsset.empty() == false && SelAsset.m_Type == xanim_package_desc::resource_type_guid_v)
        {
            e24::LoadAnimPackage(AnimState, AsserBrowser.getSelectedLibrary(), SelAsset);
        }

        //
        // Clip list - one flat table across every import source (which file a clip came from doesn't
        // matter for browsing/editing - only for name-matching underneath, see xanim_package_desc::
        // clip's own comment on why the DATA is still scoped per-source). "Source: <file>" is folded
        // into the Name hover tooltip instead of a visible grouping/column, so a second import source
        // doesn't fragment the table into hard-to-scan sections. Driven by the DESCRIPTOR's own
        // m_ImportSources[*].m_Clips (every raw-imported clip, via MergeWithDetails at load - see
        // LoadAnimPackage), not the compiled resource, so Name/Delete/Loop/DownSample/Trim/RootMotion
        // are directly authorable here. Only one Name field is shown/edited - m_OriginalName (the raw
        // imported name) is the stable match key across re-imports, never displayed or touched
        // directly.
        //
        {
            ImGui::SetNextWindowPos(ImVec2(915, 18), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(680, 320), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Clips"))
            {
                if (!AnimState.m_ErrorMessage.empty())
                {
                    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", AnimState.m_ErrorMessage.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::Separator();
                }

                if (AnimState.empty())
                {
                    ImGui::TextDisabled("File > Open AnimPackage... to begin.");
                }
                else
                {
                    auto& Sources = AnimState.m_Descriptor.m_ImportSources;
                    auto* pPackage = xresource::g_Mgr.getResource(AnimState.m_Ref);

                    int TotalClips = 0;
                    for (auto& S : Sources) TotalClips += static_cast<int>(S.m_Clips.size());
                    ImGui::Text("%d clip(s) - click a name to preview, edit columns inline", TotalClips);
                    ImGui::Separator();

                    constexpr ImGuiTableFlags TableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
                    if (ImGui::BeginTable("###ClipsTable", 8, TableFlags))
                    {
                        ImGui::TableSetupScrollFreeze(0, 1);
                        ImGui::TableSetupColumn("Name",   ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Delete", ImGuiTableColumnFlags_WidthFixed, 32.0f);
                        ImGui::TableSetupColumn("Loop",   ImGuiTableColumnFlags_WidthFixed, 34.0f);
                        ImGui::TableSetupColumn("DS",     ImGuiTableColumnFlags_WidthFixed, 42.0f);
                        ImGui::TableSetupColumn("In",     ImGuiTableColumnFlags_WidthFixed, 42.0f);
                        ImGui::TableSetupColumn("Out",    ImGuiTableColumnFlags_WidthFixed, 42.0f);
                        ImGui::TableSetupColumn("RM",     ImGuiTableColumnFlags_WidthFixed, 42.0f);
                        ImGui::TableSetupColumn("###spacer", ImGuiTableColumnFlags_WidthFixed, 1.0f);

                        // Custom header row (rather than TableHeadersRow()) so Delete can show the
                        // same trash icon E23_SkeletonEditor's bone tree uses; every other column uses
                        // a short text abbreviation instead - tried real icon glyphs here (Loop/DS/Trim/
                        // RootMotion all rendered fine, verified via screenshot) but the abbreviations
                        // read clearer at a glance, so kept as text with the full meaning in the tooltip.
                        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
                        ImGui::TableSetColumnIndex(0); ImGui::TableHeader("Name");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Compiled clip name - hover a row's name for its original import stats and source file");
                        ImGui::TableSetColumnIndex(1); ImGui::TableHeader(e24::g_DeleteIcon);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete Clip - excluded from the compiled output entirely (still listed here, so it can be re-enabled)");
                        ImGui::TableSetColumnIndex(2); ImGui::TableHeader("Loop");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Loop - play this clip as a seamless loop; the compiler blends out any start/end pose mismatch automatically");
                        ImGui::TableSetColumnIndex(3); ImGui::TableHeader("DS");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Down Sample - 0 = keep the imported 60fps rate. The importer always samples at 60fps, so the only reason to lower this (e.g. to 30) is to trade quality for memory; never to fix anything");
                        ImGui::TableSetColumnIndex(4); ImGui::TableHeader("In");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Trim In - first frame to keep (post-resample) - -1 = from the start");
                        ImGui::TableSetColumnIndex(5); ImGui::TableHeader("Out");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Trim Out - last frame to keep (post-resample) - -1 = to the end");
                        ImGui::TableSetColumnIndex(6); ImGui::TableHeader("RM");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Root Motion - extract the root bone's own translation into a separate per-frame channel instead of animating in place - see the Playback panel for a live preview");

                        for (int iSource = 0; iSource < static_cast<int>(Sources.size()); ++iSource)
                        {
                            auto& Source = Sources[iSource];
                            auto& Clips  = Source.m_Clips;
                            ImGui::PushID(iSource);

                            for (int i = 0; i < static_cast<int>(Clips.size()); ++i)
                            {
                                auto& Clip = Clips[i];
                                ImGui::PushID(i);
                                ImGui::TableNextRow();

                                ImGui::TableSetColumnIndex(0);
                                const bool bSelected = (AnimState.m_iSelectedImportSource == iSource && AnimState.m_iSelectedDescriptorClip == i);
                                const bool bRenaming = (AnimState.m_iRenamingImportSource == iSource && AnimState.m_iRenamingClip == i);

                                if (bRenaming)
                                {
                                    static char Buf[128]; // only ever one row renames at a time - a single scratch buffer is enough
                                    if (AnimState.m_bRenameJustStarted)
                                    {
                                        std::snprintf(Buf, sizeof(Buf), "%s", AnimState.m_RenameBuf.c_str());
                                        ImGui::SetKeyboardFocusHere();
                                        AnimState.m_bRenameJustStarted = false;
                                    }
                                    ImGui::SetNextItemWidth(-FLT_MIN);
                                    const bool bEnter = ImGui::InputText("##name", Buf, sizeof(Buf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                                    if (bEnter || ImGui::IsItemDeactivatedAfterEdit())
                                    {
                                        Clip.m_Name = Buf;
                                        AnimState.m_iRenamingImportSource = -1;
                                        AnimState.m_iRenamingClip         = -1;
                                    }
                                    else if (ImGui::IsItemDeactivated())
                                    {
                                        AnimState.m_iRenamingImportSource = -1; // Escape, or anything else that didn't actually change the text - cancel, don't write
                                        AnimState.m_iRenamingClip         = -1;
                                    }
                                }
                                else
                                {
                                    // A single click just selects/previews the row - renaming needs a
                                    // deliberate double-click, otherwise every click-to-preview would
                                    // also drop you into text-edit mode and make simple row selection
                                    // painful. Deliberately NOT SpanAllColumns: that made the selection
                                    // highlight cover the whole row as its own rectangle, drawn behind
                                    // the checkboxes/inputs in the other columns rather than blending
                                    // with them - confined to just this cell (matching how
                                    // E23_SkeletonEditor's own tree-node highlight never spans into its
                                    // checkbox columns either), it reads as a normal selected cell.
                                    if (ImGui::Selectable(Clip.m_Name.c_str(), bSelected))
                                    {
                                        AnimState.m_iSelectedImportSource   = iSource;
                                        AnimState.m_iSelectedDescriptorClip = i;
                                        AnimState.m_iSelectedClip = (pPackage && !Clip.m_bDelete) ? e24::FindCompiledClipIndex(*pPackage, Clip) : -1;
                                        AnimState.m_TimeSeconds   = 0.0f;
                                        AnimState.m_LoopsElapsed  = 0;
                                        AnimState.m_Timeline      = {}; // fresh zoom/pan for this clip's own duration
                                    }
                                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                                    {
                                        AnimState.m_iRenamingImportSource = iSource;
                                        AnimState.m_iRenamingClip         = i;
                                        AnimState.m_bRenameJustStarted    = true;
                                        AnimState.m_RenameBuf             = Clip.m_Name;
                                    }
                                    if (ImGui::IsItemHovered())
                                    {
                                        const auto FileName = std::filesystem::path(Source.m_Path).filename().string();
                                        const int  iDetailsSource = AnimState.m_Details.findSource(Source.m_Path);
                                        const int  iDetailsClip   = (iDetailsSource == -1) ? -1 : AnimState.m_Details.m_Sources[iDetailsSource].findClip(Clip.m_OriginalName);
                                        if (iDetailsClip != -1)
                                        {
                                            auto& D = AnimState.m_Details.m_Sources[iDetailsSource].m_ClipList[iDetailsClip];
                                            ImGui::SetTooltip("Imported as \"%s\"\nSource: %s\n%d fps, %d frames, %.2fs\n(double-click to rename)", Clip.m_OriginalName.c_str(), FileName.empty() ? "(no path set)" : FileName.c_str(), D.m_OriginalFPS, D.m_OriginalFrameCount, D.m_DurationSeconds);
                                        }
                                        else
                                        {
                                            ImGui::SetTooltip("Imported as \"%s\"\nSource: %s (not in the last import)\n(double-click to rename)", Clip.m_OriginalName.c_str(), FileName.empty() ? "(no path set)" : FileName.c_str());
                                        }
                                    }
                                }

                                ImGui::TableSetColumnIndex(1);
                                ImGui::Checkbox("##delete", &Clip.m_bDelete);

                                ImGui::TableSetColumnIndex(2);
                                ImGui::Checkbox("##loop", &Clip.m_bLoop);

                                ImGui::TableSetColumnIndex(3);
                                ImGui::SetNextItemWidth(-FLT_MIN);
                                ImGui::InputInt("##downsamplefps", &Clip.m_DownsampleFPS, 0);

                                ImGui::TableSetColumnIndex(4);
                                ImGui::SetNextItemWidth(-FLT_MIN);
                                ImGui::InputInt("##trimstart", &Clip.m_TrimStartFrame, 0);

                                ImGui::TableSetColumnIndex(5);
                                ImGui::SetNextItemWidth(-FLT_MIN);
                                ImGui::InputInt("##trimend", &Clip.m_TrimEndFrame, 0);

                                ImGui::TableSetColumnIndex(6);
                                ImGui::SetNextItemWidth(-FLT_MIN);
                                int Mode = static_cast<int>(Clip.m_RootMotion);
                                if (ImGui::Combo("##rootmotion", &Mode, "None\0XZ Only\0XYZ\0"))
                                    Clip.m_RootMotion = static_cast<xanim_package::root_motion_mode>(Mode);

                                ImGui::PopID();
                            }

                            ImGui::PopID();
                        }

                        ImGui::EndTable();
                    }

                    if (Sources.empty())
                        ImGui::TextDisabled("No import sources yet - add one in the AnimPackage Properties panel.");
                }
            }
            ImGui::End();
        }

        //
        // Transport bar - Play/Pause, a scrubbable timeline bound to [0, ClipLength), and read-only
        // clip info. No event tracks yet (see E24_Timeline.h's own comment) - just an empty span.
        //
        {
            ImGui::SetNextWindowPos(ImVec2(915, 322), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(500, 190), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Playback"))
            {
                auto* pPackage = AnimState.empty() ? nullptr : xresource::g_Mgr.getResource(AnimState.m_Ref);

                if (pPackage && AnimState.m_iSelectedClip >= 0 && AnimState.m_iSelectedClip < int(pPackage->getClips().size()))
                {
                    auto&       Clip       = pPackage->getClips()[AnimState.m_iSelectedClip];
                    const float ClipLength = (Clip.m_FPS > 0 && Clip.m_nFrames > 0) ? float(Clip.m_nFrames) / float(Clip.m_FPS) : 0.0f;

                    // Three real transport buttons (Play/Pause, go-to-start, go-to-end) instead of one
                    // button plus a redundant "Clip N" index nobody needs - the clip's own NAME goes in
                    // the timeline's gutter instead (see ClipName below).
                    if (ImGui::Button(AnimState.m_bPlaying ? e24::g_PauseIcon : e24::g_PlayIcon))
                        AnimState.m_bPlaying = !AnimState.m_bPlaying;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(AnimState.m_bPlaying ? "Pause" : "Play");

                    ImGui::SameLine();
                    if (ImGui::Button(e24::g_GoToStartIcon))
                    {
                        AnimState.m_TimeSeconds  = 0.0f;
                        AnimState.m_LoopsElapsed = 0;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Go to start");

                    ImGui::SameLine();
                    if (ImGui::Button(e24::g_GoToEndIcon))
                        AnimState.m_TimeSeconds = ClipLength;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Go to end");

                    // Playback speed - discrete steps (not a continuous float) so landing exactly back
                    // on 1x is trivial instead of a fiddly drag.
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(140.0f);
                    ImGui::SliderInt("##speed", &AnimState.m_iSpeedIndex, 0, e24::g_NumPlaybackSpeeds - 1, e24::g_PlaybackSpeedLabels[AnimState.m_iSpeedIndex]);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Playback speed");

                    // The clip's own display name - matches how a nested import_source is identified,
                    // read straight from the descriptor entry that selecting this clip already tracks.
                    const char* pClipName = nullptr;
                    if (AnimState.m_iSelectedImportSource >= 0 && AnimState.m_iSelectedImportSource < int(AnimState.m_Descriptor.m_ImportSources.size()))
                    {
                        auto& Source = AnimState.m_Descriptor.m_ImportSources[AnimState.m_iSelectedImportSource];
                        if (AnimState.m_iSelectedDescriptorClip >= 0 && AnimState.m_iSelectedDescriptorClip < int(Source.m_Clips.size()))
                            pClipName = Source.m_Clips[AnimState.m_iSelectedDescriptorClip].m_Name.c_str();
                    }

                    // The footer line (below) is pinned to the very bottom of the window - the timeline
                    // widget stretches to fill everything above it, so the table's own background
                    // occupies the rest of the panel instead of leaving dead space even when it has
                    // nothing more to show (a single clip, no event tracks yet).
                    const float FooterHeight   = ImGui::GetTextLineHeightWithSpacing();
                    const float MinTimelineHeight = std::max(ImGui::GetContentRegionAvail().y - FooterHeight, 0.0f);

                    if (xgpu::tools::imgui::timeline::Draw(AnimState.m_Timeline, AnimState.m_TimeSeconds, ClipLength, static_cast<float>(Clip.m_FPS), {}, "playback_timeline", pClipName, MinTimelineHeight))
                        AnimState.m_LoopsElapsed = 0; // manual scrub - the "elapsed loops" count no longer means anything

                    const float ZoomPercent = xgpu::tools::imgui::timeline::GetZoomPercent(AnimState.m_Timeline, ClipLength);
                    ImGui::Text("Zoom: %.0f%%    FPS: %d    Frames: %d    Loop: %s    Root Motion: %s"
                               , ZoomPercent, Clip.m_FPS, Clip.m_nFrames, Clip.m_bLoop ? "Yes" : "No", e24::RootMotionModeName(Clip.m_RootMotionMode));
                }
                else
                {
                    ImGui::TextDisabled("Select a clip to preview.");
                }
            }
            ImGui::End();
        }

        xgpu::tools::imgui::Render();

        MainWindow.PageFlip();

        xresource::g_Mgr.OnEndFrameDelegate();
    }

    xgpu::tools::imgui::Shutdown();

    return 0;
}
