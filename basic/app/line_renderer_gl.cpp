#include "line_renderer.h"

#include "log.h"

#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>

#include <cstring>
#include <map>
#include <vector>

namespace line_renderer {
namespace {

// One instance is one segment: a rectangle of 4 vertices around two successive points of the buffer.
// The fragment shader gives each pixel the part of its area that the line covers, from its distance to the segment.
// The rectangle continues past point b with a round end, which fills the corner to the next segment.
const char* vertex_source = R"(#version 330 core
layout(location = 0) in vec4 a;
layout(location = 1) in float a_curve;
layout(location = 2) in vec4 b;
layout(location = 3) in float b_curve;
uniform vec4 origin;  // x and y of the top left corner of the view, each as hi and lo
uniform vec2 scale;
uniform vec2 corner;
uniform vec2 framebuffer;
uniform vec4 hidden;
uniform sampler2D styles;
out vec2 at;  // position relative to point a: along the segment, across the segment
flat out float segment_length;
flat out float half_width;
flat out vec4 color;

vec2 pixel(vec4 p) {
    return corner + vec2((p.x - origin.x) + (p.y - origin.y), (p.z - origin.z) + (p.w - origin.w)) * scale;
}

void main() {
    int texel = 2 * int(a_curve);
    color = texelFetch(styles, ivec2(texel % 1024, texel / 1024), 0);
    half_width = 0.5 * texelFetch(styles, ivec2((texel + 1) % 1024, (texel + 1) / 1024), 0).x;
    segment_length = 0.0;
    at = vec2(0.0);
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);  // outside the view: not drawn
    if (a_curve != b_curve || half_width <= 0.0 || any(equal(vec4(a_curve), hidden))) return;

    vec2 pa = pixel(a), pb = pixel(b);
    segment_length = length(pb - pa);
    vec2 along = segment_length > 0.0 ? (pb - pa) / segment_length : vec2(1.0, 0.0);
    vec2 across = vec2(-along.y, along.x);
    float margin = half_width + 0.5;
    at = vec2((gl_VertexID & 1) == 0 ? 0.0 : segment_length + margin, (gl_VertexID & 2) == 0 ? -margin : margin);
    vec2 position = pa + along * at.x + across * at.y;
    gl_Position = vec4(position.x / framebuffer.x * 2.0 - 1.0, 1.0 - position.y / framebuffer.y * 2.0, 0.0, 1.0);
}
)";

const char* fragment_source = R"(#version 330 core
in vec2 at;
flat in float segment_length;
flat in float half_width;
flat in vec4 color;
out vec4 result;
void main() {
    float distance_to_segment = length(vec2(at.x - clamp(at.x, 0.0, segment_length), at.y));
    float covered = clamp(half_width + 0.5 - distance_to_segment, 0.0, 1.0);
    result = vec4(color.rgb, color.a * covered);
}
)";

constexpr GLsizei point_bytes = sizeof(Point);
constexpr int styles_in_row = 512;  // a style is 2 texels; a texture row has 1024 texels

struct Buffer {
    GLuint object = 0;
    size_t capacity = 0;
};

GLuint program = 0, vertex_array = 0, style_texture = 0;
GLint origin_at = -1, scale_at = -1, corner_at = -1, framebuffer_at = -1, hidden_at = -1;
std::map<int, Buffer> buffers;
int next_buffer = 1;
std::vector<Style> styles_sent;
int style_rows = 0;
size_t segments_drawn = 0;

GLuint compile(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char text[1024] = "";
        glGetShaderInfoLog(shader, sizeof text, nullptr, text);
        log_line("line renderer: shader not compiled: %s", text);
    }
    return shader;
}

}  // namespace

bool start() {
    GLuint vertex = compile(GL_VERTEX_SHADER, vertex_source), fragment = compile(GL_FRAGMENT_SHADER, fragment_source);
    program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char text[1024] = "";
        glGetProgramInfoLog(program, sizeof text, nullptr, text);
        log_line("line renderer: program not linked: %s", text);
        return false;
    }
    origin_at = glGetUniformLocation(program, "origin");
    scale_at = glGetUniformLocation(program, "scale");
    corner_at = glGetUniformLocation(program, "corner");
    framebuffer_at = glGetUniformLocation(program, "framebuffer");
    hidden_at = glGetUniformLocation(program, "hidden");
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "styles"), 0);
    glUseProgram(0);
    glGenVertexArrays(1, &vertex_array);
    glGenTextures(1, &style_texture);
    glBindTexture(GL_TEXTURE_2D, style_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void stop() {
    for (auto& [id, buffer] : buffers) glDeleteBuffers(1, &buffer.object);
    buffers.clear();
    glDeleteTextures(1, &style_texture);
    glDeleteVertexArrays(1, &vertex_array);
    glDeleteProgram(program);
}

int make_buffer() {
    int id = next_buffer++;
    glGenBuffers(1, &buffers[id].object);
    return id;
}

void delete_buffer(int id) {
    auto found = buffers.find(id);
    if (found == buffers.end()) return;
    glDeleteBuffers(1, &found->second.object);
    buffers.erase(found);
}

Totals totals() {
    Totals t{0, segments_drawn};
    for (const auto& [id, buffer] : buffers) t.bytes_reserved += buffer.capacity * sizeof(Point);
    return t;
}

size_t capacity(int id) { return buffers.at(id).capacity; }

bool reserve(int id, size_t points) {
    Buffer& buffer = buffers.at(id);
    if (points <= buffer.capacity) return true;
    size_t capacity = points + points / 4;
    GLuint larger = 0;
    glGenBuffers(1, &larger);
    while (glGetError() != GL_NO_ERROR) {}
    glBindBuffer(GL_ARRAY_BUFFER, larger);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity * sizeof(Point)), nullptr, GL_DYNAMIC_DRAW);
    if (glGetError() != GL_NO_ERROR) {
        log_line("line renderer: no graphics memory for %zu points", capacity);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDeleteBuffers(1, &larger);
        return false;
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDeleteBuffers(1, &buffer.object);
    buffer.object = larger;
    buffer.capacity = capacity;
    return true;
}

void set_points(int id, size_t first, const Point* points, size_t count) {
    const Buffer& buffer = buffers.at(id);
    if (first + count > buffer.capacity) return;
    glBindBuffer(GL_ARRAY_BUFFER, buffer.object);
    glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(first * sizeof(Point)), static_cast<GLsizeiptr>(count * sizeof(Point)), points);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void set_styles(const Style* styles, size_t count) {
    if (count == styles_sent.size() && (count == 0 || std::memcmp(styles, styles_sent.data(), count * sizeof(Style)) == 0)) return;
    styles_sent.assign(styles, styles + count);
    int rows = static_cast<int>(count / styles_in_row) + 1;
    std::vector<Style> texels(static_cast<size_t>(rows) * styles_in_row);
    std::copy(styles, styles + count, texels.begin());
    glBindTexture(GL_TEXTURE_2D, style_texture);
    if (rows != style_rows) glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 2 * styles_in_row, rows, 0, GL_RGBA, GL_FLOAT, texels.data());
    else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2 * styles_in_row, rows, GL_RGBA, GL_FLOAT, texels.data());
    style_rows = rows;
    glBindTexture(GL_TEXTURE_2D, 0);
}

void draw(int id, size_t first, size_t count, const View& view) {
    auto found = buffers.find(id);
    if (found == buffers.end() || count < 2 || first + count > found->second.capacity || style_rows == 0) return;
    float x_hi, x_lo, y_hi, y_lo;
    split(view.x_at_left, x_hi, x_lo);
    split(view.y_at_top, y_hi, y_lo);
    glUseProgram(program);
    glUniform4f(origin_at, x_hi, x_lo, y_hi, y_lo);
    glUniform2f(scale_at, view.pixels_per_x, view.pixels_per_y);
    glUniform2f(corner_at, view.left, view.top);
    glUniform2f(framebuffer_at, view.framebuffer_width, view.framebuffer_height);
    glUniform4fv(hidden_at, 1, view.hidden_curves);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, style_texture);
    glEnable(GL_SCISSOR_TEST);
    glScissor(static_cast<GLint>(view.left), static_cast<GLint>(view.framebuffer_height - view.top - view.height), static_cast<GLsizei>(view.width), static_cast<GLsizei>(view.height));

    glBindVertexArray(vertex_array);
    glBindBuffer(GL_ARRAY_BUFFER, found->second.object);
    const size_t start = first * sizeof(Point);
    const size_t offsets[4] = {start, start + 4 * sizeof(float), start + sizeof(Point), start + sizeof(Point) + 4 * sizeof(float)};
    for (GLuint i = 0; i < 4; ++i) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, i % 2 == 0 ? 4 : 1, GL_FLOAT, GL_FALSE, point_bytes, reinterpret_cast<const void*>(offsets[i]));
        glVertexAttribDivisor(i, 1);
    }
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(count - 1));
    segments_drawn += count - 1;
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

}  // namespace line_renderer
