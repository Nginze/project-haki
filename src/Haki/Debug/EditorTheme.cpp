#include "Haki/Debug/EditorTheme.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace Haki
{

void ApplyEditorTheme(float opacity)
{
    opacity = std::isfinite(opacity) ? std::clamp(opacity, 0.2f, 1.0f) : 0.72f;
    ImGuiStyle &style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);

    style.WindowRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 3.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = {8.0f, 8.0f};
    style.FramePadding = {6.0f, 3.0f};
    style.ItemSpacing = {6.0f, 4.0f};
    style.IndentSpacing = 14.0f;
    style.ScrollbarSize = 12.0f;

    const auto rgba = [](int r, int g, int b, float a) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); };
    const float menuAlpha = std::min(opacity + 0.15f, 1.0f);
    ImVec4 *colors = style.Colors;
    colors[ImGuiCol_WindowBg] = rgba(30, 35, 43, opacity);          // slate
    colors[ImGuiCol_ChildBg] = rgba(24, 28, 35, opacity * 0.5f);
    colors[ImGuiCol_PopupBg] = rgba(26, 30, 37, std::max(opacity, 0.94f));
    colors[ImGuiCol_MenuBarBg] = rgba(22, 26, 32, menuAlpha);
    colors[ImGuiCol_TitleBg] = rgba(22, 26, 32, menuAlpha);
    colors[ImGuiCol_TitleBgActive] = rgba(34, 42, 54, menuAlpha);
    colors[ImGuiCol_TitleBgCollapsed] = rgba(22, 26, 32, opacity);
    colors[ImGuiCol_Border] = rgba(70, 80, 95, 0.5f);
    colors[ImGuiCol_Text] = rgba(220, 226, 234, 1.0f);
    colors[ImGuiCol_TextDisabled] = rgba(128, 138, 152, 1.0f);
    colors[ImGuiCol_FrameBg] = rgba(44, 51, 62, 0.85f);
    colors[ImGuiCol_FrameBgHovered] = rgba(56, 66, 80, 0.9f);
    colors[ImGuiCol_FrameBgActive] = rgba(66, 78, 96, 1.0f);
    colors[ImGuiCol_Header] = rgba(58, 110, 170, 0.45f);            // accent blue
    colors[ImGuiCol_HeaderHovered] = rgba(66, 125, 190, 0.7f);
    colors[ImGuiCol_HeaderActive] = rgba(74, 138, 210, 0.85f);
    colors[ImGuiCol_Button] = rgba(52, 62, 76, 0.85f);
    colors[ImGuiCol_ButtonHovered] = rgba(66, 125, 190, 0.8f);
    colors[ImGuiCol_ButtonActive] = rgba(74, 138, 210, 1.0f);
    colors[ImGuiCol_CheckMark] = rgba(110, 170, 240, 1.0f);
    colors[ImGuiCol_SliderGrab] = rgba(90, 150, 220, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = rgba(120, 175, 245, 1.0f);
    colors[ImGuiCol_Separator] = rgba(70, 80, 95, 0.6f);
    colors[ImGuiCol_ResizeGrip] = rgba(90, 150, 220, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = rgba(90, 150, 220, 0.6f);
    colors[ImGuiCol_ResizeGripActive] = rgba(90, 150, 220, 0.9f);
    colors[ImGuiCol_ScrollbarBg] = rgba(0, 0, 0, 0.0f);
}

} // namespace Haki
