#include "views.h"

#include "platform.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

std::string short_folder(const std::string& folder) {
    size_t last = folder.find_last_of("/\\");
    if (last == std::string::npos || last == 0) return folder;
    size_t before = folder.find_last_of("/\\", last - 1);
    return before == std::string::npos ? folder : folder.substr(before + 1);
}

bool contains(const std::string& text, const char* typed) {
    size_t length = std::strlen(typed);
    if (length == 0) return true;
    auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
    for (size_t start = 0; start + length <= text.size(); ++start) {
        size_t k = 0;
        while (k < length && lower(text[start + k]) == lower(typed[k])) ++k;
        if (k == length) return true;
    }
    return false;
}

struct Folder {
    const std::string* path;
    std::vector<Curve*> curves;
    bool open;
};

struct Row {
    Folder* folder;
    Curve* curve;
};

void draw_folder_row(Folder& folder, std::vector<std::string>& closed_folders) {
    ImGui::PushID(folder.path->c_str());
    if (ImGui::ArrowButton("open", folder.open ? ImGuiDir_Down : ImGuiDir_Right)) {
        if (folder.open) closed_folders.push_back(*folder.path);
        else std::erase(closed_folders, *folder.path);
    }
    ImGui::SameLine();
    size_t visible = static_cast<size_t>(std::count_if(folder.curves.begin(), folder.curves.end(), [](Curve* c) { return c->visible; }));
    bool all = visible == folder.curves.size(), some = visible > 0 && !all;
    bool checked = all;
    ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, some);
    if (ImGui::Checkbox("##visible", &checked))
        for (Curve* c : folder.curves) c->visible = !all;
    ImGui::PopItemFlag();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s  (%zu)", short_folder(*folder.path).c_str(), folder.curves.size());
    ImGui::SetItemTooltip("%s", folder.path->c_str());
    ImGui::PopID();
}

}  // namespace

void draw_file_panel(Session& session) {
    static char typed_filter[128] = "";
    static int anchor = 0;
    static std::vector<std::string> closed_folders;

    if (ImGui::Button("Add files or folders")) {
        if (folders_need_their_own_dialog) ImGui::OpenPopup("add");
        else open_paths(session, ask_for_files());
    }
    ImGui::SetItemTooltip("Add .sNp or .csv files, or folders, or open a session file.\nFiles and folders can also be dropped on the window.");
    if (ImGui::BeginPopup("add")) {
        if (ImGui::MenuItem("Files")) open_paths(session, ask_for_files());
        if (ImGui::MenuItem("Folder")) open_paths(session, ask_for_folder());
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(session.curves.empty());
    if (ImGui::Button(ImGui::GetTime() - session.session_saved_at < 1.5 ? "Saved###save" : "Save session###save")) save_session_as(session);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Save all loaded files and the state of the window in one file.\nThe file opens on another computer without the measurement files. Ctrl+S does the same.");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "Filter by name", typed_filter, sizeof typed_filter);

    std::vector<Folder> folders;
    for (auto& c : session.curves) {
        if (!contains(c->name, typed_filter)) continue;
        auto folder = std::find_if(folders.begin(), folders.end(), [&](const Folder& f) { return *f.path == c->folder; });
        if (folder == folders.end()) {
            bool closed = std::find(closed_folders.begin(), closed_folders.end(), c->folder) != closed_folders.end();
            folder = folders.insert(folders.end(), Folder{&c->folder, {}, !closed});
        }
        folder->curves.push_back(c.get());
    }
    std::vector<Curve*> shown;
    for (Folder& f : folders) shown.insert(shown.end(), f.curves.begin(), f.curves.end());
    int selected = static_cast<int>(std::count_if(shown.begin(), shown.end(), [](Curve* c) { return c->selected; }));

    bool remove = false;
    bool mouse_on_row = false;

    if (!session.load_errors.empty()) {
        const size_t most_shown = 3;
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.15f, 0.1f, 1.0f));
        ImGui::TextWrapped("Not done:");
        for (size_t i = 0; i < std::min(most_shown, session.load_errors.size()); ++i) ImGui::TextWrapped("%s", session.load_errors[i].c_str());
        if (session.load_errors.size() > most_shown) ImGui::Text("and %zu more", session.load_errors.size() - most_shown);
        ImGui::PopStyleColor();
        if (ImGui::SmallButton("Clear")) session.load_errors.clear();
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) remove = selected > 0;
        if (ImGui::IsKeyPressed(ImGuiKey_Space) && selected) {
            bool any_hidden = std::any_of(shown.begin(), shown.end(), [](Curve* c) { return c->selected && !c->visible; });
            for (Curve* c : shown)
                if (c->selected) c->visible = any_hidden;
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A))
            for (Curve* c : shown) c->selected = true;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            for (auto& c : session.curves) c->selected = false;
    }

    ImGui::Separator();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 2.0f));
    ImGui::BeginChild("file list", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoSavedSettings);
    if (session.curves.empty()) ImGui::TextDisabled("\nNo files.\nDrop .sNp or .csv files, or folders,\non the window.");

    std::vector<Row> rows;
    for (Folder& f : folders) {
        rows.push_back({&f, nullptr});
        if (f.open)
            for (Curve* c : f.curves) rows.push_back({&f, c});
    }
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()), ImGui::GetFrameHeightWithSpacing());
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const Row& row = rows[static_cast<size_t>(i)];
            if (!row.curve) {
                draw_folder_row(*row.folder, closed_folders);
                continue;
            }
            Curve* c = row.curve;
            ImGui::PushID(c->id);
            ImGui::Indent(22.0f * ui_scale());
            if (ImGui::Checkbox("##visible", &c->visible) && c->selected)
                for (Curve* other : shown)
                    if (other->selected) other->visible = c->visible;
            mouse_on_row |= ImGui::IsItemHovered();
            ImGui::SameLine();
            ImVec2 at = ImGui::GetCursorScreenPos();
            float height = ImGui::GetFrameHeight();
            ImVec4 line_color = c->color;
            if (!c->visible) line_color.w = 0.30f;
            draw_line_sample(ImGui::GetWindowDrawList(), ImVec2(at.x, at.y + height * 0.5f), ImVec2(at.x + 18.0f * ui_scale(), at.y + height * 0.5f),
                             ImGui::ColorConvertFloat4ToU32(line_color), std::clamp(c->width, 1.5f, 4.0f), c->style);
            ImGui::SetCursorScreenPos(ImVec2(at.x + 24.0f * ui_scale(), at.y));
            if (!c->visible) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            bool clicked = ImGui::Selectable(c->name.c_str(), c->selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, height));
            if (!c->visible) ImGui::PopStyleColor();
            mouse_on_row |= ImGui::IsItemHovered();
            if (ImGui::IsItemHovered() && c->visible) session.highlighted_curve_next = c->id;
            if (ImGui::BeginPopupContextItem("row menu")) {
                if (!c->selected) {
                    for (auto& other : session.curves) other->selected = false;
                    c->selected = true;
                    anchor = c->id;
                }
                if (ImGui::MenuItem("Edit label, color, line")) {
                    session.edited_curve = c->id;
                    session.open_editor = true;
                }
                if (ImGui::MenuItem("Show"))
                    for (auto& other : session.curves)
                        if (other->selected) other->visible = true;
                if (ImGui::MenuItem("Hide"))
                    for (auto& other : session.curves)
                        if (other->selected) other->visible = false;
                if (ImGui::MenuItem("Hide all other files"))
                    for (auto& other : session.curves) other->visible = other->selected;
                ImGui::Separator();
                if (ImGui::MenuItem("Remove", "Delete")) remove = true;
                ImGui::EndPopup();
            }
            if (clicked) {
                const ImGuiIO& io = ImGui::GetIO();
                auto anchor_at = std::find_if(shown.begin(), shown.end(), [&](Curve* s) { return s->id == anchor; });
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    session.edited_curve = c->id;
                    session.open_editor = true;
                } else if (io.KeyShift && anchor_at != shown.end()) {
                    auto clicked_at = std::find(shown.begin(), shown.end(), c);
                    for (auto it = std::min(anchor_at, clicked_at); it <= std::max(anchor_at, clicked_at); ++it) (*it)->selected = true;
                } else if (io.KeyCtrl) {
                    c->selected = !c->selected;
                    anchor = c->id;
                } else {
                    for (auto& other : session.curves) other->selected = false;
                    c->selected = true;
                    anchor = c->id;
                }
            }
            ImGui::Unindent(22.0f * ui_scale());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !mouse_on_row && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        for (auto& c : session.curves) c->selected = false;

    if (remove) remove_selected_curves(session);  // must be last: the lists above point into the session
}
