#pragma once
#include "session.h"

enum class Trace { frequency_db, frequency_deg, time_db, gated_db };
struct Panel {
    Trace trace;
    Parameter parameter;
    double x_unit;  // x is in Hz or ns, and is shown divided by this
    const char* x_unit_name;
    const char* y_unit_name;
};

void plot_curves(Session& session, const Panel& panel);

struct LegendSpace {
    LegendPlace place = LegendPlace::off;
    float width_beside = 0.0f;
};
LegendSpace legend_space(const Session& session, ImVec2 room);
struct PanelArea {
    std::string name;
    ImVec2 min, max;
};
PanelArea current_panel_area(const std::string& name);  // call between BeginPlot and EndPlot
void draw_legend(Session& session, LegendPlace place, const std::vector<PanelArea>& panels, ImVec2 column_min, ImVec2 column_max);

void share_x_range_with_ctrl(const std::vector<AxisRange*>& ranges, const std::vector<AxisRange>& before);

float ui_scale();

constexpr float panel_edge_width = 8.0f;
float draw_panel_edge(const char* id, float height);  // returns the horizontal mouse movement during a drag

void draw_line_sample(ImDrawList* draw, ImVec2 from, ImVec2 to, ImU32 color, float width, LineStyle style);
void draw_curve_editor(Session& session);
void draw_marker_menu(Session& session);
void draw_median_menu(Session& session);
void draw_file_panel(Session& session);
void draw_order_highlight_choice(Session& session);
std::vector<int> draw_order_highlight(const Session& session, const Panel& panel);
std::string order_highlight_note(const Session& session);

void draw_title(const Session& session, float width, const std::string& note);
void draw_drop_hint();
int format_frequency(double value, char* buffer, int size, void* user_data);

void begin_plots_without_mouse_marks(const Session& session);
void end_plots_without_mouse_marks(const Session& session);

void draw_frequency_view(Session& session);
void draw_time_view(Session& session);
