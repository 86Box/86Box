/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Configuration file handler.
 *
 * Version:	@(#)config.c	1.0.48	2018/05/25
 *
 * Authors:	Sarah Walker,
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Overdoze,
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 *
 * NOTE:	Forcing config files to be in Unicode encoding breaks
 *		it on Windows XP, and possibly also Vista. Use the
 *		-DANSI_CFG for use on these systems.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "86box.h"
#include "cpu/cpu.h"
#include "device.h"
#include "nvr.h"
#include "config.h"
#include "lpt.h"
#include "disk/hdd.h"
#include "disk/hdc.h"
#include "disk/hdc_ide.h"
#include "floppy/fdd.h"
#include "floppy/fdc.h"
#include "game/gameport.h"
#include "machine/machine.h"
#include "mouse.h"
#include "network/network.h"
#include "scsi/scsi.h"
#include "cdrom/cdrom.h"
#include "disk/zip.h"
#include "sound/sound.h"
#include "sound/midi.h"
#include "sound/snd_dbopl.h"
#include "sound/snd_mpu401.h"
#include "sound/snd_opl.h"
#include "sound/sound.h"
#include "video/video.h"
#include "plat.h"
#include "plat_midi.h"
#include "ui.h"


typedef struct _list_ {
    struct _list_ *next;
} list_t;

typedef struct {
    list_t	list;

    char	name[128];

    list_t	entry_head;
} section_t;

typedef struct {
    list_t	list;

    char	name[128];
    char	data[256];
    wchar_t	wdata[512];
} entry_t;

#define list_add(new, head) {		\
    list_t *next = head;		\
					\
    while (next->next != NULL)		\
	next = next->next;		\
					\
    (next)->next = new;			\
    (new)->next = NULL;			\
}

#define list_delete(old, head) {	\
    list_t *next = head;		\
					\
    while ((next)->next != old) {	\
	next = (next)->next;		\
    }					\
					\
    (next)->next = (old)->next;		\
}


static list_t	config_head;


#ifdef ENABLE_CONFIG_LOG
int config_do_log = ENABLE_CONFIG_LOG;
#endif


static void
config_log(const char *format, ...)
{
#ifdef ENABLE_CONFIG_LOG
    va_list ap;

    if (config_do_log) {
	va_start(ap, format);
	pclog_ex(format, ap);
	va_end(ap);
    }
#endif
}


static section_t *
find_section(char *name)
{
    section_t *sec;
    char blank[] = "";

    sec = (section_t *)config_head.next;
    if (name == NULL)
	name = blank;

    while (sec != NULL) {
	if (! strncmp(sec->name, name, sizeof(sec->name)))
				return(sec);

	sec = (section_t *)sec->list.next;
    }

    return(NULL);
}


static entry_t *
find_entry(section_t *section, char *name)
{
    entry_t *ent;

    ent = (entry_t *)section->entry_head.next;

    while (ent != NULL) {
	if (! strncmp(ent->name, name, sizeof(ent->name)))
				return(ent);

	ent = (entry_t *)ent->list.next;
    }

    return(NULL);
}


static int
entries_num(section_t *section)
{
    entry_t *ent;
    int i = 0;

    ent = (entry_t *)section->entry_head.next;

    while (ent != NULL) {
	if (strlen(ent->name) > 0) i++;

	ent = (entry_t *)ent->list.next;
    }

    return(i);
}


static void
delete_section_if_empty(char *head)
{
    section_t *section;

    section = find_section(head);
    if (section == NULL) return;

    if (entries_num(section) == 0) {
	list_delete(&section->list, &config_head);
	free(section);
    }
}


static section_t *
create_section(char *name)
{
    section_t *ns = malloc(sizeof(section_t));

    memset(ns, 0x00, sizeof(section_t));
    strncpy(ns->name, name, sizeof(ns->name));
    list_add(&ns->list, &config_head);

    return(ns);
}


static entry_t *
create_entry(section_t *section, char *name)
{
    entry_t *ne = malloc(sizeof(entry_t));

    memset(ne, 0x00, sizeof(entry_t));
    strncpy(ne->name, name, sizeof(ne->name));
    list_add(&ne->list, &section->entry_head);

    return(ne);
}


#if 0
static void
config_free(void)
{
    section_t *sec, *ns;
    entry_t *ent;

    sec = (section_t *)config_head.next;
    while (sec != NULL) {
	ns = (section_t *)sec->list.next;
	ent = (entry_t *)sec->entry_head.next;

	while (ent != NULL) {
		entry_t *nent = (entry_t *)ent->list.next;

		free(ent);
		ent = nent;
	}

	free(sec);		
	sec = ns;
    }
}
#endif


/* Read and parse the configuration file into memory. */
static int
config_read(wchar_t *fn)
{
    char sname[128], ename[128];
    wchar_t buff[1024];
    section_t *sec, *ns;
    entry_t *ne;
    int c, d;
    FILE *f;

#if defined(ANSI_CFG) || !defined(_WIN32)
    f = plat_fopen(fn, L"rt");
#else
    f = plat_fopen(fn, L"rt, ccs=UNICODE");
#endif
    if (f == NULL) return(0);
	
    sec = malloc(sizeof(section_t));
    memset(sec, 0x00, sizeof(section_t));
    memset(&config_head, 0x00, sizeof(list_t));
    list_add(&sec->list, &config_head);

    while (1) {
	memset(buff, 0x00, sizeof(buff));
	fgetws(buff, sizeof_w(buff), f);
	if (feof(f)) break;

	/* Make sure there are no stray newlines or hard-returns in there. */
	if (buff[wcslen(buff)-1] == L'\n') buff[wcslen(buff)-1] = L'\0';
	if (buff[wcslen(buff)-1] == L'\r') buff[wcslen(buff)-1] = L'\0';

	/* Skip any leading whitespace. */
	c = 0;
	while ((buff[c] == L' ') || (buff[c] == L'\t'))
		  c++;

	/* Skip empty lines. */
	if (buff[c] == L'\0') continue;

	/* Skip lines that (only) have a comment. */
	if ((buff[c] == L'#') || (buff[c] == L';')) continue;

	if (buff[c] == L'[') {	/*Section*/
		c++;
		d = 0;
		while (buff[c] != L']' && buff[c])
			wctomb(&(sname[d++]), buff[c++]);
		sname[d] = L'\0';

		/* Is the section name properly terminated? */
		if (buff[c] != L']') continue;

		/* Create a new section and insert it. */
		ns = malloc(sizeof(section_t));
		memset(ns, 0x00, sizeof(section_t));
		strncpy(ns->name, sname, sizeof(ns->name));
		list_add(&ns->list, &config_head);

		/* New section is now the current one. */
		sec = ns;			
		continue;
	}

	/* Get the variable name. */
	d = 0;
	while ((buff[c] != L'=') && (buff[c] != L' ') && buff[c])
		wctomb(&(ename[d++]), buff[c++]);
	ename[d] = L'\0';

	/* Skip incomplete lines. */
	if (buff[c] == L'\0') continue;

	/* Look for =, skip whitespace. */
	while ((buff[c] == L'=' || buff[c] == L' ') && buff[c])
		c++;

	/* Skip incomplete lines. */
	if (buff[c] == L'\0') continue;

	/* This is where the value part starts. */
	d = c;

	/* Allocate a new variable entry.. */
	ne = malloc(sizeof(entry_t));
	memset(ne, 0x00, sizeof(entry_t));
	strncpy(ne->name, ename, sizeof(ne->name));
	wcsncpy(ne->wdata, &buff[d], sizeof_w(ne->wdata)-1);
	ne->wdata[sizeof_w(ne->wdata)-1] = L'\0';
	wcstombs(ne->data, ne->wdata, sizeof(ne->data));
	ne->data[sizeof(ne->data)-1] = '\0';

	/* .. and insert it. */
	list_add(&ne->list, &sec->entry_head);
    }

    (void)fclose(f);

    if (do_dump_config)
	config_dump();

    return(1);
}


/*
 * Write the in-memory configuration to disk.
 * This is a public function, because the Settings UI
 * want to directly write the configuration after it
 * has changed it.
 */
void
config_write(wchar_t *fn)
{
    wchar_t wtemp[512];
    section_t *sec;
    FILE *f;
    int fl = 0;

#if defined(ANSI_CFG) || !defined(_WIN32)
    f = plat_fopen(fn, L"wt");
#else
    f = plat_fopen(fn, L"wt, ccs=UNICODE");
#endif
    if (f == NULL) return;

    sec = (section_t *)config_head.next;
    while (sec != NULL) {
	entry_t *ent;

	if (sec->name[0]) {
		mbstowcs(wtemp, sec->name, strlen(sec->name)+1);
		if (fl)
			fwprintf(f, L"\n[%ls]\n", wtemp);
		  else
			fwprintf(f, L"[%ls]\n", wtemp);
		fl++;
	}

	ent = (entry_t *)sec->entry_head.next;
	while (ent != NULL) {
		if (ent->name[0] != '\0') {
			mbstowcs(wtemp, ent->name, sizeof_w(wtemp));
			if (ent->wdata[0] == L'\0')
				fwprintf(f, L"%ls = \n", wtemp);
			  else
				fwprintf(f, L"%ls = %ls\n", wtemp, ent->wdata);
			fl++;
		}

		ent = (entry_t *)ent->list.next;
	}

	sec = (section_t *)sec->list.next;
    }
	
    (void)fclose(f);
}


#if NOT_USED
static void
config_new(void)
{
#if defined(ANSI_CFG) || !defined(_WIN32)
    FILE *f = _wfopen(config_file, L"wt");
#else
    FILE *f = _wfopen(config_file, L"wt, ccs=UNICODE");
#endif

    if (file != NULL)
	(void)fclose(f);
}
#endif


/* Load "General" section. */
static void
load_general(void)
{
    char *cat = "General";
    char temp[512];
    char *p;

    vid_resize = !!config_get_int(cat, "vid_resize", 0);

    memset(temp, '\0', sizeof(temp));
    p = config_get_string(cat, "vid_renderer", "default");
    vid_api = plat_vidapi(p);
    config_delete_var(cat, "vid_api");

    video_fullscreen_scale = config_get_int(cat, "video_fullscreen_scale", 0);

    video_fullscreen_first = config_get_int(cat, "video_fullscreen_first", 0);

    force_43 = !!config_get_int(cat, "force_43", 0);
    scale = config_get_int(cat, "scale", 1);
    if (scale > 3)
	scale = 3;

    enable_overscan = !!config_get_int(cat, "enable_overscan", 0);
    vid_cga_contrast = !!config_get_int(cat, "vid_cga_contrast", 0);
    video_grayscale = config_get_int(cat, "video_grayscale", 0);
    video_graytype = config_get_int(cat, "video_graytype", 0);

    rctrl_is_lalt = config_get_int(cat, "rctrl_is_lalt", 0);
    update_icons = config_get_int(cat, "update_icons", 1);

    window_remember = config_get_int(cat, "window_remember", 0);
    if (window_remember) {
	p = config_get_string(cat, "window_coordinates", NULL);
	if (p == NULL)
		p = "0, 0, 0, 0";
	sscanf(p, "%i, %i, %i, %i", &window_w, &window_h, &window_x, &window_y);
    } else {
	config_delete_var(cat, "window_remember");
	config_delete_var(cat, "window_coordinates");

	window_w = window_h = window_x = window_y = 0;
    }

    sound_gain = config_get_int(cat, "sound_gain", 0);

#ifdef USE_LANGUAGE
    /*
     * Currently, 86Box is English (US) only, but in the future
     * (version 3.0 at the earliest) other languages will be
     * added, therefore it is better to future-proof the code.
     */
    plat_langid = config_get_hex16(cat, "language", 0x0409);
#endif
}


/* Load "Machine" section. */
static void
load_machine(void)
{
    char *cat = "Machine";
    char *p;

    p = config_get_string(cat, "machine", NULL);
    if (p != NULL)
	machine = machine_get_machine_from_internal_name(p);
      else 
	machine = 0;
    if (machine >= machine_count())
	machine = machine_count() - 1;

    /* This is for backwards compatibility. */
    p = config_get_string(cat, "model", NULL);
    if (p != NULL) {
	/* Detect the old model typos and fix them. */
	if (! strcmp(p, "p55r2p4")) {
		machine = machine_get_machine_from_internal_name("p55t2p4");
	} else {
		machine = machine_get_machine_from_internal_name(p);
	}
	config_delete_var(cat, "model");
    }
    if (machine >= machine_count())
	machine = machine_count() - 1;

    romset = machine_getromset();
    cpu_manufacturer = config_get_int(cat, "cpu_manufacturer", 0);
    cpu = config_get_int(cat, "cpu", 0);
    cpu_waitstates = config_get_int(cat, "cpu_waitstates", 0);

    mem_size = config_get_int(cat, "mem_size", 4096);
    if (mem_size < (((machines[machine].flags & MACHINE_AT) &&
        (machines[machine].ram_granularity < 128)) ? machines[machine].min_ram*1024 : machines[machine].min_ram))
	mem_size = (((machines[machine].flags & MACHINE_AT) && (machines[machine].ram_granularity < 128)) ? machines[machine].min_ram*1024 : machines[machine].min_ram);
    if (mem_size > 1048576)
	mem_size = 1048576;

    cpu_use_dynarec = !!config_get_int(cat, "cpu_use_dynarec", 0);

    enable_external_fpu = !!config_get_int(cat, "cpu_enable_fpu", 0);

    enable_sync = !!config_get_int(cat, "enable_sync", 1);

    /* Remove this after a while.. */
    config_delete_var(cat, "nvr_path");
}


/* Load "Video" section. */
static void
load_video(void)
{
    char *cat = "Video";
    char *p;

    if (machines[machine].fixed_gfxcard) {
	config_delete_var(cat, "gfxcard");
	gfxcard = GFX_INTERNAL;
    } else {
	p = config_get_string(cat, "gfxcard", NULL);
	if (p == NULL) {
		if (machines[machine].flags & MACHINE_VIDEO) {
			p = (char *)malloc((strlen("internal")+1)*sizeof(char));
			strcpy(p, "internal");
		} else {
			p = (char *)malloc((strlen("none")+1)*sizeof(char));
			strcpy(p, "none");
		}
	}
	gfxcard = video_get_video_from_internal_name(p);
    }

    voodoo_enabled = !!config_get_int(cat, "voodoo", 0);
}


/* Load "Input Devices" section. */
static void
load_input_devices(void)
{
    char *cat = "Input devices";
    char temp[512];
    int c, d;
    char *p;

    p = config_get_string(cat, "mouse_type", NULL);
    if (p != NULL)
	mouse_type = mouse_get_from_internal_name(p);
      else
	mouse_type = 0;

    joystick_type = config_get_int(cat, "joystick_type", 7);

    for (c=0; c<joystick_get_max_joysticks(joystick_type); c++) {
	sprintf(temp, "joystick_%i_nr", c);
	joystick_state[c].plat_joystick_nr = config_get_int(cat, temp, 0);

	if (joystick_state[c].plat_joystick_nr) {
		for (d=0; d<joystick_get_axis_count(joystick_type); d++) {
			sprintf(temp, "joystick_%i_axis_%i", c, d);
			joystick_state[c].axis_mapping[d] = config_get_int(cat, temp, d);
		}
		for (d=0; d<joystick_get_button_count(joystick_type); d++) {			
			sprintf(temp, "joystick_%i_button_%i", c, d);
			joystick_state[c].button_mapping[d] = config_get_int(cat, temp, d);
		}
		for (d=0; d<joystick_get_pov_count(joystick_type); d++) {
			sprintf(temp, "joystick_%i_pov_%i", c, d);
			p = config_get_string(cat, temp, "0, 0");
			joystick_state[c].pov_mapping[d][0] = joystick_state[c].pov_mapping[d][1] = 0;
			sscanf(p, "%i, %i", &joystick_state[c].pov_mapping[d][0], &joystick_state[c].pov_mapping[d][1]);
		}
	}
    }
}


/* Load "Sound" section. */
static void
load_sound(void)
{
    char *cat = "Sound";
    char temp[512];
    char *p;

    p = config_get_string(cat, "sndcard", NULL);
    if (p != NULL)
	sound_card_current = sound_card_get_from_internal_name(p);
      else
	sound_card_current = 0;

    p = config_get_string(cat, "midi_device", NULL);
    if (p != NULL)
	midi_device_current = midi_device_get_from_internal_name(p);
      else
	midi_device_current = 0;

    mpu401_standalone_enable = !!config_get_int(cat, "mpu401_standalone", 0);

    SSI2001 = !!config_get_int(cat, "ssi2001", 0);
    GAMEBLASTER = !!config_get_int(cat, "gameblaster", 0);
    GUS = !!config_get_int(cat, "gus", 0);

    memset(temp, '\0', sizeof(temp));
    p = config_get_string(cat, "opl_type", "dbopl");
    strcpy(temp, p);
    if (!strcmp(temp, "nukedopl") || !strcmp(temp, "1"))
	opl_type = 1;
    else
	opl_type = 0;

    memset(temp, '\0', sizeof(temp));
    p = config_get_string(cat, "sound_type", "float");
    strcpy(temp, p);
    if (!strcmp(temp, "float") || !strcmp(temp, "1"))
	sound_is_float = 1;
      else
	sound_is_float = 0;
}


/* Load "Network" section. */
static void
load_network(void)
{
    char *cat = "Network";
    char *p;

    p = config_get_string(cat, "net_type", NULL);
    if (p != NULL) {
	if (!strcmp(p, "pcap") || !strcmp(p, "1"))
		network_type = NET_TYPE_PCAP;
	else
	if (!strcmp(p, "slirp") || !strcmp(p, "2"))
		network_type = NET_TYPE_SLIRP;
	else
		network_type = NET_TYPE_NONE;
    } else
	network_type = NET_TYPE_NONE;

    memset(network_host, '\0', sizeof(network_host));
    p = config_get_string(cat, "net_host_device", NULL);
    if (p == NULL) {
	p = config_get_string(cat, "net_host_device", NULL);
	if (p != NULL)
		config_delete_var(cat, "net_host_device");
    }
    if (p != NULL) {
	if ((network_dev_to_id(p) == -1) || (network_ndev == 1)) {
		if ((network_ndev == 1) && strcmp(network_host, "none")) {
			ui_msgbox(MBX_ERROR, (wchar_t *)IDS_2103);
		} else if (network_dev_to_id(p) == -1) {
			ui_msgbox(MBX_ERROR, (wchar_t *)IDS_2104);
		}

		strcpy(network_host, "none");
	} else {
		strcpy(network_host, p);
	}
    } else
	strcpy(network_host, "none");

    p = config_get_string(cat, "net_card", NULL);
    if (p != NULL)
	network_card = network_card_get_from_internal_name(p);
      else
	network_card = 0;
}


/* Load "Ports" section. */
static void
load_ports(void)
{
    char *cat = "Ports (COM & LPT)";
    char *p;

    serial_enabled[0] = !!config_get_int(cat, "serial1_enabled", 1);
    serial_enabled[1] = !!config_get_int(cat, "serial2_enabled", 1);
    lpt_enabled = !!config_get_int(cat, "lpt_enabled", 1);

    p = (char *)config_get_string(cat, "lpt1_device", NULL);
    if (p != NULL)
	strcpy(lpt_device_names[0], p);
      else
	strcpy(lpt_device_names[0], "none");

    p = (char *)config_get_string(cat, "lpt2_device", NULL);
    if (p != NULL)
	strcpy(lpt_device_names[1], p);
      else
	strcpy(lpt_device_names[1], "none");

    p = (char *)config_get_string(cat, "lpt3_device", NULL);
    if (p != NULL)
	strcpy(lpt_device_names[2], p);
      else
	strcpy(lpt_device_names[2], "none");
}


/* Load "Other Peripherals" section. */
static void
load_other_peripherals(void)
{
    char *cat = "Other peripherals";
    char *p;

    p = config_get_string(cat, "scsicard", NULL);
    if (p != NULL)
	scsi_card_current = scsi_card_get_from_internal_name(p);
      else
	scsi_card_current = 0;

    if (hdc_name) {
	free(hdc_name);
	hdc_name = NULL;
    }
    p = config_get_string(cat, "hdc", NULL);
    if (p == NULL) {
	p = config_get_string(cat, "hdd_controller", NULL);
	if (p != NULL)
		config_delete_var(cat, "hdd_controller");
    }
    if (p == NULL) {
	if (machines[machine].flags & MACHINE_HDC) {
		hdc_name = (char *) malloc((strlen("internal") + 1) * sizeof(char));
		strcpy(hdc_name, "internal");
	} else {
		hdc_name = (char *) malloc((strlen("none") + 1) * sizeof(char));
		strcpy(hdc_name, "none");
	}
    } else {
	hdc_name = (char *) malloc((strlen(p) + 1) * sizeof(char));
	strcpy(hdc_name, p);
    }
    config_set_string(cat, "hdc", hdc_name);

    ide_ter_enabled = !!config_get_int(cat, "ide_ter", 0);
    ide_qua_enabled = !!config_get_int(cat, "ide_qua", 0);

    bugger_enabled = !!config_get_int(cat, "bugger_enabled", 0);
}


/* Load "Hard Disks" section. */
static void
load_hard_disks(void)
{
    char *cat = "Hard disks";
    char temp[512], tmp2[512];
    char s[512];
    int c;
    char *p;
    wchar_t *wp;
    uint32_t max_spt, max_hpc, max_tracks;
    uint32_t board = 0, dev = 0;
    /* FIXME: Remove in a month. */
    int lun;

    memset(temp, '\0', sizeof(temp));
    for (c=0; c<HDD_NUM; c++) {
	sprintf(temp, "hdd_%02i_parameters", c+1);
	p = config_get_string(cat, temp, "0, 0, 0, 0, none");
	sscanf(p, "%u, %u, %u, %i, %s",
		&hdd[c].spt, &hdd[c].hpc, &hdd[c].tracks, (int *)&hdd[c].wp, s);

	hdd[c].bus = hdd_string_to_bus(s, 0);
	switch(hdd[c].bus) {
		case HDD_BUS_DISABLED:
		default:
			max_spt = max_hpc = max_tracks = 0;
			break;

		case HDD_BUS_MFM:
			max_spt = 17;	/* 26 for RLL */
			max_hpc = 15;
			max_tracks = 1023;
			break;

		case HDD_BUS_ESDI:
		case HDD_BUS_XTA:
			max_spt = 63;
			max_hpc = 16;
			max_tracks = 1023;
			break;

		case HDD_BUS_IDE:
			max_spt = 63;
			max_hpc = 16;
			max_tracks = 266305;
			break;

		case HDD_BUS_SCSI:
			max_spt = 99;
			max_hpc = 255;
			max_tracks = 266305;
			break;
	}

	if (hdd[c].spt > max_spt)
		hdd[c].spt = max_spt;
	if (hdd[c].hpc > max_hpc)
		hdd[c].hpc = max_hpc;
	if (hdd[c].tracks > max_tracks)
		hdd[c].tracks = max_tracks;

	/* MFM/RLL */
	sprintf(temp, "hdd_%02i_mfm_channel", c+1);
	if (hdd[c].bus == HDD_BUS_MFM)
		hdd[c].mfm_channel = !!config_get_int(cat, temp, c & 1);
	  else
		config_delete_var(cat, temp);

	/* XTA */
	sprintf(temp, "hdd_%02i_xta_channel", c+1);
	if (hdd[c].bus == HDD_BUS_XTA)
		hdd[c].xta_channel = !!config_get_int(cat, temp, c & 1);
	  else
		config_delete_var(cat, temp);

	/* ESDI */
	sprintf(temp, "hdd_%02i_esdi_channel", c+1);
	if (hdd[c].bus == HDD_BUS_ESDI)
		hdd[c].esdi_channel = !!config_get_int(cat, temp, c & 1);
	  else
		config_delete_var(cat, temp);

	/* IDE */
	// FIXME: Remove in a month.
	sprintf(temp, "hdd_%02i_xtide_channel", c+1);
	if (hdd[c].bus == HDD_BUS_IDE)
		hdd[c].ide_channel = !!config_get_int(cat, temp, c & 1);
	  else
		config_delete_var(cat, temp);

	sprintf(temp, "hdd_%02i_ide_channel", c+1);
	if (hdd[c].bus == HDD_BUS_IDE) {
		sprintf(tmp2, "%01u:%01u", c>>1, c&1);
		p = config_get_string(cat, temp, tmp2);
		sscanf(p, "%01u:%01u", &board, &dev);
		board &= 3;
		dev &= 1;
		hdd[c].ide_channel = (board<<1) + dev;

		if (hdd[c].ide_channel > 7)
			hdd[c].ide_channel = 7;
	} else {
		config_delete_var(cat, temp);
	}

	/* SCSI */
	sprintf(temp, "hdd_%02i_scsi_id", c+1);
	if (hdd[c].bus == HDD_BUS_SCSI) {
		hdd[c].scsi_id = config_get_int(cat, temp, c);

		if (hdd[c].scsi_id > 15)
			hdd[c].scsi_id = 15;
	} else
		config_delete_var(cat, temp);

	/* FIXME: Remove in a month. */
	sprintf(temp, "hdd_%02i_scsi_location", c+1);
	if (hdd[c].bus == HDD_BUS_SCSI) {
		p = config_get_string(cat, temp, NULL);

		if (p) {
			sscanf(p, "%02i:%02i",
				(int *)&hdd[c].scsi_id, (int *)&lun);

			if (hdd[c].scsi_id > 15)
				hdd[c].scsi_id = 15;
		}
	}
	config_delete_var(cat, temp);

	memset(hdd[c].fn, 0x00, sizeof(hdd[c].fn));
	memset(hdd[c].prev_fn, 0x00, sizeof(hdd[c].prev_fn));
	sprintf(temp, "hdd_%02i_fn", c+1);
	wp = config_get_wstring(cat, temp, L"");

#if 0
	/*
	 * NOTE:
	 * Temporary hack to remove the absolute
	 * path currently saved in most config
	 * files.  We should remove this before
	 * finalizing this release!  --FvK
	 */
	if (! wcsnicmp(wp, usr_path, wcslen(usr_path))) {
		/*
		 * Yep, its absolute and prefixed
		 * with the CFG path.  Just strip
		 * that off for now...
		 */
		wcsncpy(hdd[c].fn, &wp[wcslen(usr_path)], sizeof_w(hdd[c].fn));
	} else
#endif
	wcsncpy(hdd[c].fn, wp, sizeof_w(hdd[c].fn));

	/* If disk is empty or invalid, mark it for deletion. */
	if (! hdd_is_valid(c)) {
		sprintf(temp, "hdd_%02i_parameters", c+1);
		config_delete_var(cat, temp);

		sprintf(temp, "hdd_%02i_preide_channels", c+1);
		config_delete_var(cat, temp);

		sprintf(temp, "hdd_%02i_ide_channels", c+1);
		config_delete_var(cat, temp);

		sprintf(temp, "hdd_%02i_scsi_id", c+1);
		config_delete_var(cat, temp);

		/* FIXME: Remove in a month. */
		sprintf(temp, "hdd_%02i_scsi_location", c+1);
		config_delete_var(cat, temp);

		sprintf(temp, "hdd_%02i_fn", c+1);
		config_delete_var(cat, temp);
	}

	sprintf(temp, "hdd_%02i_mfm_channel", c+1);
	config_delete_var(cat, temp);

	sprintf(temp, "hdd_%02i_ide_channel", c+1);
	config_delete_var(cat, temp);
    }
}


/* Load "Floppy Drives" section. */
static void
load_floppy_drives(void)
{
    char *cat = "Floppy drives";
    char temp[512], *p;
    wchar_t *wp;
    int c;

    for (c=0; c<FDD_NUM; c++) {
	sprintf(temp, "fdd_%02i_type", c+1);
	p = config_get_string(cat, temp, (c < 2) ? "525_2dd" : "none");
       	fdd_set_type(c, fdd_get_from_internal_name(p));
	if (fdd_get_type(c) > 13)
		fdd_set_type(c, 13);

	sprintf(temp, "fdd_%02i_fn", c + 1);
	wp = config_get_wstring(cat, temp, L"");

#if 0
	/*
	 * NOTE:
	 * Temporary hack to remove the absolute
	 * path currently saved in most config
	 * files.  We should remove this before
	 * finalizing this release!  --FvK
	 */
	if (! wcsnicmp(wp, usr_path, wcslen(usr_path))) {
		/*
		 * Yep, its absolute and prefixed
		 * with the EXE path.  Just strip
		 * that off for now...
		 */
		wcsncpy(floppyfns[c], &wp[wcslen(usr_path)], sizeof_w(floppyfns[c]));
	} else
#endif
	wcsncpy(floppyfns[c], wp, sizeof_w(floppyfns[c]));

	/* if (*wp != L'\0')
		config_log("Floppy%d: %ls\n", c, floppyfns[c]); */
	sprintf(temp, "fdd_%02i_writeprot", c+1);
	ui_writeprot[c] = !!config_get_int(cat, temp, 0);
	sprintf(temp, "fdd_%02i_turbo", c + 1);
	fdd_set_turbo(c, !!config_get_int(cat, temp, 0));
	sprintf(temp, "fdd_%02i_check_bpb", c+1);
	fdd_set_check_bpb(c, !!config_get_int(cat, temp, 1));

	/* Check whether each value is default, if yes, delete it so that only non-default values will later be saved. */
	if (fdd_get_type(c) == ((c < 2) ? 2 : 0)) {
		sprintf(temp, "fdd_%02i_type", c+1);
		config_delete_var(cat, temp);
	}
	if (wcslen(floppyfns[c]) == 0) {
		sprintf(temp, "fdd_%02i_fn", c+1);
		config_delete_var(cat, temp);
	}
	if (ui_writeprot[c] == 0) {
		sprintf(temp, "fdd_%02i_writeprot", c+1);
		config_delete_var(cat, temp);
	}
	if (fdd_get_turbo(c) == 0) {
		sprintf(temp, "fdd_%02i_turbo", c+1);
		config_delete_var(cat, temp);
	}
	if (fdd_get_check_bpb(c) == 1) {
		sprintf(temp, "fdd_%02i_check_bpb", c+1);
		config_delete_var(cat, temp);
	}
    }
}


/* Load "Other Removable Devices" section. */
static void
load_other_removable_devices(void)
{
    char *cat = "Other removable devices";
    char temp[512], tmp2[512], *p;
    char s[512];
    unsigned int board = 0, dev = 0;
    wchar_t *wp;
    int c;
    /* FIXME: Remove in a month. */
    int lun;

    memset(temp, 0x00, sizeof(temp));
    for (c=0; c<CDROM_NUM; c++) {
	sprintf(temp, "cdrom_%02i_host_drive", c+1);
	cdrom_drives[c].host_drive = config_get_int(cat, temp, 0);
	cdrom_drives[c].prev_host_drive = cdrom_drives[c].host_drive;

	sprintf(temp, "cdrom_%02i_parameters", c+1);
	p = config_get_string(cat, temp, NULL);
	if (p != NULL)
		sscanf(p, "%01u, %s", &cdrom_drives[c].sound_on, s);
	  else
		sscanf("0, none", "%01u, %s", &cdrom_drives[c].sound_on, s);
	cdrom_drives[c].bus_type = hdd_string_to_bus(s, 1);

	sprintf(temp, "cdrom_%02i_speed", c+1);
	cdrom_drives[c].speed = config_get_int(cat, temp, 8);

	/* Default values, needed for proper operation of the Settings dialog. */
	cdrom_drives[c].ide_channel = cdrom_drives[c].scsi_device_id = c + 2;

	sprintf(temp, "cdrom_%02i_ide_channel", c+1);
	if (cdrom_drives[c].bus_type == CDROM_BUS_ATAPI) {
		sprintf(tmp2, "%01u:%01u", (c+2)>>1, (c+2)&1);
		p = config_get_string(cat, temp, tmp2);
		sscanf(p, "%01u:%01u", &board, &dev);
		board &= 3;
		dev &= 1;
		cdrom_drives[c].ide_channel = (board<<1)+dev;

		if (cdrom_drives[c].ide_channel > 7)
			cdrom_drives[c].ide_channel = 7;
	} else {
		sprintf(temp, "cdrom_%02i_scsi_id", c+1);
		if (cdrom_drives[c].bus_type == CDROM_BUS_SCSI) {
			cdrom_drives[c].scsi_device_id = config_get_int(cat, temp, c+2);
	
			if (cdrom_drives[c].scsi_device_id > 15)
				cdrom_drives[c].scsi_device_id = 15;
		} else
			config_delete_var(cat, temp);

		/* FIXME: Remove in a month. */
		sprintf(temp, "cdrom_%02i_scsi_location", c+1);
		if (cdrom_drives[c].bus_type == CDROM_BUS_SCSI) {
			p = config_get_string(cat, temp, NULL);
			if (p) {
				sscanf(p, "%02u:%02u",
					&cdrom_drives[c].scsi_device_id, &lun);
	
				if (cdrom_drives[c].scsi_device_id > 15)
					cdrom_drives[c].scsi_device_id = 15;
			}
		}
		config_delete_var(cat, temp);
	}

	sprintf(temp, "cdrom_%02i_image_path", c+1);
	wp = config_get_wstring(cat, temp, L"");

#if 0
	/*
	 * NOTE:
	 * Temporary hack to remove the absolute
	 * path currently saved in most config
	 * files.  We should remove this before
	 * finalizing this release!  --FvK
	 */
	if (! wcsnicmp(wp, usr_path, wcslen(usr_path))) {
		/*
		 * Yep, its absolute and prefixed
		 * with the EXE path.  Just strip
		 * that off for now...
		 */
		wcsncpy(cdrom_image[c].image_path, &wp[wcslen(usr_path)], sizeof_w(cdrom_image[c].image_path));
	} else
#endif
	wcsncpy(cdrom_image[c].image_path, wp, sizeof_w(cdrom_image[c].image_path));

	if (cdrom_drives[c].host_drive < 'A')
		cdrom_drives[c].host_drive = 0;

	if ((cdrom_drives[c].host_drive == 0x200) &&
	    (wcslen(cdrom_image[c].image_path) == 0))
		cdrom_drives[c].host_drive = 0;

	/* If the CD-ROM is disabled, delete all its variables. */
	if (cdrom_drives[c].bus_type == CDROM_BUS_DISABLED) {
		sprintf(temp, "cdrom_%02i_host_drive", c+1);
		config_delete_var(cat, temp);

		sprintf(temp, "cdrom_%02i_parameters", c+1);
		config_delete_var(cat, temp);

		sprintf(temp, "cdrom_%02i_ide_channel", c+1);
		config_delete_var(cat, temp);

		sprintf(temp, "cdrom_%02i_scsi_id", c+1);
		config_delete_var(cat, temp);

		/* FIXME: Remove in a month. */
		sprintf(temp, "cdrom_%02i_scsi_location", c+1);
		config_delete_var(cat, temp);

		sprintf(temp, "cdrom_%02i_image_path", c+1);
		config_delete_var(cat, temp);
	}

	sprintf(temp, "cdrom_%02i_iso_path", c+1);
	config_delete_var(cat, temp);
    }

    memset(temp, 0x00, sizeof(temp));
    for (c=0; c<ZIP_NUM; c++) {
	sprintf(temp, "zip_%02i_parameters", c+1);
	p = config_get_string(cat, temp, NULL);
	if (p != NULL)
		sscanf(p, "%01u, %s", &zip_drives[c].is_250, s);
	  else
		sscanf("0, none", "%01u, %s", &zip_drives[c].is_250, s);
	zip_drives[c].bus_type = hdd_string_to_bus(s, 1);

	/* Default values, needed for proper operation of the Settings dialog. */
	zip_drives[c].ide_channel = zip_drives[c].scsi_device_id = c + 2;

	sprintf(temp, "zip_%02i_ide_channel", c+1);
	if (zip_drives[c].bus_type == ZIP_BUS_ATAPI) {
		sprintf(tmp2, "%01u:%01u", (c+2)>>1, (c+2)&1);
		p = config_get_string(cat, temp, tmp2);
		sscanf(p, "%01u:%01u", &board, &dev);
		board &= 3;
		dev &= 1;
		zip_drives[c].ide_channel = (board<<1)+dev;

		if (zip_drives[c].ide_channel > 7)
			zip_drives[c].ide_channel = 7;
	} else {
		sprintf(temp, "zip_%02i_scsi_id", c+1);
		if (zip_drives[c].bus_type == CDROM_BUS_SCSI) {
			zip_drives[c].scsi_device_id = config_get_int(cat, temp, c+2);
	
			if (zip_drives[c].scsi_device_id > 15)
				zip_drives[c].scsi_device_id = 15;
		} else
			config_delete_var(cat, temp);

		/* FIXME: Remove in a month. */
		sprintf(temp, "zip_%02i_scsi_location", c+1);
		if (zip_drives[c].bus_type == CDROM_BUS_SCSI) {
			p = config_get_string(cat, temp, NULL);
			if (p) {
				sscanf(p, "%02u:%02u",
					&zip_drives[c].scsi_device_id, &lun);
	
				if (zip_drives[c].scsi_device_id > 15)
					zip_drives[c].scsi_device_id = 15;
			}
		}
		config_delete_var(cat, temp);
	}

	sprintf(temp, "zip_%02i_image_path", c+1);
	wp = config_get_wstring(cat, temp, L"");

#if 0
	/*
	 * NOTE:
	 * Temporary hack to remove the absolute
	 * path currently saved in most config
	 * files.  We should remove this before
	 * finalizing this release!  --FvK
	 */
	if (! wcsnicmp(wp, usr_path, wcslen(usr_path))) {
		/*
		 * Yep, its absolute and prefixed
		 * with the EXE path.  Just strip
		 * that off for now...
		 */
		wcsncpy(zip_drives[c].image_path, &wp[wcslen(usr_path)], sizeof_w(zip_drives[c].image_path));
	} else
#endif
	wcsncpy(zip_drives[c].image_path, wp, sizeof_w(zip_drives[c].image_path));

	/* If the CD-ROM is disabled, delete all its variables. */
	if (zip_drives[c].bus_type == ZIP_BUS_DISABLED) {
		sprintf(temp, "zip_%02i_host_drive", c+1);
		config_delete_var(cat, temp);

		sprintf(temp, "zip_%02i_parameters", c+1);
		config_delete_var(cat, temp);

		sprintf(temp, "zip_%02i_ide_channel", c+1);
		config_delete_var(cat, temp);

		sprintf(temp, "zip_%02i_scsi_id", c+1);
		config_delete_var(cat, temp);

		/* FIXME: Remove in a month. */
		sprintf(temp, "zip_%02i_scsi_location", c+1);
		config_delete_var(cat, temp);

		sprintf(temp, "zip_%02i_image_path", c+1);
		config_delete_var(cat, temp);
	}

	sprintf(temp, "zip_%02i_iso_path", c+1);
	config_delete_var(cat, temp);
    }
}


/* Load the specified or a default configuration file. */
void
config_load(void)
{
    int i;

    config_log("Loading config file '%ls'..\n", cfg_path);

    memset(hdd, 0, sizeof(hard_disk_t));
    memset(cdrom_drives, 0, sizeof(cdrom_drive_t) * CDROM_NUM);
    memset(cdrom_image, 0, sizeof(cdrom_image_t) * CDROM_NUM);
#ifdef USE_IOCTL
    memset(cdrom_ioctl, 0, sizeof(cdrom_ioctl_t) * CDROM_NUM);
#endif
    memset(zip_drives, 0, sizeof(zip_drive_t));

    if (! config_read(cfg_path)) {
	cpu = 0;
#ifdef USE_LANGUAGE
	plat_langid = 0x0409;
#endif
	scale = 1;
	machine = machine_get_machine_from_internal_name("ibmpc");
	gfxcard = GFX_CGA;
	vid_api = plat_vidapi("default");
	enable_sync = 1;
	joystick_type = 7;
	if (hdc_name) {
		free(hdc_name);
		hdc_name = NULL;
	}
	hdc_name = (char *) malloc((strlen("none")+1) * sizeof(char));
	strcpy(hdc_name, "none");
	serial_enabled[0] = 1;
	serial_enabled[1] = 1;
	lpt_enabled = 1;
	for (i = 0; i < FDD_NUM; i++) {
		if (i < 2)
			fdd_set_type(i, 2);
		else
			fdd_set_type(i, 0);

		fdd_set_turbo(i, 0);
		fdd_set_check_bpb(i, 1);
	}
	mem_size = 640;
	opl_type = 0;

	config_log("Config file not present or invalid!\n");
	return;
    }

    load_general();			/* General */
    load_machine();			/* Machine */
    load_video();			/* Video */
    load_input_devices();		/* Input devices */
    load_sound();			/* Sound */
    load_network();			/* Network */
    load_ports();			/* Ports (COM & LPT) */
    load_other_peripherals();		/* Other peripherals */
    load_hard_disks();			/* Hard disks */
    load_floppy_drives();		/* Floppy drives */
    load_other_removable_devices();	/* Other removable devices */

    /* Mark the configuration as changed. */
    config_changed = 1;

    config_log("Config loaded.\n\n");
}


/* Save "General" section. */
static void
save_general(void)
{
    char *cat = "General";
    char temp[512];

    char *va_name;

    config_set_int(cat, "vid_resize", vid_resize);
    if (vid_resize == 0)
	config_delete_var(cat, "vid_resize");

    va_name = plat_vidapi_name(vid_api);
    if (!strcmp(va_name, "default")) {
	config_delete_var(cat, "vid_renderer");
    } else {
	config_set_string(cat, "vid_renderer", va_name);
    }

    if (video_fullscreen_scale == 0)
	config_delete_var(cat, "video_fullscreen_scale");
      else
	config_set_int(cat, "video_fullscreen_scale", video_fullscreen_scale);

    if (video_fullscreen_first == 0)
	config_delete_var(cat, "video_fullscreen_first");
      else
	config_set_int(cat, "video_fullscreen_first", video_fullscreen_first);

    if (force_43 == 0)
	config_delete_var(cat, "force_43");
      else
	config_set_int(cat, "force_43", force_43);

    if (scale == 1)
	config_delete_var(cat, "scale");
      else
	config_set_int(cat, "scale", scale);

    if (enable_overscan == 0)
	config_delete_var(cat, "enable_overscan");
      else
	config_set_int(cat, "enable_overscan", enable_overscan);

    if (vid_cga_contrast == 0)
	config_delete_var(cat, "vid_cga_contrast");
      else
	config_set_int(cat, "vid_cga_contrast", vid_cga_contrast);

    if (video_grayscale == 0)
	config_delete_var(cat, "video_grayscale");
      else
	config_set_int(cat, "video_grayscale", video_grayscale);

    if (video_graytype == 0)
	config_delete_var(cat, "video_graytype");
      else
	config_set_int(cat, "video_graytype", video_graytype);

    if (rctrl_is_lalt == 0)
	config_delete_var(cat, "rctrl_is_lalt");
      else
	config_set_int(cat, "rctrl_is_lalt", rctrl_is_lalt);

    if (update_icons == 1)
	config_delete_var(cat, "update_icons");
      else
	config_set_int(cat, "update_icons", update_icons);

    if (window_remember) {
	config_set_int(cat, "window_remember", window_remember);

	sprintf(temp, "%i, %i, %i, %i", window_w, window_h, window_x, window_y);
	config_set_string(cat, "window_coordinates", temp);
    } else {
	config_delete_var(cat, "window_remember");
	config_delete_var(cat, "window_coordinates");
    }

    if (sound_gain != 0)
	config_set_int(cat, "sound_gain", sound_gain);
    else
	config_delete_var(cat, "sound_gain");

#ifdef USE_LANGUAGE
    if (plat_langid == 0x0409)
	config_delete_var(cat, "language");
      else
	config_set_hex16(cat, "language", plat_langid);
#endif

    delete_section_if_empty(cat);
}


/* Save "Machine" section. */
static void
save_machine(void)
{
    char *cat = "Machine";

    config_set_string(cat, "machine", machine_get_internal_name());

    if (cpu_manufacturer == 0)
	config_delete_var(cat, "cpu_manufacturer");
      else
	config_set_int(cat, "cpu_manufacturer", cpu_manufacturer);

    if (cpu == 0)
	config_delete_var(cat, "cpu");
      else
	config_set_int(cat, "cpu", cpu);

    if (cpu_waitstates == 0)
	config_delete_var(cat, "cpu_waitstates");
      else
	config_set_int(cat, "cpu_waitstates", cpu_waitstates);

    if (mem_size == 4096)
	config_delete_var(cat, "mem_size");
      else
	config_set_int(cat, "mem_size", mem_size);

    config_set_int(cat, "cpu_use_dynarec", cpu_use_dynarec);

    if (enable_external_fpu == 0)
	config_delete_var(cat, "cpu_enable_fpu");
      else
	config_set_int(cat, "cpu_enable_fpu", enable_external_fpu);

    if (enable_sync == 1)
	config_delete_var(cat, "enable_sync");
      else
	config_set_int(cat, "enable_sync", enable_sync);

    delete_section_if_empty(cat);
}


/* Save "Video" section. */
static void
save_video(void)
{
    char *cat = "Video";

    config_set_string(cat, "gfxcard",
	video_get_internal_name(video_old_to_new(gfxcard)));

    if (voodoo_enabled == 0)
	config_delete_var(cat, "voodoo");
      else
	config_set_int(cat, "voodoo", voodoo_enabled);

    delete_section_if_empty(cat);
}


/* Save "Input Devices" section. */
static void
save_input_devices(void)
{
    char *cat = "Input devices";
    char temp[512], tmp2[512];
    int c, d;

    config_set_string(cat, "mouse_type", mouse_get_internal_name(mouse_type));

    if ((joystick_type == 0) || (joystick_type == 7)) {
	if (joystick_type == 7)
		config_delete_var(cat, "joystick_type");
	  else
		config_set_int(cat, "joystick_type", joystick_type);

	for (c=0; c<16; c++) {
		sprintf(tmp2, "joystick_%i_nr", c);
		config_delete_var(cat, tmp2);

		for (d=0; d<16; d++) {			
			sprintf(tmp2, "joystick_%i_axis_%i", c, d);
			config_delete_var(cat, tmp2);
		}
		for (d=0; d<16; d++) {			
			sprintf(tmp2, "joystick_%i_button_%i", c, d);
			config_delete_var(cat, tmp2);
		}
		for (d=0; d<16; d++) {			
			sprintf(tmp2, "joystick_%i_pov_%i", c, d);
			config_delete_var(cat, tmp2);
		}
	}
    } else {
	config_set_int(cat, "joystick_type", joystick_type);

	for (c=0; c<joystick_get_max_joysticks(joystick_type); c++) {
		sprintf(tmp2, "joystick_%i_nr", c);
		config_set_int(cat, tmp2, joystick_state[c].plat_joystick_nr);

		if (joystick_state[c].plat_joystick_nr) {
			for (d=0; d<joystick_get_axis_count(joystick_type); d++) {			
				sprintf(tmp2, "joystick_%i_axis_%i", c, d);
				config_set_int(cat, tmp2, joystick_state[c].axis_mapping[d]);
			}
			for (d=0; d<joystick_get_button_count(joystick_type); d++) {			
				sprintf(tmp2, "joystick_%i_button_%i", c, d);
				config_set_int(cat, tmp2, joystick_state[c].button_mapping[d]);
			}
			for (d=0; d<joystick_get_pov_count(joystick_type); d++) {			
				sprintf(tmp2, "joystick_%i_pov_%i", c, d);
				sprintf(temp, "%i, %i", joystick_state[c].pov_mapping[d][0], joystick_state[c].pov_mapping[d][1]);
				config_set_string(cat, tmp2, temp);
			}
		}
	}
    }

    delete_section_if_empty(cat);
}


/* Save "Sound" section. */
static void
save_sound(void)
{
    char *cat = "Sound";

    if (sound_card_current == 0)
	config_delete_var(cat, "sndcard");
      else
	config_set_string(cat, "sndcard", sound_card_get_internal_name(sound_card_current));

    if (!strcmp(midi_device_get_internal_name(midi_device_current), "none"))
	config_delete_var(cat, "midi_device");
      else
	config_set_string(cat, "midi_device", midi_device_get_internal_name(midi_device_current));

    if (mpu401_standalone_enable == 0)
	config_delete_var(cat, "mpu401_standalone");
      else
	config_set_int(cat, "mpu401_standalone", mpu401_standalone_enable);

    if (SSI2001 == 0)
	config_delete_var(cat, "ssi2001");
      else
	config_set_int(cat, "ssi2001", SSI2001);

    if (GAMEBLASTER == 0)
	config_delete_var(cat, "gameblaster");
      else
	config_set_int(cat, "gameblaster", GAMEBLASTER);

    if (GUS == 0)
	config_delete_var(cat, "gus");
      else
	config_set_int(cat, "gus", GUS);

    if (opl_type == 0)
	config_delete_var(cat, "opl_type");
      else
	config_set_string(cat, "opl_type", (opl_type == 1) ? "nukedopl" : "dbopl");

    if (sound_is_float == 1)
	config_delete_var(cat, "sound_type");
      else
	config_set_string(cat, "sound_type", (sound_is_float == 1) ? "float" : "int16");

    delete_section_if_empty(cat);
}


/* Save "Network" section. */
static void
save_network(void)
{
    char *cat = "Network";

    if (network_type == NET_TYPE_NONE)
	config_delete_var(cat, "net_type");
      else
	config_set_string(cat, "net_type",
		(network_type == NET_TYPE_SLIRP) ? "slirp" : "pcap");

    if (network_host[0] != '\0') {
	if (! strcmp(network_host, "none"))
		config_delete_var(cat, "net_host_device");
	  else
		config_set_string(cat, "net_host_device", network_host);
    } else {
	/* config_set_string(cat, "net_host_device", "none"); */
	config_delete_var(cat, "net_host_device");
    }

    if (network_card == 0)
	config_delete_var(cat, "net_card");
      else
	config_set_string(cat, "net_card",
			  network_card_get_internal_name(network_card));

    delete_section_if_empty(cat);
}


/* Save "Ports" section. */
static void
save_ports(void)
{
    char *cat = "Ports (COM & LPT)";

    if (serial_enabled[0])
	config_delete_var(cat, "serial1_enabled");
      else
	config_set_int(cat, "serial1_enabled", serial_enabled[0]);

    if (serial_enabled[1])
	config_delete_var(cat, "serial2_enabled");
      else
	config_set_int(cat, "serial2_enabled", serial_enabled[1]);

    if (lpt_enabled)
	config_delete_var(cat, "lpt_enabled");
      else
	config_set_int(cat, "lpt_enabled", lpt_enabled);

    if (!strcmp(lpt_device_names[0], "none"))
	config_delete_var(cat, "lpt1_device");
      else
	config_set_string(cat, "lpt1_device", lpt_device_names[0]);

    if (!strcmp(lpt_device_names[1], "none"))
	config_delete_var(cat, "lpt2_device");
      else
	config_set_string(cat, "lpt2_device", lpt_device_names[1]);

    if (!strcmp(lpt_device_names[2], "none"))
	config_delete_var(cat, "lpt3_device");
      else
	config_set_string(cat, "lpt3_device", lpt_device_names[2]);

    delete_section_if_empty(cat);
}


/* Save "Other Peripherals" section. */
static void
save_other_peripherals(void)
{
    char *cat = "Other peripherals";

    if (scsi_card_current == 0)
	config_delete_var(cat, "scsicard");
      else
	config_set_string(cat, "scsicard",
			  scsi_card_get_internal_name(scsi_card_current));

    config_set_string(cat, "hdc", hdc_name);

    if (ide_ter_enabled == 0)
	config_delete_var(cat, "ide_ter");
      else
	config_set_int(cat, "ide_ter", ide_ter_enabled);

    if (ide_qua_enabled == 0)
	config_delete_var(cat, "ide_qua");
      else
	config_set_int(cat, "ide_qua", ide_qua_enabled);

    if (bugger_enabled == 0)
	config_delete_var(cat, "bugger_enabled");
      else
	config_set_int(cat, "bugger_enabled", bugger_enabled);

    delete_section_if_empty(cat);
}


/* Save "Hard Disks" section. */
static void
save_hard_disks(void)
{
    char *cat = "Hard disks";
    char temp[24], tmp2[64];
    char *p;
    int c;

    memset(temp, 0x00, sizeof(temp));
    for (c=0; c<HDD_NUM; c++) {
	sprintf(temp, "hdd_%02i_parameters", c+1);
	if (hdd_is_valid(c)) {
		p = hdd_bus_to_string(hdd[c].bus, 0);
		sprintf(tmp2, "%u, %u, %u, %i, %s",
			hdd[c].spt, hdd[c].hpc, hdd[c].tracks, hdd[c].wp, p);
		config_set_string(cat, temp, tmp2);
	} else {
		config_delete_var(cat, temp);
	}

	sprintf(temp, "hdd_%02i_mfm_channel", c+1);
	if (hdd_is_valid(c) && (hdd[c].bus == HDD_BUS_MFM))
		config_set_int(cat, temp, hdd[c].mfm_channel);
	  else
		config_delete_var(cat, temp);

	sprintf(temp, "hdd_%02i_xta_channel", c+1);
	if (hdd_is_valid(c) && (hdd[c].bus == HDD_BUS_XTA))
		config_set_int(cat, temp, hdd[c].xta_channel);
	  else
		config_delete_var(cat, temp);

	sprintf(temp, "hdd_%02i_esdi_channel", c+1);
	if (hdd_is_valid(c) && (hdd[c].bus == HDD_BUS_ESDI))
		config_set_int(cat, temp, hdd[c].esdi_channel);
	else
		config_delete_var(cat, temp);

	sprintf(temp, "hdd_%02i_ide_channel", c+1);
	if (! hdd_is_valid(c) || (hdd[c].bus != HDD_BUS_IDE)) {
		config_delete_var(cat, temp);
	} else {
		sprintf(tmp2, "%01u:%01u", hdd[c].ide_channel >> 1, hdd[c].ide_channel & 1);
		config_set_string(cat, temp, tmp2);
	}

	sprintf(temp, "hdd_%02i_scsi_id", c+1);
	if (hdd_is_valid(c) && (hdd[c].bus == HDD_BUS_SCSI))
		config_set_int(cat, temp, hdd[c].scsi_id);
	else
		config_delete_var(cat, temp);

	sprintf(temp, "hdd_%02i_fn", c+1);
	if (hdd_is_valid(c) && (wcslen(hdd[c].fn) != 0))
		config_set_wstring(cat, temp, hdd[c].fn);
	else
		config_delete_var(cat, temp);
    }

    delete_section_if_empty(cat);
}


/* Save "Floppy Drives" section. */
static void
save_floppy_drives(void)
{
    char *cat = "Floppy drives";
    char temp[512];
    int c;

    for (c=0; c<FDD_NUM; c++) {
	sprintf(temp, "fdd_%02i_type", c+1);
	if (fdd_get_type(c) == ((c < 2) ? 2 : 0))
		config_delete_var(cat, temp);
	  else
		config_set_string(cat, temp,
				  fdd_get_internal_name(fdd_get_type(c)));

	sprintf(temp, "fdd_%02i_fn", c+1);
	if (wcslen(floppyfns[c]) == 0) {
		config_delete_var(cat, temp);

		ui_writeprot[c] = 0;

		sprintf(temp, "fdd_%02i_writeprot", c+1);
		config_delete_var(cat, temp);
	} else {
		config_set_wstring(cat, temp, floppyfns[c]);
	}

	sprintf(temp, "fdd_%02i_writeprot", c+1);
	if (ui_writeprot[c] == 0)
		config_delete_var(cat, temp);
	  else
		config_set_int(cat, temp, ui_writeprot[c]);

	sprintf(temp, "fdd_%02i_turbo", c+1);
	if (fdd_get_turbo(c) == 0)
		config_delete_var(cat, temp);
	  else
		config_set_int(cat, temp, fdd_get_turbo(c));

	sprintf(temp, "fdd_%02i_check_bpb", c+1);
	if (fdd_get_check_bpb(c) == 1)
		config_delete_var(cat, temp);
	  else
		config_set_int(cat, temp, fdd_get_check_bpb(c));
    }

    delete_section_if_empty(cat);
}


/* Save "Other Removable Devices" section. */
static void
save_other_removable_devices(void)
{
    char *cat = "Other removable devices";
    char temp[512], tmp2[512];
    int c;

    for (c=0; c<CDROM_NUM; c++) {
	sprintf(temp, "cdrom_%02i_host_drive", c+1);
	if ((cdrom_drives[c].bus_type == 0) ||
	    (cdrom_drives[c].host_drive < 'A') || ((cdrom_drives[c].host_drive > 'Z') && (cdrom_drives[c].host_drive != 200))) {
		config_delete_var(cat, temp);
	} else {
		config_set_int(cat, temp, cdrom_drives[c].host_drive);
	}

	sprintf(temp, "cdrom_%02i_speed", c+1);
	if ((cdrom_drives[c].bus_type == 0) || (cdrom_drives[c].speed == 8)) {
		config_delete_var(cat, temp);
	} else {
		config_set_int(cat, temp, cdrom_drives[c].speed);
	}

	sprintf(temp, "cdrom_%02i_parameters", c+1);
	if (cdrom_drives[c].bus_type == 0) {
		config_delete_var(cat, temp);
	} else {
		sprintf(tmp2, "%u, %s", cdrom_drives[c].sound_on,
			hdd_bus_to_string(cdrom_drives[c].bus_type, 1));
		config_set_string(cat, temp, tmp2);
	}

	sprintf(temp, "cdrom_%02i_ide_channel", c+1);
	if (cdrom_drives[c].bus_type != CDROM_BUS_ATAPI)
		config_delete_var(cat, temp);
	else {
		sprintf(tmp2, "%01u:%01u", cdrom_drives[c].ide_channel>>1,
					cdrom_drives[c].ide_channel & 1);
		config_set_string(cat, temp, tmp2);
	}

	sprintf(temp, "cdrom_%02i_scsi_id", c + 1);
	if (cdrom_drives[c].bus_type != CDROM_BUS_SCSI) {
		config_delete_var(cat, temp);
	} else {
		config_set_int(cat, temp, cdrom_drives[c].scsi_device_id);
	}

	sprintf(temp, "cdrom_%02i_image_path", c + 1);
	if ((cdrom_drives[c].bus_type == 0) ||
	    (wcslen(cdrom_image[c].image_path) == 0)) {
		config_delete_var(cat, temp);
	} else {
		config_set_wstring(cat, temp, cdrom_image[c].image_path);
	}
    }

    for (c=0; c<ZIP_NUM; c++) {
	sprintf(temp, "zip_%02i_parameters", c+1);
	if (zip_drives[c].bus_type == 0) {
		config_delete_var(cat, temp);
	} else {
		sprintf(tmp2, "%u, %s", zip_drives[c].is_250,
			hdd_bus_to_string(zip_drives[c].bus_type, 1));
		config_set_string(cat, temp, tmp2);
	}
		
	sprintf(temp, "zip_%02i_ide_channel", c+1);
	if (zip_drives[c].bus_type != ZIP_BUS_ATAPI)
		config_delete_var(cat, temp);
	else {
		sprintf(tmp2, "%01u:%01u", zip_drives[c].ide_channel>>1,
					zip_drives[c].ide_channel & 1);
		config_set_string(cat, temp, tmp2);
	}

	sprintf(temp, "zip_%02i_scsi_id", c + 1);
	if (zip_drives[c].bus_type != ZIP_BUS_SCSI) {
		config_delete_var(cat, temp);
	} else {
		config_set_int(cat, temp, zip_drives[c].scsi_device_id);
	}

	sprintf(temp, "zip_%02i_image_path", c + 1);
	if ((zip_drives[c].bus_type == 0) ||
	    (wcslen(zip_drives[c].image_path) == 0)) {
		config_delete_var(cat, temp);
	} else {
		config_set_wstring(cat, temp, zip_drives[c].image_path);
	}
    }

    delete_section_if_empty(cat);
}


void
config_save(void)
{
    save_general();			/* General */
    save_machine();			/* Machine */
    save_video();			/* Video */
    save_input_devices();		/* Input devices */
    save_sound();			/* Sound */
    save_network();			/* Network */
    save_ports();			/* Ports (COM & LPT) */
    save_other_peripherals();		/* Other peripherals */
    save_hard_disks();			/* Hard disks */
    save_floppy_drives();		/* Floppy drives */
    save_other_removable_devices();	/* Other removable devices */

    config_write(cfg_path);
}


void
config_dump(void)
{
    section_t *sec;
	
    sec = (section_t *)config_head.next;
    while (sec != NULL) {
	entry_t *ent;

	if (sec->name && sec->name[0])
		config_log("[%s]\n", sec->name);
	
	ent = (entry_t *)sec->entry_head.next;
	while (ent != NULL) {
		config_log("%s = %ls\n", ent->name, ent->wdata);

		ent = (entry_t *)ent->list.next;
	}

	sec = (section_t *)sec->list.next;
    }
}


void
config_delete_var(char *head, char *name)
{
    section_t *section;
    entry_t *entry;

    section = find_section(head);
    if (section == NULL) return;
		
    entry = find_entry(section, name);
    if (entry != NULL) {
	list_delete(&entry->list, &section->entry_head);
	free(entry);
    }
}


int
config_get_int(char *head, char *name, int def)
{
    section_t *section;
    entry_t *entry;
    int value;

    section = find_section(head);
    if (section == NULL)
	return(def);
		
    entry = find_entry(section, name);
    if (entry == NULL)
	return(def);

    sscanf(entry->data, "%i", &value);

    return(value);
}


int
config_get_hex16(char *head, char *name, int def)
{
    section_t *section;
    entry_t *entry;
    unsigned int value;

    section = find_section(head);
    if (section == NULL)
	return(def);

    entry = find_entry(section, name);
    if (entry == NULL)
	return(def);

    sscanf(entry->data, "%04X", &value);

    return(value);
}


int
config_get_hex20(char *head, char *name, int def)
{
    section_t *section;
    entry_t *entry;
    unsigned int value;

    section = find_section(head);
    if (section == NULL)
	return(def);

    entry = find_entry(section, name);
    if (entry == NULL)
	return(def);

    sscanf(entry->data, "%05X", &value);

    return(value);
}


int
config_get_mac(char *head, char *name, int def)
{
    section_t *section;
    entry_t *entry;
    unsigned int val0 = 0, val1 = 0, val2 = 0;

    section = find_section(head);
    if (section == NULL)
	return(def);

    entry = find_entry(section, name);
    if (entry == NULL)
	return(def);

    sscanf(entry->data, "%02x:%02x:%02x", &val0, &val1, &val2);

    return((val0 << 16) + (val1 << 8) + val2);
}


char *
config_get_string(char *head, char *name, char *def)
{
    section_t *section;
    entry_t *entry;

    section = find_section(head);
    if (section == NULL)
	return(def);

    entry = find_entry(section, name);
    if (entry == NULL)
	return(def);
     
    return(entry->data);
}


wchar_t *
config_get_wstring(char *head, char *name, wchar_t *def)
{
    section_t *section;
    entry_t *entry;

    section = find_section(head);
    if (section == NULL)
	return(def);

    entry = find_entry(section, name);
    if (entry == NULL)
	return(def);
   
    return(entry->wdata);
}


void
config_set_int(char *head, char *name, int val)
{
    section_t *section;
    entry_t *ent;

    section = find_section(head);
    if (section == NULL)
	section = create_section(head);

    ent = find_entry(section, name);
    if (ent == NULL)
	ent = create_entry(section, name);

    sprintf(ent->data, "%i", val);
    mbstowcs(ent->wdata, ent->data, sizeof_w(ent->wdata));
}


void
config_set_hex16(char *head, char *name, int val)
{
    section_t *section;
    entry_t *ent;

    section = find_section(head);
    if (section == NULL)
	section = create_section(head);

    ent = find_entry(section, name);
    if (ent == NULL)
	ent = create_entry(section, name);

    sprintf(ent->data, "%04X", val);
    mbstowcs(ent->wdata, ent->data, sizeof_w(ent->wdata));
}


void
config_set_hex20(char *head, char *name, int val)
{
    section_t *section;
    entry_t *ent;

    section = find_section(head);
    if (section == NULL)
	section = create_section(head);

    ent = find_entry(section, name);
    if (ent == NULL)
	ent = create_entry(section, name);

    sprintf(ent->data, "%05X", val);
    mbstowcs(ent->wdata, ent->data, sizeof_w(ent->wdata));
}


void
config_set_mac(char *head, char *name, int val)
{
    section_t *section;
    entry_t *ent;

    section = find_section(head);
    if (section == NULL)
	section = create_section(head);

    ent = find_entry(section, name);
    if (ent == NULL)
	ent = create_entry(section, name);

    sprintf(ent->data, "%02x:%02x:%02x",
		(val>>16)&0xff, (val>>8)&0xff, val&0xff);
    mbstowcs(ent->wdata, ent->data, sizeof_w(ent->wdata));
}


void
config_set_string(char *head, char *name, char *val)
{
    section_t *section;
    entry_t *ent;

    section = find_section(head);
    if (section == NULL)
	section = create_section(head);

    ent = find_entry(section, name);
    if (ent == NULL)
	ent = create_entry(section, name);

    strncpy(ent->data, val, sizeof(ent->data));
    mbstowcs(ent->wdata, ent->data, sizeof_w(ent->wdata));
}


void
config_set_wstring(char *head, char *name, wchar_t *val)
{
    section_t *section;
    entry_t *ent;

    section = find_section(head);
    if (section == NULL)
	section = create_section(head);

    ent = find_entry(section, name);
    if (ent == NULL)
	ent = create_entry(section, name);

    memcpy(ent->wdata, val, sizeof_w(ent->wdata));
    wcstombs(ent->data, ent->wdata, sizeof(ent->data));
}
