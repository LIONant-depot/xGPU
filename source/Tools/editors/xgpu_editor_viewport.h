#ifndef XGPU_EDITOR_VIEWPORT_H
#define XGPU_EDITOR_VIEWPORT_H
#pragma once

#include "source/tools/xgpu_view.h"

// Shared viewport/camera plumbing pulled out of E23/E24, which had it duplicated line-for-line -
// every 3D-preview editor in this codebase (E19/E20/E21/E23/E24) opens the same kind of plain,
// dockable ImGui window and drives the same right-drag-pitch-yaw/middle-drag-pan/wheel-zoom orbit
// camera. New editors (starting with E25) should reach for this instead of re-deriving it again.
namespace xgpu::tools::editors
{
    //--------------------------------------------------------------------------------------
    // Right-drag pitch/yaw, middle-drag pan, wheel zoom - identical math E23/E24 each had inline.
    // Free-function form (operating on refs to primitives) so an editor with its own loose
    // Angles/Distance/Target locals (E23, E24) can adopt this without restructuring them into a
    // struct; orbit_camera below is sugar over the same two functions for a NEW editor (E25) that
    // has no existing layout to preserve.
    //--------------------------------------------------------------------------------------
    inline void HandleOrbitCameraInput(xgpu::mouse& Mouse, const xgpu::tools::view& View, xmath::radian3& Angles, float& Distance, xmath::fvec3& Target) noexcept
    {
        if (Mouse.isPressed(xgpu::mouse::digital::BTN_RIGHT))
        {
            auto MousePos = Mouse.getValue(xgpu::mouse::analog::POS_REL);
            Angles.m_Pitch.m_Value -= 0.01f * MousePos[1];
            Angles.m_Yaw.m_Value   -= 0.01f * MousePos[0];
        }

        if (Mouse.isPressed(xgpu::mouse::digital::BTN_MIDDLE))
        {
            auto MousePos = Mouse.getValue(xgpu::mouse::analog::POS_REL);
            Target += View.getWorldYVector() * (0.005f * MousePos[1]);
            Target += View.getWorldXVector() * (0.005f * MousePos[0]);
        }

        if (Distance != -1)
        {
            Distance += Distance * -0.2f * Mouse.getValue(xgpu::mouse::analog::WHEEL_REL)[0];
            if (Distance < 0.5f)
            {
                Target += View.getWorldZVector() * (0.5f * (0.5f - Distance));
                Distance = 0.5f;
            }
        }
    }

    // Frames the camera so a sphere of the given radius/center fills the current FOV - same
    // "distance that keeps the subject framed regardless of scale" logic E23/E24 both derive.
    inline void ReframeOrbitCamera(const xgpu::tools::view& View, float Radius, const xmath::fvec3& Center, float& Distance, xmath::fvec3& Target) noexcept
    {
        const float VerticalFov = View.getFov().m_Value;
        const float Aspect      = View.getAspect();
        const float HFov        = 2.0f * std::atan(Aspect * std::tan(VerticalFov * 0.5f));
        const float MinFov      = std::min(VerticalFov, HFov);

        Distance = Radius / std::tan(MinFov * 0.5f);
        Target   = Center;
    }

    struct orbit_camera
    {
        xmath::radian3  m_Angles    = {};
        float           m_Distance  = 2.0f;
        xmath::fvec3    m_Target    = xmath::fvec3::fromZero();

        // Call once per frame while the viewport window is hovered (see viewport_frame::m_bHovered).
        inline void HandleInput(xgpu::mouse& Mouse, const xgpu::tools::view& View) noexcept
        {
            HandleOrbitCameraInput(Mouse, View, m_Angles, m_Distance, m_Target);
        }

        inline void Reframe(const xgpu::tools::view& View, float Radius, const xmath::fvec3& Center) noexcept
        {
            ReframeOrbitCamera(View, Radius, Center, m_Distance, m_Target);
        }

        inline void Apply(xgpu::tools::view& View) const noexcept
        {
            View.LookAt(m_Distance, m_Angles, m_Target);
        }
    };

    //--------------------------------------------------------------------------------------

    struct viewport_frame
    {
        ImVec2  m_WindowPos;
        ImVec2  m_WindowSize;
        bool    m_bHovered;
    };

    // Opens the ordinary, dockable ImGui window every 3D-preview editor hosts its scene in via
    // AddCustomRenderCallback (no special window flags - see E23's own comment on why: a specially-
    // flagged window for 3D content alone kept opening as a separate OS window). Caller is still
    // responsible for ImGui::End() - the body between Begin/End varies too much per-editor to
    // templatize here, only the opening boilerplate is shared.
    inline viewport_frame BeginViewportWindow(const char* pTitle, ImVec2 DefaultSize = ImVec2(900, 620), ImVec4 BgColor = ImVec4(0.45f, 0.45f, 0.45f, 1.0f)) noexcept
    {
        ImGui::SetNextWindowSize(DefaultSize, ImGuiCond_FirstUseEver);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, BgColor);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin(pTitle);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        viewport_frame Frame;
        Frame.m_WindowPos  = ImGui::GetCursorScreenPos();
        Frame.m_WindowSize = ImGui::GetContentRegionAvail();
        Frame.m_bHovered   = ImGui::IsWindowHovered();
        return Frame;
    }
}

#endif
