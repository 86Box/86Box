#define UNICODE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <86box/86box.h>
#include <86box/cdrom.h>
#include <86box/config.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/fdd_86f.h>
#include <86box/hdc.h>
#include <86box/language.h>
#include <86box/machine.h>
#include <86box/scsi_device.h>
#include <86box/mo.h>
#include <86box/plat.h>
#include <86box/scsi.h>
#include <86box/sound.h>
#include <86box/ui.h>
#include <86box/zip.h>
#include <86box/win.h>

#define MACHINE_HAS_IDE	(machines[machine].flags & MACHINE_IDE_QUAD)

#define FDD_FIRST	0
#define CDROM_FIRST	FDD_FIRST + FDD_NUM
#define ZIP_FIRST	CDROM_FIRST + CDROM_NUM
#define MO_FIRST	ZIP_FIRST + ZIP_NUM

static HMENU	media_menu, stbar_menu;
static HMENU	menus[FDD_NUM + CDROM_NUM + ZIP_NUM + MO_NUM];
static char	index_map[255];

static void
media_menu_set_ids(HMENU hMenu, int id)
{
    int c = GetMenuItemCount(hMenu);

    MENUITEMINFO mii = { 0 };
    mii.fMask = MIIM_ID;
    mii.cbSize = sizeof(mii);

    for(int i = 0; i < c; i++)
    {
	GetMenuItemInfo(hMenu, i, TRUE, &mii);
	mii.wID |= id;
	SetMenuItemInfo(hMenu, i, TRUE, &mii);
    }
}

/* Loads the submenu from resource by name */
static HMENU
media_menu_load_resource(wchar_t *lpName)
{
    HMENU loaded = LoadMenu(NULL, lpName);

    /* The actual submenu is in a dummy popup menu item */
    HMENU actual = GetSubMenu(loaded, 0);

    /* Now that we have our submenu, we can destroy the parent menu */
    RemoveMenu(loaded, (UINT_PTR)actual, MF_BYCOMMAND);
    DestroyMenu(loaded);

    return actual;
}

static void
media_menu_set_name_floppy(int drive)
{
    wchar_t name[512], temp[512];
    MENUITEMINFO mii = { 0 };

    mbstowcs(temp, fdd_getname(fdd_get_type(drive)),
	     strlen(fdd_getname(fdd_get_type(drive))) + 1);
    if (wcslen(floppyfns[drive]) == 0) {
	_swprintf(name, plat_get_string(IDS_2108),
		  drive + 1, temp, plat_get_string(IDS_2057));
    } else {
	_swprintf(name, plat_get_string(IDS_2108),
		  drive + 1, temp, floppyfns[drive]);
    }

    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STRING;
    mii.dwTypeData = name;

    SetMenuItemInfo(media_menu, (UINT_PTR)menus[FDD_FIRST + drive], FALSE, &mii);
}

static void
media_menu_set_name_cdrom(int drive)
{
    wchar_t name[512], *temp;
    MENUITEMINFO mii = { 0 };

    int bus = cdrom[drive].bus_type;
    int id = IDS_5377 + (bus - 1);

    temp = plat_get_string(id);

    if (cdrom[drive].host_drive == 200) {
	if (wcslen(cdrom[drive].image_path) == 0)
		_swprintf(name, plat_get_string(IDS_5120), drive+1, temp, plat_get_string(IDS_2057));
	else
		_swprintf(name, plat_get_string(IDS_5120), drive+1, temp, cdrom[drive].image_path);
    } else
	_swprintf(name, plat_get_string(IDS_5120), drive+1, temp, plat_get_string(IDS_2057));

    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STRING;
    mii.dwTypeData = name;

    SetMenuItemInfo(media_menu, (UINT_PTR)menus[CDROM_FIRST + drive], FALSE, &mii);
}

static void
media_menu_set_name_zip(int drive)
{
    wchar_t name[512], *temp;
    MENUITEMINFO mii = { 0 };

    int bus = zip_drives[drive].bus_type;
    int id = IDS_5377 + (bus - 1);

    temp = plat_get_string(id);

    int type = zip_drives[drive].is_250 ? 250 : 100;

    if (wcslen(zip_drives[drive].image_path) == 0) {
	_swprintf(name, plat_get_string(IDS_2054),
		  type, drive+1, temp, plat_get_string(IDS_2057));
    } else {
	_swprintf(name, plat_get_string(IDS_2054),
		  type, drive+1, temp, zip_drives[drive].image_path);
    }

    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STRING;
    mii.dwTypeData = name;

    SetMenuItemInfo(media_menu, (UINT_PTR)menus[ZIP_FIRST + drive], FALSE, &mii);
}

static void
media_menu_set_name_mo(int drive)
{
    wchar_t name[512], *temp;
    MENUITEMINFO mii = { 0 };

    int bus = mo_drives[drive].bus_type;
    int id = IDS_5377 + (bus - 1);

    temp = plat_get_string(id);

    if (wcslen(mo_drives[drive].image_path) == 0) {
	_swprintf(name, plat_get_string(IDS_2115),
		  drive+1, temp, plat_get_string(IDS_2057));
    } else {
	_swprintf(name, plat_get_string(IDS_2115),
		  drive+1, temp, mo_drives[drive].image_path);
    }

    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STRING;
    mii.dwTypeData = name;

    SetMenuItemInfo(media_menu, (UINT_PTR)menus[MO_FIRST + drive], FALSE, &mii);
}

void
media_menu_update_floppy(int id)
{
    int i = FDD_FIRST + id;

    if (floppyfns[id][0] == 0x0000) {
	EnableMenuItem(menus[i], IDM_FLOPPY_EJECT | id, MF_BYCOMMAND | MF_GRAYED);
	EnableMenuItem(menus[i], IDM_FLOPPY_EXPORT_TO_86F | id, MF_BYCOMMAND | MF_GRAYED);
    } else {
	EnableMenuItem(menus[i], IDM_FLOPPY_EJECT | id, MF_BYCOMMAND | MF_ENABLED);
	EnableMenuItem(menus[i], IDM_FLOPPY_EXPORT_TO_86F | id, MF_BYCOMMAND | MF_ENABLED);
    }

    media_menu_set_name_floppy(id);
}

void
media_menu_update_cdrom(int id)
{
    int i = CDROM_FIRST + id;

    if (! cdrom[id].sound_on)
	CheckMenuItem(menus[i], IDM_CDROM_MUTE | id, MF_BYCOMMAND | MF_CHECKED);
    else
	CheckMenuItem(menus[i], IDM_CDROM_MUTE | id, MF_BYCOMMAND | MF_UNCHECKED);

    if (cdrom[id].host_drive == 200) {
	CheckMenuItem(menus[i], IDM_CDROM_IMAGE | id, MF_BYCOMMAND | MF_CHECKED);
	CheckMenuItem(menus[i], IDM_CDROM_EMPTY | id, MF_BYCOMMAND | MF_UNCHECKED);
    } else {
	cdrom[id].host_drive = 0;
	CheckMenuItem(menus[i], IDM_CDROM_IMAGE | id, MF_BYCOMMAND | MF_UNCHECKED);
	CheckMenuItem(menus[i], IDM_CDROM_EMPTY | id, MF_BYCOMMAND | MF_CHECKED);
    }

    if(cdrom[id].prev_host_drive == 0)
	EnableMenuItem(menus[i], IDM_CDROM_RELOAD | id, MF_BYCOMMAND | MF_GRAYED);
    else
	EnableMenuItem(menus[i], IDM_CDROM_RELOAD | id, MF_BYCOMMAND | MF_ENABLED);

    media_menu_set_name_cdrom(id);
}

void
media_menu_update_zip(int id)
{
    int i = ZIP_FIRST + id;

    if (zip_drives[id].image_path[0] == 0x0000)
	EnableMenuItem(menus[i], IDM_ZIP_EJECT | id, MF_BYCOMMAND | MF_GRAYED);
    else
	EnableMenuItem(menus[i], IDM_ZIP_EJECT | id, MF_BYCOMMAND | MF_ENABLED);

    if(zip_drives[id].prev_image_path[0] == 0x0000)
	EnableMenuItem(menus[i], IDM_ZIP_RELOAD | id, MF_BYCOMMAND | MF_GRAYED);
    else
	EnableMenuItem(menus[i], IDM_ZIP_RELOAD | id, MF_BYCOMMAND | MF_ENABLED);

    media_menu_set_name_zip(id);
}

void
media_menu_update_mo(int id)
{
    int i = MO_FIRST + id;

    if (mo_drives[id].image_path[0] == 0x0000)
	EnableMenuItem(menus[i], IDM_MO_EJECT | id, MF_BYCOMMAND | MF_GRAYED);
    else
	EnableMenuItem(menus[i], IDM_MO_EJECT | id, MF_BYCOMMAND | MF_ENABLED);

    if(mo_drives[id].prev_image_path[0] == 0x0000)
	EnableMenuItem(menus[i], IDM_MO_RELOAD | id, MF_BYCOMMAND | MF_GRAYED);
    else
	EnableMenuItem(menus[i], IDM_MO_RELOAD | id, MF_BYCOMMAND | MF_ENABLED);

    media_menu_set_name_mo(id);
}

static void
media_menu_load_submenus()
{
    memset(index_map, -1, sizeof(index_map));

    int curr = 0;

    for(int i = 0; i < FDD_NUM; i++) {
	menus[curr] = media_menu_load_resource(FLOPPY_SUBMENU_NAME);
	media_menu_set_ids(menus[curr++], i);
    }

    for(int i = 0; i < CDROM_NUM; i++) {
	menus[curr] = media_menu_load_resource(CDROM_SUBMENU_NAME);
	media_menu_set_ids(menus[curr++], i);
    }

    for(int i = 0; i < ZIP_NUM; i++) {
	menus[curr] = media_menu_load_resource(ZIP_SUBMENU_NAME);
	media_menu_set_ids(menus[curr++], i);
    }

    for(int i = 0; i < MO_NUM; i++) {
	menus[curr] = media_menu_load_resource(MO_SUBMENU_NAME);
	media_menu_set_ids(menus[curr++], i);
    }
}

static inline int
is_valid_fdd(int i)
{
    return fdd_get_type(i) != 0;
}

static inline int
is_valid_cdrom(int i)
{
    if ((cdrom[i].bus_type == CDROM_BUS_ATAPI) && !MACHINE_HAS_IDE)
	return 0;
    if ((cdrom[i].bus_type == CDROM_BUS_SCSI) && (scsi_card_current == 0))
	return 0;
    return cdrom[i].bus_type != 0;
}

static inline int
is_valid_zip(int i)
{
    if ((zip_drives[i].bus_type == ZIP_BUS_ATAPI) && !MACHINE_HAS_IDE)
	return 0;
    if ((zip_drives[i].bus_type == ZIP_BUS_SCSI) && (scsi_card_current == 0))
	return 0;
    return zip_drives[i].bus_type != 0;
}

static inline int
is_valid_mo(int i)
{
    if ((mo_drives[i].bus_type == MO_BUS_ATAPI) && !MACHINE_HAS_IDE)
	return 0;
    if ((mo_drives[i].bus_type == MO_BUS_SCSI) && (scsi_card_current == 0))
	return 0;
    return mo_drives[i].bus_type != 0;
}

void
media_menu_reset()
{
    /* Remove existing entries. */
    int c = GetMenuItemCount(media_menu);

    for(int i = 0; i < c; i++)
	RemoveMenu(media_menu, 0, MF_BYPOSITION);

    /* Add new ones. */
    int curr = 0;

    for(int i = 0; i < FDD_NUM; i++) {
	if(is_valid_fdd(i)) {
		AppendMenu(media_menu, MF_POPUP | MF_STRING, (UINT_PTR)menus[curr], L"Test");
		media_menu_update_floppy(i);
	}
	curr++;
    }

    for(int i = 0; i < CDROM_NUM; i++) {
	if(is_valid_cdrom(i)) {
		AppendMenu(media_menu, MF_POPUP | MF_STRING, (UINT_PTR)menus[curr], L"Test");
		media_menu_update_cdrom(i);
	}
	curr++;
    }

    for(int i = 0; i < ZIP_NUM; i++) {
	if(is_valid_zip(i)) {
		AppendMenu(media_menu, MF_POPUP | MF_STRING, (UINT_PTR)menus[curr], L"Test");
		media_menu_update_zip(i);
	}
	curr++;
    }

    for(int i = 0; i < MO_NUM; i++) {
	if(is_valid_mo(i)) {
		AppendMenu(media_menu, MF_POPUP | MF_STRING, (UINT_PTR)menus[curr], L"Test");
		media_menu_update_mo(i);
	}
	curr++;
    }
}

/* Initializes the Media menu in the main menu bar. */
static void
media_menu_main_init()
{
    HMENU hMenu;
    LPWSTR lpMenuName;

    hMenu = GetMenu(hwndMain);
    media_menu = CreatePopupMenu();

    /* Get the menu name */
    int len = GetMenuString(hMenu, IDM_MEDIA, NULL, 0, MF_BYCOMMAND);
    lpMenuName = malloc((len + 1) * sizeof(WCHAR));
    GetMenuString(hMenu, IDM_MEDIA, lpMenuName, len + 1, MF_BYCOMMAND);

    /* Replace the placeholder menu item */
    ModifyMenu(hMenu, IDM_MEDIA, MF_BYCOMMAND | MF_STRING | MF_POPUP, (UINT_PTR)media_menu, lpMenuName);

    /* Clean up */
    DrawMenuBar(hwndMain);
    free(lpMenuName);
}

void
media_menu_init()
{
    /* Initialize the main menu bar menu */
    media_menu_main_init();

    /* Initialize the dummy status bar menu. */
    stbar_menu = CreateMenu();
    AppendMenu(stbar_menu, MF_POPUP, (UINT_PTR)media_menu, NULL);

    /* Load the submenus for each drive type. */
    media_menu_load_submenus();

    /* Populate the Media and status bar menus. */
    media_menu_reset();
}

int
media_menu_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int id = 0, ret = 0, wp = 0;

    id = LOWORD(wParam) & 0x00ff;

    switch (LOWORD(wParam) & 0xff00) {
	case IDM_FLOPPY_IMAGE_NEW:
		NewFloppyDialogCreate(hwnd, id, 0);
		break;

	case IDM_FLOPPY_IMAGE_EXISTING_WP:
		wp = 1;
		/* FALLTHROUGH */
	case IDM_FLOPPY_IMAGE_EXISTING:
		ret = file_dlg_w_st(hwnd, IDS_2109, floppyfns[id], 0);
		if (! ret) {
			floppy_mount(id, wopenfilestring, wp);
		}
		break;

	case IDM_FLOPPY_EJECT:
		floppy_eject(id);
		break;

	case IDM_FLOPPY_EXPORT_TO_86F:
		ret = file_dlg_w_st(hwnd, IDS_2076, floppyfns[id], 1);
		if (! ret) {
			plat_pause(1);
			ret = d86f_export(id, wopenfilestring);
			if (!ret)
				ui_msgbox_header(MBX_ERROR, (wchar_t *) IDS_4108, (wchar_t *) IDS_4115);
			plat_pause(0);
		}
		break;

	case IDM_CDROM_MUTE:
		cdrom[id].sound_on ^= 1;
		config_save();
		media_menu_update_cdrom(id);
		sound_cd_thread_reset();
		break;

	case IDM_CDROM_EMPTY:
		cdrom_eject(id);
		break;

	case IDM_CDROM_RELOAD:
		cdrom_reload(id);
		break;

	case IDM_CDROM_IMAGE:
		if (!file_dlg_w_st(hwnd, IDS_2075, cdrom[id].image_path, 0)) {
			cdrom_mount(id, wopenfilestring);
		}
		break;

	case IDM_ZIP_IMAGE_NEW:
		NewFloppyDialogCreate(hwnd, id | 0x80, 0);	/* NewZIPDialogCreate */
		break;

	case IDM_ZIP_IMAGE_EXISTING_WP:
		wp = 1;
		/* FALLTHROUGH */
	case IDM_ZIP_IMAGE_EXISTING:
		ret = file_dlg_w_st(hwnd, IDS_2058, zip_drives[id].image_path, 0);
		if (! ret)
			zip_mount(id, wopenfilestring, wp);
		break;

	case IDM_ZIP_EJECT:
		zip_eject(id);
		break;

	case IDM_ZIP_RELOAD:
		zip_reload(id);
		break;

	case IDM_MO_IMAGE_NEW:
		NewFloppyDialogCreate(hwnd, id | 0x100, 0);	/* NewZIPDialogCreate */
		break;

	case IDM_MO_IMAGE_EXISTING_WP:
		wp = 1;
		/* FALLTHROUGH */
	case IDM_MO_IMAGE_EXISTING:
		ret = file_dlg_w_st(hwnd, IDS_2116, mo_drives[id].image_path, 0);
		if (! ret)
			mo_mount(id, wopenfilestring, wp);
		break;

	case IDM_MO_EJECT:
		mo_eject(id);
		break;

	case IDM_MO_RELOAD:
		mo_reload(id);
		break;

	default:
		return(0);
    }

    return(1);
}

HMENU
media_menu_get_floppy(int id)
{
    return menus[FDD_FIRST + id];
}

HMENU
media_menu_get_cdrom(int id)
{
    return menus[CDROM_FIRST + id];
}

HMENU
media_menu_get_zip(int id)
{
    return menus[ZIP_FIRST + id];
}

HMENU
media_menu_get_mo(int id)
{
    return menus[MO_FIRST + id];
}
