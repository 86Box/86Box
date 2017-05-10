/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
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
#include "net_ne2000.h"
#include "nvr.h"
#include "plat-joystick.h"
#include "scsi.h"
#include "sound/snd_dbopl.h"
#include "sound/snd_opl.h"
#include "sound/sound.h"
#include "video/video.h"

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

void config_dump()
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

void config_free()
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

void config_load(wchar_t *fn)
{
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
                int c;
                wchar_t buffer[1024];
		int org_pos;

		memset(buffer, 0, 2048);
                fgetws(buffer, 255, f);
                if (feof(f)) break;
                
                c = 0;
                
                while (buffer[c] == L' ')
                      c++;

                if (buffer[c] == L'\0') continue;
                
                if (buffer[c] == L'#') /*Comment*/
                        continue;

                if (buffer[c] == L'[') /*Section*/
                {
                        section_t *new_section;
                        char name[256];
                        int d = 0;
                        
                        c++;
                        while (buffer[c] != L']' && buffer[c])
                                wctomb(&(name[d++]), buffer[c++]);

                        if (buffer[c] != L']')
                                continue;
                        name[d] = 0;
                        
                        new_section = malloc(sizeof(section_t));
                        memset(new_section, 0, sizeof(section_t));
                        strncpy(new_section->name, name, 256);
                        list_add(&new_section->list, &config_head);
                        
                        current_section = new_section;                        
                }
                else
                {
                        entry_t *new_entry;
                        char name[256];
                        int d = 0, data_pos;

                        while (buffer[c] != L'=' && buffer[c] != L' ' && buffer[c])
                                wctomb(&(name[d++]), buffer[c++]);
                
                        if (buffer[c] == L'\0') continue;
                        name[d] = 0;

                        while ((buffer[c] == L'=' || buffer[c] == L' ') && buffer[c])
                                c++;
                        
                        if (!buffer[c]) continue;
                        
                        data_pos = c;
                        while (buffer[c])
                        {
                                if (buffer[c] == L'\n')
                                        buffer[c] = L'\0';
                                c++;
                        }

                        new_entry = malloc(sizeof(entry_t));
                        memset(new_entry, 0, sizeof(entry_t));
                        strncpy(new_entry->name, name, 256);
			memcpy(new_entry->wdata, &buffer[data_pos], 512);
			new_entry->wdata[255] = L'\0';
			wcstombs(new_entry->data, new_entry->wdata, 512);
			new_entry->data[255] = '\0';
                        list_add(&new_entry->list, &current_section->entry_head);
                }
        }
        
        fclose(f);
        
        config_dump();
}



void config_new()
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
        
        current_section = (section_t *)config_head.next;
        
        while (current_section)
        {
                entry_t *current_entry;
                
                if (current_section->name[0])
		{
			mbstowcs(wname, current_section->name, strlen(current_section->name) + 1);
                        fwprintf(f, L"\n[%ws]\n", wname);
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

                        current_entry = (entry_t *)current_entry->list.next;
                }

                current_section = (section_t *)current_section->list.next;
        }
        
        fclose(f);
}
void loadconfig(wchar_t *fn)
{
	int c, d;
	char s[512];
        char *p;
        wchar_t *wp, *wq;
	char temps[512];
        
        if (!fn)
                config_load(config_file_default);
        else
                config_load(fn);
        
        GAMEBLASTER = !!config_get_int(NULL, "gameblaster", 0);
        GUS = !!config_get_int(NULL, "gus", 0);
        SSI2001 = !!config_get_int(NULL, "ssi2001", 0);
        voodoo_enabled = !!config_get_int(NULL, "voodoo", 0);

	/* SCSI */
        p = (char *)config_get_string(NULL, "scsicard", "");
        if (p)
                scsi_card_current = scsi_card_get_from_internal_name(p);
        else
                scsi_card_current = 0;

	/* network */
	ethif = config_get_int(NULL, "netinterface", 1);
        if (ethif >= inum)
            inum = ethif + 1;
        p = (char *)config_get_string(NULL, "netcard", "");
        if (p)
                network_card_current = network_card_get_from_internal_name(p);
        else
                network_card_current = 0;
	ne2000_generate_maclocal(config_get_int(NULL, "maclocal", -1));
	ne2000_generate_maclocal_pci(config_get_int(NULL, "maclocal_pci", -1));

        p = (char *)config_get_string(NULL, "model", "");
        if (p)
                model = model_get_model_from_internal_name(p);
        else
                model = 0;

        if (model >= model_count())
                model = model_count() - 1;

        romset = model_getromset();
        cpu_manufacturer = config_get_int(NULL, "cpu_manufacturer", 0);
        cpu = config_get_int(NULL, "cpu", 0);
        cpu_use_dynarec = !!config_get_int(NULL, "cpu_use_dynarec", 0);
        
	cpu_waitstates = config_get_int(NULL, "cpu_waitstates", 0);
                
        p = (char *)config_get_string(NULL, "gfxcard", "");
        if (p)
                gfxcard = video_get_video_from_internal_name(p);
        else
                gfxcard = 0;
        video_speed = config_get_int(NULL, "video_speed", 3);
        p = (char *)config_get_string(NULL, "sndcard", "");
        if (p)
                sound_card_current = sound_card_get_from_internal_name(p);
        else
                sound_card_current = 0;

        mem_size = config_get_int(NULL, "mem_size", 4096);
        if (mem_size < ((models[model].flags & MODEL_AT) ? models[model].min_ram*1024 : models[model].min_ram))
                mem_size = ((models[model].flags & MODEL_AT) ? models[model].min_ram*1024 : models[model].min_ram);
	if (mem_size > 1048576)
	{
		mem_size = 1048576;
	}
 
	for (c = 0; c < FDD_NUM; c++)
	{
		sprintf(temps, "fdd_%02i_type", c + 1);
	        p = (char *)config_get_string(NULL, temps, (c < 2) ? "525_2dd" : "none");
        	if (p)
                	fdd_set_type(c, fdd_get_from_internal_name(p));
	        else
        	        fdd_set_type(c, (c < 2) ? 2 : 0);
		if (fdd_get_type(c) > 13)
		{
			fdd_set_type(c, 13);
		}

		sprintf(temps, "fdd_%02i_fn", c + 1);
	        wp = (wchar_t *)config_get_wstring(NULL, temps, L"");
        	if (wp) memcpy(discfns[c], wp, 512);
	        else    {
			memcpy(discfns[c], L"", 2);
			discfns[c][0] = L'\0';
		}
		printf("Floppy: %ws\n", discfns[c]);
		sprintf(temps, "fdd_%02i_writeprot", c + 1);
	        ui_writeprot[c] = !!config_get_int(NULL, temps, 0);
	}

        p = (char *)config_get_string(NULL, "hdd_controller", "");
        if (p)
                strncpy(hdd_controller_name, p, sizeof(hdd_controller_name)-1);
        else
                strncpy(hdd_controller_name, "none", sizeof(hdd_controller_name)-1);        

	memset(temps, 0, 512);
	for (c = 2; c < 4; c++)
	{
		sprintf(temps, "ide_%02i_enable", c + 1);
		ide_enable[c] = config_get_int(NULL, temps, 0);
		sprintf(temps, "ide_%02i_irq", c + 1);
		ide_irq[c] = config_get_int(NULL, temps, 8 + c);
	}

	memset(temps, 0, 512);
	for (c = 0; c < HDC_NUM; c++)
	{
		sprintf(temps, "hdd_%02i_sectors", c + 1);
		hdc[c].spt = config_get_int(NULL, temps, 0);
		if (hdc[c].spt > 99)
		{
			hdc[c].spt = 99;
		}
		sprintf(temps, "hdd_%02i_heads", c + 1);
		hdc[c].hpc = config_get_int(NULL, temps, 0);
		if (hdc[c].hpc > 64)
		{
			hdc[c].hpc = 64;
		}
		sprintf(temps, "hdd_%02i_cylinders", c + 1);
		hdc[c].tracks = config_get_int(NULL, temps, 0);
		if (hdc[c].tracks > 266305)
		{
			hdc[c].tracks = 266305;
		}
		sprintf(temps, "hdd_%02i_bus_type", c + 1);
		hdc[c].bus = config_get_int(NULL, temps, 0);
		if (hdc[c].bus > 4)
		{
			hdc[c].bus = 4;
		}
		sprintf(temps, "hdd_%02i_mfm_channel", c + 1);
		hdc[c].mfm_channel = config_get_int(NULL, temps, 0);
		if (hdc[c].mfm_channel > 1)
		{
			hdc[c].mfm_channel = 1;
		}
		sprintf(temps, "hdd_%02i_ide_channel", c + 1);
		hdc[c].ide_channel = config_get_int(NULL, temps, 0);
		if (hdc[c].ide_channel > 7)
		{
			hdc[c].ide_channel = 7;
		}
		sprintf(temps, "hdd_%02i_scsi_device_id", c + 1);
		hdc[c].scsi_id = config_get_int(NULL, temps, (c < 7) ? c : ((c < 15) ? (c + 1) : 15));
		if (hdc[c].scsi_id > 15)
		{
			hdc[c].scsi_id = 15;
		}
		sprintf(temps, "hdd_%02i_scsi_device_lun", c + 1);
		hdc[c].scsi_lun = config_get_int(NULL, temps, 0);
		if (hdc[c].scsi_lun > 7)
		{
			hdc[c].scsi_lun = 7;
		}
		sprintf(temps, "hdd_%02i_fn", c + 1);
	        wp = (wchar_t *)config_get_wstring(NULL, temps, L"");
        	if (wp) memcpy(hdd_fn[c], wp, 512);
	        else    {
			memcpy(hdd_fn[c], L"", 2);
			hdd_fn[c][0] = L'\0';
		}
	}

	memset(temps, 0, 512);
	for (c = 0; c < CDROM_NUM; c++)
	{
		sprintf(temps, "cdrom_%02i_host_drive", c + 1);
		cdrom_drives[c].host_drive = config_get_int(NULL, temps, 0);
		cdrom_drives[c].prev_host_drive = cdrom_drives[c].host_drive;
		sprintf(temps, "cdrom_%02i_enabled", c + 1);
		cdrom_drives[c].enabled = !!config_get_int(NULL, temps, 0);
		sprintf(temps, "cdrom_%02i_sound_on", c + 1);
		cdrom_drives[c].sound_on = !!config_get_int(NULL, temps, 1);
		sprintf(temps, "cdrom_%02i_bus_type", c + 1);
		cdrom_drives[c].bus_type = config_get_int(NULL, temps, 0);
		if (cdrom_drives[c].bus_type > 1)
		{
			cdrom_drives[c].bus_type = 1;
		}
		sprintf(temps, "cdrom_%02i_atapi_dma", c + 1);
		cdrom_drives[c].atapi_dma = !!config_get_int(NULL, temps, 0);
		sprintf(temps, "cdrom_%02i_ide_channel", c + 1);
		cdrom_drives[c].ide_channel = config_get_int(NULL, temps, 2);
		if (cdrom_drives[c].ide_channel > 7)
		{
			cdrom_drives[c].ide_channel = 7;
		}
		sprintf(temps, "cdrom_%02i_scsi_device_id", c + 1);
		cdrom_drives[c].scsi_device_id = config_get_int(NULL, temps, c + 2);
		if (cdrom_drives[c].scsi_device_id > 15)
		{
			cdrom_drives[c].scsi_device_id = 15;
		}
		sprintf(temps, "cdrom_%02i_scsi_device_lun", c + 1);
		cdrom_drives[c].scsi_device_lun = config_get_int(NULL, temps, 0);
		if (cdrom_drives[c].scsi_device_lun > 7)
		{
			cdrom_drives[c].scsi_device_lun = 7;
		}

		sprintf(temps, "cdrom_%02i_image_path", c + 1);
	        wp = (wchar_t *)config_get_wstring(NULL, temps, L"");
        	if (wp) memcpy(cdrom_image[c].image_path, wp, 512);
	        else    {
			memcpy(cdrom_image[c].image_path, L"", 2);
			cdrom_image[c].image_path[0] = L'\0';
		}
	}

        vid_resize = !!config_get_int(NULL, "vid_resize", 0);
        vid_api = config_get_int(NULL, "vid_api", 0);
        video_fullscreen_scale = config_get_int(NULL, "video_fullscreen_scale", 0);
        video_fullscreen_first = config_get_int(NULL, "video_fullscreen_first", 1);

	force_43 = !!config_get_int(NULL, "force_43", 0);
	scale = !!config_get_int(NULL, "scale", 1);
	enable_overscan = !!config_get_int(NULL, "enable_overscan", 0);

        enable_sync = !!config_get_int(NULL, "enable_sync", 1);
        opl3_type = !!config_get_int(NULL, "opl3_type", 1);

        window_w = config_get_int(NULL, "window_w", 0);
        window_h = config_get_int(NULL, "window_h", 0);
        window_x = config_get_int(NULL, "window_x", 0);
        window_y = config_get_int(NULL, "window_y", 0);
        window_remember = config_get_int(NULL, "window_remember", 0);

        joystick_type = config_get_int(NULL, "joystick_type", 0);
        p = (char *)config_get_string(NULL, "mouse_type", "");
        if (p)
                mouse_type = mouse_get_from_internal_name(p);
        else
                mouse_type = 0;

	enable_external_fpu = !!config_get_int(NULL, "enable_external_fpu", 0);

        for (c = 0; c < joystick_get_max_joysticks(joystick_type); c++)
        {
                sprintf(s, "joystick_%i_nr", c);
                joystick_state[c].plat_joystick_nr = config_get_int("Joysticks", s, 0);

                if (joystick_state[c].plat_joystick_nr)
                {
                        for (d = 0; d < joystick_get_axis_count(joystick_type); d++)
                        {                        
                                sprintf(s, "joystick_%i_axis_%i", c, d);
                                joystick_state[c].axis_mapping[d] = config_get_int("Joysticks", s, d);
                        }
                        for (d = 0; d < joystick_get_button_count(joystick_type); d++)
                        {                        
                                sprintf(s, "joystick_%i_button_%i", c, d);
                                joystick_state[c].button_mapping[d] = config_get_int("Joysticks", s, d);
                        }
                        for (d = 0; d < joystick_get_pov_count(joystick_type); d++)
                        {                        
                                sprintf(s, "joystick_%i_pov_%i_x", c, d);
                                joystick_state[c].pov_mapping[d][0] = config_get_int("Joysticks", s, d);
                                sprintf(s, "joystick_%i_pov_%i_y", c, d);
                                joystick_state[c].pov_mapping[d][1] = config_get_int("Joysticks", s, d);
                        }
                }
        }

	memset(nvr_path, 0, 2048);
        wp = (wchar_t *)config_get_wstring(NULL, "nvr_path", L"");
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

        serial_enabled[0] = !!config_get_int(NULL, "serial1_enabled", 1);
        serial_enabled[1] = !!config_get_int(NULL, "serial2_enabled", 1);
        lpt_enabled = !!config_get_int(NULL, "lpt_enabled", 1);
        bugger_enabled = !!config_get_int(NULL, "bugger_enabled", 0);
}

wchar_t temp_nvr_path[1024];

wchar_t *nvr_concat(wchar_t *to_concat)
{
	char *p;

	memset(temp_nvr_path, 0, 2048);
	wcscpy(temp_nvr_path, nvr_path);

	p = (char *) temp_nvr_path;
	p += (path_len * 2);
	wchar_t *wp = (wchar_t *) p;

	wcscpy(wp, to_concat);
	return temp_nvr_path;
}

void saveconfig(void)
{
        int c, d;

	char temps[512];

        config_set_int(NULL, "gameblaster", GAMEBLASTER);
        config_set_int(NULL, "gus", GUS);
        config_set_int(NULL, "ssi2001", SSI2001);
        config_set_int(NULL, "voodoo", voodoo_enabled);

	config_set_string(NULL, "scsicard", scsi_card_get_internal_name(scsi_card_current));

	config_set_int(NULL, "netinterface", ethif);
	config_set_string(NULL, "netcard", network_card_get_internal_name(network_card_current));
	config_set_int(NULL, "maclocal", ne2000_get_maclocal());
	config_set_int(NULL, "maclocal_pci", ne2000_get_maclocal_pci());

        config_set_string(NULL, "model", model_get_internal_name());
        config_set_int(NULL, "cpu_manufacturer", cpu_manufacturer);
        config_set_int(NULL, "cpu", cpu);
        config_set_int(NULL, "cpu_use_dynarec", cpu_use_dynarec);
	config_set_int(NULL, "cpu_waitstates", cpu_waitstates);
        
        config_set_string(NULL, "gfxcard", video_get_internal_name(video_old_to_new(gfxcard)));
        config_set_int(NULL, "video_speed", video_speed);
	config_set_string(NULL, "sndcard", sound_card_get_internal_name(sound_card_current));
        config_set_int(NULL, "cpu_speed", cpuspeed);
        config_set_int(NULL, "has_fpu", hasfpu);

        config_set_int(NULL, "mem_size", mem_size);

	memset(temps, 0, 512);
	for (c = 0; c < FDD_NUM; c++)
	{
		sprintf(temps, "fdd_%02i_type", c + 1);
	        config_set_string(NULL, temps, fdd_get_internal_name(fdd_get_type(c)));
		sprintf(temps, "fdd_%02i_fn", c + 1);
	        config_set_wstring(NULL, temps, discfns[c]);
		sprintf(temps, "fdd_%02i_writeprot", c + 1);
	        config_set_int(NULL, temps, ui_writeprot[c]);
	}

        config_set_string(NULL, "hdd_controller", hdd_controller_name);

	memset(temps, 0, 512);
	for (c = 2; c < 4; c++)
	{
		sprintf(temps, "ide_%02i_enable", c + 1);
	        config_set_int(NULL, temps, ide_enable[c]);
		sprintf(temps, "ide_%02i_irq", c + 1);
	        config_set_int(NULL, temps, ide_irq[c]);
	}

	memset(temps, 0, 512);
	for (c = 0; c < HDC_NUM; c++)
	{
		sprintf(temps, "hdd_%02i_sectors", c + 1);
		config_set_int(NULL, temps, hdc[c].spt);
		sprintf(temps, "hdd_%02i_heads", c + 1);
		config_set_int(NULL, temps, hdc[c].hpc);
		sprintf(temps, "hdd_%02i_cylinders", c + 1);
		config_set_int(NULL, temps, hdc[c].tracks);
		sprintf(temps, "hdd_%02i_bus_type", c + 1);
		config_set_int(NULL, temps, hdc[c].bus);
		sprintf(temps, "hdd_%02i_mfm_channel", c + 1);
		config_set_int(NULL, temps, hdc[c].mfm_channel);
		sprintf(temps, "hdd_%02i_ide_channel", c + 1);
		config_set_int(NULL, temps, hdc[c].ide_channel);
		sprintf(temps, "hdd_%02i_scsi_device_id", c + 1);
		config_set_int(NULL, temps, hdc[c].scsi_id);
		sprintf(temps, "hdd_%02i_scsi_device_lun", c + 1);
		config_set_int(NULL, temps, hdc[c].scsi_lun);
		sprintf(temps, "hdd_%02i_fn", c + 1);
	        config_set_wstring(NULL, temps, hdd_fn[c]);
	}

	memset(temps, 0, 512);
	for (c = 0; c < CDROM_NUM; c++)
	{
		sprintf(temps, "cdrom_%02i_host_drive", c + 1);
		config_set_int(NULL, temps, cdrom_drives[c].host_drive);
		sprintf(temps, "cdrom_%02i_enabled", c + 1);
		config_set_int(NULL, temps, cdrom_drives[c].enabled);
		sprintf(temps, "cdrom_%02i_sound_on", c + 1);
		config_set_int(NULL, temps, cdrom_drives[c].sound_on);
		sprintf(temps, "cdrom_%02i_bus_type", c + 1);
		config_set_int(NULL, temps, cdrom_drives[c].bus_type);
		sprintf(temps, "cdrom_%02i_atapi_dma", c + 1);
		config_set_int(NULL, temps, cdrom_drives[c].atapi_dma);
		sprintf(temps, "cdrom_%02i_ide_channel", c + 1);
		config_set_int(NULL, temps, cdrom_drives[c].ide_channel);
		sprintf(temps, "cdrom_%02i_scsi_device_id", c + 1);
		config_set_int(NULL, temps, cdrom_drives[c].scsi_device_id);
		sprintf(temps, "cdrom_%02i_scsi_device_lun", c + 1);
		config_set_int(NULL, temps, cdrom_drives[c].scsi_device_lun);

		sprintf(temps, "cdrom_%02i_image_path", c + 1);
		config_set_wstring(NULL, temps, cdrom_image[c].image_path);
	}

        config_set_int(NULL, "vid_resize", vid_resize);
        config_set_int(NULL, "vid_api", vid_api);
        config_set_int(NULL, "video_fullscreen_scale", video_fullscreen_scale);
        config_set_int(NULL, "video_fullscreen_first", video_fullscreen_first);

        config_set_int(NULL, "force_43", force_43);
        config_set_int(NULL, "scale", scale);
        config_set_int(NULL, "enable_overscan", enable_overscan);

        config_set_int(NULL, "enable_sync", enable_sync);
        config_set_int(NULL, "opl3_type", opl3_type);

        config_set_int(NULL, "joystick_type", joystick_type);
	config_set_string(NULL, "mouse_type", mouse_get_internal_name(mouse_type));

        config_set_int(NULL, "enable_external_fpu", enable_external_fpu);

        for (c = 0; c < joystick_get_max_joysticks(joystick_type); c++)
        {
                char s[80];

                sprintf(s, "joystick_%i_nr", c);
                config_set_int("Joysticks", s, joystick_state[c].plat_joystick_nr);

                if (joystick_state[c].plat_joystick_nr)
                {
                        for (d = 0; d < joystick_get_axis_count(joystick_type); d++)
                        {                        
                                sprintf(s, "joystick_%i_axis_%i", c, d);
                                config_set_int("Joysticks", s, joystick_state[c].axis_mapping[d]);
                        }
                        for (d = 0; d < joystick_get_button_count(joystick_type); d++)
                        {                        
                                sprintf(s, "joystick_%i_button_%i", c, d);
                                config_set_int("Joysticks", s, joystick_state[c].button_mapping[d]);
                        }
                        for (d = 0; d < joystick_get_pov_count(joystick_type); d++)
                        {                        
                                sprintf(s, "joystick_%i_pov_%i_x", c, d);
                                config_set_int("Joysticks", s, joystick_state[c].pov_mapping[d][0]);
                                sprintf(s, "joystick_%i_pov_%i_y", c, d);
                                config_set_int("Joysticks", s, joystick_state[c].pov_mapping[d][1]);
                        }
                }
        }

        config_set_int(NULL, "window_w", window_w);
        config_set_int(NULL, "window_h", window_h);
        config_set_int(NULL, "window_x", window_x);
        config_set_int(NULL, "window_y", window_y);
        config_set_int(NULL, "window_remember", window_remember);

        config_set_int(NULL, "serial1_enabled", serial_enabled[0]);
        config_set_int(NULL, "serial2_enabled", serial_enabled[1]);
        config_set_int(NULL, "lpt_enabled", lpt_enabled);
        config_set_int(NULL, "bugger_enabled", bugger_enabled);
        
        config_save(config_file_default);
}
