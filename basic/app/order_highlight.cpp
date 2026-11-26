#include "implot.h"
#include "misc/cpp/imgui_stdlib.h"
#include "names.h"
#include "order.h"
#include "series.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <regex>
#include <set>
#include <tuple>

namespace {

struct NumberReader {
    const OrderHighlight& settings;
    std::regex pattern;
    bool pattern_ok = true;

    explicit NumberReader(const OrderHighlight& h) : settings(h) {
        if (!h.by_pattern) return;
        try {
            pattern = std::regex(h.pattern);
        } catch (const std::regex_error&) {
            pattern_ok = false;
        }
    }
    bool read(const std::string& name, double& number) const {
        std::vector<core::NumberInName> numbers;
        if (settings.by_pattern) {
            std::smatch match;
            if (!pattern_ok || settings.pattern.empty() || !std::regex_search(name, match, pattern)) return false;
            numbers = core::numbers_in(match[match.size() > 1 ? 1 : 0].str());  // the first group, or the full match
            if (numbers.empty()) return false;
            number = numbers[0].value;
            return true;
        }
        numbers = core::numbers_in(name);
        if (static_cast<size_t>(settings.field) >= numbers.size()) return false;
        number = numbers[static_cast<size_t>(settings.field)].value;
        return true;
    }
};

struct Regions {
    std::tuple<int, bool, std::string, int, int, uint64_t, uint64_t> inputs;
    std::vector<double> x;
    std::vector<int8_t> direction;
    std::vector<int> left_out;  // Curve::id
    size_t curves = 0;
    int used_in_frame = 0;
};
std::map<std::tuple<int, int, int>, Regions> regions_of_panel;

void find_regions(const Session& session, const Panel& panel, Regions& regions) {
    const OrderHighlight& h = session.order_highlight;
    NumberReader reader(h);
    std::vector<const double*> y;
    std::vector<double> numbers;
    std::vector<int> ids;
    Series grid;  // of the first curve; curves with other frequency points are not used
    for (const auto& c : session.curves) {
        Series d = series_of(*c, panel);
        double number;
        if (!c->visible || d.n < 2 || !reader.read(c->name, number)) continue;
        if (grid.n == 0) grid = d;
        auto equal = [](double a, double b) { return !(a < b) && !(b < a); };
        if (d.n != grid.n || !equal(d.x[0], grid.x[0]) || !equal(d.x[d.n - 1], grid.x[grid.n - 1])) continue;
        y.push_back(d.y);
        numbers.push_back(number);
        ids.push_back(c->id);
    }
    size_t leave_out = y.size() <= 300 ? static_cast<size_t>(h.curves_to_leave_out) : 0;
    core::Ordered ordered = core::ordered_samples_without_some(y, numbers, grid.n, static_cast<size_t>(h.allowed_rank_difference), leave_out);
    regions.x.assign(grid.x, grid.x + grid.n);
    regions.direction = std::move(ordered.direction);
    regions.left_out.clear();
    for (size_t i : ordered.left_out) regions.left_out.push_back(ids[i]);
    regions.curves = y.size();
}

std::string panel_name(const std::tuple<int, int, int>& key) {
    const char* traces[] = {"", " phase", "", " gated"};
    return parameter_name({std::get<1>(key), std::get<2>(key)}) + traces[std::get<0>(key)];
}

}  // namespace

std::vector<int> draw_order_highlight(const Session& session, const Panel& panel) {
    const OrderHighlight& h = session.order_highlight;
    if (!h.on || panel.trace == Trace::time_db) return {};
    Regions& regions = regions_of_panel[{static_cast<int>(panel.trace), panel.parameter.out_port, panel.parameter.in_port}];
    regions.used_in_frame = ImGui::GetFrameCount();
    uint64_t visible = 0, versions = 0;
    for (const auto& c : session.curves) {
        if (!c->visible || series_of(*c, panel).n < 2) continue;
        visible += static_cast<uint64_t>(c->id) * static_cast<uint64_t>(c->id);
        versions += version_of(*c, panel);
    }
    auto inputs = std::make_tuple(h.field, h.by_pattern, h.pattern, h.allowed_rank_difference, h.curves_to_leave_out, visible, versions);
    if (inputs != regions.inputs || regions.x.empty()) {
        find_regions(session, panel, regions);
        regions.inputs = inputs;
    }

    // One rectangle for each run of samples with the same direction, edges halfway to the adjacent samples.
    const std::vector<double>& x = regions.x;
    const ImPlotRect limits = ImPlot::GetPlotLimits();
    ImDrawList* draw = ImPlot::GetPlotDrawList();
    ImPlot::PushPlotClipRect();
    for (size_t first = 0; first < x.size();) {
        size_t last = first;
        while (last + 1 < x.size() && regions.direction[last + 1] == regions.direction[first]) ++last;
        double lo = first == 0 ? x[0] : 0.5 * (x[first - 1] + x[first]), hi = last + 1 == x.size() ? x[last] : 0.5 * (x[last] + x[last + 1]);
        if (regions.direction[first] != 0 && hi >= limits.X.Min && lo <= limits.X.Max)
            draw->AddRectFilled(ImPlot::PlotToPixels(lo, limits.Y.Max), ImPlot::PlotToPixels(hi, limits.Y.Min), IM_COL32(0, 128, 0, 60));
        first = last + 1;
    }
    ImPlot::PopPlotClipRect();
    return regions.left_out;
}

std::string order_highlight_note(const Session& session) {
    const OrderHighlight& h = session.order_highlight;
    if (!h.on) return "";
    size_t left_out = 0;  // as drawn in the frame before
    for (const auto& [key, regions] : regions_of_panel)
        if (regions.used_in_frame >= ImGui::GetFrameCount() - 1) left_out = std::max(left_out, regions.left_out.size());
    std::string note;
    if (h.allowed_rank_difference > 0) note = "allowed rank difference " + std::to_string(h.allowed_rank_difference);
    if (left_out > 0) note += (note.empty() ? "" : ", ") + std::to_string(left_out) + (left_out == 1 ? " curve left out" : " curves left out");
    return note.empty() ? "" : "Highlight: " + note;
}

void draw_order_highlight_choice(Session& session) {
    OrderHighlight& h = session.order_highlight;
    if (ImGui::Button("Highlight")) ImGui::OpenPopup("highlight");
    ImGui::SetItemTooltip("Highlight the frequency ranges where the curves are in the order of a number in their file names.");
    if (!ImGui::BeginPopup("highlight")) return;
    const float width = 430.0f * ui_scale();
    ImGui::Checkbox("Highlight the ranges where the curves are in order", &h.on);
    ImGui::PushTextWrapPos(width);
    ImGui::TextDisabled("The order is that of a number in the file names. The value can rise or fall with the number.");
    ImGui::PopTextWrapPos();

    ImGui::SeparatorText("Number");
    bool any_field = false;
    for (int field = 0; field < 8; ++field) {
        std::set<double> values;
        std::string example;
        for (const auto& c : session.curves) {
            std::vector<core::NumberInName> numbers = core::numbers_in(c->name);
            if (!c->visible || static_cast<size_t>(field) >= numbers.size()) continue;
            const core::NumberInName& number = numbers[static_cast<size_t>(field)];
            if (example.empty()) example = c->name.substr(0, number.begin) + "[" + c->name.substr(number.begin, number.end - number.begin) + "]" + c->name.substr(number.end);
            values.insert(number.value);
        }
        if (values.size() < 2) continue;
        any_field = true;
        char label[256];
        std::snprintf(label, sizeof label, "%s    %zu values, %g to %g", example.c_str(), values.size(), *values.begin(), *values.rbegin());
        if (ImGui::RadioButton(label, !h.by_pattern && h.field == field)) {
            h.field = field;
            h.by_pattern = false;
        }
    }
    if (!any_field) ImGui::TextDisabled("The names of the visible files have no number that differs.");

    ImGui::SetNextItemWidth(150.0f * ui_scale());
    ImGui::SliderInt("Allowed rank difference", &h.allowed_rank_difference, 0, 10);
    ImGui::SetItemTooltip("0: each curve must be at its position in the order.\n1: a curve can be one position away, and so on.");
    ImGui::SetNextItemWidth(150.0f * ui_scale());
    ImGui::SliderInt("Curves to leave out", &h.curves_to_leave_out, 0, 3);
    ImGui::SetItemTooltip("The curves that break the order most are left out. They are drawn dotted.\nNot available for more than 300 curves.");

    if (ImGui::TreeNodeEx("Advanced", ImGuiTreeNodeFlags_SpanAvailWidth)) {
        if (ImGui::RadioButton("Number from a regular expression", h.by_pattern)) h.by_pattern = true;
        ImGui::SetNextItemWidth(width - ImGui::GetCursorPosX());
        ImGui::InputTextWithHint("##pattern", "example: (\\d+)ml", &h.pattern);
        if (h.by_pattern) {
            NumberReader reader(h);
            int matched = 0, visible = 0;
            double number;
            for (const auto& c : session.curves) {
                visible += c->visible;
                matched += c->visible && reader.read(c->name, number);
            }
            if (!reader.pattern_ok) ImGui::TextColored(ImVec4(0.75f, 0.15f, 0.1f, 1.0f), "The expression is not valid.");
            else ImGui::TextDisabled("Gives a number for %d of %d visible files. The number is in the first group.", matched, visible);
        }
        ImGui::TreePop();
    }

    if (h.on) {
        ImGui::SeparatorText("Result");
        for (const auto& [key, regions] : regions_of_panel) {
            if (regions.used_in_frame < ImGui::GetFrameCount() - 1) continue;
            size_t ordered = static_cast<size_t>(std::count_if(regions.direction.begin(), regions.direction.end(), [](int8_t d) { return d != 0; }));
            ImGui::Text("%s: %zu curves, in order at %.0f%% of the points", panel_name(key).c_str(), regions.curves,
                        regions.direction.empty() ? 0.0 : 100.0 * static_cast<double>(ordered) / static_cast<double>(regions.direction.size()));
            for (int id : regions.left_out)
                if (const Curve* c = find_curve(session, id)) ImGui::TextDisabled("    left out: %s", c->shown_label().c_str());
        }
    }
    ImGui::EndPopup();
}
