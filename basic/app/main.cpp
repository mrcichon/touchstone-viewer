#include "graphics.h"
#include "implot.h"
#include "line_renderer.h"
#include "log.h"
#include "misc/cpp/imgui_stdlib.h"
#include "platform.h"
#include "play_script.h"
#include "session.h"
#include "simd.h"
#include "stress_test.h"
#include "views.h"

#include "imgui_impl_glfw.h"
#include "imgui_internal.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

Session session;

// The window is drawn only after input, for long enough to show tooltips and popups.
double draw_until = 0.0;
void keep_drawing() { draw_until = glfwGetTime() + 0.7; }

void on_drop(GLFWwindow*, int count, const char** paths) {
    log_line("drop: %d paths, first: %s", count, count > 0 ? paths[0] : "");
    open_paths(session, std::vector<std::string>(paths, paths + count));
    keep_drawing();
}

// Call before the ImGui backend installs its callbacks: the backend then calls these first.
void watch_input(GLFWwindow* window) {
    glfwSetCursorPosCallback(window, [](GLFWwindow*, double, double) { keep_drawing(); });
    glfwSetMouseButtonCallback(window, [](GLFWwindow*, int, int, int) { keep_drawing(); });
    glfwSetScrollCallback(window, [](GLFWwindow*, double, double) { keep_drawing(); });
    glfwSetKeyCallback(window, [](GLFWwindow*, int, int, int, int) { keep_drawing(); });
    glfwSetCharCallback(window, [](GLFWwindow*, unsigned) { keep_drawing(); });
    glfwSetCursorEnterCallback(window, [](GLFWwindow*, int) { keep_drawing(); });
    glfwSetWindowFocusCallback(window, [](GLFWwindow*, int) { keep_drawing(); });
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow*, int, int) { keep_drawing(); });
    glfwSetWindowRefreshCallback(window, [](GLFWwindow*) { keep_drawing(); });
    glfwSetDropCallback(window, on_drop);
}

const ImVec4 accent(0.16f, 0.43f, 0.86f, 1.0f);

void set_look() {
    ImGui::GetStyle() = ImGuiStyle();
    ImPlot::GetStyle() = ImPlotStyle();
    ImGui::StyleColorsLight();
    ImPlot::StyleColorsLight();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.FramePadding = ImVec2(7.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.AntiAliasedLines = true;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(1, 1, 1, 1);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(1, 1, 1, 0);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(1, 1, 1, 1);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.945f, 0.945f, 0.955f, 1);
    style.Colors[ImGuiCol_Border] = ImVec4(0.82f, 0.82f, 0.84f, 1);
    style.Colors[ImGuiCol_Text] = ImVec4(0.12f, 0.12f, 0.14f, 1);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.52f, 0.56f, 1);
    style.Colors[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.16f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.10f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(accent.x, accent.y, accent.z, 0.24f);

    ImPlotStyle& plot = ImPlot::GetStyle();
    plot.Colors[ImPlotCol_FrameBg] = ImVec4(1, 1, 1, 1);
    plot.Colors[ImPlotCol_PlotBg] = ImVec4(1, 1, 1, 1);
    plot.Colors[ImPlotCol_PlotBorder] = ImVec4(0.35f, 0.35f, 0.38f, 1);
    plot.Colors[ImPlotCol_AxisText] = ImVec4(0.20f, 0.20f, 0.23f, 1);
    plot.Colors[ImPlotCol_AxisGrid] = ImVec4(0.0f, 0.0f, 0.0f, 0.13f);
    plot.Colors[ImPlotCol_AxisTick] = ImVec4(0.35f, 0.35f, 0.38f, 1);
    plot.Colors[ImPlotCol_TitleText] = ImVec4(0.12f, 0.12f, 0.14f, 1);
    plot.Colors[ImPlotCol_AxisBgHovered] = ImVec4(0, 0, 0, 0.04f);
    plot.Colors[ImPlotCol_AxisBgActive] = ImVec4(0, 0, 0, 0.07f);
    plot.PlotPadding = ImVec2(10.0f, 6.0f);
    plot.LabelPadding = ImVec2(6.0f, 5.0f);
    plot.FitPadding = ImVec2(0.0f, 0.04f);
    plot.PlotBorderSize = 1.0f;
    // By default ImPlot ignores all plot input while Ctrl is down. Ctrl applies a zoom to all panels here.
    ImPlot::GetInputMap().OverrideMod = ImGuiMod_None;
}

float user_scale = 1.0f;

// 1.5 for a display set to 150% on Windows. 1 on macOS, where window coordinates are points and the system scales.
float display_scale(GLFWwindow* window) {
    float content_x, content_y;
    int window_width, window_height, framebuffer_width, framebuffer_height;
    glfwGetWindowContentScale(window, &content_x, &content_y);
    glfwGetWindowSize(window, &window_width, &window_height);
    glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
    if (window_width <= 0 || framebuffer_width <= 0) return 1.0f;
    return content_x * static_cast<float>(window_width) / static_cast<float>(framebuffer_width);
}

void use_scale(GLFWwindow* window) {
    static float current = 0.0f;
    float scale = std::clamp(display_scale(window) * user_scale, 0.5f, 4.0f);
    if (std::abs(scale - current) < 0.001f) return;
    float ratio = current > 0.0f ? scale / current : scale;
    current = scale;
    set_look();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetStyle().FontScaleDpi = scale;
    ImPlotStyle& plot = ImPlot::GetStyle();
    for (ImVec2* v : {&plot.PlotPadding, &plot.LabelPadding, &plot.LegendPadding, &plot.MousePosPadding, &plot.AnnotationPadding}) *v = ImVec2(v->x * scale, v->y * scale);
    for (float* v : {&plot.MajorTickLen.x, &plot.MajorTickLen.y, &plot.MinorTickLen.x, &plot.MinorTickLen.y}) *v *= scale;
    session.files_panel_width *= ratio;
    session.settings_panel_width *= ratio;
    session.legend_beside_width *= ratio;
    session.legend_box = ImVec2(session.legend_box.x * ratio, session.legend_box.y * ratio);
    glfwSetWindowSizeLimits(window, static_cast<int>(560 * scale), static_cast<int>(380 * scale), GLFW_DONT_CARE, GLFW_DONT_CARE);
    keep_drawing();
}

bool lit_button(const char* label, bool lit) {
    ImGui::PushStyleColor(ImGuiCol_Button, lit ? accent : ImVec4(0.93f, 0.93f, 0.95f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, lit ? ImVec4(1, 1, 1, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    bool clicked = ImGui::Button(label);
    ImGui::PopStyleColor(2);
    return clicked;
}

void draw_parameter_choice() {
    int ports = largest_port_count(session);
    auto checkbox = [](Parameter p) {
        bool shown = is_shown(session, p);
        if (ImGui::Checkbox(parameter_name(p).c_str(), &shown)) set_shown(session, p, shown);
    };
    if (ports <= 2) {
        for (int in = 1; in <= std::max(ports, 1); ++in)
            for (int out = 1; out <= std::max(ports, 1); ++out) {
                if (in + out > 2) ImGui::SameLine();
                checkbox({out, in});
            }
        return;
    }
    if (ImGui::Button("Parameters")) ImGui::OpenPopup("parameters");
    if (ImGui::BeginPopup("parameters")) {
        for (int out = 1; out <= ports; ++out)
            for (int in = 1; in <= ports; ++in) {
                checkbox({out, in});
                if (in < ports) ImGui::SameLine();
            }
        ImGui::EndPopup();
    }
}

void draw_legend_choice() {
    LegendPlace place = legend_place_in_use(session);
    bool shown = place != LegendPlace::off, beside = place == LegendPlace::beside;
    if (ImGui::Button("Legend")) ImGui::OpenPopup("legend");
    ImGui::SetItemTooltip("Legend options.\nDrag the legend to move it. Click an entry to edit the curve.");
    if (!ImGui::BeginPopup("legend")) return;
    if (ImGui::Checkbox("Show", &shown)) set_legend_place(session, shown ? LegendPlace::top_right : LegendPlace::off);
    if (ImGui::Checkbox("Beside the plots", &beside)) {
        set_legend_place(session, beside ? LegendPlace::beside : LegendPlace::top_right);
        if (beside) session.settings_panel_open = false;
    }
    ImGui::SetNextItemWidth(150.0f * ui_scale());
    ImGui::SliderFloat("Text size", &session.legend_text_size, 7.0f, 32.0f, "%.0f");
    ImGui::EndPopup();
}

double copied_until = 0.0;

// About 4 pixels for each point of a display at 100%. Fewer for a large window.
int copied_image_scale(GLFWwindow* window) {
    const ImVec2 window_size = ImGui::GetIO().DisplaySize;
    const float plots_area = (session.plots_max.x - session.plots_min.x) * (session.plots_max.y - session.plots_min.y);
    int scale = std::max(static_cast<int>(std::lround(4.0f / display_scale(window))), 2);
    while (scale > 1 && (std::max(window_size.x, window_size.y) * static_cast<float>(scale) > static_cast<float>(graphics::largest_image_side()) ||
                         plots_area * static_cast<float>(scale * scale) > 40e6f))
        --scale;
    return scale;
}

void copy_plots_to_clipboard(GLFWwindow* window) {
    double started = glfwGetTime();
    ImDrawData* frame = ImGui::GetDrawData();
    const ImVec2 scale = frame->FramebufferScale;
    int left = static_cast<int>(session.plots_min.x * scale.x), top = static_cast<int>(session.plots_min.y * scale.y);
    int width = static_cast<int>(session.plots_max.x * scale.x) - left, height = static_cast<int>(session.plots_max.y * scale.y) - top;
    if (width <= 0 || height <= 0) return;
    // The main window and the windows in it: no popups, no tooltips.
    ImVector<ImDrawList*> lists;
    lists.swap(frame->CmdLists);
    for (ImDrawList* list : lists)
        if (list->_OwnerName && std::strncmp(list->_OwnerName, "main", 4) == 0) frame->CmdLists.push_back(list);
    frame->CmdListsCount = frame->CmdLists.Size;
    std::vector<unsigned char> pixels = graphics::draw_off_screen(static_cast<int>(frame->DisplaySize.x * scale.x + 0.5f), static_cast<int>(frame->DisplaySize.y * scale.y + 0.5f), left, top, width, height);
    if (pixels.empty()) {
        session.load_errors.push_back("Plots not copied: the graphics card did not make the image.");
        log_line("plots not copied: no image of %d x %d pixels", width, height);
        return;
    }
    copy_image_to_clipboard(pixels.data(), width, height, scale.x * display_scale(window));
    copied_until = glfwGetTime() + 1.5;
    log_line("plots copied: %d x %d pixels, %.0f ms", width, height, (glfwGetTime() - started) * 1e3);
}

void same_row_if_room(float spacing, float width) {
    float right_of_last = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
    if (right_of_last + (spacing + width) * ui_scale() <= ImGui::GetWindowContentRegionMax().x) ImGui::SameLine(0.0f, spacing * ui_scale());
}

void draw_top_bar(GLFWwindow* window) {
    char files_label[48];
    std::snprintf(files_label, sizeof files_label, "Files (%zu)###files", session.curves.size());
    ImGui::Checkbox(files_label, &session.files_panel_open);
    ImGui::SetItemTooltip("Show or hide the file panel.");
    if (session.view == View::time) {
        ImGui::SameLine();
        bool settings = session.settings_panel_open && legend_place_in_use(session) != LegendPlace::beside;
        if (ImGui::Checkbox("Settings", &settings)) {
            session.settings_panel_open = settings;
            if (settings && legend_place_in_use(session) == LegendPlace::beside) set_legend_place(session, LegendPlace::top_right);
        }
        ImGui::SetItemTooltip("Show or hide the settings panel of the time view.");
    }
    ImGui::SameLine();
    draw_legend_choice();
    ImGui::SameLine();
    draw_order_highlight_choice(session);
    ImGui::SameLine();
    if (ImGui::Button(glfwGetTime() < copied_until ? "Copied###copy" : "Copy plots###copy")) session.copy_plots = true;
    ImGui::SetItemTooltip("Copy the plots with title and legend to the clipboard, as an image.\nCtrl+C does the same.");

    same_row_if_room(8.0f, 180.0f);
    ImGui::SetNextItemWidth(std::clamp(ImGui::GetIO().DisplaySize.x * 0.17f, 150.0f * ui_scale(), 260.0f * ui_scale()));
    if (ImGui::InputTextWithHint("##title", "Window name", &session.title))
        glfwSetWindowTitle(window, session.title.empty() ? "Touchstone viewer" : (session.title + " - Touchstone viewer").c_str());

    same_row_if_room(20.0f, 150.0f);
    if (lit_button("Frequency", session.view == View::frequency)) session.view = View::frequency;
    ImGui::SameLine(0.0f, 4.0f);
    if (lit_button("Time", session.view == View::time)) session.view = View::time;

    same_row_if_room(20.0f, largest_port_count(session) <= 1 ? 60.0f : 250.0f);
    draw_parameter_choice();
    if (session.view == View::frequency) {
        same_row_if_room(20.0f, 130.0f);
        Quantity before = session.quantity;
        if (ImGui::RadioButton("dB", session.quantity == Quantity::magnitude_db)) session.quantity = Quantity::magnitude_db;
        ImGui::SameLine();
        if (ImGui::RadioButton("Phase", session.quantity == Quantity::phase_deg)) session.quantity = Quantity::phase_deg;
        if (session.quantity != before) session.fit_frequency_plots = true;
    }

    int hidden = static_cast<int>(session.curves.size()) - visible_curve_count(session);
    if (hidden > 0) {
        same_row_if_room(20.0f, 170.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Hidden: %d", hidden);
        ImGui::SameLine();
        if (ImGui::Button("Show all"))
            for (auto& c : session.curves) c->visible = true;
    }
    if (loading(session)) {
        int done, found;
        loading_progress(session, done, found);
        same_row_if_room(20.0f, 220.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Loading: %d of %d", done, found);
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) cancel_loading(session);
    }
}

void draw_frame(GLFWwindow* window) {
    // Shift and a drag make the median range: no pan at the same time.
    ImPlot::GetInputMap().Pan = ImGui::GetIO().KeyShift ? ImGuiMouseButton_Middle : ImGuiMouseButton_Left;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    draw_top_bar(window);
    ImGui::Separator();
    if (session.files_panel_open) {
        float files_width = std::min(session.files_panel_width, ImGui::GetIO().DisplaySize.x * 0.45f);
        ImGui::BeginChild("files", ImVec2(files_width, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoSavedSettings);
        draw_file_panel(session);
        ImGui::EndChild();
        ImGui::SameLine(0.0f, 0.0f);
        float moved = draw_panel_edge("files edge", ImGui::GetContentRegionAvail().y);
        if (std::abs(moved) > 0.0f) session.files_panel_width = std::clamp(files_width + moved, 150.0f * ui_scale(), 900.0f * ui_scale());
        ImGui::SameLine(0.0f, 0.0f);
    }
    ImGui::BeginChild("view", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (session.view == View::frequency) draw_frequency_view(session);
    else draw_time_view(session);
    ImGui::EndChild();
    open_requested_session(session);
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) save_session_as(session);
    draw_curve_editor(session);
    draw_marker_menu(session);
    draw_median_menu(session);
    ImGui::End();

    session.median_shown = session.median_panel_drawn;
    session.median_panel_drawn = false;

    // Without the delay the plot flashes when the mouse moves across many curves.
    static int candidate = 0;
    static double candidate_since = 0.0;
    if (session.highlighted_curve_next != candidate) {
        candidate = session.highlighted_curve_next;
        candidate_since = glfwGetTime();
    }
    int highlighted = (candidate && glfwGetTime() - candidate_since >= 0.08) ? candidate : 0;
    if (highlighted != session.highlighted_curve || candidate != highlighted) keep_drawing();
    session.highlighted_curve = highlighted;
    session.highlighted_curve_next = 0;
}

void log_frame(double frame_ms) {
    std::string state = session.view == View::time ? "time view," : "frequency view,";
    for (Parameter p : shown_panels(session)) state += " " + parameter_name(p);
    state += session.quantity == Quantity::phase_deg ? ", phase" : ", dB";
    state += session.gate_on ? ", gate on, " : ", gate off, ";
    state += std::to_string(visible_curve_count(session)) + " of " + std::to_string(session.curves.size()) + " curves visible, " + std::to_string(session.markers.size()) + " markers";
    static std::string state_before;
    if (state != state_before) log_line("state: %s", state.c_str());
    state_before = state;

    static std::vector<double> times;
    static double summary_at = glfwGetTime() + 10.0, slow_frame_logged_at = -10.0;
    static size_t segments_before = 0;
    times.push_back(frame_ms);
    if (frame_ms > 50.0 && glfwGetTime() - slow_frame_logged_at > 1.0) {
        slow_frame_logged_at = glfwGetTime();
        log_line("slow frame: %.0f ms, memory %.0f MB", frame_ms, memory_in_use_mb());
    }
    if (glfwGetTime() < summary_at) return;
    std::sort(times.begin(), times.end());
    line_renderer::Totals totals = line_renderer::totals();
    log_line("frames: %zu, median %.1f ms, worst %.1f ms, %zu segments in a frame, memory %.0f MB, graphics memory %.0f MB", times.size(), times[times.size() / 2], times.back(),
             (totals.segments_drawn - segments_before) / times.size(), memory_in_use_mb(), static_cast<double>(totals.bytes_reserved) / (1024.0 * 1024.0));
    times.clear();
    summary_at = glfwGetTime() + 10.0;
    segments_before = totals.segments_drawn;
}

std::string simd_check() {
    const size_t n = 1003;  // not a multiple of the vector length
    std::vector<double> re(n), im(n), with_simd(n), scalar(n);
    for (size_t i = 0; i < n; ++i) {
        double magnitude = std::pow(10.0, -15.0 + 17.0 * static_cast<double>(i) / static_cast<double>(n - 1));
        re[i] = magnitude * std::cos(static_cast<double>(i));
        im[i] = magnitude * std::sin(static_cast<double>(i));
    }
    core::power_db(re.data(), im.data(), with_simd.data(), n);
    core::scalar::power_db(re.data(), im.data(), scalar.data(), n);
    double largest = 0.0;
    for (size_t i = 0; i < n; ++i) largest = std::max(largest, std::abs(with_simd[i] - scalar[i]));
    char text[96];
    std::snprintf(text, sizeof text, "%s, largest difference from the scalar loop %.1e dB", core::simd_set_name(), largest);
    return text;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> paths;
    int width = 1500, height = 900;
    bool scripted = false, size_given = false, write_log = false, stress = false;
    const std::vector<std::string> arguments = program_arguments(argc, argv);
    for (size_t i = 0; i < arguments.size(); ++i) {
        const std::string& argument = arguments[i];
        if (argument == "--play" && i + 1 < arguments.size()) {
            std::string error;
            if (!start_script(arguments[++i], error)) {
                std::fprintf(stderr, "%s\n", error.c_str());
                return 2;
            }
            scripted = true;
        } else if (argument == "--size" && i + 1 < arguments.size()) {
            std::sscanf(arguments[++i].c_str(), "%dx%d", &width, &height);
            size_given = true;
        } else if (argument == "--scale" && i + 1 < arguments.size()) {
            user_scale = std::clamp(static_cast<float>(std::atof(arguments[++i].c_str())), 0.5f, 4.0f);
        } else if (argument == "--log") {
            write_log = true;
        } else if (argument == "--stress") {
            stress = true;
        } else if (argument == "--help") {
            std::puts("usage: touchstone_viewer [files or folders ...] [--size WIDTHxHEIGHT] [--scale FACTOR] [--log] [--stress] [--play script]\n"
                      "--scale makes text and controls larger or smaller. Ctrl with + or - does the same in the window.\n"
                      "--log writes a log file: system, each change of the shown data, frame times, and the call stack after a crash.\n"
                      "--stress adds generated curves in steps to 10000, measures the frame time in both views, and prints a table.\n"
                      "--play operates the window from a script.");
            return 0;
        } else {
            paths.push_back(argument);
        }
    }

    const std::string how_to_report = write_log ? "\nThe log file has the details: " + log_file_path() : "\nStart the program with --log, then send the log file: " + log_file_path();
    if (write_log) std::printf("log file: %s\n", start_log().c_str());
    glfwSetErrorCallback([](int code, const char* text) { log_line("GLFW error %d: %s", code, text); });
    if (!glfwInit()) {
        show_start_failure("The window system did not start." + how_to_report);
        return 1;
    }
    graphics::set_window_hints();
    if (scripted) glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);  // script positions are for the requested size
    GLFWwindow* window = glfwCreateWindow(width, height, "Touchstone viewer", nullptr, nullptr);
    if (!window) {
        show_start_failure("The window was not made." + how_to_report);
        return 1;
    }
    watch_input(window);

    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigInputTextCursorBlink = false;  // a blinking cursor needs redraws while idle
    use_scale(window);
    if (!size_given) {
        int x, y, area_width = 0, area_height = 0;
        glfwGetMonitorWorkarea(glfwGetPrimaryMonitor(), &x, &y, &area_width, &area_height);
        float scale = display_scale(window);
        int wanted_width = static_cast<int>(1500 * scale), wanted_height = static_cast<int>(900 * scale);
        if (area_width > 0) wanted_width = std::min(wanted_width, area_width * 9 / 10);
        if (area_height > 0) wanted_height = std::min(wanted_height, area_height * 9 / 10);
        glfwSetWindowSize(window, wanted_width, wanted_height);
    }
    std::string font = interface_font_path();
    if (!font.empty()) io.Fonts->AddFontFromFileTTF(font.c_str(), 15.0f);
    ImGui_ImplGlfw_InitForOther(window, !scripted);
    if (!graphics::start(window)) {
        log_line("graphics did not start");
        show_start_failure("The graphics did not start." + how_to_report);
        return 1;
    }
    int window_width, window_height, framebuffer_width, framebuffer_height;
    glfwGetWindowSize(window, &window_width, &window_height);
    glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
    std::string system_lines = std::string("version: ") + VIEWER_VERSION + "\nsystem: " + system_description() + "\nprocessor threads: " + std::to_string(std::thread::hardware_concurrency()) + ", simd: " + simd_check() +
                               "\ngraphics: " + graphics::description() + "\nwindow: " + std::to_string(window_width) + " x " + std::to_string(window_height) + ", framebuffer " +
                               std::to_string(framebuffer_width) + " x " + std::to_string(framebuffer_height) + " pixels\n";
    log_line("%s", system_lines.c_str());
    if (stress) {
        start_stress_test(system_lines);
        graphics::set_vertical_sync(false);
    }
    session.loader.wake_frame_loop = glfwPostEmptyEvent;
    open_paths(session, paths);
    keep_drawing();
    std::set<std::string> logged_errors;
    while (!glfwWindowShouldClose(window)) {
        bool button_shows_result = glfwGetTime() < copied_until + 0.1 || ImGui::GetTime() - session.session_saved_at < 1.6;
        if (glfwGetTime() < draw_until || button_shows_result || loading(session) || script_running() || stress_test_running()) {
            glfwPollEvents();
        } else {
            glfwWaitEvents();
            keep_drawing();
        }
        size_t curves_before = session.curves.size();
        if (collect_loaded(session)) {
            keep_drawing();
            if (write_log) log_line("files added: %zu, total %zu, memory %.0f MB", session.curves.size() - curves_before, session.curves.size(), memory_in_use_mb());
        }
        for (const std::string& error : session.load_errors)
            if (write_log && logged_errors.insert(error).second) log_line("not loaded: %s", error.c_str());
        double frame_started = glfwGetTime();

        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Equal)) user_scale = std::min(user_scale * 1.1f, 4.0f);
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Minus)) user_scale = std::max(user_scale / 1.1f, 0.5f);
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_0)) user_scale = 1.0f;
        ImGuiInputTextState* text_field = ImGui::GetInputTextState(ImGui::GetActiveID());
        bool text_selected = text_field && text_field->HasSelection();
        session.copying = !session.curves.empty() && (session.copy_plots || (!text_selected && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)));
        session.copy_plots = false;
        if (session.copying) session.highlighted_curve = 0;
        use_scale(window);  // in each frame: the window can move to a monitor with another scale

        graphics::new_frame();
        ImGui_ImplGlfw_NewFrame();
        // ImGui makes text and line edges for this number of pixels for each unit.
        if (session.copying) {
            const float scale = static_cast<float>(copied_image_scale(window));
            ImGui::GetIO().DisplayFramebufferScale = ImVec2(scale, scale);
        }
        if (scripted) script_before_frame(session);
        stress_before_frame(session);
        ImGui::NewFrame();
        draw_frame(window);
        ImGui::Render();

        if (session.copying) {
            copy_plots_to_clipboard(window);  // this frame is made for the image and not shown
            keep_drawing();
        } else {
            graphics::draw(window);
        }
        if (scripted) script_after_frame(window, session);
        if (write_log || stress) {
            graphics::wait_until_drawn();  // the time then has the work of the graphics card, and not the wait for the display
            double frame_ms = (glfwGetTime() - frame_started) * 1e3;
            if (write_log) log_frame(frame_ms);
            stress_after_frame(session, frame_ms);
            if (stress && !stress_test_running()) glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        if (!session.copying) graphics::show(window);
    }

    log_line("exit: window closed");
    finish_loading(session);
    graphics::stop();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return script_exit_code();
}
