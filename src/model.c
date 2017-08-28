/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Handling of the emulated machines.
 *
 * Version:	@(#)model.c	1.0.7	2017/08/24
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#include <stdint.h>
#include <stdio.h>
#include "ibm.h"
#include "cpu/cpu.h"
#include "io.h"
#include "mem.h"
#include "rom.h"
#include "device.h"
#include "model.h"
#include "mouse.h"
#include "cdrom.h"

#include "disc.h"
#include "dma.h"
#include "fdc.h"
#include "fdc37c665.h"
#include "fdc37c669.h"
#include "fdc37c932fr.h"
#include "gameport.h"
#include "i82335.h"
#include "hdd/hdd_ide_at.h"
#include "intel.h"
#include "intel_flash.h"
#include "keyboard_amstrad.h"
#include "keyboard_at.h"
#include "keyboard_olim24.h"
#include "keyboard_pcjr.h"
#include "keyboard_xt.h"
#include "lpt.h"
#include "mem.h"
#include "memregs.h"
#include "nmi.h"
#include "nvr.h"
#include "pc87306.h"
#include "pci.h"
#include "pic.h"
#include "piix.h"
#include "pit.h"
#include "ps2_mca.h"
#include "serial.h"
#include "sis85c471.h"
#include "sio.h"
#include "sound/snd_ps1.h"
#include "sound/snd_pssj.h"
#include "sound/snd_sn76489.h"
#if 0
#include "superio_detect.h"
#endif
#include "tandy_eeprom.h"
#include "tandy_rom.h"
#include "um8669f.h"
#include "video/vid_pcjr.h"
#include "video/vid_tandy.h"
#include "w83877f.h"
#include "wd76c10.h"
#include "hdd/hdd_ide_xt.h"
#include "bugger.h"


extern void             xt_init(void);
extern void           pcjr_init(void);
extern void        tandy1k_init(void);
extern void     tandy1ksl2_init(void);
extern void            ams_init(void);
extern void         europc_init(void);
extern void         olim24_init(void);
extern void             at_init(void);
extern void         ibm_at_init(void);
extern void         at_ide_init(void);
extern void        cmdpc30_init(void);
extern void     deskpro386_init(void);
extern void      ps1_m2011_init(void);
extern void      ps1_m2121_init(void);
extern void      ps1_m2133_init(void);
extern void    ps2_m30_286_init(void);
extern void   ps2_model_50_init(void);
extern void ps2_model_55sx_init(void);
extern void   ps2_model_80_init(void);
extern void        at_neat_init(void);
extern void        at_scat_init(void);
extern void     at_wd76c10_init(void);
extern void     at_ali1429_init(void);
extern void    at_headland_init(void);
extern void     at_opti495_init(void);
extern void      at_i430vx_init(void);
extern void      at_batman_init(void);
#if 0
extern void	 at_586mc1_init(void);
#endif
extern void    at_endeavor_init(void);

extern void      at_dtk486_init(void);
extern void        at_r418_init(void);
extern void       at_plato_init(void);
extern void      at_mb500n_init(void);
extern void   at_president_init(void);
extern void    at_p54tp4xe_init(void);
extern void        at_ap53_init(void);
extern void      at_p55t2s_init(void);
extern void     at_acerm3a_init(void);
extern void    at_acerv35n_init(void);
extern void     at_p55t2p4_init(void);
extern void     at_p55tvp4_init(void);
extern void       at_p55va_init(void);
extern void      at_i440fx_init(void);
extern void       at_s1668_init(void);

extern void     xt_laserxt_init(void);

int model;

int AMSTRAD, AT, PCI, TANDY;

PCI_RESET pci_reset_handler;

int serial_enabled[2] = { 0, 0 };
int lpt_enabled = 0, bugger_enabled = 0;

int romset;

MODEL models[] =
{
        {"[8088] AMI XT clone",		ROM_AMIXT,		"amixt",		{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, 0,							 64,  640,  64,   0,             xt_init, NULL			},
        {"[8088] Compaq Portable",	ROM_PORTABLE,		"portable",		{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, 0,							128,  640, 128,   0,             xt_init, NULL			},
        {"[8088] DTK XT clone",		ROM_DTKXT,		"dtk",			{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, 0,							 64,  640,  64,   0,             xt_init, NULL			},
        {"[8088] IBM PC",		ROM_IBMPC,		"ibmpc",		{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, 0,							 64,  640,  32,   0,             xt_init, NULL			},
        {"[8088] IBM PCjr",		ROM_IBMPCJR,		"ibmpcjr",		{{"",      cpus_pcjr},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, 0,							128,  640, 128,   0,           pcjr_init, &pcjr_device		},
        {"[8088] IBM XT",		ROM_IBMXT,		"ibmxt",		{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, 0,							 64,  640,  64,   0,             xt_init, NULL			},
        {"[8088] Generic XT clone",	ROM_GENXT,		"genxt",		{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, 0,							 64,  640,  64,   0,             xt_init, NULL			},
        {"[8088] Juko XT clone",	ROM_JUKOPC,		"jukopc",		{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, 0,							 64,  640,  64,   0,             xt_init, NULL			},
        {"[8088] Phoenix XT clone",	ROM_PXXT,		"pxxt",			{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, 0,							 64,  640,  64,   0,             xt_init, NULL			},
        {"[8088] Schneider EuroPC",	ROM_EUROPC,		"europc",		{{"",      cpus_europc},      {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, 0,							512,  640, 128,   0,         europc_init, NULL			},
        {"[8088] Tandy 1000",		ROM_TANDY,		"tandy",		{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, 0,							128,  640, 128,   0,        tandy1k_init, &tandy1000_device	},
        {"[8088] Tandy 1000 HX",	ROM_TANDY1000HX,	"tandy1000hx",		{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, 0,							256,  640, 128,   0,        tandy1k_init, &tandy1000hx_device	},
        {"[8088] VTech Laser Turbo XT",	ROM_LTXT,		"ltxt",			{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, 0,							 64, 1152,  64,   0,     xt_laserxt_init, NULL			},
	{"[8088] VTech Laser XT3",	ROM_LXT3,		"lxt3",			{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, 0,							 64, 1152,  64,   0,     xt_laserxt_init, NULL			},

        {"[8086] Amstrad PC1512",	ROM_PC1512,		"pc1512",		{{"",      cpus_pc1512},      {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, MODEL_AMSTRAD,						512,  640, 128,  63,            ams_init, NULL			},
        {"[8086] Amstrad PC1640",	ROM_PC1640,		"pc1640",		{{"",      cpus_8086},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, MODEL_AMSTRAD,						640,  640,   0,  63,            ams_init, NULL			},
        {"[8086] Amstrad PC2086",	ROM_PC2086,		"pc2086",		{{"",      cpus_8086},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, MODEL_AMSTRAD,						640,  640,   0,  63,            ams_init, NULL			},
        {"[8086] Amstrad PC3086",	ROM_PC3086,		"pc3086",		{{"",      cpus_8086},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, MODEL_AMSTRAD,						640,  640,   0,  63,            ams_init, NULL			},
        {"[8086] Olivetti M24",		ROM_OLIM24,		"olivetti_m24",		{{"",      cpus_8086},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, MODEL_OLIM24,						128,  640, 128,   0,         olim24_init, NULL			},
        {"[8086] Sinclair PC200",	ROM_PC200,		"pc200",		{{"",      cpus_8086},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, MODEL_AMSTRAD,						512,  640, 128,  63,            ams_init, NULL			},
        {"[8086] Tandy 1000 SL/2",	ROM_TANDY1000SL2,	"tandy1000sl2",		{{"",      cpus_8086},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, 0,							512,  768, 128,   0,     tandy1ksl2_init, NULL			},

        {"[286] AMI 286 clone",		ROM_AMI286,		"ami286",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_HAS_IDE,				512,16384, 128, 127,        at_neat_init, NULL			},
        {"[286] Award 286 clone",	ROM_AWARD286,		"award286",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_HAS_IDE,				512,16384, 128, 127,        at_scat_init, NULL			},
#if 0
        {"[286] Compaq Portable II",	ROM_PORTABLEII,		"portableii",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MODEL_AT,						  1,   15,   1,  63,             at_init, NULL			},
        {"[286] Compaq Portable III",	ROM_PORTABLEIII,	"portableiii",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MODEL_AT,						  1,   15,   1,  63,             at_init, NULL			},
#endif
        {"[286] Commodore PC 30 III",	ROM_CMDPC30,		"cmdpc30",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_HAS_IDE,				640,16384, 128, 127,        cmdpc30_init, NULL			},
        {"[286] Hyundai Super-286TR",	ROM_SUPER286TR,		"super286tr",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_HAS_IDE,				512,16384, 128, 127,        at_scat_init, NULL			},
        {"[286] IBM AT",		ROM_IBMAT,		"ibmat",		{{"",      cpus_ibmat},       {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MODEL_AT,						256,15872, 128,  63,         ibm_at_init, NULL			},
        {"[286] IBM PS/1 model 2011",	ROM_IBMPS1_2011,	"ibmps1es",		{{"",      cpus_ps1_m2011},   {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, MODEL_AT | MODEL_PS2 | MODEL_PS2_HDD,			512,16384, 512, 127,      ps1_m2011_init, NULL			},
        {"[286] IBM PS/2 Model 30-286",	ROM_IBMPS2_M30_286,	"ibmps2_m30_286",	{{"",      cpus_ps2_m30_286}, {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, MODEL_AT | MODEL_PS2 | MODEL_PS2_HDD,			  1,   16,   1, 127,    ps2_m30_286_init, NULL			},
        {"[286] IBM PS/2 Model 50",	ROM_IBMPS2_M50,		"ibmps2_m50",		{{"",      cpus_ps2_m30_286}, {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, MODEL_AT | MODEL_PS2 | MODEL_PS2_HDD | MODEL_MCA,	  1,   16,   1,  63,   ps2_model_50_init, NULL			},
        {"[286] Samsung SPC-4200P",	ROM_SPC4200P,		"spc4200p",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE,			512,16384, 128, 127,        at_scat_init, NULL			},

        {"[386SX] AMI 386SX clone",	ROM_AMI386SX,		"ami386",		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_HAS_IDE,				512,16384, 128, 127,    at_headland_init, NULL			},
        {"[386SX] Amstrad MegaPC",	ROM_MEGAPC,		"megapc",		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, 1, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE,			  1,   16,   1, 127,     at_wd76c10_init, NULL			},
        {"[386SX] Award 386SX clone",	ROM_AWARD386SX_OPTI495,	"award386sx",		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_HAS_IDE,				  1,   64,   1, 127,     at_opti495_init, NULL			},
        {"[386SX] DTK 386SX clone",	ROM_DTK386,		"dtk386",		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_HAS_IDE,				512,16384, 128, 127,        at_neat_init, NULL			},
        {"[386SX] IBM PS/1 model 2121",	ROM_IBMPS1_2121,	"ibmps1_2121",		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, 1, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE,			  1,   16,   1, 127,      ps1_m2121_init, NULL			},
        {"[386SX] IBM PS/1 m.2121+ISA", ROM_IBMPS1_2121_ISA,	"ibmps1_2121_isa",	{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE,			  1,   16,   1, 127,      ps1_m2121_init, NULL			},
        {"[386SX] IBM PS/2 Model 55SX",	ROM_IBMPS2_M55SX,	"ibmps2_m55sx",		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, 1, MODEL_AT | MODEL_PS2 | MODEL_PS2_HDD | MODEL_MCA,	  1,    8,   1,  63, ps2_model_55sx_init, NULL			},

        {"[386DX] AMI 386DX clone",	ROM_AMI386DX_OPTI495,	"ami386dx",		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_HAS_IDE,				  1,   64,   1, 127,     at_opti495_init, NULL			},
        {"[386DX] Amstrad MegaPC 386DX",ROM_MEGAPCDX,		"megapcdx",		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, 1, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE,			  1,   16,   1, 127,     at_wd76c10_init, NULL			},
        {"[386DX] Award 386DX clone",	ROM_AWARD386DX_OPTI495,	"award386dx",		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_HAS_IDE,				  1,   64,   1, 127,     at_opti495_init, NULL			},
        {"[386DX] Compaq Deskpro 386",	ROM_DESKPRO_386,	"dekspro386",		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, 0, MODEL_AT,						  1,   15,   1,  63,     deskpro386_init, NULL			},
#if 0
        {"[386DX] Compaq Portable III 386",ROM_PORTABLEIII386,	"portableiii386",	{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, 0, MODEL_AT,						  1,   15,   1,  63,             at_init, NULL			},
#endif
        {"[386DX] IBM PS/2 Model 80",	ROM_IBMPS2_M80,		"ibmps2_m80",		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, 1, MODEL_AT | MODEL_PS2 | MODEL_PS2_HDD | MODEL_MCA,	  1,   12,   1,  63,   ps2_model_80_init, NULL			},
        {"[386DX] MR 386DX clone",	ROM_MR386DX_OPTI495,	"mr386dx",		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_HAS_IDE,				  1,   64,   1, 127,     at_opti495_init, NULL			},

        {"[486] AMI 486 clone",		ROM_AMI486,		"ami486",		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_HAS_IDE,				  1,   64,   1, 127,     at_ali1429_init, NULL			},
        {"[486] AMI WinBIOS 486",	ROM_WIN486,		"win486",		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_HAS_IDE,				  1,   64,   1, 127,     at_ali1429_init, NULL			},
        {"[486] Award 486 clone",	ROM_AWARD486_OPTI495,	"award486",		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_HAS_IDE,				  1,   64,   1, 127,     at_opti495_init, NULL			},
        {"[486] DTK PKM-0038S E-2",	ROM_DTK486,		"dtk486",		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_HAS_IDE,				  1,   64,   1, 127,      at_dtk486_init, NULL			},
        {"[486] IBM PS/1 model 2133",	ROM_IBMPS1_2133,	"ibmps1_2133",		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE,			  1,   64,   1, 127,      ps1_m2133_init, NULL			},
        {"[486] Rise Computer R418",	ROM_R418,		"r418",			{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_HAS_IDE | MODEL_PCI,			  1,   64,   1, 127,        at_r418_init, NULL			},

        {"[Socket 4 LX] Intel Premiere/PCI",ROM_REVENGE,	"revenge",		{{"Intel", cpus_Pentium5V},   {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE | MODEL_PCI,	  1,  128,   1, 127,      at_batman_init, NULL			},
#if 0
        {"[Socket 4 LX] Micro Star 586MC1",ROM_586MC1,		"586mc1",		{{"Intel", cpus_Pentium5V50}, {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE | MODEL_PCI,	  1,  128,   1, 127,      at_586mc1_init, NULL			},
#endif

        {"[Socket 5 NX] Intel Premiere/PCI II",	ROM_PLATO,	"plato",		{{ "Intel", cpus_PentiumS5},  {"IDT", cpus_WinChip}, {"AMD",   cpus_K5},     {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE | MODEL_PCI,	   1,  128,   1, 127,      at_plato_init, NULL			},

        {"[Socket 5 FX] ASUS P/I-P54TP4XE",	ROM_P54TP4XE,	"p54tp4xe",		{{ "Intel", cpus_PentiumS5},  {"IDT", cpus_WinChip}, {"AMD",   cpus_K5},     {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE | MODEL_PCI,	   1,  256,   1, 127,   at_p54tp4xe_init, NULL			},
        {"[Socket 5 FX] Intel Advanced/EV",	ROM_ENDEAVOR,	"endeavor",		{{ "Intel", cpus_PentiumS5},  {"IDT", cpus_WinChip}, {"AMD",   cpus_K5},     {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE | MODEL_PCI,	   1,  128,   1, 127,   at_endeavor_init, NULL			},
        {"[Socket 5 FX] Intel Advanced/ZP",	ROM_ZAPPA,	"zappa",		{{ "Intel", cpus_PentiumS5},  {"IDT", cpus_WinChip}, {"AMD",   cpus_K5},     {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE | MODEL_PCI,	   1,  128,   1, 127,   at_endeavor_init, NULL			},
        {"[Socket 5 FX] PC Partner MB500N",	ROM_MB500N,	"mb500n",		{{ "Intel", cpus_PentiumS5},  {"IDT", cpus_WinChip}, {"AMD",   cpus_K5},     {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE | MODEL_PCI,	   1,  128,   1, 127,     at_mb500n_init, NULL			},
        {"[Socket 5 FX] President Award 430FX PCI",ROM_PRESIDENT,"president",		{{ "Intel", cpus_PentiumS5},  {"IDT", cpus_WinChip}, {"AMD",   cpus_K5},     {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_HAS_IDE | MODEL_PCI,			  1,  128,   1, 127,   at_president_init, NULL			},

        {"[Socket 7 FX] Intel Advanced/ATX",	ROM_THOR,	"thor",			{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"AMD",   cpus_K56},    {"Cyrix", cpus_6x86},{"",      NULL}}, 0, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE | MODEL_PCI,	   1,  256,   1, 127,   at_endeavor_init, NULL			},
        {"[Socket 7 FX] MR Intel Advanced/ATX",	ROM_MRTHOR,	"mrthor",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"AMD",   cpus_K56},    {"Cyrix", cpus_6x86},{"",      NULL}}, 0, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE | MODEL_PCI,	   1,  256,   1, 127,   at_endeavor_init, NULL			},

        {"[Socket 7 HX] Acer M3a",		ROM_ACERM3A,	"acerm3a",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"AMD",   cpus_K56},    {"Cyrix", cpus_6x86},{"",      NULL}}, 0, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE | MODEL_PCI,	   1,  256,   1, 127,    at_acerm3a_init, NULL			},
        {"[Socket 7 HX] Acer V35n",		ROM_ACERV35N,	"acerv35n",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"AMD",   cpus_K56},    {"Cyrix", cpus_6x86},{"",      NULL}}, 0, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE | MODEL_PCI,	   1,  256,   1, 127,   at_acerv35n_init, NULL			},
        {"[Socket 7 HX] AOpen AP53",		ROM_AP53,	"ap53",			{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"AMD",   cpus_K56},    {"Cyrix", cpus_6x86},{"",      NULL}}, 0, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE | MODEL_PCI,	   1,  256,   1, 127,       at_ap53_init, NULL			},
        {"[Socket 7 HX] ASUS P/I-P55T2P4",	ROM_P55T2P4,	"p55t2p4",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"AMD",   cpus_K56},    {"Cyrix", cpus_6x86},{"",      NULL}}, 0, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE | MODEL_PCI,	   1,  256,   1, 127,    at_p55t2p4_init, NULL			},
        {"[Socket 7 HX] ASUS P/I-P55T2S",	ROM_P55T2S,	"p55t2s",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"AMD",   cpus_K56},    {"Cyrix", cpus_6x86},{"",      NULL}}, 0, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE | MODEL_PCI,	   1,  256,   1, 127,     at_p55t2s_init, NULL			},

        {"[Socket 7 VX] ASUS P/I-P55TVP4",	ROM_P55TVP4,	"p55tvp4",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"AMD",   cpus_K56},    {"Cyrix", cpus_6x86},{"",      NULL}}, 0, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE | MODEL_PCI,	   1,  256,   1, 127,    at_p55tvp4_init, NULL			},
        {"[Socket 7 VX] Award 430VX PCI",	ROM_430VX,	"430vx",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"AMD",   cpus_K56},    {"Cyrix", cpus_6x86},{"",      NULL}}, 0, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE | MODEL_PCI,	   1,  256,   1, 127,     at_i430vx_init, NULL			},
        {"[Socket 7 VX] Epox P55-VA",		ROM_P55VA,	"p55va",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"AMD",   cpus_K56},    {"Cyrix", cpus_6x86},{"",      NULL}}, 0, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE | MODEL_PCI,	   1,  256,   1, 127,      at_p55va_init, NULL			},

        {"[Socket 8 FX] Tyan Titan-Pro AT",	ROM_440FX,	"440fx",		{{"Intel", cpus_PentiumPro},  {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE | MODEL_PCI,	   1,  256,   1, 127,     at_i440fx_init, NULL			},
        {"[Socket 8 FX] Tyan Titan-Pro ATX",	ROM_S1668,	"tpatx",		{{"Intel", cpus_PentiumPro},  {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MODEL_AT | MODEL_PS2 | MODEL_HAS_IDE | MODEL_PCI,	   1,  256,   1, 127,      at_s1668_init, NULL			},
        {"",				-1,			"",			{{"", 0},                     {"", 0},               {"", 0}},			0,0,0,0, 0																			}
};


int model_count(void)
{
        return (sizeof(models) / sizeof(MODEL)) - 1;
}

int model_getromset(void)
{
        return models[model].id;
}

int model_getromset_ex(int m)
{
        return models[m].id;
}

int model_getmodel(int romset)
{
	int c = 0;
	
	while (models[c].id != -1)
	{
		if (models[c].id == romset)
			return c;
		c++;
	}
	
	return 0;
}

char *model_getname()
{
        return models[model].name;
}


device_t *model_getdevice(int model)
{
        return models[model].device;
}

char *model_get_internal_name(void)
{
        return models[model].internal_name;
}

char *model_get_internal_name_ex(int m)
{
        return models[m].internal_name;
}

int model_get_nvrmask(int m)
{
        return models[m].nvrmask;
}

int model_get_model_from_internal_name(char *s)
{
	int c = 0;
	
	while (models[c].id != -1)
	{
		if (!strcmp(models[c].internal_name, s))
			return c;
		c++;
	}
	
	return 0;
}

void common_init(void)
{
        dma_init();
        fdc_add();
	if (lpt_enabled)
	{
	        lpt_init();
	}
        pic_init();
        pit_init();
	if (serial_enabled[0])
	{
	        serial_setup(1, SERIAL1_ADDR, SERIAL1_IRQ);
	}
	if (serial_enabled[1])
	{
	        serial_setup(2, SERIAL2_ADDR, SERIAL2_IRQ);
	}
}

void xt_init(void)
{
        common_init();
	mem_add_bios();
        pit_set_out_func(&pit, 1, pit_refresh_timer_xt);
        keyboard_xt_init();
	nmi_init();
	if (joystick_type != 7)  device_add(&gameport_device);
	if (bugger_enabled)
	{
		bugger_init();
	}
}

void pcjr_init(void)
{
	mem_add_bios();
        fdc_add_pcjr();
        pic_init();
        pit_init();
        pit_set_out_func(&pit, 0, pit_irq0_timer_pcjr);
	if (serial_enabled[0])
	{
	        serial_setup(1, 0x2f8, 3);
	}
        keyboard_pcjr_init();
        device_add(&sn76489_device);
	nmi_mask = 0x80;
}

void tandy1k_init(void)
{
        TANDY = 1;
        common_init();
	mem_add_bios();
        keyboard_tandy_init();
        if (romset == ROM_TANDY)
                device_add(&sn76489_device);
        else
                device_add(&ncr8496_device);
	nmi_init();
	if (romset != ROM_TANDY)
                device_add(&tandy_eeprom_device);
	if (joystick_type != 7)  device_add(&gameport_device);
}
void tandy1ksl2_init(void)
{
        common_init();
	mem_add_bios();
        keyboard_tandy_init();
        device_add(&pssj_device);
	nmi_init();
        device_add(&tandy_rom_device);
        device_add(&tandy_eeprom_device);
	if (joystick_type != 7)  device_add(&gameport_device);
}

void ams_init(void)
{
        AMSTRAD = 1;
        common_init();
	mem_add_bios();
        amstrad_init();
        keyboard_amstrad_init();
        nvr_init();
	nmi_init();
	fdc_set_dskchg_activelow();
	if (joystick_type != 7)  device_add(&gameport_device);
}

void europc_init(void)
{
        common_init();
	mem_add_bios();
	lpt3_init(0x3bc);
        jim_init();
        keyboard_xt_init();
	nmi_init();
	if (joystick_type != 7)  device_add(&gameport_device);
}

void olim24_init(void)
{
        common_init();
	mem_add_bios();
        keyboard_olim24_init();
        nvr_init();
        olivetti_m24_init();
	nmi_init();
	if (joystick_type != 7)  device_add(&gameport_device);
}

void xt_laserxt_init(void)
{
        xt_init();
        laserxt_init();
}

void at_init(void)
{
	AT = 1;
        common_init();
	if (lpt_enabled)
	{
		lpt2_remove();
	}
	mem_add_bios();
        pit_set_out_func(&pit, 1, pit_refresh_timer_at);
        dma16_init();
        keyboard_at_init();
        nvr_init();
        pic2_init();
	if (joystick_type != 7)  device_add(&gameport_device);
	if (bugger_enabled)
	{
		bugger_init();
	}
}

void ibm_at_init(void)
{
        at_init();
        mem_remap_top_384k();
}

void at_ide_init(void)
{
	at_init();
	ide_init();
}

void cmdpc30_init(void)
{
	at_ide_init();
        mem_remap_top_384k();
}

void deskpro386_init(void)
{
        at_init();
        compaq_init();
}

void ps1_common_init(void)
{
        AT = 1;
        common_init();
	mem_add_bios();
        pit_set_out_func(&pit, 1, pit_refresh_timer_at);
        dma16_init();
        if (romset != ROM_IBMPS1_2011)
	{
		ide_init();
	}
        keyboard_at_init();
        nvr_init();
        pic2_init();
		if (romset != ROM_IBMPS1_2133)
		{			
			fdc_set_dskchg_activelow();
			device_add(&ps1_audio_device);
		}
        /*PS/1 audio uses ports 200h and 202-207h, so only initialise gameport on 201h*/
        if (joystick_type != 7)  device_add(&gameport_201_device);
}
 
void ps1_m2011_init(void)
{
        ps1_common_init();
        ps1mb_init();
        mem_remap_top_384k();
}

void ps1_m2121_init(void)
{
        ps1_common_init();
        ps1mb_m2121_init();
        fdc_set_ps1();
}

void ps1_m2133_init(void)
{
        ps1_common_init();
        ps1mb_m2133_init();
}

void ps2_m30_286_init(void)
{
        AT = 1;
        common_init();
        mem_add_bios();
        pit_set_out_func(&pit, 1, pit_refresh_timer_at);
        dma16_init();
        keyboard_at_init();
        nvr_init();
        pic2_init();
        ps2board_init();
        fdc_set_dskchg_activelow();
        fdc_set_ps1();
}

static void ps2_common_init(void)
{
        AT = 1;
        common_init();
        mem_add_bios();
        dma16_init();
        ps2_dma_init();
        ide_init();
        keyboard_at_init();
        keyboard_at_init_ps2();
        mouse_ps2_init();
        nvr_init();
        pic2_init();

        pit_ps2_init();
}

void ps2_model_50_init(void)
{
        ps2_common_init();
        ps2_mca_board_model_50_init();
}

void ps2_model_55sx_init(void)
{
        ps2_common_init();
        ps2_mca_board_model_55sx_init();
}

void ps2_model_80_init(void)
{
        ps2_common_init();
        ps2_mca_board_model_80_type2_init();
}

void at_neat_init(void)
{
        at_ide_init();
        neat_init();
}

void at_scat_init(void)
{
        at_ide_init();
        scat_init();
}

/* void at_acer386sx_init(void)
{
        at_ide_init();
        acer386sx_init();
}

void at_82335_init(void)
{
        at_ide_init();
        i82335_init();
} */

void at_wd76c10_init(void)
{
        at_ide_init();
        wd76c10_init();
}

void at_headland_init(void)
{
        at_ide_init();
        headland_init();
}

void at_opti495_init(void)
{
        at_ide_init();
        opti495_init();
}

void secondary_ide_check(void)
{
	int i = 0;
	int secondary_cdroms = 0;

	for (i = 0; i < CDROM_NUM; i++)
	{
		if ((cdrom_drives[i].ide_channel >= 2) && (cdrom_drives[i].ide_channel <= 3) && ((cdrom_drives[i].bus_type == CDROM_BUS_ATAPI_PIO_ONLY) || (cdrom_drives[i].bus_type == CDROM_BUS_ATAPI_PIO_AND_DMA)))
		{
			secondary_cdroms++;
		}
	}
	if (!secondary_cdroms)  ide_sec_disable();
}

void at_ali1429_init(void)
{
        ali1429_reset();

        at_ide_init();
        ali1429_init();

	secondary_ide_check();
}

/* void at_um8881f_init(void)
{
        at_ide_init();
        pci_init(PCI_CONFIG_TYPE_1, 0, 31);
        um8881f_init();
} */

void at_dtk486_init(void)
{
        at_ide_init();
	memregs_init();
	sis85c471_init();
	secondary_ide_check();
}

void at_sis496_init(void)
{
        at_ide_init();
	memregs_init();
        pci_init(PCI_CONFIG_TYPE_1);
        pci_slot(0xb);
        pci_slot(0xd);
        pci_slot(0xf);
	sis496_init();
	trc_init();
}

void at_r418_init(void)
{
	at_sis496_init();
        fdc37c665_init();
}

void at_premiere_common_init(void)
{
        at_ide_init();
	memregs_init();
        pci_init(PCI_CONFIG_TYPE_2);
        pci_slot(0xc);
        pci_slot(0xe);
        pci_slot(0x6);
 	sio_init(2, 0xc, 0xe, 0x6, 0);
        fdc37c665_init();
        intel_batman_init();
        device_add(&intel_flash_bxt_ami_device);
}

void at_batman_init(void)
{
	at_premiere_common_init();
        i430lx_init();
}

#if 0
void at_586mc1_init(void)
{
        at_ide_init();
	memregs_init();
        pci_init(PCI_CONFIG_TYPE_2);
        i430lx_init();
        pci_slot(0xc);
        pci_slot(0xe);
        pci_slot(0x6);
	sio_init(2, 0xc, 0xe, 0x6, 0);
        device_add(&intel_flash_bxt_device);
	secondary_ide_check();
}
#endif

void at_plato_init(void)
{
	at_premiere_common_init();
        i430nx_init();
}

void at_advanced_common_init(void)
{
        at_ide_init();
	memregs_init();
        pci_init(PCI_CONFIG_TYPE_1);
        pci_slot(0xd);
        pci_slot(0xe);
        pci_slot(0xf);
        pci_slot(0x10);
        i430fx_init();
        piix_init(7, 0xd, 0xe, 0xf, 0x10);
        pc87306_init();
}

void at_endeavor_init(void)
{
        at_advanced_common_init();
        device_add(&intel_flash_bxt_ami_device);
}

void at_mb500n_init(void)
{
        at_ide_init();
        pci_init(PCI_CONFIG_TYPE_1);
        pci_slot(0x14);
        pci_slot(0x13);
        pci_slot(0x12);
        pci_slot(0x11);
        i430fx_init();
        piix_init(7, 0x14, 0x13, 0x12, 0x11);
        fdc37c665_init();
        device_add(&intel_flash_bxt_device);
}

void at_president_init(void)
{
        at_ide_init();
	memregs_init();
        pci_init(PCI_CONFIG_TYPE_1);
        pci_slot(8);
        pci_slot(9);
        pci_slot(10);
        pci_slot(11);
        i430fx_init();
        piix_init(7, 8, 9, 10, 11);
#if 0
	superio_detect_init();
#endif
        w83877f_init();
        device_add(&intel_flash_bxt_device);
}

void at_p54tp4xe_init(void)
{
        at_ide_init();
	memregs_init();
        pci_init(PCI_CONFIG_TYPE_1);
        pci_slot(12);
        pci_slot(11);
        pci_slot(10);
        pci_slot(9);
        i430fx_init();
        piix_init(7, 12, 11, 10, 9);
        fdc37c665_init();
        device_add(&intel_flash_bxt_device);
}

void at_ap53_init(void)
{
        at_ide_init();
        memregs_init();
        powermate_memregs_init();
        pci_init(PCI_CONFIG_TYPE_1);
        pci_slot(0x11);
        pci_slot(0x12);
        pci_slot(0x13);
        pci_slot(0x14);
        i430hx_init();
        piix3_init(7, 0x11, 0x12, 0x13, 0x14);
        fdc37c669_init();
        device_add(&intel_flash_bxt_device);
}

void at_p55t2s_init(void)
{
        at_ide_init();
        memregs_init();
        powermate_memregs_init();
        pci_init(PCI_CONFIG_TYPE_1);
        pci_slot(0x12);
        pci_slot(0x11);
        pci_slot(0x14);
        pci_slot(0x13);
        i430hx_init();
        piix3_init(7, 0x12, 0x11, 0x14, 0x13);
        pc87306_init();
        device_add(&intel_flash_bxt_device);
}

void at_acerm3a_init(void)
{
        at_ide_init();
	powermate_memregs_init();
        pci_init(PCI_CONFIG_TYPE_1);
        pci_slot(0xc);
        pci_slot(0xd);
        pci_slot(0xe);
        pci_slot(0xf);
        i430hx_init();
        piix3_init(7, 0xc, 0xd, 0xe, 0xf);
        fdc37c932fr_init();
        device_add(&intel_flash_bxb_device);
}

void at_acerv35n_init(void)
{
        at_ide_init();
	powermate_memregs_init();
        pci_init(PCI_CONFIG_TYPE_1);
        pci_slot(0x11);
        pci_slot(0x12);
        pci_slot(0x13);
        pci_slot(0x14);
        i430hx_init();
        piix3_init(7, 0x11, 0x12, 0x13, 0x14);
        fdc37c932fr_init();
        device_add(&intel_flash_bxb_device);
}

void at_p55t2p4_init(void)
{
        at_ide_init();
	memregs_init();
        pci_init(PCI_CONFIG_TYPE_1);
        pci_slot(12);
        pci_slot(11);
        pci_slot(10);
        pci_slot(9);
        i430hx_init();
        piix3_init(7, 12, 11, 10, 9);
        w83877f_init();
        device_add(&intel_flash_bxt_device);
}

#if 0
void at_i430vx_init(void)
{
        at_ide_init();
	memregs_init();
        pci_init(PCI_CONFIG_TYPE_1);
        pci_slot(0x11);
        pci_slot(0x12);
        pci_slot(0x13);
        pci_slot(0x14);
        i430vx_init();
        piix3_init(7, 17, 18, 20, 19);
        um8669f_init();
        device_add(&intel_flash_bxt_device);
}
#endif

void at_p55tvp4_init(void)
{
        at_ide_init();
	memregs_init();
        pci_init(PCI_CONFIG_TYPE_1);
        pci_slot(12);
        pci_slot(11);
        pci_slot(10);
        pci_slot(9);
        i430vx_init();
        piix3_init(7, 12, 11, 10, 9);
        w83877f_init();
        device_add(&intel_flash_bxt_device);
}

void at_i430vx_init(void)
{
        at_ide_init();
        pci_init(PCI_CONFIG_TYPE_1);
        pci_slot(0x11);
        pci_slot(0x12);
        pci_slot(0x13);
        pci_slot(0x14);
        i430vx_init();
        piix_init(7, 18, 17, 20, 19);
        um8669f_init();
        device_add(&intel_flash_bxt_device);
}

void at_p55va_init(void)
{
        at_ide_init();
        pci_init(PCI_CONFIG_TYPE_1);
        pci_slot(8);
        pci_slot(9);
        pci_slot(10);
        pci_slot(11);
        i430vx_init();
        piix3_init(7, 8, 9, 10, 11);
        fdc37c932fr_init();
        device_add(&intel_flash_bxt_device);
}

void at_i440fx_init(void)
{
        at_ide_init();
	memregs_init();
        pci_init(PCI_CONFIG_TYPE_1);
        pci_slot(0xe);
        pci_slot(0xd);
        pci_slot(0xc);
        pci_slot(0xb);
        i430vx_init();
        piix3_init(7, 0xe, 0xd, 0xc, 0xb);
	fdc37c665_init();
        device_add(&intel_flash_bxt_device);
}

void at_s1668_init(void)
{
        at_ide_init();
	memregs_init();
        pci_init(PCI_CONFIG_TYPE_1);
        pci_slot(0xe);
        pci_slot(0xd);
        pci_slot(0xc);
        pci_slot(0xb);
        i440fx_init();
        piix3_init(7, 0xe, 0xd, 0xc, 0xb);
	fdc37c665_init();
        device_add(&intel_flash_bxt_device);
}

void model_init(void)
{
        pclog("Initializing as %s\n", model_getname());
        AMSTRAD = AT = PCI = TANDY = 0;
        io_init();
        
	pci_reset_handler.pci_master_reset = NULL;
	pci_reset_handler.pci_set_reset = NULL;
	pci_reset_handler.super_io_reset = NULL;
	fdc_update_is_nsc(0);
        models[model].init();
        if (models[model].device)
                device_add(models[model].device);
}
