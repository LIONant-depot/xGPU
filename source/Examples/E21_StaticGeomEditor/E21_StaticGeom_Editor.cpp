﻿#include "dependencies/xproperty/source/xcore/my_properties.h"
#include "dependencies/xstrtool/source/xstrtool.h"

#include "source/Examples/E05_Textures/E05_BitmapInspector.h"
#include "dependencies/xproperty/source/examples/imgui/xPropertyImGuiInspector.h" 
#include <fstream>
#include "source/tools/xgpu_xcore_bitmap_helpers.h"
#include "source/tools/xgpu_view.h"
#include <regex>
#include <algorithm>
#include <unordered_set>
#include <filesystem>

#define XRESOURCE_PIPELINE_NO_COMPILER
#include "dependencies/xresource_pipeline_v2/source/xresource_pipeline.h"
#include "source/xstrtool.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_Resources.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_AssetMgr.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_AssetBrowser.h"

#include "Plugins/xmaterial.plugin/source/xmaterial_xgpu_rsc_loader.h"
#include "Plugins/xmaterial.plugin/source/xmaterial_runtime.h"

#include "Plugins/xmaterial_instance.plugin/source/xmaterial_intance_descriptor.h"
#include "Plugins/xmaterial_instance.plugin/source/xmaterial_instance_xgpu_rsc_loader.h"
#include "Plugins/xmaterial_instance.plugin/source/xmaterial_instance_runtime.h"

#include "../E19_MaterialEditor/E19_mesh_manager.h"

#include "plugins/xgeom.plugin/source/xgeom_rsc_descriptor.h"
#include "plugins/xgeom.plugin/source/xgeom_xgpu_rsc_loader.h"

//-----------------------------------------------------------------------------------

namespace e21
{
    static int                  g_SelectedTexture = 0;
    constexpr auto              g_2DVertShader = std::array
    {
        #include "imgui_vert.h"
    };
    constexpr auto              g_2DFragShader = std::array
    {
        #include "draw_frag.h"
    };

    static void Debugger(std::string_view view)
    {
        printf("%s\n", view.data());
    }

    struct push_const2D
    {
        xmath::fvec2    m_Scale;
        xmath::fvec2    m_Translation;
        xmath::fvec2    m_UVScale;
    };

    struct push_constants
    {
        xmath::fmat4    m_L2C;
    };

    std::string ExtractNodeName(const std::string_view clog)
    {
        std::regex  nodeRegex(R"(N_(\d+))");
        std::smatch match;
        std::string srt(clog);

        if (std::regex_search(srt, match, nodeRegex))
        {
            return match[1].str(); // The node name
        }

        return "";
    }
}

namespace e21
{
    void MeshPreviewStyle(void)
    {
        ImGui::PushStyleColor(ImGuiCol_Text,            IM_COL32(0, 0, 0, 255));           //Text black colour
        ImGui::PushStyleColor(ImGuiCol_FrameBg,         IM_COL32(200, 200, 200, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,  IM_COL32(180, 180, 180, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,   IM_COL32(160, 160, 160, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg,         IM_COL32(100, 100, 100, 255));// dropdown popup bg
        ImGui::PushStyleColor(ImGuiCol_Border,          IM_COL32(0, 0, 0, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f); // enable border
    }

    struct selected_descriptor
    {
        bool empty() const
        {
            return m_InfoGUID.empty();
        }

        void clear()
        {
            m_InfoGUID.clear();
            m_LibraryGUID.clear();
            m_Log.reset();
            m_DescriptorPath.clear();
            m_ResourcePath.clear();

            m_Log = std::make_shared<e10::compilation::historical_entry::log>(e10::compilation::historical_entry::communication{ .m_Result = e10::compilation::historical_entry::result::SUCCESS });
        }

        void GeneratePaths(const std::wstring& InfoPath)
        {
            m_DescriptorPath = InfoPath;
            m_DescriptorPath = m_DescriptorPath.replace(InfoPath.find(L"info.txt"), std::wstring_view(L"info.txt").length(), L"Descriptor.txt");
            m_ResourcePath   = m_DescriptorPath.substr(0, m_DescriptorPath.rfind(L'\\') - sizeof("desc"));
            m_LogPath        = std::format(L"{}.log", m_ResourcePath);

            //
            // Finish generating the resource path
            //
            size_t pos = m_ResourcePath.rfind(L"Descriptors");
            assert(pos != std::wstring::npos);


            assert(m_DescriptorPath[pos - 1] == '\\');
            if (m_DescriptorPath[pos - 2] == L'e'
                && m_DescriptorPath[pos - 3] == L'h'
                && m_DescriptorPath[pos - 4] == L'c'
                && m_DescriptorPath[pos - 5] == L'a'
                && m_DescriptorPath[pos - 6] == L'C'
                && m_DescriptorPath[pos - 7] == L'\\')
            {
                m_LogPath.replace     (pos, sizeof("Descriptors"), L"Resources\\Logs\\");
                m_ResourcePath.replace(pos, sizeof("Descriptors"), L"Resources\\Platforms\\WINDOWS\\");
                
            }
            else
            {
                m_LogPath.replace     (pos, sizeof("Descriptors"), L"Cache\\Resources\\Logs\\");
                m_ResourcePath.replace(pos, sizeof("Descriptors"), L"Cache\\Resources\\Platforms\\WINDOWS\\");
            }
        }

        void SaveDescriptor()
        {
            xproperty::settings::context Context;
            if (auto Err = m_pDescriptor->Serialize(false, m_DescriptorPath, Context); Err)
            {
                assert(false);
            }
        }

        xrsc::material_instance_ref                                 m_MaterialInstanceRef   = {};
        e10::library::guid                                          m_LibraryGUID           = {};
        xresource::full_guid                                        m_InfoGUID              = {};
        std::unique_ptr<xresource_pipeline::descriptor::base>       m_pDescriptor           = {};
        std::shared_ptr<e10::compilation::historical_entry::log>    m_Log                   = {};
        std::wstring                                                m_DescriptorPath        = {};
        std::wstring                                                m_ResourcePath          = {};
        std::wstring                                                m_LogPath               = {};
        bool                                                        m_bReload               = {};
        bool                                                        m_bErrors               = {};
    };

    void PipelineReload( xgpu::device& Device, xgpu::vertex_descriptor& vd, xgpu::pipeline& material, xgpu::pipeline_instance& materialInst, selected_descriptor& SelcDesc, xresource::mgr& Mgr )
    {
        //destroy old pipeline and pipeline instance
        //after compile a few time will cause error hence need to call destroy 
        Device.Destroy(std::move(material));
        Mgr.ReleaseRef(SelcDesc.m_MaterialInstanceRef);
        SelcDesc.m_MaterialInstanceRef.m_Instance = SelcDesc.m_InfoGUID.m_Instance;

        if ( auto pMatIRef = Mgr.getResource(SelcDesc.m_MaterialInstanceRef); pMatIRef)
        {
            auto pRef = Mgr.getResource(pMatIRef->m_MaterialRef);
            assert(pRef);

            //
            // Material for static meshes
            //
            {
                xgpu::shader VertexShader;
                {
                    auto RawData = xgpu::shader::setup::raw_data
                    { std::array
                        {
                            #include "draw_vert.h"
                        }
                    };

                    xgpu::shader::setup Setup
                    { .m_Type   = xgpu::shader::type::bit::VERTEX
                    , .m_Sharer = RawData
                    };
                    if (auto Err = Device.Create(VertexShader, Setup); Err)
                    {
                        assert(false);
                        exit(xgpu::getErrorInt(Err));
                    }
                }

                std::vector<xgpu::pipeline::sampler> SL;
                SL.resize(pMatIRef->m_nTexturesList);

                auto Shaders  = std::array<const xgpu::shader*, 2>{ &pRef->getShader(), &VertexShader};
                auto Setup    = xgpu::pipeline::setup
                { .m_VertexDescriptor  = vd
                , .m_Shaders           = Shaders
                , .m_PushConstantsSize = sizeof(e21::push_constants)
                , .m_Samplers          = SL
                };

                if (auto Err = Device.Create(material, Setup); Err)
                {
                    assert(false);
                    exit(xgpu::getErrorInt(Err));
                }
            }

            //
            // Instance the material instance 
            //
            if (pMatIRef->m_nTexturesList)
            {
                std::vector<xgpu::pipeline_instance::sampler_binding>   Bindings;
                Bindings.reserve(pMatIRef->m_nTexturesList);
                for (int i = 0; i < pMatIRef->m_nTexturesList; ++i)
                {
                    auto& E = pMatIRef->m_pTextureList[i];
                    assert(not E.m_TexureRef.empty());

                    if (auto t = xresource::g_Mgr.getResource(E.m_TexureRef); t)
                    {
                        Bindings.emplace_back(*t);
                    }
                    else
                    {
                        assert(false);
                    }
                }

                Device.Destroy(std::move(materialInst));
                if (auto Err = Device.Create(materialInst, { .m_PipeLine = material, .m_SamplersBindings = Bindings }); Err)
                {
                    assert(false);
                    exit(xgpu::getErrorInt(Err));
                }
            }
        }
    }

    void RemapGUIDToString(std::string& Name, const xresource::full_guid& PreFullGuid)
    {
        // if it is empty the just print empty
        if (PreFullGuid.empty())
        {
            Name = "empty";
        }
        else
        {
            // make sure that there are not pointer issues
            auto FullGuid = xresource::g_Mgr.getFullGuid(PreFullGuid);

            // Find our entry and get the name
            e10::g_LibMgr.getNodeInfo(FullGuid, [&](e10::library_db::info_node& Node)
                {
                    Name = Node.m_Info.m_Name;
                });

            // If we fail to find it for whatever reason let us just use the GUID
            if (Name.empty()) Name = std::format("{:X}", FullGuid.m_Instance.m_Value);
        }
    }

    // A nice cache for the inspector to deal with previewing textures and such...
    // May be this can become generic rather than just textures later...
    struct texture_cache_rlu
    {
        using call_back = std::function<void(const xresource::full_guid&, xrsc::texture_ref&)>;

        // Entry stored in the hash map: holds the value and its node position in the LRU list
        struct entry
        {
            xrsc::texture_ref                         m_Value;
            std::list<xresource::full_guid>::iterator m_Pos;   // position in LRU list
        };

        using map = std::unordered_map<xresource::full_guid, entry>;

        std::size_t                     m_Capacity;
        call_back                       m_Callback;
        std::list<xresource::full_guid> m_LRU; // front = most-recent, back = least-recent
        map                             m_Map;

        std::size_t size()     const noexcept { return m_Map.size(); }
        std::size_t capacity() const noexcept { return m_Capacity; }

        explicit texture_cache_rlu(std::size_t Capacity = 100, call_back Callback = {})
            : m_Capacity(Capacity), m_Callback(std::move(Callback))
        {
            // Optional: heuristic reserve to avoid early rehashing.
            m_Map.reserve(m_Capacity + m_Capacity / 2);
        }

        void setupCapacity(std::size_t new_cap)
        {
            m_Capacity = new_cap;
            EvictIfNeeded();
        }

        void clear()
        {
            // If you need to run the callback for everything, do it here before clearing.
            if (m_Callback)
            {
                for (const auto& k : m_LRU)
                {
                    auto it = m_Map.find(k);
                    if (it != m_Map.end()) m_Callback(it->first, it->second.m_Value);
                }
            }

            m_Map.clear();
            m_LRU.clear();
        }

        bool Contains(const xresource::full_guid& k) const
        {
            return m_Map.find(k) != m_Map.end();
        }

        // Get a pointer to the value if present; marks as most-recent.
        // Returns nullptr if not found.
        xrsc::texture_ref* find(const xresource::full_guid& k)
        {
            auto it = m_Map.find(k);
            if (it == m_Map.end()) return nullptr;
            touch(it);
            return &it->second.m_Value;
        }

        // Touch without needing the value (if you already have it elsewhere).
        bool touch(const xresource::full_guid& k)
        {
            auto it = m_Map.find(k);
            if (it == m_Map.end()) return false;
            touch(it);
            return true;
        }

        // Insert or assign; marks as most-recent.
        // Overload 1: pass a Value
        void InsertOrAssign(const xresource::full_guid& k, xrsc::texture_ref v)
        {
            auto it = m_Map.find(k);
            if (it != m_Map.end())
            {
                it->second.m_Value = std::move(v);
                touch(it);
            }
            else
            {
                m_LRU.push_front(k);
                entry e;
                e.m_Value = std::move(v);
                e.m_Pos = m_LRU.begin();
                m_Map.emplace(k, std::move(e));
                EvictIfNeeded();
            }
        }

        // Erase a specific key (runs callback if provided)
        bool erase(const xresource::full_guid& k)
        {
            auto it = m_Map.find(k);
            if (it == m_Map.end()) return false;
            if (m_Callback) m_Callback(it->first, it->second.m_Value);
            m_LRU.erase(it->second.m_Pos);
            m_Map.erase(it);
            return true;
        }

    private:

        void touch(map::iterator it)
        {
            // Move this key to the front (most-recent)
            m_LRU.splice(m_LRU.begin(), m_LRU, it->second.m_Pos);
        }

        void EvictIfNeeded()
        {
            while (m_Map.size() > m_Capacity)
            {
                const xresource::full_guid& OldKey = m_LRU.back(); // least-recent
                auto it = m_Map.find(OldKey);
                if (it != m_Map.end())
                {
                    if (m_Callback) m_Callback(it->first, it->second.m_Value);
                    m_Map.erase(it);
                }
                m_LRU.pop_back();
            }
        }
    };

    void RenderResourceWigzmos(texture_cache_rlu& TextureLRU, bool& bOpen, const xresource::full_guid& PreFullGuid)
    {
        std::string Name;

        RemapGUIDToString(Name, PreFullGuid);

        ImVec4 base = ImGui::GetStyleColorVec4(ImGuiCol_Button);
        base.w = 1;
        base.x *= 0.75f;
        base.y *= 0.75f;
        base.z *= 0.75f;
        ImGui::PushStyleColor(ImGuiCol_Button, base);
        if (PreFullGuid.empty() == false && PreFullGuid.m_Type == xrsc::texture_type_guid_v )
        {
            xrsc::texture_ref Ref;
            bool              bFound = false;
            if (auto pEntry = TextureLRU.find(PreFullGuid); pEntry)
            {
                Ref = *pEntry;
                bFound = true;
            }
            else
            {
                Ref.m_Instance = PreFullGuid.m_Instance;
            }

            ImGui::BeginGroup();
            ImVec2 imageSize;
            imageSize.x = 48;
            imageSize.y = imageSize.x;
            if (auto p = xresource::g_Mgr.getResource(Ref); p )
            {
                ImGui::Image(static_cast<void*>(p), imageSize);
                if (ImGui::BeginItemTooltip())
                {
                    const auto      Size   = p->getTextureDimensions();
                    const float     Ration = Size[1] / static_cast<float>(Size[0]);
                    ImVec2          big(500, 500*Ration);

                    // Optional: clamp to viewport so the tooltip doesn't go off-screen
                    const ImVec2 vp = ImGui::GetMainViewport()->Size;
                    big.x = std::min(big.x, vp.x * 0.75f);
                    big.y = std::min(big.y, vp.y * 0.75f);

                    ImGui::Image(static_cast<void*>(p), big);
                    ImGui::EndTooltip();
                }
            }
            if (bFound==false)
            {
                TextureLRU.InsertOrAssign(PreFullGuid, Ref);
            }

            ImGui::SameLine();

            bOpen = ImGui::Button(Name.c_str(), ImVec2(-1, imageSize.y));
            ImGui::EndGroup();
        }
        else
        {
            bOpen = ImGui::Button(Name.c_str(), ImVec2(-1, 48));
        }
        ImGui::PopStyleColor();
    }

    e10::assert_browser g_AssetBrowserPopup;

    void ResourceBrowserPopup(const void* pUID, bool& Open, xresource::full_guid& Output, std::span<const xresource::type_guid > Filters )
    {
        //
        // Add drag and drop
        //
        if (ImGui::BeginDragDropTarget())
        {
            // This is the drag and drop payload from the asset browser we just duplicated here
            struct drag_and_drop_folder_payload_t
            {
                e10::folder::guid           m_Parent;
                xresource::full_guid        m_Source;
                bool                        m_bSelection;
            };

            const ImGuiPayload* payload = ImGui::GetDragDropPayload();
            if (payload && payload->IsDataType("DESCRIPTOR_GUID"))
            {
                IM_ASSERT(payload->DataSize == sizeof(drag_and_drop_folder_payload_t));
                auto& payload_n = *static_cast<const drag_and_drop_folder_payload_t*>(payload->Data);

                bool bFound = Output.m_Type == payload_n.m_Source.m_Type;
                if (not bFound) for (auto& Type : Filters)
                {
                    if ( payload_n.m_Source.m_Type == Type)
                    {
                        bFound = true;
                        break;
                    }
                }

                if (bFound)
                {
                    if (const ImGuiPayload* accepted = ImGui::AcceptDragDropPayload("DESCRIPTOR_GUID"))
                    {
                        Output = payload_n.m_Source;
                        if (g_AssetBrowserPopup.isVisible())  g_AssetBrowserPopup.ClosePopup();
                        Open = false;
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        //
        // If the g_AsserBrowserPopup it's been used, and we are not the owner then simply return?
        //
        if (g_AssetBrowserPopup.getCurrentID() != nullptr && g_AssetBrowserPopup.getCurrentID() != pUID )
            return;

        //
        // If the user want us to open let us do so... (as long as it is not already visible)
        //
        if (Open && not g_AssetBrowserPopup.isVisible()) 
        {
            g_AssetBrowserPopup.ShowAsPopup(e10::g_LibMgr, pUID, Filters, Output.m_Type);
        }

        //
        // If the asset-browser sended us a new asset let us open and see what we have...
        //
        if (auto SelectedAsset = g_AssetBrowserPopup.getSelectedAsset(); SelectedAsset.empty() == false )
        {
            // Make sure that the type in question is relevant to us
            if (Output.m_Type == SelectedAsset.m_Type)
            {
                Output = SelectedAsset;
            }
            else for (auto& Type : Filters)
            {
                if (SelectedAsset.m_Type == Type)
                {
                    Output = SelectedAsset;
                    break;
                }
            }
        }

        //
        // Let the user know what is the current state of the popup...
        //
        Open = g_AssetBrowserPopup.isVisible();
    }

    xrsc::texture_ref CreateBackgroundTexture(xgpu::device& Device, const xbitmap& Bitmap)
    {
        // Generate a unique ID for it
        xrsc::texture_ref Ref;
        Ref.m_Instance = xresource::guid_generator::Instance64();

        // Create officially the material instance
        auto Texture = std::make_unique<xgpu::texture>();
        if (auto Err = xgpu::tools::bitmap::Create(*Texture, Device, Bitmap); Err)
        {
            assert(false);
            e21::Debugger(xgpu::getErrorMsg(Err));
            std::exit(xgpu::getErrorInt(Err));
        }

        // Register it with the resource manager
        xresource::g_Mgr.RegisterResource(Ref, Texture.release());

        return Ref;
    }
}

int E21_Example()
{
    //create vulkan instance
    xgpu::instance Instance;
    if (auto Err = xgpu::CreateInstance(Instance, { .m_bDebugMode = true, .m_bEnableRenderDoc = true, .m_pLogErrorFunc = e21::Debugger }); Err)
        return xgpu::getErrorInt(Err);

    //create device
    xgpu::device Device;
    if (auto Err = Instance.Create(Device); Err) //render and swapchain
        return xgpu::getErrorInt(Err);

    //create window
    xgpu::window MainWindow;
    if (auto Err = Device.Create(MainWindow, {}); Err)
        return xgpu::getErrorInt(Err);
    //
    //setup vertex descriptor for mesh
    //
    xgpu::vertex_descriptor VertexDescriptor;
    {
        auto Attributes = std::array
        { xgpu::vertex_descriptor::attribute
          { .m_Offset = offsetof(e19::draw_vert, m_X)
          , .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D
          }
        , xgpu::vertex_descriptor::attribute
          { .m_Offset = offsetof(e19::draw_vert, m_U)
          , .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
          }
        , xgpu::vertex_descriptor::attribute
          { .m_Offset = offsetof(e19::draw_vert, m_Color)
          , .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED
          }
        };
        auto Setup = xgpu::vertex_descriptor::setup
        { .m_VertexSize = sizeof(e19::draw_vert)
        , .m_Attributes = Attributes
        };

        if (auto Err = Device.Create(VertexDescriptor, Setup); Err)
            return xgpu::getErrorInt(Err);
    }
    
    //
    //setup 2D vertex descriptor for background
    //
    xgpu::pipeline Pipeline2D;
    {
        xgpu::vertex_descriptor VertexDescriptor2D;
        {
            auto Attributes = std::array
            {
                xgpu::vertex_descriptor::attribute
                { .m_Offset = offsetof(e19::vert_2d, m_X)
                , .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
                }
            ,   xgpu::vertex_descriptor::attribute
                { .m_Offset = offsetof(e19::vert_2d, m_U)
                , .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D
                }
            ,   xgpu::vertex_descriptor::attribute
                { .m_Offset = offsetof(e19::vert_2d, m_Color)
                , .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED
                }
            };
            auto Setup = xgpu::vertex_descriptor::setup
            { .m_VertexSize = sizeof(e19::vert_2d)
            , .m_Attributes = Attributes
            };

            if (auto Err = Device.Create(VertexDescriptor2D, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader FragmentShader2D;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::FRAGMENT
            , .m_Sharer = xgpu::shader::setup::raw_data{ e21::g_2DFragShader }
            };
            if (auto Err = Device.Create(FragmentShader2D, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader VertexShader2D;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::VERTEX
            , .m_Sharer = xgpu::shader::setup::raw_data{e21::g_2DVertShader}
            };

            if (auto Err = Device.Create(VertexShader2D, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        auto Shaders    = std::array<const xgpu::shader*, 2>{ &FragmentShader2D, &VertexShader2D };
        auto Samplers   = std::array{ xgpu::pipeline::sampler{} };
        auto Setup      = xgpu::pipeline::setup
        { .m_VertexDescriptor     = VertexDescriptor2D
        , .m_Shaders              = Shaders
        , .m_PushConstantsSize    = sizeof(e21::push_const2D)
        , .m_Samplers             = Samplers
        , .m_DepthStencil         = {.m_bDepthTestEnable = false }
        };

        if (auto Err = Device.Create(Pipeline2D, Setup); Err)
            return xgpu::getErrorInt(Err);
    }

    //
    // Asset Mgr
    //
    resource_mgr_user_data  ResourceMgrUserData;
    xresource::g_Mgr.Initiallize();
    xmaterial_instance::descriptor Descriptor;

    e10::assert_browser         AsserBrowser;
    e21::selected_descriptor    SelectedDescriptor;
    auto                        CallBackForCompilation = [&](e10::library_mgr& LibMgr, e10::library::guid gLibrary, xresource::full_guid gCompilingEntry, std::shared_ptr<e10::compilation::historical_entry::log>& LogInformation)
    {
        // Filter by our entry...
        if (SelectedDescriptor.m_InfoGUID == gCompilingEntry)
        {
            if (SelectedDescriptor.m_Log.get() != LogInformation.get())
                SelectedDescriptor.m_Log = LogInformation;

            assert(SelectedDescriptor.m_Log);

            //
            // Check if we are done compiling
            //
            e10::compilation::historical_entry::result Results;
            {
                xcontainer::lock::scope lk(*SelectedDescriptor.m_Log);
                Results = SelectedDescriptor.m_Log->get().m_Result;
            }

            // If we are successful we should reload the texture
            if (Results == e10::compilation::historical_entry::result::SUCCESS_WARNINGS || Results == e10::compilation::historical_entry::result::SUCCESS)
            {
                SelectedDescriptor.m_bReload = true;
                SelectedDescriptor.m_bErrors = false;
            }
            else
            {
                SelectedDescriptor.m_bErrors = true;
            }
        }
    };
    e10::g_LibMgr.m_OnCompilationState.Register(CallBackForCompilation);



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
                e21::Debugger(Err.getMessage());
                return 1;
            }

            //
            // Set the path for the resources
            //
            ResourceMgrUserData.m_Device = Device;
            xresource::g_Mgr.setUserData(&ResourceMgrUserData, false);
            xresource::g_Mgr.setRootPath(std::format(L"{}//Cache//Resources//Platforms//Windows", e10::g_LibMgr.m_ProjectPath));
        }
    }

    //
    // Create mesh mgr
    //
    e19::mesh_manager           MeshManager             = {};
    xgpu::pipeline              material                = {};
    xgpu::pipeline_instance     material_instance       = {};
    xgpu::texture               defaulttexture          = {};

    MeshManager.Init(Device);

    e19::mesh_manager::model CurrentModel = e19::mesh_manager::model::CUBE;

    //
    // Create Background material
    //

    xrsc::texture_ref BgTexture = e21::CreateBackgroundTexture(Device, xbitmap::getDefaultBitmap());
    xgpu::pipeline_instance BackGroundMaterialInstance;
    if( auto p = xresource::g_Mgr.getResource(BgTexture); p )
    {
        auto Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{*p} };
        auto setup = xgpu::pipeline_instance::setup
        {
            .m_PipeLine = Pipeline2D
        ,	.m_SamplersBindings = Bindings
        };

        if (auto Err = Device.Create(BackGroundMaterialInstance, setup); Err)
        {
            e21::Debugger(xgpu::getErrorMsg(Err));
            assert(false);
            std::exit(xgpu::getErrorInt(Err));
        }
    }

    //
    // setup Imgui interface
    //
    xgpu::tools::imgui::CreateInstance(MainWindow);

    xgpu::tools::view   View        = {};
    xmath::radian3      Angles      = {};
    float               Distance    = 2;

    View.setFov(60_xdeg);

    //
    //create input devices
    //
    xgpu::mouse         Mouse;
    xgpu::keyboard      Keyboard;
    Instance.Create(Mouse, {});
    Instance.Create(Keyboard, {});

    //
    // Create the inspector window
    //
    e21::texture_cache_rlu TextureLRU( 100, [&](const xresource::full_guid&, xrsc::texture_ref& Ref)
    {
        xresource::g_Mgr.ReleaseRef(Ref);
    });

    xproperty::inspector    Inspector("Material Instance Properties");
    auto                    RenderResourceWigzmosCallBack = [&TextureLRU](xproperty::inspector&, bool& bOpen, const xresource::full_guid& PreFullGuid) { RenderResourceWigzmos(TextureLRU, bOpen, PreFullGuid); };
    Inspector.m_OnResourceWigzmos.m_Delegates.clear();
    Inspector.m_OnResourceBrowser.m_Delegates.clear();
    Inspector.m_OnResourceLeftSize.m_Delegates.clear();

    auto CallbackWhenPropsChanges = [&](xproperty::inspector& Inspector, const xproperty::ui::undo::cmd& Cmd)
    {
        if ( Cmd.m_Name == "material_instance_descriptor/MaterialRef")
        {
            // May need to change the resource descriptor
            auto FullGuid           = Cmd.m_NewValue.get<xresource::full_guid>();
            auto TemplateFileName   = std::format( L"{}/Cache/Resources/Logs/Material/{:02X}/{:02X}/{:X}.log/MaterialInstance.txt"
                                                 , e10::g_LibMgr.m_ProjectPath
                                                 , FullGuid.m_Instance.m_Value & 0xff
                                                 , (FullGuid.m_Instance.m_Value & 0xff00) >> 8
                                                 , FullGuid.m_Instance.m_Value
                                                 );
            xtextfile::stream TextFile;
            if ( auto Err = TextFile.Open( true, TemplateFileName, xtextfile::file_type::TEXT); Err )
            {
                assert(false);
            }
            else
            {
                xproperty::settings::context C;
                auto Pair = Inspector.getComponent(0, 0);
                if( auto Err2 = xproperty::sprop::serializer::Stream( TextFile, Pair.second, *Pair.first, C ); Err2 )
                {
                    assert(false);
                }
                else
                {
                    xmaterial_instance::descriptor* pDesc = static_cast<xmaterial_instance::descriptor*>(Pair.second);
                    pDesc->m_lTextures.resize(pDesc->m_lTextureDefaults.size());

                    // set all the textures to the default values
                    int Index=0;
                    for( auto& E : pDesc->m_lTextureDefaults )
                    {
                        pDesc->m_lTextures[Index++] = pDesc->m_lFinalTextures[ E.m_Index ].m_TextureRef;
                    }
                }
            }
        }
    };
    Inspector.m_OnChangeEvent.Register(CallbackWhenPropsChanges);
    Inspector.m_OnResourceWigzmos.Register(RenderResourceWigzmosCallBack);
    Inspector.m_OnResourceBrowser.Register<[](xproperty::inspector&, const void* pUID, bool& bOpen, xresource::full_guid& Out, std::span<const xresource::type_guid> Filters)
    {
        e21::ResourceBrowserPopup(pUID, bOpen, Out, Filters);
    }>();
    Inspector.m_OnResourceLeftSize.Register<[](xproperty::inspector& Inspector, void* pID, ImGuiTreeNodeFlags flags, const char* pName, bool& Open)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, 18.0f));
        Inspector.RenderBackground();

        // Get the bounding box of the last item (the tree node)
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0.2f));

        std::string NewName;
        if (pName[0] == '[')
        {
            auto                            Pair    = Inspector.getComponent(0, 0);
            auto                            View    = std::string_view(pName);
            xmaterial_instance::descriptor* pDesc   = static_cast<xmaterial_instance::descriptor*>(Pair.second);

            // Change the name to point to the texture...
            if (not pDesc->m_lTextureDefaults.empty())
            {
                // Remove the [ and ]
                View = View.substr(1, View.size() - 2);

                // get index
                int Index;
                auto result = std::from_chars(View.data(), View.data() + View.size(), Index);
                assert(result.ec == std::errc());

                NewName = std::format("{} {}", pName, pDesc->m_lTextureDefaults[Index].m_Name);
                pName = NewName.c_str();

            }
        }

        if (pID)
        {
            Open = ImGui::TreeNodeEx(pID, ImGuiTreeNodeFlags_Framed | flags, "  %s", pName);
        }
        else
        {
            Open = ImGui::TreeNodeEx(pName, flags);
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }>();

    //
    // Main Loop
    //
    while (Instance.ProcessInputEvents())
    {
        if (xgpu::tools::imgui::BeginRendering(true)) continue;

        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("\xEE\x98\xAB Home\xee\xa5\xb2"))
            {
                if (ImGui::MenuItem("Resource Browser", "Ctrl-Space"))
                {
                    // Handle Open action
                    // Opens a popup window and allows you to select a Texture from there
                    // The texture is then loaded and the material is updated
                    // The material is then updated
                    AsserBrowser.Show(true);
                }

                ImGui::Separator();
                {
                    bool bDisableSave = !e10::g_LibMgr.isReadyToSave() && SelectedDescriptor.m_InfoGUID.empty();
                    if (bDisableSave) ImGui::BeginDisabled();
                    if (ImGui::MenuItem("\xEE\x9D\x8E Save ", "Ctrl+S"))
                    {
                        xproperty::settings::context Context;
                        e10::g_LibMgr.Save(Context);

                        if (SelectedDescriptor.m_InfoGUID.empty() == false )
                        {
                            ////g.serialize(g, false, SelectedDescriptor.m_DescriptorPath);
                        }
                    }
                    if (bDisableSave) ImGui::EndDisabled();
                }

            ImGui::EndMenu();
            }

            ImGui::SameLine(410);

            if (false == SelectedDescriptor.m_InfoGUID.empty() )
            {
                ImGui::Separator();
                xcontainer::lock::scope lk(*SelectedDescriptor.m_Log);
                auto& Log = SelectedDescriptor.m_Log->get();

                const bool Disable = Log.m_Result == e10::compilation::historical_entry::result::COMPILING_WARNINGS
                                  || Log.m_Result == e10::compilation::historical_entry::result::COMPILING;
                if (Disable) ImGui::BeginDisabled();
                if (ImGui::Button("\xEF\x96\xB0 Compile "))
                {
                    SelectedDescriptor.SaveDescriptor();
                }
                if (Disable) ImGui::EndDisabled();

                std::uint32_t Color;
                {
                    switch (Log.m_Result)
                    {
                    case e10::compilation::historical_entry::result::COMPILING_WARNINGS:
                        Color = IM_COL32(255, 255, 0, 255);
                        break;
                    case e10::compilation::historical_entry::result::COMPILING:
                        Color = IM_COL32(0, 255, 0, 255);
                        break;
                    case e10::compilation::historical_entry::result::FAILURE:
                        Color = IM_COL32(255, 170, 140, 255);
                        break;
                    case e10::compilation::historical_entry::result::SUCCESS_WARNINGS:
                        Color = IM_COL32(255, 255, 0, 255);
                        break;
                    case e10::compilation::historical_entry::result::SUCCESS:
                        Color = IM_COL32(255, 255, 255, 255);
                        break;
                    }
                }

                ImGui::PushStyleColor(ImGuiCol_Text, Color);
                if (ImGui::Button("Feedback:\xee\xa5\xb2"))
                {
                    // Get button position and size
                    ImVec2 button_pos = ImGui::GetItemRectMin();
                    ImVec2 button_size = ImGui::GetItemRectSize();

                    // Set popup position just below the button
                    ImGui::SetNextWindowPos(ImVec2(button_pos.x, button_pos.y + button_size.y));
                    ImGui::OpenPopup("Feedback");
                }
                ImGui::PopStyleColor();

                if (ImGui::BeginPopup("Feedback"))
                {
                    ImGui::BeginChild("###Feedback-Child", ImVec2(600, 300));
                    ImGui::PushTextWrapPos(600);

                    if (Log.m_Log.empty() == false)
                    {
                        size_t start = 0;
                        while (start < Log.m_Log.length()) 
                        {
                            const std::string_view view{ Log.m_Log };
                            size_t end = view.find('\n', start);
                            if (end == std::string_view::npos) end = view.size();
                            std::string_view line = view.substr(start, end - start);

                            if (xstrtool::findI( line, "ERROR:") != std::string::npos)
                            {
                                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
                                ImGui::TextUnformatted(std::string(line).c_str());
                                ImGui::PopStyleColor();

                                std::string nodeName = e21::ExtractNodeName(line);
                                if (!nodeName.empty() && nodeName != "Final FragOut")
                                {
                                    /*
                                    for (auto& [id, node] : g.m_InstanceNodes)
                                    {
                                        auto& n = *node;
                                        if (std::to_string(n.m_Guid.m_Value) == nodeName)
                                        {
                                            n.m_HasErrMsg = true;
                                            n.m_ErrMsg    = line;
                                        }
                                    }
                                    */
                                }
                            }
                            else
                            {
                                ImGui::TextUnformatted(std::string(line).c_str());
                            }

                            start = end + 1;
                        }
                    }
                    ImGui::PopTextWrapPos();
                    ImGui::EndChild();
                    ImGui::EndPopup();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gives information about the compilation process");

                if (Log.m_Log.empty() == false)
                {
                    ImGui::Text("%s", std::string(xstrtool::getLastLine(Log.m_Log)).c_str());
                }
            }

            ImGui::EndMainMenuBar();
        }

        //
        // Re-load material if we have to
        //
        if (SelectedDescriptor.m_bReload)
        {
            SelectedDescriptor.m_bReload = false;
            PipelineReload( Device, VertexDescriptor, material, material_instance, SelectedDescriptor, xresource::g_Mgr);
        }

        //
        // Preview window
        //
        {
            ImGui::Begin("Material Instance Preview");

            //
            // render background
            //
            xgpu::tools::imgui::AddCustomRenderCallback([&](const ImVec2& windowPos, const ImVec2& windowSize)
            {
                auto CmdBufferRef = MainWindow.getCmdBuffer();

                {
                    e21::push_const2D pc;
                    pc.m_Scale =
                    { (150 * 2.0f) / windowSize.x
                    , (150 * 2.0f) / windowSize.y
                    };
                    pc.m_Translation.setup(0);
                    pc.m_UVScale = { 100.0f,100.0f };

                    CmdBufferRef.setPipelineInstance(BackGroundMaterialInstance);
                    CmdBufferRef.setPushConstants(pc);
                    MeshManager.Rendering(CmdBufferRef, e19::mesh_manager::model::PLANE2D);
                }
            });

            //
            // Camera controls
            //
            if (ImGui::IsWindowHovered())
            {
                if (Mouse.isPressed(xgpu::mouse::digital::BTN_RIGHT))
                {
                    auto MousePos = Mouse.getValue(xgpu::mouse::analog::POS_REL);
                    Angles.m_Pitch.m_Value -= 0.01f * MousePos[1];
                    Angles.m_Yaw.m_Value   -= 0.01f * MousePos[0];
                }

                // zoom
                Distance += Distance * -0.02f * Mouse.getValue(xgpu::mouse::analog::WHEEL_REL)[0];
            }

            //
            // Render mesh
            //
            if (material_instance.m_Private) xgpu::tools::imgui::AddCustomRenderCallback([&](const ImVec2& windowPos, const ImVec2& windowSize)
            {
                auto CmdBufferRef = MainWindow.getCmdBuffer();

                View.setViewport({ static_cast<int>(windowPos.x)
                                 , static_cast<int>(windowPos.y)
                                 , static_cast<int>(windowPos.x + windowSize.x)
                                 , static_cast<int>(windowPos.y + windowSize.y)
                                });

                // Help compute the distance to keep the object inside the view port
                const float vertical_fov    = View.getFov().m_Value;
                const float aspect          = View.getAspect();
                const float radius          = 0.5f;
                const float hfov            = 2.0f * std::atan(aspect * std::tan(vertical_fov / 2.0f));
                const float min_fov         = std::min(vertical_fov, hfov);
                const float distance        = radius / std::tan(min_fov / 2.0f);

                // Update the camera
                View.LookAt(Distance + distance - 1, Angles, { 0,0,0 });

                e21::push_constants pushConst;
                pushConst.m_L2C = (View.getW2C() * xmath::fmat4::fromScale({ 2.f }));

                // Set pipeline and push constants
                CmdBufferRef.setPipelineInstance(material_instance);
                CmdBufferRef.setPushConstants(pushConst);

                // Render the mesh
                MeshManager.Rendering(CmdBufferRef, CurrentModel);
            });

            //
            // mesh selection
            //
            static int selected = 0;
            const char* meshSelection[] = { "Cube", "Sphere","Capsule","Cylinder" };
            e21::MeshPreviewStyle();
            if (ImGui::Button("\xEE\xAF\x92 Meshes"))
            {
                ImVec2 button_pos = ImGui::GetItemRectMin();
                ImVec2 button_size = ImGui::GetItemRectSize();
                ImGui::SetNextWindowPos(ImVec2(button_pos.x, button_pos.y + button_size.y));
                ImGui::OpenPopup("Meshes");
            }

            if (ImGui::BeginPopup("Meshes"))
            {
                for (int n = 0; n < IM_ARRAYSIZE(meshSelection); n++)
                {
                    bool is_selected = (selected == n);
                    if (ImGui::Selectable(meshSelection[n], is_selected))
                    {
                        selected = n;
                        CurrentModel = static_cast<e19::mesh_manager::model>(n);
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndPopup();
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(6);

            //
            // Background texture selection
            //
            e21::MeshPreviewStyle();
            ImGui::SameLine();
            if (ImGui::Button("\xEE\xBC\x9F BackGround"))
            {
                ImVec2 button_pos = ImGui::GetItemRectMin();
                ImVec2 button_size = ImGui::GetItemRectSize();
                ImGui::SetNextWindowPos(ImVec2(button_pos.x, button_pos.y + button_size.y));
                ImGui::OpenPopup("BackGround");
            }

            if (ImGui::BeginPopup("BackGround"))
            {
                /*
                for (int i = 0; i < texturemgr.m_Names.size(); ++i)
                {
                    bool selected = (texturemgr.m_CurrentBGIndex == i);
                    if (ImGui::Selectable(texturemgr.m_Names[i].c_str(), selected))
                    {
                        texturemgr.m_CurrentBGIndex = i;

                        // Update binding immediately
                        xgpu::texture* curTex = texturemgr.GetCurrentBGTexture();
                        auto Bindings = (curTex)
                            ? std::array{ xgpu::pipeline_instance::sampler_binding{*curTex} }
                        : std::array{ xgpu::pipeline_instance::sampler_binding{BgTexture} }; // fallback default checkerboard

                        //need to destroy material_instance if not complain
                        Device.Destroy(std::move(BackGroundMaterialInstance));
                        if (auto Err = Device.Create(BackGroundMaterialInstance, { .m_PipeLine = Pipeline2D, .m_SamplersBindings = Bindings }); Err)
                        {
                            assert(false);
                            exit(xgpu::getErrorInt(Err));
                        }
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                */
                ImGui::EndPopup();
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(6);


            ImGui::End();
        }
        
        //
        // Render the Inspector when node is selected
        //
        /*
        if (nSelected != 1)
        {
            Inspector.clear();
        }
        else if (LastSelectedNode != SelectedNodes[0])
        {
            LastSelectedNode = SelectedNodes[0];
            Inspector.clear();
            Inspector.AppendEntity();
            
            auto& Entry = *g.m_InstanceNodes[xmaterial_compiler::node_guid{ LastSelectedNode.Get() }];
            
            Inspector.AppendEntityComponent(*xproperty::getObject(Entry), &Entry);
        }
        else if(g.m_InstanceNodes.find(xmaterial_compiler::node_guid{ LastSelectedNode.Get() }) == g.m_InstanceNodes.end())
        {
            Inspector.clear();
        }
        */

        xproperty::settings::context Context;
        Inspector.Show(Context, [&] {});

        //
        // Show a texture selector in IMGUI
        //
        AsserBrowser.Render(e10::g_LibMgr, xresource::g_Mgr);

        // We let the asset browser to decide if it needs to show or not
        e21::g_AssetBrowserPopup.RenderAsPopup( e10::g_LibMgr, xresource::g_Mgr);

        if (auto NewAsset = AsserBrowser.getNewAsset(); NewAsset.empty() == false && NewAsset.m_Type == xrsc::geom_type_guid_v)
        {
            // Generate the paths
            e10::g_LibMgr.getNodeInfo(SelectedDescriptor.m_LibraryGUID, SelectedDescriptor.m_InfoGUID, [&](e10::library_db::info_node& NodeInfo)
            {
                SelectedDescriptor.GeneratePaths(NodeInfo.m_Path);
            });
        }
        else if (auto SelectedAsset = AsserBrowser.getSelectedAsset(); SelectedAsset.empty() == false && SelectedAsset.m_Type == xrsc::geom_type_guid_v)
        {
            auto xxx = xrsc::material_instance_type_guid_v;

            SelectedDescriptor.clear();
            SelectedDescriptor.m_pDescriptor = xresource_pipeline::factory_base::Find(std::string_view{ "Geom" })->CreateDescriptor();
            SelectedDescriptor.m_LibraryGUID = AsserBrowser.getSelectedLibrary();
            SelectedDescriptor.m_InfoGUID    = SelectedAsset;

            // Generate the paths
            e10::g_LibMgr.getNodeInfo(SelectedDescriptor.m_LibraryGUID, SelectedDescriptor.m_InfoGUID, [&](e10::library_db::info_node& NodeInfo)
            {
                SelectedDescriptor.GeneratePaths(NodeInfo.m_Path);
            });

            // If it is a new resource it probably does not have a descriptor yet...
            if (std::filesystem::exists(SelectedDescriptor.m_DescriptorPath))
            {
                xproperty::settings::context Context;
                if (auto Err = SelectedDescriptor.m_pDescriptor->Serialize(true, SelectedDescriptor.m_DescriptorPath, Context); Err)
                {
                    assert(false);
                }
            }

            // Set the descriptor with the inspector
            Inspector.clear();
            Inspector.AppendEntity();
            Inspector.AppendEntityComponent(*SelectedDescriptor.m_pDescriptor->getProperties(), SelectedDescriptor.m_pDescriptor.get());

            // Tell the system if we should be loading the sprv
            SelectedDescriptor.m_bReload = std::filesystem::exists(SelectedDescriptor.m_ResourcePath);
        }

        xgpu::tools::imgui::Render();

        MainWindow.PageFlip();

        // Let the resource manager know we have changed the frame
        xresource::g_Mgr.OnEndFrameDelegate();
    }

    xgpu::tools::imgui::Shutdown();

    return 0;
}