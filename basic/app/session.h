#pragma once
#include "imgui.h"
#include "measurement.h"
#include "time_domain.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class LineStyle { solid, dashed, dotted };
enum class Quantity { magnitude_db, phase_deg };
enum class View { frequency, time };
enum class LegendPlace { top_right, top_left, bottom_right, bottom_left, beside, off };

struct AxisRange {
    double lo = 0.0, hi = 1.0;
    bool fitted = false;
};

struct Parameter {  // {2, 1} is S21
    int out_port = 1, in_port = 1;
    bool operator==(const Parameter&) const = default;
};
std::string parameter_name(Parameter p);

struct GatePosition {
    double center_ns = 5.0, span_ns = 4.0;
    unsigned version = 1;
};

enum class PanelData { measured, time_response, gated };

struct Marker {
    int id = 0;
    int curve = 0;
    Parameter parameter;
    PanelData data = PanelData::measured;
    double x = 0.0;  // Hz for measured and gated, ns for time_response
    std::string label;
};

struct MedianRange {
    bool on = false;
    Parameter parameter;
    PanelData data = PanelData::measured;
    double lo = 0.0, hi = 0.0;  // Hz for measured and gated, ns for time_response
};

struct OrderHighlight {
    bool on = false;
    int field = 0;
    bool by_pattern = false;
    std::string pattern;
    int allowed_rank_difference = 0;
    int curves_to_leave_out = 0;
};

struct Curve {
    int id = 0;
    core::Measurement data;
    std::string name;
    std::string folder;
    std::string label;
    std::string short_name;

    struct Source {
        std::string path;
        uint64_t offset = 0, size = 0;
        uint64_t hash = 0;
        bool in_session = false;  // path is a session file, and offset is a position in its decrypted content
    };
    Source source;

    int color_slot = -1;  // index into the palette; add_curves sets it if it is -1
    ImVec4 color;
    float width = 1.6f;
    LineStyle style = LineStyle::solid;
    bool visible = true;
    bool selected = false;

    std::vector<std::vector<double>> db, deg;
    struct TimeArrays {
        unsigned version = 0;       // Session::time_settings_version of the calculation
        unsigned gate_version = 0;  // GatePosition::version of the calculation
        std::vector<double> time_ns, time_db, gated_db;
    };
    std::vector<TimeArrays> time;

    const std::string& shown_label() const { return label.empty() ? short_name : label; }
};

struct Loader {
    std::mutex lock;  // protects arrived, errors, found, done
    std::vector<std::unique_ptr<Curve>> arrived;
    std::vector<std::string> errors;
    int found = 0, done = 0;
    std::atomic<int> running{0};
    // A cancel applies to all loads started before it.
    std::atomic<unsigned> loads_started{0}, loads_cancelled{0};
    std::vector<std::thread> threads;
    void (*wake_frame_loop)() = nullptr;
};

struct Session {
    std::vector<std::unique_ptr<Curve>> curves;
    std::string title;

    View view = View::frequency;
    Quantity quantity = Quantity::magnitude_db;
    std::vector<Parameter> shown_parameters{{1, 1}, {2, 1}};
    std::map<std::string, AxisRange> x_ranges;  // by panel name
    std::map<std::string, AxisRange> y_ranges;
    struct LegendPosition {
        LegendPlace place = LegendPlace::top_right;
        bool chosen = false;
        std::string panel;  // empty = first panel
    };
    LegendPosition legend_positions[2];  // index: View
    float legend_text_size = 15.0f;
    ImVec2 legend_box{0.0f, 0.0f};     // legend inside a panel; 0 = automatic
    float legend_beside_width = 0.0f;  // 0 = automatic
    bool files_panel_open = true;
    float files_panel_width = 300.0f;
    bool settings_panel_open = false;
    float settings_panel_width = 280.0f;

    core::TimeResponseSpec time_response;
    bool gate_on = false;
    core::GateSpec gate;  // center_ns and span_ns in it are not used: refer to gate_positions
    std::map<std::pair<int, int>, GatePosition> gate_positions;  // by {out_port, in_port}
    unsigned time_settings_version = 1;  // not increased for gate_positions

    // set in one frame, used in the next
    int highlighted_curve = 0, highlighted_curve_next = 0;
    int edited_curve = 0;
    bool open_editor = false;
    bool fit_frequency_plots = false, fit_time_plots = false;
    std::vector<std::string> load_errors;

    std::vector<Marker> markers;
    int next_marker_id = 1;
    int edited_marker = 0;
    bool open_marker_menu = false;

    OrderHighlight order_highlight;

    MedianRange median_range;
    std::map<int, double> medians;
    std::string median_unit;
    bool median_panel_drawn = false;
    bool median_shown = false;
    bool open_median_menu = false;

    bool copy_plots = false;
    bool copying = false;
    ImVec2 plots_min, plots_max;

    std::string session_to_open;
    double session_saved_at = -10.0;

    Loader loader;
    std::vector<bool> color_slot_taken;
    int next_curve_id = 1;
};

std::unique_ptr<Curve> make_curve(core::Measurement measurement);
void add_curves(Session& session, std::vector<std::unique_ptr<Curve>> curves);
void remove_all_curves(Session& session);
uint64_t hash_of(const std::string& bytes);

constexpr const char* session_file_extension = ".tvsession";
void open_paths(Session& session, const std::vector<std::string>& paths);
void open_requested_session(Session& session);
void save_session_as(Session& session);
std::string save_session(Session& session, const std::string& path);
void load_paths(Session& session, const std::vector<std::string>& paths);
bool collect_loaded(Session& session);
bool loading(const Session& session);
void loading_progress(Session& session, int& done, int& found);
void cancel_loading(Session& session);
void finish_loading(Session& session);

void remove_selected_curves(Session& session);
void remove_curve(Session& session, int id);
Curve* find_curve(Session& session, int id);
int largest_port_count(const Session& session);
int visible_curve_count(const Session& session);

bool is_shown(const Session& session, Parameter p);
void set_shown(Session& session, Parameter p, bool shown);
GatePosition& gate_position(Session& session, Parameter p);
Session::LegendPosition& legend_position(Session& session);
LegendPlace legend_place_in_use(const Session& session);
void set_legend_place(Session& session, LegendPlace place);

std::vector<Parameter> shown_panels(const Session& session);

struct FrequencyUnit {
    double hz;
    const char* name;
};
FrequencyUnit frequency_unit(const Session& session);

void compute_phase(Session& session);
void compute_time_arrays(Session& session);
