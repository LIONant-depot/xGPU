#ifndef E29_PANEL_COMPONENT_SELECTOR_H
#define E29_PANEL_COMPONENT_SELECTOR_H
#pragma once

// Component "Add" popup contents for Entity Properties.
// Grouped by g_ComponentDisplayInfo category (E29_REGISTER_COMPONENT), collapsible
// (initially closed), with a top search bar matching Level Tree / Asset views
// (RenderTreeSearchBar + ContainsCaseInsensitive).
//
// Meant to be included via the umbrella (E29_LevelSceneEditorKit.h) only, after
// editor_state, g_ComponentDisplayInfo, and command infrastructure are defined.
// Not designed to be included standalone. Not a docked editor window — Entity
// Properties opens this as an ImGui popup that replaces the old BeginCombo list.
#include "source/Examples/E29_LevelSceneEditor/commands/E29_Commands_ComponentEdit.h"
#include "source/Examples/E29_LevelSceneEditor/GameProject/E29_GameRegistration.h"
#include <cstring>

namespace e29
{
    //---------------------------------------------------------------------------
    // Render inside an already-open ImGui popup (caller OpenPopup / BeginPopup).
    // Returns true if a component was added this frame.
    //---------------------------------------------------------------------------
    inline bool RenderComponentSelectorPopupContents(
        editor_state&                    State,
        xundo::system&                   Undo,
        xecs::pool::instance* pPool) noexcept
    {
        if (pPool == nullptr)
        {
            ImGui::TextDisabled("Select an entity.");
            return false;
        }

        e29::RenderTreeSearchBar(State.m_ComponentSelectorSearchString, ImGui::GetContentRegionAvail().x);
        ImGui::Separator();

        const bool bHasSearch = !State.m_ComponentSelectorSearchString.empty();

        struct component_entry
        {
            const xecs::component::type::info* m_pInfo = nullptr;
            std::string                         m_Category;
            int                                 m_Priority = 0;
        };

        // Registry (not the entity DataSpan) — same source as the old BeginCombo list.
        std::vector<component_entry> Available;
        Available.reserve(xecs::component::mgr::s_Registry.m_ComponentInfoMap.size());

        for (auto& Pair : xecs::component::mgr::s_Registry.m_ComponentInfoMap)
        {
            auto* pInfo = Pair.second;
            if (pInfo->m_TypeID != xecs::component::type::id::DATA) continue;
            if (e29::IsInternalComponent(pInfo)) continue;
            // findIndexComponentFromInfo, not getComponentBits().getBit() — see
            // dependencies/xECSV2/doc/getbit_vs_findindexcomponentfrominfo.md.
            if (pPool->findIndexComponentFromInfo(*pInfo) >= 0) continue;

            const char* pName = pInfo->m_pName ? pInfo->m_pName : "";
            if (bHasSearch && !e29::ContainsCaseInsensitive(pName, State.m_ComponentSelectorSearchString))
                continue;

            std::string Category;
            int         Priority = 0;
            if (auto It = e29::g_ComponentDisplayInfo.find(pInfo->m_pName); It != e29::g_ComponentDisplayInfo.end())
            {
                Category = It->second.m_Category;
                Priority = It->second.m_Priority;
            }

            Available.push_back({ pInfo, std::move(Category), Priority });
        }

        std::stable_sort(Available.begin(), Available.end(),
            [](const component_entry& A, const component_entry& B) noexcept
            {
                if (A.m_Category != B.m_Category) return A.m_Category < B.m_Category;
                if (A.m_Priority != B.m_Priority) return A.m_Priority < B.m_Priority;
                const char* NA = A.m_pInfo->m_pName ? A.m_pInfo->m_pName : "";
                const char* NB = B.m_pInfo->m_pName ? B.m_pInfo->m_pName : "";
                return std::strcmp(NA, NB) < 0;
            });

        struct category_group
        {
            std::string                  m_Name;
            std::vector<component_entry> m_Components;
        };

        std::vector<category_group> Groups;
        for (auto& Comp : Available)
        {
            if (Groups.empty() || Groups.back().m_Name != Comp.m_Category)
                Groups.push_back({ Comp.m_Category, {} });
            Groups.back().m_Components.push_back(std::move(Comp));
        }

        if (Groups.empty())
        {
            ImGui::TextDisabled(bHasSearch ? "No matching components." : "No components to add.");
            return false;
        }

        bool bAdded = false;

        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 8.0f);
        ImGui::BeginChild("##ComponentSelectorList", ImVec2(0, 0), ImGuiChildFlags_None);

        for (auto& Group : Groups)
        {
            const std::string DisplayName = Group.m_Name.empty() ? "Uncategorized" : Group.m_Name;
            const std::string GroupLabel  = std::format("{} ({})", DisplayName, Group.m_Components.size());

            bool bOpen = false;
            if (bHasSearch)
            {
                bOpen = true;
            }
            else if (auto ItOpen = State.m_ComponentSelectorCategoryOpen.find(Group.m_Name);
                     ItOpen != State.m_ComponentSelectorCategoryOpen.end())
            {
                bOpen = ItOpen->second;
            }

            ImGui::SetNextItemOpen(bOpen, ImGuiCond_Always);
            const ImGuiTreeNodeFlags GroupFlags = ImGuiTreeNodeFlags_SpanFullWidth; // whole row toggles (no OpenOnArrow)
            const bool bNodeOpen = ImGui::TreeNodeEx(GroupLabel.c_str(), GroupFlags);
            if (!bHasSearch)
                State.m_ComponentSelectorCategoryOpen[Group.m_Name] = bNodeOpen;

            if (!bNodeOpen)
                continue;

            for (auto& Comp : Group.m_Components)
            {
                ImGui::PushID(Comp.m_pInfo->m_pName);
                if (ImGui::Selectable(Comp.m_pInfo->m_pName))
                {
                    e29::commands::Run(e29::LevelDocUndo(), std::format("AddComponent -Scene {} -Id {} -Component {:016X}"
                        , e29::commands::FormatSceneGuid(State.m_SelectedEntityScene)
                        , e29::commands::FormatEntityId(State.m_SelectedEntityId)
                        , Comp.m_pInfo->m_Guid.m_Value
                    ));
                    bAdded = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        ImGui::EndChild();
        ImGui::PopStyleVar(1);
        return bAdded;
    }
} // namespace e29

#endif // E29_PANEL_COMPONENT_SELECTOR_H
