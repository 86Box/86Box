/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		General include file.
 *
 * Version:	@(#)ibm.h	1.0.1	2017/06/03
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#define printf pclog


/*Memory*/
extern uint8_t *ram;
extern uint32_t rammask;

extern int readlookup[256],readlookupp[256];
extern uintptr_t *readlookup2;
extern int readlnext;
extern int writelookup[256],writelookupp[256];
extern uintptr_t *writelookup2;
extern int writelnext;

extern int mmu_perm;

#define readmemb(a) ((readlookup2[(a)>>12]==-1)?readmembl(a):*(uint8_t *)(readlookup2[(a) >> 12] + (a)))
#define readmemw(s,a) ((readlookup2[(uint32_t)((s)+(a))>>12]==-1 || (s)==0xFFFFFFFF || (((s)+(a)) & 1))?readmemwl(s,a):*(uint16_t *)(readlookup2[(uint32_t)((s)+(a))>>12]+(uint32_t)((s)+(a))))
#define readmeml(s,a) ((readlookup2[(uint32_t)((s)+(a))>>12]==-1 || (s)==0xFFFFFFFF || (((s)+(a)) & 3))?readmemll(s,a):*(uint32_t *)(readlookup2[(uint32_t)((s)+(a))>>12]+(uint32_t)((s)+(a))))

extern uint8_t	readmembl(uint32_t addr);
extern void	writemembl(uint32_t addr, uint8_t val);
extern uint8_t	readmemb386l(uint32_t seg, uint32_t addr);
extern void	writememb386l(uint32_t seg, uint32_t addr, uint8_t val);
extern uint16_t	readmemwl(uint32_t seg, uint32_t addr);
extern void	writememwl(uint32_t seg, uint32_t addr, uint16_t val);
extern uint32_t	readmemll(uint32_t seg, uint32_t addr);
extern void	writememll(uint32_t seg, uint32_t addr, uint32_t val);
extern uint64_t	readmemql(uint32_t seg, uint32_t addr);
extern void	writememql(uint32_t seg, uint32_t addr, uint64_t val);

extern uint8_t	*getpccache(uint32_t a);
extern uint32_t	mmutranslatereal(uint32_t addr, int rw);
extern void	addreadlookup(uint32_t virt, uint32_t phys);
extern void	addwritelookup(uint32_t virt, uint32_t phys);


/*IO*/
extern uint8_t	inb(uint16_t port);
extern void	outb(uint16_t port, uint8_t  val);
extern uint16_t	inw(uint16_t port);
extern void	outw(uint16_t port, uint16_t val);
extern uint32_t	inl(uint16_t port);
extern void	outl(uint16_t port, uint32_t val);

extern int shadowbios,shadowbios_write;
extern int mem_size;
extern int readlnum,writelnum;


/*Processor*/
#define EAX cpu_state.regs[0].l
#define ECX cpu_state.regs[1].l
#define EDX cpu_state.regs[2].l
#define EBX cpu_state.regs[3].l
#define ESP cpu_state.regs[4].l
#define EBP cpu_state.regs[5].l
#define ESI cpu_state.regs[6].l
#define EDI cpu_state.regs[7].l
#define AX cpu_state.regs[0].w
#define CX cpu_state.regs[1].w
#define DX cpu_state.regs[2].w
#define BX cpu_state.regs[3].w
#define SP cpu_state.regs[4].w
#define BP cpu_state.regs[5].w
#define SI cpu_state.regs[6].w
#define DI cpu_state.regs[7].w
#define AL cpu_state.regs[0].b.l
#define AH cpu_state.regs[0].b.h
#define CL cpu_state.regs[1].b.l
#define CH cpu_state.regs[1].b.h
#define DL cpu_state.regs[2].b.l
#define DH cpu_state.regs[2].b.h
#define BL cpu_state.regs[3].b.l
#define BH cpu_state.regs[3].b.h

typedef union
{
        uint32_t l;
        uint16_t w;
        struct
        {
                uint8_t l,h;
        } b;
} x86reg;

typedef struct
{
        uint32_t base;
        uint32_t limit;
        uint8_t access;
        uint16_t seg;
        uint32_t limit_low, limit_high;
        int checked; /*Non-zero if selector is known to be valid*/
} x86seg;

typedef union MMX_REG
{
        uint64_t q;
        int64_t  sq;
        uint32_t l[2];
        int32_t  sl[2];
        uint16_t w[4];
        int16_t  sw[4];
        uint8_t  b[8];
        int8_t   sb[8];
} MMX_REG;

struct
{
        x86reg regs[8];

        uint8_t tag[8];

        x86seg *ea_seg;
        uint32_t eaaddr;

        int flags_op;
        uint32_t flags_res;
        uint32_t flags_op1, flags_op2;
        
        uint32_t pc;
        uint32_t oldpc;
        uint32_t op32;  

	int TOP;
        
        union
        {
                struct
                {
                        int8_t rm, mod, reg;
                } rm_mod_reg;
                uint32_t rm_mod_reg_data;
        } rm_data;
        
        int8_t ssegs;
	int8_t ismmx;
	int8_t abrt;

	int _cycles;
	int cpu_recomp_ins;
        
        uint16_t npxs, npxc;

        double ST[8];        
     
        uint16_t MM_w4[8];
        
        MMX_REG MM[8];
        
        uint16_t old_npxc, new_npxc;
	uint32_t last_ea;
} cpu_state;

#define cycles cpu_state._cycles

extern uint32_t cpu_cur_status;

#define CPU_STATUS_USE32   (1 << 0)
#define CPU_STATUS_STACK32 (1 << 1)
#define CPU_STATUS_FLATDS  (1 << 2)
#define CPU_STATUS_FLATSS  (1 << 3)

#define cpu_rm  cpu_state.rm_data.rm_mod_reg.rm
#define cpu_mod cpu_state.rm_data.rm_mod_reg.mod
#define cpu_reg cpu_state.rm_data.rm_mod_reg.reg

#ifdef __MSC__
# define COMPILE_TIME_ASSERT(expr)	/*nada*/
#else
# define COMPILE_TIME_ASSERT(expr) typedef char COMP_TIME_ASSERT[(expr) ? 1 : 0];
#endif

COMPILE_TIME_ASSERT(sizeof(cpu_state) <= 128)

#define cpu_state_offset(MEMBER) ((uint8_t)((uintptr_t)&cpu_state.MEMBER - (uintptr_t)&cpu_state - 128))

/*x86reg regs[8];*/

extern uint16_t flags,eflags;
extern uint32_t oldds,oldss,olddslimit,oldsslimit,olddslimitw,oldsslimitw;

extern int ins,output;
extern int cycdiff;

extern x86seg gdt,ldt,idt,tr;
extern x86seg _cs,_ds,_es,_ss,_fs,_gs;
extern x86seg _oldds;

extern uint32_t pccache;
extern uint8_t *pccache2;
/*Segments -
  _cs,_ds,_es,_ss are the segment structures
  CS,DS,ES,SS is the 16-bit data
  cs,ds,es,ss are defines to the bases*/
#define CS _cs.seg
#define DS _ds.seg
#define ES _es.seg
#define SS _ss.seg
#define FS _fs.seg
#define GS _gs.seg
#define cs _cs.base
#define ds _ds.base
#define es _es.base
#define ss _ss.base
#define seg_fs _fs.base
#define gs _gs.base

#define CPL ((_cs.access>>5)&3)

void loadseg(uint16_t seg, x86seg *s);
void loadcs(uint16_t seg);

union
{
        uint32_t l;
        uint16_t w;
} CR0;

#define cr0 CR0.l
#define msw CR0.w

extern uint32_t cr2, cr3, cr4;
extern uint32_t dr[8];

#define C_FLAG  0x0001
#define P_FLAG  0x0004
#define A_FLAG  0x0010
#define Z_FLAG  0x0040
#define N_FLAG  0x0080
#define T_FLAG  0x0100
#define I_FLAG  0x0200
#define D_FLAG  0x0400
#define V_FLAG  0x0800
#define NT_FLAG 0x4000
#define VM_FLAG 0x0002 /*In EFLAGS*/

#define WP_FLAG 0x10000 /*In CR0*/

#define IOPL ((flags>>12)&3)

#define IOPLp ((!(msw&1)) || (CPL<=IOPL))

extern int cycles_lost;
extern int israpidcad;
extern int is486;
extern int is_pentium;
extern uint8_t opcode;
extern int insc;
extern int fpucount;
extern float mips,flops;
extern int clockrate;
extern int cgate16;
extern int CPUID;

extern int cpl_override;

/*Timer*/
typedef struct PIT_nr
{
        int nr;
        struct PIT *pit;
} PIT_nr;

typedef struct PIT
{
        uint32_t l[3];
        int c[3];
        uint8_t m[3];
        uint8_t ctrl,ctrls[3];
        int wp,rm[3],wm[3];
        uint16_t rl[3];
        int thit[3];
        int delay[3];
        int rereadlatch[3];
        int gate[3];
        int out[3];
        int running[3];
        int enabled[3];
        int newcount[3];
        int count[3];
        int using_timer[3];
        int initial[3];
        int latched[3];
        int disabled[3];
        
        uint8_t read_status[3];
        int do_read_status[3];
        
        PIT_nr pit_nr[3];
        
        void (*set_out_funcs[3])(int new_out, int old_out);
} PIT;

PIT pit, pit2;
extern void	setpitclock(float clock);
extern float	pit_timer0_freq(void);



/*DMA*/
typedef struct DMA
{
        uint32_t ab[4],ac[4];
        uint16_t cb[4];
        int cc[4];
        int wp;
        uint8_t m,mode[4];
        uint8_t page[4];
        uint8_t stat;
        uint8_t command;
        uint8_t request;
        
        int xfr_command, xfr_channel;
        int byte_ptr;
        
        int is_ps2;
	uint8_t arb_level[4];
	uint8_t ps2_mode[4];
} DMA;

extern DMA dma, dma16;


/*PPI*/
typedef struct PPI
{
        int s2;
        uint8_t pa,pb;
} PPI;

extern PPI ppi;


/*PIC*/
typedef struct PIC
{
        uint8_t icw1,icw4,mask,ins,pend,mask2;
        int icw;
        uint8_t vector;
        int read;
} PIC;

extern PIC pic, pic2;
extern int pic_intpending;


extern int disctime;
extern wchar_t discfns[4][256];
extern int driveempty[4];

#define MDA ((gfxcard==GFX_MDA || gfxcard==GFX_HERCULES || gfxcard==GFX_HERCULESPLUS || gfxcard==GFX_INCOLOR || gfxcard==GFX_GENIUS) && (romset<ROM_TANDY || romset>=ROM_IBMAT))
#define VGA ((gfxcard>=GFX_TVGA || romset==ROM_ACER386) && gfxcard!=GFX_COLORPLUS && gfxcard!=GFX_INCOLOR && gfxcard!=GFX_WY700 && gfxcard!=GFX_GENIUS && gfxcard!=GFX_COMPAQ_EGA && gfxcard!=GFX_SUPER_EGA && gfxcard!=GFX_HERCULESPLUS && romset!=ROM_PC1640 && romset!=ROM_PC1512 && romset!=ROM_TANDY && romset!=ROM_PC200)
#define PCJR (romset == ROM_IBMPCJR)
#define AMIBIOS (romset==ROM_AMI386SX || romset==ROM_AMI486 || romset == ROM_WIN486)

int GAMEBLASTER, GUS, SSI2001, voodoo_enabled, buslogic_enabled;
extern int AMSTRAD, AT, is286, is386, PCI, TANDY;

enum
{
        ROM_IBMPC = 0,  /*301 keyboard error, 131 cassette (!!!) error*/
        ROM_IBMXT,      /*301 keyboard error*/
        ROM_IBMPCJR,
        ROM_GENXT,      /*'Generic XT BIOS'*/
        ROM_DTKXT,
        ROM_EUROPC,
        ROM_OLIM24,
        ROM_TANDY,
        ROM_PC1512,
        ROM_PC200,
        ROM_PC1640,
        ROM_PC2086,
        ROM_PC3086,        
        ROM_AMIXT,      /*XT Clone with AMI BIOS*/
	ROM_LTXT,
	ROM_LXT3,
	ROM_PX386,
        ROM_DTK386,
        ROM_PXXT,
        ROM_JUKOPC,
        ROM_TANDY1000HX,
        ROM_TANDY1000SL2,
        ROM_IBMAT,
        ROM_CMDPC30,
        ROM_AMI286,
        ROM_AWARD286,
        ROM_DELL200,
        ROM_MISC286,
        ROM_IBMAT386,
        ROM_ACER386,
        ROM_MEGAPC,
        ROM_AMI386SX,
        ROM_AMI486,
        ROM_WIN486,
        ROM_PCI486,
        ROM_SIS496,
        ROM_430VX,
        ROM_ENDEAVOR,
        ROM_REVENGE,
        ROM_IBMPS1_2011,
        ROM_DESKPRO_386,
		ROM_PORTABLE,
		ROM_PORTABLEII,
		ROM_PORTABLEIII,
		ROM_PORTABLEIII386, /* The original Compaq Portable III shipped with an Intel 80286 CPU, but later switched to a 386DX. */
        ROM_IBMPS1_2121,

        ROM_AMI386DX_OPTI495,
        ROM_MR386DX_OPTI495,

	ROM_IBMPS2_M30_286,
	ROM_IBMPS2_M50,
	ROM_IBMPS2_M55SX,
	ROM_IBMPS2_M80,

        ROM_DTK486,     /*DTK PKM-0038S E-2 / SiS 471 / Award BIOS / SiS 85C471*/
        ROM_VLI486SV2G, /*ASUS VL/I-486SV2G / SiS 471 / Award BIOS / SiS 85C471*/
        ROM_R418,       /*Rise Computer R418 / SiS 496/497 / Award BIOS / SMC FDC37C665*/
        ROM_586MC1,     /*Micro Star 586MC1 MS-5103 / 430LX / Award BIOS*/
	ROM_PLATO,      /*Intel Premiere/PCI II / 430NX / AMI BIOS / SMC FDC37C665*/
        ROM_MB500N,     /*PC Partner MB500N / 430FX / Award BIOS / SMC FDC37C665*/
        ROM_P54TP4XE,   /*ASUS P/I-P55TP4XE / 430FX / Award BIOS / SMC FDC37C665*/
	ROM_AP53,       /*AOpen AP53 / 430HX / AMI BIOS / SMC FDC37C665/669*/
	ROM_P55T2S,     /*ASUS P/I-P55T2S / 430HX / AMI BIOS / National Semiconductors PC87306*/
	ROM_ACERM3A,    /*Acer M3A / 430HX / Acer BIOS / SMC FDC37C932FR*/
	ROM_ACERV35N,   /*Acer V35N / 430HX / Acer BIOS / SMC FDC37C932FR*/
        ROM_P55T2P4,    /*ASUS P/I-P55T2P4 / 430HX / Award BIOS / Winbond W8387F*/
        ROM_P55TVP4,    /*ASUS P/I-P55TVP4 / 430HX / Award BIOS / Winbond W8387F*/
        ROM_P55VA,      /*Epox P55-VA / 430VX / Award BIOS / SMC FDC37C932FR*/

	ROM_440FX,	/*Tyan Titan-Pro AT / 440FX / Award BIOS / SMC FDC37C665*/

        ROM_MARL,	/*Intel Advanced/ML / 430HX / AMI BIOS / National Semiconductors PC87306*/
        ROM_THOR,	/*Intel Advanced/ATX / 430FX / AMI BIOS / National Semiconductors PC87306*/
        ROM_MRTHOR,	/*Intel Advanced/ATX / 430FX / MR.BIOS / National Semiconductors PC87306*/
        ROM_POWERMATE_V,/*NEC PowerMate V / 430FX / Phoenix BIOS / SMC FDC37C665*/

        ROM_IBMPS1_2121_ISA,/*IBM PS/1 Model 2121 with ISA expansion bus*/

        ROM_SPC4200P,	/*Samsung SPC-4200P / SCAT / Phoenix BIOS*/
        ROM_SUPER286TR,	/*Hyundai Super-286TR / SCAT / Award BIOS*/

        ROM_MEGAPCDX,	/*386DX mdoel of the Mega PC - Note by Tohka: The documentation (that I have in German) clearly says such a model exists.*/
        ROM_ZAPPA,	/*Intel Advanced/ZP / 430FX / AMI BIOS / National Semiconductors PC87306*/

        ROM_CMDPC60,

        ROM_S1668,      /*Tyan Titan-Pro ATX / 440FX / AMI BIOS / SMC FDC37C669*/
        ROM_IBMPS1_2133,

        ROM_MAX
};

extern int romspresent[ROM_MAX];

extern int hasfpu;
extern int romset;

enum
{
        GFX_CGA = 0,
        GFX_MDA,
        GFX_HERCULES,
        GFX_EGA,        /*Using IBM EGA BIOS*/
        GFX_TVGA,       /*Using Trident TVGA8900D BIOS*/
        GFX_ET4000,     /*Tseng ET4000*/
        GFX_ET4000W32,  /*Tseng ET4000/W32p (Diamond Stealth 32)*/
        GFX_BAHAMAS64,  /*S3 Vision864 (Paradise Bahamas 64)*/
        GFX_N9_9FX,     /*S3 764/Trio64 (Number Nine 9FX)*/
        GFX_VIRGE,      /*S3 Virge*/
        GFX_TGUI9440,   /*Trident TGUI9440*/
        GFX_VGA,        /*IBM VGA*/        
        GFX_VGAEDGE16,  /*ATI VGA Edge-16 (18800-1)*/
        GFX_VGACHARGER, /*ATI VGA Charger (28800-5)*/
        GFX_OTI067,     /*Oak OTI-067*/
        GFX_MACH64GX,   /*ATI Graphics Pro Turbo (Mach64)*/
        GFX_CL_GD5429,  /*Cirrus Logic CL-GD5429*/
        GFX_VIRGEDX,    /*S3 Virge/DX*/
        GFX_PHOENIX_TRIO32, /*S3 732/Trio32 (Phoenix)*/
        GFX_PHOENIX_TRIO64, /*S3 764/Trio64 (Phoenix)*/
       	GFX_INCOLOR,	/* Hercules InColor */
	GFX_COLORPLUS,	/* Plantronics ColorPlus */
	GFX_WY700,	/* Wyse 700 */
	GFX_GENIUS,	/* MDSI Genius */
        GFX_MACH64VT2,  /*ATI Mach64 VT2*/

	GFX_COMPAQ_EGA,	/*Compaq EGA*/
	GFX_SUPER_EGA,	/*Using Chips & Technologies SuperEGA BIOS*/
	GFX_COMPAQ_VGA,	/*Compaq/Paradise VGA*/
        GFX_MIRO_VISION964, /*S3 Vision964 (Miro Crystal)*/
	GFX_CL_GD5446,	/*Cirrus Logic CL-GD5446*/
	GFX_VGAWONDERXL,	/*Compaq ATI VGA Wonder XL (28800-5)*/
	GFX_WD90C11,	/*Paradise WD90C11 Standalone*/
        GFX_OTI077,     /*Oak OTI-077*/
	GFX_VGAWONDERXL24,	/*Compaq ATI VGA Wonder XL24 (28800-6)*/
	GFX_STEALTH64,	/*S3 Vision864 (Diamond Stealth 64)*/
	GFX_PHOENIX_VISION864,	/*S3 Vision864 (Phoenix)*/
        GFX_RIVATNT,
        GFX_RIVA128,
        GFX_HERCULESPLUS,
        GFX_RIVATNT2,

	GFX_TRIGEM_UNK,
        GFX_OTI037,	/*Oak OTI-037*/

        GFX_VIRGEVX,    /*S3 Virge/VX*/
        GFX_VIRGEDX4,   /*S3 Virge/DX (VBE 2.0)*/

        GFX_MAX
};

extern int gfx_present[GFX_MAX];

int gfxcard;

int cpuspeed;


/*Video*/
extern int egareads,egawrites;
extern int vid_resize;
extern int vid_api;
extern int winsizex,winsizey;
extern int changeframecount;


/*Sound*/
extern int ppispeakon;
extern float CGACONST;
extern float MDACONST;
extern float VGACONST1,VGACONST2;
extern float RTCCONST;
extern int gated,speakval,speakon;

#define SOUNDBUFLEN (48000/50)


/*Sound Blaster*/
#define SADLIB    1     /*No DSP*/
#define SB1       2     /*DSP v1.05*/
#define SB15      3     /*DSP v2.00*/
#define SB2       4     /*DSP v2.01 - needed for high-speed DMA*/
#define SBPRO     5     /*DSP v3.00*/
#define SBPRO2    6     /*DSP v3.02 + OPL3*/
#define SB16      7     /*DSP v4.05 + OPL3*/
#define SADGOLD   8     /*AdLib Gold*/
#define SND_WSS   9     /*Windows Sound System*/
#define SND_PAS16 10    /*Pro Audio Spectrum 16*/

extern wchar_t pcempath[512];


/*Hard disk*/
enum
{
	HDD_BUS_DISABLED = 0,
	HDD_BUS_MFM,
	HDD_BUS_XTIDE,
	HDD_BUS_RLL,
	HDD_BUS_IDE_PIO_ONLY,
	HDD_BUS_IDE_PIO_AND_DMA,
	HDD_BUS_SCSI,
	HDD_BUS_SCSI_REMOVABLE,
	HDD_BUS_USB
};

#define HDC_NUM		30
#define MFM_NUM		2
#define RLL_NUM		2
#define XTIDE_NUM	2
#define IDE_NUM		8
#define SCSI_NUM	16	/* Theoretically the controller can have at least 64 devices, or even 128 in case of a wide bus, but
				   let's not exaggerate with them - 16 ought to be enough for everyone. */

#pragma pack(push,1)
typedef struct {
	FILE *f;
	uint64_t spt,hpc; /*Sectors per track, heads per cylinder*/
	uint64_t tracks;
	int is_hdi;
	int wp;
	uint32_t base;
	uint64_t at_spt,at_hpc; /*[Translation] Sectors per track, heads per cylinder*/
	unsigned int bus;	/* 0 = none, 1 = MFM/RLL, 2 = IDE, 3 = SCSI */
	unsigned int mfm_channel;
	unsigned int rll_channel;
	unsigned int xtide_channel;
	unsigned int ide_channel;
	unsigned int scsi_id;
	unsigned int scsi_lun;
	wchar_t fn[260];
	wchar_t prev_fn[260];
} hard_disk_t;
#pragma pack(pop)

extern hard_disk_t hdc[HDC_NUM];

uint64_t hdt[128][3];
uint64_t hdt_mfm[128][3];

int image_is_hdi(const wchar_t *s);
int image_is_hdx(const wchar_t *s, int check_signature);

/*Keyboard*/
extern int keybsenddelay;


/*CD-ROM*/
enum
{
	CDROM_BUS_DISABLED = 0,
	CDROM_BUS_ATAPI_PIO_ONLY = 4,
	CDROM_BUS_ATAPI_PIO_AND_DMA,
	CDROM_BUS_SCSI,
	CDROM_BUS_USB = 8
};

extern int idecallback[5];

#define CD_STATUS_EMPTY		0
#define CD_STATUS_DATA_ONLY	1
#define CD_STATUS_PLAYING	2
#define CD_STATUS_PAUSED	3
#define CD_STATUS_STOPPED	4

extern uint32_t SCSIGetCDVolume(int channel);
extern uint32_t SCSIGetCDChannel(int channel);

#define MIN(a, b) 				((a) < (b) ? (a) : (b))
#define ELEMENTS(Array)         (sizeof(Array) / sizeof((Array)[0]))

extern int ui_writeprot[4];


extern int nmi;
extern int nmi_auto_clear;

extern float isa_timing, bus_timing;


extern uint64_t timer_read(void);
extern uint64_t timer_freq;


extern int infocus;

extern void onesec(void);

extern void resetpc_cad(void);

extern int dump_on_exit;
extern int start_in_fullscreen;
extern int window_w, window_h, window_x, window_y, window_remember;

extern uint64_t pmc[2];

extern uint16_t temp_seg_data[4];

extern uint16_t cs_msr;
extern uint32_t esp_msr;
extern uint32_t eip_msr;

/* For the AMD K6. */
extern uint64_t star;

#define FPU_CW_Reserved_Bits (0xe0c0)

extern wchar_t nvr_path[1024];
extern int path_len;

wchar_t *nvr_concat(wchar_t *to_concat);

extern int mem_a20_state;


#ifdef ENABLE_LOG_TOGGLES
extern int buslogic_do_log;
extern int cdrom_do_log;
extern int d86f_do_log;
extern int fdc_do_log;
extern int ide_do_log;
extern int serial_do_log;
extern int nic_do_log;
#endif

extern int suppress_overscan;

typedef struct PCI_RESET
{
	void (*pci_master_reset)(void);
	void (*pci_set_reset)(void);
	void (*super_io_reset)(void);
} PCI_RESET;

extern PCI_RESET pci_reset_handler;

extern uint8_t	trc_read(uint16_t port, void *priv);
extern void	trc_write(uint16_t port, uint8_t val, void *priv);
extern void	trc_init(void);

extern int enable_xtide;
extern int enable_external_fpu;

extern int serial_enabled[2];
extern int lpt_enabled, bugger_enabled;

extern int invert_display;

uint32_t svga_color_transform(uint32_t color);

extern int scale;


/* Function prototypes. */
extern int	checkio(int port);
extern void	closepc(void);
extern void	codegen_block_end(void);
extern void	codegen_reset(void);
extern void	cpu_set_edx(void);
extern int	divl(uint32_t val);
extern void	dumpregs(int __force);
extern void	exec386(int cycs);
extern void	exec386_dynarec(int cycs);
extern void	execx86(int cycs);
extern void	flushmmucache(void);
extern void	flushmmucache_cr3(void);
extern int	idivl(int32_t val);
extern void	initmodules(void);
extern void	initpc(int argc, wchar_t *argv[]);
extern void	loadcscall(uint16_t seg);
extern void	loadcsjmp(uint16_t seg, uint32_t oxpc);
extern void	mmu_invalidate(uint32_t addr);
extern void	pclog(const char *format, ...);
extern void	pmodeint(int num, int soft);
extern void	pmoderetf(int is32, uint16_t off);
extern void	pmodeiret(int is32);
extern void	port_92_clear_reset(void);
extern uint8_t	readdacfifo(void);
extern void	refreshread(void);
extern int	rep386(int fv);
extern void	resetmcr(void);
extern void	resetpchard(void);
extern void	resetreadlookup(void);
extern void	resetx86(void);
extern void	runpc(void);
extern void	saveconfig(void);
extern void	softresetx86(void);
extern void	speedchanged(void);
extern void	trc_reset(uint8_t val);
extern void	x86_int_sw(int num);
extern void	x86gpf(char *s, uint16_t error);
extern void	x86np(char *s, uint16_t error);
extern void	x86ss(char *s, uint16_t error);
extern void	x86ts(char *s, uint16_t error);
extern void	x87_dumpregs(void);
extern void	x87_reset(void);

/* Platform functions. */
extern void	pclog(const char *format, ...);
extern void	pclog_w(const wchar_t *format, ...);
extern void	fatal(const char *format, ...);

extern void	update_status_bar_icon(int tag, int active);
extern void	update_status_bar_icon_state(int tag, int state);
extern void	status_settextw(wchar_t *wstr);
extern void	status_settext(char *str);

#define SB_FLOPPY	0x00
#define SB_CDROM	0x10
#define SB_RDISK	0x20
#define SB_HDD		0x40
#define SB_TEXT		0x50
