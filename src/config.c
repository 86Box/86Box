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
 * Version:	@(#)config.c	1.0.16	2017/10/11
 *
 * Authors:	Sarah Walker,
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Overdoze,
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 *
 * NOTE:	Forcing config files to be in Unicode encoding breaks
 *		it on Windows XP, and possibly also Vista. Use the
 *		-DANSI_CFG for use on these systems.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <inttypes.h>
#include "ibm.h"
#include "cpu/cpu.h"
#include "nvr.h"
#include "config.h"
#include "device.h"
#include "lpt.h"
#include "cdrom/cdrom.h"
#include "disk/hdd.h"
#include "disk/hdc.h"
#include "disk/hdc_ide.h"
#include "floppy/floppy.h"
#include "floppy/fdc.h"
#include "floppy/fdd.h"
#include "game/gameport.h"
#include "machine/machine.h"
#include "mouse.h"
#include "network/network.h"
#include "scsi/scsi.h"
#include "sound/midi.h"
#include "sound/snd_dbopl.h"
#include "sound/snd_mpu401.h"
#include "sound/snd_opl.h"
#include "sound/sound.h"
#include "video/video.h"
#include "ui.h"
#include "win/plat_joystick.h"
#include "win/plat_midi.h"


typedef struct _list_ {
    struct _list_ *next;
} list_t;

typedef struct {
    list_t list;

    char name[256];

    list_t entry_head;
} section_t;

typedef struct {
    list_t list;

    char name[256];
    char data[256];
    wchar_t wdata[256];
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


wchar_t config_file_default[256];

static list_t	config_head;


static section_t *
find_section(char *name)
{
    section_t *sec;
    char blank[] = "";

    sec = (section_t *)config_head.next;
    if (name == NULL)
	name = blank;

    while (sec != NULL) {
	if (! strncmp(sec->name, name, 256))
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
	if (! strncmp(ent->name, name, 256))
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
    strncpy(ns->name, name, 256);
    list_add(&ns->list, &config_head);

    return(ns);
}


static entry_t *
create_entry(section_t *section, char *name)
{
    entry_t *ne = malloc(sizeof(entry_t));

    memset(ne, 0x00, sizeof(entry_t));
    strncpy(ne->name, name, 256);
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
    char sname[256], ename[256];
    wchar_t buff[1024];
    section_t *sec, *ns;
    int sd = 0, ed = 0;
    int c, data_pos;
    entry_t *ne;
    FILE *f;

#ifdef ANSI_CFG
    f = _wfopen(fn, L"rt");
#else
    f = _wfopen(fn, L"rt, ccs=UNICODE");
#endif
    if (f == NULL) return(0);
	
    sec = malloc(sizeof(section_t));
    memset(sec, 0x00, sizeof(section_t));
    memset(&config_head, 0x00, sizeof(list_t));
    list_add(&sec->list, &config_head);

    while (1) {
	memset(buff, 0x00, sizeof(buff));
	fgetws(buff, sizeof(buff)-1, f);
	if (feof(f)) break;

	c = 0;
	while (buff[c] == L' ')
		  c++;

	if (buff[c] == L'\0') continue;

	if ((buff[c] == L'#') || (buff[c] == L';')) {
		/*Comment*/
		continue;
	}

	if (buff[c] == L'[') {	/*Section*/
		sd = 0;
		c++;
		while (buff[c] != L']' && buff[c])
			wctomb(&(sname[sd++]), buff[c++]);

		if (buff[c] != L']') continue;

		sname[sd] = 0;

		ns = malloc(sizeof(section_t));
		memset(ns, 0x00, sizeof(section_t));
		strncpy(ns->name, sname, sizeof(ns->name));
		list_add(&ns->list, &config_head);
		
		sec = ns;			
	} else {
		ed = 0;
		while (buff[c] != L'=' && buff[c] != L' ' && buff[c])
				wctomb(&(ename[ed++]), buff[c++]);

		if (buff[c] == L'\0') continue;

		ename[ed] = 0;

		while ((buff[c] == L'=' || buff[c] == L' ') && buff[c])
			c++;

		if (! buff[c]) continue;

		data_pos = c;
		while (buff[c]) {
			if (buff[c] == L'\n')
				buff[c] = L'\0';
			c++;
		}

		ne = malloc(sizeof(entry_t));
		memset(ne, 0x00, sizeof(entry_t));
		strncpy(ne->name, ename, 256);
		memcpy(ne->wdata, &buff[data_pos], 512);
		ne->wdata[255] = L'\0';
		wcstombs(ne->data, ne->wdata, sizeof(ne->data));
		ne->data[255] = '\0';
		list_add(&ne->list, &sec->entry_head);
	}
    }

    (void)fclose(f);
	
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
    wchar_t wname[512];
    section_t *sec;
    FILE *f;
    int fl = 0;

#ifdef ANSI_CFG
    f = _wfopen(fn, L"wt");
#else
    f = _wfopen(fn, L"wt, ccs=UNICODE");
#endif
    if (f == NULL) return;

    sec = (section_t *)config_head.next;
    while (sec != NULL) {
	entry_t *ent;

	if (sec->name[0]) {
		mbstowcs(wname, sec->name, strlen(sec->name)+1);
		if (fl)
			fwprintf(f, L"\n[%ws]\n", wname);
		  else
			fwprintf(f, L"[%ws]\n", wname);
		fl++;
	}

	ent = (entry_t *)sec->entry_head.next;
	while (ent != NULL) {
		if (ent->name[0]) {
			mbstowcs(wname, ent->name, strlen(ent->name)+1);
			if (ent->wdata[0] == L'\0')
				fwprintf(f, L"%ws = \n", wname);
			  else
				fwprintf(f, L"%ws = %ws\n", wname, ent->wdata);
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
#ifdef ANSI_CFG
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
    p = config_get_string(cat, "vid_renderer", "d3d9");
    if (p != NULL)
	strcpy(temp, p);
    if (! strcmp(temp, "ddraw"))
	vid_api = 0;
      else
	vid_api = 1;
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

#ifdef USE_LANGUAGE
    /*
     * Currently, 86Box is English (US) only, but in the future
     * (version 1.30 at the earliest) other languages will be
     * added, therefore it is better to future-proof the code.
     */
    plat_language = config_get_hex16(cat, "language", 0x0409);
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

    p = config_get_string(cat, "gfxcard", NULL);
    if (p != NULL)
	gfxcard = video_get_video_from_internal_name(p);
      else
	gfxcard = 0;

    video_speed = config_get_int(cat, "video_speed", 3);

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
    p = config_get_string(cat, "opl3_type", "dbopl");
    strcpy(temp, p);
    if (!strcmp(temp, "nukedopl") || !strcmp(temp, "1"))
	opl3_type = 1;
      else
	opl3_type = 0;

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

    memset(network_pcap, '\0', sizeof(network_pcap));
    p = config_get_string(cat, "net_pcap_device", NULL);
    if (p != NULL) {
	if ((network_dev_to_id(p) == -1) || (network_ndev == 1)) {
		if ((network_ndev == 1) && strcmp(network_pcap, "none")) {
			ui_msgbox(MBX_ERROR, (wchar_t *)IDS_2140);
		} else if (network_dev_to_id(p) == -1) {
			ui_msgbox(MBX_ERROR, (wchar_t *)IDS_2141);
		}

		strcpy(network_pcap, "none");
	} else {
		strcpy(network_pcap, p);
	}
    } else
	strcpy(network_pcap, "none");

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
	strcpy(lpt1_device_name, p);
      else
	strcpy(lpt1_device_name, "none");
}


/* Load "Other Peripherals" section. */
static void
load_other_peripherals(void)
{
    char *cat = "Other peripherals";
    char temp[512], *p;
    int c;

    p = config_get_string(cat, "scsicard", NULL);
    if (p != NULL)
	scsi_card_current = scsi_card_get_from_internal_name(p);
      else
	scsi_card_current = 0;

    memset(hdc_name, '\0', sizeof(hdc_name));
    p = config_get_string(cat, "hdc", NULL);
    if (p == NULL) {
	p = config_get_string(cat, "hdd_controller", NULL);
	if (p != NULL)
		config_delete_var(cat, "hdd_controller");
    }
    if (p == NULL) {
	if (machines[machine].flags & MACHINE_HAS_HDC)
		strcpy(hdc_name, "internal");
	  else
		strcpy(hdc_name, "none");
    } else
	strcpy(hdc_name, p);
    config_set_string(cat, "hdc", hdc_name);

    memset(temp, '\0', sizeof(temp));
    for (c=2; c<4; c++) {
	sprintf(temp, "ide_%02i", c + 1);
	p = config_get_string(cat, temp, NULL);
	if (p == NULL)
		p = "0, 00";
	sscanf(p, "%i, %02i", &ide_enable[c], &ide_irq[c]);
    }

    bugger_enabled = !!config_get_int(cat, "bugger_enabled", 0);

    /* Remove these after a while. */
    serial_enabled[0] = !!config_get_int(cat, "serial1_enabled", 1);
    config_delete_var(cat, "serial1_enabled");
    serial_enabled[1] = !!config_get_int(cat, "serial2_enabled", 1);
    config_delete_var(cat, "serial2_enabled");
    lpt_enabled = !!config_get_int(cat, "lpt_enabled", 1);
    config_delete_var(cat, "lpt_enabled");
}


static int
tally_char(char *str, char c)
{
    int tally;

    tally = 0;
    if (str != NULL) {
	while (*str)
		if (*str++ == c) tally++;
    }

    return(tally);
}


/* Load "Hard Disks" section. */
static void
load_hard_disks(void)
{
    char *cat = "Hard disks";
    char temp[512];
    char temp2[512];
    char s[512];
    int c;
    char *p;
    wchar_t *wp;
    int max_spt, max_hpc, max_tracks;
    int board = 0, dev = 0;

    memset(temp, '\0', sizeof(temp));
    for (c=0; c<HDD_NUM; c++) {
	sprintf(temp, "hdd_%02i_parameters", c + 1);
	p = config_get_string(cat, temp, NULL);
	if (p == NULL)
		p = "0, 0, 0, 0, none";
	if (tally_char(p, ',') == 3) {
		sscanf(p, "%" PRIu64 ", %" PRIu64", %" PRIu64 ", %s",
			&hdd[c].spt, &hdd[c].hpc, &hdd[c].tracks, s);
		hdd[c].wp = 0;
	} else {
		sscanf(p, "%" PRIu64 ", %" PRIu64", %" PRIu64 ", %i, %s",
			&hdd[c].spt, &hdd[c].hpc, &hdd[c].tracks, (int *)&hdd[c].wp, s);
	}

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

	/* XT IDE */
	sprintf(temp, "hdd_%02i_xtide_channel", c+1);
	if (hdd[c].bus == HDD_BUS_XTIDE)
		hdd[c].xtide_channel = !!config_get_int(cat, temp, c & 1);
	  else
		config_delete_var(cat, temp);

	/* ESDI */
	sprintf(temp, "hdd_%02i_esdi_channel", c+1);
	if (hdd[c].bus == HDD_BUS_ESDI)
		hdd[c].esdi_channel = !!config_get_int(cat, temp, c & 1);
	  else
		config_delete_var(cat, temp);

	/* IDE */
	sprintf(temp, "hdd_%02i_ide_channel", c+1);
	if ((hdd[c].bus == HDD_BUS_IDE_PIO_ONLY) ||
	    (hdd[c].bus == HDD_BUS_IDE_PIO_AND_DMA)) {
		sprintf(temp2, "%01u:%01u", c>>1, c&1);
		p = config_get_string(cat, temp, temp2);
		if (! strstr(p, ":")) {
			sscanf(p, "%i", (int *)&hdd[c].ide_channel);
			hdd[c].ide_channel &= 7;
		} else {
			sscanf(p, "%01u:%01u", &board, &dev);

			board &= 3;
			dev &= 1;
			hdd[c].ide_channel = (board<<1) + dev;
		}

		if (hdd[c].ide_channel > 7)
			hdd[c].ide_channel = 7;
	} else {
		config_delete_var(cat, temp);
	}

	/* SCSI */
	sprintf(temp, "hdd_%02i_scsi_location", c + 1);
	if ((hdd[c].bus == HDD_BUS_SCSI) ||
	    (hdd[c].bus == HDD_BUS_SCSI_REMOVABLE)) {
		sprintf(temp, "%02u:%02u", c, 0);
		p = config_get_string(cat, temp, temp2);

		sscanf(p, "%02u:%02u",
			(int *)&hdd[c].scsi_id, (int *)&hdd[c].scsi_lun);

		if (hdd[c].scsi_id > 15)
			hdd[c].scsi_id = 15;
		if (hdd[c].scsi_lun > 7)
			hdd[c].scsi_lun = 7;
	} else {
		config_delete_var(cat, temp);
	}

	memset(hdd[c].fn, 0x00, sizeof(hdd[c].fn));
	memset(hdd[c].prev_fn, 0x00, sizeof(hdd[c].prev_fn));
	sprintf(temp, "hdd_%02i_fn", c + 1);
	wp = config_get_wstring(cat, temp, L"");

#if 0
	/*
	 * NOTE:
	 * Temporary hack to remove the absolute
	 * path currently saved in most config
	 * files.  We should remove this before
	 * finalizing this release!  --FvK
	 */
	if (! wcsnicmp(wp, cfg_path, wcslen(cfg_path))) {
		/*
		 * Yep, its absolute and prefixed
		 * with the CFG path.  Just strip
		 * that off for now...
		 */
		wcscpy((wchar_t *)hdd[c].fn, &wp[wcslen(cfg_path)]);
	} else
#endif
	memcpy(hdd[c].fn, wp, (wcslen(wp) << 1) + 2);

	/* If disk is empty or invalid, mark it for deletion. */
	if (! hdd_is_valid(c)) {
		sprintf(temp, "hdd_%02i_parameters", c+1);
		config_delete_var(cat, temp);

		sprintf(temp, "hdd_%02i_preide_channels", c+1);
		config_delete_var(cat, temp);

		sprintf(temp, "hdd_%02i_ide_channels", c+1);
		config_delete_var(cat, temp);

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


/* Load "Removable Devices" section. */
static void
load_removable_devices(void)
{
    char *cat = "Removable devices";
    char temp[512], temp2[512], *p;
    char s[512];
    unsigned int board = 0, dev = 0;
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
	if (! wcsnicmp(wp, cfg_path, wcslen(cfg_path))) {
		/*
		 * Yep, its absolute and prefixed
		 * with the EXE path.  Just strip
		 * that off for now...
		 */
		wcscpy((wchar_t *)floppyfns[c], &wp[wcslen(cfg_path)]);
	} else
#endif
	memcpy(floppyfns[c], wp, (wcslen(wp) << 1) + 2);

	if (*wp != L'\0')
		printf("Floppy%d: %ws\n", c, floppyfns[c]);
	sprintf(temp, "fdd_%02i_writeprot", c+1);
	ui_writeprot[c] = !!config_get_int(cat, temp, 0);
	sprintf(temp, "fdd_%02i_turbo", c + 1);
	fdd_set_turbo(c, !!config_get_int(cat, temp, 0));
	sprintf(temp, "fdd_%02i_check_bpb", c+1);
	fdd_set_check_bpb(c, !!config_get_int(cat, temp, 1));

	/* Check, whether each value is default, if yes, delete it so that only non-default values will later be saved. */
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

    memset(temp, 0x00, sizeof(temp));
    for (c=0; c<CDROM_NUM; c++) {
	sprintf(temp, "cdrom_%02i_host_drive", c+1);
	cdrom_drives[c].host_drive = config_get_int(cat, temp, 0);
	cdrom_drives[c].prev_host_drive = cdrom_drives[c].host_drive;

	sprintf(temp, "cdrom_%02i_parameters", c+1);
	p = config_get_string(cat, temp, NULL);
	if (p != NULL)
		sscanf(p, "%u, %s", &cdrom_drives[c].sound_on, s);
	  else
		sscanf("0, none", "%u, %s", &cdrom_drives[c].sound_on, s);
	cdrom_drives[c].bus_type = hdd_string_to_bus(s, 1);

	sprintf(temp, "cdrom_%02i_ide_channel", c+1);
	if ((cdrom_drives[c].bus_type == CDROM_BUS_ATAPI_PIO_ONLY) ||
	    (cdrom_drives[c].bus_type == CDROM_BUS_ATAPI_PIO_AND_DMA)) {
		sprintf(temp2, "%01u:%01u", (c+2)>>1, (c+2)&1);
		p = config_get_string(cat, temp, temp2);
		if (! strstr(p, ":")) {
			sscanf(p, "%i", (int *)&hdd[c].ide_channel);
			cdrom_drives[c].ide_channel &= 7;
		} else {
			sscanf(p, "%02u:%02u", &board, &dev);

			board &= 3;
			dev &= 1;
			cdrom_drives[c].ide_channel = (board<<1)+dev;
		}

		if (cdrom_drives[c].ide_channel > 7)
			cdrom_drives[c].ide_channel = 7;
		  else
			config_delete_var(cat, temp);
	} else {
		sprintf(temp, "cdrom_%02i_scsi_location", c+1);
		if (cdrom_drives[c].bus_type == CDROM_BUS_SCSI) {
			sprintf(temp2, "%02u:%02u", c+2, 0);
			p = config_get_string(cat, temp, temp2);
			sscanf(p, "%02u:%02u",
				&cdrom_drives[c].scsi_device_id,
				&cdrom_drives[c].scsi_device_lun);
	
			if (cdrom_drives[c].scsi_device_id > 15)
				cdrom_drives[c].scsi_device_id = 15;
			if (cdrom_drives[c].scsi_device_lun > 7)
				cdrom_drives[c].scsi_device_lun = 7;
		} else {
			config_delete_var(cat, temp);
		}
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
	if (! wcsnicmp(wp, cfg_path, wcslen(cfg_path))) {
		/*
		 * Yep, its absolute and prefixed
		 * with the EXE path.  Just strip
		 * that off for now...
		 */
		wcscpy(cdrom_image[c].image_path,&wp[wcslen(cfg_path)]);
	} else
#endif
	memcpy(cdrom_image[c].image_path, wp, (wcslen(wp) << 1) + 2);
	wcscpy(cdrom_image[c].prev_image_path, cdrom_image[c].image_path);

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

		sprintf(temp, "cdrom_%02i_scsi_location", c+1);
		config_delete_var(cat, temp);

		sprintf(temp, "cdrom_%02i_image_path", c+1);
		config_delete_var(cat, temp);
	}

	sprintf(temp, "cdrom_%02i_iso_path", c+1);
	config_delete_var(cat, temp);
    }
}


/* Load the specified or a default configuration file. */
void
config_load(wchar_t *fn)
{
    if (fn == NULL)
	fn = config_file_default;
    pclog("Loading config file '%ws'..\n", fn);

    if (! config_read(fn)) {
	cpu = 0;
#ifdef USE_LANGUAGE
	plat_language = 0x0409;
#endif
	scale = 1;
	vid_api = 1;
	enable_sync = 1;
	joystick_type = 7;
	strcpy(hdc_name, "none");
	serial_enabled[0] = 0;
	serial_enabled[1] = 0;
	lpt_enabled = 0;
	fdd_set_type(0, 2);
	fdd_set_type(1, 2);
	mem_size = 640;

	pclog("Config file not present or invalid!\n");
	return;
    }

    load_general();		/* General */
    load_machine();		/* Machine */
    load_video();		/* Video */
    load_input_devices();	/* Input devices */
    load_sound();		/* Sound */
    load_network();		/* Network */
    load_ports();		/* Ports (COM & LPT) */
    load_other_peripherals();	/* Other peripherals */
    load_hard_disks();		/* Hard disks */
    load_removable_devices();	/* Removable devices */

    pclog("Config loaded.\n");
}


/* Save "General" section. */
static void
save_general(void)
{
    char *cat = "General";
    char temp[512];

    config_set_int(cat, "vid_resize", vid_resize);
    if (vid_resize == 0)
	config_delete_var(cat, "vid_resize");

    if (vid_api == 1) {
	config_delete_var(cat, "vid_renderer");
    } else switch(vid_api) {
	case 0:
		config_set_string(cat, "vid_renderer", "ddraw");
		break;

	case 1:
	default:
		config_set_string(cat, "vid_renderer", "d3d9");
		break;
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

    if (window_remember) {
	config_set_int(cat, "window_remember", window_remember);

	sprintf(temp, "%i, %i, %i, %i", window_w, window_h, window_x, window_y);
	config_set_string(cat, "window_coordinates", temp);
    } else {
	config_delete_var(cat, "window_remember");
	config_delete_var(cat, "window_coordinates");
    }

#ifdef USE_LANGUAGE
    if (plat_language == 0x0409)
	config_delete_var(cat, "language");
      else
	config_set_hex16(cat, "language", plat_language);
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

    if (video_speed == 3)
	config_delete_var(cat, "video_speed");
      else
	config_set_int(cat, "video_speed", video_speed);

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
    char temp[512], temp2[512];
    int c, d;

    config_set_string(cat, "mouse_type", mouse_get_internal_name(mouse_type));

    if ((joystick_type == 0) || (joystick_type == 7)) {
	if (joystick_type == 7)
		config_delete_var(cat, "joystick_type");
	  else
		config_set_int(cat, "joystick_type", joystick_type);

	for (c=0; c<16; c++) {
		sprintf(temp2, "joystick_%i_nr", c);
		config_delete_var(cat, temp2);

		for (d=0; d<16; d++) {			
			sprintf(temp2, "joystick_%i_axis_%i", c, d);
			config_delete_var(cat, temp2);
		}
		for (d=0; d<16; d++) {			
			sprintf(temp2, "joystick_%i_button_%i", c, d);
			config_delete_var(cat, temp2);
		}
		for (d=0; d<16; d++) {			
			sprintf(temp2, "joystick_%i_pov_%i", c, d);
			config_delete_var(cat, temp2);
		}
	}
    } else {
	config_set_int(cat, "joystick_type", joystick_type);

	for (c=0; c<joystick_get_max_joysticks(joystick_type); c++) {
		sprintf(temp2, "joystick_%i_nr", c);
		config_set_int(cat, temp2, joystick_state[c].plat_joystick_nr);

		if (joystick_state[c].plat_joystick_nr) {
			for (d=0; d<joystick_get_axis_count(joystick_type); d++) {			
				sprintf(temp2, "joystick_%i_axis_%i", c, d);
				config_set_int(cat, temp2, joystick_state[c].axis_mapping[d]);
			}
			for (d=0; d<joystick_get_button_count(joystick_type); d++) {			
				sprintf(temp2, "joystick_%i_button_%i", c, d);
				config_set_int(cat, temp2, joystick_state[c].button_mapping[d]);
			}
			for (d=0; d<joystick_get_pov_count(joystick_type); d++) {			
				sprintf(temp2, "joystick_%i_pov_%i", c, d);
				sprintf(temp, "%i, %i", joystick_state[c].pov_mapping[d][0], joystick_state[c].pov_mapping[d][1]);
				config_set_string(cat, temp2, temp);
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

    if (opl3_type == 0)
	config_delete_var(cat, "opl3_type");
      else
	config_set_string(cat, "opl3_type", (opl3_type == 1) ? "nukedopl" : "dbopl");

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

    if (network_pcap[0] != '\0') {
	if (! strcmp(network_pcap, "none"))
		config_delete_var(cat, "net_pcap_device");
	else
		config_set_string(cat, "net_pcap_device", network_pcap);
    } else {
	/* config_set_string(cat, "net_pcap_device", "none"); */
	config_delete_var(cat, "net_pcap_device");
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

    if (!strcmp(lpt1_device_name, "none"))
	config_delete_var(cat, "lpt1_device");
      else
	config_set_string(cat, "lpt1_device", lpt1_device_name);

    delete_section_if_empty(cat);
}


/* Save "Other Peripherals" section. */
static void
save_other_peripherals(void)
{
    char *cat = "Other peripherals";
    char temp[512], temp2[512];
    int c;

    if (scsi_card_current == 0)
	config_delete_var(cat, "scsicard");
      else
	config_set_string(cat, "scsicard",
			  scsi_card_get_internal_name(scsi_card_current));

    config_set_string(cat, "hdc", hdc_name);

    memset(temp, '\0', sizeof(temp));
    for (c=2; c<4; c++) {
	sprintf(temp, "ide_%02i", c + 1);
	sprintf(temp2, "%i, %02i", !!ide_enable[c], ide_irq[c]);
	if (ide_enable[c] == 0)
		config_delete_var(cat, temp);
	  else
		config_set_string(cat, temp, temp2);
    }

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
    char temp[24], temp2[64];
    char *p;
    int c;

    memset(temp, 0x00, sizeof(temp));
    for (c=0; c<HDD_NUM; c++) {
	sprintf(temp, "hdd_%02i_parameters", c+1);
	if (hdd_is_valid(c)) {
		p = hdd_bus_to_string(hdd[c].bus, 0);
		sprintf(temp2, "%" PRIu64 ", %" PRIu64", %" PRIu64 ", %i, %s",
			hdd[c].spt, hdd[c].hpc, hdd[c].tracks, hdd[c].wp, p);
		config_set_string(cat, temp, temp2);
	} else {
		config_delete_var(cat, temp);
	}

	sprintf(temp, "hdd_%02i_mfm_channel", c+1);
	if (hdd_is_valid(c) && (hdd[c].bus == HDD_BUS_MFM))
		config_set_int(cat, temp, hdd[c].mfm_channel);
	  else
		config_delete_var(cat, temp);

	sprintf(temp, "hdd_%02i_xtide_channel", c+1);
	if (hdd_is_valid(c) && (hdd[c].bus == HDD_BUS_XTIDE))
		config_set_int(cat, temp, hdd[c].xtide_channel);
	  else
		config_delete_var(cat, temp);

	sprintf(temp, "hdd_%02i_esdi_channel", c+1);
	if (hdd_is_valid(c) && (hdd[c].bus == HDD_BUS_ESDI))
		config_set_int(cat, temp, hdd[c].esdi_channel);
	  else
		config_delete_var(cat, temp);

	sprintf(temp, "hdd_%02i_ide_channel", c+1);
	if (! hdd_is_valid(c) || ((hdd[c].bus != HDD_BUS_IDE_PIO_ONLY) && (hdd[c].bus != HDD_BUS_IDE_PIO_AND_DMA))) {
		config_delete_var(cat, temp);
	} else {
		sprintf(temp2, "%01u:%01u", hdd[c].ide_channel >> 1, hdd[c].ide_channel & 1);
		config_set_string(cat, temp, temp2);
	}

	sprintf(temp, "hdd_%02i_scsi_location", c+1);
	if (! hdd_is_valid(c) || ((hdd[c].bus != HDD_BUS_SCSI) && (hdd[c].bus != HDD_BUS_SCSI_REMOVABLE))) {
		config_delete_var(cat, temp);
	} else {
		sprintf(temp2, "%02u:%02u", hdd[c].scsi_id, hdd[c].scsi_lun);
		config_set_string(cat, temp, temp2);
	}

	sprintf(temp, "hdd_%02i_fn", c+1);
	if (hdd_is_valid(c) && (wcslen(hdd[c].fn) != 0))
		config_set_wstring(cat, temp, hdd[c].fn);
	  else
		config_delete_var(cat, temp);
    }

    delete_section_if_empty(cat);
}


/* Save "Removable Devices" section. */
static void
save_removable_devices(void)
{
    char *cat = "Removable devices";
    char temp[512], temp2[512];
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

    for (c=0; c<CDROM_NUM; c++) {
	sprintf(temp, "cdrom_%02i_host_drive", c+1);
	if ((cdrom_drives[c].bus_type == 0) ||
	    (cdrom_drives[c].host_drive < 'A') || ((cdrom_drives[c].host_drive > 'Z') && (cdrom_drives[c].host_drive != 200))) {
		config_delete_var(cat, temp);
	} else {
		config_set_int(cat, temp, cdrom_drives[c].host_drive);
	}

	sprintf(temp, "cdrom_%02i_parameters", c+1);
	if (cdrom_drives[c].bus_type == 0) {
		config_delete_var(cat, temp);
	} else {
		sprintf(temp2, "%u, %s", cdrom_drives[c].sound_on,
			hdd_bus_to_string(cdrom_drives[c].bus_type, 1));
		config_set_string(cat, temp, temp2);
	}
		
	sprintf(temp, "cdrom_%02i_ide_channel", c+1);
	if ((cdrom_drives[c].bus_type != CDROM_BUS_ATAPI_PIO_ONLY) &&
	    (cdrom_drives[c].bus_type != CDROM_BUS_ATAPI_PIO_AND_DMA)) {
		config_delete_var(cat, temp);
	} else {
		sprintf(temp2, "%01u:%01u", cdrom_drives[c].ide_channel>>1,
					cdrom_drives[c].ide_channel & 1);
		config_set_string(cat, temp, temp2);
	}

	sprintf(temp, "cdrom_%02i_scsi_location", c + 1);
	if (cdrom_drives[c].bus_type != CDROM_BUS_SCSI) {
		config_delete_var(cat, temp);
	} else {
		sprintf(temp2, "%02u:%02u", cdrom_drives[c].scsi_device_id,
					cdrom_drives[c].scsi_device_lun);
		config_set_string(cat, temp, temp2);
	}

	sprintf(temp, "cdrom_%02i_image_path", c + 1);
	if ((cdrom_drives[c].bus_type == 0) ||
	    (wcslen(cdrom_image[c].image_path) == 0)) {
		config_delete_var(cat, temp);
	} else {
		config_set_wstring(cat, temp, cdrom_image[c].image_path);
	}
    }

    delete_section_if_empty(cat);
}


void
config_save(void)
{
    save_general();		/* General */
    save_machine();		/* Machine */
    save_video();		/* Video */
    save_input_devices();	/* Input devices */
    save_sound();		/* Sound */
    save_network();		/* Network */
    save_ports();		/* Ports (COM & LPT) */
    save_other_peripherals();	/* Other peripherals */
    save_hard_disks();		/* Hard disks */
    save_removable_devices();	/* Removable devices */

    config_write(config_file_default);
}


void
config_dump(void)
{
    section_t *sec;
	
    sec = (section_t *)config_head.next;
    while (sec != NULL) {
	entry_t *ent;

	if (sec->name && sec->name[0])
		pclog("[%s]\n", sec->name);
	
	ent = (entry_t *)sec->entry_head.next;
	while (ent != NULL) {
		pclog("%s = %s\n", ent->name, ent->data);

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
    int value;

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
    int value;

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
    int val0 = 0, val1 = 0, val2 = 0;

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
    entry_t *entry;

    section = find_section(head);
    if (section == NULL)
	section = create_section(head);

    entry = find_entry(section, name);
    if (entry == NULL)
	entry = create_entry(section, name);

    sprintf(entry->data, "%i", val);
    mbstowcs(entry->wdata, entry->data, 512);
}


void
config_set_hex16(char *head, char *name, int val)
{
    section_t *section;
    entry_t *entry;

    section = find_section(head);
    if (section == NULL)
	section = create_section(head);

    entry = find_entry(section, name);
    if (entry == NULL)
	entry = create_entry(section, name);

    sprintf(entry->data, "%04X", val);
    mbstowcs(entry->wdata, entry->data, 512);
}


void
config_set_hex20(char *head, char *name, int val)
{
    section_t *section;
    entry_t *entry;

    section = find_section(head);
    if (section == NULL)
	section = create_section(head);

    entry = find_entry(section, name);
    if (entry == NULL)
	entry = create_entry(section, name);

    sprintf(entry->data, "%05X", val);
    mbstowcs(entry->wdata, entry->data, 512);
}


void
config_set_mac(char *head, char *name, int val)
{
    section_t *section;
    entry_t *entry;

    section = find_section(head);
    if (section == NULL)
	section = create_section(head);

    entry = find_entry(section, name);
    if (entry == NULL)
	entry = create_entry(section, name);

    sprintf(entry->data, "%02x:%02x:%02x",
		(val>>16)&0xff, (val>>8)&0xff, val&0xff);
    mbstowcs(entry->wdata, entry->data, 512);
}


void
config_set_string(char *head, char *name, char *val)
{
    section_t *section;
    entry_t *entry;

    section = find_section(head);
    if (section == NULL)
	section = create_section(head);

    entry = find_entry(section, name);
    if (entry == NULL)
	entry = create_entry(section, name);

    strncpy(entry->data, val, 256);
    mbstowcs(entry->wdata, entry->data, 256);
}


void
config_set_wstring(char *head, char *name, wchar_t *val)
{
    section_t *section;
    entry_t *entry;

    section = find_section(head);
    if (section == NULL)
	section = create_section(head);

    entry = find_entry(section, name);
    if (entry == NULL)
	entry = create_entry(section, name);

    memcpy(entry->wdata, val, 512);
    wcstombs(entry->data, entry->wdata, 256);
}


/* FIXME: should be moved elsewhere. --FvK */
char *
get_filename(char *s)
{
    int c = strlen(s) - 1;

    while (c > 0) {
	if (s[c] == '/' || s[c] == '\\')
	   return(&s[c+1]);
       c--;
    }

    return(s);
}


/* FIXME: should be moved elsewhere. --FvK */
wchar_t *
get_filename_w(wchar_t *s)
{
    int c = wcslen(s) - 1;

    while (c > 0) {
	if (s[c] == L'/' || s[c] == L'\\')
	   return(&s[c+1]);
       c--;
    }

    return(s);
}


/* FIXME: should be moved elsewhere. --FvK */
void
append_filename(char *dest, char *s1, char *s2, int size)
{
    sprintf(dest, "%s%s", s1, s2);
}


/* FIXME: should be moved elsewhere. --FvK */
void
append_filename_w(wchar_t *dest, wchar_t *s1, wchar_t *s2, int size)
{
    _swprintf(dest, L"%s%s", s1, s2);
}


/* FIXME: should be moved elsewhere. --FvK */
void
put_backslash(char *s)
{
    int c = strlen(s) - 1;

    if (s[c] != '/' && s[c] != '\\')
	   s[c] = '/';
}


/* FIXME: should be moved elsewhere. --FvK */
void
put_backslash_w(wchar_t *s)
{
    int c = wcslen(s) - 1;

    if (s[c] != L'/' && s[c] != L'\\')
	   s[c] = L'/';
}


/* FIXME: should be moved elsewhere. --FvK */
char *
get_extension(char *s)
{
    int c = strlen(s) - 1;

    if (c <= 0)
	return(s);

    while (c && s[c] != '.')
		c--;
		
    if (!c)
	return(&s[strlen(s)]);

    return(&s[c+1]);
}	       


/* FIXME: should be moved elsewhere. --FvK */
wchar_t
*get_extension_w(wchar_t *s)
{
    int c = wcslen(s) - 1;

    if (c <= 0)
	return(s);
	
    while (c && s[c] != L'.')
		c--;
		
    if (!c)
	return(&s[wcslen(s)]);

    return(&s[c+1]);
}	       
