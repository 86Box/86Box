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
 *
 *
 * Authors:	Sarah Walker,
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Overdoze,
 *		David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2017-2019 Fred N. van Kempen.
 *		Copyright 2018,2019 David Hrdlička.
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
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/cassette.h>
#include <86box/cartridge.h>
#include <86box/nvr.h>
#include <86box/config.h>
#include <86box/isamem.h>
#include <86box/isartc.h>
#include <86box/lpt.h>
#include <86box/hdd.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/gameport.h>
#include <86box/machine.h>
#include <86box/mouse.h>
#include <86box/network.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/cdrom.h>
#include <86box/zip.h>
#include <86box/mo.h>
#include <86box/sound.h>
#include <86box/midi.h>
#include <86box/snd_mpu401.h>
#include <86box/video.h>
#include <86box/plat.h>
#include <86box/plat_midi.h>
#include <86box/plat_dir.h>
#include <86box/ui.h>


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
    char	data[512];
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
    if ((next) == (head))		\
	(head)->next = (old)->next;	\
}


static list_t	config_head;

/* TODO: Backwards compatibility, get rid of this when enough time has passed. */
static int	backwards_compat = 0;
static int	backwards_compat2 = 0;


#ifdef ENABLE_CONFIG_LOG
int config_do_log = ENABLE_CONFIG_LOG;


static void
config_log(const char *fmt, ...)
{
    va_list ap;

    if (config_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define config_log(fmt, ...)
#endif


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


void *
config_find_section(char *name)
{
    return (void *) find_section(name);
}


void
config_rename_section(void *priv, char *name)
{
    section_t *sec = (section_t *) priv;

    memset(sec->name, 0x00, sizeof(sec->name));
    memcpy(sec->name, name, MIN(128, strlen(name) + 1));
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
    memcpy(ns->name, name, strlen(name) + 1);
    list_add(&ns->list, &config_head);

    return(ns);
}


static entry_t *
create_entry(section_t *section, char *name)
{
    entry_t *ne = malloc(sizeof(entry_t));

    memset(ne, 0x00, sizeof(entry_t));
    memcpy(ne->name, name, strlen(name) + 1);
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

static int
config_detect_bom(char *fn)
{
	FILE *f;
	unsigned char bom[4] = { 0, 0, 0, 0 };

#if defined(ANSI_CFG) || !defined(_WIN32)
    f = plat_fopen(fn, "rt");
#else
    f = plat_fopen(fn, "rt, ccs=UTF-8");
#endif
    if (f == NULL) return(0);
	fread(bom, 1, 3, f);
	if (bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF)
	{
		fclose(f);
		return 1;
	}
	fclose(f);
	return 0;
}

/* Read and parse the configuration file into memory. */
static int
config_read(char *fn)
{
    char sname[128], ename[128];
    wchar_t buff[1024];
    section_t *sec, *ns;
    entry_t *ne;
    int c, d, bom;
    FILE *f;

	bom = config_detect_bom(fn);
#if defined(ANSI_CFG) || !defined(_WIN32)
    f = plat_fopen(fn, "rt");
#else
    f = plat_fopen(fn, "rt, ccs=UTF-8");
#endif
    if (f == NULL) return(0);
	
    sec = malloc(sizeof(section_t));
    memset(sec, 0x00, sizeof(section_t));
    memset(&config_head, 0x00, sizeof(list_t));
    list_add(&sec->list, &config_head);
	if (bom)
		fseek(f, 3, SEEK_SET);

    while (1) {
	memset(buff, 0x00, sizeof(buff));
	fgetws(buff, sizeof_w(buff), f);
	if (feof(f)) break;

	/* Make sure there are no stray newlines or hard-returns in there. */
	if (wcslen(buff) > 0)
		if (buff[wcslen(buff)-1] == L'\n') buff[wcslen(buff)-1] = L'\0';
	if (wcslen(buff) > 0)
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
		memcpy(ns->name, sname, 128);
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
	memcpy(ne->name, ename, 128);
	wcsncpy(ne->wdata, &buff[d], sizeof_w(ne->wdata)-1);
	ne->wdata[sizeof_w(ne->wdata)-1] = L'\0';
#ifdef _WIN32	/* Make sure the string is converted to UTF-8 rather than a legacy codepage */
	c16stombs(ne->data, ne->wdata, sizeof(ne->data));
#else
	wcstombs(ne->data, ne->wdata, sizeof(ne->data));
#endif
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
config_write(char *fn)
{
    wchar_t wtemp[512];
    section_t *sec;
    FILE *f;
    int fl = 0;

#if defined(ANSI_CFG) || !defined(_WIN32)
    f = plat_fopen(fn, "wt");
#else
    f = plat_fopen(fn, "wt, ccs=UTF-8");
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
			mbstowcs(wtemp, ent->name, 128);
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
    FILE *f = _wfopen(config_file, L"wt, ccs=UTF-8");
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

    vid_resize = config_get_int(cat, "vid_resize", 0);
    if (vid_resize & ~3)
	vid_resize &= 3;

    memset(temp, '\0', sizeof(temp));
    p = config_get_string(cat, "vid_renderer", "default");
    vid_api = plat_vidapi(p);
    config_delete_var(cat, "vid_api");

    video_fullscreen_scale = config_get_int(cat, "video_fullscreen_scale", 0);

    video_fullscreen_first = config_get_int(cat, "video_fullscreen_first", 1);

    video_filter_method = config_get_int(cat, "video_filter_method", 1);

    force_43 = !!config_get_int(cat, "force_43", 0);
    scale = config_get_int(cat, "scale", 1);
    if (scale > 3)
        scale = 3;
    dpi_scale = config_get_int(cat, "dpi_scale", 1);

    enable_overscan = !!config_get_int(cat, "enable_overscan", 0);
    vid_cga_contrast = !!config_get_int(cat, "vid_cga_contrast", 0);
    video_grayscale = config_get_int(cat, "video_grayscale", 0);
    video_graytype = config_get_int(cat, "video_graytype", 0);

    rctrl_is_lalt = config_get_int(cat, "rctrl_is_lalt", 0);
    update_icons = config_get_int(cat, "update_icons", 1);

    window_remember = config_get_int(cat, "window_remember", 0);
    if (window_remember || (vid_resize & 2)) {
	if (!window_remember)
		config_delete_var(cat, "window_remember");

	p = config_get_string(cat, "window_coordinates", NULL);
	if (p == NULL)
		p = "0, 0, 0, 0";
	sscanf(p, "%i, %i, %i, %i", &window_w, &window_h, &window_x, &window_y);
    } else {
	config_delete_var(cat, "window_remember");
	config_delete_var(cat, "window_coordinates");

	window_w = window_h = window_x = window_y = 0;
    }

    if (vid_resize & 2) {
	p = config_get_string(cat, "window_fixed_res", NULL);
	if (p == NULL)
		p = "120x120";
	sscanf(p, "%ix%i", &fixed_size_x, &fixed_size_y);
	if (fixed_size_x < 120)
		fixed_size_x = 120;
	if (fixed_size_x > 2048)
		fixed_size_x = 2048;
	if (fixed_size_y < 120)
		fixed_size_y = 120;
	if (fixed_size_y > 2048)
		fixed_size_y = 2048;
    } else {
	config_delete_var(cat, "window_fixed_res");

	fixed_size_x = fixed_size_y = 120;
    }

    sound_gain = config_get_int(cat, "sound_gain", 0);

    kbd_req_capture = config_get_int(cat, "kbd_req_capture", 0);
    hide_status_bar = config_get_int(cat, "hide_status_bar", 0);

    confirm_reset = config_get_int(cat, "confirm_reset", 1);
    confirm_exit = config_get_int(cat, "confirm_exit", 1);
    confirm_save = config_get_int(cat, "confirm_save", 1);

#ifdef USE_LANGUAGE
    /*
     * Currently, 86Box is English (US) only, but in the future
     * (version 3.0 at the earliest) other languages will be
     * added, therefore it is better to future-proof the code.
     */
    plat_langid = config_get_hex16(cat, "language", 0x0409);
#endif

#if USE_DISCORD
    enable_discord = !!config_get_int(cat, "enable_discord", 0);
#endif

#if defined(DEV_BRANCH) && defined(USE_OPENGL)
    video_framerate = config_get_int(cat, "video_gl_framerate", -1);
    video_vsync = config_get_int(cat, "video_gl_vsync", 0);
    strcpy_s(video_shader, sizeof(video_shader), config_get_string(cat, "video_gl_shader", ""));
#endif
}


/* Load "Machine" section. */
static void
load_machine(void)
{
    char *cat = "Machine";
    char *p, *migrate_from = NULL;
    int c, i, j, speed, legacy_mfg, legacy_cpu;
    double multi;

    p = config_get_string(cat, "machine", NULL);
    if (p != NULL) {
    	migrate_from = p;
	if (! strcmp(p, "8500ttc")) /* migrate typo... */
		machine = machine_get_machine_from_internal_name("8600ttc");
	else if (! strcmp(p, "eagle_pcspirit")) /* ...legacy names... */
		machine = machine_get_machine_from_internal_name("pcspirit");
	else if (! strcmp(p, "multitech_pc700"))
		machine = machine_get_machine_from_internal_name("pc700");
	else if (! strcmp(p, "ncr_pc4i"))
		machine = machine_get_machine_from_internal_name("pc4i");
	else if (! strcmp(p, "olivetti_m19"))
		machine = machine_get_machine_from_internal_name("m19");
	else if (! strcmp(p, "open_xt"))
		machine = machine_get_machine_from_internal_name("openxt");
	else if (! strcmp(p, "open_at"))
		machine = machine_get_machine_from_internal_name("openat");
	else if (! strcmp(p, "philips_p3105"))
		machine = machine_get_machine_from_internal_name("p3105");
	else if (! strcmp(p, "philips_p3120"))
		machine = machine_get_machine_from_internal_name("p3120");
	else if (! strcmp(p, "olivetti_m24"))
		machine = machine_get_machine_from_internal_name("m24");
	else if (! strcmp(p, "olivetti_m240"))
		machine = machine_get_machine_from_internal_name("m240");
	else if (! strcmp(p, "ncr_pc8"))
		machine = machine_get_machine_from_internal_name("pc8");
	else if (! strcmp(p, "olivetti_m290"))
		machine = machine_get_machine_from_internal_name("m290");
	else if (! strcmp(p, "ncr_3302"))
		machine = machine_get_machine_from_internal_name("3302");
	else if (! strcmp(p, "ncr_pc916sx"))
		machine = machine_get_machine_from_internal_name("pc916sx");
	else if (! strcmp(p, "cbm_sl386sx16"))
		machine = machine_get_machine_from_internal_name("cmdsl386sx16");
	else if (! strcmp(p, "olivetti_m300_08"))
		machine = machine_get_machine_from_internal_name("m30008");
	else if (! strcmp(p, "olivetti_m300_15"))
		machine = machine_get_machine_from_internal_name("m30015");
	else if (! strcmp(p, "cbm_sl386sx25"))
		machine = machine_get_machine_from_internal_name("cmdsl386sx25");
	else if (! strcmp(p, "award386dx")) /* ...merged machines... */
		machine = machine_get_machine_from_internal_name("award486");
	else if (! strcmp(p, "ami386dx"))
		machine = machine_get_machine_from_internal_name("ami486");
	else if (! strcmp(p, "mr386dx"))
		machine = machine_get_machine_from_internal_name("mr486");
	else if (! strcmp(p, "fw6400gx_s1"))
		machine = machine_get_machine_from_internal_name("fw6400gx");
	else if (! strcmp(p, "president")) { /* ...and removed machines */
		machine = machine_get_machine_from_internal_name("mb500n");
		migrate_from = NULL;
	} else if (! strcmp(p, "j656vxd")) {
		machine = machine_get_machine_from_internal_name("p55va");
		migrate_from = NULL;
	} else {
		machine = machine_get_machine_from_internal_name(p);
		migrate_from = NULL;
	}
    } else 
	machine = 0;

    /* This is for backwards compatibility. */
    p = config_get_string(cat, "model", NULL);
    if (p != NULL) {
    	migrate_from = p;
	if (! strcmp(p, "p55r2p4")) /* migrate typo */
		machine = machine_get_machine_from_internal_name("p55t2p4");
	else {
		machine = machine_get_machine_from_internal_name(p);
		migrate_from = NULL;
	}
	config_delete_var(cat, "model");
    }
    if (machine >= machine_count())
	machine = machine_count() - 1;

    /* Copy NVR files when migrating a machine to a new internal name. */
    if (migrate_from) {
    	char old_fn[256];
	strcpy(old_fn, migrate_from);
	strcat(old_fn, ".");
	c = strlen(old_fn);
	char new_fn[256];
	strcpy(new_fn, machines[machine].internal_name);
	strcat(new_fn, ".");
	i = strlen(new_fn);

	/* Iterate through NVR files. */
	DIR *dirp = opendir(nvr_path("."));
	if (dirp) {
		struct dirent *entry;
		while ((entry = readdir(dirp))) {
			/* Check if this file corresponds to the old name. */
			if (strncmp(entry->d_name, old_fn, c))
				continue;

			/* Add extension to the new name. */
			strcpy(&new_fn[i], &entry->d_name[c]);

			/* Only copy if a file with the new name doesn't already exist. */
			FILE *g = nvr_fopen(new_fn, "rb");
			if (!g) {
				FILE *f = nvr_fopen(entry->d_name, "rb");
				g = nvr_fopen(new_fn, "wb");

				uint8_t buf[4096];
				while ((j = fread(buf, 1, sizeof(buf), f)))
					fwrite(buf, 1, j, g);

				fclose(f);
			}
			fclose(g);
		}
	}
    }

    cpu_override = config_get_int(cat, "cpu_override", 0);
    cpu_f = NULL;
    p = config_get_string(cat, "cpu_family", NULL);
    if (p) {
	if (! strcmp(p, "enh_am486dx2")) /* migrate modified names */
		cpu_f = cpu_get_family("am486dx2_slenh");
	else if (! strcmp(p, "enh_am486dx4"))
		cpu_f = cpu_get_family("am486dx4_slenh");
	else
		cpu_f = cpu_get_family(p);

	if (cpu_f && !cpu_family_is_eligible(cpu_f, machine)) /* only honor eligible families */
		cpu_f = NULL;
    } else {
	/* Backwards compatibility with the previous CPU model system. */
	legacy_mfg = config_get_int(cat, "cpu_manufacturer", 0);
	legacy_cpu = config_get_int(cat, "cpu", 0);

	/* Check if either legacy ID is present, and if they are within bounds. */
	if (((legacy_mfg > 0) || (legacy_cpu > 0)) && (legacy_mfg >= 0) && (legacy_mfg < 4) && (legacy_cpu >= 0)) {
		/* Look for a machine entry on the legacy table. */
		p = machine_get_internal_name();
		c = 0;
		while (cpu_legacy_table[c].machine) {
			if (!strcmp(p, cpu_legacy_table[c].machine))
				break;
			c++;
		}
		if (cpu_legacy_table[c].machine) {
			/* Determine the amount of CPU entries on the table. */
			i = -1;
			while (cpu_legacy_table[c].tables[legacy_mfg][++i].family);

			/* If the CPU ID is out of bounds, reset to the last known ID. */
			if (legacy_cpu >= i)
				legacy_cpu = i - 1;

			const cpu_legacy_table_t *legacy_table_entry = &cpu_legacy_table[c].tables[legacy_mfg][legacy_cpu];

			/* Check if the referenced family exists. */
			cpu_f = cpu_get_family(legacy_table_entry->family);
			if (cpu_f) {
				/* Save the new values. */
				config_set_string(cat, "cpu_family", (char *) legacy_table_entry->family);
				config_set_int(cat, "cpu_speed", legacy_table_entry->rspeed);
				config_set_double(cat, "cpu_multi", legacy_table_entry->multi);
			}
		}
	}
    }

    if (cpu_f) {
	speed = config_get_int(cat, "cpu_speed", 0);
	multi = config_get_double(cat, "cpu_multi", 0);

	/* Find the configured CPU. */
	cpu = 0;
	c = 0;
	i = 256;
	while (cpu_f->cpus[cpu].cpu_type) {
		if (cpu_is_eligible(cpu_f, cpu, machine)) { /* skip ineligible CPUs */
			if ((cpu_f->cpus[cpu].rspeed == speed) && (cpu_f->cpus[cpu].multi == multi)) /* exact speed/multiplier match */
				break;
			else if ((cpu_f->cpus[cpu].rspeed >= speed) && (i == 256)) /* closest speed match */
				i = cpu;
			c = cpu; /* store fastest eligible CPU */
		}
		cpu++;
	}
	if (!cpu_f->cpus[cpu].cpu_type) /* if no exact match was found, use closest matching faster CPU, or fastest eligible CPU */
		cpu = MIN(i, c);
    } else { /* default */
	/* Find first eligible family. */
	c = 0;
	while (!cpu_family_is_eligible(&cpu_families[c], machine)) {
		if (cpu_families[c++].package == 0) { /* end of list */
			fatal("No eligible CPU families for the selected machine\n");
			return;
		}
	}
	cpu_f = (cpu_family_t *) &cpu_families[c];

	/* Find first eligible CPU in that family. */
	cpu = 0;
	while (!cpu_is_eligible(cpu_f, cpu, machine)) {
		if (cpu_f->cpus[cpu++].cpu_type == 0) { /* end of list */
			cpu = 0;
			break;
		}
	}
    }
    cpu_s = (CPU *) &cpu_f->cpus[cpu];

    cpu_waitstates = config_get_int(cat, "cpu_waitstates", 0);

    p = (char *)config_get_string(cat, "fpu_type", "none");
    fpu_type = fpu_get_type(cpu_f, cpu, p);

    mem_size = config_get_int(cat, "mem_size", 4096);
	
#if 0
    if (mem_size < (((machines[machine].flags & MACHINE_AT) &&
        (machines[machine].ram_granularity < 128)) ? machines[machine].min_ram*1024 : machines[machine].min_ram))
	mem_size = (((machines[machine].flags & MACHINE_AT) && (machines[machine].ram_granularity < 128)) ? machines[machine].min_ram*1024 : machines[machine].min_ram);
#endif
	
    if (mem_size > 2097152)
	mem_size = 2097152;

    cpu_use_dynarec = !!config_get_int(cat, "cpu_use_dynarec", 0);

    p = config_get_string(cat, "time_sync", NULL);
    if (p != NULL) {        
	if (!strcmp(p, "disabled"))
		time_sync = TIME_SYNC_DISABLED;
	else
	if (!strcmp(p, "local"))
		time_sync = TIME_SYNC_ENABLED;
	else
	if (!strcmp(p, "utc") || !strcmp(p, "gmt"))
		time_sync = TIME_SYNC_ENABLED | TIME_SYNC_UTC;
	else
		time_sync = TIME_SYNC_ENABLED;
    } else
	time_sync = !!config_get_int(cat, "enable_sync", 1);

    /* Remove this after a while.. */
    config_delete_var(cat, "nvr_path");
    config_delete_var(cat, "enable_sync");

    /* Set up the architecture flags. */
    AT = IS_AT(machine);
    PCI = IS_ARCH(machine, MACHINE_BUS_PCI);
}


/* Load "Video" section. */
static void
load_video(void)
{
    char *cat = "Video";
    char *p;
    int free_p = 0;

    if (machines[machine].flags & MACHINE_VIDEO_ONLY) {
	config_delete_var(cat, "gfxcard");
	gfxcard = VID_INTERNAL;
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
		free_p = 1;
	}
	gfxcard = video_get_video_from_internal_name(p);
	if (free_p)
		free(p);
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

    p = config_get_string(cat, "joystick_type", NULL);
    if (p != NULL) {
	joystick_type = joystick_get_from_internal_name(p);
	if (!joystick_type) {
		/* Try to read an integer for backwards compatibility with old configs */
		c = config_get_int(cat, "joystick_type", 8);
		if ((c >= 0) && (c < 8))
			/* "None" was type 8 instead of 0 previously, shift the number accordingly */
			joystick_type = c + 1;
		else
			joystick_type = 0;
	}
    } else
	joystick_type = 0;

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
    /* FIXME: Hack to not break configs with the Sound Blaster 128 PCI set. */
    if ((p != NULL) && (!strcmp(p, "sbpci128") || !strcmp(p, "sb128pci")))
	p = "es1371";
    if (p != NULL)
	sound_card_current = sound_card_get_from_internal_name(p);
      else
	sound_card_current = 0;

    p = config_get_string(cat, "midi_device", NULL);
    if (p != NULL)
	midi_device_current = midi_device_get_from_internal_name(p);
      else
	midi_device_current = 0;

    p = config_get_string(cat, "midi_in_device", NULL);
    if (p != NULL)
	midi_input_device_current = midi_in_device_get_from_internal_name(p);
      else
	midi_input_device_current = 0;

    mpu401_standalone_enable = !!config_get_int(cat, "mpu401_standalone", 0);

    SSI2001 = !!config_get_int(cat, "ssi2001", 0);
    GAMEBLASTER = !!config_get_int(cat, "gameblaster", 0);
    GUS = !!config_get_int(cat, "gus", 0);
    
    memset(temp, '\0', sizeof(temp));
    p = config_get_string(cat, "sound_type", "float");
    if (strlen(p) > 511)
	fatal("load_sound(): strlen(p) > 511\n");
    else
	strncpy(temp, p, strlen(p) + 1);
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
			ui_msgbox_header(MBX_ERROR, (wchar_t *) IDS_2094, (wchar_t *) IDS_2129);
		} else if (network_dev_to_id(p) == -1) {
			ui_msgbox_header(MBX_ERROR, (wchar_t *) IDS_2095, (wchar_t *) IDS_2129);
		}

		strcpy(network_host, "none");
	} else {
		strncpy(network_host, p, sizeof(network_host) - 1);
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
    char temp[512];
    int c, d;

    for (c = 0; c < 4; c++) {
	sprintf(temp, "serial%d_enabled", c + 1);
	serial_enabled[c] = !!config_get_int(cat, temp, (c >= 2) ? 0 : 1);
    }

    for (c = 0; c < 3; c++) {
	sprintf(temp, "lpt%d_enabled", c + 1);
	lpt_ports[c].enabled = !!config_get_int(cat, temp, (c == 0) ? 1 : 0);

	sprintf(temp, "lpt%d_device", c + 1);
	p = (char *) config_get_string(cat, temp, "none");
	lpt_ports[c].device = lpt_device_get_from_internal_name(p);
    }

    /* Legacy config compatibility. */
    d = config_get_int(cat, "lpt_enabled", 2);
    if (d < 2) {
	for (c = 0; c < 3; c++)
		lpt_ports[c].enabled = d;
    }
    config_delete_var(cat, "lpt_enabled");
}


/* Load "Storage Controllers" section. */
static void
load_storage_controllers(void)
{
    char *cat = "Storage controllers";
    char *p, temp[512];
    int c, min = 0;
    int free_p = 0;

    /* TODO: Backwards compatibility, get rid of this when enough time has passed. */
    backwards_compat2 = (find_section(cat) == NULL);
	
    /* TODO: Backwards compatibility, get rid of this when enough time has passed. */
    p = config_get_string(cat, "scsicard", NULL);
    if (p != NULL) {
	scsi_card_current[0] = scsi_card_get_from_internal_name(p);
	min++;
    }
    config_delete_var(cat, "scsi_card");

    for (c = min; c < SCSI_BUS_MAX; c++) {
	sprintf(temp, "scsicard_%d", c + 1);

	p = config_get_string(cat, temp, NULL);
	if (p != NULL)
		scsi_card_current[c] = scsi_card_get_from_internal_name(p);
	else
		scsi_card_current[c] = 0;
    }

    p = config_get_string(cat, "fdc", NULL);
    if (p != NULL)
	fdc_type = fdc_card_get_from_internal_name(p);
      else
	fdc_type = FDC_INTERNAL;

    p = config_get_string(cat, "hdc", NULL);
    if (p == NULL) {
	if (machines[machine].flags & MACHINE_HDC) {
		p = (char *)malloc((strlen("internal")+1)*sizeof(char));
		strcpy(p, "internal");
	} else {
		p = (char *)malloc((strlen("none")+1)*sizeof(char));
		strcpy(p, "none");
	}
	free_p = 1;
    }
    if (!strcmp(p, "mfm_xt"))
	hdc_current = hdc_get_from_internal_name("st506_xt");
    else if (!strcmp(p, "mfm_xt_dtc5150x"))
	hdc_current = hdc_get_from_internal_name("st506_xt_dtc5150x");
    else if (!strcmp(p, "mfm_at"))
	hdc_current = hdc_get_from_internal_name("st506_at");
    else if (!strcmp(p, "vlb_isa"))
	hdc_current = hdc_get_from_internal_name("ide_vlb");
    else if (!strcmp(p, "vlb_isa_2ch"))
	hdc_current = hdc_get_from_internal_name("ide_vlb_2ch");
    else
	hdc_current = hdc_get_from_internal_name(p);

    if (free_p) {
	free(p);
	p = NULL;
    }

    ide_ter_enabled = !!config_get_int(cat, "ide_ter", 0);
    ide_qua_enabled = !!config_get_int(cat, "ide_qua", 0);

    cassette_enable = !!config_get_int(cat, "cassette_enabled", AT ? 0 : 1);
    p = config_get_string(cat, "cassette_file", "");
    if (strlen(p) > 511)
	fatal("load_storage_controllers(): strlen(p) > 511\n");
    else
	strncpy(cassette_fname, p, MIN(511, strlen(p) + 1));
    p = config_get_string(cat, "cassette_mode", "");
    if (strlen(p) > 511)
	fatal("load_storage_controllers(): strlen(p) > 511\n");
    else
	strncpy(cassette_mode, p, MIN(511, strlen(p) + 1));
    cassette_pos = config_get_int(cat, "cassette_position", 0);
    cassette_srate = config_get_int(cat, "cassette_srate", 44100);
    cassette_append = !!config_get_int(cat, "cassette_append", 0);
    cassette_pcm = config_get_int(cat, "cassette_pcm", 0);
    cassette_ui_writeprot = !!config_get_int(cat, "cassette_writeprot", 0);

    for (c=0; c<2; c++) {
	sprintf(temp, "cartridge_%02i_fn", c + 1);
	p = config_get_string(cat, temp, "");

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
		wcsncpy(floppyfns[c], &wp[wcslen(usr_path)], sizeof_w(cart_fns[c]));
	} else
#endif
	if (strlen(p) > 511)
		fatal("load_storage_controllers(): strlen(p) > 511\n");
	else
		strncpy(cart_fns[c], p, strlen(p) + 1);
    }
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
    uint32_t max_spt, max_hpc, max_tracks;
    uint32_t board = 0, dev = 0;

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
			max_spt = 26;	/* 26 for RLL */
			max_hpc = 15;
			max_tracks = 2047;
			break;

		case HDD_BUS_XTA:
			max_spt = 63;
			max_hpc = 16;
			max_tracks = 1023;
			break;

		case HDD_BUS_ESDI:
			max_spt = 99;
			max_hpc = 16;
			max_tracks = 266305;
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
	if (hdd[c].bus == HDD_BUS_SCSI) {
		sprintf(temp, "hdd_%02i_scsi_location", c+1);
		sprintf(tmp2, "%01u:%02u", SCSI_BUS_MAX, c+2);
		p = config_get_string(cat, temp, tmp2);
		sscanf(p, "%01u:%02u", &board, &dev);
		if (board >= SCSI_BUS_MAX) {
			/* Invalid bus - check legacy ID */
			sprintf(temp, "hdd_%02i_scsi_id", c+1);
			hdd[c].scsi_id = config_get_int(cat, temp, c+2);

			if (hdd[c].scsi_id > 15)
				hdd[c].scsi_id = 15;
		} else {
			board %= SCSI_BUS_MAX;
			dev &= 15;
			hdd[c].scsi_id = (board<<4)+dev;
		}
	} else {
		sprintf(temp, "hdd_%02i_scsi_location", c+1);
		config_delete_var(cat, temp);
	}

	sprintf(temp, "hdd_%02i_scsi_id", c+1);
	config_delete_var(cat, temp);

	memset(hdd[c].fn, 0x00, sizeof(hdd[c].fn));
	memset(hdd[c].prev_fn, 0x00, sizeof(hdd[c].prev_fn));
	sprintf(temp, "hdd_%02i_fn", c+1);
	p = config_get_string(cat, temp, "");

#if 0
	/*
	 * NOTE:
	 * Temporary hack to remove the absolute
	 * path currently saved in most config
	 * files.  We should remove this before
	 * finalizing this release!  --FvK
	 */
	/*
	 * ANOTHER NOTE:
	 * When loading differencing VHDs, the absolute path is required.
	 * So we should not convert absolute paths to relative. -sards
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
	if (plat_path_abs(p)) {
		strncpy(hdd[c].fn, p, sizeof(hdd[c].fn) - 1);
	} else {
		plat_append_filename(hdd[c].fn, usr_path, p);
	}

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

		sprintf(temp, "hdd_%02i_fn", c+1);
		config_delete_var(cat, temp);
	}

	sprintf(temp, "hdd_%02i_mfm_channel", c+1);
	config_delete_var(cat, temp);

	sprintf(temp, "hdd_%02i_ide_channel", c+1);
	config_delete_var(cat, temp);
    }
}


/* TODO: Backwards compatibility, get rid of this when enough time has passed. */
/* Load "Floppy Drives" section. */
static void
load_floppy_drives(void)
{
    char *cat = "Floppy drives";
    char temp[512], *p;
    int c;

    if (!backwards_compat)
	return;

    for (c=0; c<FDD_NUM; c++) {
	sprintf(temp, "fdd_%02i_type", c+1);
	p = config_get_string(cat, temp, (c < 2) ? "525_2dd" : "none");
   	fdd_set_type(c, fdd_get_from_internal_name(p));
	if (fdd_get_type(c) > 13)
		fdd_set_type(c, 13);
	config_delete_var(cat, temp);

	sprintf(temp, "fdd_%02i_fn", c + 1);
	p = config_get_string(cat, temp, "");
	config_delete_var(cat, temp);

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
	if (strlen(p) > 511)
		fatal("load_floppy_drives(): strlen(p) > 511\n");
	else
		strncpy(floppyfns[c], p, strlen(p) + 1);

	/* if (*wp != L'\0')
		config_log("Floppy%d: %ls\n", c, floppyfns[c]); */
	sprintf(temp, "fdd_%02i_writeprot", c+1);
	ui_writeprot[c] = !!config_get_int(cat, temp, 0);
	config_delete_var(cat, temp);
	sprintf(temp, "fdd_%02i_turbo", c + 1);
	fdd_set_turbo(c, !!config_get_int(cat, temp, 0));
	config_delete_var(cat, temp);
	sprintf(temp, "fdd_%02i_check_bpb", c+1);
	fdd_set_check_bpb(c, !!config_get_int(cat, temp, 1));
	config_delete_var(cat, temp);
    }

    delete_section_if_empty(cat);
}


/* Load "Floppy and CD-ROM Drives" section. */
static void
load_floppy_and_cdrom_drives(void)
{
    char *cat = "Floppy and CD-ROM drives";
    char temp[512], tmp2[512], *p;
    char s[512];
    unsigned int board = 0, dev = 0;
    int c, d = 0;

    /* TODO: Backwards compatibility, get rid of this when enough time has passed. */
    backwards_compat = (find_section(cat) == NULL);

    memset(temp, 0x00, sizeof(temp));
    for (c=0; c<FDD_NUM; c++) {
	sprintf(temp, "fdd_%02i_type", c+1);
	p = config_get_string(cat, temp, (c < 2) ? "525_2dd" : "none");
   	fdd_set_type(c, fdd_get_from_internal_name(p));
	if (fdd_get_type(c) > 13)
		fdd_set_type(c, 13);

	sprintf(temp, "fdd_%02i_fn", c + 1);
	p = config_get_string(cat, temp, "");

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
	if (strlen(p) > 511)
		fatal("load_floppy_and_cdrom_drives(): strlen(p) > 511\n");
	else
		strncpy(floppyfns[c], p, strlen(p) + 1);

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
	if (strlen(floppyfns[c]) == 0) {
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
	cdrom[c].host_drive = config_get_int(cat, temp, 0);
	cdrom[c].prev_host_drive = cdrom[c].host_drive;

	sprintf(temp, "cdrom_%02i_parameters", c+1);
	p = config_get_string(cat, temp, NULL);
	if (p != NULL)
		sscanf(p, "%01u, %s", &d, s);
	else if (c == 0)
		/* If this is the first drive, unmute the audio. */
		sscanf("1, none", "%01u, %s", &d, s);
	else
		sscanf("0, none", "%01u, %s", &d, s);
	cdrom[c].sound_on = d;
	cdrom[c].bus_type = hdd_string_to_bus(s, 1);

	sprintf(temp, "cdrom_%02i_speed", c+1);
	cdrom[c].speed = config_get_int(cat, temp, 8);

	/* Default values, needed for proper operation of the Settings dialog. */
	cdrom[c].ide_channel = cdrom[c].scsi_device_id = c + 2;

	if (cdrom[c].bus_type == CDROM_BUS_ATAPI) {
		sprintf(temp, "cdrom_%02i_ide_channel", c+1);
		sprintf(tmp2, "%01u:%01u", (c+2)>>1, (c+2)&1);
		p = config_get_string(cat, temp, tmp2);
		sscanf(p, "%01u:%01u", &board, &dev);
		board &= 3;
		dev &= 1;
		cdrom[c].ide_channel = (board<<1)+dev;

		if (cdrom[c].ide_channel > 7)
			cdrom[c].ide_channel = 7;
	} else if (cdrom[c].bus_type == CDROM_BUS_SCSI) {
		sprintf(temp, "cdrom_%02i_scsi_location", c+1);
		sprintf(tmp2, "%01u:%02u", SCSI_BUS_MAX, c+2);
		p = config_get_string(cat, temp, tmp2);
		sscanf(p, "%01u:%02u", &board, &dev);
		if (board >= SCSI_BUS_MAX) {
			/* Invalid bus - check legacy ID */
			sprintf(temp, "cdrom_%02i_scsi_id", c+1);
			cdrom[c].scsi_device_id = config_get_int(cat, temp, c+2);

			if (cdrom[c].scsi_device_id > 15)
				cdrom[c].scsi_device_id = 15;
		} else {
			board %= SCSI_BUS_MAX;
			dev &= 15;
			cdrom[c].scsi_device_id = (board<<4)+dev;
		}
	}

	if (cdrom[c].bus_type != CDROM_BUS_ATAPI) {
		sprintf(temp, "cdrom_%02i_ide_channel", c+1);
		config_delete_var(cat, temp);
	}

	if (cdrom[c].bus_type != CDROM_BUS_SCSI) {
		sprintf(temp, "cdrom_%02i_scsi_location", c+1);
		config_delete_var(cat, temp);
	}

	sprintf(temp, "cdrom_%02i_scsi_id", c+1);
	config_delete_var(cat, temp);

	sprintf(temp, "cdrom_%02i_image_path", c+1);
	p = config_get_string(cat, temp, "");

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
		wcsncpy(cdrom[c].image_path, &wp[wcslen(usr_path)], sizeof_w(cdrom[c].image_path));
	} else
#endif
	strncpy(cdrom[c].image_path, p, sizeof(cdrom[c].image_path) - 1);

	if (cdrom[c].host_drive && (cdrom[c].host_drive != 200))
		cdrom[c].host_drive = 0;

	if ((cdrom[c].host_drive == 0x200) &&
	    (strlen(cdrom[c].image_path) == 0))
		cdrom[c].host_drive = 0;

	/* If the CD-ROM is disabled, delete all its variables. */
	if (cdrom[c].bus_type == CDROM_BUS_DISABLED) {
		sprintf(temp, "cdrom_%02i_host_drive", c+1);
		config_delete_var(cat, temp);

		sprintf(temp, "cdrom_%02i_parameters", c+1);
		config_delete_var(cat, temp);

		sprintf(temp, "cdrom_%02i_ide_channel", c+1);
		config_delete_var(cat, temp);

		sprintf(temp, "cdrom_%02i_scsi_id", c+1);
		config_delete_var(cat, temp);

		sprintf(temp, "cdrom_%02i_image_path", c+1);
		config_delete_var(cat, temp);
	}

	sprintf(temp, "cdrom_%02i_iso_path", c+1);
	config_delete_var(cat, temp);
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
    int c, d = 0;

    /* TODO: Backwards compatibility, get rid of this when enough time has passed. */
    if (backwards_compat) {
	memset(temp, 0x00, sizeof(temp));
	for (c=0; c<CDROM_NUM; c++) {
		sprintf(temp, "cdrom_%02i_host_drive", c+1);
		cdrom[c].host_drive = config_get_int(cat, temp, 0);
		cdrom[c].prev_host_drive = cdrom[c].host_drive;
		config_delete_var(cat, temp);

		sprintf(temp, "cdrom_%02i_parameters", c+1);
		p = config_get_string(cat, temp, NULL);
		if (p != NULL)
			sscanf(p, "%01u, %s", &d, s);
		else
			sscanf("0, none", "%01u, %s", &d, s);
		cdrom[c].sound_on = d;
		cdrom[c].bus_type = hdd_string_to_bus(s, 1);
		config_delete_var(cat, temp);

		sprintf(temp, "cdrom_%02i_speed", c+1);
		cdrom[c].speed = config_get_int(cat, temp, 8);
		config_delete_var(cat, temp);

		/* Default values, needed for proper operation of the Settings dialog. */
		cdrom[c].ide_channel = cdrom[c].scsi_device_id = c + 2;
		config_delete_var(cat, temp);

		if (cdrom[c].bus_type == CDROM_BUS_ATAPI) {
			sprintf(temp, "cdrom_%02i_ide_channel", c+1);
			sprintf(tmp2, "%01u:%01u", (c+2)>>1, (c+2)&1);
			p = config_get_string(cat, temp, tmp2);
			sscanf(p, "%01u:%01u", &board, &dev);
			board &= 3;
			dev &= 1;
			cdrom[c].ide_channel = (board<<1)+dev;

			if (cdrom[c].ide_channel > 7)
				cdrom[c].ide_channel = 7;

			config_delete_var(cat, temp);
		} else if (cdrom[c].bus_type == CDROM_BUS_SCSI) {
			sprintf(temp, "cdrom_%02i_scsi_id", c+1);
			cdrom[c].scsi_device_id = config_get_int(cat, temp, c+2);

			if (cdrom[c].scsi_device_id > 15)
				cdrom[c].scsi_device_id = 15;

			config_delete_var(cat, temp);
		}

		sprintf(temp, "cdrom_%02i_image_path", c+1);
		p = config_get_string(cat, temp, "");
		config_delete_var(cat, temp);

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
			wcsncpy(cdrom[c].image_path, &wp[wcslen(usr_path)], sizeof_w(cdrom[c].image_path));
		} else
#endif
		strncpy(cdrom[c].image_path, p, sizeof(cdrom[c].image_path) - 1);

		if (cdrom[c].host_drive && (cdrom[c].host_drive != 200))
			cdrom[c].host_drive = 0;

		if ((cdrom[c].host_drive == 0x200) &&
		    (strlen(cdrom[c].image_path) == 0))
			cdrom[c].host_drive = 0;
	}
    }
    backwards_compat = 0;

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

	if (zip_drives[c].bus_type == ZIP_BUS_ATAPI) {
		sprintf(temp, "zip_%02i_ide_channel", c+1);
		sprintf(tmp2, "%01u:%01u", (c+2)>>1, (c+2)&1);
		p = config_get_string(cat, temp, tmp2);
		sscanf(p, "%01u:%01u", &board, &dev);
		board &= 3;
		dev &= 1;
		zip_drives[c].ide_channel = (board<<1)+dev;

		if (zip_drives[c].ide_channel > 7)
			zip_drives[c].ide_channel = 7;
	} else if (zip_drives[c].bus_type == ZIP_BUS_SCSI) {
		sprintf(temp, "zip_%02i_scsi_location", c+1);
		sprintf(tmp2, "%01u:%02u", SCSI_BUS_MAX, c+2);
		p = config_get_string(cat, temp, tmp2);
		sscanf(p, "%01u:%02u", &board, &dev);
		if (board >= SCSI_BUS_MAX) {
			/* Invalid bus - check legacy ID */
			sprintf(temp, "zip_%02i_scsi_id", c+1);
			zip_drives[c].scsi_device_id = config_get_int(cat, temp, c+2);

			if (zip_drives[c].scsi_device_id > 15)
				zip_drives[c].scsi_device_id = 15;
		} else {
			board %= SCSI_BUS_MAX;
			dev &= 15;
			zip_drives[c].scsi_device_id = (board<<4)+dev;
		}
	}

	if (zip_drives[c].bus_type != ZIP_BUS_ATAPI) {
		sprintf(temp, "zip_%02i_ide_channel", c+1);
		config_delete_var(cat, temp);
	}

	if (zip_drives[c].bus_type != ZIP_BUS_SCSI) {
		sprintf(temp, "zip_%02i_scsi_location", c+1);
		config_delete_var(cat, temp);
	}

	sprintf(temp, "zip_%02i_scsi_id", c+1);
	config_delete_var(cat, temp);

	sprintf(temp, "zip_%02i_image_path", c+1);
	p = config_get_string(cat, temp, "");

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
	strncpy(zip_drives[c].image_path, p, sizeof(zip_drives[c].image_path) - 1);

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

		sprintf(temp, "zip_%02i_image_path", c+1);
		config_delete_var(cat, temp);
	}

	sprintf(temp, "zip_%02i_iso_path", c+1);
	config_delete_var(cat, temp);
    }

    memset(temp, 0x00, sizeof(temp));
    for (c=0; c<MO_NUM; c++) {
	sprintf(temp, "mo_%02i_parameters", c+1);
	p = config_get_string(cat, temp, NULL);
	if (p != NULL)
		sscanf(p, "%u, %s", &mo_drives[c].type, s);
	else
		sscanf("00, none", "%u, %s", &mo_drives[c].type, s);
	mo_drives[c].bus_type = hdd_string_to_bus(s, 1);

	/* Default values, needed for proper operation of the Settings dialog. */
	mo_drives[c].ide_channel = mo_drives[c].scsi_device_id = c + 2;

	if (mo_drives[c].bus_type == MO_BUS_ATAPI) {
		sprintf(temp, "mo_%02i_ide_channel", c+1);
		sprintf(tmp2, "%01u:%01u", (c+2)>>1, (c+2)&1);
		p = config_get_string(cat, temp, tmp2);
		sscanf(p, "%01u:%01u", &board, &dev);
		board &= 3;
		dev &= 1;
		mo_drives[c].ide_channel = (board<<1)+dev;

		if (mo_drives[c].ide_channel > 7)
			mo_drives[c].ide_channel = 7;
	} else if (mo_drives[c].bus_type == MO_BUS_SCSI) {
		sprintf(temp, "mo_%02i_scsi_location", c+1);
		sprintf(tmp2, "%01u:%02u", SCSI_BUS_MAX, c+2);
		p = config_get_string(cat, temp, tmp2);
		sscanf(p, "%01u:%02u", &board, &dev);
		if (board >= SCSI_BUS_MAX) {
			/* Invalid bus - check legacy ID */
			sprintf(temp, "mo_%02i_scsi_id", c+1);
			mo_drives[c].scsi_device_id = config_get_int(cat, temp, c+2);

			if (mo_drives[c].scsi_device_id > 15)
				mo_drives[c].scsi_device_id = 15;
		} else {
			board %= SCSI_BUS_MAX;
			dev &= 15;
			mo_drives[c].scsi_device_id = (board<<4)+dev;
		}
	}

	if (mo_drives[c].bus_type != MO_BUS_ATAPI) {
		sprintf(temp, "mo_%02i_ide_channel", c+1);
		config_delete_var(cat, temp);
	}

	if (mo_drives[c].bus_type != MO_BUS_SCSI) {
		sprintf(temp, "mo_%02i_scsi_location", c+1);
		config_delete_var(cat, temp);
	}

	sprintf(temp, "mo_%02i_scsi_id", c+1);
	config_delete_var(cat, temp);

	sprintf(temp, "mo_%02i_image_path", c+1);
	p = config_get_string(cat, temp, "");

	strncpy(mo_drives[c].image_path, p, sizeof(mo_drives[c].image_path) - 1);

	/* If the CD-ROM is disabled, delete all its variables. */
	if (mo_drives[c].bus_type == MO_BUS_DISABLED) {
		sprintf(temp, "mo_%02i_host_drive", c+1);
		config_delete_var(cat, temp);

		sprintf(temp, "mo_%02i_parameters", c+1);
		config_delete_var(cat, temp);

		sprintf(temp, "mo_%02i_ide_channel", c+1);
		config_delete_var(cat, temp);

		sprintf(temp, "mo_%02i_scsi_id", c+1);
		config_delete_var(cat, temp);

		sprintf(temp, "mo_%02i_image_path", c+1);
		config_delete_var(cat, temp);
	}

	sprintf(temp, "mo_%02i_iso_path", c+1);
	config_delete_var(cat, temp);
    }    
}


/* Load "Other Peripherals" section. */
static void
load_other_peripherals(void)
{
    char *cat = "Other peripherals";
    char *p;
    char temp[512];
    int c, free_p = 0;

    if (backwards_compat2) {	
	p = config_get_string(cat, "scsicard", NULL);
	if (p != NULL)
		scsi_card_current[0] = scsi_card_get_from_internal_name(p);
	else
		scsi_card_current[0] = 0;
	config_delete_var(cat, "scsicard");

	p = config_get_string(cat, "fdc", NULL);
	if (p != NULL)
		fdc_type = fdc_card_get_from_internal_name(p);
	else
		fdc_type = FDC_INTERNAL;
	config_delete_var(cat, "fdc");

	p = config_get_string(cat, "hdc", NULL);
	if (p == NULL) {
		if (machines[machine].flags & MACHINE_HDC) {
			p = (char *)malloc((strlen("internal")+1)*sizeof(char));
			strcpy(p, "internal");
		} else {
			p = (char *)malloc((strlen("none")+1)*sizeof(char));
			strcpy(p, "none");
		}
		free_p = 1;
	}
	if (!strcmp(p, "mfm_xt"))
		hdc_current = hdc_get_from_internal_name("st506_xt");
	else if (!strcmp(p, "mfm_xt_dtc5150x"))
		hdc_current = hdc_get_from_internal_name("st506_xt_dtc5150x");
	else if (!strcmp(p, "mfm_at"))
		hdc_current = hdc_get_from_internal_name("st506_at");
	else if (!strcmp(p, "vlb_isa"))
		hdc_current = hdc_get_from_internal_name("ide_vlb");
	else if (!strcmp(p, "vlb_isa_2ch"))
		hdc_current = hdc_get_from_internal_name("ide_vlb_2ch");
	else
		hdc_current = hdc_get_from_internal_name(p);
	config_delete_var(cat, "hdc");

	if (free_p) {
		free(p);
		p = NULL;
	}

	ide_ter_enabled = !!config_get_int(cat, "ide_ter", 0);
	config_delete_var(cat, "ide_ter");
	ide_qua_enabled = !!config_get_int(cat, "ide_qua", 0);
	config_delete_var(cat, "ide_qua");
    }
    backwards_compat2 = 0;

    bugger_enabled = !!config_get_int(cat, "bugger_enabled", 0);
    postcard_enabled = !!config_get_int(cat, "postcard_enabled", 0);

    for (c = 0; c < ISAMEM_MAX; c++) {
	sprintf(temp, "isamem%d_type", c);

	p = config_get_string(cat, temp, "none");
	isamem_type[c] = isamem_get_from_internal_name(p);
    }

    p = config_get_string(cat, "isartc_type", "none");
    isartc_type = isartc_get_from_internal_name(p);	
}


/* Load the specified or a default configuration file. */
void
config_load(void)
{
    int i;

    config_log("Loading config file '%s'..\n", cfg_path);

    memset(hdd, 0, sizeof(hard_disk_t));
    memset(cdrom, 0, sizeof(cdrom_t) * CDROM_NUM);
#ifdef USE_IOCTL
    memset(cdrom_ioctl, 0, sizeof(cdrom_ioctl_t) * CDROM_NUM);
#endif
    memset(zip_drives, 0, sizeof(zip_drive_t));

    if (! config_read(cfg_path)) {
	config_changed = 1;

	cpu_f = (cpu_family_t *) &cpu_families[0];
	cpu = 0;
#ifdef USE_LANGUAGE
	plat_langid = 0x0409;
#endif
	kbd_req_capture = 0;
	hide_status_bar = 0;
	scale = 1;
	machine = machine_get_machine_from_internal_name("ibmpc");
	dpi_scale = 1;

	/* Set up the architecture flags. */
	AT = IS_AT(machine);
	PCI = IS_ARCH(machine, MACHINE_BUS_PCI);

	fpu_type = fpu_get_type(cpu_f, cpu, "none");
	gfxcard = video_get_video_from_internal_name("cga");
	vid_api = plat_vidapi("default");
	vid_resize = 0;
	video_fullscreen_first = 1;
	time_sync = TIME_SYNC_ENABLED;
	hdc_current = hdc_get_from_internal_name("none");
	serial_enabled[0] = 1;
	serial_enabled[1] = 1;
	serial_enabled[2] = 0;
	serial_enabled[3] = 0;
	lpt_ports[0].enabled = 1;
	lpt_ports[1].enabled = 0;
	lpt_ports[2].enabled = 0;
	for (i = 0; i < FDD_NUM; i++) {
		if (i < 2)
			fdd_set_type(i, 2);
		else
			fdd_set_type(i, 0);

		fdd_set_turbo(i, 0);
		fdd_set_check_bpb(i, 1);
	}

	/* Unmute the CD audio on the first CD-ROM drive. */
	cdrom[0].sound_on = 1;
	mem_size = 640;
	isartc_type = 0;
	for (i = 0; i < ISAMEM_MAX; i++)
		isamem_type[i] = 0;

	cassette_enable = AT ? 0 : 1;
	memset(cassette_fname, 0x00, sizeof(cassette_fname));
	memcpy(cassette_mode, "load", strlen("load") + 1);
	cassette_pos = 0;
	cassette_srate = 44100;
	cassette_append = 0;
	cassette_pcm = 0;
	cassette_ui_writeprot = 0;

	config_log("Config file not present or invalid!\n");
    } else {
	load_general();			/* General */
	load_machine();			/* Machine */
	load_video();			/* Video */
	load_input_devices();		/* Input devices */
	load_sound();			/* Sound */
	load_network();			/* Network */
	load_ports();			/* Ports (COM & LPT) */
	load_storage_controllers();		/* Storage controllers */
	load_hard_disks();			/* Hard disks */
	load_floppy_and_cdrom_drives();	/* Floppy and CD-ROM drives */
	/* TODO: Backwards compatibility, get rid of this when enough time has passed. */
	load_floppy_drives();		/* Floppy drives */
	load_other_removable_devices();	/* Other removable devices */
	load_other_peripherals();		/* Other peripherals */

	/* Mark the configuration as changed. */
	config_changed = 1;

	config_log("Config loaded.\n\n");
    }

    video_copy = (video_grayscale || invert_display) ? video_transform_copy : memcpy;
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

    if (video_fullscreen_first == 1)
	config_delete_var(cat, "video_fullscreen_first");
      else
	config_set_int(cat, "video_fullscreen_first", video_fullscreen_first);

    if (video_filter_method == 1)
	config_delete_var(cat, "video_filter_method");
      else
	config_set_int(cat, "video_filter_method", video_filter_method);

    if (force_43 == 0)
	config_delete_var(cat, "force_43");
      else
	config_set_int(cat, "force_43", force_43);

    if (scale == 1)
	config_delete_var(cat, "scale");
      else
	config_set_int(cat, "scale", scale);

    if (dpi_scale == 1)
	config_delete_var(cat, "dpi_scale");
      else
	config_set_int(cat, "dpi_scale", dpi_scale);

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

    if (window_remember || (vid_resize & 2)) {
	if (window_remember)
		config_set_int(cat, "window_remember", window_remember);
	else
		config_delete_var(cat, "window_remember");

	sprintf(temp, "%i, %i, %i, %i", window_w, window_h, window_x, window_y);
	config_set_string(cat, "window_coordinates", temp);
    } else {
	config_delete_var(cat, "window_remember");
	config_delete_var(cat, "window_coordinates");
    }

    if (vid_resize & 2) {
	sprintf(temp, "%ix%i", fixed_size_x, fixed_size_y);
	config_set_string(cat, "window_fixed_res", temp);
    } else
	config_delete_var(cat, "window_fixed_res");

    if (sound_gain != 0)
	config_set_int(cat, "sound_gain", sound_gain);
    else
	config_delete_var(cat, "sound_gain");

    if (kbd_req_capture != 0)
	config_set_int(cat, "kbd_req_capture", kbd_req_capture);
    else
	config_delete_var(cat, "kbd_req_capture");

    if (hide_status_bar != 0)
	config_set_int(cat, "hide_status_bar", hide_status_bar);
    else
	config_delete_var(cat, "hide_status_bar");

    if (confirm_reset != 1)
	config_set_int(cat, "confirm_reset", confirm_reset);
    else
	config_delete_var(cat, "confirm_reset");

    if (confirm_exit != 1)
	config_set_int(cat, "confirm_exit", confirm_exit);
    else
	config_delete_var(cat, "confirm_exit");

    if (confirm_save != 1)
	config_set_int(cat, "confirm_save", confirm_save);
    else
	config_delete_var(cat, "confirm_save");

#ifdef USE_LANGUAGE
    if (plat_langid == 0x0409)
	config_delete_var(cat, "language");
      else
	config_set_hex16(cat, "language", plat_langid);
#endif

#if USE_DISCORD
    if (enable_discord)
	config_set_int(cat, "enable_discord", enable_discord);
    else
	config_delete_var(cat, "enable_discord");
#endif

#if defined(DEV_BRANCH) && defined(USE_OPENGL)
    if (video_framerate != -1)
	    config_set_int(cat, "video_gl_framerate", video_framerate);
    else
	    config_delete_var(cat, "video_gl_framerate");
    if (video_vsync != 0)
	    config_set_int(cat, "video_gl_vsync", video_vsync);
    else
	    config_delete_var(cat, "video_gl_vsync");
    if (strlen(video_shader) > 0)
	    config_set_string(cat, "video_gl_shader", video_shader);
    else
	    config_delete_var(cat, "video_gl_shader");
#endif

    delete_section_if_empty(cat);
}


/* Save "Machine" section. */
static void
save_machine(void)
{
    char *cat = "Machine";
    char *p;
    int c, i = 0, legacy_mfg, legacy_cpu = -1, closest_legacy_cpu = -1;

    p = machine_get_internal_name();
    config_set_string(cat, "machine", p);

    config_set_string(cat, "cpu_family", (char *) cpu_f->internal_name);
    config_set_int(cat, "cpu_speed", cpu_f->cpus[cpu].rspeed);
    config_set_double(cat, "cpu_multi", cpu_f->cpus[cpu].multi);
    if (cpu_override)
	config_set_int(cat, "cpu_override", cpu_override);
    else
	config_delete_var(cat, "cpu_override");

    /* Forwards compatibility with the previous CPU model system. */
    config_delete_var(cat, "cpu_manufacturer");
    config_delete_var(cat, "cpu");

    /* Look for a machine entry on the legacy table. */
    c = 0;
    while (cpu_legacy_table[c].machine) {
	if (!strcmp(p, cpu_legacy_table[c].machine))
		break;
	c++;
    }
    if (cpu_legacy_table[c].machine) {
	/* Look for a corresponding CPU entry. */
	cpu_legacy_table_t *legacy_table_entry;
	for (legacy_mfg = 0; legacy_mfg < 4; legacy_mfg++) {
		if (!cpu_legacy_table[c].tables[legacy_mfg])
			continue;

		i = 0;
		while (cpu_legacy_table[c].tables[legacy_mfg][i].family) {
			legacy_table_entry = (cpu_legacy_table_t *) &cpu_legacy_table[c].tables[legacy_mfg][i];

			/* Match the family name, speed and multiplier. */
			if (!strcmp(cpu_f->internal_name, legacy_table_entry->family)) {
				if ((legacy_table_entry->rspeed == cpu_f->cpus[cpu].rspeed) && 
				    (legacy_table_entry->multi == cpu_f->cpus[cpu].multi)) { /* exact speed/multiplier match */
					legacy_cpu = i;
					break;
				} else if ((legacy_table_entry->rspeed >= cpu_f->cpus[cpu].rspeed) &&
					   (closest_legacy_cpu == -1)) { /* closest speed match */
					closest_legacy_cpu = i;
				}
			}

			i++;
		}

		/* Use the closest speed match if no exact match was found. */
		if ((legacy_cpu == -1) && (closest_legacy_cpu > -1)) {
			legacy_cpu = closest_legacy_cpu;
			break;
		} else if (legacy_cpu > -1) /* exact match found */
			break;
	}

	/* Set legacy values if a match was found. */
	if (legacy_cpu > -1) {
		if (legacy_mfg)
			config_set_int(cat, "cpu_manufacturer", legacy_mfg);
		if (legacy_cpu)
			config_set_int(cat, "cpu", legacy_cpu);
	}
    }

    if (cpu_waitstates == 0)
	config_delete_var(cat, "cpu_waitstates");
      else
	config_set_int(cat, "cpu_waitstates", cpu_waitstates);

    if (fpu_type == 0)
	config_delete_var(cat, "fpu_type");
      else
	config_set_string(cat, "fpu_type", (char *) fpu_get_internal_name(cpu_f, cpu, fpu_type));

    if (mem_size == 4096)
	config_delete_var(cat, "mem_size");
      else
	config_set_int(cat, "mem_size", mem_size);

    config_set_int(cat, "cpu_use_dynarec", cpu_use_dynarec);

    if (time_sync & TIME_SYNC_ENABLED)
	if (time_sync & TIME_SYNC_UTC)
		config_set_string(cat, "time_sync", "utc");
	else
		config_set_string(cat, "time_sync", "local");
    else
	config_set_string(cat, "time_sync", "disabled");

    delete_section_if_empty(cat);
}


/* Save "Video" section. */
static void
save_video(void)
{
    char *cat = "Video";

    config_set_string(cat, "gfxcard",
	video_get_internal_name(gfxcard));

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

    if (!joystick_type) {
	config_delete_var(cat, "joystick_type");

	for (c = 0; c < 16; c++) {
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
	config_set_string(cat, "joystick_type", joystick_get_internal_name(joystick_type));

	for (c = 0; c < joystick_get_max_joysticks(joystick_type); c++) {
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

    if (!strcmp(midi_in_device_get_internal_name(midi_input_device_current), "none"))
	config_delete_var(cat, "midi_in_device");
      else
	config_set_string(cat, "midi_in_device", midi_in_device_get_internal_name(midi_input_device_current));

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
    char temp[512];
    int c, d;

    for (c = 0; c < 4; c++) {
	sprintf(temp, "serial%d_enabled", c + 1);
	if (((c < 2) && serial_enabled[c]) || ((c >= 2) && !serial_enabled[c]))
		config_delete_var(cat, temp);
	else
		config_set_int(cat, temp, serial_enabled[c]);
    }

    for (c = 0; c < 3; c++) {
	sprintf(temp, "lpt%d_enabled", c + 1);
	d = (c == 0) ? 1 : 0;
	if (lpt_ports[c].enabled == d)
		config_delete_var(cat, temp);
	else
		config_set_int(cat, temp, lpt_ports[c].enabled);

	sprintf(temp, "lpt%d_device", c + 1);
	if (lpt_ports[c].device == 0)
		config_delete_var(cat, temp);
	  else
		config_set_string(cat, temp,
				  (char *) lpt_device_get_internal_name(lpt_ports[c].device));
    }

    delete_section_if_empty(cat);
}


/* Save "Storage Controllers" section. */
static void
save_storage_controllers(void)
{
    char *cat = "Storage controllers";
    char temp[512];
    int c;

    config_delete_var(cat, "scsicard");

    for (c = 0; c < SCSI_BUS_MAX; c++) {
	sprintf(temp, "scsicard_%d", c + 1);

	if (scsi_card_current[c] == 0)
		config_delete_var(cat, temp);
	  else
		config_set_string(cat, temp,
				  scsi_card_get_internal_name(scsi_card_current[c]));
    }

    if (fdc_type == FDC_INTERNAL)
	config_delete_var(cat, "fdc");
      else
	config_set_string(cat, "fdc",
			  fdc_card_get_internal_name(fdc_type));

    config_set_string(cat, "hdc",
	hdc_get_internal_name(hdc_current));

    if (ide_ter_enabled == 0)
	config_delete_var(cat, "ide_ter");
      else
	config_set_int(cat, "ide_ter", ide_ter_enabled);

    if (ide_qua_enabled == 0)
	config_delete_var(cat, "ide_qua");
      else
	config_set_int(cat, "ide_qua", ide_qua_enabled);

    delete_section_if_empty(cat);

    if (cassette_enable == 1)
	config_delete_var(cat, "cassette_enabled");
    else
	config_set_int(cat, "cassette_enabled", cassette_enable);

    if (strlen(cassette_fname) == 0)
	config_delete_var(cat, "cassette_file");
    else
	config_set_string(cat, "cassette_file", cassette_fname);

    if (strlen(cassette_mode) == 0)
	config_delete_var(cat, "cassette_mode");
    else
	config_set_string(cat, "cassette_mode", cassette_mode);

    if (cassette_pos == 0)
	config_delete_var(cat, "cassette_position");
    else
	config_set_int(cat, "cassette_position", cassette_pos);

    if (cassette_srate == 44100)
	config_delete_var(cat, "cassette_srate");
    else
	config_set_int(cat, "cassette_srate", cassette_srate);

    if (cassette_append == 0)
	config_delete_var(cat, "cassette_append");
    else
	config_set_int(cat, "cassette_append", cassette_append);

    if (cassette_pcm == 0)
	config_delete_var(cat, "cassette_pcm");
    else
	config_set_int(cat, "cassette_pcm", cassette_pcm);

    if (cassette_ui_writeprot == 0)
	config_delete_var(cat, "cassette_writeprot");
    else
	config_set_int(cat, "cassette_writeprot", cassette_ui_writeprot);

    for (c=0; c<2; c++) {
	sprintf(temp, "cartridge_%02i_fn", c+1);
	if (strlen(cart_fns[c]) == 0)
		config_delete_var(cat, temp);
	else
		config_set_string(cat, temp, cart_fns[c]);
    }
}


/* Save "Other Peripherals" section. */
static void
save_other_peripherals(void)
{
    char *cat = "Other peripherals";
    char temp[512];
    int c;

    if (bugger_enabled == 0)
	config_delete_var(cat, "bugger_enabled");
      else
	config_set_int(cat, "bugger_enabled", bugger_enabled);

    if (postcard_enabled == 0)
	config_delete_var(cat, "postcard_enabled");
      else
	config_set_int(cat, "postcard_enabled", postcard_enabled);

    for (c = 0; c < ISAMEM_MAX; c++) {
	sprintf(temp, "isamem%d_type", c);
	if (isamem_type[c] == 0)
		config_delete_var(cat, temp);
	  else
		config_set_string(cat, temp,
				  (char *) isamem_get_internal_name(isamem_type[c]));
    }

    if (isartc_type == 0)
	config_delete_var(cat, "isartc_type");
      else
	config_set_string(cat, "isartc_type",
			  isartc_get_internal_name(isartc_type));	
	
    delete_section_if_empty(cat);
}


/* Save "Hard Disks" section. */
static void
save_hard_disks(void)
{
    char *cat = "Hard disks";
    char temp[32], tmp2[512];
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
	config_delete_var(cat, temp);

	sprintf(temp, "hdd_%02i_scsi_location", c+1);
	if (hdd[c].bus != HDD_BUS_SCSI)
		config_delete_var(cat, temp);
	else {
		sprintf(tmp2, "%01u:%02u", hdd[c].scsi_id>>4,
					hdd[c].scsi_id & 15);
		config_set_string(cat, temp, tmp2);
	}

	sprintf(temp, "hdd_%02i_fn", c+1);
	if (hdd_is_valid(c) && (strlen(hdd[c].fn) != 0))
		if (!strnicmp(hdd[c].fn, usr_path, strlen(usr_path)))
			config_set_string(cat, temp, &hdd[c].fn[strlen(usr_path)]);
		else
			config_set_string(cat, temp, hdd[c].fn);
	else
		config_delete_var(cat, temp);
    }

    delete_section_if_empty(cat);
}


/* Save "Floppy Drives" section. */
static void
save_floppy_and_cdrom_drives(void)
{
    char *cat = "Floppy and CD-ROM drives";
    char temp[512], tmp2[512];
    int c;

    for (c=0; c<FDD_NUM; c++) {
	sprintf(temp, "fdd_%02i_type", c+1);
	if (fdd_get_type(c) == ((c < 2) ? 2 : 0))
		config_delete_var(cat, temp);
	  else
		config_set_string(cat, temp,
				  fdd_get_internal_name(fdd_get_type(c)));

	sprintf(temp, "fdd_%02i_fn", c+1);
	if (strlen(floppyfns[c]) == 0) {
		config_delete_var(cat, temp);

		ui_writeprot[c] = 0;

		sprintf(temp, "fdd_%02i_writeprot", c+1);
		config_delete_var(cat, temp);
	} else {
		config_set_string(cat, temp, floppyfns[c]);
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
	if ((cdrom[c].bus_type == 0) || (cdrom[c].host_drive != 200)) {
		config_delete_var(cat, temp);
	} else {
		config_set_int(cat, temp, cdrom[c].host_drive);
	}

	sprintf(temp, "cdrom_%02i_speed", c+1);
	if ((cdrom[c].bus_type == 0) || (cdrom[c].speed == 8)) {
		config_delete_var(cat, temp);
	} else {
		config_set_int(cat, temp, cdrom[c].speed);
	}

	sprintf(temp, "cdrom_%02i_parameters", c+1);
	if (cdrom[c].bus_type == 0) {
		config_delete_var(cat, temp);
	} else {
		sprintf(tmp2, "%u, %s", cdrom[c].sound_on,
			hdd_bus_to_string(cdrom[c].bus_type, 1));
		config_set_string(cat, temp, tmp2);
	}

	sprintf(temp, "cdrom_%02i_ide_channel", c+1);
	if (cdrom[c].bus_type != CDROM_BUS_ATAPI)
		config_delete_var(cat, temp);
	else {
		sprintf(tmp2, "%01u:%01u", cdrom[c].ide_channel>>1,
					cdrom[c].ide_channel & 1);
		config_set_string(cat, temp, tmp2);
	}

	sprintf(temp, "cdrom_%02i_scsi_id", c + 1);
	config_delete_var(cat, temp);

	sprintf(temp, "cdrom_%02i_scsi_location", c+1);
	if (cdrom[c].bus_type != CDROM_BUS_SCSI)
		config_delete_var(cat, temp);
	else {
		sprintf(tmp2, "%01u:%02u", cdrom[c].scsi_device_id>>4,
					cdrom[c].scsi_device_id & 15);
		config_set_string(cat, temp, tmp2);
	}

	sprintf(temp, "cdrom_%02i_image_path", c + 1);
	if ((cdrom[c].bus_type == 0) ||
	    (strlen(cdrom[c].image_path) == 0)) {
		config_delete_var(cat, temp);
	} else {
		config_set_string(cat, temp, cdrom[c].image_path);
	}
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
	config_delete_var(cat, temp);

	sprintf(temp, "zip_%02i_scsi_location", c+1);
	if (zip_drives[c].bus_type != ZIP_BUS_SCSI)
		config_delete_var(cat, temp);
	else {
		sprintf(tmp2, "%01u:%02u", zip_drives[c].scsi_device_id>>4,
					zip_drives[c].scsi_device_id & 15);
		config_set_string(cat, temp, tmp2);
	}

	sprintf(temp, "zip_%02i_image_path", c + 1);
	if ((zip_drives[c].bus_type == 0) ||
	    (strlen(zip_drives[c].image_path) == 0)) {
		config_delete_var(cat, temp);
	} else {
		config_set_string(cat, temp, zip_drives[c].image_path);
	}
    }

    for (c=0; c<MO_NUM; c++) {
	sprintf(temp, "mo_%02i_parameters", c+1);
	if (mo_drives[c].bus_type == 0) {
		config_delete_var(cat, temp);
	} else {
		sprintf(tmp2, "%u, %s", mo_drives[c].type,
			hdd_bus_to_string(mo_drives[c].bus_type, 1));
		config_set_string(cat, temp, tmp2);
	}
		
	sprintf(temp, "mo_%02i_ide_channel", c+1);
	if (mo_drives[c].bus_type != MO_BUS_ATAPI)
		config_delete_var(cat, temp);
	else {
		sprintf(tmp2, "%01u:%01u", mo_drives[c].ide_channel>>1,
					mo_drives[c].ide_channel & 1);
		config_set_string(cat, temp, tmp2);
	}

	sprintf(temp, "mo_%02i_scsi_id", c + 1);
	config_delete_var(cat, temp);

	sprintf(temp, "mo_%02i_scsi_location", c+1);
	if (mo_drives[c].bus_type != MO_BUS_SCSI)
		config_delete_var(cat, temp);
	else {
		sprintf(tmp2, "%01u:%02u", mo_drives[c].scsi_device_id>>4,
					mo_drives[c].scsi_device_id & 15);
		config_set_string(cat, temp, tmp2);
	}

	sprintf(temp, "mo_%02i_image_path", c + 1);
	if ((mo_drives[c].bus_type == 0) ||
	    (strlen(mo_drives[c].image_path) == 0)) {
		config_delete_var(cat, temp);
	} else {
		config_set_string(cat, temp, mo_drives[c].image_path);
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
    save_storage_controllers();		/* Storage controllers */
    save_hard_disks();			/* Hard disks */
    save_floppy_and_cdrom_drives();	/* Floppy and CD-ROM drives */
    save_other_removable_devices();	/* Other removable devices */
    save_other_peripherals();		/* Other peripherals */

    config_write(cfg_path);
}


void
config_dump(void)
{
    section_t *sec;
	
    sec = (section_t *)config_head.next;
    while (sec != NULL) {
	entry_t *ent;

	if (sec->name[0])
		config_log("[%s]\n", sec->name);
	
	ent = (entry_t *)sec->entry_head.next;
	while (ent != NULL) {
		config_log("%s = %s\n", ent->name, ent->data);

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


double
config_get_double(char *head, char *name, double def)
{
    section_t *section;
    entry_t *entry;
    double value;

    section = find_section(head);
    if (section == NULL)
	return(def);
		
    entry = find_entry(section, name);
    if (entry == NULL)
	return(def);

    sscanf(entry->data, "%lg", &value);

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
    mbstowcs(ent->wdata, ent->data, 512);
}


void
config_set_double(char *head, char *name, double val)
{
    section_t *section;
    entry_t *ent;

    section = find_section(head);
    if (section == NULL)
	section = create_section(head);

    ent = find_entry(section, name);
    if (ent == NULL)
	ent = create_entry(section, name);

    sprintf(ent->data, "%lg", val);
    mbstowcs(ent->wdata, ent->data, 512);
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
    mbstowcs(ent->wdata, ent->data, 512);
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

    if ((strlen(val) + 1) <= sizeof(ent->data))
	memcpy(ent->data, val, strlen(val) + 1);
    else
	memcpy(ent->data, val, sizeof(ent->data));
#ifdef _WIN32	/* Make sure the string is converted from UTF-8 rather than a legacy codepage */
    mbstoc16s(ent->wdata, ent->data, sizeof_w(ent->wdata));
#else
    mbstowcs(ent->wdata, ent->data, sizeof_w(ent->wdata));
#endif
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
#ifdef _WIN32	/* Make sure the string is converted to UTF-8 rather than a legacy codepage */
    c16stombs(ent->data, ent->wdata, sizeof(ent->data));
#else
    wcstombs(ent->data, ent->wdata, sizeof(ent->data));
#endif
}
