/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Backend-neutral OSD core.
 */
#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <set>
#include <sys/stat.h>
#include <utility>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/video.h>
#include <86box/ui.h>
#include <86box/version.h>
#include <86box/cdrom.h>

#include "osd_core.hpp"

/* ------------------------------------------------------------------ */
/*  Constants                                                          */
/* ------------------------------------------------------------------ */
enum {
    OSD_FONT_SIZE     = 13,
    OSD_FILE_CAPACITY = 512,
    OSD_PATH_CAPACITY = 1024,
    OSD_LOG_LINES     = 64,
    OSD_LOG_LINE_LEN  = 256,
    OSD_LIST_PAGE     = 12   /* rows moved per PageUp/PageDown */
};

static constexpr float OSD_MIN_OUTPUT_SCALE = 1.0f;
static constexpr float OSD_MIN_LAYOUT_SCALE = 1.0f;
static constexpr float OSD_MAX_SCALE        = 6.0f;
static constexpr float OSD_REF_WIDTH        = 768.0f;
static constexpr float OSD_REF_HEIGHT       = 576.0f;

/* ------------------------------------------------------------------ */
/*  State                                                              */
/* ------------------------------------------------------------------ */
enum OsdView {
    VIEW_MENU,
    VIEW_LOG,
    VIEW_FILE_FLOPPY,
    VIEW_FILE_CD,
    VIEW_FILE_RDISK,
    VIEW_FILE_CART,
    VIEW_FILE_MO,
    VIEW_CD_FOLDER
};

static OsdView   current_view   = VIEW_MENU;
static int       menu_sel       = -1;
static int       file_sel       = 0;
static bool      file_list_focus_pending = false;

static char      files[OSD_FILE_CAPACITY][OSD_PATH_CAPACITY];
static int       file_count     = 0;

static char      osd_title[512] = "";
static osd_host_t osd_host       = { nullptr, nullptr };
static float      osd_layout_scale = 1.0f;
static float      osd_font_raster_scale = 1.0f;
static ImGuiStyle osd_style_base;
static bool       osd_style_ready = false;

/* ------------------------------------------------------------------ */
/*  Log ring buffer                                                    */
/* ------------------------------------------------------------------ */
static char        log_ring[OSD_LOG_LINES][OSD_LOG_LINE_LEN];
static int         log_ring_head  = 0;   /* next write slot */
static int         log_ring_count = 0;   /* entries populated */
static std::mutex  log_mutex;
static bool        log_scroll_pending = false;

static void show_main_menu(void);
static bool focused_button(const char *label, bool focused);

static float clamp_layout_scale(float scale)
{
    if (scale < OSD_MIN_LAYOUT_SCALE)
        return OSD_MIN_LAYOUT_SCALE;
    if (scale > OSD_MAX_SCALE)
        return OSD_MAX_SCALE;
    return std::floor(scale + 0.5f);
}

static float clamp_output_scale(float scale)
{
    if (scale < OSD_MIN_OUTPUT_SCALE)
        return OSD_MIN_OUTPUT_SCALE;
    if (scale > OSD_MAX_SCALE)
        return OSD_MAX_SCALE;
    return std::floor(scale + 0.5f);
}

float osd_core_layout_scale_for_output(int output_w, int output_h)
{
    if (output_w <= 0 || output_h <= 0)
        return 1.0f;

    const float x_scale = (float) output_w / OSD_REF_WIDTH;
    const float y_scale = (float) output_h / OSD_REF_HEIGHT;
    return clamp_output_scale(std::min(x_scale, y_scale));
}

static float osd_scaled(float value)
{
    return (value > 0.0f) ? (value * osd_layout_scale) : value;
}

static ImVec2 osd_scaled_size(float width, float height)
{
    return ImVec2(osd_scaled(width), osd_scaled(height));
}

static void apply_layout_scale(void)
{
    if (!osd_style_ready || ImGui::GetCurrentContext() == nullptr)
        return;

    ImGuiStyle &s = ImGui::GetStyle();

    s = osd_style_base;
    s.ScaleAllSizes(osd_layout_scale);
    s.FontScaleMain = osd_layout_scale / std::max(1.0f, osd_font_raster_scale);
}

void osd_core_set_layout_scale(float scale)
{
    osd_layout_scale = clamp_layout_scale(scale);
    apply_layout_scale();
}

int
osd_core_default_font_size(void)
{
    return OSD_FONT_SIZE;
}

void
osd_core_rebuild_default_font(int pixel_size)
{
    ImGuiIO &io = ImGui::GetIO();
    ImFontConfig cfg;

    if (pixel_size < OSD_FONT_SIZE)
        pixel_size = OSD_FONT_SIZE;

    io.Fonts->Clear();
    cfg.PixelSnapH  = true;
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;
    cfg.SizePixels  = (float) pixel_size;
    io.Fonts->AddFontDefaultBitmap(&cfg);

    osd_font_raster_scale = (float) pixel_size / (float) OSD_FONT_SIZE;
    apply_layout_scale();
}

static void normalize_slashes(char *path)
{
    while (*path != '\0') {
        if (*path == '\\')
            *path = '/';
        path++;
    }
}

static void osd_log_push(const char *line)
{
    std::lock_guard<std::mutex> lock(log_mutex);
    strncpy(log_ring[log_ring_head], line, OSD_LOG_LINE_LEN - 1);
    log_ring[log_ring_head][OSD_LOG_LINE_LEN - 1] = '\0';
    int len = (int)strlen(log_ring[log_ring_head]);
    while (len > 0 && (log_ring[log_ring_head][len - 1] == '\n'
                    || log_ring[log_ring_head][len - 1] == '\r'))
        log_ring[log_ring_head][--len] = '\0';
    log_ring_head = (log_ring_head + 1) % OSD_LOG_LINES;
    if (log_ring_count < OSD_LOG_LINES)
        log_ring_count++;
}

extern "C" void osd_core_log_push_c(const char *line)
{
    osd_log_push(line);
}

/* ------------------------------------------------------------------ */
/*  File extensions per media type                                     */
/* ------------------------------------------------------------------ */
static const char *const floppy_exts[] = {
    ".0??", ".1??", ".??0",
    ".86f", ".cq?", ".ddi", ".dsk",
    ".fdf", ".fdi", ".flp", ".hdm", ".im?", ".json", ".mfm", ".td0", ".vfd", ".xdf",
    nullptr
};

/* .ccd/.nrg/.mdf not supported by backend; .mdx is encrypted MDS. */
static const char *const cd_exts[] = {
    ".iso", ".cue", ".mds", ".mdx", nullptr
};

static const char *const rdisk_exts[] = {
    ".im?", ".rdi", ".sdi", ".zdi", nullptr
};

static const char *const cart_exts[] = {
    ".a", ".b", ".jrc", nullptr
};

static const char *const mo_exts[] = {
    ".im?", ".mdi", nullptr
};

/* ------------------------------------------------------------------ */
/*  File browser helpers                                               */
/* ------------------------------------------------------------------ */

/* Case-insensitive glob: '?' = one char, '*' = any chars. */
static bool glob_match_ci(const char *pat, const char *str)
{
    while (*pat && *str) {
        if (*pat == '*') {
            while (*pat == '*') pat++;
            if (!*pat) return true;
            while (*str)
                if (glob_match_ci(pat, str++)) return true;
            return false;
        }
        if (*pat != '?' && tolower((unsigned char)*pat) != tolower((unsigned char)*str))
            return false;
        pat++; str++;
    }
    while (*pat == '*') pat++;
    return !*pat && !*str;
}

/* Match file's extension (including leading '.') against dotted glob patterns. */
static bool has_ext(const char *name, const char *const *pats)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    for (int i = 0; pats[i]; i++)
        if (glob_match_ci(pats[i], dot))
            return true;
    return false;
}

static const char *const *exts_for_view(OsdView v)
{
    switch (v) {
        case VIEW_FILE_FLOPPY: return floppy_exts;
        case VIEW_FILE_CD:     return cd_exts;
        case VIEW_FILE_RDISK:  return rdisk_exts;
        case VIEW_FILE_CART:   return cart_exts;
        case VIEW_FILE_MO:     return mo_exts;
        default:               return nullptr;
    }
}

using DirSet = std::set<std::pair<dev_t, ino_t>>;

static void scan_dir_recursive(char path[], size_t path_len, const char *const *exts, DirSet &visited)
{
    struct stat dst;
    if (stat(path, &dst) != 0 || !S_ISDIR(dst.st_mode))
        return;
    auto key = std::make_pair(dst.st_dev, dst.st_ino);
    if (!visited.insert(key).second)
        return; /* already visited (symlink / bind-mount) */

    DIR *dir = opendir(path);
    if (!dir)
        return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        const char *name = entry->d_name;
        if (file_count < OSD_FILE_CAPACITY && name[0] != '.') {
            int added = snprintf(path + path_len, OSD_PATH_CAPACITY - path_len,
                                 "/%s", name);
            if (added > 0 && added < (int)(OSD_PATH_CAPACITY - path_len)) {
                struct stat st;
                if (stat(path, &st) == 0) {
                    if (S_ISDIR(st.st_mode))
                        scan_dir_recursive(path, path_len + (size_t)added, exts, visited);
                    else if (S_ISREG(st.st_mode) && has_ext(name, exts))
                        if (snprintf(files[file_count], OSD_PATH_CAPACITY, "%s", path) < OSD_PATH_CAPACITY)
                            file_count++;
                }
            }
            path[path_len] = '\0';
        }
    }

    closedir(dir);
}

static void load_files(OsdView view)
{
    file_count = 0;
    file_sel   = 0;
    file_list_focus_pending = true;
    memset(files, 0, sizeof(files));
    const char *const *exts = exts_for_view(view);
    if (!exts)
        return;

    char path[OSD_PATH_CAPACITY];
    int len;
    DirSet visited; /* (dev,ino) dedup — handles symlinks and bind-mounts */

    len = snprintf(path, sizeof(path), ".");
    scan_dir_recursive(path, (size_t)len, exts, visited);
    len = snprintf(path, sizeof(path), "/mnt");
    scan_dir_recursive(path, (size_t)len, exts, visited);
    len = snprintf(path, sizeof(path), "/media");
    scan_dir_recursive(path, (size_t)len, exts, visited);
}

/* ------------------------------------------------------------------ */
/*  State: CD folder browser (VISO)                                   */
/* ------------------------------------------------------------------ */
static char  cd_folder_path[OSD_PATH_CAPACITY];
static char  cd_folder_entries[OSD_FILE_CAPACITY][256];
static int   cd_folder_count   = 0;
static int   cd_folder_sel     = 0;
static bool  cd_folder_pending = false;

static void load_cd_folders(void)
{
    cd_folder_count = 0;
    DIR *dir = opendir(cd_folder_path);
    if (!dir)
        return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        const char *name = entry->d_name;
        if (name[0] != '.') {
            char tmp[OSD_PATH_CAPACITY];
            snprintf(tmp, sizeof(tmp), "%s/%s", cd_folder_path, name);
            struct stat st;
            if (stat(tmp, &st) == 0 && S_ISDIR(st.st_mode) && cd_folder_count < OSD_FILE_CAPACITY) {
                snprintf(cd_folder_entries[cd_folder_count], 256, "%s", name);
                cd_folder_count++;
            }
        }
    }

    closedir(dir);
}

static void cd_folder_go_up(void)
{
    char *slash = strrchr(cd_folder_path, '/');
    if (!slash) return;
    if (slash == cd_folder_path)
        slash[1] = '\0'; /* /foo → / */
    else
        *slash = '\0';   /* /a/b → /a */
    cd_folder_sel = 0;
    load_cd_folders();
}

static void cd_folder_enter(int idx)
{
    size_t len = strlen(cd_folder_path);
    if (len > 1 && cd_folder_path[len - 1] == '/')
        cd_folder_path[--len] = '\0';
    if (len + 1 + strlen(cd_folder_entries[idx]) >= OSD_PATH_CAPACITY)
        return;
    snprintf(cd_folder_path + len, OSD_PATH_CAPACITY - len, "/%s", cd_folder_entries[idx]);
    cd_folder_sel = 0;
    load_cd_folders();
}

static void open_cd_folder_browser(void)
{
    if (!plat_getcwd(cd_folder_path, sizeof(cd_folder_path)))
        snprintf(cd_folder_path, sizeof(cd_folder_path), ".");
    normalize_slashes(cd_folder_path);
    cd_folder_pending = true;
    load_cd_folders();
}

/* ------------------------------------------------------------------ */
/*  Mount helpers                                                      */
/* ------------------------------------------------------------------ */
static void mount_selected(void)
{
    char *f = files[file_sel];

    /* Disable log suppression around mount to capture all output. */
    char msg[OSD_PATH_CAPACITY + 32];
    snprintf(msg, sizeof(msg), "Loading: %s", f);
    osd_log_push(msg);
    pclog_toggle_suppr();
    switch (current_view) {
        case VIEW_FILE_FLOPPY: floppy_mount(0, f, 0);    break;
        case VIEW_FILE_CD:     cdrom_mount(0, f);         break;
        case VIEW_FILE_RDISK:  rdisk_mount(0, f, 0);      break;
        case VIEW_FILE_CART:   cartridge_mount(0, f, 0);  break;
        case VIEW_FILE_MO:     mo_mount(0, f, 0);         break;
        default: pclog_toggle_suppr(); return;
    }
    pclog_toggle_suppr();
}

/* ------------------------------------------------------------------ */
/*  OSD theme: retro / CRT-inspired                                   */
/* ------------------------------------------------------------------ */
void osd_core_setup_style(void)
{
    osd_style_base      = ImGuiStyle();
    ImGuiStyle &s       = osd_style_base;
    s.WindowRounding    = 2.0f;
    s.FrameRounding     = 1.0f;
    s.ScrollbarRounding = 1.0f;
    s.GrabRounding      = 1.0f;
    s.WindowBorderSize  = 1.0f;
    s.FrameBorderSize   = 1.0f;
    s.ItemSpacing       = ImVec2(6, 4);
    s.WindowPadding     = ImVec2(8, 8);
    s.FramePadding      = ImVec2(4, 2);
    s.ScrollbarSize     = 10.0f;

    ImVec4 *c = s.Colors;
    c[ImGuiCol_WindowBg]             = ImVec4(0.04f, 0.05f, 0.10f, 0.92f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.04f, 0.05f, 0.10f, 0.92f);
    c[ImGuiCol_Border]               = ImVec4(0.20f, 0.60f, 0.20f, 0.60f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.06f, 0.15f, 0.06f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.10f, 0.30f, 0.10f, 1.00f);
    c[ImGuiCol_Header]               = ImVec4(0.10f, 0.30f, 0.10f, 0.80f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.15f, 0.50f, 0.15f, 0.80f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.20f, 0.60f, 0.20f, 1.00f);
    c[ImGuiCol_Button]               = ImVec4(0.10f, 0.25f, 0.10f, 0.80f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.15f, 0.45f, 0.15f, 1.00f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.20f, 0.60f, 0.20f, 1.00f);
    c[ImGuiCol_FrameBg]              = ImVec4(0.08f, 0.12f, 0.08f, 0.80f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.12f, 0.20f, 0.12f, 0.80f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.15f, 0.30f, 0.15f, 1.00f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.04f, 0.06f, 0.04f, 0.80f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.15f, 0.40f, 0.15f, 0.80f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.20f, 0.55f, 0.20f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.25f, 0.65f, 0.25f, 1.00f);
    c[ImGuiCol_Text]                 = ImVec4(0.40f, 1.00f, 0.40f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.25f, 0.50f, 0.25f, 1.00f);
    c[ImGuiCol_Separator]            = ImVec4(0.15f, 0.40f, 0.15f, 0.60f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.05f, 0.08f, 0.05f, 0.95f);
    c[ImGuiCol_NavHighlight]         = ImVec4(0.30f, 0.80f, 0.30f, 1.00f);

    osd_style_ready = true;
    apply_layout_scale();
}

/* ------------------------------------------------------------------ */
/*  Menu items definition                                              */
/* ------------------------------------------------------------------ */
enum OsdAction {
    ACT_NONE,
    ACT_EJECT_FLOPPY,
    ACT_EJECT_CD,
    ACT_EJECT_RDISK,
    ACT_EJECT_CART,
    ACT_EJECT_MO,
    ACT_HARDRESET,
    ACT_FULLSCREEN,
    ACT_EXIT,
    ACT_CLOSE_OSD
};

struct MenuItem {
    const char *label;
    OsdAction   action;     /* ACT_NONE → open a view */
    OsdView     view;       /* used when action == ACT_NONE */
};

static const MenuItem menu_items[] = {
    { "Load Floppy Image...",      ACT_NONE,         VIEW_FILE_FLOPPY },
    { "Load CD-ROM Image...",      ACT_NONE,         VIEW_FILE_CD     },
    { "Mount CD Folder (VISO)...", ACT_NONE,         VIEW_CD_FOLDER   },
    { "Load Removable Disk...",    ACT_NONE,         VIEW_FILE_RDISK  },
    { "Load Cartridge...",         ACT_NONE,         VIEW_FILE_CART   },
    { "Load MO Image...",          ACT_NONE,         VIEW_FILE_MO     },
    { nullptr, ACT_NONE, VIEW_MENU }, /* separator */
    { "Eject Floppy",              ACT_EJECT_FLOPPY, VIEW_MENU        },
    { "Eject CD-ROM",              ACT_EJECT_CD,     VIEW_MENU        },
    { "Eject Removable Disk",      ACT_EJECT_RDISK,  VIEW_MENU        },
    { "Eject Cartridge",           ACT_EJECT_CART,   VIEW_MENU        },
    { "Eject MO",                  ACT_EJECT_MO,     VIEW_MENU        },
    { nullptr, ACT_NONE, VIEW_MENU }, /* separator */
    { "Show Log",                  ACT_NONE,         VIEW_LOG         },
    { nullptr, ACT_NONE, VIEW_MENU }, /* separator */
    { "Hard Reset",                ACT_HARDRESET,    VIEW_MENU        },
    { "Toggle Fullscreen",         ACT_FULLSCREEN,   VIEW_MENU        },
    { "Exit 86Box",                ACT_EXIT,         VIEW_MENU        },
    { nullptr, ACT_NONE, VIEW_MENU }, /* separator */
    { "Close OSD",                 ACT_CLOSE_OSD,    VIEW_MENU        },
};
static constexpr int MENU_COUNT = sizeof(menu_items) / sizeof(menu_items[0]);

static int menu_find_selectable(int start, int step)
{
    for (int i = start; i >= 0 && i < MENU_COUNT; i += step)
        if (menu_items[i].label)
            return i;
    return -1;
}

static int menu_first_selectable(void)
{
    return menu_find_selectable(0, 1);
}

static int menu_last_selectable(void)
{
    return menu_find_selectable(MENU_COUNT - 1, -1);
}

static void menu_normalize_selection(void)
{
    if (menu_sel < 0 || menu_sel >= MENU_COUNT || !menu_items[menu_sel].label)
        menu_sel = menu_first_selectable();
}

static void show_main_menu(void)
{
    current_view = VIEW_MENU;
    menu_normalize_selection();
}

static void activate_menu_item(int idx, bool *close_osd)
{
    const MenuItem &mi = menu_items[idx];

    switch (mi.action) {
        case ACT_EJECT_FLOPPY: floppy_eject(0);    return;
        case ACT_EJECT_CD:     cdrom_eject(0);     return;
        case ACT_EJECT_RDISK:  rdisk_eject(0);     return;
        case ACT_EJECT_CART:   cartridge_eject(0); return;
        case ACT_EJECT_MO:     mo_eject(0);        return;
        case ACT_HARDRESET:    pc_reset_hard();    *close_osd = true; return;
        case ACT_FULLSCREEN:
            if (osd_host.toggle_fullscreen)
                osd_host.toggle_fullscreen();
            *close_osd = true;
            return;
        case ACT_EXIT:
            if (osd_host.request_exit)
                osd_host.request_exit();
            *close_osd = true;
            return;
        case ACT_CLOSE_OSD:    *close_osd = true;  return;
        case ACT_NONE:         break;
    }

    if (mi.view == VIEW_LOG)
        log_scroll_pending = true;
    else if (mi.view == VIEW_CD_FOLDER)
        open_cd_folder_browser();
    else
        load_files(mi.view);
    current_view = mi.view;
}

/* ------------------------------------------------------------------ */
/*  Draw: Main menu                                                    */
/* ------------------------------------------------------------------ */
static bool draw_menu(void)
{
    const bool up    = ImGui::IsKeyPressed(ImGuiKey_UpArrow,     true);
    const bool down  = ImGui::IsKeyPressed(ImGuiKey_DownArrow,   true);
    const bool home  = ImGui::IsKeyPressed(ImGuiKey_Home,        true);
    const bool end   = ImGui::IsKeyPressed(ImGuiKey_End,         true);
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter,       false)
                    || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);

    menu_normalize_selection();

    bool close_osd = false;
    if (up) {
        const int prev = menu_find_selectable(menu_sel - 1, -1);
        menu_sel = (prev >= 0) ? prev : menu_last_selectable();
    }
    if (down) {
        const int next = menu_find_selectable(menu_sel + 1, 1);
        menu_sel = (next >= 0) ? next : menu_first_selectable();
    }
    if (home)
        menu_sel = menu_first_selectable();
    if (end)
        menu_sel = menu_last_selectable();
    if (enter && menu_sel >= 0)
        activate_menu_item(menu_sel, &close_osd);

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(osd_scaled_size(320.0f, 0.0f), ImGuiCond_Always);

    ImGui::Begin("86Box OSD", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoNav);

    /* Show window title (machine info) */
    if (osd_title[0]) {
        ImGui::TextDisabled("%s", osd_title);
        ImGui::Separator();
    }

    for (int i = 0; i < MENU_COUNT; i++) {
        const MenuItem &mi = menu_items[i];
        if (!mi.label) {
            ImGui::Separator();
            continue;
        }

        const bool selected = (i == menu_sel);
        if (ImGui::Selectable(mi.label, selected)) {
            menu_sel = i;
            activate_menu_item(i, &close_osd);
        }
    }

    ImGui::End();
    return !close_osd;
}

/* ------------------------------------------------------------------ */
/*  Draw: Log viewer                                                   */
/* ------------------------------------------------------------------ */
/* Next focus slot on Tab. 0=list, 1=Mount (if files), 2=Back. */
static int tab_cycle(int slot, bool shift, bool has_mount)
{
    if (!shift) {
        if (slot == 0) return has_mount ? 1 : 2;
        if (slot == 1) return 2;
        return 0;
    } else {
        if (slot == 0) return 2;
        if (slot == 1) return 0;
        return has_mount ? 1 : 0;
    }
}

/* Button with keyboard-focus highlight; Enter handled by caller. */
static bool focused_button(const char *label, bool focused)
{
    if (focused)
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
    bool clicked = ImGui::Button(label);
    if (focused)
        ImGui::PopStyleColor();
    return clicked;
}

/* Up/Down/PageUp/PageDown/Home/End navigation for a vertical list. Updates sel
 * in place and returns true when it moved. swallowed_up suppresses one Up that
 * was already consumed handing focus back from a button. */
static bool list_key_nav(int &sel, int total, bool swallowed_up)
{
    if (total <= 0)
        return false;

    const int prev = sel;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow,   true) && sel > 0 && !swallowed_up)
        sel--;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true) && sel < total - 1)
        sel++;
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp,    true))
        sel = std::max(0, sel - OSD_LIST_PAGE);
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown,  true))
        sel = std::min(total - 1, sel + OSD_LIST_PAGE);
    if (ImGui::IsKeyPressed(ImGuiKey_Home,      true))
        sel = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_End,       true))
        sel = total - 1;
    return sel != prev;
}

static bool draw_log(void)
{
    /* Slots: 0=log area, 1=Back. Tab cycles; ESC → osd_handle. */
    static int  log_focused_slot = 1;
    static bool log_focus_area   = false;

    const bool tab     = ImGui::IsKeyPressed(ImGuiKey_Tab,         false);
    const bool up      = ImGui::IsKeyPressed(ImGuiKey_UpArrow,     true);
    const bool enter   = ImGui::IsKeyPressed(ImGuiKey_Enter,       false)
                      || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);

    if (log_scroll_pending) {
        log_focused_slot = 1;
        log_focus_area   = false;
    }

    if (tab) {
        log_focused_slot = (log_focused_slot == 0) ? 1 : 0;
        if (log_focused_slot == 0) log_focus_area = true;
    }
    bool swallowed_up = false;
    if (log_focused_slot > 0 && up) {
        log_focused_slot = 0;
        log_focus_area   = true;
        swallowed_up     = true;
    }
    if (enter && log_focused_slot == 1)
        show_main_menu();

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(osd_scaled_size(560.0f, 340.0f), ImGuiCond_Always);
    ImGui::Begin("Log", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoNav);

    if (log_focus_area) { ImGui::SetNextWindowFocus(); log_focus_area = false; }
    ImGui::BeginChild("##loglines", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()),
                      true, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNav);

    {
        std::lock_guard<std::mutex> lock(log_mutex);
        const int total  = log_ring_count;
        const int oldest = (log_ring_count < OSD_LOG_LINES) ? 0 : log_ring_head;
        for (int i = 0; i < total; i++) {
            int idx = (oldest + i) % OSD_LOG_LINES;
            if (log_ring[idx][0])
                ImGui::TextUnformatted(log_ring[idx]);
        }
    }

    if (log_focused_slot == 0 && !swallowed_up) {
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp,   true))
            ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetWindowHeight() * 0.8f);
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown, true))
            ImGui::SetScrollY(ImGui::GetScrollY() + ImGui::GetWindowHeight() * 0.8f);
        if (ImGui::IsKeyPressed(ImGuiKey_Home,     true))
            ImGui::SetScrollY(0.0f);
        if (ImGui::IsKeyPressed(ImGuiKey_End,      true))
            ImGui::SetScrollHereY(1.0f);
    }

    if (log_scroll_pending) { ImGui::SetScrollHereY(1.0f); log_scroll_pending = false; }

    ImGui::EndChild();

    if (focused_button("Back", log_focused_slot == 1))
        show_main_menu();
    if (ImGui::IsItemClicked()) log_focused_slot = 1;

    ImGui::End();
    return true;
}

/* ------------------------------------------------------------------ */
/*  Draw: CD folder browser (VISO)                                     */
/* ------------------------------------------------------------------ */
static bool draw_folder_browser(void)
{
    static int  focused_slot  = 0;
    static bool focus_list    = false;
    static bool scroll_to_sel = false;

    const ImGuiIO &io = ImGui::GetIO();
    const bool tab   = ImGui::IsKeyPressed(ImGuiKey_Tab,         false);
    const bool up    = ImGui::IsKeyPressed(ImGuiKey_UpArrow,     true);
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter,       false)
                    || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);

    if (cd_folder_pending) {
        focused_slot  = 0;
        focus_list    = true;
        scroll_to_sel = false;
        cd_folder_pending = false;
    }

    if (tab) {
        focused_slot = tab_cycle(focused_slot, io.KeyShift, true);
        if (focused_slot == 0) focus_list = true;
    }

    bool swallowed_up = false;
    if (focused_slot > 0 && up) {
        focused_slot = 0;
        focus_list   = true;
        swallowed_up = true;
    }

    const bool has_parent = (strcmp(cd_folder_path, "/") != 0 && cd_folder_path[0] != '\0');
    const int  total      = has_parent ? cd_folder_count + 1 : cd_folder_count;

    auto do_mount = [&]() {
        char msg[OSD_PATH_CAPACITY + 32];
        snprintf(msg, sizeof(msg), "Loading: %s", cd_folder_path);
        osd_log_push(msg);
        pclog_toggle_suppr();
        cdrom_mount(0, cd_folder_path);
        pclog_toggle_suppr();
        current_view       = VIEW_LOG;
        log_scroll_pending = true;
    };

    if (enter && focused_slot == 1) do_mount();
    if (enter && focused_slot == 2)
        show_main_menu();

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(osd_scaled_size(520.0f, 320.0f), ImGuiCond_Always);
    ImGui::Begin("Mount Folder as CD-ROM", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoNav);

    ImGui::TextDisabled("%s", cd_folder_path);
    ImGui::Separator();

    if (focus_list) { ImGui::SetNextWindowFocus(); focus_list = false; }

    ImGui::BeginChild("##folders",
                      ImVec2(0, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing()),
                      true, ImGuiWindowFlags_NoNav);

    if (total == 0) {
        ImGui::TextDisabled("No subdirectories.");
    } else {
        int hovered_idx = -1;
        for (int i = 0; i < total; i++) {
            const bool is_dotdot = has_parent && i == 0;
            const char *disp     = is_dotdot ? ".." : cd_folder_entries[i - (has_parent ? 1 : 0)];
            const bool sel       = (i == cd_folder_sel);
            ImGui::PushID(i);
            if (is_dotdot)
                ImGui::PushStyleColor(ImGuiCol_Text,
                    ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Selectable(disp, sel, ImGuiSelectableFlags_AllowDoubleClick)) {
                cd_folder_sel = i;
                focused_slot  = 0;
                if (ImGui::IsMouseDoubleClicked(0)) {
                    if (is_dotdot) cd_folder_go_up();
                    else           cd_folder_enter(i - (has_parent ? 1 : 0));
                }
            }
            if (is_dotdot)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) hovered_idx = i;
            if (sel) {
                if (scroll_to_sel) { ImGui::SetScrollHereY(0.5f); scroll_to_sel = false; }
                ImGui::GetWindowDrawList()->AddRect(
                    ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                    ImGui::GetColorU32(ImGuiCol_NavHighlight), 0.0f, 0, 1.5f);
            }
            ImGui::PopID();
        }

        if (io.MouseWheel != 0.0f && hovered_idx >= 0)
            cd_folder_sel = hovered_idx;

        if (focused_slot == 0) {
            if (list_key_nav(cd_folder_sel, total, swallowed_up))
                scroll_to_sel = true;
            if (enter) {
                const bool is_dotdot = has_parent && cd_folder_sel == 0;
                if (is_dotdot)      cd_folder_go_up();
                else if (total > 0) cd_folder_enter(cd_folder_sel - (has_parent ? 1 : 0));
            }
        }
    }

    ImGui::EndChild();

    if (focused_button("Mount", focused_slot == 1)) do_mount();
    if (ImGui::IsItemClicked()) focused_slot = 1;
    ImGui::SameLine();
    if (focused_button("Back", focused_slot == 2))
        show_main_menu();
    if (ImGui::IsItemClicked()) focused_slot = 2;

    ImGui::End();
    return true;
}

/* ------------------------------------------------------------------ */
/*  Draw: File selector                                                */
/* ------------------------------------------------------------------ */
static const char *view_title(OsdView v)
{
    switch (v) {
        case VIEW_FILE_FLOPPY: return "Select Floppy Image";
        case VIEW_FILE_CD:     return "Select CD-ROM Image";
        case VIEW_FILE_RDISK:  return "Select Removable Disk Image";
        case VIEW_FILE_CART:   return "Select Cartridge";
        case VIEW_FILE_MO:     return "Select MO Image";
        default:               return "Select File";
    }
}

static bool draw_file_selector(void)
{
    /* NoNav on both parent+child avoids ghost Tab stops.
       focused_button fakes visual focus; Enter handled before Begin(). */
    static int  focused_slot  = 0;   /* 0 = list, 1 = Mount, 2 = Back */
    static bool focus_list    = false;
    static bool scroll_to_sel = false;

    const ImGuiIO &io = ImGui::GetIO();

    /* Snapshot keys before Begin(). */
    const bool tab   = ImGui::IsKeyPressed(ImGuiKey_Tab,         false);
    const bool up    = ImGui::IsKeyPressed(ImGuiKey_UpArrow,     true);
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter,       false)
                    || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);

    if (file_list_focus_pending) {
        focused_slot  = 0;
        focus_list    = true;
        scroll_to_sel = false;
        file_list_focus_pending = false;
    }

    if (tab) {
        focused_slot = tab_cycle(focused_slot, io.KeyShift, file_count > 0);
        if (focused_slot == 0) focus_list = true;
    }

    /* Up from button: return focus to list; swallowed_up avoids double-move. */
    bool swallowed_up = false;
    if (focused_slot > 0 && up) {
        focused_slot = 0;
        focus_list   = true;
        swallowed_up = true;
    }

    if (enter && focused_slot == 1 && file_count > 0) {
        mount_selected();
        current_view = VIEW_LOG;
        log_scroll_pending = true;
    }
    if (enter && focused_slot == 2)
        show_main_menu();

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(osd_scaled_size(520.0f, 320.0f), ImGuiCond_Always);
    ImGui::Begin(view_title(current_view), nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoNav);

    if (file_count == 0) {
        ImGui::TextDisabled("No files found.");
    } else {
        if (focus_list) { ImGui::SetNextWindowFocus(); focus_list = false; }

        ImGui::BeginChild("##filelist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()),
                          true, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNav);

        int hovered_idx = -1;
        for (int i = 0; i < file_count; i++) {
            const bool sel = (i == file_sel);
            ImGui::PushID(i);
            if (ImGui::Selectable(files[i], sel, ImGuiSelectableFlags_AllowDoubleClick)) {
                file_sel     = i;
                focused_slot = 0;
                if (ImGui::IsMouseDoubleClicked(0)) {
                    mount_selected();
                    current_view = VIEW_LOG;
                    log_scroll_pending = true;
                }
            }
            if (ImGui::IsItemHovered()) hovered_idx = i;
            if (sel) {
                if (scroll_to_sel) { ImGui::SetScrollHereY(0.5f); scroll_to_sel = false; }
                ImGui::GetWindowDrawList()->AddRect(
                    ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                    ImGui::GetColorU32(ImGuiCol_NavHighlight), 0.0f, 0, 1.5f);
            }
            ImGui::PopID();
        }

        /* Wheel: sync keyboard sel to hovered item (no scroll_to_sel). */
        if (io.MouseWheel != 0.0f && hovered_idx >= 0)
            file_sel = hovered_idx;

        if (focused_slot == 0) {
            if (list_key_nav(file_sel, file_count, swallowed_up))
                scroll_to_sel = true;
            if (enter) { mount_selected(); current_view = VIEW_LOG; log_scroll_pending = true; }
        }

        ImGui::EndChild();
    }

    if (file_count > 0) {
        if (focused_button("Mount", focused_slot == 1)) {
            mount_selected();
            current_view = VIEW_LOG;
            log_scroll_pending = true;
        }
        if (ImGui::IsItemClicked()) focused_slot = 1;
        ImGui::SameLine();
    }
    if (focused_button("Back", focused_slot == 2))
        show_main_menu();
    if (ImGui::IsItemClicked()) focused_slot = 2;

    ImGui::End();
    return true;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */
void osd_core_set_host(const osd_host_t *host)
{
    if (host)
        osd_host = *host;
    else
        osd_host = osd_host_t{ nullptr, nullptr };
}

void osd_core_set_title(const char *title)
{
    if (!title) { osd_title[0] = '\0'; return; }
    strncpy(osd_title, title, sizeof(osd_title) - 1);
    osd_title[sizeof(osd_title) - 1] = '\0';
}

void osd_core_reset_to_menu(void)
{
    show_main_menu();
}

bool osd_core_escape(void)
{
    if (current_view == VIEW_MENU)
        return true; /* close OSD entirely */
    show_main_menu();
    return false;
}

bool osd_core_build_ui(void)
{
    switch (current_view) {
        case VIEW_MENU:      return draw_menu();
        case VIEW_LOG:       return draw_log();
        case VIEW_CD_FOLDER: return draw_folder_browser();
        default:             return draw_file_selector();
    }
}

void osd_core_install_log_hook(void)
{
    pclog_hook = osd_core_log_push_c;
}

void osd_core_remove_log_hook(void)
{
    if (pclog_hook == osd_core_log_push_c)
        pclog_hook = nullptr;
}
