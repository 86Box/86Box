/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the ROM image handler.
 *
 * Version:	@(#)rom.h	1.0.15	2018/03/02
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2018 Fred N. van Kempen.
 */
#ifndef EMU_ROM_H
# define EMU_ROM_H


#define PCJR	(romset==ROM_IBMPCJR)
#if defined(DEV_BRANCH) && defined(USE_GREENB)
#define AMIBIOS	(romset==ROM_AMI386SX || \
		 romset==ROM_AMI486 || \
		 romset==ROM_WIN486 || \
		 romset==ROM_4GPV31)
#else
#define AMIBIOS	(romset==ROM_AMI386SX || \
		 romset==ROM_AMI486 || \
		 romset==ROM_WIN486)
#endif


typedef struct {
    uint8_t		*rom;
    uint32_t		mask;
    mem_mapping_t	mapping;
} rom_t;


enum {
    ROM_IBMPC = 0,	/* 301 keyboard error, 131 cassette (!!!) error */
    ROM_AMIXT,		/* XT Clone with AMI BIOS */
    ROM_DTKXT,
    ROM_IBMXT,		/* 301 keyboard error */
    ROM_GENXT,		/* 'Generic XT BIOS' */
    ROM_JUKOPC,
    ROM_PORTABLE,
    ROM_PXXT,
#if defined(DEV_BRANCH) && defined(USE_LASERXT)
    ROM_LTXT,

    ROM_LXT3,
#endif

    ROM_T1000,
    ROM_T1200,

    ROM_IBMPCJR,
    ROM_TANDY,
    ROM_TANDY1000HX,
    ROM_EUROPC,
    ROM_PC1512,
    ROM_PC1640,
    ROM_PC200,
    ROM_PC2086,
    ROM_PC3086,        
    ROM_OLIM24,
    ROM_TANDY1000SL2,

    ROM_T3100E,

    ROM_AMI286,
    ROM_AWARD286,
    ROM_CMDPC30,
    ROM_PORTABLEII,
#if defined(DEV_BRANCH) && defined(USE_PORTABLE3)
    ROM_PORTABLEIII,
#endif
    ROM_GW286CT,
    ROM_SUPER286TR,	/* Hyundai Super-286TR/SCAT/Award */
    ROM_IBMAT,
    ROM_IBMPS1_2011,
    ROM_IBMPS2_M30_286,
    ROM_IBMXT286,
    ROM_SPC4200P,	/* Samsung SPC-4200P/SCAT/Phoenix */
    ROM_SPC4216P,	/* Samsung SPC-4216P/SCAT */
#ifdef WALTJE
    ROM_OPENAT,		/* PC/AT clone with Open BIOS */
#endif

    ROM_IBMPS2_M50,

    ROM_AMI386SX,
    ROM_KMXC02,
    ROM_MEGAPC,
    ROM_AWARD386SX_OPTI495,
#if defined(DEV_BRANCH) && defined(USE_PORTABLE3)
    ROM_DESKPRO_386,
#endif
    ROM_DTK386,
    ROM_IBMPS1_2121,
    ROM_IBMPS1_2121_ISA,/* IBM PS/1 Model 2121 with ISA expansion bus */
#if defined(DEV_BRANCH) && defined(USE_PORTABLE3)
    ROM_PORTABLEIII386,
#endif

    ROM_IBMPS2_M55SX,

    ROM_AMI386DX_OPTI495,
    ROM_MEGAPCDX,	/* 386DX mdl - Note: documentation (in German) clearly says such a model exists */
    ROM_AWARD386DX_OPTI495,
    ROM_MR386DX_OPTI495,

    ROM_IBMPS2_M80,

    ROM_AMI486,
    ROM_WIN486,
#ifdef UNIMPLEMENTED_MACHINES
    ROM_VLI486SV2G,	/* ASUS VL/I-486SV2G/SiS 471/Award/SiS 85C471 */	/* 51 */
#endif
    ROM_AWARD486_OPTI495,
    ROM_DTK486,		/* DTK PKM-0038S E-2/SiS 471/Award/SiS 85C471 */
#if defined(DEV_BRANCH) && defined(USE_GREENB)
    ROM_4GPV31,		/* Green-B 4GPV3.1 ISA/VLB 486/Pentium, AMI */
#endif
    ROM_IBMPS1_2133,

#ifdef WALTJE
    ROM_IBMPS2_M80_486,
#endif

    ROM_R418,		/* Rise Computer R418/SiS 496/497/Award/SMC FDC37C665 */

    ROM_REVENGE,	/* Intel Premiere/PCI I/430LX/AMI/SMC FDC37C665 */

    ROM_PLATO,		/* Intel Premiere/PCI II/430NX/AMI/SMC FDC37C665 */

    ROM_P54TP4XE,	/* ASUS P/I-P55TP4XE/430FX/Award/SMC FDC37C665 */
    ROM_ENDEAVOR,	/* Intel Advanced_EV/430FX/AMI/NS PC87306 */
    ROM_ZAPPA,		/* Intel Advanced_ZP/430FX/AMI/NS PC87306 */
#ifdef UNIMPLEMENTED_MACHINES
    ROM_POWERMATE_V,	/* NEC PowerMate V/430FX/Phoenix/SMC FDC37C66 5*/
#endif
    ROM_MB500N,		/* PC Partner MB500N/430FX/Award/SMC FDC37C665 */
    ROM_PRESIDENT,	/* President Award 430FX PCI/430FX/Award/Unknown SIO */

    ROM_THOR,		/* Intel Advanced_ATX/430FX/AMI/NS PC87306 */
    ROM_MRTHOR,		/* Intel Advanced_ATX/430FX/MR.BIOS/NS PC87306 */

    ROM_ACERM3A,	/* Acer M3A/430HX/Acer/SMC FDC37C932FR */
    ROM_ACERV35N,	/* Acer V35N/430HX/Acer/SMC FDC37C932FR */
    ROM_AP53,		/* AOpen AP53/430HX/AMI/SMC FDC37C665/669 */
    ROM_P55T2P4,	/* ASUS P/I-P55T2P4/430HX/Award/Winbond W8387F*/
    ROM_P55T2S,		/* ASUS P/I-P55T2S/430HX/AMI/NS PC87306 */

    ROM_P55TVP4,	/* ASUS P/I-P55TVP4/430HX/Award/Winbond W8387F*/
    ROM_430VX,		/* Award 430VX PCI/430VX/Award/UMC UM8669F*/
    ROM_P55VA,		/* Epox P55-VA/430VX/Award/SMC FDC37C932FR*/

#if defined(DEV_BRANCH) && defined(USE_I686)
    ROM_440FX,		/* Tyan Titan-Pro AT/440FX/Award BIOS/SMC FDC37C665 */
    ROM_S1668,		/* Tyan Titan-Pro ATX/440FX/AMI/SMC FDC37C669 */
#endif

    ROM_MAX
};


extern int	romspresent[ROM_MAX];

extern uint8_t	rom_read(uint32_t addr, void *p);
extern uint16_t	rom_readw(uint32_t addr, void *p);
extern uint32_t	rom_readl(uint32_t addr, void *p);

extern FILE	*rom_fopen(wchar_t *fn, wchar_t *mode);
extern int	rom_getfile(wchar_t *fn, wchar_t *s, int size);
extern int	rom_present(wchar_t *fn);

extern int	rom_load_linear(wchar_t *fn, uint32_t addr, int sz,
				int off, uint8_t *ptr);
extern int	rom_load_interleaved(wchar_t *fnl, wchar_t *fnh, uint32_t addr,
				     int sz, int off, uint8_t *ptr);

extern int	rom_init(rom_t *rom, wchar_t *fn, uint32_t address, int size,
			 int mask, int file_offset, uint32_t flags);
extern int	rom_init_interleaved(rom_t *rom, wchar_t *fn_low,
				     wchar_t *fn_high, uint32_t address,
				     int size, int mask, int file_offset,
				     uint32_t flags);

extern int	rom_load_bios(int rom_id);


#endif	/*EMU_ROM_H*/
