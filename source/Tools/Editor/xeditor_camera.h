#ifndef XEDITOR_CAMERA_H
#define XEDITOR_CAMERA_H
#pragma once

// The orbit camera of a 3D preview as commands, so an AI turns and frames a view exactly as the mouse does (right drag turns, wheel zooms, middle
// drag moves the target). A preview gives access to the numbers of its own camera; angles are in degrees on the command line. View state: the
// commands are queries, not undoable, and never dirty a document.
#include "dependencies/xeditor/include/xeditor/commands.h"
#include "source/Tools/Editor/xeditor_document_editor.h"

#include <functional>

namespace xeditor
{
    struct camera_access
    {
        xmath::radian3*         m_pAngles   = nullptr;
        float*                  m_pDistance = nullptr;
        xmath::fvec3*           m_pTarget   = nullptr;          // null: the view always looks at the subject's center
        std::function<void()>   m_Frame;                        // fits the camera to the subject
    };

    // A 2D view (a picture the wheel zooms and dragging pans): SetView names which one
    struct view2d_access
    {
        float*  m_pZoom = nullptr;
        float*  m_pPanX = nullptr;
        float*  m_pPanY = nullptr;
    };

    struct view2d_cmd : xundo::query_command_base
    {
        std::vector<std::pair<std::string, view2d_access>> m_Views;

        view2d_cmd(xundo::system& System, std::vector<std::pair<std::string, view2d_access>> Views) noexcept : query_command_base(System, "SetView", nullptr), m_Views(std::move(Views)) { RegisterArguments(); }
        const char* getCommandHelp() const noexcept override { return "The zoom and pan of a 2D view (view state); without options it says what they are. Usage: SetView -View name [-Zoom z] [-PanX px] [-PanY px] [-Reset true]"; }
        void RegisterArguments() noexcept override
        {
            m_hView  = m_Parser.addOption("View",  "Which view (see the names in the reply of a wrong name)", true,  1);
            m_hZoom  = m_Parser.addOption("Zoom",  "Zoom factor (0.05 to 40)",                                false, 1);
            m_hPanX  = m_Parser.addOption("PanX",  "Pan in pixels",                                           false, 1);
            m_hPanY  = m_Parser.addOption("PanY",  "Pan in pixels",                                           false, 1);
            m_hReset = m_Parser.addOption("Reset", "true: zoom 1 and no pan",                                 false, 1);
        }
        std::string Query() noexcept override
        {
            std::string Name, Zoom, PanX, PanY, Reset;
            auto Names = [&] { std::string Text; for (auto& V : m_Views) Text += (Text.empty() ? "" : ", ") + V.first; return Text; };
            if (!cmd_util::GetArg(m_Parser, m_hView, Name)) return "SetView: bad arguments";
            auto It = std::ranges::find_if(m_Views, [&](auto& V) { return V.first == Name; });
            if (It == m_Views.end()) return "SetView: View is " + Names();
            auto& V = It->second;

            auto Number = [](const std::string& Text, float& Out) { return std::from_chars(Text.data(), Text.data() + Text.size(), Out).ec == std::errc(); };
            if (cmd_util::GetArg(m_Parser, m_hReset, Reset) && Reset == "true") { *V.m_pZoom = 1.0f; *V.m_pPanX = 0.0f; *V.m_pPanY = 0.0f; }
            float F = 0;
            if (cmd_util::GetArg(m_Parser, m_hZoom, Zoom)) { if (!Number(Zoom, F)) return "SetView: Zoom takes a number"; *V.m_pZoom = std::clamp(F, 0.05f, 40.0f); }
            if (cmd_util::GetArg(m_Parser, m_hPanX, PanX)) { if (!Number(PanX, F)) return "SetView: PanX takes a number"; *V.m_pPanX = F; }
            if (cmd_util::GetArg(m_Parser, m_hPanY, PanY)) { if (!Number(PanY, F)) return "SetView: PanY takes a number"; *V.m_pPanY = F; }
            return std::format("SetView: {} zoom {:.3f} pan {:.1f},{:.1f}", Name, *V.m_pZoom, *V.m_pPanX, *V.m_pPanY);
        }
        xcmdline::parser::handle m_hView, m_hZoom, m_hPanX, m_hPanY, m_hReset;
    };

    struct camera_cmds
    {
        struct set_cmd : xundo::query_command_base
        {
            camera_access m_Camera;
            set_cmd(xundo::system& System, camera_access Camera) noexcept : query_command_base(System, "SetCamera", nullptr), m_Camera(std::move(Camera)) { RegisterArguments(); }
            const char* getCommandHelp() const noexcept override { return "Places the preview camera. Usage: SetCamera [-Yaw degrees] [-Pitch degrees] [-Distance d] [-Target x,y,z]"; }
            void RegisterArguments() noexcept override
            {
                m_hYaw      = m_Parser.addOption("Yaw",      "Degrees around the vertical axis",  false, 1);
                m_hPitch    = m_Parser.addOption("Pitch",    "Degrees above the horizon",         false, 1);
                m_hDistance = m_Parser.addOption("Distance", "How far from the target",           false, 1);
                m_hTarget   = m_Parser.addOption("Target",   "What the camera looks at: x,y,z",   false, 1);
            }

            static bool Number(const std::string& Text, float& Out) noexcept
            {
                const auto Result = std::from_chars(Text.data(), Text.data() + Text.size(), Out);
                return Result.ec == std::errc() && Result.ptr == Text.data() + Text.size();
            }

            std::string Query() noexcept override
            {
                std::string Yaw, Pitch, Distance, Target;
                const bool bYaw = cmd_util::GetArg(m_Parser, m_hYaw, Yaw), bPitch = cmd_util::GetArg(m_Parser, m_hPitch, Pitch);
                const bool bDistance = cmd_util::GetArg(m_Parser, m_hDistance, Distance), bTarget = cmd_util::GetArg(m_Parser, m_hTarget, Target);
                if (!bYaw && !bPitch && !bDistance && !bTarget) return "SetCamera: nothing to change";

                float fYaw = 0, fPitch = 0, fDistance = 0;
                xmath::fvec3 vTarget(0, 0, 0);
                if (bYaw && !Number(Yaw, fYaw))               return "SetCamera: Yaw takes a number of degrees";
                if (bPitch && !Number(Pitch, fPitch))         return "SetCamera: Pitch takes a number of degrees";
                if (bDistance && (!Number(Distance, fDistance) || fDistance <= 0.0f)) return "SetCamera: Distance takes a number above 0";
                if (bTarget)
                {
                    if (!m_Camera.m_pTarget) return "SetCamera: this view always looks at its subject";
                    const auto A = Target.find(','), B = Target.find(',', A == std::string::npos ? 0 : A + 1);
                    float X, Y, Z;
                    if (A == std::string::npos || B == std::string::npos || !Number(Target.substr(0, A), X) || !Number(Target.substr(A + 1, B - A - 1), Y) || !Number(Target.substr(B + 1), Z)) return "SetCamera: Target takes x,y,z";
                    vTarget = xmath::fvec3(X, Y, Z);
                }

                constexpr float ToRadians = 3.14159265358979f / 180.0f;
                if (bYaw)      m_Camera.m_pAngles->m_Yaw.m_Value   = fYaw * ToRadians;
                if (bPitch)    m_Camera.m_pAngles->m_Pitch.m_Value = fPitch * ToRadians;
                if (bDistance) *m_Camera.m_pDistance = fDistance;
                if (bTarget)   *m_Camera.m_pTarget = vTarget;
                return "SetCamera: done";
            }
            xcmdline::parser::handle m_hYaw, m_hPitch, m_hDistance, m_hTarget;
        };

        struct get_cmd : xundo::query_command_base
        {
            camera_access m_Camera;
            get_cmd(xundo::system& System, camera_access Camera) noexcept : query_command_base(System, "GetCamera", nullptr), m_Camera(std::move(Camera)) {}
            const char* getCommandHelp() const noexcept override { return "The preview camera: yaw and pitch in degrees, distance, target. Usage: GetCamera"; }
            void RegisterArguments() noexcept override {}
            std::string Query() noexcept override
            {
                constexpr float ToDegrees = 180.0f / 3.14159265358979f;
                std::string Text = std::format("yaw: {:.2f}\npitch: {:.2f}\ndistance: {:.4f}\n", m_Camera.m_pAngles->m_Yaw.m_Value * ToDegrees, m_Camera.m_pAngles->m_Pitch.m_Value * ToDegrees, *m_Camera.m_pDistance);
                if (m_Camera.m_pTarget) Text += std::format("target: {:.4f},{:.4f},{:.4f}\n", m_Camera.m_pTarget->m_X, m_Camera.m_pTarget->m_Y, m_Camera.m_pTarget->m_Z);
                return Text;
            }
        };

        struct frame_cmd : xundo::query_command_base
        {
            camera_access m_Camera;
            frame_cmd(xundo::system& System, camera_access Camera) noexcept : query_command_base(System, "FrameSubject", nullptr), m_Camera(std::move(Camera)) {}
            const char* getCommandHelp() const noexcept override { return "Fits the preview camera to what it shows (the Recenter button). Usage: FrameSubject"; }
            void RegisterArguments() noexcept override {}
            std::string Query() noexcept override { if (m_Camera.m_Frame) m_Camera.m_Frame(); return "FrameSubject: done"; }
        };

        set_cmd     m_Set;
        get_cmd     m_Get;
        frame_cmd   m_Frame;

        camera_cmds(xundo::system& System, const camera_access& Camera) noexcept : m_Set(System, Camera), m_Get(System, Camera), m_Frame(System, Camera) {}
    };
}

#endif // XEDITOR_CAMERA_H
