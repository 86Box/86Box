#define UNICODE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>
#include <CommCtrl.h>
#include <86box/86box.h>
#include <86box/resource.h>
#include <86box/ui.h>
#include <86box/win.h>

HWND			hwndRebar;
static HWND		hwndToolbar;
static HIMAGELIST	hImageList;

static TBBUTTON buttons[] = {
    { 0,	IDM_ACTION_PAUSE,		TBSTATE_ENABLED,	BTNS_BUTTON,	{ 0 }, 0, 0 },	// Pause
    { 0,	0,				TBSTATE_INDETERMINATE,	BTNS_SEP,	{ 0 }, 0, 0 },
    { 0,	IDM_ACTION_RESET_CAD,		TBSTATE_ENABLED,	BTNS_BUTTON,	{ 0 }, 0, 0 },	// Ctrl+Alt+Del
    { 0,	IDM_ACTION_CTRL_ALT_ESC,	TBSTATE_ENABLED,	BTNS_BUTTON,	{ 0 }, 0, 0 },	// Ctrl+Alt+Esc
    { 0,	IDM_ACTION_HRESET,		TBSTATE_ENABLED,	BTNS_BUTTON,	{ 0 }, 0, 0 },	// Hard reset
    { 0,	0,				TBSTATE_INDETERMINATE,	BTNS_BUTTON,	{ 0 }, 0, 0 },	// ACPI shutdown
    { 0,	0,				TBSTATE_INDETERMINATE,	BTNS_SEP,	{ 0 }, 0, 0 },
    { 0,	IDM_CONFIG,			TBSTATE_ENABLED,	BTNS_BUTTON,	{ 0 }, 0, 0 }	// Settings
};

void
ToolBarCreate(HWND hwndParent, HINSTANCE hInst)
{
    REBARINFO rbi = { 0 };
    REBARBANDINFO rbbi = { 0 };
    int btnSize;

    // Create the toolbar.
    hwndToolbar = CreateWindowEx(WS_EX_PALETTEWINDOW, TOOLBARCLASSNAME, NULL,
				WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN |
				WS_CLIPSIBLINGS | TBSTYLE_TOOLTIPS |
				TBSTYLE_FLAT | CCS_TOP | BTNS_AUTOSIZE |
				CCS_NOPARENTALIGN | CCS_NORESIZE | 
				CCS_NODIVIDER,
				0, 0, 0, 0,
				hwndParent, NULL, hInst, NULL);

    // Create the image list.
    hImageList = ImageList_Create(win_get_system_metrics(SM_CXSMICON, dpi),
				  win_get_system_metrics(SM_CYSMICON, dpi),
				  ILC_MASK | ILC_COLOR32, 1, 1);

    ImageList_AddIcon(hImageList, hIcon[241]);

    SendMessage(hwndToolbar, TB_SETIMAGELIST, 0, (LPARAM) hImageList);

    // Add buttons.
    SendMessage(hwndToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessage(hwndToolbar, TB_ADDBUTTONS, sizeof(buttons) / sizeof(TBBUTTON), (LPARAM) &buttons);

    // Autosize the toolbar and determine its size.
    btnSize = LOWORD(SendMessage(hwndToolbar, TB_GETBUTTONSIZE, 0,0));

    // Create the containing Rebar.
    hwndRebar = CreateWindowEx(0, REBARCLASSNAME, NULL,
				WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | 
				WS_CLIPCHILDREN | RBS_VARHEIGHT |
				RBS_BANDBORDERS | CCS_NODIVIDER |
				CCS_NOPARENTALIGN,
				0, 0, 0, 0,
				hwndParent, NULL, hInst, NULL);

    // Create and send the REBARINFO structure.
    rbi.cbSize = sizeof(rbi);
    SendMessage(hwndRebar, RB_SETBARINFO, 0, (LPARAM)&rbi);

    // Add the toolbar to the rebar.
    rbbi.cbSize = sizeof(rbbi);
    rbbi.fMask = RBBIM_CHILD | RBBIM_CHILDSIZE;
    rbbi.hwndChild = hwndToolbar;
    rbbi.cxMinChild = 0;
    rbbi.cyMinChild = btnSize;
    SendMessage(hwndRebar, RB_INSERTBAND, -1, (LPARAM)&rbbi);
    SendMessage(hwndRebar, RB_MAXIMIZEBAND, 0, 0);

    ShowWindow(hwndRebar, TRUE);

    return;
}