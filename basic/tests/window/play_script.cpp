#include "play_script.h"

#include "graphics.h"
#include "simd.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "stb_image_write.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace {

enum class Do { wait, wait_loaded, move, button, wheel, key, type, shot, frame_times, expect_visible, expect_loaded, expect_selected, expect_markers, expect_medians, save_session, quit };

struct Step {
    Do what = Do::wait;
    float x = 0, y = 0;  // move: position; wheel: y is the amount
    int n = 0;           // wait, frame_times, expect: a count; button: the button number
    bool down = false;   // button, key
    ImGuiKey key = ImGuiKey_None;
    std::string text;    // type, shot
};

std::vector<Step> steps;
size_t next_step = 0;
int frames_to_wait = 0;
ImVec2 mouse(-100.0f, -100.0f);
std::string shot_to_save;
int frames_to_time = 0, frames_timed = 0;
double timing_started = 0.0;
bool quit_requested = false;
int failed_expectations = 0;

Step& add(Do what, int n = 0) {
    steps.emplace_back();
    steps.back().what = what;
    steps.back().n = n;
    return steps.back();
}
void add_move(float x, float y) {
    Step& step = add(Do::move);
    step.x = x;
    step.y = y;
}
void add_button(int which, bool down) { add(Do::button, which).down = down; }
void add_key(ImGuiKey key, bool down) {
    Step& step = add(Do::key);
    step.key = key;
    step.down = down;
}
void add_text(Do what, const std::string& text) { add(what).text = text; }

ImGuiKey key_named(const std::string& name) {
    if (name.size() == 1 && name[0] >= 'a' && name[0] <= 'z') return static_cast<ImGuiKey>(ImGuiKey_A + (name[0] - 'a'));
    if (name == "delete") return ImGuiKey_Delete;
    if (name == "backspace") return ImGuiKey_Backspace;
    if (name == "space") return ImGuiKey_Space;
    if (name == "escape") return ImGuiKey_Escape;
    if (name == "enter") return ImGuiKey_Enter;
    if (name == "tab") return ImGuiKey_Tab;
    // The Ctrl modifier of ImGui is the Cmd key on macOS, which the backend sends as Super.
#if defined(__APPLE__)
    if (name == "ctrl") return ImGuiMod_Super;
#else
    if (name == "ctrl") return ImGuiMod_Ctrl;
#endif
    if (name == "shift") return ImGuiMod_Shift;
    return ImGuiKey_None;
}

void add_click(float x, float y, int button, int times) {
    add_move(x, y);
    for (int i = 0; i < times; ++i) {
        add_button(button, true);
        add_button(button, false);
    }
}

bool add_command(const std::string& line, std::string& error) {
    std::istringstream in(line);
    std::string command, word;
    in >> command;
    float x = 0, y = 0, x2 = 0, y2 = 0;
    int n = 0;
    if (command.empty() || command[0] == '#') return true;
    if (command == "wait" && in >> n) add(Do::wait, n);
    else if (command == "wait_loaded") add(Do::wait_loaded);
    else if (command == "move" && in >> x >> y) add_move(x, y);
    else if (command == "click" && in >> x >> y) add_click(x, y, 0, 1);
    else if (command == "rclick" && in >> x >> y) add_click(x, y, 1, 1);
    else if (command == "dclick" && in >> x >> y) add_click(x, y, 0, 2);
    else if ((command == "press" || command == "release") && in >> x >> y) {
        add_move(x, y);
        add_button(0, command == "press");
    } else if (command == "drag" && in >> x >> y >> x2 >> y2) {
        add_move(x, y);
        add_button(0, true);
        for (int i = 1; i <= 8; ++i) add_move(x + (x2 - x) * static_cast<float>(i) / 8.0f, y + (y2 - y) * static_cast<float>(i) / 8.0f);
        add_button(0, false);
    } else if (command == "scroll" && in >> y) {
        add(Do::wheel).y = y;
    } else if (command == "key" && in >> word) {
        std::vector<ImGuiKey> keys;
        std::istringstream parts(word);
        for (std::string part; std::getline(parts, part, '+');) keys.push_back(key_named(part));
        for (ImGuiKey key : keys)
            if (key == ImGuiKey_None) {
                error = "unknown key in: " + line;
                return false;
            }
        for (size_t i = 0; i < keys.size(); ++i) add_key(keys[i], true);
        for (size_t i = keys.size(); i-- > 0;) add_key(keys[i], false);
    } else if ((command == "keydown" || command == "keyup") && in >> word && key_named(word) != ImGuiKey_None) {
        add_key(key_named(word), command == "keydown");
    } else if (command == "type") {
        std::string text;
        std::getline(in >> std::ws, text);
        add_text(Do::type, text);
    } else if (command == "shot" && in >> word) add_text(Do::shot, word);
    else if (command == "frame_times" && in >> n) add(Do::frame_times, n);
    else if (command == "expect" && in >> word >> n && (word == "visible" || word == "loaded" || word == "selected" || word == "markers" || word == "medians"))
        add(word == "visible" ? Do::expect_visible : word == "loaded" ? Do::expect_loaded : word == "selected" ? Do::expect_selected : word == "markers" ? Do::expect_markers : Do::expect_medians, n);
    else if (command == "save_session" && std::getline(in >> std::ws, word)) add_text(Do::save_session, word);
    else if (command == "quit") add(Do::quit);
    else {
        error = "cannot read: " + line;
        return false;
    }
    return true;
}

void expect_count(const char* what, int got, int want) {
    bool ok = got == want;
    std::printf("%s expected %d (%s), found %d\n", ok ? "ok  " : "FAIL", want, what, got);
    if (!ok) ++failed_expectations;
}

void save_window_as_png(GLFWwindow* window, const std::string& path) {
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    std::vector<unsigned char> pixels = graphics::read_frame(window, 0, 0, width, height);
    stbi_write_png(path.c_str(), width, height, 4, pixels.data(), width * 4);
}

}  // namespace

bool start_script(const std::string& path, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "cannot read " + path;
        return false;
    }
    for (std::string line; std::getline(in, line);)
        if (!add_command(line, error)) return false;
    return true;
}

bool script_running() { return next_step < steps.size() || frames_to_wait > 0 || frames_to_time > 0; }
int script_exit_code() { return failed_expectations ? 1 : 0; }

void script_before_frame(Session& session) {
    ImGuiIO& io = ImGui::GetIO();
    // In each frame, after the real mouse position that the platform backend sends while the window has focus.
    io.AddMousePosEvent(mouse.x, mouse.y);
    if (frames_to_time > 0) return;
    if (frames_to_wait > 0) {
        --frames_to_wait;
        return;
    }
    if (next_step >= steps.size()) return;
    const Step& step = steps[next_step];
    if (step.what == Do::wait_loaded && loading(session)) return;
    ++next_step;
    switch (step.what) {
    case Do::wait: frames_to_wait = step.n; break;
    case Do::wait_loaded: frames_to_wait = 3; break;
    case Do::move:
        mouse = ImVec2(step.x, step.y);
        io.AddMousePosEvent(mouse.x, mouse.y);
        break;
    case Do::button: io.AddMouseButtonEvent(step.n, step.down); break;
    case Do::wheel: io.AddMouseWheelEvent(0.0f, step.y); break;
    case Do::key: io.AddKeyEvent(step.key, step.down); break;
    case Do::type: io.AddInputCharactersUTF8(step.text.c_str()); break;
    case Do::shot: shot_to_save = step.text; break;
    case Do::frame_times: frames_to_time = step.n; break;
    case Do::expect_visible: expect_count("visible", visible_curve_count(session), step.n); break;
    case Do::expect_loaded: expect_count("loaded", static_cast<int>(session.curves.size()), step.n); break;
    case Do::expect_selected: {
        int selected = 0;
        for (const auto& c : session.curves) selected += c->selected;
        expect_count("selected", selected, step.n);
        break;
    }
    case Do::expect_markers: expect_count("marker", static_cast<int>(session.markers.size()), step.n); break;
    case Do::expect_medians: expect_count("median", static_cast<int>(session.medians.size()), step.n); break;
    case Do::save_session:
        if (std::string error = save_session(session, step.text); !error.empty()) {
            std::printf("FAIL save_session: %s\n", error.c_str());
            ++failed_expectations;
        }
        break;
    case Do::quit: quit_requested = true; break;
    }
}

void script_after_frame(GLFWwindow* window, Session& session) {
    if (!shot_to_save.empty()) {
        save_window_as_png(window, shot_to_save);
        shot_to_save.clear();
    }
    if (frames_to_time > 0) {
        if (frames_timed == 0) {
            graphics::set_vertical_sync(false);
            timing_started = glfwGetTime();
        }
        if (++frames_timed > frames_to_time) {
            std::printf("%zu curves loaded, %d visible, simd %s: %.2f ms per frame\n", session.curves.size(), visible_curve_count(session),
                        core::simd_set_name(), (glfwGetTime() - timing_started) * 1e3 / frames_to_time);
            graphics::set_vertical_sync(true);
            frames_to_time = frames_timed = 0;
        }
    }
    if (quit_requested) glfwSetWindowShouldClose(window, GLFW_TRUE);
}
