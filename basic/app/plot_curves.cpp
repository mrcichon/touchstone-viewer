#include "series.h"

#include "implot.h"
#include "implot_internal.h"
#include "line_renderer.h"
#include "log.h"
#include "median.h"
#include "misc/cpp/imgui_stdlib.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <vector>

Series series_of(const Curve& c, const Panel& panel) {
    const core::Measurement& m = c.data;
    if (panel.parameter.out_port > m.ports || panel.parameter.in_port > m.ports) return {};
    size_t k = static_cast<size_t>(m.parameter(panel.parameter.out_port, panel.parameter.in_port));
    if (panel.trace == Trace::time_db || panel.trace == Trace::gated_db) {
        if (k >= c.time.size()) return {};
        const Curve::TimeArrays& t = c.time[k];
        if (panel.trace == Trace::time_db) return {t.time_ns.data(), t.time_db.data(), t.time_db.size()};
        return {m.freq_hz.data(), t.gated_db.data(), t.gated_db.size()};
    }
    if (panel.trace == Trace::frequency_deg) {
        if (k >= c.deg.size()) return {};
        return {m.freq_hz.data(), c.deg[k].data(), c.deg[k].size()};
    }
    return {m.freq_hz.data(), c.db[k].data(), c.db[k].size()};
}

uint64_t version_of(const Curve& c, const Panel& panel) {
    if (panel.trace != Trace::time_db && panel.trace != Trace::gated_db) return 0;  // measured data does not change
    const Curve::TimeArrays& t = c.time[static_cast<size_t>(c.data.parameter(panel.parameter.out_port, panel.parameter.in_port))];
    return panel.trace == Trace::time_db ? t.version : (static_cast<uint64_t>(t.version) << 32) | t.gate_version;
}

namespace {

PanelData data_of(const Panel& panel) {
    if (panel.trace == Trace::time_db) return PanelData::time_response;
    return panel.trace == Trace::gated_db ? PanelData::gated : PanelData::measured;
}

// Opaque: a transparent line is darker where its segments overlap.
ImVec4 color_in_frame(const Curve& c, int highlighted) {
    if (!highlighted || c.id == highlighted) return c.color;
    const ImVec4 paper = ImPlot::GetStyle().Colors[ImPlotCol_PlotBg];
    const float part = 0.22f;
    return ImVec4(paper.x + (c.color.x - paper.x) * part, paper.y + (c.color.y - paper.y) * part, paper.z + (c.color.z - paper.z) * part, 1.0f);
}
float width_in_frame(const Curve& c, int highlighted) { return (c.width + (c.id == highlighted ? 1.2f : 0.0f)) * ui_scale(); }

// One part for each curve, in the order of session.curves. Hidden curves are included: the style hides them,
// so a change of visibility sends no points.
struct PanelPoints {
    struct Part {
        int curve;
        uint64_t version;
        size_t first, count;
    };
    int buffer = 0;
    std::vector<Part> parts;
    size_t count = 0;
    int last_used_frame = 0;
};
std::map<std::tuple<int, int, int>, PanelPoints> panel_points;

// A part keeps its position while all parts before it have the same curve and point count as before;
// it is then sent only if its values changed. All parts after the first difference are sent.
const PanelPoints* points_in_graphics_memory(const Session& session, const Panel& panel) {
    PanelPoints& held = panel_points[{static_cast<int>(panel.trace), panel.parameter.out_port, panel.parameter.in_port}];
    if (!held.buffer) held.buffer = line_renderer::make_buffer();
    held.last_used_frame = ImGui::GetFrameCount();

    std::vector<PanelPoints::Part> parts;
    std::vector<const Curve*> curves;
    std::vector<bool> send;
    bool same_so_far = true;
    size_t count = 0;
    for (const auto& c : session.curves) {
        Series d = series_of(*c, panel);
        if (d.n < 2) continue;
        PanelPoints::Part part{c->id, version_of(*c, panel), count, d.n};
        size_t i = parts.size();
        same_so_far = same_so_far && i < held.parts.size() && held.parts[i].curve == part.curve && held.parts[i].count == part.count;
        send.push_back(!same_so_far || held.parts[i].version != part.version);
        curves.push_back(c.get());
        parts.push_back(part);
        count += d.n;
    }
    if (std::find(send.begin(), send.end(), true) != send.end()) {
        if (count > line_renderer::capacity(held.buffer)) {
            held.parts.clear();
            if (!line_renderer::reserve(held.buffer, count)) return nullptr;
            send.assign(send.size(), true);  // a larger buffer starts empty
        }
        auto started = std::chrono::steady_clock::now();
        size_t sent = 0;
        std::vector<line_renderer::Point> points;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (!send[i]) continue;
            Series d = series_of(*curves[i], panel);
            points.resize(d.n);
            for (size_t k = 0; k < d.n; ++k) {
                line_renderer::split(d.x[k], points[k].x_hi, points[k].x_lo);
                line_renderer::split(d.y[k], points[k].y_hi, points[k].y_lo);
                points[k].curve = static_cast<float>(parts[i].curve);
            }
            line_renderer::set_points(held.buffer, parts[i].first, points.data(), d.n);
            sent += d.n;
        }
        const char* traces[] = {"dB", "phase", "time response", "gated"};
        log_line("points sent: %s %s, %zu of %zu points, %zu curves, %.1f ms, graphics memory %.0f MB", parameter_name(panel.parameter).c_str(), traces[static_cast<int>(panel.trace)],
                 sent, count, parts.size(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(),
                 static_cast<double>(line_renderer::totals().bytes_reserved) / (1024.0 * 1024.0));
    }
    held.parts = std::move(parts);
    held.count = count;
    return &held;
}

void start_frame(const Session& session) {
    static int frame = -1;
    if (frame == ImGui::GetFrameCount()) return;
    frame = ImGui::GetFrameCount();
    static std::vector<line_renderer::Style> styles;
    styles.assign(static_cast<size_t>(session.next_curve_id), line_renderer::Style{});
    const float pixels_per_unit = ImGui::GetIO().DisplayFramebufferScale.x;
    for (const auto& c : session.curves) {
        if (!c->visible || c->style != LineStyle::solid) continue;
        ImVec4 color = color_in_frame(*c, session.highlighted_curve);
        styles[static_cast<size_t>(c->id)] = {color.x, color.y, color.z, color.w, width_in_frame(*c, session.highlighted_curve) * pixels_per_unit, {0, 0, 0}};
    }
    line_renderer::set_styles(styles.data(), styles.size());
    for (auto held = panel_points.begin(); held != panel_points.end();) {
        if (frame - held->second.last_used_frame > 600) {
            line_renderer::delete_buffer(held->second.buffer);
            held = panel_points.erase(held);
        } else {
            ++held;
        }
    }
}

struct DrawCall {
    int buffer;
    size_t first, count;
    line_renderer::View view;
};
void run_draw_call(const ImDrawList*, const ImDrawCmd* command) {
    DrawCall call;
    std::memcpy(&call, command->UserCallbackData, sizeof call);  // the stored copy is not aligned
    line_renderer::draw(call.buffer, call.first, call.count, call.view);
}
void add_draw_call(DrawCall call) {
    ImDrawList* draw = ImPlot::GetPlotDrawList();
    draw->AddCallback(run_draw_call, &call, sizeof call);
    draw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void draw_patterned_line(const Series& d, const ImPlotRect& limits, ImU32 color, float width, LineStyle style) {
    const float on = style == LineStyle::dashed ? 7.0f * width : 1.2f * width;
    const float off = style == LineStyle::dashed ? 4.0f * width : 2.6f * width;
    size_t first = static_cast<size_t>(std::lower_bound(d.x, d.x + d.n, limits.X.Min) - d.x);
    size_t end = static_cast<size_t>(std::upper_bound(d.x, d.x + d.n, limits.X.Max) - d.x);
    if (first > 0) --first;
    if (end < d.n) ++end;
    if (end - first < 2) return;

    ImDrawList* draw = ImPlot::GetPlotDrawList();
    ImPlot::PushPlotClipRect();
    bool pen_down = true;
    float left_in_piece = on;
    ImVec2 a = ImPlot::PlotToPixels(d.x[first], d.y[first]);
    draw->PathLineTo(a);
    for (size_t i = first + 1; i < end; ++i) {
        ImVec2 b = ImPlot::PlotToPixels(d.x[i], d.y[i]);
        float length = std::hypot(b.x - a.x, b.y - a.y);
        while (length > left_in_piece) {
            float t = left_in_piece / length;
            a = ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
            length -= left_in_piece;
            draw->PathLineTo(a);
            if (pen_down) draw->PathStroke(color, 0, width);
            pen_down = !pen_down;
            left_in_piece = pen_down ? on : off;
        }
        left_in_piece -= length;
        a = b;
        if (pen_down) draw->PathLineTo(b);
    }
    if (pen_down) draw->PathStroke(color, 0, width);
    else draw->PathClear();
    ImPlot::PopPlotClipRect();
}

float distance_to_segment(ImVec2 p, ImVec2 a, ImVec2 b) {
    float dx = b.x - a.x, dy = b.y - a.y;
    float length2 = dx * dx + dy * dy;
    float t = length2 > 0.0f ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length2, 0.0f, 1.0f) : 0.0f;
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

void find_curve_under_mouse(Session& session, const Panel& panel) {
    if (session.copying || !ImPlot::IsPlotHovered() || ImGui::IsMouseDragging(ImGuiMouseButton_Left)) return;
    ImVec2 mouse = ImGui::GetMousePos();
    double mouse_x = ImPlot::GetPlotMousePos().x;
    float best = 7.0f;  // maximum distance in pixels
    const Curve* found = nullptr;
    size_t found_sample = 0;
    for (const auto& c : session.curves) {
        if (!c->visible) continue;
        Series d = series_of(*c, panel);
        if (d.n < 2) continue;
        size_t i = static_cast<size_t>(std::lower_bound(d.x, d.x + d.n, mouse_x) - d.x);
        size_t from = i > 2 ? i - 2 : 0, to = std::min(i + 2, d.n - 1);
        for (size_t k = from; k < to; ++k) {
            float distance = distance_to_segment(mouse, ImPlot::PlotToPixels(d.x[k], d.y[k]), ImPlot::PlotToPixels(d.x[k + 1], d.y[k + 1]));
            if (distance < best) {
                best = distance;
                found = c.get();
                found_sample = std::abs(d.x[k] - mouse_x) < std::abs(d.x[k + 1] - mouse_x) ? k : k + 1;
            }
        }
    }
    if (!found) return;
    session.highlighted_curve_next = found->id;
    Series d = series_of(*found, panel);
    if (ImGui::GetIO().KeyCtrl && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        Marker marker;
        marker.id = session.next_marker_id++;
        marker.curve = found->id;
        marker.parameter = panel.parameter;
        marker.data = data_of(panel);
        marker.x = d.x[found_sample];
        session.markers.push_back(marker);
    }
    if (session.highlighted_curve != found->id) return;
    ImGui::BeginTooltip();
    ImVec2 at = ImGui::GetCursorScreenPos();
    float line_height = ImGui::GetTextLineHeight();
    draw_line_sample(ImGui::GetWindowDrawList(), ImVec2(at.x, at.y + line_height * 0.5f), ImVec2(at.x + 22.0f, at.y + line_height * 0.5f),
                     ImGui::ColorConvertFloat4ToU32(found->color), std::max(found->width, 2.0f), found->style);
    ImGui::Dummy(ImVec2(24.0f, line_height));
    ImGui::SameLine();
    ImGui::TextUnformatted(found->label.empty() ? found->name.c_str() : found->label.c_str());
    ImGui::TextDisabled("%.6g %s    %.2f %s", d.x[found_sample] / panel.x_unit, panel.x_unit_name, d.y[found_sample], panel.y_unit_name);
    ImGui::EndTooltip();
}

size_t nearest_sample(const Series& d, double x) {
    size_t i = static_cast<size_t>(std::lower_bound(d.x, d.x + d.n, x) - d.x);
    if (i == 0) return 0;
    if (i == d.n) return d.n - 1;
    return x - d.x[i - 1] < d.x[i] - x ? i - 1 : i;
}

Panel panel_of(const Session& session, const Marker& marker) {
    if (marker.data == PanelData::time_response) return {Trace::time_db, marker.parameter, 1.0, "ns", "dB"};
    FrequencyUnit unit = frequency_unit(session);
    if (marker.data == PanelData::gated) return {Trace::gated_db, marker.parameter, unit.hz, unit.name, "dB"};
    bool phase = session.view == View::frequency && session.quantity == Quantity::phase_deg;
    return {phase ? Trace::frequency_deg : Trace::frequency_db, marker.parameter, unit.hz, unit.name, phase ? "deg" : "dB"};
}

struct ShownMarker {
    int id;
    ImU32 color;
    ImVec2 at, box_min, box_max;
    std::string text;
};

// The marker under the mouse moves to the end of the list, which is drawn on top.
void draw_markers(Session& session, const Panel& panel) {
    ImDrawList* draw = ImPlot::GetPlotDrawList();
    ImVec2 plot_min = ImPlot::GetPlotPos(), plot_size = ImPlot::GetPlotSize();
    ImVec2 plot_max(plot_min.x + plot_size.x, plot_min.y + plot_size.y);
    ImVec2 mouse = ImGui::GetMousePos();
    const float scale = ui_scale(), gap = 8.0f * scale, pad = 4.0f * scale;
    std::vector<ShownMarker> shown;
    for (const Marker& marker : session.markers) {
        if (marker.data != data_of(panel) || !(marker.parameter == panel.parameter)) continue;
        const Curve* c = find_curve(session, marker.curve);
        if (!c || !c->visible) continue;
        Series d = series_of(*c, panel);
        if (d.n == 0) continue;
        size_t sample = nearest_sample(d, marker.x);
        double x = d.x[sample], y = d.y[sample];
        ImVec2 at = ImPlot::PlotToPixels(x, y);
        if (at.x < plot_min.x || at.x > plot_max.x || at.y < plot_min.y || at.y > plot_max.y) continue;

        char values[96];
        std::snprintf(values, sizeof values, "%.6g %s   %.2f %s", x / panel.x_unit, panel.x_unit_name, y, panel.y_unit_name);
        std::string text = marker.label.empty() ? values : marker.label + "\n" + values;
        ImVec2 text_size = ImGui::CalcTextSize(text.c_str());
        ImVec2 box_size(text_size.x + 2.0f * pad, text_size.y + 2.0f * pad);
        ImVec2 box_min(at.x + gap, at.y - gap - box_size.y);
        if (box_min.x + box_size.x > plot_max.x) box_min.x = at.x - gap - box_size.x;
        if (box_min.y < plot_min.y) box_min.y = at.y + gap;
        ImVec2 box_max(box_min.x + box_size.x, box_min.y + box_size.y);
        shown.push_back({marker.id, ImGui::ColorConvertFloat4ToU32(c->color), at, box_min, box_max, text});
    }

    const ShownMarker* hovered = nullptr;
    if (ImPlot::IsPlotHovered())
        for (const ShownMarker& m : shown) {
            bool on_dot = std::hypot(mouse.x - m.at.x, mouse.y - m.at.y) < 8.0f * scale;
            bool on_box = mouse.x >= m.box_min.x && mouse.x <= m.box_max.x && mouse.y >= m.box_min.y && mouse.y <= m.box_max.y;
            if (on_dot || on_box) hovered = &m;
        }
    auto draw_marker = [&](const ShownMarker& m) {
        bool larger = &m == hovered;
        draw->AddLine(m.at, ImVec2(std::clamp(m.at.x, m.box_min.x, m.box_max.x), std::clamp(m.at.y, m.box_min.y, m.box_max.y)), m.color, 1.0f * scale);
        draw->AddRectFilled(m.box_min, m.box_max, IM_COL32(255, 255, 255, 235), 3.0f * scale);
        draw->AddRect(m.box_min, m.box_max, m.color, 3.0f * scale, 0, (larger ? 2.0f : 1.0f) * scale);
        draw->AddText(ImVec2(m.box_min.x + pad, m.box_min.y + pad), IM_COL32(30, 30, 35, 255), m.text.c_str());
        draw->AddCircleFilled(m.at, (larger ? 5.5f : 4.5f) * scale, m.color);
        draw->AddCircle(m.at, (larger ? 5.5f : 4.5f) * scale, IM_COL32(255, 255, 255, 255), 0, 1.5f * scale);
    };
    ImPlot::PushPlotClipRect();
    for (const ShownMarker& m : shown)
        if (&m != hovered) draw_marker(m);
    if (hovered) draw_marker(*hovered);
    ImPlot::PopPlotClipRect();
    if (!hovered) return;

    auto found = std::find_if(session.markers.begin(), session.markers.end(), [&](const Marker& m) { return m.id == hovered->id; });
    session.highlighted_curve_next = found->curve;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        session.edited_marker = found->id;
        session.open_marker_menu = true;
    }
    std::rotate(found, found + 1, session.markers.end());
}

void draw_median_range(Session& session, const Panel& panel) {
    static bool making = false;
    MedianRange& range = session.median_range;
    const ImGuiIO& io = ImGui::GetIO();
    if (ImPlot::IsPlotHovered() && io.KeyShift && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        double x = ImPlot::GetPlotMousePos().x;
        range = MedianRange{true, panel.parameter, data_of(panel), x, x};
        making = true;
    }
    if (!range.on || range.data != data_of(panel) || !(range.parameter == panel.parameter)) return;
    if (making) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            range.hi = ImPlot::GetPlotMousePos().x;
        } else {
            making = false;
            if (range.lo > range.hi) std::swap(range.lo, range.hi);
            if (std::abs(ImPlot::PlotToPixels(range.hi, 0.0).x - ImPlot::PlotToPixels(range.lo, 0.0).x) < 3.0f) range.on = false;
        }
    }
    if (!range.on) return;
    double lo = std::min(range.lo, range.hi), hi = std::max(range.lo, range.hi);

    ImPlotRect limits = ImPlot::GetPlotLimits();
    ImVec2 top_left = ImPlot::PlotToPixels(lo, limits.Y.Max), bottom_right = ImPlot::PlotToPixels(hi, limits.Y.Min);
    const ImU32 fill = IM_COL32(120, 120, 130, 36);
    const ImVec4 edge(0.40f, 0.40f, 0.45f, 1.0f);
    ImPlot::PushPlotClipRect();
    ImPlot::GetPlotDrawList()->AddRectFilled(top_left, bottom_right, fill);
    char text[96];
    std::snprintf(text, sizeof text, "median  %.6g to %.6g %s", lo / panel.x_unit, hi / panel.x_unit, panel.x_unit_name);
    ImPlot::GetPlotDrawList()->AddText(ImVec2(top_left.x + 4.0f * ui_scale(), top_left.y + 3.0f * ui_scale()), IM_COL32(70, 70, 80, 255), text);
    ImPlot::PopPlotClipRect();
    if (!making) {
        ImPlot::DragLineX(11, &range.lo, edge, 1.5f, ImPlotDragToolFlags_NoFit);
        ImPlot::DragLineX(12, &range.hi, edge, 1.5f, ImPlotDragToolFlags_NoFit);
        ImVec2 mouse = ImGui::GetMousePos();
        bool on_range = ImPlot::IsPlotHovered() && mouse.x >= top_left.x && mouse.x <= bottom_right.x;
        if (on_range && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !session.open_marker_menu) session.open_median_menu = true;
    }

    session.median_panel_drawn = true;
    session.median_unit = panel.y_unit_name;
    uint64_t visible_curves = 0;
    for (const auto& c : session.curves)
        if (c->visible) visible_curves += static_cast<uint64_t>(c->id) * static_cast<uint64_t>(c->id) + c->time.size();
    auto inputs = std::make_tuple(lo, hi, panel.trace, session.time_settings_version, gate_position(session, panel.parameter).version, session.gate_on, visible_curves);
    static decltype(inputs) inputs_before;
    if (inputs == inputs_before && !session.medians.empty()) return;
    inputs_before = inputs;
    session.medians.clear();
    for (const auto& c : session.curves) {
        if (!c->visible) continue;
        Series d = series_of(*c, panel);
        bool found;
        double median = core::median_in_range(d.x, d.y, d.n, lo, hi, found);
        if (found) session.medians[c->id] = median;
    }
}

}  // namespace

void draw_median_menu(Session& session) {
    if (session.open_median_menu) ImGui::OpenPopup("median menu");
    session.open_median_menu = false;
    if (!ImGui::BeginPopup("median menu")) return;
    const MedianRange& range = session.median_range;
    if (ImGui::MenuItem("Copy table")) {
        bool time = range.data == PanelData::time_response;
        FrequencyUnit unit = time ? FrequencyUnit{1.0, "ns"} : frequency_unit(session);
        const char* data_names[] = {"", " time response", " gated"};
        char header[200];
        std::snprintf(header, sizeof header, "Curve\tMedian %s%s, %.6g to %.6g %s [%s]\n", parameter_name(range.parameter).c_str(), data_names[static_cast<int>(range.data)],
                      std::min(range.lo, range.hi) / unit.hz, std::max(range.lo, range.hi) / unit.hz, unit.name, session.median_unit.c_str());
        std::string table = header;
        for (const auto& c : session.curves) {
            auto median = session.medians.find(c->id);
            if (median == session.medians.end()) continue;
            char row[64];
            std::snprintf(row, sizeof row, "\t%.4f\n", median->second);
            table += c->shown_label() + row;
        }
        ImGui::SetClipboardText(table.c_str());
    }
    if (ImGui::MenuItem("Remove range")) {
        session.median_range.on = false;
        session.medians.clear();
    }
    ImGui::EndPopup();
}

void draw_marker_menu(Session& session) {
    if (session.open_marker_menu) ImGui::OpenPopup("marker menu");
    session.open_marker_menu = false;
    if (!ImGui::BeginPopup("marker menu")) return;
    auto found = std::find_if(session.markers.begin(), session.markers.end(), [&](const Marker& m) { return m.id == session.edited_marker; });
    Curve* c = found == session.markers.end() ? nullptr : find_curve(session, found->curve);
    if (!c) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    Marker marker = *found;  // a copy: the menu items below can change the list
    Panel panel = panel_of(session, marker);
    Series d = series_of(*c, panel);
    ImGui::TextDisabled("%s, %s", c->shown_label().c_str(), parameter_name(marker.parameter).c_str());
    ImGui::SetNextItemWidth(220.0f * ui_scale());
    ImGui::InputTextWithHint("##label", "Label", &found->label);
    if (d.n > 0) {
        size_t sample = nearest_sample(d, found->x);
        ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
        if (ImGui::ArrowButton("previous sample", ImGuiDir_Left) && sample > 0) --sample;
        ImGui::SameLine();
        double typed = d.x[sample] / panel.x_unit;
        ImGui::SetNextItemWidth(220.0f * ui_scale() - 2.0f * (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x));
        if (ImGui::InputDouble("##x", &typed, 0.0, 0.0, (std::string("%.9g ") + panel.x_unit_name).c_str(), ImGuiInputTextFlags_EnterReturnsTrue))
            sample = nearest_sample(d, typed * panel.x_unit);
        ImGui::SameLine();
        if (ImGui::ArrowButton("next sample", ImGuiDir_Right) && sample + 1 < d.n) ++sample;
        ImGui::PopItemFlag();
        found->x = d.x[sample];
        ImGui::TextDisabled("sample %zu of %zu:  %.4f %s", sample + 1, d.n, d.y[sample], panel.y_unit_name);
    }
    ImGui::Separator();
    auto remove_if = [&](auto which) {
        std::erase_if(session.markers, which);
        ImGui::CloseCurrentPopup();
    };
    if (ImGui::MenuItem("Delete")) remove_if([&](const Marker& m) { return m.id == marker.id; });
    if (ImGui::MenuItem("Delete all markers of this curve")) remove_if([&](const Marker& m) { return m.curve == marker.curve; });
    if (ImGui::MenuItem("Delete all markers in this panel")) remove_if([&](const Marker& m) { return m.data == marker.data && m.parameter == marker.parameter; });
    if (ImGui::MenuItem("Delete all markers")) remove_if([](const Marker&) { return true; });
    ImGui::EndPopup();
}

void plot_curves(Session& session, const Panel& panel) {
    start_frame(session);
    const ImPlotRect limits = ImPlot::GetPlotLimits();
    // ImPlot gets the points only for a fit. FitThisFrame is valid after GetPlotLimits, which completes the plot setup.
    if (ImPlot::GetCurrentPlot()->FitThisFrame) {
        for (const auto& c : session.curves) {
            if (!c->visible) continue;
            Series d = series_of(*c, panel);
            for (size_t i = 0; i < d.n; ++i) ImPlot::FitPoint(ImPlotPoint(d.x[i], d.y[i]));
        }
    }

    std::vector<int> left_out = draw_order_highlight(session, panel);
    auto is_left_out = [&](const Curve& c) { return std::find(left_out.begin(), left_out.end(), c.id) != left_out.end(); };

    const PanelPoints* points = points_in_graphics_memory(session, panel);
    if (!points) {
        const char* message = "Not enough graphics memory for the curves of this panel.\nRemove files.";
        ImVec2 at = ImPlot::GetPlotPos(), size = ImPlot::GetPlotSize(), text = ImGui::CalcTextSize(message);
        ImPlot::GetPlotDrawList()->AddText(ImVec2(at.x + (size.x - text.x) * 0.5f, at.y + (size.y - text.y) * 0.5f), IM_COL32(150, 60, 40, 255), message);
    } else if (points->count >= 2) {
        const ImVec2 pixels_per_unit = ImGui::GetIO().DisplayFramebufferScale, display = ImGui::GetIO().DisplaySize;
        const ImVec2 at = ImPlot::GetPlotPos(), size = ImPlot::GetPlotSize();
        DrawCall call{points->buffer, 0, points->count, {}};
        call.view.x_at_left = limits.X.Min;
        call.view.y_at_top = limits.Y.Max;
        call.view.width = size.x * pixels_per_unit.x;
        call.view.height = size.y * pixels_per_unit.y;
        call.view.left = at.x * pixels_per_unit.x;
        call.view.top = at.y * pixels_per_unit.y;
        call.view.pixels_per_x = static_cast<float>(static_cast<double>(call.view.width) / (limits.X.Max - limits.X.Min));
        call.view.pixels_per_y = static_cast<float>(-static_cast<double>(call.view.height) / (limits.Y.Max - limits.Y.Min));
        call.view.framebuffer_width = display.x * pixels_per_unit.x;
        call.view.framebuffer_height = display.y * pixels_per_unit.y;
        for (size_t i = 0; i < left_out.size() && i < 4; ++i) call.view.hidden_curves[i] = static_cast<float>(left_out[i]);
        add_draw_call(call);
        for (const PanelPoints::Part& part : points->parts) {
            if (part.curve != session.highlighted_curve) continue;
            call.first = part.first;
            call.count = part.count;
            add_draw_call(call);
        }
    }
    for (const auto& c : session.curves) {
        if (!c->visible || (c->style == LineStyle::solid && !is_left_out(*c))) continue;
        Series d = series_of(*c, panel);
        if (d.n < 2) continue;
        draw_patterned_line(d, limits, ImGui::ColorConvertFloat4ToU32(color_in_frame(*c, session.highlighted_curve)), width_in_frame(*c, session.highlighted_curve),
                            is_left_out(*c) ? LineStyle::dotted : c->style);
    }
    find_curve_under_mouse(session, panel);
    draw_markers(session, panel);
    draw_median_range(session, panel);
}

void draw_line_sample(ImDrawList* draw, ImVec2 from, ImVec2 to, ImU32 color, float width, LineStyle style) {
    if (style == LineStyle::solid) {
        draw->AddLine(from, to, color, width);
        return;
    }
    float on = style == LineStyle::dashed ? 6.0f : 1.6f, off = style == LineStyle::dashed ? 3.5f : 3.0f;
    for (float x = from.x; x < to.x; x += on + off) draw->AddLine(ImVec2(x, from.y), ImVec2(std::min(x + on, to.x), from.y), color, width);
}
