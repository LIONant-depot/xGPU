#include "source/xGPU.h"

#include "dependencies/xproperty/source/xcore/my_properties.h"
#include "dependencies/xproperty/source/examples/imgui/xPropertyImGuiInspector.h"
#include "dependencies/xstrtool/source/xstrtool.h"

#include "source/tools/xgpu_xcore_bitmap_helpers.h"
#include "source/tools/xgpu_view.h"
#include <algorithm>
#include <cfloat>
#include <unordered_map>
#include <functional>
#include <filesystem>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>
#include <optional>
#include <set>

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

    // GPU picking - same position transform as g_OutlineVertShader, but its own vertex shader:
    // xGPU requires every stage in a pipeline to declare an identical push-constant layout, and
    // draw_vert.glsl's push-constant block (just mat4 L2C) is shorter than E23_Pick_frag.glsl's
    // (mat4 L2C + int BoneID), so it can't be reused here. See E23_Pick_vert/frag.glsl.
    constexpr static std::uint32_t g_PickVertShader[] =
    {
        #include "E23_Pick_vert.h"
    };
    constexpr static std::uint32_t g_PickFragShader[] =
    {
        #include "E23_Pick_frag.h"
    };

    // Wedge fill - same texture*color+gamma-decode as g_OutlineFragShader (draw_frag.h), plus an
    // RGB brightness boost applied after the alpha blend's own dilution would otherwise wash it out
    // (see E23_WedgeFill_frag.glsl's own comment for the full reasoning). Its own vertex shader for
    // the same push-constant-layout reason as the pick pipeline above.
    constexpr static std::uint32_t g_WedgeFillVertShader[] =
    {
        #include "E23_WedgeFill_vert.h"
    };
    constexpr static std::uint32_t g_WedgeFillFragShader[] =
    {
        #include "E23_WedgeFill_frag.h"
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

    // Matches E23_WedgeFill_vert/frag.glsl's PushConsts block exactly (mat4 first, then the float) -
    // the vertex shader ignores m_Boost, but it has to be declared there too so it lands at the same
    // byte offset on both sides of the push-constant range (same reason pick_push_constants exists).
    struct wedge_fill_push_constants
    {
        xmath::fmat4    m_L2C;
        float           m_Boost;
    };

    // Matches E23_Pick_frag.glsl's PushConsts block exactly (mat4 first, then the int) - the
    // fragment shader ignores m_L2C, but it has to be declared there too so m_BoneID lands at the
    // same byte offset on both sides of the push-constant range.
    struct pick_push_constants
    {
        xmath::fmat4    m_L2C;
        std::int32_t    m_BoneID;
    };

    // Matches E23_Pick_frag.glsl's kBoneIDBits/kBoneIDMask exactly - PickBuffer.BestKey packs
    // (quantized depth << kBoneIDBits) | BoneID and is reduced with atomicMin there (a plain store
    // race between two same-pixel candidates otherwise let either one "win" regardless of which was
    // actually closer - see that shader's own comment). 0xFFFFFFFF means no candidate ever passed
    // depth at this pixel - the same bit pattern the CPU-side reset already writes as int32_t(-1).
    constexpr std::uint32_t g_PickNoHitKey = 0xFFFFFFFFu;
    int DecodePickKey(std::uint32_t Key) noexcept
    {
        constexpr std::uint32_t BoneIDMask = 0xFFFu; // kBoneIDMask in E23_Pick_frag.glsl
        return (Key == g_PickNoHitKey) ? -1 : static_cast<int>(Key & BoneIDMask);
    }

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

    // FROZEN: forward-kinematics pose from each bone's local rest transform - the pose the skeleton
    // resource was authored/animated in. BIND: the pose the mesh was originally skinned against;
    // bones with no real bind data (see the fill/outline dimming) had their position inferred by
    // propagating from the nearest ancestor that has one.
    enum class pose_mode { FROZEN, BIND };

    //---------------------------------------------------------------------------

    struct skeleton_state
    {
        xrsc::skeleton                                     m_Ref           = {};
        e10::library::guid                                 m_LibraryGUID   = {};
        xresource::full_guid                               m_InfoGUID      = {};
        std::wstring                                       m_DescriptorPath= {};
        xskeleton_desc::descriptor                          m_Descriptor    = {}; // backs the "Skeleton Properties" inspector - see LoadSkeleton
        std::unordered_map<std::uint32_t, std::string>     m_BoneNames     = {};
        std::unordered_map<std::uint32_t, std::string>     m_RawBoneNames  = {}; // display-name hash -> raw (pre-strip) import name, see LoadBoneNameMap
        std::vector<bone_world>                            m_BoneWorldFrozen = {};
        std::vector<bone_world>                            m_BoneWorldBind   = {};
        std::vector<bool>                                  m_bIsTwistBone    = {}; // pose-independent, see LoadSkeleton
        xmath::fvec3                                       m_Center        = xmath::fvec3(0.0f, 0.0f, 0.0f);
        float                                              m_Radius        = 1.0f;

        // Multi-select: m_SelectedBones is the actual selection; m_iAnchorBone is the standard
        // Explorer-style anchor - the bone a plain or Ctrl+click last landed on, which a following
        // Shift+click ranges from (and which does NOT move on a Shift+click itself).
        std::set<int>                                       m_SelectedBones = {};
        int                                                 m_iAnchorBone   = -1;
        bool                                                m_bNeedsReframe = true;
        pose_mode                                           m_PoseMode      = pose_mode::FROZEN; // a view preference, not asset state - left alone by clear()
        bool                                                m_bAlwaysShowNames = false;           // view preference - see RenderBoneLabelsAndCollectHits
        bool                                                m_bNormalizeSize   = true;            // view preference - see ViewScale()
        bool                                                m_bColorByLOD      = false;           // view preference - see LODColor()/GetBoneLODLevel()

        // Inline tree rename (double-click a bone's label) - transient UI state, not asset state.
        int                                                 m_iRenamingBone     = -1;
        bool                                                m_bRenameJustStarted = false;
        std::string                                         m_RenameBuf         = {};

        // Compile/save tracking - mirrors E21_StaticGeomEditor's selected_descriptor. Saving the
        // descriptor (SaveSkeletonDescriptor) is what actually triggers a recompile - a background
        // file-watcher in the library manager picks up the change and runs the plugin's compiler,
        // broadcasting progress via e10::g_LibMgr.m_OnCompilationState (see the registration in
        // E23_Example) to whichever shared_ptr<log> this GUID's compile currently owns.
        std::shared_ptr<e10::compilation::historical_entry::log> m_Log     = {};
        bool                                                m_bReload       = false;
        bool                                                m_bErrors       = false;

        bool empty() const noexcept { return m_InfoGUID.empty(); }

        const std::vector<bone_world>& ActiveBoneWorld() const noexcept
        {
            return m_PoseMode == pose_mode::BIND ? m_BoneWorldBind : m_BoneWorldFrozen;
        }

        // Asset units are whatever the source FBX used - could be a 2cm mannequin or a 200m dragon.
        // "Resize Skeleton to 1m" scales the VIEW ONLY (never the underlying asset) so m_Radius reads
        // as 1 unit, keeping the grid/near-far planes/wedge-size floors (all tuned assuming a roughly
        // human-scale subject) sane regardless of the asset's native scale.
        float ViewScale() const noexcept
        {
            return (m_bNormalizeSize && m_Radius > 1.0e-6f) ? (1.0f / m_Radius) : 1.0f;
        }

        void clear()
        {
            m_LibraryGUID.clear();
            m_InfoGUID.clear();
            m_DescriptorPath.clear();
            m_Descriptor    = {};
            m_BoneNames.clear();
            m_RawBoneNames.clear();
            m_BoneWorldFrozen.clear();
            m_BoneWorldBind.clear();
            m_bIsTwistBone.clear();
            m_Center        = xmath::fvec3(0.0f, 0.0f, 0.0f);
            m_Radius        = 1.0f;
            m_SelectedBones.clear();
            m_iAnchorBone   = -1;
            m_bNeedsReframe = true;
            m_iRenamingBone = -1;
            m_bRenameJustStarted = false;
            m_RenameBuf.clear();
            m_Log           = std::make_shared<e10::compilation::historical_entry::log>(e10::compilation::historical_entry::communication{ .m_Result = e10::compilation::historical_entry::result::SUCCESS });
            m_bReload       = false;
            m_bErrors       = false;
        }

        void SaveDescriptor()
        {
            xproperty::settings::context Context;
            if (auto Err = m_Descriptor.Serialize(false, m_DescriptorPath, Context); Err)
                assert(false);
        }

        // Render/view settings, as a property (see the "Rendering Settings" inspector in the main
        // loop) rather than hardcoded ImGui widgets - matches E21_StaticGeomEditor's pattern. Pose
        // is a dynamic string property rendered as a button (member_ui<std::string>::button<>,
        // same idiom E21_StaticGeom_Editor.cpp's own "Recenter" button uses): on read, the lambda
        // reports the pose it'll switch TO as the button's own label; on write (i.e. the button was
        // clicked), it toggles the actual mode. This keeps it a real row in this same property grid
        // instead of a hand-drawn ImGui::Button floating outside it.
        XPROPERTY_DEF
        ( "Skeleton View", skeleton_state
        , obj_member<"Pose", +[](skeleton_state& O, bool bRead, std::string& Value)
            {
                if (bRead) Value = (O.m_PoseMode == pose_mode::FROZEN) ? "To Bind" : "To Frozen";
                else       O.m_PoseMode = (O.m_PoseMode == pose_mode::FROZEN) ? pose_mode::BIND : pose_mode::FROZEN;
            }
          , member_ui<std::string>::button<>
          >
        , obj_member<"Always Show Names",     &skeleton_state::m_bAlwaysShowNames >
        , obj_member<"Resize Skeleton to 1m", &skeleton_state::m_bNormalizeSize >
        , obj_member<"Color by LOD",          &skeleton_state::m_bColorByLOD >
        )
    };
    XPROPERTY_REG(skeleton_state)

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

    // Details.txt is the compiler's log of what it actually saw on the last import - the source of
    // truth for raw bone names (see xskeleton_desc::details and xskeleton_compiler.cpp:428). Loaded
    // once per LoadSkeleton and shared between name resolution and MergeWithDetails, rather than
    // re-parsed by each.
    xskeleton_desc::details LoadDetails(const std::wstring& DescriptorPath)
    {
        xskeleton_desc::details Details;
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

    // OutRawMap is keyed the same way (hash of the display name) but holds the RAW, pre-strip name -
    // the compiler's override lookup (CollectOverrides in xskeleton_compiler.cpp) matches descriptor
    // bone entries against the raw imported name, vbone_ prefix and all, not the stripped display
    // name - authoring a new override under the display name would silently never match.
    void LoadBoneNameMap(const xskeleton_desc::details& Details, const std::wstring& DescriptorPath, std::unordered_map<std::uint32_t, std::string>& OutMap, std::unordered_map<std::uint32_t, std::string>& OutRawMap)
    {
        OutMap.clear();
        OutRawMap.clear();

        auto AddName = [&](std::string_view RawName)
        {
            const std::string   DisplayName = StripVBoneTag(RawName);
            const std::uint32_t Hash        = xstrtool::CRC32(DisplayName);
            OutMap[Hash]    = DisplayName;
            OutRawMap[Hash] = std::string(RawName);
        };

        for (auto& Name : Details.m_BoneList) AddName(Name);

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
    // "Resize Skeleton to 1m" (skeleton_state::ViewScale()) never touches the loaded asset - instead
    // every consumer of bone positions each frame works off THIS scaled copy. m_Right/m_Up are unit
    // directions, unaffected by a uniform scale.
    //---------------------------------------------------------------------------

    void ScaleBoneWorld(const std::vector<bone_world>& Src, float Scale, std::vector<bone_world>& Out)
    {
        Out.resize(Src.size());
        for (std::size_t i = 0; i < Src.size(); ++i)
        {
            Out[i]            = Src[i];
            Out[i].m_Position = Src[i].m_Position * Scale;
        }
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
        {
            const auto Details = LoadDetails(State.m_DescriptorPath);
            LoadBoneNameMap(Details, State.m_DescriptorPath, State.m_BoneNames, State.m_RawBoneNames);

            xproperty::settings::context Context;
            if (auto Err = State.m_Descriptor.Serialize(true, State.m_DescriptorPath, Context); Err)
                assert(false);

            // Seed/reconcile the descriptor with an entry for every currently-imported bone (see
            // xskeleton_desc::descriptor::MergeWithDetails) - same job E21_StaticGeomEditor's
            // MergeWithDetails does on selection/reload, so "Bone Hierarchy" in the Skeleton
            // Properties inspector shows the whole skeleton immediately, not just whichever bones
            // someone already curated. In-memory only - Compile is what persists it to disk.
            State.m_Descriptor.MergeWithDetails(Details);
        }

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

    // The raw (pre-strip) import name - what the compiler's CollectOverrides actually matches
    // descriptor bone entries against (see LoadBoneNameMap). Falls back to the display name for a
    // bone with no known raw counterpart (e.g. purely descriptor-authored, never imported).
    std::string GetRawBoneName(const skeleton_state& State, const xskeleton::skeleton& Skeleton, int iBone)
    {
        const std::uint32_t Hash = Skeleton.getBoneNames()[iBone].m_NameHash;
        if (auto It = State.m_RawBoneNames.find(Hash); It != State.m_RawBoneNames.end())
            return It->second;
        return GetBoneDisplayName(State, Skeleton, iBone);
    }

    //---------------------------------------------------------------------------
    // Bone overrides - sparse entries in the descriptor's own m_RootBone tree, matched BY NAME
    // against the raw imported bone (xskeleton_compiler.cpp's CollectOverrides walks the whole tree
    // into a flat name->override map before compiling) - the same "survives re-import" pattern
    // xgeom_static's merge/ungroup/delete lists use. Tree position inside the override tree is
    // irrelevant to the compiler, so a newly-authored entry can just be appended as a direct child of
    // the root rather than mirroring the bone's real position in the hierarchy.
    //---------------------------------------------------------------------------

    xskeleton_desc::bone* FindBoneOverride(xskeleton_desc::bone& Node, std::string_view Name)
    {
        if (Node.m_Name == Name) return &Node;
        for (auto& Child : Node.m_Bones)
            if (auto* pFound = FindBoneOverride(Child, Name)) return pFound;
        return nullptr;
    }

    const xskeleton_desc::bone* FindBoneOverride(const xskeleton_desc::bone& Node, std::string_view Name)
    {
        if (Node.m_Name == Name) return &Node;
        for (auto& Child : Node.m_Bones)
            if (auto* pFound = FindBoneOverride(Child, Name)) return pFound;
        return nullptr;
    }

    xskeleton_desc::bone& FindOrCreateBoneOverride(xskeleton_desc::descriptor& Descriptor, std::string_view Name)
    {
        if (auto* pFound = FindBoneOverride(Descriptor.m_RootBone, Name)) return *pFound;
        xskeleton_desc::bone NewOverride;
        NewOverride.m_Name = std::string(Name);
        Descriptor.m_RootBone.m_Bones.push_back(std::move(NewOverride));
        return Descriptor.m_RootBone.m_Bones.back();
    }

    // The name to actually SHOW anywhere in this editor (tree, 3D labels) - the override's m_Rename
    // if the user has renamed this bone, otherwise the imported display name. m_Name itself never
    // changes on rename (see xskeleton_desc::bone::m_Rename's own comment), so overrides keep
    // matching this bone across re-imports even after it's been renamed here.
    std::string GetEffectiveBoneName(const skeleton_state& State, const xskeleton::skeleton& Skeleton, int iBone)
    {
        const std::string RawName = GetRawBoneName(State, Skeleton, iBone);
        if (const auto* pOv = FindBoneOverride(State.m_Descriptor.m_RootBone, RawName); pOv && !pOv->m_Rename.empty())
            return pOv->m_Rename;
        return GetBoneDisplayName(State, Skeleton, iBone);
    }

    //---------------------------------------------------------------------------
    // Selection - standard Explorer-style multi-select: a plain click replaces the selection and
    // moves the anchor; Ctrl+click toggles one bone in/out without moving the anchor's fellow
    // members (but does move the anchor to whatever was just toggled, matching Explorer); Shift+
    // click selects every bone whose raw index falls between the anchor and the clicked bone
    // (inclusive) WITHOUT moving the anchor, so repeated Shift+clicks resize the same range rather
    // than compounding. Raw bone index is used as the "range" ordering rather than tree position or
    // screen position - bones are already stored parent-before-child (see LoadSkeleton), so index
    // order tracks the hierarchy/import order closely enough to read as a sensible range either from
    // the tree view or from repeated viewport clicks.
    //---------------------------------------------------------------------------

    void ApplySelection(skeleton_state& State, int iBone, bool bCtrl, bool bShift)
    {
        if (bShift && State.m_iAnchorBone != -1)
        {
            if (!bCtrl) State.m_SelectedBones.clear();
            const int Lo = std::min(State.m_iAnchorBone, iBone);
            const int Hi = std::max(State.m_iAnchorBone, iBone);
            for (int i = Lo; i <= Hi; ++i) State.m_SelectedBones.insert(i);
        }
        else if (bCtrl)
        {
            if (State.m_SelectedBones.count(iBone)) State.m_SelectedBones.erase(iBone);
            else                                    State.m_SelectedBones.insert(iBone);
            State.m_iAnchorBone = iBone;
        }
        else
        {
            State.m_SelectedBones.clear();
            State.m_SelectedBones.insert(iBone);
            State.m_iAnchorBone = iBone;
        }
    }

    // A click that hits nothing (no label, no bone) clears the selection - but only a PLAIN click;
    // Ctrl/Shift-clicking empty space is a no-op, same convention as Explorer/most 3D editors (you
    // don't lose a multi-selection just because the next Ctrl+click missed).
    void ClearSelectionOnEmptyClick(skeleton_state& State, bool bCtrl, bool bShift)
    {
        if (bCtrl || bShift) return;
        State.m_SelectedBones.clear();
        State.m_iAnchorBone = -1;
    }

    //---------------------------------------------------------------------------
    // Side-panel bone list, as a tree following the actual skeleton hierarchy rather than a flat
    // sort - bones are stored parent-index-before-child, so a single pass builds each parent's
    // child list, then a normal recursive TreeNode walk from the root does the rest. Each node is
    // colored/tagged by its current (uncompiled) override state and offers a right-click menu to
    // change it - Save/Compile is what actually applies the edit, matching E21_StaticGeomEditor's
    // scene-hierarchy tree (color-coded per grouped/deleted state, right-click context menu).
    //---------------------------------------------------------------------------

    // Segoe MDL2 Assets glyphs - the icon font is already merged into the default ImGui font (see
    // xgpu_imgui_breach.cpp), so no PushFont is needed; UTF-8 encoded directly since that's what
    // ImGui's text calls expect. Hover tooltips (see RenderBoneTree) carry the actual meaning -
    // these just need to read as distinct pictograms at a glance.
    constexpr const char* g_ExposeIcon  = "\xEE\x9C\x98"; // Pin
    constexpr const char* g_VirtualIcon = "\xEE\x9C\x9B"; // Link
    constexpr const char* g_DeleteIcon  = "\xEE\x9D\x8D"; // Delete (trash)

    // Bones edited via the checkbox columns or the context menu apply to the WHOLE current
    // selection when the acted-on bone is part of a multi-bone selection (standard "edit one,
    // apply to all selected" convention) - otherwise just to that one bone.
    std::vector<int> EditTargets(const skeleton_state& State, int iBone)
    {
        if (State.m_SelectedBones.count(iBone) && State.m_SelectedBones.size() > 1)
            return std::vector<int>(State.m_SelectedBones.begin(), State.m_SelectedBones.end());
        return { iBone };
    }

    // The compiler enforces "a bone's LOD can never be lower than its parent's" and silently
    // clamps-and-warns when the descriptor disagrees (xskeleton_compiler.cpp's BuildMergedBones) -
    // mirroring that here, live, means the editor never lets you author something the compiler is
    // just going to override anyway. Propagates the JUST-CHANGED bone's own (possibly already-
    // clamped) LOD down through its whole subtree, raising any descendant that's currently lower;
    // a descendant already at or above stays untouched, including its own effect on its children.
    void ClampDescendantLODs(skeleton_state& State, const xskeleton::skeleton& Skeleton, const std::vector<std::vector<int>>& Children, int iBone, int MinLOD)
    {
        for (int iChild : Children[iBone])
        {
            auto& ChildOv = FindOrCreateBoneOverride(State.m_Descriptor, GetRawBoneName(State, Skeleton, iChild));
            if (ChildOv.m_LODLevel < MinLOD) ChildOv.m_LODLevel = MinLOD;
            ClampDescendantLODs(State, Skeleton, Children, iChild, ChildOv.m_LODLevel);
        }
    }

    // Centers a checkbox-sized widget horizontally within whatever's left of the current column -
    // otherwise it hugs the column's left edge, misaligned with the icon header above it.
    void CenterNextCheckbox()
    {
        const float ColW = ImGui::GetContentRegionAvail().x;
        const float BoxW = ImGui::GetFrameHeight();
        if (ColW > BoxW) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ColW - BoxW) * 0.5f);
    }

    void RenderBoneNode(skeleton_state& State, const xskeleton::skeleton& Skeleton, const std::vector<std::vector<int>>& Children, int iBone)
    {
        const bool        bSelected = State.m_SelectedBones.count(iBone) != 0;
        const bool        bHasKids  = !Children[iBone].empty();
        const std::string Name      = GetEffectiveBoneName(State, Skeleton, iBone);
        const std::string RawName   = GetRawBoneName(State, Skeleton, iBone);

        const xskeleton_desc::bone* pOv = FindBoneOverride(State.m_Descriptor.m_RootBone, RawName);

        const bool bPendingDelete  = pOv && pOv->m_bDeleteBone;
        const bool bPendingVirtual = pOv && pOv->m_Type == xskeleton_desc::bone_type::VIRTUAL;
        const bool bPendingExpose  = pOv && pOv->m_bExpose;
        const bool bRenaming       = State.m_iRenamingBone == iBone;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_DefaultOpen;
        if (bSelected)  Flags |= ImGuiTreeNodeFlags_Selected;
        if (!bHasKids)  Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        const bool bPushedColor = bPendingDelete || bPendingVirtual;
        if (bPendingDelete)       ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 90, 90, 255));
        else if (bPendingVirtual) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 180, 84, 255));

        std::string Label = Name;
        if (pOv && pOv->m_LODLevel > 0) Label += std::format("  [LOD{}]", pOv->m_LODLevel);
        if (bPendingDelete)              Label += "  (pending delete)";

        // While renaming, the node still owns the arrow/indentation/click region but shows an empty
        // label - the actual editable text lives in the InputText drawn right after it below.
        const bool bOpen = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<std::intptr_t>(iBone)), Flags, "%s", bRenaming ? "" : Label.c_str());
        if (bPushedColor) ImGui::PopStyleColor();

        if (bRenaming)
        {
            ImGui::SameLine();
            static char Buf[128]; // only ever one row renames at a time - a single scratch buffer is enough
            if (State.m_bRenameJustStarted)
            {
                std::snprintf(Buf, sizeof(Buf), "%s", State.m_RenameBuf.c_str());
                ImGui::SetKeyboardFocusHere();
                State.m_bRenameJustStarted = false;
            }
            ImGui::PushID(iBone);
            ImGui::SetNextItemWidth(-FLT_MIN);
            const bool bEnter = ImGui::InputText("##rename", Buf, sizeof(Buf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (bEnter || ImGui::IsItemDeactivatedAfterEdit())
            {
                FindOrCreateBoneOverride(State.m_Descriptor, RawName).m_Rename = Buf;
                State.m_iRenamingBone = -1;
            }
            else if (ImGui::IsItemDeactivated())
            {
                State.m_iRenamingBone = -1; // Escape (or anything else that didn't actually change the text) - cancel, don't write
            }
            ImGui::PopID();
        }
        else
        {
            if (ImGui::IsItemClicked())
                ApplySelection(State, iBone, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                State.m_iRenamingBone      = iBone;
                State.m_bRenameJustStarted = true;
                State.m_RenameBuf          = Name;
            }
        }

        // Right-click context menu - anchored to the tree node (SpanFullWidth, so this covers the
        // whole row) with an explicit per-bone ID, rather than relying on BeginPopupContextItem()'s
        // default "use whatever the last-drawn widget's ID was" behavior: that broke once the LOD
        // column's InputInt became the new "last item" this popup implicitly leaned on, hitting
        // ImGui's own "id != 0" assert (imgui.cpp - you cannot rely on a last-item ID that got reset).
        ImGui::PushID(iBone);
        if (ImGui::BeginPopupContextItem("BoneRowContextMenu"))
        {
            if (!bSelected) ApplySelection(State, iBone, false, false);

            if (ImGui::MenuItem("Rename"))
            {
                State.m_iRenamingBone      = iBone;
                State.m_bRenameJustStarted = true;
                State.m_RenameBuf          = Name;
            }

            if (ImGui::MenuItem(bPendingVirtual ? "Mark as Normal Bone" : "Mark as Virtual Bone"))
                for (int i : EditTargets(State, iBone))
                    FindOrCreateBoneOverride(State.m_Descriptor, GetRawBoneName(State, Skeleton, i)).m_Type = bPendingVirtual ? xskeleton_desc::bone_type::NORMAL : xskeleton_desc::bone_type::VIRTUAL;

            if (ImGui::MenuItem(bPendingDelete ? "Unmark Deletion" : "Mark for Deletion"))
                for (int i : EditTargets(State, iBone))
                    FindOrCreateBoneOverride(State.m_Descriptor, GetRawBoneName(State, Skeleton, i)).m_bDeleteBone = !bPendingDelete;

            if (ImGui::MenuItem(bPendingExpose ? "Unexpose Socket" : "Expose as Socket"))
                for (int i : EditTargets(State, iBone))
                    FindOrCreateBoneOverride(State.m_Descriptor, GetRawBoneName(State, Skeleton, i)).m_bExpose = !bPendingExpose;

            ImGui::EndPopup();
        }
        ImGui::PopID();

        ImGui::TableSetColumnIndex(1);
        {
            bool bExpose = bPendingExpose;
            ImGui::PushID(iBone);
            CenterNextCheckbox();
            if (ImGui::Checkbox("##expose", &bExpose))
                for (int i : EditTargets(State, iBone))
                    FindOrCreateBoneOverride(State.m_Descriptor, GetRawBoneName(State, Skeleton, i)).m_bExpose = bExpose;
            ImGui::PopID();
        }

        ImGui::TableSetColumnIndex(2);
        {
            bool bVirtual = bPendingVirtual;
            ImGui::PushID(iBone);
            CenterNextCheckbox();
            if (ImGui::Checkbox("##virtual", &bVirtual))
                for (int i : EditTargets(State, iBone))
                    FindOrCreateBoneOverride(State.m_Descriptor, GetRawBoneName(State, Skeleton, i)).m_Type = bVirtual ? xskeleton_desc::bone_type::VIRTUAL : xskeleton_desc::bone_type::NORMAL;
            ImGui::PopID();
        }

        ImGui::TableSetColumnIndex(3);
        {
            bool bDelete = bPendingDelete;
            ImGui::PushID(iBone);
            CenterNextCheckbox();
            if (ImGui::Checkbox("##delete", &bDelete))
                for (int i : EditTargets(State, iBone))
                    FindOrCreateBoneOverride(State.m_Descriptor, GetRawBoneName(State, Skeleton, i)).m_bDeleteBone = bDelete;
            ImGui::PopID();
        }

        ImGui::TableSetColumnIndex(4);
        {
            int CurLOD = pOv ? pOv->m_LODLevel : 0;
            ImGui::PushID(iBone);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputInt("##lod", &CurLOD, 1, 1))
            {
                CurLOD = std::max(0, CurLOD);
                for (int i : EditTargets(State, iBone))
                {
                    FindOrCreateBoneOverride(State.m_Descriptor, GetRawBoneName(State, Skeleton, i)).m_LODLevel = CurLOD;
                    ClampDescendantLODs(State, Skeleton, Children, i, CurLOD);
                }
            }
            ImGui::PopID();
        }

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

        // Default IndentSpacing/ItemSpacing eat a lot of width/height per level - a real rig runs
        // 8-10 levels deep (Hip -> Spine -> Clavicle -> Upperarm -> Forearm -> Hand -> fingers), so
        // the default spacing alone can push a chain wider than the panel and taller than the whole
        // hierarchy fits on screen at once.
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 7.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 1.0f));

        constexpr ImGuiTableFlags TableFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("###BoneTable", 5, TableFlags, ImGui::GetContentRegionAvail()))
        {
            ImGui::TableSetupColumn("Bone",    ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Expose",  ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("Virtual", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 36.0f);
            ImGui::TableSetupColumn("Delete",  ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 36.0f);
            ImGui::TableSetupColumn("LOD",     ImGuiTableColumnFlags_WidthFixed, 70.0f);

            // Custom header row (rather than TableHeadersRow()) so the checkbox columns can show an
            // icon glyph instead of a full word, with the word itself moved to a hover tooltip.
            ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
            ImGui::TableSetColumnIndex(0); ImGui::TableHeader("Bone");
            ImGui::TableSetColumnIndex(1); ImGui::TableHeader(g_ExposeIcon);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Expose as Socket");
            ImGui::TableSetColumnIndex(2); ImGui::TableHeader(g_VirtualIcon);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Virtual Bone");
            ImGui::TableSetColumnIndex(3); ImGui::TableHeader(g_DeleteIcon);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete Bone");
            ImGui::TableSetColumnIndex(4); ImGui::TableHeader("LOD");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Level of Detail cutoff - highest LOD index this bone stays active at");

            if (iRoot != -1) RenderBoneNode(State, Skeleton, Children, iRoot);

            ImGui::EndTable();
        }

        ImGui::PopStyleVar(2);
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

        // The bone's own Right/Up come straight out of its world matrix, which can carry non-uniform
        // scale or shear from the source rig (twist/helper joints especially) - using them as-is can
        // collapse the ring toward a line, rendering that bone as a flat 2D sliver instead of a solid
        // wedge. Re-orthogonalize against Dir (Gram-Schmidt) so every bone gets a clean, non-degenerate
        // cross-section regardless of how well-formed its source matrix is.
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
    // Root marker - a small sphere gizmo for any parentless bone. A regular wedge is drawn BETWEEN
    // a bone and its parent, so a root (no parent) never gets one and, before this, rendered as
    // nothing at all. Always solid yellow (not virtual/normal color-coded like a regular wedge -
    // this is a landmark, not a limb segment; virtual state is still visible via the tree's checkbox
    // and text color), using the same fill alpha/depth-tint machinery as a regular wedge's fill.
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
        U = F.Cross(R).Normalize(); // re-orthogonalize, same reasoning as ComputeWedgeShape

        Out = { R, U, F };
        return true;
    }

    xmath::fvec3 SpherePoint(const xmath::fvec3& Center, const sphere_frame& Frame, float Radius, float Theta, float Phi)
    {
        // Phi in [0,pi] sweeps from the +Forward pole to the -Forward pole; Theta in [0,2pi) sweeps
        // around the Forward axis through the Right/Up equatorial plane.
        return Center
             + Frame.m_Forward * (Radius * std::cos(Phi))
             + Frame.m_Right   * (Radius * std::sin(Phi) * std::cos(Theta))
             + Frame.m_Up      * (Radius * std::sin(Phi) * std::sin(Theta));
    }

    // Sized off the distance to the root's nearest child so the marker reads at the same scale as
    // the wedges hanging off it. OverallRadius (the whole skeleton's own bounding radius, already
    // computed in ComputeBoneWorldsAndFraming) sets a scale-appropriate FLOOR for both that and the
    // degenerate no-children case - a fixed absolute floor (what this used to fall back to) reads
    // fine on a 1-2 unit tall rig but is sub-pixel and invisible on rigs authored at a much larger
    // scale (e.g. centimeters, where the whole character's bounding radius is ~100+ units).
    float RootSphereRadius(const xskeleton::skeleton& Skeleton, const std::vector<bone_world>& World, int iRoot, float OverallRadius)
    {
        const auto  Bones = Skeleton.getBones();
        float       MinDist = -1.0f;
        for (int i = 0; i < int(Bones.size()); ++i)
        {
            if (Bones[i].m_iParent != iRoot) continue;
            const float D = (World[i].m_Position - World[iRoot].m_Position).Length();
            if (MinDist < 0.0f || D < MinDist) MinDist = D;
        }
        const float Floor = std::max(OverallRadius * 0.02f, 1.0e-4f);
        return (MinDist > 1.0e-5f) ? std::max(MinDist * 0.18f, Floor) : Floor * 1.5f;
    }

    constexpr int g_RootSphereLonSegs  = 14;
    constexpr int g_RootSphereLatRings = 8;
    constexpr int g_RootSphereWireSegs = 28; // per great circle

    //---------------------------------------------------------------------------
    // Depth tint - CPU-computed per vertex every frame (camera-distance based), lerped toward the
    // background clear color. Selection is depth-invariant: always full brightness.
    //---------------------------------------------------------------------------

    struct wedge_style
    {
        xmath::fvec3    m_CameraPos         {};
        float           m_NearDepth         = 1.0f;
        float           m_FarDepth          = 15.0f;
        std::uint32_t   m_BackgroundColor   = IM_COL32(115, 115, 115, 255); // matches the viewport's own 0.45 gray
        std::uint32_t   m_NormalColor       = IM_COL32(255, 255, 255, 255); // white - now that the fill's alpha/boost are fixed, white reads as bright/clean rather than diluted-into-the-floor
        std::uint32_t   m_VirtualColor      = IM_COL32(255, 180, 84, 255);
        std::uint32_t   m_TwistColor        = IM_COL32(90, 110, 125, 255); // dim, desaturated - a real bone, just not one that should compete visually with the main limb chain
        std::uint32_t   m_SelectedColor     = IM_COL32(255, 40, 180, 255); // hot magenta - a distinct hue from the (now also white-ish) normal color, so selection still reads clearly
        std::uint32_t   m_RootColor         = IM_COL32(255, 220, 40, 255); // yellow - the root sphere's own color, unconditional on virtual/normal
        float           m_HoverBoost        = 2.5f; // multiplicative RGB brighten for whichever bone the mouse is over, regardless of selection/LOD/type color - a no-op for a channel already at 255 (e.g. plain white), which is why the fill ALSO goes fully opaque on hover (see BuildWedgeFillGeometry's TintAt)
    };

    std::uint32_t DepthTint(const xmath::fvec3& P, const xmath::fvec3& CameraPos, float NearD, float FarD, std::uint32_t BaseColor, std::uint32_t BgColor)
    {
        const float D     = (P - CameraPos).Length();
        const float Range = std::max(FarD - NearD, 0.001f);
        // Capped well under 1.0 so depth still reads as a cue, not a fade-to-neutral - at the old
        // 0.72 cap, a bone at typical viewing distance was already 72% blended into the gray
        // background, which is why every base color still looked "neutral" regardless of hue.
        const float T     = std::clamp((D - NearD) / Range, 0.0f, 1.0f) * 0.35f;

        auto Channel = [](std::uint32_t C, int Shift) -> int { return int((C >> Shift) & 0xFFu); };

        const int Br = Channel(BaseColor, 0),  Bg_ = Channel(BaseColor, 8),  Bb = Channel(BaseColor, 16), Ba = Channel(BaseColor, 24);
        const int Gr = Channel(BgColor,   0),  Gg  = Channel(BgColor,   8),  Gb = Channel(BgColor,   16);

        const int R = int(float(Br) + float(Gr - Br) * T);
        const int G = int(float(Bg_) + float(Gg - Bg_) * T);
        const int B = int(float(Bb) + float(Gb - Bb) * T);

        return IM_COL32(R, G, B, Ba);
    }

    // Multiplies just the RGB channels by Factor, clamped to 255, alpha untouched - shared by
    // VertexColor's hover highlight and FaceLit's per-triangle camera-facing shading.
    std::uint32_t ScaleColorRGB(std::uint32_t Color, float Factor)
    {
        auto Channel = [](std::uint32_t C, int Shift) -> int { return int((C >> Shift) & 0xFFu); };
        const int R = std::clamp(int(Channel(Color, 0)  * Factor), 0, 255);
        const int G = std::clamp(int(Channel(Color, 8)  * Factor), 0, 255);
        const int B = std::clamp(int(Channel(Color, 16) * Factor), 0, 255);
        return (Color & 0xFF000000u) | (std::uint32_t(B) << 16) | (std::uint32_t(G) << 8) | std::uint32_t(R);
    }

    // bHovered brightens the FINAL color multiplicatively, after selection/depth-tint have already
    // picked it - "no matter if it is selected or whatever" was the explicit ask, so this has to be
    // the last step, not folded into the selected/normal color choice itself.
    std::uint32_t VertexColor(const wedge_style& Style, const xmath::fvec3& P, bool bSelected, bool bHovered, std::uint32_t BaseColor)
    {
        std::uint32_t C = bSelected ? Style.m_SelectedColor : DepthTint(P, Style.m_CameraPos, Style.m_NearDepth, Style.m_FarDepth, BaseColor, Style.m_BackgroundColor);
        if (bHovered) C = ScaleColorRGB(C, Style.m_HoverBoost);
        return C;
    }

    //---------------------------------------------------------------------------
    // "Color by LOD" view option - one color per LOD tier, cycling if a rig somehow has more LODs
    // than the table. The compiled skeleton doesn't store a per-bone LOD value directly (see
    // xskeleton::skeleton::getLODBoneCounts' own comment): bones are sorted ascending by LOD, and
    // m_pLODBoneCount[L] is the cumulative count of bones active at LOD <= L, so a bone's own LOD is
    // just "the first L whose cumulative count exceeds this bone's index".
    //---------------------------------------------------------------------------

    constexpr std::uint32_t g_LODColors[] =
    { IM_COL32(255,  80,  80, 255)  // LOD0 - red
    , IM_COL32(255, 170,  60, 255)  // LOD1 - orange
    , IM_COL32(255, 230,  60, 255)  // LOD2 - yellow
    , IM_COL32(120, 255,  90, 255)  // LOD3 - green
    , IM_COL32( 80, 200, 255, 255)  // LOD4 - blue
    , IM_COL32(200, 120, 255, 255)  // LOD5 - purple
    };
    constexpr int g_nLODColors = int(sizeof(g_LODColors) / sizeof(g_LODColors[0]));

    int GetBoneLODLevel(const xskeleton::skeleton& Skeleton, int iBone)
    {
        const auto Counts = Skeleton.getLODBoneCounts();
        for (int L = 0; L < int(Counts.size()); ++L)
            if (iBone < int(Counts[L])) return L;
        return 0; // no LOD data at all - everything is LOD0
    }

    std::uint32_t LODColor(int Level)
    {
        return g_LODColors[std::clamp(Level, 0, g_nLODColors - 1)];
    }

    //---------------------------------------------------------------------------
    // Buffer capacity: 8 edges/bone, dashed edges split into 3 emitted sub-segments (6 verts) vs
    // 1 segment (2 verts) when solid - worst case (every bone virtual) is 8*6=48 verts/bone.
    //---------------------------------------------------------------------------

    inline constexpr int   g_MaxWedgeVertices = 24576;

    // Compensates for the wedge fill's own alpha blend diluting brightness against the gray floor
    // (FillAlphaScale ~0.55 means the true color only ever contributes ~55% of what reaches the
    // screen) - see E23_WedgeFill_frag.glsl's own comment. Tunable in one place.
    inline constexpr float g_WedgeFillBoost   = 1.8f;

    void EmitSegment(std::vector<e19::draw_vert>& Verts, const xmath::fvec3& A, const xmath::fvec3& B, std::uint32_t ColorA, std::uint32_t ColorB)
    {
        e19::draw_vert VA{}; VA.m_X = A.m_X; VA.m_Y = A.m_Y; VA.m_Z = A.m_Z; VA.m_U = 0.0f; VA.m_V = 0.0f; VA.m_Color = ColorA;
        e19::draw_vert VB{}; VB.m_X = B.m_X; VB.m_Y = B.m_Y; VB.m_Z = B.m_Z; VB.m_U = 0.0f; VB.m_V = 0.0f; VB.m_Color = ColorB;
        Verts.push_back(VA);
        Verts.push_back(VB);
    }

    // Virtual bones are dashed by only emitting alternating sub-segments along the edge - LINE_LIST
    // has no native dash support, so this approximates it geometrically.
    void EmitEdge(std::vector<e19::draw_vert>& Verts, const xmath::fvec3& A, const xmath::fvec3& B, bool bDashed, const wedge_style& Style, bool bSelected, bool bHovered, std::uint32_t BaseColor)
    {
        auto ColorAt = [&](const xmath::fvec3& P) { return VertexColor(Style, P, bSelected, bHovered, BaseColor); };

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

    // Wireframe: 3 orthogonal great circles rather than a full lat/long wire mesh - reads clearly as
    // "a sphere" for a fraction of the vertex cost.
    void BuildRootSphereWireframe(const xmath::fvec3& Center, const sphere_frame& Frame, float Radius, bool bSelected, bool bHovered, const wedge_style& Style, std::uint32_t Color, std::vector<e19::draw_vert>& Verts)
    {
        auto Circle = [&](const xmath::fvec3& AxisA, const xmath::fvec3& AxisB)
        {
            xmath::fvec3 Prev = Center + AxisA * Radius;
            for (int i = 1; i <= g_RootSphereWireSegs; ++i)
            {
                const float Angle = (2.0f * std::numbers::pi_v<float>) * (float(i) / float(g_RootSphereWireSegs));
                const xmath::fvec3 Cur = Center + AxisA * (Radius * std::cos(Angle)) + AxisB * (Radius * std::sin(Angle));
                EmitEdge(Verts, Prev, Cur, false, Style, bSelected, bHovered, Color);
                Prev = Cur;
            }
        };
        Circle(Frame.m_Right,   Frame.m_Up);
        Circle(Frame.m_Up,      Frame.m_Forward);
        Circle(Frame.m_Forward, Frame.m_Right);
    }

    void BuildWedgeGeometry(const xskeleton::skeleton& Skeleton, const std::vector<bone_world>& World, const std::vector<bool>& IsTwistBone, const wedge_style& Style, const std::set<int>& SelectedBones, float OverallRadius, bool bColorByLOD, int HoveredBone, std::vector<e19::draw_vert>& Verts)
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

            // Dashing is a TYPE-based style choice (virtual placeholder, or a twist helper joint - see
            // LoadSkeleton - that shouldn't visually compete with the main limb chain) and stays the
            // same in both poses. Bind-data confidence (see BuildWedgeFillGeometry's bInferred) is
            // signaled separately, through fill dimness only - dashing it too made bind pose look like
            // a completely different, mostly-broken render style next to frozen's fully solid one, for
            // any rig (like this one) where most bones only have propagated, not real, bind data.
            const bool          bSelected  = SelectedBones.count(i) != 0;
            const bool          bHovered   = i == HoveredBone;
            const bool          bVirtual   = Bones[i].m_Flags.m_bVirtual;
            const bool          bTwist     = i < int(IsTwistBone.size()) && IsTwistBone[i];
            const bool          bDashed    = (bVirtual || bTwist) && !bSelected;
            const std::uint32_t BaseColor  = bColorByLOD ? LODColor(GetBoneLODLevel(Skeleton, i))
                                            : bVirtual ? Style.m_VirtualColor : bTwist ? Style.m_TwistColor : Style.m_NormalColor;

            // BuildWedgeFillGeometry gives every non-virtual, non-twist bone (bFilled here) a solid
            // body already - drawing the full 8-edge wireframe (both tips to all 4 ring points) on
            // TOP of that solid shape just doubled up as visual clutter, since the fill alone already
            // reads clearly as "a tapered bone". The ring alone (its widest cross-section) is enough
            // of an outline/accent on a filled bone. Virtual/twist bones have no fill to lean on, so
            // they keep the full wireframe - it's the only thing conveying their shape at all.
            const bool bFilled = !bVirtual && !bTwist;
            if (bFilled)
            {
                for (int k = 0; k < 4; ++k)
                    EmitEdge(Verts, Shape.m_Ring[k], Shape.m_Ring[(k + 1) & 3], false, Style, bSelected, bHovered, BaseColor);
            }
            else
            {
                for (int k = 0; k < 4; ++k)
                {
                    EmitEdge(Verts, A,               Shape.m_Ring[k], bDashed, Style, bSelected, bHovered, BaseColor);
                    EmitEdge(Verts, Shape.m_Ring[k],  B,               bDashed, Style, bSelected, bHovered, BaseColor);
                }
            }

            if (Verts.size() > std::size_t(g_MaxWedgeVertices - 96))
                break; // stay comfortably under the buffer's capacity
        }

        for (int i = 0; i < int(Bones.size()); ++i)
        {
            if (Bones[i].m_iParent >= 0) continue; // only parentless bones get a marker

            const bool bSelected = SelectedBones.count(i) != 0;
            const bool bHovered  = i == HoveredBone;

            sphere_frame Frame;
            if (!ComputeOrthoFrame(World[i].m_Right, World[i].m_Up, Frame)) continue;

            const std::uint32_t RootColor = bColorByLOD ? LODColor(GetBoneLODLevel(Skeleton, i)) : Style.m_RootColor;
            BuildRootSphereWireframe(World[i].m_Position, Frame, RootSphereRadius(Skeleton, World, i, OverallRadius), bSelected, bHovered, Style, RootColor, Verts);
        }
    }

    //---------------------------------------------------------------------------
    // A very light fill so a wedge registers as a solid shape rather than just a hairline outline -
    // the concept mockup this design comes from always paired the two ("Pass 1 - very light fill...
    // Pass 2 - the outline is the real signal now, not a backstop"), but only the outline pass ever
    // got built here. Virtual/twist bones stay outline-only (a deliberate style choice, unrelated to
    // bind-data confidence). Inferred bones (no real bind data - see BuildWedgeGeometry) still get
    // filled, just dimmer than confident ones - for rigs where most bones lack real bind data, bind
    // pose would otherwise render as an almost-invisible dashed skeleton next to frozen pose's fully
    // solid one; a dimmer fill keeps bind pose reading as a body while still flagging uncertainty.
    //---------------------------------------------------------------------------

    void EmitTri(std::vector<e19::draw_vert>& Verts, const xmath::fvec3& A, const xmath::fvec3& B, const xmath::fvec3& C, std::uint32_t Color)
    {
        e19::draw_vert V{}; V.m_U = 0.0f; V.m_V = 0.0f; V.m_Color = Color;
        V.m_X = A.m_X; V.m_Y = A.m_Y; V.m_Z = A.m_Z; Verts.push_back(V);
        V.m_X = B.m_X; V.m_Y = B.m_Y; V.m_Z = B.m_Z; Verts.push_back(V);
        V.m_X = C.m_X; V.m_Y = C.m_Y; V.m_Z = C.m_Z; Verts.push_back(V);
    }

    // "Headlight" shading baked per-triangle on the CPU: a face angled toward the camera reads
    // brighter, one angled away reads dimmer - gives the otherwise flat-tinted wedge fills a sense
    // of 3D form without needing real normals in e19::draw_vert (shared with other examples, not
    // something to extend just for this). Every EmitTri call here is already one flat face with a
    // single color for all 3 vertices, so computing the face normal from those same 3 points and
    // treating the camera as the light direction is exact, not an approximation.
    std::uint32_t FaceLit(const xmath::fvec3& P0, const xmath::fvec3& P1, const xmath::fvec3& P2, const xmath::fvec3& CameraPos, std::uint32_t Color)
    {
        // NormalizeSafe, not Normalize: a degenerate (zero-area) triangle - e.g. the root sphere's
        // own poles, already noted as zero-area where they're built - has a zero-length cross
        // product, and plain Normalize() asserts on that (xmath_fvec3_inline.h's own documented
        // behavior). NormalizeSafe just yields zero instead, which correctly contributes no lighting
        // rather than crashing.
        xmath::fvec3       Normal = (P1 - P0).Cross(P2 - P0);
        Normal.NormalizeSafe();
        const xmath::fvec3 Center = (P0 + P1 + P2) * (1.0f / 3.0f);
        xmath::fvec3       ViewDir = CameraPos - Center;
        ViewDir.NormalizeSafe();
        const float NdotV = std::max(0.0f, Normal.Dot(ViewDir));

        constexpr float Ambient = 0.55f; // never fully dark on the away-facing side
        const float     Lit     = Ambient + (1.0f - Ambient) * NdotV;
        return ScaleColorRGB(Color, Lit);
    }

    // Per-object (not per-triangle) back-to-front ordering - the standard, good-enough approximation
    // for alpha blending a scene of separate translucent objects without a full triangle sort. Each
    // entry is either a regular wedge (bIsRoot false, iBone/iParent both meaningful) or a root sphere
    // (bIsRoot true, iParent unused).
    struct fill_entry
    {
        int     m_iBone;
        int     m_iParent;
        bool    m_bIsRoot;
        float   m_CameraDist;
    };

    void BuildWedgeFillGeometry(const xskeleton::skeleton& Skeleton, const std::vector<bone_world>& World, const std::vector<bool>& IsTwistBone, const wedge_style& Style, const std::set<int>& SelectedBones, float OverallRadius, bool bColorByLOD, int HoveredBone, std::vector<e19::draw_vert>& Verts)
    {
        Verts.clear();

        const auto Bones = Skeleton.getBones();
        if (World.size() != Bones.size()) return;

        // This, not the base RGB hue, was the real reason the skeleton kept reading as "dim/neutral"
        // through several color changes: at 0.16 alpha, ANY color is 84% gray floor showing through -
        // the base hue barely matters once it's diluted that much.
        constexpr float FillAlphaScale         = 0.55f;
        constexpr float InferredFillAlphaScale = 0.30f; // still visibly dimmer - flagged uncertain bind data - but no longer barely-there

        std::vector<fill_entry> Entries;
        Entries.reserve(Bones.size());
        for (int i = 0; i < int(Bones.size()); ++i)
        {
            const int iParent = Bones[i].m_iParent;
            if (iParent < 0)
            {
                const float Dist = (World[i].m_Position - Style.m_CameraPos).Length();
                Entries.push_back({ i, -1, true, Dist });
                continue;
            }

            if (Bones[i].m_Flags.m_bVirtual) continue; // matches wedges: virtual bones stay outline-only
            if (i < int(IsTwistBone.size()) && IsTwistBone[i]) continue;

            const float Dist = ((World[iParent].m_Position + World[i].m_Position) * 0.5f - Style.m_CameraPos).Length();
            Entries.push_back({ i, iParent, false, Dist });
        }

        // Far first - each entry then draws over whatever's already behind it, the standard way to
        // get correct-looking alpha blending without sorting individual triangles.
        std::sort(Entries.begin(), Entries.end(), [](const fill_entry& A, const fill_entry& B) { return A.m_CameraDist > B.m_CameraDist; });

        for (auto& E : Entries)
        {
            const bool  bSelected = SelectedBones.count(E.m_iBone) != 0;
            const bool  bHovered  = E.m_iBone == HoveredBone;

            if (E.m_bIsRoot)
            {
                sphere_frame Frame;
                if (!ComputeOrthoFrame(World[E.m_iBone].m_Right, World[E.m_iBone].m_Up, Frame)) continue;

                const bool  bInferred = !World[E.m_iBone].m_bRealBindData;
                const float ThisAlpha = bInferred ? InferredFillAlphaScale : FillAlphaScale;
                const std::uint32_t RootBaseColor = bColorByLOD ? LODColor(GetBoneLODLevel(Skeleton, E.m_iBone)) : Style.m_RootColor;
                const auto  TintAt    = [&](const xmath::fvec3& P) -> std::uint32_t
                {
                    const std::uint32_t C = VertexColor(Style, P, bSelected, bHovered, RootBaseColor);
                    // Fully opaque, not just RGB-boosted: a white bone's RGB is already at 255, so
                    // multiplying it (VertexColor's own hover boost) is a no-op - alpha is the only
                    // lever left that can actually make a maxed-out color look brighter once blended
                    // against the floor. Same reasoning selection already gets.
                    if (bSelected || bHovered) return C;
                    const int A8 = int(((C >> 24) & 0xFFu) * ThisAlpha);
                    return (C & 0x00FFFFFFu) | (std::uint32_t(A8) << 24);
                };

                const float Radius = RootSphereRadius(Skeleton, World, E.m_iBone, OverallRadius);
                for (int j = 0; j < g_RootSphereLatRings; ++j)
                {
                    const float Phi0 = std::numbers::pi_v<float> * (float(j)     / float(g_RootSphereLatRings));
                    const float Phi1 = std::numbers::pi_v<float> * (float(j + 1) / float(g_RootSphereLatRings));
                    for (int i = 0; i < g_RootSphereLonSegs; ++i)
                    {
                        const float Theta0 = (2.0f * std::numbers::pi_v<float>) * (float(i)     / float(g_RootSphereLonSegs));
                        const float Theta1 = (2.0f * std::numbers::pi_v<float>) * (float(i + 1) / float(g_RootSphereLonSegs));

                        const xmath::fvec3 P00 = SpherePoint(World[E.m_iBone].m_Position, Frame, Radius, Theta0, Phi0);
                        const xmath::fvec3 P10 = SpherePoint(World[E.m_iBone].m_Position, Frame, Radius, Theta1, Phi0);
                        const xmath::fvec3 P01 = SpherePoint(World[E.m_iBone].m_Position, Frame, Radius, Theta0, Phi1);
                        const xmath::fvec3 P11 = SpherePoint(World[E.m_iBone].m_Position, Frame, Radius, Theta1, Phi1);

                        // Winding order matters now that cull::BACK (the pipeline default) is
                        // actually removing something visible - (P00,P10,P11)/(P00,P11,P01) computes
                        // an INWARD-pointing normal for this Right/Up/Forward parametrization
                        // (verified by hand), so the OUTER hemisphere was the one getting culled and
                        // the inner one showed through instead. Swapped to the outward winding.
                        EmitTri(Verts, P00, P11, P10, FaceLit(P00, P11, P10, Style.m_CameraPos, TintAt(P00))); // degenerate (zero-area) at the poles - harmless
                        EmitTri(Verts, P00, P01, P11, FaceLit(P00, P01, P11, Style.m_CameraPos, TintAt(P00)));
                    }
                }
                continue;
            }

            const xmath::fvec3& A = World[E.m_iParent].m_Position;
            const xmath::fvec3& B = World[E.m_iBone].m_Position;

            wedge_shape Shape;
            if (!ComputeWedgeShape(A, B, World[E.m_iBone].m_Right, World[E.m_iBone].m_Up, Shape)) continue;

            const bool  bInferred = !World[E.m_iBone].m_bRealBindData || !World[E.m_iParent].m_bRealBindData;
            const float ThisAlpha = bInferred ? InferredFillAlphaScale : FillAlphaScale;
            const std::uint32_t WedgeBaseColor = bColorByLOD ? LODColor(GetBoneLODLevel(Skeleton, E.m_iBone)) : Style.m_NormalColor;
            const auto  TintAt    = [&](const xmath::fvec3& P) -> std::uint32_t
            {
                const std::uint32_t C = VertexColor(Style, P, bSelected, bHovered, WedgeBaseColor);
                // Fully opaque, not just RGB-boosted - see the root sphere's identical TintAt for why.
                if (bSelected || bHovered) return C;
                const int A8 = int(((C >> 24) & 0xFFu) * ThisAlpha);
                return (C & 0x00FFFFFFu) | (std::uint32_t(A8) << 24);
            };

            for (int k = 0; k < 4; ++k)
            {
                const xmath::fvec3& R0 = Shape.m_Ring[k];
                const xmath::fvec3& R1 = Shape.m_Ring[(k + 1) & 3];
                EmitTri(Verts, A, R0, R1, FaceLit(A, R0, R1, Style.m_CameraPos, TintAt(A)));
                EmitTri(Verts, B, R1, R0, FaceLit(B, R1, R0, Style.m_CameraPos, TintAt(B)));
            }

            if (Verts.size() > std::size_t(g_MaxWedgeVertices - 24))
                break;
        }
    }

    //---------------------------------------------------------------------------
    // GPU picking geometry - one [Start, Start+Count) slice of Verts per bone (root sphere or
    // wedge, same shapes as the visual passes), used to issue one small Draw per bone with a
    // per-draw BoneID push constant (see the pick pipeline in E23_Example). Deliberately DECOUPLED
    // from BuildWedgeFillGeometry: that one skips virtual/twist bones because they're meant to stay
    // outline-only on screen, but a bone being visually outline-only shouldn't make it unclickable -
    // every bone gets a solid pickable volume here regardless of how it's styled. Color is
    // irrelevant (the pick fragment shader never writes it to the visible framebuffer), so 0 is
    // used throughout.
    //---------------------------------------------------------------------------

    struct pick_range
    {
        int     m_iBone;
        int     m_Start;
        int     m_Count;
    };

    // A click freezes the mouse position and modifier-key state; the actual GPU draw/readback
    // happens over the next few frames (see PickDelayFrames' own comment) using THIS frozen
    // position, not wherever the mouse has drifted to by the time the result is ready.
    struct pick_request
    {
        ImVec2  m_MousePos; // framebuffer-pixel space (already DisplayPos-adjusted), not raw ImGui screen space - see where this is constructed
        bool    m_bCtrl        = false;
        bool    m_bShift       = false;
        bool    m_bDrawIssued  = false;
        int     m_FramesLeft   = 0;
    };

    void BuildPickGeometry(const xskeleton::skeleton& Skeleton, const std::vector<bone_world>& World, float OverallRadius, std::vector<e19::draw_vert>& Verts, std::vector<pick_range>& OutRanges)
    {
        Verts.clear();
        OutRanges.clear();

        const auto Bones = Skeleton.getBones();
        if (World.size() != Bones.size()) return;

        auto EmitRange = [&](int iBone, int Start)
        {
            const int Count = int(Verts.size()) - Start;
            if (Count > 0) OutRanges.push_back({ iBone, Start, Count });
        };

        for (int i = 0; i < int(Bones.size()); ++i)
        {
            const int Start   = int(Verts.size());
            const int iParent = Bones[i].m_iParent;

            if (iParent < 0)
            {
                sphere_frame Frame;
                if (!ComputeOrthoFrame(World[i].m_Right, World[i].m_Up, Frame)) continue;

                const float Radius = RootSphereRadius(Skeleton, World, i, OverallRadius);
                for (int j = 0; j < g_RootSphereLatRings; ++j)
                {
                    const float Phi0 = std::numbers::pi_v<float> * (float(j)     / float(g_RootSphereLatRings));
                    const float Phi1 = std::numbers::pi_v<float> * (float(j + 1) / float(g_RootSphereLatRings));
                    for (int k = 0; k < g_RootSphereLonSegs; ++k)
                    {
                        const float Theta0 = (2.0f * std::numbers::pi_v<float>) * (float(k)     / float(g_RootSphereLonSegs));
                        const float Theta1 = (2.0f * std::numbers::pi_v<float>) * (float(k + 1) / float(g_RootSphereLonSegs));

                        const xmath::fvec3 P00 = SpherePoint(World[i].m_Position, Frame, Radius, Theta0, Phi0);
                        const xmath::fvec3 P10 = SpherePoint(World[i].m_Position, Frame, Radius, Theta1, Phi0);
                        const xmath::fvec3 P01 = SpherePoint(World[i].m_Position, Frame, Radius, Theta0, Phi1);
                        const xmath::fvec3 P11 = SpherePoint(World[i].m_Position, Frame, Radius, Theta1, Phi1);

                        // Same outward-winding fix as BuildWedgeFillGeometry's root sphere - without
                        // it the pick pipeline's own cull::BACK would make the visible hemisphere
                        // unpickable and the hidden one pickable instead.
                        EmitTri(Verts, P00, P11, P10, 0);
                        EmitTri(Verts, P00, P01, P11, 0);
                    }
                }
                EmitRange(i, Start);
                continue;
            }

            const xmath::fvec3& A = World[iParent].m_Position;
            const xmath::fvec3& B = World[i].m_Position;

            wedge_shape Shape;
            if (!ComputeWedgeShape(A, B, World[i].m_Right, World[i].m_Up, Shape)) continue;

            for (int k = 0; k < 4; ++k)
            {
                const xmath::fvec3& R0 = Shape.m_Ring[k];
                const xmath::fvec3& R1 = Shape.m_Ring[(k + 1) & 3];
                EmitTri(Verts, A, R0, R1, 0);
                EmitTri(Verts, B, R1, R0, 0);
            }
            EmitRange(i, Start);

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

    // Generic (Dir need not be unit length - T then matches RayTriangleIntersect's own convention of
    // "same units as Dir", which is all that matters since both are only ever compared against each
    // other for the same ray).
    bool RaySphereIntersect(const xmath::fvec3& Origin, const xmath::fvec3& Dir, const xmath::fvec3& Center, float Radius, float& OutT)
    {
        const xmath::fvec3 OC = Origin - Center;
        const float A = Dir.Dot(Dir);
        if (A < 1.0e-12f) return false;
        const float B = 2.0f * Dir.Dot(OC);
        const float C = OC.Dot(OC) - Radius * Radius;
        const float Disc = B * B - 4.0f * A * C;
        if (Disc < 0.0f) return false;

        const float SqrtDisc = std::sqrt(Disc);
        float T = (-B - SqrtDisc) / (2.0f * A);
        if (T <= 1.0e-6f) T = (-B + SqrtDisc) / (2.0f * A);
        if (T <= 1.0e-6f) return false;

        OutT = T;
        return true;
    }

    void PickWedge(const xskeleton::skeleton& Skeleton, const std::vector<bone_world>& World, const xmath::fvec3& Origin, const xmath::fvec3& Dir, float OverallRadius, int& OutBone, float& OutT)
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

        for (int i = 0; i < int(Bones.size()); ++i)
        {
            if (Bones[i].m_iParent >= 0) continue;

            float T;
            if (RaySphereIntersect(Origin, Dir, World[i].m_Position, RootSphereRadius(Skeleton, World, i, OverallRadius), T) && T < OutT) { OutT = T; OutBone = i; }
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
        float           m_FontSize  = 14.0f;
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
    , const std::vector<bone_world>&  BoneWorld  // caller's - so this agrees with whatever's actually rendered (see ViewScale())
    , int                             HoveredBone
    , const xmath::irect&             Viewport
    , std::vector<label_rect_hit>&    OutHits
    )
    {
        OutHits.clear();

        const auto Bones  = Skeleton.getBones();
        const int  nBones = int(Bones.size());
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
            const bool bSelected = State.m_SelectedBones.count(i) != 0;

            // "Always Show Names" off: decluttering mode - only the current selection (and whatever
            // bone the mouse happens to be over right now) gets a label, so the viewport (and its
            // click-picking) isn't buried under every bone's name at once.
            if (!State.m_bAlwaysShowNames && !bSelected && i != HoveredBone) continue;

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

            const bool bVirtual  = Bones[i].m_Flags.m_bVirtual;

            L.m_Text = GetEffectiveBoneName(State, Skeleton, i);
            if (bSelected)      { L.m_FontSize = 20.0f; L.m_Alpha = 1.00f; L.m_EdgeColor = IM_COL32(255, 255, 255, 255); }
            else if (bVirtual)  { L.m_FontSize = 16.0f; L.m_Alpha = 0.85f; L.m_EdgeColor = IM_COL32(255, 180, 84, 255); }
            else                { L.m_FontSize = 14.0f; L.m_Alpha = 0.55f; L.m_EdgeColor = IM_COL32(120, 150, 170, 255); }

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

        // Top/bottom groups pack along X only - a single row can't spread boxes any further apart
        // once the row itself is full, so a crowded run (e.g. every finger on a hand routed to the
        // same edge) ends up with a shrinking, eventually negative, gap and boxes start overlapping.
        // Wrap into multiple rows instead, each stepping further away from the viewport edge -
        // greedily fill a row in natural (x-sorted) order, start a new one once the next box wouldn't
        // fit, then run the existing PackInBounds *within* each row (now with far fewer items, so it
        // rarely if ever needs to shrink the gap).
        auto PackGroupTD = [&](bool bBottom, float Lo, float Hi, float Gap, float RowGap)
        {
            std::vector<label_item*> Items;
            for (auto& L : Labels) if (!L.m_bLR && L.m_bRightOrBottom == bBottom) Items.push_back(&L);
            if (Items.empty()) return;

            std::sort(Items.begin(), Items.end(), [](label_item* A, label_item* B) { return A->m_AX < B->m_AX; });
            for (auto* It : Items) { It->m_Nat = It->m_AX; It->m_Extent = It->m_BoxW; }

            std::vector<std::vector<label_item*>> Rows;
            std::vector<label_item*>              Current;
            float                                 RowExtent = 0.0f;
            for (auto* It : Items)
            {
                const float WithGap = It->m_Extent + (Current.empty() ? 0.0f : Gap);
                if (!Current.empty() && RowExtent + WithGap > (Hi - Lo))
                {
                    Rows.push_back(std::move(Current));
                    Current.clear();
                    RowExtent = 0.0f;
                }
                Current.push_back(It);
                RowExtent += It->m_Extent + (Current.size() > 1 ? Gap : 0.0f);
            }
            if (!Current.empty()) Rows.push_back(std::move(Current));

            float CumulativeOffset = 0.0f;
            for (auto& Row : Rows)
            {
                PackInBounds(Row, Lo, Hi, Gap);

                float MaxBoxH = 0.0f;
                for (auto* It : Row) MaxBoxH = std::max(MaxBoxH, It->m_BoxH);

                const float RowCenterOffset = CumulativeOffset + MaxBoxH * 0.5f;
                for (auto* It : Row)
                {
                    It->m_AX = It->m_Pos;
                    It->m_AY = bBottom ? (VpY + VpH - MarginY - RowCenterOffset) : (VpY + MarginY + RowCenterOffset);
                }

                CumulativeOffset += MaxBoxH + RowGap;
            }
        };

        PackGroup(true,  false, false, VpY + MarginY, VpY + VpH - MarginY, 6.0f);   // lr, left
        PackGroup(true,  true,  false, VpY + MarginY, VpY + VpH - MarginY, 6.0f);   // lr, right
        PackGroupTD(false, VpX + MarginX, VpX + VpW - MarginX, 10.0f, 4.0f);        // td, top
        PackGroupTD(true,  VpX + MarginX, VpX + VpW - MarginX, 10.0f, 4.0f);        // td, bottom

        // The raw 3D scene used to be drawn straight to the window's swapchain, entirely outside any
        // ImGui window - ImGui had no relationship to it at all, which is why neither
        // GetForegroundDrawList() nor a separate pinned overlay window ever composited correctly (a
        // brand new, unrelated top-level window is exactly what triggered this engine's multi-viewport
        // logic to spawn it as its own OS window). Now that the 3D content itself is drawn from inside
        // a real window (via AddCustomRenderCallback - see the call site), labels just draw into that
        // SAME window's own draw list - the caller must already have it open (ImGui::Begin/End) when
        // calling this function, same as the callback's own draw calls belong to that window's list.
        ImDrawList* pDrawList  = ImGui::GetWindowDrawList();
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
    // Wedge fill pipeline - same wedge shapes, TRIANGLE_LIST instead of LINE_LIST (Primitive3D
    // VertexDescriptor already defaults to it), alpha-blended for the light fill, and its own
    // shaders (E23_WedgeFill_vert/frag.glsl) rather than the outline's - the fragment stage boosts
    // brightness after the blend's own dilution (see that shader's own comment for why).
    //
    xgpu::pipeline          WedgeFillPipeline;
    xgpu::pipeline_instance WedgeFillPipelineInstance;
    {
        xgpu::shader VertexShader;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::VERTEX
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e23::g_WedgeFillVertShader, sizeof(e23::g_WedgeFillVertShader) / sizeof(int)}}
            };
            if (auto Err = Device.Create(VertexShader, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader FragShader;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::FRAGMENT
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e23::g_WedgeFillFragShader, sizeof(e23::g_WedgeFillFragShader) / sizeof(int)}}
            };
            if (auto Err = Device.Create(FragShader, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        auto Samplers = std::array{ xgpu::pipeline::sampler{} };
        auto Shaders  = std::array<const xgpu::shader*, 2>{ &FragShader, &VertexShader };
        auto Setup    = xgpu::pipeline::setup
        { .m_VertexDescriptor   = Primitive3DVertexDescriptor
        , .m_Shaders            = Shaders
        , .m_PushConstantsSize  = sizeof(e23::wedge_fill_push_constants)
        , .m_Samplers           = Samplers
        // Depth-test stays on (still occluded by the opaque grid/outlines), but depth-WRITE is off -
        // paired with BuildWedgeFillGeometry's back-to-front sort, this is what lets one translucent
        // bone's fill blend correctly against another's instead of z-fighting/occluding it.
        , .m_DepthStencil       = { .m_bDepthWriteEnable = false }
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
    // GPU picking pipeline - same position-transform logic as the outline/fill pipelines, but its
    // own vertex shader (E23_Pick_vert.glsl) so its push-constant block matches the fragment
    // shader's exactly (xGPU requires identical push-constant layouts across a pipeline's stages).
    // The fragment shader writes the per-draw BoneID push constant into a dynamic SSBO instead of
    // sampling a texture. Color writes are fully masked off (this pass never has to look right,
    // only BoneID has to land correctly) - default depth test+WRITE stay on so that (a) a bone
    // occluded by real scene geometry (e.g. the grid) never gets picked, and (b) drawing one bone
    // per call, each testing against whatever the previous pick draw already wrote into the depth
    // buffer, resolves "nearest bone wins" when more than one bone's geometry covers the same
    // pixel - regardless of the (arbitrary) order bones are drawn in.
    //
    xgpu::pipeline          PickPipeline;
    xgpu::pipeline_instance PickPipelineInstance;
    {
        xgpu::shader VertexShader;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::VERTEX
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e23::g_PickVertShader, sizeof(e23::g_PickVertShader) / sizeof(int)}}
            };
            if (auto Err = Device.Create(VertexShader, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader FragShader;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::FRAGMENT
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e23::g_PickFragShader, sizeof(e23::g_PickFragShader) / sizeof(int)}}
            };
            if (auto Err = Device.Create(FragShader, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        auto Shaders      = std::array<const xgpu::shader*, 2>{ &FragShader, &VertexShader };
        auto UniformBinds = std::array{ xgpu::pipeline::uniform_binds{.m_BindIndex = 0, .m_Usage = xgpu::shader::type{xgpu::shader::type::bit::FRAGMENT}, .m_Type = xgpu::pipeline::uniform_binds::type::SSBO_DYNAMIC} };
        auto Setup        = xgpu::pipeline::setup
        { .m_VertexDescriptor   = Primitive3DVertexDescriptor
        , .m_Shaders            = Shaders
        , .m_PushConstantsSize  = sizeof(e23::pick_push_constants)
        , .m_UniformBinds       = UniformBinds
        , .m_DepthStencil       = { .m_DepthCompare = xgpu::pipeline::depth_stencil::depth_compare::LESS } // NOT the struct default (LESS_OR_EQUAL) - lets an exactly-tied-depth candidate fail cheaply instead of contesting the pick buffer at all. The actual "closest wins" guarantee comes from PickBuffer.BestKey's atomicMin reduction (see E23_Pick_frag.glsl) - depth test alone only gates whether a candidate is even eligible to try, it doesn't order same-pixel candidates' writes against each other
        , .m_Blend              = { .m_ColorWriteMask = 0 } // never visible - only PickBuffer.PickedID is this pass's real output
        };

        if (auto Err = Device.Create(PickPipeline, Setup); Err)
            return xgpu::getErrorInt(Err);

        auto InstSetup = xgpu::pipeline_instance::setup{ .m_PipeLine = PickPipeline };
        if (auto Err = Device.Create(PickPipelineInstance, InstSetup); Err)
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

    // Also reuses WedgeIndexBuffer - see BuildPickGeometry.
    xgpu::buffer PickVertexBuffer;
    if (auto Err = Device.Create(PickVertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_Usage = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ, .m_EntryByteSize = sizeof(e19::draw_vert), .m_EntryCount = e23::g_MaxWedgeVertices }); Err)
        return xgpu::getErrorInt(Err);

    // GPU->CPU picking result - a single int written by the pick fragment shader (STORAGE type
    // gives it VK_BUFFER_USAGE_STORAGE_BUFFER_BIT so it can be bound as a writable SSBO;
    // CPU_WRITE_GPU_READ gives it host-visible memory so the SAME allocation is readable from the
    // CPU side too via MemoryMap - no separate readback/staging buffer or image copy needed at all).
    // -1 means "nothing picked". Never touched via allocEntry - always bound at its one and only
    // (zero) offset via setDynamicUBO.
    xgpu::buffer PickResultBuffer;
    if (auto Err = Device.Create(PickResultBuffer, { .m_Type = xgpu::buffer::type::STORAGE, .m_Usage = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ, .m_EntryByteSize = sizeof(std::int32_t), .m_EntryCount = 1 }); Err)
        return xgpu::getErrorInt(Err);

    // A second, independent instance of the exact same thing, continuously refreshed every hovered
    // frame (not click-gated) purely to drive the hover label - see its own use below for why this
    // can't just share PickResultBuffer with the click path.
    xgpu::buffer HoverResultBuffer;
    if (auto Err = Device.Create(HoverResultBuffer, { .m_Type = xgpu::buffer::type::STORAGE, .m_Usage = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ, .m_EntryByteSize = sizeof(std::int32_t), .m_EntryCount = 1 }); Err)
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

    // Compile-progress subscriber - the library manager broadcasts every resource's compile state
    // (any type, any selection) through this one delegate; filter to whichever skeleton is currently
    // loaded and mirror its log/result locally, matching E21_StaticGeomEditor's CallBackForCompilation.
    auto CallBackForCompilation = [&](e10::library_mgr&, e10::library::guid, xresource::full_guid gCompilingEntry, std::shared_ptr<e10::compilation::historical_entry::log>& LogInformation)
    {
        if (SkeletonState.m_InfoGUID != gCompilingEntry) return;

        if (SkeletonState.m_Log.get() != LogInformation.get())
            SkeletonState.m_Log = LogInformation;

        e10::compilation::historical_entry::result Result;
        {
            xcontainer::lock::scope lk(*SkeletonState.m_Log);
            Result = SkeletonState.m_Log->get().m_Result;
        }

        if (Result == e10::compilation::historical_entry::result::SUCCESS || Result == e10::compilation::historical_entry::result::SUCCESS_WARNINGS)
        {
            SkeletonState.m_bReload = true;
            SkeletonState.m_bErrors = false;
        }
        else if (Result == e10::compilation::historical_entry::result::FAILURE)
        {
            SkeletonState.m_bErrors = true;
        }
    };
    e10::g_LibMgr.m_OnCompilationState.Register(CallBackForCompilation);

    //
    // Property inspectors - render settings (currently just pose mode) and the selected skeleton
    // descriptor's own properties, both driven by xproperty rather than hardcoded ImGui widgets,
    // matching E21_StaticGeomEditor's "Rendering Settings" / "Static Geom Properties" pattern. Bound
    // once: SkeletonState and its m_Descriptor member never change address, only their contents do
    // (LoadSkeleton overwrites m_Descriptor in place via Serialize), so the inspectors stay valid
    // across skeleton loads with no need to re-bind on every selection change.
    //
    xproperty::inspector InspectorSettings("Rendering Settings");
    xproperty::inspector Inspector("Skeleton Properties");

    for (auto* E : std::array{ &Inspector, &InspectorSettings })
    {
        E->m_Settings.m_ColorVScalar1 = 0.270f * 1.4f;
        E->m_Settings.m_ColorVScalar2 = 0.305f * 1.4f;
        E->m_Settings.m_ColorSScalar  = 0.26f * 1.4f;
    }

    InspectorSettings.AppendEntity();
    InspectorSettings.AppendEntityComponent(*xproperty::getObject(SkeletonState), &SkeletonState);

    Inspector.AppendEntity();
    Inspector.AppendEntityComponent(*SkeletonState.m_Descriptor.getProperties(), &SkeletonState.m_Descriptor);

    //
    // Setup Imgui interface
    //
    xgpu::tools::imgui::CreateInstance(MainWindow);

    // ImGui's context (and therefore its style) only exists after CreateInstance - matches
    // E21_StaticGeomEditor's own property-inspector theming for a consistent look across examples.
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 0.5f;

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
    std::vector<e19::draw_vert>          PickVerts;
    std::vector<e23::pick_range>         PickRanges;
    std::vector<e23::label_rect_hit>     LabelHits;
    std::vector<e23::bone_world>         ScaledBoneWorld; // ActiveBoneWorld() * ViewScale() - see ScaleBoneWorld
    int                                   HoveredBone = -1; // CPU ray test, refreshed every hovered frame - see the label pass below
    int                                   HoverMissStreak = 0; // consecutive "nothing" GPU reads - see its own use below for why this debounces instead of clearing immediately
    std::optional<e23::pick_request>     HoverPick; // same arm/wait/read cycle as PendingPick, but continuously re-armed instead of one-shot - see its own use below for why continuous reset-every-frame was a real CPU/GPU race, not just a staleness cosmetic issue
    bool                                  bLastNormalizeSize = SkeletonState.m_bNormalizeSize; // detects the checkbox flipping - see its use below

    // How many frames to wait after issuing a pick request's one draw before trusting its
    // readback - there's no fence to wait on directly here, so this is the cheap alternative:
    // conservatively longer than the swapchain's actual frames-in-flight count, so by the time
    // this many main-loop iterations have passed, that draw's GPU work is guaranteed done.
    constexpr int                    PickDelayFrames = 4;
    std::optional<e23::pick_request> PendingPick;

    constexpr bool g_bGPUPickingEnabled = true;

    //
    // Main Loop
    //
    while (Instance.ProcessInputEvents())
    {
        if (xgpu::tools::imgui::BeginRendering(true)) continue;

        //
        // Skeleton viewport - a plain, dockable ImGui::Begin(...) window hosting the 3D scene via
        // AddCustomRenderCallback, matching E19/E20's "Mesh Preview"/"Material Instance Preview"
        // panels (no special window flags). The 3D content used to be drawn straight to the window's
        // swapchain outside any ImGui window at all, and every attempt to add a separate, specially-
        // flagged window for just the labels kept opening as its own OS window - once everything (3D
        // content and labels alike) lives inside this one ordinary window, that stops happening.
        // Labels/picking/hover now all relate to this one real window instead.
        //
        if (!SkeletonState.empty())
        {
            if (auto* pSkeleton = xresource::g_Mgr.getResource(SkeletonState.m_Ref); pSkeleton)
            {
                // A docked window's own background flag doesn't help here - when docked in a split
                // alongside another panel, ImGui's dock node itself still paints its own fill behind
                // the window regardless of NoBackground, so the "empty" areas came out inconsistent
                // (see-through only while floating/central). Simpler and consistent either way: stay
                // opaque, but match the app's real background - the 0.45 mid-gray every other example
                // (e.g. E21) gets from the swapchain's own default clear color, since they never wrap
                // their 3D view in an ImGui window at all - rather than ImGui's near-black theme default.
                ImGui::SetNextWindowSize(ImVec2(900, 620), ImGuiCond_FirstUseEver);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::Begin("Skeleton Viewport");
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();

                // Content region (excludes the title bar/border), not the outer window rect - the
                // render bridge sets the actual Vulkan viewport for AddCustomRenderCallback draws from
                // the window's own clip rect (i.e. this same content region), so the camera/label math
                // has to agree with that rect or bones and labels drift apart.
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

                // The "Resize Skeleton to 1m" checkbox has no on-change callback of its own (it's a
                // plain XPROPERTY_DEF member) - noticing the flip here and re-triggering the same
                // auto-frame LoadSkeleton uses is what makes the camera actually zoom to match
                // instead of keeping whatever distance was tuned for the OTHER scale.
                if (SkeletonState.m_bNormalizeSize != bLastNormalizeSize)
                {
                    bLastNormalizeSize      = SkeletonState.m_bNormalizeSize;
                    SkeletonState.m_bNeedsReframe = true;
                }

                // "Resize Skeleton to 1m" - never touches the loaded asset, just what the camera,
                // grid, and geometry-building below treat as this skeleton's radius/center/bone
                // positions for THIS frame. See skeleton_state::ViewScale()'s own comment.
                const float         ViewScale = SkeletonState.ViewScale();
                const float         EffRadius = SkeletonState.m_Radius * ViewScale;
                const xmath::fvec3  EffCenter = SkeletonState.m_Center * ViewScale;
                e23::ScaleBoneWorld(SkeletonState.ActiveBoneWorld(), ViewScale, ScaledBoneWorld);

                if (SkeletonState.m_bNeedsReframe)
                {
                    SkeletonState.m_bNeedsReframe = false;

                    const float VerticalFov = View.getFov().m_Value;
                    const float Aspect      = View.getAspect();
                    const float HFov        = 2.0f * std::atan(Aspect * std::tan(VerticalFov * 0.5f));
                    const float MinFov      = std::min(VerticalFov, HFov);

                    Distance     = EffRadius / std::tan(MinFov * 0.5f);
                    CameraTarget = EffCenter;
                }

                View.LookAt(Distance, Angles, CameraTarget);

                e23::wedge_style Style;
                Style.m_CameraPos = View.getPosition();
                Style.m_NearDepth = std::max(0.01f, Distance * 0.25f);
                Style.m_FarDepth  = Distance * 1.6f + EffRadius;

                // HoveredBone here is last frame's hover result (this frame's hover-pick block runs
                // further down) - a one-frame lag on the highlight, imperceptible and consistent with
                // every other bit of latency this hover mechanism already accepts.
                e23::BuildWedgeGeometry(*pSkeleton, ScaledBoneWorld, SkeletonState.m_bIsTwistBone, Style, SkeletonState.m_SelectedBones, EffRadius, SkeletonState.m_bColorByLOD, HoveredBone, WedgeVerts);
                e23::BuildWedgeFillGeometry(*pSkeleton, ScaledBoneWorld, SkeletonState.m_bIsTwistBone, Style, SkeletonState.m_SelectedBones, EffRadius, SkeletonState.m_bColorByLOD, HoveredBone, WedgeFillVerts);

                if (!WedgeFillVerts.empty())
                {
                    (void)WedgeFillVertexBuffer.MemoryMap(0, static_cast<int>(WedgeFillVerts.size()), [&](void* pData)
                    {
                        std::memcpy(pData, WedgeFillVerts.data(), WedgeFillVerts.size() * sizeof(e19::draw_vert));
                    });
                }
                if (!WedgeVerts.empty())
                {
                    (void)WedgeVertexBuffer.MemoryMap(0, static_cast<int>(WedgeVerts.size()), [&](void* pData)
                    {
                        std::memcpy(pData, WedgeVerts.data(), WedgeVerts.size() * sizeof(e19::draw_vert));
                    });
                }

                //
                // GPU picking - a solid, clickable volume per bone (root sphere or wedge, same
                // shapes as the visual passes) drawn one bone at a time into a 1-ish-pixel scissor
                // rect at PendingPick's frozen mouse position, each writing its own BoneID into
                // PickResultBuffer through the pick fragment shader if it's the closest thing
                // there (hardware depth test/write - see the pipeline's own comment). Click
                // arms PendingPick (see the click handler below); this issues its one draw the
                // frame after that, then just counts down until the readback below is trustworthy.
                //
                // Hover picking - the SAME arm/wait-N-frames/read cycle as the click-request path
                // below (see PendingPick's own comment), just continuously RE-ARMED instead of
                // one-shot, so it keeps tracking the mouse while hovering. This used to reset the
                // buffer to -1 and issue a brand-new draw EVERY frame with no wait at all - since
                // HoverResultBuffer is host-visible memory the GPU is writing via the fragment
                // shader's SSBO write, resetting it from the CPU with no delay could race the
                // in-flight GPU write from a still-executing earlier frame (multiple frames in
                // flight is normal double/triple-buffering, not a bug) - a genuine CPU/GPU memory
                // race, not just "stale but harmless" staleness, and exactly the kind of
                // nondeterministic result that shows up as flicker.
                //
                // Click always wins the mutual exclusion against hover's GPU draw further below
                // (see bDrawHoverPickThisFrameFinal's own comment) - but that check happens much
                // later, after hover's state machine below has already decided to transition from
                // "armed" to "drawn, counting down". Peeking at PendingPick's PRE-transition state
                // here (about to arm == about to draw, since click's arm and its one draw are the
                // same frame) lets hover's transition be skipped entirely on a frame it would lose
                // that exclusion, instead of marking itself as drawn/counting down for a draw that
                // silently never happened - which was reading HoverResultBuffer's unwritten "-1"
                // reset value back as a genuine miss once its delay elapsed, and desyncing hover's
                // hit/miss state from what the mouse was actually over.
                const bool bClickWillDrawThisFrame = g_bGPUPickingEnabled && PendingPick.has_value() && !PendingPick->m_bDrawIssued;

                constexpr int HoverMissStreakToClear = 4;
                bool          bDrawHoverPickThisFrame = false;
                ImVec2        HoverScissorMousePos{};
                if (bViewportHovered && g_bGPUPickingEnabled)
                {
                    if (!HoverPick.has_value())
                    {
                        const ImVec2 ViewportPos = ImGui::GetWindowViewport()->Pos;
                        const ImVec2 RawMousePos = ImGui::GetMousePos();
                        HoverPick = e23::pick_request{ .m_MousePos = ImVec2(RawMousePos.x - ViewportPos.x, RawMousePos.y - ViewportPos.y) };
                    }

                    if (!HoverPick->m_bDrawIssued && !bClickWillDrawThisFrame)
                    {
                        (void)HoverResultBuffer.MemoryMap(0, 1, [&](void* pData)
                        {
                            *static_cast<std::uint32_t*>(pData) = e23::g_PickNoHitKey;
                        });

                        e23::BuildPickGeometry(*pSkeleton, ScaledBoneWorld, EffRadius, PickVerts, PickRanges);
                        if (!PickVerts.empty())
                        {
                            (void)PickVertexBuffer.MemoryMap(0, static_cast<int>(PickVerts.size()), [&](void* pData)
                            {
                                std::memcpy(pData, PickVerts.data(), PickVerts.size() * sizeof(e19::draw_vert));
                            });
                        }

                        HoverPick->m_bDrawIssued = true;
                        HoverPick->m_FramesLeft  = PickDelayFrames;
                        bDrawHoverPickThisFrame  = true;
                        HoverScissorMousePos     = HoverPick->m_MousePos;
                    }
                    else if (HoverPick->m_FramesLeft > 0)
                    {
                        --HoverPick->m_FramesLeft;
                    }
                    else
                    {
                        int ThisRead = -1;
                        (void)HoverResultBuffer.MemoryMap(0, 1, [&](void* pData)
                        {
                            ThisRead = e23::DecodePickKey(*static_cast<std::uint32_t*>(pData));
                        });

                        // PickBuffer.BestKey's atomicMin (see E23_Pick_frag.glsl) makes this read
                        // genuinely correct every single cycle now - no more consecutive-match
                        // confirmation needed before trusting it (that was only ever working around
                        // the pre-atomicMin SSBO write race, not real mouse-position tremor).
                        // HoverMissStreak is a separate, still-wanted debounce: a "-1" read shouldn't
                        // instantly drop the hover label the moment the mouse grazes off a bone's
                        // edge - see HoverMissStreakToClear's own comment.
                        if (ThisRead != -1)
                        {
                            HoveredBone     = ThisRead;
                            HoverMissStreak = 0;
                        }
                        else if (++HoverMissStreak >= HoverMissStreakToClear)
                        {
                            HoveredBone = -1;
                        }

                        HoverPick.reset(); // re-arm next frame at wherever the mouse is by then
                    }
                }
                else if (bViewportHovered)
                {
                    // GPU picking disabled entirely - fall back to the same CPU ray test the click
                    // path itself falls back to: less precise, but consistent with click's own
                    // degraded behavior rather than leaving hover with no answer at all. Synchronous,
                    // no GPU readback involved, so no race and no debounce needed either.
                    const ImVec2       HoverMousePos = ImGui::GetMousePos();
                    const xmath::fvec3 RayOrigin     = View.getPosition();
                    const xmath::fvec3 RayDir        = View.RayFromScreen(HoverMousePos.x, HoverMousePos.y);
                    float HoverT;
                    e23::PickWedge(*pSkeleton, ScaledBoneWorld, RayOrigin, RayDir, EffRadius, HoveredBone, HoverT);
                    HoverMissStreak = 0;
                    HoverPick.reset();
                }
                else
                {
                    HoveredBone     = -1;
                    HoverMissStreak = 0;
                    HoverPick.reset();
                }

                if (g_bGPUPickingEnabled && PendingPick.has_value() && !PendingPick->m_bDrawIssued)
                {
                    (void)PickResultBuffer.MemoryMap(0, 1, [&](void* pData)
                    {
                        *static_cast<std::uint32_t*>(pData) = e23::g_PickNoHitKey;
                    });

                    PendingPick->m_bDrawIssued = true;
                    PendingPick->m_FramesLeft  = PickDelayFrames;
                }
                else if (g_bGPUPickingEnabled && PendingPick.has_value() && PendingPick->m_FramesLeft > 0)
                {
                    --PendingPick->m_FramesLeft;
                }
                else if (g_bGPUPickingEnabled && PendingPick.has_value())
                {
                    int PickedBone = -1;
                    (void)PickResultBuffer.MemoryMap(0, 1, [&](void* pData)
                    {
                        PickedBone = e23::DecodePickKey(*static_cast<std::uint32_t*>(pData));
                    });

                    if (PickedBone != -1) e23::ApplySelection(SkeletonState, PickedBone, PendingPick->m_bCtrl, PendingPick->m_bShift);
                    else                  e23::ClearSelectionOnEmptyClick(SkeletonState, PendingPick->m_bCtrl, PendingPick->m_bShift);
                    PendingPick.reset();
                }

                // Deferred to actual render time (see AddCustomRenderCallback) - capture the small
                // POD values the draw calls need by value, everything else (pipelines/buffers/managers,
                // all alive for the app's whole lifetime) by reference.
                const std::size_t nWedgeFillVerts = WedgeFillVerts.size();
                const std::size_t nWedgeVerts      = WedgeVerts.size();
                const xmath::fmat4 W2C             = View.getW2C();

                // True on exactly one frame per pick request - the one where the block above just
                // issued its draw (m_FramesLeft was freshly set to PickDelayFrames). Captured by
                // value like everything else here since PendingPick keeps changing after this.
                const bool   bDrawPickThisFrame = g_bGPUPickingEnabled && PendingPick.has_value() && PendingPick->m_bDrawIssued && PendingPick->m_FramesLeft == PickDelayFrames;
                const ImVec2 PickScissorMousePos = bDrawPickThisFrame ? PendingPick->m_MousePos : ImVec2{};

                // Both pick passes rely on the depth test to pick "closest wins" (see either pass's
                // own comment) - which only works if each pass gets to resolve it against a depth
                // buffer THAT PASS'S OWN draws haven't already half-written. Running both in the same
                // frame at (usually) the same mouse position meant the second pass's draws could fail
                // depth-test against depth the FIRST pass just wrote for the exact same triangle (an
                // equal, not lesser, depth - rejected under LESS) - the click and hover results would
                // then disagree/flicker depending on which pass happened to run second. Mutually
                // exclusive per frame: on the one frame click is actually drawing, hover just keeps
                // showing its last (still valid, one-frame-stale) result instead of redrawing into
                // the same contested depth.
                const bool bDrawHoverPickThisFrameFinal = bDrawHoverPickThisFrame && !bDrawPickThisFrame;

                xgpu::tools::imgui::AddCustomRenderCallback([&, nWedgeFillVerts, nWedgeVerts, W2C, bDrawPickThisFrame, PickScissorMousePos, bDrawHoverPickThisFrame = bDrawHoverPickThisFrameFinal, HoverScissorMousePos, EffRadius, EffCenter](xgpu::cmd_buffer& CmdBuffer, const ImVec2&, const ImVec2&)
                {
                    //
                    // Ground grid, for spatial context
                    //
                    {
                        CmdBuffer.setPipelineInstance(Grid3dMaterialInstance);
                        grid_push_constants Push;
                        Push.m_WorldSpaceCameraPos = View.getPosition();
                        Push.m_L2W        = xmath::fmat4(xmath::fvec3(100.f, 100.0f, 1.f), xmath::radian3(-90_xdeg, 0_xdeg, 0_xdeg), xmath::fvec3(0, EffCenter.m_Y - EffRadius, 0));
                        Push.m_W2C        = W2C;
                        Push.m_L2CTShadow = g_DisabledShadowL2C;
                        CmdBuffer.setPushConstants(Push);
                        MeshManager.Rendering(CmdBuffer, e19::mesh_manager::model::PLANE3D);
                    }

                    //
                    // Bone wedges - ALL opaque lines first, then the semi-transparent fills back-to-
                    // front (BuildWedgeFillGeometry already sorted them far-to-near) on top. Drawing
                    // fills first (the old order) let a nearer fill get overwritten by a farther one's
                    // blend, and drew every outline over every fill regardless of actual depth - wrong
                    // both ways once more than one translucent bone is on screen at once.
                    //
                    if (nWedgeVerts)
                    {
                        CmdBuffer.setPipelineInstance(WedgeOutlinePipelineInstance);
                        CmdBuffer.setBuffer(WedgeIndexBuffer);
                        CmdBuffer.setBuffer(WedgeVertexBuffer);
                        CmdBuffer.setPushConstants(e23::push_constants{ .m_L2C = W2C });
                        CmdBuffer.Draw(static_cast<int>(nWedgeVerts));
                    }

                    if (nWedgeFillVerts)
                    {
                        CmdBuffer.setPipelineInstance(WedgeFillPipelineInstance);
                        CmdBuffer.setBuffer(WedgeIndexBuffer);
                        CmdBuffer.setBuffer(WedgeFillVertexBuffer);
                        CmdBuffer.setPushConstants(e23::wedge_fill_push_constants{ .m_L2C = W2C, .m_Boost = e23::g_WedgeFillBoost });
                        CmdBuffer.Draw(static_cast<int>(nWedgeFillVerts));
                    }

                    //
                    // GPU picking - one tiny draw per bone, scissored down to the exact mouse pixel
                    // so the fragment shader only ever runs a handful of times regardless of viewport
                    // resolution. The pipeline's depth test/write against whatever the wedge draws
                    // above left in the depth buffer only gates which per-bone draws are even
                    // eligible to contest the pixel - it does NOT itself make "closest wins"
                    // deterministic, since depth-passing is not the same thing as the SSBO write
                    // being ordered. See E23_Pick_frag.glsl's own comment: "closest wins" is actually
                    // provided by PickBuffer.BestKey's atomicMin over a packed (depth, BoneID) key,
                    // which is genuinely order-independent regardless of how many bones pass depth
                    // at this pixel or in what order their draws land.
                    //
                    // Exactly 1x1 pixel, not a small neighborhood: kept for performance (only a
                    // handful of fragment shader invocations regardless of viewport resolution), not
                    // for correctness - the atomicMin key would resolve correctly even across a wider
                    // scissor, since every covered pixel's candidates converge to their own correct
                    // minimum independently.
                    if (bDrawPickThisFrame && !PickRanges.empty())
                    {
                        CmdBuffer.setPipelineInstance(PickPipelineInstance);
                        CmdBuffer.setDynamicUBO(PickResultBuffer, 0);
                        CmdBuffer.setBuffer(WedgeIndexBuffer);
                        CmdBuffer.setBuffer(PickVertexBuffer);
                        CmdBuffer.setScissor(static_cast<int>(PickScissorMousePos.x), static_cast<int>(PickScissorMousePos.y), 1, 1);

                        for (auto& R : PickRanges)
                        {
                            CmdBuffer.setPushConstants(e23::pick_push_constants{ .m_L2C = W2C, .m_BoneID = R.m_iBone });
                            CmdBuffer.Draw(R.m_Count, 0, R.m_Start);
                        }

                        // Restore the full-window scissor - nothing else draws into this window
                        // after this today, but leaving the tiny rect active would be a landmine
                        // for whoever adds the next draw call here.
                        CmdBuffer.setScissor(static_cast<int>(WindowPos.x), static_cast<int>(WindowPos.y), static_cast<int>(WindowSize.x), static_cast<int>(WindowSize.y));
                    }

                    // Same as the click pick draw above, but every hovered frame and into
                    // HoverResultBuffer instead - see HoverScissorMousePos's own comment for why
                    // this can't just reuse the click path's buffer.
                    if (bDrawHoverPickThisFrame && !PickRanges.empty())
                    {
                        CmdBuffer.setPipelineInstance(PickPipelineInstance);
                        CmdBuffer.setDynamicUBO(HoverResultBuffer, 0);
                        CmdBuffer.setBuffer(WedgeIndexBuffer);
                        CmdBuffer.setBuffer(PickVertexBuffer);
                        CmdBuffer.setScissor(static_cast<int>(HoverScissorMousePos.x), static_cast<int>(HoverScissorMousePos.y), 1, 1); // see the click pass's own comment for why 1x1, not a small neighborhood

                        for (auto& R : PickRanges)
                        {
                            CmdBuffer.setPushConstants(e23::pick_push_constants{ .m_L2C = W2C, .m_BoneID = R.m_iBone });
                            CmdBuffer.Draw(R.m_Count, 0, R.m_Start);
                        }

                        CmdBuffer.setScissor(static_cast<int>(WindowPos.x), static_cast<int>(WindowPos.y), static_cast<int>(WindowSize.x), static_cast<int>(WindowSize.y));
                    }
                });

                //
                // Labels + hit rects for this frame's click test - drawn into this same window.
                //
                const xmath::irect Viewport{ static_cast<int>(WindowPos.x), static_cast<int>(WindowPos.y)
                                           , static_cast<int>(WindowPos.x + WindowSize.x), static_cast<int>(WindowPos.y + WindowSize.y) };
                e23::RenderBoneLabelsAndCollectHits(View, *pSkeleton, SkeletonState, ScaledBoneWorld, HoveredBone, Viewport, LabelHits);

                //
                // Viewport click: label rect first (cheap, resolved immediately). Otherwise, with
                // GPU picking on, arms PendingPick - the actual pixel-accurate result lands a few
                // frames later (see PendingPick's own comment) and applies the selection then, using
                // the ctrl/shift state captured here at click time. Falls back to the CPU ray-vs-
                // triangle test only when GPU picking is disabled entirely.
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

                    if (iHitBone != -1)
                    {
                        e23::ApplySelection(SkeletonState, iHitBone, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);
                    }
                    else if (g_bGPUPickingEnabled)
                    {
                        // ImGui::GetMousePos() is in ImGui's global screen space, which only equals
                        // framebuffer-pixel space when DisplayPos is (0,0) - true for the main
                        // viewport, false once this window is docked/floated into any other OS
                        // window. CmdBuffer.setScissor() below needs actual framebuffer pixels (see
                        // xgpu_imgui_breach.cpp's own "clip_off = draw_data->DisplayPos" - that's the
                        // exact same conversion, done there for ImGui's own clip rects), so subtract
                        // this window's own viewport position here, once, rather than at every use.
                        const ImVec2 ViewportPos = ImGui::GetWindowViewport()->Pos;

                        // Replaces whatever pick request was still pending - a new click always
                        // supersedes the intent behind an older, not-yet-resolved one.
                        PendingPick = e23::pick_request{ .m_MousePos = ImVec2(MousePos.x - ViewportPos.x, MousePos.y - ViewportPos.y), .m_bCtrl = ImGui::GetIO().KeyCtrl, .m_bShift = ImGui::GetIO().KeyShift };
                    }
                    else
                    {
                        const xmath::fvec3 RayOrigin = View.getPosition();
                        const xmath::fvec3 RayDir    = View.RayFromScreen(MousePos.x, MousePos.y);
                        float BestT;
                        int   iRayHitBone = -1;
                        e23::PickWedge(*pSkeleton, ScaledBoneWorld, RayOrigin, RayDir, EffRadius, iRayHitBone, BestT);
                        if (iRayHitBone != -1) e23::ApplySelection(SkeletonState, iRayHitBone, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);
                        else                   e23::ClearSelectionOnEmptyClick(SkeletonState, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);
                    }
                }

                ImGui::End();
            }
        }

        // Bone hierarchy used to live in its own "Bones" window; it's now rendered inside the
        // "Skeleton Properties" inspector itself (see Inspector.Show() below), matching
        // E21_StaticGeomEditor's "Scene Hierarchy" CollapsingHeader embedded in its own descriptor
        // property window - one place to see and edit a bone, not a separate panel to hunt for.
        if (false)
        {
            if (!SkeletonState.empty())
            {
                if (auto* pSkeleton = xresource::g_Mgr.getResource(SkeletonState.m_Ref); pSkeleton)
                {
                    ImGui::SetNextWindowSize(ImVec2(280, 420), ImGuiCond_FirstUseEver);
                    if (ImGui::Begin("Bones"))
                    {
                        ImGui::Text("%d bones", static_cast<int>(pSkeleton->getBones().size()));
                        ImGui::Separator();
                        ImGui::BeginChild("###BoneListChild");
                        e23::RenderBoneTree(SkeletonState, *pSkeleton);
                        ImGui::EndChild();
                    }
                    ImGui::End();
                }
            }
        }

        //
        // Main menu bar - Save/Compile/Feedback mirror E21_StaticGeomEditor's so this editor doesn't
        // feel like a different tool. Saving the descriptor is what actually kicks off a recompile
        // (see the m_OnCompilationState subscriber above); this is just the button + status readout.
        //
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("\xEE\x98\xAB Home\xee\xa5\xb2"))
            {
                if (ImGui::MenuItem("Resource Browser", "Ctrl-Space"))
                    AsserBrowser.Show(true);

                ImGui::Separator();
                {
                    const bool bDisableSave = !e10::g_LibMgr.isReadyToSave() && SkeletonState.empty();
                    if (bDisableSave) ImGui::BeginDisabled();
                    if (ImGui::MenuItem("\xEE\x9D\x8E Save ", "Ctrl+S"))
                    {
                        xproperty::settings::context Context;
                        e10::g_LibMgr.Save(Context);
                    }
                    if (bDisableSave) ImGui::EndDisabled();
                }
                ImGui::EndMenu();
            }

            ImGui::SameLine(410);

            if (!SkeletonState.empty())
            {
                xcontainer::lock::scope lk(*SkeletonState.m_Log);
                auto& Log = SkeletonState.m_Log->get();

                bool bDisable = Log.m_Result == e10::compilation::historical_entry::result::COMPILING
                             || Log.m_Result == e10::compilation::historical_entry::result::COMPILING_WARNINGS;

                std::vector<std::string> ValidationErrors;
                if (!bDisable)
                {
                    SkeletonState.m_Descriptor.Validate(ValidationErrors);
                    if (!ValidationErrors.empty()) bDisable = true;
                }

                if (bDisable) ImGui::BeginDisabled();
                if (ImGui::Button("\xEF\x96\xB0 Compile "))
                    SkeletonState.SaveDescriptor();
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
                if (ImGui::Button("Feedback:\xee\xa5\xb2"))
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

        AsserBrowser.Render(e10::g_LibMgr, xresource::g_Mgr);

        if (auto SelAsset = AsserBrowser.getSelectedAsset(); SelAsset.empty() == false && SelAsset.m_Type == xrsc::skeleton_type_guid_v)
        {
            e23::LoadSkeleton(SkeletonState, AsserBrowser.getSelectedLibrary(), SelAsset);
        }

        // A successful recompile means the runtime resource changed under us - reload everything
        // (descriptor, compiled resource, bone worlds) the same way a fresh selection would. clear()
        // (inside LoadSkeleton) would otherwise wipe m_Log back to a blank SUCCESS entry, discarding
        // the very compile output the Feedback popup is about to show - preserve it across the reload.
        if (SkeletonState.m_bReload)
        {
            SkeletonState.m_bReload = false;
            const auto SavedLog = SkeletonState.m_Log;
            e23::LoadSkeleton(SkeletonState, SkeletonState.m_LibraryGUID, SkeletonState.m_InfoGUID);
            SkeletonState.m_Log = SavedLog;
        }

        {
            xproperty::settings::context Context;
            // Show() opens its own ImGui window - both default to roughly the same cascade position
            // on first run with nothing to visually tell them apart, so seed distinct starting spots.
            ImGui::SetNextWindowPos(ImVec2(300, 40), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(320, 300), ImGuiCond_FirstUseEver);
            InspectorSettings.Show(Context, []{});
            ImGui::SetNextWindowPos(ImVec2(300, 360), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(320, 300), ImGuiCond_FirstUseEver);
            Inspector.Show(Context, []{});
        }

        // Bone hierarchy - its own dockable window (previously embedded in "Skeleton Properties")
        // so it can be docked/floated independently of the descriptor inspector.
        if (!SkeletonState.empty())
        {
            if (auto* pSkeleton = xresource::g_Mgr.getResource(SkeletonState.m_Ref); pSkeleton)
            {
                ImGui::SetNextWindowPos(ImVec2(915, 18), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2(357, 420), ImGuiCond_FirstUseEver);
                if (ImGui::Begin("Bone Hierarchy"))
                {
                    ImGui::Text("%d bones", static_cast<int>(pSkeleton->getBones().size()));
                    e23::RenderBoneTree(SkeletonState, *pSkeleton);
                }
                ImGui::End();
            }
        }

        xgpu::tools::imgui::Render();

        MainWindow.PageFlip();

        // Let the resource manager know we have changed the frame
        xresource::g_Mgr.OnEndFrameDelegate();
    }

    xgpu::tools::imgui::Shutdown();

    return 0;
}
