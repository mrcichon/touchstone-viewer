#pragma once
#include <string>
#include <vector>

std::vector<std::string> program_arguments(int argc, char** argv);

void show_start_failure(const std::string& text);

std::vector<std::string> ask_for_files();
// Windows has no dialog that selects files and folders together.
extern const bool folders_need_their_own_dialog;
std::vector<std::string> ask_for_folder();
std::string ask_for_save_path(const std::string& suggested_name, const std::string& extension);
std::string interface_font_path();
std::string log_file_path();
// pixels: RGBA, 8 bits each, rows from the top. scale: image pixels for one point of the display (1/96 inch on Windows).
void copy_image_to_clipboard(const unsigned char* pixels, int width, int height, float scale);
std::string system_description();
double memory_in_use_mb();
