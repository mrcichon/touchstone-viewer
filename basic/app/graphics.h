#pragma once
#include <string>
#include <vector>

struct GLFWwindow;

namespace graphics {

void set_window_hints();         // call before glfwCreateWindow
bool start(GLFWwindow* window);  // the ImGui context must exist
void stop();
std::string description();
void set_vertical_sync(bool on);
void new_frame();  // call before ImGui::NewFrame
void draw(GLFWwindow* window);
void wait_until_drawn();
void show(GLFWwindow* window);
// RGBA, 8 bits each, rows from the top. Call between draw and show.
std::vector<unsigned char> read_frame(GLFWwindow* window, int left, int top, int width, int height);
// Draws the frame into an image, not the window, and returns a part of it. Empty: no image was made.
std::vector<unsigned char> draw_off_screen(int width, int height, int left, int top, int part_width, int part_height);
int largest_image_side();

}  // namespace graphics
