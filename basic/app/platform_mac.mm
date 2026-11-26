#include "platform.h"

#import <Cocoa/Cocoa.h>
#include <mach/mach.h>
#include <sys/sysctl.h>

const bool folders_need_their_own_dialog = false;
std::vector<std::string> ask_for_folder() { return {}; }

void show_start_failure(const std::string& text) { fprintf(stderr, "%s\n", text.c_str()); }

std::vector<std::string> program_arguments(int argc, char** argv) { return std::vector<std::string>(argv + 1, argv + argc); }

std::vector<std::string> ask_for_files() {
    std::vector<std::string> paths;
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.allowsMultipleSelection = YES;
        panel.canChooseDirectories = YES;
        panel.canChooseFiles = YES;
        panel.message = @"Select Touchstone (.sNp) or .csv files, or folders.";
        if ([panel runModal] == NSModalResponseOK)
            for (NSURL* url in panel.URLs) paths.emplace_back(url.fileSystemRepresentation);
    }
    return paths;
}

std::string ask_for_save_path(const std::string& suggested_name, const std::string& extension) {
    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];
        panel.nameFieldStringValue = @((suggested_name + extension).c_str());
        if ([panel runModal] != NSModalResponseOK) return "";
        std::string path = panel.URL.fileSystemRepresentation;
        bool has_extension = path.size() >= extension.size() && path.compare(path.size() - extension.size(), extension.size(), extension) == 0;
        return has_extension ? path : path + extension;
    }
}

std::string interface_font_path() {
    for (const char* path : {"/System/Library/Fonts/Helvetica.ttc", "/System/Library/Fonts/SFNS.ttf"})
        if ([[NSFileManager defaultManager] fileExistsAtPath:@(path)]) return path;
    return "";
}

std::string log_file_path() { return std::string(NSHomeDirectory().fileSystemRepresentation) + "/Library/Logs/touchstone-viewer.log"; }

void copy_image_to_clipboard(const unsigned char* pixels, int width, int height, float scale) {
    @autoreleasepool {
        NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:width pixelsHigh:height bitsPerSample:8 samplesPerPixel:4
                                                                                hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:width * 4 bitsPerPixel:32];
        memcpy(bitmap.bitmapData, pixels, static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        NSImage* image = [[NSImage alloc] initWithSize:NSMakeSize(width / static_cast<double>(scale), height / static_cast<double>(scale))];  // the pasted size is in points
        [image addRepresentation:bitmap];
        [[NSPasteboard generalPasteboard] clearContents];
        [[NSPasteboard generalPasteboard] writeObjects:@[image]];
    }
}

std::string system_description() {
    char processor[128] = "";
    size_t size = sizeof processor;
    sysctlbyname("machdep.cpu.brand_string", processor, &size, nullptr, 0);
    NSProcessInfo* info = [NSProcessInfo processInfo];
    return std::string("macOS ") + info.operatingSystemVersionString.UTF8String + ", " + processor + ", " + std::to_string(info.physicalMemory >> 30) + " GB memory";
}

double memory_in_use_mb() {
    mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) return 0.0;
    return static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
}
