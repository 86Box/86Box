#ifndef SDL_OSD_EXPLORER_H
#define SDL_OSD_EXPLORER_H

#include <array>
#include <string>
#include <vector>

#include "filesystem.hpp"

namespace fs = emu::filesystem;

enum class OsdExplorerMode {
    File,
    Directory
};

enum class OsdExplorerResultType {
    None,
    Accepted,
    Cancelled
};

struct OsdExplorerConfig {
    const char        *title           = "Select";
    const char        *accept_label    = "Open";
    OsdExplorerMode    mode            = OsdExplorerMode::File;
    const char *const *extension_globs = nullptr;
    const char        *initial_path    = nullptr;
};

struct OsdExplorerResult {
    OsdExplorerResultType type = OsdExplorerResultType::None;
    std::array<char, 1024>   path = {};
};

class OsdExplorer {
public:
    enum class FocusSlot {
        Filename,
        CurrentPath,
        ShowAllFiles,
        EntryList,
        FileList,
        DirectoryList,
        Accept,
        Cancel
    };

    enum class EntryKind {
        Parent,
        Directory,
        File,
        Drive
    };

    struct Entry {
        EntryKind   kind = EntryKind::File;
        std::string name;
        fs::path path;
    };

    void Open(const OsdExplorerConfig &config);
    OsdExplorerResult Draw();

private:
    bool         SetCurrentPath(const fs::path &path);
    void         RefreshEntries();
    void         ResetSelection();
    void         QueueFocusSlot(FocusSlot slot);
    bool         ResolveTypedPath(fs::path *path, bool *is_directory, bool *is_file) const;
    bool         TryHandleFilenameInput(OsdExplorerResult *result);
    bool         TryActivateEntry(const Entry &entry, OsdExplorerResult *result);
    bool         BuildAcceptedPath(fs::path *path) const;
    bool         HasVisibleFilter() const;
    const Entry *SelectedDirectoryEntry() const;
    const Entry *SelectedFileEntry() const;
    const Entry *SelectedVisibleEntry() const;

    OsdExplorerConfig      config_                    = {};
    bool                   is_open_                   = false;
    bool                   show_all_files_            = false;
    bool                   refresh_pending_           = false;
    bool                   focus_filename_input_      = false;
    bool                   focus_current_path_input_  = false;
    bool                   focus_show_all_files_      = false;
    bool                   focus_entry_list_          = false;
    bool                   focus_files_pane_          = false;
    bool                   focus_directories_pane_    = false;
    FocusSlot              focused_slot_              = FocusSlot::EntryList;
    int                    visible_entry_selection_   = -1;
    int                    directory_entry_selection_ = -1;
    int                    file_entry_selection_      = -1;
    fs::path               current_path_;
    std::vector<Entry>     visible_entries_;
    std::vector<Entry>     directory_entries_;
    std::vector<Entry>     file_entries_;
    std::array<char, 1024> filename_input_            = {};
};

#endif