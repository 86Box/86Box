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
 * NOTES:	OpenAT wip for 286-class machine with open BIOS.
 *		PS2_M80-486 wip, pending receipt of TRM's for machine.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2017-2020 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/machine.h>


#if defined(DEV_BRANCH) && defined(USE_AMD_K5)
#if defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)
#define MACHINE_CPUS_PENTIUM_S5        {{ "Intel", cpus_PentiumS5},  {"IDT", cpus_WinChip},     {"AMD",   cpus_K5},      {"",      NULL},         {"",      NULL}}
#define MACHINE_CPUS_PENTIUM_S73V      {{ "Intel", cpus_Pentium3V},  {"IDT", cpus_WinChip},     {"AMD",   cpus_K5},      {"Cyrix", cpus_6x863V},  {"",      NULL}}
#define MACHINE_CPUS_PENTIUM_S73VCH    {{ "Intel", cpus_Pentium3V},  {"AMD", cpus_K5},          {"",      NULL},         {"",      NULL},         {"",      NULL}}
#define MACHINE_CPUS_PENTIUM_S7        {{ "Intel", cpus_Pentium},    {"IDT", cpus_WinChip},     {"AMD",   cpus_K56},     {"Cyrix", cpus_6x86},    {"",      NULL}}
#define MACHINE_CPUS_PENTIUM_SS7       {{ "Intel", cpus_Pentium},    {"IDT", cpus_WinChip_SS7}, {"AMD",   cpus_K56_SS7}, {"Cyrix", cpus_6x86SS7}, {"",      NULL}}
#else
#define MACHINE_CPUS_PENTIUM_S5        {{ "Intel", cpus_PentiumS5},  {"IDT", cpus_WinChip},     {"AMD",   cpus_K5},      {"",      NULL},         {"",      NULL}}
#define MACHINE_CPUS_PENTIUM_S73V      {{ "Intel", cpus_Pentium3V},  {"IDT", cpus_WinChip},     {"AMD",   cpus_K5},      {"",      NULL},         {"",      NULL}}
#define MACHINE_CPUS_PENTIUM_S73VCH    {{ "Intel", cpus_Pentium3V},  {"AMD", cpus_K5},          {"",      NULL},         {"",      NULL},         {"",      NULL}}
#define MACHINE_CPUS_PENTIUM_S7        {{ "Intel", cpus_Pentium},    {"IDT", cpus_WinChip},     {"AMD",   cpus_K56},     {"",      NULL},         {"",      NULL}}
#define MACHINE_CPUS_PENTIUM_SS7       {{ "Intel", cpus_Pentium},    {"IDT", cpus_WinChip_SS7}, {"AMD",   cpus_K56_SS7}, {"",      NULL},         {"",      NULL}}
#endif
#else
#if defined(DEV_BRANCH) && defined(USE_CYRIX_6X86)
#define MACHINE_CPUS_PENTIUM_S5        {{ "Intel", cpus_PentiumS5},  {"IDT", cpus_WinChip},     {"",      NULL},         {"",      NULL},         {"",      NULL}}
#define MACHINE_CPUS_PENTIUM_S73V      {{ "Intel", cpus_Pentium3V},  {"IDT", cpus_WinChip},     {"Cyrix", cpus_6x863V},  {"",      NULL},         {"",      NULL}}
#define MACHINE_CPUS_PENTIUM_S73VCH    {{ "Intel", cpus_Pentium3V},  {"",    NULL     },        {"",      NULL},         {"",      NULL},         {"",      NULL}}
#define MACHINE_CPUS_PENTIUM_S7        {{ "Intel", cpus_Pentium},    {"IDT", cpus_WinChip},     {"AMD",   cpus_K56},     {"Cyrix", cpus_6x86},    {"",      NULL}}
#define MACHINE_CPUS_PENTIUM_SS7       {{ "Intel", cpus_Pentium},    {"IDT", cpus_WinChip_SS7}, {"AMD",   cpus_K56_SS7}, {"Cyrix", cpus_6x86SS7}, {"",      NULL}}
#else
#define MACHINE_CPUS_PENTIUM_S5        {{ "Intel", cpus_PentiumS5},  {"IDT", cpus_WinChip},     {"",      NULL},         {"",      NULL},         {"",      NULL}}
#define MACHINE_CPUS_PENTIUM_S73V      {{ "Intel", cpus_Pentium3V},  {"IDT", cpus_WinChip},     {"",      NULL},         {"",      NULL},         {"",      NULL}}
#define MACHINE_CPUS_PENTIUM_S73VCH    {{ "Intel", cpus_Pentium3V},  {"",    NULL     },        {"",      NULL},         {"",      NULL},         {"",      NULL}}
#define MACHINE_CPUS_PENTIUM_S7        {{ "Intel", cpus_Pentium},    {"IDT", cpus_WinChip},     {"AMD",   cpus_K56},     {"",      NULL},         {"",      NULL}}
#define MACHINE_CPUS_PENTIUM_S7_INTEL  {{"Intel", cpus_Pentium},    {"", NULL},     {"",   NULL},     {"", NULL},    {"",      NULL}}
#define MACHINE_CPUS_PENTIUM_SS7       {{ "Intel", cpus_Pentium},    {"IDT", cpus_WinChip_SS7}, {"AMD",   cpus_K56_SS7}, {"",      NULL},         {"",      NULL}}
#endif
#endif


const machine_type_t machine_types[] = {
    { "None",		MACHINE_TYPE_NONE	},
    { "8088",		MACHINE_TYPE_8088	},
    { "8086",		MACHINE_TYPE_8086	},
    { "80286",		MACHINE_TYPE_286	},
    { "i386SX",		MACHINE_TYPE_386SX	},
    { "i386DX",		MACHINE_TYPE_386DX	},
    { "i486",		MACHINE_TYPE_486	},
    { "Socket 4",	MACHINE_TYPE_SOCKET4	},
    { "Socket 5",	MACHINE_TYPE_SOCKET5	},
    { "Socket 7-3V",	MACHINE_TYPE_SOCKET7_3V	},
    { "Socket 7",	MACHINE_TYPE_SOCKET7	},
    { "Super Socket 7",	MACHINE_TYPE_SOCKETS7	},
    { "Socket 8",	MACHINE_TYPE_SOCKET8	},
    { "Slot 1",		MACHINE_TYPE_SLOT1	},
    { "Slot 2",		MACHINE_TYPE_SLOT2	},
    { "Socket 370",	MACHINE_TYPE_SOCKET370	},
    { "Miscellaneous", MACHINE_TYPE_MISC    },
    { "Miscellaneous",	MACHINE_TYPE_MISC	}
};


const machine_t machines[] = {
    /* 8088 Machines */
    { "[8088] IBM PC (1981)",			"ibmpc",		MACHINE_TYPE_8088,		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC,									 16,   64,  16,   0,		      machine_pc_init, NULL			},
    { "[8088] IBM PC (1982)",			"ibmpc82",		MACHINE_TYPE_8088,		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC,									256,  256, 256,   0,		    machine_pc82_init, NULL			},
    { "[8088] IBM PCjr",			"ibmpcjr",		MACHINE_TYPE_8088,		{{"Intel",      cpus_pcjr},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC | MACHINE_VIDEO_FIXED,						128,  640, 128,   0,		    machine_pcjr_init, pcjr_get_device		},
    { "[8088] IBM XT (1982)",			"ibmxt",		MACHINE_TYPE_8088,		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC,									 64,  256,  64,   0,		      machine_xt_init, NULL			},
    { "[8088] IBM XT (1986)",			"ibmxt86",		MACHINE_TYPE_8088,		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC,									256,  640,  64,   0,		    machine_xt86_init, NULL			},
    { "[8088] American XT Computer",		"americxt",		MACHINE_TYPE_8088,		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC,									 64,  640,  64,   0,	     machine_xt_americxt_init, NULL			},
    { "[8088] AMI XT clone",			"amixt",		MACHINE_TYPE_8088,		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC,									 64,  640,  64,   0,		machine_xt_amixt_init, NULL			},
    { "[8088] Tandy 1000",			"tandy",		MACHINE_TYPE_8088,		{{"Intel",      cpus_europc}, {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC | MACHINE_VIDEO_FIXED,						128,  640, 128,   0,		   machine_tandy_init, tandy1k_get_device	},
    { "[8088] Tandy 1000 HX",			"tandy1000hx",		MACHINE_TYPE_8088,		{{"Intel",      cpus_europc}, {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC | MACHINE_VIDEO_FIXED,						256,  640, 128,   0,	     machine_tandy1000hx_init, tandy1k_hx_get_device	},
    { "[8088] Compaq Portable",			"portable",		MACHINE_TYPE_8088,		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC | MACHINE_VIDEO,							128,  640, 128,   0,  machine_xt_compaq_portable_init, NULL			},
    { "[8088] Generic XT clone",		"genxt",		MACHINE_TYPE_8088,		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC,									 64,  640,  64,   0,		   machine_genxt_init, NULL			},
    { "[8088] DTK XT clone",			"dtk",			MACHINE_TYPE_8088,		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC,									 64,  640,  64,   0,		  machine_xt_dtk_init, NULL			},
    { "[8088] Juko XT clone",			"jukopc",		MACHINE_TYPE_8088,		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC,									 64,  640,  64,   0,	       machine_xt_jukopc_init, NULL			},
    { "[8088] OpenXT",				"open_xt",		MACHINE_TYPE_8088,		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC,									 64,  640,  64,   0,	      machine_xt_open_xt_init, NULL			},
    { "[8088] Phoenix XT clone",		"pxxt",			MACHINE_TYPE_8088,		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC,									 64,  640,  64,   0,		 machine_xt_pxxt_init, NULL			},
    { "[8088] Schneider EuroPC",		"europc",		MACHINE_TYPE_8088,		{{"Siemens",	cpus_europc}, {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC | MACHINE_XTA | MACHINE_MOUSE,					512,  640, 128,  15,		  machine_europc_init, NULL			},
    { "[8088] Toshiba T1000",			"t1000",		MACHINE_TYPE_8088,		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC | MACHINE_VIDEO,							512, 1280, 768,  63,		machine_xt_t1000_init, t1000_get_device		},
#if defined(DEV_BRANCH) && defined(USE_LASERXT)
    { "[8088] VTech Laser Turbo XT",		"ltxt",			MACHINE_TYPE_8088,		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC,									256,  640, 256,   0,	      machine_xt_laserxt_init, NULL			},
#endif
    { "[8088] Xi8088",				"xi8088",		MACHINE_TYPE_8088,		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT | MACHINE_BUS_PS2,						 64, 1024, 128, 127,	       machine_xt_xi8088_init, xi8088_get_device	},
    { "[8088] Zenith Data SupersPort",		"zdsupers",		MACHINE_TYPE_8088,		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC,									128,  640, 128,   0,	       machine_xt_zenith_init, NULL			},
    
    /* 8086 Machines */
    { "[8086] Amstrad PC1512",			"pc1512",		MACHINE_TYPE_8086,		{{"Intel",      cpus_pc1512}, {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC | MACHINE_VIDEO_FIXED | MACHINE_MOUSE,				512,  640, 128,  63,		  machine_pc1512_init, pc1512_get_device	},
    { "[8086] Amstrad PC1640",			"pc1640",		MACHINE_TYPE_8086,		{{"Intel",      cpus_8086},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC | MACHINE_VIDEO | MACHINE_MOUSE,					640,  640,   0,  63,		  machine_pc1640_init, pc1640_get_device	},
    { "[8086] Amstrad PC2086",			"pc2086",		MACHINE_TYPE_8086,		{{"Intel",      cpus_8086},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC | MACHINE_VIDEO_FIXED | MACHINE_MOUSE,				640,  640,   0,  63,		  machine_pc2086_init, pc2086_get_device	},
    { "[8086] Amstrad PC3086",			"pc3086",		MACHINE_TYPE_8086,		{{"Intel",      cpus_8086},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC | MACHINE_VIDEO_FIXED | MACHINE_MOUSE,				640,  640,   0,  63,		  machine_pc3086_init, pc3086_get_device	},
    { "[8086] Amstrad PC20(0)",			"pc200",		MACHINE_TYPE_8086,		{{"Intel",      cpus_8086},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC | MACHINE_VIDEO | MACHINE_MOUSE | MACHINE_NONMI,			512,  640, 128,  63,		   machine_pc200_init, pc200_get_device		},
    { "[8086] Amstrad PPC512/640",		"ppc512",		MACHINE_TYPE_8086,		{{"Intel",      cpus_8086},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC | MACHINE_VIDEO | MACHINE_MOUSE | MACHINE_NONMI,			512,  640, 128,  63,		  machine_ppc512_init, ppc512_get_device	},
    { "[8086] Compaq Deskpro",			"deskpro",		MACHINE_TYPE_8086,		{{"Intel",      cpus_8086},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC,									128,  640, 128,   0,   machine_xt_compaq_deskpro_init, NULL			},
    { "[8086] Olivetti M24",			"olivetti_m24",		MACHINE_TYPE_8086,		{{"Intel",      cpus_8086},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC | MACHINE_VIDEO_FIXED | MACHINE_MOUSE,				128,  640, 128,   0,		  machine_olim24_init, m24_get_device		},
    { "[8086] Schetmash Iskra-3104",		"iskra3104",		MACHINE_TYPE_8086,		{{"Intel",      cpus_8086},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC,									128,  640, 128,   0,	    machine_xt_iskra3104_init, NULL			},
    { "[8086] Tandy 1000 SL/2",			"tandy1000sl2",		MACHINE_TYPE_8086,		{{"Intel",      cpus_8086},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC | MACHINE_VIDEO_FIXED,						512,  768, 128,   0,	    machine_tandy1000sl2_init, tandy1k_sl_get_device	},
    { "[8086] Toshiba T1200",			"t1200",		MACHINE_TYPE_8086,		{{"Intel",      cpus_8086},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC | MACHINE_VIDEO,						       1024, 2048,1024,  63,		machine_xt_t1200_init, t1200_get_device		},

#if defined(DEV_BRANCH) && defined(USE_LASERXT)
    { "[8086] VTech Laser XT3",			"lxt3",			MACHINE_TYPE_8086,		{{"Intel",      cpus_8086},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC,									256,  640, 256,   0,		 machine_xt_lxt3_init, NULL			},
#endif

    /* 286 XT machines */
#if defined(DEV_BRANCH) && defined(USE_HEDAKA)
    { "[Citygate D30 XT] Hedaka HED-919",	"hed919",		MACHINE_TYPE_286,		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PC,									 64, 1024,  64,   0,	       machine_xt_hed919_init, NULL			},
#endif

    /* 286 AT machines */
    { "[ISA] IBM AT",				"ibmat",		MACHINE_TYPE_286,		{{"",      cpus_ibmat},       {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT,									256,15872, 128,  63,		  machine_at_ibm_init, NULL			},
    { "[ISA] AMI IBM AT",			"ibmatami",		MACHINE_TYPE_286,		{{"",      cpus_ibmat},       {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT,									256,15872, 128,  63,	     machine_at_ibmatami_init, NULL			},
    { "[ISA] Quadtel IBM AT",			"ibmatquadtel",		MACHINE_TYPE_286,		{{"",      cpus_ibmat},       {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT,									256,15872, 128,  63,	 machine_at_ibmatquadtel_init, NULL			},
    { "[ISA] Phoenix IBM AT",			"ibmatpx",		MACHINE_TYPE_286,		{{"",      cpus_ibmat},       {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT,									256,15872, 128,  63,	      machine_at_ibmatpx_init, NULL			},
    { "[ISA] IBM PS/1 model 2011",		"ibmps1es",		MACHINE_TYPE_286,		{{"",      cpus_ps1_m2011},   {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT | MACHINE_BUS_PS2 | MACHINE_XTA | MACHINE_VIDEO_FIXED,		512,16384, 512,  63,	       machine_ps1_m2011_init, NULL			},
    { "[ISA] IBM PS/2 model 30-286",		"ibmps2_m30_286",	MACHINE_TYPE_286,		{{"Intel", cpus_ps2_m30_286}, {"IBM",cpus_IBM486SLC},{"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT | MACHINE_BUS_PS2 | MACHINE_XTA | MACHINE_VIDEO_FIXED,		  1,   16,   1, 127,	     machine_ps2_m30_286_init, NULL			},
    { "[ISA] IBM XT Model 286",			"ibmxt286",		MACHINE_TYPE_286,		{{"",      cpus_ibmxt286},    {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT,									256,15872, 128, 127,	     machine_at_ibmxt286_init, NULL			},
    { "[ISA] Commodore PC 30 III",		"cmdpc30",		MACHINE_TYPE_286,		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT,									640,16384, 128, 127,		machine_at_cmdpc_init, NULL			},
    { "[ISA] Compaq Portable II",		"portableii",		MACHINE_TYPE_286,		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT,									640,16384, 128, 127,	   machine_at_portableii_init, NULL			},
    { "[ISA] Compaq Portable III",		"portableiii",		MACHINE_TYPE_286,		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT | MACHINE_VIDEO,							640,16384, 128, 127,	  machine_at_portableiii_init, at_cpqiii_get_device	},
    { "[NEAT] AMI 286 clone",			"ami286",		MACHINE_TYPE_286,		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT,									512, 8192, 128, 127,	     machine_at_neat_ami_init, NULL			},
    { "[NEAT] Phoenix 286 clone",		"px286",		MACHINE_TYPE_286,		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT,									512,16384, 128, 127,	        machine_at_px286_init, NULL			},
    { "[SCAT] Award 286 clone",			"award286",		MACHINE_TYPE_286,		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT,									512,16384, 128, 127,	     machine_at_award286_init, NULL			},
    { "[SCAT] GW-286CT GEAR",			"gw286ct",		MACHINE_TYPE_286,		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT,									512,16384, 128, 127,	      machine_at_gw286ct_init, NULL			},
    { "[SCAT] Goldstar GDC-212M",		"gdc212m",		MACHINE_TYPE_286,		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT | MACHINE_IDE | MACHINE_BUS_PS2,					512, 4096, 512, 127,	      machine_at_gdc212m_init, NULL			},
    { "[SCAT] Hyundai Super-286TR",		"super286tr",		MACHINE_TYPE_286,		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT,									512,16384, 128, 127,	   machine_at_super286tr_init, NULL			},
    { "[SCAT] Samsung SPC-4200P",		"spc4200p",		MACHINE_TYPE_286,		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT | MACHINE_BUS_PS2,						512, 2048, 128, 127,	     machine_at_spc4200p_init, NULL			},
    { "[SCAT] Samsung SPC-4216P",		"spc4216p",		MACHINE_TYPE_286,		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT | MACHINE_BUS_PS2,						  1,    5,   1, 127,	     machine_at_spc4216p_init, NULL			},
    { "[SCAT] Samsung Deskmaster 286",		"deskmaster286",	MACHINE_TYPE_286,		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT,									512,16384, 128, 127,	machine_at_deskmaster286_init, NULL			},
    { "[GC103] Quadtel 286 clone",		"quadt286",		MACHINE_TYPE_286,		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT,									512,16384, 128, 127,	     machine_at_quadt286_init, NULL			},
    { "[GC103] Trigem 286M",			"tg286m",		MACHINE_TYPE_286,		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT | MACHINE_IDE,						  	512, 8192, 128, 127,	       machine_at_tg286m_init, NULL			},
    { "[ISA] MR 286 clone",			"mr286",		MACHINE_TYPE_286,		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT | MACHINE_IDE,							512,16384, 128, 127,	        machine_at_mr286_init, NULL			},
#if defined(DEV_BRANCH) && defined(USE_SIEMENS)
    { "[ISA] Siemens PCD-2L",			"siemens",		MACHINE_TYPE_286,		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT,									256,15872, 128,  63,	      machine_at_siemens_init, NULL			},
#endif
#if defined(DEV_BRANCH) && defined(USE_OPEN_AT)
    { "[ISA] OpenAT",				"open_at",		MACHINE_TYPE_286,		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT,									256,15872, 128,  63,	      machine_at_open_at_init, NULL			},
#endif
    { "[ISA] Toshiba T3100e",			"t3100e",		MACHINE_TYPE_286,		{{"",      cpus_286},         {"",    NULL},	     {"",      NULL},	     {"",      NULL},	  {"",      NULL}}, MACHINE_AT | MACHINE_IDE | MACHINE_VIDEO_FIXED,			       1024, 5120, 256,  63,	       machine_at_t3100e_init, NULL			},

    /* 286 machines that utilize the MCA bus */
    { "[MCA] IBM PS/2 model 50",		"ibmps2_m50",		MACHINE_TYPE_286,		{{"Intel", cpus_ps2_m30_286}, {"IBM",cpus_IBM486SLC},{"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_MCA | MACHINE_BUS_PS2 | MACHINE_VIDEO,				  1,   10,   1,  63,	    machine_ps2_model_50_init, NULL			},

    /* 386SX machines */
    { "[ISA] IBM PS/1 model 2121",		"ibmps1_2121",		MACHINE_TYPE_386SX,		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, MACHINE_AT | MACHINE_BUS_PS2 | MACHINE_IDE | MACHINE_VIDEO_FIXED,		  2,    6,   1,  63,	       machine_ps1_m2121_init, NULL			},
    { "[ISA] IBM PS/1 m.2121+ISA",		"ibmps1_2121_isa",	MACHINE_TYPE_386SX,		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, MACHINE_AT | MACHINE_BUS_PS2 | MACHINE_IDE | MACHINE_VIDEO,			  2,    6,   1,  63,	       machine_ps1_m2121_init, NULL			},
    { "[Intel 82335] Shuttle 386SX",		"shuttle386sx",		MACHINE_TYPE_386SX,		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, MACHINE_AT,									512, 8192, 128, 127,	 machine_at_shuttle386sx_init, NULL			},
    { "[Intel 82335] ADI 386SX",		"adi386sx",		MACHINE_TYPE_386SX,		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, MACHINE_AT,									512, 8192, 128, 127,	     machine_at_adi386sx_init, NULL			},
    { "[HT18] AMA-932J",			"ama932j",		MACHINE_TYPE_386SX,		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, MACHINE_AT | MACHINE_IDE | MACHINE_VIDEO,					512, 8192, 128, 127,          machine_at_ama932j_init, at_ama932j_get_device 	},
    { "[WD76C10] Amstrad MegaPC",		"megapc",		MACHINE_TYPE_386SX,		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, MACHINE_AT | MACHINE_BUS_PS2 | MACHINE_IDE | MACHINE_VIDEO,			  1,   32,   1, 127,	      machine_at_wd76c10_init, NULL			},
    { "[SCAMP] Commodore SL386SX",		"cbm_sl386sx25",	MACHINE_TYPE_386SX,		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, MACHINE_AT | MACHINE_BUS_PS2 | MACHINE_IDE | MACHINE_VIDEO,		       1024, 8192, 512, 127,machine_at_commodore_sl386sx_init, at_commodore_sl386sx_get_device	},  
    { "[NEAT] DTK 386SX clone",			"dtk386",		MACHINE_TYPE_386SX,		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, MACHINE_AT,									512, 8192, 128, 127,		 machine_at_neat_init, NULL			},
    { "[SCAT] KMX-C-02",			"kmxc02",		MACHINE_TYPE_386SX,		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, MACHINE_AT,									512,16384, 512, 127,	       machine_at_kmxc02_init, NULL			},
    { "[OPTi 291] DTK PPM-3333P",		"awardsx",		MACHINE_TYPE_386SX,		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, MACHINE_AT,									  1,   16,   1, 127,	      machine_at_awardsx_init, NULL			},
#if defined(DEV_BRANCH) && defined(USE_M6117)
    { "[ALi M6117D] Acrosser AR-B1375",		"arb1375",		MACHINE_TYPE_386SX,		{{"ALi", cpus_ALiM6117},      {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT | MACHINE_BUS_PS2 | MACHINE_IDE,					  1,   32,   1, 127,	      machine_at_arb1375_init, NULL			},
    { "[ALi M6117D] Acrosser PJ-A511M",		"pja511m",		MACHINE_TYPE_386SX,		{{"ALi", cpus_ALiM6117},      {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT | MACHINE_BUS_PS2 | MACHINE_IDE,					  1,   32,   1, 127,	      machine_at_pja511m_init, NULL			},
#endif
  
    /* 386SX machines which utilize the MCA bus */
    { "[MCA] IBM PS/2 model 55SX",		"ibmps2_m55sx",		MACHINE_TYPE_386SX,		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"IBM",cpus_IBM486SLC},{"",    NULL}}, MACHINE_MCA | MACHINE_BUS_PS2 | MACHINE_VIDEO,				  1,    8,   1,  63,	  machine_ps2_model_55sx_init, NULL			},

    /* 386DX machines */
    { "[ACC 2168] AMI 386DX clone",		"acc386",		MACHINE_TYPE_386DX,		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, MACHINE_AT,									  1,   16,   1, 127,	       machine_at_acc386_init, NULL			},
    { "[SiS 310] ASUS ISA-386C",		"asus386",		MACHINE_TYPE_386DX,		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, MACHINE_AT,									512,16384, 128, 127,	      machine_at_asus386_init, NULL			},
    { "[ISA] Compaq Portable III (386)",	"portableiii386",       MACHINE_TYPE_386DX,		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, MACHINE_AT | MACHINE_IDE | MACHINE_VIDEO,		  			  1,   14,   1, 127,   machine_at_portableiii386_init, at_cpqiii_get_device	},
    { "[ISA] Micronics 386 clone",		"micronics386",		MACHINE_TYPE_386DX,		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, MACHINE_AT,									512, 8192, 128, 127,	 machine_at_micronics386_init, NULL			},
    { "[C&T 386] ECS 386/32",			"ecs386",		MACHINE_TYPE_386DX,		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, MACHINE_AT,									  1,   16,   1, 127,	       machine_at_ecs386_init, NULL			},		
    { "[UMC 491] US Technologies 386",		"ustechnologies386",	MACHINE_TYPE_386DX,		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, MACHINE_AT,								          1,   16,   1, 127,machine_at_ustechnologies386_init, NULL			},

    /* 386DX machines which utilize the VLB bus */
    { "[OPTi 495] Award 386DX clone",		"award386dx",		MACHINE_TYPE_386DX,		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, MACHINE_VLB | MACHINE_IDE,							  1,   32,   1, 127,	      machine_at_opti495_init, NULL			},
    { "[OPTi 495] Dataexpert SX495 (386DX)",	"ami386dx",		MACHINE_TYPE_386DX,		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, MACHINE_VLB | MACHINE_IDE,							  1,   32,   1, 127,	  machine_at_opti495_ami_init, NULL			},
    { "[OPTi 495] MR 386DX clone",		"mr386dx",		MACHINE_TYPE_386DX,		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, MACHINE_VLB | MACHINE_IDE,							  1,   32,   1, 127,	   machine_at_opti495_mr_init, NULL			},

    /* 386DX machines which utilize the MCA bus */
    { "[MCA] IBM PS/2 model 70 (type 3)",	"ibmps2_m70_type3",	MACHINE_TYPE_386DX,		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"IBM",cpus_IBM486BL},{"",     NULL}}, MACHINE_MCA | MACHINE_BUS_PS2 | MACHINE_VIDEO,				  2,   16,   2,  63,  machine_ps2_model_70_type3_init, NULL			},
    { "[MCA] IBM PS/2 model 80",		"ibmps2_m80",		MACHINE_TYPE_386DX,		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"IBM",cpus_IBM486BL},{"",     NULL}}, MACHINE_MCA | MACHINE_BUS_PS2 | MACHINE_VIDEO,				  1,   12,   1,  63,	    machine_ps2_model_80_init, NULL			},

    /* 486 machines with just the ISA slot */
    { "[ACC 2168] Packard Bell PB410A",		"pb410a",		MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_AT | MACHINE_BUS_PS2 | MACHINE_IDE | MACHINE_VIDEO,	 	 	  4,   36,   1, 127,	       machine_at_pb410a_init, NULL			},

    /* 486 machines */
    { "[OPTi 283] RYC Leopard LX",		"rycleopardlx",		MACHINE_TYPE_486,		{{"IBM", cpus_IBM486SLC},     {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AT | MACHINE_IDE,							  1,   16,   1, 127,     machine_at_rycleopardlx_init, NULL			},
    { "[OPTi 495] Award 486 clone",		"award486",		MACHINE_TYPE_486,		{{"Intel", cpus_i486S1},      {"AMD", cpus_Am486S1}, {"Cyrix", cpus_Cx486S1},{"",      NULL},     {"",      NULL}}, MACHINE_VLB | MACHINE_IDE,							  1,   32,   1, 127,	      machine_at_opti495_init, NULL			},
    { "[OPTi 495] MR 486 clone",		"mr486",		MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_VLB | MACHINE_IDE,							  1,   32,   1, 127,	   machine_at_opti495_mr_init, NULL			},
    { "[OPTi 495] Dataexpert SX495 (486)",	"ami486",		MACHINE_TYPE_486,		{{"Intel", cpus_i486S1},      {"AMD", cpus_Am486S1}, {"Cyrix", cpus_Cx486S1},{"",      NULL},     {"",      NULL}}, MACHINE_VLB | MACHINE_IDE,							  1,   32,   1, 127,	  machine_at_opti495_ami_init, NULL			},
    { "[OPTi 895] Jetway J-403TG",		"403tg",		MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_VLB,								  1,   64,   1, 127,	        machine_at_403tg_init, NULL			},
    { "[OPTi 802G] IBM PC 330 (type 6571)",	"pc330_6571",		MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_VLB | MACHINE_BUS_PS2 | MACHINE_IDE,				  1,   64,   1, 127,	   machine_at_pc330_6571_init, NULL			},
    { "[CS4031] AMI 486 CS4031",		"cs4031",		MACHINE_TYPE_486,		{{"Intel", cpus_i486S1},      {"AMD", cpus_Am486S1}, {"Cyrix", cpus_Cx486S1},{"",      NULL},     {"",      NULL}}, MACHINE_VLB,								  1,   64,   1, 127,	       machine_at_cs4031_init, NULL			},
    { "[SiS 401] AMI 486 Clone",		"sis401",		MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_AT | MACHINE_IDE,							  1,   64,   1, 127,	       machine_at_sis401_init, NULL			},
    { "[SiS 461] IBM PS/ValuePoint 433DX/Si",		"valuepoint433",		MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_AT | MACHINE_BUS_PS2 | MACHINE_IDE,	1,   64,   1, 127,	       machine_at_valuepoint433_init, NULL			},
	{ "[SiS 471] ASUS VL/I-486SV2G (GX4)",	"vli486sv2g",		MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_VLB | MACHINE_IDE_DUAL,						  1,   64,   1, 127,	   machine_at_vli486sv2g_init, NULL			},
    { "[SiS 471] AMI 486 Clone",		"ami471",		MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_VLB | MACHINE_IDE,							  1,   64,   1, 127,	       machine_at_ami471_init, NULL			},
    { "[SiS 471] AMI WinBIOS 486 clone",	"win471",		MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_VLB | MACHINE_IDE,							  1,   64,   1, 127,	       machine_at_win471_init, NULL			},
    { "[SiS 471] AOpen Vi15G",			"vi15g",		MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_VLB | MACHINE_IDE,							  1,   64,   1, 127,	        machine_at_vi15g_init, NULL			},
    { "[SiS 471] DTK PKM-0038S E-2",		"dtk486",		MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_VLB | MACHINE_IDE,							  1,   64,   1, 127,	       machine_at_dtk486_init, NULL			},
    { "[SiS 471] Phoenix SiS 471",		"px471",		MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_VLB | MACHINE_IDE,							  1,  128,   1, 127,	        machine_at_px471_init, NULL			},
    { "[ALi M1429G] Acer A1G",			"acera1g",		MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_AT | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL | MACHINE_VIDEO,		  4,   36,   1, 127,	      machine_at_acera1g_init, at_acera1g_get_device	},
    { "[ALi M1429] Olystar LIL1429",		"ali1429",		MACHINE_TYPE_486,		{{"Intel", cpus_i486S1},      {"AMD", cpus_Am486S1}, {"Cyrix", cpus_Cx486S1},{"",      NULL},     {"",      NULL}}, MACHINE_VLB | MACHINE_IDE,							  1,   32,   1, 127,	      machine_at_ali1429_init, NULL			},
    { "[ALi M1429] AMI WinBIOS 486",		"win486",		MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_VLB | MACHINE_IDE,							  1,   32,   1, 127,	  machine_at_winbios1429_init, NULL			},
    { "[VLSI 82C480] IBM PS/1 model 2133",	"ibmps1_2133",		MACHINE_TYPE_486,		{{"Intel", cpus_i486S1},      {"AMD", cpus_Am486S1}, {"Cyrix", cpus_Cx486S1},{"",      NULL},     {"",      NULL}}, MACHINE_VLB | MACHINE_BUS_PS2 | MACHINE_IDE | MACHINE_NONMI | MACHINE_VIDEO,  2,   32,   1, 127,	       machine_ps1_m2133_init, ps1_m2133_get_device	},
#if defined(DEV_BRANCH) && defined(USE_VECT486VL)
	{ "[VLSI 82C480] HP Vectra 486VL",	"vect486vl",		MACHINE_TYPE_486,		{{"Intel", cpus_i486S1},      {"AMD", cpus_Am486S1}, {"Cyrix", cpus_Cx486S1},{"",      NULL},     {"",      NULL}}, MACHINE_AT | MACHINE_BUS_PS2 | MACHINE_IDE | MACHINE_VIDEO,  2,   64,   1, 127,	       machine_at_vect486vl_init, at_vect486vl_get_device	},
#endif
	{ "[VIA VT82C495] FIC 486-VC-HD",		"486vchd",		MACHINE_TYPE_486,		{{"Intel", cpus_i486S1},      {"AMD", cpus_Am486S1}, {"Cyrix", cpus_Cx486S1},{"",      NULL},     {"",      NULL}}, MACHINE_AT,									  1,   32,   1, 127,	      machine_at_486vchd_init, NULL			},

    /* 486 machines with utilize the MCA bus */
#if defined(DEV_BRANCH) && defined(USE_PS2M70T4)
    { "[MCA] IBM PS/2 model 70 (type 4)",	"ibmps2_m70_type4",	MACHINE_TYPE_486,		{{"Intel", cpus_i486S1},      {"AMD", cpus_Am486S1}, {"Cyrix", cpus_Cx486S1},{"",      NULL},     {"",      NULL}}, MACHINE_MCA | MACHINE_BUS_PS2 | MACHINE_VIDEO,				  2,   16,   2,  63,  machine_ps2_model_70_type4_init, NULL			},
#endif

    /* 486 machines which utilize the PCI bus */
    { "[i420EX] ASUS PVI-486AP4",		"486ap4",		MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_PCIV | MACHINE_IDE_DUAL,						  1,  128,   1, 127,	       machine_at_486ap4_init, NULL			},
    { "[i420ZX] ASUS PCI/I-486SP3G",		"486sp3g",		MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_IDE_DUAL,						  1,  128,   1, 127,	      machine_at_486sp3g_init, NULL			},
    { "[i420TX] Intel Classic/PCI",		"alfredo",		MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		  		  2,  128,   2, 127,	      machine_at_alfredo_init, NULL			},
    { "[SiS 496] Lucky Star LS-486E",		"ls486e",		MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_IDE_DUAL,						  1,  128,   1, 255,	       machine_at_ls486e_init, NULL			},
    { "[SiS 496] Rise Computer R418",		"r418",			MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_IDE_DUAL,						  1,  255,   1, 255,		 machine_at_r418_init, NULL			},
    { "[SiS 496] Soyo 4SA2",			"4sa2",			MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_IDE_DUAL,						  1,  255,   1, 255,		 machine_at_4sa2_init, NULL			},
	{ "[SiS 496] Micronics M4Li",			"m4li",			MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,						  1,  128,   1, 127,		 machine_at_m4li_init, NULL			},
    { "[SiS 496] Zida Tomato 4DPS",		"4dps",			MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_IDE_DUAL,						  1,  255,   1, 255,		 machine_at_4dps_init, NULL			},
#if defined(DEV_BRANCH) && defined(NO_SIO)
    { "[VIA VT82C496G] FIC VIP-IO2",		"486vipio2",		MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_PCIV | MACHINE_IDE_DUAL,						  1,  128,   1, 255,	    machine_at_486vipio2_init, NULL			},
#endif
#if defined(DEV_BRANCH) && defined(USE_M1489)
    { "[ALi M1489] ABIT AB-PB4",		"abpb4",		MACHINE_TYPE_486,		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_IDE_DUAL,						  1,  64,   1, 255,		machine_at_abpb4_init, NULL			},
#endif
#if defined(DEV_BRANCH) && defined(USE_STPC)
    { "[STPC Client] ITOX STAR",		"itoxstar",		MACHINE_TYPE_486,		{{"ST", cpus_STPCDX},         {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  128,   8, 255,	     machine_at_itoxstar_init, NULL			},
    { "[STPC Consumer-II] Acrosser AR-B1479",	"arb1479",		MACHINE_TYPE_486,		{{"ST", cpus_STPCDX2},        {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				 32,  160,   8, 255,	      machine_at_arb1479_init, NULL			},
    { "[STPC Elite] Advantech PCM-9340",	"pcm9340",		MACHINE_TYPE_486,		{{"ST", cpus_STPCDX2},        {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				 32,   96,   8, 255,	      machine_at_pcm9340_init, NULL			},
    { "[STPC Atlas] AAEON PCM-5330",		"pcm5330",		MACHINE_TYPE_486,		{{"ST", cpus_STPCDX2},        {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				 32,  128,  32, 255,	      machine_at_pcm5330_init, NULL			},
#endif

    /* Socket 4 machines */
    /* OPTi 596/597 */
    { "[OPTi 597] AMI Excalibur VLB",		"excalibur",		MACHINE_TYPE_SOCKET4,		{{"Intel", cpus_Pentium5V},   {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_VLB | MACHINE_IDE,							  2,   64,   2, 127,	    machine_at_excalibur_init, NULL			},
        
    /* 430LX */
    { "[i430LX] IBM Ambra DP60 PCI",		"ambradp60",		MACHINE_TYPE_SOCKET4,		{{"Intel", cpus_Pentium5V},   {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  2,  128,   2, 127,	    machine_at_ambradp60_init, NULL			},
    { "[i430LX] IBM PS/ValuePoint P60",		"valuepointp60",	MACHINE_TYPE_SOCKET4,		{{"Intel", cpus_Pentium5V},   {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		 		  2,  128,   2, 127,	machine_at_valuepointp60_init, NULL			},
#if defined(DEV_BRANCH) && defined(USE_DELLS4)
    { "[i430LX] Dell Dimension XPS P60",		"dellxp60",		MACHINE_TYPE_SOCKET4,		{{"Intel", cpus_Pentium5V},   {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  2,  128,   2, 127,	       machine_at_dellxp60_init, NULL			},
    { "[i430LX] Dell OptiPlex 560/L",		"opti560l",		MACHINE_TYPE_SOCKET4,		{{"Intel", cpus_Pentium5V},   {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  2,  128,   2, 127,	       machine_at_opti560l_init, NULL			},
#endif
    { "[i430LX] Intel Premiere/PCI",		"revenge",		MACHINE_TYPE_SOCKET4,		{{"Intel", cpus_Pentium5V},   {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  2,  128,   2, 127,	       machine_at_batman_init, NULL			},
    { "[i430LX] ASUS P/I-P5MP3",		"p5mp3",		MACHINE_TYPE_SOCKET4,		{{"Intel", cpus_Pentium5V},   {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE,		 		  2,  192,   2, 127,	        machine_at_p5mp3_init, NULL			},
    { "[i430LX] Micro Star 586MC1",		"586mc1",		MACHINE_TYPE_SOCKET4,		{{"Intel", cpus_Pentium5V},   {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		 		  2,  128,   2, 127,	       machine_at_586mc1_init, NULL			},
    { "[i430LX] Packard Bell PB520R",		"pb520r",		MACHINE_TYPE_SOCKET4,		{{"Intel", cpus_Pentium5V},   {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL | MACHINE_VIDEO,		  8,  136,   2, 127,	       machine_at_pb520r_init, at_pb520r_get_device	},

    /* Socket 5 machines */
    /* 430NX */
    { "[i430NX] Intel Premiere/PCI II",		"plato",		MACHINE_TYPE_SOCKET5,		MACHINE_CPUS_PENTIUM_S5,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  2,  128,   2, 127,		machine_at_plato_init, NULL			},
    { "[i430NX] IBM Ambra DP90 PCI",		"ambradp90",		MACHINE_TYPE_SOCKET5,		MACHINE_CPUS_PENTIUM_S5,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  2,  128,   2, 127,	    machine_at_ambradp90_init, NULL			},
    { "[i430NX] Gigabyte GA-586IP",		"430nx",		MACHINE_TYPE_SOCKET5,		MACHINE_CPUS_PENTIUM_S5,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  2,  128,   2, 127,		machine_at_430nx_init, NULL			},

    /* 430FX */
    { "[i430FX] Acer V30",			"acerv30",		MACHINE_TYPE_SOCKET5,		MACHINE_CPUS_PENTIUM_S5,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  128,   8, 127,	      machine_at_acerv30_init, NULL			},
    { "[i430FX] AMI Apollo",			"apollo",		MACHINE_TYPE_SOCKET5,		MACHINE_CPUS_PENTIUM_S5,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  128,   8, 127,	       machine_at_apollo_init, NULL			},
#if defined(DEV_BRANCH) && defined(USE_VECTRA54)
    { "[i430FX] HP Vectra VL 5 Series 4",	"vectra54",		MACHINE_TYPE_SOCKET5,		MACHINE_CPUS_PENTIUM_S5,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  128,   8, 511,	     machine_at_vectra54_init, NULL			},
#endif
    { "[i430FX] Intel Advanced/ZP",		"zappa",		MACHINE_TYPE_SOCKET5,		MACHINE_CPUS_PENTIUM_S5,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  128,   8, 127,		machine_at_zappa_init, NULL			},
    { "[i430FX] NEC PowerMate V",  		"powermate_v",		MACHINE_TYPE_SOCKET5,		MACHINE_CPUS_PENTIUM_S5,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  128,   8, 127,	  machine_at_powermate_v_init, NULL			},
    { "[i430FX] PC Partner MB500N",		"mb500n",		MACHINE_TYPE_SOCKET5,		MACHINE_CPUS_PENTIUM_S5,											    MACHINE_PCI | MACHINE_IDE_DUAL,						  8,  128,   8, 127,	       machine_at_mb500n_init, NULL			},

    /* Socket 7 machines */
    /* 430FX */
    { "[i430FX] ASUS P/I-P54TP4XE",		"p54tp4xe",		MACHINE_TYPE_SOCKET7_3V,	MACHINE_CPUS_PENTIUM_S73V,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  128,   8, 127,	     machine_at_p54tp4xe_init, NULL			},
    { "[i430FX] ASUS P/I-P54TP4XE (MR BIOS)",	"mr586",		MACHINE_TYPE_SOCKET7_3V,	MACHINE_CPUS_PENTIUM_S73V,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  128,   8, 127,	        machine_at_mr586_init, NULL			},
    { "[i430FX] Gateway 2000 Thor",		"gw2katx",		MACHINE_TYPE_SOCKET7_3V,	MACHINE_CPUS_PENTIUM_S73V,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL | MACHINE_VIDEO,		  8,  128,   8, 127,	      machine_at_gw2katx_init, NULL			},
    { "[i430FX] Intel Advanced/ATX",		"thor",			MACHINE_TYPE_SOCKET7_3V,	MACHINE_CPUS_PENTIUM_S73V,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL | MACHINE_VIDEO,		  8,  128,   8, 127,		 machine_at_thor_init, NULL			},
    { "[i430FX] Intel Advanced/ATX (MR BIOS)",	"mrthor",		MACHINE_TYPE_SOCKET7_3V,	MACHINE_CPUS_PENTIUM_S73V,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL | MACHINE_VIDEO,		  8,  128,   8, 127,	       machine_at_mrthor_init, NULL			},
    { "[i430FX] Intel Advanced/EV",		"endeavor",		MACHINE_TYPE_SOCKET7_3V,	MACHINE_CPUS_PENTIUM_S73V,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL | MACHINE_VIDEO,		  8,  128,   8, 127,	     machine_at_endeavor_init, at_endeavor_get_device	},
    { "[i430FX] Packard Bell PB640",		"pb640",		MACHINE_TYPE_SOCKET7_3V,	MACHINE_CPUS_PENTIUM_S73V,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL | MACHINE_VIDEO, 		  8,  128,   8, 127,		machine_at_pb640_init, at_pb640_get_device	},
    { "[i430FX] QDI Chariot",			"chariot",		MACHINE_TYPE_SOCKET7_3V,	MACHINE_CPUS_PENTIUM_S73VCH,											    MACHINE_PCI | MACHINE_IDE_DUAL,						  8,  128,   8, 127,	      machine_at_chariot_init, NULL			},

    /* 430HX */
    { "[i430HX] Acer M3A",			"acerm3a",		MACHINE_TYPE_SOCKET7_3V,	MACHINE_CPUS_PENTIUM_S73V,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  192,   8, 127,	      machine_at_acerm3a_init, NULL			},
    { "[i430HX] AOpen AP53",			"ap53",			MACHINE_TYPE_SOCKET7_3V,	MACHINE_CPUS_PENTIUM_S73V,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		 		  8,  512,   8, 127,		 machine_at_ap53_init, NULL			},
    { "[i430HX] Biostar MB-8500TUC",		"8500tuc",		MACHINE_TYPE_SOCKET7_3V,	MACHINE_CPUS_PENTIUM_S73V,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  512,   8, 127,	      machine_at_8500tuc_init, NULL			},
    { "[i430HX] SuperMicro Super P55T2S",	"p55t2s",		MACHINE_TYPE_SOCKET7_3V,	MACHINE_CPUS_PENTIUM_S73V,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  768,   8, 127,	       machine_at_p55t2s_init, NULL			},

    { "[i430HX] Acer V35N",			"acerv35n",		MACHINE_TYPE_SOCKET7,		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  192,   8, 127,	     machine_at_acerv35n_init, NULL			},
    { "[i430HX] ASUS P/I-P55T2P4",		"p55t2p4",		MACHINE_TYPE_SOCKET7,		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  256,   8, 127,	      machine_at_p55t2p4_init, NULL			},
    { "[i430HX] Micronics M7S-Hi",		"m7shi",		MACHINE_TYPE_SOCKET7,		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		 		  8,  128,   8, 511,	        machine_at_m7shi_init, NULL			},
    { "[i430HX] Intel TC430HX",			"tc430hx",		MACHINE_TYPE_SOCKET7,		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		  		  8,  128,   8, 255,	      machine_at_tc430hx_init, NULL			},
    { "[i430HX] Toshiba Equium 5200D",		"equium5200",		MACHINE_TYPE_SOCKET7,		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		  		  8,  192,   8, 127,	   machine_at_equium5200_init, NULL			},
    { "[i430HX] Sony Vaio PCV-240",		"pcv240",		MACHINE_TYPE_SOCKET7,		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		  		  8,  192,   8, 127,	       machine_at_pcv240_init, NULL			},
    { "[i430HX] ASUS P/I-P65UP5 (C-P55T2D)",	"p65up5_cp55t2d",	MACHINE_TYPE_SOCKET7,		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		  		  8,  512,   8, 127,   machine_at_p65up5_cp55t2d_init, NULL			},

    /* 430VX */
    { "[i430VX] ASUS P/I-P55TVP4",		"p55tvp4",		MACHINE_TYPE_SOCKET7,		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		 		  8,  128,   8, 127,	      machine_at_p55tvp4_init, NULL			},
    { "[i430VX] Shuttle HOT-557",		"430vx",		MACHINE_TYPE_SOCKET7,		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  128,   8, 127,	       machine_at_i430vx_init, NULL			},
    { "[i430VX] Epox P55-VA",			"p55va",		MACHINE_TYPE_SOCKET7,		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		  		  8,  128,   8, 127,		machine_at_p55va_init, NULL			},
    { "[i430VX] Gateway 2000 Tigereye",		"gw2kte",		MACHINE_TYPE_SOCKET7,		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		 		  8,  128,   8, 127,	       machine_at_gw2kte_init, NULL			},
    { "[i430VX] HP Brio 80xx",			"brio80xx",		MACHINE_TYPE_SOCKET7,		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		 		  8,  128,   8, 127,	     machine_at_brio80xx_init, NULL			},
    { "[i430VX] Biostar MB-8500TVX-A",		"8500tvxa",		MACHINE_TYPE_SOCKET7,		MACHINE_CPUS_PENTIUM_S7,		  	    								    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		 		  8,  128,   8, 127,	     machine_at_8500tvxa_init, NULL			},
    { "[i430VX] Compaq Presario 4500",		"presario4500",		MACHINE_TYPE_SOCKET7,		MACHINE_CPUS_PENTIUM_S7,		  	    								    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL | MACHINE_VIDEO,		  8,  128,   8, 127,	 machine_at_presario4500_init, NULL			},
    { "[i430VX] Packard Bell PB680",		"pb680",		MACHINE_TYPE_SOCKET7,		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		 		  8,  128,   8, 127,	        machine_at_pb680_init, NULL			},

    /* 430TX */
    { "[i430TX] ADLink NuPRO-592",		"nupro592",		MACHINE_TYPE_SOCKET7,		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  256,   8, 255,	     machine_at_nupro592_init, NULL			},
    { "[i430TX] ASUS TX97",			"tx97",			MACHINE_TYPE_SOCKET7,		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  256,   8, 255,	         machine_at_tx97_init, NULL			},
    { "[i430TX] Intel YM430TX",			"ym430tx",		MACHINE_TYPE_SOCKET7,		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  256,   8, 255,	      machine_at_ym430tx_init, NULL			},
    { "[i430TX] PC Partner MB540N",		"mb540n",		MACHINE_TYPE_SOCKET7,		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  256,   8, 255,	       machine_at_mb540n_init, NULL			},
    { "[i430TX] SuperMicro Super P5MMS98",	"p5mms98",		MACHINE_TYPE_SOCKET7,		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  256,   8, 255,	      machine_at_p5mms98_init, NULL			},

    /* Apollo VPX */
    { "[VIA VPX] FIC VA-502",			"ficva502",		MACHINE_TYPE_SOCKET7,		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		 		  8,  512,   8, 127,	     machine_at_ficva502_init, NULL			},

    /* Apollo VP3 */
    { "[VIA VP3] FIC PA-2012",			"ficpa2012",		MACHINE_TYPE_SOCKET7,		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		 		  8, 1024,   8, 127,	    machine_at_ficpa2012_init, NULL			},
  
    /* Super Socket 7 machines */
    /* Apollo MVP3 */
    { "[VIA MVP3] AOpen AX59 Pro",		"ax59pro",		MACHINE_TYPE_SOCKETS7,		MACHINE_CPUS_PENTIUM_SS7,											    MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8, 1024,   8, 255,	      machine_at_ax59pro_init, NULL			},
    { "[VIA MVP3] FIC VA-503+",			"ficva503p",		MACHINE_TYPE_SOCKETS7,		MACHINE_CPUS_PENTIUM_SS7,											    MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8, 1024,   8, 255,	         machine_at_mvp3_init, NULL			},
    { "[VIA MVP3] FIC VA-503A",			"ficva503a",		MACHINE_TYPE_SOCKETS7,		MACHINE_CPUS_PENTIUM_SS7,											    MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  768,   8, 255,	    machine_at_ficva503a_init, NULL			},

    /* Socket 8 machines */
    /* 440FX */
    { "[i440FX] ASUS P/I-P65UP5 (C-P6ND)",	"p65up5_cp6nd",		MACHINE_TYPE_SOCKET8,		{{"Intel", cpus_PentiumPro},  {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8, 1024,   8, 127,	 machine_at_p65up5_cp6nd_init, NULL			},
    { "[i440FX] Biostar MB-8600TTC",		"8600ttc",		MACHINE_TYPE_SOCKET8,		{{"Intel", cpus_PentiumPro},  {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8, 1024,   8, 127,	      machine_at_8500ttc_init, NULL			},
    { "[i440FX] Gigabyte GA-686NX",		"686nx",		MACHINE_TYPE_SOCKET8,		{{"Intel", cpus_PentiumPro},  {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  512,   8, 127,	        machine_at_686nx_init, NULL	                },
    { "[i440FX] Acer V60N",	        	"v60n",		        MACHINE_TYPE_SOCKET8,		{{"Intel", cpus_PentiumPro},  {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  512,   8, 127,	         machine_at_v60n_init, NULL			},
    { "[i440FX] Intel AP440FX",			"ap440fx",		MACHINE_TYPE_SOCKET8,		{{"Intel", cpus_PentiumPro},  {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  128,   8, 127,	      machine_at_ap440fx_init, NULL		        },
    { "[i440FX] Intel VS440FX",			"vs440fx",		MACHINE_TYPE_SOCKET8,		{{"Intel", cpus_PentiumPro},  {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  512,   8, 127,	      machine_at_vs440fx_init, NULL		        },
    { "[i440FX] Micronics M6Mi",		"m6mi",			MACHINE_TYPE_SOCKET8,		{{"Intel", cpus_PentiumPro},  {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  768,   8, 127,	         machine_at_m6mi_init, NULL			},
    { "[i440FX] PC Partner MB600N",		"mb600n",		MACHINE_TYPE_SOCKET8,		{{"Intel", cpus_PentiumPro},  {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  512,   8, 127,	       machine_at_mb600n_init, NULL			},

    /* Slot 1 machines */
    /* 440FX */
    { "[i440FX] ASUS P/I-P65UP5 (C-PKND)",	"p65up5_cpknd",		MACHINE_TYPE_SLOT1,		{{"Intel", cpus_PentiumII66}, {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8, 1024,   8, 127,	 machine_at_p65up5_cpknd_init, NULL			},
    { "[i440FX] ASUS KN97",			"kn97",			MACHINE_TYPE_SLOT1,		{{"Intel", cpus_PentiumII66}, {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  768,   8, 127,	         machine_at_kn97_init, NULL			},

    /* 440LX */
    { "[i440LX] ABIT LX6",			"lx6",			MACHINE_TYPE_SLOT1,		{{"Intel", cpus_PentiumII66}, {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8, 1024,   8, 255,	          machine_at_lx6_init, NULL			},
    { "[i440LX] Micronics Spitfire",		"spitfire",		MACHINE_TYPE_SLOT1,		{{"Intel", cpus_PentiumII66}, {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8, 1024,   8, 255,	     machine_at_spitfire_init, NULL			},

    /* 440EX */
    { "[i440EX] QDI EXCELLENT II",		"p6i440e2",		MACHINE_TYPE_SLOT1,		{{"Intel", cpus_PentiumII66}, {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  512,   8, 255,	     machine_at_p6i440e2_init, NULL			},
    /* 440BX */
    { "[i440BX] ASUS P2B-LS",			"p2bls",		MACHINE_TYPE_SLOT1,		{{"Intel", cpus_PentiumII},   {"Intel/PGA370", cpus_Celeron},{"VIA", cpus_Cyrix3},{"", NULL},	  {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		 		  8, 1024,   8, 255,		machine_at_p2bls_init, NULL			},
    { "[i440BX] ASUS P3B-F",			"p3bf",			MACHINE_TYPE_SLOT1,		{{"Intel", cpus_PentiumII},   {"Intel/PGA370", cpus_Celeron},{"VIA", cpus_Cyrix3},{"", NULL},	  {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		 		  8, 1024,   8, 255,		 machine_at_p3bf_init, NULL			},
    { "[i440BX] ABIT BF6",			"bf6",			MACHINE_TYPE_SLOT1,		{{"Intel", cpus_PentiumII},   {"Intel/PGA370", cpus_Celeron},{"VIA", cpus_Cyrix3},{"", NULL},	  {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		  		  8,  768,   8, 255,		  machine_at_bf6_init, NULL			},
    { "[i440BX] AOpen AX6BC",			"ax6bc",		MACHINE_TYPE_SLOT1,		{{"Intel", cpus_PentiumII},   {"Intel/PGA370", cpus_Celeron},{"VIA", cpus_Cyrix3},{"", NULL},	  {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		  		  8,  768,   8, 255,		machine_at_ax6bc_init, NULL			},
    { "[i440BX] A-Trend ATC6310BXII",		"atc6310bxii",		MACHINE_TYPE_SLOT1,		{{"Intel", cpus_PentiumII},   {"Intel/PGA370", cpus_Celeron},{"VIA", cpus_Cyrix3},{"", NULL},	  {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		 		  8,  768,   8, 255,	  machine_at_atc6310bxii_init, NULL			},
    { "[i440BX] Tyan Tsunami ATX",		"tsunamiatx",		MACHINE_TYPE_SLOT1,		{{"Intel", cpus_PentiumII},   {"Intel/PGA370", cpus_Celeron},{"VIA", cpus_Cyrix3},{"", NULL},	  {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL | MACHINE_SOUND,	 	  8, 1024,   8, 255,	   machine_at_tsunamiatx_init, at_tsunamiatx_get_device	},
    { "[i440BX] SuperMicro Super P6SBA",	"p6sba",		MACHINE_TYPE_SLOT1,		{{"Intel", cpus_PentiumII},   {"Intel/PGA370", cpus_Celeron},{"VIA", cpus_Cyrix3},{"", NULL},	  {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		  		  8,  768,   8, 255,	        machine_at_p6sba_init, NULL			},

    /* 440GX */
    { "[i440GX] Freeway FW-6400GX",		"fw6400gx_s1",		MACHINE_TYPE_SLOT1,		{{"Intel", cpus_PentiumII},   {"Intel/PGA370", cpus_Celeron},{"VIA", cpus_Cyrix3},{"", NULL},	  {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				 16, 2032,  16, 511,	     machine_at_fw6400gx_init, NULL			},

    /* VIA Apollo Pro */
    { "[VIA Apollo Pro] FIC KA-6130",		"ficka6130",		MACHINE_TYPE_SLOT1,		{{"Intel", cpus_PentiumII},   {"Intel/PGA370", cpus_Celeron},{"VIA", cpus_Cyrix3},{"", NULL},	  {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,	 			  8,  512,   8, 255,	    machine_at_ficka6130_init, NULL			},

    /* Slot 2 machines(Including Slot 1/2 Hybrids) */
    /* 440GX */
    { "[i440GX] Gigabyte GA-6GXU",		"6gxu",			MACHINE_TYPE_SLOT2,		{{"Intel", cpus_Xeon},        {"",      NULL},	     {"",      NULL},	     {"",      NULL},	  {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		 		 16, 2048,  16, 511,	         machine_at_6gxu_init, NULL			},
    { "[i440GX] SuperMicro Super S2DGE",	"s2dge",		MACHINE_TYPE_SLOT2,		{{"Intel", cpus_Xeon},        {"",      NULL},	     {"",      NULL},	     {"",      NULL},	  {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		 		 16, 2048,  16, 511,	        machine_at_s2dge_init, NULL			},
    { "[i440GX] Freeway FW-6400GX",		"fw6400gx",		MACHINE_TYPE_SLOT2,		{{"Intel", cpus_Xeon},        {"",      NULL},	     {"",      NULL},	     {"",      NULL},	  {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				 16, 2032,  16, 511,	     machine_at_fw6400gx_init, NULL			},

    /* PGA370 machines */
    /* 440LX */
    { "[i440LX] SuperMicro Super 370SLM",	"s370slm",		MACHINE_TYPE_SOCKET370,		{{"Intel", cpus_Celeron},     {"VIA", cpus_Cyrix3},  {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		 		  8,  768,   8, 255,	      machine_at_s370slm_init, NULL			},

    /* 440BX */
    { "[i440BX] ASUS CUBX",			"cubx",			MACHINE_TYPE_SOCKET370,		{{"Intel", cpus_Celeron},     {"VIA", cpus_Cyrix3},  {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		 		  8, 1024,   8, 255,		 machine_at_cubx_init, NULL			},
    { "[i440BX] A-Trend ATC7020BXII",		"atc7020bxii",		MACHINE_TYPE_SOCKET370,		{{"Intel", cpus_Celeron},     {"VIA", cpus_Cyrix3},  {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		 		  8, 1024,   8, 255,	  machine_at_atc7020bxii_init, NULL			},
    { "[i440BX] AmazePC AM-BX133",		"ambx133",		MACHINE_TYPE_SOCKET370,		{{"Intel", cpus_Celeron},     {"VIA", cpus_Cyrix3},  {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		 		  8,  768,   8, 255,	      machine_at_ambx133_init, NULL			},
    { "[i440BX] AEWIN AW-O671R",		"awo671r",		MACHINE_TYPE_SOCKET370,		{{"Intel", cpus_Celeron},     {"VIA", cpus_Cyrix3},  {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		 		  8,  512,   8, 255,	      machine_at_awo671r_init, NULL			},
    { "[i440BX] TYAN Trinity 371",			"trinity371",			MACHINE_TYPE_SOCKET370,		{{"Intel", cpus_Celeron},     {"",      NULL},  {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		 		  8, 768,   8, 255,		 machine_at_trinity371_init, NULL			},

    /* 440ZX */
    { "[i440ZX] Soltek SL-63A1",		"63a",			MACHINE_TYPE_SOCKET370,		{{"Intel", cpus_Celeron},     {"VIA", cpus_Cyrix3},  {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,				  8,  512,   8, 255,		  machine_at_63a_init, NULL			},

    /* VIA Apollo Pro */
    { "[VIA Apollo Pro] PC Partner APAS3",	"apas3",		MACHINE_TYPE_SOCKET370,		{{"Intel", cpus_Celeron},     {"VIA", cpus_Cyrix3},  {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,	  			  8,  768,   8, 255,            machine_at_apas3_init, NULL			},
    { "[VIA Apollo Pro133A] Aewin WCF-681",	"wcf681",		MACHINE_TYPE_SOCKET370,		{{"Intel", cpus_Celeron},     {"VIA", cpus_Cyrix3},  {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,	  			  8,  768,   8, 255,            machine_at_wcf681_init, NULL			},
    { "[VIA Apollo Pro133A] Acorp 6VIA85X",	"6via85x",		MACHINE_TYPE_SOCKET370,		{{"Intel", cpus_Celeron},     {"VIA", cpus_Cyrix3},  {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,	  			  8,  768,   8, 255,            machine_at_6via85x_init, NULL			},
	{ "[VIA Apollo Pro133A] ECS P6BAP", "e6bap",		MACHINE_TYPE_SOCKET370,		{{"Intel", cpus_Celeron},     {"VIA", cpus_Cyrix3},  {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,	  			  8,  768,   8, 255,            machine_at_p6bap_init, NULL			},
    { "[VIA Apollo ProMedia] Jetway 603TCF",	"603tcf",		MACHINE_TYPE_SOCKET370,		{{"Intel", cpus_Celeron},     {"VIA", cpus_Cyrix3},  {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,	  			  8,  512,   8, 255,            machine_at_603tcf_init, NULL			},

    /* Miscellaneous/Fake/Hypervisor machines */
    { "[i440BX] Microsoft Virtual PC 2007",	"vpc2007",		MACHINE_TYPE_MISC,		{{"Intel", cpus_PentiumIID},  {"Intel/PGA370", cpus_Celeron},{"",      NULL}, {"",     NULL},     {"",      NULL}}, MACHINE_AGP | MACHINE_BUS_PS2 | MACHINE_IDE_DUAL,		  		  8, 1024,   8, 255,	      machine_at_vpc2007_init, NULL			},

    { NULL,					NULL,			MACHINE_TYPE_NONE,		{{"",      0},                {"",    0},            {"",      0},           {"",         0},     {"",      0}},    0,										  0,    0,   0,   0,			         NULL, NULL			}
};


int
machine_count(void)
{
    return((sizeof(machines) / sizeof(machine)) - 1);
}


char *
machine_getname(void)
{
    return((char *)machines[machine].name);
}


char *
machine_getname_ex(int m)
{
    return((char *)machines[m].name);
}


const device_t *
machine_getdevice(int m)
{
    if (machines[m].get_device)
	return(machines[m].get_device());

    return(NULL);
}


char *
machine_get_internal_name(void)
{
    return((char *)machines[machine].internal_name);
}


char *
machine_get_internal_name_ex(int m)
{
    return((char *)machines[m].internal_name);
}


int
machine_get_nvrmask(int m)
{
    return(machines[m].nvrmask);
}


int
machine_get_machine_from_internal_name(char *s)
{
    int c = 0;

    while (machines[c].init != NULL) {
	if (!strcmp(machines[c].internal_name, (const char *)s))
		return(c);
	c++;
    }

    return(0);
}
