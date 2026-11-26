#pragma once
#include <cstddef>

namespace line_renderer {

// The graphics card calculates in float, and a float cannot hold a frequency in Hz exactly. Each coordinate is a double
// split into two floats, value = hi + lo. The card subtracts the corner of the view from each, then adds the differences.
struct Point {
    float x_hi, x_lo, y_hi, y_lo;
    float curve;  // Curve::id; a segment is drawn only between two successive points of the same curve
};
inline void split(double value, float& hi, float& lo) {
    hi = static_cast<float>(value);
    lo = static_cast<float>(value - static_cast<double>(hi));
}

struct Style {  // the index into the style array is Curve::id
    float r = 0, g = 0, b = 0, a = 0;
    float width = 0;  // in framebuffer pixels; 0 = not drawn
    float not_used[3] = {0, 0, 0};
};

struct View {
    double x_at_left = 0, y_at_top = 0;
    float pixels_per_x = 1, pixels_per_y = 1;  // pixels_per_y is negative: y increases upward
    float left = 0, top = 0, width = 0, height = 0;
    float framebuffer_width = 1, framebuffer_height = 1;
    float hidden_curves[4] = {-1, -1, -1, -1};
};

bool start();
void stop();

int make_buffer();
void delete_buffer(int buffer);
size_t capacity(int buffer);  // in points
// Above the capacity the points in the buffer are lost. False if there is not enough graphics memory.
bool reserve(int buffer, size_t points);
void set_points(int buffer, size_t first, const Point* points, size_t count);
void set_styles(const Style* styles, size_t count);
struct Totals {
    size_t bytes_reserved = 0;
    size_t segments_drawn = 0;
};
Totals totals();

void draw(int buffer, size_t first, size_t count, const View& view);

}  // namespace line_renderer
