#include "source/xGPU.h"
#include "source/tools/xgpu_imgui_breach.h"

#include "dependencies/xmath/source/xmath.h"
#include "dependencies/xmath/source/xmath_ftransform.h"

#include <chrono>
struct draw_vert
{
    float           m_X, m_Y, m_Z;
    float           m_U, m_V;
    std::uint32_t   m_Color;
};

int E22_Example()
{
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = true }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err)
        return xgpu::getErrorInt(Err);

    xgpu::window MainWindow;
    if (auto Err = Device.Create(MainWindow, {}); Err)
        return xgpu::getErrorInt(Err);

    //
    // Setup ImGui
    //
    xgpu::tools::imgui::CreateInstance(MainWindow);

    xgpu::vertex_descriptor VertexDescriptor;
    {
        auto Attributes = std::array
        {
            xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(draw_vert, m_X)
            ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D
            }
        ,   xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(draw_vert, m_U)
            ,   .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
            }
        ,   xgpu::vertex_descriptor::attribute
            {
                .m_Offset = offsetof(draw_vert, m_Color)
            ,   .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED
            }
        };
        auto Setup = xgpu::vertex_descriptor::setup
        {
            .m_VertexSize = sizeof(draw_vert)
        ,   .m_Attributes = Attributes
        };

        if (auto Err = Device.Create(VertexDescriptor, Setup); Err)
            return xgpu::getErrorInt(Err);
    }

    // create pipeline

    xgpu::pipeline PipeLine;
    {
        xgpu::shader MyFragmentShader;
        {
            auto RawData = xgpu::shader::setup::raw_data
            { std::array
                {
                    #include "draw_frag.h"
                }
            };

            if (auto Err = Device.Create(MyFragmentShader, { .m_Type = xgpu::shader::type::bit::FRAGMENT, .m_Sharer = RawData }); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader MyVertexShader;
        {
            auto UniformConstans = std::array
            { static_cast<int>(sizeof(float) * 4 * 4)   // LocalToClip
            };
            auto RawData = xgpu::shader::setup::raw_data
            { std::array
                {
                    #include "draw_vert.h"
                }
            };
            xgpu::shader::setup Setup
            {
                .m_Type = xgpu::shader::type::bit::VERTEX
            ,   .m_Sharer = RawData
            };

            if (auto Err = Device.Create(MyVertexShader, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        auto Shaders    = std::array<const xgpu::shader*, 2>{ &MyFragmentShader, &MyVertexShader };
        auto Samplers   = std::array{ xgpu::pipeline::sampler{} };
        auto Setup      = xgpu::pipeline::setup
        { .m_VertexDescriptor   = VertexDescriptor
        , .m_Shaders            = Shaders
        , .m_PushConstantsSize  = sizeof(float) * 4 * 4
        , .m_Samplers           = Samplers
        };

        if (auto Err = Device.Create(PipeLine, Setup); Err)
            return xgpu::getErrorInt(Err);
    }

    // Define render pass to draw to texture

    xmath::fvec2 FramebufferSize = { 1500, 1000 };
    xgpu::renderpass RenderPass, ImGuiRenderPass;
    xgpu::texture    SceneTexture;
    xgpu::pipeline_instance ViewportPipeLineInstance;
    auto UpdateRenderPass = [&]() -> int {
        xgpu::tools::imgui::ClearTexture(SceneTexture);
        if (SceneTexture.m_Private) Device.Destroy(std::move(SceneTexture));
        if (RenderPass.m_Private) Device.Destroy(std::move(RenderPass));
        if (ViewportPipeLineInstance.m_Private) Device.Destroy(std::move(ViewportPipeLineInstance));
        if (FramebufferSize.x() <= 0 || FramebufferSize.y() <= 0) FramebufferSize = { 100, 100 };
        if (auto Err = Device.Create(SceneTexture, { .m_Format = xgpu::texture::format::R8G8B8A8_UNORM, .m_Width = static_cast<int>(FramebufferSize.x()), .m_Height = static_cast<int>(FramebufferSize.y()), .m_isGamma = false }); Err)
            return xgpu::getErrorInt(Err);


        std::array<xgpu::renderpass::attachment, 1> Attachments
        {
            SceneTexture
        };

        if (auto Err = Device.Create(RenderPass, { .m_Attachments = Attachments }); Err)
            return xgpu::getErrorInt(Err);

        {
            auto  Bindings  = std::array{ xgpu::pipeline_instance::sampler_binding{ SceneTexture } };
            auto  Setup     = xgpu::pipeline_instance::setup
            { .m_PipeLine           = PipeLine
            , .m_SamplersBindings   = Bindings
            };

            if (auto Err = Device.Create(ViewportPipeLineInstance, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        return 0;
        };

    if (int err = UpdateRenderPass()) return err;


    // create pipeline instance

    xgpu::pipeline_instance MainPipeLineInstance;
    {
        xgpu::texture Texture;
        {
            constexpr auto          size_v = 32;
            xgpu::texture::setup    Setup;
            const auto              Mips = std::array{ xgpu::texture::setup::mip_size{ size_v * size_v * sizeof(std::uint32_t) } };

            Setup.m_Height      = size_v;
            Setup.m_Width       = size_v;
            Setup.m_MipSizes    = Mips;

            auto TextureData = std::make_unique< std::array<std::uint32_t, size_v* size_v> >();
            std::fill(TextureData->begin(), TextureData->end(), 0xffffffffu);

            Setup.m_AllFacesData = std::span{ reinterpret_cast<const std::byte*>(TextureData->data()), Mips[0].m_SizeInBytes };

            if (auto Err = Device.Create(Texture, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        auto  Bindings  = std::array{ xgpu::pipeline_instance::sampler_binding{ Texture } };
        auto  Setup     = xgpu::pipeline_instance::setup
        { .m_PipeLine = PipeLine
        , .m_SamplersBindings = Bindings
        };

        if (auto Err = Device.Create(MainPipeLineInstance, Setup); Err)
            return xgpu::getErrorInt(Err);
    }


    // create meshes

    xgpu::buffer VertexBuffer;
    {
        if (auto Err = Device.Create(VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(draw_vert), .m_EntryCount = 24 }); Err)
            return xgpu::getErrorInt(Err);

        (void)VertexBuffer.MemoryMap(0, 24, [&](void* pData)
            {
                auto pVertex = static_cast<draw_vert*>(pData);
                pVertex[0] = { -0.5f, -0.5f,  0.5f, 0.0f, 1.0f, 0xffffffff };
                pVertex[1] = { 0.5f, -0.5f,  0.5f, 1.0f, 1.0f, 0xffffffff };
                pVertex[2] = { 0.5f,  0.5f,  0.5f, 1.0f, 0.0f, 0xffffffff };
                pVertex[3] = { -0.5f,  0.5f,  0.5f, 0.0f, 0.0f, 0xffffffff };

                // Back face
                pVertex[4] = { -0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0xccccccff };
                pVertex[5] = { -0.5f,  0.5f, -0.5f, 1.0f, 1.0f, 0xccccccff };
                pVertex[6] = { 0.5f,  0.5f, -0.5f, 1.0f, 0.0f, 0xccccccff };
                pVertex[7] = { 0.5f, -0.5f, -0.5f, 0.0f, 0.0f, 0xccccccff };

                // Top face
                pVertex[8] = { -0.5f,  0.5f, -0.5f, 0.0f, 1.0f, 0xccccccff };
                pVertex[9] = { -0.5f,  0.5f,  0.5f, 1.0f, 1.0f, 0xffffffff };
                pVertex[10] = { 0.5f,  0.5f,  0.5f, 1.0f, 0.0f, 0xffffffff };
                pVertex[11] = { 0.5f,  0.5f, -0.5f, 0.0f, 0.0f, 0xccccccff };

                // Bottom face
                pVertex[12] = { -0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0xccccccff };
                pVertex[13] = { 0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 0xccccccff };
                pVertex[14] = { 0.5f, -0.5f,  0.5f, 1.0f, 0.0f, 0xffffffff };
                pVertex[15] = { -0.5f, -0.5f,  0.5f, 0.0f, 0.0f, 0xffffffff };

                // Right face
                pVertex[16] = { 0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0xccccccff };
                pVertex[17] = { 0.5f,  0.5f, -0.5f, 1.0f, 1.0f, 0xccccccff };
                pVertex[18] = { 0.5f,  0.5f,  0.5f, 1.0f, 0.0f, 0xffffffff };
                pVertex[19] = { 0.5f, -0.5f,  0.5f, 0.0f, 0.0f, 0xffffffff };

                // Left face
                pVertex[20] = { -0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0xccccccff };
                pVertex[21] = { -0.5f, -0.5f,  0.5f, 1.0f, 1.0f, 0xffffffff };
                pVertex[22] = { -0.5f,  0.5f,  0.5f, 1.0f, 0.0f, 0xffffffff };
                pVertex[23] = { -0.5f,  0.5f, -0.5f, 0.0f, 0.0f, 0xccccccff };
            });
    }

    xgpu::buffer IndexBuffer;
    {
        if (auto Err = Device.Create(IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = 6 * 6 }); Err)
            return xgpu::getErrorInt(Err);

        (void)IndexBuffer.MemoryMap(0, 6 * 6, [&](void* pData)
            {
                auto            pIndex = static_cast<std::uint32_t*>(pData);
                constexpr auto  StaticIndex = std::array
                {
                    0u,  1u,  2u,      0u,  2u,  3u,    // front
                    4u,  5u,  6u,      4u,  6u,  7u,    // back
                    8u,  9u,  10u,     8u,  10u, 11u,   // top
                    12u, 13u, 14u,     12u, 14u, 15u,   // bottom
                    16u, 17u, 18u,     16u, 18u, 19u,   // right
                    20u, 21u, 22u,     20u, 22u, 23u    // left
                };
                static_assert(StaticIndex.size() == 6 * 6);
                for (auto i : StaticIndex)
                {
                    *pIndex = i;
                    pIndex++;
                }
            });
    }

    xmath::radian angle = xmath::radian{ 0 };
    xmath::fvec3 EyePosition = { 0, 1, 2 };
    bool bShouldResizeFramebuffer = true;
    bool bSpin = false;
    float SpinSpeed = 0;
    std::chrono::high_resolution_clock::time_point Now = std::chrono::high_resolution_clock::now();
    float DeltaTime = 0;
    //
    // Main loop
    //
    while (Instance.ProcessInputEvents())
    {

        if (bShouldResizeFramebuffer)
        {
            UpdateRenderPass();
            bShouldResizeFramebuffer = false;
        }

        if (bSpin)
        {
            angle = angle + xmath::radian(SpinSpeed * DeltaTime);
            EyePosition = xmath::fvec3( xmath::Cos(angle), 0.5, xmath::Sin(angle) ) * 2.f;
        }

        if (xgpu::tools::imgui::BeginRendering(true))
            continue;

        // Render
        {

            auto CmdBuffer      = MainWindow.StartRenderPass(RenderPass);
            auto Perspective    = xmath::fmat4::fromPerspective(xmath::radian(1.2f), FramebufferSize.x() / FramebufferSize.y(), 0.1f, 100);
            auto View           = xmath::fmat4::fromLookAt(EyePosition, xmath::fvec3{ 0, 0, 0 }, xmath::fvec3::fromUp());
            auto FinalMatrix    = Perspective * View;

            CmdBuffer.setPipelineInstance(MainPipeLineInstance);
            CmdBuffer.setBuffer(VertexBuffer);
            CmdBuffer.setBuffer(IndexBuffer);
            CmdBuffer.setPushConstants(&FinalMatrix.m_Elements[0], sizeof(FinalMatrix));
            CmdBuffer.Draw(IndexBuffer.getEntryCount());
        }

        // ImGui
        {
            auto CmdBuffer = MainWindow.getCmdBuffer();
            CmdBuffer.setPipelineInstance(ViewportPipeLineInstance);
            ImGui::Begin("Viewport");
            {
                ImVec2 ImageSize = ImGui::GetContentRegionAvail();
                xmath::fvec2 NewFramebufferSize = { ImageSize.x, ImageSize.y };
                if (NewFramebufferSize != FramebufferSize) bShouldResizeFramebuffer = true;
                FramebufferSize = NewFramebufferSize;
                ImGui::Image((ImTextureRef)((void*)&SceneTexture), ImageSize, ImVec2{0, 1}, ImVec2{1, 0});
            }
            ImGui::End();

            ImGui::Begin("Controls");
            {
                if(ImGui::Button(bSpin ? "Stop" : "Spin"))
                {
                    bSpin = !bSpin;
                }
                ImGui::SliderFloat("Spin Speed (rad/s)", &SpinSpeed, -3.60f, 3.60f);
            }
            ImGui::End();
            //
            // Render
            //
            xgpu::tools::imgui::Render();

        }
        DeltaTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - Now).count() * 1E-6f;
        Now = std::chrono::high_resolution_clock::now();
        //
        // Pageflip the windows
        //
        MainWindow.PageFlip();
    }

    return 0;
}
