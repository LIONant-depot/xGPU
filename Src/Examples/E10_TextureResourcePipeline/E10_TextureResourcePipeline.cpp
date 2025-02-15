#include "../E05_Textures/E05_BitmapInspector.h"
#include "../../tools/xgpu_imgui_breach.h"
#include "../../tools/xgpu_xcore_bitmap_helpers.h"
#include "../../dependencies/xtexture.plugin/dependencies/xresource_pipeline_v2/dependencies/xproperty/source/examples/imgui/xPropertyImGuiInspector.h"
#include "../../tools/xgpu_basis_universal_texture_loader.h"
#include "../../dependencies/xprim_geom/src/xprim_geom.h"
#include "../../tools/xgpu_view.h"
#include <format>

// This define forces the pipeline to ignore including the empty functions that the compiler needs to link
#define XRESOURCE_PIPELINE_NO_COMPILER
#include "../../dependencies/xtexture.plugin/dependencies/xresource_pipeline_v2/source/xresource_pipeline.h"
#include "../../dependencies/xtexture.plugin/source/xtexture_rsc_descriptor.h"
#include "imgui_internal.h"

constexpr auto g_VertShader2DSPV = std::array
{
    #include "x64\e10_2d_vert.h"
};
constexpr auto g_FragShader2DSPV = std::array
{
    #include "x64\e10_2d_frag.h"
};

constexpr auto g_VertShader3DSPV = std::array
{
    #include "x64\e10_3d_vert.h"
};
constexpr auto g_FragShader3DSPV = std::array
{
    #include "x64\e10_3d_frag.h"
};

constexpr auto g_VertShader2DCubeSPV = std::array
{
    #include "x64\e10_2d_cube_vert.h"
};
constexpr auto g_FragShader2DCubeSPV = std::array
{
    #include "x64\e10_2d_cube_frag.h"
};

constexpr auto g_VertShader3DCubeSPV = std::array
{
    #include "x64\e10_3d_cube_vert.h"
};
constexpr auto g_FragShader3DCubeSPV = std::array
{
    #include "x64\e10_3d_cube_frag.h"
};



static_assert(xproperty::meta::meets_requirements_v< true, false, std::string> );


namespace e10
{
    //------------------------------------------------------------------------------------------------

    struct vert_2d
    {
        float           m_X, m_Y;
        xcore::vector3d m_UV;           // UV able to deal with cube maps as well...
    };

    //------------------------------------------------------------------------------------------------

    struct vert_3d
    {
        xcore::vector3d m_Position;
        xcore::vector3d m_Binormal;
        xcore::vector3d m_Tangent;
        xcore::vector3d m_Normal;
        xcore::vector2  m_TexCoord;
    };

    //------------------------------------------------------------------------------------------------

    struct push_contants
    {
        float          m_MipLevel       {0};
        float          m_ToGamma        {1};
        xcore::vector2 m_Scale          {1};
        xcore::vector2 m_Translation    {0};
        xcore::vector2 m_UVScale        {1};
        xcore::vector4 m_TintColor      {1};
        xcore::vector4 m_ColorMask      {0};
        xcore::vector4 m_Mode           {0};
        xcore::vector4 m_NormalModes    {0};
        xcore::matrix4 m_L2C;
        xcore::vector3 m_LocalSpaceLightPosition;
        xcore::vector4 m_UVMode;
    };

    //------------------------------------------------------------------------------------------------

    static
    void DebugMessage(std::string_view View)
    {
        printf("%s\n", View.data());
    }

    //------------------------------------------------------------------------------------------------

    struct bitmap_inspector;

    //------------------------------------------------------------------------------------------------

    struct ivec2
    {
        std::uint32_t x, y;
        XPROPERTY_DEF
        ("ivec2", ivec2, xproperty::settings::vector2_group
            , obj_member_ro<"W", &ivec2::x >
            , obj_member_ro<"H", &ivec2::y >
        )
    };
    XPROPERTY_REG(ivec2)
}

//------------------------------------------------------------------------------------------------

#include "E10_Compiler.h"

//------------------------------------------------------------------------------------------------
// Give properties for xcore::vector2 and xcore::vector3
//------------------------------------------------------------------------------------------------
struct vec2_friend : xcore::vector2
{
    XPROPERTY_DEF
    ("vector2", xcore::vector2, xproperty::settings::vector2_group
    , obj_member<"X", &xcore::vector2::m_X >
    , obj_member<"Y", &xcore::vector2::m_Y >
    )
};
XPROPERTY_REG(vec2_friend)

//------------------------------------------------------------------------------------------------

struct vec3_friend : xcore::vector3
{
    XPROPERTY_DEF
    ("vector3", xcore::vector3, xproperty::settings::vector3_group
    , obj_member<"X", &xcore::vector3::m_X >
    , obj_member<"Y", &xcore::vector3::m_Y >
    , obj_member<"Z", &xcore::vector3::m_Z >
    )
};
XPROPERTY_REG(vec3_friend)

//------------------------------------------------------------------------------------------------

struct draw_options
{
    enum class render_mode
    { RENDER_2D
    , RENDER_3D
    , RENDER_3D_WITH_LIGHTING
    };

    static constexpr auto render_modes_v = std::array
    { xproperty::settings::enum_item("2D",   render_mode::RENDER_2D,  "Renders the texture in a 2D plane. Good for close examination of a general texture.")
    , xproperty::settings::enum_item("3D",   render_mode::RENDER_3D,  "Renders using a 3d cube to see the textures in different angles. Good to check any mipmap issues")
    , xproperty::settings::enum_item("3D_LIGHTING",   render_mode::RENDER_3D_WITH_LIGHTING,  "Renders using a 3d cube assuming that the texture is a normal map. "
                                                                                              "It will render using a diffuse lighting. This method is good for checking "
                                                                                              "the quality of your normal maps.\nNOTE: You can freeze and unfreeze the light "
                                                                                              "direction by pressing the space bar.")
    };

    enum class channels_mode
    { COLOR_ALPHA
    , NO_ALPHA
    , A_ONLY
    , R_ONLY
    , G_ONLY
    , B_ONLY
    };

    static constexpr auto channels_mode_v = std::array
    { xproperty::settings::enum_item("COLOR + ALPHA",   channels_mode::COLOR_ALPHA,  "Render the image normally using all the channels available")
    , xproperty::settings::enum_item("NO_ALPHA",        channels_mode::NO_ALPHA,     "Render the image normally using all available channels except Alpha. Alpha will be set to opaque.")
    , xproperty::settings::enum_item("A_ONLY",          channels_mode::A_ONLY,       "Renders only the alpha channel as a single color without any alpha")
    , xproperty::settings::enum_item("R_ONLY",          channels_mode::R_ONLY,       "Renders only the red channel as a single color without any alpha")
    , xproperty::settings::enum_item("G_ONLY",          channels_mode::G_ONLY,       "Renders only the green channel as a single color without any alpha")
    , xproperty::settings::enum_item("B_ONLY",          channels_mode::B_ONLY,       "Renders only the blue channel as a single color without any alpha")
    };

    enum class display_gamma_mode
    { GAMMA
    , LINEAR
    , RAW_DATA_INFILE
    };

    static constexpr auto display_gamma_mode_v = std::array
    { xproperty::settings::enum_item("GAMMA",           display_gamma_mode::GAMMA,              "Normal rendering. This is how games and other applications render their images.")
    , xproperty::settings::enum_item("LINEAR",          display_gamma_mode::LINEAR,             "This is how the shader is receiving the image so that it can operate...")
    , xproperty::settings::enum_item("RAW_DATA_INFILE", display_gamma_mode::RAW_DATA_INFILE,    "This is the raw data in the file. Useful to see the kind of precision of the data.")
    };

    render_mode         m_RenderMode            = render_mode::RENDER_2D;  
    float               m_UVScale               = 1;
    float               m_BackgroundIntensity   = 0.16f;
    channels_mode       m_ChannelsMode          = channels_mode::COLOR_ALPHA;
    bool                m_bBilinearMode         = false;
    int                 m_ChooseMipLevel        = -1;
    int                 m_MaxMipLevels          = 0;
    display_gamma_mode  m_DisplayInGammaMode    = display_gamma_mode::GAMMA;
    float               m_DisplayGamma          = 2.2f;

    XPROPERTY_DEF
    ("Render", draw_options
    , obj_member
        < "Render Mode"
        , &draw_options::m_RenderMode
        , member_enum_span<render_modes_v>
        , member_help<"Changes between 2D and 3D views for your texture."
        >>

    , obj_member
        < "Bilinear"
        , &draw_options::m_bBilinearMode
        , member_help<"Render the texture with bilinear filtering or nearest"
        >>
    , obj_member
        < "UVScale"
        , &draw_options::m_UVScale
        , member_ui<float>::scroll_bar<1, 10>
        , member_help<"Scales the UV of the image"
        >>
    , obj_member
        < "Channels Mode"
        , &draw_options::m_ChannelsMode
        , member_enum_span<channels_mode_v>
        , member_help<"Renders the image following one of the rules selected"
        >>
    , obj_member
        < "Background Intensity"
        , &draw_options::m_BackgroundIntensity
        , member_ui<float>::scroll_bar<0, 3>
        , member_help<"Changes the intensity of the background"
        >>
    , obj_member
        < "ChooseMip"
        , +[](draw_options& O, bool bRead, int& Value)
        {
            if (bRead) Value = O.m_ChooseMipLevel;
            else       O.m_ChooseMipLevel = std::min( O.m_MaxMipLevels, Value);
        }
        , member_ui<int>::scroll_bar<-1, 20>
        , member_help<"Selects a particular mip to render the texture. If you set the value to -1 "
                      "then it will go back to using the texture with all the mips (trilinear when bilinear is enable)"
        >>
    , obj_member
        < "Display Mode"
        , &draw_options::m_DisplayInGammaMode
        , member_enum_span<display_gamma_mode_v>
        , member_help<"This mode shows the image in gamma (This is the normal mode, also how all the displays works), "
                      "or it can show the image in LINEAR which is how the shader receives the image."
        >>
    , obj_member
        < "Display Gamma"
        , &draw_options::m_DisplayGamma
        , member_ui<float>::scroll_bar<1, 4>
        , member_dynamic_flags < +[](const draw_options& O)
        {
            xproperty::flags::type Flags{};
            Flags.m_bDontShow = O.m_DisplayInGammaMode != display_gamma_mode::GAMMA;
            return Flags;
        } >
        , member_help<"Changes the display gamma for the image"
        >>
    )
};
XPROPERTY_REG(draw_options)

//------------------------------------------------------------------------------------------------

struct draw_controls
{
    draw_options*       m_pOptions                  = nullptr;

    float               m_MainWindowWidth           = {};
    float               m_MainWindowHeight          = {};

    float               m_2DMouseScale              = 1;
    xcore::vector2      m_2DMouseTranslate          = { 0.0f, 0.0f };

    xgpu::tools::view   m_3DView                    = {};
    xcore::vector3      m_3DLightPosition           = {};
    xcore::radian3      m_3DAngles                  = {};
    float               m_3DDistance                = 2;
    bool                m_3DFollowCamera            = true;

    draw_controls() = default;
    draw_controls(draw_options& O) : m_pOptions(&O)
    {
        m_3DView.setFov(60_xdeg);
        m_3DView.setPosition({ 0,0,m_3DDistance });
    }

    constexpr static xproperty::flags::type filter_2d(const draw_controls& O)
    {
        xproperty::flags::type Flags{};
        Flags.m_bDontShow = O.m_pOptions->m_RenderMode != draw_options::render_mode::RENDER_2D;
        return Flags;
    }

    constexpr static xproperty::flags::type filter_3d(const draw_controls& O)
    {
        xproperty::flags::type Flags{};
        Flags.m_bDontShow = O.m_pOptions->m_RenderMode == draw_options::render_mode::RENDER_2D;
        return Flags;
    }

    constexpr static xproperty::flags::type filter_3d_wl(const draw_controls& O)
    {
        xproperty::flags::type Flags{};
        Flags.m_bDontShow = O.m_pOptions->m_RenderMode != draw_options::render_mode::RENDER_3D_WITH_LIGHTING;
        return Flags;
    }

    XPROPERTY_DEF
    ( "Controls"
    , draw_controls
    , obj_member
        < "Image Location"
        , &draw_controls::m_2DMouseTranslate
        , member_dynamic_flags<filter_2d>
        , member_help<"The location of the texture"
        >>
    , obj_member_ro
        < "Camera Location"
        , +[](draw_controls& O) -> xcore::vector3& { static xcore::vector3 pos; pos = O.m_3DView.getPosition(); return pos; }
        , member_dynamic_flags<filter_3d>
        , member_help<"Zooms in and out"
        >>
    , obj_member
        < "Light Location"
        , &draw_controls::m_3DLightPosition
        , member_dynamic_flags<filter_3d_wl>
        , member_help<"The action location of the light relative to the object"
        >>
    , obj_member
        < "Light Follow"
        , &draw_controls::m_3DFollowCamera
        , member_dynamic_flags<filter_3d_wl>
        , member_help<"Tells if the light should follow the camera position or hold in place.\nNOTE: You can press space-bar to achieve the same effect"
        >>
    , obj_member
        < "Recenter"
        , +[](draw_controls& O, bool bRead, std::string& Value)
        {
            if (bRead) Value = "Recenter";
            else
            {
                if ( O.m_pOptions->m_RenderMode == draw_options::render_mode::RENDER_2D )
                {
                    O.m_2DMouseTranslate = { 0.0f, 0.0f };
                }
                else
                {
                    O.m_3DView.setPosition({ 0,0,O.m_3DDistance });
                    O.m_3DAngles = xcore::radian3{ 0_xdeg,0_xdeg,0_xdeg };
                }
            }
        }
        , member_ui<std::string>::button<>
        , member_help<"Reset the image at the center of the screen"
        >>
    , obj_member 
        < "Zoom"
        , +[](draw_controls& O, bool bRead, float& Value)
        {
            if (O.m_pOptions->m_RenderMode == draw_options::render_mode::RENDER_2D)
            {
                static constexpr auto max_v = 30;
                if (bRead)
                {
                    Value = O.m_2DMouseScale / max_v;
                }
                else
                {
                    float v = Value * max_v;
                    O.m_2DMouseTranslate.m_X += (O.m_2DMouseTranslate.m_X) * (v - O.m_2DMouseScale) / O.m_2DMouseScale;
                    O.m_2DMouseTranslate.m_Y += (O.m_2DMouseTranslate.m_Y) * (v - O.m_2DMouseScale) / O.m_2DMouseScale;
                    O.m_2DMouseScale = v;
                }
            }
            else
            {
                static constexpr auto max_v = 7;
                static constexpr auto min_v = 0.61f;
                static constexpr auto range_v = max_v - min_v;
                if (bRead) Value = 1 - (O.m_3DDistance - min_v) / range_v;
                else O.m_3DDistance = (1 - Value) * range_v + min_v;
            }
        }
        , member_ui<float>::scroll_bar<0.01, 1>
//        , member_dynamic_flags<filter_2d>
        , member_help<"Zooms in and out of the image"
        >>
    )
};
XPROPERTY_REG(draw_controls)

//------------------------------------------------------------------------------------------------

struct material_mgr
{
    union material
    {
        std::uint32_t m_Value {};

        static constexpr auto num_bits_used = 7;
        struct
        {
            std::uint8_t    m_Bilinear : 1
            ,               m_CubeMap  : 1
            ,               m_3DRender : 1
            ,               m_UWrapMode: 2      // base on xcore::bitmap::wrap_mode
            ,               m_VWrapMode: 2      // base on xcore::bitmap::wrap_mode
            ;
        };

        std::uint32_t getGuid() const noexcept
        {
            return xcore::crc<32>::FromBytes({reinterpret_cast<const std::byte*>(this), sizeof(*this)}).m_Value;
        }
    };

    struct material_instance
    {
        xgpu::texture m_Texture;

        std::uint32_t getGuid() const noexcept
        {
            return xcore::crc<32>::FromBytes({ reinterpret_cast<const std::byte*>(this), sizeof(*this) }).m_Value;
        }
    };

    //----------------------------------------------------------------------------------------

    void SetMaterialInstance(xgpu::device& Device, xgpu::cmd_buffer& CmdBuffer, material_instance& MaterialInstance, bool b2D, bool bBilinear )
    {
        material Material
        { .m_Bilinear  = bBilinear
        , .m_CubeMap   = MaterialInstance.m_Texture.isCubemap()
        , .m_3DRender  = !b2D
        , .m_UWrapMode = static_cast<std::uint8_t>(MaterialInstance.m_Texture.getAdressModes()[0])
        , .m_VWrapMode = static_cast<std::uint8_t>(MaterialInstance.m_Texture.getAdressModes()[1])
        };

        std::uint32_t PipelineGuid = Material.getGuid() ^ MaterialInstance.getGuid();

        if (auto Entry = m_PipelineInstances.find(PipelineGuid); Entry != m_PipelineInstances.end())
        {
            CmdBuffer.setPipelineInstance(Entry->second);
        }
        else
        {
            auto& Pipeline = getMaterial(Device, Material);
            auto  Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{ MaterialInstance.m_Texture } };
            auto  Setup    = xgpu::pipeline_instance::setup
            { .m_PipeLine           = Pipeline
            , .m_SamplersBindings   = Bindings
            };

            xgpu::pipeline_instance TempPI;
            if (auto Err = Device.Create(TempPI, Setup); Err)
            {
                e10::DebugMessage(xgpu::getErrorMsg(Err));
                std::exit(xgpu::getErrorInt(Err));
            }

            CmdBuffer.setPipelineInstance(TempPI);
            m_PipelineInstances.insert({ PipelineGuid, std::move(TempPI) });
        }
    }

    //----------------------------------------------------------------------------------------

    void CreateMaterialInstance(xgpu::device& Device, material_instance& MaterialInstance, const xcore::bitmap& Bitmap)
    {
        assert(MaterialInstance.m_Texture.m_Private == nullptr);

        // Create officially the material instance
        if (auto Err = xgpu::tools::bitmap::Create(MaterialInstance.m_Texture, Device, Bitmap); Err)
        {
            e10::DebugMessage(xgpu::getErrorMsg(Err));
            std::exit(xgpu::getErrorInt(Err));
        }
    }

    //----------------------------------------------------------------------------------------

    void UpdateMaterialInstance( xgpu::device& Device, material_instance& MaterialInstance, const xcore::bitmap& Bitmap )
    {
        // First let us be sure that we are clear...
        ReleaseMaterialInstance(Device, MaterialInstance );

        // Now we can create a new material instance
        CreateMaterialInstance(Device, MaterialInstance, Bitmap);
    }

    //----------------------------------------------------------------------------------------

    void ReleaseMaterialInstance(xgpu::device& Device, material_instance& MaterialInstance)
    {
        if (MaterialInstance.m_Texture.m_Private == nullptr ) return;
        
        for (int i = 0; i < (1 << material::num_bits_used); ++i)
        {
            material Material;
            Material.m_Value = i;
            std::uint32_t PipelineGuid = Material.getGuid() ^ MaterialInstance.getGuid();
            if (auto Entry = m_PipelineInstances.find(PipelineGuid); Entry != m_PipelineInstances.end())
            {
                // This is what we should do to release all combinations
                //Device.Destroy(Entry->second);
                //m_PipelineInstances.erase(Entry);
            }

            // Release the texture
            // MaterialInstance.m_Texture.m_Private.reset();
            // HACK: This is a hack to avoid the texture to be released
            memset(&MaterialInstance.m_Texture, 0, sizeof(MaterialInstance.m_Texture));
        }
    }

    //----------------------------------------------------------------------------------------

    xgpu::pipeline& getMaterial( xgpu::device& Device, material Material )
    {
        // Do first the simple job of finding the right pipeline
        {
            auto Entry = m_Pipelines.find(Material.getGuid());
            if (Entry != m_Pipelines.end())
                return Entry->second;
        }

        //
        // Vertex descriptors
        //
        xgpu::vertex_descriptor VertexDescriptor;
        if( Material.m_3DRender )
        {
            auto Attributes = std::array
            {
                xgpu::vertex_descriptor::attribute
                {
                    .m_Offset = offsetof(e10::vert_3d, m_Position)
                ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D
                }
            ,   xgpu::vertex_descriptor::attribute
                {
                    .m_Offset = offsetof(e10::vert_3d, m_Binormal)
                ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D
                }
            ,   xgpu::vertex_descriptor::attribute
                {
                    .m_Offset = offsetof(e10::vert_3d, m_Tangent)
                ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D
                }
            ,   xgpu::vertex_descriptor::attribute
                {
                    .m_Offset = offsetof(e10::vert_3d, m_Normal)
                ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D
                }
            ,   xgpu::vertex_descriptor::attribute
                {
                    .m_Offset = offsetof(e10::vert_3d, m_TexCoord)
                ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
                }
            };
            auto Setup = xgpu::vertex_descriptor::setup
            {
                .m_VertexSize = sizeof(e10::vert_3d)
            ,   .m_Attributes = Attributes
            };

            if (auto Err = Device.Create(VertexDescriptor, Setup); Err)
                exit( xgpu::getErrorInt(Err) );
        }
        else
        {
            auto Attributes = std::array
            {
                xgpu::vertex_descriptor::attribute
                { .m_Offset = offsetof(e10::vert_2d, m_X)
                , .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
                }
            ,   xgpu::vertex_descriptor::attribute
                { .m_Offset = offsetof(e10::vert_2d, m_UV)
                , .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D
                }
            };
            auto Setup = xgpu::vertex_descriptor::setup
            { .m_VertexSize = sizeof(e10::vert_2d)
            , .m_Attributes = Attributes
            };

            if (auto Err = Device.Create(VertexDescriptor, Setup); Err)
                exit(xgpu::getErrorInt(Err));
        }

        //
        // Shaders
        //
        xgpu::shader FragmentShader;
        xgpu::shader VertexShader;
        if (Material.m_3DRender)
        {
            if (Material.m_CubeMap)
            {
                xgpu::shader::setup SetupF
                { .m_Type = xgpu::shader::type::bit::FRAGMENT
                , .m_Sharer = xgpu::shader::setup::raw_data{ g_FragShader3DCubeSPV }
                };
                if (auto Err = Device.Create(FragmentShader, SetupF); Err)
                    exit(xgpu::getErrorInt(Err));

                xgpu::shader::setup Setup
                {
                    .m_Type = xgpu::shader::type::bit::VERTEX
                ,   .m_Sharer = xgpu::shader::setup::raw_data{g_VertShader3DCubeSPV}
                };

                if (auto Err = Device.Create(VertexShader, Setup); Err)
                    exit(xgpu::getErrorInt(Err));
            }
            else
            {
                xgpu::shader::setup SetupF
                { .m_Type = xgpu::shader::type::bit::FRAGMENT
                , .m_Sharer = xgpu::shader::setup::raw_data{ g_FragShader3DSPV }
                };
                if (auto Err = Device.Create(FragmentShader, SetupF); Err)
                    exit(xgpu::getErrorInt(Err));

                xgpu::shader::setup Setup
                {
                    .m_Type = xgpu::shader::type::bit::VERTEX
                ,   .m_Sharer = xgpu::shader::setup::raw_data{g_VertShader3DSPV}
                };

                if (auto Err = Device.Create(VertexShader, Setup); Err)
                    exit(xgpu::getErrorInt(Err));
            }
        }
        else
        {
            if ( Material.m_CubeMap )
            {
                xgpu::shader::setup SetupF
                { .m_Type   = xgpu::shader::type::bit::FRAGMENT
                , .m_Sharer = xgpu::shader::setup::raw_data{ g_FragShader2DCubeSPV }
                };
                if (auto Err = Device.Create(FragmentShader, SetupF); Err)
                    exit(xgpu::getErrorInt(Err));

                xgpu::shader::setup SetupV
                { .m_Type   = xgpu::shader::type::bit::VERTEX
                , .m_Sharer = xgpu::shader::setup::raw_data{g_VertShader2DCubeSPV}
                };

                if (auto Err = Device.Create(VertexShader, SetupV); Err)
                    exit(xgpu::getErrorInt(Err));
            }
            else
            {
                xgpu::shader::setup SetupF
                { .m_Type   = xgpu::shader::type::bit::FRAGMENT
                , .m_Sharer = xgpu::shader::setup::raw_data{ g_FragShader2DSPV }
                };
                if (auto Err = Device.Create(FragmentShader, SetupF); Err)
                    exit(xgpu::getErrorInt(Err));

                xgpu::shader::setup SetupV
                { .m_Type   = xgpu::shader::type::bit::VERTEX
                , .m_Sharer = xgpu::shader::setup::raw_data{g_VertShader2DSPV}
                };

                if (auto Err = Device.Create(VertexShader, SetupV); Err)
                    exit(xgpu::getErrorInt(Err));
            }
        }

        auto Shaders  = std::array<const xgpu::shader*, 2>{ &FragmentShader, &VertexShader };
        auto Samplers = std::array{ xgpu::pipeline::sampler{} };
        auto Setup    = xgpu::pipeline::setup
        {
            .m_VertexDescriptor  = VertexDescriptor
        ,   .m_Shaders           = Shaders
        ,   .m_PushConstantsSize = sizeof(e10::push_contants)
        ,   .m_Samplers          = Samplers
        ,   .m_Primitive         = {.m_Cull             = xgpu::pipeline::primitive::cull::NONE }
        ,   .m_DepthStencil      = {.m_bDepthTestEnable = (bool)Material.m_3DRender }
        ,   .m_Blend             = xgpu::pipeline::blend::getAlphaOriginal()
        };

        Samplers[0].m_MipmapMin  = Material.m_Bilinear ? xgpu::pipeline::sampler::mipmap_sampler::LINEAR : xgpu::pipeline::sampler::mipmap_sampler::NEAREST;
        Samplers[0].m_MipmapMag  = Material.m_Bilinear ? xgpu::pipeline::sampler::mipmap_sampler::LINEAR : xgpu::pipeline::sampler::mipmap_sampler::NEAREST;
        Samplers[0].m_MipmapMode = Material.m_Bilinear ? xgpu::pipeline::sampler::mipmap_mode::LINEAR    : xgpu::pipeline::sampler::mipmap_mode::NEAREST;
        Samplers[0].m_AddressMode[0] = static_cast<xgpu::pipeline::sampler::address_mode>(Material.m_UWrapMode);
        Samplers[0].m_AddressMode[1] = static_cast<xgpu::pipeline::sampler::address_mode>(Material.m_VWrapMode);

        xgpu::pipeline Temp;
        if (auto Err = Device.Create(Temp, Setup); Err)
            exit(xgpu::getErrorInt(Err));

        m_Pipelines.insert({Material.getGuid(), std::move(Temp) });

        return m_Pipelines.find(Material.getGuid())->second;
    }

    //----------------------------------------------------------------------------------------

    std::unordered_map<std::uint32_t, xgpu::pipeline>           m_Pipelines;
    std::unordered_map<std::uint32_t, xgpu::pipeline_instance>  m_PipelineInstances;
};

//------------------------------------------------------------------------------------------------

struct mesh_mgr
{
    struct mesh
    {
        xgpu::buffer m_VertexBuffer;
        xgpu::buffer m_IndexBuffer;
        int          m_IndexCount;
    };

    enum class model
    { PLANE_2D
    , EXPLODED_CUBE_2D
    , CUBE_3D
    , SPHERE_3D
    , ENUM_COUNT
    };

    //----------------------------------------------------------------------------------

    void Initialize(xgpu::device& Device)
    {
        Create_2DPlane(Device);
        Create_2DExplodedCube(Device);
        Create_3DCube(Device);
        Create_3DSphere(Device);
    }

    //----------------------------------------------------------------------------------

    void Render( xgpu::cmd_buffer& CmdBuffer, model Model )
    {
        auto& Mesh = m_Meshes[static_cast<int>(Model)];
        CmdBuffer.setBuffer(Mesh.m_VertexBuffer);
        CmdBuffer.setBuffer(Mesh.m_IndexBuffer);
        CmdBuffer.Draw(Mesh.m_IndexCount);
    }

    //----------------------------------------------------------------------------------

    void Create_2DPlane(xgpu::device& Device)
    {
        mesh& Mesh = m_Meshes[static_cast<int>(model::PLANE_2D)];
        Mesh.m_IndexCount = 6;

        if (auto Err = Device.Create(Mesh.m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(e10::vert_2d), .m_EntryCount = 4 }); Err)
            exit(xgpu::getErrorInt(Err));

        (void)Mesh.m_VertexBuffer.MemoryMap(0, 4, [&](void* pData)
        {
            auto pVertex = static_cast<e10::vert_2d*>(pData);
            pVertex[0] = { -100.0f, -100.0f,  { 0.0f, 0.0f, 0.0f } };
            pVertex[1] = {  100.0f, -100.0f,  { 1.0f, 0.0f, 0.0f } };
            pVertex[2] = {  100.0f,  100.0f,  { 1.0f, 1.0f, 0.0f } };
            pVertex[3] = { -100.0f,  100.0f,  { 0.0f, 1.0f, 0.0f } };
        });

        if (auto Err = Device.Create(Mesh.m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = Mesh.m_IndexCount }); Err)
            exit(xgpu::getErrorInt(Err));

        (void)Mesh.m_IndexBuffer.MemoryMap(0, Mesh.m_IndexCount, [&](void* pData)
        {
            auto            pIndex = static_cast<std::uint32_t*>(pData);
            constexpr auto  StaticIndex = std::array
            {
                2u,  1u,  0u,      3u,  2u,  0u,    // front
            };
            static_assert(StaticIndex.size() == 6);
            for (auto i : StaticIndex)
            {
                *pIndex = i;
                pIndex++;
            }
        });
    }

    //----------------------------------------------------------------------------------

    void Create_2DExplodedCube(xgpu::device& Device)
    {
        mesh& Mesh = m_Meshes[static_cast<int>(model::EXPLODED_CUBE_2D)];
        Mesh.m_IndexCount = 6 * 6;

        if (auto Err = Device.Create(Mesh.m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(e10::vert_2d), .m_EntryCount = 4*6 }); Err)
            exit( xgpu::getErrorInt(Err) );

        (void)Mesh.m_VertexBuffer.MemoryMap(0, 4*6, [&](void* pData)
        {
            auto pVertex = static_cast<e10::vert_2d*>(pData);

            int iVert=0;
            pVertex[iVert++] = { 0.0f, -100.0f,  xcore::vector3(1.0f,  1.0f,  1.0f).NormalizeSafe() };
            pVertex[iVert++] = { 200.0f, -100.0f,  xcore::vector3(1.0f,  1.0f, -1.0f).NormalizeSafe() };
            pVertex[iVert++] = { 200.0f,  100.0f,  xcore::vector3(1.0f, -1.0f, -1.0f).NormalizeSafe() };
            pVertex[iVert++] = { 0.0f,  100.0f,  xcore::vector3(1.0f, -1.0f,  1.0f).NormalizeSafe() };

            for ( int iFace=0; iFace<2; iFace++)
            {
                for (int i=0;i<4;++i)
                {
                    pVertex[iVert] = pVertex[iVert - 4];
                    pVertex[iVert].m_UV = xcore::vector3(pVertex[iVert].m_UV).RotateY(-90_xdeg);
                    pVertex[iVert].m_X -= 200.0f;
                    iVert++;
                }
            }

            for (int i = 0; i < 4; ++i)
            {
                pVertex[iVert] = pVertex[i];
                pVertex[iVert].m_UV = xcore::vector3(pVertex[iVert].m_UV).RotateY(90_xdeg);
                pVertex[iVert].m_X += 200.0f;
                iVert++;
            }

            for (int i = 0; i < 4; ++i)
            {
                pVertex[iVert] = pVertex[4 + i];
                pVertex[iVert].m_UV = xcore::vector3(pVertex[iVert].m_UV).RotateX(-90_xdeg);
                pVertex[iVert].m_Y -= 200.0f;
                iVert++;
            }

            for (int i = 0; i < 4; ++i)
            {
                pVertex[iVert] = pVertex[4 + i];
                pVertex[iVert].m_UV = xcore::vector3(pVertex[iVert].m_UV).RotateX(90_xdeg);
                pVertex[iVert].m_Y += 200.0f;
                iVert++;
            }

            assert(iVert <= (4 * 6) );
        });

        if (auto Err = Device.Create(Mesh.m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = Mesh.m_IndexCount }); Err)
            exit( xgpu::getErrorInt(Err) );

        (void)Mesh.m_IndexBuffer.MemoryMap(0, Mesh.m_IndexCount, [&](void* pData)
        {
            auto            pIndex = static_cast<std::uint32_t*>(pData);
            constexpr auto  StaticIndex = std::array
            {
                2u,  1u,  0u,      3u,  2u,  0u,    // front
            };
            static_assert(StaticIndex.size() == 6);

            for (int iFace = 0; iFace < 6; ++iFace)
            {
                for (auto i : StaticIndex)
                {
                    *pIndex = static_cast<std::uint32_t>(i + iFace* 4);
                    pIndex++;
                }
            }
        });
    }

    //----------------------------------------------------------------------------------

    void Create_3DCube(xgpu::device& Device)
    {
        const auto  Primitive = xprim_geom::cube::Generate(4, 4, 4, 4, xprim_geom::float3{ 1,1,1 });
        mesh&       Mesh      = m_Meshes[static_cast<int>(model::CUBE_3D)];

        Mesh.m_IndexCount = static_cast<int>(Primitive.m_Indices.size());

        if (auto Err = Device.Create(Mesh.m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(e10::vert_3d), .m_EntryCount = static_cast<int>(Primitive.m_Vertices.size()) }); Err)
            exit(xgpu::getErrorInt(Err));

        (void)Mesh.m_VertexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Vertices.size()), [&](void* pData)
        {
            auto pVertex = static_cast<e10::vert_3d*>(pData);
            for( int i=0; i< static_cast<int>(Primitive.m_Vertices.size()); ++i )
            {
                auto&       V  = pVertex[i];
                const auto& v  = Primitive.m_Vertices[i];
                V.m_Position.setup( v.m_Position.m_X, v.m_Position.m_Y, v.m_Position.m_Z );
                V.m_Normal.setup( v.m_Normal.m_X, v.m_Normal.m_Y, v.m_Normal.m_Z );

                V.m_Tangent.setup(v.m_Tangent.m_X, v.m_Tangent.m_Y, v.m_Tangent.m_Z);
                V.m_Binormal = (xcore::vector3{ V.m_Normal }.Cross(xcore::vector3{ V.m_Tangent } )).NormalizeSafe();

                V.m_TexCoord.setup(v.m_Texcoord.m_X, v.m_Texcoord.m_Y);
            }
        });
        
        if (auto Err = Device.Create(Mesh.m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = static_cast<int>(Primitive.m_Indices.size()) }); Err)
            exit( xgpu::getErrorInt(Err) );

        (void)Mesh.m_IndexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Indices.size()), [&](void* pData)
        {
            auto            pIndex      = static_cast<std::uint32_t*>(pData);
            for( int i=0; i< static_cast<int>(Primitive.m_Indices.size()); ++i )
            {
                pIndex[i] = Primitive.m_Indices[i];
            }
        });
    }

    //----------------------------------------------------------------------------------

    void Create_3DSphere(xgpu::device& Device)
    {
        const auto  Primitive = xprim_geom::uvsphere::Generate( 70, 70, 1, 0.5f );
        mesh&       Mesh      = m_Meshes[static_cast<int>(model::SPHERE_3D)];

        Mesh.m_IndexCount = static_cast<int>(Primitive.m_Indices.size());

        if (auto Err = Device.Create(Mesh.m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(e10::vert_3d), .m_EntryCount = static_cast<int>(Primitive.m_Vertices.size()) }); Err)
            exit(xgpu::getErrorInt(Err));

        (void)Mesh.m_VertexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Vertices.size()), [&](void* pData)
        {
            auto pVertex = static_cast<e10::vert_3d*>(pData);
            for( int i=0; i< static_cast<int>(Primitive.m_Vertices.size()); ++i )
            {
                auto&       V  = pVertex[i];
                const auto& v  = Primitive.m_Vertices[i];
                V.m_Position.setup( v.m_Position.m_X, v.m_Position.m_Y, v.m_Position.m_Z );
                V.m_Normal.setup( v.m_Normal.m_X, v.m_Normal.m_Y, v.m_Normal.m_Z );

                V.m_Tangent.setup(v.m_Tangent.m_X, v.m_Tangent.m_Y, v.m_Tangent.m_Z);
                V.m_Binormal = (xcore::vector3{ V.m_Normal }.Cross(xcore::vector3{ V.m_Tangent } )).NormalizeSafe();

                V.m_TexCoord.setup(v.m_Texcoord.m_X, v.m_Texcoord.m_Y);
            }
        });

        if (auto Err = Device.Create(Mesh.m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = static_cast<int>(Primitive.m_Indices.size()) }); Err)
            exit( xgpu::getErrorInt(Err) );

        (void)Mesh.m_IndexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Indices.size()), [&](void* pData)
        {
            auto            pIndex      = static_cast<std::uint32_t*>(pData);
            for( int i=0; i< static_cast<int>(Primitive.m_Indices.size()); ++i )
            {
                pIndex[i] = Primitive.m_Indices[i];
            }
        });
    }

    std::array<mesh, static_cast<int>(mesh_mgr::model::ENUM_COUNT)> m_Meshes;
};

//------------------------------------------------------------------------------------------------

int E10_Example()
{
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = true, .m_pLogErrorFunc = e10::DebugMessage, .m_pLogWarning = e10::DebugMessage }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err)
        return xgpu::getErrorInt(Err);

    xgpu::window MainWindow;
    if (auto Err = Device.Create(MainWindow, {}); Err)
        return xgpu::getErrorInt(Err);

    //
    // Create the main render managers
    //
    material_mgr MaterialMgr;
    mesh_mgr     MeshMgr;

    MeshMgr.Initialize(Device);


    //
    // Setup ImGui
    //
    xgpu::tools::imgui::CreateInstance(MainWindow);

    //
    // Create the background material instance
    //

    material_mgr::material_instance BackgroundMaterialInstance;
    MaterialMgr.CreateMaterialInstance(Device, BackgroundMaterialInstance, xcore::bitmap::getDefaultBitmap());

    //
    // Setup the compiler
    //
    auto Compiler = std::make_unique<e10::compiler>();

    //
    // Set the project path
    //
    {
        TCHAR szFileName[MAX_PATH];
        GetModuleFileName(NULL, szFileName, MAX_PATH);

        std::wcout << L"Full path: " << szFileName << L"\n";
        if (auto I = e10::FindCaseInsensitive(std::wstring{ szFileName }, { L"xGPU" }); I != std::string::npos )
        {
            I += 4; // Skip the xGPU part
            szFileName[I] = 0;
            std::wcout << L"Found xGPU at: " << szFileName << L"\n";

            TCHAR LIONantProject[] = L"\\dependencies\\xtexture.plugin\\bin\\example.lion_project";
            for (int i = 0; szFileName[I++] = LIONantProject[i]; ++i);

            std::wcout << "Project Path: " << szFileName << "\n";

            Compiler->SetupProject(strXstr(szFileName));
        }
    }

    // Set active the particular descriptor
    if ( auto Err = Compiler->SetupDescriptor( "Texture", 0x34DBB69E8762EFA9); Err )
    {
        e10::DebugMessage(Err.getCode().m_pString);
        return 1;
    }

    //
    // Define the draw options
    //
    draw_options            DrawOptions;
    e05::bitmap_inspector   BitmapInspector;
    draw_controls           DrawControls(DrawOptions);

    //
    // This will be the texture that we compiled
    //
    material_mgr::material_instance UserMaterialInstance;

    auto UpdateTextureMaterial = [&]()
    {
        BitmapInspector.Load(Compiler->m_ResourcePath.c_str());

        // update the material instance to use the new texture
        MaterialMgr.UpdateMaterialInstance(Device, UserMaterialInstance, *BitmapInspector.m_pBitmap);

        // Set the max mip levels
        // Make sure the current level is within the range
        DrawOptions.m_MaxMipLevels = BitmapInspector.m_pBitmap->getMipCount()-1;
        if (DrawOptions.m_ChooseMipLevel >= DrawOptions.m_MaxMipLevels )
        {
            DrawOptions.m_ChooseMipLevel = std::min(DrawOptions.m_MaxMipLevels, std::max( 0, DrawOptions.m_MaxMipLevels - 1) );
        }
    };

    //
    // Setup the inspector windows
    //
    auto                  Inspectors = std::array
    { xproperty::inspector("Descriptor")
    , xproperty::inspector("Viewer")
    };

    Inspectors[0].AppendEntity();
    Inspectors[0].AppendEntityComponent(*xproperty::getObject(*Compiler->m_pInfo), Compiler->m_pInfo.get());
    Inspectors[0].AppendEntityComponent(*Compiler->m_pDescriptor->getProperties(), Compiler->m_pDescriptor.get());

    Inspectors[1].AppendEntity();
    Inspectors[1].AppendEntityComponent(*xproperty::getObject(*Compiler.get()), Compiler.get());
    Inspectors[1].AppendEntityComponent(*xproperty::getObject(DrawControls), &DrawControls);
    Inspectors[1].AppendEntityComponent(*xproperty::getObject(DrawOptions), &DrawOptions);
    Inspectors[1].AppendEntityComponent(*xproperty::getObject(BitmapInspector), &BitmapInspector );

    // Theme the property dialogs to be more readable
    for ( auto& E: Inspectors)
    {
        E.m_Settings.m_ColorVScalar1 = 0.270f*1.4f;
        E.m_Settings.m_ColorVScalar2 = 0.305f* 1.4f;
        E.m_Settings.m_ColorSScalar  = 0.26f * 1.4f;
    }
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 0.5f;

    //
    // Create the input devices
    //
    xgpu::mouse     Mouse;
    xgpu::keyboard  Keyboard;
    {
        Instance.Create(Mouse, {});
        Instance.Create(Keyboard, {});
    }

    //
    // Main loop
    //
    while (Instance.ProcessInputEvents())
    {
        if (xgpu::tools::imgui::BeginRendering(true))
            continue;

        const float MainWindowWidth  = static_cast<float>(MainWindow.getWidth());
        const float MainWindowHeight = static_cast<float>(MainWindow.getHeight());

        //
        // Handle Input
        //
        if (auto ctx = ImGui::GetCurrentContext(); ctx->HoveredWindow == nullptr || ctx->HoveredWindow->ID == ImGui::GetID("MainDockSpace"))
        {
            if ( DrawOptions.m_RenderMode == draw_options::render_mode::RENDER_2D )
            {
                const float  OldScale = DrawControls.m_2DMouseScale;
                auto& io = ImGui::GetIO();

                if (io.MouseDown[1] || io.MouseDown[2])
                {
                    if (io.MouseDown[0])
                    {
                        DrawControls.m_2DMouseScale -= 8000.0f * io.DeltaTime * (io.MouseDelta.y * (2.0f / MainWindowHeight));
                    }
                    else
                    {
                        DrawControls.m_2DMouseTranslate.m_X += io.MouseDelta.x * (2.0f / MainWindowWidth);
                        DrawControls.m_2DMouseTranslate.m_Y += io.MouseDelta.y * (2.0f / MainWindowHeight);
                    }
                }

                // Wheel scale
                DrawControls.m_2DMouseScale += 1.9f * io.MouseWheel;
                if (DrawControls.m_2DMouseScale < 0.1f) DrawControls.m_2DMouseScale = 0.1f;

                // Always zoom from the perspective of the mouse
                const float mx = ((io.MousePos.x / (float)MainWindowWidth) - 0.5f) * 2.0f;
                const float my = ((io.MousePos.y / (float)MainWindowHeight) - 0.5f) * 2.0f;
                DrawControls.m_2DMouseTranslate.m_X += (DrawControls.m_2DMouseTranslate.m_X - mx) * (DrawControls.m_2DMouseScale - OldScale) / OldScale;
                DrawControls.m_2DMouseTranslate.m_Y += (DrawControls.m_2DMouseTranslate.m_Y - my) * (DrawControls.m_2DMouseScale - OldScale) / OldScale;
            }
            else
            {
                //
                // Input
                //
                if (Mouse.isPressed(xgpu::mouse::digital::BTN_RIGHT))
                {
                    auto MousePos = Mouse.getValue(xgpu::mouse::analog::POS_REL);
                    DrawControls.m_3DAngles.m_Pitch.m_Value -= 0.01f * MousePos[1];
                    DrawControls.m_3DAngles.m_Yaw.m_Value   -= 0.01f * MousePos[0];
                }

                if (Keyboard.wasPressed(xgpu::keyboard::digital::KEY_SPACE))
                {
                    DrawControls.m_3DLightPosition = DrawControls.m_3DView.getPosition();
                    DrawControls.m_3DFollowCamera = !DrawControls.m_3DFollowCamera;
                }

                DrawControls.m_3DDistance += DrawControls.m_3DDistance * -0.2f * Mouse.getValue(xgpu::mouse::analog::WHEEL_REL)[0];
                if (DrawControls.m_3DDistance < 0.5f) DrawControls.m_3DDistance = 0.5f;
            }
        }

        //
        // Update the camera
        //
        DrawControls.m_3DView.LookAt(DrawControls.m_3DDistance, DrawControls.m_3DAngles, { 0,0,0 });

        //
        // Render the image
        //
        {
            auto CmdBuffer = MainWindow.getCmdBuffer();

            //
            // Render background
            //
            {
                //CmdBuffer.setPipelineInstance(BackgroundMaterialInstance);
                MaterialMgr.SetMaterialInstance(Device, CmdBuffer, BackgroundMaterialInstance, true, true);

                e10::push_contants PushContants;

                PushContants.m_Scale =
                { (150 * 2.0f) / MainWindowWidth
                , (150 * 2.0f) / MainWindowHeight
                };
                PushContants.m_Translation.setZero();
                PushContants.m_UVScale = { 120.0f,120.0f };

                PushContants.m_ToGamma   = 2.2f;

                PushContants.m_ColorMask = xcore::vector4(1);
                PushContants.m_Mode      = xcore::vector4(0,0,1,1);
                PushContants.m_TintColor = xcore::vector4(DrawOptions.m_BackgroundIntensity
                                                        , DrawOptions.m_BackgroundIntensity
                                                        , DrawOptions.m_BackgroundIntensity
                                                        , 1);
                PushContants.m_MipLevel = 0;
                PushContants.m_NormalModes.setZero();

                CmdBuffer.setPushConstants(PushContants);
                MeshMgr.Render(CmdBuffer, mesh_mgr::model::PLANE_2D);
            }

            //
            // Render the Texture
            //
            if( UserMaterialInstance.m_Texture.m_Private != nullptr )
            {
                e10::push_contants PushContants;

                PushContants.m_Scale = { (DrawControls.m_2DMouseScale * 2.0f) / MainWindowWidth * BitmapInspector.m_pBitmap->getAspectRatio()
                                       , (DrawControls.m_2DMouseScale * 2.0f) / MainWindowHeight
                };
                PushContants.m_UVScale     = DrawOptions.m_UVScale;
                PushContants.m_Translation = DrawControls.m_2DMouseTranslate;

                const float MipMode = DrawOptions.m_ChooseMipLevel == -1 ? 1.0f : 0.0f;

                switch (DrawOptions.m_ChannelsMode)
                {
                case draw_options::channels_mode::COLOR_ALPHA:
                    PushContants.m_ColorMask = xcore::vector4(1);
                    PushContants.m_Mode = xcore::vector4(1, 0, 0, MipMode);
                    break;
                case draw_options::channels_mode::NO_ALPHA:
                    PushContants.m_ColorMask = xcore::vector4(1);
                    PushContants.m_Mode = xcore::vector4(0, 0, 1, MipMode);
                    break;
                case draw_options::channels_mode::A_ONLY:
                    PushContants.m_ColorMask = xcore::vector4(0, 0, 0, 1);
                    PushContants.m_Mode = xcore::vector4(0, 1, 0, MipMode);
                    break;
                case draw_options::channels_mode::R_ONLY:
                    PushContants.m_ColorMask = xcore::vector4(1, 0, 0, 0);
                    PushContants.m_Mode = xcore::vector4(0, 1, 0, MipMode);
                    break;
                case draw_options::channels_mode::G_ONLY:
                    PushContants.m_ColorMask = xcore::vector4(0, 1, 0, 0);
                    PushContants.m_Mode = xcore::vector4(0, 1, 0, MipMode);
                    break;
                case draw_options::channels_mode::B_ONLY:
                    PushContants.m_ColorMask = xcore::vector4(0, 0, 1, 0);
                    PushContants.m_Mode = xcore::vector4(0, 1, 0, MipMode);
                    break;
                }

                if (BitmapInspector.m_pBitmap->getFormat() == xcore::bitmap::format::BC3_81Y0X_NORMAL
                    && DrawOptions.m_DisplayInGammaMode != draw_options::display_gamma_mode::RAW_DATA_INFILE)
                {
                    PushContants.m_NormalModes = xcore::vector4(1, 0, 0, 0);
                }
                else if (BitmapInspector.m_pBitmap->getFormat() == xcore::bitmap::format::BC5_8YX_NORMAL
                    && DrawOptions.m_DisplayInGammaMode != draw_options::display_gamma_mode::RAW_DATA_INFILE)
                {
                    PushContants.m_NormalModes = xcore::vector4(0, 1, 0, 0);
                }
                else
                {
                    PushContants.m_NormalModes = xcore::vector4(0, 0, 0, 0);
                }

                PushContants.m_MipLevel = static_cast<float>(std::max(0, DrawOptions.m_ChooseMipLevel));

                PushContants.m_TintColor = xcore::vector4(1);
                switch (DrawOptions.m_DisplayInGammaMode)
                {
                case draw_options::display_gamma_mode::GAMMA:
                    PushContants.m_ToGamma = DrawOptions.m_DisplayGamma;
                    break;
                case draw_options::display_gamma_mode::LINEAR:
                    PushContants.m_ToGamma = 1;
                    break;
                case draw_options::display_gamma_mode::RAW_DATA_INFILE:
                    if (BitmapInspector.m_pBitmap->getColorSpace() == xcore::bitmap::color_space::SRGB)
                    {
                        PushContants.m_ToGamma = 2.2f;
                    }
                    else
                    {
                        PushContants.m_ToGamma = 1.0f;
                    }
                    break;
                }

                // Set the right material
                MaterialMgr.SetMaterialInstance
                ( Device
                , CmdBuffer
                , UserMaterialInstance
                , DrawOptions.m_RenderMode == draw_options::render_mode::RENDER_2D
                , DrawOptions.m_bBilinearMode
                );

                if (DrawOptions.m_RenderMode == draw_options::render_mode::RENDER_2D)
                {
                    if ( BitmapInspector.m_pBitmap->isCubemap() )
                    {
                        CmdBuffer.setPushConstants(PushContants);
                        MeshMgr.Render(CmdBuffer, mesh_mgr::model::EXPLODED_CUBE_2D);

                    }
                    else
                    {
                        CmdBuffer.setPushConstants(PushContants);
                        MeshMgr.Render(CmdBuffer, mesh_mgr::model::PLANE_2D);
                    }
                }
                else
                {
                    //
                    // Render Image (3D)
                    //
                    // Update the view with latest window size
                    DrawControls.m_3DView.setViewport({ 0, 0, (int)MainWindowWidth, (int)MainWindowHeight });

                    const auto  W2C = DrawControls.m_3DView.getW2C();
                    xcore::matrix4 L2W;
                    L2W.setIdentity();
                    L2W.setScale({BitmapInspector.m_pBitmap->getAspectRatio(), 1, BitmapInspector.m_pBitmap->getAspectRatio()});

                    // Take the light to local space of the object
                    auto W2L = L2W;
                    W2L.InvertSRT();

                    if (DrawControls.m_3DFollowCamera) DrawControls.m_3DLightPosition = DrawControls.m_3DView.getPosition();

                    PushContants.m_L2C = W2C * L2W;
                    PushContants.m_LocalSpaceLightPosition = W2L * DrawControls.m_3DLightPosition;

                    // Enable lighting if the user requested it
                    if (DrawOptions.m_RenderMode == draw_options::render_mode::RENDER_3D_WITH_LIGHTING)
                        PushContants.m_NormalModes.m_Z = 1;

                    CmdBuffer.setPushConstants(PushContants);

                    if (BitmapInspector.m_pBitmap->isCubemap())
                    {
                        MeshMgr.Render(CmdBuffer, mesh_mgr::model::SPHERE_3D);
                        
                    }
                    else
                    {
                        MeshMgr.Render(CmdBuffer, mesh_mgr::model::CUBE_3D);
                    }
                }
            }
        }


        //
        // Check if we should refresh the texture pipeline
        //
        if (Compiler->ReloadTexture())
        {
            UpdateTextureMaterial();
        }

        //
        // Render the Inspectors
        //
        Compiler->m_ValidationErrors.clear();
        Compiler->m_pDescriptor->Validate(Compiler->m_ValidationErrors);

        for (auto& E: Inspectors )
        {
            ImGui::SetNextWindowBgAlpha(0.5f);
            E.Show([&]{});
        }

        //
        // Render
        //
        xgpu::tools::imgui::Render();

        //
        // Pageflip the windows
        //
        MainWindow.PageFlip();
    }

    return 0;
}