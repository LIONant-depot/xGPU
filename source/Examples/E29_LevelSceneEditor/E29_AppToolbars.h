#pragma once

// e29::app: the menu-bar/dock toolbars and the Host Drawer tab bodies.
namespace e29
{
    inline void app::RenderParentEditorToolbar()
    {



        if (!ImGui::BeginMenuBar())



            return;







        if (ImGui::BeginMenu("File"))



        {



            if (ImGui::MenuItem("Asset Browser..."))



                AsserBrowser.Show(true);







            ImGui::Separator();



            const bool bCanSave = !State.isPlaying()



                && (!State.m_CurrentLevel.empty() || !State.m_OpenScenes.empty())



                && e29::HasUnsavedDocumentChanges(State, LevelDocUndo());



            ImGui::BeginDisabled(!bCanSave);



            if (ImGui::MenuItem("Save", "Ctrl+S"))



            {



                e29::SaveEverything(*pGameMgr, State);



                e29::MarkDocumentClean(State, LevelDocUndo());



            }



            ImGui::EndDisabled();







            const bool bCanClose = !State.isPlaying()



                && (!State.m_CurrentLevel.empty() || !State.m_OpenScenes.empty());



            ImGui::BeginDisabled(!bCanClose);



            if (ImGui::MenuItem("Close"))



                e29::RequestCloseLevel(*pGameMgr, State, LevelDocUndo());



            ImGui::EndDisabled();



            ImGui::EndMenu();



        }







        if (GamePlugin.m_bBuilding)



        {



            ImGui::SameLine(ImGui::GetWindowWidth() - 250.0f);



            ImGui::TextDisabled("Game.dll: building...");



        }







        e29::RenderPlayTransport(State, GamePlugin, { ImVec2(30.0f, 0.0f), true, true });



        ImGui::EndMenuBar();



    }

    inline void app::RenderEditorToolbar(const char* Name, ximgui::toolbar::axis Axis)
    {



        const bool bHorizontal = Axis == ximgui::toolbar::axis::Horizontal;



        const float ButtonHeight = bHorizontal ? EditorToolbarHeight - 4.0f : 28.0f;



        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(EditorToolbarItemSpacing, ImGui::GetStyle().ItemSpacing.y));



        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * EditorToolbarFontScale);



        bool bFirstButton = true;



        auto ToolbarButton = [&](const char* LongLabel, const char* ShortLabel, bool bActive, bool bDisabled, auto&& OnClick)



        {



            if (bHorizontal && !bFirstButton)



                ImGui::SameLine();



            bFirstButton = false;



            if (bActive)



                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_Header]);



            ImGui::BeginDisabled(bDisabled);



            if (ImGui::Button(bHorizontal ? LongLabel : ShortLabel, bHorizontal



                ? ImVec2(52.0f, ButtonHeight) : ImVec2(32.0f, ButtonHeight)))



                OnClick();



            ImGui::EndDisabled();



            if (bActive)



                ImGui::PopStyleColor();



            if (ImGui::IsItemHovered())



            {



                ImGui::BeginTooltip();



                ImGui::TextUnformatted(LongLabel);



                ImGui::EndTooltip();



            }



        };



        auto ToolbarSeparator = [&]()



        {



            if (bHorizontal)



            {



                ImGui::SameLine();



                ImGui::TextDisabled("|");



            }



            else



            {



                ImGui::Separator();



            }



        };







        if (std::strcmp(Name, "Editor") == 0)



        {



            const bool bCanSave = !State.isPlaying()



                && (!State.m_CurrentLevel.empty() || !State.m_OpenScenes.empty())



                && e29::HasUnsavedDocumentChanges(State, LevelDocUndo());



            ToolbarButton("Save", "S", false, !bCanSave, [&]()



            {



                e29::SaveEverything(*pGameMgr, State);



                e29::MarkDocumentClean(State, LevelDocUndo());



            });



            ToolbarButton("Undo", "U", false, State.isPlaying(), [&]() { LevelDocUndo().Undo(); });



            ToolbarButton("Redo", "R", false, State.isPlaying(), [&]() { LevelDocUndo().Redo(); });



            ToolbarButton("Assets", "A", false, false, [&]() { EditorHost.open_drawer_tab(ImGui::GetMainViewport(), 1); });



            ToolbarSeparator();







            e29::RenderPlayTransport(State, GamePlugin, { ImVec2(52.0f, ButtonHeight), bHorizontal, false });
            bFirstButton = false;



            ToolbarSeparator();



            ToolbarButton("Hierarchy", "H", false, false, [&]() { ImGui::SetWindowFocus(e29::editor_tabs::kLevelTreeWindow); });



            ToolbarButton("Inspector", "I", false, false, [&]() { ImGui::SetWindowFocus(e29::editor_tabs::kInspectorWindow); });



            ToolbarButton("Systems", "Y", false, false, [&]() { ImGui::SetWindowFocus(e29::editor_tabs::kSystemRegistryWindow); });



        }



        else



        {



            auto SceneButton = [&](const char* Label, int ToolIndex, const char* Tooltip)



            {



                if (bHorizontal && !bFirstButton)



                    ImGui::SameLine();



                bFirstButton = false;



                if (SceneTool == ToolIndex)



                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_Header]);



                if (ImGui::Button(Label, ImVec2(32.0f, ButtonHeight)))



                    SceneTool = ToolIndex;



                if (SceneTool == ToolIndex)



                    ImGui::PopStyleColor();



                if (ImGui::IsItemHovered())



                {



                    ImGui::BeginTooltip();



                    ImGui::TextUnformatted(Tooltip);



                    ImGui::EndTooltip();



                }



            };



            SceneButton("Q", 0, "Select tool");



            SceneButton("W", 1, "Move tool");



            SceneButton("E", 2, "Rotate tool");



            SceneButton("R", 3, "Scale tool");



            SceneButton("F", 4, "Frame selected");



            ToolbarSeparator();







            auto SceneToggle = [&](const char* LongLabel, const char* ShortLabel, bool& bValue)



            {



                if (bHorizontal)



                    ImGui::SameLine();



                if (ImGui::Button(bHorizontal ? LongLabel : ShortLabel



                    , bHorizontal ? ImVec2(58.0f, ButtonHeight) : ImVec2(32.0f, ButtonHeight)))



                    bValue = !bValue;



                if (ImGui::IsItemHovered())



                {



                    ImGui::BeginTooltip();



                    ImGui::TextUnformatted(LongLabel);



                    ImGui::EndTooltip();



                }



            };



            SceneToggle("Pivot", "P", bPivotCenter);



            SceneToggle("Local", "L", bLocalSpace);



            SceneToggle("Grid", "G", bGridVisible);



        }



        ImGui::PopFont();



        ImGui::PopStyleVar();



    }

    // Host Drawer (xeditor::host): one call — Space + all OS-window manifestations. Editors do not wire this.

    inline void app::DrawDrawerTab(int TabIndex)
    {

            switch (TabIndex)

            {

            case 0:

                AsserBrowser.SetDevice(Device);

                AsserBrowser.RenderEmbeddedTab(e10::g_LibMgr, xresource::g_Mgr, "Resources");

                break;

            case 1:

                AsserBrowser.SetDevice(Device);

                AsserBrowser.RenderEmbeddedTab(e10::g_LibMgr, xresource::g_Mgr, "Assets");

                break;

            case 2:

                e29::RenderSourceControlPanel(E29Undo, /*bEmbedded*/ true);

                break;

            case 3:

                e29::RenderIdleWorkPanel(IdleWork, pGameMgr.get(), State, /*bEmbedded*/ true);

                break;

            case 4:

                e29::RenderGamePluginLogPanel(/*bEmbedded*/ true);

                break;

            case 5:

                e29::DrawCommandConsolePanel(E29History, EditorHost.m_ConsoleLog, /*bEmbedded*/ true);

                break;

            case 6:

                AsserBrowser.SetDevice(Device);

                AsserBrowser.RenderEmbeddedTab(e10::g_LibMgr, xresource::g_Mgr, "Compilation");

                break;

            case 7:

                AsserBrowser.SetDevice(Device);

                AsserBrowser.RenderEmbeddedTab(e10::g_LibMgr, xresource::g_Mgr, "Project Settings");

                break;

            default:

                break;

            }

    }
}
