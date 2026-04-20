/* ImGui OSD for 86Box SDL2/GLES2. Renders into FBO so shaders apply. */
#include <GLES2/gl2.h>
#include <SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <dirent.h>
#include <sys/stat.h>
#include <set>
#include <utility>

/* SDL header redefines HAVE_STDARG_H. */
#undef HAVE_STDARG_H
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/video.h>
#include <86box/ui.h>
#include <86box/version.h>
#include <86box/unix_sdl.h>
#include <86box/unix_osd.h>
#include "unix_sdl_shader.h"

/* ------------------------------------------------------------------ */
/*  Extern interface to SDL environment                                */
/* ------------------------------------------------------------------ */
extern "C" {
extern SDL_Window  *sdl_win;
extern wchar_t      sdl_win_title[512];
extern void         unix_executeLine(char *line);
}

/* ------------------------------------------------------------------ */
/*  Constants                                                          */
/* ------------------------------------------------------------------ */
enum {
    OSD_FILE_CAPACITY = 512,
    OSD_PATH_CAPACITY = 1024,
    OSD_LOG_LINES     = 64,
    OSD_LOG_LINE_LEN  = 256
};

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

static bool      imgui_inited   = false;
static bool      osd_visible    = false;
static bool      pending_close  = false;
static OsdView   current_view   = VIEW_MENU;
static int       menu_sel       = -1;
static int       file_sel       = 0;
static bool      file_list_focus_pending = false;
static bool      mouse_was_captured = false;

static char      files[OSD_FILE_CAPACITY][OSD_PATH_CAPACITY];
static int       file_count     = 0;

static char      sdl_win_title_mb[512] = "";

/* ------------------------------------------------------------------ */
/*  Log ring buffer                                                    */
/* ------------------------------------------------------------------ */
static char        log_ring[OSD_LOG_LINES][OSD_LOG_LINE_LEN];
static int         log_ring_head  = 0;   /* next write slot */
static int         log_ring_count = 0;   /* entries populated */
static SDL_mutex  *log_mutex      = nullptr;
static bool        log_scroll_pending = false;

static void show_main_menu(void);
static bool FocusedButton(const char *label, bool focused);

static void osd_log_push(const char *line)
{
    if (!log_mutex)
        return;
    SDL_LockMutex(log_mutex);
    strncpy(log_ring[log_ring_head], line, OSD_LOG_LINE_LEN - 1);
    log_ring[log_ring_head][OSD_LOG_LINE_LEN - 1] = '\0';
    int len = (int)strlen(log_ring[log_ring_head]);
    while (len > 0 && (log_ring[log_ring_head][len - 1] == '\n'
                    || log_ring[log_ring_head][len - 1] == '\r'))
        log_ring[log_ring_head][--len] = '\0';
    log_ring_head = (log_ring_head + 1) % OSD_LOG_LINES;
    if (log_ring_count < OSD_LOG_LINES)
        log_ring_count++;
    SDL_UnlockMutex(log_mutex);
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

    struct dirent **nl;
    int n = scandir(path, &nl, nullptr, alphasort);
    if (n < 0)
        return;

    for (int i = 0; i < n; i++) {
        const char *name = nl[i]->d_name;
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
        free(nl[i]);
    }
    free(nl);
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
    struct dirent **nl;
    int n = scandir(cd_folder_path, &nl, nullptr, alphasort);
    if (n < 0) return;
    for (int i = 0; i < n; i++) {
        const char *name = nl[i]->d_name;
        if (name[0] != '.') {
            char tmp[OSD_PATH_CAPACITY];
            snprintf(tmp, sizeof(tmp), "%s/%s", cd_folder_path, name);
            struct stat st;
            if (stat(tmp, &st) == 0 && S_ISDIR(st.st_mode) && cd_folder_count < OSD_FILE_CAPACITY) {
                snprintf(cd_folder_entries[cd_folder_count], 256, "%s", name);
                cd_folder_count++;
            }
        }
        free(nl[i]);
    }
    free(nl);
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
    char *rp = realpath(".", NULL); /* allocating form; no PATH_MAX constraint */
    snprintf(cd_folder_path, sizeof(cd_folder_path), "%s", rp ? rp : "/");
    free(rp);
    cd_folder_pending = true;
    load_cd_folders();
}

/* ------------------------------------------------------------------ */
/*  Mount / command helpers                                            */
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

static void run_cmd(const char *cmd)
{
    char *buf = strdup(cmd);
    if (buf) {
        unix_executeLine(buf);
        free(buf);
    }
}

/* ------------------------------------------------------------------ */
/*  ImGui theme — retro / CRT-inspired                                */
/* ------------------------------------------------------------------ */
static void setup_retro_style(void)
{
    ImGuiStyle &s       = ImGui::GetStyle();
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
}

/* ------------------------------------------------------------------ */
/*  Public API: init / deinit                                          */
/* ------------------------------------------------------------------ */
void osd_init(void)
{
    if (imgui_inited)
        return;

    SDL_GLContext ctx = sdl_shader_get_context();
    if (!ctx)
        return;

    SDL_GL_MakeCurrent(sdl_win, ctx);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; /* don't save layout */

    setup_retro_style();

    ImGui_ImplSDL2_InitForOpenGL(sdl_win, ctx);
    ImGui_ImplOpenGL3_Init("#version 100");

    log_mutex = SDL_CreateMutex();
    pclog_hook = osd_log_push;

    imgui_inited = true;
}

void osd_deinit(void)
{
    if (!imgui_inited)
        return;

    pclog_hook = nullptr;
    if (log_mutex) { SDL_DestroyMutex(log_mutex); log_mutex = nullptr; }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    imgui_inited = false;
}

/* ------------------------------------------------------------------ */
/*  Public API: open / close                                           */
/* ------------------------------------------------------------------ */
int osd_open(SDL_Event event)
{
    (void)event;
    osd_visible  = true;
    show_main_menu();

    mouse_was_captured = (SDL_GetRelativeMouseMode() == SDL_TRUE);
    if (mouse_was_captured)
        plat_mouse_capture(0);
    SDL_ShowCursor(SDL_TRUE);

    /* Prevent stale ESC from firing on re-open. */
    if (imgui_inited)
        ImGui::GetIO().ClearInputKeys();

    return 1;
}

int osd_close(SDL_Event event)
{
    (void)event;
    osd_visible = false;

    /* Restore mouse capture if it was active before OSD opened. */
    if (mouse_was_captured) {
        plat_mouse_capture(1);
        mouse_was_captured = false;
    }
    SDL_ShowCursor(SDL_FALSE);

    return 1;
}

/* ------------------------------------------------------------------ */
/*  Public API: event handling                                         */
/* ------------------------------------------------------------------ */
int osd_handle(SDL_Event event)
{
    if (!osd_visible || !imgui_inited)
        return 0;

    /* Handle ESC manually — imgui nav would interfere. */
    if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
        && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
        if (event.type == SDL_KEYUP) {
            if (current_view == VIEW_MENU)
                return 0; /* close OSD entirely */
            show_main_menu();
        }
        return 1; /* consume */
    }

    if (pending_close) {
        pending_close = false;
        return 0;
    }

    ImGui_ImplSDL2_ProcessEvent(&event);
    return 1; /* keep open */
}

/* ------------------------------------------------------------------ */
/*  Menu items definition                                              */
/* ------------------------------------------------------------------ */
struct MenuItem {
    const char *label;
    const char *cmd;        /* nullptr → special action */
    OsdView     view;       /* only when cmd==nullptr */
    bool        close_after; /* close OSD after executing cmd */
};

static const MenuItem menu_items[] = {
    { "Load Floppy Image...",      nullptr,        VIEW_FILE_FLOPPY, false },
    { "Load CD-ROM Image...",      nullptr,        VIEW_FILE_CD,     false },
    { "Mount CD Folder (VISO)...", nullptr,        VIEW_CD_FOLDER,   false },
    { "Load Removable Disk...",    nullptr,        VIEW_FILE_RDISK,  false },
    { "Load Cartridge...",         nullptr,        VIEW_FILE_CART,   false },
    { "Load MO Image...",          nullptr,        VIEW_FILE_MO,     false },
    { nullptr, nullptr, VIEW_MENU, false }, /* separator */
    { "Eject Floppy",              "fddeject 0",   VIEW_MENU,        true },
    { "Eject CD-ROM",              "cdeject 0",    VIEW_MENU,        true },
    { "Eject Removable Disk",      "rdiskeject 0", VIEW_MENU,        true },
    { "Eject Cartridge",           "carteject 0",  VIEW_MENU,        true },
    { "Eject MO",                  "moeject 0",    VIEW_MENU,        true },
    { nullptr, nullptr, VIEW_MENU, false }, /* separator */
    { "Show Log",                  nullptr,        VIEW_LOG,         false },
    { nullptr, nullptr, VIEW_MENU, false }, /* separator */
    { "Hard Reset",                "hardreset",    VIEW_MENU,        true },
    { "Toggle Fullscreen",         "fullscreen",   VIEW_MENU,        true },
    { "Exit 86Box",                "exit",         VIEW_MENU,        true },
    { nullptr, nullptr, VIEW_MENU, false }, /* separator */
    { "Close OSD",                 nullptr,        VIEW_MENU,        true },
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

    if (mi.close_after && !mi.cmd && mi.view == VIEW_MENU) {
        *close_osd = true;
        return;
    }

    if (mi.cmd) {
        run_cmd(mi.cmd);
        if (mi.close_after)
            *close_osd = true;
        return;
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

    wcstombs(sdl_win_title_mb, sdl_win_title, sizeof(sdl_win_title_mb) - 1);

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
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_Always);

    ImGui::Begin("86Box OSD", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoNav);

    /* Show window title (machine info) */
    if (sdl_win_title_mb[0]) {
        ImGui::TextDisabled("%s", sdl_win_title_mb);
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
static bool FocusedButton(const char *label, bool focused)
{
    if (focused)
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
    bool clicked = ImGui::Button(label);
    if (focused)
        ImGui::PopStyleColor();
    return clicked;
}

static bool draw_log(void)
{
    /* Slots: 0=log area, 1=Back. Tab cycles; ESC → osd_handle. */
    static int  log_focused_slot = 1;
    static bool log_focus_area   = false;

    const ImGuiIO &io  = ImGui::GetIO();
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
    ImGui::SetNextWindowSize(ImVec2(560, 340), ImGuiCond_Always);
    ImGui::Begin("Log", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoNav);

    if (log_focus_area) { ImGui::SetNextWindowFocus(); log_focus_area = false; }
    ImGui::BeginChild("##loglines", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()),
                      true, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNav);

    SDL_LockMutex(log_mutex);
    const int total  = log_ring_count;
    const int oldest = (log_ring_count < OSD_LOG_LINES) ? 0 : log_ring_head;
    for (int i = 0; i < total; i++) {
        int idx = (oldest + i) % OSD_LOG_LINES;
        if (log_ring[idx][0])
            ImGui::TextUnformatted(log_ring[idx]);
    }
    SDL_UnlockMutex(log_mutex);

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

    if (FocusedButton("Back", log_focused_slot == 1))
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
    ImGui::SetNextWindowSize(ImVec2(520, 320), ImGuiCond_Always);
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
            const int prev = cd_folder_sel;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow,   true) && cd_folder_sel > 0 && !swallowed_up)                    cd_folder_sel--;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true) && cd_folder_sel < total - 1)                             cd_folder_sel++;
            if (ImGui::IsKeyPressed(ImGuiKey_PageUp,    true)) { int n = cd_folder_sel - 12; cd_folder_sel = n < 0      ? 0          : n; }
            if (ImGui::IsKeyPressed(ImGuiKey_PageDown,  true)) { int n = cd_folder_sel + 12; cd_folder_sel = n >= total ? total - 1  : n; }
            if (ImGui::IsKeyPressed(ImGuiKey_Home,      true))                                                          cd_folder_sel = 0;
            if (ImGui::IsKeyPressed(ImGuiKey_End,       true))                                                          cd_folder_sel = total > 0 ? total - 1 : 0;
            if (cd_folder_sel != prev) scroll_to_sel = true;
            if (enter) {
                const bool is_dotdot = has_parent && cd_folder_sel == 0;
                if (is_dotdot)      cd_folder_go_up();
                else if (total > 0) cd_folder_enter(cd_folder_sel - (has_parent ? 1 : 0));
            }
        }
    }

    ImGui::EndChild();

    if (FocusedButton("Mount", focused_slot == 1)) do_mount();
    if (ImGui::IsItemClicked()) focused_slot = 1;
    ImGui::SameLine();
    if (FocusedButton("Back", focused_slot == 2))
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
       FocusedButton fakes visual focus; Enter handled before Begin(). */
    static int  focused_slot  = 0;   /* 0 = list, 1 = Mount, 2 = Back */
    static bool focus_list    = false;
    static bool scroll_to_sel = false;

    bool stay     = true;
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
    ImGui::SetNextWindowSize(ImVec2(520, 320), ImGuiCond_Always);
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
            const int prev_sel = file_sel;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow,   true) && file_sel > 0 && !swallowed_up)                    file_sel--;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true) && file_sel < file_count - 1)                        file_sel++;
            if (ImGui::IsKeyPressed(ImGuiKey_PageUp,    true))   { int n = file_sel - 12; file_sel = n < 0         ? 0              : n; }
            if (ImGui::IsKeyPressed(ImGuiKey_PageDown,  true)) { int n = file_sel + 12; file_sel = n >= file_count ? file_count - 1 : n; }
            if (ImGui::IsKeyPressed(ImGuiKey_Home,      true))                                                     file_sel = 0;
            if (ImGui::IsKeyPressed(ImGuiKey_End,       true))                                                     file_sel = file_count - 1;
            if (file_sel != prev_sel) scroll_to_sel = true;
            if (enter) { mount_selected(); current_view = VIEW_LOG; log_scroll_pending = true; }
        }

        ImGui::EndChild();
    }

    if (file_count > 0) {
        if (FocusedButton("Mount", focused_slot == 1)) {
            mount_selected();
            current_view = VIEW_LOG;
            log_scroll_pending = true;
        }
        if (ImGui::IsItemClicked()) focused_slot = 1;
        ImGui::SameLine();
    }
    if (FocusedButton("Back", focused_slot == 2))
        show_main_menu();
    if (ImGui::IsItemClicked()) focused_slot = 2;

    ImGui::End();
    return stay;
}

/* ------------------------------------------------------------------ */
/*  Y-flip helper: inverts draw data to match FBO orientation          */
/* ------------------------------------------------------------------ */
static void flip_draw_data_y(ImDrawData *dd)
{
    float h = dd->DisplaySize.y;
    for (int n = 0; n < dd->CmdListsCount; n++) {
        ImDrawList *dl = dd->CmdLists[n];
        for (int i = 0; i < dl->VtxBuffer.Size; i++)
            dl->VtxBuffer.Data[i].pos.y = h - dl->VtxBuffer.Data[i].pos.y;
        for (int i = 0; i < dl->CmdBuffer.Size; i++) {
            float y1 = h - dl->CmdBuffer.Data[i].ClipRect.w;
            float y2 = h - dl->CmdBuffer.Data[i].ClipRect.y;
            dl->CmdBuffer.Data[i].ClipRect.y = y1;
            dl->CmdBuffer.Data[i].ClipRect.w = y2;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Public API: present (called during blit)                           */
/* ------------------------------------------------------------------ */
void osd_present(int fb_w, int fb_h)
{
    if (!osd_visible || !imgui_inited)
        return;

    SDL_GLContext ctx = sdl_shader_get_context();
    if (!ctx)
        return;

    SDL_GL_MakeCurrent(sdl_win, ctx);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();

    /* Use FBO resolution so imgui renders into the texture. */
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)fb_w, (float)fb_h);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    /* Scale mouse: window → drawable → FBO texel via shader viewport. */
    {
        int mx, my;
        SDL_GetMouseState(&mx, &my);

        int win_w, win_h, draw_w, draw_h;
        SDL_GetWindowSize(sdl_win, &win_w, &win_h);
        SDL_GL_GetDrawableSize(sdl_win, &draw_w, &draw_h);
        float dmx = (float)mx * (float)draw_w / (float)(win_w > 0 ? win_w : 1);
        float dmy = (float)my * (float)draw_h / (float)(win_h > 0 ? win_h : 1);

        int vp_x, vp_y, vp_w, vp_h;
        sdl_shader_get_viewport(&vp_x, &vp_y, &vp_w, &vp_h);

        if (vp_w > 0 && vp_h > 0) {
            float fx = ((dmx - (float)vp_x) / (float)vp_w) * (float)fb_w;
            float fy = ((dmy - (float)vp_y) / (float)vp_h) * (float)fb_h;
            io.AddMousePosEvent(fx, fy);
        }
    }

    ImGui::NewFrame();

    bool still_open = true;
    switch (current_view) {
        case VIEW_MENU:
            still_open = draw_menu();
            break;
        case VIEW_LOG:
            still_open = draw_log();
            break;
        case VIEW_CD_FOLDER:
            still_open = draw_folder_browser();
            break;
        default:
            still_open = draw_file_selector();
            break;
    }

    if (!still_open)
        pending_close = true;

    /* Render */
    ImGui::Render();

    /* Y-flip draw data: FBO has Y=0 at bottom, imgui expects Y=0 at top. */
    ImDrawData *dd = ImGui::GetDrawData();
    flip_draw_data_y(dd);
    ImGui_ImplOpenGL3_RenderDrawData(dd);
}

/* ------------------------------------------------------------------ */
/*  Stubs for status bar icons (not yet implemented)                   */
/* ------------------------------------------------------------------ */
void osd_ui_sb_update_icon_state(int tag, int state)   { (void)tag; (void)state; }
void osd_ui_sb_update_icon(int tag, int active)        { (void)tag; (void)active; }
void osd_ui_sb_update_icon_write(int tag, int active)  { (void)tag; (void)active; }
void osd_ui_sb_update_icon_wp(int tag, int state)      { (void)tag; (void)state; }
