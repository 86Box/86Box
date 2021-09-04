#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_sdl.h"
#include <SDL.h>
#include <86box/86box.h>
#include <86box/machine.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/config.h>
#include <86box/timer.h>
#include <86box/cassette.h>
#include <86box/cartridge.h>
#include <86box/fdd.h>
#include <86box/fdd_86f.h>
#include <86box/cdrom.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/mo.h>
#include <86box/zip.h>
#include <86box/hdc.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/unix_sdl.h>
#include <86box/timer.h>
#include <86box/ui.h>

#include <string>
#include <vector>

extern "C" SDL_Window* sdl_win;
extern "C" SDL_Renderer	*sdl_render;
extern "C" float menubarheight;
static bool imrendererinit = false;
static bool firstrender = true;

#define MACHINE_HAS_IDE		(machines[machine].flags & MACHINE_IDE_QUAD)
#define MACHINE_HAS_SCSI	(machines[machine].flags & MACHINE_SCSI_DUAL)

static inline int
is_valid_cartridge(void)
{
    return ((machines[machine].flags & MACHINE_CARTRIDGE) ? 1 : 0);
}

static inline int
is_valid_fdd(int i)
{
    return fdd_get_type(i) != 0;
}

static inline int
is_valid_cdrom(int i)
{
    if ((cdrom[i].bus_type == CDROM_BUS_ATAPI) && !MACHINE_HAS_IDE && memcmp(hdc_get_internal_name(hdc_current), "ide", 3))
	return 0;
    if ((cdrom[i].bus_type == CDROM_BUS_SCSI) && !MACHINE_HAS_SCSI &&
	(scsi_card_current[0] == 0) && (scsi_card_current[1] == 0) &&
	(scsi_card_current[2] == 0) && (scsi_card_current[3] == 0))
	return 0;
    return cdrom[i].bus_type != 0;
}

static inline int
is_valid_zip(int i)
{
    if ((zip_drives[i].bus_type == ZIP_BUS_ATAPI) && !MACHINE_HAS_IDE && memcmp(hdc_get_internal_name(hdc_current), "ide", 3))
	return 0;
    if ((zip_drives[i].bus_type == ZIP_BUS_SCSI) && !MACHINE_HAS_SCSI &&
	(scsi_card_current[0] == 0) && (scsi_card_current[1] == 0) &&
	(scsi_card_current[2] == 0) && (scsi_card_current[3] == 0))
	return 0;
    return zip_drives[i].bus_type != 0;
}

static inline int
is_valid_mo(int i)
{
    if ((mo_drives[i].bus_type == MO_BUS_ATAPI) && !MACHINE_HAS_IDE && memcmp(hdc_get_internal_name(hdc_current), "ide", 3))
	return 0;
    if ((mo_drives[i].bus_type == MO_BUS_SCSI) && !MACHINE_HAS_SCSI &&
	(scsi_card_current[0] == 0) && (scsi_card_current[1] == 0) &&
	(scsi_card_current[2] == 0) && (scsi_card_current[3] == 0))
	return 0;
    return mo_drives[i].bus_type != 0;
}

static bool OpenFileChooser(char* res, size_t n)
{
    bool boolres = false;
    FILE* output;
    int origpause = dopause;
    plat_pause(1);
    output = popen("zenity --file-selection", "r");
    if (output)
    {
        if (fgets(res, n, output) != NULL)
        {
            res[strcspn(res, "\r\n")] = 0;
            boolres = true;
        }
    }
    plat_pause(origpause);
    return boolres;
}

struct CartMenu
{
    int cartid;
    CartMenu(int id)
    {
        cartid = id;
    }
    void RenderImGuiMenu()
    {
        std::string str = "Cartridge ";
        str += std::to_string(cartid);
        str += " ";
        str += strlen(cart_fns[cartid]) == 0 ? "(empty)" : cart_fns[cartid];
        if (ImGui::BeginMenu(str.c_str()))
        {
            if (ImGui::MenuItem("Image..."))
            {
                char res[4096];
                if (OpenFileChooser(res, sizeof(res)))
                {
                    cartridge_mount(cartid, res, 0);
                }
            }
            if (ImGui::MenuItem("Image... (write-protected)"))
            {
                char res[4096];
                if (OpenFileChooser(res, sizeof(res)))
                {
                    cartridge_mount(cartid, res, 1);
                }
            }
            if (ImGui::MenuItem("Eject"))
            {
                cartridge_eject(cartid);
            }
        }
    }
};

struct FloppyMenu
{
    int flpid;
    FloppyMenu(int id)
    {
        flpid = id;
    }
    void RenderImGuiMenu()
    {
        std::string str = "Floppy ";
        str += std::to_string(flpid + 1);
        str += " (";
        str += fdd_getname(fdd_get_type(flpid));
        str += ") ";
        str += strlen(floppyfns[flpid]) == 0 ? "(empty)" : floppyfns[flpid];
        if (ImGui::BeginMenu(str.c_str()))
        {
            if (ImGui::MenuItem("Image..."))
            {
                char res[4096];
                if (OpenFileChooser(res, sizeof(res)))
                {
                    floppy_mount(flpid, res, 0);
                }
            }
            if (ImGui::MenuItem("Image... (write-protected)"))
            {
                char res[4096];
                if (OpenFileChooser(res, sizeof(res)))
                {
                    floppy_mount(flpid, res, 1);
                }
            }
            if (ImGui::MenuItem("Eject"))
            {
                floppy_eject(flpid);
            }
            ImGui::EndMenu();
        }
    }
};

struct CDMenu
{
    int cdid;
    CDMenu(int id)
    {
        cdid = id;
    }
    void RenderImGuiMenu()
    {
        std::string str = "CD-ROM ";
        str += std::to_string(cdid + 1);
        str += " (";
        str += cdrom[cdid].bus_type == 1 ? "SCSI" : "ATAPI";
        str += ") ";
        str += strlen(cdrom[cdid].image_path) == 0 ? "(empty)" : cdrom[cdid].image_path;
        if (ImGui::BeginMenu(str.c_str()))
        {
            if (ImGui::MenuItem("Image"))
            {
                char res[4096];
                if (OpenFileChooser(res, sizeof(res)))
                {
                    cdrom_mount(cdid, res);
                }
            }
            if (ImGui::MenuItem("Reload previous image"))
            {
                cdrom_mount(cdid, cdrom[cdid].prev_image_path);
            }
            if (ImGui::MenuItem("Empty", NULL, strlen(cdrom[cdid].image_path) == 0))
            {
                cdrom_eject(cdid);
            }
            ImGui::EndMenu();
        }
    }
};

struct ZIPMenu
{
    int zipid;
    ZIPMenu(int id)
    {
        zipid = id;
    }
    void RenderImGuiMenu()
    {
        std::string str = "ZIP ";
        str += std::to_string(zip_drives[zipid].is_250 ? 250 : 100);
        str += " ";
        str += std::to_string(zipid + 1);
        str += " (";
        str += zip_drives[zipid].bus_type == 1 ? "SCSI" : "ATAPI";
        str += ") ";
        str += strlen(zip_drives[zipid].image_path) == 0 ? "(empty)" : zip_drives[zipid].image_path;
        if (ImGui::BeginMenu(str.c_str()))
        {
            if (ImGui::MenuItem("Image..."))
            {
                char res[4096];
                if (OpenFileChooser(res, sizeof(res)))
                {
                    zip_mount(zipid, res, 0);
                }
            }
            if (ImGui::MenuItem("Image... (write-protected)"))
            {
                char res[4096];
                if (OpenFileChooser(res, sizeof(res)))
                {
                    zip_mount(zipid, res, 1);
                }
            }
            if (ImGui::MenuItem("Reload previous image"))
            {
                zip_reload(zipid);
            }
            if (ImGui::MenuItem("Eject"))
            {
                zip_eject(zipid);
            }
            ImGui::EndMenu();
        }
    }
};

struct MOMenu
{
    int moid;
    MOMenu(int id)
    {
        moid = id;
    }
    void RenderImGuiMenu()
    {
        std::string str = "MO ";
        str += std::to_string(moid + 1);
        str += " (";
        switch(mo_drives[moid].bus_type)
        {
            case MO_BUS_ATAPI:
                str += "ATAPI";
                break;
            case MO_BUS_SCSI:
                str += "SCSI";
                break;
            case MO_BUS_USB:
                str += "USB";
                break;
        }
        str += ") ";
        str += strlen(mo_drives[moid].image_path) == 0 ? "(empty)" : mo_drives[moid].image_path;
        if (ImGui::BeginMenu(str.c_str()))
        {
            if (ImGui::MenuItem("Image..."))
            {
                char res[4096];
                if (OpenFileChooser(res, sizeof(res)))
                {
                    mo_mount(moid, res, 0);
                }
            }
            if (ImGui::MenuItem("Image... (write-protected)"))
            {
                char res[4096];
                if (OpenFileChooser(res, sizeof(res)))
                {
                    mo_mount(moid, res, 1);
                }
            }
            if (ImGui::MenuItem("Reload previous image"))
            {
                mo_reload(moid);
            }
            if (ImGui::MenuItem("Eject"))
            {
                mo_eject(moid);
            }
            ImGui::EndMenu();
        }
    }
};

std::vector<CartMenu> cmenu;
std::vector<FloppyMenu> fddmenu;
std::vector<CDMenu> cdmenu;
std::vector<ZIPMenu> zipmenu;
std::vector<MOMenu> momenu;

extern "C" void
media_menu_reset()
{
    int curr;
    for(int i = 0; i < 2; i++) {
	if(is_valid_cartridge()) {
		cmenu.emplace_back(i);
	}
	curr++;
    }
    for(int i = 0; i < FDD_NUM; i++) {
	if(is_valid_fdd(i)) {
		fddmenu.emplace_back(i);
	}
	curr++;
    }
    for(int i = 0; i < CDROM_NUM; i++) {
	if(is_valid_cdrom(i)) {
		cdmenu.emplace_back(i);
	}
	curr++;
    }
    for(int i = 0; i < ZIP_NUM; i++) {
	if(is_valid_zip(i)) {
		zipmenu.emplace_back(i);
	}
	curr++;
    }
    for(int i = 0; i < MO_NUM; i++) {
	if(is_valid_mo(i)) {
		momenu.emplace_back(i);
	}
	curr++;
    }
}

extern "C" void InitImGui()
{
    ImGui::CreateContext(NULL);
    ImGui_ImplSDL2_InitForOpenGL(sdl_win, NULL);
}

extern "C" void HandleSizeChange()
{
    int w, h;
    if (!ImGui::GetCurrentContext()) ImGui::CreateContext(NULL);
    SDL_GetRendererOutputSize(sdl_render, &w, &h);
    ImGuiSDL::Initialize(sdl_render, w, h);
    imrendererinit = true;
}
extern "C" void plat_resize(int w, int h);
extern "C" bool ImGuiWantsMouseCapture()
{
    return ImGui::GetIO().WantCaptureMouse;
}
extern "C" bool ImGuiWantsKeyboardCapture()
{
    return ImGui::GetIO().WantCaptureKeyboard;
}
extern "C" void RenderImGui()
{
    if (!imrendererinit) HandleSizeChange();
    ImGui_ImplSDL2_NewFrame(sdl_win);
    ImGui::NewFrame();
    if (ImGui::BeginMainMenuBar())
    {
        menubarheight = ImGui::GetFrameHeight();
        if (firstrender)
        {
            firstrender = false;
            plat_resize(640, 480 + menubarheight);
            media_menu_reset();
        }
        if (ImGui::BeginMenu("Action"))
        {
            if (ImGui::MenuItem("Keyboard requires capture", NULL, (bool)kbd_req_capture))
                kbd_req_capture ^= 1;
            if (ImGui::MenuItem("Right CTRL is left ALT", NULL, (bool)rctrl_is_lalt))
                rctrl_is_lalt ^= 1;
            ImGui::Separator();
            if (ImGui::MenuItem("Hard Reset", NULL))
            {
                pc_reset_hard();
            }
            if (ImGui::MenuItem("Ctrl+Alt+Del", "Ctrl+F12"))
            {
                pc_send_cad();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Ctrl+Alt+Esc", NULL))
            {
                pc_send_cae();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Pause", NULL, (bool)dopause))
            {
                plat_pause(dopause ^ 1);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4"))
            {
                SDL_Event event;
                event.type = SDL_QUIT;
                SDL_PushEvent(&event);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Media"))
        {
            for (auto &cartMenu : cmenu)
            {
                cartMenu.RenderImGuiMenu();
            }
            for (auto &floppyMenu : fddmenu)
            {
                floppyMenu.RenderImGuiMenu();
            }
            for (auto &curcdmenu : cdmenu)
            {
                curcdmenu.RenderImGuiMenu();
            }
            for (auto &curzipmenu : zipmenu)
            {
                curzipmenu.RenderImGuiMenu();
            }
            for (auto &curmomenu : momenu)
            {
                curmomenu.RenderImGuiMenu();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    ImGui::Render();
    ImGuiSDL::Render(ImGui::GetDrawData());
}