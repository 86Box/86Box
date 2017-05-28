/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

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
#include "scsi.h"
#include "win/plat_joystick.h"
#include "win/plat_midi.h"
#include "sound/snd_dbopl.h"
#include "sound/snd_opl.h"
#include "sound/sound.h"
#include "video/video.h"

#include "win/win.h"
#include "win/win_language.h"


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


static void config_free(void)
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


static int entries_num(section_t *section)
{
        entry_t *current_entry;
	int i = 0;
        
        current_entry = (entry_t *)section->entry_head.next;
        
        while (current_entry)
        {
                if (strlen(current_entry->name) > 0)
		{
                        i++;
		}

                current_entry = (entry_t *)current_entry->list.next;
        }
        return i;
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


int config_get_hex20(char *head, char *name, int def)
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
        
        sscanf(entry->data, "%05X", &value);
        
        return value;
}


int config_get_mac(char *head, char *name, int def)
{
        section_t *section;
        entry_t *entry;
        int val0 = 0, val1 = 0, val2 = 0;

        section = find_section(head);
        
        if (!section)
                return def;
                
        entry = find_entry(section, name);

        if (!entry)
                return def;
        
        sscanf(entry->data, "%02x:%02x:%02x", &val0, &val1, &val2);

        return (val0 << 16) + (val1 << 8) + val2;
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


void config_delete_var(char *head, char *name)
{
        section_t *section;
        entry_t *entry;

        section = find_section(head);
        
        if (!section)
                return;
                
        entry = find_entry(section, name);

        if (!entry)
                return;

	memset(entry->name, 0, strlen(entry->name));
        
        return;
}


void config_delete_section_if_empty(char *head)
{
        section_t *section;
        entry_t *entry;

        section = find_section(head);
        
        if (!section)
                return;

	if (entries_num(section) == 0)
	{
		memset(section->name, 0, strlen(section->name));
	}
        
        return;
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


void config_set_hex20(char *head, char *name, int val)
{
        section_t *section;
        entry_t *entry;

        section = find_section(head);
        
        if (!section)
                section = create_section(head);
                
        entry = find_entry(section, name);

        if (!entry)
                entry = create_entry(section, name);

        sprintf(entry->data, "%05X", val);
	mbstowcs(entry->wdata, entry->data, 512);
}


void config_set_mac(char *head, char *name, int val)
{
        section_t *section;
        entry_t *entry;

        section = find_section(head);
        
        if (!section)
                section = create_section(head);
                
        entry = find_entry(section, name);

        if (!entry)
                entry = create_entry(section, name);

        sprintf(entry->data, "%02x:%02x:%02x", (val >> 16) & 0xff, (val >> 8) & 0xff, val & 0xff);
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
			if(strlen(current_entry->name) > 0)
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
			}

                        current_entry = (entry_t *)current_entry->list.next;
                }

                current_section = (section_t *)current_section->list.next;
        }
        
        fclose(f);
}



static wchar_t *read_nvr_path;

/* General */
static void loadconfig_general(void)
{
	char *cat = "General";
	char temps[512];
        wchar_t *wp;
        char *p;

        vid_resize = !!config_get_int(cat, "vid_resize", 0);

	memset(temps, '\0', sizeof(temps));
        p = config_get_string(cat, "vid_renderer", "d3d9");
	if (p != NULL)
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

	config_delete_var(cat, "vid_api");

        video_fullscreen_scale = config_get_int(cat, "video_fullscreen_scale", 0);
        video_fullscreen_first = config_get_int(cat, "video_fullscreen_first", 1);

	force_43 = !!config_get_int(cat, "force_43", 0);
	scale = config_get_int(cat, "scale", 1);
	if (scale > 3)
	{
		scale = 3;
	}
	enable_overscan = !!config_get_int(cat, "enable_overscan", 0);

        window_remember = config_get_int(cat, "window_remember", 0);

	if (window_remember)
	{
		p = config_get_string(cat, "window_coordinates", NULL);
		if (p == NULL)
			p = "0, 0, 0, 0";
		sscanf(p, "%i, %i, %i, %i", &window_w, &window_h, &window_x, &window_y);
	}
	else
	{
	        config_delete_var(cat, "window_remember");
	        config_delete_var(cat, "window_coordinates");
		window_w = window_h = window_x = window_y = 0;
	}

	if (read_nvr_path != NULL)
	{
		free(read_nvr_path);
		read_nvr_path = NULL;
	}

	memset(nvr_path, 0x00, sizeof(nvr_path));
        wp = config_get_wstring(cat, "nvr_path", L"");
        if (wp != NULL) {
		if (wcslen(wp) && (wcslen(wp) <= 992))
		{
			read_nvr_path = (wchar_t *) malloc((wcslen(wp) << 1) + 2);
			wcscpy(read_nvr_path, wp);
			wcscpy(nvr_path, wp);
		}
		else
		{
			append_filename_w(nvr_path, pcempath, L"nvr\\", 511);
		}
	}
        else   append_filename_w(nvr_path, pcempath, L"nvr\\", 511);

	if (nvr_path[wcslen(nvr_path) - 1] != L'/')
	{
		if (nvr_path[wcslen(nvr_path) - 1] != L'\\')
		{
			nvr_path[wcslen(nvr_path)] = L'\\';
			nvr_path[wcslen(nvr_path) + 1] = L'\0';
		}
	}

	path_len = wcslen(nvr_path);

#ifndef __unix
	/* Currently, 86Box is English (US) only, but in the future (version 1.30 at the earliest) other languages will be added,
	   therefore it is better to future-proof the code. */
	dwLanguage = config_get_hex16(cat, "language", 0x0409);
#endif
}


/* Machine */
static void loadconfig_machine(void)
{
	char *cat = "Machine";
        char *p;

        p = config_get_string(cat, "model", NULL);
        if (p != NULL)
                model = model_get_model_from_internal_name(p);
        else
                model = 0;
        if (model >= model_count())
                model = model_count() - 1;

        romset = model_getromset();
        cpu_manufacturer = config_get_int(cat, "cpu_manufacturer", 0);
        cpu = config_get_int(cat, "cpu", 0);
	cpu_waitstates = config_get_int(cat, "cpu_waitstates", 0);

        mem_size = config_get_int(cat, "mem_size", 4096);
        if (mem_size < ((models[model].flags & MODEL_AT) ? models[model].min_ram*1024 : models[model].min_ram))
                mem_size = ((models[model].flags & MODEL_AT) ? models[model].min_ram*1024 : models[model].min_ram);
	if (mem_size > 262144)
	{
		mem_size = 262144;
	}

        cpu_use_dynarec = !!config_get_int(cat, "cpu_use_dynarec", 0);

	enable_external_fpu = !!config_get_int(cat, "cpu_enable_fpu", 0);

        enable_sync = !!config_get_int(cat, "enable_sync", 1);
}


/* Video */
static void loadconfig_video(void)
{
	char *cat = "Video";
        char *p;

        p = config_get_string(cat, "gfxcard", NULL);
        if (p != NULL)
                gfxcard = video_get_video_from_internal_name(p);
        else
                gfxcard = 0;

        video_speed = config_get_int(cat, "video_speed", 3);

        voodoo_enabled = !!config_get_int(cat, "voodoo", 0);
}


/* Input devices */
static void loadconfig_input_devices(void)
{
	char *cat = "Input devices";
	char temps[512];
	int c, d;
        char *p;

        p = config_get_string(cat, "mouse_type", NULL);
        if (p != NULL)
                mouse_type = mouse_get_from_internal_name(p);
        else
                mouse_type = 0;

        joystick_type = config_get_int(cat, "joystick_type", 7);

        for (c = 0; c < joystick_get_max_joysticks(joystick_type); c++)
        {
                sprintf(temps, "joystick_%i_nr", c);
                joystick_state[c].plat_joystick_nr = config_get_int(cat, temps, 0);

                if (joystick_state[c].plat_joystick_nr)
                {
                        for (d = 0; d < joystick_get_axis_count(joystick_type); d++)
                        {                        
                                sprintf(temps, "joystick_%i_axis_%i", c, d);
                                joystick_state[c].axis_mapping[d] = config_get_int(cat, temps, d);
                        }
                        for (d = 0; d < joystick_get_button_count(joystick_type); d++)
                        {                        
                                sprintf(temps, "joystick_%i_button_%i", c, d);
                                joystick_state[c].button_mapping[d] = config_get_int(cat, temps, d);
                        }
                        for (d = 0; d < joystick_get_pov_count(joystick_type); d++)
                        {                        
                                sprintf(temps, "joystick_%i_pov_%i", c, d);
			        p = config_get_string(cat, temps, "0, 0");
				joystick_state[c].pov_mapping[d][0] = joystick_state[c].pov_mapping[d][1] = 0;
				sscanf(p, "%i, %i", &joystick_state[c].pov_mapping[d][0], &joystick_state[c].pov_mapping[d][1]);
                        }
                }
        }
}


/* Sound */
static void loadconfig_sound(void)
{
	char *cat = "Sound";
	char temps[512];
        char *p;

        p = config_get_string(cat, "sndcard", NULL);
        if (p != NULL)
                sound_card_current = sound_card_get_from_internal_name(p);
        else
                sound_card_current = 0;

        midi_id = config_get_int(cat, "midi_host_device", 0);

        SSI2001 = !!config_get_int(cat, "ssi2001", 0);
        GAMEBLASTER = !!config_get_int(cat, "gameblaster", 0);
        GUS = !!config_get_int(cat, "gus", 0);

	memset(temps, '\0', sizeof(temps));
        p = config_get_string(cat, "opl3_type", "dbopl");
	if (p != NULL)
	{
		strcpy(temps, p);
	}
	if (!strcmp(temps, "nukedopl") || !strcmp(temps, "1"))
	{
		opl3_type = 1;
	}
	else
	{
		opl3_type = 0;
	}
}


/* Network */
static void loadconfig_network(void)
{
	char *cat = "Network";
	char temps[512];
        char *p;

	memset(temps, '\0', sizeof(temps));
        p = config_get_string(cat, "net_type", "none");
	if (p != NULL)
	{
		strcpy(temps, p);
	}
	if (!strcmp(temps, "slirp") || !strcmp(temps, "2"))
	{
		network_type = NET_TYPE_SLIRP;
	}
	else if (!strcmp(temps, "pcap") || !strcmp(temps, "1"))
	{
		network_type = NET_TYPE_PCAP;
	}
	else
	{
		network_type = NET_TYPE_NONE;
	}

	memset(network_pcap, '\0', sizeof(network_pcap));
        p = config_get_string(cat, "net_pcap_device", "none");
        if (p != NULL)
		if ((network_dev_to_id(p) == -1) || (network_ndev == 1))
		{
			if ((network_ndev == 1) && strcmp(network_pcap, "none"))
			{
				msgbox_error(ghwnd, 2107);
			}
			else if (network_dev_to_id(p) == -1)
			{
				msgbox_error(ghwnd, 2200);
			}

	                strcpy(network_pcap, "none");
		}
		else
		{
	                strcpy(network_pcap, p);
		}
        else
                strcpy(network_pcap, "none");
	p = config_get_string(cat, "net_card", NULL);
	if (p != NULL)
		network_card = network_card_get_from_internal_name(p);
	else
		network_card = 0;
}


/* Other peripherals */
static void loadconfig_other_peripherals(void)
{
	char *cat = "Other peripherals";
	char temps[512];
        char *p;
	int c;

        p = config_get_string(cat, "scsicard", NULL);
        if (p != NULL)
                scsi_card_current = scsi_card_get_from_internal_name(p);
        else
                scsi_card_current = 0;

	memset(hdd_controller_name, '\0', sizeof(hdd_controller_name));
        p = config_get_string(cat, "hdd_controller", NULL);
        if (p != NULL)
                strcpy(hdd_controller_name, p);
        else
                strcpy(hdd_controller_name, "none");

	memset(temps, '\0', sizeof(temps));
	for (c = 2; c < 4; c++)
	{
		sprintf(temps, "ide_%02i", c + 1);
		p = config_get_string(cat, temps, NULL);
		if (p == NULL)
			p = "0, 00";
		sscanf(p, "%i, %02i", &ide_enable[c], &ide_irq[c]);
	}

        serial_enabled[0] = !!config_get_int(cat, "serial1_enabled", 1);
        serial_enabled[1] = !!config_get_int(cat, "serial2_enabled", 1);
        lpt_enabled = !!config_get_int(cat, "lpt_enabled", 1);
        bugger_enabled = !!config_get_int(cat, "bugger_enabled", 0);
}


static int config_string_to_bus(char *str, int cdrom)
{
	if (!strcmp(str, "none"))
	{
		return HDD_BUS_DISABLED;
	}

	if (!strcmp(str, "mfm"))
	{
		if (cdrom)  goto no_mfm_cdrom;

		return HDD_BUS_MFM;
	}

	if (!strcmp(str, "rll"))
	{
		if (cdrom)  goto no_mfm_cdrom;

		return HDD_BUS_RLL;
	}

	if (!strcmp(str, "esdi"))
	{
		if (cdrom)  goto no_mfm_cdrom;

		return HDD_BUS_RLL;
	}

	if (!strcmp(str, "ide_pio_only"))
	{
		return HDD_BUS_IDE_PIO_ONLY;
	}

	if (!strcmp(str, "ide"))
	{
		return HDD_BUS_IDE_PIO_ONLY;
	}

	if (!strcmp(str, "atapi_pio_only"))
	{
		return HDD_BUS_IDE_PIO_ONLY;
	}

	if (!strcmp(str, "atapi"))
	{
		return HDD_BUS_IDE_PIO_ONLY;
	}

	if (!strcmp(str, "eide"))
	{
		return HDD_BUS_IDE_PIO_ONLY;
	}

	if (!strcmp(str, "xtide"))
	{
		return HDD_BUS_XTIDE;
	}

	if (!strcmp(str, "atide"))
	{
		return HDD_BUS_IDE_PIO_ONLY;
	}

	if (!strcmp(str, "ide_pio_and_dma"))
	{
		return HDD_BUS_IDE_PIO_AND_DMA;
	}

	if (!strcmp(str, "atapi_pio_and_dma"))
	{
		return HDD_BUS_IDE_PIO_AND_DMA;
	}

	if (!strcmp(str, "scsi"))
	{
		return HDD_BUS_SCSI;
	}

	if (!strcmp(str, "removable"))
	{
		if (cdrom)  goto no_mfm_cdrom;

		return HDD_BUS_SCSI_REMOVABLE;
	}

	if (!strcmp(str, "scsi_removable"))
	{
		if (cdrom)  goto no_mfm_cdrom;

		return HDD_BUS_SCSI_REMOVABLE;
	}

	if (!strcmp(str, "removable_scsi"))
	{
		if (cdrom)  goto no_mfm_cdrom;

		return HDD_BUS_SCSI_REMOVABLE;
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


static int hard_disk_is_valid(int c)
{
	if (hdc[c].bus == HDD_BUS_DISABLED)
	{
		return 0;
	}

	if ((wcslen(hdc[c].fn) == 0) && (hdc[c].bus != HDD_BUS_SCSI_REMOVABLE))
	{
		return 0;
	}

	if ((hdc[c].tracks == 0) || (hdc[c].hpc == 0) || (hdc[c].spt == 0))
	{
		return 0;
	}

	return 1;
}


static int tally_char(char *string, char c)
{
	int i = 0;
	int tally = 0;

	if (string == NULL)
	{
		return 0;
	}

	if (strlen(string) == 0)
	{
		return 0;
	}

	for (i = 0; i < strlen(string); i++)
	{
		if (string[i] == c)
		{
			tally++;
		}
	}

	return tally;
}


/* Hard disks */
static void loadconfig_hard_disks(void)
{
	char *cat = "Hard disks";
	char temps[512];
	char temps2[512];
	char s[512];
	int c;
        char *p;
        wchar_t *wp;
	int max_spt, max_hpc, max_tracks;
	int board = 0, dev = 0;

	memset(temps, '\0', sizeof(temps));
	for (c = 0; c < HDC_NUM; c++)
	{
		sprintf(temps, "hdd_%02i_parameters", c + 1);
		p = config_get_string(cat, temps, NULL);
		if (p == NULL)
			p = "0, 0, 0, 0, none";
		if (tally_char(p, ',') == 3)
		{
			sscanf(p, "%" PRIu64 ", %" PRIu64", %" PRIu64 ", %s", &hdc[c].spt, &hdc[c].hpc, &hdc[c].tracks, s);
			hdc[c].wp = 0;
		}
		else
		{
			sscanf(p, "%" PRIu64 ", %" PRIu64", %" PRIu64 ", %i, %s", &hdc[c].spt, &hdc[c].hpc, &hdc[c].tracks, &hdc[c].wp, s);
		}

		hdc[c].bus = config_string_to_bus(s, 0);

		switch(hdc[c].bus)
		{
			case HDD_BUS_DISABLED:
			default:
				max_spt = max_hpc = max_tracks = 0;
				break;
			case HDD_BUS_MFM:
				max_spt = 17;
				max_hpc = 15;
				max_tracks = 1023;
				break;
			case HDD_BUS_RLL:
			case HDD_BUS_XTIDE:
				max_spt = 63;
				max_hpc = 16;
				max_tracks = 1023;
				break;
			case HDD_BUS_IDE_PIO_ONLY:
			case HDD_BUS_IDE_PIO_AND_DMA:
				max_spt = 63;
				max_hpc = 16;
				max_tracks = 266305;
				break;
			case HDD_BUS_SCSI:
			case HDD_BUS_SCSI_REMOVABLE:
				max_spt = 99;
				max_hpc = 255;
				max_tracks = 266305;
				break;
		}

		if (hdc[c].spt > max_spt)
		{
			hdc[c].spt = max_spt;
		}
		if (hdc[c].hpc > max_hpc)
		{
			hdc[c].hpc = max_hpc;
		}
		if (hdc[c].tracks > max_tracks)
		{
			hdc[c].tracks = max_tracks;
		}

		/* MFM */
		sprintf(temps, "hdd_%02i_mfm_channel", c + 1);
		if (hdc[c].bus == HDD_BUS_MFM)
		{
			hdc[c].mfm_channel = !!config_get_int(cat, temps, c & 1);
		}
		else
		{
			config_delete_var(cat, temps);
		}

		/* XT IDE */
		sprintf(temps, "hdd_%02i_xtide_channel", c + 1);
		if (hdc[c].bus == HDD_BUS_XTIDE)
		{
			hdc[c].xtide_channel = !!config_get_int(cat, temps, c & 1);
		}
		else
		{
			config_delete_var(cat, temps);
		}

		/* RLL (ESDI) */
		sprintf(temps, "hdd_%02i_rll_channel", c + 1);
		if (hdc[c].bus == HDD_BUS_RLL)
		{
			hdc[c].rll_channel = !!config_get_int(cat, temps, c & 1);
		}
		else
		{
			config_delete_var(cat, temps);
		}

		/* IDE */
		sprintf(temps, "hdd_%02i_ide_channel", c + 1);
		if ((hdc[c].bus == HDD_BUS_IDE_PIO_ONLY) || (hdc[c].bus == HDD_BUS_IDE_PIO_AND_DMA))
		{
			sprintf(temps2, "%01u:%01u", c >> 1, c & 1);
			p = config_get_string(cat, temps, temps2);

			if (strstr(p, ":") == NULL)
			{
				sscanf(p, "%i", &hdc[c].ide_channel);
				hdc[c].ide_channel &= 7;
			}
			else
			{
				sscanf(p, "%01u:%01u", &board, &dev);

				board &= 3;
				dev &= 1;
				hdc[c].ide_channel = (board << 1) + dev;
			}

			if (hdc[c].ide_channel > 7)
			{
				hdc[c].ide_channel = 7;
			}
		}
		else
		{
			config_delete_var(cat, temps);
		}

		/* SCSI */
		sprintf(temps, "hdd_%02i_scsi_location", c + 1);
		if ((hdc[c].bus == HDD_BUS_SCSI) || (hdc[c].bus == HDD_BUS_SCSI_REMOVABLE))
		{
			sprintf(temps2, "%02u:%02u", c, 0);
			p = config_get_string(cat, temps, temps2);

			sscanf(p, "%02u:%02u", &hdc[c].scsi_id, &hdc[c].scsi_lun);

			if (hdc[c].scsi_id > 15)
			{
				hdc[c].scsi_id = 15;
			}
			if (hdc[c].scsi_lun > 7)
			{
				hdc[c].scsi_lun = 7;
			}
		}
		else
		{
			config_delete_var(cat, temps);
		}

		memset(hdc[c].fn, 0, sizeof(hdc[c].fn));
		memset(hdc[c].prev_fn, 0, sizeof(hdc[c].prev_fn));
		sprintf(temps, "hdd_%02i_fn", c + 1);
	        wp = config_get_wstring(cat, temps, L"");
        	memcpy(hdc[c].fn, wp, (wcslen(wp) << 1) + 2);

		/* If the hard disk is in any way empty or invalid, mark the relevant variables for deletion. */
		if (!hard_disk_is_valid(c))
		{
			sprintf(temps, "hdd_%02i_parameters", c + 1);
			config_delete_var(cat, temps);

			sprintf(temps, "hdd_%02i_preide_channels", c + 1);
			config_delete_var(cat, temps);

			sprintf(temps, "hdd_%02i_ide_channels", c + 1);
			config_delete_var(cat, temps);

			sprintf(temps, "hdd_%02i_scsi_location", c + 1);
			config_delete_var(cat, temps);

			sprintf(temps, "hdd_%02i_fn", c + 1);
			config_delete_var(cat, temps);
		}

		sprintf(temps, "hdd_%02i_mfm_channel", c + 1);
		config_delete_var(cat, temps);

		sprintf(temps, "hdd_%02i_ide_channel", c + 1);
		config_delete_var(cat, temps);
	}
}


/* Removable devices */
static void loadconfig_removable_devices(void)
{
	char *cat = "Removable devices";
	char temps[512];
	char temps2[512];
	char s[512];
	int c;
        char *p;
        wchar_t *wp;
	unsigned int board = 0, dev = 0;

	for (c = 0; c < FDD_NUM; c++)
	{
		sprintf(temps, "fdd_%02i_type", c + 1);
	        p = config_get_string(cat, temps, (c < 2) ? "525_2dd" : "none");
               	fdd_set_type(c, fdd_get_from_internal_name(p));
		if (fdd_get_type(c) > 13)
		{
			fdd_set_type(c, 13);
		}

		sprintf(temps, "fdd_%02i_fn", c + 1);
	        wp = config_get_wstring(cat, temps, L"");
        	memcpy(discfns[c], wp, (wcslen(wp) << 1) + 2);
		printf("Floppy: %ws\n", discfns[c]);
		sprintf(temps, "fdd_%02i_writeprot", c + 1);
	        ui_writeprot[c] = !!config_get_int(cat, temps, 0);

		/* Check, whether each value is default, if yes, delete it so that only non-default values will later be saved. */
		if (fdd_get_type(c) == ((c < 2) ? 2 : 0))
		{
			sprintf(temps, "fdd_%02i_type", c + 1);
			config_delete_var(cat, temps);
		}

		if (wcslen(discfns[c]) == 0)
		{
			sprintf(temps, "fdd_%02i_fn", c + 1);
			config_delete_var(cat, temps);
		}

		if (ui_writeprot[c] == 0)
		{
			sprintf(temps, "fdd_%02i_writeprot", c + 1);
			config_delete_var(cat, temps);
		}
	}

	memset(temps, 0, 512);
	for (c = 0; c < CDROM_NUM; c++)
	{
		sprintf(temps, "cdrom_%02i_host_drive", c + 1);
		cdrom_drives[c].host_drive = config_get_int(cat, temps, 0);
		cdrom_drives[c].prev_host_drive = cdrom_drives[c].host_drive;

		sprintf(temps, "cdrom_%02i_parameters", c + 1);
		p = config_get_string(cat, temps, NULL);
		if (p != NULL)
		{
			sscanf(p, "%u, %s", &cdrom_drives[c].sound_on, s);
		}
		else
		{
			sscanf("0, none", "%u, %s", &cdrom_drives[c].sound_on, s);
		}

		cdrom_drives[c].bus_type = config_string_to_bus(s, 1);

		sprintf(temps, "cdrom_%02i_ide_channel", c + 1);
		if ((cdrom_drives[c].bus_type == CDROM_BUS_ATAPI_PIO_ONLY) || (cdrom_drives[c].bus_type == CDROM_BUS_ATAPI_PIO_AND_DMA))
		{
			sprintf(temps2, "%01u:%01u", (c + 2) >> 1, (c + 2) & 1);
			p = config_get_string(cat, temps, temps2);

			if (strstr(p, ":") == NULL)
			{
				sscanf(p, "%i", &hdc[c].ide_channel);
				cdrom_drives[c].ide_channel &= 7;
			}
			else
			{
				sscanf(p, "%02u:%02u", &board, &dev);

				board &= 3;
				dev &= 1;
				cdrom_drives[c].ide_channel = (board << 1) + dev;
			}

			if (cdrom_drives[c].ide_channel > 7)
			{
				cdrom_drives[c].ide_channel = 7;
			}
		}
		else
		{
			config_delete_var(cat, temps);
		}

		sprintf(temps, "cdrom_%02i_scsi_location", c + 1);
		if (cdrom_drives[c].bus_type == CDROM_BUS_SCSI)
		{
			sprintf(temps2, "%02u:%02u", c + 2, 0);
			p = config_get_string(cat, temps, temps2);
			sscanf(p, "%02u:%02u", &cdrom_drives[c].scsi_device_id, &cdrom_drives[c].scsi_device_lun);

			if (cdrom_drives[c].scsi_device_id > 15)
			{
				cdrom_drives[c].scsi_device_id = 15;
			}
			if (cdrom_drives[c].scsi_device_lun > 7)
			{
				cdrom_drives[c].scsi_device_lun = 7;
			}
		}
		else
		{
			config_delete_var(cat, temps);
		}

		sprintf(temps, "cdrom_%02i_image_path", c + 1);
	        wp = config_get_wstring(cat, temps, L"");
        	memcpy(cdrom_image[c].image_path, wp, (wcslen(wp) << 1) + 2);

		if ((cdrom_drives[c].host_drive < 0x41) || ((cdrom_drives[c].host_drive == 0x200) && (wcslen(cdrom_image[c].image_path) == 0)))
		{
			cdrom_drives[c].host_drive = 0;
		}

		/* If the CD-ROM is disabled, delete all its variables. */
		if (cdrom_drives[c].bus_type == CDROM_BUS_DISABLED)
		{
			sprintf(temps, "cdrom_%02i_host_drive", c + 1);
			config_delete_var(cat, temps);

			sprintf(temps, "cdrom_%02i_parameters", c + 1);
			config_delete_var(cat, temps);

			sprintf(temps, "cdrom_%02i_ide_channel", c + 1);
			config_delete_var(cat, temps);

			sprintf(temps, "cdrom_%02i_scsi_location", c + 1);
			config_delete_var(cat, temps);

			sprintf(temps, "cdrom_%02i_image_path", c + 1);
			config_delete_var(cat, temps);
		}

		sprintf(temps, "cdrom_%02i_iso_path", c + 1);
		config_delete_var(cat, temps);
	}
}


void loadconfig(wchar_t *fn)
{
	if (fn == NULL)
		fn = config_file_default;
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


wchar_t *nvr_concat(wchar_t *to_concat)
{
	static wchar_t temp_nvr_path[1024];
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


static void saveconfig_general(void)
{
	char *cat = "General";
	char temps[512];

        config_set_int(cat, "vid_resize", vid_resize);
	if (vid_resize == 0)
	{
		config_delete_var(cat, "vid_resize");
	}

	if (vid_api == 1)
	{
		config_delete_var(cat, "vid_renderer");
	}
	else
	{
		switch(vid_api)
		{
			case 0:
			        config_set_string(cat, "vid_renderer", "ddraw");
				break;
			case 1:
			default:
			        config_set_string(cat, "vid_renderer", "d3d9");
				break;
		}
	}

	if (video_fullscreen_scale == 0)
	{
		config_delete_var(cat, "video_fullscreen_scale");
	}
	else
	{
	        config_set_int(cat, "video_fullscreen_scale", video_fullscreen_scale);
	}

	if (video_fullscreen_first == 1)
	{
		config_delete_var(cat, "video_fullscreen_first");
	}
	else
	{
	        config_set_int(cat, "video_fullscreen_first", video_fullscreen_first);
	}

	if (force_43 == 0)
	{
		config_delete_var(cat, "force_43");
	}
	else
	{
	        config_set_int(cat, "force_43", force_43);
	}

	if (scale == 1)
	{
		config_delete_var(cat, "scale");
	}
	else
	{
	        config_set_int(cat, "scale", scale);
	}

	if (enable_overscan == 0)
	{
		config_delete_var(cat, "enable_overscan");
	}
	else
	{
	        config_set_int(cat, "enable_overscan", enable_overscan);
	}

        config_set_int(cat, "window_remember", window_remember);
	if (window_remember)
	{
		sprintf(temps, "%i, %i, %i, %i", window_w, window_h, window_x, window_y);
		config_set_string(cat, "window_coordinates", temps);
	}
	else
	{
	        config_delete_var(cat, "window_remember");
	        config_delete_var(cat, "window_coordinates");
	}

	if (read_nvr_path == NULL)
	{
		config_delete_var(cat, "nvr_path");
	}
	else
	{
	        config_set_wstring(cat, "nvr_path", nvr_path);
	}

#ifndef __unix
	if (dwLanguage == 0x0409)
	{
		config_delete_var(cat, "language");
	}
	else
	{
	        config_set_hex16(cat, "language", dwLanguage);
	}
#endif

	config_delete_section_if_empty(cat);
}


/* Machine */
static void saveconfig_machine(void)
{
	char *cat = "Machine";

        config_set_string(cat, "model", model_get_internal_name());

	if (cpu_manufacturer == 0)
	{
		config_delete_var(cat, "cpu_manufacturer");
	}
	else
	{
	        config_set_int(cat, "cpu_manufacturer", cpu_manufacturer);
	}

	if (cpu == 0)
	{
		config_delete_var(cat, "cpu");
	}
	else
	{
	        config_set_int(cat, "cpu", cpu);
	}

	if (cpu_waitstates == 0)
	{
		config_delete_var(cat, "cpu_waitstates");
	}
	else
	{
		config_set_int(cat, "cpu_waitstates", cpu_waitstates);
	}

	if (mem_size == 4096)
	{
		config_delete_var(cat, "mem_size");
	}
	else
	{
	        config_set_int(cat, "mem_size", mem_size);
	}

        config_set_int(cat, "cpu_use_dynarec", cpu_use_dynarec);

	if (enable_external_fpu == 0)
	{
		config_delete_var(cat, "cpu_enable_fpu");
	}
	else
	{
	        config_set_int(cat, "cpu_enable_fpu", enable_external_fpu);
	}

	if (enable_sync == 1)
	{
		config_delete_var(cat, "enable_sync");
	}
	else
	{
	        config_set_int(cat, "enable_sync", enable_sync);
	}

	config_delete_section_if_empty(cat);
}


/* Video */
static void saveconfig_video(void)
{
	char *cat = "Video";

        config_set_string(cat, "gfxcard", video_get_internal_name(video_old_to_new(gfxcard)));

	if (video_speed == 3)
	{
		config_delete_var(cat, "video_speed");
	}
	else
	{
	        config_set_int(cat, "video_speed", video_speed);
	}

	if (voodoo_enabled == 0)
	{
		config_delete_var(cat, "voodoo");
	}
	else
	{
	        config_set_int(cat, "voodoo", voodoo_enabled);
	}

	config_delete_section_if_empty(cat);
}


/* Input devices */
static void saveconfig_input_devices(void)
{
	char *cat = "Input devices";
	char temps[512];
	char s[512];
        int c, d;

	config_set_string(cat, "mouse_type", mouse_get_internal_name(mouse_type));

	if ((joystick_type == 0) || (joystick_type == 7))
	{
		if (joystick_type == 7)
		{
			config_delete_var(cat, "joystick_type");
		}
		else
		{
		        config_set_int(cat, "joystick_type", joystick_type);
		}

	        for (c = 0; c < 16; c++)
        	{
	                sprintf(s, "joystick_%i_nr", c);
        	        config_delete_var(cat, s);

                        for (d = 0; d < 16; d++)
       	                {                        
               	                sprintf(s, "joystick_%i_axis_%i", c, d);
                       	        config_delete_var(cat, s);
                        }
       	                for (d = 0; d < 16; d++)
               	        {                        
                       	        sprintf(s, "joystick_%i_button_%i", c, d);
                               	config_delete_var(cat, s);
                        }
       	                for (d = 0; d < 16; d++)
               	        {                        
                       	        sprintf(s, "joystick_%i_pov_%i", c, d);
       	                        config_delete_var(cat, s);
               	        }
        	}
	}
	else
	{
	        config_set_int(cat, "joystick_type", joystick_type);

	        for (c = 0; c < joystick_get_max_joysticks(joystick_type); c++)
        	{
	                sprintf(s, "joystick_%i_nr", c);
        	        config_set_int(cat, s, joystick_state[c].plat_joystick_nr);

	                if (joystick_state[c].plat_joystick_nr)
        	        {
	                        for (d = 0; d < joystick_get_axis_count(joystick_type); d++)
        	                {                        
                	                sprintf(s, "joystick_%i_axis_%i", c, d);
                        	        config_set_int(cat, s, joystick_state[c].axis_mapping[d]);
	                        }
        	                for (d = 0; d < joystick_get_button_count(joystick_type); d++)
                	        {                        
                        	        sprintf(s, "joystick_%i_button_%i", c, d);
                                	config_set_int(cat, s, joystick_state[c].button_mapping[d]);
	                        }
        	                for (d = 0; d < joystick_get_pov_count(joystick_type); d++)
                	        {                        
                        	        sprintf(s, "joystick_%i_pov_%i", c, d);
					sprintf(temps, "%i, %i", joystick_state[c].pov_mapping[d][0], joystick_state[c].pov_mapping[d][1]);
        	                        config_set_string(cat, s, temps);
                	        }
	                }
        	}
	}

	config_delete_section_if_empty(cat);
}


/* Sound */
static void saveconfig_sound(void)
{
	char *cat = "Sound";

	if (sound_card_current == 0)
	{
		config_delete_var(cat, "sndcard");
	}
	else
	{
		config_set_string(cat, "sndcard", sound_card_get_internal_name(sound_card_current));
	}

	if (midi_id == 0)
	{
		config_delete_var(cat, "midi_host_device");
	}
	else
	{
	        config_set_int(cat, "midi_host_device", midi_id);
	}

	if (SSI2001 == 0)
	{
		config_delete_var(cat, "ssi2001");
	}
	else
	{
	        config_set_int(cat, "ssi2001", SSI2001);
	}

	if (GAMEBLASTER == 0)
	{
		config_delete_var(cat, "gameblaster");
	}
	else
	{
	        config_set_int(cat, "gameblaster", GAMEBLASTER);
	}

	if (GUS == 0)
	{
		config_delete_var(cat, "gus");
	}
	else
	{
		config_set_int(cat, "gus", GUS);
	}

	if (opl3_type == 0)
	{
		config_delete_var(cat, "opl3_type");
	}
	else
	{
		config_set_string(cat, "opl3_type", (opl3_type == 1) ? "nukedopl" : "dbopl");
	}

	config_delete_section_if_empty(cat);
}


/* Network */
static void saveconfig_network(void)
{
	char *cat = "Network";

	if (network_type == NET_TYPE_NONE)
	{
		config_delete_var(cat, "net_type");
	}
	else
	{
		config_set_string(cat, "net_type", (network_type == NET_TYPE_SLIRP) ? "slirp" : "pcap");
	}

	if (network_pcap[0] != '\0')
	{
		if (!strcmp(network_pcap, "none"))
		{
			config_delete_var(cat, "net_pcap_device");
		}
		else
		{
			config_set_string(cat, "net_pcap_device", network_pcap);
		}
	}
	else
	{
		/* config_set_string(cat, "net_pcap_device", "none"); */
		config_delete_var(cat, "net_pcap_device");
	}

	if (network_card == 0)
	{
		config_delete_var(cat, "net_card");
	}
	else
	{
		config_set_string(cat, "net_card", network_card_get_internal_name(network_card));
	}

	config_delete_section_if_empty(cat);
}


/* Other peripherals */
static void saveconfig_other_peripherals(void)
{
	char *cat = "Other peripherals";
	char temps[512];
	char temps2[512];
        int c;

	if (scsi_card_current == 0)
	{
		config_delete_var(cat, "scsicard");
	}
	else
	{
		config_set_string(cat, "scsicard", scsi_card_get_internal_name(scsi_card_current));
	}

	if (!strcmp(hdd_controller_name, "none"))
	{
		config_delete_var(cat, "hdd_controller");
	}
	else
	{
	        config_set_string(cat, "hdd_controller", hdd_controller_name);
	}

	memset(temps, '\0', sizeof(temps));
	for (c = 2; c < 4; c++)
	{
		sprintf(temps, "ide_%02i", c + 1);
		sprintf(temps2, "%i, %02i", !!ide_enable[c], ide_irq[c]);

		if (ide_enable[c] == 0)
		{
			config_delete_var(cat, temps);
		}
		else
		{
		        config_set_string(cat, temps, temps2);
		}
	}

	if (serial_enabled[0])
	{
		config_delete_var(cat, "serial1_enabled");
	}
	else
	{
	        config_set_int(cat, "serial1_enabled", serial_enabled[0]);
	}

	if (serial_enabled[1])
	{
		config_delete_var(cat, "serial2_enabled");
	}
	else
	{
	        config_set_int(cat, "serial2_enabled", serial_enabled[1]);
	}

	if (lpt_enabled)
	{
		config_delete_var(cat, "lpt_enabled");
	}
	else
	{
	        config_set_int(cat, "lpt_enabled", lpt_enabled);
	}

	if (bugger_enabled == 0)
	{
		config_delete_var(cat, "bugger_enabled");
	}
	else
	{
	        config_set_int(cat, "bugger_enabled", bugger_enabled);
	}

	config_delete_section_if_empty(cat);
}


static char *config_bus_to_string(int bus, int cdrom)
{
	switch (bus)
	{
		case HDD_BUS_DISABLED:
		default:
			return "none";
			break;
		case HDD_BUS_MFM:
			return "mfm";
			break;
		case HDD_BUS_XTIDE:
			return "xtide";
			break;
		case HDD_BUS_RLL:
			return "rll";
			break;
		case HDD_BUS_IDE_PIO_ONLY:
			return cdrom ? "atapi_pio_only" : "ide_pio_only";
			break;
		case HDD_BUS_IDE_PIO_AND_DMA:
			return cdrom ? "atapi_pio_and_dma" : "ide_pio_and_dma";
			break;
		case HDD_BUS_SCSI:
			return "scsi";
			break;
		case HDD_BUS_SCSI_REMOVABLE:
			return "scsi_removable";
			break;
	}
}


/* Hard disks */
static void saveconfig_hard_disks(void)
{
	char *cat = "Hard disks";
	char temps[24];
	char temps2[64];
	char s[512];
        int c;
	char *p;

	memset(temps, 0, sizeof(temps));
	for (c = 0; c < HDC_NUM; c++)
	{
		sprintf(temps, "hdd_%02i_parameters", c + 1);
		memset(s, 0, sizeof(s));
		if (!hard_disk_is_valid(c))
		{
			config_delete_var(cat, temps);
		}
		else
		{
			p = config_bus_to_string(hdc[c].bus, 0);
			sprintf(temps2, "%" PRIu64 ", %" PRIu64", %" PRIu64 ", %i, %s", hdc[c].spt, hdc[c].hpc, hdc[c].tracks, hdc[c].wp, p);
			config_set_string(cat, temps, temps2);
		}

		sprintf(temps, "hdd_%02i_mfm_channel", c + 1);
		if (!hard_disk_is_valid(c) || (hdc[c].bus != HDD_BUS_MFM))
		{
			config_delete_var(cat, temps);
		}
		else
		{
			config_set_int(cat, temps, hdc[c].mfm_channel);
		}

		sprintf(temps, "hdd_%02i_xtide_channel", c + 1);
		if (!hard_disk_is_valid(c) || (hdc[c].bus != HDD_BUS_XTIDE))
		{
			config_delete_var(cat, temps);
		}
		else
		{
			config_set_int(cat, temps, hdc[c].xtide_channel);
		}

		sprintf(temps, "hdd_%02i_rll_channel", c + 1);
		if (!hard_disk_is_valid(c) || (hdc[c].bus != HDD_BUS_RLL))
		{
			config_delete_var(cat, temps);
		}
		else
		{
			config_set_int(cat, temps, hdc[c].rll_channel);
		}

		sprintf(temps, "hdd_%02i_ide_channel", c + 1);
		if (!hard_disk_is_valid(c) || ((hdc[c].bus != HDD_BUS_IDE_PIO_ONLY) && (hdc[c].bus != HDD_BUS_IDE_PIO_AND_DMA)))
		{
			config_delete_var(cat, temps);
		}
		else
		{
			sprintf(temps2, "%01u:%01u", hdc[c].ide_channel >> 1, hdc[c].ide_channel & 1);
			config_set_string(cat, temps, temps2);
		}

		sprintf(temps, "hdd_%02i_scsi_location", c + 1);
		if (!hard_disk_is_valid(c) || ((hdc[c].bus != HDD_BUS_SCSI) && (hdc[c].bus != HDD_BUS_SCSI_REMOVABLE)))
		{
			config_delete_var(cat, temps);
		}
		else
		{
			sprintf(temps2, "%02u:%02u", hdc[c].scsi_id, hdc[c].scsi_lun);
			config_set_string(cat, temps, temps2);
		}

		sprintf(temps, "hdd_%02i_fn", c + 1);
		if (!hard_disk_is_valid(c) || (wcslen(hdc[c].fn) == 0))
		{
			config_delete_var(cat, temps);
		}
		else
		{
		        config_set_wstring(cat, temps, hdc[c].fn);
		}
	}

	config_delete_section_if_empty(cat);
}


/* Removable devices */
static void saveconfig_removable_devices(void)
{
	char *cat = "Removable devices";
	char temps[512];
	char temps2[512];
        int c;

	memset(temps, '\0', sizeof(temps));
	for (c = 0; c < FDD_NUM; c++)
	{
		sprintf(temps, "fdd_%02i_type", c + 1);
		if (fdd_get_type(c) == ((c < 2) ? 2 : 0))
		{
			config_delete_var(cat, temps);
		}
		else
		{
		        config_set_string(cat, temps, fdd_get_internal_name(fdd_get_type(c)));
		}

		sprintf(temps, "fdd_%02i_fn", c + 1);
		if (wcslen(discfns[c]) == 0)
		{
			config_delete_var(cat, temps);

			ui_writeprot[c] = 0;

			sprintf(temps, "fdd_%02i_writeprot", c + 1);
			config_delete_var(cat, temps);
		}
		else
		{
		        config_set_wstring(cat, temps, discfns[c]);
		}

		sprintf(temps, "fdd_%02i_writeprot", c + 1);
		if (ui_writeprot[c] == 0)
		{
			config_delete_var(cat, temps);
		}
		else
		{
		        config_set_int(cat, temps, ui_writeprot[c]);
		}
	}

	memset(temps, '\0', sizeof(temps));
	for (c = 0; c < CDROM_NUM; c++)
	{
		sprintf(temps, "cdrom_%02i_host_drive", c + 1);
		if ((cdrom_drives[c].bus_type == 0) || (cdrom_drives[c].host_drive < 'A') || ((cdrom_drives[c].host_drive > 'Z') && (cdrom_drives[c].host_drive != 200)))
		{
			config_delete_var(cat, temps);
		}
		else
		{
			config_set_int(cat, temps, cdrom_drives[c].host_drive);
		}

		sprintf(temps, "cdrom_%02i_parameters", c + 1);
		if (cdrom_drives[c].bus_type == 0)
		{
			config_delete_var(cat, temps);
		}
		else
		{
			sprintf(temps2, "%u, %s", cdrom_drives[c].sound_on, config_bus_to_string(cdrom_drives[c].bus_type, 1));
			config_set_string(cat, temps, temps2);
		}
		
		sprintf(temps, "cdrom_%02i_ide_channel", c + 1);
		if ((cdrom_drives[c].bus_type != CDROM_BUS_ATAPI_PIO_ONLY) && (cdrom_drives[c].bus_type != CDROM_BUS_ATAPI_PIO_AND_DMA))
		{
			config_delete_var(cat, temps);
		}
		else
		{
			sprintf(temps2, "%01u:%01u", cdrom_drives[c].ide_channel >> 1, cdrom_drives[c].ide_channel & 1);
			config_set_string(cat, temps, temps2);
		}

		sprintf(temps, "cdrom_%02i_scsi_location", c + 1);
		if (cdrom_drives[c].bus_type != CDROM_BUS_SCSI)
		{
			config_delete_var(cat, temps);
		}
		else
		{
			sprintf(temps2, "%02u:%02u", cdrom_drives[c].scsi_device_id, cdrom_drives[c].scsi_device_lun);
			config_set_string(cat, temps, temps2);
		}

		sprintf(temps, "cdrom_%02i_image_path", c + 1);
		if ((cdrom_drives[c].bus_type == 0) || (wcslen(cdrom_image[c].image_path) == 0))
		{
			config_delete_var(cat, temps);
		}
		else
		{
			config_set_wstring(cat, temps, cdrom_image[c].image_path);
		}
	}

	config_delete_section_if_empty(cat);
}


void saveconfig(void)
{
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
