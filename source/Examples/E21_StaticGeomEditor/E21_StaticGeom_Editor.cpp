#include "dependencies/xproperty/source/xcore/my_properties.h"
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

#include "plugins/xgeom_static.plugin/source/xgeom_static_details.h"
#include "plugins/xgeom_static.plugin/source/xgeom_static_descriptor.h"
#include "plugins/xgeom_static.plugin/source/xgeom_static_xgpu_rsc_loader.h"
#include "plugins/xgeom_static.plugin/source/xgeom_static_xgpu_rsc_loader.cpp"

#include "imgui_internal.h"

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

    constexpr static std::uint32_t g_GeomStaticFragShader[] =
    {
        #include "GeomStaticBasicShader_frag.h"
    };

    constexpr static std::uint32_t g_GeomStaticVertShader[] =
    {
        #include "GeomStaticBasicShader_vert.h"
    };

    constexpr static std::uint32_t g_GridVertShader[] = 
    {
        #include "E21_GridShader_vert.h"
    };

    constexpr static std::uint32_t g_GridFragShader[] =
    {
        #include "E21_GridShader_frag.h"
    };

    constexpr static std::uint32_t g_WireframeFragShader[] =
    {
        #include "E21_WireFrame_frag.h"
    };

    constexpr static std::uint32_t g_WireframeVertShader[] =
    {
        #include "E21_WireFrame_vert.h"
    };

    constexpr static std::uint32_t g_WireframeGeomShader[] =
    {
        #include "E21_WireFrame_geom.h"
    };

    constexpr static std::uint32_t g_DebugNormalFragShader[] =
    {
        #include "E21_DebugNormalRender_frag.h"
    };

    constexpr static std::uint32_t g_DebugNormalVertShader[] =
    {
        #include "E21_DebugNormalRender_vert.h"
    };

    constexpr static std::uint32_t g_DebugNormalGeomShader[] =
    {
        #include "E21_DebugNormalRender_geom.h"
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

        xrsc::geom_static                                           m_GeomRef               = {};
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

    void PipelineReload( xgpu::device& Device, xgpu::pipeline& material, xgpu::pipeline_instance& materialInst, selected_descriptor& SelcDesc, xresource::mgr& Mgr )
    {
        Mgr.ReleaseRef(SelcDesc.m_GeomRef);
        SelcDesc.m_GeomRef.m_Instance = SelcDesc.m_InfoGUID.m_Instance;
        if (auto pGeomStatic = Mgr.getResource(SelcDesc.m_GeomRef); pGeomStatic)
        {
            int a = 0;

        }
        else
        {
            assert(false);
        }

        /*
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
        */
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
    struct alignas(256) ubo_geom_static_mesh
    {
        xmath::fmat4  m_L2w;
        xmath::fmat4  m_w2C;
        xmath::fmat4  m_L2CShadow;
        xmath::fvec4  m_LightColor;
        xmath::fvec4  m_AmbientLightColor;
        xmath::fvec4  m_wSpaceLightPos;
        xmath::fvec4  m_wSpaceEyePos;
    };

    //
    //setup vertex descriptor for mesh
    //
    xgpu::vertex_descriptor Primitive3DVertexDescriptor;
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

        if (auto Err = Device.Create(Primitive3DVertexDescriptor, Setup); Err)
            return xgpu::getErrorInt(Err);
    }


    //
    // Material of all static geometry
    //
    xgpu::buffer StaticGeomDynamicUBOMesh;
    if (auto Err = Device.Create(StaticGeomDynamicUBOMesh, { .m_Type = xgpu::buffer::type::UNIFORM, .m_Usage = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ, .m_EntryByteSize = sizeof(ubo_geom_static_mesh), .m_EntryCount = 100 }); Err)
        return xgpu::getErrorInt(Err);

    struct static_geom_push_const
    {
        std::uint32_t       m_ClusterIndex;         // Which cluster we are going to be using
    };

    xgpu::pipeline Pipeline3D;
    {
        //
        // These registration should be factor-out
        // 
        xgpu::vertex_descriptor GeomStaticVertexDescriptor;
        {
            auto Attributes = std::array
            {
                xgpu::vertex_descriptor::attribute
                { .m_Offset  = offsetof(xgeom_static::geom::vertex, m_XPos)
                , .m_Format  = xgpu::vertex_descriptor::format::SINT16_4D           // Position + extras
                , .m_iStream = 0
                }
                , xgpu::vertex_descriptor::attribute
                { .m_Offset  = offsetof(xgeom_static::geom::vertex_extras, m_UV)
                , .m_Format  = xgpu::vertex_descriptor::format::UINT16_2D           // UV
                , .m_iStream = 1
                }
                , xgpu::vertex_descriptor::attribute
                { .m_Offset  = offsetof(xgeom_static::geom::vertex_extras, m_OctNormal)
                , .m_Format  = xgpu::vertex_descriptor::format::UINT8_4D           // oct normal + tangent
                , .m_iStream = 1
                }
            };
            if (auto Err = Device.Create(GeomStaticVertexDescriptor
            , xgpu::vertex_descriptor::setup
            { .m_bUseStreaming  = true
            , .m_Topology       = xgpu::vertex_descriptor::topology::TRIANGLE_LIST
            , .m_VertexSize     = 0
            , .m_Attributes     = Attributes
            }); Err )
            {
                assert(false);
            }
        }

        xgpu::shader FragmentShader3D;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::FRAGMENT
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e21::g_GeomStaticFragShader, sizeof(e21::g_GeomStaticFragShader) / sizeof(int)}}
            };
            if (auto Err = Device.Create(FragmentShader3D, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader VertexShader3D;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::VERTEX
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e21::g_GeomStaticVertShader, sizeof(e21::g_GeomStaticVertShader) / sizeof(int)}}
            };

            if (auto Err = Device.Create(VertexShader3D, Setup); Err)
                return xgpu::getErrorInt(Err);
        }

        auto Shaders    = std::array<const xgpu::shader*, 2>{ &FragmentShader3D, &VertexShader3D };
        auto Samplers   = std::array{ xgpu::pipeline::sampler{.m_AddressMode = std::array{ xgpu::pipeline::sampler::address_mode::CLAMP // Shadowmap
                                                                                         , xgpu::pipeline::sampler::address_mode::CLAMP
                                                                                         , xgpu::pipeline::sampler::address_mode::CLAMP
                                                                                         }}         
                                    , xgpu::pipeline::sampler{}         // Albedo
                                    };
        auto UBuffersUsage = std::array { xgpu::pipeline::uniform_binds { .m_BindIndex  = 0         // MeshUniforms, bind 0 set 2, changes per mesh/draw call
                                                                        , .m_Usage      = { .m_bVertex = true, .m_bFragment = true }
                                                                        , .m_Type       = xgpu::pipeline::uniform_binds::type::UBO_DYNAMIC      // MUST be dynamic (per object)
                                                                        }      
                                        , xgpu::pipeline::uniform_binds { .m_BindIndex  = 0         // ClusterUniforms clusters[], bind 0 set 1, never changes
                                                                        , .m_Usage      = xgpu::shader::type{xgpu::shader::type::bit::VERTEX}
                                                                        , .m_Type       = xgpu::pipeline::uniform_binds::type::SSBO_STATIC     // Static = big array + push_constant index
                                                                        }      
                                        };
        auto Setup      = xgpu::pipeline::setup
        { .m_VertexDescriptor   = GeomStaticVertexDescriptor
        , .m_Shaders            = Shaders
        , .m_PushConstantsSize  = sizeof(static_geom_push_const)
        , .m_UniformBinds       = UBuffersUsage
        , .m_Samplers           = Samplers
        };

        if (auto Err = Device.Create(Pipeline3D, Setup); Err)
        {
            assert(false);
            return xgpu::getErrorInt(Err);
        }
    }

    //
    // Material of all static geometry
    //
    struct alignas(256) debug_normal_ubo_entry
    {
        xmath::fmat4 m_L2C;       
        xmath::fvec4 m_ScaleFactor;
    };

    xgpu::buffer DebugNormalDynamicUBOMesh;
    if (auto Err = Device.Create(DebugNormalDynamicUBOMesh, { .m_Type = xgpu::buffer::type::UNIFORM, .m_Usage = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ, .m_EntryByteSize = sizeof(debug_normal_ubo_entry), .m_EntryCount = 100 }); Err)
        return xgpu::getErrorInt(Err);

    struct debug_normal_push_const
    {
        std::uint32_t       m_ClusterIndex;         // Which cluster we are going to be using
    };

    xgpu::pipeline_instance DebugNormalPipeLineInstance;
    {
        //
        // These registration should be factor-out
        // 
        xgpu::vertex_descriptor GeomStaticVertexDescriptor;
        {
            auto Attributes = std::array
            {
                xgpu::vertex_descriptor::attribute
                { .m_Offset  = offsetof(xgeom_static::geom::vertex, m_XPos)
                , .m_Format  = xgpu::vertex_descriptor::format::SINT16_4D           // Position + extras
                , .m_iStream = 0
                }
                , xgpu::vertex_descriptor::attribute
                { .m_Offset  = offsetof(xgeom_static::geom::vertex_extras, m_UV)
                , .m_Format  = xgpu::vertex_descriptor::format::UINT16_2D           // UV
                , .m_iStream = 1
                }
                , xgpu::vertex_descriptor::attribute
                { .m_Offset  = offsetof(xgeom_static::geom::vertex_extras, m_OctNormal)
                , .m_Format  = xgpu::vertex_descriptor::format::UINT8_4D           // oct normal + tangent
                , .m_iStream = 1
                }
            };
            if (auto Err = Device.Create(GeomStaticVertexDescriptor
            , xgpu::vertex_descriptor::setup
            { .m_bUseStreaming  = true
            , .m_Topology       = xgpu::vertex_descriptor::topology::TRIANGLE_LIST
            , .m_VertexSize     = 0
            , .m_Attributes     = Attributes
            }); Err )
            {
                assert(false);
            }
        }

        xgpu::shader FragmentShader3D;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::FRAGMENT
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e21::g_DebugNormalFragShader, sizeof(e21::g_DebugNormalFragShader) / sizeof(int)}}
            };
            if (auto Err = Device.Create(FragmentShader3D, Setup); Err)
            {
                assert(false);
                return xgpu::getErrorInt(Err);
            }
        }

        xgpu::shader VertexShader3D;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::VERTEX
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e21::g_DebugNormalVertShader, sizeof(e21::g_DebugNormalVertShader) / sizeof(int)}}
            };

            if (auto Err = Device.Create(VertexShader3D, Setup); Err)
            {
                assert(false);
                return xgpu::getErrorInt(Err);
            }
        }

        xgpu::shader GeomShader3D;
        {
            xgpu::shader::setup Setup
            { .m_Type = xgpu::shader::type::bit::GEOMETRY
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e21::g_DebugNormalGeomShader, sizeof(e21::g_DebugNormalGeomShader) / sizeof(int)}}
            };

            if (auto Err = Device.Create(GeomShader3D, Setup); Err)
            {
                assert(false);
                return xgpu::getErrorInt(Err);
            }
        }

        auto Shaders    = std::array<const xgpu::shader*, 3>{ &FragmentShader3D, &VertexShader3D, &GeomShader3D };
        auto UBuffersUsage = std::array { xgpu::pipeline::uniform_binds { .m_BindIndex  = 0         // MeshUniforms, bind 0 set 2, changes per mesh/draw call
                                                                        , .m_Usage      = { .m_bVertex = true }
                                                                        , .m_Type       = xgpu::pipeline::uniform_binds::type::UBO_DYNAMIC      // MUST be dynamic (per object)
                                                                        }      
                                        , xgpu::pipeline::uniform_binds { .m_BindIndex  = 0         // ClusterUniforms clusters[], bind 0 set 1, never changes
                                                                        , .m_Usage      = xgpu::shader::type{xgpu::shader::type::bit::VERTEX}
                                                                        , .m_Type       = xgpu::pipeline::uniform_binds::type::SSBO_STATIC     // Static = big array + push_constant index
                                                                        }      
                                        };
        auto Setup      = xgpu::pipeline::setup
        { .m_VertexDescriptor   = GeomStaticVertexDescriptor
        , .m_Shaders            = Shaders
        , .m_PushConstantsSize  = sizeof(debug_normal_push_const)
        , .m_UniformBinds       = UBuffersUsage
        , .m_Blend              = xgpu::pipeline::blend::getAlphaOriginal()
        };

        xgpu::pipeline DebugNormalPipeline3D;
        if (auto Err = Device.Create(DebugNormalPipeline3D, Setup); Err)
        {
            assert(false);
            return xgpu::getErrorInt(Err);
        }

        if (auto Err = Device.Create(DebugNormalPipeLineInstance, { .m_PipeLine = DebugNormalPipeline3D }); Err)
        {
            assert(false);
            return xgpu::getErrorInt(Err);
        }
    }


    //
    // Shadow map creation vertex descriptor
    //
    xgpu::vertex_descriptor ShadowmapCreationVertexDescriptor;
    auto Attributes = std::array
    {
        xgpu::vertex_descriptor::attribute
        { .m_Offset = offsetof(xgeom_static::geom::vertex, m_XPos)
        , .m_Format = xgpu::vertex_descriptor::format::SINT16_4D           // Position + extras
        }
    };
    if (auto Err = Device.Create(ShadowmapCreationVertexDescriptor
        , xgpu::vertex_descriptor::setup
        { .m_Topology   = xgpu::vertex_descriptor::topology::TRIANGLE_LIST
        , .m_VertexSize = sizeof(xgeom_static::geom::vertex)
        , .m_Attributes = Attributes
        }); Err)
    {
        assert(false);
    }

    //
    // Shadow Generation Pipeline Instance
    //  
    struct shadow_generation_push_constants
    {
        xmath::fmat4    m_L2C;                      // To Shadow Space
        std::uint32_t   m_ClusterIndex;
    };

    xgpu::pipeline_instance ShadowGenerationPipeLineInstance;
    {
        xgpu::shader FragmentShader;
        {
            auto RawData = xgpu::shader::setup::raw_data
            { std::array {
                #include "GeomStaticShadowMapCreation_frag.h" 
            } };

            if (auto Err = Device.Create(FragmentShader, { .m_Type = xgpu::shader::type::bit::FRAGMENT, .m_Sharer = RawData }); Err)
                return xgpu::getErrorInt(Err);
        }

        xgpu::shader VertexShader;
        {
            auto RawData = xgpu::shader::setup::raw_data
            { std::array {
                #include "GeomStaticShadowMapCreation_vert.h"
            } };

            if (auto Err = Device.Create(VertexShader, { .m_Type = xgpu::shader::type::bit::VERTEX, .m_Sharer = RawData }); Err)
                return xgpu::getErrorInt(Err);
        }

        auto UBuffersUsage = std::array { xgpu::pipeline::uniform_binds { .m_BindIndex  = 0         // ClusterUniforms clusters[], bind 0 set 1, never changes
                                                                        , .m_Usage      = xgpu::shader::type{xgpu::shader::type::bit::VERTEX}
                                                                        , .m_Type       = xgpu::pipeline::uniform_binds::type::SSBO_STATIC     // Static = big array + push_constant index
                                                                        }      
                                        };

        auto Shaders = std::array<const xgpu::shader*, 2>{ &VertexShader, &FragmentShader };
        auto Setup   = xgpu::pipeline::setup
        { .m_VertexDescriptor   = ShadowmapCreationVertexDescriptor
        , .m_Shaders            = Shaders
        , .m_PushConstantsSize  = sizeof(shadow_generation_push_constants)
        , .m_UniformBinds       = UBuffersUsage
        , .m_Primitive          = {} //{ .m_FrontFace = xgpu::pipeline::primitive::front_face::CLOCKWISE } // When rendering shadows we don't want the front faces we want the back faces (Light leaking can happen)
        , .m_DepthStencil       = { .m_DepthBiasConstantFactor  = 2.00f       // Depth bias (and slope) are used to avoid shadowing artifacts (always applied)
                                  , .m_DepthBiasSlopeFactor     = 4.0f        // Slope depth bias factor, applied depending on polygon's slope
                                  , .m_bDepthBiasEnable         = true        // Enable the depth bias
                                  , .m_bDepthClampEnable        = true        // Depth clamp to avoid near plane clipping
                                  }
        };

        xgpu::pipeline ShadowGenerationPipeLine;
        if (auto Err = Device.Create(ShadowGenerationPipeLine, Setup); Err)
        {
            assert(false);
            return xgpu::getErrorInt(Err);
        }

        if (auto Err = Device.Create(ShadowGenerationPipeLineInstance, { .m_PipeLine = ShadowGenerationPipeLine }); Err)
        {
            assert(false);
            return xgpu::getErrorInt(Err);
        }
    }

    //
    // Generate Shadow map render pass
    //
    xgpu::renderpass RenderPass;
    xgpu::texture    ShadowMapTexture;
    {
        if (auto Err = Device.Create(ShadowMapTexture, { .m_Format = xgpu::texture::format::DEPTH_U16, .m_Width = 1024, .m_Height = 1024, .m_isGamma = false }); Err)
        {
            assert(false);
            return xgpu::getErrorInt(Err);
        }

        std::array<xgpu::renderpass::attachment, 1> Attachments
        {
            ShadowMapTexture
        };

        if (auto Err = Device.Create(RenderPass, { .m_Attachments = Attachments }); Err)
        {
            assert(false);
            return xgpu::getErrorInt(Err);
        }
    }

    //
    // Wire frame material
    //
    struct alignas(256) wireframe_UBO_geom_static_mesh
    {
        xmath::fmat4  m_L2w;
        xmath::fmat4  m_w2C;
    };

    xgpu::buffer WireFrameDynamicUBOMesh;
    if (auto Err = Device.Create(WireFrameDynamicUBOMesh, { .m_Type = xgpu::buffer::type::UNIFORM, .m_Usage = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ, .m_EntryByteSize = sizeof(wireframe_UBO_geom_static_mesh), .m_EntryCount = 100 }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::pipeline_instance WireFramePipeLineInstance3D;
    xgpu::pipeline          WireFramePipeline3D;
    {
        xgpu::shader FragmentShader3D;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::FRAGMENT
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e21::g_WireframeFragShader, sizeof(e21::g_WireframeFragShader) / sizeof(int)}}
            };
            if (auto Err = Device.Create(FragmentShader3D, Setup); Err)
            {
                assert(false);
                return xgpu::getErrorInt(Err);
            }
        }

        xgpu::shader VertexShader3D;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::VERTEX
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e21::g_WireframeVertShader, sizeof(e21::g_WireframeVertShader) / sizeof(int)}}
            };

            if (auto Err = Device.Create(VertexShader3D, Setup); Err)
            {
                assert(false);
                return xgpu::getErrorInt(Err);
            }
        }

        xgpu::shader GeomShader3D;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::GEOMETRY
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e21::g_WireframeGeomShader, sizeof(e21::g_WireframeGeomShader) / sizeof(int)}}
            };

            if (auto Err = Device.Create(GeomShader3D, Setup); Err)
            {
                assert(false);
                return xgpu::getErrorInt(Err);
            }
        }

        auto Shaders       = std::array<const xgpu::shader*, 3>{ &FragmentShader3D, &GeomShader3D, &VertexShader3D };
        auto UBuffersUsage = std::array { xgpu::pipeline::uniform_binds { .m_BindIndex  = 0         // MeshUniforms, bind 0 set 2, changes per mesh/draw call
                                                                        , .m_Usage      = { .m_bVertex = true }
                                                                        , .m_Type       = xgpu::pipeline::uniform_binds::type::UBO_DYNAMIC      // MUST be dynamic (per object)
                                                                        }      
                                        , xgpu::pipeline::uniform_binds { .m_BindIndex  = 0         // ClusterUniforms clusters[], bind 0 set 1, never changes
                                                                        , .m_Usage      = xgpu::shader::type{xgpu::shader::type::bit::VERTEX}
                                                                        , .m_Type       = xgpu::pipeline::uniform_binds::type::SSBO_STATIC     // Static = big array + push_constant index
                                                                        }      
                                        };
        auto Setup      = xgpu::pipeline::setup
        { .m_VertexDescriptor   = ShadowmapCreationVertexDescriptor
        , .m_Shaders            = Shaders
        , .m_PushConstantsSize  = sizeof(static_geom_push_const)
        , .m_UniformBinds       = UBuffersUsage
        , .m_Blend              = xgpu::pipeline::blend::getAlphaOriginal()
        };

        if (auto Err = Device.Create(WireFramePipeline3D, Setup); Err)
        {
            assert(false);
            return xgpu::getErrorInt(Err);
        }

        if (auto Err = Device.Create(WireFramePipeLineInstance3D, { .m_PipeLine = WireFramePipeline3D }); Err)
        {
            assert(false);
            return xgpu::getErrorInt(Err);
        }
    }



    //
    // Grid Materials
    //
    struct alignas(256) grid_uniform
    {
        xmath::fmat4    m_L2W;                                                              // Identity matrix for local-to-world
        xmath::fmat4    m_W2C;                                                              // Identity matrix for world-to-clip (adjust based on camera projection)
        xmath::fmat4    m_L2CTShadow;                                                       // Local to shadow texture
        xmath::fvec3    m_WorldSpaceCameraPos = xmath::fvec3(0.0f, 10.0f, 0.0f);            // Typical overhead camera position
        float           m_MajorGridDiv = 10.0f;                                             // 10 major divisions
    };

    xgpu::buffer GridDynamicUBO;
    if (auto Err = Device.Create(GridDynamicUBO, { .m_Type = xgpu::buffer::type::UNIFORM, .m_Usage = xgpu::buffer::setup::usage::CPU_WRITE_GPU_READ, .m_EntryByteSize = sizeof(grid_uniform), .m_EntryCount = 10 }); Err)
        return xgpu::getErrorInt(Err);

    xgpu::pipeline          Grid3dMaterial;
    xgpu::pipeline_instance Grid3dMaterialInstance;
    {
        xgpu::shader VertexShader;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::VERTEX
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e21::g_GridVertShader, sizeof(e21::g_GridVertShader) / sizeof(int)}}
            };
            if (auto Err = Device.Create(VertexShader, Setup); Err)
            {
                assert(false);
                exit(xgpu::getErrorInt(Err));
            }
        }

        xgpu::shader FragShader;
        {
            xgpu::shader::setup Setup
            { .m_Type   = xgpu::shader::type::bit::FRAGMENT
            , .m_Sharer = xgpu::shader::setup::raw_data{std::span{ (std::int32_t*)e21::g_GridFragShader, sizeof(e21::g_GridFragShader) / sizeof(int)}}
            };
            if (auto Err = Device.Create(FragShader, Setup); Err)
            {
                assert(false);
                exit(xgpu::getErrorInt(Err));
            }
        }

        auto UBuffersUsage = std::array{ xgpu::pipeline::uniform_binds {  .m_BindIndex  = 0         // ClusterUniforms clusters[], bind 0 set 2, never changes
                                                                        , .m_Usage      = { .m_bVertex = true, .m_bFragment = true }
                                                                        , .m_Type       = xgpu::pipeline::uniform_binds::type::UBO_DYNAMIC  
                                                                       }
                                       };

        // grid mat
        {
            auto Samplers = std::array{ xgpu::pipeline::sampler{.m_AddressMode = std::array{ xgpu::pipeline::sampler::address_mode::CLAMP, xgpu::pipeline::sampler::address_mode::CLAMP, xgpu::pipeline::sampler::address_mode::CLAMP}}         // Shadowmap
            };

            auto Shaders = std::array<const xgpu::shader*, 2>{ &FragShader, &VertexShader };
            auto Setup = xgpu::pipeline::setup
            { .m_VertexDescriptor   = Primitive3DVertexDescriptor
            , .m_Shaders            = Shaders
            , .m_UniformBinds       = UBuffersUsage
            , .m_Samplers           = Samplers
            , .m_Blend              = xgpu::pipeline::blend::getAlphaOriginal()
            };

            if (auto Err = Device.Create(Grid3dMaterial, Setup); Err)
            {
                assert(false);
                exit(xgpu::getErrorInt(Err));
            }
        }

        // grid mat instance
        {
            auto Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{ShadowMapTexture} };
            auto setup = xgpu::pipeline_instance::setup
            { .m_PipeLine           = Grid3dMaterial
            , .m_SamplersBindings   = Bindings
            };

            if (auto Err = Device.Create(Grid3dMaterialInstance, setup); Err)
            {
                e21::Debugger(xgpu::getErrorMsg(Err));
                assert(false);
                std::exit(xgpu::getErrorInt(Err));
            }
        }
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
    xmaterial_instance::descriptor  Descriptor;
    xgeom_static::details           GeomStaticDetails;

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
    xgpu::pipeline_instance BackGroundMaterialInstance;
    xrsc::texture_ref       DefaultTextureRef = e21::CreateBackgroundTexture(Device, xbitmap::getDefaultBitmap());
    if( auto p = xresource::g_Mgr.getResource(DefaultTextureRef); p )
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
    // Mesh render pipelines
    //
    std::vector<xrsc::material_instance_ref>    Mesh3DRscRefMaterialInstance;
    std::vector<xgpu::pipeline_instance>        Mesh3DMatInstance;

    //
    // setup Imgui interface
    //
    xgpu::tools::imgui::CreateInstance(MainWindow);


    //
    // Set the project path
    //
    {
        TCHAR szFileName[MAX_PATH];
        GetModuleFileName(NULL, szFileName, MAX_PATH);

        std::wcout << L"Full path: " << szFileName << L"\n";
        if (auto I = xstrtool::findI(std::wstring{ szFileName }, { L"\\xGPU\\" }); I != std::string::npos)
        {
            I += 5; // Skip the xGPU part
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
    // Setup all the views...
    //
    xgpu::tools::view   View        = {};
    xmath::radian3      Angles      = {};
    float               Distance    = -1;   // Let it to automatically compute it
    xmath::fvec3        CameraTarget(0, 0, 0);
    View.setFov(60_xdeg);

    xgpu::tools::view LightingView;
    LightingView.setFov(46_xdeg);
    LightingView.setViewport({ 0,0,ShadowMapTexture.getTextureDimensions()[0], ShadowMapTexture.getTextureDimensions()[1] });
    //LightingView.setNearZ(0.5f);

    xmath::fvec3 LightDirection (-1, -2, 0);

    LightDirection.NormalizeSafe();

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

    xproperty::inspector    Inspector("Static Geom Properties");
    auto                    RenderResourceWigzmosCallBack = [&TextureLRU](xproperty::inspector&, bool& bOpen, const xresource::full_guid& PreFullGuid) { RenderResourceWigzmos(TextureLRU, bOpen, PreFullGuid); };
    Inspector.m_OnResourceWigzmos.m_Delegates.clear();
    Inspector.m_OnResourceBrowser.m_Delegates.clear();
    Inspector.m_OnResourceLeftSize.m_Delegates.clear();


    // Theme the property dialogs to be more readable
    for (auto& E : std::array{ &Inspector })
    {
        E->m_Settings.m_ColorVScalar1 = 0.270f * 1.4f;
        E->m_Settings.m_ColorVScalar2 = 0.305f * 1.4f;
        E->m_Settings.m_ColorSScalar = 0.26f * 1.4f;
    }
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 0.5f;

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
        bool bDisable = false;
        if (pName[0] == '[')
        {
            auto Pair    = Inspector.getComponent(0, 0);
            auto View    = std::string_view(pName);
            auto pDesc   = static_cast<xgeom_static::descriptor*>(Pair.second);

            // Change the name to point to the texture...
            if (not pDesc->m_MaterialDetailsList.empty())
            {
                // Remove the [ and ]
                View = View.substr(1, View.size() - 2);

                // get index
                int Index;
                auto result = std::from_chars(View.data(), View.data() + View.size(), Index);
                assert(result.ec == std::errc());

                NewName = std::format("{} {}", pName, pDesc->m_MaterialDetailsList[Index].m_Name);
                pName = NewName.c_str();
                bDisable = pDesc->m_MaterialDetailsList[Index].m_RefCount <= 0;
            }
        }

        if (bDisable) ImGui::BeginDisabled(true);
        if (pID)
        {
            Open = ImGui::TreeNodeEx(pID, ImGuiTreeNodeFlags_Framed | flags, "  %s", pName);
        }
        else
        {
            Open = ImGui::TreeNodeEx(pName, flags);
        }
        if (bDisable) ImGui::EndDisabled();

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }>();

    bool ResetAssetBroswerPosiotion = false;
    bool LightFollowsCamera = false;

    //
    // Main Loop
    //
    while (Instance.ProcessInputEvents())
    {
        if (xgpu::tools::imgui::BeginRendering(true)) continue;


        const float MainWindowWidth = static_cast<float>(MainWindow.getWidth());
        const float MainWindowHeight = static_cast<float>(MainWindow.getHeight());

        if (auto ctx = ImGui::GetCurrentContext(); ctx->HoveredWindow == nullptr || ctx->HoveredWindow->ID == ImGui::GetID("MainDockSpace"))
        {
            //
            // Camera controls
            //
            if (Mouse.isPressed(xgpu::mouse::digital::BTN_RIGHT))
            {
                auto MousePos = Mouse.getValue(xgpu::mouse::analog::POS_REL);
                Angles.m_Pitch.m_Value -= 0.01f * MousePos[1];
                Angles.m_Yaw.m_Value -= 0.01f * MousePos[0];
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

            if (Keyboard.wasPressed(xgpu::keyboard::digital::KEY_SPACE))
            {
                LightFollowsCamera = !LightFollowsCamera;
            }
        }

        if (not Mesh3DMatInstance.empty())
        {
            shadow_generation_push_constants    ShadowGenerationPushC;

            LightingView.setViewport({ 0, 0, static_cast<int>(1024), static_cast<int>(1024) });
            View.setViewport({ 0, 0, static_cast<int>(MainWindowWidth), static_cast<int>(MainWindowHeight) });

            //
            // Render the shadow scene
            //
            if (auto p = xresource::g_Mgr.getResource(SelectedDescriptor.m_GeomRef); p)
            {
                if (LightFollowsCamera)
                {
                    LightDirection = -View.getPosition();
                    LightDirection.NormalizeSafeCopy();
                }

                //
                // Make the shadow view look at the object
                //
                {
                    // Help compute the distance to keep the object inside the view port
                    const float vertical_fov    = LightingView.getFov().m_Value;
                    const float aspect          = LightingView.getAspect();
                    const float radius          = p->m_BBox.getRadius();
                    const float hfov            = 2.0f * std::atan(aspect * std::tan(vertical_fov / 2.0f));
                    const float min_fov         = std::min(vertical_fov, hfov);
                    const float distance        = radius / std::tan(min_fov / 2.0f);

                    auto L = -LightDirection;

                    // Set the view correctly
                    LightingView.LookAt(distance, xmath::radian3(L.Pitch(), L.Yaw(), 0_xdeg), p->m_BBox.getCenter());
                }
                

                auto CmdBuffer = MainWindow.StartRenderPass(RenderPass);

                std::array StaticUBO{ &p->ClusterBuffer() };
                CmdBuffer.setPipelineInstance(ShadowGenerationPipeLineInstance, StaticUBO);

                ShadowGenerationPushC.m_L2C = LightingView.getW2C();
                CmdBuffer.setBuffer(p->IndexBuffer());
                CmdBuffer.setBuffer(p->VertexBuffer());

                for (auto& M : p->getMeshes())
                {
                    for (auto& L : p->getLODs().subspan(M.m_iLOD, M.m_nLODs))
                    {
                        for ( auto& S : p->getSubmeshes().subspan(L.m_iSubmesh, L.m_nSubmesh))
                        {
                            ShadowGenerationPushC.m_ClusterIndex = S.m_iCluster;
                            for (auto& C : p->getClusters().subspan(S.m_iCluster, S.m_nCluster))
                            {
                                CmdBuffer.setPushConstants(ShadowGenerationPushC);
                                CmdBuffer.Draw(C.m_nIndices, C.m_iIndex, C.m_iVertex);

                                // Prepare for the next cluster
                                ShadowGenerationPushC.m_ClusterIndex++;
                            }
                        }
                    }
                }
            }

            static const xmath::fmat4 C2T = []
            {
                xmath::fmat4 ClipSpaceToTextureSpaceMatrix;
                ClipSpaceToTextureSpaceMatrix.setupSRT({ 0.5f, 0.5f, 1.0f }, { 0_xdeg }, { 0.5f, 0.5f, 0.0f });
                return ClipSpaceToTextureSpaceMatrix;
            }();


            //
            // Render the normal color scene
            //
            if (auto p = xresource::g_Mgr.getResource(SelectedDescriptor.m_GeomRef); p)
            {
                auto CmdBuffer = MainWindow.getCmdBuffer();

                if( Distance == -1 )
                {
                    // Help compute the distance to keep the object inside the view port
                    const float vertical_fov    = View.getFov().m_Value;
                    const float aspect          = View.getAspect();
                    const float radius          = p->m_BBox.getRadius();
                    const float hfov            = 2.0f * std::atan(aspect * std::tan(vertical_fov / 2.0f));
                    const float min_fov         = std::min(vertical_fov, hfov);
                    Distance     = radius / std::tan(min_fov / 2.0f);
                    CameraTarget = p->m_BBox.getCenter();
                }

                // Update the camera
                View.LookAt(Distance, Angles, CameraTarget);

                const float maxY = p->m_BBox.m_Min.m_Y;
                const auto L2w   = xmath::fmat4::fromTranslation(-View.getPosition()) * xmath::fmat4::fromTranslation({ 0, 0, 0 });
                const auto w2C   = View.getW2C() * xmath::fmat4::fromTranslation(View.getPosition());

                // Render the mesh
                {
                    CmdBuffer.setStreamingBuffers({ &p->IndexBuffer(),3 });

                    {
                        auto& MeshUBO       = StaticGeomDynamicUBOMesh.allocEntry<ubo_geom_static_mesh>();
                        MeshUBO.m_L2w       = L2w;
                        MeshUBO.m_w2C       = w2C;
                        MeshUBO.m_L2CShadow = C2T * ShadowGenerationPushC.m_L2C;

                        MeshUBO.m_LightColor        = xmath::fvec4(1);
                        MeshUBO.m_AmbientLightColor = xmath::fvec4(0.5f);
                        MeshUBO.m_wSpaceLightPos    = xmath::fvec4(LightingView.getPosition() - View.getPosition(), p->m_BBox.getRadius());
                        MeshUBO.m_wSpaceEyePos      = xmath::fvec4(0);
                    }

                    for (auto& M : p->getMeshes())
                    {
                        for (auto& L : p->getLODs().subspan(M.m_iLOD, M.m_nLODs))
                        {
                            for (auto& S : p->getSubmeshes().subspan(L.m_iSubmesh, L.m_nSubmesh))
                            {
                                std::array StaticUBO{ &p->ClusterBuffer() };
                                CmdBuffer.setPipelineInstance(Mesh3DMatInstance[S.m_iMaterial], StaticUBO);
                                CmdBuffer.setDynamicUBO(StaticGeomDynamicUBOMesh, 0);

                                static_geom_push_const PustConst{ .m_ClusterIndex = S.m_iCluster };
                                for (auto& C : p->getClusters().subspan(S.m_iCluster, S.m_nCluster))
                                {
                                    CmdBuffer.setPushConstants(PustConst);
                                    CmdBuffer.Draw(C.m_nIndices, C.m_iIndex, C.m_iVertex);
                                    PustConst.m_ClusterIndex++;
                                }
                            }
                        }
                    }
                }

                //
                // Render wireframe
                //
                if (0)
                {
                    std::array StaticUBO{ &p->ClusterBuffer() };
                    CmdBuffer.setPipelineInstance(WireFramePipeLineInstance3D, StaticUBO);

                    {
                        auto& MeshUBO = WireFrameDynamicUBOMesh.allocEntry<wireframe_UBO_geom_static_mesh>();
                        MeshUBO.m_w2C = w2C;
                        MeshUBO.m_L2w = L2w;
                    }

                    CmdBuffer.setDynamicUBO(WireFrameDynamicUBOMesh, 0);

                    for (auto& M : p->getMeshes())
                    {
                        for (auto& L : p->getLODs().subspan(M.m_iLOD, M.m_nLODs))
                        {
                            for (auto& S : p->getSubmeshes().subspan(L.m_iSubmesh, L.m_nSubmesh))
                            {
                                static_geom_push_const PustConst{ .m_ClusterIndex = S.m_iCluster };
                                for (auto& C : p->getClusters().subspan(S.m_iCluster, S.m_nCluster))
                                {
                                    CmdBuffer.setPushConstants(PustConst);
                                    CmdBuffer.Draw(C.m_nIndices, C.m_iIndex, C.m_iVertex);
                                    PustConst.m_ClusterIndex++;
                                }
                            }
                        }
                    }
                }

                //
                // Render plane
                //
                {
                    CmdBuffer.setPipelineInstance(Grid3dMaterialInstance);


                    auto& Uniform = GridDynamicUBO.allocEntry<grid_uniform>();
                    Uniform.m_WorldSpaceCameraPos = View.getPosition();
                    Uniform.m_L2W          = xmath::fmat4(xmath::fvec3(100.f, 100.0f, 1.f), xmath::radian3(-90_xdeg, 0_xdeg, 0_xdeg), xmath::fvec3(0, maxY, 0));
                    Uniform.m_W2C          = View.getW2C();
                    Uniform.m_L2CTShadow   = C2T * ShadowGenerationPushC.m_L2C * Uniform.m_L2W;
                    CmdBuffer.setDynamicUBO(GridDynamicUBO, 0);
                    MeshManager.Rendering(CmdBuffer, e19::mesh_manager::model::PLANE3D);
                }


                //
                // Render Normal
                //
                {
                    std::array StaticUBO{ &p->ClusterBuffer() };
                    CmdBuffer.setPipelineInstance(DebugNormalPipeLineInstance, StaticUBO);

                    {
                        auto& MeshUBO = DebugNormalDynamicUBOMesh.allocEntry<debug_normal_ubo_entry>();
                        MeshUBO.m_L2C = w2C * L2w;
                        MeshUBO.m_ScaleFactor = xmath::fvec4(1,0,0,0.05f);
                    }

                    CmdBuffer.setDynamicUBO(DebugNormalDynamicUBOMesh, 0);
                    CmdBuffer.setStreamingBuffers({ &p->IndexBuffer(),3 });

                    for (auto& M : p->getMeshes())
                    {
                        for (auto& L : p->getLODs().subspan(M.m_iLOD, M.m_nLODs))
                        {
                            for (auto& S : p->getSubmeshes().subspan(L.m_iSubmesh, L.m_nSubmesh))
                            {
                                debug_normal_push_const PustConst{ .m_ClusterIndex = S.m_iCluster };
                                for (auto& C : p->getClusters().subspan(S.m_iCluster, S.m_nCluster))
                                {
                                    CmdBuffer.setPushConstants(PustConst);
                                    CmdBuffer.Draw(C.m_nIndices, C.m_iIndex, C.m_iVertex);
                                    PustConst.m_ClusterIndex++;
                                }
                            }
                        }
                    }
                }

            }
        }

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

                if (ImGui::MenuItem("Reset Browser Position", "Ctrl-Space"))
                {
                    ResetAssetBroswerPosiotion = true;
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

                    if (!Log.m_Log.empty())
                    {
                        // Precompute line start offsets (efficient scan)
                        std::vector<size_t> line_offsets;
                        line_offsets.reserve(100);  // Optional: rough estimate to reduce reallocs
                        line_offsets.push_back(0);
                        size_t pos = 0;
                        while ((pos = Log.m_Log.find('\n', pos)) != std::string::npos)
                        {
                            line_offsets.push_back(pos + 1);
                            ++pos;
                        }
                        int num_lines = (int)line_offsets.size();

                        ImGuiListClipper clipper;
                        clipper.Begin(num_lines);
                        while (clipper.Step())
                        {
                            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                            {
                                size_t start = line_offsets[row];
                                size_t end = (row + 1 < num_lines) ? line_offsets[row + 1] - 1 : Log.m_Log.size();
                                std::string_view line(Log.m_Log.data() + start, end - start);

                                if (xstrtool::findI(line, "ERROR:") != std::string::npos)
                                {
                                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
                                    ImGui::TextUnformatted(line.data(), line.data() + line.size());
                                    ImGui::PopStyleColor();
                                }
                                else
                                {
                                    ImGui::TextUnformatted(line.data(), line.data() + line.size());
                                }
                            }
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
            PipelineReload( Device, material, material_instance, SelectedDescriptor, xresource::g_Mgr);

            // Destroy all the material instances
            for(auto& E : Mesh3DMatInstance) Device.Destroy(std::move(E));
            for(auto& E : Mesh3DRscRefMaterialInstance) xresource::g_Mgr.ReleaseRef(E);

            //
            // Set
            //
            if (auto pGeom = xresource::g_Mgr.getResource(SelectedDescriptor.m_GeomRef); pGeom)
            {
                Mesh3DMatInstance.resize( pGeom->m_nDefaultMaterialInstances );
                Mesh3DRscRefMaterialInstance.resize(pGeom->m_nDefaultMaterialInstances);

                auto pDesc = static_cast<xgeom_static::descriptor*>(SelectedDescriptor.m_pDescriptor.get());

                for( auto& MI : pGeom->getDefaultMaterialInstances() )
                {
                    auto Index = static_cast<int>(&MI - pGeom->getDefaultMaterialInstances().data());

                    xresource::g_Mgr.CloneRef(Mesh3DRscRefMaterialInstance[Index], MI);

                    if (MI.empty())
                    {
                        if (auto p = xresource::g_Mgr.getResource(DefaultTextureRef); p)
                        {
                            auto Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{ShadowMapTexture}, xgpu::pipeline_instance::sampler_binding{*p} };
                            auto setup = xgpu::pipeline_instance::setup
                            { .m_PipeLine               = Pipeline3D
                            , .m_SamplersBindings       = Bindings
                            };

                            if (auto Err = Device.Create(Mesh3DMatInstance[Index], setup); Err)
                            {
                                e21::Debugger(xgpu::getErrorMsg(Err));
                                assert(false);
                                std::exit(xgpu::getErrorInt(Err));
                            }
                        }
                    }
                    else if ( auto pMI = xresource::g_Mgr.getResource(Mesh3DRscRefMaterialInstance[Index]); pMI )
                    {
                        if ( auto pT = xresource::g_Mgr.getResource(pMI->m_pTextureList[0].m_TexureRef); pT )
                        {
                            auto Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{ShadowMapTexture}, xgpu::pipeline_instance::sampler_binding{*pT} };
                            auto setup = xgpu::pipeline_instance::setup
                            { .m_PipeLine               = Pipeline3D
                            , .m_SamplersBindings       = Bindings
                            };

                            if (auto Err = Device.Create(Mesh3DMatInstance[Index], setup); Err)
                            {
                                e21::Debugger(xgpu::getErrorMsg(Err));
                                assert(false);
                                std::exit(xgpu::getErrorInt(Err));
                            }
                        }
                    }
                }
            }


            {
                std::wstring DetailsFilePath = std::format(L"{}//Details.txt", SelectedDescriptor.m_LogPath);

                if (std::filesystem::exists(SelectedDescriptor.m_DescriptorPath))
                {
                    xtextfile::stream File;
                    if (auto Err = File.Open(true, DetailsFilePath, {}); Err)
                    {
                        printf("Error Loading the details file...");
                    }
                    else
                    {
                        xproperty::settings::context Context;
                        if (auto Err = xproperty::sprop::serializer::Stream(File, GeomStaticDetails, Context); Err)
                        {
                            printf("Error Loading the details file...");
                        }
                        else
                        {
                            auto pDesc = static_cast<xgeom_static::descriptor*>(SelectedDescriptor.m_pDescriptor.get());

                            if (auto Messages = pDesc->MergeWithDetails(GeomStaticDetails); not Messages.empty())
                            {
                                for( auto& S : Messages)
                                {
                                    xstrtool::print("{}\n", S);
                                }
                            }




                            //
                            // Update the mesh list to make sure it matches with details (latest info)
                            //
/*
                            // First remove any meshes not longer found in details from the descriptor
                            pDesc->m_MeshList.erase
                            (
                                std::remove_if
                                (
                                    pDesc->m_MeshList.begin(), pDesc->m_MeshList.end()
                                    , [&](const auto& Mesh)
                                    {
                                        return GeomStaticDetails.findMesh(Mesh.m_OriginalName) == -1;
                                    }
                                )
                                ,pDesc->m_MeshList.end()
                            );

                            // Add all new meshes from the details into the descriptor
                            for ( auto& E : GeomStaticDetails.m_MeshList )
                            {
                                if ( auto Index = pDesc->findMesh(E.m_Name); Index == -1 )
                                {
                                    auto& Mesh = pDesc->m_MeshList.emplace_back();
                                    Mesh.m_OriginalName = E.m_Name;
                                }
                            }

                            // Update general information about the meshes from the details
                            for (auto& E : GeomStaticDetails.m_MeshList)
                            {
                                auto Index = pDesc->findMesh(E.m_Name);
                                assert(Index != -1);

                                auto& Mesh = pDesc->m_MeshList[Index];

                                Mesh.m_MaterialList.clear();
                                for ( auto& M : E.m_MaterialList ) Mesh.m_MaterialList.push_back(GeomStaticDetails.m_MaterialList[M]);
                            }

                            pDesc->m_MeshNoncollapseVisibleList.clear();
                            for (auto& E : pDesc->m_MeshList)
                            {
                                if (E.m_bMerge == false) pDesc->m_MeshNoncollapseVisibleList.push_back(&E);
                            }

                            //
                            // Check if it should have the merge mesh or not
                            //
                            if (pDesc->m_bMergeMeshes && pDesc->hasMergedMesh() == false)
                            {
                                pDesc->AddMergedMesh();
                            }

                            //
                            // Update the materials if we have to...
                            //

                            // First remove any materials no longer in used
                            for ( int i=0; i< pDesc->m_MaterialInstNamesList.size(); ++i)
                            {
                                if (GeomStaticDetails.findMaterial(pDesc->m_MaterialInstNamesList[i]) == -1)
                                {
                                    pDesc->m_MaterialInstNamesList.erase(pDesc->m_MaterialInstNamesList.begin() + i);
                                    pDesc->m_MaterialInstRefList.erase(pDesc->m_MaterialInstRefList.begin() + i);
                                    --i;
                                }
                            }

                            // Add all new Materials if we have to...
                            for (auto& E : GeomStaticDetails.m_MaterialList)
                            {
                                if (auto Index = pDesc->findMaterial(E); Index == -1)
                                {
                                    pDesc->m_MaterialInstNamesList.emplace_back(E);
                                    pDesc->m_MaterialInstRefList.emplace_back();
                                }
                            }

*/
                        }
                    }
                }
            }
        }


        xproperty::settings::context Context;
        Inspector.Show(Context, [&]
        {
            if (not GeomStaticDetails.m_RootNode.m_Children.empty() || not GeomStaticDetails.m_RootNode.m_MeshList.empty() && SelectedDescriptor.m_pDescriptor)
            {
                if (ImGui::CollapsingHeader("Scene Hierarchy", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Separator();
                    ImGui::Dummy(ImVec2(0, 12)); // extra breathing room after header

                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 7));       // default is (8,4) more vertical
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 2));      // taller tree nodes
                    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 12.0f);            // nicer indent
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));
                    std::function<bool(const xgeom_static::details::node&)> WorthRendering = [&](const xgeom_static::details::node& n)
                    {
                        if (n.m_Children.empty() && n.m_MeshList.empty())
                            return false;

                        if (not n.m_Children.empty() && n.m_MeshList.empty())
                        {
                            for (auto& x : n.m_Children)
                            {
                                if (WorthRendering(x))
                                {
                                    return true;
                                }
                            }

                            return false;
                        }

                        return true;
                    };

                    constexpr static ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_SpanAvailWidth;
                    auto                                pDesc = static_cast<xgeom_static::descriptor*>(SelectedDescriptor.m_pDescriptor.get());
                    auto                                DefaultTextColor = ImGui::GetStyle().Colors[ImGuiCol_Text];
                    auto                                GroupColor = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
                    auto                                DeletedColor = ImVec4(0.8f, 0.3f, 0.3f, 1.0f);
                    xgeom_static::node_path             CurrectNodePath;

                    std::function<void(const xgeom_static::details::node&, const xgeom_static::details&, bool, bool)> DisplayNode = [&](const xgeom_static::details::node& n, const xgeom_static::details& d, bool bIncluded, bool isDeletedParent)
                    {
                        if (n.m_Children.empty() && n.m_MeshList.empty())
                            return;

                        if (not n.m_Children.empty() && n.m_MeshList.empty())
                        {
                            if (WorthRendering(n) == false) return;
                        }

                        // Update the path to the current state of things
                        size_t prev_len = CurrectNodePath.size();
                        if (!CurrectNodePath.empty()) CurrectNodePath += "/";
                        CurrectNodePath += n.m_Name;

                        const bool  isInDeletedList = pDesc->isNodeInDeleteList(CurrectNodePath);
                        bool        isDeleted = isDeletedParent || isInDeletedList;
                        auto        Pair = pDesc->findMergeGroupFromNode(CurrectNodePath);
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
                            if (ImGui::BeginPopupContextItem("NodeContextMenu"))   // triggered by right-click on the node above
                            {
                                ImGui::PushStyleColor(ImGuiCol_Text, DefaultTextColor);
                                if (not bIncluded)
                                {
                                    if (Pair.first)
                                    {
                                        if (ImGui::MenuItem("Remove from Group"))
                                        {
                                            pDesc->RemoveNodeFromGroup(*Pair.first, Pair.second, GeomStaticDetails);
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

                                                for (int j = 0; j < pDesc->m_MergeGroupList.size(); ++j)
                                                {
                                                    if (NewName == pDesc->m_MergeGroupList[j].m_Name)
                                                    {
                                                        NewName.clear();
                                                        break;
                                                    }
                                                }

                                                if (not NewName.empty())
                                                {
                                                    Pair.first = &pDesc->m_MergeGroupList.emplace_back();
                                                    Pair.first->m_Name = std::move(NewName);

                                                    pDesc->AddNodeInGroupList(*Pair.first, CurrectNodePath);
                                                    Pair.second = 0;
                                                    break;
                                                }
                                            }
                                        }

                                        if (not pDesc->m_MergeGroupList.empty())
                                        {
                                            if (ImGui::BeginMenu("Add to Merge Group"))
                                            {
                                                for (int i = 0; i < pDesc->m_MergeGroupList.size(); ++i)
                                                {
                                                    // ImGui::PushID(&pDesc->m_MergeGroupList[i]);
                                                    if (ImGui::MenuItem(pDesc->m_MergeGroupList[i].m_Name.c_str()))
                                                    {
                                                        pDesc->AddNodeInGroupList(pDesc->m_MergeGroupList[i], CurrectNodePath);
                                                        Pair.first = &pDesc->m_MergeGroupList[i];
                                                        Pair.second = (int)pDesc->m_MergeGroupList[i].m_NodePathList.size() - 1;
                                                        break;
                                                    }
                                                    //ImGui::PopID();
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
                                        pDesc->AddNodeInDeleteList(CurrectNodePath, GeomStaticDetails);
                                        isDeleted = true;
                                    }
                                }
                                else if (isInDeletedList)
                                {
                                    if (ImGui::MenuItem("\xEE\x9D\x8D UnDelete Node"))
                                    {
                                        pDesc->RemoveNodeFromDeleteList(CurrectNodePath, GeomStaticDetails);
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

                                // Tooltip on hover
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
                                        , m.m_MaterialList.size()
                                    );
                                    for (auto& mat : m.m_MaterialList)
                                    {
                                        ImGui::Text("%2d.%s\n", 1 + static_cast<int>(&mat - m.m_MaterialList.data()), d.m_MaterialList[mat].c_str());
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

                        // Restore the path
                        CurrectNodePath.resize(prev_len);
                    };

                    // Display tree
                    const bool node_open = [&]
                    {
                        if (pDesc->m_bMergeAllMeshes) return ImGui::TreeNodeEx(&GeomStaticDetails.m_RootNode, flags, "\xEE\xAF\x92 Root");
                        else                          return ImGui::TreeNodeEx(&GeomStaticDetails.m_RootNode, flags, "Root");
                    }();

                    if (ImGui::BeginPopupContextItem("NodeContextMenu"))   // triggered by right-click on the node above
                    {
                        if (ImGui::MenuItem("Merge All as single mesh"))
                        {
                            // TODO: your merge logic here
                            //ImGui::LogText("Merge all children of '%s'", n.m_Name.c_str());
                        }
                        ImGui::EndPopup();
                    }

                    if (node_open)
                    {
                        if (pDesc->m_bMergeAllMeshes)  ImGui::PushStyleColor(ImGuiCol_Text, GroupColor);

                        CurrectNodePath = GeomStaticDetails.m_RootNode.m_Name;
                        for (const auto& child : GeomStaticDetails.m_RootNode.m_Children)
                        {
                            DisplayNode(child, GeomStaticDetails, pDesc->m_bMergeAllMeshes, false);
                        }

                        ImGui::TreePop();
                        if (pDesc->m_bMergeAllMeshes) ImGui::PopStyleColor();
                    }

                    // pop styles
                    ImGui::PopStyleVar(4);
                }
            }
        });

        //
        // Show a texture selector in IMGUI
        //

        if (ResetAssetBroswerPosiotion)
        {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImVec2 center = viewport->GetCenter(); // Center of main screen
            ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
            ResetAssetBroswerPosiotion = false;
        }

        AsserBrowser.Render(e10::g_LibMgr, xresource::g_Mgr);

        // We let the asset browser to decide if it needs to show or not
        e21::g_AssetBrowserPopup.RenderAsPopup( e10::g_LibMgr, xresource::g_Mgr);

        if (auto NewAsset = AsserBrowser.getNewAsset(); NewAsset.empty() == false && NewAsset.m_Type == xrsc::geom_static_type_guid_v)
        {
            // Generate the paths
            e10::g_LibMgr.getNodeInfo(SelectedDescriptor.m_LibraryGUID, SelectedDescriptor.m_InfoGUID, [&](e10::library_db::info_node& NodeInfo)
            {
                SelectedDescriptor.GeneratePaths(NodeInfo.m_Path);
            });
        }
        else if (auto SelectedAsset = AsserBrowser.getSelectedAsset(); SelectedAsset.empty() == false && SelectedAsset.m_Type == xrsc::geom_static_type_guid_v)
        {
            auto xxx = xrsc::material_instance_type_guid_v;

            SelectedDescriptor.clear();
            SelectedDescriptor.m_pDescriptor = xresource_pipeline::factory_base::Find(std::string_view{ "GeomStatic" })->CreateDescriptor();
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

            // Let the system recenter base on the object 
            Distance = -1;
        }

        xgpu::tools::imgui::Render();

        MainWindow.PageFlip();

        // Let the resource manager know we have changed the frame
        xresource::g_Mgr.OnEndFrameDelegate();
    }

    xgpu::tools::imgui::Shutdown();

    return 0;
}