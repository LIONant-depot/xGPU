#include "source/xGPU.h"

#include "dependencies/xproperty/source/xcore/my_properties.h"
#include "dependencies/xstrtool/source/xstrtool.h"

#include "source/tools/xgpu_xcore_bitmap_helpers.h"
#include "source/tools/xgpu_view.h"
#include <algorithm>
#include <unordered_map>
#include <functional>
#include <filesystem>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>

#define XRESOURCE_PIPELINE_NO_COMPILER
#include "dependencies/xresource_pipeline_v2/source/xresource_pipeline.h"
#include "source/xstrtool.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_Resources.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_AssetMgr.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_AssetBrowser.h"

#include "plugins/xtexture.plugin/source/xtexture_xgpu_rsc_loader.h"

#include "../E19_MaterialEditor/E19_mesh_manager.h"

#include "plugins/xskeleton.plugin/source/xskeleton.h"
#include "plugins/xskeleton.plugin/source/xskeleton_descriptor.h"
#include "plugins/xskeleton.plugin/source/xskeleton_details.h"
#include "plugins/xskeleton.plugin/source/xskeleton_xgpu_rsc_loader.h"
#include "plugins/xskeleton.plugin/source/xskeleton_xgpu_rsc_loader.cpp"

#include "imgui_internal.h"

//-----------------------------------------------------------------------------------
//
// E23 - Skeleton viewer/editor.
//
// Lean, purpose-built viewer for xskeleton assets: camera + orbit controls (xgpu::tools::view,
// same as every other example), the E21 ground grid (kept as-is for spatial context, minus its
// shadow input which this viewer has no producer for), and a new LINE_LIST "wedge outline"
// pipeline that draws every bone as a Blender-style octahedral gizmo using the bone's own
// world-space Right()/Up() axes for its cross-section. Selection works two ways: an ImGui side
// panel list, and direct viewport clicking (label-rect hit-test first, then ray-vs-triangle
// against the wedge faces).
//
//-----------------------------------------------------------------------------------

namespace e23
{
    //---------------------------------------------------------------------------
    // Shaders. draw_vert.h/draw_frag.h are already compiled project-wide (Bin/draw_vert.glsl is
    // in CMakeLists' global shader list) - the wedge outline pipeline reuses them verbatim: one
    // push-constant mat4 (L2C), per-vertex color, a white sampler so texture*color==color.
    //---------------------------------------------------------------------------

    constexpr auto g_2DVertShader = std::array
    {
        #include "imgui_vert.h"
    };
    constexpr auto g_2DFragShader = std::array
    {
        #include "draw_frag.h"
    };

    constexpr static std::uint32_t g_GridVertShader[] =
    {
        #include "E21_GridShader_vert.h"
    };
    constexpr static std::uint32_t g_GridFragShader[] =
    {
        #include "E21_GridShader_frag.h"
    };

    constexpr static std::uint32_t g_OutlineVertShader[] =
    {
        #include "draw_vert.h"
    };
    constexpr static std::uint32_t g_OutlineFragShader[] =
    {
        #include "draw_frag.h"
    };

    //---------------------------------------------------------------------------

    static void Debugger(std::string_view View)
    {
        printf("%s\n", View.data());
    }

    struct push_const2D
    {
        xmath::fvec2    m_Scale;
        xmath::fvec2    m_Translation;
        xmath::fvec2    m_UVScale;
    };

    // Shared by the wedge outline pipeline: a single local-to-clip matrix (wedge vertices are
    // already built in world space, so this is really just View.getW2C()).
    struct push_constants
    {
        xmath::fmat4    m_L2C;
    };

    //---------------------------------------------------------------------------
    // Bone name resolution. The compiled runtime skeleton has no strings - only a CRC32 hash per
    // bone (xskeleton::skeleton::name::m_NameHash). To show real names we reverse the hash by
    // loading the *editor* descriptor (xskeleton_desc::descriptor) for the same asset and hashing
    // its bone tree exactly the way the compiler does. IsVBoneTag/StripVBoneTag below are copied
    // verbatim from plugins/xskeleton.plugin/source/Compiler/xskeleton_compiler.cpp (lines ~35-46)
    // so the hashes always match what actually got baked into the compiled resource.
    //---------------------------------------------------------------------------

    static bool IsVBoneTag(std::string_view Name) noexcept
    {
        constexpr std::string_view Tag = "vbone_";
        if (Name.size() < Tag.size()) return false;
        for (auto i = 0u; i < Tag.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(Name[i])) != Tag[i]) return false;
        return true;
    }

    static std::string StripVBoneTag(std::string_view Name) noexcept
    {
        return IsVBoneTag(Name) ? std::string(Name.substr(6)) : std::string(Name);
    }

    //---------------------------------------------------------------------------
    // Per-bone world-space info. Two independent poses, both computed once at load time:
    //   FROZEN - forward kinematics through each bone's local m_RestPose. Always valid, since
    //            m_RestPose is populated regardless of skinning.
    //   BIND   - Bones[i].m_InvBindPose.Inverse(). Only meaningful for a bone the compiler actually
    //            saw mesh-skin data for; falls back to that same bone's FROZEN position otherwise
    //            (see ComputeBoneWorldsAndFraming) rather than collapsing to the origin.
    // A real mesh bound in a different configuration than its authored rest pose will show the two
    // arrays disagreeing per-bone - that's the point of keeping both, not a bug to reconcile away.
    //---------------------------------------------------------------------------

    struct bone_world
    {
        xmath::fvec3    m_Position      {};
        xmath::fvec3    m_Right         {};
        xmath::fvec3    m_Up            {};
        bool            m_bRealBindData {true}; // FROZEN: always true. BIND: false where propagated (see above).
    };

    enum class pose_mode { FROZEN, BIND };

    //---------------------------------------------------------------------------

    struct skeleton_state
    {
        xrsc::skeleton                                     m_Ref           = {};
        e10::library::guid                                 m_LibraryGUID   = {};
        xresource::full_guid                               m_InfoGUID      = {};
        std::wstring                                       m_DescriptorPath= {};
        std::unordered_map<std::uint32_t, std::string>     m_BoneNames     = {};
        std::vector<bone_world>                            m_BoneWorldFrozen = {};
        std::vector<bone_world>                            m_BoneWorldBind   = {};
        std::vector<bool>                                  m_bIsTwistBone    = {}; // pose-independent, see LoadSkeleton
        xmath::fvec3                                       m_Center        = xmath::fvec3(0.0f, 0.0f, 0.0f);
        float                                              m_Radius        = 1.0f;
        int                                                 m_iSelectedBone = -1;
        bool                                                m_bNeedsReframe = true;
        pose_mode                                           m_PoseMode      = pose_mode::FROZEN; // a view preference, not asset state - left alone by clear()

        bool empty() const noexcept { return m_InfoGUID.empty(); }

        const std::vector<bone_world>& ActiveBoneWorld() const noexcept
        {
            return m_PoseMode == pose_mode::BIND ? m_BoneWorldBind : m_BoneWorldFrozen;
        }

        void clear()
        {
            m_LibraryGUID.clear();
            m_InfoGUID.clear();
            m_DescriptorPath.clear();
            m_BoneNames.clear();
            m_BoneWorldFrozen.clear();
            m_BoneWorldBind.clear();
            m_bIsTwistBone.clear();
            m_Center        = xmath::fvec3(0.0f, 0.0f, 0.0f);
            m_Radius        = 1.0f;
            m_iSelectedBone = -1;
            m_bNeedsReframe = true;
        }
    };

    //---------------------------------------------------------------------------

    xrsc::texture_ref CreateBackgroundTexture(xgpu::device& Device, const xbitmap& Bitmap)
    {
        xrsc::texture_ref Ref;
        Ref.m_Instance = xresource::guid_generator::Instance64();

        auto Texture = std::make_unique<xgpu::texture>();
        if (auto Err = xgpu::tools::bitmap::Create(*Texture, Device, Bitmap); Err)
        {
            assert(false);
            e23::Debugger(xgpu::getErrorMsg(Err));
            std::exit(xgpu::getErrorInt(Err));
        }

        xresource::g_Mgr.RegisterResource(Ref, Texture.release());
        return Ref;
    }

    //---------------------------------------------------------------------------
    // Mirrors selected_descriptor::GeneratePaths from the old geom_static editor, trimmed to just
    // the one path this read-only viewer actually needs (the editor Descriptor.txt, for name
    // resolution) - no compile/log/resource paths, since this viewer never compiles anything.
    //---------------------------------------------------------------------------

    void GenerateDescriptorPath(skeleton_state& State, const std::wstring& InfoPath)
    {
        State.m_DescriptorPath = InfoPath;
        if (auto Pos = InfoPath.find(L"info.txt"); Pos != std::wstring::npos)
            State.m_DescriptorPath.replace(Pos, std::wstring_view(L"info.txt").length(), L"Descriptor.txt");
    }

    //---------------------------------------------------------------------------

    // The Descriptor's own m_RootBone is a sparse *override* layer - for an FBX-imported skeleton
    // with no manually-authored bones, it's nearly empty (this is why the first version of this
    // function only ever resolved 1 name). The compiler separately logs the full raw-imported tree,
    // with real names, to Details.txt next to the resource's compiled log - see
    // xskeleton_desc::details and xskeleton_compiler.cpp:428. That's the actual source of truth for
    // names; the descriptor tree is only consulted too in case a bone was authored purely in the
    // descriptor with no raw-import counterpart at all.
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

    void LoadBoneNameMap(const std::wstring& DescriptorPath, std::unordered_map<std::uint32_t, std::string>& OutMap)
    {
        OutMap.clear();

        auto AddName = [&](std::string_view RawName)
        {
            const std::string   DisplayName = StripVBoneTag(RawName);
            const std::uint32_t Hash        = xstrtool::CRC32(DisplayName);
            OutMap[Hash] = DisplayName;
        };

        if (const auto DetailsPath = GenerateDetailsLogPath(DescriptorPath); std::filesystem::exists(DetailsPath))
        {
            xtextfile::stream TextFile;
            if (auto Err = TextFile.Open(true, DetailsPath, xtextfile::file_type::TEXT); !Err)
            {
                xskeleton_desc::details       Details;
                xproperty::settings::context  Context;
                if (auto Err2 = xproperty::sprop::serializer::Stream(TextFile, Details, Context); !Err2)
                {
                    for (auto& Name : Details.m_BoneList) AddName(Name);
                }
            }
        }

        xskeleton_desc::descriptor Descriptor;
        xproperty::settings::context Context;
        if (auto Err = Descriptor.Serialize(true, DescriptorPath, Context); Err)
        {
            assert(false);
            return;
        }

        std::function<void(const xskeleton_desc::bone&)> Walk = [&](const xskeleton_desc::bone& Node)
        {
            AddName(Node.m_Name);
            for (auto& Child : Node.m_Bones) Walk(Child);
        };
        Walk(Descriptor.m_RootBone);
    }

    //---------------------------------------------------------------------------

    void ComputeBoneWorldsAndFraming(const xskeleton::skeleton& Skeleton, skeleton_state& State)
    {
        const auto Bones = Skeleton.getBones();
        const auto Rests = Skeleton.getBoneRests();
        State.m_BoneWorldFrozen.resize(Bones.size());
        State.m_BoneWorldBind.resize(Bones.size());

        // FROZEN pose: forward kinematics through each bone's local m_RestPose. Bones are
        // topologically sorted (parent index < child index), so a single forward pass is enough.
        // Always valid - m_RestPose is populated regardless of skinning.
        std::vector<xmath::fmat4> WorldMats(Bones.size());

        xmath::fvec3 Center(0.0f, 0.0f, 0.0f);
        for (std::size_t i = 0; i < Bones.size(); ++i)
        {
            const xmath::fmat4 LocalMat = Rests[i].m_RestPose.toMatrix();
            const int          iParent  = Bones[i].m_iParent;
            WorldMats[i] = (iParent < 0) ? LocalMat : (WorldMats[iParent] * LocalMat);

            auto& W      = State.m_BoneWorldFrozen[i];
            W.m_Position = WorldMats[i].ExtractPosition();
            W.m_Right    = WorldMats[i].Right();
            W.m_Up       = WorldMats[i].Up();
            Center      += W.m_Position;
        }

        // BIND pose: Bones[i].m_InvBindPose.Inverse() where that's real (the compiler actually saw
        // mesh-skin data for this bone). VIRTUAL bones (and any bone with no skin data) never got a
        // real inverse bind pose, so it's left at identity - falling back to that bone's FROZEN
        // *world* position (as an earlier version of this did) is wrong: bind and frozen poses are
        // wholly unrelated placements (this is a walking-animation asset, so the frozen/rest pose is
        // a mid-stride snapshot, not the neutral bind pose), so a bone between two real-bind-data
        // ancestors would jump out to an unrelated frozen-space position and back, drawing a
        // spurious dangling wedge on both sides of it. Instead propagate forward from the nearest
        // bind-posed ancestor using this bone's own local (parent-relative) rest offset - the same
        // offset FK uses for the frozen pose - so a fallback bone still hangs naturally off its
        // real-bind-data parent rather than teleporting to a different pose's coordinate frame.
        // Reuses WorldMats as scratch for this second forward pass - every FROZEN value it held is
        // already saved into State.m_BoneWorldFrozen above and isn't needed again.
        for (std::size_t i = 0; i < Bones.size(); ++i)
        {
            auto& W = State.m_BoneWorldBind[i];
            if (!Bones[i].m_InvBindPose.isIdentity())
            {
                const xmath::fmat4 BindMat = Bones[i].m_InvBindPose.Inverse();
                WorldMats[i] = BindMat;
                W.m_Position = BindMat.ExtractPosition();
                W.m_Right    = BindMat.Right();
                W.m_Up       = BindMat.Up();
                W.m_bRealBindData = true;
            }
            else
            {
                const xmath::fmat4 LocalMat = Rests[i].m_RestPose.toMatrix();
                const int          iParent  = Bones[i].m_iParent;
                WorldMats[i] = (iParent < 0) ? LocalMat : (WorldMats[iParent] * LocalMat);
                W.m_Position = WorldMats[i].ExtractPosition();
                W.m_Right    = WorldMats[i].Right();
                W.m_Up       = WorldMats[i].Up();
                W.m_bRealBindData = false;
            }
        }

        if (!Bones.empty()) Center /= float(Bones.size());

        float Radius = 0.5f;
        for (auto& W : State.m_BoneWorldFrozen) Radius = std::max(Radius, (W.m_Position - Center).Length());
        for (auto& W : State.m_BoneWorldBind)   Radius = std::max(Radius, (W.m_Position - Center).Length());

        State.m_Center        = Center;
        State.m_Radius         = Radius;
        State.m_bNeedsReframe  = true;
    }

    //---------------------------------------------------------------------------

    void LoadSkeleton(skeleton_state& State, e10::library::guid LibraryGUID, xresource::full_guid InfoGUID)
    {
        xresource::g_Mgr.ReleaseRef(State.m_Ref);
        State.clear();
        State.m_Ref.clear();

        State.m_LibraryGUID = LibraryGUID;
        State.m_InfoGUID    = InfoGUID;

        e10::g_LibMgr.getNodeInfo(State.m_LibraryGUID, State.m_InfoGUID, [&](e10::library_db::info_node& NodeInfo)
        {
            GenerateDescriptorPath(State, NodeInfo.m_Path);
        });

        if (!State.m_DescriptorPath.empty() && std::filesystem::exists(State.m_DescriptorPath))
            LoadBoneNameMap(State.m_DescriptorPath, State.m_BoneNames);

        State.m_Ref.m_Instance = InfoGUID.m_Instance;

        if (auto* pSkeleton = xresource::g_Mgr.getResource(State.m_Ref); pSkeleton)
        {
            ComputeBoneWorldsAndFraming(*pSkeleton, State);

            // "Twist" bones (CC_Base/AccuRig, Mixamo, and most other common rigs use this exact
            // naming convention) are real, correct data - a short dead-end branch off the main limb
            // chain that exists purely to smooth skin deformation (UpperarmTwist01->UpperarmTwist02
            // hanging off Upperarm alongside the real continuation to Forearm). Rendered with the
            // same visual weight as the real chain, that stub reads as a second, competing limb -
            // this is what "multiple arms" actually was. De-emphasizing them (see BuildWedgeGeometry)
            // is a visual clarity fix, not a data fix; the underlying hierarchy is correct as-is.
            const auto Bones = pSkeleton->getBones();
            State.m_bIsTwistBone.assign(Bones.size(), false);
            for (int i = 0; i < int(Bones.size()); ++i)
            {
                const std::uint32_t Hash = pSkeleton->getBoneNames()[i].m_NameHash;
                if (auto It = State.m_BoneNames.find(Hash); It != State.m_BoneNames.end())
                    State.m_bIsTwistBone[i] = xstrtool::findI(It->second, "Twist") != std::string::npos;
            }
        }
        else
        {
            assert(false);
        }
    }

    //---------------------------------------------------------------------------

    std::string GetBoneDisplayName(const skeleton_state& State, const xskeleton::skeleton& Skeleton, int iBone)
    {
        const std::uint32_t Hash = Skeleton.getBoneNames()[iBone].m_NameHash;
        if (auto It = State.m_BoneNames.find(Hash); It != State.m_BoneNames.end())
            return It->second;
        return std::format("0x{:08X}", Hash);
    }

    //---------------------------------------------------------------------------
    // Side-panel bone list, as a tree following the actual skeleton hierarchy rather than a flat
    // sort - bones are stored parent-index-before-child, so a single pass builds each parent's
    // child list, then a normal recursive TreeNode walk from the root does the rest.
    //---------------------------------------------------------------------------

    void RenderBoneNode(skeleton_state& State, const xskeleton::skeleton& Skeleton, const std::vector<std::vector<int>>& Children, int iBone)
    {
        const bool        bSelected = (iBone == State.m_iSelectedBone);
        const bool        bHasKids  = !Children[iBone].empty();
        const std::string Name      = GetBoneDisplayName(State, Skeleton, iBone);

        ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (bSelected)  Flags |= ImGuiTreeNodeFlags_Selected;
        if (!bHasKids)  Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        const bool bOpen = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<std::intptr_t>(iBone)), Flags, "%s", Name.c_str());
        if (ImGui::IsItemClicked()) State.m_iSelectedBone = iBone;

        if (bHasKids && bOpen)
        {
            for (int iChild : Children[iBone]) RenderBoneNode(State, Skeleton, Children, iChild);
            ImGui::TreePop();
        }
    }

    void RenderBoneTree(skeleton_state& State, const xskeleton::skeleton& Skeleton)
    {
        const auto Bones = Skeleton.getBones();
        std::vector<std::vector<int>> Children(Bones.size());
        int iRoot = -1;
        for (int i = 0; i < static_cast<int>(Bones.size()); ++i)
        {
            if (Bones[i].m_iParent < 0) iRoot = i;
            else                        Children[Bones[i].m_iParent].push_back(i);
        }
        if (iRoot != -1) RenderBoneNode(State, Skeleton, Children, iRoot);
    }

    //---------------------------------------------------------------------------
    // Wedge geometry - the classic bone gizmo: wide "ring" near the joint, tapering to the child.
    // The ring's cross-section uses the BONE'S OWN world Right()/Up() axes (not the parent's, not
    // a camera-facing basis), matching a real 3D wedge rather than a billboard.
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

        const float HeadWidth = std::max(Len * 0.10f, 1.0e-4f);
        const float HeadDist  = std::min(Len * 0.22f, HeadWidth * 2.4f);
        const xmath::fvec3 Center = A + Dir * HeadDist;

        Out.m_Ring =
        { Center + Right * HeadWidth
        , Center + Up    * HeadWidth
        , Center - Right * HeadWidth
        , Center - Up    * HeadWidth
        };
        return true;
    }

    //---------------------------------------------------------------------------
    // Depth tint - CPU-computed per vertex every frame (camera-distance based), lerped toward the
    // background clear color. Selection is depth-invariant: always full brightness.
    //---------------------------------------------------------------------------

    struct wedge_style
    {
        xmath::fvec3    m_CameraPos         {};
        float           m_NearDepth         = 1.0f;
        float           m_FarDepth          = 15.0f;
        std::uint32_t   m_BackgroundColor   = IM_COL32(10, 14, 20, 255);
        std::uint32_t   m_NormalColor       = IM_COL32(79, 195, 232, 255);
        std::uint32_t   m_VirtualColor      = IM_COL32(255, 180, 84, 255);
        std::uint32_t   m_TwistColor        = IM_COL32(90, 110, 125, 255); // dim, desaturated - a real bone, just not one that should compete visually with the main limb chain
        std::uint32_t   m_SelectedColor     = IM_COL32(255, 255, 255, 255);
    };

    std::uint32_t DepthTint(const xmath::fvec3& P, const xmath::fvec3& CameraPos, float NearD, float FarD, std::uint32_t BaseColor, std::uint32_t BgColor)
    {
        const float D     = (P - CameraPos).Length();
        const float Range = std::max(FarD - NearD, 0.001f);
        const float T     = std::clamp((D - NearD) / Range, 0.0f, 1.0f) * 0.72f; // never fully vanish into bg

        auto Channel = [](std::uint32_t C, int Shift) -> int { return int((C >> Shift) & 0xFFu); };

        const int Br = Channel(BaseColor, 0),  Bg_ = Channel(BaseColor, 8),  Bb = Channel(BaseColor, 16), Ba = Channel(BaseColor, 24);
        const int Gr = Channel(BgColor,   0),  Gg  = Channel(BgColor,   8),  Gb = Channel(BgColor,   16);

        const int R = int(float(Br) + float(Gr - Br) * T);
        const int G = int(float(Bg_) + float(Gg - Bg_) * T);
        const int B = int(float(Bb) + float(Gb - Bb) * T);

        return IM_COL32(R, G, B, Ba);
    }

    std::uint32_t VertexColor(const wedge_style& Style, const xmath::fvec3& P, bool bSelected, std::uint32_t BaseColor)
    {
        if (bSelected) return Style.m_SelectedColor;
        return DepthTint(P, Style.m_CameraPos, Style.m_NearDepth, Style.m_FarDepth, BaseColor, Style.m_BackgroundColor);
    }

    //---------------------------------------------------------------------------
    // Buffer capacity: 8 edges/bone, dashed edges split into 3 emitted sub-segments (6 verts) vs
    // 1 segment (2 verts) when solid - worst case (every bone virtual) is 8*6=48 verts/bone.
    //---------------------------------------------------------------------------

    inline constexpr int g_MaxWedgeVertices = 24576;

    void EmitSegment(std::vector<e19::draw_vert>& Verts, const xmath::fvec3& A, const xmath::fvec3& B, std::uint32_t ColorA, std::uint32_t ColorB)
    {
        e19::draw_vert VA{}; VA.m_X = A.m_X; VA.m_Y = A.m_Y; VA.m_Z = A.m_Z; VA.m_U = 0.0f; VA.m_V = 0.0f; VA.m_Color = ColorA;
        e19::draw_vert VB{}; VB.m_X = B.m_X; VB.m_Y = B.m_Y; VB.m_Z = B.m_Z; VB.m_U = 0.0f; VB.m_V = 0.0f; VB.m_Color = ColorB;
        Verts.push_back(VA);
        Verts.push_back(VB);
    }

    // Virtual bones are dashed by only emitting alternating sub-segments along the edge - LINE_LIST
    // has no native dash support, so this approximates it geometrically.
    void EmitEdge(std::vector<e19::draw_vert>& Verts, const xmath::fvec3& A, const xmath::fvec3& B, bool bDashed, const wedge_style& Style, bool bSelected, std::uint32_t BaseColor)
    {
        auto ColorAt = [&](const xmath::fvec3& P) { return VertexColor(Style, P, bSelected, BaseColor); };

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

    void BuildWedgeGeometry(const xskeleton::skeleton& Skeleton, const std::vector<bone_world>& World, const std::vector<bool>& IsTwistBone, const wedge_style& Style, int iSelectedBone, std::vector<e19::draw_vert>& Verts)
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

            // A bone with no real bind data got its BIND position/axes propagated from its nearest
            // real-bind-data ancestor using a FROZEN-relative local offset - fine on its own, but
            // nothing guarantees a *chain* of propagated bones (e.g. Upperarm -> Forearm, both
            // guessed) ends up anywhere near where real bind data would have put them, even though
            // both ends "agree" with each other. Flagging only a real/propagated *mismatch* missed
            // exactly that case - a wholly-guessed edge would still pass since both sides matched -
            // so this checks each endpoint's own reliability instead: any propagated endpoint, mismatch
            // or not, is inferred. Doesn't affect FROZEN, where m_bRealBindData is always true.
            //
            // A "Twist" bone (see LoadSkeleton) is real, correct data - just a short dead-end branch
            // off the main limb purely for skin-deformation smoothing. Full-weight rendering made it
            // read as a second, competing limb; dim + dash it instead so the real chain reads clearly.
            const bool          bSelected  = (i == iSelectedBone);
            const bool          bVirtual   = Bones[i].m_Type == xskeleton::bone_type::VIRTUAL;
            const bool          bTwist     = i < int(IsTwistBone.size()) && IsTwistBone[i];
            const bool          bInferred  = !World[i].m_bRealBindData || !World[iParent].m_bRealBindData;
            const bool          bDashed    = (bVirtual || bInferred || bTwist) && !bSelected;
            const std::uint32_t BaseColor  = bSelected ? Style.m_NormalColor : bVirtual ? Style.m_VirtualColor : bTwist ? Style.m_TwistColor : Style.m_NormalColor;

            for (int k = 0; k < 4; ++k)
            {
                EmitEdge(Verts, A,               Shape.m_Ring[k], bDashed, Style, bSelected, BaseColor);
                EmitEdge(Verts, Shape.m_Ring[k],  B,               bDashed, Style, bSelected, BaseColor);
            }

            if (Verts.size() > std::size_t(g_MaxWedgeVertices - 96))
                break; // stay comfortably under the buffer's capacity
        }
    }

    //---------------------------------------------------------------------------
    // A very light fill so a wedge registers as a solid shape rather than just a hairline outline -
    // the concept mockup this design comes from always paired the two ("Pass 1 - very light fill...
    // Pass 2 - the outline is the real signal now, not a backstop"), but only the outline pass ever
    // got built here. Only confident bones (not virtual/twist/inferred - see BuildWedgeGeometry) get
    // filled, so an uncertain wedge stays outline-only and doesn't visually compete for attention.
    //---------------------------------------------------------------------------

    void EmitTri(std::vector<e19::draw_vert>& Verts, const xmath::fvec3& A, const xmath::fvec3& B, const xmath::fvec3& C, std::uint32_t Color)
    {
        e19::draw_vert V{}; V.m_U = 0.0f; V.m_V = 0.0f; V.m_Color = Color;
        V.m_X = A.m_X; V.m_Y = A.m_Y; V.m_Z = A.m_Z; Verts.push_back(V);
        V.m_X = B.m_X; V.m_Y = B.m_Y; V.m_Z = B.m_Z; Verts.push_back(V);
        V.m_X = C.m_X; V.m_Y = C.m_Y; V.m_Z = C.m_Z; Verts.push_back(V);
    }

    void BuildWedgeFillGeometry(const xskeleton::skeleton& Skeleton, const std::vector<bone_world>& World, const std::vector<bool>& IsTwistBone, const wedge_style& Style, int iSelectedBone, std::vector<e19::draw_vert>& Verts)
    {
        Verts.clear();

        const auto Bones = Skeleton.getBones();
        if (World.size() != Bones.size()) return;

        constexpr float FillAlphaScale = 0.16f; // matches the mockup's own light-fill alpha

        for (int i = 0; i < int(Bones.size()); ++i)
        {
            const int iParent = Bones[i].m_iParent;
            if (iParent < 0) continue;

            const bool bVirtual  = Bones[i].m_Type == xskeleton::bone_type::VIRTUAL;
            const bool bTwist    = i < int(IsTwistBone.size()) && IsTwistBone[i];
            const bool bInferred = !World[i].m_bRealBindData || !World[iParent].m_bRealBindData;
            if (bVirtual || bTwist || bInferred) continue;

            const xmath::fvec3& A = World[iParent].m_Position;
            const xmath::fvec3& B = World[i].m_Position;

            wedge_shape Shape;
            if (!ComputeWedgeShape(A, B, World[i].m_Right, World[i].m_Up, Shape)) continue;

            const bool     bSelected = (i == iSelectedBone);
            const auto     TintAt    = [&](const xmath::fvec3& P) -> std::uint32_t
            {
                const std::uint32_t C = VertexColor(Style, P, bSelected, Style.m_NormalColor);
                const int A8 = int(((C >> 24) & 0xFFu) * FillAlphaScale);
                return (C & 0x00FFFFFFu) | (std::uint32_t(A8) << 24);
            };

            for (int k = 0; k < 4; ++k)
            {
                const xmath::fvec3& R0 = Shape.m_Ring[k];
                const xmath::fvec3& R1 = Shape.m_Ring[(k + 1) & 3];
                EmitTri(Verts, A, R0, R1, TintAt(A));
                EmitTri(Verts, B, R1, R0, TintAt(B));
            }

            if (Verts.size() > std::size_t(g_MaxWedgeVertices - 24))
                break;
        }
    }

    //---------------------------------------------------------------------------
    // Viewport picking - ray-vs-triangle (Moller-Trumbore) against the same 8 wedge faces per
    // bone (4 near: A-ring[k]-ring[k+1], 4 far: B-ring[k+1]-ring[k]), closest hit wins.
    //---------------------------------------------------------------------------

    bool RayTriangleIntersect(const xmath::fvec3& Origin, const xmath::fvec3& Dir, const xmath::fvec3& V0, const xmath::fvec3& V1, const xmath::fvec3& V2, float& OutT)
    {
        constexpr float Epsilon = 1.0e-6f;

        const xmath::fvec3 Edge1 = V1 - V0;
        const xmath::fvec3 Edge2 = V2 - V0;
        const xmath::fvec3 H     = Dir.Cross(Edge2);
        const float        A     = Edge1.Dot(H);
        if (std::fabs(A) < Epsilon) return false;

        const float        F = 1.0f / A;
        const xmath::fvec3  S = Origin - V0;
        const float         U = F * S.Dot(H);
        if (U < 0.0f || U > 1.0f) return false;

        const xmath::fvec3 Q = S.Cross(Edge1);
        const float        V = F * Dir.Dot(Q);
        if (V < 0.0f || U + V > 1.0f) return false;

        const float T = F * Edge2.Dot(Q);
        if (T <= Epsilon) return false;

        OutT = T;
        return true;
    }

    void PickWedge(const xskeleton::skeleton& Skeleton, const std::vector<bone_world>& World, const xmath::fvec3& Origin, const xmath::fvec3& Dir, int& OutBone, float& OutT)
    {
        OutBone = -1;
        OutT    = std::numeric_limits<float>::max();

        const auto Bones = Skeleton.getBones();
        if (World.size() != Bones.size()) return;

        for (int i = 0; i < int(Bones.size()); ++i)
        {
            const int iParent = Bones[i].m_iParent;
            if (iParent < 0) continue;

            const xmath::fvec3& A = World[iParent].m_Position;
            const xmath::fvec3& B = World[i].m_Position;

            wedge_shape Shape;
            if (!ComputeWedgeShape(A, B, World[i].m_Right, World[i].m_Up, Shape)) continue;

            for (int k = 0; k < 4; ++k)
            {
                const xmath::fvec3& R0 = Shape.m_Ring[k];
                const xmath::fvec3& R1 = Shape.m_Ring[(k + 1) & 3];

                float T;
                if (RayTriangleIntersect(Origin, Dir, A, R0, R1, T) && T < OutT) { OutT = T; OutBone = i; }
                if (RayTriangleIntersect(Origin, Dir, B, R1, R0, T) && T < OutT) { OutT = T; OutBone = i; }
            }
        }
    }

    //---------------------------------------------------------------------------
    // Label overlay - ported from the approved concept mockup (bone_viz_concept.html): edge-pinned
    // labels (straight horizontal projection for left/right, straight vertical for top/bottom),
    // packed with a minimal-displacement pass per edge so an isolated label's leader line stays
    // perfectly straight and only a genuinely crowded run absorbs any shift.
    //---------------------------------------------------------------------------

    struct label_rect_hit
    {
        int             m_iBone = -1;
        xmath::fvec2    m_Min   {};
        xmath::fvec2    m_Max   {};
    };

    struct label_item
    {
        int             m_iBone             = -1;
        xmath::fvec2    m_ScreenLead        {};
        float           m_Angle             = 0.0f;
        bool            m_bLR               = true;   // true = left/right category, false = top/bottom
        bool            m_bRightOrBottom    = false;  // right (lr) or bottom (td)
        float           m_AX = 0.0f, m_AY = 0.0f;
        float           m_Nat = 0.0f, m_Pos = 0.0f, m_Extent = 0.0f;
        float           m_BoxW = 0.0f, m_BoxH = 0.0f;
        std::string     m_Text;
        float           m_FontSize  = 9.5f;
        float           m_Alpha     = 0.55f;
        ImU32           m_EdgeColor = IM_COL32(120, 150, 170, 255);
    };

    // Deviation from the nearest horizontal axis (0 or 180deg), in [0,90].
    float DeviationFromHorizontal(float AngleRad)
    {
        const float Deg = AngleRad * (180.0f / std::numbers::pi_v<float>);
        const float D   = std::fmod(std::fmod(Deg, 180.0f) + 180.0f, 180.0f);
        return D <= 90.0f ? D : 180.0f - D;
    }

    // Minimal-displacement packing: start every item at its natural position, push forward only
    // far enough to clear whoever's ahead, then pull back wherever there was more push than the
    // final neighbor actually needed. Ported verbatim (same logic) from the concept mockup's
    // packMinimal().
    void PackMinimal(std::vector<label_item*>& Items, float Gap)
    {
        if (Items.empty()) return;
        for (auto* It : Items) It->m_Pos = It->m_Nat;
        for (std::size_t i = 1; i < Items.size(); ++i)
        {
            const float MinPos = Items[i-1]->m_Pos + Items[i-1]->m_Extent * 0.5f + Gap + Items[i]->m_Extent * 0.5f;
            if (Items[i]->m_Pos < MinPos) Items[i]->m_Pos = MinPos;
        }
        for (int i = int(Items.size()) - 2; i >= 0; --i)
        {
            const float MaxPos = Items[i+1]->m_Pos - Items[i+1]->m_Extent * 0.5f - Gap - Items[i]->m_Extent * 0.5f;
            Items[i]->m_Pos = std::max(Items[i]->m_Nat, std::min(Items[i]->m_Pos, MaxPos));
        }
    }

    // Shifting the whole packed group by one common amount (only if it actually overflows) keeps
    // PackMinimal's internal spacing intact, unlike clamping each item independently.
    void ShiftGroupIntoBounds(std::vector<label_item*>& Items, float Lo, float Hi)
    {
        if (Items.empty()) return;
        float MinEdge = std::numeric_limits<float>::max();
        float MaxEdge = -std::numeric_limits<float>::max();
        for (auto* It : Items)
        {
            MinEdge = std::min(MinEdge, It->m_Pos - It->m_Extent * 0.5f);
            MaxEdge = std::max(MaxEdge, It->m_Pos + It->m_Extent * 0.5f);
        }
        float Shift = 0.0f;
        if (MaxEdge + Shift > Hi) Shift = Hi - MaxEdge;
        if (MinEdge + Shift < Lo) Shift = Lo - MinEdge;
        for (auto* It : Items) It->m_Pos += Shift;
    }

    // Shrinks the gap (never below what's needed) so a crowded run always fits, packs minimally,
    // recenters the whole block on the mean of the *natural* positions (so a single wide box
    // doesn't drag every later item toward one edge), then shifts the block into bounds if needed.
    void PackInBounds(std::vector<label_item*>& Items, float Lo, float Hi, float Gap)
    {
        if (Items.empty()) return;

        float TotalExtent = 0.0f;
        for (auto* It : Items) TotalExtent += It->m_Extent;

        const float Avail = Hi - Lo;
        float EffectiveGap = Gap;
        if (Items.size() > 1)
        {
            const float Needed = TotalExtent + Gap * float(Items.size() - 1);
            if (Needed > Avail) EffectiveGap = (Avail - TotalExtent) / float(Items.size() - 1);
        }

        PackMinimal(Items, EffectiveGap);

        float NatMean = 0.0f, PosMean = 0.0f;
        for (auto* It : Items) { NatMean += It->m_Nat; PosMean += It->m_Pos; }
        NatMean /= float(Items.size());
        PosMean /= float(Items.size());
        const float Recenter = NatMean - PosMean;
        for (auto* It : Items) It->m_Pos += Recenter;

        ShiftGroupIntoBounds(Items, Lo, Hi);
    }

    //---------------------------------------------------------------------------

    bool WorldToScreen(xgpu::tools::view& View, const xmath::fvec3& WorldPos, xmath::fvec2& OutScreen)
    {
        const xmath::fvec4 Clip = View.getW2C() * xmath::fvec4(WorldPos, 1.0f);
        if (Clip.m_W <= 1.0e-4f) return false; // behind the camera

        const xmath::fvec3 Ndc{ Clip.m_X / Clip.m_W, Clip.m_Y / Clip.m_W, Clip.m_Z / Clip.m_W };
        const xmath::fvec4 Screen = View.getC2S() * xmath::fvec4(Ndc, 1.0f);
        OutScreen = xmath::fvec2{ Screen.m_X, Screen.m_Y };
        return true;
    }

    //---------------------------------------------------------------------------
    // Recomputed every frame (camera moves -> projections change). Fine at interactive framerates
    // for a few hundred bones - capped at 200 labels so a pathologically large skeleton doesn't
    // tank the frame; 100+ labels on screen at once is otherwise perfectly acceptable.
    //---------------------------------------------------------------------------

    void RenderBoneLabelsAndCollectHits
    ( xgpu::tools::view&              View
    , const xskeleton::skeleton&      Skeleton
    , const skeleton_state&           State
    , const xmath::irect&             Viewport
    , std::vector<label_rect_hit>&    OutHits
    )
    {
        OutHits.clear();

        const auto Bones  = Skeleton.getBones();
        const int  nBones = int(Bones.size());
        const auto& BoneWorld = State.ActiveBoneWorld();
        if (nBones == 0 || int(BoneWorld.size()) != nBones) return;

        const float VpX = float(Viewport.m_Min.m_X);
        const float VpY = float(Viewport.m_Min.m_Y);
        const float VpW = float(Viewport.getWidth());
        const float VpH = float(Viewport.getHeight());
        const float Cx  = VpX + VpW * 0.5f;
        const float Cy  = VpY + VpH * 0.5f;
        constexpr float MarginX   = 24.0f;
        constexpr float MarginY   = 16.0f;
        constexpr int   MaxLabels = 200;

        std::vector<label_item> Labels;
        Labels.reserve(std::min(nBones, MaxLabels));

        for (int i = 0; i < nBones && int(Labels.size()) < MaxLabels; ++i)
        {
            const int iParent = Bones[i].m_iParent;
            const xmath::fvec3 LeadWorld = (iParent < 0)
                ? BoneWorld[i].m_Position
                : (BoneWorld[iParent].m_Position + BoneWorld[i].m_Position) * 0.5f;

            xmath::fvec2 Screen;
            if (!WorldToScreen(View, LeadWorld, Screen)) continue;

            label_item L;
            L.m_iBone      = i;
            L.m_ScreenLead = Screen;
            L.m_Angle      = std::atan2(Screen.m_Y - Cy, Screen.m_X - Cx);
            L.m_bLR        = DeviationFromHorizontal(L.m_Angle) < 45.0f;

            const bool bSelected = (i == State.m_iSelectedBone);
            const bool bVirtual  = Bones[i].m_Type == xskeleton::bone_type::VIRTUAL;

            L.m_Text = GetBoneDisplayName(State, Skeleton, i);
            if (bSelected)      { L.m_FontSize = 14.0f; L.m_Alpha = 1.00f; L.m_EdgeColor = IM_COL32(255, 255, 255, 255); }
            else if (bVirtual)  { L.m_FontSize = 11.0f; L.m_Alpha = 0.85f; L.m_EdgeColor = IM_COL32(255, 180, 84, 255); }
            else                { L.m_FontSize = 9.5f;  L.m_Alpha = 0.55f; L.m_EdgeColor = IM_COL32(120, 150, 170, 255); }

            Labels.push_back(std::move(L));
        }

        const float BaseFontPx = ImGui::GetFontSize();
        for (auto& L : Labels)
        {
            const float dx = std::cos(L.m_Angle), dy = std::sin(L.m_Angle);
            if (L.m_bLR)
            {
                L.m_bRightOrBottom = dx >= 0.0f;
                L.m_AX = L.m_bRightOrBottom ? (VpX + VpW - MarginX) : (VpX + MarginX);
                L.m_AY = L.m_ScreenLead.m_Y;
            }
            else
            {
                L.m_bRightOrBottom = dy >= 0.0f;
                L.m_AY = L.m_bRightOrBottom ? (VpY + VpH - MarginY) : (VpY + MarginY);
                L.m_AX = L.m_ScreenLead.m_X;
            }

            const float  Scale   = L.m_FontSize / BaseFontPx;
            const ImVec2 RawSize = ImGui::CalcTextSize(L.m_Text.c_str());
            L.m_BoxW = RawSize.x * Scale + 12.0f;
            L.m_BoxH = RawSize.y * Scale + 6.0f;
        }

        auto PackGroup = [&](bool bLR, bool bRightOrBottom, bool bHorizontalExtent, float Lo, float Hi, float Gap)
        {
            std::vector<label_item*> Items;
            for (auto& L : Labels) if (L.m_bLR == bLR && L.m_bRightOrBottom == bRightOrBottom) Items.push_back(&L);

            std::sort(Items.begin(), Items.end(), [&](label_item* A, label_item* B)
            {
                return bHorizontalExtent ? (A->m_AX < B->m_AX) : (A->m_AY < B->m_AY);
            });
            for (auto* It : Items)
            {
                It->m_Nat    = bHorizontalExtent ? It->m_AX : It->m_AY;
                It->m_Extent = bHorizontalExtent ? It->m_BoxW : It->m_BoxH;
            }

            PackInBounds(Items, Lo, Hi, Gap);

            for (auto* It : Items)
            {
                if (bHorizontalExtent) It->m_AX = It->m_Pos;
                else                   It->m_AY = It->m_Pos;
            }
        };

        PackGroup(true,  false, false, VpY + MarginY, VpY + VpH - MarginY, 6.0f);   // lr, left
        PackGroup(true,  true,  false, VpY + MarginY, VpY + VpH - MarginY, 6.0f);   // lr, right
        PackGroup(false, false, true,  VpX + MarginX, VpX + VpW - MarginX, 10.0f);  // td, top
        PackGroup(false, true,  true,  VpX + MarginX, VpX + VpW - MarginX, 10.0f);  // td, bottom

        ImDrawList* pDrawList  = ImGui::GetForegroundDrawList();
        const ImU32 PanelColor = IM_COL32(17, 25, 39, 235);

        for (auto& L : Labels)
        {
            float BoxMinX, BoxMaxX;
            if (L.m_bLR)
            {
                if (L.m_bRightOrBottom) { BoxMinX = L.m_AX - L.m_BoxW; BoxMaxX = L.m_AX; }
                else                    { BoxMinX = L.m_AX;            BoxMaxX = L.m_AX + L.m_BoxW; }
            }
            else
            {
                BoxMinX = L.m_AX - L.m_BoxW * 0.5f;
                BoxMaxX = L.m_AX + L.m_BoxW * 0.5f;
            }
            const float BoxMinY = L.m_AY - L.m_BoxH * 0.5f;
            const float BoxMaxY = L.m_AY + L.m_BoxH * 0.5f;

            // Leader line touches whichever box edge is nearest the bone - never the far edge.
            float LineX, LineY;
            if (L.m_bLR) { LineX = L.m_bRightOrBottom ? BoxMinX : BoxMaxX; LineY = L.m_AY; }
            else         { LineX = L.m_AX; LineY = L.m_bRightOrBottom ? BoxMinY : BoxMaxY; }

            const ImU32 EdgeColorA  = (L.m_EdgeColor & 0x00FFFFFFu) | (ImU32(L.m_Alpha * 255.0f) << 24);
            const ImU32 PanelColorA = (PanelColor    & 0x00FFFFFFu) | (ImU32(L.m_Alpha * 235.0f) << 24);

            pDrawList->AddLine(ImVec2(L.m_ScreenLead.m_X, L.m_ScreenLead.m_Y), ImVec2(LineX, LineY), EdgeColorA, 1.0f);
            pDrawList->AddRectFilled(ImVec2(BoxMinX, BoxMinY), ImVec2(BoxMaxX, BoxMaxY), PanelColorA, 2.0f);
            pDrawList->AddRect(ImVec2(BoxMinX, BoxMinY), ImVec2(BoxMaxX, BoxMaxY), EdgeColorA, 2.0f);

            const float  Scale    = L.m_FontSize / BaseFontPx;
            const ImVec2 TextSize = ImVec2(ImGui::CalcTextSize(L.m_Text.c_str()).x * Scale, ImGui::CalcTextSize(L.m_Text.c_str()).y * Scale);

            float TextX;
            if (L.m_bLR) TextX = L.m_bRightOrBottom ? (BoxMaxX - 6.0f - TextSize.x) : (BoxMinX + 6.0f);
            else         TextX = L.m_AX - TextSize.x * 0.5f;
            const float TextY = L.m_AY - TextSize.y * 0.5f;

            const ImU32 TextColorA = IM_COL32(225, 232, 240, int(L.m_Alpha * 255.0f));
            pDrawList->AddText(ImGui::GetFont(), L.m_FontSize, ImVec2(TextX, TextY), TextColorA, L.m_Text.c_str());

            OutHits.push_back({ L.m_iBone, xmath::fvec2{BoxMinX, BoxMinY}, xmath::fvec2{BoxMaxX, BoxMaxY} });
        }
    }
}

//-----------------------------------------------------------------------------------

int E23_Example()
{
    // Create Vulkan instance
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = true, .m_pLogErrorFunc = e23::Debugger }); Err)
        return xgpu::getErrorInt(Err);

    // Create device
    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err)
        return xgpu::getErrorInt(Err);

    // Create window
    xgpu::window MainWindow;
    if (auto Err = Device.Create(MainWindow, {}); Err)
        return xgpu::getErrorInt(Err);

    // Must happen before anything registers a resource (e.g. CreateBackgroundTexture below) -
    // the pool's free-list isn't set up until this runs, and the default of 1000 is too small
    // for this project's accumulated test assets anyway.
    xresource::g_Mgr.Initiallize(20000);

    //
    // Default (white, 1x1) texture. Used by the 2D background AND as the sampler for the grid's
    // (unused, no producer in this viewer) shadow input and for the wedge outline pipeline.
    //
    xrsc::texture_ref DefaultTextureRef = e23::CreateBackgroundTexture(Device, xbitmap::getDefaultBitmap());
    xgpu::texture*    pDefaultTexture   = xresource::g_Mgr.getResource(DefaultTextureRef);
    if (pDefaultTexture == nullptr)
    {
        assert(false);
        return 1;
    }

    //
    // Vertex descriptors - both built from e19::draw_vert (E19_mesh_manager.h), differing only in
    // topology: the grid needs triangles, the wedge outline needs a real LINE_LIST.
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

    //
    // Grid pipeline - E21's ground grid, kept as-is for spatial context. The shader still expects
    // a "shadow map" sampler and projection; since this viewer has no shadow pass, the sampler is
    // bound to the default white texture and the projection is rigged (see g_DisabledShadowL2C
    // below) so the shader's ShadowPCF() always resolves to "fully lit".
    //
    struct grid_push_constants
    {
        xmath::fmat4    m_L2W;
        xmath::fmat4    m_W2C;
        xmath::fmat4    m_L2CTShadow;
        xmath::fvec3d   m_WorldSpaceCameraPos = xmath::fvec3(0.0f, 10.0f, 0.0f);
        float           m_MajorGridDiv = 10.0f;
    };

    xgpu::pipeline          Grid3dMaterial;
    xgpu::pipeline_instance Grid3dMaterialInstance;
    {
        xgpu::shader VertexShader;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::VERTEX
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e23::g_GridVertShader, sizeof(e23::g_GridVertShader) / sizeof(int)}}
            };
            if (auto Err = Device.Create(VertexShader, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader FragShader;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::FRAGMENT
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e23::g_GridFragShader, sizeof(e23::g_GridFragShader) / sizeof(int)}}
            };
            if (auto Err = Device.Create(FragShader, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        auto Samplers = std::array{ xgpu::pipeline::sampler{.m_AddressMode = std::array{ xgpu::pipeline::sampler::address_mode::CLAMP, xgpu::pipeline::sampler::address_mode::CLAMP, xgpu::pipeline::sampler::address_mode::CLAMP}} };
        auto Shaders  = std::array<const xgpu::shader*, 2>{ &FragShader, &VertexShader };
        auto Setup    = xgpu::pipeline::setup
        { .m_VertexDescriptor   = Primitive3DVertexDescriptor
        , .m_Shaders            = Shaders
        , .m_PushConstantsSize  = sizeof(grid_push_constants)
        , .m_Samplers           = Samplers
        , .m_Blend              = xgpu::pipeline::blend::getAlphaOriginal()
        };

        if (auto Err = Device.Create(Grid3dMaterial, Setup); Err)
            return xgpu::getErrorInt(Err);

        auto Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{*pDefaultTexture} };
        auto InstSetup = xgpu::pipeline_instance::setup{ .m_PipeLine = Grid3dMaterial, .m_SamplersBindings = Bindings };
        if (auto Err = Device.Create(Grid3dMaterialInstance, InstSetup); Err)
            return xgpu::getErrorInt(Err);
    }

    //
    // Wedge outline pipeline - LINE_LIST octahedral bone gizmos. Reuses the already-compiled
    // draw_vert/draw_frag shaders: one push-constant mat4 (L2C = View.getW2C(), since wedge
    // vertices are already in world space), per-vertex color, white sampler.
    //
    xgpu::pipeline          WedgeOutlinePipeline;
    xgpu::pipeline_instance WedgeOutlinePipelineInstance;
    {
        xgpu::shader VertexShader;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::VERTEX
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e23::g_OutlineVertShader, sizeof(e23::g_OutlineVertShader) / sizeof(int)}}
            };
            if (auto Err = Device.Create(VertexShader, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader FragShader;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::FRAGMENT
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e23::g_OutlineFragShader, sizeof(e23::g_OutlineFragShader) / sizeof(int)}}
            };
            if (auto Err = Device.Create(FragShader, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        auto Samplers = std::array{ xgpu::pipeline::sampler{} };
        auto Shaders  = std::array<const xgpu::shader*, 2>{ &FragShader, &VertexShader };
        auto Setup    = xgpu::pipeline::setup
        { .m_VertexDescriptor   = WedgeOutlineVertexDescriptor
        , .m_Shaders            = Shaders
        , .m_PushConstantsSize  = sizeof(e23::push_constants)
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
    // Wedge fill pipeline - same wedge shapes, same shaders, TRIANGLE_LIST instead of LINE_LIST
    // (Primitive3DVertexDescriptor already defaults to it) and alpha-blended for the light fill.
    //
    xgpu::pipeline          WedgeFillPipeline;
    xgpu::pipeline_instance WedgeFillPipelineInstance;
    {
        xgpu::shader VertexShader;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::VERTEX
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e23::g_OutlineVertShader, sizeof(e23::g_OutlineVertShader) / sizeof(int)}}
            };
            if (auto Err = Device.Create(VertexShader, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader FragShader;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::FRAGMENT
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e23::g_OutlineFragShader, sizeof(e23::g_OutlineFragShader) / sizeof(int)}}
            };
            if (auto Err = Device.Create(FragShader, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        auto Samplers = std::array{ xgpu::pipeline::sampler{} };
        auto Shaders  = std::array<const xgpu::shader*, 2>{ &FragShader, &VertexShader };
        auto Setup    = xgpu::pipeline::setup
        { .m_VertexDescriptor   = Primitive3DVertexDescriptor
        , .m_Shaders            = Shaders
        , .m_PushConstantsSize  = sizeof(e23::push_constants)
        , .m_Samplers           = Samplers
        , .m_Blend              = xgpu::pipeline::blend::getAlphaOriginal()
        };

        if (auto Err = Device.Create(WedgeFillPipeline, Setup); Err)
            return xgpu::getErrorInt(Err);

        auto Bindings  = std::array{ xgpu::pipeline_instance::sampler_binding{*pDefaultTexture} };
        auto InstSetup = xgpu::pipeline_instance::setup{ .m_PipeLine = WedgeFillPipeline, .m_SamplersBindings = Bindings };
        if (auto Err = Device.Create(WedgeFillPipelineInstance, InstSetup); Err)
            return xgpu::getErrorInt(Err);
    }

    //
    // 2D background pipeline (unchanged from the geom_static editor)
    //
    xgpu::pipeline Pipeline2D;
    {
        xgpu::vertex_descriptor VertexDescriptor2D;
        {
            auto Attributes = std::array
            { xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(e19::vert_2d, m_X),     .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D }
            , xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(e19::vert_2d, m_U),     .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D }
            , xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(e19::vert_2d, m_Color), .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED }
            };
            auto Setup = xgpu::vertex_descriptor::setup{ .m_VertexSize = sizeof(e19::vert_2d), .m_Attributes = Attributes };
            if (auto Err = Device.Create(VertexDescriptor2D, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader FragmentShader2D;
        {
            xgpu::shader::setup Setup{ .m_Type = xgpu::shader::type::bit::FRAGMENT, .m_Sharer = xgpu::shader::setup::raw_data{ e23::g_2DFragShader } };
            if (auto Err = Device.Create(FragmentShader2D, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader VertexShader2D;
        {
            xgpu::shader::setup Setup{ .m_Type = xgpu::shader::type::bit::VERTEX, .m_Sharer = xgpu::shader::setup::raw_data{ e23::g_2DVertShader } };
            if (auto Err = Device.Create(VertexShader2D, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        auto Shaders  = std::array<const xgpu::shader*, 2>{ &FragmentShader2D, &VertexShader2D };
        auto Samplers = std::array{ xgpu::pipeline::sampler{} };
        auto Setup    = xgpu::pipeline::setup
        { .m_VertexDescriptor   = VertexDescriptor2D
        , .m_Shaders            = Shaders
        , .m_PushConstantsSize  = sizeof(e23::push_const2D)
        , .m_Samplers           = Samplers
        , .m_DepthStencil       = {.m_bDepthTestEnable = false }
        };

        if (auto Err = Device.Create(Pipeline2D, Setup); Err)
            return xgpu::getErrorInt(Err);
    }

    xgpu::pipeline_instance BackGroundMaterialInstance;
    {
        auto Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{*pDefaultTexture} };
        auto Setup    = xgpu::pipeline_instance::setup{ .m_PipeLine = Pipeline2D, .m_SamplersBindings = Bindings };
        if (auto Err = Device.Create(BackGroundMaterialInstance, Setup); Err)
            return xgpu::getErrorInt(Err);
    }

    //
    // Dynamic wedge geometry buffers. The index buffer is a static 0..N-1 identity ramp (created
    // once); only the vertex buffer changes every frame, since depth tint depends on the live
    // camera position and bone count/geometry depends on the loaded skeleton.
    //
    xgpu::buffer WedgeIndexBuffer;
    if (auto Err = Device.Create(WedgeIndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = e23::g_MaxWedgeVertices }); Err)
        return xgpu::getErrorInt(Err);

    (void)WedgeIndexBuffer.MemoryMap(0, e23::g_MaxWedgeVertices, [&](void* pData)
    {
        auto* pIndex = static_cast<std::uint32_t*>(pData);
        for (int i = 0; i < e23::g_MaxWedgeVertices; ++i) pIndex[i] = static_cast<std::uint32_t>(i);
    });

    xgpu::buffer WedgeVertexBuffer;
    if (auto Err = Device.Create(WedgeVertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_Usage = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ, .m_EntryByteSize = sizeof(e19::draw_vert), .m_EntryCount = e23::g_MaxWedgeVertices }); Err)
        return xgpu::getErrorInt(Err);

    // Reuses WedgeIndexBuffer (0,1,2,3,... identity mapping) - valid for TRIANGLE_LIST too.
    xgpu::buffer WedgeFillVertexBuffer;
    if (auto Err = Device.Create(WedgeFillVertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_Usage = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ, .m_EntryByteSize = sizeof(e19::draw_vert), .m_EntryCount = e23::g_MaxWedgeVertices }); Err)
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
    e23::skeleton_state  SkeletonState;

    //
    // Setup Imgui interface
    //
    xgpu::tools::imgui::CreateInstance(MainWindow);

    //
    // Set the project path
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

            //
            // Open the project
            //
            if (auto Err = e10::g_LibMgr.OpenProject(szFileName); Err)
            {
                e23::Debugger(Err.getMessage());
                return 1;
            }

            ImGuiIO& io = ImGui::GetIO();
            static std::string IniSave = std::format("{}/Assets/imgui.ini", xstrtool::To(szFileName));
            io.IniFilename = IniSave.c_str();

            //
            // Set the path for the resources
            //
            ResourceMgrUserData.m_Device = Device;
            xresource::g_Mgr.setUserData(&ResourceMgrUserData, false);
            xresource::g_Mgr.setRootPath(std::format(L"{}//Cache//Resources//Platforms//Windows", e10::g_LibMgr.m_ProjectPath));
        }
    }

    //
    // Setup the view (camera)
    //
    xgpu::tools::view  View        = {};
    xmath::radian3     Angles      = {};
    float              Distance    = -1;   // Let it automatically compute it once a skeleton loads
    xmath::fvec3       CameraTarget(0, 0, 0);
    View.setFov(60_xdeg);

    //
    // Create input devices
    //
    xgpu::mouse    Mouse;
    xgpu::keyboard Keyboard;
    Instance.Create(Mouse, {});
    Instance.Create(Keyboard, {});

    // Rigged shadow projection: every world position maps to (0,0,2,1) - z=2 is outside the
    // shader's [-1,1] shadow range, so ShadowPCF() always resolves to "fully lit" (Shadow = 1.0).
    const xmath::fmat4 g_DisabledShadowL2C = []
    {
        xmath::fmat4 M;
        M.setupZero();
        M(2, 3) = 2.0f;
        M(3, 3) = 1.0f;
        return M;
    }();

    std::vector<e19::draw_vert>          WedgeVerts;
    std::vector<e19::draw_vert>          WedgeFillVerts;
    std::vector<e23::label_rect_hit>     LabelHits;

    //
    // Main Loop
    //
    while (Instance.ProcessInputEvents())
    {
        if (xgpu::tools::imgui::BeginRendering(true)) continue;

        const float MainWindowWidth  = static_cast<float>(MainWindow.getWidth());
        const float MainWindowHeight = static_cast<float>(MainWindow.getHeight());

        const bool bViewportHovered = [&]
        {
            auto ctx = ImGui::GetCurrentContext();
            return ctx->HoveredWindow == nullptr || ctx->HoveredWindow->ID == ImGui::GetID("MainDockSpace");
        }();

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

        //
        // Skeleton render + labels + picking
        //
        if (!SkeletonState.empty())
        {
            if (auto* pSkeleton = xresource::g_Mgr.getResource(SkeletonState.m_Ref); pSkeleton)
            {
                View.setViewport({ 0, 0, static_cast<int>(MainWindowWidth), static_cast<int>(MainWindowHeight) });

                if (SkeletonState.m_bNeedsReframe)
                {
                    SkeletonState.m_bNeedsReframe = false;

                    const float VerticalFov = View.getFov().m_Value;
                    const float Aspect      = View.getAspect();
                    const float HFov        = 2.0f * std::atan(Aspect * std::tan(VerticalFov * 0.5f));
                    const float MinFov      = std::min(VerticalFov, HFov);

                    Distance     = SkeletonState.m_Radius / std::tan(MinFov * 0.5f);
                    CameraTarget = SkeletonState.m_Center;
                }

                View.LookAt(Distance, Angles, CameraTarget);

                e23::wedge_style Style;
                Style.m_CameraPos = View.getPosition();
                Style.m_NearDepth = std::max(0.01f, Distance * 0.25f);
                Style.m_FarDepth  = Distance * 1.6f + SkeletonState.m_Radius;

                e23::BuildWedgeGeometry(*pSkeleton, SkeletonState.ActiveBoneWorld(), SkeletonState.m_bIsTwistBone, Style, SkeletonState.m_iSelectedBone, WedgeVerts);
                e23::BuildWedgeFillGeometry(*pSkeleton, SkeletonState.ActiveBoneWorld(), SkeletonState.m_bIsTwistBone, Style, SkeletonState.m_iSelectedBone, WedgeFillVerts);

                auto CmdBuffer = MainWindow.getCmdBuffer();

                //
                // Ground grid, for spatial context
                //
                {
                    CmdBuffer.setPipelineInstance(Grid3dMaterialInstance);
                    grid_push_constants Push;
                    Push.m_WorldSpaceCameraPos = View.getPosition();
                    Push.m_L2W        = xmath::fmat4(xmath::fvec3(100.f, 100.0f, 1.f), xmath::radian3(-90_xdeg, 0_xdeg, 0_xdeg), xmath::fvec3(0, SkeletonState.m_Center.m_Y - SkeletonState.m_Radius, 0));
                    Push.m_W2C        = View.getW2C();
                    Push.m_L2CTShadow = g_DisabledShadowL2C;
                    CmdBuffer.setPushConstants(Push);
                    MeshManager.Rendering(CmdBuffer, e19::mesh_manager::model::PLANE3D);
                }

                //
                // Bone wedges - light fill first, so the outline pass draws crisply on top of it.
                //
                if (!WedgeFillVerts.empty())
                {
                    (void)WedgeFillVertexBuffer.MemoryMap(0, static_cast<int>(WedgeFillVerts.size()), [&](void* pData)
                    {
                        std::memcpy(pData, WedgeFillVerts.data(), WedgeFillVerts.size() * sizeof(e19::draw_vert));
                    });

                    CmdBuffer.setPipelineInstance(WedgeFillPipelineInstance);
                    CmdBuffer.setBuffer(WedgeIndexBuffer);
                    CmdBuffer.setBuffer(WedgeFillVertexBuffer);
                    CmdBuffer.setPushConstants(e23::push_constants{ .m_L2C = View.getW2C() });
                    CmdBuffer.Draw(static_cast<int>(WedgeFillVerts.size()));
                }

                if (!WedgeVerts.empty())
                {
                    (void)WedgeVertexBuffer.MemoryMap(0, static_cast<int>(WedgeVerts.size()), [&](void* pData)
                    {
                        std::memcpy(pData, WedgeVerts.data(), WedgeVerts.size() * sizeof(e19::draw_vert));
                    });

                    CmdBuffer.setPipelineInstance(WedgeOutlinePipelineInstance);
                    CmdBuffer.setBuffer(WedgeIndexBuffer);
                    CmdBuffer.setBuffer(WedgeVertexBuffer);
                    CmdBuffer.setPushConstants(e23::push_constants{ .m_L2C = View.getW2C() });
                    CmdBuffer.Draw(static_cast<int>(WedgeVerts.size()));
                }

                //
                // Labels (foreground overlay) + hit rects for this frame's click test
                //
                const xmath::irect Viewport{ 0, 0, static_cast<int>(MainWindowWidth), static_cast<int>(MainWindowHeight) };
                e23::RenderBoneLabelsAndCollectHits(View, *pSkeleton, SkeletonState, Viewport, LabelHits);

                //
                // Viewport click: label rect first (cheap), then ray-vs-wedge-face
                //
                if (bViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    const ImVec2 MousePos = ImGui::GetMousePos();
                    int iHitBone = -1;

                    for (auto& H : LabelHits)
                    {
                        if (MousePos.x >= H.m_Min.m_X && MousePos.x <= H.m_Max.m_X &&
                            MousePos.y >= H.m_Min.m_Y && MousePos.y <= H.m_Max.m_Y)
                        {
                            iHitBone = H.m_iBone;
                            break;
                        }
                    }

                    if (iHitBone == -1)
                    {
                        const xmath::fvec3 RayOrigin = View.getPosition();
                        const xmath::fvec3 RayDir    = View.RayFromScreen(MousePos.x, MousePos.y);
                        float BestT;
                        e23::PickWedge(*pSkeleton, SkeletonState.ActiveBoneWorld(), RayOrigin, RayDir, iHitBone, BestT);
                    }

                    if (iHitBone != -1) SkeletonState.m_iSelectedBone = iHitBone;
                }
            }
        }

        //
        // Bones side panel
        //
        if (!SkeletonState.empty())
        {
            if (auto* pSkeleton = xresource::g_Mgr.getResource(SkeletonState.m_Ref); pSkeleton)
            {
                ImGui::SetNextWindowSize(ImVec2(280, 420), ImGuiCond_FirstUseEver);
                if (ImGui::Begin("Bones"))
                {
                    ImGui::Text("%d bones", static_cast<int>(pSkeleton->getBones().size()));
                    ImGui::RadioButton("Frozen Pose", reinterpret_cast<int*>(&SkeletonState.m_PoseMode), static_cast<int>(e23::pose_mode::FROZEN));
                    ImGui::SameLine();
                    ImGui::RadioButton("Bind Pose", reinterpret_cast<int*>(&SkeletonState.m_PoseMode), static_cast<int>(e23::pose_mode::BIND));
                    ImGui::Separator();
                    ImGui::BeginChild("###BoneListChild");
                    e23::RenderBoneTree(SkeletonState, *pSkeleton);
                    ImGui::EndChild();
                }
                ImGui::End();
            }
        }

        //
        // Main menu bar
        //
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("\xEE\x98\xAB Home\xee\xa5\xb2"))
            {
                if (ImGui::MenuItem("Resource Browser", "Ctrl-Space"))
                    AsserBrowser.Show(true);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        AsserBrowser.Render(e10::g_LibMgr, xresource::g_Mgr);

        if (auto SelAsset = AsserBrowser.getSelectedAsset(); SelAsset.empty() == false && SelAsset.m_Type == xrsc::skeleton_type_guid_v)
        {
            e23::LoadSkeleton(SkeletonState, AsserBrowser.getSelectedLibrary(), SelAsset);
        }

        xgpu::tools::imgui::Render();

        MainWindow.PageFlip();

        // Let the resource manager know we have changed the frame
        xresource::g_Mgr.OnEndFrameDelegate();
    }

    xgpu::tools::imgui::Shutdown();

    return 0;
}
