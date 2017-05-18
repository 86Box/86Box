/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>

#include "cdrom.h"
#include "config.h"
#include "device.h"
#include "disc.h"
#include "fdc.h"
#include "fdd.h"
#include "ibm.h"
#include "cpu/cpu.h"
#include "gameport.h"
#include "ide.h"
#include "hdd.h"
#include "model.h"
#include "mouse.h"
#include "network.h"
#include "nvr.h"
#include "plat-joystick.h"
#include "plat-midi.h"
#include "scsi.h"
#include "sound/snd_dbopl.h"
#include "sound/snd_opl.h"
#include "sound/sound.h"
#include "video/video.h"

#ifndef __unix
#define UNICODE
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#undef BITMAP
#include "win.h"
#include "win-language.h"
#endif


wchar_t config_file_default[256];

static wchar_t config_file[256];


typedef struct list_t
{
        struct list_t *next;
} list_t;

static list_t config_head;

typedef struct section_t
{
        struct list_t list;
        
        char name[256];
        
        struct list_t entry_head;
} section_t;

typedef struct entry_t
{
        struct list_t list;
        
        char name[256];
        char data[256];
        wchar_t wdata[256];
} entry_t;

#define list_add(new, head)                             \
        {                                               \
                struct list_t *next = head;             \
                                                        \
                while (next->next)                      \
                        next = next->next;              \
                                                        \
                (next)->next = new;                     \
                (new)->next = NULL;                     \
        }

void config_dump(void)
{
        section_t *current_section;
        
        pclog("Config data :\n");
        
        current_section = (section_t *)config_head.next;
        
        while (current_section)
        {
                entry_t *current_entry;
                
                pclog("[%s]\n", current_section->name);
                
                current_entry = (entry_t *)current_section->entry_head.next;
                
                while (current_entry)
                {
                        pclog("%s = %s\n", current_entry->name, current_entry->data);

                        current_entry = (entry_t *)current_entry->list.next;
                }

                current_section = (section_t *)current_section->list.next;
        }
}

void config_free(void)
{
        section_t *current_section;
        current_section = (section_t *)config_head.next;
        
        while (current_section)
        {
                section_t *next_section = (section_t *)current_section->list.next;
                entry_t *current_entry;
                
                current_entry = (entry_t *)current_section->entry_head.next;
                
                while (current_entry)
                {
                        entry_t *next_entry = (entry_t *)current_entry->list.next;
                        
                        free(current_entry);
                        current_entry = next_entry;
                }

                free(current_section);                
                current_section = next_section;
        }
}

static wchar_t cfgbuffer[1024];
static char sname[256];
static char ename[256];

void config_load(wchar_t *fn)
{
	int c;

	section_t *new_section;
	entry_t *new_entry;
	int sd = 0, ed = 0, data_pos;

        FILE *f = _wfopen(fn, L"rt, ccs=UNICODE");
        section_t *current_section;
        
        memset(&config_head, 0, sizeof(list_t));

        current_section = malloc(sizeof(section_t));
        memset(current_section, 0, sizeof(section_t));
        list_add(&current_section->list, &config_head);

        if (!f)
                return;

        while (1)
        {
		memset(cfgbuffer, 0, 2048);
                fgetws(cfgbuffer, 255, f);
                if (feof(f)) break;
                
                c = 0;
                
                while (cfgbuffer[c] == L' ')
                      c++;

                if (cfgbuffer[c] == L'\0') continue;
                
                if (cfgbuffer[c] == L'#') /*Comment*/
                        continue;

                if (cfgbuffer[c] == L'[') /*Section*/
                {
			sd = 0;
                        c++;
                        while (cfgbuffer[c] != L']' && cfgbuffer[c])
                                wctomb(&(sname[sd++]), cfgbuffer[c++]);

                        if (cfgbuffer[c] != L']')
                                continue;
                        sname[sd] = 0;
                        
                        new_section = malloc(sizeof(section_t));
                        memset(new_section, 0, sizeof(section_t));
                        strncpy(new_section->name, sname, 256);
                        list_add(&new_section->list, &config_head);
                        
                        current_section = new_section;                        
                }
                else
                {
			ed = 0;
                        while (cfgbuffer[c] != L'=' && cfgbuffer[c] != L' ' && cfgbuffer[c])
                                wctomb(&(ename[ed++]), cfgbuffer[c++]);
                
                        if (cfgbuffer[c] == L'\0') continue;
                        ename[ed] = 0;

                        while ((cfgbuffer[c] == L'=' || cfgbuffer[c] == L' ') && cfgbuffer[c])
                                c++;
                        
                        if (!cfgbuffer[c]) continue;
                        
                        data_pos = c;
                        while (cfgbuffer[c])
                        {
                                if (cfgbuffer[c] == L'\n')
                                        cfgbuffer[c] = L'\0';
                                c++;
                        }

                        new_entry = malloc(sizeof(entry_t));
                        memset(new_entry, 0, sizeof(entry_t));
                        strncpy(new_entry->name, ename, 256);
			memcpy(new_entry->wdata, &cfgbuffer[data_pos], 512);
			new_entry->wdata[255] = L'\0';
			wcstombs(new_entry->data, new_entry->wdata, 512);
			new_entry->data[255] = '\0';
                        list_add(&new_entry->list, &current_section->entry_head);
                }
        }
        
        fclose(f);
        
        config_dump();
}



void config_new(void)
{
        FILE *f = _wfopen(config_file, L"wt, ccs=UNICODE");
        fclose(f);
}

static section_t *find_section(char *name)
{
        section_t *current_section;
        char blank[] = "";
        
        current_section = (section_t *)config_head.next;
        if (!name)
                name = blank;

        while (current_section)
        {
                if (!strncmp(current_section->name, name, 256))
                        return current_section;
                
                current_section = (section_t *)current_section->list.next;
        }
        return NULL;
}

static entry_t *find_entry(section_t *section, char *name)
{
        entry_t *current_entry;
        
        current_entry = (entry_t *)section->entry_head.next;
        
        while (current_entry)
        {
                if (!strncmp(current_entry->name, name, 256))
                        return current_entry;

                current_entry = (entry_t *)current_entry->list.next;
        }
        return NULL;
}

static section_t *create_section(char *name)
{
        section_t *new_section = malloc(sizeof(section_t));

        memset(new_section, 0, sizeof(section_t));
        strncpy(new_section->name, name, 256);
        list_add(&new_section->list, &config_head);
        
        return new_section;
}

static entry_t *create_entry(section_t *section, char *name)
{
        entry_t *new_entry = malloc(sizeof(entry_t));
        memset(new_entry, 0, sizeof(entry_t));
        strncpy(new_entry->name, name, 256);
        list_add(&new_entry->list, &section->entry_head);
        
        return new_entry;
}
        
int config_get_int(char *head, char *name, int def)
{
        section_t *section;
        entry_t *entry;
        int value;

        section = find_section(head);
        
        if (!section)
                return def;
                
        entry = find_entry(section, name);

        if (!entry)
                return def;
        
        sscanf(entry->data, "%i", &value);
        
        return value;
}

int config_get_hex16(char *head, char *name, int def)
{
        section_t *section;
        entry_t *entry;
        int value;

        section = find_section(head);
        
        if (!section)
                return def;
                
        entry = find_entry(section, name);

        if (!entry)
                return def;
        
        sscanf(entry->data, "%04X", &value);
        
        return value;
}

char *config_get_string(char *head, char *name, char *def)
{
        section_t *section;
        entry_t *entry;

        section = find_section(head);
        
        if (!section)
                return def;
                
        entry = find_entry(section, name);

        if (!entry)
                return def;
       
        return entry->data; 
}

wchar_t *config_get_wstring(char *head, char *name, wchar_t *def)
{
        section_t *section;
        entry_t *entry;

        section = find_section(head);
        
        if (!section)
                return def;
                
        entry = find_entry(section, name);

        if (!entry)
                return def;
       
        return entry->wdata; 
}

void config_set_int(char *head, char *name, int val)
{
        section_t *section;
        entry_t *entry;

        section = find_section(head);
        
        if (!section)
                section = create_section(head);
                
        entry = find_entry(section, name);

        if (!entry)
                entry = create_entry(section, name);

        sprintf(entry->data, "%i", val);
	mbstowcs(entry->wdata, entry->data, 512);
}

void config_set_hex16(char *head, char *name, int val)
{
        section_t *section;
        entry_t *entry;

        section = find_section(head);
        
        if (!section)
                section = create_section(head);
                
        entry = find_entry(section, name);

        if (!entry)
                entry = create_entry(section, name);

        sprintf(entry->data, "%04X", val);
	mbstowcs(entry->wdata, entry->data, 512);
}

void config_set_string(char *head, char *name, char *val)
{
        section_t *section;
        entry_t *entry;

        section = find_section(head);
        
        if (!section)
                section = create_section(head);
                
        entry = find_entry(section, name);

        if (!entry)
                entry = create_entry(section, name);

        strncpy(entry->data, val, 256);
	mbstowcs(entry->wdata, entry->data, 256);
}

void config_set_wstring(char *head, char *name, wchar_t *val)
{
        section_t *section;
        entry_t *entry;

        section = find_section(head);
        
        if (!section)
                section = create_section(head);
                
        entry = find_entry(section, name);

        if (!entry)
                entry = create_entry(section, name);

        memcpy(entry->wdata, val, 512);
}


char *get_filename(char *s)
{
        int c = strlen(s) - 1;
        while (c > 0)
        {
                if (s[c] == '/' || s[c] == '\\')
                   return &s[c+1];
               c--;
        }
        return s;
}

wchar_t *get_filename_w(wchar_t *s)
{
        int c = wcslen(s) - 1;
        while (c > 0)
        {
                if (s[c] == L'/' || s[c] == L'\\')
                   return &s[c+1];
               c--;
        }
        return s;
}

void append_filename(char *dest, char *s1, char *s2, int size)
{
        sprintf(dest, "%s%s", s1, s2);
}

void append_filename_w(wchar_t *dest, wchar_t *s1, wchar_t *s2, int size)
{
        _swprintf(dest, L"%s%s", s1, s2);
}

void put_backslash(char *s)
{
        int c = strlen(s) - 1;
        if (s[c] != '/' && s[c] != '\\')
           s[c] = '/';
}

void put_backslash_w(wchar_t *s)
{
        int c = wcslen(s) - 1;
        if (s[c] != L'/' && s[c] != L'\\')
           s[c] = L'/';
}

char *get_extension(char *s)
{
        int c = strlen(s) - 1;

        if (c <= 0)
                return s;
        
        while (c && s[c] != '.')
                c--;
                
        if (!c)
                return &s[strlen(s)];

        return &s[c+1];
}               

wchar_t *get_extension_w(wchar_t *s)
{
        int c = wcslen(s) - 1;

        if (c <= 0)
                return s;
        
        while (c && s[c] != L'.')
                c--;
                
        if (!c)
                return &s[wcslen(s)];

        return &s[c+1];
}               

static wchar_t wname[512];

void config_save(wchar_t *fn)
{
        FILE *f = _wfopen(fn, L"wt, ccs=UNICODE");
        section_t *current_section;

	int fl = 0;
        
        current_section = (section_t *)config_head.next;
        
        while (current_section)
        {
                entry_t *current_entry;
                
                if (current_section->name[0])
		{
			mbstowcs(wname, current_section->name, strlen(current_section->name) + 1);
			if (fl)
			{
	                        fwprintf(f, L"\n[%ws]\n", wname);
			}
			else
			{
	                        fwprintf(f, L"[%ws]\n", wname);
			}
			fl++;
		}
                
                current_entry = (entry_t *)current_section->entry_head.next;
                
                while (current_entry)
                {
			mbstowcs(wname, current_entry->name, strlen(current_entry->name) + 1);
			if (current_entry->wdata[0] == L'\0')
			{
	                        fwprintf(f, L"%ws = \n", wname);
			}
			else
			{
	                        fwprintf(f, L"%ws = %ws\n", wname, current_entry->wdata);
			}
			fl++;

                        current_entry = (entry_t *)current_entry->list.next;
                }

                current_section = (section_t *)current_section->list.next;
        }
        
        fclose(f);
}

static char temps[512];

void loadconfig_general(void)
{
        wchar_t *wp;
        char *p;

	/* General */
        vid_resize = !!config_get_int("General", "vid_resize", 0);

        p = (char *)config_get_string("General", "vid_renderer", "d3d9");
	memset(temps, 0, 512);
	if (p)
	{
		strcpy(temps, p);
	}
	if (!strcmp(temps, "ddraw"))
	{
		vid_api = 0;
	}
	else
	{
		vid_api = 1;
	}

        video_fullscreen_scale = config_get_int("General", "video_fullscreen_scale", 0);
        video_fullscreen_first = config_get_int("General", "video_fullscreen_first", 1);

	force_43 = !!config_get_int("General", "force_43", 0);
	scale = !!config_get_int("General", "scale", 1);
	enable_overscan = !!config_get_int("General", "enable_overscan", 0);

	p = config_get_string("General", "window_coordinates", "0, 0, 0, 0");
	if (p)
	{
		sscanf(p, "%i, %i, %i, %i", &window_w, &window_h, &window_x, &window_y);
	}
	else
	{
		sscanf("0, 0, 0, 0", "%i, %i, %i, %i", &window_w, &window_h, &window_x, &window_y);
	}
        window_remember = config_get_int("General", "window_remember", 0);

	memset(nvr_path, 0, 2048);
        wp = (wchar_t *)config_get_wstring("General", "nvr_path", L"");
        if (wp) {
		if (wcslen(wp) && (wcslen(wp) <= 992))  wcscpy(nvr_path, wp);
		else
		{
			append_filename_w(nvr_path, pcempath, L"nvr", 511);
		}
	}
        else   append_filename_w(nvr_path, pcempath, L"nvr", 511);

	if (nvr_path[wcslen(nvr_path) - 1] != L'/')
	{
		if (nvr_path[wcslen(nvr_path) - 1] != L'\\')
		{
			nvr_path[wcslen(nvr_path)] = L'/';
			nvr_path[wcslen(nvr_path) + 1] = L'\0';
		}
	}

	path_len = wcslen(nvr_path);

#ifndef __unix
	/* Currently, 86Box is English (US) only, but in the future (version 1.30 at the earliest) other languages will be added,
	   therefore it is better to future-proof the code. */
	dwLanguage = config_get_hex16("General", "language", 0x0409);
#endif
}

static void loadconfig_machine(void)
{
        char *p;

	/* Machine */
        p = (char *)config_get_string("Machine", "model", "");
        if (p)
                model = model_get_model_from_internal_name(p);
        else
                model = 0;

        if (model >= model_count())
                model = model_count() - 1;

        romset = model_getromset();
        cpu_manufacturer = config_get_int("Machine", "cpu_manufacturer", 0);
        cpu = config_get_int("Machine", "cpu", 0);

	cpu_waitstates = config_get_int("Machine", "cpu_waitstates", 0);

        mem_size = config_get_int("Machine", "mem_size", 4096);
        if (mem_size < ((models[model].flags & MODEL_AT) ? models[model].min_ram*1024 : models[model].min_ram))
                mem_size = ((models[model].flags & MODEL_AT) ? models[model].min_ram*1024 : models[model].min_ram);
	if (mem_size > 1048576)
	{
		mem_size = 1048576;
	}

        cpu_use_dynarec = !!config_get_int("Machine", "cpu_use_dynarec", 0);

	enable_external_fpu = !!config_get_int("Machine", "cpu_enable_fpu", 0);

        enable_sync = !!config_get_int("Machine", "enable_sync", 1);
}

static void loadconfig_video(void)
{
        char *p;

	/* Video */
        p = (char *)config_get_string("Video", "gfxcard", "");
        if (p)
                gfxcard = video_get_video_from_internal_name(p);
        else
                gfxcard = 0;

        video_speed = config_get_int("Video", "video_speed", 3);

        voodoo_enabled = !!config_get_int("Video", "voodoo", 0);
}

static char s[512];

static void loadconfig_input_devices(void)
{
	int c, d;
        char *p;

	/* Input devices */
        p = (char *)config_get_string("Input devices", "mouse_type", "");
        if (p)
                mouse_type = mouse_get_from_internal_name(p);
        else
                mouse_type = 0;

        joystick_type = config_get_int("Input devices", "joystick_type", 0);

        for (c = 0; c < joystick_get_max_joysticks(joystick_type); c++)
        {
                sprintf(s, "joystick_%i_nr", c);
                joystick_state[c].plat_joystick_nr = config_get_int("Input devices", s, 0);

                if (joystick_state[c].plat_joystick_nr)
                {
                        for (d = 0; d < joystick_get_axis_count(joystick_type); d++)
                        {                        
                                sprintf(s, "joystick_%i_axis_%i", c, d);
                                joystick_state[c].axis_mapping[d] = config_get_int("Input devices", s, d);
                        }
                        for (d = 0; d < joystick_get_button_count(joystick_type); d++)
                        {                        
                                sprintf(s, "joystick_%i_button_%i", c, d);
                                joystick_state[c].button_mapping[d] = config_get_int("Input devices", s, d);
                        }
                        for (d = 0; d < joystick_get_pov_count(joystick_type); d++)
                        {                        
                                sprintf(s, "joystick_%i_pov_%i", c, d);
			        p = (char *)config_get_string("Input devices", s, "0, 0");
				joystick_state[c].pov_mapping[d][0] = joystick_state[c].pov_mapping[d][1] = 0;
				sscanf(p, "%i, %i", &joystick_state[c].pov_mapping[d][0], &joystick_state[c].pov_mapping[d][1]);
                        }
                }
        }
}

static void loadconfig_sound(void)
{
        char *p;

	/* Sound */
        p = (char *)config_get_string("Sound", "sndcard", "");
        if (p)
                sound_card_current = sound_card_get_from_internal_name(p);
        else
                sound_card_current = 0;

        midi_id = config_get_int("Sound", "midi_host_device", 0);

        SSI2001 = !!config_get_int("Sound", "ssi2001", 0);
        GAMEBLASTER = !!config_get_int("Sound", "gameblaster", 0);
        GUS = !!config_get_int("Sound", "gus", 0);
        opl3_type = !!config_get_int("Sound", "opl3_type", 1);
}

static void loadconfig_network(void)
{
        char *p;

	/* Network */
	network_type = config_get_int("Network", "net_type", -1);
	memset(pcap_dev, 0, 512);
        p = (char *)config_get_string("Network", "net_pcap_device", "none");
        if (p)
		if ((network_dev_to_id(p) == -1) || (netdev_num == 1))
		{
			if ((netdev_num == 1) && strcmp(pcap_dev, "none"))
			{
				msgbox_error(ghwnd, 2107);
			}
			else if (network_dev_to_id(p) == -1)
			{
				msgbox_error(ghwnd, 2200);
			}

	                strcpy(pcap_dev, "none");
		}
		else
		{
	                strcpy(pcap_dev, p);
		}
        else
                strcpy(pcap_dev, "none");
	p = (char *)config_get_string("Network", "net_card", NULL);
	network_card = (p) ? network_card_get_from_internal_name(p) : 0;
}

static void loadconfig_other_peripherals(void)
{
	int c;
        char *p;

	/* Other peripherals */
        p = (char *)config_get_string("Other peripherals", "scsicard", "");
        if (p)
                scsi_card_current = scsi_card_get_from_internal_name(p);
        else
                scsi_card_current = 0;

	memset(hdd_controller_name, 0, 16);
        p = (char *)config_get_string("Other peripherals", "hdd_controller", "");
        if (p)
                strcpy(hdd_controller_name, p);
        else
                strcpy(hdd_controller_name, "none");

	memset(temps, 0, 512);
	for (c = 2; c < 4; c++)
	{
		sprintf(temps, "ide_%02i", c + 1);
		p = config_get_string("Other peripherals", temps, "0, 00");
		if (p)
		{
			sscanf(p, "%i, %02i", &ide_enable[c], &ide_irq[c]);
		}
		else
		{
			sscanf(p, "0, 00", &ide_enable[c], &ide_irq[c]);
		}
	}

        serial_enabled[0] = !!config_get_int("Other peripherals", "serial1_enabled", 1);
        serial_enabled[1] = !!config_get_int("Other peripherals", "serial2_enabled", 1);
        lpt_enabled = !!config_get_int("Other peripherals", "lpt_enabled", 1);
        bugger_enabled = !!config_get_int("Other peripherals", "bugger_enabled", 0);
}

static char temps2[512];

static int config_string_to_bus(char *str, int cdrom)
{
	if (!strcmp(str, "none"))
	{
		return 0;
	}

	if (!strcmp(str, "mfm"))
	{
		if (cdrom)  goto no_mfm_cdrom;

		return 1;
	}

	if (!strcmp(str, "rll"))
	{
		if (cdrom)  goto no_mfm_cdrom;

		return 1;
	}

	if (!strcmp(str, "esdi"))
	{
		if (cdrom)  goto no_mfm_cdrom;

		return 1;
	}

	if (!strcmp(str, "ide_pio_only"))
	{
		return 2;
	}

	if (!strcmp(str, "ide"))
	{
		return 2;
	}

	if (!strcmp(str, "eide"))
	{
		return 2;
	}

	if (!strcmp(str, "xtide"))
	{
		return 2;
	}

	if (!strcmp(str, "atide"))
	{
		return 2;
	}

	if (!strcmp(str, "ide_pio_and_dma"))
	{
		return 3;
	}

	if (!strcmp(str, "scsi"))
	{
		return 4;
	}

	if (!strcmp(str, "usb"))
	{
		msgbox_error(ghwnd, 2199);
		return 0;
	}

	return 0;

no_mfm_cdrom:
	msgbox_error(ghwnd, 2095);
	return 0;
}

static void loadconfig_hard_disks(void)
{
	int c;
        char *p;
        wchar_t *wp;

	/* Hard disks */
	memset(temps, 0, 512);
	for (c = 0; c < HDC_NUM; c++)
	{
		sprintf(temps, "hdd_%02i_parameters", c + 1);
		p = (char *)config_get_string("Hard disks", temps, "0, 0, 0, none");
		if (p)
		{
			sscanf(p, "%" PRIu64 ", %" PRIu64", %" PRIu64 ", %s", &hdc[c].spt, &hdc[c].hpc, &hdc[c].tracks, s);
		}
		else
		{
			sscanf("0, 0, 0, none", "%" PRIu64 ", %" PRIu64", %" PRIu64 ", %s", &hdc[c].spt, &hdc[c].hpc, &hdc[c].tracks, s);
		}

		hdc[c].bus = config_string_to_bus(s, 0);

		if (hdc[c].spt > 99)
		{
			hdc[c].spt = 99;
		}
		if (hdc[c].hpc > 64)
		{
			hdc[c].hpc = 64;
		}
		if (hdc[c].tracks > 266305)
		{
			hdc[c].tracks = 266305;
		}

		sprintf(temps, "hdd_%02i_mfm_channel", c + 1);
		hdc[c].mfm_channel = config_get_int("Hard disks", temps, 0);
		if (hdc[c].mfm_channel > 1)
		{
			hdc[c].mfm_channel = 1;
		}

		sprintf(temps, "hdd_%02i_ide_channel", c + 1);
		hdc[c].ide_channel = config_get_int("Hard disks", temps, 0);
		if (hdc[c].ide_channel > 7)
		{
			hdc[c].ide_channel = 7;
		}

		sprintf(temps, "hdd_%02i_scsi_location", c + 1);
		sprintf(temps2, "%02u:%02u", c, 0);
		p = (char *)config_get_string("Hard disks", temps, temps2);
		if (p)
		{
			sscanf(p, "%02u:%02u", &hdc[c].scsi_id, &hdc[c].scsi_lun);
		}
		else
		{
			sscanf(temps2, "%02u:%02u", &hdc[c].scsi_id, &hdc[c].scsi_lun);
		}

		if (hdc[c].scsi_id > 15)
		{
			hdc[c].scsi_id = 15;
		}
		if (hdc[c].scsi_lun > 7)
		{
			hdc[c].scsi_lun = 7;
		}

		memset(hdd_fn[c], 0, 1024);
		sprintf(temps, "hdd_%02i_fn", c + 1);
	        wp = (wchar_t *)config_get_wstring("Hard disks", temps, L"");
        	if (wp) memcpy(hdd_fn[c], wp, 512);
	        else    {
			memcpy(hdd_fn[c], L"", 2);
			hdd_fn[c][0] = L'\0';
		}
	}
}

static void loadconfig_removable_devices(void)
{
	int c;
        char *p;
        wchar_t *wp;

	/* Removable devices */
	for (c = 0; c < FDD_NUM; c++)
	{
		sprintf(temps, "fdd_%02i_type", c + 1);
	        p = (char *)config_get_string("Removable devices", temps, (c < 2) ? "525_2dd" : "none");
        	if (p)
                	fdd_set_type(c, fdd_get_from_internal_name(p));
	        else
        	        fdd_set_type(c, (c < 2) ? 2 : 0);
		if (fdd_get_type(c) > 13)
		{
			fdd_set_type(c, 13);
		}

		sprintf(temps, "fdd_%02i_fn", c + 1);
	        wp = (wchar_t *)config_get_wstring("Removable devices", temps, L"");
        	if (wp) memcpy(discfns[c], wp, 512);
	        else    {
			memcpy(discfns[c], L"", 2);
			discfns[c][0] = L'\0';
		}
		printf("Floppy: %ws\n", discfns[c]);
		sprintf(temps, "fdd_%02i_writeprot", c + 1);
	        ui_writeprot[c] = !!config_get_int("Removable devices", temps, 0);
	}

	memset(temps, 0, 512);
	for (c = 0; c < CDROM_NUM; c++)
	{
		sprintf(temps, "cdrom_%02i_host_drive", c + 1);
		cdrom_drives[c].host_drive = config_get_int("Removable devices", temps, 0);
		cdrom_drives[c].prev_host_drive = cdrom_drives[c].host_drive;

		sprintf(temps, "cdrom_%02i_parameters", c + 1);
		p = (char *)config_get_string("Removable devices", temps, "0, none");
		if (p)
		{
			sscanf(p, "%u, %s", &cdrom_drives[c].sound_on, s);
		}
		else
		{
			sscanf("0, none", "%u, %s", &cdrom_drives[c].sound_on, s);
		}

		cdrom_drives[c].bus_type = config_string_to_bus(s, 1);

		sprintf(temps, "cdrom_%02i_ide_channel", c + 1);
		cdrom_drives[c].ide_channel = config_get_int("Removable devices", temps, c + 2);
		if (cdrom_drives[c].ide_channel > 7)
		{
			cdrom_drives[c].ide_channel = 7;
		}

		sprintf(temps, "cdrom_%02i_scsi_location", c + 1);
		sprintf(temps2, "%02u:%02u", c + 2, 0);
		p = (char *)config_get_string("Removable devices", temps, temps2);
		if (p)
		{
			sscanf(p, "%02u:%02u", &cdrom_drives[c].scsi_device_id, &cdrom_drives[c].scsi_device_lun);
		}
		else
		{
			sscanf(temps2, "%02u:%02u", &cdrom_drives[c].scsi_device_id, &cdrom_drives[c].scsi_device_lun);
		}


		if (cdrom_drives[c].scsi_device_id > 15)
		{
			cdrom_drives[c].scsi_device_id = 15;
		}
		if (cdrom_drives[c].scsi_device_lun > 7)
		{
			cdrom_drives[c].scsi_device_lun = 7;
		}

		sprintf(temps, "cdrom_%02i_image_path", c + 1);
	        wp = (wchar_t *)config_get_wstring("Removable devices", temps, L"");
        	if (wp) memcpy(cdrom_image[c].image_path, wp, 512);
	        else    {
			memcpy(cdrom_image[c].image_path, L"", 2);
			cdrom_image[c].image_path[0] = L'\0';
		}
	}
}

void loadconfig(wchar_t *fn)
{
        if (!fn)
                config_load(config_file_default);
        else
                config_load(fn);

	/* General */
	loadconfig_general();

	/* Machine */
	loadconfig_machine();

	/* Video */
	loadconfig_video();

	/* Input devices */
	loadconfig_input_devices();

	/* Sound */
	loadconfig_sound();

	/* Network */
	loadconfig_network();

	/* Other peripherals */
	loadconfig_other_peripherals();

	/* Hard disks */
	loadconfig_hard_disks();

	/* Removable devices */
	loadconfig_removable_devices();

}

wchar_t temp_nvr_path[1024];

wchar_t *nvr_concat(wchar_t *to_concat)
{
	char *p;
	wchar_t *wp;

	memset(temp_nvr_path, 0, 2048);
	wcscpy(temp_nvr_path, nvr_path);

	p = (char *) temp_nvr_path;
	p += (path_len * 2);
	wp = (wchar_t *) p;

	wcscpy(wp, to_concat);
	return temp_nvr_path;
}

void saveconfig_general(void)
{
        config_set_int("General", "vid_resize", vid_resize);
	switch(vid_api)
	{
		case 0:
		        config_set_string("General", "vid_renderer", "ddraw");
			break;
		case 1:
		default:
		        config_set_string("General", "vid_renderer", "d3d9");
			break;
	}
        config_set_int("General", "video_fullscreen_scale", video_fullscreen_scale);
        config_set_int("General", "video_fullscreen_first", video_fullscreen_first);

        config_set_int("General", "force_43", force_43);
        config_set_int("General", "scale", scale);
        config_set_int("General", "enable_overscan", enable_overscan);

	sprintf(temps, "%i, %i, %i, %i", window_w, window_h, window_x, window_y);
	config_set_string("General", "window_coordinates", temps);
        config_set_int("General", "window_remember", window_remember);

        config_set_wstring("General", "nvr_path", nvr_path);

#ifndef __unix
        config_set_hex16("General", "language", dwLanguage);
#endif
}

void saveconfig_machine(void)
{
	/* Machine */
        config_set_string("Machine", "model", model_get_internal_name());
        config_set_int("Machine", "cpu_manufacturer", cpu_manufacturer);
        config_set_int("Machine", "cpu", cpu);
	config_set_int("Machine", "cpu_waitstates", cpu_waitstates);

        config_set_int("Machine", "mem_size", mem_size);
        config_set_int("Machine", "cpu_use_dynarec", cpu_use_dynarec);
        config_set_int("Machine", "cpu_enable_fpu", enable_external_fpu);
        config_set_int("Machine", "enable_sync", enable_sync);
}

void saveconfig_video(void)
{
	/* Video */
        config_set_string("Video", "gfxcard", video_get_internal_name(video_old_to_new(gfxcard)));
        config_set_int("Video", "video_speed", video_speed);
        config_set_int("Video", "voodoo", voodoo_enabled);
}

void saveconfig_input_devices(void)
{
        int c, d;

	/* Input devices */
	config_set_string("Input devices", "mouse_type", mouse_get_internal_name(mouse_type));

        config_set_int("Input devices", "joystick_type", joystick_type);

        for (c = 0; c < joystick_get_max_joysticks(joystick_type); c++)
        {
                sprintf(s, "joystick_%i_nr", c);
                config_set_int("Input devices", s, joystick_state[c].plat_joystick_nr);

                if (joystick_state[c].plat_joystick_nr)
                {
                        for (d = 0; d < joystick_get_axis_count(joystick_type); d++)
                        {                        
                                sprintf(s, "joystick_%i_axis_%i", c, d);
                                config_set_int("Input devices", s, joystick_state[c].axis_mapping[d]);
                        }
                        for (d = 0; d < joystick_get_button_count(joystick_type); d++)
                        {                        
                                sprintf(s, "joystick_%i_button_%i", c, d);
                                config_set_int("Input devices", s, joystick_state[c].button_mapping[d]);
                        }
                        for (d = 0; d < joystick_get_pov_count(joystick_type); d++)
                        {                        
                                sprintf(s, "joystick_%i_pov_%i", c, d);
				sprintf(temps, "%i, %i", joystick_state[c].pov_mapping[d][0], joystick_state[c].pov_mapping[d][1]);
                                config_set_string("Input devices", s, temps);
                        }
                }
        }
}

void saveconfig_sound(void)
{
	/* Sound */
	config_set_string("Sound", "sndcard", sound_card_get_internal_name(sound_card_current));

        config_set_int("Sound", "midi_host_device", midi_id);

        config_set_int("Sound", "gameblaster", GAMEBLASTER);
        config_set_int("Sound", "gus", GUS);
        config_set_int("Sound", "ssi2001", SSI2001);
        config_set_int("Sound", "opl3_type", opl3_type);
}

void saveconfig_network(void)
{
	/* Network */
	if (pcap_dev != NULL)
	{
		config_set_string("Network", "net_pcap_device", pcap_dev);
	}
	else
	{
		config_set_string("Network", "net_pcap_device", "none");
	}
	config_set_int("Network", "net_type", network_type);
	config_set_string("Network", "net_card", network_card_get_internal_name(network_card));
}

void saveconfig_other_peripherals(void)
{
        int c, d;

	/* Other peripherals */
	config_set_string("Other peripherals", "scsicard", scsi_card_get_internal_name(scsi_card_current));

        config_set_string("Other peripherals", "hdd_controller", hdd_controller_name);

	memset(temps, 0, 512);
	for (c = 2; c < 4; c++)
	{
		sprintf(temps, "ide_%02i", c + 1);
		sprintf(temps2, "%i, %02i", !!ide_enable[c], ide_irq[c]);
	        config_set_string("Other peripherals", temps, temps2);
	}

        config_set_int("Other peripherals", "serial1_enabled", serial_enabled[0]);
        config_set_int("Other peripherals", "serial2_enabled", serial_enabled[1]);
        config_set_int("Other peripherals", "lpt_enabled", lpt_enabled);
        config_set_int("Other peripherals", "bugger_enabled", bugger_enabled);
}

static char *config_bus_to_string(int bus)
{
	switch (bus)
	{
		case 0:
		default:
			return "none";
			break;
		case 1:
			return "mfm";
			break;
		case 2:
			return "ide_pio_only";
			break;
		case 3:
			return "ide_pio_and_dma";
			break;
		case 4:
			return "scsi";
			break;
	}
}

void saveconfig_hard_disks(void)
{
        int c, d;
	char *p;

	/* Hard disks */
	memset(temps, 0, 512);
	for (c = 0; c < HDC_NUM; c++)
	{
		sprintf(temps, "hdd_%02i_parameters", c + 1);
		memset(s, 0, 512);
		p = config_bus_to_string(hdc[c].bus);
		sprintf(temps2, "%" PRIu64 ", %" PRIu64", %" PRIu64 ", %s", hdc[c].spt, hdc[c].hpc, hdc[c].tracks, p);
		config_set_string("Hard disks", temps, temps2);

		sprintf(temps, "hdd_%02i_mfm_channel", c + 1);
		config_set_int("Hard disks", temps, hdc[c].mfm_channel);

		sprintf(temps, "hdd_%02i_ide_channel", c + 1);
		config_set_int("Hard disks", temps, hdc[c].ide_channel);

		sprintf(temps, "hdd_%02i_scsi_location", c + 1);
		sprintf(temps2, "%02u:%02u", hdc[c].scsi_id, hdc[c].scsi_lun);
		config_set_string("Hard disks", temps, temps2);

		sprintf(temps, "hdd_%02i_fn", c + 1);
	        config_set_wstring("Hard disks", temps, hdd_fn[c]);
	}
}

void saveconfig_removable_devices(void)
{
        int c, d;

	/* Removable devices */
	memset(temps, 0, 512);
	for (c = 0; c < FDD_NUM; c++)
	{
		sprintf(temps, "fdd_%02i_type", c + 1);
	        config_set_string("Removable devices", temps, fdd_get_internal_name(fdd_get_type(c)));
		sprintf(temps, "fdd_%02i_fn", c + 1);
	        config_set_wstring("Removable devices", temps, discfns[c]);
		sprintf(temps, "fdd_%02i_writeprot", c + 1);
	        config_set_int("Removable devices", temps, ui_writeprot[c]);
	}

	memset(temps, 0, 512);
	for (c = 0; c < CDROM_NUM; c++)
	{
		sprintf(temps, "cdrom_%02i_host_drive", c + 1);
		config_set_int("Removable devices", temps, cdrom_drives[c].host_drive);

		sprintf(temps, "cdrom_%02i_parameters", c + 1);
		sprintf(temps2, "%u, %s", cdrom_drives[c].sound_on, config_bus_to_string(cdrom_drives[c].bus_type));
		config_set_string("Removable devices", temps, temps2);
		
		sprintf(temps, "cdrom_%02i_ide_channel", c + 1);
		config_set_int("Removable devices", temps, cdrom_drives[c].ide_channel);

		sprintf(temps, "cdrom_%02i_scsi_location", c + 1);
		sprintf(temps2, "%02u:%02u", cdrom_drives[c].scsi_device_id, cdrom_drives[c].scsi_device_lun);
		config_set_string("Removable devices", temps, temps2);

		sprintf(temps, "cdrom_%02i_image_path", c + 1);
		config_set_wstring("Removable devices", temps, cdrom_image[c].image_path);
	}
}

void saveconfig(void)
{
        int c, d;

	/* General */
	saveconfig_general();

	/* Machine */
	saveconfig_machine();

	/* Video */
	saveconfig_video();

	/* Input devices */
	saveconfig_input_devices();

	/* Sound */
	saveconfig_sound();

	/* Network */
	saveconfig_network();

	/* Other peripherals */
	saveconfig_other_peripherals();

	/* Hard disks */
	saveconfig_hard_disks();

	/* Removable devices */
	saveconfig_removable_devices();

        config_save(config_file_default);
}
