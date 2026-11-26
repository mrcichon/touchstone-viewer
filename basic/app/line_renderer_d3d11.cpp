#include "line_renderer.h"

#include "d3d11_device.h"
#include "log.h"

#include <cstring>
#include <d3dcompiler.h>
#include <map>
#include <vector>

namespace line_renderer {
namespace {

const char* shader_source = R"(
cbuffer View : register(b0) {
    float4 origin;  // x and y of the top left corner of the view, each as hi and lo
    float2 scale;
    float2 corner;
    float2 framebuffer;
    float2 not_used;
    float4 hidden;
};
Texture2D<float4> styles : register(t0);

struct Segment {
    float4 a : POINT_A;
    float a_curve : CURVE_A;
    float4 b : POINT_B;
    float b_curve : CURVE_B;
    uint vertex : SV_VertexID;
};
struct Corner {
    float4 position : SV_Position;
    float2 at : AT;
    nointerpolation float segment_length : SEGMENT_LENGTH;
    nointerpolation float half_width : HALF_WIDTH;
    nointerpolation float4 color : COLOR;
};

float2 pixel(float4 p) {
    return corner + float2((p.x - origin.x) + (p.y - origin.y), (p.z - origin.z) + (p.w - origin.w)) * scale;
}

Corner vertex_main(Segment segment) {
    Corner result;
    int texel = 2 * (int)segment.a_curve;
    result.color = styles.Load(int3(texel % 1024, texel / 1024, 0));
    result.half_width = 0.5 * styles.Load(int3((texel + 1) % 1024, (texel + 1) / 1024, 0)).x;
    result.segment_length = 0.0;
    result.at = float2(0.0, 0.0);
    result.position = float4(2.0, 2.0, 2.0, 1.0);  // outside the view: not drawn
    if (segment.a_curve != segment.b_curve || result.half_width <= 0.0 || any(segment.a_curve == hidden)) return result;

    float2 pa = pixel(segment.a), pb = pixel(segment.b);
    result.segment_length = length(pb - pa);
    float2 along = result.segment_length > 0.0 ? (pb - pa) / result.segment_length : float2(1.0, 0.0);
    float2 across = float2(-along.y, along.x);
    float margin = result.half_width + 0.5;
    result.at = float2((segment.vertex & 1) == 0 ? 0.0 : result.segment_length + margin, (segment.vertex & 2) == 0 ? -margin : margin);
    float2 position = pa + along * result.at.x + across * result.at.y;
    result.position = float4(position.x / framebuffer.x * 2.0 - 1.0, 1.0 - position.y / framebuffer.y * 2.0, 0.0, 1.0);
    return result;
}

float4 pixel_main(Corner corner_of_segment) : SV_Target {
    float distance_to_segment = length(float2(corner_of_segment.at.x - clamp(corner_of_segment.at.x, 0.0, corner_of_segment.segment_length), corner_of_segment.at.y));
    float covered = saturate(corner_of_segment.half_width + 0.5 - distance_to_segment);
    return float4(corner_of_segment.color.rgb, corner_of_segment.color.a * covered);
}
)";

struct ViewConstants {
    float origin[4], scale[2], corner[2], framebuffer[2], not_used[2], hidden[4];
};

constexpr UINT styles_in_row = 512;  // a style is 2 texels; a texture row has 1024 texels

struct Buffer {
    ID3D11Buffer* object = nullptr;
    size_t capacity = 0;
};

ID3D11VertexShader* vertex_shader = nullptr;
ID3D11PixelShader* pixel_shader = nullptr;
ID3D11InputLayout* layout = nullptr;
ID3D11Buffer* view_constants = nullptr;
ID3D11Texture2D* style_texture = nullptr;
ID3D11ShaderResourceView* style_view = nullptr;
std::map<int, Buffer> buffers;
int next_buffer = 1;
std::vector<Style> styles_sent;
UINT style_rows = 0;
size_t segments_drawn = 0;

ID3DBlob* compile(const char* entry, const char* target) {
    ID3DBlob *code = nullptr, *errors = nullptr;
    // IEEE strictness: no reordering of the hi and lo subtractions in pixel().
    HRESULT result = D3DCompile(shader_source, std::strlen(shader_source), "lines", nullptr, nullptr, entry, target, D3DCOMPILE_IEEE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(result)) log_line("line renderer: %s not compiled, result 0x%08lx: %s", entry, static_cast<unsigned long>(result), errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
    if (errors) errors->Release();
    return code;
}

template <typename Object>
void release(Object*& object) {
    if (object) object->Release();
    object = nullptr;
}

}  // namespace

bool start() {
    ID3DBlob *vertex_code = compile("vertex_main", "vs_4_0"), *pixel_code = compile("pixel_main", "ps_4_0");
    if (!vertex_code || !pixel_code) return false;
    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        // One instance is one segment: two successive points of the buffer.
        {"POINT_A", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"CURVE_A", 0, DXGI_FORMAT_R32_FLOAT, 0, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"POINT_B", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, sizeof(Point), D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"CURVE_B", 0, DXGI_FORMAT_R32_FLOAT, 0, sizeof(Point) + 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };
    D3D11_BUFFER_DESC constants{};
    constants.ByteWidth = sizeof(ViewConstants);
    constants.Usage = D3D11_USAGE_DEFAULT;
    constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bool ok = SUCCEEDED(d3d_device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, &vertex_shader)) &&
              SUCCEEDED(d3d_device->CreatePixelShader(pixel_code->GetBufferPointer(), pixel_code->GetBufferSize(), nullptr, &pixel_shader)) &&
              SUCCEEDED(d3d_device->CreateInputLayout(elements, 4, vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), &layout)) &&
              SUCCEEDED(d3d_device->CreateBuffer(&constants, nullptr, &view_constants));
    vertex_code->Release();
    pixel_code->Release();
    if (!ok) log_line("line renderer: Direct3D objects not made");
    return ok;
}

void stop() {
    for (auto& [id, buffer] : buffers) release(buffer.object);
    buffers.clear();
    release(style_view);
    release(style_texture);
    release(view_constants);
    release(layout);
    release(pixel_shader);
    release(vertex_shader);
}

int make_buffer() {
    int id = next_buffer++;
    buffers[id] = Buffer{};
    return id;
}

void delete_buffer(int id) {
    auto found = buffers.find(id);
    if (found == buffers.end()) return;
    release(found->second.object);
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
    size_t new_capacity = points + points / 4;
    D3D11_BUFFER_DESC description{};
    description.ByteWidth = static_cast<UINT>(new_capacity * sizeof(Point));
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    ID3D11Buffer* larger = nullptr;
    HRESULT result = new_capacity * sizeof(Point) > 0xFFFFFFFFull ? E_OUTOFMEMORY : d3d_device->CreateBuffer(&description, nullptr, &larger);
    if (FAILED(result)) {
        log_line("line renderer: no graphics memory for %zu points, result 0x%08lx", new_capacity, static_cast<unsigned long>(result));
        return false;
    }
    release(buffer.object);
    buffer.object = larger;
    buffer.capacity = new_capacity;
    return true;
}

void set_points(int id, size_t first, const Point* points, size_t count) {
    const Buffer& buffer = buffers.at(id);
    if (count == 0 || first + count > buffer.capacity) return;
    D3D11_BOX range{static_cast<UINT>(first * sizeof(Point)), 0, 0, static_cast<UINT>((first + count) * sizeof(Point)), 1, 1};
    d3d_context->UpdateSubresource(buffer.object, 0, &range, points, 0, 0);
}

void set_styles(const Style* styles, size_t count) {
    if (count == styles_sent.size() && (count == 0 || std::memcmp(styles, styles_sent.data(), count * sizeof(Style)) == 0)) return;
    styles_sent.assign(styles, styles + count);
    UINT rows = static_cast<UINT>(count / styles_in_row) + 1;
    std::vector<Style> texels(static_cast<size_t>(rows) * styles_in_row);
    std::copy(styles, styles + count, texels.begin());
    if (rows != style_rows) {
        release(style_view);
        release(style_texture);
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 2 * styles_in_row;
        description.Height = rows;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(d3d_device->CreateTexture2D(&description, nullptr, &style_texture)) || FAILED(d3d_device->CreateShaderResourceView(style_texture, nullptr, &style_view))) {
            log_line("line renderer: style texture of %u rows not made", rows);
            style_rows = 0;
            return;
        }
        style_rows = rows;
    }
    d3d_context->UpdateSubresource(style_texture, 0, nullptr, texels.data(), 2 * styles_in_row * 4 * sizeof(float), 0);
}

void draw(int id, size_t first, size_t count, const View& view) {
    auto found = buffers.find(id);
    if (found == buffers.end() || count < 2 || first + count > found->second.capacity || style_rows == 0) return;
    ViewConstants constants{};
    split(view.x_at_left, constants.origin[0], constants.origin[1]);
    split(view.y_at_top, constants.origin[2], constants.origin[3]);
    constants.scale[0] = view.pixels_per_x;
    constants.scale[1] = view.pixels_per_y;
    constants.corner[0] = view.left;
    constants.corner[1] = view.top;
    constants.framebuffer[0] = view.framebuffer_width;
    constants.framebuffer[1] = view.framebuffer_height;
    std::memcpy(constants.hidden, view.hidden_curves, sizeof constants.hidden);
    d3d_context->UpdateSubresource(view_constants, 0, nullptr, &constants, 0, 0);

    // Blend, depth and rasterizer states stay as ImGui set them: alpha blend, no depth test, scissor on.
    const D3D11_RECT scissor{static_cast<LONG>(view.left), static_cast<LONG>(view.top), static_cast<LONG>(view.left + view.width), static_cast<LONG>(view.top + view.height)};
    const UINT stride = sizeof(Point), offset = 0;
    d3d_context->RSSetScissorRects(1, &scissor);
    d3d_context->IASetInputLayout(layout);
    d3d_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    d3d_context->IASetVertexBuffers(0, 1, &found->second.object, &stride, &offset);
    d3d_context->VSSetShader(vertex_shader, nullptr, 0);
    d3d_context->VSSetConstantBuffers(0, 1, &view_constants);
    d3d_context->VSSetShaderResources(0, 1, &style_view);
    d3d_context->PSSetShader(pixel_shader, nullptr, 0);
    d3d_context->DrawInstanced(4, static_cast<UINT>(count - 1), 0, static_cast<UINT>(first));
    segments_drawn += count - 1;
}

}  // namespace line_renderer
