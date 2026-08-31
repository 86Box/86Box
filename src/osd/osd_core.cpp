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

#include <functional>
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
extern "C"
{
#include <86box/machine.h>
#include <86box/device.h>
#include <86box/version.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/cdrom_interface.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/cdrom.h>
#include <86box/hdc.h>
#include <86box/rdisk.h>
#include <86box/mo.h>
#include <86box/cartridge.h>
#include <86box/rom.h>
}
#include <86box/plat.h>
#include <86box/ui.h>

#include "osd_core.hpp"
#include "osd_explorer.hpp"

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

static OsdExplorer       explorer;
static OsdExplorerConfig explorer_config;

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

float osd_core_scaled(float value)
{
    return (value > 0.0f) ? (value * osd_layout_scale) : value;
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
    char font_cfg_fn[4096] = { 0 };

    if (pixel_size < OSD_FONT_SIZE)
        pixel_size = OSD_FONT_SIZE;

    io.Fonts->Clear();
    cfg.PixelSnapH  = true;
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;
    cfg.SizePixels  = (float) pixel_size;

    static const ImWchar glyph_ranges[] = { 0x0020, 0xffff, 0 }; // Will not be copied by AddFont* so keep in scope.
    int ret = asset_getfile("assets/fonts/unifont-17.0.05.otf", font_cfg_fn, 4096);
    if (ret)
        io.Fonts->AddFontFromFileTTF(font_cfg_fn, (float)pixel_size, &cfg, glyph_ranges);
    else
        io.Fonts->AddFontDefaultBitmap(&cfg);

    osd_font_raster_scale = (float) pixel_size / (float) OSD_FONT_SIZE;
    apply_layout_scale();
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
    ".iso", ".cue", ".toc", ".ccd", ".mds", ".mdx", nullptr
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

/* ------------------------------------------------------------------ */
/*  Mount helpers                                                      */
/* ------------------------------------------------------------------ */
static void mount_path(const char *path)
{
    /* Disable log suppression around mount to capture all output. */
    char msg[OSD_PATH_CAPACITY + 32];
    snprintf(msg, sizeof(msg), "Loading: %s", path);
    osd_log_push(msg);
    pclog_toggle_suppr();
    switch (current_view) {
        case VIEW_FILE_FLOPPY:
            floppy_mount(0, (char *) path, 0);
            break;
        case VIEW_FILE_CD:
        case VIEW_CD_FOLDER:
            cdrom_mount(0, (char *) path);
            break;
        case VIEW_FILE_RDISK:
            rdisk_mount(0, (char *) path, 0);
            break;
        case VIEW_FILE_CART:
            cartridge_mount(0, (char *) path, 0);
            break;
        case VIEW_FILE_MO:
            mo_mount(0, (char *) path, 0);
            break;
        default:
            pclog_toggle_suppr();
            return;
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
    std::function<bool()> is_enabled;
};

static bool
isFirstCdromAvailable(void)
{
    const char *name = hdc_get_internal_name(hdc_current[0]);
    if ((cdrom[0].bus_type == CDROM_BUS_ATAPI) && !((machine_has_flags(machine, MACHINE_IDE_QUAD) > 0) || other_ide_present) && memcmp(name, "ide", 3) && memcmp(name, "xtide", 5) && memcmp(name, "mcide", 5))
        return false;
    if ((cdrom[0].bus_type == CDROM_BUS_SCSI) && !((machine_has_flags(machine, MACHINE_SCSI) > 0) || other_scsi_present) && (scsi_card_current[0] == 0) && (scsi_card_current[1] == 0) && (scsi_card_current[2] == 0) && (scsi_card_current[3] == 0))
        return false;
    if ((cdrom[0].bus_type == CDROM_BUS_MITSUMI || cdrom[0].bus_type == CDROM_BUS_MKE) && (cdrom_interface_current == 0))
        return false;
    if (cdrom[0].bus_type != 0) {
        return true;
    }
    return false;
}

static bool
isFirstRdiskAvailable(void)
{
    const char *name = hdc_get_internal_name(hdc_current[0]);
    if ((rdisk_drives[0].bus_type == RDISK_BUS_ATAPI) && !((machine_has_flags(machine, MACHINE_IDE_QUAD) > 0) || other_ide_present) && memcmp(name, "ide", 3) && memcmp(name, "xtide", 5) && memcmp(name, "mcide", 5))
        return false;
    if ((rdisk_drives[0].bus_type == RDISK_BUS_SCSI) && !((machine_has_flags(machine, MACHINE_SCSI) > 0) || other_scsi_present) && (scsi_card_current[0] == 0) && (scsi_card_current[1] == 0) && (scsi_card_current[2] == 0) && (scsi_card_current[3] == 0))
        return false;
    if (rdisk_drives[0].bus_type != 0) {
        return true;
    }
    return false;
}

static bool
isFirstMoAvailable(void)
{
    const char *name = hdc_get_internal_name(hdc_current[0]);
    if ((mo_drives[0].bus_type == RDISK_BUS_ATAPI) && !((machine_has_flags(machine, MACHINE_IDE_QUAD) > 0) || other_ide_present) && memcmp(name, "ide", 3) && memcmp(name, "xtide", 5) && memcmp(name, "mcide", 5))
        return false;
    if ((mo_drives[0].bus_type == RDISK_BUS_SCSI) && !((machine_has_flags(machine, MACHINE_SCSI) > 0) || other_scsi_present) && (scsi_card_current[0] == 0) && (scsi_card_current[1] == 0) && (scsi_card_current[2] == 0) && (scsi_card_current[3] == 0))
        return false;
    if (mo_drives[0].bus_type != 0) {
        return true;
    }
    return false;
}

static const MenuItem menu_items[] = {
    { "Load Floppy Image...",      ACT_NONE,         VIEW_FILE_FLOPPY, [] () -> bool { return fdd_get_type(0); }                 },
    { "Load CD-ROM Image...",      ACT_NONE,         VIEW_FILE_CD,     isFirstCdromAvailable                                     },
    { "Mount CD Folder (VISO)...", ACT_NONE,         VIEW_CD_FOLDER,   isFirstCdromAvailable                                     },
    { "Load Removable Disk...",    ACT_NONE,         VIEW_FILE_RDISK,  isFirstRdiskAvailable                                     },
    { "Load Cartridge...",         ACT_NONE,         VIEW_FILE_CART,   [] () -> bool { return machine_has_cartridge(machine); }  },
    { "Load MO Image...",          ACT_NONE,         VIEW_FILE_MO,     isFirstMoAvailable                                        },
    { nullptr, ACT_NONE, VIEW_MENU }, /* separator */
    { "Eject Floppy",              ACT_EJECT_FLOPPY, VIEW_MENU, [] () -> bool { return fdd_get_type(0) && floppyfns[0][0] != 0; }                        },
    { "Eject CD-ROM",              ACT_EJECT_CD,     VIEW_MENU, [] () -> bool { return isFirstCdromAvailable() && cdrom[0].image_path[0] != 0; }         },
    { "Eject Removable Disk",      ACT_EJECT_RDISK,  VIEW_MENU, [] () -> bool { return isFirstRdiskAvailable() && rdisk_drives[0].image_path[0] != 0; }  },
    { "Eject Cartridge",           ACT_EJECT_CART,   VIEW_MENU, [] () -> bool { return machine_has_cartridge(machine) && cart_fns[0][0] != 0; }          },
    { "Eject MO",                  ACT_EJECT_MO,     VIEW_MENU, [] () -> bool { return isFirstMoAvailable() && mo_drives[0].image_path[0] != 0; }        },
    { nullptr, ACT_NONE, VIEW_MENU }, /* separator */
    { "Show Log",                  ACT_NONE,         VIEW_LOG,  [] () -> bool { return true; }  },
    { nullptr, ACT_NONE, VIEW_MENU }, /* separator */
    { "Hard Reset",                ACT_HARDRESET,    VIEW_MENU, [] () -> bool { return true; }  },
    { "Toggle Fullscreen",         ACT_FULLSCREEN,   VIEW_MENU, [] () -> bool { return true; }  },
    { "Exit 86Box",                ACT_EXIT,         VIEW_MENU, [] () -> bool { return true; }  },
    { nullptr, ACT_NONE, VIEW_MENU }, /* separator */
    { "Close OSD",                 ACT_CLOSE_OSD,    VIEW_MENU, [] () -> bool { return true; }  },
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

static const char *
view_title(OsdView v)
{
    switch (v) {
        case VIEW_FILE_FLOPPY:
            return "Select Floppy Image";
        case VIEW_FILE_CD:
            return "Select CD-ROM Image";
        case VIEW_FILE_RDISK:
            return "Select Removable Disk Image";
        case VIEW_FILE_CART:
            return "Select Cartridge";
        case VIEW_FILE_MO:
            return "Select MO Image";
        case VIEW_CD_FOLDER:
            return "Mount Folder as CD-ROM";
        default:
            return "Select File";
    }
}

static const char *
view_accept_label(OsdView v)
{
    return (v == VIEW_CD_FOLDER) ? "Mount" : "Load";
}

static void
open_browser(OsdView view)
{
    explorer_config.title           = view_title(view);
    explorer_config.accept_label    = view_accept_label(view);
    explorer_config.mode            = (view == VIEW_CD_FOLDER) ? OsdExplorerMode::Directory : OsdExplorerMode::File;
    explorer_config.extension_globs = exts_for_view(view);
    explorer_config.initial_path    = nullptr;

    explorer.Open(explorer_config);
    current_view = view;
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
    else
        open_browser(mi.view);

    if (mi.view == VIEW_LOG)
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
    if (enter && menu_sel >= 0 && menu_items[menu_sel].is_enabled())
        activate_menu_item(menu_sel, &close_osd);

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(osd_core_scaled(320.0f), osd_core_scaled(0.0f)), ImGuiCond_Always);

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
        if (ImGui::Selectable(mi.label, selected, !mi.is_enabled() ? ImGuiSelectableFlags_Disabled : 0)) {
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
    ImGui::SetNextWindowSize(ImVec2(osd_core_scaled(560.0f), osd_core_scaled(340.0f)), ImGuiCond_Always);
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
/*  Draw: File selector                                               */
/* ------------------------------------------------------------------ */
static bool draw_browser(void)
{
    OsdExplorerResult result = explorer.Draw();
    if (result.type == OsdExplorerResultType::Accepted) {
        mount_path(result.path.data());
        current_view       = VIEW_LOG;
        log_scroll_pending = true;
    } else if (result.type == OsdExplorerResultType::Cancelled)
        show_main_menu();

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
        default:             return draw_browser();
    }
}

int osd_percentage = 0;

void osd_core_draw_indicators(void)
{
#if 0
    ImGuiWindowFlags window_flags = 0;
    window_flags |= ImGuiWindowFlags_NoBackground;
    window_flags |= ImGuiWindowFlags_NoTitleBar;
    bool open_ptr = true;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    if (ImGui::Begin("DrawWin", &open_ptr, window_flags)) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1.0), "%d%%", osd_percentage);
        ImGui::End();
    }
#endif
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
