#include "session.h"

#include "log.h"

#include "names.h"
#include "palette.h"
#include "parallel.h"
#include "paths.h"
#include "simd.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <tuple>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {

struct FileToLoad {
    std::string path;
    bool found_in_folder;  // false if the file was dropped or selected by name
};

std::vector<FileToLoad> files_under(const std::vector<std::string>& paths, std::vector<std::string>& errors) {
    std::vector<FileToLoad> files;
    std::error_code error;
    for (const std::string& given : paths) {
        fs::path path = core::path_from_utf8(given);
        if (fs::is_directory(path, error)) {
            for (auto it = fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied, error);
                 it != fs::recursive_directory_iterator(); it.increment(error)) {
                std::string found = core::utf8_from_path(it->path());
                if (it->is_regular_file(error) && core::file_kind(found) != core::FileKind::other) files.push_back({found, true});
            }
        } else if (core::file_kind(given) != core::FileKind::other) {
            files.push_back({given, false});
        } else {
            errors.push_back(given + ": not a .sNp or .csv file");
        }
    }
    std::sort(files.begin(), files.end(), [](const FileToLoad& a, const FileToLoad& b) { return core::natural_less(a.path, b.path); });
    return files;
}

std::unique_ptr<Curve> curve_from_file(const FileToLoad& file, std::string& error) {
    std::string text;
    if (!core::read_whole_file(file.path, text)) {
        error = file.path + ": cannot read the file";
        return nullptr;
    }
    Curve::Source source{file.path, 0, text.size(), hash_of(text), false};
    core::LoadResult loaded = core::load_measurement_from_text(file.path, text, file.found_in_folder ? core::CsvRule::analyzer_export_only : core::CsvRule::any);
    if (!loaded.error.empty()) {
        // A csv file in a folder that is not a spectrum analyzer export is not a measurement, and not an error.
        bool csv = core::file_kind(file.path) == core::FileKind::csv;
        if (!(file.found_in_folder && csv)) error = file.path + ": " + loaded.error;
        return nullptr;
    }
    std::unique_ptr<Curve> curve = make_curve(std::move(loaded.measurement));
    curve->source = source;
    return curve;
}

void load_on_this_thread(Loader& loader, const std::vector<std::string>& paths, unsigned load_number) {
    std::vector<std::string> errors;
    std::vector<FileToLoad> files = files_under(paths, errors);
    {
        std::lock_guard<std::mutex> hold(loader.lock);
        loader.found += static_cast<int>(files.size());
        loader.errors.insert(loader.errors.end(), errors.begin(), errors.end());
    }
    const size_t batch = 64;
    size_t handed_over = 0;
    while (handed_over < files.size() && load_number > loader.loads_cancelled) {
        size_t count = std::min(batch, files.size() - handed_over);
        std::vector<std::unique_ptr<Curve>> curves(count);
        std::vector<std::string> failures(count);
        core::run_in_parallel(count, [&](size_t i) { curves[i] = curve_from_file(files[handed_over + i], failures[i]); });
        {
            std::lock_guard<std::mutex> hold(loader.lock);
            for (size_t i = 0; i < count; ++i) {
                if (curves[i]) loader.arrived.push_back(std::move(curves[i]));
                else if (!failures[i].empty()) loader.errors.push_back(failures[i]);
            }
            loader.done += static_cast<int>(count);
        }
        handed_over += count;
        if (loader.wake_frame_loop) loader.wake_frame_loop();
    }
    {
        std::lock_guard<std::mutex> hold(loader.lock);
        loader.found -= static_cast<int>(files.size());
        loader.done -= static_cast<int>(handed_over);
    }
    --loader.running;
    if (loader.wake_frame_loop) loader.wake_frame_loop();
}

void refresh_short_names(Session& session) {
    std::vector<std::string> names, folders;
    for (const auto& c : session.curves) {
        names.push_back(c->name);
        folders.push_back(c->folder);
    }
    std::vector<std::string> labels = core::short_labels(names, folders);
    for (size_t i = 0; i < session.curves.size(); ++i) session.curves[i]->short_name = labels[i];
}

int take_color_slot(Session& session) {
    auto free_slot = std::find(session.color_slot_taken.begin(), session.color_slot_taken.end(), false);
    if (free_slot == session.color_slot_taken.end()) {
        session.color_slot_taken.push_back(true);
        return static_cast<int>(session.color_slot_taken.size()) - 1;
    }
    *free_slot = true;
    return static_cast<int>(free_slot - session.color_slot_taken.begin());
}

}  // namespace

std::unique_ptr<Curve> make_curve(core::Measurement measurement) {
    auto curve = std::make_unique<Curve>();
    curve->data = std::move(measurement);
    fs::path p = core::path_from_utf8(curve->data.path);
    curve->name = core::utf8_from_path(p.stem());
    curve->folder = core::utf8_from_path(p.parent_path());
    const core::Measurement& m = curve->data;
    curve->db.resize(m.re.size());
    for (size_t k = 0; k < m.re.size(); ++k) {
        curve->db[k].resize(m.points());
        core::power_db(m.re[k].data(), m.im[k].data(), curve->db[k].data(), m.points());
    }
    return curve;
}

std::string parameter_name(Parameter p) {
    std::string separator = (p.out_port > 9 || p.in_port > 9) ? "," : "";
    return "S" + std::to_string(p.out_port) + separator + std::to_string(p.in_port);
}

void load_paths(Session& session, const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    Loader& loader = session.loader;
    ++loader.running;
    loader.threads.emplace_back(load_on_this_thread, std::ref(loader), paths, ++loader.loads_started);
}

bool collect_loaded(Session& session) {
    Loader& loader = session.loader;
    std::vector<std::unique_ptr<Curve>> arrived;
    {
        std::lock_guard<std::mutex> hold(loader.lock);
        arrived.swap(loader.arrived);
        session.load_errors.insert(session.load_errors.end(), loader.errors.begin(), loader.errors.end());
        loader.errors.clear();
    }
    if (loader.running == 0 && !loader.threads.empty()) {
        for (std::thread& t : loader.threads) t.join();
        loader.threads.clear();
    }
    if (arrived.empty()) return false;

    std::unordered_set<std::string> open;
    for (const auto& c : session.curves) open.insert(c->data.path);
    std::erase_if(arrived, [&](const auto& curve) { return !open.insert(curve->data.path).second; });
    add_curves(session, std::move(arrived));
    return true;
}

void add_curves(Session& session, std::vector<std::unique_ptr<Curve>> curves) {
    for (auto& curve : curves) {
        curve->id = session.next_curve_id++;
        if (curve->color_slot < 0) {
            curve->color_slot = take_color_slot(session);
            curve->color = palette_color(curve->color_slot);
        } else {
            if (session.color_slot_taken.size() <= static_cast<size_t>(curve->color_slot)) session.color_slot_taken.resize(static_cast<size_t>(curve->color_slot) + 1);
            session.color_slot_taken[static_cast<size_t>(curve->color_slot)] = true;
        }
        session.curves.push_back(std::move(curve));
    }
    refresh_short_names(session);
    session.fit_frequency_plots = session.fit_time_plots = true;
}

uint64_t hash_of(const std::string& bytes) {  // FNV-1a
    uint64_t hash = 14695981039346656037ull;
    for (char c : bytes) hash = (hash ^ static_cast<unsigned char>(c)) * 1099511628211ull;
    return hash;
}

bool loading(const Session& session) { return session.loader.running > 0; }

void loading_progress(Session& session, int& done, int& found) {
    std::lock_guard<std::mutex> hold(session.loader.lock);
    done = session.loader.done;
    found = session.loader.found;
}

void cancel_loading(Session& session) { session.loader.loads_cancelled = session.loader.loads_started.load(); }

void finish_loading(Session& session) {
    cancel_loading(session);
    for (std::thread& t : session.loader.threads) t.join();
    session.loader.threads.clear();
}

template <typename Which>
static void remove_curves(Session& session, Which which) {
    for (const auto& c : session.curves)
        if (which(*c)) session.color_slot_taken[static_cast<size_t>(c->color_slot)] = false;
    for (const auto& c : session.curves)
        if (which(*c)) std::erase_if(session.markers, [&](const Marker& m) { return m.curve == c->id; });
    std::erase_if(session.curves, [&](const auto& c) { return which(*c); });
    refresh_short_names(session);
}

void remove_all_curves(Session& session) {
    remove_curves(session, [](const Curve&) { return true; });
}

void remove_selected_curves(Session& session) {
    remove_curves(session, [](const Curve& c) { return c.selected; });
}

void remove_curve(Session& session, int id) {
    remove_curves(session, [id](const Curve& c) { return c.id == id; });
}

Curve* find_curve(Session& session, int id) {
    for (auto& c : session.curves)
        if (c->id == id) return c.get();
    return nullptr;
}

int largest_port_count(const Session& session) {
    int ports = 0;
    for (const auto& c : session.curves) ports = std::max(ports, c->data.ports);
    return ports;
}

int visible_curve_count(const Session& session) {
    return static_cast<int>(std::count_if(session.curves.begin(), session.curves.end(), [](const auto& c) { return c->visible; }));
}

bool is_shown(const Session& session, Parameter p) {
    return std::find(session.shown_parameters.begin(), session.shown_parameters.end(), p) != session.shown_parameters.end();
}

void set_shown(Session& session, Parameter p, bool shown) {
    std::erase(session.shown_parameters, p);
    if (shown) session.shown_parameters.push_back(p);
    std::sort(session.shown_parameters.begin(), session.shown_parameters.end(),
              [](Parameter a, Parameter b) { return std::tie(a.in_port, a.out_port) < std::tie(b.in_port, b.out_port); });
}

std::vector<Parameter> shown_panels(const Session& session) {
    int ports = largest_port_count(session);
    std::vector<Parameter> panels;
    for (Parameter p : session.shown_parameters)
        if (p.out_port <= ports && p.in_port <= ports) panels.push_back(p);
    if (panels.empty()) panels.push_back({1, 1});
    return panels;
}

FrequencyUnit frequency_unit(const Session& session) {
    double highest = 0.0;
    for (const auto& c : session.curves) highest = std::max(highest, c->data.freq_hz.back());
    if (highest >= 1e9 || highest <= 0.0) return {1e9, "GHz"};
    if (highest >= 1e6) return {1e6, "MHz"};
    if (highest >= 1e3) return {1e3, "kHz"};
    return {1.0, "Hz"};
}

void compute_phase(Session& session) {
    std::vector<Curve*> todo;
    for (auto& c : session.curves)
        if (c->deg.empty() && c->data.has_phase) todo.push_back(c.get());
    core::run_in_parallel(todo.size(), [&](size_t i) {
        const core::Measurement& m = todo[i]->data;
        todo[i]->deg.resize(m.re.size());
        for (size_t k = 0; k < m.re.size(); ++k) {
            todo[i]->deg[k].resize(m.points());
            core::phase_deg(m.re[k].data(), m.im[k].data(), todo[i]->deg[k].data(), m.points());
        }
    });
}

GatePosition& gate_position(Session& session, Parameter p) { return session.gate_positions[{p.out_port, p.in_port}]; }

Session::LegendPosition& legend_position(Session& session) { return session.legend_positions[static_cast<int>(session.view)]; }

LegendPlace legend_place_in_use(const Session& session) {
    const Session::LegendPosition& position = session.legend_positions[static_cast<int>(session.view)];
    if (position.chosen) return position.place;
    return session.curves.size() > 10 ? LegendPlace::beside : LegendPlace::top_right;
}

void set_legend_place(Session& session, LegendPlace place) {
    legend_position(session).place = place;
    legend_position(session).chosen = true;
}

void compute_time_arrays(Session& session) {
    struct Item {
        Curve* curve;
        Parameter parameter;
        size_t index;
    };
    std::vector<Item> todo;
    for (auto& c : session.curves) {
        if (!c->visible || !c->data.has_phase) continue;  // csv curves have no phase and thus no time response
        c->time.resize(c->data.re.size());
        for (Parameter p : shown_panels(session)) {
            if (p.out_port > c->data.ports || p.in_port > c->data.ports) continue;
            size_t k = static_cast<size_t>(c->data.parameter(p.out_port, p.in_port));
            const Curve::TimeArrays& arrays = c->time[k];
            bool old = arrays.version != session.time_settings_version || (session.gate_on && arrays.gate_version != gate_position(session, p).version);
            if (old) todo.push_back({c.get(), p, k});
        }
    }
    if (todo.empty()) return;
    auto started = std::chrono::steady_clock::now();

    // One time response plan for each frequency grid. One gate plan for each grid and parameter.
    using Grid = std::tuple<size_t, double, double>;  // points, first frequency, last frequency
    using GridAndParameter = std::tuple<size_t, double, double, int, int>;
    auto grid_of = [](const Curve& c) { return Grid{c.data.points(), c.data.freq_hz.front(), c.data.freq_hz.back()}; };
    auto gate_key = [&](const Item& item) { return std::tuple_cat(grid_of(*item.curve), std::make_tuple(item.parameter.out_port, item.parameter.in_port)); };
    std::map<Grid, core::TimeResponsePlan> response_plans;
    std::map<GridAndParameter, core::GatePlan> gate_plans;
    for (const Item& item : todo) {
        const core::Measurement& m = item.curve->data;
        if (!response_plans.count(grid_of(*item.curve)))
            response_plans[grid_of(*item.curve)] = core::make_time_response_plan(session.time_response, m.freq_hz.data(), m.points());
        if (session.gate_on && !gate_plans.count(gate_key(item))) {
            core::GateSpec spec = session.gate;
            spec.center_ns = gate_position(session, item.parameter).center_ns;
            spec.span_ns = gate_position(session, item.parameter).span_ns;
            gate_plans[gate_key(item)] = core::make_gate_plan(spec, m.freq_hz.data(), m.points());
        }
    }
    std::vector<unsigned> gate_versions;  // read here: gate_position can change the map, which is not safe in the threads
    for (const Item& item : todo) gate_versions.push_back(gate_position(session, item.parameter).version);

    core::run_in_parallel(todo.size(), [&](size_t i) {
        const core::Measurement& m = todo[i].curve->data;
        size_t k = todo[i].index;
        Curve::TimeArrays& arrays = todo[i].curve->time[k];
        const core::TimeResponsePlan& response = response_plans.at(grid_of(*todo[i].curve));
        arrays.version = session.time_settings_version;
        arrays.gate_version = gate_versions[i];
        arrays.time_ns = response.t_ns;
        arrays.time_db.resize(arrays.time_ns.size());
        core::time_response_db(response, m.re[k].data(), m.im[k].data(), arrays.time_db.data());
        arrays.gated_db.clear();
        if (session.gate_on) {
            std::vector<double> gated_re(m.points()), gated_im(m.points());
            core::apply_gate(gate_plans.at(gate_key(todo[i])), m.re[k].data(), m.im[k].data(), gated_re.data(), gated_im.data());
            arrays.gated_db.resize(m.points());
            core::power_db(gated_re.data(), gated_im.data(), arrays.gated_db.data(), m.points());
        }
    });
    log_line("time arrays: %zu parameters of curves, gate %s, %.1f ms", todo.size(), session.gate_on ? "on" : "off",
             std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
}
