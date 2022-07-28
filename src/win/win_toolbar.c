#define UNICODE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>
#include <CommCtrl.h>
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/resource.h>
#include <86box/ui.h>
#include <86box/video.h>
#include <86box/win.h>

HWND              hwndRebar          = NULL;
static HWND       hwndToolbar        = NULL;
static HIMAGELIST hImageList         = NULL;
static wchar_t    wTitle[512]        = { 0 };
static WNDPROC    pOriginalProcedure = NULL;

enum image_index {
    RUN,
    PAUSE,
    CTRL_ALT_DEL,
    CTRL_ALT_ESC,
    HARD_RESET,
    ACPI_SHUTDOWN,
    SETTINGS
};

void
ToolBarLoadIcons()
{
    if (!hwndToolbar)
        return;

    if (hImageList)
        ImageList_Destroy(hImageList);

    hImageList = ImageList_Create(win_get_system_metrics(SM_CXSMICON, dpi),
                                  win_get_system_metrics(SM_CYSMICON, dpi),
                                  ILC_MASK | ILC_COLOR32, 1, 1);

    // The icons must be loaded in the same order as the `image_index`
    // enumeration above.

    ImageList_AddIcon(hImageList, hIcon[200]); // Run
    ImageList_AddIcon(hImageList, hIcon[201]); // Pause
    ImageList_AddIcon(hImageList, hIcon[202]); // Ctrl+Alt+Delete
    ImageList_AddIcon(hImageList, hIcon[203]); // Ctrl+Alt+Esc
    ImageList_AddIcon(hImageList, hIcon[204]); // Hard reset
    ImageList_AddIcon(hImageList, hIcon[205]); // ACPI shutdown
    ImageList_AddIcon(hImageList, hIcon[206]); // Settings

    SendMessage(hwndToolbar, TB_SETIMAGELIST, 0, (LPARAM) hImageList);
}

int
ToolBarProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
        case WM_NOTIFY:
            switch (((LPNMHDR) lParam)->code) {
                case TTN_GETDISPINFO:
                    {
                        LPTOOLTIPTEXT lpttt = (LPTOOLTIPTEXT) lParam;

                        // Set the instance of the module that contains the resource.
                        lpttt->hinst = hinstance;

                        uintptr_t idButton = lpttt->hdr.idFrom;

                        switch (idButton) {
                            case IDM_ACTION_PAUSE:
                                if (dopause)
                                    lpttt->lpszText = MAKEINTRESOURCE(IDS_2154);
                                else
                                    lpttt->lpszText = MAKEINTRESOURCE(IDS_2155);
                                break;

                            case IDM_ACTION_RESET_CAD:
                                lpttt->lpszText = MAKEINTRESOURCE(IDS_2156);
                                break;

                            case IDM_ACTION_CTRL_ALT_ESC:
                                lpttt->lpszText = MAKEINTRESOURCE(IDS_2157);
                                break;

                            case IDM_ACTION_HRESET:
                                lpttt->lpszText = MAKEINTRESOURCE(IDS_2158);
                                break;

                            case IDM_CONFIG:
                                lpttt->lpszText = MAKEINTRESOURCE(IDS_2160);
                                break;
                        }

                        return TRUE;
                    }
            }
    }

    return (CallWindowProc(pOriginalProcedure, hwnd, message, wParam, lParam));
}

void
ToolBarUpdatePause(int pause)
{
    TBBUTTONINFO tbbi;

    tbbi.cbSize = sizeof(tbbi);
    tbbi.dwMask = TBIF_IMAGE;
    tbbi.iImage = pause ? RUN : PAUSE;

    SendMessage(hwndToolbar, TB_SETBUTTONINFO, IDM_ACTION_PAUSE, (LPARAM) &tbbi);
}

static TBBUTTON buttons[] = {
    {PAUSE,          IDM_ACTION_PAUSE,        TBSTATE_ENABLED,       BTNS_BUTTON, { 0 }, 0, 0},
    { HARD_RESET,    IDM_ACTION_HRESET,       TBSTATE_ENABLED,       BTNS_BUTTON, { 0 }, 0, 0},
    { ACPI_SHUTDOWN, 0,                       TBSTATE_HIDDEN,        BTNS_BUTTON, { 0 }, 0, 0},
    { 0,             0,                       TBSTATE_INDETERMINATE, BTNS_SEP,    { 0 }, 0, 0},
    { CTRL_ALT_DEL,  IDM_ACTION_RESET_CAD,    TBSTATE_ENABLED,       BTNS_BUTTON, { 0 }, 0, 0},
    { CTRL_ALT_ESC,  IDM_ACTION_CTRL_ALT_ESC, TBSTATE_ENABLED,       BTNS_BUTTON, { 0 }, 0, 0},
    { 0,             0,                       TBSTATE_INDETERMINATE, BTNS_SEP,    { 0 }, 0, 0},
    { SETTINGS,      IDM_CONFIG,              TBSTATE_ENABLED,       BTNS_BUTTON, { 0 }, 0, 0}
};

void
ToolBarCreate(HWND hwndParent, HINSTANCE hInst)
{
    REBARINFO     rbi  = { 0 };
    REBARBANDINFO rbbi = { 0 };
    int           btnSize;

    // Create the toolbar.
    hwndToolbar = CreateWindowEx(WS_EX_PALETTEWINDOW, TOOLBARCLASSNAME, NULL,
                                 WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | TBSTYLE_TOOLTIPS | TBSTYLE_FLAT | CCS_TOP | BTNS_AUTOSIZE | CCS_NOPARENTALIGN | CCS_NORESIZE | CCS_NODIVIDER,
                                 0, 0, 0, 0,
                                 hwndParent, NULL, hInst, NULL);

    ToolBarLoadIcons();

    // Add buttons.
    SendMessage(hwndToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessage(hwndToolbar, TB_ADDBUTTONS, sizeof(buttons) / sizeof(TBBUTTON), (LPARAM) &buttons);

    // Autosize the toolbar and determine its size.
    btnSize = LOWORD(SendMessage(hwndToolbar, TB_GETBUTTONSIZE, 0, 0));

    // Replace the original procedure with ours.
    pOriginalProcedure = (WNDPROC) GetWindowLongPtr(hwndToolbar, GWLP_WNDPROC);
    SetWindowLongPtr(hwndToolbar, GWLP_WNDPROC, (LONG_PTR) &ToolBarProcedure);

    // Make sure the Pause button is in the correct state.
    ToolBarUpdatePause(dopause);

    // Create the containing Rebar.
    hwndRebar = CreateWindowEx(0, REBARCLASSNAME, NULL,
                               WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | RBS_VARHEIGHT | CCS_NODIVIDER | CCS_NOPARENTALIGN,
                               0, 0, scrnsz_x, 0,
                               hwndParent, NULL, hInst, NULL);

    // Create and send the REBARINFO structure.
    rbi.cbSize = sizeof(rbi);
    SendMessage(hwndRebar, RB_SETBARINFO, 0, (LPARAM) &rbi);

    // Add the toolbar to the rebar.
    rbbi.cbSize     = sizeof(rbbi);
    rbbi.fMask      = RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_STYLE;
    rbbi.hwndChild  = hwndToolbar;
    rbbi.cxMinChild = 0;
    rbbi.cyMinChild = btnSize;
    rbbi.fStyle     = RBBS_NOGRIPPER;
    SendMessage(hwndRebar, RB_INSERTBAND, -1, (LPARAM) &rbbi);

    // Add a label for machine information.
    rbbi.fMask  = RBBIM_TEXT | RBBIM_STYLE;
    rbbi.lpText = TEXT("Test");
    rbbi.fStyle = RBBS_NOGRIPPER;
    SendMessage(hwndRebar, RB_INSERTBAND, -1, (LPARAM) &rbbi);

    SendMessage(hwndRebar, RB_MAXIMIZEBAND, 0, 0);
    ShowWindow(hwndRebar, TRUE);

    return;
}

wchar_t *
ui_window_title(wchar_t *s)
{
    REBARBANDINFO rbbi = { 0 };
    if (!video_fullscreen) {
        if (s != NULL) {
            wcsncpy(wTitle, s, sizeof_w(wTitle) - 1);
        } else
            s = wTitle;

        rbbi.cbSize = sizeof(rbbi);
        rbbi.fMask  = RBBIM_TEXT;
        rbbi.lpText = s;
        SendMessage(hwndRebar, RB_SETBANDINFO, 1, (LPARAM) &rbbi);
    } else {
        if (s == NULL)
            s = wTitle;
    }

    return (s);
}
