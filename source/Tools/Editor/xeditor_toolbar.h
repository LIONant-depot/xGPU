#ifndef XEDITOR_TOOLBAR_H
#define XEDITOR_TOOLBAR_H
#pragma once

// Per-editor top bar for the shared editor framework — Undo/Redo, Save, Compile + Feedback.
// Drawn via ImGui::BeginMenuBar() so it picks up ImGuiCol_MenuBarBg — the same App Toolbar
// color E29's Level Editor menu bar uses (official editor theme). Host windows must pass
// ImGuiWindowFlags_MenuBar to ImGui::Begin. Hosts must also PushStyleVar(WindowPadding, 0) around Begin/End like E29's Level Editor, or a hairline gap appears under the menu bar.
// Feedback colors/layout follow E10's Compile + Feedback strip.
#include "dependencies/xundo/source/xundo_system.h"
#include "source/Examples/E10_TextureResourcePipeline/E10_AssetMgr.h"
#include "dependencies/xstrtool/source/xstrtool.h"
#include "imgui.h"
#include <memory>
#include <vector>
#include <string>
#include <format>

namespace xeditor
{
    struct toolbar_model
    {
        xundo::system*                                                  m_pUndo              = nullptr;
        bool                                                            m_bDirty             = false;
        bool                                                            m_bCanCompile        = true;
        std::shared_ptr<e10::compilation::historical_entry::log>        m_Log;
        std::vector<std::string>*                                       m_pValidationErrors  = nullptr;

        void (*m_OnSave)(void* pUser)    = nullptr;
        void (*m_OnCompile)(void* pUser) = nullptr;
        void* m_pUser                    = nullptr;
    };

    // Requires the host window to have been begun with ImGuiWindowFlags_MenuBar.
    // Undo/Redo/Save stay left; Compile + Feedback are centered by default (E29 Play/Stop pattern).
    inline void RenderEditorToolbar(toolbar_model& Model) noexcept
    {
        if (!ImGui::BeginMenuBar())
            return;

        // Theme FramePadding.y is 1 (Unity density). MenuBar gets +4px from
        // ApplyMainDockTabTitleBarOffset; bump button FramePadding.y to match so
        // Save/Compile fill the bar instead of looking clipped or tiny.
        const ImGuiStyle& Style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(Style.FramePadding.x, Style.FramePadding.y + 2.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));

        if (Model.m_pUndo)
        {
            const bool bCanUndo = Model.m_pUndo->GetUndoIndex() > 0;
            const bool bCanRedo = Model.m_pUndo->GetUndoIndex() < static_cast<int>(Model.m_pUndo->GetHistoryCount());

            if (!bCanUndo) ImGui::BeginDisabled();
            if (ImGui::Button(" \xEE\x9E\xA7 "))
                Model.m_pUndo->Undo();
            if (!bCanUndo) ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Undo the last change");

            ImGui::SameLine(0, 4);

            if (!bCanRedo) ImGui::BeginDisabled();
            if (ImGui::Button(" \xEE\x9E\xA6 "))
                Model.m_pUndo->Redo();
            if (!bCanRedo) ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Redo the last change");

            ImGui::SameLine(0, 8);
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine(0, 8);
        }

        {
            if (!Model.m_bDirty) ImGui::BeginDisabled();
            if (ImGui::Button(" Save ") && Model.m_OnSave)
                Model.m_OnSave(Model.m_pUser);
            if (!Model.m_bDirty) ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save the descriptor");
        }

        if (Model.m_bCanCompile)
        {
            e10::compilation::historical_entry::result Results = e10::compilation::historical_entry::result::SUCCESS;
            if (Model.m_Log)
            {
                xcontainer::lock::scope lk(*Model.m_Log);
                Results = Model.m_Log->get().m_Result;
            }

            const bool bValidationFail = Model.m_pValidationErrors && !Model.m_pValidationErrors->empty();
            const bool bBusy = Results == e10::compilation::historical_entry::result::COMPILING
                            || Results == e10::compilation::historical_entry::result::COMPILING_WARNINGS;

            // Default for all editors: center Compile + Feedback like E29 Play/Stop
            // (SameLine((windowWidth - groupW) * 0.5f)).
            const float CompileW  = ImGui::CalcTextSize("\xEF\x96\xB0 Compile ").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            const float FeedbackW = ImGui::CalcTextSize("Feedback:\xee\xa5\xb2").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            const float Gap       = 4.0f;
            const float GroupW    = CompileW + Gap + FeedbackW;
            // SetCursorPosX avoids SameLine wrap when left controls already pass center
            // (wrap made Compile/Feedback look like a second, taller toolbar row).
            {
                const float CenterX = (ImGui::GetWindowWidth() - GroupW) * 0.5f;
                const float Y = ImGui::GetCursorPosY();
                ImGui::SetCursorPos(ImVec2(ImMax(CenterX, ImGui::GetCursorPosX() + 8.0f), Y));
            }

            if (bBusy || bValidationFail) ImGui::BeginDisabled();
            if (ImGui::Button("\xEF\x96\xB0 Compile ") && Model.m_OnCompile)
                Model.m_OnCompile(Model.m_pUser);
            if (bBusy || bValidationFail) ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save the descriptor and trigger compilation");

            ImGui::SameLine(0, 4);

            std::uint32_t Color = IM_COL32(255, 255, 255, 255);
            if (bValidationFail)
                Color = IM_COL32(255, 170, 140, 255);
            else
            {
                switch (Results)
                {
                case e10::compilation::historical_entry::result::COMPILING_WARNINGS: Color = IM_COL32(255, 255, 0, 255); break;
                case e10::compilation::historical_entry::result::COMPILING:          Color = IM_COL32(0, 255, 0, 255);   break;
                case e10::compilation::historical_entry::result::FAILURE:            Color = IM_COL32(255, 170, 140, 255); break;
                case e10::compilation::historical_entry::result::SUCCESS_WARNINGS:   Color = IM_COL32(255, 255, 0, 255); break;
                case e10::compilation::historical_entry::result::SUCCESS:            Color = IM_COL32(255, 255, 255, 255); break;
                }
            }

            ImGui::PushStyleColor(ImGuiCol_Text, Color);
            if (ImGui::Button("Feedback:\xee\xa5\xb2"))
            {
                const ImVec2 ButtonPos  = ImGui::GetItemRectMin();
                const ImVec2 ButtonSize = ImGui::GetItemRectSize();
                ImGui::SetNextWindowPos(ImVec2(ButtonPos.x, ButtonPos.y + ButtonSize.y));
                ImGui::OpenPopup("###EditorToolbarFeedback");
            }
            ImGui::PopStyleColor();

            if (ImGui::BeginPopup("###EditorToolbarFeedback"))
            {
                ImGui::BeginChild("###EditorToolbarFeedback-Child", ImVec2(600, 300));
                ImGui::PushTextWrapPos(600);
                if (bValidationFail)
                {
                    ImGui::TextUnformatted("Validation Errors:");
                    ImGui::TextUnformatted("=====================================================");
                    for (size_t i = 0; i < Model.m_pValidationErrors->size(); ++i)
                    {
                        ImGui::Text("ERROR[%d]: ", static_cast<int>(i));
                        ImGui::SameLine();
                        ImGui::TextUnformatted((*Model.m_pValidationErrors)[i].c_str());
                    }
                    ImGui::TextUnformatted("=====================================================");
                }
                if (Model.m_Log)
                {
                    xcontainer::lock::scope lk(*Model.m_Log);
                    auto& Log = Model.m_Log->get();
                    if (!Log.m_Log.empty())
                        ImGui::TextUnformatted(Log.m_Log.c_str());
                }
                ImGui::PopTextWrapPos();
                ImGui::EndChild();
                ImGui::EndPopup();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Compilation / validation feedback");

            if (Model.m_Log)
            {
                xcontainer::lock::scope lk(*Model.m_Log);
                auto& Log = Model.m_Log->get();
                if (!Log.m_Log.empty())
                {
                    ImGui::SameLine(0, 8);
                    ImGui::TextUnformatted(std::format("{}", xstrtool::getLastLine(Log.m_Log)).c_str());
                }
            }
        }

        ImGui::PopStyleVar(2); // ItemSpacing + FramePadding
        ImGui::EndMenuBar();
    }
}

#endif // XEDITOR_TOOLBAR_H
