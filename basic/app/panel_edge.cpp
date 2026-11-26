#include "views.h"

float ui_scale() { return ImGui::GetStyle().FontScaleDpi; }

float draw_panel_edge(const char* id, float height) {
    ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(panel_edge_width, height));
    bool active = ImGui::IsItemActive(), hovered = ImGui::IsItemHovered();
    if (active || hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    ImU32 color = active || hovered ? IM_COL32(41, 110, 219, 255) : ImGui::GetColorU32(ImGuiCol_Border);
    float x = at.x + panel_edge_width * 0.5f;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(x, at.y), ImVec2(x, at.y + height), color, active || hovered ? 2.0f : 1.0f);
    return active ? ImGui::GetIO().MouseDelta.x : 0.0f;
}
