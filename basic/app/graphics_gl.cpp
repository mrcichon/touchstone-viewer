#include "graphics.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "line_renderer.h"

#define GL_SILENCE_DEPRECATION
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <OpenGL/gl3.h>

#include <algorithm>

namespace graphics {

void set_window_hints() {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
}

bool start(GLFWwindow* window) {
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    return ImGui_ImplOpenGL3_Init("#version 150") && line_renderer::start();
}

void stop() {
    line_renderer::stop();
    ImGui_ImplOpenGL3_Shutdown();
}

std::string description() {
    auto text = [](GLenum name) { return std::string(reinterpret_cast<const char*>(glGetString(name))); };
    return text(GL_VENDOR) + ", " + text(GL_RENDERER) + ", OpenGL " + text(GL_VERSION);
}

void set_vertical_sync(bool on) { glfwSwapInterval(on ? 1 : 0); }

void new_frame() { ImGui_ImplOpenGL3_NewFrame(); }

void draw(GLFWwindow* window) {
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void wait_until_drawn() { glFinish(); }

void show(GLFWwindow* window) { glfwSwapBuffers(window); }

namespace {

std::vector<unsigned char> read_pixels(int left, int bottom, int width, int height) {
    const size_t row = static_cast<size_t>(width) * 4, rows = static_cast<size_t>(height);
    std::vector<unsigned char> pixels(row * rows);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(left, bottom, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (size_t y = 0; y < rows / 2; ++y)  // OpenGL gives the bottom row first
        std::swap_ranges(pixels.data() + row * y, pixels.data() + row * (y + 1), pixels.data() + row * (rows - 1 - y));
    return pixels;
}

}  // namespace

std::vector<unsigned char> read_frame(GLFWwindow* window, int left, int top, int width, int height) {
    int framebuffer_width, framebuffer_height;
    glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
    return read_pixels(left, framebuffer_height - top - height, width, height);
}

std::vector<unsigned char> draw_off_screen(int width, int height, int left, int top, int part_width, int part_height) {
    GLuint image = 0, framebuffer = 0;
    glGenTextures(1, &image);
    glBindTexture(GL_TEXTURE_2D, image);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, image, 0);
    std::vector<unsigned char> pixels;
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        glViewport(0, 0, width, height);
        glClearColor(1, 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        pixels = read_pixels(left, height - top - part_height, part_width, part_height);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &image);
    return pixels;
}

int largest_image_side() {
    GLint side = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &side);
    return side;
}

}  // namespace graphics
