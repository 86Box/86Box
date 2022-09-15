/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Configuration file handler header.
 *
 *
 *
 * Authors:	Sarah Walker,
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Overdoze,
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#ifndef EMU_CONFIG_H
#define EMU_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#if 0
typedef struct {
    uint8_t	id,
    uint8_t	bus_type,		/* Bus type: IDE, SCSI, etc. */
		bus,		:4,	/* ID of the bus (for example, for IDE,
					   0 = primary, 1 = secondary, etc. */
		bus_id,		:4,	/* ID of the device on the bus */
    uint8_t	type,			/* Type flags, interpretation depends
					   on the device */
    uint8_t	is_image;		/* This is only used for CD-ROM:
					   0 = Image;
					   1 = Host drive */

    wchar_t	path[1024];		/* Name of current image file or
					   host drive */

    uint32_t	spt,			/* Physical geometry parameters */
		hpc,
		tracks;
} storage_cfg_t;

typedef struct {
    /* General configuration */
    int			vid_resize,			/* Window is resizable or not */
			vid_renderer,			/* Renderer */
			vid_fullscreen_scale,		/* Full screen scale type */
			vid_fullscreen_start,		/* Start emulator in full screen */
			vid_force_43,			/* Force 4:3 display ratio in windowed mode */
			vid_scale,			/* Windowed mode scale */
			vid_overscan,			/* EGA/(S)VGA overscan enabled */
			vid_cga_contrast,		/* CGA alternate contrast enabled */
			vid_grayscale,			/* Video is grayscale */
			vid_grayscale_type,		/* Video grayscale type */
			vid_invert_display,		/* Invert display */
			rctrl_is_lalt,			/* Right CTRL is left ALT */
			update_icons,			/* Update status bar icons */
			window_remember,		/* Remember window position and size */
			window_w,			/* Window coordinates */
			window_h,
			window_x,
			window_y,
			sound_gain;			/* Sound gain */
#    ifdef USE_LANGUAGE
    uint16_t		language_id;			/* Language ID (0x0409 = English (US)) */
#    endif

    /* Machine cateogory */
    int			machine,			/* Machine */
			cpu,				/* CPU */
#    ifdef USE_DYNAREC
			cpu_use_dynarec,		/* CPU recompiler enabled */
#    endif
			wait_states,			/* CPU wait states */
			enable_external_fpu,		/* FPU enabled */
			time_sync;			/* Time sync enabled */
    uint32_t		mem_size;			/* Memory size */

    /* Video category */
    int			video_card,			/* Video card */
			voodoo_enabled;			/* Voodoo enabled */

    /* Input devices category */
    int			mouse_type,			/* Mouse type */
			joystick_type;			/* Joystick type */

    /* Sound category */
    int			sound_card,			/* Sound card */
			midi_device,			/* Midi device */
			mpu_401,			/* Standalone MPU-401 enabled */
			ssi_2001_enabled,		/* SSI-2001 enabled */
			game_blaster_enabled,		/* Game blaster enabled */
			gus_enabled,			/* Gravis Ultrasound enabled */
			opl_type,			/* OPL emulation type */
			sound_is_float;			/* Sound is 32-bit float or 16-bit integer */

    /* Network category */
    int			network_type,			/* Network type (SLiRP or PCap) */
			network_card;			/* Network card */
    char		network_host[520];		/* PCap device */

    /* Ports category */
    char		parallel_devices[PARALLEL_MAX][32];	/* LPT device names */
#    ifdef USE_SERIAL_DEVICES
    char		serial_devices[SERIAL_MAX][32];		/* Serial device names */
#    endif

    /* Other peripherals category */
    int			fdc_type,			/* Floppy disk controller type */
			hdc,				/* Hard disk controller */
			scsi_card,			/* SCSI controller */
			ide_ter_enabled,		/* Tertiary IDE controller enabled */
			ide_qua_enabled,		/* Quaternary IDE controller enabled */
			bugger_enabled,			/* ISA bugger device enabled */
			isa_rtc_type,			/* ISA RTC card */
			isa_mem_type[ISAMEM_MAX];	/* ISA memory boards */

    /* Hard disks category */
    storage_cfg_t	hdd[HDD_NUM];			/* Hard disk drives */

    /* Floppy drives category */
    storage_cfg_t	fdd[FDD_NUM];			/* Floppy drives */

    /* Other removable devices category */
    storage_cfg_t	cdrom[CDROM_NUM],		/* CD-ROM drives */
    storage_cfg_t	rdisk[ZIP_NUM];			/* Removable disk drives */
} config_t;
#endif

extern void config_load(void);
extern void config_save(void);

#ifdef EMU_INI_H
extern ini_t config_get_ini(void);
#else
extern void *config_get_ini(void);
#endif

#define config_delete_var(head, name)       ini_delete_var(config_get_ini(), head, name)

#define config_get_int(head, name, def)     ini_get_int(config_get_ini(), head, name, def)
#define config_get_double(head, name, def)  ini_get_double(config_get_ini(), head, name, def)
#define config_get_hex16(head, name, def)   ini_get_hex16(config_get_ini(), head, name, def)
#define config_get_hex20(head, name, def)   ini_get_hex20(config_get_ini(), head, name, def)
#define config_get_mac(head, name, def)     ini_get_mac(config_get_ini(), head, name, def)
#define config_get_string(head, name, def)  ini_get_string(config_get_ini(), head, name, def)
#define config_get_wstring(head, name, def) ini_get_wstring(config_get_ini(), head, name, def)

#define config_set_int(head, name, val)     ini_set_int(config_get_ini(), head, name, val)
#define config_set_double(head, name, val)  ini_set_double(config_get_ini(), head, name, val)
#define config_set_hex16(head, name, val)   ini_set_hex16(config_get_ini(), head, name, val)
#define config_set_hex20(head, name, val)   ini_set_hex20(config_get_ini(), head, name, val)
#define config_set_mac(head, name, val)     ini_set_mac(config_get_ini(), head, name, val)
#define config_set_string(head, name, val)  ini_set_string(config_get_ini(), head, name, val)
#define config_set_wstring(head, name, val) ini_set_wstring(config_get_ini(), head, name, val)

#define config_find_section(name)           ini_find_section(config_get_ini(), name)
#define config_rename_section               ini_rename_section

#ifdef __cplusplus
}
#endif

#endif /*EMU_CONFIG_H*/
