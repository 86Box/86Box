/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Windows device configuration dialog implementation.
 *
 * Version:	@(#)win_devconf.c	1.0.10	2017/11/25
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../config.h"
#include "../device.h"
#include "../plat.h"
#include "../plat_midi.h"
#include "../ui.h"
#include "win.h"
#include <windowsx.h>


static device_t *config_device;

static uint8_t deviceconfig_changed = 0;


static BOOL CALLBACK
deviceconfig_dlgproc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND h;

	int val_int;
	int id;
	int c;
    int num;
	int changed;
    int cid;
	device_config_t *config;
	char s[80];
	wchar_t ws[512];
	LPTSTR lptsTemp;

        switch (message)
        {
                case WM_INITDIALOG:
                {
                        id = IDC_CONFIG_BASE;
                        config = config_device->config;

			lptsTemp = (LPTSTR) malloc(512);

                        while (config->type != -1)
                        {
                                device_config_selection_t *selection = config->selection;
                                h = GetDlgItem(hdlg, id);
                                
                                switch (config->type)
                                {
                                        case CONFIG_BINARY:
                                        val_int = config_get_int(config_device->name, config->name, config->default_int);
                                        
                                        SendMessage(h, BM_SETCHECK, val_int, 0);
                                                
                                        id++;
                                        break;

                                        case CONFIG_SELECTION:
                                        val_int = config_get_int(config_device->name, config->name, config->default_int);
                                        
                                        c = 0;
                                        while (selection->description[0])
                                        {
						mbstowcs(lptsTemp, selection->description, strlen(selection->description) + 1);
                                                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)lptsTemp);
                                                if (val_int == selection->value)
                                                        SendMessage(h, CB_SETCURSEL, c, 0);
                                                selection++;
                                                c++;
                                        }
                                        
                                        id += 2;
                                        break;

                                        case CONFIG_MIDI:
                                        val_int = config_get_int(config_device->name, config->name, config->default_int);
                                        
                                        num  = plat_midi_get_num_devs();
                                        for (c = 0; c < num; c++)
                                        {
                                                plat_midi_get_dev_name(c, s);
						mbstowcs(lptsTemp, s, strlen(s) + 1);
                                                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)lptsTemp);
                                                if (val_int == c)
                                                        SendMessage(h, CB_SETCURSEL, c, 0);
                                        }
                                        
                                        id += 2;
                                        break;

                                        case CONFIG_SPINNER:
                                        val_int = config_get_int(config_device->name, config->name, config->default_int);

                                        _swprintf(ws, L"%i", val_int);
                                        SendMessage(h, WM_SETTEXT, 0, (LPARAM)ws);

                                        id += 2;
                                        break;

                                        case CONFIG_FNAME:
                                        {
                                                wchar_t* str = config_get_wstring(config_device->name, config->name, 0);
                                                if (str)
                                                        SendMessage(h, WM_SETTEXT, 0, (LPARAM)str);
                                                id += 3;
                                        }
                                        break;

                                        case CONFIG_HEX16:
                                        val_int = config_get_hex16(config_device->name, config->name, config->default_int);
                                        
                                        c = 0;
                                        while (selection->description[0])
                                        {
						mbstowcs(lptsTemp, selection->description, strlen(selection->description) + 1);
                                                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)lptsTemp);
                                                if (val_int == selection->value)
                                                        SendMessage(h, CB_SETCURSEL, c, 0);
                                                selection++;
                                                c++;
                                        }
                                        
                                        id += 2;
                                        break;

                                        case CONFIG_HEX20:
                                        val_int = config_get_hex20(config_device->name, config->name, config->default_int);
                                        
                                        c = 0;
                                        while (selection->description[0])
                                        {
						mbstowcs(lptsTemp, selection->description, strlen(selection->description) + 1);
                                                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)lptsTemp);
                                                if (val_int == selection->value)
                                                        SendMessage(h, CB_SETCURSEL, c, 0);
                                                selection++;
                                                c++;
                                        }
                                        
                                        id += 2;
                                        break;
                                }
                                config++;
                        }

			free(lptsTemp);
                }
                return TRUE;
                
                case WM_COMMAND:
				{
                    cid = LOWORD(wParam);
	                if (cid == IDOK)
                    {
                                id = IDC_CONFIG_BASE;
                                config = config_device->config;
                                changed = 0;
                                char s[512];
                                
                                while (config->type != -1)
                                {
                                        device_config_selection_t *selection = config->selection;
                                        h = GetDlgItem(hdlg, id);
                                
                                        switch (config->type)
                                        {
                                                case CONFIG_BINARY:
                                                val_int = config_get_int(config_device->name, config->name, config->default_int);

                                                if (val_int != SendMessage(h, BM_GETCHECK, 0, 0))
                                                        changed = 1;
                                                
                                                id++;
                                                break;

                                                case CONFIG_SELECTION:
                                                val_int = config_get_int(config_device->name, config->name, config->default_int);

                                                c = SendMessage(h, CB_GETCURSEL, 0, 0);

                                                for (; c > 0; c--)
                                                        selection++;

                                                if (val_int != selection->value)
                                                        changed = 1;
                                        
                                                id += 2;
                                                break;

                                                case CONFIG_MIDI:
                                                val_int = config_get_int(config_device->name, config->name, config->default_int);

                                                c = SendMessage(h, CB_GETCURSEL, 0, 0);

                                                if (val_int != c)
                                                        changed = 1;
                                        
                                                id += 2;
                                                break;

                                                case CONFIG_FNAME:
                                                {
                                                        char* str = config_get_string(config_device->name, config->name, (char*)"");
                                                        SendMessage(h, WM_GETTEXT, 511, (LPARAM)s);
                                                        if (strcmp(str, s))
                                                                changed = 1;

                                                        id += 3;
                                                }
                                                break;

                                                case CONFIG_SPINNER:
                                                val_int = config_get_int(config_device->name, config->name, config->default_int);
                                                if (val_int > config->spinner.max)
                                                        val_int = config->spinner.max;
                                                else if (val_int < config->spinner.min)
                                                        val_int = config->spinner.min;

                                                SendMessage(h, WM_GETTEXT, 79, (LPARAM)ws);
						wcstombs(s, ws, 79);
                                                sscanf(s, "%i", &c);

                                                if (val_int != c)
                                                        changed = 1;

                                                id += 2;
                                                break;

                                                case CONFIG_HEX16:
                                                val_int = config_get_hex16(config_device->name, config->name, config->default_int);

                                                c = SendMessage(h, CB_GETCURSEL, 0, 0);

                                                for (; c > 0; c--)
                                                        selection++;

                                                if (val_int != selection->value)
                                                        changed = 1;
                                        
                                                id += 2;
                                                break;

                                                case CONFIG_HEX20:
                                                val_int = config_get_hex20(config_device->name, config->name, config->default_int);

                                                c = SendMessage(h, CB_GETCURSEL, 0, 0);

                                                for (; c > 0; c--)
                                                        selection++;

                                                if (val_int != selection->value)
                                                        changed = 1;
                                        
                                                id += 2;
                                                break;
                                        }
                                        config++;
                                }
                                
                                if (!changed)
                                {
					deviceconfig_changed = 0;
                                        EndDialog(hdlg, 0);
                                        return TRUE;
                                }

				deviceconfig_changed = 1;

                                id = IDC_CONFIG_BASE;
                                config = config_device->config;
                                
                                while (config->type != -1)
                                {
                                        device_config_selection_t *selection = config->selection;
                                        h = GetDlgItem(hdlg, id);
                                
                                        switch (config->type)
                                        {
                                                case CONFIG_BINARY:
                                                config_set_int(config_device->name, config->name, SendMessage(h, BM_GETCHECK, 0, 0));
                                                
                                                id++;
                                                break;

                                                case CONFIG_SELECTION:
                                                c = SendMessage(h, CB_GETCURSEL, 0, 0);
                                                for (; c > 0; c--)
                                                        selection++;
                                                config_set_int(config_device->name, config->name, selection->value);
                                        
                                                id += 2;
                                                break;

                                                case CONFIG_MIDI:
                                                c = SendMessage(h, CB_GETCURSEL, 0, 0);
                                                config_set_int(config_device->name, config->name, c);
                                        
                                                id += 2;
                                                break;

                                                case CONFIG_FNAME:
                                                SendMessage(h, WM_GETTEXT, 511, (LPARAM)ws);
                                                config_set_wstring(config_device->name, config->name, ws);

                                                id += 3;
                                                break;

                                                case CONFIG_SPINNER:
                                                SendMessage(h, WM_GETTEXT, 79, (LPARAM)s);
                                                sscanf(s, "%i", &c);
                                                if (c > config->spinner.max)
                                                        c = config->spinner.max;
                                                else if (c < config->spinner.min)
                                                        c = config->spinner.min;

                                                config_set_int(config_device->name, config->name, c);

                                                id += 2;
                                                break;

                                                case CONFIG_HEX16:
                                                c = SendMessage(h, CB_GETCURSEL, 0, 0);
                                                for (; c > 0; c--)
                                                        selection++;
                                                config_set_hex16(config_device->name, config->name, selection->value);

                                                id += 2;
                                                break;

                                                case CONFIG_HEX20:
                                                c = SendMessage(h, CB_GETCURSEL, 0, 0);
                                                for (; c > 0; c--)
                                                        selection++;
                                                config_set_hex20(config_device->name, config->name, selection->value);

                                                id += 2;
                                                break;
                                        }
                                        config++;
                                }

                                EndDialog(hdlg, 0);
                                return TRUE;
                    }
                    else if (cid == IDCANCEL)
                    {
			    deviceconfig_changed = 0;
                            EndDialog(hdlg, 0);
                            return TRUE;
                    }
                    else
                    {
                                int id = IDC_CONFIG_BASE;
                                device_config_t *config = config_device->config;

                                while (config->type != -1)
                                {
                                        switch (config->type)
                                        {
                                                case CONFIG_BINARY:
                                                id++;
                                                break;

                                                case CONFIG_SELECTION:
                                                case CONFIG_MIDI:
                                                case CONFIG_SPINNER:
                                                id += 2;
                                                break;

                                                case CONFIG_FNAME:
                                                {
                                                        if (cid == id+1)
                                                        {
                                                                char s[512];
                                                                s[0] = 0;
                                                                int c, d;
                                                                HWND h = GetDlgItem(hdlg, id);
                                                                SendMessage(h, WM_GETTEXT, 511, (LPARAM)s);
                                                                char file_filter[512];
                                                                file_filter[0] = 0;

                                                                c = 0;
                                                                while (config->file_filter[c].description[0])
                                                                {
                                                                        if (c > 0)
                                                                                strcat(file_filter, "|");
                                                                        strcat(file_filter, config->file_filter[c].description);
                                                                        strcat(file_filter, " (");
                                                                        d = 0;
                                                                        while (config->file_filter[c].extensions[d][0])
                                                                        {
                                                                                if (d > 0)
                                                                                        strcat(file_filter, ";");
                                                                                strcat(file_filter, "*.");
                                                                                strcat(file_filter, config->file_filter[c].extensions[d]);
                                                                                d++;
                                                                        }
                                                                        strcat(file_filter, ")|");
                                                                        d = 0;
                                                                        while (config->file_filter[c].extensions[d][0])
                                                                        {
                                                                                if (d > 0)
                                                                                        strcat(file_filter, ";");
                                                                                strcat(file_filter, "*.");
                                                                                strcat(file_filter, config->file_filter[c].extensions[d]);
                                                                                d++;
                                                                        }
                                                                        c++;
                                                                }
                                                                strcat(file_filter, "|All files (*.*)|*.*|");
								mbstowcs(ws, file_filter, strlen(file_filter) + 1);
                                                                d = strlen(file_filter);

                                                                /* replace | with \0 */
                                                                for (c = 0; c < d; ++c)
                                                                        if (ws[c] == L'|')
                                                                                ws[c] = 0;

                                                                if (!file_dlg(hdlg, ws, s, 0))
                                                                        SendMessage(h, WM_SETTEXT, 0, (LPARAM)wopenfilestring);
                                                        }
												}
												break;
                                        }
                                        config++;
                                }
                    }
				}
				break;
        }
        return FALSE;
}

uint8_t deviceconfig_open(HWND hwnd, device_t *device)
{
        device_config_t *config = device->config;
        uint16_t *data_block = malloc(16384);
        uint16_t *data;
        DLGTEMPLATE *dlg = (DLGTEMPLATE *)data_block;
        DLGITEMTEMPLATE *item;
        int y = 10;
        int id = IDC_CONFIG_BASE;

	deviceconfig_changed = 0;

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

        *data++ = 9; /*Point*/
        data += MultiByteToWideChar(CP_ACP, 0, "Segoe UI", -1, data, 50);
        
        if (((unsigned long)data) & 2)
                data++;

        while (config->type != -1)
        {
                switch (config->type)
                {
                        case CONFIG_BINARY:
                        item = (DLGITEMTEMPLATE *)data;
                        item->x = 10;
                        item->y = y;
                        item->id = id++;
                
                        item->cx = 80;
                        item->cy = 15;

                        item->style = WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX;

                        data = (uint16_t *)(item + 1);
                        *data++ = 0xFFFF;
                        *data++ = 0x0080;    /* button class */

                        data += MultiByteToWideChar(CP_ACP, 0, config->description, -1, data, 256);
                        *data++ = 0;              /* no creation data */

                        y += 20;
                        break;

                        case CONFIG_SELECTION:
                        case CONFIG_MIDI:
                        case CONFIG_HEX16:
                        case CONFIG_HEX20:
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

                        data += MultiByteToWideChar(CP_ACP, 0, config->description, -1, data, 256);
                        *data++ = 0;              /* no creation data */
                        
                        if (((unsigned long)data) & 2)
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

                        data += MultiByteToWideChar(CP_ACP, 0, config->description, -1, data, 256);
                        *data++ = 0;              /* no creation data */
                        
                        if (((unsigned long)data) & 2)
                                data++;

                        y += 20;
                        break;

                        case CONFIG_SPINNER:
                        /*Spinner*/
                        item = (DLGITEMTEMPLATE *)data;
                        item->x = 70;
                        item->y = y;
                        item->id = id++;

                        item->cx = 140;
                        item->cy = 14;

                        item->style = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER;
                        item->dwExtendedStyle = WS_EX_CLIENTEDGE;

                        data = (uint16_t *)(item + 1);
                        *data++ = 0xFFFF;
                        *data++ = 0x0081;    /* edit text class */

                        data += MultiByteToWideChar(CP_ACP, 0, "", -1, data, 256);
                        *data++ = 0;              /* no creation data */

                        if (((uintptr_t)data) & 2)
                                data++;

                        /* TODO: add up down class */
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

                        data += MultiByteToWideChar(CP_ACP, 0, config->description, -1, data, 256);
                        *data++ = 0;              /* no creation data */

                        if (((uintptr_t)data) & 2)
                                data++;

                        y += 20;
                        break;

                        case CONFIG_FNAME:
                        /*File*/
                        item = (DLGITEMTEMPLATE *)data;
                        item->x = 70;
                        item->y = y;
                        item->id = id++;

                        item->cx = 100;
                        item->cy = 14;

                        item->style = WS_CHILD | WS_VISIBLE | ES_READONLY;
                        item->dwExtendedStyle = WS_EX_CLIENTEDGE;

                        data = (uint16_t *)(item + 1);
                        *data++ = 0xFFFF;
                        *data++ = 0x0081;    /* edit text class */

                        data += MultiByteToWideChar(CP_ACP, 0, "", -1, data, 256);
                        *data++ = 0;              /* no creation data */

                        if (((uintptr_t)data) & 2)
                                data++;

                        /* Button */
                        item = (DLGITEMTEMPLATE *)data;
                        item->x = 175;
                        item->y = y;
                        item->id = id++;

                        item->cx = 35;
                        item->cy = 14;

                        item->style = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;

                        data = (uint16_t *)(item + 1);
                        *data++ = 0xFFFF;
                        *data++ = 0x0080;    /* button class */

                        data += MultiByteToWideChar(CP_ACP, 0, "Browse", -1, data, 256);
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

                        data += MultiByteToWideChar(CP_ACP, 0, config->description, -1, data, 256);
                        *data++ = 0;              /* no creation data */

                        if (((uintptr_t)data) & 2)
                                data++;

                        y += 20;
                        break;
                }

                if (((unsigned long)data) & 2)
                        data++;

                config++;
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

        if (((unsigned long)data) & 2)
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
        
        config_device = device;
        
        DialogBoxIndirect(hinstance, dlg, hwnd, deviceconfig_dlgproc);

        free(data_block);

	return deviceconfig_changed;
}
