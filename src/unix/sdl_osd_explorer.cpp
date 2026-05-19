#include "sdl_osd_explorer.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#include "imgui.h"

namespace {

constexpr size_t kPathCapacity = 1024;

bool glob_match_ci(const char *pattern, const char *text)
{
    while (*pattern && *text) {
        if (*pattern == '*') {
            while (*pattern == '*')
                pattern++;
            if (!*pattern)
                return true;
            while (*text) {
                if (glob_match_ci(pattern, text++))
                    return true;
            }
            return false;
        }
        if (*pattern != '?' && std::tolower(static_cast<unsigned char>(*pattern)) != std::tolower(static_cast<unsigned char>(*text)))
            return false;
        pattern++;
        text++;
    }
    while (*pattern == '*')
        pattern++;
    return !*pattern && !*text;
}

bool matches_filter(const char *name, const char *const *patterns)
{
    if (patterns == nullptr)
        return true;

    const char *dot = strrchr(name, '.');
    if (dot == nullptr)
        return false;

    for (int index = 0; patterns[index] != nullptr; index++) {
        if (glob_match_ci(patterns[index], dot))
            return true;
    }
    return false;
}

void copy_path(std::array<char, 1024> *buffer, const std::filesystem::path &path)
{
    const std::string path_string = path.generic_string();
    snprintf(buffer->data(), buffer->size(), "%s", path_string.c_str());
}

const char *entry_label(const SdlOsdExplorer::Entry &entry)
{
    switch (entry.kind) {
        case SdlOsdExplorer::EntryKind::Parent:
            return "..";
        default:
            return entry.name.c_str();
    }
}

} // namespace

void
SdlOsdExplorer::Open(const SdlOsdExplorerConfig &config)
{
    config_               = config;
    is_open_              = true;
    show_all_files_       = false;
    focus_filename_input_ = false;
    focus_current_path_input_ = false;
    focus_show_all_files_ = false;
    focus_entry_list_     = true;
    focus_files_pane_     = false;
    focus_directories_pane_ = false;
    focused_slot_         = FocusSlot::EntryList;
    ResetSelection();
    filename_input_.fill('\0');

    std::filesystem::path initial_path;
    if (config.initial_path != nullptr && config.initial_path[0] != '\0') {
        initial_path = std::filesystem::path(config.initial_path).lexically_normal();
    }
    else {
        std::error_code error;
        initial_path = std::filesystem::current_path(error).lexically_normal();
    }

    if (!initial_path.empty()) {
        std::error_code error;
        const std::filesystem::file_status status = std::filesystem::status(initial_path, error);
        if (!error && std::filesystem::exists(status) && !std::filesystem::is_directory(status)) {
            if (initial_path == initial_path.parent_path())
                initial_path.clear();
            else
                initial_path = initial_path.parent_path();
        }
    }

    SetCurrentPath(initial_path);
    RefreshEntries();
}

SdlOsdExplorerResult
SdlOsdExplorer::Draw()
{
    SdlOsdExplorerResult result;
    if (!is_open_)
        return result;

#ifdef USE_SDL_OSD_EXPLORER_OLD_STYLE
    constexpr bool kUseSplitPanes = true;
#else
    constexpr bool kUseSplitPanes = false;
#endif

    const bool has_show_all_files_checkbox = config_.mode == SdlOsdExplorerMode::File && HasVisibleFilter();
    auto preferred_list_slot = [&]() {
        if constexpr (!kUseSplitPanes)
            return FocusSlot::EntryList;

        if (config_.mode == SdlOsdExplorerMode::File)
            return file_entries_.empty() ? FocusSlot::DirectoryList : FocusSlot::FileList;
        return FocusSlot::DirectoryList;
    };
    auto slot_is_enabled = [&](FocusSlot slot) {
        switch (slot) {
            case FocusSlot::ShowAllFiles:
                return has_show_all_files_checkbox;
            case FocusSlot::EntryList:
                return !kUseSplitPanes;
            case FocusSlot::FileList:
                return kUseSplitPanes && config_.mode == SdlOsdExplorerMode::File;
            case FocusSlot::DirectoryList:
                return kUseSplitPanes;
            default:
                return true;
        }
    };

    std::vector<FocusSlot> tab_order = {
        FocusSlot::Filename,
        FocusSlot::CurrentPath
    };
    if (has_show_all_files_checkbox)
        tab_order.push_back(FocusSlot::ShowAllFiles);
    if constexpr (kUseSplitPanes) {
        if (config_.mode == SdlOsdExplorerMode::File)
            tab_order.push_back(FocusSlot::FileList);
        tab_order.push_back(FocusSlot::DirectoryList);
    } else {
        tab_order.push_back(FocusSlot::EntryList);
    }
    tab_order.push_back(FocusSlot::Accept);
    tab_order.push_back(FocusSlot::Cancel);

    auto next_slot = [&](FocusSlot slot) -> FocusSlot {
        size_t current_index = 0;
        for (size_t index = 0; index < tab_order.size(); index++) {
            if (tab_order[index] == slot) {
                current_index = index;
                break;
            }
        }

        for (size_t step = 1; step <= tab_order.size(); step++) {
            const FocusSlot candidate = tab_order[(current_index + step) % tab_order.size()];
            if (slot_is_enabled(candidate))
                return candidate;
        }
        return slot;
    };
    auto previous_slot = [&](FocusSlot slot) -> FocusSlot {
        size_t current_index = 0;
        for (size_t index = 0; index < tab_order.size(); index++) {
            if (tab_order[index] == slot) {
                current_index = index;
                break;
            }
        }

        for (size_t step = 1; step <= tab_order.size(); step++) {
            int candidate_index = static_cast<int>(current_index) - static_cast<int>(step);
            while (candidate_index < 0)
                candidate_index += static_cast<int>(tab_order.size());
            const FocusSlot candidate = tab_order[static_cast<size_t>(candidate_index)];
            if (slot_is_enabled(candidate))
                return candidate;
        }
        return slot;
    };

    if (!slot_is_enabled(focused_slot_)) {
        focused_slot_ = preferred_list_slot();
        QueueFocusSlot(focused_slot_);
    }
    if (!has_show_all_files_checkbox)
        focus_show_all_files_ = false;

    if (refresh_pending_)
        RefreshEntries();

    const ImGuiIO &io  = ImGui::GetIO();
    const bool     tab = ImGui::IsKeyPressed(ImGuiKey_Tab, false);
    if (tab) {
        focused_slot_ = io.KeyShift ? previous_slot(focused_slot_) : next_slot(focused_slot_);
        QueueFocusSlot(focused_slot_);
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    ImGui::SetNextWindowSize(ImVec2(560, 380), ImGuiCond_Always);

    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false)
                    || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);

    ImGui::Begin(config_.title, nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove);

    const float label_column_width = 96.0f;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Filename");
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (label_column_width - ImGui::CalcTextSize("Filename").x));
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (focus_filename_input_ && focused_slot_ == FocusSlot::Filename) {
        ImGui::SetKeyboardFocusHere();
        focus_filename_input_ = false;
    }
    if (ImGui::InputText("##filename", filename_input_.data(), filename_input_.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (TryHandleFilenameInput(&result)) {
            ImGui::End();
            return result;
        }
    }
    const bool filename_input_active = ImGui::IsItemActive();
    const bool up                    = ImGui::IsKeyPressed(ImGuiKey_UpArrow, true);

    if (!tab && ImGui::IsItemFocused())
        focused_slot_ = FocusSlot::Filename;

    if (!filename_input_active && up &&
        (focused_slot_ == FocusSlot::Accept || focused_slot_ == FocusSlot::Cancel)) {
        focused_slot_ = preferred_list_slot();
        QueueFocusSlot(focused_slot_);
    }

    std::array<char, 1024> current_path_text = {};
    // Empty path = virtual "Computer" folder with drive root directories
    if (current_path_.empty())
        snprintf(current_path_text.data(), current_path_text.size(), "%s", "Computer");
    else
        snprintf(current_path_text.data(), current_path_text.size(), "%s", current_path_.string().c_str());

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Current path");
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (label_column_width - ImGui::CalcTextSize("Current path").x));
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (focus_current_path_input_ && focused_slot_ == FocusSlot::CurrentPath) {
        ImGui::SetKeyboardFocusHere();
        focus_current_path_input_ = false;
    }
    ImGui::InputText("##current_path", current_path_text.data(), current_path_text.size(), ImGuiInputTextFlags_ReadOnly);
    if (!tab && ImGui::IsItemFocused())
        focused_slot_ = FocusSlot::CurrentPath;

    if (has_show_all_files_checkbox) {
        if (focus_show_all_files_ && focused_slot_ == FocusSlot::ShowAllFiles) {
            ImGui::SetKeyboardFocusHere();
            focus_show_all_files_ = false;
        }

        bool changed = ImGui::Checkbox("Show all files", &show_all_files_);
        if (!tab && ImGui::IsItemFocused())
            focused_slot_ = FocusSlot::ShowAllFiles;

        if (changed)
            RefreshEntries();
    }

    const float button_row_height = ImGui::GetFrameHeightWithSpacing();

    if constexpr (kUseSplitPanes) {
        auto activate_entry = [&](const Entry &entry) {
            const bool accepted = TryActivateEntry(entry, &result);
            return accepted || entry.kind != EntryKind::File;
        };

        const float available_width = ImGui::GetContentRegionAvail().x;
        const float spacing         = ImGui::GetStyle().ItemSpacing.x;
        const float pane_width      = (available_width - spacing) * 0.5f;

        if (config_.mode == SdlOsdExplorerMode::File) {
            ImGui::BeginChild("##files", ImVec2(pane_width, -button_row_height), true);
            if (focus_files_pane_ && focused_slot_ == FocusSlot::FileList && !file_entries_.empty() && file_entry_selection_ < 0)
                file_entry_selection_ = 0;

            for (int index = 0; index < static_cast<int>(file_entries_.size()); index++) {
                const Entry &entry = file_entries_[index];

                if (focus_files_pane_ && focused_slot_ == FocusSlot::FileList && index == file_entry_selection_) {
                    ImGui::SetKeyboardFocusHere();
                    focus_files_pane_ = false;
                }

                if (ImGui::Selectable(entry.name.c_str(), file_entry_selection_ == index,
                                      ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SelectOnNav)) {
                    focused_slot_ = FocusSlot::FileList;
                    file_entry_selection_ = index;
                    snprintf(filename_input_.data(), filename_input_.size(), "%s", entry_label(entry));
                    if (ImGui::IsMouseDoubleClicked(0) && activate_entry(entry)) {
                        ImGui::EndChild();
                        ImGui::End();
                        return result;
                    }
                }
                if (!tab && ImGui::IsItemFocused()) {
                    focused_slot_ = FocusSlot::FileList;
                    file_entry_selection_ = index;
                    snprintf(filename_input_.data(), filename_input_.size(), "%s", entry_label(entry));
                }
            }

            if (file_entries_.empty())
                ImGui::TextDisabled("No files found.");
            ImGui::EndChild();

            ImGui::SameLine();
        }

        ImGui::BeginChild("##directories", ImVec2(config_.mode == SdlOsdExplorerMode::Directory ? 0 : pane_width, -button_row_height), true);
        if (focus_directories_pane_ && focused_slot_ == FocusSlot::DirectoryList && !directory_entries_.empty() && directory_entry_selection_ < 0)
            directory_entry_selection_ = 0;

        for (int index = 0; index < static_cast<int>(directory_entries_.size()); index++) {
            const Entry      &entry = directory_entries_[index];

            if (focus_directories_pane_ && focused_slot_ == FocusSlot::DirectoryList && index == directory_entry_selection_) {
                ImGui::SetKeyboardFocusHere();
                focus_directories_pane_ = false;
            }

            if (ImGui::Selectable(entry.name.c_str(), directory_entry_selection_ == index,
                                  ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SelectOnNav)) {
                focused_slot_              = FocusSlot::DirectoryList;
                directory_entry_selection_ = index;
                if (ImGui::IsMouseDoubleClicked(0) && activate_entry(entry)) {
                    ImGui::EndChild();
                    ImGui::End();
                    return result;
                }
            }
            if (!tab && ImGui::IsItemFocused()) {
                focused_slot_              = FocusSlot::DirectoryList;
                directory_entry_selection_ = index;
            }
        }

        if (directory_entries_.empty())
            ImGui::TextDisabled("No directories found.");
        ImGui::EndChild();

        if (!filename_input_active && enter) {
            if (focused_slot_ == FocusSlot::DirectoryList) {
                if (const Entry *entry = SelectedDirectoryEntry()) {
                    if (activate_entry(*entry)) {
                        ImGui::End();
                        return result;
                    }
                }
            }
            if (focused_slot_ == FocusSlot::FileList) {
                if (const Entry *entry = SelectedFileEntry()) {
                    if (activate_entry(*entry)) {
                        ImGui::End();
                        return result;
                    }
                }
            }
        }
    } else {
        ImGui::BeginChild("##entries", ImVec2(0, -button_row_height), true);
        if (focus_entry_list_ && focused_slot_ == FocusSlot::EntryList && !visible_entries_.empty() && visible_entry_selection_ < 0)
            visible_entry_selection_ = 0;

        for (int index = 0; index < static_cast<int>(visible_entries_.size()); index++) {
            const Entry &entry = visible_entries_[index];

            if (focus_entry_list_ && focused_slot_ == FocusSlot::EntryList && index == visible_entry_selection_) {
                ImGui::SetKeyboardFocusHere();
                focus_entry_list_ = false;
            }

            if (ImGui::Selectable(entry.name.c_str(), visible_entry_selection_ == index,
                                  ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SelectOnNav)) {
                focused_slot_ = FocusSlot::EntryList;
                visible_entry_selection_ = index;
                snprintf(filename_input_.data(), filename_input_.size(), "%s", entry_label(entry));
                if (ImGui::IsMouseDoubleClicked(0)) {
                    const bool accepted = TryActivateEntry(entry, &result);
                    if (accepted || entry.kind != EntryKind::File) {
                        ImGui::EndChild();
                        ImGui::End();
                        return result;
                    }
                }
            }
            if (!tab && ImGui::IsItemFocused())
                focused_slot_ = FocusSlot::EntryList;
        }

        if (!filename_input_active && focused_slot_ == FocusSlot::EntryList && visible_entry_selection_ >= 0 &&
            visible_entry_selection_ < static_cast<int>(visible_entries_.size()) && enter) {
            const Entry &entry = visible_entries_[visible_entry_selection_];
            const bool accepted = TryActivateEntry(entry, &result);
            if (accepted || entry.kind != EntryKind::File) {
                ImGui::EndChild();
                ImGui::End();
                return result;
            }
        }

        if (visible_entries_.empty())
            ImGui::TextDisabled("No entries found.");
        ImGui::EndChild();
    }

    std::filesystem::path accepted_path;
    const bool  can_accept = BuildAcceptedPath(&accepted_path);

    if (!filename_input_active && enter) {
        if (focused_slot_ == FocusSlot::Accept && can_accept) {
            copy_path(&result.path, accepted_path);
            result.type = SdlOsdExplorerResultType::Accepted;
            is_open_    = false;
            ImGui::End();
            return result;
        }
        if (focused_slot_ == FocusSlot::Cancel) {
            result.type = SdlOsdExplorerResultType::Cancelled;
            is_open_    = false;
            ImGui::End();
            return result;
        }
    }

    if (focused_slot_ == FocusSlot::Accept)
        ImGui::SetKeyboardFocusHere();
    ImGui::BeginDisabled(!can_accept);
    if (ImGui::Button(config_.accept_label) && can_accept) {
        focused_slot_ = FocusSlot::Accept;
        copy_path(&result.path, accepted_path);
        result.type = SdlOsdExplorerResultType::Accepted;
        is_open_    = false;
    }
    if (!tab && ImGui::IsItemFocused())
        focused_slot_ = FocusSlot::Accept;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (focused_slot_ == FocusSlot::Cancel)
        ImGui::SetKeyboardFocusHere();
    if (ImGui::Button("Cancel")) {
        focused_slot_ = FocusSlot::Cancel;
        result.type = SdlOsdExplorerResultType::Cancelled;
        is_open_    = false;
    }
    if (!tab && ImGui::IsItemFocused())
        focused_slot_ = FocusSlot::Cancel;

    ImGui::End();
    return result;
}

void
SdlOsdExplorer::QueueFocusSlot(FocusSlot slot)
{
    focus_filename_input_   = slot == FocusSlot::Filename;
    focus_current_path_input_ = slot == FocusSlot::CurrentPath;
    focus_show_all_files_   = slot == FocusSlot::ShowAllFiles;
    focus_entry_list_       = slot == FocusSlot::EntryList;
    focus_files_pane_       = slot == FocusSlot::FileList;
    focus_directories_pane_ = slot == FocusSlot::DirectoryList;
}

bool
SdlOsdExplorer::SetCurrentPath(const std::filesystem::path &path)
{
    if (path.empty()) {
#ifdef _WIN32
        current_path_.clear();
#else
        current_path_ = "/";
#endif
    } else {
        std::error_code error;
        if (!std::filesystem::is_directory(std::filesystem::status(path, error)))
            return false;

        current_path_ = path.lexically_normal();
    }

    refresh_pending_ = true;
#ifdef USE_SDL_OSD_EXPLORER_OLD_STYLE
    focused_slot_ = FocusSlot::DirectoryList;
#else
    focused_slot_ = FocusSlot::EntryList;
#endif
    QueueFocusSlot(focused_slot_);

    ResetSelection();

    filename_input_.fill('\0');

    return true;
}

void
SdlOsdExplorer::RefreshEntries()
{
    visible_entries_.clear();
    directory_entries_.clear();
    file_entries_.clear();

#ifdef _WIN32
#ifndef USE_SDL_OSD_EXPLORER_OLD_STYLE
    // Implement a virtual "Computer" folder on Windows that lists drive roots
    if (current_path_.empty()) {
#endif
        for (char drive = 'A'; drive <= 'Z'; drive++) {
            Entry entry;
            entry.kind = EntryKind::Drive;
            entry.path = std::filesystem::path(std::string(1, drive) + ":\\");

            std::error_code error;
            const std::filesystem::file_status status = std::filesystem::status(entry.path, error);
            if (error || !std::filesystem::exists(status) || !std::filesystem::is_directory(status))
                continue;

#ifdef USE_SDL_OSD_EXPLORER_OLD_STYLE
            entry.name = "[-" + std::string(1, drive) + "-]";
#else
            entry.name = std::string(1, drive) + ":\\";
#endif
            directory_entries_.push_back(entry);
            visible_entries_.push_back(entry);
        }
        refresh_pending_ = false;
#endif
#if defined(_WIN32) && !defined(USE_SDL_OSD_EXPLORER_OLD_STYLE)
        return;
    } else {
#else
    if (current_path_ != current_path_.parent_path()) {
#endif
        Entry parent;

        parent.kind = EntryKind::Parent;
        parent.name = "..";
#if defined(_WIN32) && !defined(USE_SDL_OSD_EXPLORER_OLD_STYLE)
        parent.path = (current_path_ == current_path_.parent_path()) ? std::filesystem::path() : current_path_.parent_path();
#else
        parent.path = current_path_.parent_path();
#endif
        directory_entries_.push_back(parent);
        visible_entries_.push_back(parent);
    }

    std::error_code error;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::directory_iterator iterator(current_path_, options, error);
    for (const std::filesystem::directory_entry &directory_entry : iterator) {
        Entry entry;
        entry.path = directory_entry.path();
        entry.name = entry.path.filename().string();

        const std::filesystem::file_status status = directory_entry.status(error);
        if (error)
            continue;

        if (std::filesystem::is_directory(status)) {
            entry.kind = EntryKind::Directory;
            entry.name += std::filesystem::path::preferred_separator;
            directory_entries_.push_back(entry);
        } else if (config_.mode == SdlOsdExplorerMode::File && (show_all_files_ || matches_filter(entry.name.c_str(), config_.extension_globs))) {
            entry.kind = EntryKind::File;
            file_entries_.push_back(entry);
        }
    }

    auto case_less = [](const Entry &lhs, const Entry &rhs) {
        if (lhs.kind != rhs.kind)
            return lhs.kind < rhs.kind;

        return strcasecmp(lhs.name.c_str(), rhs.name.c_str()) < 0;
    };

    std::sort(directory_entries_.begin(), directory_entries_.end(), case_less);
    std::sort(file_entries_.begin(), file_entries_.end(), case_less);

    for (const Entry &entry : directory_entries_) {
        if (entry.kind != EntryKind::Parent)
            visible_entries_.push_back(entry);
    }
    for (const Entry &entry : file_entries_)
        visible_entries_.push_back(entry);

    refresh_pending_ = false;
}

void
SdlOsdExplorer::ResetSelection()
{
    visible_entry_selection_ = -1;
    directory_entry_selection_ = -1;
    file_entry_selection_ = -1;
}

bool
SdlOsdExplorer::ResolveTypedPath(std::filesystem::path *path, bool *is_directory, bool *is_file) const
{
    *is_directory = false;
    *is_file      = false;

    std::string typed = filename_input_.data();

    const std::filesystem::path typed_path(typed);
    std::filesystem::path       resolved_path = typed_path;
    if (!typed_path.is_absolute())
        resolved_path = current_path_ / typed_path;

    if (!resolved_path.is_absolute())
        return false;

    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::status(resolved_path, error);
    *is_directory = !error && std::filesystem::is_directory(status);
    *is_file      = !error && std::filesystem::exists(status) && !std::filesystem::is_directory(status);

    if (!*is_directory && !*is_file)
        return false;

    *path = resolved_path;
    return true;
}

bool
SdlOsdExplorer::TryHandleFilenameInput(SdlOsdExplorerResult *result)
{
    std::filesystem::path path;
    bool        is_directory = false;
    bool        is_file      = false;

    if (!ResolveTypedPath(&path, &is_directory, &is_file))
        return false;

    if (is_directory) {
        SetCurrentPath(path);
        RefreshEntries();
        return false;
    }

    if (config_.mode == SdlOsdExplorerMode::File && is_file) {
        copy_path(&result->path, path);
        result->type = SdlOsdExplorerResultType::Accepted;
        is_open_     = false;
        return true;
    }

    return false;
}

bool
SdlOsdExplorer::TryActivateEntry(const Entry &entry, SdlOsdExplorerResult *result)
{
    switch (entry.kind) {
        case EntryKind::Parent:
        case EntryKind::Drive:
        case EntryKind::Directory:
            SetCurrentPath(entry.path);
            RefreshEntries();
            return false;

        case EntryKind::File:
            if (config_.mode == SdlOsdExplorerMode::File) {
                copy_path(&result->path, entry.path);
                result->type = SdlOsdExplorerResultType::Accepted;
                is_open_     = false;
                return true;
            }
            return false;
    }

    return false;
}

bool
SdlOsdExplorer::BuildAcceptedPath(std::filesystem::path *path) const
{
    std::filesystem::path resolved_path;
    bool        is_directory = false;
    bool        is_file      = false;
    if (ResolveTypedPath(&resolved_path, &is_directory, &is_file)) {
        if (config_.mode == SdlOsdExplorerMode::Directory && is_directory) {
            *path = resolved_path;
            return true;
        }
        if (config_.mode == SdlOsdExplorerMode::File && is_file) {
            *path = resolved_path;
            return true;
        }
    }

    if (config_.mode == SdlOsdExplorerMode::Directory) {
#ifdef USE_SDL_OSD_EXPLORER_OLD_STYLE
        if (const Entry *entry = SelectedDirectoryEntry()) {
#else
        if (const Entry *entry = SelectedVisibleEntry()) {
#endif
            if (entry->kind == EntryKind::Directory || entry->kind == EntryKind::Drive) {
                *path = entry->path;
                return true;
            }
        }

        if (!current_path_.empty()) {
            *path = current_path_;
            return true;
        }
        return false;
    }

#ifdef USE_SDL_OSD_EXPLORER_OLD_STYLE
    if (const Entry *entry = SelectedFileEntry()) {
#else
    if (const Entry *entry = SelectedVisibleEntry()) {
#endif
        if (entry->kind == EntryKind::File) {
            *path = entry->path;
            return true;
        }
    }

    return false;
}

bool
SdlOsdExplorer::HasVisibleFilter() const
{
    return config_.extension_globs != nullptr;
}

const SdlOsdExplorer::Entry *
SdlOsdExplorer::SelectedDirectoryEntry() const
{
    if (directory_entry_selection_ < 0 || directory_entry_selection_ >= static_cast<int>(directory_entries_.size()))
        return nullptr;
    return &directory_entries_[directory_entry_selection_];
}

const SdlOsdExplorer::Entry *
SdlOsdExplorer::SelectedFileEntry() const
{
    if (file_entry_selection_ < 0 || file_entry_selection_ >= static_cast<int>(file_entries_.size()))
        return nullptr;
    return &file_entries_[file_entry_selection_];
}

const SdlOsdExplorer::Entry *
SdlOsdExplorer::SelectedVisibleEntry() const
{
    if (visible_entry_selection_ < 0 || visible_entry_selection_ >= static_cast<int>(visible_entries_.size()))
        return nullptr;
    return &visible_entries_[visible_entry_selection_];
}