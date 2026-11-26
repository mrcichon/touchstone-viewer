#include "views.h"

#include "implot.h"
#include "misc/cpp/imgui_stdlib.h"
#include "palette.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

struct Layout {
    int columns = 1, rows = 0;
    float column_width = 0, row_height = 0, sample_width = 0, sample_gap = 0, padding = 0, label_room = 0;
    float value_width = 0;
    bool scrolls = false;
    ImVec2 size{0, 0};
};

Layout layout_for(const Session& session, ImVec2 limit, ImVec2 fixed) {
    Layout l;
    const float scrollbar_width = ImGui::GetStyle().ScrollbarSize;
    int count = visible_curve_count(session);
    ImGui::PushFont(nullptr, session.legend_text_size);
    float text = ImGui::GetFontSize();
    l.sample_width = 2.0f * text;
    l.sample_gap = 0.5f * text;
    l.padding = 0.6f * text;
    l.row_height = std::round(1.35f * text);
    float longest = 0.0f;
    for (const auto& c : session.curves)
        if (c->visible) longest = std::max(longest, ImGui::CalcTextSize(c->shown_label().c_str()).x);
    if (session.median_shown && session.median_range.on) l.value_width = ImGui::CalcTextSize(("-000.00 " + session.median_unit + "  ").c_str()).x;
    ImGui::PopFont();

    float width = (fixed.x > 0.0f ? std::min(fixed.x, limit.x) : limit.x) - 2.0f * l.padding;
    float height = (fixed.y > 0.0f ? std::min(fixed.y, limit.y) : limit.y) - 2.0f * l.padding;
    const float full_column = l.sample_width + l.sample_gap + std::min(longest, 18.0f * text) + l.value_width + text;
    const float narrowest_column = std::min(l.sample_width + l.sample_gap + 2.0f * text, full_column);
    int rows_that_fit = std::max(1, static_cast<int>(height / l.row_height));
    auto fill = [&](float usable_width) {
        l.column_width = std::clamp(usable_width, narrowest_column, full_column);
        int columns_that_fit = std::max(1, static_cast<int>(usable_width / l.column_width));
        l.columns = std::clamp((count + rows_that_fit - 1) / rows_that_fit, 1, columns_that_fit);
        l.rows = (count + l.columns - 1) / l.columns;
        l.scrolls = l.rows > rows_that_fit;
    };
    fill(width);
    if (l.scrolls) fill(width - scrollbar_width);
    l.label_room = l.column_width - l.sample_width - l.sample_gap - l.value_width - 0.5f * text;
    l.size.x = fixed.x > 0.0f ? std::min(fixed.x, limit.x) : static_cast<float>(l.columns) * l.column_width + 2.0f * l.padding + (l.scrolls ? scrollbar_width : 0.0f);
    l.size.y = fixed.y > 0.0f ? std::min(fixed.y, limit.y) : static_cast<float>(std::min(l.rows, rows_that_fit)) * l.row_height + 2.0f * l.padding;
    return l;
}

Layout layout_inside(const Session& session, ImVec2 panel) {
    bool sized_by_user = session.legend_box.x > 0.0f;
    ImVec2 limit = sized_by_user ? ImVec2(panel.x * 0.95f, panel.y * 0.95f) : ImVec2(panel.x * 0.55f, panel.y * 0.85f);
    return layout_for(session, limit, session.legend_box);
}

Layout layout_beside(const Session& session, ImVec2 room) {
    if (session.legend_beside_width > 0.0f) return layout_for(session, ImVec2(room.x * 0.6f, room.y), ImVec2(session.legend_beside_width, room.y));
    Layout l = layout_for(session, ImVec2(room.x * 0.4f, room.y), ImVec2(0.0f, 0.0f));
    l.size.y = room.y;
    return l;
}

std::string fitted(const std::string& label, float max_width) {
    if (ImGui::CalcTextSize(label.c_str()).x <= max_width) return label;
    auto starts_character = [&](size_t i) { return i >= label.size() || (static_cast<unsigned char>(label[i]) & 0xC0) != 0x80; };
    for (size_t kept = label.size() - 1; kept > 1; --kept) {
        size_t head = (kept + 1) / 2, tail_start = label.size() - kept / 2;
        while (head > 0 && !starts_character(head)) --head;
        while (!starts_character(tail_start)) ++tail_start;
        std::string shortened = label.substr(0, head) + "..." + label.substr(tail_start);
        if (ImGui::CalcTextSize(shortened.c_str()).x <= max_width) return shortened;
    }
    return "...";
}

// The mouse position is tested directly: an ImGui item at this position changes the scroll range.
bool resize_by_corner(Session& session, ImVec2 window_min, ImVec2 window_max, bool anchored_right, bool anchored_bottom) {
    static bool dragging = false;
    const float reach = 16.0f * ui_scale();
    ImVec2 corner(anchored_right ? window_min.x : window_max.x, anchored_bottom ? window_min.y : window_max.y);
    float into_x = anchored_right ? 1.0f : -1.0f, into_y = anchored_bottom ? 1.0f : -1.0f;
    ImVec2 box_min(std::min(corner.x, corner.x + into_x * reach), std::min(corner.y, corner.y + into_y * reach));
    ImVec2 box_max(box_min.x + reach, box_min.y + reach);
    bool over = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseHoveringRect(box_min, box_max);
    if (over && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        session.legend_box = ImVec2(0.0f, 0.0f);
        dragging = false;
    } else if (over && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        dragging = true;
        session.legend_box = ImVec2(window_max.x - window_min.x, window_max.y - window_min.y);
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragging = false;
    if (over || dragging) ImGui::SetMouseCursor(anchored_right == anchored_bottom ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeNESW);
    if (over && !dragging) ImGui::SetTooltip("Drag to set the legend size.\nDouble-click for automatic size.");
    if (dragging) {
        ImVec2 moved = ImGui::GetIO().MouseDelta;
        session.legend_box.x = std::max(session.legend_box.x - into_x * moved.x, 60.0f * ui_scale());
        session.legend_box.y = std::max(session.legend_box.y - into_y * moved.y, 30.0f * ui_scale());
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImU32 color = (over || dragging) ? IM_COL32(41, 110, 219, 255) : IM_COL32(0, 0, 0, 90);
    draw->PushClipRect(window_min, window_max);
    draw->AddTriangleFilled(corner, ImVec2(corner.x + into_x * 0.7f * reach, corner.y), ImVec2(corner.x, corner.y + into_y * 0.7f * reach), color);
    draw->PopClipRect();
    return over || dragging;
}

bool move_pressed = false;
bool moving = false;
bool moved_in_this_press = false;

void move_by_drag(Session& session, bool mouse_on_legend, const std::vector<PanelArea>& panels) {
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        moved_in_this_press = false;
        move_pressed = mouse_on_legend;
    }
    if (move_pressed && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 6.0f)) moving = moved_in_this_press = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) move_pressed = moving = false;
    if (!moving || panels.empty()) return;
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    ImVec2 mouse = ImGui::GetMousePos();
    float right_end = panels[0].max.x;
    for (const PanelArea& p : panels) right_end = std::max(right_end, p.max.x);
    if (mouse.x > right_end + 4.0f) {
        set_legend_place(session, LegendPlace::beside);
        session.settings_panel_open = false;
        return;
    }
    for (const PanelArea& p : panels) {
        if (mouse.x < p.min.x || mouse.x > p.max.x || mouse.y < p.min.y || mouse.y > p.max.y) continue;
        bool right = mouse.x > 0.5f * (p.min.x + p.max.x), bottom = mouse.y > 0.5f * (p.min.y + p.max.y);
        set_legend_place(session, right ? (bottom ? LegendPlace::bottom_right : LegendPlace::top_right) : (bottom ? LegendPlace::bottom_left : LegendPlace::top_left));
        legend_position(session).panel = p.name;
        return;
    }
}

void draw_entries(Session& session, const Layout& l) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorPos();
    int index = 0;
    for (auto& c : session.curves) {
        if (!c->visible) continue;
        int column = index / l.rows, row = index % l.rows;
        ++index;
        ImGui::SetCursorPos(ImVec2(origin.x + static_cast<float>(column) * l.column_width, origin.y + static_cast<float>(row) * l.row_height));
        ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::PushID(c->id);
        bool clicked = ImGui::InvisibleButton("entry", ImVec2(l.column_width - l.sample_gap, l.row_height));
        if (ImGui::IsItemHovered()) {
            session.highlighted_curve_next = c->id;
            draw->AddRectFilled(ImVec2(at.x - 3.0f, at.y), ImVec2(at.x + l.column_width - l.sample_gap, at.y + l.row_height), IM_COL32(0, 0, 0, 14), 3.0f);
            if (c->shown_label() != c->name) ImGui::SetItemTooltip("%s", c->name.c_str());
        }
        if ((clicked && !moved_in_this_press) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            session.edited_curve = c->id;
            session.open_editor = true;
        }
        ImGui::PopID();

        bool dimmed = session.highlighted_curve && session.highlighted_curve != c->id;
        ImVec4 line_color = c->color;
        if (dimmed) line_color.w = 0.30f;
        float middle = at.y + l.row_height * 0.5f;
        draw_line_sample(draw, ImVec2(at.x + 1.0f, middle), ImVec2(at.x + l.sample_width, middle), ImGui::ColorConvertFloat4ToU32(line_color),
                         std::clamp(c->width + (session.highlighted_curve == c->id ? 1.2f : 0.0f), 1.2f, 4.5f), c->style);
        draw->AddText(ImVec2(at.x + l.sample_width + l.sample_gap, at.y + (l.row_height - ImGui::GetFontSize()) * 0.5f),
                      dimmed ? IM_COL32(40, 40, 45, 110) : IM_COL32(30, 30, 35, 255), fitted(c->shown_label(), l.label_room).c_str());
        auto median = session.medians.find(c->id);
        if (l.value_width > 0.0f && median != session.medians.end()) {
            char value[48];
            std::snprintf(value, sizeof value, "%.2f %s", median->second, session.median_unit.c_str());
            float x = at.x + l.column_width - l.sample_gap - ImGui::CalcTextSize(value).x - 0.3f * ImGui::GetFontSize();
            draw->AddText(ImVec2(x, at.y + (l.row_height - ImGui::GetFontSize()) * 0.5f), dimmed ? IM_COL32(40, 40, 45, 90) : IM_COL32(90, 90, 100, 255), value);
        }
    }
}

}  // namespace

LegendSpace legend_space(const Session& session, ImVec2 room) {
    LegendSpace space;
    space.place = visible_curve_count(session) ? legend_place_in_use(session) : LegendPlace::off;
    if (space.place == LegendPlace::beside) space.width_beside = layout_beside(session, room).size.x + panel_edge_width;
    return space;
}

PanelArea current_panel_area(const std::string& name) {
    ImVec2 min = ImPlot::GetPlotPos(), size = ImPlot::GetPlotSize();
    return {name, min, ImVec2(min.x + size.x, min.y + size.y)};
}

void draw_legend(Session& session, LegendPlace place, const std::vector<PanelArea>& panels, ImVec2 column_min, ImVec2 column_max) {
    if (place == LegendPlace::off || panels.empty()) {
        move_pressed = moving = false;
        return;
    }
    bool beside = place == LegendPlace::beside;
    const PanelArea* panel = &panels[0];
    for (const PanelArea& p : panels)
        if (p.name == legend_position(session).panel) panel = &p;
    ImVec2 panel_min = panel->min, panel_max = panel->max;
    Layout l;
    ImVec2 corner;
    bool anchored_right = beside || place == LegendPlace::top_right || place == LegendPlace::bottom_right;
    bool anchored_bottom = place == LegendPlace::bottom_right || place == LegendPlace::bottom_left;
    if (beside) {
        ImVec2 box(column_max.x - column_min.x - panel_edge_width, column_max.y - column_min.y);
        l = layout_for(session, box, box);
        ImGui::SetCursorScreenPos(column_min);
        float moved = draw_panel_edge("legend edge", box.y);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) session.legend_beside_width = 0.0f;
        else if (std::abs(moved) > 0.0f) session.legend_beside_width = std::clamp(box.x - moved, 80.0f * ui_scale(), 900.0f * ui_scale());
        corner = ImVec2(column_min.x + panel_edge_width, column_min.y);
    } else {
        const float inset = 10.0f * ui_scale();
        l = layout_inside(session, ImVec2(panel_max.x - panel_min.x, panel_max.y - panel_min.y));
        corner.x = anchored_right ? panel_max.x - inset - l.size.x : panel_min.x + inset;
        corner.y = anchored_bottom ? panel_max.y - inset - l.size.y : panel_min.y + inset;
    }

    // A child window: it gets the mouse before the plot and has its own scroll bar.
    ImGui::SetCursorScreenPos(corner);
    ImGui::PushFont(nullptr, session.legend_text_size);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, beside ? 1.0f : 0.90f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.80f, 0.80f, 0.82f, beside ? 0.0f : 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(l.padding, l.padding));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | (l.scrolls ? 0 : ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    bool mouse_on_legend = false;
    if (ImGui::BeginChild("legend", l.size, ImGuiChildFlags_Borders, flags)) {
        draw_entries(session, l);
        ImVec2 window_min = ImGui::GetWindowPos(), window_max(window_min.x + l.size.x, window_min.y + l.size.y);
        bool on_corner = !beside && !session.copying && resize_by_corner(session, window_min, window_max, anchored_right, anchored_bottom);
        bool on_scrollbar = l.scrolls && ImGui::GetMousePos().x > window_max.x - ImGui::GetStyle().ScrollbarSize;
        mouse_on_legend = !on_corner && !on_scrollbar && ImGui::IsMouseHoveringRect(window_min, window_max) &&
                          ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImGui::PopFont();
    move_by_drag(session, mouse_on_legend, panels);
}

void draw_curve_editor(Session& session) {
    if (session.open_editor) ImGui::OpenPopup("curve editor");
    session.open_editor = false;
    if (!ImGui::BeginPopup("curve editor")) return;
    Curve* c = find_curve(session, session.edited_curve);
    if (!c) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const float scale = ui_scale();
    ImGui::SetNextItemWidth(330.0f * scale);
    ImGui::InputTextWithHint("##label", c->short_name.c_str(), &c->label);
    ImGui::SameLine();
    ImGui::TextDisabled("Label");

    ImGui::Spacing();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f * scale, 3.0f * scale));
    for (int slot = 0; slot < palette_swatch_count; ++slot) {
        ImGui::PushID(slot);
        if (ImGui::ColorButton("##swatch", palette_color(slot), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(24.5f * scale, 20.0f * scale)))
            c->color = palette_color(slot);
        ImGui::PopID();
        if (slot % 12 != 11) ImGui::SameLine();
    }
    ImGui::PopStyleVar();
    ImGui::ColorEdit3("Other color", &c->color.x, ImGuiColorEditFlags_NoInputs);

    ImGui::Spacing();
    ImGui::SetNextItemWidth(200.0f * scale);
    ImGui::SliderFloat("Width", &c->width, 0.5f, 5.0f, "%.1f px");
    int style = static_cast<int>(c->style);
    ImGui::RadioButton("Solid", &style, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Dashed", &style, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Dotted", &style, 2);
    c->style = static_cast<LineStyle>(style);

    ImGui::Separator();
    bool remove = false;
    if (ImGui::Button(c->visible ? "Hide" : "Show")) {
        c->visible = !c->visible;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove file")) remove = true;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 330.0f * scale);
    ImGui::TextDisabled("%s", c->data.path.c_str());
    ImGui::PopTextWrapPos();
    if (remove) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    if (remove) remove_curve(session, session.edited_curve);  // c is not valid after this line
}
