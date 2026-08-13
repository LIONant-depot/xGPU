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
