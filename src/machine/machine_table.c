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
#define MACHINE_CPUS_PENTIUM_SS7       {{ "Intel", cpus_Pentium},    {"IDT", cpus_WinChip_SS7}, {"AMD",   cpus_K56_SS7}, {"",      NULL},         {"",      NULL}}
#endif
#endif


const machine_t machines[] = {
    /* 8088 Machines */
    { "[8088] AMI XT clone",			"amixt",		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA,											 64,  640,  64,   0,		machine_xt_amixt_init, NULL			},
    { "[8088] Compaq Portable",			"portable",		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VIDEO,									128,  640, 128,   0,	       machine_xt_compaq_init, NULL			},
    { "[8088] DTK XT clone",			"dtk",			{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA,											 64,  640,  64,   0,		  machine_xt_dtk_init, NULL			},
    { "[8088] IBM PC (1981)",			"ibmpc",		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA,											 16,   64,  16,   0,		      machine_pc_init, NULL			},
    { "[8088] IBM PC (1982)",			"ibmpc82",		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA,											256,  256, 256,   0,		    machine_pc82_init, NULL			},
    { "[8088] IBM PCjr",			"ibmpcjr",		{{"Intel",      cpus_pcjr},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VIDEO | MACHINE_VIDEO_FIXED,							128,  640, 128,   0,		    machine_pcjr_init, pcjr_get_device		},
    { "[8088] IBM XT (1982)",			"ibmxt",		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA,											 64,  256,  64,   0,		      machine_xt_init, NULL			},
    { "[8088] IBM XT (1986)",			"ibmxt86",		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA,											256,  640,  64,   0,		    machine_xt86_init, NULL			},
    { "[8088] Generic XT clone",		"genxt",		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA,											 64,  640,  64,   0,		   machine_genxt_init, NULL			},
    { "[8088] Juko XT clone",			"jukopc",		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA,											 64,  640,  64,   0,	       machine_xt_jukopc_init, NULL			},
    { "[8088] OpenXT",				"open_xt",		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA,											 64,  640,  64,   0,	      machine_xt_open_xt_init, NULL			},
    { "[8088] Phoenix XT clone",		"pxxt",			{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA,											 64,  640,  64,   0,		 machine_xt_pxxt_init, NULL			},
    { "[8088] Schneider EuroPC",		"europc",		{{"Siemens",	cpus_europc}, {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_HDC | MACHINE_MOUSE,								512,  640, 128,  15,		  machine_europc_init, NULL			},
    { "[8088] Tandy 1000",			"tandy",		{{"Intel",      cpus_europc}, {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VIDEO | MACHINE_VIDEO_FIXED,							128,  640, 128,   0,		   machine_tandy_init, tandy1k_get_device	},
    { "[8088] Tandy 1000 HX",			"tandy1000hx",		{{"Intel",      cpus_europc}, {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VIDEO | MACHINE_VIDEO_FIXED,							256,  640, 128,   0,	     machine_tandy1000hx_init, tandy1k_hx_get_device	},
    { "[8088] Toshiba T1000",			"t1000",		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VIDEO,									512, 1280, 768,  63,		machine_xt_t1000_init, t1000_get_device		},
#if defined(DEV_BRANCH) && defined(USE_LASERXT)
    { "[8088] VTech Laser Turbo XT",		"ltxt",			{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA,											256,  640, 256,   0,	      machine_xt_laserxt_init, NULL			},
#endif
    { "[8088] Xi8088",				"xi8088",		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT | MACHINE_PS2,								 64, 1024, 128, 127,	       machine_xt_xi8088_init, xi8088_get_device	},
    { "[8088] Zenith Data SupersPort",		"zdsupers",		{{"Intel",      cpus_8088},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA,											128,  640, 128,   0,	       machine_xt_zenith_init, NULL			},
    
    /* 8086 Machines */
    { "[8086] Amstrad PC1512",			"pc1512",		{{"Intel",      cpus_pc1512}, {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VIDEO | MACHINE_VIDEO_FIXED | MACHINE_MOUSE,					512,  640, 128,  63,		  machine_pc1512_init, pc1512_get_device	},
    { "[8086] Amstrad PC1640",			"pc1640",		{{"Intel",      cpus_8086},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VIDEO | MACHINE_MOUSE,							640,  640,   0,  63,		  machine_pc1640_init, pc1640_get_device	},
    { "[8086] Amstrad PC2086",			"pc2086",		{{"Intel",      cpus_8086},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VIDEO | MACHINE_VIDEO_FIXED | MACHINE_MOUSE,					640,  640,   0,  63,		  machine_pc2086_init, pc2086_get_device	},
    { "[8086] Amstrad PC3086",			"pc3086",		{{"Intel",      cpus_8086},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VIDEO | MACHINE_VIDEO_FIXED | MACHINE_MOUSE,					640,  640,   0,  63,		  machine_pc3086_init, pc3086_get_device	},
    { "[8086] Amstrad PC20(0)",			"pc200",		{{"Intel",      cpus_8086},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VIDEO | MACHINE_MOUSE | MACHINE_NONMI,					512,  640, 128,  63,		   machine_pc200_init, pc200_get_device		},
    { "[8086] Amstrad PPC512/640",		"ppc512",		{{"Intel",      cpus_8086},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VIDEO | MACHINE_MOUSE | MACHINE_NONMI,					512,  640, 128,  63,		  machine_ppc512_init, ppc512_get_device	},
    { "[8086] Olivetti M24",			"olivetti_m24",		{{"Intel",      cpus_8086},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VIDEO | MACHINE_VIDEO_FIXED | MACHINE_MOUSE,					128,  640, 128,   0,		  machine_olim24_init, NULL			},
    { "[8086] Tandy 1000 SL/2",			"tandy1000sl2",		{{"Intel",      cpus_8086},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VIDEO | MACHINE_VIDEO_FIXED,							512,  768, 128,   0,	    machine_tandy1000sl2_init, NULL			},
    { "[8086] Toshiba T1200",			"t1200",		{{"Intel",      cpus_8086},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VIDEO,								       1024, 2048,1024,  63,		machine_xt_t1200_init, t1200_get_device		},
#if defined(DEV_BRANCH) && defined(USE_LASERXT)
    { "[8086] VTech Laser XT3",			"lxt3",			{{"Intel",      cpus_8086},   {"",      NULL},       {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA,											256,  640, 256,   0,		 machine_xt_lxt3_init, NULL			},
#endif

    /* 286 XT machines */
    { "[286 XT] Hedaka HED-919",		"hed919",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA,											 64,  640,  64,   0,	       machine_xt_hed919_init, NULL			},

    /* 286 AT machines */
    { "[286 ISA] AMI 286 clone",		"ami286",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT,										512, 8192, 128, 127,	     machine_at_neat_ami_init, NULL			},
    { "[286 ISA] Award 286 clone",		"award286",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT,										512,16384, 128, 127,	     machine_at_award286_init, NULL			},
    { "[286 ISA] Phoenix 286 clone",		"px286",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT,										512,16384, 128, 127,	     machine_at_px286_init, NULL			},
    { "[286 ISA] Quadtel 286 clone",		"quadt286",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT,										512,16384, 128, 127,	     machine_at_quadt286_init, NULL			},
    { "[286 ISA] MR 286 clone",		"mr286",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT,										512, 16384, 128, 127,	     machine_at_mr286_init, NULL			},
    { "[286 ISA] Commodore PC 30 III",		"cmdpc30",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT,										640,16384, 128, 127,		machine_at_cmdpc_init, NULL			},
    { "[286 ISA] Compaq Portable II",		"portableii",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT,										640,16384, 128, 127,	   machine_at_portableii_init, NULL			},
    { "[286 ISA] Compaq Portable III",		"portableiii",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT | MACHINE_VIDEO,								640,16384, 128, 127,	  machine_at_portableiii_init, at_cpqiii_get_device	},
    { "[286 ISA] GW-286CT GEAR",		"gw286ct",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT,										512,16384, 128, 127,	      machine_at_gw286ct_init, NULL			},
    { "[286 ISA] Goldstar GDC-212M",		"gdc212m",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT | MACHINE_HDC | MACHINE_PS2,						512, 4096, 512, 127,	      machine_at_gdc212m_init, NULL			},
    { "[286 ISA] Hyundai Super-286TR",		"super286tr",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT,										512,16384, 128, 127,	   machine_at_super286tr_init, NULL			},
    { "[286 ISA] IBM AT",			"ibmat",		{{"",      cpus_ibmat},       {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT,										256,15872, 128,  63,		  machine_at_ibm_init, NULL			},
    { "[286 ISA] AMI IBM AT",			"ibmatami",		{{"",      cpus_ibmat},       {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT,										256,15872, 128,  63,	     machine_at_ibmatami_init, NULL			},
    { "[286 ISA] Quadtel IBM AT",		"ibmatquadtel",		{{"",      cpus_ibmat},       {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT,										256,15872, 128,  63,	 machine_at_ibmatquadtel_init, NULL			},
    { "[286 ISA] Phoenix IBM AT",		"ibmatpx",		{{"",      cpus_ibmat},       {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT,										256,15872, 128,  63,	      machine_at_ibmatpx_init, NULL			},
    { "[286 ISA] IBM PS/1 model 2011",		"ibmps1es",		{{"",      cpus_ps1_m2011},   {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT | MACHINE_VIDEO | MACHINE_VIDEO_FIXED | MACHINE_HDC | MACHINE_PS2,		512,16384, 512,  63,	       machine_ps1_m2011_init, NULL			},
    { "[286 ISA] IBM PS/2 model 30-286",	"ibmps2_m30_286",	{{"Intel", cpus_ps2_m30_286}, {"IBM",cpus_IBM486SLC},{"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT | MACHINE_VIDEO | MACHINE_VIDEO_FIXED | MACHINE_HDC | MACHINE_PS2,		  1,   16,   1, 127,	     machine_ps2_m30_286_init, NULL			},
    { "[286 ISA] IBM XT Model 286",		"ibmxt286",		{{"",      cpus_ibmxt286},    {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT,										256,15872, 128, 127,	     machine_at_ibmxt286_init, NULL			},
#if defined(DEV_BRANCH) && defined(USE_SIEMENS)
    { "[286 ISA] Siemens PCD-2L",		"siemens",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT,										256,15872, 128,  63,	      machine_at_siemens_init, NULL			},
#endif
#if defined(DEV_BRANCH) && defined(USE_OPEN_AT)
    { "[286 ISA] OpenAT",			"open_at",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT,										256,15872, 128,  63,	      machine_at_open_at_init, NULL			},
#endif
    { "[286 ISA] Samsung SPC-4200P",		"spc4200p",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT | MACHINE_PS2,								512, 2048, 128, 127,	     machine_at_spc4200p_init, NULL			},
    { "[286 ISA] Samsung SPC-4216P",		"spc4216p",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT | MACHINE_PS2,								  1,    5,   1, 127,	     machine_at_spc4216p_init, NULL			},
    { "[286 ISA] Toshiba T3100e",		"t3100e",		{{"",      cpus_286},         {"",    NULL},	     {"",      NULL},	     {"",      NULL},	  {"",      NULL}}, MACHINE_ISA | MACHINE_AT | MACHINE_VIDEO | MACHINE_VIDEO_FIXED | MACHINE_HDC,		       1024, 5120, 256,  63,	       machine_at_t3100e_init, NULL			},
    { "[286 ISA] Trigem 286M",			"tg286m",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT | MACHINE_HDC,							  	512, 8192, 128, 127,	       machine_at_tg286m_init, NULL			},

    { "[286 ISA] Samsung Deskmaster 286",	"deskmaster286",	{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT,										512,16384, 128, 127,	   machine_at_deskmaster286_init, NULL			},

    /* 286 machines that utilize the MCA bus */
    { "[286 MCA] IBM PS/2 model 50",		"ibmps2_m50",		{{"Intel", cpus_ps2_m30_286}, {"IBM",cpus_IBM486SLC},{"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_MCA | MACHINE_AT | MACHINE_PS2 | MACHINE_VIDEO,						  1,   10,   1,  63,	    machine_ps2_model_50_init, NULL			},

    /* 386SX machines */
    { "[386SX ISA] AMA-932J",			"ama932j",		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT | MACHINE_HDC | MACHINE_VIDEO,						512, 8192, 128, 127,          machine_at_ama932j_init, at_ama932j_get_device 	},
#if defined(DEV_BRANCH) && defined(USE_AMI386SX)
    { "[386SX ISA] AMI Unknown 386SX",		"ami386",		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT | MACHINE_HDC,								512,16384, 128, 127,	     machine_at_headland_init, NULL			},
#endif
    { "[386SX ISA] Amstrad MegaPC",		"megapc",		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_VIDEO | MACHINE_HDC,				  1,   32,   1, 127,	      machine_at_wd76c10_init, NULL			},
    { "[386SX ISA] Commodore SL386SX",		"cbm_sl386sx25",	{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_VIDEO | MACHINE_HDC,				1024, 8192, 512, 127,	machine_at_commodore_sl386sx_init, at_commodore_sl386sx_get_device	},  
    { "[386SX ISA] DTK 386SX clone",		"dtk386",		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT | MACHINE_HDC,								512, 8192, 128, 127,		 machine_at_neat_init, NULL			},
    { "[386SX ISA] IBM PS/1 model 2121",	"ibmps1_2121",		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC | MACHINE_VIDEO | MACHINE_VIDEO_FIXED,		  2,    6,   1,  63,	       machine_ps1_m2121_init, NULL			},
    { "[386SX ISA] IBM PS/1 m.2121+ISA",	"ibmps1_2121_isa",	{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC | MACHINE_VIDEO,				  2,    6,   1,  63,	       machine_ps1_m2121_init, NULL			},
    { "[386SX ISA] KMX-C-02",			"kmxc02",		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT,										512,16384, 512, 127,	       machine_at_kmxc02_init, NULL			},

    { "[386SX ISA] Goldstar 386",		"goldstar386",		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT | MACHINE_HDC,								512, 8192, 128, 127,	  machine_at_goldstar386_init, NULL			},

    /* 386SX machines which utilize the MCA bus */
    { "[386SX MCA] IBM PS/2 model 55SX",	"ibmps2_m55sx",		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"IBM",cpus_IBM486SLC},{"",    NULL}}, MACHINE_MCA | MACHINE_AT | MACHINE_PS2 | MACHINE_VIDEO,						  1,    8,   1,  63,	  machine_ps2_model_55sx_init, NULL			},

    /* 386DX machines */
    { "[386DX ISA] Compaq Portable III (386)",  "portableiii386",       {{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT | MACHINE_HDC | MACHINE_VIDEO,			  			  1,   14,   1, 127,   machine_at_portableiii386_init, at_cpqiii_get_device	},
    { "[386DX ISA] AMI 386DX clone",	"acc386",		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT | MACHINE_HDC,								512, 16384, 128, 127,	 machine_at_acc386_init, NULL			},
    { "[386DX ISA] ECS 386/32",			"ecs386",		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT,										1,   32,   1, 127,	       machine_at_ecs386_init, NULL			},		
    { "[386DX ISA] Micronics 386 clone",	"micronics386",		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT | MACHINE_HDC,								512, 8192, 128, 127,	 machine_at_micronics386_init, NULL			},

    /* 386DX machines which utilize the VLB bus */
    { "[386DX VLB] Award 386DX clone",		"award386dx",		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_HDC,						  1,   32,   1, 127,	      machine_at_opti495_init, NULL			},
    { "[386DX VLB] Dataexpert SX495 (386DX)",	"ami386dx",		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_HDC,						  1,   32,   1, 127,	  machine_at_opti495_ami_init, NULL			},
    { "[386DX VLB] MR 386DX clone",		"mr386dx",		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_HDC,						  1,   32,   1, 127,	   machine_at_opti495_mr_init, NULL			},

    /* 386DX machines which utilize the MCA bus */
    { "[386DX MCA] IBM PS/2 model 70 (type 3)",	"ibmps2_m70_type3",	{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"IBM",cpus_IBM486BL},{"",     NULL}}, MACHINE_MCA | MACHINE_AT | MACHINE_PS2 | MACHINE_VIDEO,						  2,   16,   2,  63,  machine_ps2_model_70_type3_init, NULL			},
    { "[386DX MCA] IBM PS/2 model 80",		"ibmps2_m80",		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"IBM",cpus_IBM486BL},{"",     NULL}}, MACHINE_MCA | MACHINE_AT | MACHINE_PS2 | MACHINE_VIDEO,						  1,   12,   1,  63,	    machine_ps2_model_80_init, NULL			},

    /* 486 machines with just the ISA slot */
    { "[486 ISA] Packard Bell PB410A",		"pb410a",		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC | MACHINE_VIDEO,			  	  4,   36,   1, 127,	       machine_at_pb410a_init, NULL			},    

    /* 486 machines */
    { "[486 VLB] Award 486 clone",		"award486",		{{"Intel", cpus_i486S1},      {"AMD", cpus_Am486S1}, {"Cyrix", cpus_Cx486S1},{"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_HDC,						  1,   32,   1, 127,	      machine_at_opti495_init, NULL			},    
    { "[486 VLB] Dataexpert SX495 (486)",	"ami486",		{{"Intel", cpus_i486S1},      {"AMD", cpus_Am486S1}, {"Cyrix", cpus_Cx486S1},{"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_HDC,						  1,   32,   1, 127,	  machine_at_opti495_ami_init, NULL			},
    { "[486 VLB] Olystar LIL1429",		"ali1429",		{{"Intel", cpus_i486S1},      {"AMD", cpus_Am486S1}, {"Cyrix", cpus_Cx486S1},{"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_HDC,						  1,   32,   1, 127,	      machine_at_ali1429_init, NULL			},
#if defined(DEV_BRANCH) && defined(USE_PS1M2133)
    { "[486 VLB] IBM PS/1 model 2133",		"ibmps1_2133",		{{"Intel", cpus_i486S1},      {"AMD", cpus_Am486S1}, {"Cyrix", cpus_Cx486S1},{"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC | MACHINE_NONMI,			  1,   64,   1, 127,	       machine_ps1_m2133_init, NULL			},
#endif
    { "[486 VLB] AMI SiS 471",			"ami471",		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_HDC,						  1,   64,   1, 127,	       machine_at_ami471_init, NULL			},
    { "[486 VLB] AMI WinBIOS 486",		"win486",		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_HDC,						  1,   32,   1, 127,	  machine_at_winbios1429_init, NULL			},
#if defined(DEV_BRANCH) && defined(USE_WIN471)
    { "[486 VLB] AMI WinBIOS SiS 471",		"win471",		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_HDC,						  1,   64,   1, 127,	       machine_at_win471_init, NULL			},
#endif
    { "[486 VLB] DTK PKM-0038S E-2",		"dtk486",		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_HDC,						  1,   64,   1, 127,	       machine_at_dtk486_init, NULL			},

    { "[486 VLB] MR 486 clone",			"mr486",		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_HDC,						  1,   32,   1, 127,	   machine_at_opti495_mr_init, NULL			},
    { "[486 VLB] Phoenix SiS 471",		"px471",		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_HDC,						  1,  128,   1, 127,	        machine_at_px471_init, NULL			},

#if defined(DEV_BRANCH) && defined(USE_PS2M70T4)
    { "[486 MCA] IBM PS/2 model 70 (type 4)",	"ibmps2_m70_type4",	{{"Intel", cpus_i486S1},      {"AMD", cpus_Am486S1}, {"Cyrix", cpus_Cx486S1},{"",      NULL},     {"",      NULL}}, MACHINE_MCA | MACHINE_AT | MACHINE_PS2 | MACHINE_VIDEO,						  2,   16,   2,  63,  machine_ps2_model_70_type4_init, NULL			},
#endif

    /* 486 machines which utilize the PCI bus */
#if defined(DEV_BRANCH) && defined(NO_SIO)
    { "[486 PCI] ASUS PCI/I-486SP3G",		"486sp3g",		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_HDC,						  1,  128,   1, 127,	      machine_at_486sp3g_init, NULL			},
#endif
    { "[486 PCI] Intel Classic/PCI",		"alfredo",		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  		  2,  128,   2, 127,	      machine_at_alfredo_init, NULL			},
    { "[486 PCI] Lucky Star LS-486E",		"ls486e",		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_HDC,						  1,  128,   1, 127,	       machine_at_ls486e_init, NULL			},
    { "[486 PCI] Rise Computer R418",		"r418",			{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_HDC,						  1,  255,   1, 127,		 machine_at_r418_init, NULL			},
    { "[486 PCI] Zida Tomato 4DP",		"4dps",			{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_HDC,						  1,  255,   1, 127,		 machine_at_4dps_init, NULL			},

    /* Socket 4 machines */
    /* 430LX */
    { "[Socket 4 LX] IBM Ambra DP60 PCI",	"ambradp60",		{{"Intel", cpus_Pentium5V},   {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  2,  128,   2, 127,	    machine_at_ambradp60_init, NULL			},
#if defined(DEV_BRANCH) && defined(USE_VPP60)
    { "[Socket 4 LX] IBM PS/ValuePoint P60",	"valuepointp60",	{{"Intel", cpus_Pentium5V},   {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			 		  2,  128,   2, 127,	machine_at_valuepointp60_init, NULL			},
#endif
    { "[Socket 4 LX] Intel Premiere/PCI",	"revenge",		{{"Intel", cpus_Pentium5V},   {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  2,  128,   2, 127,	       machine_at_batman_init, NULL			},
    { "[Socket 4 LX] Micro Star 586MC1",	"586mc1",		{{"Intel", cpus_Pentium5V},   {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			 		  2,  128,   2, 127,	       machine_at_586mc1_init, NULL			},
    /* Socket 5 machines */
    /* 430NX */
    { "[Socket 5 NX] Intel Premiere/PCI II",	"plato",		MACHINE_CPUS_PENTIUM_S5,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  2,  128,   2, 127,		machine_at_plato_init, NULL			},
    { "[Socket 5 NX] IBM Ambra DP90 PCI",	"ambradp90",		MACHINE_CPUS_PENTIUM_S5,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  2,  128,   2, 127,	    machine_at_ambradp90_init, NULL			},
    { "[Socket 5 NX] Gigabyte GA-586IP",	"430nx",		MACHINE_CPUS_PENTIUM_S5,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  2,  128,   2, 127,		machine_at_430nx_init, NULL			},

    /* 430FX */
#if defined(DEV_BRANCH) && defined(USE_VECTRA54)
    { "[Socket 5 FX] HP Vectra VL 5 Series 4",  "vectra54",		MACHINE_CPUS_PENTIUM_S5,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_HDC,						  8,  128,   8, 511,	     machine_at_vectra54_init, NULL			},
#endif
    { "[Socket 5 FX] Intel Advanced/ZP",	"zappa",		MACHINE_CPUS_PENTIUM_S5,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  128,   8, 127,		machine_at_zappa_init, NULL			},
    { "[Socket 5 FX] NEC PowerMate V",  	"powermate_v",		MACHINE_CPUS_PENTIUM_S5,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  128,   8, 127,	  machine_at_powermate_v_init, NULL			},
    { "[Socket 5 FX] PC Partner MB500N",	"mb500n",		MACHINE_CPUS_PENTIUM_S5,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_HDC,						  8,  128,   8, 127,	       machine_at_mb500n_init, NULL			},

    /* Socket 7 machines */
    /* 430FX */
    { "[Socket 7-3V FX] ASUS P/I-P54TP4XE",	"p54tp4xe",		MACHINE_CPUS_PENTIUM_S73V,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  128,   8, 127,	     machine_at_p54tp4xe_init, NULL			},
    { "[Socket 7-3V FX] QDI Chariot",		"chariot",		MACHINE_CPUS_PENTIUM_S73VCH,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_HDC,					  8,  128,   8, 127,	     machine_at_chariot_init, NULL			},
    { "[Socket 7-3V FX] MR 430FX clone",	"mr586",		MACHINE_CPUS_PENTIUM_S73V,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_HDC | MACHINE_PS2,					  8,  128,   8, 127,	     machine_at_mr586_init, NULL			},
    { "[Socket 7-3V FX] Intel Advanced/ATX",	"thor",			MACHINE_CPUS_PENTIUM_S73V,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  128,   8, 127,		 machine_at_thor_init, NULL			},
    { "[Socket 7-3V FX] Intel Advanced/EV",	"endeavor",		MACHINE_CPUS_PENTIUM_S73V,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC | MACHINE_VIDEO,			  8,  128,   8, 127,	     machine_at_endeavor_init, at_endeavor_get_device	},
    { "[Socket 7-3V FX] MR Intel Advanced/ATX",	"mrthor",		MACHINE_CPUS_PENTIUM_S73V,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			 		  8,  128,   8, 127,	       machine_at_mrthor_init, NULL			},
    { "[Socket 7-3V FX] Packard Bell PB640",	"pb640",		MACHINE_CPUS_PENTIUM_S73V,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC | MACHINE_VIDEO,	 		  8,  128,   8, 127,		machine_at_pb640_init, at_pb640_get_device	},

    /* 430HX */
    { "[Socket 7-3V HX] Acer M3a",		"acerm3a",		MACHINE_CPUS_PENTIUM_S73V,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  192,   8, 127,	      machine_at_acerm3a_init, NULL			},
    { "[Socket 7-3V HX] AOpen AP53",		"ap53",			MACHINE_CPUS_PENTIUM_S73V,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			 		  8,  512,   8, 127,		 machine_at_ap53_init, NULL			},    
    { "[Socket 7-3V HX] SuperMicro Super P55T2S","p55t2s",		MACHINE_CPUS_PENTIUM_S73V,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  768,   8, 127,	       machine_at_p55t2s_init, NULL			},

    { "[Socket 7 HX] Acer V35n",		"acerv35n",		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  192,   8, 127,	     machine_at_acerv35n_init, NULL			},
    { "[Socket 7 HX] ASUS P/I-P55T2P4",		"p55t2p4",		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  256,   8, 127,	      machine_at_p55t2p4_init, NULL			},
    { "[Socket 7 HX] Micronics M7S-Hi",		"m7shi",		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			 		  8,  128,   8, 511,	        machine_at_m7shi_init, NULL			},
    { "[Socket 7 HX] Intel TC430HX",		"tc430hx",		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  		  8,  128,   8, 255,	      machine_at_tc430hx_init, NULL			},
    { "[Socket 7 HX] Toshiba Equium 5200D",	"equium5200",		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  		  8,  192,   8, 127,	   machine_at_equium5200_init, NULL			},
    { "[Socket 7 HX] ASUS P/I-P65UP5 (C-P55T2D)","p65up5_cp55t2d",	MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  		  8,  512,   8, 127,   machine_at_p65up5_cp55t2d_init, NULL			},

    /* 430VX */
    { "[Socket 7 VX] ASUS P/I-P55TVP4",		"p55tvp4",		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			 		  8,  128,   8, 127,	      machine_at_p55tvp4_init, NULL			},
    { "[Socket 7 VX] Shuttle HOT-557",		"430vx",		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  128,   8, 127,	       machine_at_i430vx_init, NULL			},
    { "[Socket 7 VX] Epox P55-VA",		"p55va",		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  		  8,  128,   8, 127,		machine_at_p55va_init, NULL			},
    { "[Socket 7 VX] HP Brio 80xx",		"brio80xx",		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			 		  8,  128,   8, 127,	     machine_at_brio80xx_init, NULL			},
    { "[Socket 7 VX] Packard Bell PB680",	"pb680",		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			 		  8,  128,   8, 127,	        machine_at_pb680_init, NULL			},

    /* 430TX */
    { "[Socket 7 TX] ASUS TX97",		"tx97",			MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  256,   8, 255,	         machine_at_tx97_init, NULL			},
#if defined(DEV_BRANCH) && defined(NO_SIO)
    { "[Socket 7 TX] Gigabyte GA-586T2",        "586t2",		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  256,   8, 255,	        machine_at_586t2_init, NULL			},
#endif
    { "[Socket 7 TX] Intel YM430TX",		"ym430tx",		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  256,   8, 255,	      machine_at_ym430tx_init, NULL			},
#if defined(DEV_BRANCH) && defined(NO_SIO)
    { "[Socket 7 TX] Iwill P55XB2",		"p55xb2",		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  256,   8, 255,	       machine_at_p55xb2_init, NULL			},
    { "[Socket 7 TX] PC Partner TXA807DS",	"807ds",		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  256,   8, 255,	        machine_at_807ds_init, NULL			},
#endif
    { "[Socket 7 TX] Supermicro P5MMS98",	"p5mms98",		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  256,   8, 255,	      machine_at_p5mms98_init, NULL			},


    /* Apollo VPX */
    { "[Socket 7 VPX] FIC VA-502",		"ficva502",		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			 		  8,  512,   8, 127,	     machine_at_ficva502_init, NULL			},

    /* Apollo VP3 */
    { "[Socket 7 VP3] FIC PA-2012",		"ficpa2012",		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			 		  8,  192,   8, 127,	    machine_at_ficpa2012_init, NULL			},
#if defined(DEV_BRANCH) && defined(NO_SIO)
    { "[Socket 7 VP3] QDI Advance II",		"advanceii",		MACHINE_CPUS_PENTIUM_S7,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			 		  8,  128,   8, 127,	    machine_at_advanceii_init, NULL			},
#endif
  
    /* Super Socket 7 machines */
    /* Apollo MVP3 */
    { "[Super 7 MVP3] AOpen AX59 Pro",		"ax59pro",		MACHINE_CPUS_PENTIUM_SS7,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8, 1024,  8, 255,	      machine_at_ax59pro_init, NULL			},
    { "[Super 7 MVP3] FIC VA-503+",		"ficva503p",		MACHINE_CPUS_PENTIUM_SS7,											    MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  512,  8, 255,	         machine_at_mvp3_init, NULL			},

    /* Socket 8 machines */
    /* 440FX */
    { "[Socket 8 FX] Gigabyte GA-686NX",	"686nx",		{{"Intel", cpus_PentiumPro},  {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  512,   8, 127,	        machine_at_686nx_init, NULL			},
    { "[Socket 8 FX] PC Partner MB600N",	"mb600n",		{{"Intel", cpus_PentiumPro},  {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  512,   8, 127,	       machine_at_mb600n_init, NULL			},
    { "[Socket 8 FX] Biostar MB-8500ttc",	"8500ttc",		{{"Intel", cpus_PentiumPro},  {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  512,   8, 127,	      machine_at_8500ttc_init, NULL			},
    { "[Socket 8 FX] Micronics M6MI",		"m6mi",			{{"Intel", cpus_PentiumPro},  {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  384,   8, 127,	         machine_at_m6mi_init, NULL			},
    { "[Socket 8 FX] ASUS P/I-P65UP5 (C-P6ND)",	"p65up5_cp6nd",		{{"Intel", cpus_PentiumPro},  {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  512,   8, 127,	 machine_at_p65up5_cp6nd_init, NULL			},


    /* Slot 1 machines */
    /* 440FX */
    { "[Slot 1 FX] ASUS P/I-P65UP5 (C-PKND)",	"p65up5_cpknd",		{{"Intel", cpus_PentiumII_28v},{"",    NULL},        {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  512,   8, 127,	 machine_at_p65up5_cpknd_init, NULL			},
    { "[Slot 1 FX] ECS P6KFX-A",		"p6kfx",		{{"Intel", cpus_PentiumII_28v},{"",    NULL},        {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  384,   8, 127,	        machine_at_p6kfx_init, NULL			},

    /* 440LX */

    /* 440BX */
#if defined(DEV_BRANCH) && defined(NO_SIO)
    { "[Slot 1 BX] Gigabyte GA-6BXC",		"6bxc",			{{"Intel", cpus_PentiumII},   {"Intel/PGA370", cpus_Celeron},{"VIA", cpus_Cyrix3},{"",      NULL},{"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  		  8,  768,   8, 255,		 machine_at_6bxc_init, NULL			},
#endif
    { "[Slot 1 BX] ASUS P2B-LS",		"p2bls",		{{"Intel", cpus_PentiumII},   {"Intel/PGA370", cpus_Celeron},{"VIA", cpus_Cyrix3},{"",      NULL},{"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			 		  8, 1024,   8, 255,		machine_at_p2bls_init, NULL			},
    { "[Slot 1 BX] ASUS P2B-LS (coreboot BIOS)","p2bls_cb",		{{"Intel", cpus_PentiumII},   {"Intel/PGA370", cpus_Celeron},{"",      NULL},     {"",      NULL},{"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC | MACHINE_COREBOOT, 		  8, 1024,   8, 255,		machine_at_p2bls_init, NULL			},
    { "[Slot 1 BX] ASUS P3B-F",			"p3bf",			{{"Intel", cpus_PentiumII},   {"Intel/PGA370", cpus_Celeron},{"VIA", cpus_Cyrix3},{"",      NULL},{"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			 		  8, 1024,   8, 255,		 machine_at_p3bf_init, NULL			},
    { "[Slot 1 BX] ASUS P3B-F (coreboot BIOS)",	"p3bf_cb",		{{"Intel", cpus_PentiumII},   {"Intel/PGA370", cpus_Celeron},{"",      NULL},     {"",      NULL},{"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC | MACHINE_COREBOOT, 		  8, 1024,   8, 255,		 machine_at_p3bf_init, NULL			},
    { "[Slot 1 BX] ABit BF6",			"bf6",			{{"Intel", cpus_PentiumII},   {"Intel/PGA370", cpus_Celeron},{"VIA", cpus_Cyrix3},{"",      NULL},{"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  		  8,  768,   8, 255,		  machine_at_bf6_init, NULL			},
#if defined(DEV_BRANCH) && defined(NO_SIO)
    { "[Slot 1 BX] Tyan Tsunami ATX",		"tsunamiatx",		{{"Intel", cpus_PentiumII},   {"Intel/PGA370", cpus_Celeron},{"VIA", cpus_Cyrix3},{"",      NULL},{"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			 		  8, 1024,   8, 255,	   machine_at_tsunamiatx_init, NULL			},
#endif
    { "[Slot 1 BX] Supermicro P6SBA",		"p6sba",		{{"Intel", cpus_PentiumII},   {"Intel/PGA370", cpus_Celeron},{"VIA", cpus_Cyrix3},{"",      NULL},{"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  		  8,  768,   8, 255,	        machine_at_p6sba_init, NULL			},

	/* Slot 2 machines */
	/* 440GX */
#if defined(DEV_BRANCH) && defined(NO_SIO)
    { "[Slot 2 GX] Supermicro S2DGE",		"s2dge",		{{"Intel", cpus_Xeon},        {"Intel/PGA370", cpus_Celeron},{"VIA", cpus_Cyrix3},{"",      NULL},{"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			 		  8, 1024,   8, 255,	        machine_at_s2dge_init, NULL			},
#endif

    /* PGA370 machines */
	/* 440LX */
#if defined(DEV_BRANCH) && defined(NO_SIO)
    { "[Socket 370 LX] Supermicro 370SLM",	"s370slm",		{{"Intel", cpus_Celeron},     {"", NULL},            {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			 		  8,  768,   8, 255,	      machine_at_s370slm_init, NULL			},
#endif
    /* 440BX */
    { "[Socket 370 BX] ASUS CUBX",		"cubx",			{{"Intel", cpus_Celeron},     {"VIA", cpus_Cyrix3},  {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			 		  8, 1024,   8, 255,		 machine_at_cubx_init, NULL			},
    { "[Socket 370 BX] A-Trend ATC7020BXII",	"atc7020bxii",		{{"Intel", cpus_Celeron},     {"VIA", cpus_Cyrix3},  {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			 		  8, 1024,   8, 255,	  machine_at_atc7020bxii_init, NULL			},

    /* 440ZX */
    { "[Socket 370 ZX] Soltek SL-63A1",		"63a",			{{"Intel", cpus_Celeron},     {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8,  512,   8, 255,		  machine_at_63a_init, NULL			},

    /* VIA Apollo Pro */
    { "[Socket 370 APRO] PC Partner APAS3",	"apas3",		{{"Intel", cpus_Celeron},     {"VIA", cpus_Cyrix3},  {"",      NULL},        {"",      NULL},     {"",      NULL}}, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,	  				  8, 1024,   8, 255,            machine_at_apas3_init, NULL			},

    { NULL,					NULL,			{{"",      0},                {"",    0},            {"",      0},           {"",         0},     {"",      0}},    0,                                                                                                    0,    0,   0,   0,			         NULL, NULL			}
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
