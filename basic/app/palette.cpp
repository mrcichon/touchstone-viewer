#include "palette.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

constexpr float pi = std::numbers::pi_v<float>;

struct Rgb {
    float r, g, b;
};

// OKLab matrices of Ottosson.
Rgb oklch_to_linear_rgb(float lightness, float chroma, float hue_degrees) {
    float h = hue_degrees * pi / 180.0f;
    float a = chroma * std::cos(h), b = chroma * std::sin(h);
    float l = lightness + 0.3963377774f * a + 0.2158037573f * b;
    float m = lightness - 0.1055613458f * a - 0.0638541728f * b;
    float s = lightness - 0.0894841775f * a - 1.2914855480f * b;
    l = l * l * l;
    m = m * m * m;
    s = s * s * s;
    return {+4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
            -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
            -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s};
}

bool displayable(Rgb c) {
    auto in = [](float v) { return v >= -0.0005f && v <= 1.0005f; };
    return in(c.r) && in(c.g) && in(c.b);
}

float srgb_transfer(float linear) {
    linear = std::clamp(linear, 0.0f, 1.0f);
    return linear <= 0.0031308f ? 12.92f * linear : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

ImVec4 oklch_to_srgb(float lightness, float chroma, float hue_degrees) {
    Rgb c = oklch_to_linear_rgb(lightness, chroma, hue_degrees);
    while (!displayable(c) && chroma > 0.0f) {
        chroma -= 0.004f;
        c = oklch_to_linear_rgb(lightness, chroma, hue_degrees);
    }
    return ImVec4(srgb_transfer(c.r), srgb_transfer(c.g), srgb_transfer(c.b), 1.0f);
}

}  // namespace

// Hue steps of 150 degrees put adjacent slots far apart. After slot 35 the hue increases by the golden angle.
ImVec4 palette_color(int slot) {
    static const float tier_lightness[3] = {0.62f, 0.44f, 0.76f};
    static const float tier_chroma[3] = {0.17f, 0.15f, 0.14f};
    if (slot < 0) slot = 0;
    int tier = (slot / 12) % 3;
    float hue;
    if (slot < palette_swatch_count) {
        hue = 25.0f + static_cast<float>((slot % 12) * 150 % 360) + 10.0f * static_cast<float>(tier);
    } else {
        hue = std::fmod(25.0f + 137.50776f * static_cast<float>(slot), 360.0f);
    }
    return oklch_to_srgb(tier_lightness[tier], tier_chroma[tier], hue);
}
