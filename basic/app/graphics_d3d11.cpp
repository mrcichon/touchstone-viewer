#include "graphics.h"

#include "d3d11_device.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "line_renderer.h"
#include "log.h"

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <algorithm>
#include <cstring>
#include <dxgi.h>

ID3D11Device* d3d_device = nullptr;
ID3D11DeviceContext* d3d_context = nullptr;

namespace graphics {
namespace {

IDXGISwapChain* swap_chain = nullptr;
ID3D11RenderTargetView* target = nullptr;
int target_width = 0, target_height = 0;
bool vertical_sync = true;
std::string card;

void make_target() {
    ID3D11Texture2D* back = nullptr;
    if (FAILED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&back)))) return;
    d3d_device->CreateRenderTargetView(back, nullptr, &target);
    back->Release();
}

}  // namespace

void set_window_hints() { glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); }

bool start(GLFWwindow* window) {
    DXGI_SWAP_CHAIN_DESC chain{};
    chain.BufferCount = 2;
    chain.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    chain.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    chain.OutputWindow = glfwGetWin32Window(window);
    chain.SampleDesc.Count = 1;
    chain.Windowed = TRUE;
    chain.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    glfwGetFramebufferSize(window, &target_width, &target_height);

    // WARP is the software renderer of Windows, for a session without a graphics driver.
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_10_0;
    const char* renderer = "";
    for (D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, type, nullptr, 0, levels, 3, D3D11_SDK_VERSION, &chain, &swap_chain, &d3d_device, &level, &d3d_context);
        renderer = type == D3D_DRIVER_TYPE_HARDWARE ? "hardware" : "WARP software renderer";
        log_line("Direct3D 11 device, %s: result 0x%08lx", renderer, static_cast<unsigned long>(result));
        if (SUCCEEDED(result)) break;
    }
    if (!d3d_device) return false;

    IDXGIDevice* dxgi_device = nullptr;
    IDXGIAdapter* adapter = nullptr;
    DXGI_ADAPTER_DESC adapter_description{};
    if (SUCCEEDED(d3d_device->QueryInterface(IID_PPV_ARGS(&dxgi_device))) && SUCCEEDED(dxgi_device->GetAdapter(&adapter))) adapter->GetDesc(&adapter_description);
    if (adapter) adapter->Release();
    if (dxgi_device) dxgi_device->Release();
    char name[256] = "";
    WideCharToMultiByte(CP_UTF8, 0, adapter_description.Description, -1, name, sizeof name, nullptr, nullptr);
    card = std::string(name) + ", " + renderer + ", Direct3D feature level " + std::to_string(level >> 12) + "." + std::to_string((level >> 8) & 15) + ", " +
           std::to_string(adapter_description.DedicatedVideoMemory >> 20) + " MB card memory, " + std::to_string(adapter_description.SharedSystemMemory >> 20) + " MB shared memory";

    make_target();
    return target && ImGui_ImplDX11_Init(d3d_device, d3d_context) && line_renderer::start();
}

void stop() {
    line_renderer::stop();
    ImGui_ImplDX11_Shutdown();
    if (target) target->Release();
    if (swap_chain) swap_chain->Release();
    if (d3d_context) d3d_context->Release();
    if (d3d_device) d3d_device->Release();
}

std::string description() { return card; }

void set_vertical_sync(bool on) { vertical_sync = on; }

void new_frame() { ImGui_ImplDX11_NewFrame(); }

void draw(GLFWwindow* window) {
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    if (width > 0 && height > 0 && (width != target_width || height != target_height)) {
        if (target) target->Release();
        target = nullptr;
        HRESULT result = swap_chain->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(result)) log_line("Direct3D: ResizeBuffers to %d x %d failed, result 0x%08lx", width, height, static_cast<unsigned long>(result));
        make_target();
        target_width = width;
        target_height = height;
    }
    if (!target) return;
    const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    d3d_context->OMSetRenderTargets(1, &target, nullptr);
    d3d_context->ClearRenderTargetView(target, white);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void wait_until_drawn() {
    D3D11_QUERY_DESC description{D3D11_QUERY_EVENT, 0};
    ID3D11Query* query = nullptr;
    if (FAILED(d3d_device->CreateQuery(&description, &query))) return;
    d3d_context->End(query);
    d3d_context->Flush();
    BOOL done = FALSE;
    for (;;) {
        HRESULT result = d3d_context->GetData(query, &done, sizeof done, 0);
        if (FAILED(result) || (result == S_OK && done)) break;
        SwitchToThread();
    }
    query->Release();
}

void show(GLFWwindow*) {
    HRESULT result = swap_chain->Present(vertical_sync ? 1 : 0, 0);
    static bool logged = false;
    if (FAILED(result) && !logged) {
        log_line("Direct3D: Present failed, result 0x%08lx, device removed reason 0x%08lx", static_cast<unsigned long>(result), static_cast<unsigned long>(d3d_device->GetDeviceRemovedReason()));
        logged = true;
    }
}

namespace {

bool read_texture(ID3D11Texture2D* texture, int left, int top, int width, int height, std::vector<unsigned char>& pixels) {
    const size_t row = static_cast<size_t>(width) * 4;
    D3D11_TEXTURE2D_DESC description;
    texture->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.MiscFlags = 0;
    ID3D11Texture2D* copy = nullptr;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    bool read = false;
    if (SUCCEEDED(d3d_device->CreateTexture2D(&description, nullptr, &copy))) {
        d3d_context->CopyResource(copy, texture);
        if (SUCCEEDED(d3d_context->Map(copy, 0, D3D11_MAP_READ, 0, &mapped))) {
            int columns = std::min(width, static_cast<int>(description.Width) - left), rows = std::min(height, static_cast<int>(description.Height) - top);
            for (int y = 0; y < rows && columns > 0; ++y)
                std::memcpy(pixels.data() + row * static_cast<size_t>(y), static_cast<const unsigned char*>(mapped.pData) + static_cast<size_t>(top + y) * mapped.RowPitch + static_cast<size_t>(left) * 4,
                            static_cast<size_t>(columns) * 4);
            d3d_context->Unmap(copy, 0);
            read = true;
        }
        copy->Release();
    }
    return read;
}

}  // namespace

std::vector<unsigned char> read_frame(GLFWwindow*, int left, int top, int width, int height) {
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * 4 * static_cast<size_t>(height), 255);
    ID3D11Texture2D* back = nullptr;
    if (FAILED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&back)))) return pixels;
    read_texture(back, left, top, width, height, pixels);
    back->Release();
    return pixels;
}

std::vector<unsigned char> draw_off_screen(int width, int height, int left, int top, int part_width, int part_height) {
    D3D11_TEXTURE2D_DESC description{};
    description.Width = static_cast<UINT>(width);
    description.Height = static_cast<UINT>(height);
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* image = nullptr;
    ID3D11RenderTargetView* image_target = nullptr;
    std::vector<unsigned char> pixels;
    HRESULT result = d3d_device->CreateTexture2D(&description, nullptr, &image);
    if (SUCCEEDED(result)) result = d3d_device->CreateRenderTargetView(image, nullptr, &image_target);
    if (SUCCEEDED(result)) {
        const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        d3d_context->OMSetRenderTargets(1, &image_target, nullptr);
        d3d_context->ClearRenderTargetView(image_target, white);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        pixels.assign(static_cast<size_t>(part_width) * 4 * static_cast<size_t>(part_height), 255);
        if (!read_texture(image, left, top, part_width, part_height, pixels)) pixels.clear();
        d3d_context->OMSetRenderTargets(1, &target, nullptr);
    } else {
        log_line("Direct3D: no image of %d x %d pixels, result 0x%08lx", width, height, static_cast<unsigned long>(result));
    }
    if (image_target) image_target->Release();
    if (image) image->Release();
    return pixels;
}

int largest_image_side() { return d3d_device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0 ? 16384 : 8192; }

}  // namespace graphics
