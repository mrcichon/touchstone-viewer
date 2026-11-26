#include "views.h"

#include "implot.h"

#include <algorithm>
#include <cstdio>
#include <vector>

void draw_title(const Session& session, float width, const std::string& note) {
    auto centred = [&](const std::string& text) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - ImGui::CalcTextSize(text.c_str()).x) * 0.5f);
        ImGui::TextUnformatted(text.c_str());
    };
    if (!session.title.empty()) {
        ImGui::PushFont(nullptr, 21.0f);
        centred(session.title);
        ImGui::PopFont();
    }
    if (!note.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.30f, 0.30f, 0.34f, 1.0f));
        for (size_t start = 0, end = 0; end < note.size(); start = end + 1) {
            end = std::min(note.find('\n', start), note.size());
            centred(note.substr(start, end - start));
        }
        ImGui::PopStyleColor();
    }
}

void draw_drop_hint() {
    ImVec2 room = ImGui::GetContentRegionAvail();
    const char* hint = "No files. Drop files or folders on the window.";
    ImGui::SetCursorPos(ImVec2((room.x - ImGui::CalcTextSize(hint).x) * 0.5f, room.y * 0.45f));
    ImGui::TextDisabled("%s", hint);
}

int format_frequency(double value, char* buffer, int size, void* user_data) {
    const FrequencyUnit* unit = static_cast<const FrequencyUnit*>(user_data);
    return std::snprintf(buffer, static_cast<size_t>(size), "%g", value / unit->hz);
}

void share_x_range_with_ctrl(const std::vector<AxisRange*>& ranges, const std::vector<AxisRange>& before) {
    if (!ImGui::GetIO().KeyCtrl) return;
    auto differs = [](double a, double b) { return a < b || a > b; };
    for (size_t i = 0; i < ranges.size(); ++i) {
        if (!differs(ranges[i]->lo, before[i].lo) && !differs(ranges[i]->hi, before[i].hi)) continue;
        for (AxisRange* other : ranges) {
            other->lo = ranges[i]->lo;
            other->hi = ranges[i]->hi;
        }
        return;
    }
}

void begin_plots_without_mouse_marks(const Session& session) {
    if (!session.copying) return;
    ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0f);
    ImGui::BeginDisabled();
}
void end_plots_without_mouse_marks(const Session& session) {
    if (!session.copying) return;
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
}

void draw_frequency_view(Session& session) {
    if (session.curves.empty()) {
        draw_drop_hint();
        return;
    }
    bool phase = session.quantity == Quantity::phase_deg;
    if (phase) compute_phase(session);

    std::vector<Parameter> panels = shown_panels(session);
    int rows = static_cast<int>(panels.size());

    session.plots_min = ImGui::GetCursorScreenPos();
    draw_title(session, ImGui::GetContentRegionAvail().x, order_highlight_note(session));
    ImVec2 room = ImGui::GetContentRegionAvail();
    if (room.x < 80.0f || room.y < 40.0f * static_cast<float>(rows)) return;
    LegendSpace legend = legend_space(session, room);
    float plots_width = room.x - legend.width_beside;
    ImVec2 corner = ImGui::GetCursorScreenPos();
    session.plots_max = ImVec2(corner.x + room.x, corner.y + room.y);

    FrequencyUnit unit = frequency_unit(session);
    char x_label[32];
    std::snprintf(x_label, sizeof x_label, "Frequency [%s]", unit.name);
    std::vector<PanelArea> areas;
    std::vector<AxisRange*> ranges;
    std::vector<AxisRange> before;
    for (Parameter p : panels) {
        ranges.push_back(&session.x_ranges[parameter_name(p)]);
        before.push_back(*ranges.back());
    }
    bool plot_hovered = false;
    begin_plots_without_mouse_marks(session);
    if (ImPlot::BeginSubplots("##frequency", rows, 1, ImVec2(plots_width, room.y), ImPlotSubplotFlags_NoTitle)) {
        for (int i = 0; i < rows; ++i) {
            Parameter p = panels[static_cast<size_t>(i)];
            std::string name = parameter_name(p);
            bool bottom = i == rows - 1;
            AxisRange& range = *ranges[static_cast<size_t>(i)];
            if (session.fit_frequency_plots || !range.fitted) ImPlot::SetNextAxesToFit();
            range.fitted = true;
            if (ImPlot::BeginPlot(("##" + name).c_str(), ImVec2(-1, 0), ImPlotFlags_NoLegend | ImPlotFlags_NoMenus | (session.copying ? ImPlotFlags_NoMouseText : 0))) {
                std::string y_label = phase ? "phase " + name + " [deg]" : "|" + name + "| [dB]";
                ImPlot::SetupAxis(ImAxis_X1, bottom ? x_label : nullptr);
                ImPlot::SetupAxis(ImAxis_Y1, y_label.c_str());
                ImPlot::SetupAxisLinks(ImAxis_X1, &range.lo, &range.hi);
                AxisRange& y_range = session.y_ranges[name];
                ImPlot::SetupAxisLinks(ImAxis_Y1, &y_range.lo, &y_range.hi);
                ImPlot::SetupAxisFormat(ImAxis_X1, format_frequency, &unit);
                plot_curves(session, Panel{phase ? Trace::frequency_deg : Trace::frequency_db, p, unit.hz, unit.name, phase ? "deg" : "dB"});
                plot_hovered |= ImPlot::IsPlotHovered();
                areas.push_back(current_panel_area(name));
                ImPlot::EndPlot();
            }
        }
        ImPlot::EndSubplots();
    }
    session.fit_frequency_plots = plot_hovered && ImGui::GetIO().KeyCtrl && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    share_x_range_with_ctrl(ranges, before);
    draw_legend(session, legend.place, areas, ImVec2(corner.x + plots_width, corner.y), ImVec2(corner.x + room.x, corner.y + room.y));
    end_plots_without_mouse_marks(session);
}
