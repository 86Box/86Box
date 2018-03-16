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
 * Version:	@(#)machine_table.c	1.0.25	2018/03/13
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../cpu/cpu.h"
#include "../mem.h"
#include "../nvr.h"
#include "../rom.h"
#include "../device.h"
#include "machine.h"


machine_t machines[] = {
    { "[8088] AMI XT clone",			ROM_AMIXT,		"amixt",		{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA,												 64,  640,  64,   0,		      machine_xt_init, NULL,			NULL			},
    { "[8088] Compaq Portable",			ROM_PORTABLE,		"portable",		{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_VIDEO,										128,  640, 128,   0,	       machine_xt_compaq_init, NULL,			NULL			},
    { "[8088] DTK XT clone",			ROM_DTKXT,		"dtk",			{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA,												 64,  640,  64,   0,		      machine_xt_init, NULL,			NULL			},
    { "[8088] IBM PC",				ROM_IBMPC,		"ibmpc",		{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA,												 64,  640,  32,   0,		      machine_xt_init, NULL,			NULL			},
    { "[8088] IBM PCjr",			ROM_IBMPCJR,		"ibmpcjr",		{{"",      cpus_pcjr},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, MACHINE_ISA | MACHINE_VIDEO,										128,  640, 128,   0,		    machine_pcjr_init, pcjr_get_device,		NULL			},
    { "[8088] IBM XT",				ROM_IBMXT,		"ibmxt",		{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA,												 64,  640,  64,   0,		      machine_xt_init, NULL,			NULL			},
    { "[8088] Generic XT clone",		ROM_GENXT,		"genxt",		{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA,												 64,  640,  64,   0,		      machine_xt_init, NULL,			NULL			},
    { "[8088] Juko XT clone",			ROM_JUKOPC,		"jukopc",		{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA,												 64,  640,  64,   0,		      machine_xt_init, NULL,			NULL			},
    { "[8088] Phoenix XT clone",		ROM_PXXT,		"pxxt",			{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA,												 64,  640,  64,   0,		      machine_xt_init, NULL,			NULL			},
    { "[8088] Schneider EuroPC",		ROM_EUROPC,		"europc",		{{"Siemens",cpus_europc},     {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_HDC | MACHINE_VIDEO | MACHINE_MOUSE,						512,  640, 128,  15,		  machine_europc_init, NULL,			NULL			},
    { "[8088] Tandy 1000",			ROM_TANDY,		"tandy",		{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, MACHINE_ISA,												128,  640, 128,   0,		 machine_tandy1k_init, tandy1k_get_device,	NULL			},
    { "[8088] Tandy 1000 HX",			ROM_TANDY1000HX,	"tandy1000hx",		{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, MACHINE_ISA,												256,  640, 128,   0,		 machine_tandy1k_init, tandy1k_hx_get_device,	NULL			},
    { "[8088] Toshiba 1000",			ROM_T1000,		"t1000",		{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, MACHINE_ISA | MACHINE_VIDEO,										512, 1280, 768,  63,		machine_xt_t1000_init, NULL,	NULL			},
#if defined(DEV_BRANCH) && defined(USE_LASERXT)
    { "[8088] VTech Laser Turbo XT",		ROM_LTXT,		"ltxt",			{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA,												512,  512, 256,   0,	      machine_xt_laserxt_init, NULL,			NULL			},
#endif
    { "[8088] Xi8088",				ROM_XI8088,		"xi8088",		{{"",      cpus_8088},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_AT | MACHINE_PS2,										 64, 1024, 128, 127,	       machine_xt_xi8088_init, NULL,	nvr_at_close		},

    { "[8086] Amstrad PC1512",			ROM_PC1512,		"pc1512",		{{"",      cpus_pc1512},      {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_VIDEO | MACHINE_MOUSE,								512,  640, 128,  63,		 machine_amstrad_init, NULL,			nvr_at_close		},
    { "[8086] Amstrad PC1640",			ROM_PC1640,		"pc1640",		{{"",      cpus_8086},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_VIDEO | MACHINE_MOUSE,								640,  640,   0,  63,		 machine_amstrad_init, NULL,			nvr_at_close		},
    { "[8086] Amstrad PC2086",			ROM_PC2086,		"pc2086",		{{"",      cpus_8086},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, MACHINE_ISA | MACHINE_VIDEO | MACHINE_MOUSE,								640,  640,   0,  63,		 machine_amstrad_init, NULL,			nvr_at_close		},
    { "[8086] Amstrad PC3086",			ROM_PC3086,		"pc3086",		{{"",      cpus_8086},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, MACHINE_ISA | MACHINE_VIDEO | MACHINE_MOUSE,								640,  640,   0,  63,		 machine_amstrad_init, NULL,			nvr_at_close		},
    { "[8086] Amstrad PC20(0)",			ROM_PC200,		"pc200",		{{"",      cpus_8086},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, MACHINE_ISA | MACHINE_VIDEO | MACHINE_MOUSE,								512,  640, 128,  63,		 machine_amstrad_init, NULL,			nvr_at_close		},
    { "[8086] Olivetti M24",			ROM_OLIM24,		"olivetti_m24",		{{"",      cpus_8086},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, MACHINE_ISA | MACHINE_VIDEO | MACHINE_MOUSE,								128,  640, 128,   0,		  machine_olim24_init, NULL,			NULL			},
    { "[8086] Tandy 1000 SL/2",			ROM_TANDY1000SL2,	"tandy1000sl2",		{{"",      cpus_8086},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, MACHINE_ISA,												512,  768, 128,   0,	         machine_tandy1k_init, NULL,			NULL			},
    { "[8086] Toshiba 1200",			ROM_T1200,		"t1200",		{{"",      cpus_8086},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, MACHINE_ISA | MACHINE_VIDEO,									       1024, 2048,1024,  63,		machine_xt_t1200_init, NULL,			NULL			},
#if defined(DEV_BRANCH) && defined(USE_LASERXT)
    { "[8086] VTech Laser XT3",			ROM_LXT3,		"lxt3",			{{"",      cpus_8086},        {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA,												256,  512, 256,   0,	      machine_xt_laserxt_init, NULL,			NULL			},
#endif

    { "[286 ISA] AMI 286 clone",		ROM_AMI286,		"ami286",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_AT,										512,16384, 128, 127,	     machine_at_neat_ami_init, NULL,			nvr_at_close		},
    { "[286 ISA] Award 286 clone",		ROM_AWARD286,		"award286",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_AT,										512,16384, 128, 127,		 machine_at_scat_init, NULL,			nvr_at_close		},
    { "[286 ISA] Commodore PC 30 III",		ROM_CMDPC30,		"cmdpc30",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_AT,										640,16384, 128, 127,		machine_at_cmdpc_init, NULL,			nvr_at_close		},
    { "[286 ISA] Compaq Portable II",		ROM_PORTABLEII,		"portableii",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_AT,										640,16384, 128, 127,	       machine_at_compaq_init, NULL,			nvr_at_close		},
#if defined(DEV_BRANCH) && defined(USE_PORTABLE3)
    { "[286 ISA] Compaq Portable III",		ROM_PORTABLEIII,	"portableiii",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, MACHINE_ISA | MACHINE_AT | MACHINE_VIDEO,								640,16384, 128, 127,	       machine_at_compaq_init, NULL,			nvr_at_close		},
#endif
    { "[286 ISA] GW-286CT GEAR",		ROM_GW286CT,		"gw286ct",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_AT,										512,16384, 128, 127,		 machine_at_scat_init, NULL,			nvr_at_close		},
    { "[286 ISA] Hyundai Super-286TR",		ROM_SUPER286TR,		"super286tr",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_AT,										512,16384, 128, 127,		 machine_at_scat_init, NULL,			nvr_at_close		},
    { "[286 ISA] IBM AT",			ROM_IBMAT,		"ibmat",		{{"",      cpus_ibmat},       {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_AT,										256,15872, 128,  63,		  machine_at_ibm_init, NULL,			nvr_at_close		},
    { "[286 ISA] IBM PS/1 model 2011",		ROM_IBMPS1_2011,	"ibmps1es",		{{"",      cpus_ps1_m2011},   {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC_PS2,						512,16384, 512, 127,	       machine_ps1_m2011_init, NULL,			nvr_at_close		},
    { "[286 ISA] IBM PS/2 model 30-286",	ROM_IBMPS2_M30_286,	"ibmps2_m30_286",	{{"",      cpus_ps2_m30_286}, {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC_PS2,						  1,   16,   1, 127,	     machine_ps2_m30_286_init, NULL,			nvr_at_close		},
    { "[286 ISA] IBM XT Model 286",		ROM_IBMXT286,		"ibmxt286",		{{"",      cpus_ibmxt286},    {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_AT,										256,15872, 128,  63,		  machine_at_ibm_init, NULL,			nvr_at_close		},
    { "[286 ISA] Samsung SPC-4200P",		ROM_SPC4200P,		"spc4200p",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_AT | MACHINE_PS2,									512, 2048, 128, 127,		 machine_at_scat_init, NULL,			nvr_at_close		},
    { "[286 ISA] Samsung SPC-4216P",		ROM_SPC4216P,		"spc4216p",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_AT | MACHINE_PS2,									  1,    5,   1, 127,		 machine_at_scat_init, NULL,			nvr_at_close		},
#ifdef WALTJE
    { "[286 ISA] OpenAT 286",			ROM_OPENAT,		"open_at",		{{"",      cpus_286},         {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_AT,										512, 4096, 128, 127,		      machine_at_init, NULL,			nvr_at_close		},
#endif
    { "[286 ISA] Toshiba 3100e",		ROM_T3100E,		"t3100e",		{{"",      cpus_286},         {"",    NULL},	     {"",      NULL},	     {"",      NULL},	  {"",      NULL}}, 1, MACHINE_ISA | MACHINE_AT | MACHINE_HDC,								       1024, 5120, 256,  63,	       machine_at_t3100e_init, NULL,			nvr_at_close		},

    { "[286 MCA] IBM PS/2 model 50",		ROM_IBMPS2_M50,		"ibmps2_m50",		{{"",      cpus_ps2_m30_286}, {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 1, MACHINE_MCA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC_PS2,						  1,   10,   1,  63,	    machine_ps2_model_50_init, NULL,			nvr_at_close		},

    { "[386SX ISA] AMI 386SX clone",		ROM_AMI386SX,		"ami386",		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_AT | MACHINE_HDC,									512,16384, 128, 127,	     machine_at_headland_init, NULL,			nvr_at_close		},
    { "[386SX ISA] Amstrad MegaPC",		ROM_MEGAPC,		"megapc",		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, 1, MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_VIDEO | MACHINE_HDC,					  1,   16,   1, 127,	      machine_at_wd76c10_init, NULL,			nvr_at_close		},
    { "[386SX ISA] Award 386SX clone",		ROM_AWARD386SX_OPTI495,	"award386sx",		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_AT | MACHINE_HDC,									  1,   64,   1, 127,	      machine_at_opti495_init, NULL,			nvr_at_close		},
    { "[386SX ISA] DTK 386SX clone",		ROM_DTK386,		"dtk386",		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_AT | MACHINE_HDC,									512,16384, 128, 127,		 machine_at_neat_init, NULL,			nvr_at_close		},
    { "[386SX ISA] IBM PS/1 model 2121",	ROM_IBMPS1_2121,	"ibmps1_2121",		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, 1, MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC | MACHINE_VIDEO,					  1,    6,   1, 127,	       machine_ps1_m2121_init, NULL,			nvr_at_close		},
    { "[386SX ISA] IBM PS/1 m.2121+ISA",	ROM_IBMPS1_2121_ISA,	"ibmps1_2121_isa",	{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC | MACHINE_VIDEO,					  1,    6,   1, 127,	       machine_ps1_m2121_init, NULL,			nvr_at_close		},
    { "[386SX ISA] KMX-C-02",			ROM_KMXC02,		"kmxc02",		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_AT,										512,16384, 512, 127,	       machine_at_scatsx_init, NULL,			nvr_at_close		},

    { "[386SX MCA] IBM PS/2 model 55SX",	ROM_IBMPS2_M55SX,	"ibmps2_m55sx",		{{"Intel", cpus_i386SX},      {"AMD", cpus_Am386SX}, {"Cyrix", cpus_486SLC}, {"",      NULL},     {"",      NULL}}, 1, MACHINE_MCA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC_PS2,						  1,    8,   1,  63,	  machine_ps2_model_55sx_init, NULL,			nvr_at_close		},

    { "[386DX ISA] AMI 386DX clone",		ROM_AMI386DX_OPTI495,	"ami386dx",		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_AT | MACHINE_HDC,									  1,   64,   1, 127,	  machine_at_opti495_ami_init, NULL,			nvr_at_close		},
    { "[386DX ISA] Amstrad MegaPC 386DX",	ROM_MEGAPCDX,		"megapcdx",		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, 1, MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC | MACHINE_VIDEO,					  1,   32,   1, 127,	      machine_at_wd76c10_init, NULL,			nvr_at_close		},
    { "[386DX ISA] Award 386DX clone",		ROM_AWARD386DX_OPTI495,	"award386dx",		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_AT | MACHINE_HDC,									  1,   64,   1, 127,	      machine_at_opti495_init, NULL,			nvr_at_close		},
    { "[386DX ISA] MR 386DX clone",		ROM_MR386DX_OPTI495,	"mr386dx",		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_AT | MACHINE_HDC,									  1,   64,   1, 127,	  machine_at_opti495_ami_init, NULL,			nvr_at_close		},
#if defined(DEV_BRANCH) && defined(USE_PORTABLE3)
    { "[386DX ISA] Compaq Portable III (386)",  ROM_PORTABLEIII386,     "portableiii386",       {{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, 1, MACHINE_ISA | MACHINE_AT | MACHINE_HDC | MACHINE_VIDEO,		                		          1,   14,   1, 127,           machine_at_compaq_init, NULL,			nvr_at_close		},
#endif
    { "[386DX MCA] IBM PS/2 model 70 (type 3)",		ROM_IBMPS2_M70_TYPE3,		"ibmps2_m70_type3",		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, 1, MACHINE_MCA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC_PS2,						  2,   16,   2,  63,	    machine_ps2_model_70_type3_init, NULL,			nvr_at_close		},

    { "[386DX MCA] IBM PS/2 model 80",		ROM_IBMPS2_M80,		"ibmps2_m80",		{{"Intel", cpus_i386DX},      {"AMD", cpus_Am386DX}, {"Cyrix", cpus_486DLC}, {"",      NULL},     {"",      NULL}}, 1, MACHINE_MCA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC_PS2,						  1,   12,   1,  63,	    machine_ps2_model_80_init, NULL,			nvr_at_close		},

    { "[486 ISA] AMI 486 clone",		ROM_AMI486,		"ami486",		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_HDC,							  1,   64,   1, 127,	      machine_at_ali1429_init, NULL,			nvr_at_close		},
    { "[486 ISA] AMI WinBIOS 486",		ROM_WIN486,		"win486",		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_HDC,							  1,   64,   1, 127,	      machine_at_ali1429_init, NULL,			nvr_at_close		},
    { "[486 ISA] Award 486 clone",		ROM_AWARD486_OPTI495,	"award486",		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_HDC,							  1,   64,   1, 127,	      machine_at_opti495_init, NULL,			nvr_at_close		},
    { "[486 ISA] DTK PKM-0038S E-2",		ROM_DTK486,		"dtk486",		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_HDC,							  1,  128,   1, 127,	       machine_at_dtk486_init, NULL,			nvr_at_close		},
    { "[486 ISA] IBM PS/1 model 2133",		ROM_IBMPS1_2133,	"ibmps1_2133",		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  1,   64,   1, 127,	       machine_ps1_m2133_init, NULL,			nvr_at_close		},

    { "[486 MCA] IBM PS/2 model 70 (type 4)",	ROM_IBMPS2_M70_TYPE4,	"ibmps2_m70_type4",	{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, 1, MACHINE_MCA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC_PS2,						  2,   64,   2,  63,	machine_ps2_model_70_type4_init, NULL,			nvr_at_close		},
	
#ifdef WALTJE
    { "[486 MCA] IBM PS/2 model 80-486",	ROM_IBMPS2_M80_486,	"ibmps2_m80-486",	{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, 1, MACHINE_MCA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC_PS2,						  1,   32,   1,  63,	machine_ps2_model_80_486_init, NULL,			nvr_at_close		},
#endif

    { "[486 PCI] Rise Computer R418",		ROM_R418,		"r418",			{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_HDC,					  1,  255,   1, 127,		 machine_at_r418_init, NULL,			nvr_at_close		},

#if defined(DEV_BRANCH) && defined(USE_GREENB)
    { "[486 VLB] Green-B 4GP V3.1",		ROM_4GPV31,		"4gpv31",		{{"Intel", cpus_i486},        {"AMD", cpus_Am486},   {"Cyrix", cpus_Cx486},  {"",      NULL},     {"",      NULL}}, 0, MACHINE_ISA | MACHINE_VLB | MACHINE_AT,							                  1,  128,   1, 127,	       machine_at_4gpv31_init, NULL,			nvr_at_close		},
#endif

    { "[Socket 4 LX] Intel Premiere/PCI",	ROM_REVENGE,		"revenge",		{{"Intel", cpus_Pentium5V},   {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  2,  128,   2, 127,	       machine_at_batman_init, NULL,			nvr_at_close		},

#if defined(DEV_BRANCH) && defined(USE_AMD_K)
    { "[Socket 5 NX] Intel Premiere/PCI II",	ROM_PLATO,		"plato",		{{ "Intel", cpus_PentiumS5},  {"IDT", cpus_WinChip}, {"AMD",   cpus_K5},     {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  2,  128,   2, 127,		machine_at_plato_init, NULL,			nvr_at_close		},

    { "[Socket 5 FX] ASUS P/I-P54TP4XE",	ROM_P54TP4XE,		"p54tp4xe",		{{ "Intel", cpus_PentiumS5},  {"IDT", cpus_WinChip}, {"AMD",   cpus_K5},     {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_HDC,					  8,  128,   8, 127,	     machine_at_p54tp4xe_init, NULL,			nvr_at_close		},
    { "[Socket 5 FX] Intel Advanced/EV",	ROM_ENDEAVOR,		"endeavor",		{{ "Intel", cpus_PentiumS5},  {"IDT", cpus_WinChip}, {"AMD",   cpus_K5},     {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC | MACHINE_VIDEO,	  8,  128,   8, 127,	     machine_at_endeavor_init, at_endeavor_get_device,	nvr_at_close		},
    { "[Socket 5 FX] Intel Advanced/ZP",	ROM_ZAPPA,		"zappa",		{{ "Intel", cpus_PentiumS5},  {"IDT", cpus_WinChip}, {"AMD",   cpus_K5},     {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  8,  128,   8, 127,		machine_at_zappa_init, NULL,			nvr_at_close		},
    { "[Socket 5 FX] PC Partner MB500N",	ROM_MB500N,		"mb500n",		{{ "Intel", cpus_PentiumS5},  {"IDT", cpus_WinChip}, {"AMD",   cpus_K5},     {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_HDC,					  8,  128,   8, 127,	       machine_at_mb500n_init, NULL,			nvr_at_close		},
    { "[Socket 5 FX] President Award 430FX PCI",ROM_PRESIDENT,		"president",		{{ "Intel", cpus_PentiumS5},  {"IDT", cpus_WinChip}, {"AMD",   cpus_K5},     {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_HDC,					  8,  128,   8, 127,	    machine_at_president_init, NULL,			nvr_at_close		},

    { "[Socket 7 FX] Intel Advanced/ATX",	ROM_THOR,		"thor",			{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"AMD",   cpus_K56},    {"Cyrix", cpus_6x86},{"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  8,  128,   8, 127,		 machine_at_thor_init, NULL,			nvr_at_close		},
    { "[Socket 7 FX] MR Intel Advanced/ATX",	ROM_MRTHOR,		"mrthor",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"AMD",   cpus_K56},    {"Cyrix", cpus_6x86},{"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  8,  128,   8, 127,		 machine_at_thor_init, NULL,			nvr_at_close		},

    { "[Socket 7 HX] Acer M3a",			ROM_ACERM3A,		"acerm3a",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"AMD",   cpus_K56},    {"Cyrix", cpus_6x86},{"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  8,  192,   8, 127,	      machine_at_acerm3a_init, NULL,			nvr_at_close		},
    { "[Socket 7 HX] Acer V35n",		ROM_ACERV35N,		"acerv35n",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"AMD",   cpus_K56},    {"Cyrix", cpus_6x86},{"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  8,  192,   8, 127,	     machine_at_acerv35n_init, NULL,			nvr_at_close		},
    { "[Socket 7 HX] AOpen AP53",		ROM_AP53,		"ap53",			{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"AMD",   cpus_K56},    {"Cyrix", cpus_6x86},{"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  8,  512,   8, 127,		 machine_at_ap53_init, NULL,			nvr_at_close		},
    { "[Socket 7 HX] ASUS P/I-P55T2P4",		ROM_P55T2P4,		"p55t2p4",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"AMD",   cpus_K56},    {"Cyrix", cpus_6x86},{"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  8,  512,   8, 127,	      machine_at_p55t2p4_init, NULL,			nvr_at_close		},
    { "[Socket 7 HX] SuperMicro Super P55T2S",	ROM_P55T2S,		"p55t2s",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"AMD",   cpus_K56},    {"Cyrix", cpus_6x86},{"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  8,  768,   8, 127,	       machine_at_p55t2s_init, NULL,			nvr_at_close		},

    { "[Socket 7 VX] ASUS P/I-P55TVP4",		ROM_P55TVP4,		"p55tvp4",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"AMD",   cpus_K56},    {"Cyrix", cpus_6x86},{"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  8,  128,   8, 127,	      machine_at_p55tvp4_init, NULL,			nvr_at_close		},
    { "[Socket 7 VX] Award 430VX PCI",		ROM_430VX,		"430vx",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"AMD",   cpus_K56},    {"Cyrix", cpus_6x86},{"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  8,  128,   8, 127,	       machine_at_i430vx_init, NULL,			nvr_at_close		},
    { "[Socket 7 VX] Epox P55-VA",		ROM_P55VA,		"p55va",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"AMD",   cpus_K56},    {"Cyrix", cpus_6x86},{"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  8,  128,   8, 127,		machine_at_p55va_init, NULL,			nvr_at_close		},
#else
    { "[Socket 5 NX] Intel Premiere/PCI II",	ROM_PLATO,		"plato",		{{ "Intel", cpus_PentiumS5},  {"IDT", cpus_WinChip}, {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  2,  128,   2, 127,		machine_at_plato_init, NULL,			nvr_at_close		},

    { "[Socket 5 FX] ASUS P/I-P54TP4XE",	ROM_P54TP4XE,		"p54tp4xe",		{{ "Intel", cpus_PentiumS5},  {"IDT", cpus_WinChip}, {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_HDC,					  8,  128,   8, 127,	     machine_at_p54tp4xe_init, NULL,			nvr_at_close		},
    { "[Socket 5 FX] Intel Advanced/EV",	ROM_ENDEAVOR,		"endeavor",		{{ "Intel", cpus_PentiumS5},  {"IDT", cpus_WinChip}, {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC | MACHINE_VIDEO,	  8,  128,   8, 127,	     machine_at_endeavor_init, at_endeavor_get_device,	nvr_at_close		},
    { "[Socket 5 FX] Intel Advanced/ZP",	ROM_ZAPPA,		"zappa",		{{ "Intel", cpus_PentiumS5},  {"IDT", cpus_WinChip}, {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  8,  128,   8, 127,		machine_at_zappa_init, NULL,			nvr_at_close		},
    { "[Socket 5 FX] PC Partner MB500N",	ROM_MB500N,		"mb500n",		{{ "Intel", cpus_PentiumS5},  {"IDT", cpus_WinChip}, {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_HDC,					  8,  128,   8, 127,	       machine_at_mb500n_init, NULL,			nvr_at_close		},
    { "[Socket 5 FX] President Award 430FX PCI",ROM_PRESIDENT,		"president",		{{ "Intel", cpus_PentiumS5},  {"IDT", cpus_WinChip}, {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_HDC,					  8,  128,   8, 127,	    machine_at_president_init, NULL,			nvr_at_close		},

    { "[Socket 7 FX] Intel Advanced/ATX",	ROM_THOR,		"thor",			{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"Cyrix", cpus_6x86},   {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  8,  128,   8, 127,		 machine_at_thor_init, NULL,			nvr_at_close		},
    { "[Socket 7 FX] MR Intel Advanced/ATX",	ROM_MRTHOR,		"mrthor",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"Cyrix", cpus_6x86},   {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  8,  128,   8, 127,		 machine_at_thor_init, NULL,			nvr_at_close		},

    { "[Socket 7 HX] Acer M3a",			ROM_ACERM3A,		"acerm3a",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"Cyrix", cpus_6x86},   {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  8,  192,   8, 127,	      machine_at_acerm3a_init, NULL,			nvr_at_close		},
    { "[Socket 7 HX] Acer V35n",		ROM_ACERV35N,		"acerv35n",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"Cyrix", cpus_6x86},   {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  8,  192,   8, 127,	     machine_at_acerv35n_init, NULL,			nvr_at_close		},
    { "[Socket 7 HX] AOpen AP53",		ROM_AP53,		"ap53",			{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"Cyrix", cpus_6x86},   {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  8,  512,   8, 127,		 machine_at_ap53_init, NULL,			nvr_at_close		},
    { "[Socket 7 HX] ASUS P/I-P55T2P4",		ROM_P55T2P4,		"p55t2p4",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"Cyrix", cpus_6x86},   {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  8,  512,   8, 127,	      machine_at_p55t2p4_init, NULL,			nvr_at_close		},
    { "[Socket 7 HX] SuperMicro Super P55T2S",	ROM_P55T2S,		"p55t2s",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"Cyrix", cpus_6x86},   {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  8,  768,   8, 127,	       machine_at_p55t2s_init, NULL,			nvr_at_close		},

    { "[Socket 7 VX] ASUS P/I-P55TVP4",		ROM_P55TVP4,		"p55tvp4",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"Cyrix", cpus_6x86},   {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  8,  128,   8, 127,	      machine_at_p55tvp4_init, NULL,			nvr_at_close		},
    { "[Socket 7 VX] Award 430VX PCI",		ROM_430VX,		"430vx",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"Cyrix", cpus_6x86},   {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  8,  128,   8, 127,	       machine_at_i430vx_init, NULL,			nvr_at_close		},
    { "[Socket 7 VX] Epox P55-VA",		ROM_P55VA,		"p55va",		{{"Intel", cpus_Pentium},     {"IDT", cpus_WinChip}, {"Cyrix", cpus_6x86},   {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_VLB | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,			  8,  128,   8, 127,		machine_at_p55va_init, NULL,			nvr_at_close		},
#endif

#if defined(DEV_BRANCH) && defined(USE_I686)
    { "[Socket 8 FX] Tyan Titan-Pro AT",	ROM_440FX,		"440fx",		{{"Intel", cpus_PentiumPro},  {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8, 1024,   8, 127,	       machine_at_i440fx_init, NULL,			nvr_at_close		},
    { "[Socket 8 FX] Tyan Titan-Pro ATX",	ROM_S1668,		"tpatx",		{{"Intel", cpus_PentiumPro},  {"",    NULL},         {"",      NULL},        {"",      NULL},     {"",      NULL}}, 0, MACHINE_PCI | MACHINE_ISA | MACHINE_AT | MACHINE_PS2 | MACHINE_HDC,					  8, 1024,   8, 127,		machine_at_s1668_init, NULL,			nvr_at_close		},
#endif
    { "",				-1,				"",			{{"", 0},                     {"", 0},               {"", 0}},			0,0,0,0, 0																														}
};


int
machine_count(void)
{
    return((sizeof(machines) / sizeof(machine)) - 1);
}


int
machine_getromset(void)
{
    return(machines[machine].id);
}


int
machine_getromset_ex(int m)
{
    return(machines[m].id);
}


int
machine_getmachine(int romset)
{
    int c = 0;

    while (machines[c].id != -1) {
	if (machines[c].id == romset)
		return(c);
	c++;
    }

    return(0);
}


char *
machine_getname(void)
{
    return((char *)machines[machine].name);
}


device_t *
machine_getdevice(int machine)
{
    if (machines[machine].get_device)
	return(machines[machine].get_device());

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

    while (machines[c].id != -1) {
	if (!strcmp(machines[c].internal_name, (const char *)s))
		return(c);
	c++;
    }

    return(0);
}


void
machine_close(void)
{
    if (machines[machine].nvr_close)
    	machines[machine].nvr_close();
}
