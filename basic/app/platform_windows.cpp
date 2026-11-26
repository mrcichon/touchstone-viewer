#include "platform.h"

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <psapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

const bool folders_need_their_own_dialog = true;

namespace {

std::string utf8(const wchar_t* wide) {
    int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    std::string text(static_cast<size_t>(size > 0 ? size - 1 : 0), '\0');
    if (size > 1) WideCharToMultiByte(CP_UTF8, 0, wide, -1, text.data(), size, nullptr, nullptr);
    return text;
}

std::wstring wide(const std::string& text) {
    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring result(static_cast<size_t>(size > 0 ? size - 1 : 0), L'\0');
    if (size > 1) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
    return result;
}

const COMDLG_FILTERSPEC measurement_files[] = {{L"Measurement and session files", L"*.s?p;*.s??p;*.csv;*.tvsession"}, {L"All files", L"*.*"}};
const COMDLG_FILTERSPEC session_files[] = {{L"Session files", L"*.tvsession"}};

std::vector<std::string> show_dialog(bool save, DWORD options, const COMDLG_FILTERSPEC* types, UINT type_count, const std::wstring& name, const wchar_t* default_extension) {
    std::vector<std::string> paths;
    static const bool com_started = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    IFileDialog* dialog = nullptr;
    HRESULT result = CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(result)) {
        log_line("file dialog not made: result 0x%08lx, COM started %d", static_cast<unsigned long>(result), com_started);
        return paths;
    }
    DWORD current = 0;
    dialog->GetOptions(&current);
    dialog->SetOptions(current | options | FOS_FORCEFILESYSTEM);
    if (types) dialog->SetFileTypes(type_count, types);
    if (!name.empty()) dialog->SetFileName(name.c_str());
    if (default_extension) dialog->SetDefaultExtension(default_extension);
    if (SUCCEEDED(dialog->Show(GetActiveWindow()))) {
        auto add = [&](IShellItem* item) {
            wchar_t* path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                paths.push_back(utf8(path));
                CoTaskMemFree(path);
            }
        };
        IFileOpenDialog* open_dialog = nullptr;
        IShellItemArray* items = nullptr;
        IShellItem* item = nullptr;
        DWORD count = 0;
        if (!save && SUCCEEDED(dialog->QueryInterface(IID_PPV_ARGS(&open_dialog))) && SUCCEEDED(open_dialog->GetResults(&items)) && SUCCEEDED(items->GetCount(&count))) {
            for (DWORD i = 0; i < count; ++i)
                if (SUCCEEDED(items->GetItemAt(i, &item))) {
                    add(item);
                    item->Release();
                }
        } else if (save && SUCCEEDED(dialog->GetResult(&item))) {
            add(item);
            item->Release();
        }
        if (items) items->Release();
        if (open_dialog) open_dialog->Release();
    }
    dialog->Release();
    return paths;
}

}  // namespace

std::vector<std::string> program_arguments(int, char**) {
    // A desktop program has no console of its own.
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        std::freopen("CONOUT$", "w", stdout);
        std::freopen("CONOUT$", "w", stderr);
    }
    // The arguments of main are in the system code page. The wide command line has all characters.
    std::vector<std::string> arguments;
    int count = 0;
    wchar_t** wide_arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; wide_arguments && i < count; ++i) arguments.push_back(utf8(wide_arguments[i]));
    if (wide_arguments) LocalFree(wide_arguments);
    return arguments;
}

void show_start_failure(const std::string& text) { MessageBoxW(nullptr, wide(text).c_str(), L"Touchstone viewer", MB_OK | MB_ICONERROR); }

std::vector<std::string> ask_for_files() { return show_dialog(false, FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST, measurement_files, 2, L"", nullptr); }

std::vector<std::string> ask_for_folder() { return show_dialog(false, FOS_PICKFOLDERS, nullptr, 0, L"", nullptr); }

std::string ask_for_save_path(const std::string& suggested_name, const std::string& extension) {
    std::vector<std::string> paths = show_dialog(true, FOS_OVERWRITEPROMPT, session_files, 1, wide(suggested_name), wide(extension).c_str() + 1);  // without the '.'
    return paths.empty() ? "" : paths[0];
}

std::string interface_font_path() {
    wchar_t folder[MAX_PATH] = L"";
    GetWindowsDirectoryW(folder, MAX_PATH);
    std::string path = utf8(folder) + "\\Fonts\\segoeui.ttf";
    return GetFileAttributesW(wide(path).c_str()) != INVALID_FILE_ATTRIBUTES ? path : "";
}

std::string log_file_path() {
    wchar_t* folder = nullptr;
    std::string path = "touchstone-viewer.log";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder))) path = utf8(folder) + "\\Touchstone Viewer\\touchstone-viewer.log";
    CoTaskMemFree(folder);
    return path;
}

void copy_image_to_clipboard(const unsigned char* pixels, int width, int height, float scale) {
    // CF_DIB: a header, then rows from the bottom, each pixel as blue, green, red, not used.
    const size_t row = static_cast<size_t>(width) * 4, size = sizeof(BITMAPINFOHEADER) + row * static_cast<size_t>(height);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory) return;
    unsigned char* bytes = static_cast<unsigned char*>(GlobalLock(memory));
    BITMAPINFOHEADER header{};
    header.biSize = sizeof header;
    header.biWidth = width;
    header.biHeight = height;
    header.biPlanes = 1;
    header.biBitCount = 32;
    header.biCompression = BI_RGB;
    header.biXPelsPerMeter = header.biYPelsPerMeter = std::lround(static_cast<double>(scale) * 96.0 / 0.0254);
    std::memcpy(bytes, &header, sizeof header);
    for (int y = 0; y < height; ++y) {
        const unsigned char* from = pixels + row * static_cast<size_t>(height - 1 - y);
        unsigned char* to = bytes + sizeof header + row * static_cast<size_t>(y);
        for (int x = 0; x < width; ++x) {
            to[4 * x] = from[4 * x + 2];
            to[4 * x + 1] = from[4 * x + 1];
            to[4 * x + 2] = from[4 * x];
            to[4 * x + 3] = 255;
        }
    }
    GlobalUnlock(memory);
    bool on_clipboard = false;
    if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        on_clipboard = SetClipboardData(CF_DIB, memory) != nullptr;  // the clipboard owns the memory after this call
        CloseClipboard();
    }
    if (!on_clipboard) {
        log_line("image not put on the clipboard: error %lu", GetLastError());
        GlobalFree(memory);
    }
}

std::string system_description() {
    // RtlGetVersion gives the true version. GetVersionEx gives the version that the manifest declares.
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof version;
    using RtlGetVersionFunction = LONG(WINAPI*)(OSVERSIONINFOW*);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"))
        if (FARPROC function = GetProcAddress(ntdll, "RtlGetVersion")) reinterpret_cast<RtlGetVersionFunction>(reinterpret_cast<void*>(function))(&version);
    wchar_t processor[256] = L"";
    DWORD processor_size = sizeof processor;
    RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString", RRF_RT_REG_SZ, nullptr, processor, &processor_size);
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof memory;
    GlobalMemoryStatusEx(&memory);
    char text[512];
    std::snprintf(text, sizeof text, "Windows %lu.%lu build %lu, %s, %.1f GB memory, %.1f GB free%s", version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber,
                  utf8(processor).c_str(), static_cast<double>(memory.ullTotalPhys) / 1073741824.0, static_cast<double>(memory.ullAvailPhys) / 1073741824.0,
                  GetSystemMetrics(SM_REMOTESESSION) ? ", remote desktop session" : "");
    return text;
}

double memory_in_use_mb() {
    PROCESS_MEMORY_COUNTERS counters{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters)) return 0.0;
    return static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0);
}
