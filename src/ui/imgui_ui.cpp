#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl.h"
#include "imgui_sdl.h"
#ifdef _WIN32
#include <windows.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#else
#include <SDL.h>
#include <SDL_syswm.h>
#endif
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <atomic>
#include <array>
#include <thread>
extern "C"
{
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
#ifdef _WIN32
#include <86box/win.h>
#ifdef USE_DISCORD
#include <86box/win_discord.h>
#endif
#endif
#include <86box/sound.h>
#include <86box/video.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/unix_sdl.h>
#include <86box/timer.h>
#include <86box/ui.h>
#include <86box/network.h>
}

#ifdef MTR_ENABLED
#include <minitrace/minitrace.h>
#endif

#ifndef _WIN32
#define INCBIN_STYLE INCBIN_STYLE_SNAKE
#include <incbin.h>
INCBIN(cdromicon, _INCBIN_DIR"/../unix/icons/cdrom.png");
INCBIN(cdromactiveicon, _INCBIN_DIR"/../unix/icons/cdrom_active.png");
INCBIN(floppy_35_icon, _INCBIN_DIR"/../unix/icons/floppy_35.png");
INCBIN(floppy_35_active_icon, _INCBIN_DIR"/../unix/icons/floppy_35_active.png");
INCBIN(floppy_525_icon, _INCBIN_DIR"/../unix/icons/floppy_525.png");
INCBIN(floppy_525_active_icon, _INCBIN_DIR"/../unix/icons/floppy_525_active.png");
INCBIN(mo_icon, _INCBIN_DIR"/../unix/icons/mo.png");
INCBIN(mo_active_icon, _INCBIN_DIR"/../unix/icons/mo_active.png");
INCBIN(zip_icon, _INCBIN_DIR"/../unix/icons/zip.png");
INCBIN(zip_active_icon, _INCBIN_DIR"/../unix/icons/zip_active.png");
INCBIN(cartridge_icon, _INCBIN_DIR"/../unix/icons/cartridge.png");
INCBIN(cassette_icon, _INCBIN_DIR"/../unix/icons/cassette.png");
INCBIN(cassette_active_icon, _INCBIN_DIR"/../unix/icons/cassette_active.png");
INCBIN(hard_disk, _INCBIN_DIR"/../unix/icons/hard_disk.png");
INCBIN(hard_disk_active, _INCBIN_DIR"/../unix/icons/hard_disk_active.png");
INCBIN(network_icon, _INCBIN_DIR"/../unix/icons/network.png");
INCBIN(network_active_icon, _INCBIN_DIR"/../unix/icons/network_active.png");
INCBIN(sound_icon, _INCBIN_DIR"/../unix/icons/sound.png");
#endif

extern "C" SDL_Window* sdl_win;
extern "C" SDL_Renderer	*sdl_render;
extern "C" float menubarheight;

static bool imrendererinit = false;
static bool firstrender = true;

#define MACHINE_HAS_IDE		(machines[machine].flags & MACHINE_IDE_QUAD)
#define MACHINE_HAS_SCSI	(machines[machine].flags & MACHINE_SCSI_DUAL)
typedef struct sdl_blit_params
{
    int x, y, y1, y2, w, h;
} sdl_blit_params;

extern sdl_blit_params params;
extern int blitreq;
extern "C" void sdl_blit(int x, int y, int y1, int y2, int w, int h);

void
take_screenshot(void)
{
	startblit();
	screenshots++;
	endblit();
	device_force_redraw();
}

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

#ifdef MTR_ENABLED
static void
handle_trace(int trace)
{
	//EnableMenuItem(hmenu, IDM_ACTION_BEGIN_TRACE, trace ? MF_GRAYED : MF_ENABLED);
	//EnableMenuItem(hmenu, IDM_ACTION_END_TRACE, trace ? MF_ENABLED : MF_GRAYED);
	if (trace) {
		init_trace();
	}
	else {
		shutdown_trace();
	}
}
#endif

static void open_url(const char* url)
{
#if SDL_VERSION_ATLEAST(2, 0, 14)
	SDL_version version{};
	SDL_GetVersion(&version);
	if (version.major >= 2 && version.minor >= 0 && version.patch >= 14)
	{
		SDL_OpenURL(url);
	}
	else
#endif
	{
#ifndef _WIN32
		char cmd[4096] = { 0 };
#endif

#ifdef _WIN32
		ShellExecuteA(nullptr, "open", url, NULL, NULL, SW_SHOW);
#elif defined __APPLE__
		sprintf(cmd, "open %s", url);
		popen(cmd, "r");
#elif defined __unix__
		sprintf(cmd, "xdg-open %s", url);
		popen(cmd, "r");
#endif
	}
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
#ifdef _WIN32
	{"All Files", "*.*"}
#else
    {"All Files", "*"}
#endif
};

static bool OpenFileChooser(char* res, size_t n, std::vector<std::pair<std::string, std::string>>& filters = allfilefilter, bool save = false)
{
#ifdef _WIN32
	std::string filterwin;
	for (auto& curFilter : filters)
	{
		std::string realfilter = std::get<1>(curFilter);
		filterwin += std::get<0>(curFilter);
		filterwin += " (";
		filterwin += realfilter;
		filterwin += ")";
		filterwin.push_back(0);
		std::transform(realfilter.begin(), realfilter.end(), realfilter.begin(), [](char c) { if (c == ' ') return ';'; return c; });
		filterwin += realfilter;
		filterwin.push_back(0);
	}
	if (std::get<0>(filters[0]) != "All Files")
	{
		filterwin += "All Files (*.*)";
		filterwin.push_back(0);
		filterwin += "*.*";
		filterwin.push_back(0);
	}
	filterwin.push_back(0);
	filterwin.push_back(0);
	std::wstring filterwinwide;
	// Filter strings are pure-ASCII for the moment.
	for (auto& curChar : filterwin)
	{
		filterwinwide.push_back(curChar);
	}
	
	SDL_SysWMinfo wmInfo;
	SDL_VERSION(&wmInfo.version);
	SDL_GetWindowWMInfo(sdl_win, &wmInfo);
	HWND hwnd = wmInfo.info.win.window;
	return (bool)file_dlg(hwnd, (wchar_t*)filterwinwide.c_str(), res, "", save);
	
#else
    bool boolres = false;
    FILE* output;
    int origpause = dopause;
    std::string cmd = "zenity --file-selection";
    for (auto &curFilter : filters)
    {
		std::string realfilter = std::get<1>(curFilter);
		cmd += " --file-filter=\'";
		cmd += std::get<0>(curFilter);
		cmd += " (";
		cmd += realfilter;
		cmd += ") | ";
		cmd += realfilter;
		std::transform(realfilter.begin(), realfilter.end(), realfilter.begin(), [](unsigned char c) { return std::tolower(c); } );
		cmd += " ";
		cmd += realfilter;
		cmd += "\'";
    }
    if (std::get<0>(filters[0]) != "All Files")
    {
	cmd += " --file-filter=\'All Files (*) | *\'";
    }
    if (save) cmd += " --save";
    //plat_pause(1);
    output = popen(cmd.c_str(), "r");
    if (output)
    {
	if (fgets(res, n, output) != NULL)
	{
	    res[strcspn(res, "\r\n")] = 0;
	    boolres = true;
	    pclose(output);
	}
    }
    //plat_pause(origpause);
    return boolres;
#endif
}

struct FileOpenSaveRequest
{
	std::vector<std::pair<std::string, std::string>>& filters = allfilefilter;
	void (*filefunc3params)(uint8_t, char*, uint8_t) = nullptr;
	void (*filefunc2params)(uint8_t, char*) = nullptr;
	void (*filefunc2paramsalt)(char*, uint8_t) = nullptr;
	bool save = false;
	bool wp = false;
	uint8_t id = 0;
};

std::atomic<int> filedlgopen{ 0 };

extern "C" bool IsFileDlgOpen()
{
	return filedlgopen;
}

void file_request_thread(FileOpenSaveRequest param)
{
	char res[4096];
	memset((void*)res, 0, sizeof(res));
	filedlgopen++;
	if (OpenFileChooser(res, sizeof(res), param.filters, param.save))
	{
		if (param.filefunc3params) param.filefunc3params(param.id, res, param.wp);
		else if (param.filefunc2params) param.filefunc2params(param.id, res);
		else if (param.filefunc2paramsalt) param.filefunc2paramsalt(res, param.wp);
		filedlgopen--;
		return;
	}
	else if (param.save && param.filefunc2paramsalt) param.filefunc2paramsalt(NULL, 0);
	filedlgopen--;
}

void DisplayFileAlreadyOpenMessage()
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "86Box",
							"A file dialog is already open. Please close it before opening a new one.\n", nullptr);
}

void file_open_request(FileOpenSaveRequest param)
{
	if (IsFileDlgOpen()) DisplayFileAlreadyOpenMessage();
	file_request_thread(param);
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
		FileOpenSaveRequest filereq{};
		filereq.filters = cartfilter;
		filereq.id = cartid;
		filereq.wp = 0;
		filereq.filefunc3params = cartridge_mount;
		std::thread thr(file_open_request, filereq);
		thr.detach();
	}
	if (ImGui::MenuItem("Image... (write-protected)"))
	{
		FileOpenSaveRequest filereq{};
		filereq.filters = cartfilter;
		filereq.id = cartid;
		filereq.wp = 1;
		filereq.filefunc3params = cartridge_mount;
		std::thread thr(file_open_request, filereq);
		thr.detach();
	}
	ImGui::Separator();
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
#ifdef _WIN32
	if (ImGui::MenuItem("New Image..."))
	{
		SDL_SysWMinfo wmInfo;
		SDL_VERSION(&wmInfo.version);
		SDL_GetWindowWMInfo(sdl_win, &wmInfo);
		HWND hwnd = wmInfo.info.win.window;
		std::thread thr(NewFloppyDialogCreate, hwnd, flpid, 0);
		thr.detach();
	}
	ImGui::Separator();
#endif
	if (ImGui::MenuItem("Image..."))
	{
		FileOpenSaveRequest filereq{};
		filereq.filters = floppyfilter;
		filereq.id = flpid;
		filereq.wp = 0;
		filereq.filefunc3params = floppy_mount;
		std::thread thr(file_open_request, filereq);
		thr.detach();
	}
	if (ImGui::MenuItem("Image... (write-protected)"))
	{
		FileOpenSaveRequest filereq{};
		filereq.filters = floppyfilter;
		filereq.id = flpid;
		filereq.wp = 1;
		filereq.filefunc3params = floppy_mount;
		std::thread thr(file_open_request, filereq);
		thr.detach();
	}
	ImGui::Separator();
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
	if (ImGui::MenuItem("Mute", NULL, cdrom[cdid].sound_on))
	{
		cdrom[cdid].sound_on ^= 1;
		config_save();
		sound_cd_thread_reset();
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Empty", NULL, strlen(cdrom[cdid].image_path) == 0))
	{
	    cdrom_eject(cdid);
	}
	if (ImGui::MenuItem("Reload previous image"))
	{
	    cdrom_reload(cdid);
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Image"))
	{
		FileOpenSaveRequest filereq{};
		filereq.filters = cdromfilter;
		filereq.id = cdid;
		filereq.wp = 0;
		filereq.filefunc2params = cdrom_mount;
		std::thread thr(file_open_request, filereq);
		thr.detach();
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
#ifdef _WIN32
	if (ImGui::MenuItem("New Image..."))
	{
		SDL_SysWMinfo wmInfo;
		SDL_VERSION(&wmInfo.version);
		SDL_GetWindowWMInfo(sdl_win, &wmInfo);
		HWND hwnd = wmInfo.info.win.window;
		std::thread thr(NewFloppyDialogCreate, hwnd, zipid | 0x80, 0);
		thr.detach();
	}
	ImGui::Separator();
#endif
	if (ImGui::MenuItem("Image..."))
	{
		FileOpenSaveRequest filereq{};
		filereq.filters = zipfilter;
		filereq.id = zipid;
		filereq.wp = 0;
		filereq.filefunc3params = zip_mount;
		std::thread thr(file_open_request, filereq);
		thr.detach();
	}
	if (ImGui::MenuItem("Image... (write-protected)"))
	{
		FileOpenSaveRequest filereq{};
		filereq.filters = zipfilter;
		filereq.id = zipid;
		filereq.wp = 1;
		filereq.filefunc3params = zip_mount;
		std::thread thr(file_open_request, filereq);
		thr.detach();
	}
	ImGui::Separator();
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
#ifdef _WIN32
	if (ImGui::MenuItem("New Image..."))
	{
		SDL_SysWMinfo wmInfo;
		SDL_VERSION(&wmInfo.version);
		SDL_GetWindowWMInfo(sdl_win, &wmInfo);
		HWND hwnd = wmInfo.info.win.window;
		std::thread thr(NewFloppyDialogCreate, hwnd, moid | 0x100, 0);
		thr.detach();
	}
	ImGui::Separator();
#endif
	if (ImGui::MenuItem("Image..."))
	{
		FileOpenSaveRequest filereq{};
		filereq.filters = mofilter;
		filereq.id = moid;
		filereq.wp = 0;
		filereq.filefunc3params = mo_mount;
		std::thread thr(file_open_request, filereq);
		thr.detach();
	}
	if (ImGui::MenuItem("Image... (write-protected)"))
	{
		FileOpenSaveRequest filereq{};
		filereq.filters = mofilter;
		filereq.id = moid;
		filereq.wp = 1;
		filereq.filefunc3params = mo_mount;
		std::thread thr(file_open_request, filereq);
		thr.detach();
	}
	ImGui::Separator();
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
	if (ImGui::MenuItem("New Image..."))
	{
		FileOpenSaveRequest filereq{};
		filereq.filters = casfilter;
		filereq.id = 0;
		filereq.wp = 0;
		filereq.save = true;
		filereq.filefunc2paramsalt = cassette_mount;
		std::thread thr(file_open_request, filereq);
		thr.detach();
	}
	ImGui::Separator();
    if (ImGui::MenuItem("Image..."))
    {
		FileOpenSaveRequest filereq{};
		filereq.filters = casfilter;
		filereq.id = 0;
		filereq.wp = 0;
		filereq.filefunc2paramsalt = cassette_mount;
		std::thread thr(file_open_request, filereq);
		thr.detach();
    }
    if (ImGui::MenuItem("Image... (write-protected)"))
    {
		FileOpenSaveRequest filereq{};
		filereq.filters = casfilter;
		filereq.id = 0;
		filereq.wp = 1;
		filereq.filefunc2paramsalt = cassette_mount;
		std::thread thr(file_open_request, filereq);
		thr.detach();
    }
	ImGui::Separator();
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
	ImGui::Separator();
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

static SDL_Texture* cdrom_status_icon[2];
static SDL_Texture* fdd_status_icon[2][2];
static SDL_Texture* cart_icon;
static SDL_Texture* mo_status_icon[2];
static SDL_Texture* zip_status_icon[2];
static SDL_Texture* cas_status_icon[2];
static SDL_Texture* hdd_status_icon[2];
static SDL_Texture* net_status_icon[2];
static SDL_Texture* sound_icon;

#ifdef _WIN32
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
				if (tex)
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
#else
static SDL_Texture* load_icon(const stbi_uc* buffer, int len)
{
    SDL_Texture* tex = nullptr;
    int w, h, c;
    stbi_uc* imgdata = stbi_load_from_memory(buffer, len, &w, &h, &c, 4);
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
    return tex;
}
#endif

extern "C" void HandleSizeChange()
{
    int w, h;
    if (!ImGui::GetCurrentContext()) ImGui::CreateContext(NULL);
    SDL_GetRendererOutputSize(sdl_render, &w, &h);
    ImGuiSDL::Initialize(sdl_render, w, h);
    w = 0, h = 0;

#ifdef _WIN32
    cdrom_status_icon[0] = load_icon(MAKEINTRESOURCE(32 + 256));
    cdrom_status_icon[1] = load_icon(MAKEINTRESOURCE(33 + 256));
    fdd_status_icon[0][0] = load_icon(MAKEINTRESOURCE(24 + 256));
    fdd_status_icon[0][1] = load_icon(MAKEINTRESOURCE(25 + 256));
    fdd_status_icon[1][0] = load_icon(MAKEINTRESOURCE(16 + 256));
    fdd_status_icon[1][1] = load_icon(MAKEINTRESOURCE(17 + 256));
    cart_icon = load_icon(MAKEINTRESOURCE(104 + 256));
    mo_status_icon[0] = load_icon(MAKEINTRESOURCE(56 + 256));
    mo_status_icon[1] = load_icon(MAKEINTRESOURCE(57 + 256));
    zip_status_icon[0] = load_icon(MAKEINTRESOURCE(48 + 256));
    zip_status_icon[1] = load_icon(MAKEINTRESOURCE(49 + 256));
    cas_status_icon[0] = load_icon(MAKEINTRESOURCE(64 + 256));
    cas_status_icon[1] = load_icon(MAKEINTRESOURCE(65 + 256));
    hdd_status_icon[0] = load_icon(MAKEINTRESOURCE(80 + 256));
    hdd_status_icon[1] = load_icon(MAKEINTRESOURCE(81 + 256));
    net_status_icon[0] = load_icon(MAKEINTRESOURCE(96 + 256));
    net_status_icon[1] = load_icon(MAKEINTRESOURCE(97 + 256));
	sound_icon = load_icon(MAKEINTRESOURCE(361));
#else
    cdrom_status_icon[0] = load_icon(gcdromicon_data, gcdromicon_size);
    cdrom_status_icon[1] = load_icon(gcdromactiveicon_data, gcdromactiveicon_size);
    fdd_status_icon[0][0] = load_icon(gfloppy_35_icon_data, gfloppy_35_icon_size);
    fdd_status_icon[0][1] = load_icon(gfloppy_35_active_icon_data, gfloppy_35_active_icon_size);
    fdd_status_icon[1][0] = load_icon(gfloppy_525_icon_data, gfloppy_525_icon_size);
    fdd_status_icon[1][1] = load_icon(gfloppy_525_active_icon_data, gfloppy_525_active_icon_size);
    cart_icon = load_icon(gcartridge_icon_data, gcartridge_icon_size);
    mo_status_icon[0] = load_icon(gmo_icon_data, gmo_icon_size);
    mo_status_icon[1] = load_icon(gmo_active_icon_data, gmo_active_icon_size);
    zip_status_icon[0] = load_icon(gzip_icon_data, gzip_icon_size);
    zip_status_icon[1] = load_icon(gzip_active_icon_data, gzip_active_icon_size);
    cas_status_icon[0] = load_icon(gcassette_icon_data, gcassette_icon_size);
    cas_status_icon[1] = load_icon(gcassette_active_icon_data, gcassette_icon_size);
    hdd_status_icon[0] = load_icon(ghard_disk_data, ghard_disk_size);
    hdd_status_icon[1] = load_icon(ghard_disk_active_data, ghard_disk_active_size);
    net_status_icon[0] = load_icon(gnetwork_icon_data, gnetwork_icon_size);
    net_status_icon[1] = load_icon(gnetwork_active_icon_data, gnetwork_active_icon_size);
	sound_icon = load_icon(gsound_icon_data, gsound_icon_size);
#endif

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

#ifndef _WIN32
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
#endif

uint32_t timer_sb_icons(uint32_t interval, void* param)
{
    std::fill(hddactive.begin(), hddactive.end(), false);
    std::fill(zipactive.begin(), zipactive.end(), false);
    std::fill(moactive.begin(), moactive.end(), false);
    netactive = false;
    return interval;
}

void show_about_dlg()
{
	int buttonid;
	SDL_MessageBoxData msgdata{};
	SDL_MessageBoxButtonData btndata[2] = { { 0 }, { 0 } };
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
	msgdata.window = sdl_win;

	if (SDL_ShowMessageBox(&msgdata, &buttonid) == -1)
	{
		msgdata.window = nullptr;
		SDL_ShowMessageBox(&msgdata, &buttonid);
	}
	if (buttonid == 1)
	{
		open_url("https://86box.net");
	}
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

	if (ImGui::BeginMenu("Tools"))
	{
#ifdef _WIN32
		if (ImGui::MenuItem("Settings"))
		{
			SDL_SysWMinfo wmInfo{};
			SDL_VERSION(&wmInfo.version);
			SDL_GetWindowWMInfo(sdl_win, &wmInfo);
			HWND hwnd = wmInfo.info.win.window;
			win_settings_open(hwnd);
		}
#endif
		if (ImGui::MenuItem("Update status bar icons", NULL, update_icons))
		{
			update_icons ^= 1;
			config_save();
		}
#if defined USE_DISCORD && defined _WIN32
		ImGui::Separator();
		if (ImGui::MenuItem("Enable Discord integration", NULL, enable_discord))
		{
			enable_discord ^= 1;
			if (enable_discord) {
				discord_init();
				discord_update_activity(dopause);
			}
			else
				discord_close();
		}
#endif
		ImGui::Separator();
		if (ImGui::MenuItem("Take screenshot"))
		{
			take_screenshot();
		}
#ifdef _WIN32
		ImGui::Separator();
		if (ImGui::MenuItem("Sound gain..."))
		{
			SDL_SysWMinfo wmInfo;
			SDL_VERSION(&wmInfo.version);
			SDL_GetWindowWMInfo(sdl_win, &wmInfo);
			HWND hwnd = wmInfo.info.win.window;
			std::thread thr(SoundGainDialogCreate, hwnd);
			thr.detach();
		}
#endif
#ifdef MTR_ENABLED
		ImGui::Separator();
		if (ImGui::MenuItem("Begin trace", NULL, tracing_on, !tracing_on))
		{
			tracing_on = !tracing_on;
			handle_trace(tracing_on);
		}
		if (ImGui::MenuItem("End trace", NULL, !tracing_on, tracing_on))
		{
			tracing_on = !tracing_on;
			handle_trace(tracing_on);
		}
#endif
		ImGui::EndMenu();
	}
#if defined(ENABLE_LOG_TOGGLES) || defined(ENABLE_LOG_COMMANDS)
	if (ImGui::BeginMenu("Logging"))
	{
		#define ENABLE_LOG(s, y, x) \
		if (ImGui::MenuItem(s, NULL, x)) \
		{ \
			x ^= 1;\
			config_save();\
		}
#ifdef ENABLE_BUSLOGIC_LOG
		ENABLE_LOG("Enable BusLogic logs", "Ctrl+F4", buslogic_do_log);
#endif
#ifdef ENABLE_CDROM_LOG
		ENABLE_LOG("Enable CD-ROM logs", "Ctrl+F5", cdrom_do_log);
#endif
#ifdef ENABLE_D86F_LOG
		ENABLE_LOG("Enable floppy (86F) logs", "Ctrl+F6", d86f_do_log);
#endif
# ifdef ENABLE_FDC_LOG
		ENABLE_LOG("Enable floppy controller logs", "Ctrl+F7", fdc_do_log);
# endif
#ifdef ENABLE_IDE_LOG
		ENABLE_LOG("Enable IDE logs", "Ctrl+F8", ide_do_log);
#endif
#ifdef ENABLE_SERIAL_LOG
		ENABLE_LOG("Enable Serial Port logs", "Ctrl+F3", serial_do_log);
#endif
#ifdef ENABLE_NIC_LOG
		ENABLE_LOG("Enable Network logs", "Ctrl+F9", serial_do_log);
#endif
#if defined(ENABLE_LOG_COMMANDS)
#ifdef ENABLE_LOG_TOGGLES
		ImGui::Separator();
#endif
#ifdef ENABLE_LOG_BREAKPOINT
		if (ImGui::MenuItem("Log breakpoint", "Ctrl+F10"))
		{
			pclog("---- LOG BREAKPOINT ----\n");
		}
#endif
#ifdef ENABLE_VRAM_DUMP
		if (ImGui::MenuItem("Dump video RAM", "Ctrl+F1"))
		{
			svga_dump_vram();
		}
#endif
#endif
		ImGui::EndMenu();
	}
#endif
	if (ImGui::BeginMenu("Help"))
	{
		if (ImGui::MenuItem("Documentation"))
		{
			open_url("https://86box.readthedocs.io");
	    }
	    if (ImGui::MenuItem("About 86Box"))
	    {
			std::thread thr(show_about_dlg);
			thr.detach();
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
	    if (ImGui::IsItemHovered() && ImGui::GetCurrentContext()->HoveredIdTimer >= 0.5) ImGui::SetTooltip("%s", CassetteFormatStr().c_str());
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
	    if (ImGui::IsItemHovered() && ImGui::GetCurrentContext()->HoveredIdTimer >= 0.5) ImGui::SetTooltip("%s", cmenu[i].FormatStr().c_str());
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
	    if (ImGui::IsItemHovered() && ImGui::GetCurrentContext()->HoveredIdTimer >= 0.5) ImGui::SetTooltip("%s", fddmenu[i].FormatStr().c_str());
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
	    if (ImGui::IsItemHovered() && ImGui::GetCurrentContext()->HoveredIdTimer >= 0.5) ImGui::SetTooltip("%s", cdmenu[i].FormatStr().c_str());
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
	    if (ImGui::IsItemHovered() && ImGui::GetCurrentContext()->HoveredIdTimer >= 0.5) ImGui::SetTooltip("%s", zipmenu[i].FormatStr().c_str());
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
	    if (ImGui::IsItemHovered() && ImGui::GetCurrentContext()->HoveredIdTimer >= 0.5) ImGui::SetTooltip("%s", momenu[i].FormatStr().c_str());
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
#ifdef _WIN32
	ImGui::ImageButton((ImTextureID)sound_icon, ImVec2(16, 16));
	if (ImGui::IsItemHovered() && ImGui::GetCurrentContext()->HoveredIdTimer >= 0.5) ImGui::SetTooltip("Sound");
	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
	{
		SDL_SysWMinfo wmInfo;
		SDL_VERSION(&wmInfo.version);
		SDL_GetWindowWMInfo(sdl_win, &wmInfo);
		HWND hwnd = wmInfo.info.win.window;
		std::thread thr(SoundGainDialogCreate, hwnd);
		thr.detach();
	}
#endif

	ImGui::PopStyleColor(3);
	ImGui::End();
    }
    ImGui::EndFrame();
    ImGui::Render();
    ImGuiSDL::Render(ImGui::GetDrawData());
}
