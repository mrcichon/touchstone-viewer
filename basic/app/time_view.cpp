#include "views.h"

#include "implot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

using namespace core;

std::string number(double v) {
    char text[32];
    std::snprintf(text, sizeof text, "%g", v);
    return text;
}

std::string window_in_words(const WindowSpec& w) {
    std::string text = window_name(w.kind);
    if (w.kind == WindowKind::kaiser) text += " (beta " + number(w.parameter) + ")";
    if (w.kind == WindowKind::tukey) text += " (taper " + number(w.parameter) + ")";
    return text;
}

std::string gate_range(const GatePosition& g) {
    auto rounded = [](double v) { return number(std::round(v * 100.0) / 100.0); };
    return rounded(g.center_ns - 0.5 * g.span_ns) + " to " + rounded(g.center_ns + 0.5 * g.span_ns) + " ns";
}

std::string processing_note(const Session& session, const GatePosition* position) {
    std::string note;
    if (session.gate_on) {
        const GateSpec& g = session.gate;
        note = std::string("Gate: ") + (g.mode == GateMode::bandpass ? "bandpass" : "bandstop") + (position ? " " + gate_range(*position) : "") + ", window " +
               window_in_words(g.window) + ", ";
        if (g.method == GateMethod::time_multiply) {
            note += "multiplication in time";
        } else {
            const char* edges[] = {"reflect", "repeat edge value", "zero", "wrap"};
            note += std::string(g.window.periodic ? "" : "symmetric window, ") + "kernel convolution, edge extension " + edges[static_cast<int>(g.edge)];
        }
    }
    const TimeResponseSpec& t = session.time_response;
    if (t.window.kind != WindowKind::boxcar || t.pad > 1) {
        note += note.empty() ? "Time response: " : ". Time response: ";
        if (t.window.kind != WindowKind::boxcar) note += "window " + window_in_words(t.window);
        if (t.pad > 1) note += std::string(t.window.kind != WindowKind::boxcar ? ", " : "") + "zero padding " + number(t.pad) + "x";
    }
    return note;
}

bool window_controls(const char* id, WindowSpec& window) {
    bool changed = false;
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##kind", window_name(window.kind))) {
        for (WindowKind kind : {WindowKind::boxcar, WindowKind::hann, WindowKind::hamming, WindowKind::blackman, WindowKind::kaiser, WindowKind::tukey}) {
            if (ImGui::Selectable(window_name(kind), kind == window.kind)) {
                window.kind = kind;
                window.parameter = kind == WindowKind::tukey ? 0.5 : 6.0;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    if (window.kind == WindowKind::kaiser || window.kind == WindowKind::tukey) {
        bool kaiser = window.kind == WindowKind::kaiser;
        float value = static_cast<float>(window.parameter);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("##parameter", &value, 0.0f, kaiser ? 20.0f : 1.0f, kaiser ? "beta %.1f" : "taper %.2f")) {
            window.parameter = static_cast<double>(value);
            changed = true;
        }
    }
    ImGui::PopID();
    return changed;
}

template <typename Value>
bool choice(const char* id, Value& value, std::initializer_list<std::pair<Value, const char*>> options) {
    bool changed = false;
    const char* current = "";
    for (const auto& o : options)
        if (o.first == value) current = o.second;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo(id, current)) {
        for (const auto& o : options)
            if (ImGui::Selectable(o.second, o.first == value)) {
                value = o.first;
                changed = true;
            }
        ImGui::EndCombo();
    }
    return changed;
}

bool draw_controls(Session& session) {
    bool changed = false;
    GateSpec& gate = session.gate;

    ImGui::SeparatorText("Gate");
    changed |= ImGui::Checkbox("Gate on", &session.gate_on);
    ImGui::BeginDisabled(!session.gate_on);
    float field_width = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("S22  ").x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    bool one_row = field_width >= ImGui::CalcTextSize("center 0.000 ns").x + 2.0f * ImGui::GetStyle().FramePadding.x;
    for (Parameter p : shown_panels(session)) {
        GatePosition& position = gate_position(session, p);
        std::string name = parameter_name(p);
        ImGui::PushID(name.c_str());
        if (one_row) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(name.c_str());
            ImGui::SameLine(ImGui::CalcTextSize("S22  ").x);
        }
        ImGui::SetNextItemWidth(one_row ? field_width : -1.0f);
        bool moved = ImGui::DragScalar("##center", ImGuiDataType_Double, &position.center_ns, 0.05f, nullptr, nullptr, one_row ? "center %.3f ns" : (name + " center %.3f ns").c_str());
        if (one_row) ImGui::SameLine();
        ImGui::SetNextItemWidth(one_row ? field_width : -1.0f);
        moved |= ImGui::DragScalar("##span", ImGuiDataType_Double, &position.span_ns, 0.05f, nullptr, nullptr, one_row ? "span %.3f ns" : (name + " span %.3f ns").c_str());
        position.span_ns = std::max(position.span_ns, 0.0);
        if (moved) ++position.version;
        ImGui::PopID();
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    if (ImGui::TreeNodeEx("Advanced", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen)) {
        ImGui::BeginDisabled(!session.gate_on);
        ImGui::TextWrapped("Gate method");
        changed |= choice("##method", gate.method, {{GateMethod::kernel_convolution, "Kernel convolution"}, {GateMethod::time_multiply, "Multiplication in time"}});
        ImGui::TextWrapped("Gate window");
        changed |= window_controls("Gate window", gate.window);
        ImGui::TextWrapped("Gate mode");
        changed |= choice("##mode", gate.mode, {{GateMode::bandpass, "Bandpass (keep inside)"}, {GateMode::bandstop, "Bandstop (remove inside)"}});
        if (gate.method == GateMethod::kernel_convolution) {
            ImGui::TextWrapped("Band edge extension");
            changed |= choice("##edge", gate.edge, {{GateEdge::reflect, "Reflect"}, {GateEdge::nearest, "Repeat edge value"}, {GateEdge::zero, "Zero"}, {GateEdge::wrap, "Wrap"}});
            changed |= ImGui::Checkbox("Periodic window", &gate.window.periodic);
            ImGui::SetItemTooltip("The window is the first n samples of a symmetric window of n+1 samples.");
        }
        ImGui::EndDisabled();
        ImGui::Spacing();
        ImGui::TextWrapped("Time response window");
        changed |= window_controls("response window", session.time_response.window);
        ImGui::TextWrapped("Zero padding");
        changed |= choice("##pad", session.time_response.pad, {{1, "None"}, {2, "2x"}, {4, "4x"}, {8, "8x"}});
        ImGui::Spacing();
        if (ImGui::Button("Default settings")) {
            gate = GateSpec{};
            session.time_response = TimeResponseSpec{};
            changed = true;
        }
    }
    return changed;
}

void draw_gate_band(GatePosition& gate) {
    double lo = gate.center_ns - 0.5 * gate.span_ns, hi = gate.center_ns + 0.5 * gate.span_ns, center = gate.center_ns;
    ImPlotRect limits = ImPlot::GetPlotLimits();
    ImPlot::PushPlotClipRect();
    ImPlot::GetPlotDrawList()->AddRectFilled(ImPlot::PlotToPixels(lo, limits.Y.Max), ImPlot::PlotToPixels(hi, limits.Y.Min), IM_COL32(40, 110, 220, 28));
    ImPlot::PopPlotClipRect();
    const ImVec4 edge_color(0.16f, 0.43f, 0.86f, 1.0f), center_color(0.16f, 0.43f, 0.86f, 0.35f);
    bool moved_edge = ImPlot::DragLineX(1, &lo, edge_color, 1.5f, ImPlotDragToolFlags_NoFit);
    moved_edge |= ImPlot::DragLineX(2, &hi, edge_color, 1.5f, ImPlotDragToolFlags_NoFit);
    bool moved_center = ImPlot::DragLineX(3, &center, center_color, 1.0f, ImPlotDragToolFlags_NoFit);
    if (moved_edge) {
        if (lo > hi) std::swap(lo, hi);
        gate.center_ns = 0.5 * (lo + hi);
        gate.span_ns = hi - lo;
    } else if (moved_center) {
        gate.center_ns = center;
    }
    if (moved_edge || moved_center) ++gate.version;
}

}  // namespace

void draw_time_view(Session& session) {
    if (session.curves.empty()) {
        draw_drop_hint();
        return;
    }
    compute_time_arrays(session);

    bool any_visible = false, any_response = false;
    for (const auto& c : session.curves) {
        any_visible |= c->visible;
        for (const Curve::TimeArrays& t : c->time) any_response |= c->visible && !t.time_db.empty();
    }
    std::vector<Parameter> panels = shown_panels(session);
    bool one_panel = panels.size() == 1;
    std::string note = (any_visible && !any_response) ? "No time response. The visible files have no phase or do not have these parameters."
                                                      : processing_note(session, one_panel ? &gate_position(session, panels[0]) : nullptr);

    if (std::string highlight = order_highlight_note(session); !highlight.empty()) note += (note.empty() ? "" : "\n") + highlight;

    bool settings = session.settings_panel_open && legend_place_in_use(session) != LegendPlace::beside;
    float settings_panel_width = std::min(session.settings_panel_width, ImGui::GetContentRegionAvail().x * 0.45f);
    float settings_width = settings ? settings_panel_width + panel_edge_width : 0.0f;
    session.plots_min = ImGui::GetCursorScreenPos();
    draw_title(session, ImGui::GetContentRegionAvail().x - settings_width, note);
    ImVec2 room = ImGui::GetContentRegionAvail();
    room.x -= settings_width;
    int columns = static_cast<int>(panels.size()), rows = session.gate_on ? 3 : 2;
    if (room.x < 80.0f * static_cast<float>(columns) || room.y < 60.0f * static_cast<float>(rows)) return;
    LegendSpace legend = legend_space(session, room);
    float plots_width = room.x - legend.width_beside;
    ImVec2 corner = ImGui::GetCursorScreenPos();
    session.plots_max = ImVec2(corner.x + room.x, corner.y + room.y);

    FrequencyUnit unit = frequency_unit(session);
    std::string frequency_label = std::string("Frequency [") + unit.name + "]";
    std::vector<AxisRange*> frequency_ranges, time_ranges;
    std::vector<bool> time_standard_view;
    for (Parameter p : panels) {
        std::string name = parameter_name(p);
        frequency_ranges.push_back(&session.x_ranges["time view, measured " + name]);
        frequency_ranges.push_back(&session.x_ranges["time view, gated " + name]);
        AxisRange& time_range = session.x_ranges["time view, time " + name];
        // The fit of ImPlot shows the full time axis, which is much longer than the standard view.
        // A double-click therefore clears `fitted`, and the standard view is set here.
        time_standard_view.push_back(!time_range.fitted || session.fit_time_plots);
        if (time_standard_view.back()) time_range = AxisRange{0.0, 50.0, true};
        time_ranges.push_back(&time_range);
    }
    std::vector<AxisRange> frequency_before, time_before;
    for (AxisRange* r : frequency_ranges) frequency_before.push_back(*r);
    for (AxisRange* r : time_ranges) time_before.push_back(*r);

    const ImPlotFlags plot_flags = ImPlotFlags_NoLegend | ImPlotFlags_NoMenus | (session.copying ? ImPlotFlags_NoMouseText : 0);
    bool changed = false, plot_hovered = false;
    std::vector<PanelArea> areas;
    auto frequency_panel = [&](const std::string& title, const std::string& y_range_name, const std::string& y_label, AxisRange& range, const Panel& panel) {
        if (session.fit_time_plots || !range.fitted) ImPlot::SetNextAxesToFit();
        range.fitted = true;
        if (!ImPlot::BeginPlot(title.c_str(), ImVec2(-1, 0), plot_flags)) return;
        ImPlot::SetupAxes(frequency_label.c_str(), y_label.c_str());
        ImPlot::SetupAxisLinks(ImAxis_X1, &range.lo, &range.hi);
        AxisRange& y_range = session.y_ranges[y_range_name];
        ImPlot::SetupAxisLinks(ImAxis_Y1, &y_range.lo, &y_range.hi);
        ImPlot::SetupAxisFormat(ImAxis_X1, format_frequency, &unit);
        plot_curves(session, panel);
        plot_hovered |= ImPlot::IsPlotHovered();
        areas.push_back(current_panel_area(title.substr(title.find("###") == std::string::npos ? 0 : title.find("###") + 3)));
        ImPlot::EndPlot();
    };
    begin_plots_without_mouse_marks(session);
    if (ImPlot::BeginSubplots("##time", rows, columns, ImVec2(plots_width, room.y), ImPlotSubplotFlags_NoTitle | ImPlotSubplotFlags_ColMajor)) {
        for (size_t i = 0; i < panels.size(); ++i) {
            Parameter p = panels[i];
            std::string name = parameter_name(p), y_label = "|" + name + "| [dB]";
            frequency_panel(name + " measured", "time view, measured " + name, y_label, *frequency_ranges[2 * i], Panel{Trace::frequency_db, p, unit.hz, unit.name, "dB"});

            if (time_standard_view[i]) ImPlot::SetNextAxisToFit(ImAxis_Y1);
            if (ImPlot::BeginPlot((name + " time response").c_str(), ImVec2(-1, 0), plot_flags)) {
                ImPlot::SetupAxes("Time [ns]", "[dB]", ImPlotAxisFlags_None, ImPlotAxisFlags_RangeFit);
                ImPlot::SetupAxisLinks(ImAxis_X1, &time_ranges[i]->lo, &time_ranges[i]->hi);
                AxisRange& y_range = session.y_ranges["time view, time " + name];
                ImPlot::SetupAxisLinks(ImAxis_Y1, &y_range.lo, &y_range.hi);
                plot_curves(session, Panel{Trace::time_db, p, 1.0, "ns", "dB"});
                plot_hovered |= ImPlot::IsPlotHovered();
                if (ImPlot::IsPlotHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) time_ranges[i]->fitted = false;
                areas.push_back(current_panel_area(name + " time response"));
                if (session.gate_on) draw_gate_band(gate_position(session, p));
                ImPlot::EndPlot();
            }
            if (session.gate_on) {
                // The text after ### identifies the plot. It does not change when the gate moves.
                std::string title = name + " gated" + (one_panel ? "" : ", " + gate_range(gate_position(session, p))) + "###" + name + " gated";
                frequency_panel(title, "time view, gated " + name, y_label, *frequency_ranges[2 * i + 1], Panel{Trace::gated_db, p, unit.hz, unit.name, "dB"});
            }
        }
        ImPlot::EndSubplots();
    }
    session.fit_time_plots = plot_hovered && ImGui::GetIO().KeyCtrl && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    share_x_range_with_ctrl(frequency_ranges, frequency_before);
    share_x_range_with_ctrl(time_ranges, time_before);
    draw_legend(session, legend.place, areas, ImVec2(corner.x + plots_width, corner.y), ImVec2(corner.x + room.x, corner.y + room.y));
    end_plots_without_mouse_marks(session);

    if (settings) {
        ImGui::SetCursorScreenPos(ImVec2(corner.x + room.x, corner.y));
        float moved = draw_panel_edge("settings edge", room.y);
        if (std::abs(moved) > 0.0f) session.settings_panel_width = std::clamp(settings_panel_width - moved, 130.0f * ui_scale(), 600.0f * ui_scale());
        ImGui::SetCursorScreenPos(ImVec2(corner.x + room.x + panel_edge_width, corner.y));
        ImGui::BeginChild("time settings", ImVec2(settings_panel_width, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoSavedSettings);
        changed |= draw_controls(session);
        ImGui::EndChild();
    }
    if (changed) ++session.time_settings_version;
}
