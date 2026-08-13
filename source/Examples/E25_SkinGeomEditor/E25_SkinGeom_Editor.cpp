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
#include <functional>
#include <vector>

#define XRESOURCE_PIPELINE_NO_COMPILER
#include "dependencies/xresource_pipeline_v2/source/xresource_pipeline.h"
#include "source/xstrtool.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_Resources.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_AssetMgr.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_AssetBrowser.h"

#include "plugins/xtexture.plugin/source/xtexture_xgpu_rsc_loader.h"

#include "../E19_MaterialEditor/E19_mesh_manager.h"

// Skeleton: only the compiled-resource loader (.h) is needed - E23_Skeleton_Editor.cpp already
// includes the .cpp once into its own translation unit, and both link into the same executable.
#include "plugins/xskeleton.plugin/source/xskeleton.h"
#include "plugins/xskeleton.plugin/source/xskeleton_xgpu_rsc_loader.h"

// AnimPackage: same reasoning - already defined by E24_AnimPackage_Editor.cpp's own .cpp include.
#include "plugins/xanim_package.plugin/source/xanim_package.h"
#include "plugins/xanim_package.plugin/source/xanim_package_descriptor.h"
#include "plugins/xanim_package.plugin/source/xanim_package_xgpu_rsc_loader.h"

// GeomSkin's runtime header uses xrsc::material_instance_ref (default material list) without
// including its declaration itself - same as xgeom_static.h, which relies on whichever consumer
// pulls this in first. Must come before xgeom_skin.h.
#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_xgpu_rsc_loader.h"

// GeomSkin: nothing else in the executable defines this loader yet, so both the declaration (.h)
// and the definition (.cpp) are included here, same pattern xanim_package used the one time IT was new.
#include "plugins/xgeom_skin.plugin/source/xgeom_skin.h"
#include "plugins/xgeom_skin.plugin/source/xgeom_skin_descriptor.h"
#include "plugins/xgeom_skin.plugin/source/xgeom_skin_details.h"
#include "plugins/xgeom_skin.plugin/source/xgeom_skin_xgpu_rsc_loader.h"
#include "plugins/xgeom_skin.plugin/source/xgeom_skin_xgpu_rsc_loader.cpp"
#include "plugins/xgeom_skin.plugin/source/xgeom_skin_xgpu_runtime.h"

#include "source/tools/xgpu_imgui_timeline.h"
#include "source/tools/editors/xgpu_editor_viewport.h"
#include "source/tools/editors/xgpu_editor_anim_pose.h"
#include "source/tools/editors/xgpu_editor_resource_picker.h"

//-----------------------------------------------------------------------------------
//
// E25 - GeomSkin editor: opens a compiled xgeom_skin asset, resolves the xskeleton it's bound to,
// and (optionally) an xanim_package to preview deformation with. Mirrors E24_AnimPackage_Editor's
// load/inspector/asset-browser shell (descriptor authoring, Compile button, clip/playback transport)
// and E21_StaticGeom_Editor's compressed-cluster mesh rendering pipeline, extended with the skin
// matrix computation the fused position+skin vertex format needs. Proves out the shared editors/
// viewport+pose-eval code (xgpu_editor_viewport.h/xgpu_editor_anim_pose.h) on a third consumer.
//
// v1 scope: single diffuse-sampler material (no per-material texture resolution - a default white
// texture satisfies the sampler slot, matching how E24's own wedge pipeline binds it) and no shadow
// pass. The point of this editor is proving the skinning pipeline deforms correctly, not building a
// second material system - E21 already owns that.
//
//-----------------------------------------------------------------------------------

namespace e25
{
    //---------------------------------------------------------------------------
    // Shaders - the two GeomSkin runtime shaders (compiled project-wide, see CMakeLists' shader
    // lists) plus the same ground grid every preview editor reuses.
    //---------------------------------------------------------------------------

    constexpr static std::uint32_t g_GeomSkinVertShader[] =
    {
        #include "GeomSkinBasicShader_vert.h"
    };
    constexpr static std::uint32_t g_GeomSkinFragShader[] =
    {
        #include "GeomSkinBasicShader_frag.h"
    };
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

    struct alignas(256) grid_uniform
    {
        xmath::fmat4    m_L2W;
        xmath::fmat4    m_W2C;
        xmath::fmat4    m_L2CTShadow;
        xmath::fvec3    m_WorldSpaceCameraPos = xmath::fvec3(0.0f, 10.0f, 0.0f);
        float           m_MajorGridDiv = 10.0f;
    };

    struct alignas(256) ubo_geom_skin_mesh
    {
        xmath::fmat4    m_L2w;
        xmath::fmat4    m_w2C;
        xmath::fmat4    m_w2ShadowT;   // always zero in v1 - no shadow pass, see file header comment
    };

    struct alignas(256) ubo_bm_lighting
    {
        xmath::fvec4    m_LightColor;
        xmath::fvec4    m_AmbientLightColor;
        xmath::fvec4    m_wSpaceLightPos;
        xmath::fvec4    m_wSpaceEyePos;
        xmath::fvec4    m_LightParams;
    };

    struct geom_skin_push_const
    {
        std::uint32_t   m_ClusterIndex;
        std::uint32_t   m_MaxInfluences;
    };

    // Generous fixed upper bound for the bone-matrix SSBO - simplest possible v1 allocation strategy
    // (one buffer sized once at startup, re-uploaded every frame) rather than resizing per-skeleton.
    constexpr int g_MaxBonesSupported = 256;

    xrsc::texture_ref CreateBackgroundTexture(xgpu::device& Device, const xbitmap& Bitmap)
    {
        xrsc::texture_ref Ref;
        Ref.m_Instance = xresource::guid_generator::Instance64();

        auto Texture = std::make_unique<xgpu::texture>();
        if (auto Err = xgpu::tools::bitmap::Create(*Texture, Device, Bitmap); Err)
        {
            assert(false);
            e25::Debugger(xgpu::getErrorMsg(Err));
            std::exit(xgpu::getErrorInt(Err));
        }

        xresource::g_Mgr.RegisterResource(Ref, Texture.release());
        return Ref;
    }

    // What actually drives the skeleton's per-bone world matrices this frame - independent of whether
    // a preview animation happens to be loaded. BIND_POSE always shows the skeleton's rest/bind pose,
    // ignoring any resolved animation entirely. FROZEN_POSE evaluates the resolved clip at its current
    // scrub position but never auto-advances (playback is forced paused; manual scrubbing still
    // works), matching E23_Skeleton_Editor's own FROZEN/BIND naming for the equivalent skeleton-only
    // toggle. ANIMATION_POSE is the existing behavior: plays/advances per the transport bar.
    enum class pose_type : std::uint8_t { BIND_POSE, FROZEN_POSE, ANIMATION_POSE };
    static constexpr auto pose_type_v = std::array
    { xproperty::settings::enum_item("Bind Pose",      pose_type::BIND_POSE)
    , xproperty::settings::enum_item("Frozen Pose",    pose_type::FROZEN_POSE)
    , xproperty::settings::enum_item("Animation Pose", pose_type::ANIMATION_POSE)
    };

    //---------------------------------------------------------------------------
    // Render/preview settings - view-only, not part of the descriptor, not persisted to disk (same
    // "view preference, not asset state" precedent as E23's own render_settings-style toggles).
    // m_PreviewAnimRef gets the built-in xproperty drag-drop resource-ref widget automatically
    // (member_ui<xresource::def_guid<...>>, see dependencies/xproperty/source/examples/imgui/
    // my_property_ui.h) - the same mechanism every descriptor's own resource-ref fields already use,
    // no custom picker code needed.
    //---------------------------------------------------------------------------
    struct render_settings
    {
        xrsc::anim_package  m_PreviewAnimRef = {};
        int                 m_iLOD           = 0;
        int                 m_MaxInfluences  = 4;
        pose_type           m_PoseType       = pose_type::ANIMATION_POSE;

        XPROPERTY_DEF
        ( "RenderSettings", render_settings
        , obj_member<"PreviewAnimRef", &render_settings::m_PreviewAnimRef >
        , obj_member<"LOD",            &render_settings::m_iLOD >
        , obj_member<"MaxInfluences",  &render_settings::m_MaxInfluences >
        , obj_member<"Pose Type",      &render_settings::m_PoseType, member_enum_span<pose_type_v> >
        )
    };
    XPROPERTY_REG(render_settings)

    //---------------------------------------------------------------------------

    struct skin_state
    {
        xrsc::geom_skin                      m_Ref            = {};   // compiled skin mesh resource
        xrsc::skeleton                       m_SkeletonRef    = {};   // compiled skeleton resource, resolved from the descriptor
        e10::library::guid                   m_LibraryGUID    = {};
        xresource::full_guid                 m_InfoGUID       = {};
        std::wstring                         m_DescriptorPath = {};
        xgeom_skin::descriptor               m_Descriptor     = {};
        xgeom_skin::details                  m_Details        = {};

        std::string                          m_ErrorMessage   = {};

        xmath::fvec3                         m_Center         = xmath::fvec3(0.0f, 0.0f, 0.0f);
        float                                m_Radius         = 1.0f;
        bool                                 m_bNeedsReframe  = true;

        // Preview animation actually resolved so far - tracked separately from render_settings'
        // m_PreviewAnimRef so a change can be detected and the playback state reset accordingly.
        // xresource::mgr::getResource(def_guid<T>&) mutates its argument in place on first resolution
        // (swaps the GUID for a cached resolved pointer, m_Instance.m_Pointer = pRSC - see
        // xresource_mgr.h's getResource) - since m_ResolvedAnimRef gets resolved every frame to draw,
        // comparing IT against m_PreviewAnimRef would only ever match on the very first frame. This
        // plain uint64 mirrors m_PreviewAnimRef.m_Instance.m_Value verbatim and is NEVER passed to
        // getResource, so it stays a real GUID value forever - the only thing safe to diff against.
        std::uint64_t                        m_LastPreviewAnimInstance = 0;
        xrsc::anim_package                   m_ResolvedAnimRef = {};
        int                                  m_iSelectedClip   = -1;
        float                                m_TimeSeconds     = 0.0f;
        int                                  m_LoopsElapsed    = 0;
        bool                                 m_bPlaying        = false;
        int                                  m_iSpeedIndex     = xgpu::tools::editors::g_DefaultSpeedIndex;
        xgpu::tools::imgui::timeline::state  m_Timeline        = {};   // scrub widget's own zoom/pan - reset whenever the selected clip changes

        std::vector<xmath::fmat4>            m_PoseWorldMats;   // per-bone world-space pose (rest or animated)
        std::vector<xmath::fmat4>            m_SkinMatrices;    // per-bone World * InvBindPose, uploaded to the BoneMatrixBuffer SSBO

        std::shared_ptr<e10::compilation::historical_entry::log> m_Log = {};
        bool                                  m_bReload        = false;
        bool                                  m_bErrors        = false;

        bool empty() const noexcept { return m_InfoGUID.empty(); }

        void clear()
        {
            m_Descriptor    = {};
            m_Details       = {};
            m_ErrorMessage.clear();
            m_Center        = xmath::fvec3(0.0f, 0.0f, 0.0f);
            m_Radius        = 1.0f;
            m_bNeedsReframe = true;
            m_iSelectedClip = -1;
            m_TimeSeconds   = 0.0f;
            m_LoopsElapsed  = 0;
            m_bPlaying      = false;
            m_PoseWorldMats.clear();
            m_SkinMatrices.clear();
            m_Log           = std::make_shared<e10::compilation::historical_entry::log>(e10::compilation::historical_entry::communication{ .m_Result = e10::compilation::historical_entry::result::SUCCESS });
            m_bReload       = false;
            m_bErrors       = false;
        }

        // Writing the descriptor to disk is what actually kicks off a recompile - this is the
        // "Compile" button's entire action, same as every other editor in this codebase.
        void SaveDescriptor()
        {
            xproperty::settings::context Context;
            if (auto Err = m_Descriptor.Serialize(false, m_DescriptorPath, Context); Err)
                assert(false);
        }
    };

    void GenerateDescriptorPath(skin_state& State, const std::wstring& InfoPath)
    {
        State.m_DescriptorPath = InfoPath;
        if (auto Pos = InfoPath.find(L"info.txt"); Pos != std::wstring::npos)
            State.m_DescriptorPath.replace(Pos, std::wstring_view(L"info.txt").length(), L"Descriptor.txt");
    }

    // Mirrors E24_AnimPackage_Editor::GenerateDetailsLogPath exactly - generic across resource types.
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

    xgeom_skin::details LoadDetails(const std::wstring& DescriptorPath)
    {
        xgeom_skin::details Details;
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

    void LoadSkinGeom(skin_state& State, e10::library::guid LibraryGUID, xresource::full_guid InfoGUID)
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
            // manifest - that part can legitimately fail (skeleton not compiled yet) even though
            // m_SkeletonRef/etc already deserialized fine, so only treat this as fatal if the
            // skeleton reference itself never came through (same convention as xanim_package_desc).
            auto Err = State.m_Descriptor.Serialize(true, State.m_DescriptorPath, Context);
            if (Err && State.m_Descriptor.m_SkeletonRef.empty())
            {
                State.m_ErrorMessage = std::format("Failed to read the GeomSkin descriptor: {}", Err.getMessage());
                return;
            }

            State.m_Details = LoadDetails(State.m_DescriptorPath);
            State.m_Descriptor.MergeWithDetails(State.m_Details);
        }

        State.m_Ref.m_Instance = InfoGUID.m_Instance;
        auto* pGeom = xresource::g_Mgr.getResource(State.m_Ref);
        if (pGeom == nullptr)
        {
            State.m_ErrorMessage = "Failed to load the compiled GeomSkin resource.";
            return;
        }

        if (State.m_Descriptor.m_SkeletonRef.empty())
        {
            State.m_ErrorMessage = "This GeomSkin's descriptor has no Skeleton reference.";
            return;
        }

        State.m_SkeletonRef.m_Instance = State.m_Descriptor.m_SkeletonRef.m_Instance;
        auto* pSkeleton = xresource::g_Mgr.getResource(State.m_SkeletonRef);
        if (pSkeleton == nullptr)
        {
            State.m_ErrorMessage = "Failed to resolve the referenced Skeleton resource.";
            return;
        }

        xgpu::tools::editors::ComputeRestBoneWorlds(*pSkeleton, State.m_PoseWorldMats);

        State.m_Center        = pGeom->m_BBox.getCenter();
        State.m_Radius        = std::max(0.01f, pGeom->m_BBox.getRadius());
        State.m_bNeedsReframe = true;
    }
}

//-----------------------------------------------------------------------------------

int E25_Example()
{
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = true, .m_pLogErrorFunc = e25::Debugger, .m_pLogWarning = e25::Debugger }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err)
        return xgpu::getErrorInt(Err);

    xgpu::window MainWindow;
    if (auto Err = Device.Create(MainWindow, {}); Err)
        return xgpu::getErrorInt(Err);

    xresource::g_Mgr.Initiallize(20000);

    //
    // Default (white, 1x1) texture - satisfies the basic-shader's diffuse sampler slot (see file
    // header comment: no per-material texture resolution in v1).
    //
    xrsc::texture_ref DefaultTextureRef = e25::CreateBackgroundTexture(Device, xbitmap::getDefaultBitmap());
    xgpu::texture*    pDefaultTexture   = xresource::g_Mgr.getResource(DefaultTextureRef);
    if (pDefaultTexture == nullptr)
    {
        assert(false);
        return 1;
    }

    //
    // Ground grid, for spatial context - same E21_GridShader as every other preview editor.
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
    if (auto Err = Device.Create(GridDynamicUBO, { .m_Type = xgpu::buffer::type::UNIFORM, .m_Usage = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ, .m_EntryByteSize = sizeof(e25::grid_uniform), .m_EntryCount = 10 }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::pipeline          Grid3dMaterial;
    xgpu::pipeline_instance Grid3dMaterialInstance;
    {
        xgpu::shader VertexShader;
        {
            xgpu::shader::setup Setup{ .m_Type = xgpu::shader::type::bit::VERTEX, .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e25::g_GridVertShader, sizeof(e25::g_GridVertShader) / sizeof(int)}} };
            if (auto Err = Device.Create(VertexShader, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader FragShader;
        {
            xgpu::shader::setup Setup{ .m_Type = xgpu::shader::type::bit::FRAGMENT, .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e25::g_GridFragShader, sizeof(e25::g_GridFragShader) / sizeof(int)}} };
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
        , .m_Primitive          = { .m_Cull = xgpu::pipeline::primitive::cull::NONE }
        , .m_Blend              = xgpu::pipeline::blend::getAlphaOriginal()
        };

        if (auto Err = Device.Create(Grid3dMaterial, Setup); Err)
            return xgpu::getErrorInt(Err);

        auto Bindings  = std::array{ xgpu::pipeline_instance::sampler_binding{*pDefaultTexture} };
        auto InstSetup = xgpu::pipeline_instance::setup{ .m_PipeLine = Grid3dMaterial, .m_SamplersBindings = Bindings };
        if (auto Err = Device.Create(Grid3dMaterialInstance, InstSetup); Err)
            return xgpu::getErrorInt(Err);
    }

    e19::mesh_manager MeshManager = {};
    MeshManager.Init(Device);

    //
    // GeomSkin mesh pipeline - the actual point of this editor. Vertex descriptor covers 2 streams:
    // stream 0 is the fused position+skin buffer (xgeom_skin::geom::vertex, 12 bytes: 3xint16 pos +
    // 6 packed bytes read back as a uvec4 + a uint16, see xgeom_skin_mb_input_full.vert's own comment
    // on why no 64-bit shader math is needed for that), stream 1 is vertex_extras (12 bytes).
    //
    xgpu::buffer GeomSkinDynamicUBOMesh;
    if (auto Err = Device.Create(GeomSkinDynamicUBOMesh, { .m_Type = xgpu::buffer::type::UNIFORM, .m_Usage = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ, .m_EntryByteSize = sizeof(e25::ubo_geom_skin_mesh), .m_EntryCount = 100 }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::buffer UBOLighting;
    if (auto Err = Device.Create(UBOLighting, { .m_Type = xgpu::buffer::type::UNIFORM, .m_Usage = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ, .m_EntryByteSize = sizeof(e25::ubo_bm_lighting), .m_EntryCount = 100 }); Err)
        return xgpu::getErrorInt(Err);

    // Per-bone skinning matrices (World * InvBindPose) for the CURRENT pose - re-uploaded every frame
    // the viewport renders. Sized once to a generous fixed bone-count ceiling (see g_MaxBonesSupported).
    xgpu::buffer BoneMatrixBuffer;
    if (auto Err = Device.Create(BoneMatrixBuffer, { .m_Type = xgpu::buffer::type::STORAGE, .m_Usage = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ, .m_EntryByteSize = sizeof(xmath::fmat4), .m_EntryCount = e25::g_MaxBonesSupported }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::pipeline GeomSkinPipeline;
    {
        xgpu::vertex_descriptor GeomSkinVertexDescriptor;
        {
            auto Attributes = std::array
            { xgpu::vertex_descriptor::attribute
              { .m_Offset  = offsetof(xgeom_skin::geom::vertex, m_XPos)
              , .m_Format  = xgpu::vertex_descriptor::format::SINT16_3D
              , .m_iStream = 0
              }
            , xgpu::vertex_descriptor::attribute
              { .m_Offset  = offsetof(xgeom_skin::geom::vertex, m_Packed)
              , .m_Format  = xgpu::vertex_descriptor::format::UINT8_4D_UINT       // m_Packed[0..3] -> lo32
              , .m_iStream = 0
              }
            , xgpu::vertex_descriptor::attribute
              { .m_Offset  = offsetof(xgeom_skin::geom::vertex, m_Packed) + 4
              , .m_Format  = xgpu::vertex_descriptor::format::UINT16_1D          // m_Packed[4..5] -> hi16
              , .m_iStream = 0
              }
            , xgpu::vertex_descriptor::attribute
              { .m_Offset  = offsetof(xgeom_skin::geom::vertex_extras, m_UV)
              , .m_Format  = xgpu::vertex_descriptor::format::UINT16_2D
              , .m_iStream = 1
              }
            , xgpu::vertex_descriptor::attribute
              { .m_Offset  = offsetof(xgeom_skin::geom::vertex_extras, m_OctNormal)
              , .m_Format  = xgpu::vertex_descriptor::format::UINT16_2D
              , .m_iStream = 1
              }
            , xgpu::vertex_descriptor::attribute
              { .m_Offset  = offsetof(xgeom_skin::geom::vertex_extras, m_OctTangentX)
              , .m_Format  = xgpu::vertex_descriptor::format::UINT16_1D
              , .m_iStream = 1
              }
            , xgpu::vertex_descriptor::attribute
              { .m_Offset  = offsetof(xgeom_skin::geom::vertex_extras, m_OctTangentY_Sign)
              , .m_Format  = xgpu::vertex_descriptor::format::UINT16_1D
              , .m_iStream = 1
              }
            };
            if (auto Err = Device.Create(GeomSkinVertexDescriptor
            , xgpu::vertex_descriptor::setup
            { .m_bUseStreaming = true
            , .m_Topology      = xgpu::vertex_descriptor::topology::TRIANGLE_LIST
            , .m_VertexSize    = 0
            , .m_Attributes    = Attributes
            }); Err)
            {
                return xgpu::getErrorInt(Err);
            }
        }

        xgpu::shader FragmentShader;
        {
            xgpu::shader::setup Setup{ .m_Type = xgpu::shader::type::bit::FRAGMENT, .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e25::g_GeomSkinFragShader, sizeof(e25::g_GeomSkinFragShader) / sizeof(int)}} };
            if (auto Err = Device.Create(FragmentShader, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader VertexShader;
        {
            xgpu::shader::setup Setup{ .m_Type = xgpu::shader::type::bit::VERTEX, .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e25::g_GeomSkinVertShader, sizeof(e25::g_GeomSkinVertShader) / sizeof(int)}} };
            if (auto Err = Device.Create(VertexShader, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        auto Shaders  = std::array<const xgpu::shader*, 2>{ &FragmentShader, &VertexShader };
        auto Samplers = std::array{ xgpu::pipeline::sampler{} };   // SamplerDiffuseMap
        auto UBuffersUsage = std::array
        { xgpu::pipeline::uniform_binds{ .m_BindIndex = 0, .m_Usage = { .m_bVertex   = true }, .m_Type = xgpu::pipeline::uniform_binds::type::UBO_DYNAMIC }   // MeshUniforms
        , xgpu::pipeline::uniform_binds{ .m_BindIndex = 1, .m_Usage = { .m_bFragment = true }, .m_Type = xgpu::pipeline::uniform_binds::type::UBO_DYNAMIC }   // Lighting
        , xgpu::pipeline::uniform_binds{ .m_BindIndex = 0, .m_Usage = { .m_bVertex   = true }, .m_Type = xgpu::pipeline::uniform_binds::type::SSBO_STATIC }   // ClusterBuffer
        , xgpu::pipeline::uniform_binds{ .m_BindIndex = 1, .m_Usage = { .m_bVertex   = true }, .m_Type = xgpu::pipeline::uniform_binds::type::SSBO_STATIC }   // BoneMatrixBuffer
        };
        auto Setup = xgpu::pipeline::setup
        { .m_VertexDescriptor   = GeomSkinVertexDescriptor
        , .m_Shaders            = Shaders
        , .m_PushConstantsSize  = sizeof(e25::geom_skin_push_const)
        , .m_UniformBinds       = UBuffersUsage
        , .m_Samplers           = Samplers
        };

        if (auto Err = Device.Create(GeomSkinPipeline, Setup); Err)
            return xgpu::getErrorInt(Err);
    }

    xgpu::pipeline_instance GeomSkinPipelineInstance;
    {
        auto Bindings  = std::array{ xgpu::pipeline_instance::sampler_binding{*pDefaultTexture} };
        auto InstSetup = xgpu::pipeline_instance::setup{ .m_PipeLine = GeomSkinPipeline, .m_SamplersBindings = Bindings };
        if (auto Err = Device.Create(GeomSkinPipelineInstance, InstSetup); Err)
            return xgpu::getErrorInt(Err);
    }

    //
    // Asset Mgr
    //
    resource_mgr_user_data ResourceMgrUserData;

    e10::assert_browser  AsserBrowser;
    e25::skin_state       SkinState;
    e25::render_settings  RenderSettings;

    // Compile-progress subscriber - mirrors E23/E24's own CallBackForCompilation exactly.
    auto CallBackForCompilation = [&](e10::library_mgr&, e10::library::guid, xresource::full_guid gCompilingEntry, std::shared_ptr<e10::compilation::historical_entry::log>& LogInformation)
    {
        if (SkinState.m_InfoGUID != gCompilingEntry) return;

        if (SkinState.m_Log.get() != LogInformation.get())
            SkinState.m_Log = LogInformation;

        e10::compilation::historical_entry::result Result;
        {
            xcontainer::lock::scope lk(*SkinState.m_Log);
            Result = SkinState.m_Log->get().m_Result;
        }

        if (Result == e10::compilation::historical_entry::result::SUCCESS || Result == e10::compilation::historical_entry::result::SUCCESS_WARNINGS)
        {
            SkinState.m_bReload = true;
            SkinState.m_bErrors = false;
        }
        else if (Result == e10::compilation::historical_entry::result::FAILURE)
        {
            SkinState.m_bErrors = true;
        }
    };
    e10::g_LibMgr.m_OnCompilationState.Register(CallBackForCompilation);

    //
    // Property inspectors - descriptor (authoring) and render settings (preview), both driven by
    // xproperty rather than hardcoded ImGui widgets, matching every other editor in this codebase.
    // Bound once: SkinState/RenderSettings never change address, only their contents do.
    //
    xproperty::inspector DescriptorInspector("GeomSkin Properties");
    DescriptorInspector.m_Settings.m_ColorVScalar1 = 0.270f * 1.4f;
    DescriptorInspector.m_Settings.m_ColorVScalar2 = 0.305f * 1.4f;
    DescriptorInspector.m_Settings.m_ColorSScalar  = 0.26f * 1.4f;
    DescriptorInspector.AppendEntity();
    DescriptorInspector.AppendEntityComponent(*SkinState.m_Descriptor.getProperties(), &SkinState.m_Descriptor);

    xproperty::inspector RenderSettingsInspector("Rendering Settings");
    RenderSettingsInspector.AppendEntity();
    RenderSettingsInspector.AppendEntityComponent(*xproperty::getObjectByType<e25::render_settings>(), &RenderSettings);

    // Wire up the resource-ref picker delegates (SkeletonRef/MaterialInstance on the descriptor,
    // PreviewAnimRef on render settings). These three are multicast (xdelegate::thread_unsafe, additive
    // Register - not an overridable slot), and xproperty::inspector's own constructor pre-registers a
    // plain default handler on m_OnResourceLeftSize. Without clearing it first, our handler runs
    // *alongside* the default one, so the same field's TreeNodeEx gets submitted twice under the same
    // ImGui ID - producing a visibly duplicated row AND leaving click/drag-drop unreliable (duplicate-ID
    // hover/active-id confusion). E19/E20/E21 all clear before registering; matching that here.
    // Left-column label for a resource-ref field/array-element. Shared by both inspectors: for
    // DescriptorInspector's MaterialInstance array, rewrite the default "[N]" label to
    // "[N] <MaterialName>" using the same positional correlation MergeWithDetails guarantees between
    // m_MaterialDetailsList and m_MaterialInstRefList (see xgeom_skin_descriptor.h), and grey out slots
    // no surviving mesh/node references; every other resource-ref field (SkeletonRef, PreviewAnimRef)
    // just gets the plain framed label. Ported from E21_StaticGeom_Editor.cpp.
    auto OnResourceLeftSize = [](xproperty::inspector& Inspector, void* pID, ImGuiTreeNodeFlags flags, const char* pName, bool& Open)
    {
        std::string NewName;
        bool        bDisable = false;
        if (pName[0] == '[' && strcmp(Inspector.getName(), "GeomSkin Properties") == 0)
        {
            auto Pair  = Inspector.getComponent(0, 0);
            auto View  = std::string_view(pName);
            auto pDesc = static_cast<xgeom_skin::descriptor*>(Pair.second);

            if (not pDesc->m_MaterialDetailsList.empty())
            {
                View = View.substr(1, View.size() - 2);
                int Index;
                auto result = std::from_chars(View.data(), View.data() + View.size(), Index);
                assert(result.ec == std::errc());

                NewName  = std::format("{} {}", pName, pDesc->m_MaterialDetailsList[Index].m_Name);
                pName    = NewName.c_str();
                bDisable = pDesc->m_MaterialDetailsList[Index].m_RefCount <= 0;
            }
        }

        if (bDisable) ImGui::BeginDisabled();
        Open = ImGui::TreeNodeEx(pID, ImGuiTreeNodeFlags_Framed | flags, "  %s", pName);
        if (bDisable) ImGui::EndDisabled();
    };

    for (auto* pInspector : { &DescriptorInspector, &RenderSettingsInspector })
    {
        pInspector->m_OnResourceWigzmos.m_Delegates.clear();
        pInspector->m_OnResourceBrowser.m_Delegates.clear();
        pInspector->m_OnResourceLeftSize.m_Delegates.clear();

        pInspector->m_OnResourceWigzmos.Register<[](xproperty::inspector&, bool& bOpen, const xresource::full_guid& PreFullGuid)
        {
            xgpu::tools::editors::RenderResourceWigzmos(bOpen, PreFullGuid);
        }>();

        pInspector->m_OnResourceBrowser.Register<[](xproperty::inspector&, const void* pUID, bool& bOpen, xresource::full_guid& Out, std::span<const xresource::type_guid> Filters)
        {
            xgpu::tools::editors::ResourceBrowserPopup(pUID, bOpen, Out, Filters);
        }>();

        pInspector->m_OnResourceLeftSize.Register(OnResourceLeftSize);
    }

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

        if (auto I = xstrtool::findI(std::wstring{ szFileName }, { L"xGPU" }); I != std::string::npos)
        {
            I += 4; // Skip the xGPU part
            szFileName[I] = 0;

            TCHAR LIONantProject[] = L"\\example.lionprj";
            for (int i = 0; szFileName[I++] = LIONantProject[i]; ++i);

            if (auto Err = e10::g_LibMgr.OpenProject(szFileName); Err)
            {
                e25::Debugger(Err.getMessage());
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
    xgpu::tools::view  View     = {};
    xmath::radian3     Angles   = {};
    float              Distance = -1;   // let it auto-compute once an asset loads
    xmath::fvec3       CameraTarget(0, 0, 0);
    View.setFov(60_xdeg);

    xgpu::mouse    Mouse;
    xgpu::keyboard Keyboard;
    Instance.Create(Mouse, {});
    Instance.Create(Keyboard, {});

    //
    // Main Loop
    //
    while (Instance.ProcessInputEvents())
    {
        if (xgpu::tools::imgui::BeginRendering(true)) continue;

        //
        // GeomSkin viewport
        //
        if (!SkinState.empty())
        {
            if (auto* pGeom = xresource::g_Mgr.getResource(SkinState.m_Ref); pGeom)
            {
                if (auto* pSkeleton = xresource::g_Mgr.getResource(SkinState.m_SkeletonRef); pSkeleton)
                {
                    const auto Frame = xgpu::tools::editors::BeginViewportWindow("Skin Viewport");
                    const ImVec2 WindowPos        = Frame.m_WindowPos;
                    const ImVec2 WindowSize       = Frame.m_WindowSize;
                    const bool   bViewportHovered = Frame.m_bHovered;

                    if (bViewportHovered)
                        xgpu::tools::editors::HandleOrbitCameraInput(Mouse, View, Angles, Distance, CameraTarget);

                    View.setViewport({ static_cast<int>(WindowPos.x), static_cast<int>(WindowPos.y)
                                     , static_cast<int>(WindowPos.x + WindowSize.x), static_cast<int>(WindowPos.y + WindowSize.y) });

                    if (SkinState.m_bNeedsReframe)
                    {
                        SkinState.m_bNeedsReframe = false;
                        xgpu::tools::editors::ReframeOrbitCamera(View, SkinState.m_Radius, SkinState.m_Center, Distance, CameraTarget);
                    }

                    View.LookAt(Distance, Angles, CameraTarget);

                    //
                    // Resolve the preview animation (tracks RenderSettings.m_PreviewAnimRef, reset
                    // playback whenever it changes).
                    //
                    if (RenderSettings.m_PreviewAnimRef.m_Instance.m_Value != SkinState.m_LastPreviewAnimInstance)
                    {
                        xresource::g_Mgr.ReleaseRef(SkinState.m_ResolvedAnimRef);
                        SkinState.m_ResolvedAnimRef       = RenderSettings.m_PreviewAnimRef;
                        SkinState.m_LastPreviewAnimInstance = RenderSettings.m_PreviewAnimRef.m_Instance.m_Value;
                        SkinState.m_iSelectedClip    = -1;
                        SkinState.m_TimeSeconds      = 0.0f;
                        SkinState.m_LoopsElapsed     = 0;
                        SkinState.m_bPlaying         = !SkinState.m_ResolvedAnimRef.empty();
                        SkinState.m_Timeline         = {};   // fresh zoom/pan for this clip's own duration
                    }

                    auto*      pAnim      = SkinState.m_ResolvedAnimRef.empty() ? nullptr : xresource::g_Mgr.getResource(SkinState.m_ResolvedAnimRef);
                    const bool bAnimValid = pAnim && !pAnim->getClips().empty() && pAnim->m_nBones == pSkeleton->getBones().size();

                    // ANIMATION_POSE is the only mode that actually touches the resolved clip - BIND_POSE
                    // and FROZEN_POSE are both static, animation-independent skeleton visualizations
                    // (they only differ in which formula builds the skin matrices below), matching
                    // E23_Skeleton_Editor's own BIND/FROZEN split.
                    if (RenderSettings.m_PoseType == e25::pose_type::ANIMATION_POSE && bAnimValid)
                    {
                        if (SkinState.m_iSelectedClip < 0 || SkinState.m_iSelectedClip >= int(pAnim->getClips().size()))
                            SkinState.m_iSelectedClip = 0;

                        auto& Clip = pAnim->getClips()[SkinState.m_iSelectedClip];
                        const float ClipLength = (Clip.m_FPS > 0 && Clip.m_nFrames > 0) ? float(Clip.m_nFrames) / float(Clip.m_FPS) : 0.0f;

                        if (SkinState.m_bPlaying && ClipLength > 0.0f)
                        {
                            xgpu::tools::editors::AdvancePlayback
                            ( SkinState.m_TimeSeconds, SkinState.m_LoopsElapsed, SkinState.m_bPlaying
                            , ClipLength, Clip.m_bLoop, ImGui::GetIO().DeltaTime, xgpu::tools::editors::g_PlaybackSpeeds[SkinState.m_iSpeedIndex]
                            );
                        }

                        xgpu::tools::editors::ComputeAnimatedBoneWorlds(*pSkeleton, *pAnim, SkinState.m_iSelectedClip, SkinState.m_TimeSeconds, SkinState.m_PoseWorldMats);

                        if (Clip.m_RootMotionMode != xanim_package::root_motion_mode::NONE)
                        {
                            const auto RootMotion = pAnim->getClipRootMotion(SkinState.m_iSelectedClip);
                            const auto Offset     = xgpu::tools::editors::ComputeRootMotionOffset(Clip, RootMotion, SkinState.m_TimeSeconds, SkinState.m_LoopsElapsed);
                            xgpu::tools::editors::ApplyWorldOffset(SkinState.m_PoseWorldMats, Offset);
                        }
                    }
                    else
                    {
                        // FROZEN_POSE (and BIND_POSE, and ANIMATION_POSE with no valid clip resolved)
                        // all land here - BIND_POSE overrides the skin matrices to Identity below
                        // regardless of what m_PoseWorldMats holds, so computing it is only meaningful
                        // for FROZEN_POSE, but it's cheap and harmless to always compute.
                        xgpu::tools::editors::ComputeRestBoneWorlds(*pSkeleton, SkinState.m_PoseWorldMats);
                    }

                    //
                    // Skin matrices: World * InvBindPose per bone, uploaded to the BoneMatrixBuffer SSBO.
                    // BIND_POSE is a special case: World*InvBindPose only cancels out to Identity (the
                    // mesh exactly as authored, zero deformation) when World is EXACTLY the bone's own
                    // bind-space transform. ComputeRestBoneWorlds instead forward-kinematics the
                    // skeleton's own separately-authored "rest pose" curve, which is a DIFFERENT thing
                    // than the pose the mesh was actually skinned against and can visibly diverge from
                    // it (this skeleton's own rest data isn't a neutral T/A-pose). Bypassing the
                    // World*InvBindPose formula entirely and forcing Identity is what "bind pose"
                    // unambiguously means, regardless of whatever the skeleton's own rest curve holds.
                    //
                    {
                        auto Bones  = pSkeleton->getBones();
                        const int nBones = std::min<int>(static_cast<int>(Bones.size()), e25::g_MaxBonesSupported);
                        SkinState.m_SkinMatrices.resize(Bones.size());
                        if (RenderSettings.m_PoseType == e25::pose_type::BIND_POSE)
                        {
                            std::fill(SkinState.m_SkinMatrices.begin(), SkinState.m_SkinMatrices.end(), xmath::fmat4::fromIdentity());
                        }
                        else
                        {
                            for (int i = 0; i < int(Bones.size()); ++i)
                                SkinState.m_SkinMatrices[i] = SkinState.m_PoseWorldMats[i] * Bones[i].m_InvBindPose;
                        }

                        (void)BoneMatrixBuffer.MemoryMap(0, nBones, [&](void* pData)
                        {
                            std::memcpy(pData, SkinState.m_SkinMatrices.data(), static_cast<std::size_t>(nBones) * sizeof(xmath::fmat4));
                        });
                    }

                    const auto L2w = xmath::fmat4::fromTranslation(-View.getPosition());
                    const auto w2C = View.getW2C() * xmath::fmat4::fromTranslation(View.getPosition());
                    const auto CameraPos = View.getPosition();

                    xgpu::tools::imgui::AddCustomRenderCallback([&, L2w, w2C, CameraPos](xgpu::cmd_buffer& CmdBuffer, const ImVec2&, const ImVec2&)
                    {
                        //
                        // Grid
                        //
                        {
                            CmdBuffer.setPipelineInstance(Grid3dMaterialInstance);

                            auto& Uniform = GridDynamicUBO.allocEntry<e25::grid_uniform>();
                            Uniform.m_WorldSpaceCameraPos = CameraPos;
                            Uniform.m_L2W        = xmath::fmat4(xmath::fvec3(100.f, 100.0f, 1.f), xmath::radian3(-90_xdeg, 0_xdeg, 0_xdeg), xmath::fvec3(0, pGeom->m_BBox.m_Min.m_Y, 0));
                            Uniform.m_W2C        = w2C;
                            Uniform.m_L2CTShadow = xmath::fmat4::fromZero();   // no shadow pass in v1
                            CmdBuffer.setDynamicUBO(GridDynamicUBO, 0);
                            MeshManager.Rendering(CmdBuffer, e19::mesh_manager::model::PLANE3D);
                        }

                        //
                        // Skin mesh
                        //
                        if (pGeom->m_nVertices > 0)
                        {
                            CmdBuffer.setStreamingBuffers({ &pGeom->IndexBuffer(), 3 });

                            auto& MeshUBO = GeomSkinDynamicUBOMesh.allocEntry<e25::ubo_geom_skin_mesh>();
                            MeshUBO.m_L2w       = L2w;
                            MeshUBO.m_w2C       = w2C;
                            MeshUBO.m_w2ShadowT = xmath::fmat4::fromZero();

                            auto& Lighting = UBOLighting.allocEntry<e25::ubo_bm_lighting>();
                            Lighting.m_LightColor        = xmath::fvec4(1) * 4;
                            Lighting.m_AmbientLightColor = xmath::fvec4(1) * 0.7f;
                            Lighting.m_wSpaceLightPos    = xmath::fvec4(0, 0, 0, pGeom->m_BBox.getRadius() * 5);   // co-located with the camera
                            Lighting.m_wSpaceEyePos      = xmath::fvec4(0);
                            Lighting.m_LightParams.m_X   = Lighting.m_wSpaceLightPos.m_W * 0.1f;
                            Lighting.m_LightParams.m_Y   = 6500;   // Temperature
                            Lighting.m_LightParams.m_Z   = 1;      // Intensity boost
                            Lighting.m_LightParams.m_W   = 0;

                            const std::uint32_t MaxInfluences = static_cast<std::uint32_t>(std::clamp(RenderSettings.m_MaxInfluences, 1, 4));

                            std::array StaticSSBO{ &pGeom->ClusterBuffer(), &BoneMatrixBuffer };
                            for (auto& M : pGeom->getMeshes())
                            {
                                const int UseLOD = std::clamp(RenderSettings.m_iLOD, 0, int(M.m_nLODs) - 1);
                                auto&     L      = pGeom->getLODs()[M.m_iLOD + UseLOD];

                                for (auto& S : pGeom->getSubmeshes().subspan(L.m_iSubmesh, L.m_nSubmesh))
                                {
                                    CmdBuffer.setPipelineInstance(GeomSkinPipelineInstance, StaticSSBO);
                                    CmdBuffer.setDynamicUBO(GeomSkinDynamicUBOMesh, 0);
                                    CmdBuffer.setDynamicUBO(UBOLighting, 1);

                                    e25::geom_skin_push_const PushConst{ .m_ClusterIndex = S.m_iCluster, .m_MaxInfluences = MaxInfluences };
                                    for (auto& C : pGeom->getClusters().subspan(S.m_iCluster, S.m_nCluster))
                                    {
                                        CmdBuffer.setPushConstants(PushConst);
                                        CmdBuffer.Draw(C.m_nIndices, C.m_iIndex, C.m_iVertex);
                                        PushConst.m_ClusterIndex++;
                                    }
                                }
                            }
                        }
                    });

                    ImGui::End();
                }
                else
                {
                    ImGui::Begin("Skin Viewport");
                    ImGui::TextDisabled("%s", SkinState.m_ErrorMessage.empty() ? "Failed to resolve the referenced Skeleton resource." : SkinState.m_ErrorMessage.c_str());
                    ImGui::End();
                }
            }
        }

        //
        // Main menu bar - Open/Save/Compile/Feedback mirrors E23/E24 so this editor doesn't feel
        // like a different tool.
        //
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open GeomSkin..."))
                    AsserBrowser.Show(true);

                ImGui::Separator();
                {
                    const bool bDisableSave = !e10::g_LibMgr.isReadyToSave() && SkinState.empty();
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

            if (!SkinState.empty())
            {
                xcontainer::lock::scope lk(*SkinState.m_Log);
                auto& Log = SkinState.m_Log->get();

                bool bDisable = Log.m_Result == e10::compilation::historical_entry::result::COMPILING
                             || Log.m_Result == e10::compilation::historical_entry::result::COMPILING_WARNINGS;

                std::vector<std::string> ValidationErrors;
                if (!bDisable)
                {
                    SkinState.m_Descriptor.Validate(ValidationErrors);
                    if (!ValidationErrors.empty()) bDisable = true;
                }

                if (bDisable) ImGui::BeginDisabled();
                if (ImGui::Button("Compile"))
                    SkinState.SaveDescriptor();
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
                        ImGui::TextUnformatted(Log.m_Log.data(), Log.m_Log.data() + Log.m_Log.size());

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
        // the same way a fresh selection would, matching E23/E24's own m_bReload handling.
        if (SkinState.m_bReload)
        {
            SkinState.m_bReload = false;
            const auto SavedLog = SkinState.m_Log;
            e25::LoadSkinGeom(SkinState, SkinState.m_LibraryGUID, SkinState.m_InfoGUID);
            SkinState.m_Log = SavedLog;
        }

        {
            xproperty::settings::context Context;
            ImGui::SetNextWindowPos(ImVec2(915, 25), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(500, 420), ImGuiCond_FirstUseEver);

            // "Scene Hierarchy" tree - ported from E21_StaticGeom_Editor.cpp verbatim (same node/mesh-
            // grouping data model, inherited unchanged by xgeom_skin_descriptor.h/details.h), letting
            // the raw imported node tree be organized into merge groups / delete entries by right-
            // click, exactly like the static geometry editor. SkinState.m_Details/m_Descriptor are
            // already loaded once at LoadSkinGeom time (unlike E21, which re-reads Details.txt and
            // calls MergeWithDetails every frame) - no per-frame file I/O needed here.
            DescriptorInspector.Show(Context, [&]
            {
                if (SkinState.empty()) return;
                if (SkinState.m_Details.m_RootNode.m_Children.empty() && SkinState.m_Details.m_RootNode.m_MeshList.empty()) return;

                if (ImGui::CollapsingHeader("Scene Hierarchy", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Separator();
                    ImGui::Dummy(ImVec2(0, 12));

                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 7));
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 2));
                    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 12.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));

                    std::function<bool(const xgeom_skin::details::node&)> WorthRendering = [&](const xgeom_skin::details::node& n)
                    {
                        if (n.m_Children.empty() && n.m_MeshList.empty())
                            return false;

                        if (not n.m_Children.empty() && n.m_MeshList.empty())
                        {
                            for (auto& x : n.m_Children)
                                if (WorthRendering(x))
                                    return true;

                            return false;
                        }

                        return true;
                    };

                    constexpr static ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_SpanAvailWidth;
                    auto&                                Desc = SkinState.m_Descriptor;
                    auto                                 DefaultTextColor = ImGui::GetStyle().Colors[ImGuiCol_Text];
                    auto                                 GroupColor   = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
                    auto                                 DeletedColor = ImVec4(0.8f, 0.3f, 0.3f, 1.0f);
                    xgeom_skin::node_path                CurrentNodePath;

                    std::function<void(const xgeom_skin::details::node&, const xgeom_skin::details&, bool, bool)> DisplayNode = [&](const xgeom_skin::details::node& n, const xgeom_skin::details& d, bool bIncluded, bool isDeletedParent)
                    {
                        if (n.m_Children.empty() && n.m_MeshList.empty())
                            return;

                        if (not n.m_Children.empty() && n.m_MeshList.empty())
                        {
                            if (WorthRendering(n) == false) return;
                        }

                        std::size_t prev_len = CurrentNodePath.size();
                        if (!CurrentNodePath.empty()) CurrentNodePath += "/";
                        CurrentNodePath += n.m_Name;

                        const bool isInDeletedList = Desc.isNodeInDeleteList(CurrentNodePath);
                        bool       isDeleted       = isDeletedParent || isInDeletedList;
                        auto       Pair            = Desc.findMergeGroupFromNode(CurrentNodePath);
                        if (isDeleted) ImGui::PushStyleColor(ImGuiCol_Text, DeletedColor);
                        const bool node_open = [&]
                            {
                                if (isInDeletedList)    return ImGui::TreeNodeEx(&n, flags, "\xEE\x9D\x8D (%s) %s", Pair.first ? Pair.first->m_Name.c_str() : "", n.m_Name.c_str());
                                else if (Pair.first)    return ImGui::TreeNodeEx(&n, flags, "\xEE\xAF\x92 (%s) %s", Pair.first->m_Name.c_str(), n.m_Name.c_str());
                                else                    return ImGui::TreeNodeEx(&n, flags, "%s", n.m_Name.c_str());
                            }();
                        if (isDeleted) ImGui::PopStyleColor();

                        {
                            ImGui::PushID(&n);
                            if (ImGui::BeginPopupContextItem("NodeContextMenu"))
                            {
                                ImGui::PushStyleColor(ImGuiCol_Text, DefaultTextColor);
                                if (not bIncluded)
                                {
                                    if (Pair.first)
                                    {
                                        if (ImGui::MenuItem("Remove from Group"))
                                        {
                                            Desc.RemoveNodeFromGroup(*Pair.first, Pair.second, SkinState.m_Details);
                                            Pair.first = nullptr;
                                        }
                                    }
                                    else
                                    {
                                        if (ImGui::MenuItem("Add to New Group"))
                                        {
                                            for (int i = 0; i < 100; i++)
                                            {
                                                std::string NewName = std::format("Group #{}", i);

                                                for (int j = 0; j < int(Desc.m_MergeGroupList.size()); ++j)
                                                {
                                                    if (NewName == Desc.m_MergeGroupList[j].m_Name)
                                                    {
                                                        NewName.clear();
                                                        break;
                                                    }
                                                }

                                                if (not NewName.empty())
                                                {
                                                    Pair.first = &Desc.m_MergeGroupList.emplace_back();
                                                    Pair.first->m_Name = std::move(NewName);

                                                    Desc.AddNodeInGroupList(*Pair.first, CurrentNodePath);
                                                    Pair.second = 0;
                                                    break;
                                                }
                                            }
                                        }

                                        if (not Desc.m_MergeGroupList.empty())
                                        {
                                            if (ImGui::BeginMenu("Add to Merge Group"))
                                            {
                                                for (int i = 0; i < int(Desc.m_MergeGroupList.size()); ++i)
                                                {
                                                    if (ImGui::MenuItem(Desc.m_MergeGroupList[i].m_Name.c_str()))
                                                    {
                                                        Desc.AddNodeInGroupList(Desc.m_MergeGroupList[i], CurrentNodePath);
                                                        Pair.first  = &Desc.m_MergeGroupList[i];
                                                        Pair.second = int(Desc.m_MergeGroupList[i].m_NodePathList.size()) - 1;
                                                        break;
                                                    }
                                                }

                                                ImGui::EndMenu();
                                            }
                                        }
                                    }
                                }

                                if (isDeleted == false)
                                {
                                    if (ImGui::MenuItem("\xEE\x9D\x8D Delete Node"))
                                    {
                                        Desc.AddNodeInDeleteList(CurrentNodePath, SkinState.m_Details);
                                        isDeleted = true;
                                    }
                                }
                                else if (isInDeletedList)
                                {
                                    if (ImGui::MenuItem("\xEE\x9D\x8D UnDelete Node"))
                                    {
                                        Desc.RemoveNodeFromDeleteList(CurrentNodePath, SkinState.m_Details);
                                        isDeleted = true;
                                    }
                                }

                                ImGui::PopStyleColor();
                                ImGui::EndPopup();
                            }
                            ImGui::PopID();
                        }

                        if (node_open)
                        {
                            if (isDeleted) ImGui::PushStyleColor(ImGuiCol_Text, DeletedColor);
                            else if (Pair.first) ImGui::PushStyleColor(ImGuiCol_Text, GroupColor);

                            for (int idx : n.m_MeshList)
                            {
                                ImGuiTreeNodeFlags mesh_flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

                                const auto& m = d.m_MeshList[idx];

                                ImGui::TreeNodeEx((void*)(intptr_t)idx, mesh_flags, "\xEE\xAF\x92 %s", m.m_Name.c_str());

                                if (ImGui::IsItemHovered())
                                {
                                    ImGui::BeginTooltip();
                                    ImGui::PushStyleColor(ImGuiCol_Text, DefaultTextColor);

                                    ImGui::Text("nFaces    : %d\n"
                                        "nUVs      : %d\n"
                                        "nColors   : %d\n"
                                        "nMaterials: %d\n"
                                        , m.m_NumFaces
                                        , m.m_NumUVs
                                        , m.m_NumColors
                                        , int(m.m_MaterialList.size())
                                    );
                                    for (auto& mat : m.m_MaterialList)
                                    {
                                        ImGui::Text("%2d.%s\n", 1 + int(&mat - m.m_MaterialList.data()), d.m_MaterialList[mat].c_str());
                                    }

                                    ImGui::PopStyleColor();
                                    ImGui::EndTooltip();
                                }
                            }
                            for (const auto& child : n.m_Children)
                            {
                                DisplayNode(child, d, !!Pair.first || bIncluded, isDeleted);
                            }
                            ImGui::TreePop();

                            if (isDeleted)  ImGui::PopStyleColor();
                            else if (Pair.first) ImGui::PopStyleColor();
                        }

                        CurrentNodePath.resize(prev_len);
                    };

                    const bool node_open = [&]
                    {
                        if (Desc.m_bMergeAllMeshes) return ImGui::TreeNodeEx(&SkinState.m_Details.m_RootNode, flags, "\xEE\xAF\x92 Root");
                        else                        return ImGui::TreeNodeEx(&SkinState.m_Details.m_RootNode, flags, "Root");
                    }();

                    if (node_open)
                    {
                        if (Desc.m_bMergeAllMeshes) ImGui::PushStyleColor(ImGuiCol_Text, GroupColor);

                        CurrentNodePath = SkinState.m_Details.m_RootNode.m_Name;
                        for (const auto& child : SkinState.m_Details.m_RootNode.m_Children)
                        {
                            DisplayNode(child, SkinState.m_Details, Desc.m_bMergeAllMeshes, false);
                        }

                        ImGui::TreePop();
                        if (Desc.m_bMergeAllMeshes) ImGui::PopStyleColor();
                    }

                    ImGui::PopStyleVar(4);
                }
            });

            ImGui::SetNextWindowPos(ImVec2(915, 460), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(500, 200), ImGuiCond_FirstUseEver);
            RenderSettingsInspector.Show(Context, []{});
        }

        AsserBrowser.Render(e10::g_LibMgr, xresource::g_Mgr);

        // Drives the per-field resource-ref picker popup (xgpu_editor_resource_picker.h's
        // g_AssetBrowserPopup) - ShowAsPopup() only arms it, this is what actually draws it each frame.
        // Missing this call is why clicking a resource-ref button previously did nothing: the popup's
        // "wants to open" state was set, but nothing ever rendered it. Matches E21's own per-frame call.
        xgpu::tools::editors::g_AssetBrowserPopup.RenderAsPopup(e10::g_LibMgr, xresource::g_Mgr);

        if (auto SelAsset = AsserBrowser.getSelectedAsset(); SelAsset.empty() == false && SelAsset.m_Type == xgeom_skin::resource_type_guid_v)
        {
            e25::LoadSkinGeom(SkinState, AsserBrowser.getSelectedLibrary(), SelAsset);
        }

        //
        // Playback transport - Play/Pause/GoToStart/GoToEnd, speed slider, and the shared scrubbable
        // timeline widget, matching E24_AnimPackage_Editor.cpp's transport bar exactly (same shared
        // xgpu_imgui_timeline.h widget + xgpu_editor_anim_pose.h icon/speed constants) rather than the
        // bespoke slider this editor started with.
        //
        if (!SkinState.empty() && !SkinState.m_ResolvedAnimRef.empty())
        {
            if (auto* pAnim = xresource::g_Mgr.getResource(SkinState.m_ResolvedAnimRef); pAnim && !pAnim->getClips().empty())
            {
                ImGui::SetNextWindowPos(ImVec2(915, 665), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2(500, 190), ImGuiCond_FirstUseEver);
                if (ImGui::Begin("Playback"))
                {
                    if (SkinState.m_iSelectedClip < 0 || SkinState.m_iSelectedClip >= int(pAnim->getClips().size()))
                        SkinState.m_iSelectedClip = 0;

                    auto&       Clip       = pAnim->getClips()[SkinState.m_iSelectedClip];
                    const float ClipLength = (Clip.m_FPS > 0 && Clip.m_nFrames > 0) ? float(Clip.m_nFrames) / float(Clip.m_FPS) : 0.0f;

                    if (ImGui::BeginCombo("Clip", std::format("Clip {}", SkinState.m_iSelectedClip).c_str()))
                    {
                        for (int i = 0; i < int(pAnim->getClips().size()); ++i)
                        {
                            const bool bSelected = (i == SkinState.m_iSelectedClip);
                            if (ImGui::Selectable(std::format("Clip {}", i).c_str(), bSelected))
                            {
                                SkinState.m_iSelectedClip = i;
                                SkinState.m_TimeSeconds   = 0.0f;
                                SkinState.m_LoopsElapsed  = 0;
                                SkinState.m_Timeline      = {};   // fresh zoom/pan for this clip's own duration
                            }
                        }
                        ImGui::EndCombo();
                    }

                    // Three real transport buttons (Play/Pause, go-to-start, go-to-end) instead of one
                    // button plus a redundant index - the clip's own label goes in the timeline's
                    // gutter instead (see ClipName below).
                    if (ImGui::Button(SkinState.m_bPlaying ? xgpu::tools::editors::g_PauseIcon : xgpu::tools::editors::g_PlayIcon))
                        SkinState.m_bPlaying = !SkinState.m_bPlaying;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(SkinState.m_bPlaying ? "Pause" : "Play");

                    ImGui::SameLine();
                    if (ImGui::Button(xgpu::tools::editors::g_GoToStartIcon))
                    {
                        SkinState.m_TimeSeconds  = 0.0f;
                        SkinState.m_LoopsElapsed = 0;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Go to start");

                    ImGui::SameLine();
                    if (ImGui::Button(xgpu::tools::editors::g_GoToEndIcon))
                        SkinState.m_TimeSeconds = ClipLength;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Go to end");

                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(140.0f);
                    ImGui::SliderInt("##speed", &SkinState.m_iSpeedIndex, 0, xgpu::tools::editors::g_NumPlaybackSpeeds - 1, xgpu::tools::editors::g_PlaybackSpeedLabels[SkinState.m_iSpeedIndex]);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Playback speed");

                    const char* pClipName = "Clip";   // E25 only ever sees the compiled anim_package
                                                       // (clip names live in ITS descriptor, which this
                                                       // editor doesn't own/load) - a generic label here
                                                       // is honest about what data is actually available.

                    const float FooterHeight      = ImGui::GetTextLineHeightWithSpacing();
                    const float MinTimelineHeight = std::max(ImGui::GetContentRegionAvail().y - FooterHeight, 0.0f);

                    if (xgpu::tools::imgui::timeline::Draw(SkinState.m_Timeline, SkinState.m_TimeSeconds, ClipLength, static_cast<float>(Clip.m_FPS), {}, "playback_timeline", pClipName, MinTimelineHeight))
                        SkinState.m_LoopsElapsed = 0;   // manual scrub - elapsed-loop count no longer means anything

                    const float ZoomPercent = xgpu::tools::imgui::timeline::GetZoomPercent(SkinState.m_Timeline, ClipLength);
                    ImGui::Text("Zoom: %.0f%%    FPS: %d    Frames: %d    Loop: %s", ZoomPercent, Clip.m_FPS, Clip.m_nFrames, Clip.m_bLoop ? "Yes" : "No");
                }
                ImGui::End();
            }
        }

        xgpu::tools::imgui::Render();

        MainWindow.PageFlip();

        xresource::g_Mgr.OnEndFrameDelegate();
    }

    xgpu::tools::imgui::Shutdown();

    return 0;
}
