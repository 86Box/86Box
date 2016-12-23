/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#undef BITMAP

#include <commctrl.h>

#include "nethandler.h"
#include "ibm.h"
#include "ide.h"
#include "cpu.h"
#include "device.h"
#include "fdd.h"
#include "gameport.h"
#include "model.h"
#include "mouse.h"
#include "nvr.h"
#include "resources.h"
#include "sound.h"
#include "video.h"
#include "vid_voodoo.h"
#include "win.h"

extern int is486;
static int romstolist[ROM_MAX], listtomodel[ROM_MAX], romstomodel[ROM_MAX], modeltolist[ROM_MAX];
static int settings_sound_to_list[20], settings_list_to_sound[20];
static int settings_mouse_to_list[20], settings_list_to_mouse[20];
static int settings_network_to_list[20], settings_list_to_network[20];

static int mouse_valid(int type, int model)
{
        if (type == MOUSE_TYPE_PS2 && !(models[model].flags & MODEL_PS2))
                return 0;
        if (type == MOUSE_TYPE_AMSTRAD && !(models[model].flags & MODEL_AMSTRAD))
                return 0;
        if (type == MOUSE_TYPE_OLIM24 && !(models[model].flags & MODEL_OLIM24))
                return 0;
        return 1;
}

static BOOL CALLBACK config_dlgproc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
        char temp_str[256];
        HWND h;
        int c, d;
        int rom, gfx, mem, fpu;
        int temp_cpu, temp_cpu_m, temp_model;
        int temp_GAMEBLASTER, temp_GUS, temp_SSI2001, temp_voodoo, temp_sound_card_current;
        int temp_dynarec;
        int cpu_flags;
        int temp_fd1_type, temp_fd2_type, temp_fd3_type, temp_fd4_type;
        int temp_network_card_current;
	int temp_network_interface_current;
        int temp_joystick_type;
        int cpu_type;
        int temp_mouse_type;
        
        UDACCEL accel;
//        pclog("Dialog msg %i %08X\n",message,message);
        switch (message)
        {
                case WM_INITDIALOG:
                pause = 1;
                h = GetDlgItem(hdlg, IDC_COMBO1);
                for (c = 0; c < ROM_MAX; c++)
                        romstolist[c] = 0;
                c = d = 0;
                while (models[c].id != -1)
                {
                        pclog("INITDIALOG : %i %i %i\n",c,models[c].id,romspresent[models[c].id]);
                        if (romspresent[models[c].id])
                        {
                                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)models[c].name);
                                modeltolist[c] = d;
                                listtomodel[d] = c;
                                romstolist[models[c].id] = d;
                                romstomodel[models[c].id] = c;
                                d++;
                        }
                        c++;
                }
                SendMessage(h, CB_SETCURSEL, modeltolist[model], 0);

                h = GetDlgItem(hdlg, IDC_COMBOVID);
                c = d = 0;
                while (1)
                {
                        char *s = video_card_getname(c);

                        if (!s[0])
                                break;

                        if (video_card_available(c) && gfx_present[video_new_to_old(c)])
                        {
                                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)s);
                                if (video_new_to_old(c) == gfxcard)
                                        SendMessage(h, CB_SETCURSEL, d, 0);                                

                                d++;
                        }

                        c++;
                }
                if (models[model].fixed_gfxcard)
                        EnableWindow(h, FALSE);

                h = GetDlgItem(hdlg, IDC_COMBOCPUM);
                c = 0;
                while (models[romstomodel[romset]].cpu[c].cpus != NULL && c < 4)
                {
                        SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)models[romstomodel[romset]].cpu[c].name);
                        c++;
                }
                EnableWindow(h, TRUE);
                SendMessage(h, CB_SETCURSEL, cpu_manufacturer, 0);
                if (c == 1) EnableWindow(h, FALSE);
                else        EnableWindow(h, TRUE);

                h = GetDlgItem(hdlg, IDC_COMBO3);
                c = 0;
                while (models[romstomodel[romset]].cpu[cpu_manufacturer].cpus[c].cpu_type != -1)
                {
                        SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)models[romstomodel[romset]].cpu[cpu_manufacturer].cpus[c].name);
                        c++;
                }
                EnableWindow(h, TRUE);
                SendMessage(h, CB_SETCURSEL, cpu, 0);

                h = GetDlgItem(hdlg, IDC_COMBOSND);
                c = d = 0;
                while (1)
                {
                        char *s = sound_card_getname(c);

                        if (!s[0])
                                break;

                        settings_sound_to_list[c] = d;
                        
                        if (sound_card_available(c))
                        {
                                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)s);
                                settings_list_to_sound[d] = c;
                                d++;
                        }

                        c++;
                }
                SendMessage(h, CB_SETCURSEL, settings_sound_to_list[sound_card_current], 0);

                /*NIC config*/
                h = GetDlgItem(hdlg, IDC_COMBONET);
                c = d = 0;
                while (1)
                {
                        char *s = network_card_getname(c);

                        if (!s[0])
                                break;

                        settings_network_to_list[c] = d;

                        if (network_card_available(c))
                        {
                                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)s);
                                settings_list_to_network[d] = c;
                                d++;
                        }

                        c++;
                }
                SendMessage(h, CB_SETCURSEL, settings_network_to_list[network_card_current], 0);
                
                h=GetDlgItem(hdlg, IDC_CHECK3);
                SendMessage(h, BM_SETCHECK, GAMEBLASTER, 0);

                h=GetDlgItem(hdlg, IDC_CHECKGUS);
                SendMessage(h, BM_SETCHECK, GUS, 0);

                h=GetDlgItem(hdlg, IDC_CHECKSSI);
                SendMessage(h, BM_SETCHECK, SSI2001, 0);
                
                h=GetDlgItem(hdlg, IDC_CHECKSYNC);
                SendMessage(h, BM_SETCHECK, enable_sync, 0);

                h=GetDlgItem(hdlg, IDC_CHECKVOODOO);
                SendMessage(h, BM_SETCHECK, voodoo_enabled, 0);

                cpu_flags = models[romstomodel[romset]].cpu[cpu_manufacturer].cpus[cpu].cpu_flags;
                h=GetDlgItem(hdlg, IDC_CHECKDYNAREC);
                if (!(cpu_flags & CPU_SUPPORTS_DYNAREC) || (cpu_flags & CPU_REQUIRES_DYNAREC))
                        EnableWindow(h, FALSE);
                else
                        EnableWindow(h, TRUE);
                SendMessage(h, BM_SETCHECK, ((cpu_flags & CPU_SUPPORTS_DYNAREC) && cpu_use_dynarec) || (cpu_flags & CPU_REQUIRES_DYNAREC), 0);

                h = GetDlgItem(hdlg, IDC_COMBOSPD);
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"8-bit");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"Slow 16-bit");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"Fast 16-bit");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"Slow VLB/PCI");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"Mid  VLB/PCI");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"Fast VLB/PCI");                
                SendMessage(h, CB_SETCURSEL, video_speed, 0);

                h = GetDlgItem(hdlg, IDC_MEMSPIN);
                SendMessage(h, UDM_SETBUDDY, (WPARAM)GetDlgItem(hdlg, IDC_MEMTEXT), 0);
                SendMessage(h, UDM_SETRANGE, 0, (models[romstomodel[romset]].min_ram << 16) | models[romstomodel[romset]].max_ram);
                if (!models[model].flags & MODEL_AT)
                        SendMessage(h, UDM_SETPOS, 0, mem_size);
                else
                        SendMessage(h, UDM_SETPOS, 0, mem_size / 1024);
                accel.nSec = 0;
                accel.nInc = models[model].ram_granularity;
                SendMessage(h, UDM_SETACCEL, 1, (LPARAM)&accel);

                h = GetDlgItem(hdlg, IDC_CONFIGUREMOD);
                if (model_getdevice(model))
                        EnableWindow(h, TRUE);
                else
                        EnableWindow(h, FALSE);
                
                h = GetDlgItem(hdlg, IDC_CONFIGUREVID);
                if (video_card_has_config(video_old_to_new(gfxcard)))
                        EnableWindow(h, TRUE);
                else
                        EnableWindow(h, FALSE);
                
                h = GetDlgItem(hdlg, IDC_CONFIGURESND);
                if (sound_card_has_config(sound_card_current))
                        EnableWindow(h, TRUE);
                else
                        EnableWindow(h, FALSE);
                        
                h = GetDlgItem(hdlg, IDC_CONFIGURENET);
                if (network_card_has_config(network_card_current))
                        EnableWindow(h, TRUE);
                else
                        EnableWindow(h, FALSE);

                h = GetDlgItem(hdlg, IDC_COMBODR1);
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"None");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"5.25\" 360k");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"5.25\" 1.2M");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"5.25\" 1.2M Dual RPM");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 720k");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 1.44M PS/2");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 1.44M");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 1.25M PC-98");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 1.44M 3-Mode");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 2.88M");
                SendMessage(h, CB_SETCURSEL, fdd_get_type(0), 0);
                h = GetDlgItem(hdlg, IDC_COMBODR2);
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"None");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"5.25\" 360k");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"5.25\" 1.2M");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"5.25\" 1.2M Dual RPM");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 720k");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 1.44M PS/2");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 1.44M");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 1.25M PC-98");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 1.44M 3-Mode");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 2.88M");
                SendMessage(h, CB_SETCURSEL, fdd_get_type(1), 0);
                h = GetDlgItem(hdlg, IDC_COMBODR3);
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"None");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"5.25\" 360k");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"5.25\" 1.2M");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"5.25\" 1.2M Dual RPM");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 720k");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 1.44M PS/2");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 1.44M");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 1.25M PC-98");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 1.44M 3-Mode");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 2.88M");
                SendMessage(h, CB_SETCURSEL, fdd_get_type(2), 0);
                h = GetDlgItem(hdlg, IDC_COMBODR4);
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"None");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"5.25\" 360k");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"5.25\" 1.2M");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"5.25\" 1.2M Dual RPM");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 720k");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 1.44M PS/2");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 1.44M");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 1.25M PC-98");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 1.44M 3-Mode");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3.5\" 2.88M");
                SendMessage(h, CB_SETCURSEL, fdd_get_type(3), 0);

                h = GetDlgItem(hdlg, IDC_TEXT_MB);
                if (models[model].flags & MODEL_AT)
                        SendMessage(h, WM_SETTEXT, 0, (LPARAM)(LPCSTR)"MB");
                else
                        SendMessage(h, WM_SETTEXT, 0, (LPARAM)(LPCSTR)"KB");

                h = GetDlgItem(hdlg, IDC_COMBOJOY);
                c = 0;
                while (joystick_get_name(c))
                {
                        SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)joystick_get_name(c));
                        c++;
                }
                EnableWindow(h, TRUE);
                SendMessage(h, CB_SETCURSEL, joystick_type, 0);

                h = GetDlgItem(hdlg, IDC_JOY1);
                EnableWindow(h, (joystick_get_max_joysticks(joystick_type) >= 1) ? TRUE : FALSE);
                h = GetDlgItem(hdlg, IDC_JOY2);
                EnableWindow(h, (joystick_get_max_joysticks(joystick_type) >= 2) ? TRUE : FALSE);
                h = GetDlgItem(hdlg, IDC_JOY3);
                EnableWindow(h, (joystick_get_max_joysticks(joystick_type) >= 3) ? TRUE : FALSE);
                h = GetDlgItem(hdlg, IDC_JOY4);
                EnableWindow(h, (joystick_get_max_joysticks(joystick_type) >= 4) ? TRUE : FALSE);

                h = GetDlgItem(hdlg, IDC_COMBOWS);
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"System default");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"0 W/S");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"1 W/S");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"2 W/S");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"3 W/S");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"4 W/S");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"5 W/S");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"6 W/S");
                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"7 W/S");
                SendMessage(h, CB_SETCURSEL, cpu_waitstates, 0);
                cpu_type = models[romstomodel[romset]].cpu[cpu_manufacturer].cpus[cpu].cpu_type;
                if (cpu_type >= CPU_286 && cpu_type <= CPU_386DX)
                        EnableWindow(h, TRUE);
                else
                        EnableWindow(h, FALSE);

                h = GetDlgItem(hdlg, IDC_COMBOMOUSE);
                c = d = 0;
                while (1)
                {
                        char *s = mouse_get_name(c);
                        int type;

                        if (!s)
                                break;

                        type = mouse_get_type(c);
                        settings_mouse_to_list[c] = d;
                        
                        if (mouse_valid(type, model))
                        {
                                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)s);

                                settings_list_to_mouse[d] = c;
                                d++;
                        }
                        c++;
                }

                SendMessage(h, CB_SETCURSEL, settings_mouse_to_list[mouse_type], 0);
                return TRUE;
                
                case WM_COMMAND:
                switch (LOWORD(wParam))
                {
                        case IDOK:
                        h = GetDlgItem(hdlg, IDC_COMBO1);
                        temp_model = listtomodel[SendMessage(h, CB_GETCURSEL, 0, 0)];

			h = GetDlgItem(hdlg, IDC_MEMTEXT);
			SendMessage(h, WM_GETTEXT, 255, (LPARAM)temp_str);
			sscanf(temp_str, "%i", &mem);
                        mem &= ~(models[temp_model].ram_granularity - 1);
                        if (mem < models[temp_model].min_ram)
                                mem = models[temp_model].min_ram;
                        else if (mem > models[temp_model].max_ram)
                                mem = models[temp_model].max_ram;
			if (models[temp_model].flags & MODEL_AT)
                                mem *= 1024;			
			
                        h = GetDlgItem(hdlg, IDC_COMBOVID);
                        SendMessage(h, CB_GETLBTEXT, SendMessage(h, CB_GETCURSEL, 0, 0), (LPARAM)temp_str);
                        gfx = video_new_to_old(video_card_getid(temp_str));

                        h = GetDlgItem(hdlg, IDC_COMBOCPUM);
                        temp_cpu_m = SendMessage(h, CB_GETCURSEL, 0, 0);
                        h = GetDlgItem(hdlg, IDC_COMBO3);
                        temp_cpu = SendMessage(h, CB_GETCURSEL, 0, 0);
                        fpu = (models[temp_model].cpu[temp_cpu_m].cpus[temp_cpu].cpu_type >= CPU_i486DX) ? 1 : 0;

                        h = GetDlgItem(hdlg, IDC_CHECK3);
                        temp_GAMEBLASTER = SendMessage(h, BM_GETCHECK, 0, 0);

                        h = GetDlgItem(hdlg, IDC_CHECKGUS);
                        temp_GUS = SendMessage(h, BM_GETCHECK, 0, 0);

                        h = GetDlgItem(hdlg, IDC_CHECKSSI);
                        temp_SSI2001 = SendMessage(h, BM_GETCHECK, 0, 0);

                        h = GetDlgItem(hdlg, IDC_CHECKSYNC);
                        enable_sync = SendMessage(h, BM_GETCHECK, 0, 0);

                        h = GetDlgItem(hdlg, IDC_CHECKVOODOO);
                        temp_voodoo = SendMessage(h, BM_GETCHECK, 0, 0);

                        h = GetDlgItem(hdlg, IDC_COMBOSND);
                        temp_sound_card_current = settings_list_to_sound[SendMessage(h, CB_GETCURSEL, 0, 0)];

                        h = GetDlgItem(hdlg, IDC_CHECKDYNAREC);
                        temp_dynarec = SendMessage(h, BM_GETCHECK, 0, 0);

                        h = GetDlgItem(hdlg, IDC_COMBONET);
                        temp_network_card_current = settings_list_to_network[SendMessage(h, CB_GETCURSEL, 0, 0)];

                        h = GetDlgItem(hdlg, IDC_COMBODR1);
                        temp_fd1_type = SendMessage(h, CB_GETCURSEL, 0, 0);
                        h = GetDlgItem(hdlg, IDC_COMBODR2);
                        temp_fd2_type = SendMessage(h, CB_GETCURSEL, 0, 0);
                        h = GetDlgItem(hdlg, IDC_COMBODR3);
                        temp_fd3_type = SendMessage(h, CB_GETCURSEL, 0, 0);
                        h = GetDlgItem(hdlg, IDC_COMBODR4);
                        temp_fd4_type = SendMessage(h, CB_GETCURSEL, 0, 0);

                        h = GetDlgItem(hdlg, IDC_COMBOJOY);
                        temp_joystick_type = SendMessage(h, CB_GETCURSEL, 0, 0);
                        h = GetDlgItem(hdlg, IDC_COMBOMOUSE);
                        temp_mouse_type = settings_list_to_mouse[SendMessage(h, CB_GETCURSEL, 0, 0)];                      

                        if (temp_model != model || gfx != gfxcard || mem != mem_size || temp_cpu != cpu || temp_cpu_m != cpu_manufacturer ||
                            fpu != hasfpu || temp_GAMEBLASTER != GAMEBLASTER || temp_GUS != GUS ||
                            temp_SSI2001 != SSI2001 || temp_sound_card_current != sound_card_current ||
                            temp_voodoo != voodoo_enabled || temp_dynarec != cpu_use_dynarec || temp_mouse_type != mouse_type ||
			    temp_fd1_type != fdd_get_type(0) || temp_fd2_type != fdd_get_type(1) || temp_fd3_type != fdd_get_type(2) || temp_fd4_type != fdd_get_type(3) || temp_network_card_current != network_card_current)
                        {
                                if (MessageBox(NULL,"This will reset 86Box!\nOkay to continue?","86Box",MB_OKCANCEL)==IDOK)
                                {
					savenvr();
                                        model = temp_model;
                                        romset = model_getromset();
                                        gfxcard = gfx;
                                        mem_size = mem;
                                        cpu_manufacturer = temp_cpu_m;
                                        cpu = temp_cpu;
                                        GAMEBLASTER = temp_GAMEBLASTER;
                                        GUS = temp_GUS;
                                        SSI2001 = temp_SSI2001;
                                        sound_card_current = temp_sound_card_current;
                                        voodoo_enabled = temp_voodoo;
                                        cpu_use_dynarec = temp_dynarec;

					fdd_set_type(0, temp_fd1_type);
					fdd_set_type(1, temp_fd2_type);
					fdd_set_type(2, temp_fd3_type);
					fdd_set_type(3, temp_fd4_type);

                                        network_card_current = temp_network_card_current;
                                        
                                        mem_resize();
                                        loadbios();
                                        resetpchard();
                                }
                                else
                                {
                                        EndDialog(hdlg, 0);
                                        pause = 0;
                                        return TRUE;
                                }
                        }

                        h = GetDlgItem(hdlg, IDC_COMBOSPD);
                        video_speed = SendMessage(h, CB_GETCURSEL, 0, 0);

                        cpu_manufacturer = temp_cpu_m;
                        cpu = temp_cpu;
                        cpu_set();
                        
                        h = GetDlgItem(hdlg, IDC_COMBOWS);
                        cpu_waitstates = SendMessage(h, CB_GETCURSEL, 0, 0);
                        cpu_update_waitstates();

                        saveconfig();

                        speedchanged();

                        joystick_type = temp_joystick_type;
                        if (joystick_type != 7)  gameport_update_joystick_type();

                        case IDCANCEL:
                        EndDialog(hdlg, 0);
                        pause=0;
                        return TRUE;
                        case IDC_COMBO1:
                        if (HIWORD(wParam) == CBN_SELCHANGE)
                        {
                                h = GetDlgItem(hdlg,IDC_COMBO1);
                                temp_model = listtomodel[SendMessage(h,CB_GETCURSEL,0,0)];
                                
                                /*Enable/disable gfxcard list*/
                                h = GetDlgItem(hdlg, IDC_COMBOVID);
                                if (!models[temp_model].fixed_gfxcard)
                                {
                                        char *s = video_card_getname(video_old_to_new(gfxcard));
                                        
                                        EnableWindow(h, TRUE);
                                        
                                        c = 0;
                                        while (1)
                                        {
                                                SendMessage(h, CB_GETLBTEXT, c, (LPARAM)temp_str);
                                                if (!strcmp(temp_str, s))
                                                        break;
                                                c++;
                                        }
                                        SendMessage(h, CB_SETCURSEL, c, 0);
                                }
                                else
                                        EnableWindow(h, FALSE);
                                
                                /*Rebuild manufacturer list*/
                                h = GetDlgItem(hdlg, IDC_COMBOCPUM);
                                temp_cpu_m = SendMessage(h, CB_GETCURSEL, 0, 0);
                                SendMessage(h, CB_RESETCONTENT, 0, 0);
                                c = 0;
                                while (models[temp_model].cpu[c].cpus != NULL && c < 4)
                                {
                                        SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)models[temp_model].cpu[c].name);
                                        c++;
                                }
                                if (temp_cpu_m >= c) temp_cpu_m = c - 1;
                                SendMessage(h, CB_SETCURSEL, temp_cpu_m, 0);
                                if (c == 1) EnableWindow(h, FALSE);
                                else        EnableWindow(h, TRUE);

                                /*Rebuild CPU list*/
                                h = GetDlgItem(hdlg, IDC_COMBO3);
                                temp_cpu = SendMessage(h, CB_GETCURSEL, 0, 0);
                                SendMessage(h, CB_RESETCONTENT, 0, 0);
                                c = 0;
                                while (models[temp_model].cpu[temp_cpu_m].cpus[c].cpu_type != -1)
                                {
                                        SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)models[temp_model].cpu[temp_cpu_m].cpus[c].name);
                                        c++;
                                }
                                if (temp_cpu >= c) temp_cpu = c - 1;
                                SendMessage(h, CB_SETCURSEL, temp_cpu, 0);

                                h = GetDlgItem(hdlg, IDC_CHECKDYNAREC);
                                temp_dynarec = SendMessage(h, BM_GETCHECK, 0, 0);

                                cpu_flags = models[temp_model].cpu[temp_cpu_m].cpus[temp_cpu].cpu_flags;
                                h=GetDlgItem(hdlg, IDC_CHECKDYNAREC);
                                if (!(cpu_flags & CPU_SUPPORTS_DYNAREC) || (cpu_flags & CPU_REQUIRES_DYNAREC))
                                        EnableWindow(h, FALSE);
                                else
                                        EnableWindow(h, TRUE);
                                SendMessage(h, BM_SETCHECK, ((cpu_flags & CPU_SUPPORTS_DYNAREC) && temp_dynarec) || (cpu_flags & CPU_REQUIRES_DYNAREC), 0);

                                h = GetDlgItem(hdlg, IDC_TEXT_MB);
                                if (models[temp_model].flags & MODEL_AT)
                                        SendMessage(h, WM_SETTEXT, 0, (LPARAM)(LPCSTR)"MB");
                                else
                                        SendMessage(h, WM_SETTEXT, 0, (LPARAM)(LPCSTR)"KB");

        			h = GetDlgItem(hdlg, IDC_MEMTEXT);
        			SendMessage(h, WM_GETTEXT, 255, (LPARAM)temp_str);
        			sscanf(temp_str, "%i", &mem);

                                h = GetDlgItem(hdlg, IDC_MEMSPIN);
                                SendMessage(h, UDM_SETRANGE, 0, (models[temp_model].min_ram << 16) | models[temp_model].max_ram);
                                mem &= ~(models[temp_model].ram_granularity - 1);
                                if (mem < models[temp_model].min_ram)
                                        mem = models[temp_model].min_ram;
                                else if (mem > models[temp_model].max_ram)
                                        mem = models[temp_model].max_ram;
                                SendMessage(h, UDM_SETPOS, 0, mem);
                                accel.nSec = 0;
                                accel.nInc = models[temp_model].ram_granularity;
                                SendMessage(h, UDM_SETACCEL, 1, (LPARAM)&accel);

                                h = GetDlgItem(hdlg, IDC_COMBOWS);
                                cpu_type = models[temp_model].cpu[temp_cpu_m].cpus[temp_cpu].cpu_type;
                                if (cpu_type >= CPU_286 && cpu_type <= CPU_386DX)
                                        EnableWindow(h, TRUE);
                                else
                                        EnableWindow(h, FALSE);

                                h = GetDlgItem(hdlg, IDC_CONFIGUREMOD);
                                if (model_getdevice(temp_model))
                                        EnableWindow(h, TRUE);
                                else
                                        EnableWindow(h, FALSE);

                                h = GetDlgItem(hdlg, IDC_COMBOMOUSE);
                                temp_mouse_type = settings_list_to_mouse[SendMessage(h, CB_GETCURSEL, 0, 0)];
                                SendMessage(h, CB_RESETCONTENT, 0, 0);
                                c = d = 0;
                                while (1)
                                {
                                        char *s = mouse_get_name(c);
                                        int type;

                                        if (!s)
                                                break;

                                        type = mouse_get_type(c);
                                        settings_mouse_to_list[c] = d;
                                                
                                        if (mouse_valid(type, temp_model))
                                        {
                                                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)s);

                                                settings_list_to_mouse[d] = c;
                                                d++;
                                        }

                                        c++;
                                }
                                if (mouse_valid(temp_mouse_type, temp_model))
                                        SendMessage(h, CB_SETCURSEL, settings_mouse_to_list[temp_mouse_type], 0);
                                else
                                        SendMessage(h, CB_SETCURSEL, 0, 0);
                        }
                        break;
                        case IDC_COMBOCPUM:
                        if (HIWORD(wParam) == CBN_SELCHANGE)
                        {
                                h = GetDlgItem(hdlg, IDC_COMBO1);
                                temp_model = listtomodel[SendMessage(h, CB_GETCURSEL, 0, 0)];
                                h = GetDlgItem(hdlg, IDC_COMBOCPUM);
                                temp_cpu_m = SendMessage(h, CB_GETCURSEL, 0, 0);
                                
                                /*Rebuild CPU list*/
                                h=GetDlgItem(hdlg, IDC_COMBO3);
                                temp_cpu = SendMessage(h, CB_GETCURSEL, 0, 0);
                                SendMessage(h, CB_RESETCONTENT, 0, 0);
                                c = 0;
                                while (models[temp_model].cpu[temp_cpu_m].cpus[c].cpu_type != -1)
                                {
                                        SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)models[temp_model].cpu[temp_cpu_m].cpus[c].name);
                                        c++;
                                }
                                if (temp_cpu >= c) temp_cpu = c - 1;
                                SendMessage(h, CB_SETCURSEL, temp_cpu, 0);

                                h = GetDlgItem(hdlg, IDC_CHECKDYNAREC);
                                temp_dynarec = SendMessage(h, BM_GETCHECK, 0, 0);

                                cpu_flags = models[temp_model].cpu[temp_cpu_m].cpus[temp_cpu].cpu_flags;
                                h=GetDlgItem(hdlg, IDC_CHECKDYNAREC);
                                if (!(cpu_flags & CPU_SUPPORTS_DYNAREC) || (cpu_flags & CPU_REQUIRES_DYNAREC))
                                        EnableWindow(h, FALSE);
                                else
                                        EnableWindow(h, TRUE);
                                SendMessage(h, BM_SETCHECK, ((cpu_flags & CPU_SUPPORTS_DYNAREC) && temp_dynarec) || (cpu_flags & CPU_REQUIRES_DYNAREC), 0);

                                h = GetDlgItem(hdlg, IDC_COMBOWS);
                                cpu_type = models[temp_model].cpu[temp_cpu_m].cpus[temp_cpu].cpu_type;
                                if (cpu_type >= CPU_286 && cpu_type <= CPU_386DX)
                                        EnableWindow(h, TRUE);
                                else
                                        EnableWindow(h, FALSE);

                        }
                        break;
                        case IDC_COMBO3:
                        if (HIWORD(wParam) == CBN_SELCHANGE)
                        {
                                h = GetDlgItem(hdlg, IDC_COMBO1);
                                temp_model = listtomodel[SendMessage(h, CB_GETCURSEL, 0, 0)];
                                h = GetDlgItem(hdlg, IDC_COMBOCPUM);
                                temp_cpu_m = SendMessage(h, CB_GETCURSEL, 0, 0);
                                h=GetDlgItem(hdlg, IDC_COMBO3);
                                temp_cpu = SendMessage(h, CB_GETCURSEL, 0, 0);

                                h = GetDlgItem(hdlg, IDC_CHECKDYNAREC);
                                temp_dynarec = SendMessage(h, BM_GETCHECK, 0, 0);

                                cpu_flags = models[temp_model].cpu[temp_cpu_m].cpus[temp_cpu].cpu_flags;
                                h=GetDlgItem(hdlg, IDC_CHECKDYNAREC);
                                if (!(cpu_flags & CPU_SUPPORTS_DYNAREC) || (cpu_flags & CPU_REQUIRES_DYNAREC))
                                        EnableWindow(h, FALSE);
                                else
                                        EnableWindow(h, TRUE);
                                SendMessage(h, BM_SETCHECK, ((cpu_flags & CPU_SUPPORTS_DYNAREC) && temp_dynarec) || (cpu_flags & CPU_REQUIRES_DYNAREC), 0);

                                h = GetDlgItem(hdlg, IDC_COMBOWS);
                                cpu_type = models[temp_model].cpu[temp_cpu_m].cpus[temp_cpu].cpu_type;
                                if (cpu_type >= CPU_286 && cpu_type <= CPU_386DX)
                                        EnableWindow(h, TRUE);
                                else
                                        EnableWindow(h, FALSE);
                        }
                        break;
                        
                        case IDC_CONFIGUREMOD:
                        h = GetDlgItem(hdlg, IDC_COMBO1);
                        temp_model = listtomodel[SendMessage(h, CB_GETCURSEL, 0, 0)];
                        
                        deviceconfig_open(hdlg, (void *)model_getdevice(temp_model));
                        break;

                        case IDC_CONFIGUREVID:
                        h = GetDlgItem(hdlg, IDC_COMBOVID);
                        SendMessage(h, CB_GETLBTEXT, SendMessage(h, CB_GETCURSEL, 0, 0), (LPARAM)temp_str);
                        
                        deviceconfig_open(hdlg, (void *)video_card_getdevice(video_card_getid(temp_str)));
                        break;
                        
                        case IDC_COMBOVID:
                        h = GetDlgItem(hdlg, IDC_COMBOVID);
                        SendMessage(h, CB_GETLBTEXT, SendMessage(h, CB_GETCURSEL, 0, 0), (LPARAM)temp_str);
                        gfx = video_card_getid(temp_str);
                        
                        h = GetDlgItem(hdlg, IDC_CONFIGUREVID);
                        if (video_card_has_config(gfx))
                                EnableWindow(h, TRUE);
                        else
                                EnableWindow(h, FALSE);
                        break;                                

                        case IDC_CONFIGURESND:
                        h = GetDlgItem(hdlg, IDC_COMBOSND);
                        temp_sound_card_current = settings_list_to_sound[SendMessage(h, CB_GETCURSEL, 0, 0)];
                        
                        deviceconfig_open(hdlg, (void *)sound_card_getdevice(temp_sound_card_current));
                        break;
                        
                        case IDC_COMBOSND:
                        h = GetDlgItem(hdlg, IDC_COMBOSND);
                        temp_sound_card_current = settings_list_to_sound[SendMessage(h, CB_GETCURSEL, 0, 0)];
                        
                        h = GetDlgItem(hdlg, IDC_CONFIGURESND);
                        if (sound_card_has_config(temp_sound_card_current))
                                EnableWindow(h, TRUE);
                        else
                                EnableWindow(h, FALSE);
                        break;                                

                        case IDC_CONFIGURENET:
                        h = GetDlgItem(hdlg, IDC_COMBONET);
                        temp_network_card_current = settings_list_to_network[SendMessage(h, CB_GETCURSEL, 0, 0)];

                        deviceconfig_open(hdlg, (void *)network_card_getdevice(temp_network_card_current));
                        break;

                        case IDC_COMBONET:
                        h = GetDlgItem(hdlg, IDC_COMBONET);
                        temp_network_card_current = settings_list_to_network[SendMessage(h, CB_GETCURSEL, 0, 0)];

                        h = GetDlgItem(hdlg, IDC_CONFIGURENET);
                        if (network_card_has_config(temp_network_card_current))
                                EnableWindow(h, TRUE);
                        else
                                EnableWindow(h, FALSE);
                        break;

                        case IDC_CONFIGUREVOODOO:
                        deviceconfig_open(hdlg, (void *)&voodoo_device);
                        break;

                       case IDC_COMBOJOY:
                        if (HIWORD(wParam) == CBN_SELCHANGE)
                        {
                                h = GetDlgItem(hdlg, IDC_COMBOJOY);
                                temp_joystick_type = SendMessage(h, CB_GETCURSEL, 0, 0);

                                h = GetDlgItem(hdlg, IDC_JOY1);
                                EnableWindow(h, (joystick_get_max_joysticks(temp_joystick_type) >= 1) ? TRUE : FALSE);
                                h = GetDlgItem(hdlg, IDC_JOY2);
                                EnableWindow(h, (joystick_get_max_joysticks(temp_joystick_type) >= 2) ? TRUE : FALSE);
                                h = GetDlgItem(hdlg, IDC_JOY3);
                                EnableWindow(h, (joystick_get_max_joysticks(temp_joystick_type) >= 3) ? TRUE : FALSE);
                                h = GetDlgItem(hdlg, IDC_JOY4);
                                EnableWindow(h, (joystick_get_max_joysticks(temp_joystick_type) >= 4) ? TRUE : FALSE);
                        }
                        break;
                        
                        case IDC_JOY1:
                        h = GetDlgItem(hdlg, IDC_COMBOJOY);
                        temp_joystick_type = SendMessage(h, CB_GETCURSEL, 0, 0);
                        joystickconfig_open(hdlg, 0, temp_joystick_type);
                        break;
                        case IDC_JOY2:
                        h = GetDlgItem(hdlg, IDC_COMBOJOY);
                        temp_joystick_type = SendMessage(h, CB_GETCURSEL, 0, 0);
                        joystickconfig_open(hdlg, 1, temp_joystick_type);
                        break;
                        case IDC_JOY3:
                        h = GetDlgItem(hdlg, IDC_COMBOJOY);
                        temp_joystick_type = SendMessage(h, CB_GETCURSEL, 0, 0);
                        joystickconfig_open(hdlg, 2, temp_joystick_type);
                        break;
                        case IDC_JOY4:
                        h = GetDlgItem(hdlg, IDC_COMBOJOY);
                        temp_joystick_type = SendMessage(h, CB_GETCURSEL, 0, 0);
                        joystickconfig_open(hdlg, 3, temp_joystick_type);
                        break;
                }
                break;
        }
        return FALSE;
}

void config_open(HWND hwnd)
{
        DialogBox(hinstance, TEXT("ConfigureDlg"), hwnd, config_dlgproc);
}
