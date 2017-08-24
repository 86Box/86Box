/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
/*MESS ROM notes :
        
        - pc2386 BIOS is corrupt (JMP at F000:FFF0 points to RAM)
        - pc2386 video BIOS is underdumped (16k instead of 24k)
        - c386sx16 BIOS fails checksum
*/
#include <stdlib.h>
#include <string.h>
#include "ibm.h"
#include "CPU/cpu.h"
#include "CPU/x86_ops.h"
#include "CPU/x86.h"
#include "config.h"
#include "io.h"
#include "mem.h"
#include "rom.h"
#include "CPU/codegen.h"
#include "VIDEO/video.h"


page_t *pages;
page_t **page_lookup;

static uint8_t         (*_mem_read_b[0x40000])(uint32_t addr, void *priv);
static uint16_t        (*_mem_read_w[0x40000])(uint32_t addr, void *priv);
static uint32_t        (*_mem_read_l[0x40000])(uint32_t addr, void *priv);
static void           (*_mem_write_b[0x40000])(uint32_t addr, uint8_t  val, void *priv);
static void           (*_mem_write_w[0x40000])(uint32_t addr, uint16_t val, void *priv);
static void           (*_mem_write_l[0x40000])(uint32_t addr, uint32_t val, void *priv);
static uint8_t            *_mem_exec[0x40000];
static void             *_mem_priv_r[0x40000];
static void             *_mem_priv_w[0x40000];
static mem_mapping_t *_mem_mapping_r[0x40000];
static mem_mapping_t *_mem_mapping_w[0x40000];
static int                _mem_state[0x40000];

static mem_mapping_t base_mapping;
mem_mapping_t ram_low_mapping;
mem_mapping_t ram_high_mapping;
mem_mapping_t ram_mid_mapping;
static mem_mapping_t ram_remapped_mapping;
mem_mapping_t bios_mapping[8];
mem_mapping_t bios_high_mapping[8];
mem_mapping_t romext_mapping;

uint8_t *ram;
uint32_t rammask;

uint32_t pccache;
uint8_t *pccache2;

int readlookup[256],readlookupp[256];
uintptr_t *readlookup2;
int readlnext;
int writelookup[256],writelookupp[256];
uintptr_t *writelookup2;
int writelnext;

int shadowbios,shadowbios_write;

int mem_a20_state;

unsigned char isram[0x10000];

static uint8_t ff_array[0x1000];

int enable_xtide = 0;

int mem_size;
uint32_t biosmask;
int readlnum=0,writelnum=0;
int cachesize=256;

uint8_t *ram,*rom;
uint8_t romext[32768];

uint32_t ram_mapped_addr[64];

static void mem_load_atide115_bios()
{
        FILE *f;
        f=romfopen(L"roms/hdd/xtide/ide_at_1_1_5.bin",L"rb");

        if (f)
        {
                fread(romext,16384,1,f);
		mem_mapping_enable(&romext_mapping);
                fclose(f);
        }
}
        
int loadbios()
{
        FILE *f=NULL,*ff=NULL;
        int c;
        
        loadfont(L"roms/video/mda/mda.rom", 0);
	loadfont(L"roms/video/wyse700/wy700.rom", 3);

        biosmask = 0xffff;

        if (!rom)
                rom = malloc(0x20000);
        memset(romext,0xff,0x8000);
        memset(rom, 0xff, 0x20000);
        
        pclog("Starting with romset %i\n", romset);

	mem_mapping_disable(&romext_mapping);
       
        switch (romset)
        {
                case ROM_PC1512:
                f=romfopen(L"roms/machines/pc1512/40043.v1",L"rb");
                ff=romfopen(L"roms/machines/pc1512/40044.v1",L"rb");
                if (!f || !ff) break;
                for (c=0xC000;c<0x10000;c+=2)
                {
                        rom[c]=getc(f);
                        rom[c+1]=getc(ff);
                }
                fclose(ff);
                fclose(f);
                loadfont(L"roms/machines/pc1512/40078.ic127", 2);
                return 1;
                case ROM_PC1640:
                f=romfopen(L"roms/machines/pc1640/40044.v3",L"rb");
                ff=romfopen(L"roms/machines/pc1640/40043.v3",L"rb");
                if (!f || !ff) break;
                for (c=0xC000;c<0x10000;c+=2)
                {
                        rom[c]=getc(f);
                        rom[c+1]=getc(ff);
                }
                fclose(ff);
                fclose(f);
                f=romfopen(L"roms/machines/pc1640/40100",L"rb");
                if (!f) break;
                fclose(f);
                return 1;
                case ROM_PC200:
                f=romfopen(L"roms/machines/pc200/pc20v2.1",L"rb");
                ff=romfopen(L"roms/machines/pc200/pc20v2.0",L"rb");
                if (!f || !ff) break;
                for (c=0xC000;c<0x10000;c+=2)
                {
                        rom[c]=getc(f);
                        rom[c+1]=getc(ff);
                }
                fclose(ff);
                fclose(f);
                loadfont(L"roms/machines/pc200/40109.bin", 1);
                return 1;
                case ROM_TANDY:
                f=romfopen(L"roms/machines/tandy/tandy1t1.020",L"rb");
                if (!f) break;
                fread(rom,65536,1,f);
                fclose(f);
                return 1;
                case ROM_TANDY1000HX:
                f = romfopen(L"roms/machines/tandy1000hx/v020000.u12", L"rb");
                if (!f) break;
                fread(rom, 0x20000, 1, f);
                fclose(f);
                biosmask = 0x1ffff;
                return 1;
                case ROM_TANDY1000SL2:
                f  = romfopen(L"roms/machines/tandy1000sl2/8079047.hu1" ,L"rb");
                ff = romfopen(L"roms/machines/tandy1000sl2/8079048.hu2",L"rb");
                if (!f || !ff) break;
                fseek(f,  0x30000/2, SEEK_SET);
                fseek(ff, 0x30000/2, SEEK_SET);
                for (c = 0x0000; c < 0x10000; c += 2)
                {
                        rom[c] = getc(f);
                        rom[c + 1] = getc(ff);
                }
                fclose(ff);
                fclose(f);
                return 1;
                case ROM_IBMXT:
                f=romfopen(L"roms/machines/ibmxt/xt.rom",L"rb");
                if (!f)
                {
                        f = romfopen(L"roms/machines/ibmxt/5000027.u19", L"rb");
                        ff = romfopen(L"roms/machines/ibmxt/1501512.u18", L"rb");
                        if (!f || !ff) break;
                        fread(rom, 0x8000, 1, f);
                        fread(rom + 0x8000, 0x8000, 1, ff);
                        fclose(ff);
                        fclose(f);
                        return 1;
                }
                else
                {
                        fread(rom,65536,1,f);
                        fclose(f);
                        return 1;
                }
                break;
                
                case ROM_IBMPCJR:
                f = romfopen(L"roms/machines/ibmpcjr/bios.rom", L"rb");
                if (!f) break;
                fread(rom, 0x10000, 1, f);
                fclose(f);
                return 1;
                
                case ROM_PORTABLE:
                f=romfopen(L"roms/machines/portable/Compaq Portable Plus 100666-001 Rev C u47.bin",L"rb");
                if (!f) break;
                fread(rom+0xE000,8192,1,f);
                fclose(f);
                return 1;

#if 0
                case ROM_PORTABLEII:
                f = romfopen(L"roms/machines/portableii/106438-001.BIN", L"rb");
                ff  =romfopen(L"roms/machines/portableii/106437-001.BIN", L"rb");
                if (!f || !ff) break;
                for (c=0x0000;c<0x8000;c+=2)
                {
                        rom[c]=getc(f);
                        rom[c+1]=getc(ff);
                }
                fclose(ff);
                fclose(f);
				biosmask = 0x7fff;
                return 1;
				
                case ROM_PORTABLEIII:
                case ROM_PORTABLEIII386:
                f = romfopen(L"roms/machines/portableiii/109738-002.BIN", L"rb");
                ff  =romfopen(L"roms/machines/portableiii/109737-002.BIN", L"rb");
                if (!f || !ff) break;
                for (c=0x0000;c<0x10000;c+=2)
                {
                        rom[c]=getc(f);
                        rom[c+1]=getc(ff);
                }
                fclose(ff);
                fclose(f);
                return 1;				
#endif
				
                case ROM_GENXT:
                f=romfopen(L"roms/machines/genxt/pcxt.rom",L"rb");
                if (!f) break;
                fread(rom+0xE000,8192,1,f);
                fclose(f);
                return 1;
                case ROM_DTKXT:
                f=romfopen(L"roms/machines/dtk/DTK_ERSO_2.42_2764.bin",L"rb");
                if (!f) break;
                fread(rom+0xE000,8192,1,f);
                fclose(f);
                return 1;
                case ROM_OLIM24:
                f  = romfopen(L"roms/machines/olivetti_m24/olivetti_m24_version_1.43_low.bin" ,L"rb");
                ff = romfopen(L"roms/machines/olivetti_m24/olivetti_m24_version_1.43_high.bin",L"rb");
                if (!f || !ff) break;
                for (c = 0x0000; c < 0x4000; c += 2)
                {
                        rom[c + 0xc000] = getc(f);
                        rom[c + 0xc001] = getc(ff);
                }
                fclose(ff);
                fclose(f);
                return 1;
                        
                case ROM_PC2086:
                f  = romfopen(L"roms/machines/pc2086/40179.ic129" ,L"rb");
                ff = romfopen(L"roms/machines/pc2086/40180.ic132",L"rb");
                if (!f || !ff) break;
                pclog("Loading BIOS\n");
                for (c = 0x0000; c < 0x4000; c += 2)
                {
                        rom[c + 0x0000] = getc(f);
                        rom[c + 0x0001] = getc(ff);
                }
                pclog("%02X %02X %02X\n", rom[0xfff0], rom[0xfff1], rom[0xfff2]);
                fclose(ff);
                fclose(f);
                f = romfopen(L"roms/machines/pc2086/40186.ic171", L"rb");
                if (!f) break;
                fclose(f);
                biosmask = 0x3fff;
                return 1;

                case ROM_PC3086:
                f  = romfopen(L"roms/machines/pc3086/fc00.bin", L"rb");
                if (!f) break;
                fread(rom, 0x4000, 1, f);
                fclose(f);
                f = romfopen(L"roms/machines/pc3086/c000.bin", L"rb");
                if (!f) break;
                fclose(f);
                biosmask = 0x3fff;                
                return 1;

                case ROM_IBMAT:
                f = romfopen(L"roms/machines/ibmat/62x0820.u27", L"rb");
                ff  =romfopen(L"roms/machines/ibmat/62x0821.u47", L"rb");
                if (!f || !ff) break;
                for (c=0x0000;c<0x10000;c+=2)
                {
                        rom[c]=getc(f);
                        rom[c+1]=getc(ff);
                }
                fclose(ff);
                fclose(f);
                return 1;
                
                case ROM_CMDPC30:
                f  = romfopen(L"roms/machines/cmdpc30/commodore pc 30 iii even.bin", L"rb");
                ff = romfopen(L"roms/machines/cmdpc30/commodore pc 30 iii odd.bin",  L"rb");
                if (!f || !ff) break;
                for (c = 0x0000; c < 0x8000; c += 2)
                {
                        rom[c]     = getc(f);
                        rom[c + 1] = getc(ff);
                }
                fclose(ff);
                fclose(f);
                biosmask = 0x7fff;
                return 1;
                
                case ROM_AMI386SX:
                f=romfopen(L"roms/machines/ami386/ami386.bin",L"rb");
                if (!f) break;
                fread(rom,65536,1,f);
                fclose(f);
                return 1;

                case ROM_AMI386DX_OPTI495: /*This uses the OPTi 82C495 chipset*/
                f=romfopen(L"roms/machines/ami386dx/OPT495SX.AMI",L"rb");
                if (!f) break;
                fread(rom,65536,1,f);
                fclose(f);
                return 1;
                case ROM_MR386DX_OPTI495: /*This uses the OPTi 82C495 chipset*/
                f=romfopen(L"roms/machines/mr386dx/OPT495SX.MR",L"rb");
                if (!f) break;
                fread(rom,65536,1,f);
                fclose(f);
                return 1;

                case ROM_AWARD386SX_OPTI495: /*This uses the OPTi 82C495 chipset*/
                case ROM_AWARD386DX_OPTI495: /*This uses the OPTi 82C495 chipset*/
                case ROM_AWARD486_OPTI495: /*This uses the OPTi 82C495 chipset*/
                f=romfopen(L"roms/machines/award495/OPT495S.AWA",L"rb");
                if (!f) break;
                fread(rom,65536,1,f);
                fclose(f);
                return 1;

                case ROM_AMI286:
                f=romfopen(L"roms/machines/ami286/amic206.bin",L"rb");
                if (!f) break;
                fread(rom,65536,1,f);
                fclose(f);
                return 1;

                case ROM_AWARD286:
                f=romfopen(L"roms/machines/award286/award.bin",L"rb");
                if (!f) break;
                fread(rom,65536,1,f);
                fclose(f);
                return 1;

                case ROM_EUROPC:
                f=romfopen(L"roms/machines/europc/50145",L"rb");
                if (!f) break;
                fread(rom+0x8000,32768,1,f);
                fclose(f);
                return 1;

                case ROM_IBMPC:
                f=romfopen(L"roms/machines/ibmpc/pc102782.bin",L"rb");
                if (!f) break;
                fread(rom+0xE000,8192,1,f);
                fclose(f);
                f=romfopen(L"roms/ibmpc/ibm-basic-1.10.rom",L"rb");
                if (!f)
                {
                        f=romfopen(L"roms/machines/ibmpc/basicc11.f6",L"rb");
                        if (!f) return 1; /*I don't really care if BASIC is there or not*/
                        fread(rom+0x6000,8192,1,f);
                        fclose(f);
                        f=romfopen(L"roms/machines/ibmpc/basicc11.f8",L"rb");
                        if (!f) break; /*But if some of it is there, then all of it must be*/
                        fread(rom+0x8000,8192,1,f);
                        fclose(f);
                        f=romfopen(L"roms/machines/ibmpc/basicc11.fa",L"rb");
                        if (!f) break;
                        fread(rom+0xA000,8192,1,f);
                        fclose(f);
                        f=romfopen(L"roms/machines/ibmpc/basicc11.fc",L"rb");
                        if (!f) break;
                        fread(rom+0xC000,8192,1,f);
                        fclose(f);
                }
                else
                {
                        fread(rom+0x6000,32768,1,f);
                        fclose(f);
                }

                return 1;

                case ROM_MEGAPC:
                case ROM_MEGAPCDX:
                f  = romfopen(L"roms/machines/megapc/41651-bios lo.u18", L"rb");
                ff = romfopen(L"roms/machines/megapc/211253-bios hi.u19", L"rb");
                if (!f || !ff) break;
                fseek(f,  0x8000, SEEK_SET);
                fseek(ff, 0x8000, SEEK_SET);                
                for (c = 0x0000; c < 0x10000; c+=2)
                {
                        rom[c]=getc(f);
                        rom[c+1]=getc(ff);
                }
                fclose(ff);
                fclose(f);
                return 1;
                        
                case ROM_AMI486:
                f=romfopen(L"roms/machines/ami486/ami486.BIN",L"rb");
                if (!f) break;
                fread(rom,65536,1,f);
                fclose(f);
                return 1;
                
                case ROM_WIN486:
                f=romfopen(L"roms/machines/win486/ALI1429G.AMW",L"rb");
                if (!f) break;
                fread(rom,65536,1,f);
                fclose(f);
                return 1;

                case ROM_430VX:
                f = romfopen(L"roms/machines/430vx/55XWUQ0E.BIN", L"rb");
                if (!f) break;
                fread(rom,           0x20000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_REVENGE:
                f = romfopen(L"roms/machines/revenge/1009AF2_.BIO", L"rb");
                if (!f) break;
                fseek(f, 0x80, SEEK_SET);
                fread(rom + 0x10000, 0x10000, 1, f);                
                fclose(f);
                f = romfopen(L"roms/machines/revenge/1009AF2_.BI1", L"rb");
                if (!f) break;
                fseek(f, 0x80, SEEK_SET);
                fread(rom, 0xc000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                return 1;
                case ROM_ENDEAVOR:
                f = romfopen(L"roms/machines/endeavor/1006CB0_.BIO", L"rb");
                if (!f) break;
                fseek(f, 0x80, SEEK_SET);
                fread(rom + 0x10000, 0x10000, 1, f);                
                fclose(f);
                f = romfopen(L"roms/machines/endeavor/1006CB0_.BI1", L"rb");
                if (!f) break;
                fseek(f, 0x80, SEEK_SET);
                fread(rom, 0xd000, 1, f);
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_IBMPS1_2011:
                f = romfopen(L"roms/machines/ibmps1es/f80000.bin", L"rb");
                if (!f) break;
                fseek(f, 0x60000, SEEK_SET);
                fread(rom, 0x20000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                return 1;
 
                case ROM_IBMPS1_2121:
		case ROM_IBMPS1_2121_ISA:
                f = romfopen(L"roms/machines/ibmps1_2121/fc0000.bin", L"rb");
                if (!f) break;
                fseek(f, 0x20000, SEEK_SET);
                fread(rom, 0x20000, 1, f);                
                fclose(f);
		if (enable_xtide)
		{
			mem_load_atide115_bios();
		}
                biosmask = 0x1ffff;
                return 1;

                case ROM_IBMPS1_2133:
                f = romfopen(L"roms/machines/ibmps1_2133/PS1_2133_52G2974_ROM.bin", L"rb");
                if (!f) break;
                fread(rom, 0x20000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_DESKPRO_386:
                f=romfopen(L"roms/machines/deskpro386/109592-005.U11.bin",L"rb");
                ff=romfopen(L"roms/machines/deskpro386/109591-005.U13.bin",L"rb");
                if (!f || !ff) break;
                for (c = 0x0000; c < 0x8000; c += 2)
                {
                        rom[c] = getc(f);
                        rom[c+1] = getc(ff);
                }
                fclose(ff);
                fclose(f);
                biosmask = 0x7fff;
                return 1;

                case ROM_AMIXT:
                f = romfopen(L"roms/machines/amixt/AMI_8088_BIOS_31JAN89.BIN", L"rb");
                if (!f) break;
                fread(rom + 0xE000, 8192, 1, f);
                fclose(f);
                return 1;
                
                case ROM_LTXT:
                f = romfopen(L"roms/machines/ltxt/27C64.bin", L"rb");
                if (!f) break;
                fread(rom + 0xE000, 8192, 1, f);
                fclose(f);
                f=romfopen(L"roms/ltxt/ibm-basic-1.10.rom",L"rb");
                if (!f)
                {
                        f=romfopen(L"roms/machines/ltxt/basicc11.f6",L"rb");
                        if (!f) return 1; /*I don't really care if BASIC is there or not*/
                        fread(rom+0x6000,8192,1,f);
                        fclose(f);
                        f=romfopen(L"roms/machines/ltxt/basicc11.f8",L"rb");
                        if (!f) break; /*But if some of it is there, then all of it must be*/
                        fread(rom+0x8000,8192,1,f);
                        fclose(f);
                        f=romfopen(L"roms/machines/ltxt/basicc11.fa",L"rb");
                        if (!f) break;
                        fread(rom+0xA000,8192,1,f);
                        fclose(f);
                        f=romfopen(L"roms/machines/ltxt/basicc11.fc",L"rb");
                        if (!f) break;
                        fread(rom+0xC000,8192,1,f);
                        fclose(f);
                }
                else
                {
                        fread(rom+0x6000,32768,1,f);
                        fclose(f);
                }

                return 1;

                case ROM_LXT3:
                f = romfopen(L"roms/machines/lxt3/27C64D.bin", L"rb");
                if (!f) break;
                fread(rom + 0xE000, 8192, 1, f);
                fclose(f);
                f=romfopen(L"roms/machines/lxt3/ibm-basic-1.10.rom",L"rb");
                if (!f)
                {
                        f=romfopen(L"roms/machines/lxt3/basicc11.f6",L"rb");
                        if (!f) return 1; /*I don't really care if BASIC is there or not*/
                        fread(rom+0x6000,8192,1,f);
                        fclose(f);
                        f=romfopen(L"roms/machines/lxt3/basicc11.f8",L"rb");
                        if (!f) break; /*But if some of it is there, then all of it must be*/
                        fread(rom+0x8000,8192,1,f);
                        fclose(f);
                        f=romfopen(L"roms/machines/lxt3/basicc11.fa",L"rb");
                        if (!f) break;
                        fread(rom+0xA000,8192,1,f);
                        fclose(f);
                        f=romfopen(L"roms/machines/lxt3/basicc11.fc",L"rb");
                        if (!f) break;
                        fread(rom+0xC000,8192,1,f);
                        fclose(f);
                }
                else
                {
                        fread(rom+0x6000,32768,1,f);
                        fclose(f);
                }

                return 1;

                case ROM_SPC4200P: /*Samsung SPC-4200P*/
                f = romfopen(L"roms/machines/spc4200p/U8.01", L"rb");
                if (!f) break;
                fread(rom, 65536, 1, f);
                fclose(f);
                return 1;

                case ROM_SUPER286TR: /*Hyundai Super-286TR*/
                f = romfopen(L"roms/machines/super286tr/hyundai_award286.bin", L"rb");
                if (!f) break;
                fread(rom, 65536, 1, f);
                fclose(f);
                return 1;

                case ROM_DTK386: /*Uses NEAT chipset*/
                f = romfopen(L"roms/machines/dtk386/3cto001.bin", L"rb");
                if (!f) break;
                fread(rom, 65536, 1, f);
                fclose(f);
                return 1;

                case ROM_PXXT:
                f = romfopen(L"roms/machines/pxxt/000p001.bin", L"rb");
                if (!f) break;
                fread(rom + 0xE000, 8192, 1, f);
                fclose(f);
                return 1;

                case ROM_JUKOPC:
                f = romfopen(L"roms/machines/jukopc/000o001.bin", L"rb");
                if (!f) break;
                fread(rom + 0xE000, 8192, 1, f);
                fclose(f);
                return 1;
				
		case ROM_IBMPS2_M30_286:
                f = romfopen(L"roms/machines/ibmps2_m30_286/33f5381a.bin", L"rb");
                if (!f) break;
                fread(rom, 0x20000, 1, f);                
                fclose(f);
		if (enable_xtide)
		{
			mem_load_atide115_bios();
		}
                biosmask = 0x1ffff;
                return 1;

                case ROM_DTK486:
                f = romfopen(L"roms/machines/dtk486/4siw005.bin", L"rb");
                if (!f) break;
                fread(rom,           0x10000, 1, f);                
                fclose(f);
                return 1;
                
                case ROM_R418:
                f = romfopen(L"roms/machines/r418/r418i.bin", L"rb");
                if (!f) break;
                fread(rom,           0x20000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                pclog("Load R418 %x %x\n", rom[0x1fff0], rom[0xfff0]);
                return 1;
                
#if 0
                case ROM_586MC1:
                f = romfopen(L"roms/machines/586mc1/IS.34", L"rb");
                if (!f) break;
                fread(rom,           0x20000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                return 1;
#endif

                case ROM_PLATO:
                f = romfopen(L"roms/machines/plato/1016AX1_.BIO", L"rb");
                if (!f) break;
                fseek(f, 0x80, SEEK_SET);
                fread(rom + 0x10000, 0x10000, 1, f);                
                fclose(f);
                f = romfopen(L"roms/machines/plato/1016AX1_.BI1", L"rb");
                if (!f) break;
                fseek(f, 0x80, SEEK_SET);
                fread(rom, 0xd000, 1, f);
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_MB500N:
                f = romfopen(L"roms/machines/mb500n/031396S.BIN", L"rb");	/* Works */
                if (!f) break;
                fread(rom,           0x20000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_AP53:
                f = romfopen(L"roms/machines/ap53/AP53R2C0.ROM", L"rb");	/* Works */
                if (!f) break;
                fread(rom,           0x20000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_P55T2S:
                f = romfopen(L"roms/machines/p55t2s/S6Y08T.ROM", L"rb");	/* Works */
                if (!f) break;
                fread(rom,           0x20000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_PRESIDENT:
                f = romfopen(L"roms/machines/president/BIOS.BIN", L"rb");
                if (!f) break;
                fread(rom,           0x20000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_P54TP4XE:
                f = romfopen(L"roms/machines/p54tp4xe/T15I0302.AWD", L"rb");
                if (!f) break;
                fread(rom,           0x20000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_ACERM3A:
                f = romfopen(L"roms/machines/acerm3a/r01-b3.bin", L"rb");
                if (!f) break;
                fread(rom,           0x20000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_ACERV35N:
                f = romfopen(L"roms/machines/acerv35n/V35ND1S1.BIN", L"rb");
                if (!f) break;
                fread(rom,           0x20000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_P55VA:
                f = romfopen(L"roms/machines/p55va/VA021297.BIN", L"rb");
                if (!f) break;
                fread(rom,           0x20000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_P55T2P4:
                f = romfopen(L"roms/machines/p55t2p4/0207_J2.BIN", L"rb");
                if (!f) break;
                fread(rom,           0x20000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_P55TVP4:
                f = romfopen(L"roms/machines/p55tvp4/TV5I0204.AWD", L"rb");
                if (!f) break;
                fread(rom,           0x20000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_440FX:
                f = romfopen(L"roms/machines/440fx/NTMAW501.BIN", L"rb");	/* Working Tyan BIOS. */
                if (!f) break;
                fread(rom,           0x20000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_S1668:
                f = romfopen(L"roms/machines/tpatx/S1668P.ROM", L"rb");	/* Working Tyan BIOS. */
                if (!f) break;
                fread(rom,           0x20000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_THOR:
                f = romfopen(L"roms/machines/thor/1006CN0_.BIO", L"rb");
                if (!f) break;
                fseek(f, 0x80, SEEK_SET);
                fread(rom + 0x10000, 0x10000, 1, f);                
                fclose(f);
                f = romfopen(L"roms/machines/thor/1006CN0_.BI1", L"rb");
                if (!f) break;
                fseek(f, 0x80, SEEK_SET);
                fread(rom, 0x10000, 1, f);
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_MRTHOR:
                f = romfopen(L"roms/machines/mrthor/MR_ATX.BIO", L"rb");
                if (!f) break;
                fread(rom,           0x20000, 1, f);                
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_ZAPPA:
                f = romfopen(L"roms/machines/zappa/1006BS0_.BIO", L"rb");
                if (!f) break;
                fseek(f, 0x80, SEEK_SET);
                fread(rom + 0x10000, 0x10000, 1, f);                
                fclose(f);
                f = romfopen(L"roms/machines/zappa/1006BS0_.BI1", L"rb");
                if (!f) break;
                fseek(f, 0x80, SEEK_SET);
                fread(rom, 0x10000, 1, f);
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_IBMPS2_M50:
                f=romfopen(L"roms/machines/ibmps2_m50/90x7423.zm14",L"rb");
                ff=romfopen(L"roms/machines/ibmps2_m50/90x7426.zm16",L"rb");
                if (!f || !ff) break;
                for (c = 0x0000; c < 0x10000; c += 2)
                {
                        rom[c] = getc(f);
                        rom[c+1] = getc(ff);
                }
                fclose(ff);
                fclose(f);
                f=romfopen(L"roms/machines/ibmps2_m50/90x7420.zm13",L"rb");
                ff=romfopen(L"roms/machines/ibmps2_m50/90x7429.zm18",L"rb");
                if (!f || !ff) break;
                for (c = 0x10000; c < 0x20000; c += 2)
                {
                        rom[c] = getc(f);
                        rom[c+1] = getc(ff);
                }
                fclose(ff);
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_IBMPS2_M55SX:
                f=romfopen(L"roms/machines/ibmps2_m55sx/33f8146.zm41",L"rb");
                ff=romfopen(L"roms/machines/ibmps2_m55sx/33f8145.zm40",L"rb");
                if (!f || !ff) break;
                for (c = 0x0000; c < 0x20000; c += 2)
                {
                        rom[c] = getc(f);
                        rom[c+1] = getc(ff);
                }
                fclose(ff);
                fclose(f);
                biosmask = 0x1ffff;
                return 1;

                case ROM_IBMPS2_M80:
                f=romfopen(L"roms/machines/ibmps2_m80/15f6637.bin",L"rb");
                ff=romfopen(L"roms/machines/ibmps2_m80/15f6639.bin",L"rb");
                if (!f || !ff) break;
                for (c = 0x0000; c < 0x20000; c += 2)
                {
                        rom[c] = getc(f);
                        rom[c+1] = getc(ff);
                }
                fclose(ff);
                fclose(f);
                biosmask = 0x1ffff;
                return 1;
        }
        printf("Failed to load ROM!\n");
        if (f) fclose(f);
        if (ff) fclose(ff);
        return 0;
}



void resetreadlookup()
{
        int c;
        memset(readlookup2,0xFF,1024*1024*sizeof(uintptr_t));
        for (c=0;c<256;c++) readlookup[c]=0xFFFFFFFF;
        readlnext=0;
        memset(writelookup2,0xFF,1024*1024*sizeof(uintptr_t));
        memset(page_lookup, 0, (1 << 20) * sizeof(page_t *));
        for (c=0;c<256;c++) writelookup[c]=0xFFFFFFFF;
        writelnext=0;
        pccache=0xFFFFFFFF;
}

int mmuflush=0;
int mmu_perm=4;

void flushmmucache()
{
        int c;
        for (c=0;c<256;c++)
        {
                if (readlookup[c]!=0xFFFFFFFF)
                {
                        readlookup2[readlookup[c]] = -1;
                        readlookup[c]=0xFFFFFFFF;
                }
                if (writelookup[c] != 0xFFFFFFFF)
                {
                        page_lookup[writelookup[c]] = NULL;
                        writelookup2[writelookup[c]] = -1;
                        writelookup[c] = 0xFFFFFFFF;
                }
        }
        mmuflush++;
        pccache=(uint32_t)0xFFFFFFFF;
        pccache2=(uint8_t *)0xFFFFFFFF;
        
        codegen_flush();
}

void flushmmucache_nopc()
{
        int c;
        for (c=0;c<256;c++)
        {
                if (readlookup[c]!=0xFFFFFFFF)
                {
                        readlookup2[readlookup[c]] = -1;
                        readlookup[c]=0xFFFFFFFF;
                }
                if (writelookup[c] != 0xFFFFFFFF)
                {
                        page_lookup[writelookup[c]] = NULL;
                        writelookup2[writelookup[c]] = -1;
                        writelookup[c] = 0xFFFFFFFF;
                }
        }
}

void flushmmucache_cr3()
{
        int c;
        for (c=0;c<256;c++)
        {
                if (readlookup[c]!=0xFFFFFFFF)
                {
                        readlookup2[readlookup[c]] = -1;
                        readlookup[c]=0xFFFFFFFF;
                }
                if (writelookup[c] != 0xFFFFFFFF)
                {
                        page_lookup[writelookup[c]] = NULL;
                        writelookup2[writelookup[c]] = -1;
                        writelookup[c] = 0xFFFFFFFF;                        
                }
        }
}

void mem_flush_write_page(uint32_t addr, uint32_t virt)
{
        int c;
        page_t *page_target = &pages[addr >> 12];

        for (c = 0; c < 256; c++)        
        {
                if (writelookup[c] != 0xffffffff)
                {
                        uintptr_t target = (uintptr_t)&ram[(uintptr_t)(addr & ~0xfff) - (virt & ~0xfff)];

                        if (writelookup2[writelookup[c]] == target || page_lookup[writelookup[c]] == page_target)
                        {
                                writelookup2[writelookup[c]] = -1;
                                page_lookup[writelookup[c]] = NULL;
                                writelookup[c] = 0xffffffff;
                        }
                }
        }
}

extern int output;

#define mmutranslate_read(addr) mmutranslatereal(addr,0)
#define mmutranslate_write(addr) mmutranslatereal(addr,1)

int pctrans=0;

extern uint32_t testr[9];

int mem_cpl3_check()
{
	if ((CPL == 3) && !cpl_override)
	{
		return 1;
	}
	return 0;
}

void mmu_page_fault(uint32_t addr, uint32_t error_code)
{
	cr2 = addr;
	cpu_state.abrt = ABRT_PF;
	abrt_error = error_code;
}

int mmu_page_fault_check(uint32_t addr, int rw, uint32_t flags, int pde, int is_abrt)
{
	uint8_t error_code = 0;

	uint8_t is_page_fault = 0;

	if (mem_cpl3_check())  error_code = 4;	/* If CPL = 3 and it's not a PDE check, set US bit. */
	if (rw)  error_code |= 2;		/* If writing and it's not a PDE check, set RW bit. */

	if (!(flags & 1))
	{
		is_page_fault = 1;
	}

	if (!pde)
	{
		if (!(flags & 4) && mem_cpl3_check())
		{
			is_page_fault = 1;
		}
		if (rw && !(flags & 2) && (mem_cpl3_check() || (cr0 & WP_FLAG)))
		{
			is_page_fault = 1;
		}
	}

	if (is_page_fault)
	{
		if (is_abrt)
		{
			mmu_page_fault(addr, error_code | (flags & 1));
		}
		return -1;
	}

	return 0;
}

#define PAGE_DIRTY_AND_ACCESSED 0x60
#define PAGE_DIRTY 0x40
#define PAGE_ACCESSED 0x20

/* rw means 0 = read, 1 = write */
uint32_t mmutranslate(uint32_t addr, int rw, int is_abrt)
{
	/* Mask out the lower 12 bits. */
	uint32_t dir_base = 0;

	uint32_t table_addr = 0;
	uint32_t page_addr = 0;

	uint32_t table_flags = 0;
	uint32_t page_flags = 0;

	if (cpu_state.abrt)
	{
		return -1;
	}

	dir_base = cr3 & ~0xfff;
	table_addr = dir_base + ((addr >> 20) & 0xffc);

	/* First check the flags of the page directory entry. */
	table_flags = ((uint32_t *)ram)[table_addr >> 2];

	if ((table_flags & 0x80) && (cr4 & CR4_PSE))
	{
		/* Do a PDE-style page fault check. */
		if (mmu_page_fault_check(addr, rw, table_flags & 7, 0, is_abrt) == -1)
		{
			return -1;
		}

		/* Since PSE is not enabled, there is no page table, so we do a slightly modified skip to the end. */
		if (is_abrt)
		{
			mmu_perm = table_flags & 4;
			((uint32_t *)ram)[table_addr >> 2] |= (rw ? PAGE_DIRTY_AND_ACCESSED : PAGE_ACCESSED);
		}

		return (table_flags & ~0x3FFFFF) + (addr & 0x3FFFFF);
	}
	else
	{
		/* Do a non-PDE-style page fault check. */
		if (mmu_page_fault_check(addr, rw, table_flags & 7, 1, is_abrt) == -1)
		{
			return -1;
		}
	}

	page_addr = table_flags & ~0xfff;
	page_addr += ((addr >> 10) & 0xffc);

	/* Then check the flags of the page table entry. */
	page_flags = ((uint32_t *)ram)[page_addr >> 2];

	if (mmu_page_fault_check(addr, rw, page_flags & 7, 0, is_abrt) == -1)
	{
		return -1;
	}

	if (is_abrt)
	{
		mmu_perm = page_flags & 4;
		((uint32_t *)ram)[table_addr >> 2] |= PAGE_ACCESSED;
		((uint32_t *)ram)[page_addr >> 2] |= (rw ? PAGE_DIRTY_AND_ACCESSED : PAGE_ACCESSED);
	}

	return (page_flags & ~0xFFF) + (addr & 0xFFF);
}

uint32_t mmutranslatereal(uint32_t addr, int rw)
{
	return mmutranslate(addr, rw, 1);
}

uint32_t mmutranslate_noabrt(uint32_t addr, int rw)
{
	return mmutranslate(addr, rw, 0);
}

void mmu_invalidate(uint32_t addr)
{
        flushmmucache_cr3();
}

void addreadlookup(uint32_t virt, uint32_t phys)
{
        if (virt == 0xffffffff)
                return;
                
        if (readlookup2[virt>>12] != -1) 
        {
                return;
        }
        
        
        if (readlookup[readlnext]!=0xFFFFFFFF)
        {
                readlookup2[readlookup[readlnext]] = -1;
        }
        readlookup2[virt>>12] = (uintptr_t)&ram[(uintptr_t)(phys & ~0xFFF) - (uintptr_t)(virt & ~0xfff)];
        readlookupp[readlnext]=mmu_perm;
        readlookup[readlnext++]=virt>>12;
        readlnext&=(cachesize-1);
        
        cycles -= 9;
}

void addwritelookup(uint32_t virt, uint32_t phys)
{
        if (virt == 0xffffffff)
                return;

        if (page_lookup[virt >> 12])
        {
                return;
        }
        
        if (writelookup[writelnext] != -1)
        {
                page_lookup[writelookup[writelnext]] = NULL;
                writelookup2[writelookup[writelnext]] = -1;
        }

        if (pages[phys >> 12].block[0] || pages[phys >> 12].block[1] || pages[phys >> 12].block[2] || pages[phys >> 12].block[3] || (phys & ~0xfff) == recomp_page)
                page_lookup[virt >> 12] = &pages[phys >> 12];
        else
                writelookup2[virt>>12] = (uintptr_t)&ram[(uintptr_t)(phys & ~0xFFF) - (uintptr_t)(virt & ~0xfff)];
        writelookupp[writelnext] = mmu_perm;
        writelookup[writelnext++] = virt >> 12;
        writelnext &= (cachesize - 1);

        cycles -= 9;
}

#undef printf
uint8_t *getpccache(uint32_t a)
{
        uint32_t a2=a;

        if (a2 < 0x100000 && ram_mapped_addr[a2 >> 14])
        {
                a = (ram_mapped_addr[a2 >> 14] & MEM_MAP_TO_SHADOW_RAM_MASK) ? a2 : (ram_mapped_addr[a2 >> 14] & ~0x3FFF) + (a2 & 0x3FFF);
                return &ram[(uintptr_t)(a & 0xFFFFF000) - (uintptr_t)(a2 & ~0xFFF)];
        }

        if (cr0>>31)
        {
		pctrans=1;
                a = mmutranslate_read(a);
                pctrans=0;

                if (a==0xFFFFFFFF) return ram;
        }
        a&=rammask;

        if (isram[a>>16])
        {
                if ((a >> 16) != 0xF || shadowbios)
                	addreadlookup(a2, a);
                return &ram[(uintptr_t)(a & 0xFFFFF000) - (uintptr_t)(a2 & ~0xFFF)];
        }

        if (_mem_exec[a >> 14])
        {
                return &_mem_exec[a >> 14][(uintptr_t)(a & 0x3000) - (uintptr_t)(a2 & ~0xFFF)];
        }

        pclog("Bad getpccache %08X\n", a);
        return &ff_array[0-(uintptr_t)(a2 & ~0xFFF)];
}
#define printf pclog

uint32_t mem_logical_addr;
uint8_t readmembl(uint32_t addr)
{
        mem_logical_addr = addr;
        if (addr < 0x100000 && ram_mapped_addr[addr >> 14])
        {
                addr = (ram_mapped_addr[addr >> 14] & MEM_MAP_TO_SHADOW_RAM_MASK) ? addr : (ram_mapped_addr[addr >> 14] & ~0x3FFF) + (addr & 0x3FFF);
                if(addr < mem_size * 1024) return ram[addr];
                return 0xFF;
        }
        if (cr0 >> 31)
        {
                addr = mmutranslate_read(addr);
                if (addr == 0xFFFFFFFF) return 0xFF;
        }
        addr &= rammask;

        if (_mem_read_b[addr >> 14]) return _mem_read_b[addr >> 14](addr, _mem_priv_r[addr >> 14]);
        return 0xFF;
}

void writemembl(uint32_t addr, uint8_t val)
{
        mem_logical_addr = addr;

        if (addr < 0x100000 && ram_mapped_addr[addr >> 14])
        {
                addr = (ram_mapped_addr[addr >> 14] & MEM_MAP_TO_SHADOW_RAM_MASK) ? addr : (ram_mapped_addr[addr >> 14] & ~0x3FFF) + (addr & 0x3FFF);
                if(addr < mem_size * 1024) ram[addr] = val;
                return;
        }
        if (page_lookup[addr>>12])
        {
                page_lookup[addr>>12]->write_b(addr, val, page_lookup[addr>>12]);
                return;
        }
        if (cr0 >> 31)
        {
                addr = mmutranslate_write(addr);
                if (addr == 0xFFFFFFFF) return;
        }
        addr &= rammask;

        if (_mem_write_b[addr >> 14]) _mem_write_b[addr >> 14](addr, val, _mem_priv_w[addr >> 14]);
}

uint8_t readmemb386l(uint32_t seg, uint32_t addr)
{
        if (seg==-1)
        {
                x86gpf("NULL segment", 0);
                return -1;
        }
        mem_logical_addr = addr = addr + seg;
        if (addr < 0x100000 && ram_mapped_addr[addr >> 14])
        {
                addr = (ram_mapped_addr[addr >> 14] & MEM_MAP_TO_SHADOW_RAM_MASK) ? addr : (ram_mapped_addr[addr >> 14] & ~0x3FFF) + (addr & 0x3FFF);
                if(addr < mem_size * 1024) return ram[addr];
                return 0xFF;
        }
        
        if (cr0 >> 31)
        {
                addr = mmutranslate_read(addr);
                if (addr == 0xFFFFFFFF) return 0xFF;
        }

        addr &= rammask;

        if (_mem_read_b[addr >> 14]) return _mem_read_b[addr >> 14](addr, _mem_priv_r[addr >> 14]);
        return 0xFF;
}

void writememb386l(uint32_t seg, uint32_t addr, uint8_t val)
{
        if (seg==-1)
        {
                x86gpf("NULL segment", 0);
                return;
        }
        
        mem_logical_addr = addr = addr + seg;
        if (addr < 0x100000 && ram_mapped_addr[addr >> 14])
        {
                addr = (ram_mapped_addr[addr >> 14] & MEM_MAP_TO_SHADOW_RAM_MASK) ? addr : (ram_mapped_addr[addr >> 14] & ~0x3FFF) + (addr & 0x3FFF);
                if(addr < mem_size * 1024) ram[addr] = val;
                return;
        }
        if (page_lookup[addr>>12])
        {
                page_lookup[addr>>12]->write_b(addr, val, page_lookup[addr>>12]);
                return;
        }
        if (cr0 >> 31)
        {
                addr = mmutranslate_write(addr);
                if (addr == 0xFFFFFFFF) return;
        }

        addr &= rammask;

        if (_mem_write_b[addr >> 14]) _mem_write_b[addr >> 14](addr, val, _mem_priv_w[addr >> 14]);
}

uint16_t readmemwl(uint32_t seg, uint32_t addr)
{
        uint32_t addr2 = mem_logical_addr = seg + addr;

        if (seg==-1)
        {
                x86gpf("NULL segment", 0);
                return -1;
        }
        if (addr2 & 1)
        {
                if (!cpu_cyrix_alignment || (addr2 & 7) == 7)
                        cycles -= timing_misaligned;
                if ((addr2 & 0xFFF) > 0xFFE)
                {
                        if (cr0 >> 31)
                        {
                                if (mmutranslate_read(addr2)   == 0xffffffff) return 0xffff;
                                if (mmutranslate_read(addr2+1) == 0xffffffff) return 0xffff;
                        }
                        if (is386) return readmemb386l(seg,addr)|(readmemb386l(seg,addr+1)<<8);
                        else       return readmembl(seg+addr)|(readmembl(seg+addr+1)<<8);
                }
                else if (readlookup2[addr2 >> 12] != -1)
                        return *(uint16_t *)(readlookup2[addr2 >> 12] + addr2);
        }
        if (addr2 < 0x100000 && ram_mapped_addr[addr2 >> 14])
        {
                addr = (ram_mapped_addr[addr2 >> 14] & MEM_MAP_TO_SHADOW_RAM_MASK) ? addr2 : (ram_mapped_addr[addr2 >> 14] & ~0x3FFF) + (addr2 & 0x3FFF);
                if(addr < mem_size * 1024) return *((uint16_t *)&ram[addr]);
                return 0xFFFF;
        }
        if (cr0>>31)
        {
                addr2 = mmutranslate_read(addr2);
                if (addr2==0xFFFFFFFF) return 0xFFFF;
        }

        addr2 &= rammask;

        if (_mem_read_w[addr2 >> 14]) return _mem_read_w[addr2 >> 14](addr2, _mem_priv_r[addr2 >> 14]);

        if (_mem_read_b[addr2 >> 14])
        {
                if (AT) return _mem_read_b[addr2 >> 14](addr2, _mem_priv_r[addr2 >> 14]) | (_mem_read_b[(addr2 + 1) >> 14](addr2 + 1, _mem_priv_r[addr2 >> 14]) << 8);
                else    return _mem_read_b[addr2 >> 14](addr2, _mem_priv_r[addr2 >> 14]) | (_mem_read_b[(seg + ((addr + 1) & 0xffff)) >> 14](seg + ((addr + 1) & 0xffff), _mem_priv_r[addr2 >> 14]) << 8);
        }
        return 0xffff;
}

void writememwl(uint32_t seg, uint32_t addr, uint16_t val)
{
        uint32_t addr2 = mem_logical_addr = seg + addr;

        if (seg==-1)
        {
                x86gpf("NULL segment", 0);
                return;
        }
        if (addr2 < 0x100000 && ram_mapped_addr[addr2 >> 14])
        {
                addr = (ram_mapped_addr[addr2 >> 14] & MEM_MAP_TO_SHADOW_RAM_MASK) ? addr2 : (ram_mapped_addr[addr2 >> 14] & ~0x3FFF) + (addr2 & 0x3FFF);
                if(addr < mem_size * 1024) *((uint16_t *)&ram[addr]) = val;
                return;
        }

        if (addr2 & 1)
        {
                if (!cpu_cyrix_alignment || (addr2 & 7) == 7)
                        cycles -= timing_misaligned;
                if ((addr2 & 0xFFF) > 0xFFE)
                {
                        if (cr0 >> 31)
                        {
                                if (mmutranslate_write(addr2)   == 0xffffffff) return;
                                if (mmutranslate_write(addr2+1) == 0xffffffff) return;
                        }
                        if (is386)
                        {
                                writememb386l(seg,addr,val);
                                writememb386l(seg,addr+1,val>>8);
                        }
                        else
                        {
                                writemembl(seg+addr,val);
                                writemembl(seg+addr+1,val>>8);
                        }
                        return;
                }
                else if (writelookup2[addr2 >> 12] != -1)
                {
                        *(uint16_t *)(writelookup2[addr2 >> 12] + addr2) = val;
                        return;
                }
        }

        if (page_lookup[addr2>>12])
        {
                page_lookup[addr2>>12]->write_w(addr2, val, page_lookup[addr2>>12]);
                return;
        }
        if (cr0>>31)
        {
                addr2 = mmutranslate_write(addr2);
                if (addr2==0xFFFFFFFF) return;
        }
        
        addr2 &= rammask;

/*        if (addr2 >= 0xa0000 && addr2 < 0xc0000)
           pclog("writememwl %08X %02X\n", addr2, val);*/
        
        if (_mem_write_w[addr2 >> 14]) 
        {
                _mem_write_w[addr2 >> 14](addr2, val, _mem_priv_w[addr2 >> 14]);
                return;
        }

        if (_mem_write_b[addr2 >> 14]) 
        {
                _mem_write_b[addr2 >> 14](addr2, val, _mem_priv_w[addr2 >> 14]);
                _mem_write_b[(addr2 + 1) >> 14](addr2 + 1, val >> 8, _mem_priv_w[addr2 >> 14]);
                return;
        }
}

uint32_t readmemll(uint32_t seg, uint32_t addr)
{
        uint32_t addr2 = mem_logical_addr = seg + addr;

        if (seg==-1)
        {
                x86gpf("NULL segment", 0);
                return -1;
        }
        
        if (addr2 < 0x100000 && ram_mapped_addr[addr2 >> 14])
        {
                addr = (ram_mapped_addr[addr2 >> 14] & MEM_MAP_TO_SHADOW_RAM_MASK) ? addr2 : (ram_mapped_addr[addr2 >> 14] & ~0x3FFF) + (addr2 & 0x3FFF);
                if(addr < mem_size * 1024) return *((uint32_t *)&ram[addr]);
                return 0xFFFFFFFF;
        }

        if (addr2 & 3)
        {
                if (!cpu_cyrix_alignment || (addr2 & 7) > 4)
                        cycles -= timing_misaligned;
                if ((addr2&0xFFF)>0xFFC)
                {
                        if (cr0>>31)
                        {
                                if (mmutranslate_read(addr2)   == 0xffffffff) return 0xffffffff;
                                if (mmutranslate_read(addr2+3) == 0xffffffff) return 0xffffffff;
                        }
                        return readmemwl(seg,addr)|(readmemwl(seg,addr+2)<<16);
                }
                else if (readlookup2[addr2 >> 12] != -1)
                        return *(uint32_t *)(readlookup2[addr2 >> 12] + addr2);
        }

        if (cr0>>31)
        {
                addr2 = mmutranslate_read(addr2);
                if (addr2==0xFFFFFFFF) return 0xFFFFFFFF;
        }

        addr2&=rammask;

        if (_mem_read_l[addr2 >> 14]) return _mem_read_l[addr2 >> 14](addr2, _mem_priv_r[addr2 >> 14]);

        if (_mem_read_w[addr2 >> 14]) return _mem_read_w[addr2 >> 14](addr2, _mem_priv_r[addr2 >> 14]) | (_mem_read_w[addr2 >> 14](addr2 + 2, _mem_priv_r[addr2 >> 14]) << 16);
        
        if (_mem_read_b[addr2 >> 14]) return _mem_read_b[addr2 >> 14](addr2, _mem_priv_r[addr2 >> 14]) | (_mem_read_b[addr2 >> 14](addr2 + 1, _mem_priv_r[addr2 >> 14]) << 8) | (_mem_read_b[addr2 >> 14](addr2 + 2, _mem_priv_r[addr2 >> 14]) << 16) | (_mem_read_b[addr2 >> 14](addr2 + 3, _mem_priv_r[addr2 >> 14]) << 24);

        return 0xffffffff;
}

void writememll(uint32_t seg, uint32_t addr, uint32_t val)
{
        uint32_t addr2 = mem_logical_addr = seg + addr;

        if (seg==-1)
        {
                x86gpf("NULL segment", 0);
                return;
        }
        if (addr2 < 0x100000 && ram_mapped_addr[addr2 >> 14])
        {
                addr = (ram_mapped_addr[addr2 >> 14] & MEM_MAP_TO_SHADOW_RAM_MASK) ? addr2 : (ram_mapped_addr[addr2 >> 14] & ~0x3FFF) + (addr2 & 0x3FFF);
                if(addr < mem_size * 1024) *((uint32_t *)&ram[addr]) = val;
                return;
        }
        if (addr2 & 3)
        {
                if (!cpu_cyrix_alignment || (addr2 & 7) > 4)
                        cycles -= timing_misaligned;
                if ((addr2 & 0xFFF) > 0xFFC)
                {
                        if (cr0>>31)
                        {
                                if (mmutranslate_write(addr2)   == 0xffffffff) return;
                                if (mmutranslate_write(addr2+3) == 0xffffffff) return;
                        }
                        writememwl(seg,addr,val);
                        writememwl(seg,addr+2,val>>16);
                        return;
                }
                else if (writelookup2[addr2 >> 12] != -1)
                {
                        *(uint32_t *)(writelookup2[addr2 >> 12] + addr2) = val;
                        return;
                }
        }
        if (page_lookup[addr2>>12])
        {
                page_lookup[addr2>>12]->write_l(addr2, val, page_lookup[addr2>>12]);
                return;
        }
        if (cr0>>31)
        {
                addr2 = mmutranslate_write(addr2);
                if (addr2==0xFFFFFFFF) return;
        }
        
        addr2&=rammask;

        if (_mem_write_l[addr2 >> 14]) 
        {
                _mem_write_l[addr2 >> 14](addr2, val,           _mem_priv_w[addr2 >> 14]);
                return;
        }
        if (_mem_write_w[addr2 >> 14]) 
        {
                _mem_write_w[addr2 >> 14](addr2,     val,       _mem_priv_w[addr2 >> 14]);
                _mem_write_w[addr2 >> 14](addr2 + 2, val >> 16, _mem_priv_w[addr2 >> 14]);
                return;
        }
        if (_mem_write_b[addr2 >> 14]) 
        {
                _mem_write_b[addr2 >> 14](addr2,     val,       _mem_priv_w[addr2 >> 14]);
                _mem_write_b[addr2 >> 14](addr2 + 1, val >> 8,  _mem_priv_w[addr2 >> 14]);
                _mem_write_b[addr2 >> 14](addr2 + 2, val >> 16, _mem_priv_w[addr2 >> 14]);
                _mem_write_b[addr2 >> 14](addr2 + 3, val >> 24, _mem_priv_w[addr2 >> 14]);
                return;
        }
}

uint64_t readmemql(uint32_t seg, uint32_t addr)
{
        uint32_t addr2 = mem_logical_addr = seg + addr;

        if (seg==-1)
        {
                x86gpf("NULL segment", 0);
                return -1;
        }
        
        if (addr2 < 0x100000 && ram_mapped_addr[addr2 >> 14])
        {
                addr = (ram_mapped_addr[addr2 >> 14] & MEM_MAP_TO_SHADOW_RAM_MASK) ? addr2 : (ram_mapped_addr[addr2 >> 14] & ~0x3FFF) + (addr2 & 0x3FFF);
                if(addr < mem_size * 1024) return *((uint64_t *)&ram[addr]);
                return -1;
        }

        if (addr2 & 7)
        {
                cycles -= timing_misaligned;
                if ((addr2 & 0xFFF) > 0xFF8)
                {
                        if (cr0>>31)
                        {
                                if (mmutranslate_read(addr2)   == 0xffffffff) return 0xffffffff;
                                if (mmutranslate_read(addr2+7) == 0xffffffff) return 0xffffffff;
                        }
                        return readmemll(seg,addr)|((uint64_t)readmemll(seg,addr+4)<<32);
                }
                else if (readlookup2[addr2 >> 12] != -1)
                        return *(uint64_t *)(readlookup2[addr2 >> 12] + addr2);
        }
        
        if (cr0>>31)
        {
                addr2 = mmutranslate_read(addr2);
                if (addr2==0xFFFFFFFF) return -1;
        }

        addr2&=rammask;

        if (_mem_read_l[addr2 >> 14])
                return _mem_read_l[addr2 >> 14](addr2, _mem_priv_r[addr2 >> 14]) |
                                 ((uint64_t)_mem_read_l[addr2 >> 14](addr2 + 4, _mem_priv_r[addr2 >> 14]) << 32);

        return readmemll(seg,addr) | ((uint64_t)readmemll(seg,addr+4)<<32);
}

void writememql(uint32_t seg, uint32_t addr, uint64_t val)
{
        uint32_t addr2 = mem_logical_addr = seg + addr;

        if (seg==-1)
        {
                x86gpf("NULL segment", 0);
                return;
        }
        if (addr2 < 0x100000 && ram_mapped_addr[addr2 >> 14])
        {
                addr = (ram_mapped_addr[addr2 >> 14] & MEM_MAP_TO_SHADOW_RAM_MASK) ? addr2 : (ram_mapped_addr[addr2 >> 14] & ~0x3FFF) + (addr2 & 0x3FFF);
                if(addr < mem_size * 1024) *((uint64_t *)&ram[addr]) = val;
                return;
        }
        if (addr2 & 7)
        {
                cycles -= timing_misaligned;
                if ((addr2 & 0xFFF) > 0xFF8)
                {
                        if (cr0>>31)
                        {
                                if (mmutranslate_write(addr2)   == 0xffffffff) return;
                                if (mmutranslate_write(addr2+7) == 0xffffffff) return;
                        }
                        writememll(seg, addr, val);
                        writememll(seg, addr+4, val >> 32);
                        return;
                }
                else if (writelookup2[addr2 >> 12] != -1)
                {
                        *(uint64_t *)(writelookup2[addr2 >> 12] + addr2) = val;
                        return;
                }
        }
        if (page_lookup[addr2>>12])
        {
                page_lookup[addr2>>12]->write_l(addr2, val, page_lookup[addr2>>12]);
                page_lookup[addr2>>12]->write_l(addr2 + 4, val >> 32, page_lookup[addr2>>12]);
                return;
        }
        if (cr0>>31)
        {
                addr2 = mmutranslate_write(addr2);
                if (addr2==0xFFFFFFFF) return;
        }
        
        addr2&=rammask;

        if (_mem_write_l[addr2 >> 14]) 
        {
                _mem_write_l[addr2 >> 14](addr2,   val,       _mem_priv_w[addr2 >> 14]);
                _mem_write_l[addr2 >> 14](addr2+4, val >> 32, _mem_priv_w[addr2 >> 14]);
                return;
        }
        if (_mem_write_w[addr2 >> 14]) 
        {
                _mem_write_w[addr2 >> 14](addr2,     val,       _mem_priv_w[addr2 >> 14]);
                _mem_write_w[addr2 >> 14](addr2 + 2, val >> 16, _mem_priv_w[addr2 >> 14]);
                _mem_write_w[addr2 >> 14](addr2 + 4, val >> 32, _mem_priv_w[addr2 >> 14]);
                _mem_write_w[addr2 >> 14](addr2 + 6, val >> 48, _mem_priv_w[addr2 >> 14]);
                return;
        }
        if (_mem_write_b[addr2 >> 14]) 
        {
                _mem_write_b[addr2 >> 14](addr2,     val,       _mem_priv_w[addr2 >> 14]);
                _mem_write_b[addr2 >> 14](addr2 + 1, val >> 8,  _mem_priv_w[addr2 >> 14]);
                _mem_write_b[addr2 >> 14](addr2 + 2, val >> 16, _mem_priv_w[addr2 >> 14]);
                _mem_write_b[addr2 >> 14](addr2 + 3, val >> 24, _mem_priv_w[addr2 >> 14]);
                _mem_write_b[addr2 >> 14](addr2 + 4, val >> 32, _mem_priv_w[addr2 >> 14]);
                _mem_write_b[addr2 >> 14](addr2 + 5, val >> 40, _mem_priv_w[addr2 >> 14]);
                _mem_write_b[addr2 >> 14](addr2 + 6, val >> 48, _mem_priv_w[addr2 >> 14]);
                _mem_write_b[addr2 >> 14](addr2 + 7, val >> 56, _mem_priv_w[addr2 >> 14]);
                return;
        }
}

uint8_t mem_readb_phys(uint32_t addr)
{
        mem_logical_addr = 0xffffffff;
        
        if (_mem_read_b[addr >> 14]) 
                return _mem_read_b[addr >> 14](addr, _mem_priv_r[addr >> 14]);
                
        return 0xff;
}

uint16_t mem_readw_phys(uint32_t addr)
{
        mem_logical_addr = 0xffffffff;
        
        if (_mem_read_w[addr >> 14]) 
                return _mem_read_w[addr >> 14](addr, _mem_priv_r[addr >> 14]);
                
        return 0xff;
}

void mem_writeb_phys(uint32_t addr, uint8_t val)
{
        mem_logical_addr = 0xffffffff;
        
        if (_mem_write_b[addr >> 14]) 
                _mem_write_b[addr >> 14](addr, val, _mem_priv_w[addr >> 14]);
}

void mem_writew_phys(uint32_t addr, uint16_t val)
{
        mem_logical_addr = 0xffffffff;
        
        if (_mem_write_w[addr >> 14]) 
                _mem_write_w[addr >> 14](addr, val, _mem_priv_w[addr >> 14]);
}

uint8_t mem_read_ram(uint32_t addr, void *priv)
{
        addreadlookup(mem_logical_addr, addr);
        return ram[addr];
}
uint16_t mem_read_ramw(uint32_t addr, void *priv)
{
        addreadlookup(mem_logical_addr, addr);
        return *(uint16_t *)&ram[addr];
}
uint32_t mem_read_raml(uint32_t addr, void *priv)
{
        addreadlookup(mem_logical_addr, addr);
        return *(uint32_t *)&ram[addr];
}

void mem_write_ramb_page(uint32_t addr, uint8_t val, page_t *p)
{      
        if (val != p->mem[addr & 0xfff] || codegen_in_recompile)
        {
                uint64_t mask = (uint64_t)1 << ((addr >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK);
                p->dirty_mask[(addr >> PAGE_MASK_INDEX_SHIFT) & PAGE_MASK_INDEX_MASK] |= mask;
                p->mem[addr & 0xfff] = val;
        }
}
void mem_write_ramw_page(uint32_t addr, uint16_t val, page_t *p)
{
        if (val != *(uint16_t *)&p->mem[addr & 0xfff] || codegen_in_recompile)
        {
                uint64_t mask = (uint64_t)1 << ((addr >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK);
                if ((addr & 0xf) == 0xf)
                        mask |= (mask << 1);
                p->dirty_mask[(addr >> PAGE_MASK_INDEX_SHIFT) & PAGE_MASK_INDEX_MASK] |= mask;
                *(uint16_t *)&p->mem[addr & 0xfff] = val;
        }
}
void mem_write_raml_page(uint32_t addr, uint32_t val, page_t *p)
{       
        if (val != *(uint32_t *)&p->mem[addr & 0xfff] || codegen_in_recompile)
        {
                uint64_t mask = (uint64_t)1 << ((addr >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK);
                if ((addr & 0xf) >= 0xd)
                        mask |= (mask << 1);
                p->dirty_mask[(addr >> PAGE_MASK_INDEX_SHIFT) & PAGE_MASK_INDEX_MASK] |= mask;
                *(uint32_t *)&p->mem[addr & 0xfff] = val;
        }
}

void mem_write_ram(uint32_t addr, uint8_t val, void *priv)
{
        addwritelookup(mem_logical_addr, addr);
        mem_write_ramb_page(addr, val, &pages[addr >> 12]);
}
void mem_write_ramw(uint32_t addr, uint16_t val, void *priv)
{
        addwritelookup(mem_logical_addr, addr);
        mem_write_ramw_page(addr, val, &pages[addr >> 12]);
}
void mem_write_raml(uint32_t addr, uint32_t val, void *priv)
{
        addwritelookup(mem_logical_addr, addr);
        mem_write_raml_page(addr, val, &pages[addr >> 12]);
}

static uint32_t remap_start_addr;

uint8_t mem_read_remapped(uint32_t addr, void *priv)
{
        addr = 0xA0000 + (addr - remap_start_addr);
        addreadlookup(mem_logical_addr, addr);
        return ram[addr];
}
uint16_t mem_read_remappedw(uint32_t addr, void *priv)
{
        addr = 0xA0000 + (addr - remap_start_addr);
        addreadlookup(mem_logical_addr, addr);
        return *(uint16_t *)&ram[addr];
}
uint32_t mem_read_remappedl(uint32_t addr, void *priv)
{
        addr = 0xA0000 + (addr - remap_start_addr);
        addreadlookup(mem_logical_addr, addr);
        return *(uint32_t *)&ram[addr];
}

void mem_write_remapped(uint32_t addr, uint8_t val, void *priv)
{
        uint32_t oldaddr = addr;
        addr = 0xA0000 + (addr - remap_start_addr);
        addwritelookup(mem_logical_addr, addr);
        mem_write_ramb_page(addr, val, &pages[oldaddr >> 12]);
}
void mem_write_remappedw(uint32_t addr, uint16_t val, void *priv)
{
        uint32_t oldaddr = addr;
        addr = 0xA0000 + (addr - remap_start_addr);
        addwritelookup(mem_logical_addr, addr);
        mem_write_ramw_page(addr, val, &pages[oldaddr >> 12]);
}
void mem_write_remappedl(uint32_t addr, uint32_t val, void *priv)
{
        uint32_t oldaddr = addr;
        addr = 0xA0000 + (addr - remap_start_addr);
        addwritelookup(mem_logical_addr, addr);
        mem_write_raml_page(addr, val, &pages[oldaddr >> 12]);
}

uint8_t mem_read_bios(uint32_t addr, void *priv)
{
	if (AMIBIOS && (addr&0xFFFFF)==0xF8281) /*This is read constantly during AMIBIOS POST, but is never written to. It's clearly a status register of some kind, but for what?*/
	{
		return 0x40;
	}
        return rom[addr & biosmask];
}
uint16_t mem_read_biosw(uint32_t addr, void *priv)
{
        return *(uint16_t *)&rom[addr & biosmask];
}
uint32_t mem_read_biosl(uint32_t addr, void *priv)
{
        return *(uint32_t *)&rom[addr & biosmask];
}

uint8_t mem_read_romext(uint32_t addr, void *priv)
{
        return romext[addr & 0x7fff];
}
uint16_t mem_read_romextw(uint32_t addr, void *priv)
{
	uint16_t *p = (uint16_t *)&romext[addr & 0x7fff];
	return *p;
}
uint32_t mem_read_romextl(uint32_t addr, void *priv)
{
	uint32_t *p = (uint32_t *)&romext[addr & 0x7fff];
        return *p;
}

void mem_write_null(uint32_t addr, uint8_t val, void *p)
{
}
void mem_write_nullw(uint32_t addr, uint16_t val, void *p)
{
}
void mem_write_nulll(uint32_t addr, uint32_t val, void *p)
{
}

void mem_invalidate_range(uint32_t start_addr, uint32_t end_addr)
{
        start_addr &= ~PAGE_MASK_MASK;
        end_addr = (end_addr + PAGE_MASK_MASK) & ~PAGE_MASK_MASK;        
        
        for (; start_addr <= end_addr; start_addr += (1 << PAGE_MASK_SHIFT))
        {
                uint64_t mask = (uint64_t)1 << ((start_addr >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK);

                pages[start_addr >> 12].dirty_mask[(start_addr >> PAGE_MASK_INDEX_SHIFT) & PAGE_MASK_INDEX_MASK] |= mask;
        }
}

static __inline int mem_mapping_read_allowed(uint32_t flags, int state)
{
        switch (state & MEM_READ_MASK)
        {
                case MEM_READ_ANY:
                return 1;
                case MEM_READ_EXTERNAL:
                return !(flags & MEM_MAPPING_INTERNAL);
                case MEM_READ_INTERNAL:
                return !(flags & MEM_MAPPING_EXTERNAL);
                default:
                fatal("mem_mapping_read_allowed : bad state %x\n", state);
		return 0;
        }
}

static __inline int mem_mapping_write_allowed(uint32_t flags, int state)
{
        switch (state & MEM_WRITE_MASK)
        {
                case MEM_WRITE_DISABLED:
                return 0;
                case MEM_WRITE_ANY:
                return 1;
                case MEM_WRITE_EXTERNAL:
                return !(flags & MEM_MAPPING_INTERNAL);
                case MEM_WRITE_INTERNAL:
                return !(flags & MEM_MAPPING_EXTERNAL);
                default:
                fatal("mem_mapping_write_allowed : bad state %x\n", state);
		return 0;
        }
}

static void mem_mapping_recalc(uint64_t base, uint64_t size)
{
        uint64_t c;
        mem_mapping_t *mapping = base_mapping.next;

        if (!size)
                return;
        /*Clear out old mappings*/
        for (c = base; c < base + size; c += 0x4000)
        {
                _mem_read_b[c >> 14] = NULL;
                _mem_read_w[c >> 14] = NULL;
                _mem_read_l[c >> 14] = NULL;
                _mem_priv_r[c >> 14] = NULL;
                _mem_mapping_r[c >> 14] = NULL;
                _mem_write_b[c >> 14] = NULL;
                _mem_write_w[c >> 14] = NULL;
                _mem_write_l[c >> 14] = NULL;
                _mem_priv_w[c >> 14] = NULL;
                _mem_mapping_w[c >> 14] = NULL;
        }

        /*Walk mapping list*/
        while (mapping != NULL)
        {
                /*In range?*/
                if (mapping->enable && (uint64_t)mapping->base < ((uint64_t)base + (uint64_t)size) && ((uint64_t)mapping->base + (uint64_t)mapping->size) > (uint64_t)base)
                {
                        uint64_t start = (mapping->base < base) ? mapping->base : base;
                        uint64_t end   = (((uint64_t)mapping->base + (uint64_t)mapping->size) < (base + size)) ? ((uint64_t)mapping->base + (uint64_t)mapping->size) : (base + size);
                        if (start < mapping->base)
                                start = mapping->base;

                        for (c = start; c < end; c += 0x4000)
                        {
                                if ((mapping->read_b || mapping->read_w || mapping->read_l) &&
                                     mem_mapping_read_allowed(mapping->flags, _mem_state[c >> 14]))
                                {
                                        _mem_read_b[c >> 14] = mapping->read_b;
                                        _mem_read_w[c >> 14] = mapping->read_w;
                                        _mem_read_l[c >> 14] = mapping->read_l;
                                        if (mapping->exec)
                                                _mem_exec[c >> 14] = mapping->exec + (c - mapping->base);
                                        else
                                                _mem_exec[c >> 14] = NULL;
                                        _mem_priv_r[c >> 14] = mapping->p;
                                        _mem_mapping_r[c >> 14] = mapping;
                                }
                                if ((mapping->write_b || mapping->write_w || mapping->write_l) &&
                                     mem_mapping_write_allowed(mapping->flags, _mem_state[c >> 14]))
                                {
                                        _mem_write_b[c >> 14] = mapping->write_b;
                                        _mem_write_w[c >> 14] = mapping->write_w;
                                        _mem_write_l[c >> 14] = mapping->write_l;
                                        _mem_priv_w[c >> 14] = mapping->p;
                                        _mem_mapping_w[c >> 14] = mapping;
                                }
                        }
                }
                mapping = mapping->next;
        }       
        flushmmucache_cr3();
}

void mem_mapping_add(mem_mapping_t *mapping,
                    uint32_t base, 
                    uint32_t size, 
                    uint8_t  (*read_b)(uint32_t addr, void *p),
                    uint16_t (*read_w)(uint32_t addr, void *p),
                    uint32_t (*read_l)(uint32_t addr, void *p),
                    void (*write_b)(uint32_t addr, uint8_t  val, void *p),
                    void (*write_w)(uint32_t addr, uint16_t val, void *p),
                    void (*write_l)(uint32_t addr, uint32_t val, void *p),
                    uint8_t *exec,
                    uint32_t flags,
                    void *p)
{
        mem_mapping_t *dest = &base_mapping;

        /*Add mapping to the end of the list*/
        while (dest->next)
                dest = dest->next;        
        dest->next = mapping;
        
        if (size)
                mapping->enable  = 1;
        else
                mapping->enable  = 0;
        mapping->base    = base;
        mapping->size    = size;
        mapping->read_b  = read_b;
        mapping->read_w  = read_w;
        mapping->read_l  = read_l;
        mapping->write_b = write_b;
        mapping->write_w = write_w;
        mapping->write_l = write_l;
        mapping->exec    = exec;
        mapping->flags   = flags;
        mapping->p       = p;
        mapping->next    = NULL;
        
        mem_mapping_recalc(mapping->base, mapping->size);
}

void mem_mapping_set_handler(mem_mapping_t *mapping,
                    uint8_t  (*read_b)(uint32_t addr, void *p),
                    uint16_t (*read_w)(uint32_t addr, void *p),
                    uint32_t (*read_l)(uint32_t addr, void *p),
                    void (*write_b)(uint32_t addr, uint8_t  val, void *p),
                    void (*write_w)(uint32_t addr, uint16_t val, void *p),
                    void (*write_l)(uint32_t addr, uint32_t val, void *p))
{
        mapping->read_b  = read_b;
        mapping->read_w  = read_w;
        mapping->read_l  = read_l;
        mapping->write_b = write_b;
        mapping->write_w = write_w;
        mapping->write_l = write_l;
        
        mem_mapping_recalc(mapping->base, mapping->size);
}

void mem_mapping_set_addr(mem_mapping_t *mapping, uint32_t base, uint32_t size)
{
        /*Remove old mapping*/
        mapping->enable = 0;
        mem_mapping_recalc(mapping->base, mapping->size);
        
        /*Set new mapping*/
        mapping->enable = 1;
        mapping->base = base;
        mapping->size = size;
        
        mem_mapping_recalc(mapping->base, mapping->size);
}

void mem_mapping_set_exec(mem_mapping_t *mapping, uint8_t *exec)
{
        mapping->exec = exec;
        
        mem_mapping_recalc(mapping->base, mapping->size);
}

void mem_mapping_set_p(mem_mapping_t *mapping, void *p)
{
        mapping->p = p;
}

void mem_mapping_disable(mem_mapping_t *mapping)
{
        mapping->enable = 0;
        
        mem_mapping_recalc(mapping->base, mapping->size);
}

void mem_mapping_enable(mem_mapping_t *mapping)
{
        mapping->enable = 1;
        
        mem_mapping_recalc(mapping->base, mapping->size);
}

void mem_set_mem_state(uint32_t base, uint32_t size, int state)
{
        uint32_t c;

        for (c = 0; c < size; c += 0x4000)
                _mem_state[(c + base) >> 14] = state;

        mem_mapping_recalc(base, size);
}

void mem_add_bios()
{
        if (AT)
        {
                mem_mapping_add(&bios_mapping[0], 0xe0000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom,                        MEM_MAPPING_EXTERNAL, 0);
                mem_mapping_add(&bios_mapping[1], 0xe4000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x4000  & biosmask), MEM_MAPPING_EXTERNAL, 0);
                mem_mapping_add(&bios_mapping[2], 0xe8000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x8000  & biosmask), MEM_MAPPING_EXTERNAL, 0);
                mem_mapping_add(&bios_mapping[3], 0xec000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0xc000  & biosmask), MEM_MAPPING_EXTERNAL, 0);
        }
        mem_mapping_add(&bios_mapping[4], 0xf0000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x10000 & biosmask), MEM_MAPPING_EXTERNAL, 0);
        mem_mapping_add(&bios_mapping[5], 0xf4000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x14000 & biosmask), MEM_MAPPING_EXTERNAL, 0);
        mem_mapping_add(&bios_mapping[6], 0xf8000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x18000 & biosmask), MEM_MAPPING_EXTERNAL, 0);
        mem_mapping_add(&bios_mapping[7], 0xfc000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x1c000 & biosmask), MEM_MAPPING_EXTERNAL, 0);

        mem_mapping_add(&bios_high_mapping[0], 0xfffe0000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom,                        0, 0);
        mem_mapping_add(&bios_high_mapping[1], 0xfffe4000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x4000  & biosmask), 0, 0);
        mem_mapping_add(&bios_high_mapping[2], 0xfffe8000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x8000  & biosmask), 0, 0);
        mem_mapping_add(&bios_high_mapping[3], 0xfffec000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0xc000  & biosmask), 0, 0);
        mem_mapping_add(&bios_high_mapping[4], 0xffff0000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x10000 & biosmask), 0, 0);
        mem_mapping_add(&bios_high_mapping[5], 0xffff4000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x14000 & biosmask), 0, 0);
        mem_mapping_add(&bios_high_mapping[6], 0xffff8000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x18000 & biosmask), 0, 0);
        mem_mapping_add(&bios_high_mapping[7], 0xffffc000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x1c000 & biosmask), 0, 0);
}

int mem_a20_key = 0, mem_a20_alt = 0;
int mem_a20_state = 1;

void mem_init()
{
        int c;

        ram = malloc(mem_size * 1024);
        readlookup2  = malloc(1024 * 1024 * sizeof(uintptr_t));
        writelookup2 = malloc(1024 * 1024 * sizeof(uintptr_t));
        biosmask = 0xffff;
        pages = malloc((((mem_size + 384) * 1024) >> 12) * sizeof(page_t));
        page_lookup = malloc((1 << 20) * sizeof(page_t *));

        memset(ram, 0, mem_size * 1024);
        memset(pages, 0, (((mem_size + 384) * 1024) >> 12) * sizeof(page_t));
        
        memset(page_lookup, 0, (1 << 20) * sizeof(page_t *));

	memset(ram_mapped_addr, 0, 64 * sizeof(uint32_t));
        
        for (c = 0; c < (((mem_size + 384) * 1024) >> 12); c++)
        {
                pages[c].mem = &ram[c << 12];
                pages[c].write_b = mem_write_ramb_page;
                pages[c].write_w = mem_write_ramw_page;
                pages[c].write_l = mem_write_raml_page;
        }

        memset(isram, 0, sizeof(isram));
        for (c = 0; c < (mem_size / 64); c++)
        {
                isram[c] = 1;
                if (c >= 0xa && c <= 0xf) 
                        isram[c] = 0;
        }

        memset(_mem_read_b,  0, sizeof(_mem_read_b));
        memset(_mem_read_w,  0, sizeof(_mem_read_w));
        memset(_mem_read_l,  0, sizeof(_mem_read_l));
        memset(_mem_write_b, 0, sizeof(_mem_write_b));
        memset(_mem_write_w, 0, sizeof(_mem_write_w));
        memset(_mem_write_l, 0, sizeof(_mem_write_l));
        memset(_mem_exec, 0, sizeof(_mem_exec));
        
        memset(ff_array, 0xff, sizeof(ff_array));

        memset(&base_mapping, 0, sizeof(base_mapping));

        memset(_mem_state, 0, sizeof(_mem_state));

        mem_set_mem_state(0x000000, (mem_size > 640) ? 0xa0000 : mem_size * 1024, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        mem_set_mem_state(0x0c0000, 0x40000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
	if (mem_size > 1024)
	{
	        mem_set_mem_state(0x100000, (mem_size - 1024) * 1024, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
	}

        mem_mapping_add(&ram_low_mapping, 0x00000, (mem_size > 640) ? 0xa0000 : mem_size * 1024, mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram,  MEM_MAPPING_INTERNAL, NULL);
        if (mem_size > 1024)
                mem_mapping_add(&ram_high_mapping, 0x100000, ((mem_size - 1024) * 1024), mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram + 0x100000, MEM_MAPPING_INTERNAL, NULL);
	if (mem_size > 768)
		mem_mapping_add(&ram_mid_mapping,   0xc0000, 0x40000, mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram + 0xc0000,  MEM_MAPPING_INTERNAL, NULL);
 
        if (romset == ROM_IBMPS1_2011)
                mem_mapping_add(&romext_mapping,  0xc8000, 0x08000, mem_read_romext, mem_read_romextw, mem_read_romextl, NULL, NULL, NULL,   romext, 0, NULL);

        mem_a20_key = 2;
	mem_a20_alt = 0;
        mem_a20_recalc();
}

static void mem_remap_top(int max_size)
{
        int c;

	if (mem_size > 640)
	{
                uint32_t start = (mem_size >= 1024) ? mem_size : 1024;
                int size = mem_size - 640;
                if (size > max_size)
                        size = max_size;
                
                remap_start_addr = start * 1024;
                        
                for (c = ((start * 1024) >> 12); c < (((start + size) * 1024) >> 12); c++)
                {
                        pages[c].mem = &ram[0xA0000 + ((c - ((start * 1024) >> 12)) << 12)];
                        pages[c].write_b = mem_write_ramb_page;
                        pages[c].write_w = mem_write_ramw_page;
                        pages[c].write_l = mem_write_raml_page;
                }

                mem_set_mem_state(start * 1024, size * 1024, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                mem_mapping_add(&ram_remapped_mapping, start * 1024, size * 1024, mem_read_remapped,    mem_read_remappedw,    mem_read_remappedl,    mem_write_remapped, mem_write_remappedw, mem_write_remappedl,   ram + 0xA0000,  MEM_MAPPING_INTERNAL, NULL);
	}
}

void mem_remap_top_256k()
{
	mem_remap_top(256);
}

void mem_remap_top_384k()
{
	mem_remap_top(384);
}

void mem_resize()
{
        int c;
        
        free(ram);
        ram = malloc(mem_size * 1024);
        memset(ram, 0, mem_size * 1024);
        
        free(pages);
        pages = malloc((((mem_size + 384) * 1024) >> 12) * sizeof(page_t));
        memset(pages, 0, (((mem_size + 384) * 1024) >> 12) * sizeof(page_t));
        for (c = 0; c < (((mem_size + 384) * 1024) >> 12); c++)
        {
                pages[c].mem = &ram[c << 12];
                pages[c].write_b = mem_write_ramb_page;
                pages[c].write_w = mem_write_ramw_page;
                pages[c].write_l = mem_write_raml_page;
        }
        
        memset(isram, 0, sizeof(isram));
        for (c = 0; c < (mem_size / 64); c++)
        {
                isram[c] = 1;
                if (c >= 0xa && c <= 0xf) 
                        isram[c] = 0;
        }

        memset(_mem_read_b,  0, sizeof(_mem_read_b));
        memset(_mem_read_w,  0, sizeof(_mem_read_w));
        memset(_mem_read_l,  0, sizeof(_mem_read_l));
        memset(_mem_write_b, 0, sizeof(_mem_write_b));
        memset(_mem_write_w, 0, sizeof(_mem_write_w));
        memset(_mem_write_l, 0, sizeof(_mem_write_l));
        memset(_mem_exec, 0, sizeof(_mem_exec));
        
        memset(&base_mapping, 0, sizeof(base_mapping));
        
        memset(_mem_state, 0, sizeof(_mem_state));

        mem_set_mem_state(0x000000, (mem_size > 640) ? 0xa0000 : mem_size * 1024, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        mem_set_mem_state(0x0c0000, 0x40000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
	if (mem_size > 1024)
	{
	        mem_set_mem_state(0x100000, (mem_size - 1024) * 1024, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
	}
        
        mem_mapping_add(&ram_low_mapping, 0x00000, (mem_size > 640) ? 0xa0000 : mem_size * 1024, mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram,  MEM_MAPPING_INTERNAL, NULL);
        if (mem_size > 1024)
                mem_mapping_add(&ram_high_mapping, 0x100000, (mem_size - 1024) * 1024, mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram + 0x100000, MEM_MAPPING_INTERNAL, NULL);
	if (mem_size > 768)
        	mem_mapping_add(&ram_mid_mapping,   0xc0000, 0x40000, mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram + 0xc0000,  MEM_MAPPING_INTERNAL, NULL);
 
        if (romset == ROM_IBMPS1_2011)
                mem_mapping_add(&romext_mapping,  0xc8000, 0x08000, mem_read_romext, mem_read_romextw, mem_read_romextl, NULL, NULL, NULL,   romext, 0, NULL);

        mem_a20_key = 2;
	mem_a20_alt = 0;
        mem_a20_recalc();
}

void mem_reset_page_blocks()
{
        int c;
        
        for (c = 0; c < ((mem_size * 1024) >> 12); c++)
        {
                pages[c].write_b = mem_write_ramb_page;
                pages[c].write_w = mem_write_ramw_page;
                pages[c].write_l = mem_write_raml_page;
                pages[c].block[0] = pages[c].block[1] = pages[c].block[2] = pages[c].block[3] = NULL;
                pages[c].block_2[0] = pages[c].block_2[1] = pages[c].block_2[2] = pages[c].block_2[3] = NULL;
        }
}

static int port_92_reg = 0;

void mem_a20_recalc()
{
        int state = mem_a20_key | mem_a20_alt;
        if (state && !mem_a20_state)
        {
                rammask = 0xffffffff;
                flushmmucache();
        }
        else if (!state && mem_a20_state)
        {
                rammask = 0xffefffff;
                flushmmucache();
        }
        mem_a20_state = state;
}

static uint8_t port_92_read(uint16_t port, void *priv)
{
	return port_92_reg;
}

static void port_92_write(uint16_t port, uint8_t val, void *priv)
{
	if ((mem_a20_alt ^ val) & 2)
	{
		mem_a20_alt = val & 2;
		mem_a20_recalc();
	}

	if ((~port_92_reg & val) & 1)
	{
		softresetx86();
		cpu_set_edx();
	}

	port_92_reg = val;
}

void port_92_clear_reset()
{
	port_92_reg &= 2;
}

void port_92_add()
{
	io_sethandler(0x0092, 0x0001, port_92_read, NULL, NULL, port_92_write, NULL, NULL, NULL);
}

void port_92_remove()
{
	io_removehandler(0x0092, 0x0001, port_92_read, NULL, NULL, port_92_write, NULL, NULL, NULL);
}

void port_92_reset()
{
	port_92_reg = 0;
	mem_a20_alt = 0;
	mem_a20_recalc();
	flushmmucache();
}

uint32_t get_phys_virt,get_phys_phys;
