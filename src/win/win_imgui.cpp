#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl.h"
#include "imgui_sdl.h"
#include <SDL2/SDL.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <windows.h>
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
#include <86box/vid_ega.h>
#include <86box/mo.h>
#include <86box/zip.h>
#include <86box/hdc.h>
#include <86box/hdd.h>
#include <86box/plat.h>
#include <86box/video.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/unix_sdl.h>
#include <86box/timer.h>
#include <86box/ui.h>
#include <86box/network.h>

#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <atomic>
#include <array>

extern "C" SDL_Window* sdl_win;
extern "C" SDL_Renderer	*sdl_render;
extern "C" float menubarheight;

typedef struct sdl_blit_params
{
    int x, y, y1, y2, w, h;
} sdl_blit_params;

static bool imrendererinit = false;
static bool firstrender = true; 

#define MACHINE_HAS_IDE		(machines[machine].flags & MACHINE_IDE_QUAD)
#define MACHINE_HAS_SCSI	(machines[machine].flags & MACHINE_SCSI_DUAL)
extern sdl_blit_params params;
extern int blitreq;
extern "C" void sdl_blit(int x, int y, int y1, int y2, int w, int h);


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

static std::vector<std::pair<std::string, std::string>> floppyfilter
{
    {"All images", "*.0?? *.1?? *.??0 *.86F *.BIN *.CQ? *.D?? *.FLP *.HDM *.IM? *.JSON *.TD0 *.*FD? *.MFM *.XDF"},
    {"Advanced sector images", "*.IMD *.JSON *.TD0"},
    {"Basic sector images", "*.0?? *.1?? *.??0 *.BIN *.CQ? *.D?? *.FLP *.HDM *.IM? *.XDF *.*FD?"},
    {"Flux images", "*.FDI"},
    {"Surface images", "*.86F *.MFM"}
};

static std::vector<std::pair<std::string, std::string>> mofilter
{
    { "MO images", "*.IM? *.MDI" }
};

static std::vector<std::pair<std::string, std::string>> cdromfilter
{
    { "CD-ROM images", "*.ISO *.CUE" }
};

static std::vector<std::pair<std::string, std::string>> casfilter
{
    { "Cassette images", "*.PCM *.RAW *.WAV *.CAS" }
};

static std::vector<std::pair<std::string, std::string>> zipfilter
{
    { "ZIP images", "*.IM? *.ZDI" }
};

static std::vector<std::pair<std::string, std::string>> cartfilter
{
    { "Cartridge images", "*.A *.B *.JRC" }
};

static std::vector<std::pair<std::string, std::string>> allfilefilter
{
    {"All Files", "*"}
};

static bool OpenFileChooser(char* res, size_t n, std::vector<std::pair<std::string, std::string>>& filters = allfilefilter, bool save = false)
{
    return false;
}
struct BaseMenu
{
    virtual std::string FormatStr() { return ""; } 
    virtual void RenderImGuiMenuItemsOnly() {}
    void RenderImGuiMenu()
    {
        if (ImGui::BeginMenu(FormatStr().c_str()))
        {
            RenderImGuiMenuItemsOnly();
            ImGui::EndMenu();
        }
    }
};

struct CartMenu : BaseMenu
{
    int cartid;
    CartMenu(int id)
    {
        cartid = id;
    }
    std::string FormatStr() override
    {
        std::string str = "Cartridge ";
        str += std::to_string(cartid);
        str += " ";
        str += strlen(cart_fns[cartid]) == 0 ? "(empty)" : cart_fns[cartid];
        return str;
    }
    void RenderImGuiMenuItemsOnly() override
    {
        if (ImGui::MenuItem("Image..."))
        {
            char res[4096];
            if (OpenFileChooser(res, sizeof(res), cartfilter))
            {
                cartridge_mount(cartid, res, 0);
            }
        }
        if (ImGui::MenuItem("Image... (write-protected)"))
        {
            char res[4096];
            if (OpenFileChooser(res, sizeof(res), cartfilter))
            {
                cartridge_mount(cartid, res, 1);
            }
        }
        if (ImGui::MenuItem("Eject"))
        {
            cartridge_eject(cartid);
        }
    }
};

struct FloppyMenu : BaseMenu
{
    int flpid;
    FloppyMenu(int id)
    {
        flpid = id;
    }
    void RenderImGuiMenuItemsOnly() override
    {
        if (ImGui::MenuItem("Image..."))
        {
            char res[4096];
            if (OpenFileChooser(res, sizeof(res), floppyfilter))
            {
                floppy_mount(flpid, res, 0);
            }
        }
        if (ImGui::MenuItem("Image... (write-protected)"))
        {
            char res[4096];
            if (OpenFileChooser(res, sizeof(res), floppyfilter))
            {
                floppy_mount(flpid, res, 1);
            }
        }
        if (ImGui::MenuItem("Eject"))
        {
            floppy_eject(flpid);
        }
    }
    std::string FormatStr() override
    {
        std::string str = "Floppy ";
        str += std::to_string(flpid + 1);
        str += " (";
        str += fdd_getname(fdd_get_type(flpid));
        str += ") ";
        str += strlen(floppyfns[flpid]) == 0 ? "(empty)" : floppyfns[flpid];
        return str;
    }
};

struct CDMenu : BaseMenu
{
    int cdid;
    CDMenu(int id)
    {
        cdid = id;
    }
    std::string FormatStr() override
    {
        std::string str = "CD-ROM ";
        str += std::to_string(cdid + 1);
        str += " (";
        str += cdrom[cdid].bus_type == CDROM_BUS_ATAPI ? "ATAPI" : "SCSI";
        str += ") ";
        str += strlen(cdrom[cdid].image_path) == 0 ? "(empty)" : cdrom[cdid].image_path;
        return str;
    }
    void RenderImGuiMenuItemsOnly() override
    {
        if (ImGui::MenuItem("Image"))
        {
            char res[4096];
            if (OpenFileChooser(res, sizeof(res), cdromfilter))
            {
                cdrom_mount(cdid, res);
            }
        }
        if (ImGui::MenuItem("Reload previous image"))
        {
            cdrom_reload(cdid);
        }
        if (ImGui::MenuItem("Empty", NULL, strlen(cdrom[cdid].image_path) == 0))
        {
            cdrom_eject(cdid);
        }
    }
};

struct ZIPMenu : BaseMenu
{
    int zipid;
    ZIPMenu(int id)
    {
        zipid = id;
    }
    std::string FormatStr() override
    {
        std::string str = "ZIP ";
        str += std::to_string(zip_drives[zipid].is_250 ? 250 : 100);
        str += " ";
        str += std::to_string(zipid + 1);
        str += " (";
        str += zip_drives[zipid].bus_type == ZIP_BUS_SCSI ? "SCSI" : "ATAPI";
        str += ") ";
        str += strlen(zip_drives[zipid].image_path) == 0 ? "(empty)" : zip_drives[zipid].image_path;
        return str;
    }
    void RenderImGuiMenuItemsOnly() override
    {
        if (ImGui::MenuItem("Image..."))
        {
            char res[4096];
            if (OpenFileChooser(res, sizeof(res), zipfilter))
            {
                zip_mount(zipid, res, 0);
            }
        }
        if (ImGui::MenuItem("Image... (write-protected)"))
        {
            char res[4096];
            if (OpenFileChooser(res, sizeof(res), zipfilter))
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
    }
};

struct MOMenu : BaseMenu
{
    int moid;
    MOMenu(int id)
    {
        moid = id;
    }
    std::string FormatStr()
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
        return str;
    }
    void RenderImGuiMenuItemsOnly()
    {
        if (ImGui::MenuItem("Image..."))
        {
            char res[4096];
            if (OpenFileChooser(res, sizeof(res), mofilter))
            {
                mo_mount(moid, res, 0);
            }
        }
        if (ImGui::MenuItem("Image... (write-protected)"))
        {
            char res[4096];
            if (OpenFileChooser(res, sizeof(res), mofilter))
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
    }
};

static std::atomic<bool> cas_active, cas_empty;

static void RenderCassetteImguiMenuItemsOnly()
{
    if (ImGui::MenuItem("Image..."))
    {
        char res[4096];
        if (OpenFileChooser(res, sizeof(res), casfilter))
        {
            cassette_mount(res, 0);
        }
    }
    if (ImGui::MenuItem("Image... (write-protected)"))
    {
        char res[4096];
        if (OpenFileChooser(res, sizeof(res), casfilter))
        {
            cassette_mount(res, 1);
        }
    }
    if (ImGui::MenuItem("Play"))
    {
        pc_cas_set_mode(cassette, 0);
    }
    if (ImGui::MenuItem("Record"))
    {
        pc_cas_set_mode(cassette, 1);
    }
    if (ImGui::MenuItem("Rewind to the beginning"))
    {
        pc_cas_rewind(cassette);
    }
    if (ImGui::MenuItem("Fast forward to the end"))
    {
        pc_cas_append(cassette);
    }
    if (ImGui::MenuItem("Eject"))
    {
        cassette_eject();
    }
}

static std::string CassetteFormatStr()
{
    std::string str = "Cassette: ";
    str += strlen(cassette_fname) == 0 ? "(empty)" : cassette_fname;
    return str;
}

static void RenderCassetteImguiMenu()
{
    if (cassette_enable)
    {
        if (ImGui::BeginMenu(CassetteFormatStr().c_str()))
        {
            RenderCassetteImguiMenuItemsOnly();
            ImGui::EndMenu();
        }
    }
}

std::vector<CartMenu> cmenu;
std::vector<FloppyMenu> fddmenu;
std::vector<CDMenu> cdmenu;
std::vector<ZIPMenu> zipmenu;
std::vector<MOMenu> momenu;

extern "C" void InitImGui()
{
    ImGui::CreateContext(NULL);
    ImGui_ImplSDL2_InitForOpenGL(sdl_win, NULL);
}

SDL_Texture* cdrom_status_icon[2];
SDL_Texture* fdd_status_icon[2][2];
SDL_Texture* cart_icon;
SDL_Texture* mo_status_icon[2];
SDL_Texture* zip_status_icon[2];
SDL_Texture* cas_status_icon[2];
SDL_Texture* hdd_status_icon[2];
SDL_Texture* net_status_icon[2];

static SDL_Texture* load_icon(char* name)
{
	SDL_Texture* tex = nullptr;
	HRSRC src = FindResource(NULL, name, RT_RCDATA);
    if (src != NULL) {
        uint32_t len = SizeofResource(NULL, src);
        HGLOBAL myResourceData = LoadResource(NULL, src);
		
		if (myResourceData != NULL)
		{
			void* pMyBinaryData = LockResource(myResourceData);
			int w, h, c;
			stbi_uc* imgdata = stbi_load_from_memory((unsigned char*)pMyBinaryData, len, &w, &h, &c, 4);
			if (imgdata)
			{
				tex = SDL_CreateTexture(sdl_render, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
				if (cdrom_status_icon)
				{
					SDL_UpdateTexture(tex, NULL, imgdata, w * 4);
					SDL_SetTextureBlendMode(tex, SDL_BlendMode::SDL_BLENDMODE_BLEND);
				}
				STBI_FREE(imgdata);
			}
			FreeResource(myResourceData);
		}
	
    }
	return tex;
}

/*

 16                     RCDATA    DISCARDABLE     ICON_PATH "../unix/floppy_525.png"
 17                     RCDATA    DISCARDABLE     ICON_PATH "../unix/floppy_525_active.png"
 24                     RCDATA    DISCARDABLE     ICON_PATH "../unix/icons/floppy_35.png"
 25                     RCDATA    DISCARDABLE     ICON_PATH "../unix/icons/floppy_35_active.png"
 32                     RCDATA    DISCARDABLE     ICON_PATH "../unix/cdrom.png"
 33                     RCDATA    DISCARDABLE     ICON_PATH "../unix/cdrom_active.png"
 48                     RCDATA    DISCARDABLE     ICON_PATH "../unix/zip.png"
 49                     RCDATA    DISCARDABLE     ICON_PATH "../unix/zip_active.png"
 56                     RCDATA    DISCARDABLE     ICON_PATH "../unix/mo.png"
 57                     RCDATA    DISCARDABLE     ICON_PATH "../unix/mo_active.png"
 64                     RCDATA    DISCARDABLE     ICON_PATH "../unix/cassette.png"
 65                     RCDATA    DISCARDABLE     ICON_PATH "../unix/cassette_active.png"
 80                     RCDATA    DISCARDABLE     ICON_PATH "../unix/hard_disk.png"
 81                     RCDATA    DISCARDABLE     ICON_PATH "../unix/hard_disk_active.png"
 96                     RCDATA    DISCARDABLE     ICON_PATH "../unix/network.png"
 97                     RCDATA    DISCARDABLE     ICON_PATH "../unix/network_active.png"
104                     RCDATA    DISCARDABLE     ICON_PATH "../unix/cartridge.png"

*/

extern "C" void HandleSizeChange()
{
    int w, h;
    if (!ImGui::GetCurrentContext()) ImGui::CreateContext(NULL);
    SDL_GetRendererOutputSize(sdl_render, &w, &h);
    ImGuiSDL::Initialize(sdl_render, w, h);
    w = 0, h = 0;
    cdrom_status_icon[0] = load_icon(MAKEINTRESOURCE(32));
    cdrom_status_icon[1] = load_icon(MAKEINTRESOURCE(33));
    fdd_status_icon[0][0] = load_icon(MAKEINTRESOURCE(24));
    fdd_status_icon[0][1] = load_icon(MAKEINTRESOURCE(25));
    fdd_status_icon[1][0] = load_icon(MAKEINTRESOURCE(16));
    fdd_status_icon[1][1] = load_icon(MAKEINTRESOURCE(17));
    cart_icon = load_icon(MAKEINTRESOURCE(104));
    mo_status_icon[0] = load_icon(MAKEINTRESOURCE(56));
    mo_status_icon[1] = load_icon(MAKEINTRESOURCE(57));
    zip_status_icon[0] = load_icon(MAKEINTRESOURCE(48));
    zip_status_icon[1] = load_icon(MAKEINTRESOURCE(49));
    cas_status_icon[0] = load_icon(MAKEINTRESOURCE(64));
    cas_status_icon[1] = load_icon(MAKEINTRESOURCE(65));
    hdd_status_icon[0] = load_icon(MAKEINTRESOURCE(80));
    hdd_status_icon[1] = load_icon(MAKEINTRESOURCE(81));
    net_status_icon[0] = load_icon(MAKEINTRESOURCE(96));
    net_status_icon[1] = load_icon(MAKEINTRESOURCE(97));
    
    imrendererinit = true;
}

extern "C" bool ImGuiWantsMouseCapture()
{
    return ImGui::GetIO().WantCaptureMouse;
}

extern "C" bool ImGuiWantsKeyboardCapture()
{
    return ImGui::GetIO().WantCaptureKeyboard;
}

extern "C" void DeinitializeImGuiSDLRenderer()
{
    ImGuiSDL::Deinitialize();
}

std::array<std::atomic<bool>, 2> cartactive, cartempty;
std::array<std::atomic<bool>, FDD_NUM> fddactive, fddempty;
std::array<std::atomic<bool>, ZIP_NUM> zipactive, zipempty;
std::array<std::atomic<bool>, CDROM_NUM> cdactive, cdempty;
std::array<std::atomic<bool>, MO_NUM> moactive, moempty;
std::array<std::atomic<bool>, 16> hddactive, hddenabled;
std::atomic<bool> netactive;

static int
hdd_count(int bus)
{
    int c = 0;
    int i;

    for (i=0; i<HDD_NUM; i++) {
	if (hdd[i].bus == bus)
		c++;
    }

    return(c);
}

extern "C" void
media_menu_reset()
{
    int curr;

    cmenu.clear();
    fddmenu.clear();
    cdmenu.clear();
    zipmenu.clear();
    momenu.clear();
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

extern "C" void ui_sb_update_panes()
{
    int cart_int, mfm_int, xta_int, esdi_int, ide_int, scsi_int;
    int c_mfm, c_esdi, c_xta;
    int c_ide, c_scsi;

    cart_int = (machines[machine].flags & MACHINE_CARTRIDGE) ? 1 : 0;
    mfm_int = (machines[machine].flags & MACHINE_MFM) ? 1 : 0;
    xta_int = (machines[machine].flags & MACHINE_XTA) ? 1 : 0;
    esdi_int = (machines[machine].flags & MACHINE_ESDI) ? 1 : 0;
    ide_int = (machines[machine].flags & MACHINE_IDE_QUAD) ? 1 : 0;
    scsi_int = (machines[machine].flags & MACHINE_SCSI_DUAL) ? 1 : 0;

    c_mfm = hdd_count(HDD_BUS_MFM);
    c_esdi = hdd_count(HDD_BUS_ESDI);
    c_xta = hdd_count(HDD_BUS_XTA);
    c_ide = hdd_count(HDD_BUS_IDE);
    c_scsi = hdd_count(HDD_BUS_SCSI);

    media_menu_reset();

    std::fill(hddenabled.begin(), hddenabled.end(), false);
    char* hdc_name = hdc_get_internal_name(hdc_current);
    if (c_mfm && (mfm_int || !memcmp(hdc_name, "st506", 5))) {
	/* MFM drives, and MFM or Internal controller. */
	hddenabled[HDD_BUS_MFM] = true;
    }
    if (c_esdi && (esdi_int || !memcmp(hdc_name, "esdi", 4))) {
	/* ESDI drives, and ESDI or Internal controller. */
	hddenabled[HDD_BUS_ESDI] = true;
    }
    if (c_xta && (xta_int || !memcmp(hdc_name, "xta", 3)))
	hddenabled[HDD_BUS_XTA] = true;
    if (c_ide && (ide_int || !memcmp(hdc_name, "xtide", 5) || !memcmp(hdc_name, "ide", 3)))
	hddenabled[HDD_BUS_IDE] = true;
    if (c_scsi && (scsi_int || (scsi_card_current[0] != 0) || (scsi_card_current[1] != 0) ||
	(scsi_card_current[2] != 0) || (scsi_card_current[3] != 0)))
	hddenabled[HDD_BUS_SCSI] = true;
}

extern "C" void ui_sb_update_icon_state(int tag, int state)
{
    uint8_t index = tag & 0x0F;
    switch (tag & 0xF0)
    {
        case SB_FLOPPY:
        {
            fddempty[index] = (bool)(state);
            break;
        }
        case SB_MO:
        {
            moempty[index] = (bool)(state);
            break;
        }
        case SB_CASSETTE:
        {
            cas_empty = (bool)(state);
            break;
        }
        case SB_ZIP:
        {
            zipempty[index] = (bool)(state);
            break;
        }
        case SB_CARTRIDGE:
        {
            cartempty[index] = (bool)(state);
            break;
        }
        case SB_CDROM:
        {
            cdempty[index] = (bool)(state);
            break;
        }
    }
}

extern "C" void ui_sb_update_icon(int tag, int active)
{
    uint8_t index = tag & 0x0F;
    switch (tag & 0xF0)
    {
        case SB_FLOPPY:
        {
            fddactive[index] = (bool)(active);
            break;
        }
        case SB_MO:
        {
            moactive[index] = (bool)(active);
            break;
        }
        case SB_CASSETTE:
        {
            cas_active = (bool)(active);
            break;
        }
        case SB_ZIP:
        {
            zipactive[index] = (bool)(active);
            break;
        }
        case SB_CARTRIDGE:
        {
            cartactive[index] = (bool)(active);
            break;
        }
        case SB_CDROM:
        {
            cdactive[index] = (bool)(active);
            break;
        }
        case SB_HDD:
        {
            hddactive[index] = (bool)(active);
            break;
        }
        case SB_NETWORK:
        {
            netactive = (bool)(active);
            break;
        }
    }
}

intptr_t
fdd_type_to_icon(int type)
{
    int ret = 248;

    switch(type) {
	case 0:
		break;

	case 1: case 2: case 3: case 4:
	case 5: case 6:
		ret = 16;
		break;

	case 7: case 8: case 9: case 10:
	case 11: case 12: case 13:
		ret = 24;
		break;

	default:
		break;
    }

    return(ret);
}

uint32_t timer_sb_icons(uint32_t interval, void* param)
{
    std::fill(hddactive.begin(), hddactive.end(), false);
    std::fill(zipactive.begin(), zipactive.end(), false);
    std::fill(moactive.begin(), moactive.end(), false);
    netactive = false;
    return interval;
}


extern "C" void RenderImGui()
{
    if (!imrendererinit) HandleSizeChange();
    if (!mouse_capture) ImGui_ImplSDL2_NewFrame(sdl_win);
    else
    {
        int w, h;
        SDL_GetRendererOutputSize(sdl_render, &w, &h);
        ImGui::GetIO().DisplaySize.x = w;
        ImGui::GetIO().DisplaySize.y = h;
    }
    ImGui::NewFrame();
    if (ImGui::BeginMainMenuBar())
    {
        menubarheight = ImGui::GetFrameHeight();
        if (firstrender)
        {
            firstrender = false;
            plat_resize(640, 480 + menubarheight);
            media_menu_reset();
            SDL_AddTimer(75, timer_sb_icons, nullptr);
        }
        if (ImGui::BeginMenu("Action"))
        {
            if (ImGui::MenuItem("Keyboard requires capture", NULL, (bool)kbd_req_capture))
            {
                kbd_req_capture ^= 1;
                config_save();
            }
            if (ImGui::MenuItem("Right CTRL is left ALT", NULL, (bool)rctrl_is_lalt))
            {
                rctrl_is_lalt ^= 1;
                config_save();
            }
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
        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("Resizable window", NULL, vid_resize == 1, vid_resize < 2))
            {
                vid_resize ^= 1;
                SDL_SetWindowResizable(sdl_win, (SDL_bool)(vid_resize & 1));
                scrnsz_x = unscaled_size_x;
				scrnsz_y = unscaled_size_y;
                config_save();
            }
            if (ImGui::MenuItem("Remember size & position", NULL, window_remember))
            {
                window_remember = !window_remember;
                if (window_remember)
                {
                    SDL_GetWindowSize(sdl_win, &window_w, &window_h);
                    if (strncasecmp(SDL_GetCurrentVideoDriver(), "wayland", 7) != 0)
                    {
                        SDL_GetWindowPosition(sdl_win, &window_x, &window_y);
                    }
                    config_save();
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Force 4:3 display ratio", NULL, force_43))
            {
                force_43 ^= 1;
                video_force_resize_set(1);
                config_save();
            }
            if (ImGui::BeginMenu("Window scale factor"))
            {
                int cur_scale = scale;
                if (ImGui::MenuItem("0.5x", NULL, scale == 0, !vid_resize))
                {
                    scale = 0;
                }
                if (ImGui::MenuItem("1x", NULL, scale == 1 || vid_resize, !vid_resize))
                {
                    scale = 1;
                }
                if (ImGui::MenuItem("1.5x", NULL, scale == 2, !vid_resize))
                {
                    scale = 2;
                }
                if (ImGui::MenuItem("2x", NULL, scale == 3, !vid_resize))
                {
                    scale = 3;
                }
                if (scale != cur_scale)
                {
                    reset_screen_size();
                    device_force_redraw();
                    video_force_resize_set(1);
                    if (!video_fullscreen)
                    {
                        if (vid_resize & 2)
                            plat_resize(fixed_size_x, fixed_size_y);
                        else
                            plat_resize(scrnsz_x, scrnsz_y);
                    }
                    config_save();
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Take screenshot"))
            {
                take_screenshot();
            }
            if (ImGui::BeginMenu("Filter options"))
            {
                SDL_Event event{};
                event.type = SDL_RENDER_DEVICE_RESET;
                int cur_video_filter_method = video_filter_method;
                if (ImGui::MenuItem("Nearest", NULL, video_filter_method == 0))
                {
                    video_filter_method = 0;
                }
                if (ImGui::MenuItem("Linear", NULL, video_filter_method == 1))
                {
                    video_filter_method = 1;
                }
                if (cur_video_filter_method != video_filter_method)
                {
                    SDL_PushEvent(&event);
                    config_save();
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Fullscreen", "Ctrl-Alt-Pageup", video_fullscreen))
            {
                video_fullscreen ^= 1;
                extern int fullscreen_pending;
                fullscreen_pending = 1;
                config_save();
            }
            if (ImGui::BeginMenu("Fullscreen stretch mode"))
            {
                int cur_video_fullscreen_scale = video_fullscreen_scale;
                if (ImGui::MenuItem("Full screen stretch", NULL, video_fullscreen_scale == FULLSCR_SCALE_FULL))
                {
                    video_fullscreen_scale = FULLSCR_SCALE_FULL;
                }
                if (ImGui::MenuItem("4:3", NULL, video_fullscreen_scale == FULLSCR_SCALE_43))
                {
                    video_fullscreen_scale = FULLSCR_SCALE_43;
                }
                if (ImGui::MenuItem("Square pixels (Keep ratio)", NULL, video_fullscreen_scale == FULLSCR_SCALE_KEEPRATIO))
                {
                    video_fullscreen_scale = FULLSCR_SCALE_KEEPRATIO;
                }
                if (ImGui::MenuItem("Integer scale", NULL, video_fullscreen_scale == FULLSCR_SCALE_INT))
                {
                    video_fullscreen_scale = FULLSCR_SCALE_INT;
                }
                if (cur_video_fullscreen_scale != video_fullscreen_scale)
                    config_save();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("EGA/(S)VGA settings"))
            {
                if (ImGui::BeginMenu("VGA screen type"))
                {
                    if (ImGui::MenuItem("RGB Color", NULL, video_grayscale == 0, true))
                    {
                        video_grayscale = 0;
                        device_force_redraw();
                        config_save();
                    }
                    if (ImGui::MenuItem("RGB Grayscale", NULL, video_grayscale == 1, true))
                    {
                        video_grayscale = 1;
                        device_force_redraw();
                        config_save();
                    }
                    if (ImGui::MenuItem("Amber monitor", NULL, video_grayscale == 2, true))
                    {
                        video_grayscale = 2;
                        device_force_redraw();
                        config_save();
                    }
                    if (ImGui::MenuItem("Green monitor", NULL, video_grayscale == 3, true))
                    {
                        video_grayscale = 3;
                        device_force_redraw();
                        config_save();
                    }
                    if (ImGui::MenuItem("White monitor", NULL, video_grayscale == 4, true))
                    {
                        video_grayscale = 4;
                        device_force_redraw();
                        config_save();
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Grayscale conversion type"))
                {
                    if (ImGui::MenuItem("BT601 (NTSC/PAL)", NULL, video_graytype == 0, true))
                    {
                        video_graytype = 0;
                        device_force_redraw();
                        config_save();
                    }
                    if (ImGui::MenuItem("BT709 (HDTV)", NULL, video_graytype == 1, true))
                    {
                        video_graytype = 1;
                        device_force_redraw();
                        config_save();
                    }
                    if (ImGui::MenuItem("Average", NULL, video_graytype == 2, true))
                    {
                        video_graytype = 2;
                        device_force_redraw();
                        config_save();
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Inverted VGA monitor", NULL, invert_display))
                {
                    invert_display ^= 1;
                    device_force_redraw();
                    config_save();
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("CGA/PCjr/Tandy/EGA/(S)VGA overscan", NULL, enable_overscan))
            {
                update_overscan = 1;
                enable_overscan ^= 1;
                video_force_resize_set(1);
            }
            if (ImGui::MenuItem("Change contrast for monochrome display", NULL, vid_cga_contrast))
            {
				vid_cga_contrast ^= 1;
				cgapal_rebuild();
				config_save();
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
            RenderCassetteImguiMenu();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("Documentation"))
            {
                SDL_OpenURL("https://86box.readthedocs.io");
            }
            if (ImGui::MenuItem("About 86Box"))
            {
                int origpause = dopause;
                int buttonid;
                SDL_MessageBoxData msgdata{};
                SDL_MessageBoxButtonData btndata[2] = { 0, 0 };
                btndata[0].buttonid = 1;
                btndata[0].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
                btndata[0].text = "86box.net";
                btndata[1].buttonid = 2;
                btndata[1].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
                btndata[1].text = "OK";
                msgdata.flags = SDL_MESSAGEBOX_INFORMATION;
                msgdata.message = "An emulator of old computers\n\nAuthors: Sarah Walker, Miran Grca, Fred N. van Kempen (waltje), SA1988, MoochMcGee, reenigne, leilei, JohnElliott, greatpsycho, and others.\n\nReleased under the GNU General Public License version 2. See LICENSE for more information.";
                msgdata.title = "About 86Box";
                msgdata.buttons = btndata;
                msgdata.colorScheme = NULL;
                msgdata.numbuttons = 2;
                msgdata.window = NULL;
                
                plat_pause(1);
                SDL_ShowMessageBox(&msgdata, &buttonid);
                if (buttonid == 1)
                {
                    SDL_OpenURL("https://86box.net");
                }
                plat_pause(origpause);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    
    ImGui::SetNextWindowPos(ImVec2(0, ImGui::GetIO().DisplaySize.y - (menubarheight * 2)));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, menubarheight * 2));
    if (ImGui::Begin("86Box status bar", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar))
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
        if (cassette_enable)
        {
            ImGui::ImageButton((ImTextureID)cas_status_icon[cas_active], ImVec2(16, 16), ImVec2(0,0), ImVec2(1, 1), -1, ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, cas_empty ? 0.75 : 1));
            if (ImGui::IsItemHovered() && ImGui::GetCurrentContext()->HoveredIdTimer >= 0.5) ImGui::SetTooltip(CassetteFormatStr().c_str());
            if (ImGui::BeginPopupContextItem("cassette") || ImGui::BeginPopupContextItem("cassette", ImGuiPopupFlags_MouseButtonLeft))
            {
                RenderCassetteImguiMenuItemsOnly();
                ImGui::EndPopup();
            }
            ImGui::SameLine(0, 0);
        }
        for (size_t i = 0; i < cmenu.size(); i++)
        {
            ImGui::ImageButton((ImTextureID)cart_icon, ImVec2(16, 16), ImVec2(0,0), ImVec2(1, 1), -1, ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, cartempty[i] ? 0.75 : 1));
            if (ImGui::IsItemHovered() && ImGui::GetCurrentContext()->HoveredIdTimer >= 0.5) ImGui::SetTooltip(cmenu[i].FormatStr().c_str());
            if (ImGui::BeginPopupContextItem(("cart" + std::to_string(cmenu[i].cartid)).c_str()) || ImGui::BeginPopupContextItem(("cart" + std::to_string(cmenu[i].cartid)).c_str(), ImGuiPopupFlags_MouseButtonLeft))
            {
                cmenu[i].RenderImGuiMenuItemsOnly();
                ImGui::EndPopup();
            }
            ImGui::SameLine(0, 0);
        }
        for (size_t i = 0; i < fddmenu.size(); i++)
        {
            ImGui::ImageButton((ImTextureID)fdd_status_icon[fdd_type_to_icon(fdd_get_type(i)) == 16][fddactive[i]], ImVec2(16, 16), ImVec2(0,0), ImVec2(1, 1), -1, ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, fddempty[i] ? 0.75 : 1));
            if (ImGui::IsItemHovered() && ImGui::GetCurrentContext()->HoveredIdTimer >= 0.5) ImGui::SetTooltip(fddmenu[i].FormatStr().c_str());
            if (ImGui::BeginPopupContextItem(("flp" + std::to_string(fddmenu[i].flpid)).c_str()) || ImGui::BeginPopupContextItem(("flp" + std::to_string(fddmenu[i].flpid)).c_str(), ImGuiPopupFlags_MouseButtonLeft))
            {
                fddmenu[i].RenderImGuiMenuItemsOnly();
                ImGui::EndPopup();
            }
            ImGui::SameLine(0, 0);
        }
        for (size_t i = 0; i < cdmenu.size(); i++)
        {
            ImGui::ImageButton((ImTextureID)cdrom_status_icon[cdactive[i]], ImVec2(16, 16), ImVec2(0,0), ImVec2(1, 1), -1, ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, cdrom[i].image_path[0] == 0 ? 0.75 : 1));
            if (ImGui::IsItemHovered() && ImGui::GetCurrentContext()->HoveredIdTimer >= 0.5) ImGui::SetTooltip(cdmenu[i].FormatStr().c_str());
            if (ImGui::BeginPopupContextItem(("cdr" + std::to_string(cdmenu[i].cdid)).c_str()) || ImGui::BeginPopupContextItem(("cdr" + std::to_string(cdmenu[i].cdid)).c_str(), ImGuiPopupFlags_MouseButtonLeft))
            {
                cdmenu[i].RenderImGuiMenuItemsOnly();
                ImGui::EndPopup();
            }
            ImGui::SameLine(0, 0);
        }
        for (size_t i = 0; i < zipmenu.size(); i++)
        {
            ImGui::ImageButton((ImTextureID)zip_status_icon[zipactive[i]], ImVec2(16, 16), ImVec2(0,0), ImVec2(1, 1), -1, ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, zip_drives[i].image_path[0] == '\0' ? 0.75 : 1));
            if (ImGui::IsItemHovered() && ImGui::GetCurrentContext()->HoveredIdTimer >= 0.5) ImGui::SetTooltip(zipmenu[i].FormatStr().c_str());
            if (ImGui::BeginPopupContextItem(("zip" + std::to_string(zipmenu[i].zipid)).c_str()) || ImGui::BeginPopupContextItem(("zip" + std::to_string(zipmenu[i].zipid)).c_str(), ImGuiPopupFlags_MouseButtonLeft))
            {
                zipmenu[i].RenderImGuiMenuItemsOnly();
                ImGui::EndPopup();
            }
            ImGui::SameLine(0, 0);
        }
        for (size_t i = 0; i < momenu.size(); i++)
        {
            ImGui::ImageButton((ImTextureID)mo_status_icon[moactive[i]], ImVec2(16, 16), ImVec2(0,0), ImVec2(1, 1), -1, ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, mo_drives[i].image_path[0] == '\0' ? 0.75 : 1));
            if (ImGui::IsItemHovered() && ImGui::GetCurrentContext()->HoveredIdTimer >= 0.5) ImGui::SetTooltip(momenu[i].FormatStr().c_str());
            if (ImGui::BeginPopupContextItem(("mo" + std::to_string(momenu[i].moid)).c_str()) || ImGui::BeginPopupContextItem(("mo" + std::to_string(momenu[i].moid)).c_str(), ImGuiPopupFlags_MouseButtonLeft))
            {
                momenu[i].RenderImGuiMenuItemsOnly();
                ImGui::EndPopup();
            }
            ImGui::SameLine(0, 0);
        }
        if (hddenabled[HDD_BUS_MFM])
        {
            ImGui::ImageButton((ImTextureID)hdd_status_icon[hddactive[HDD_BUS_MFM]], ImVec2(16, 16));
            if (ImGui::IsItemHovered() && ImGui::GetCurrentContext()->HoveredIdTimer >= 0.5) ImGui::SetTooltip("Hard Disk (MFM/RLL)");
            ImGui::SameLine(0, 0);
        }
        if (hddenabled[HDD_BUS_ESDI])
        {
            ImGui::ImageButton((ImTextureID)hdd_status_icon[hddactive[HDD_BUS_ESDI]], ImVec2(16, 16));
            if (ImGui::IsItemHovered() && ImGui::GetCurrentContext()->HoveredIdTimer >= 0.5) ImGui::SetTooltip("Hard Disk (ESDI)");
            ImGui::SameLine(0, 0);
        }
        if (hddenabled[HDD_BUS_XTA])
        {
            ImGui::ImageButton((ImTextureID)hdd_status_icon[hddactive[HDD_BUS_XTA]], ImVec2(16, 16));
            if (ImGui::IsItemHovered() && ImGui::GetCurrentContext()->HoveredIdTimer >= 0.5) ImGui::SetTooltip("Hard Disk (XTA)");
            ImGui::SameLine(0, 0);
        }
        if (hddenabled[HDD_BUS_IDE])
        {
            ImGui::ImageButton((ImTextureID)hdd_status_icon[hddactive[HDD_BUS_IDE]], ImVec2(16, 16));
            if (ImGui::IsItemHovered() && ImGui::GetCurrentContext()->HoveredIdTimer >= 0.5) ImGui::SetTooltip("Hard Disk (IDE)");
            ImGui::SameLine(0, 0);
        }
        if (hddenabled[HDD_BUS_SCSI])
        {
            ImGui::ImageButton((ImTextureID)hdd_status_icon[hddactive[HDD_BUS_SCSI]], ImVec2(16, 16));
            if (ImGui::IsItemHovered() && ImGui::GetCurrentContext()->HoveredIdTimer >= 0.5) ImGui::SetTooltip("Hard Disk (SCSI)");
            ImGui::SameLine(0, 0);
        }
        if (network_available())
        {
            ImGui::ImageButton((ImTextureID)net_status_icon[netactive], ImVec2(16, 16));
            if (ImGui::IsItemHovered() && ImGui::GetCurrentContext()->HoveredIdTimer >= 0.5) ImGui::SetTooltip("Network");
            ImGui::SameLine(0, 0);
        }

        ImGui::PopStyleColor(3);
        ImGui::End();
    }
    ImGui::EndFrame();
    ImGui::Render();
    ImGuiSDL::Render(ImGui::GetDrawData());
}
