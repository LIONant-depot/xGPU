#ifndef XGPU_EDITOR_RESOURCE_PICKER_H
#define XGPU_EDITOR_RESOURCE_PICKER_H
#pragma once

// Pulled out of E21_StaticGeom_Editor.cpp verbatim - the only existing inline-picker-with-drag-drop
// mechanism in the codebase (drag-drop via a "DESCRIPTOR_GUID" payload, or a popup asset browser
// button). E23/E24 never needed this (neither has a resource-ref field outside its main descriptor);
// E25's render_settings.m_PreviewAnimRef is the first consumer beyond E21 itself.
//
// Requires the includer to already have pulled in E10_AssetMgr.h/E10_AssetBrowser.h (for e10::g_LibMgr
// / e10::assert_browser) - not included here to avoid dragging E10's asset-browser machinery into
// every editor that only wants the viewport/pose-eval halves of this folder.
namespace xgpu::tools::editors
{
    inline e10::assert_browser g_AssetBrowserPopup;

    // Pulled out of E21_StaticGeom_Editor.cpp's RemapGUIDToString - resolves a resource ref to its
    // asset-browser display name (falling back to the raw GUID hex if the node lookup fails), so a
    // picker button reads e.g. "WalkingSkeleton" instead of a bare hex value.
    inline void RemapGUIDToString(std::string& Name, const xresource::full_guid& PreFullGuid)
    {
        if (PreFullGuid.empty())
        {
            Name = "empty";
        }
        else
        {
            auto FullGuid = xresource::g_Mgr.getFullGuid(PreFullGuid);

            e10::g_LibMgr.getNodeInfo(FullGuid, [&](e10::library_db::info_node& Node)
            {
                Name = Node.m_Info.m_Name;
            });

            if (Name.empty()) Name = std::format("{:X}", FullGuid.m_Instance.m_Value);
        }
    }

    // Simplified version of E21's RenderResourceWigzmos - E21's version also special-cases
    // xrsc::texture_type_guid_v refs with a thumbnail preview (via a texture LRU cache), which no
    // consumer of this shared header needs yet (E23/E24/E25's resource-ref fields - skeleton, anim
    // package, material instance - are never textures). Add the thumbnail branch back here, generically,
    // if/when an editor needs it, rather than duplicating this whole function again.
    inline void RenderResourceWigzmos(bool& bOpen, const xresource::full_guid& PreFullGuid)
    {
        std::string Name;
        RemapGUIDToString(Name, PreFullGuid);

        ImVec4 base = ImGui::GetStyleColorVec4(ImGuiCol_Button);
        base.w = 1;
        base.x *= 0.75f;
        base.y *= 0.75f;
        base.z *= 0.75f;
        ImGui::PushStyleColor(ImGuiCol_Button, base);
        bOpen = ImGui::Button(Name.c_str(), ImVec2(-1, 48));
        ImGui::PopStyleColor();

        if (ImGui::IsItemHovered() && not PreFullGuid.empty())
            ImGui::SetTooltip("%llX", static_cast<unsigned long long>(PreFullGuid.m_Instance.m_Value));
    }

    inline void ResourceBrowserPopup(const void* pUID, bool& Open, xresource::full_guid& Output, std::span<const xresource::type_guid> Filters)
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
}

#endif
