#ifndef XEDITOR_TEXTURE_THUMBNAILS_H
#define XEDITOR_TEXTURE_THUMBNAILS_H
#pragma once

// The resource button of a property that references a resource, with a small picture next to it when the resource is a texture (a big one in the
// tooltip). The editor owns one and wires it to its inspector; it keeps a reference to the textures it shows (the least recently used are let go)
// so a picture is not loaded again every frame.
#include "dependencies/xresource_pipeline_v2/source/editor/E10_InspectorPickers.h"
#include "Plugins/xtexture.plugin/source/xtexture_xgpu_rsc_loader.h"

#include <algorithm>
#include <functional>
#include <list>
#include <unordered_map>

namespace xeditor
{
    class texture_thumbnails
    {
    public:
        explicit texture_thumbnails(std::size_t Capacity = 100) noexcept : m_Capacity(Capacity) {}
        ~texture_thumbnails() noexcept { Clear(); }
        texture_thumbnails(const texture_thumbnails&) = delete;

        void Clear() noexcept
        {
            for (auto& [Guid, Ref] : m_Refs) xresource::g_Mgr.ReleaseRef(Ref);
            m_Refs.clear();
            m_Order.clear();
        }

        // The picker's button: a thumbnail and the resource's name for a texture, just the name for anything else. bOpen: the button was pressed.
        void Render(bool& bOpen, const xresource::full_guid& PreFullGuid) noexcept
        {
            std::string Name;
            e10::RemapGUIDToString(Name, PreFullGuid);

            ImVec4 Base = ImGui::GetStyleColorVec4(ImGuiCol_Button);
            Base.w = 1; Base.x *= 0.75f; Base.y *= 0.75f; Base.z *= 0.75f;
            ImGui::PushStyleColor(ImGuiCol_Button, Base);

            if (!PreFullGuid.empty() && PreFullGuid.m_Type == xrsc::texture_type_guid_v)
            {
                ImGui::BeginGroup();
                constexpr float ImageSize = 48;
                if (auto* pTexture = xresource::g_Mgr.getResource(Reference(PreFullGuid)))
                {
                    ImGui::Image(static_cast<void*>(pTexture), ImVec2(ImageSize, ImageSize));
                    if (ImGui::BeginItemTooltip())
                    {
                        const auto  Size   = pTexture->getTextureDimensions();
                        const float Ratio  = Size[1] / static_cast<float>(Size[0]);
                        ImVec2      Big(500, 500 * Ratio);
                        const ImVec2 Viewport = ImGui::GetMainViewport()->Size;     // keep the tooltip on screen
                        Big.x = std::min(Big.x, Viewport.x * 0.75f);
                        Big.y = std::min(Big.y, Viewport.y * 0.75f);
                        ImGui::Image(static_cast<void*>(pTexture), Big);
                        ImGui::EndTooltip();
                    }
                }
                ImGui::SameLine();
                bOpen = ImGui::Button(Name.c_str(), ImVec2(-1, ImageSize));
                ImGui::EndGroup();
            }
            else
            {
                bOpen = ImGui::Button(Name.c_str(), ImVec2(-1, 0));
            }
            ImGui::PopStyleColor();
        }

        // Makes the inspector draw its resource buttons (and their picker popup) with this.
        void Wire(xproperty::inspector& Inspector) noexcept
        {
            m_Wigzmos = [this](xproperty::inspector&, const xproperty::type::object&, void*, std::string_view, bool& bOpen, const xresource::full_guid& PreFullGuid) { Render(bOpen, PreFullGuid); };
            Inspector.m_OnResourceWigzmos.m_Delegates.clear();
            Inspector.m_OnResourceWigzmos.Register(m_Wigzmos);
            Inspector.m_OnResourceBrowser.m_Delegates.clear();
            Inspector.m_OnResourceBrowser.Register<[](xproperty::inspector&, const xproperty::type::object&, void*, std::string_view Path, bool& bOpen, xresource::full_guid& Out, std::span<const xresource::type_guid> Filters)
            {
                const void* pUID = reinterpret_cast<const void*>(std::hash<std::string_view>{}(Path));
                e10::ResourceBrowserPopup(pUID, bOpen, Out, Filters);
            }>();
        }

    private:
        // The reference this cache holds for the texture (the resource manager counts it when it is first used through this object), most recently used first.
        xrsc::texture_ref& Reference(const xresource::full_guid& Guid) noexcept
        {
            if (auto It = m_Refs.find(Guid); It != m_Refs.end())
            {
                m_Order.remove(Guid);
                m_Order.push_front(Guid);
                return It->second;
            }

            xrsc::texture_ref Ref;
            Ref.m_Instance = Guid.m_Instance;
            auto& Held = m_Refs.emplace(Guid, Ref).first->second;
            m_Order.push_front(Guid);
            while (m_Refs.size() > m_Capacity)
            {
                const auto Oldest = m_Order.back();
                if (auto It = m_Refs.find(Oldest); It != m_Refs.end()) { xresource::g_Mgr.ReleaseRef(It->second); m_Refs.erase(It); }
                m_Order.pop_back();
            }
            return Held;
        }

        std::size_t                                             m_Capacity;
        std::unordered_map<xresource::full_guid, xrsc::texture_ref> m_Refs;
        std::list<xresource::full_guid>                         m_Order;
        std::function<void(xproperty::inspector&, const xproperty::type::object&, void*, std::string_view, bool&, const xresource::full_guid&)> m_Wigzmos;
    };
}

#endif // XEDITOR_TEXTURE_THUMBNAILS_H
