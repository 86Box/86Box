/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../config.h"
#include "../device.h"
#include "../game/gameport.h"
#include "../plat.h"
#include "win.h"


static int joystick_nr;
static int joystick_config_type;
#define AXIS_STRINGS_MAX 3
static char *axis_strings[AXIS_STRINGS_MAX] = {"X Axis", "Y Axis", "Z Axis"};

static uint8_t joystickconfig_changed = 0;


static void rebuild_axis_button_selections(HWND hdlg)
{
        int id = IDC_CONFIG_BASE + 2;
        HWND h;
        int joystick;
        int c, d;
	char s[269];

        h = GetDlgItem(hdlg, IDC_CONFIG_BASE);
        joystick = SendMessage(h, CB_GETCURSEL, 0, 0);

        for (c = 0; c < joystick_get_axis_count(joystick_config_type); c++)
        {
                int sel = c;
                                        
                h = GetDlgItem(hdlg, id);
                SendMessage(h, CB_RESETCONTENT, 0, 0);

                if (joystick)
                {
                        for (d = 0; d < plat_joystick_state[joystick-1].nr_axes; d++)
                        {
                                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)plat_joystick_state[joystick-1].axis[d].name);
                                if (c < AXIS_STRINGS_MAX)
                                {
                                        if (!stricmp(axis_strings[c], plat_joystick_state[joystick-1].axis[d].name))
                                                sel = d;
                                }
                        }
                        for (d = 0; d < plat_joystick_state[joystick-1].nr_povs; d++)
                        {
                                sprintf(s, "%s (X axis)", plat_joystick_state[joystick-1].pov[d].name);
                                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)s);
                                sprintf(s, "%s (Y axis)", plat_joystick_state[joystick-1].pov[d].name);
                                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)s);
                        }
						for (d = 0; d < plat_joystick_state[joystick - 1].nr_sliders; d++)
                        {
                                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)plat_joystick_state[joystick - 1].slider[d].name);
                        }
                        SendMessage(h, CB_SETCURSEL, sel, 0);
                        EnableWindow(h, TRUE);
                }
                else
                        EnableWindow(h, FALSE);
                                        
                id += 2;
        }

        for (c = 0; c < joystick_get_button_count(joystick_config_type); c++)
        {
                h = GetDlgItem(hdlg, id);
                SendMessage(h, CB_RESETCONTENT, 0, 0);

                if (joystick)
                {
                        for (d = 0; d < plat_joystick_state[joystick-1].nr_buttons; d++)
                                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)plat_joystick_state[joystick-1].button[d].name);
                        SendMessage(h, CB_SETCURSEL, c, 0);
                        EnableWindow(h, TRUE);
                }
                else
                        EnableWindow(h, FALSE);

                id += 2;
        }

        for (c = 0; c < joystick_get_pov_count(joystick_config_type)*2; c++)
        {
                int sel = c;
                                        
                h = GetDlgItem(hdlg, id);
                SendMessage(h, CB_RESETCONTENT, 0, 0);

                if (joystick)
                {
                        for (d = 0; d < plat_joystick_state[joystick-1].nr_povs; d++)
                        {
                                sprintf(s, "%s (X axis)", plat_joystick_state[joystick-1].pov[d].name);
                                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)s);
                                sprintf(s, "%s (Y axis)", plat_joystick_state[joystick-1].pov[d].name);
                                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)s);
                        }
                        for (d = 0; d < plat_joystick_state[joystick-1].nr_axes; d++)
                        {
                                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)plat_joystick_state[joystick-1].axis[d].name);
                        }
                        SendMessage(h, CB_SETCURSEL, sel, 0);
                        EnableWindow(h, TRUE);
                }
                else
                        EnableWindow(h, FALSE);
                                        
                id += 2;
        }

}

static int get_axis(HWND hdlg, int id)
{
        HWND h = GetDlgItem(hdlg, id);
        int axis_sel = SendMessage(h, CB_GETCURSEL, 0, 0);
        int nr_axes = plat_joystick_state[joystick_state[joystick_nr].plat_joystick_nr-1].nr_axes;
		int nr_povs = plat_joystick_state[joystick_state[joystick_nr].plat_joystick_nr - 1].nr_povs;
        int nr_sliders = plat_joystick_state[joystick_state[joystick_nr].plat_joystick_nr - 1].nr_sliders;
		
        if (axis_sel < nr_axes)
                return axis_sel;
        
        axis_sel -= nr_axes;
        if (axis_sel < nr_povs * 2)
        {
                if (axis_sel & 1)
                        return POV_Y | (axis_sel >> 1);
                else
                        return POV_X | (axis_sel >> 1);
        }
        axis_sel -= nr_povs;

        return SLIDER | (axis_sel >> 1);
}

static int get_pov(HWND hdlg, int id)
{
        HWND h = GetDlgItem(hdlg, id);
        int axis_sel = SendMessage(h, CB_GETCURSEL, 0, 0);
        int nr_povs = plat_joystick_state[joystick_state[joystick_nr].plat_joystick_nr-1].nr_povs*2;

        if (axis_sel < nr_povs)
        {
                if (axis_sel & 1)
                        return POV_Y | (axis_sel >> 1);
                else
                        return POV_X | (axis_sel >> 1);
        }
        
        return axis_sel - nr_povs;
}

#if defined(__amd64__) || defined(__aarch64__)
static LRESULT CALLBACK
#else
static BOOL CALLBACK
#endif
joystickconfig_dlgproc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND h;
	int c;
	int id;
	int joystick;
	int nr_axes;
	int nr_povs;
	int nr_sliders;
	int mapping;

        switch (message)
        {
                case WM_INITDIALOG:
                {
                        h = GetDlgItem(hdlg, IDC_CONFIG_BASE);
                        id = IDC_CONFIG_BASE + 2;
                        joystick = joystick_state[joystick_nr].plat_joystick_nr;

                        SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"None");

                        for (c = 0; c < joysticks_present; c++)
                                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)plat_joystick_state[c].name);
                        
                        SendMessage(h, CB_SETCURSEL, joystick, 0);

                        rebuild_axis_button_selections(hdlg);
                                
                        if (joystick_state[joystick_nr].plat_joystick_nr)
                        {
                                nr_axes = plat_joystick_state[joystick-1].nr_axes;
                                nr_povs = plat_joystick_state[joystick-1].nr_povs;
								nr_sliders = plat_joystick_state[joystick - 1].nr_sliders;
								
                                for (c = 0; c < joystick_get_axis_count(joystick_config_type); c++)
                                {
                                        int mapping = joystick_state[joystick_nr].axis_mapping[c];
                                        
                                        h = GetDlgItem(hdlg, id);
                                        if (mapping & POV_X)
                                                SendMessage(h, CB_SETCURSEL, nr_axes + (mapping & 3)*2, 0);
                                        else if (mapping & POV_Y)
                                                SendMessage(h, CB_SETCURSEL, nr_axes + (mapping & 3)*2 + 1, 0);
                                        else if (mapping & SLIDER)
												SendMessage(h, CB_SETCURSEL, nr_axes + nr_povs * 2 + (mapping & 3), 0);										
										else
                                                SendMessage(h, CB_SETCURSEL, mapping, 0);
                                        id += 2;
                                } 
                                for (c = 0; c < joystick_get_button_count(joystick_config_type); c++)
                                {
                                        h = GetDlgItem(hdlg, id);
                                        SendMessage(h, CB_SETCURSEL, joystick_state[joystick_nr].button_mapping[c], 0);
                                        id += 2;
                                }
                                for (c = 0; c < joystick_get_pov_count(joystick_config_type); c++)
                                {
                                        h = GetDlgItem(hdlg, id);
                                        mapping = joystick_state[joystick_nr].pov_mapping[c][0];
                                        if (mapping & POV_X)
                                                SendMessage(h, CB_SETCURSEL, (mapping & 3)*2, 0);
                                        else if (mapping & POV_Y)
                                                SendMessage(h, CB_SETCURSEL, (mapping & 3)*2 + 1, 0);
                                        else
                                                SendMessage(h, CB_SETCURSEL, mapping + nr_povs*2, 0);
                                        id += 2;
                                        h = GetDlgItem(hdlg, id);
                                        mapping = joystick_state[joystick_nr].pov_mapping[c][1];
                                        if (mapping & POV_X)
                                                SendMessage(h, CB_SETCURSEL, (mapping & 3)*2, 0);
                                        else if (mapping & POV_Y)
                                                SendMessage(h, CB_SETCURSEL, (mapping & 3)*2 + 1, 0);
                                        else
                                                SendMessage(h, CB_SETCURSEL, mapping + nr_povs*2, 0);
                                        id += 2;
                                }
                        }
                }
                return TRUE;
                
                case WM_COMMAND:
                switch (LOWORD(wParam))
                {
                        case IDC_CONFIG_BASE:
                        if (HIWORD(wParam) == CBN_SELCHANGE)
                                rebuild_axis_button_selections(hdlg);
                        break;
                        
                        case IDOK:
                        {
                                id = IDC_CONFIG_BASE + 2;
                                                                
                                h = GetDlgItem(hdlg, IDC_CONFIG_BASE);
                                joystick_state[joystick_nr].plat_joystick_nr = SendMessage(h, CB_GETCURSEL, 0, 0);
                                
                                if (joystick_state[joystick_nr].plat_joystick_nr)
                                {
                                        for (c = 0; c < joystick_get_axis_count(joystick_config_type); c++)
                                        {
                                                joystick_state[joystick_nr].axis_mapping[c] = get_axis(hdlg, id);
                                                id += 2;
                                        }                               
                                        for (c = 0; c < joystick_get_button_count(joystick_config_type); c++)
                                        {
                                                h = GetDlgItem(hdlg, id);
                                                joystick_state[joystick_nr].button_mapping[c] = SendMessage(h, CB_GETCURSEL, 0, 0);
                                                id += 2;
                                        }
                                        for (c = 0; c < joystick_get_button_count(joystick_config_type); c++)
                                        {
                                                h = GetDlgItem(hdlg, id);
                                                joystick_state[joystick_nr].pov_mapping[c][0] = get_pov(hdlg, id);
                                                id += 2;
                                                h = GetDlgItem(hdlg, id);
                                                joystick_state[joystick_nr].pov_mapping[c][1] = get_pov(hdlg, id);
                                                id += 2;
                                        }
                                }
                        }
			joystickconfig_changed = 1;
                        EndDialog(hdlg, 0);
                        return TRUE;
                        case IDCANCEL:
			joystickconfig_changed = 0;
                        EndDialog(hdlg, 0);
                        return TRUE;
                }
                break;
        }
        return FALSE;
}

uint8_t joystickconfig_open(HWND hwnd, int joy_nr, int type)
{
        uint16_t *data_block = malloc(16384);
        uint16_t *data;
        DLGTEMPLATE *dlg = (DLGTEMPLATE *)data_block;
        DLGITEMTEMPLATE *item;
        int y = 10;
        int id = IDC_CONFIG_BASE;
        int c;
	char s[269];

	joystickconfig_changed = 0;
        
        joystick_nr = joy_nr;
        joystick_config_type = type;

        memset(data_block, 0, 4096);
        
        dlg->style = DS_SETFONT | DS_MODALFRAME | DS_FIXEDSYS | WS_POPUP | WS_CAPTION | WS_SYSMENU;
        dlg->x  = 10;
        dlg->y  = 10;
        dlg->cx = 220;
        dlg->cy = 70;
        
        data = (uint16_t *)(dlg + 1);
        
        *data++ = 0; /*no menu*/
        *data++ = 0; /*predefined dialog box class*/
        data += MultiByteToWideChar(CP_ACP, 0, "Device Configuration", -1, data, 50);

        *data++ = 8; /*Point*/
        data += MultiByteToWideChar(CP_ACP, 0, "MS Sans Serif", -1, data, 50);
        
        if (((uintptr_t)data) & 2)
                data++;


        /*Combo box*/
        item = (DLGITEMTEMPLATE *)data;
        item->x = 70;
        item->y = y;
        item->id = id++;
                
        item->cx = 140;
        item->cy = 150;

        item->style = WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL;

        data = (uint16_t *)(item + 1);
        *data++ = 0xFFFF;
        *data++ = 0x0085;    /* combo box class */

        data += MultiByteToWideChar(CP_ACP, 0, "Device", -1, data, 256);
        *data++ = 0;              /* no creation data */
                        
        if (((uintptr_t)data) & 2)
                data++;

        /*Static text*/
        item = (DLGITEMTEMPLATE *)data;
        item->x = 10;
        item->y = y;
        item->id = id++;
                
        item->cx = 60;
        item->cy = 15;

        item->style = WS_CHILD | WS_VISIBLE;

        data = (uint16_t *)(item + 1);
        *data++ = 0xFFFF;
        *data++ = 0x0082;    /* static class */

        data += MultiByteToWideChar(CP_ACP, 0, "Device :", -1, data, 256);
        *data++ = 0;              /* no creation data */
                        
        if (((uintptr_t)data) & 2)
                data++;

        y += 20;


        for (c = 0; c < joystick_get_axis_count(type); c++)
        {
                /*Combo box*/
                item = (DLGITEMTEMPLATE *)data;
                item->x = 70;
                item->y = y;
                item->id = id++;
                
                item->cx = 140;
                item->cy = 150;

                item->style = WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL;

                data = (uint16_t *)(item + 1);
                *data++ = 0xFFFF;
                *data++ = 0x0085;    /* combo box class */

                data += MultiByteToWideChar(CP_ACP, 0, joystick_get_axis_name(type, c), -1, data, 256);
                *data++ = 0;              /* no creation data */
                        
                if (((uintptr_t)data) & 2)
                        data++;

                /*Static text*/
                item = (DLGITEMTEMPLATE *)data;
                item->x = 10;
                item->y = y;
                item->id = id++;
                
                item->cx = 60;
                item->cy = 15;

                item->style = WS_CHILD | WS_VISIBLE;

                data = (uint16_t *)(item + 1);
                *data++ = 0xFFFF;
                *data++ = 0x0082;    /* static class */

                data += MultiByteToWideChar(CP_ACP, 0, joystick_get_axis_name(type, c), -1, data, 256);
                *data++ = 0;              /* no creation data */
                        
                if (((uintptr_t)data) & 2)
                        data++;

                y += 20;
        }                

        for (c = 0; c < joystick_get_button_count(type); c++)
        {
                /*Combo box*/
                item = (DLGITEMTEMPLATE *)data;
                item->x = 70;
                item->y = y;
                item->id = id++;
                
                item->cx = 140;
                item->cy = 150;

                item->style = WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL;

                data = (uint16_t *)(item + 1);
                *data++ = 0xFFFF;
                *data++ = 0x0085;    /* combo box class */

                data += MultiByteToWideChar(CP_ACP, 0, joystick_get_button_name(type, c), -1, data, 256);
                *data++ = 0;              /* no creation data */
                        
                if (((uintptr_t)data) & 2)
                        data++;

                /*Static text*/
                item = (DLGITEMTEMPLATE *)data;
                item->x = 10;
                item->y = y;
                item->id = id++;
                
                item->cx = 60;
                item->cy = 15;

                item->style = WS_CHILD | WS_VISIBLE;

                data = (uint16_t *)(item + 1);
                *data++ = 0xFFFF;
                *data++ = 0x0082;    /* static class */

                data += MultiByteToWideChar(CP_ACP, 0, joystick_get_button_name(type, c), -1, data, 256);
                *data++ = 0;              /* no creation data */
                        
                if (((uintptr_t)data) & 2)
                        data++;

                y += 20;
        }                
 
        for (c = 0; c < joystick_get_pov_count(type)*2; c++)
        {
                /*Combo box*/
                item = (DLGITEMTEMPLATE *)data;
                item->x = 70;
                item->y = y;
                item->id = id++;
                
                item->cx = 140;
                item->cy = 150;

                item->style = WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL;

                data = (uint16_t *)(item + 1);
                *data++ = 0xFFFF;
                *data++ = 0x0085;    /* combo box class */

                if (c & 1)
                        sprintf(s, "%s (Y axis)", joystick_get_pov_name(type, c/2));
                else
                        sprintf(s, "%s (X axis)", joystick_get_pov_name(type, c/2));
                data += MultiByteToWideChar(CP_ACP, 0, s, -1, data, 256);
                *data++ = 0;              /* no creation data */
                        
                if (((uintptr_t)data) & 2)
                        data++;

                /*Static text*/
                item = (DLGITEMTEMPLATE *)data;
                item->x = 10;
                item->y = y;
                item->id = id++;
                
                item->cx = 60;
                item->cy = 15;

                item->style = WS_CHILD | WS_VISIBLE;

                data = (uint16_t *)(item + 1);
                *data++ = 0xFFFF;
                *data++ = 0x0082;    /* static class */

                data += MultiByteToWideChar(CP_ACP, 0, s, -1, data, 256);
                *data++ = 0;              /* no creation data */
                        
                if (((uintptr_t)data) & 2)
                        data++;

                y += 20;
        }

        dlg->cdit = (id - IDC_CONFIG_BASE) + 2;

        item = (DLGITEMTEMPLATE *)data;
        item->x = 20;
        item->y = y;
        item->cx = 50;
        item->cy = 14;
        item->id = IDOK;  /* OK button identifier */
        item->style = WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON;

        data = (uint16_t *)(item + 1);
        *data++ = 0xFFFF;
        *data++ = 0x0080;    /* button class */

        data += MultiByteToWideChar(CP_ACP, 0, "OK", -1, data, 50);
        *data++ = 0;              /* no creation data */

        if (((uintptr_t)data) & 2)
                data++;
                
        item = (DLGITEMTEMPLATE *)data;
        item->x = 80;
        item->y = y;
        item->cx = 50;
        item->cy = 14;
        item->id = IDCANCEL;  /* OK button identifier */
        item->style = WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON;

        data = (uint16_t *)(item + 1);
        *data++ = 0xFFFF;
        *data++ = 0x0080;    /* button class */

        data += MultiByteToWideChar(CP_ACP, 0, "Cancel", -1, data, 50);
        *data++ = 0;              /* no creation data */

        dlg->cy = y + 20;

        DialogBoxIndirect(hinstance, dlg, hwnd, joystickconfig_dlgproc);

        free(data_block);

	return joystickconfig_changed;
}
