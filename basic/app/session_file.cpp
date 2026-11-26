// File: a version line, lines "key values", "end". A "curve" line is followed by "path", "label" and the bytes of the file.
#include "imgui.h"
#include "log.h"
#include "parallel.h"
#include "paths.h"
#include "platform.h"
#include "session.h"
#include "views.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace {

const char* first_line = "touchstone viewer session 1";

std::string read_content(const std::string& path, std::string& content) {
    return core::read_whole_file(path, content) ? "" : path + ": cannot read the file";
}

CHECKED_AS_PRINTF(2, 3) void add_line(std::string& out, const char* format, ...) {
    char line[512];
    va_list values;
    va_start(values, format);
    int length = std::vsnprintf(line, sizeof line, format, values);
    va_end(values);
    out.append(line, static_cast<size_t>(std::min(length, static_cast<int>(sizeof line) - 1)));
    out += '\n';
}

// If the file changed or is gone, the bytes are written from the loaded numbers.
bool bytes_of(const Curve& c, std::string& bytes, bool& original, std::map<std::string, std::string>& sessions) {
    if (c.source.in_session) {
        if (!sessions.count(c.source.path)) read_content(c.source.path, sessions[c.source.path]);  // empty after an error
        const std::string& content = sessions[c.source.path];
        bytes = c.source.offset + c.source.size <= content.size() ? content.substr(c.source.offset, c.source.size) : "";
    } else {
        std::ifstream in(core::path_from_utf8(c.source.path), std::ios::binary);
        bytes.resize(c.source.size);
        in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!in || in.peek() != std::ifstream::traits_type::eof()) bytes.clear();  // shorter or longer than at the load
    }
    original = bytes.size() == c.source.size && hash_of(bytes) == c.source.hash;
    if (original) return true;
    if (!c.data.has_phase) return false;  // csv: no lossless text form
    bytes = core::touchstone_text(c.data);
    return true;
}

}  // namespace

std::string save_session(Session& s, const std::string& path) {
    const float scale = ui_scale();
    std::string out;
    add_line(out, "%s", first_line);
    add_line(out, "title %s", s.title.c_str());
    add_line(out, "view %d %d", static_cast<int>(s.view), static_cast<int>(s.quantity));
    for (Parameter p : s.shown_parameters) add_line(out, "shown %d %d", p.out_port, p.in_port);
    for (const auto& [name, range] : s.x_ranges) add_line(out, "x_range %.17g %.17g %d %s", range.lo, range.hi, range.fitted, name.c_str());
    for (const auto& [name, range] : s.y_ranges) add_line(out, "y_range %.17g %.17g %s", range.lo, range.hi, name.c_str());
    for (int view = 0; view < 2; ++view) {
        const Session::LegendPosition& legend = s.legend_positions[view];
        add_line(out, "legend %d %d %d %s", view, static_cast<int>(legend.place), legend.chosen, legend.panel.c_str());
    }
    add_line(out, "legend_size %.9g %.9g %.9g %.9g", static_cast<double>(s.legend_text_size), static_cast<double>(s.legend_box.x / scale), static_cast<double>(s.legend_box.y / scale),
             static_cast<double>(s.legend_beside_width / scale));
    add_line(out, "panels %d %.9g %d %.9g", s.files_panel_open, static_cast<double>(s.files_panel_width / scale), s.settings_panel_open, static_cast<double>(s.settings_panel_width / scale));
    const core::WindowSpec& response = s.time_response.window;
    add_line(out, "time_response %d %.17g %d %d", static_cast<int>(response.kind), response.parameter, response.periodic, s.time_response.pad);
    add_line(out, "gate %d %d %d %d %d %.17g %d", s.gate_on, static_cast<int>(s.gate.method), static_cast<int>(s.gate.mode), static_cast<int>(s.gate.edge),
             static_cast<int>(s.gate.window.kind), s.gate.window.parameter, s.gate.window.periodic);
    for (const auto& [parameter, position] : s.gate_positions) add_line(out, "gate_position %d %d %.17g %.17g", parameter.first, parameter.second, position.center_ns, position.span_ns);
    const OrderHighlight& h = s.order_highlight;
    add_line(out, "highlight %d %d %d %d %d", h.on, h.field, h.by_pattern, h.allowed_rank_difference, h.curves_to_leave_out);
    add_line(out, "highlight_pattern %s", h.pattern.c_str());
    const MedianRange& median = s.median_range;
    add_line(out, "median %d %d %d %d %.17g %.17g", median.on, median.parameter.out_port, median.parameter.in_port, static_cast<int>(median.data), median.lo, median.hi);

    int not_original = 0;
    std::vector<Curve::Source> saved_at;
    std::map<std::string, std::string> sessions;
    for (const auto& c : s.curves) {
        std::string bytes;
        bool original;
        if (!bytes_of(*c, bytes, original, sessions)) return c->name + ": the file changed or is not there, and a csv file cannot be written from memory";
        not_original += !original;
        add_line(out, "curve %zu %d %.9g %d %.9g %.9g %.9g %.9g %d", bytes.size(), c->visible, static_cast<double>(c->width), static_cast<int>(c->style), static_cast<double>(c->color.x),
                 static_cast<double>(c->color.y), static_cast<double>(c->color.z), static_cast<double>(c->color.w), c->color_slot);
        add_line(out, "path %s", c->data.path.c_str());
        add_line(out, "label %s", c->label.c_str());
        saved_at.push_back({path, out.size(), bytes.size(), hash_of(bytes), true});
        out += bytes;
        out += '\n';
    }
    for (const Marker& m : s.markers) {
        size_t index = 0;
        while (index < s.curves.size() && s.curves[index]->id != m.curve) ++index;
        add_line(out, "marker %zu %d %d %d %.17g", index, m.parameter.out_port, m.parameter.in_port, static_cast<int>(m.data), m.x);
        add_line(out, "marker_label %s", m.label.c_str());
    }
    add_line(out, "end");

    // A failed write must not destroy a session file that has this name.
    std::string unfinished = path + ".unfinished";
    {
        std::ofstream file(core::path_from_utf8(unfinished), std::ios::binary);
        file.write(out.data(), static_cast<std::streamsize>(out.size()));
        if (!file) return "cannot write " + path;
    }
    std::error_code error;
    std::filesystem::rename(core::path_from_utf8(unfinished), core::path_from_utf8(path), error);
    if (error) return "cannot write " + path;
    // The file that was the source of the bytes can be the file that was replaced.
    for (size_t i = 0; i < s.curves.size(); ++i) s.curves[i]->source = saved_at[i];
    log_line("session saved: %s, %zu curves, %zu bytes, %d curves written from memory because the file changed", path.c_str(), s.curves.size(), out.size(), not_original);
    return "";
}

namespace {

std::string open_session(Session& s, const std::string& path) {
    std::string text;
    if (std::string error = read_content(path, text); !error.empty()) return error;
    size_t at = text.find('\n');
    if (at == std::string::npos || text.compare(0, at, first_line) != 0)
        return path + (text.starts_with("touchstone viewer session ") ? ": the session file is from another program version" : ": not a session file");
    ++at;

    struct StoredCurve {
        std::unique_ptr<Curve> curve;
        std::string path, label, error;
        size_t offset = 0, size = 0;
        int visible = 1, style = 0, color_slot = -1;
        float width = 1.6f, r = 0, g = 0, b = 0, a = 1;
    };
    std::vector<StoredCurve> stored;
    struct StoredMarker {
        Marker marker;
        size_t curve = 0;
    };
    std::vector<StoredMarker> markers;
    const float scale = ui_scale();
    auto rest_of = [](std::istringstream& in) {
        std::string rest;
        std::getline(in >> std::ws, rest);
        return rest;
    };
    auto next_line = [&] {
        size_t line_end = std::min(text.find('\n', at), text.size());
        std::string line = text.substr(at, line_end - at);
        at = std::min(line_end + 1, text.size());
        return line;
    };

    remove_all_curves(s);
    s.shown_parameters.clear();
    s.x_ranges.clear();
    s.y_ranges.clear();
    s.gate_positions.clear();
    s.medians.clear();
    bool complete = false;
    while (at < text.size() && !complete) {
        std::istringstream in(next_line());
        std::string key;
        in >> key;
        int a = 0, b = 0, c = 0, d = 0;
        if (key == "title") s.title = rest_of(in);
        else if (key == "view" && in >> a >> b) {
            s.view = static_cast<View>(a);
            s.quantity = static_cast<Quantity>(b);
        }
        else if (key == "shown" && in >> a >> b) s.shown_parameters.push_back({a, b});
        else if (key == "x_range") {
            AxisRange range;
            if (in >> range.lo >> range.hi >> range.fitted) s.x_ranges[rest_of(in)] = range;
        } else if (key == "y_range") {
            AxisRange range;
            if (in >> range.lo >> range.hi) s.y_ranges[rest_of(in)] = range;
        } else if (key == "legend" && in >> a >> b >> c && a >= 0 && a < 2) s.legend_positions[a] = {static_cast<LegendPlace>(b), c != 0, rest_of(in)};
        else if (key == "legend_size") {
            in >> s.legend_text_size >> s.legend_box.x >> s.legend_box.y >> s.legend_beside_width;
            s.legend_box = ImVec2(s.legend_box.x * scale, s.legend_box.y * scale);
            s.legend_beside_width *= scale;
        } else if (key == "panels") {
            in >> s.files_panel_open >> s.files_panel_width >> s.settings_panel_open >> s.settings_panel_width;
            s.files_panel_width *= scale;
            s.settings_panel_width *= scale;
        } else if (key == "time_response" && in >> a >> s.time_response.window.parameter >> s.time_response.window.periodic >> s.time_response.pad)
            s.time_response.window.kind = static_cast<core::WindowKind>(a);
        else if (key == "gate" && in >> s.gate_on >> a >> b >> c >> d >> s.gate.window.parameter >> s.gate.window.periodic) {
            s.gate.method = static_cast<core::GateMethod>(a);
            s.gate.mode = static_cast<core::GateMode>(b);
            s.gate.edge = static_cast<core::GateEdge>(c);
            s.gate.window.kind = static_cast<core::WindowKind>(d);
        } else if (key == "gate_position" && in >> a >> b) {
            GatePosition& position = s.gate_positions[{a, b}];
            in >> position.center_ns >> position.span_ns;
        } else if (key == "highlight") in >> s.order_highlight.on >> s.order_highlight.field >> s.order_highlight.by_pattern >> s.order_highlight.allowed_rank_difference >> s.order_highlight.curves_to_leave_out;
        else if (key == "highlight_pattern") s.order_highlight.pattern = rest_of(in);
        else if (key == "median" && in >> s.median_range.on >> a >> b >> c >> s.median_range.lo >> s.median_range.hi) {
            s.median_range.parameter = {a, b};
            s.median_range.data = static_cast<PanelData>(c);
        } else if (key == "curve") {
            StoredCurve curve;
            in >> curve.size >> curve.visible >> curve.width >> curve.style >> curve.r >> curve.g >> curve.b >> curve.a >> curve.color_slot;
            std::string path_line = next_line(), label_line = next_line();
            if (!in || path_line.compare(0, 5, "path ") != 0 || label_line.compare(0, 5, "label") != 0 || at + curve.size > text.size()) return path + ": the session file is damaged";
            curve.path = path_line.substr(5);
            curve.label = label_line.size() > 6 ? label_line.substr(6) : "";
            curve.offset = at;
            at += curve.size + 1;
            stored.push_back(std::move(curve));
        } else if (key == "marker") {
            StoredMarker marker;
            if (in >> marker.curve >> a >> b >> c >> marker.marker.x) {
                marker.marker.parameter = {a, b};
                marker.marker.data = static_cast<PanelData>(c);
                std::string label_line = next_line();
                marker.marker.label = label_line.size() > 13 ? label_line.substr(13) : "";
                markers.push_back(marker);
            }
        } else if (key == "end") complete = true;
    }
    if (!complete) return path + ": the session file is not complete";
    if (s.shown_parameters.empty()) s.shown_parameters = {{1, 1}, {2, 1}};
    ++s.time_settings_version;

    core::run_in_parallel(stored.size(), [&](size_t i) {
        StoredCurve& from = stored[i];
        std::string bytes = text.substr(from.offset, from.size);
        Curve::Source source{path, from.offset, from.size, hash_of(bytes), true};
        core::LoadResult loaded = core::load_measurement_from_text(from.path, bytes);
        if (!loaded.error.empty()) {
            from.error = from.path + ": " + loaded.error;
            return;
        }
        from.curve = make_curve(std::move(loaded.measurement));
        Curve& c = *from.curve;
        c.source = source;
        c.label = from.label;
        c.visible = from.visible != 0;
        c.width = from.width;
        c.style = static_cast<LineStyle>(from.style);
        c.color = ImVec4(from.r, from.g, from.b, from.a);
        c.color_slot = from.color_slot;
    });
    std::vector<std::unique_ptr<Curve>> curves;
    std::vector<Curve*> by_index;
    for (StoredCurve& from : stored) {
        by_index.push_back(from.curve.get());
        if (from.curve) curves.push_back(std::move(from.curve));
        else s.load_errors.push_back(from.error);
    }
    add_curves(s, std::move(curves));
    for (StoredMarker& m : markers) {
        if (m.curve >= by_index.size() || !by_index[m.curve]) continue;
        m.marker.id = s.next_marker_id++;
        m.marker.curve = by_index[m.curve]->id;
        s.markers.push_back(m.marker);
    }
    s.fit_frequency_plots = s.fit_time_plots = false;  // the session has its x ranges
    log_line("session opened: %s, %zu curves, %zu markers", path.c_str(), s.curves.size(), s.markers.size());
    return "";
}

bool is_session_file(const std::string& path) {
    std::string extension = session_file_extension;
    return path.size() > extension.size() && path.compare(path.size() - extension.size(), extension.size(), extension) == 0;
}

}  // namespace

void open_paths(Session& session, const std::vector<std::string>& paths) {
    std::vector<std::string> files;
    for (const std::string& path : paths) {
        if (is_session_file(path)) session.session_to_open = path;
        else files.push_back(path);
    }
    load_paths(session, files);
}

void open_requested_session(Session& session) {
    if (session.session_to_open.empty()) return;
    bool open = session.curves.empty() && !loading(session);
    if (!open) {
        ImGui::OpenPopup("Open session");
        if (ImGui::BeginPopupModal("Open session", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::Text("The session replaces the %zu loaded files and the state of the window.", session.curves.size());
            open = ImGui::Button("Open the session");
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                session.session_to_open.clear();
                ImGui::CloseCurrentPopup();
            }
            if (open) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    if (!open) return;
    cancel_loading(session);
    std::string error = open_session(session, session.session_to_open);
    if (!error.empty()) session.load_errors.push_back(error);
    session.session_to_open.clear();
}

void save_session_as(Session& session) {
    if (session.curves.empty()) return;
    std::string path = ask_for_save_path(session.title.empty() ? "session" : session.title, session_file_extension);
    if (path.empty()) return;
    std::string error = save_session(session, path);
    if (error.empty()) session.session_saved_at = ImGui::GetTime();
    else session.load_errors.push_back("Session not saved: " + error);
}
