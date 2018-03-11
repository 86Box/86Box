/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "86box.h"
#include "cpu/cpu.h"
#include "cpu/x86_ops.h"
#include "cpu/x86.h"
#include "machine/machine.h"
#include "machine/m_xt_xi8088.h"
#include "config.h"
#include "io.h"
#include "mem.h"
#include "rom.h"

#ifdef USE_DYNAREC
# include "cpu/codegen.h"
#else
#define PAGE_MASK_INDEX_MASK 3
#define PAGE_MASK_INDEX_SHIFT 10
#define PAGE_MASK_MASK 63
#define PAGE_MASK_SHIFT 4
#endif


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
static mem_mapping_t ram_remapped_mapping;
static mem_mapping_t ram_split_mapping;
mem_mapping_t ram_low_mapping;
mem_mapping_t ram_high_mapping;
mem_mapping_t ram_mid_mapping;
mem_mapping_t bios_mapping[8];
mem_mapping_t bios_high_mapping[8];
mem_mapping_t romext_mapping;

page_t *pages;
page_t **page_lookup;

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

int shadowbios = 0,
    shadowbios_write;

int mem_a20_state;

unsigned char isram[0x10000];

static uint8_t ff_array[0x1000];

int mem_size;
uint32_t biosmask;
int readlnum=0,writelnum=0;
int cachesize=256;

uint8_t *rom;
uint8_t romext[32768];

uint32_t ram_mapped_addr[64];

int split_mapping_enabled = 0;


void resetreadlookup(void)
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

void flushmmucache(void)
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
        
#ifdef USE_DYNAREC
        codegen_flush();
#endif
}

void flushmmucache_nopc(void)
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

void flushmmucache_cr3(void)
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

#define rammap(x)	((uint32_t *)(_mem_exec[(x) >> 14]))[((x) >> 2) & 0xfff]

uint32_t mmutranslatereal(uint32_t addr, int rw)
{
        uint32_t addr2;
        uint32_t temp,temp2,temp3;
        
	if (cpu_state.abrt) 
		return -1;
        addr2 = ((cr3 & ~0xfff) + ((addr >> 20) & 0xffc));
        temp=temp2=rammap(addr2);
        if (!(temp&1)) {
                cr2=addr;
                temp&=1;
                if (CPL==3) temp|=4;
                if (rw) temp|=2;
                cpu_state.abrt = ABRT_PF;
                abrt_error = temp;
                return -1;
        }
        
        if ((temp & 0x80) && (cr4 & CR4_PSE)) {
                /*4MB page*/
                if ((CPL == 3 && !(temp & 4) && !cpl_override) || (rw && !(temp & 2) && ((CPL == 3 && !cpl_override) || cr0 & WP_FLAG))) {
                        cr2 = addr;
                        temp &= 1;
                        if (CPL == 3)
                                temp |= 4;
                        if (rw)
                                temp |= 2;
                        cpu_state.abrt = ABRT_PF;
                        abrt_error = temp;

                        return -1;
                }

                mmu_perm = temp & 4;
                rammap(addr2) |= 0x20;

                return (temp & ~0x3fffff) + (addr & 0x3fffff);
        }
        
        temp=rammap((temp&~0xFFF)+((addr>>10)&0xFFC));
        temp3=temp&temp2;
        if (!(temp&1) || (CPL==3 && !(temp3&4) && !cpl_override) || (rw && !(temp3&2) && ((CPL == 3 && !cpl_override) || cr0&WP_FLAG))) {
                cr2=addr;
                temp&=1;
                if (CPL==3) temp|=4;
                if (rw) temp|=2;
                cpu_state.abrt = ABRT_PF;
                abrt_error = temp;
                return -1;
        }
        mmu_perm=temp&4;
        rammap(addr2)|=0x20;
        rammap((temp2&~0xFFF)+((addr>>10)&0xFFC))|=(rw?0x60:0x20);

        return (temp&~0xFFF)+(addr&0xFFF);
}

uint32_t mmutranslate_noabrt(uint32_t addr, int rw)
{
        uint32_t addr2;
        uint32_t temp,temp2,temp3;
        
        if (cpu_state.abrt) 
                return -1;

        addr2 = ((cr3 & ~0xfff) + ((addr >> 20) & 0xffc));
        temp=temp2=rammap(addr2);

        if (!(temp&1))
                return -1;

        if ((temp & 0x80) && (cr4 & CR4_PSE)) {
                /*4MB page*/
                if ((CPL == 3 && !(temp & 4) && !cpl_override) || (rw && !(temp & 2) && (CPL == 3 || cr0 & WP_FLAG)))
                        return -1;

                return (temp & ~0x3fffff) + (addr & 0x3fffff);
        }

        temp=rammap((temp&~0xFFF)+((addr>>10)&0xFFC));
        temp3=temp&temp2;

        if (!(temp&1) || (CPL==3 && !(temp3&4) && !cpl_override) || (rw && !(temp3&2) && (CPL==3 || cr0&WP_FLAG)))
                return -1;

        return (temp&~0xFFF)+(addr&0xFFF);
}

void mmu_invalidate(uint32_t addr)
{
        flushmmucache_cr3();
}

uint8_t mem_addr_range_match(uint32_t addr, uint32_t start, uint32_t len)
{
	if (addr < start)
		return 0;
	else if (addr >= (start + len))
		return 0;
	else
		return 1;
}

uint32_t mem_addr_translate(uint32_t addr, uint32_t chunk_start, uint32_t len)
{
	uint32_t mask = len - 1;
	return chunk_start + (addr & mask);
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

#ifdef USE_DYNAREC
        if (pages[phys >> 12].block[0] || pages[phys >> 12].block[1] || pages[phys >> 12].block[2] || pages[phys >> 12].block[3] || (phys & ~0xfff) == recomp_page)
#else
        if (pages[phys >> 12].block[0] || pages[phys >> 12].block[1] || pages[phys >> 12].block[2] || pages[phys >> 12].block[3])
#endif
                page_lookup[virt >> 12] = &pages[phys >> 12];
        else {
                writelookup2[virt>>12] = (uintptr_t)&ram[(uintptr_t)(phys & ~0xFFF) - (uintptr_t)(virt & ~0xfff)];
	}
        writelookupp[writelnext] = mmu_perm;
        writelookup[writelnext++] = virt >> 12;
        writelnext &= (cachesize - 1);

        cycles -= 9;
}

uint8_t *getpccache(uint32_t a)
{
        uint32_t a2;

	a2=a;

        if (a2 < 0x100000 && ram_mapped_addr[a2 >> 14])
        {
                a = (ram_mapped_addr[a2 >> 14] & MEM_MAP_TO_SHADOW_RAM_MASK) ? a2 : (ram_mapped_addr[a2 >> 14] & ~0x3FFF) + (a2 & 0x3FFF);
                return &ram[(uintptr_t)(a & 0xFFFFF000) - (uintptr_t)(a2 & ~0xFFF)];
        }

	a2 = a;

        if (cr0>>31)
        {
		pctrans=1;
                a = mmutranslate_read(a);
                pctrans=0;

                if (a==0xFFFFFFFF) return ram;
        }
        a&=rammask;

        if (_mem_exec[a >> 14])
        {
                if (_mem_mapping_r[a >> 14]->flags & MEM_MAPPING_ROM)
                        cpu_prefetch_cycles = cpu_rom_prefetch_cycles;
                else
                        cpu_prefetch_cycles = cpu_mem_prefetch_cycles;
			
			
                return &_mem_exec[a >> 14][(uintptr_t)(a & 0x3000) - (uintptr_t)(a2 & ~0xFFF)];
        }

        pclog("Bad getpccache %08X\n", a);
        return &ff_array[0-(uintptr_t)(a2 & ~0xFFF)];
}

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

        if (_mem_read_b[addr >> 14]) {
		return _mem_read_b[addr >> 14](addr, _mem_priv_r[addr >> 14]);
	}
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

/* Version of mem_readby_phys that doesn't go through the CPU paging mechanism. */
uint8_t mem_readb_phys_dma(uint32_t addr)
{
        /* mem_logical_addr = 0xffffffff; */

	if (_mem_exec[addr >> 14])
		return _mem_exec[addr >> 14][addr & 0x3fff];
	else if (_mem_read_b[addr >> 14])
               	return _mem_read_b[addr >> 14](addr, _mem_priv_r[addr >> 14]);
	else
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

/* Version of mem_readby_phys that doesn't go through the CPU paging mechanism. */
void mem_writeb_phys_dma(uint32_t addr, uint8_t val)
{
        /* mem_logical_addr = 0xffffffff; */

	if (_mem_exec[addr >> 14])
		_mem_exec[addr >> 14][addr & 0x3fff] = val;
	else if (_mem_write_b[addr >> 14])
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
#ifdef USE_DYNAREC
        if (val != p->mem[addr & 0xfff] || codegen_in_recompile)
#else
        if (val != p->mem[addr & 0xfff])
#endif
        {
                uint64_t mask = (uint64_t)1 << ((addr >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK);
                p->dirty_mask[(addr >> PAGE_MASK_INDEX_SHIFT) & PAGE_MASK_INDEX_MASK] |= mask;
                p->mem[addr & 0xfff] = val;
        }
}
void mem_write_ramw_page(uint32_t addr, uint16_t val, page_t *p)
{
#ifdef USE_DYNAREC
        if (val != *(uint16_t *)&p->mem[addr & 0xfff] || codegen_in_recompile)
#else
        if (val != *(uint16_t *)&p->mem[addr & 0xfff])
#endif
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
#ifdef USE_DYNAREC
        if (val != *(uint32_t *)&p->mem[addr & 0xfff] || codegen_in_recompile)
#else
        if (val != *(uint32_t *)&p->mem[addr & 0xfff])
#endif
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


uint8_t mem_read_bios(uint32_t addr, void *priv)
{
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
	mapping->prev = dest;
        
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
        if (AT || (romset == ROM_XI8088 && xi8088_bios_128kb()))
        {
                mem_mapping_add(&bios_mapping[0], 0xe0000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom,                        MEM_MAPPING_EXTERNAL|MEM_MAPPING_ROM, 0);
                mem_mapping_add(&bios_mapping[1], 0xe4000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x4000  & biosmask), MEM_MAPPING_EXTERNAL|MEM_MAPPING_ROM, 0);
                mem_mapping_add(&bios_mapping[2], 0xe8000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x8000  & biosmask), MEM_MAPPING_EXTERNAL|MEM_MAPPING_ROM, 0);
                mem_mapping_add(&bios_mapping[3], 0xec000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0xc000  & biosmask), MEM_MAPPING_EXTERNAL|MEM_MAPPING_ROM, 0);
        }
        mem_mapping_add(&bios_mapping[4], 0xf0000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x10000 & biosmask), MEM_MAPPING_EXTERNAL|MEM_MAPPING_ROM, 0);
        mem_mapping_add(&bios_mapping[5], 0xf4000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x14000 & biosmask), MEM_MAPPING_EXTERNAL|MEM_MAPPING_ROM, 0);
        mem_mapping_add(&bios_mapping[6], 0xf8000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x18000 & biosmask), MEM_MAPPING_EXTERNAL|MEM_MAPPING_ROM, 0);
        mem_mapping_add(&bios_mapping[7], 0xfc000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x1c000 & biosmask), MEM_MAPPING_EXTERNAL|MEM_MAPPING_ROM, 0);

        mem_mapping_add(&bios_high_mapping[0], (AT && cpu_16bitbus) ? 0xfe0000 : 0xfffe0000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom,                        MEM_MAPPING_ROM, 0);
        mem_mapping_add(&bios_high_mapping[1], (AT && cpu_16bitbus) ? 0xfe4000 : 0xfffe4000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x4000  & biosmask), MEM_MAPPING_ROM, 0);
        mem_mapping_add(&bios_high_mapping[2], (AT && cpu_16bitbus) ? 0xfe8000 : 0xfffe8000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x8000  & biosmask), MEM_MAPPING_ROM, 0);
        mem_mapping_add(&bios_high_mapping[3], (AT && cpu_16bitbus) ? 0xfec000 : 0xfffec000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0xc000  & biosmask), MEM_MAPPING_ROM, 0);
        mem_mapping_add(&bios_high_mapping[4], (AT && cpu_16bitbus) ? 0xff0000 : 0xffff0000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x10000 & biosmask), MEM_MAPPING_ROM, 0);
        mem_mapping_add(&bios_high_mapping[5], (AT && cpu_16bitbus) ? 0xff4000 : 0xffff4000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x14000 & biosmask), MEM_MAPPING_ROM, 0);
        mem_mapping_add(&bios_high_mapping[6], (AT && cpu_16bitbus) ? 0xff8000 : 0xffff8000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x18000 & biosmask), MEM_MAPPING_ROM, 0);
        mem_mapping_add(&bios_high_mapping[7], (AT && cpu_16bitbus) ? 0xffc000 : 0xffffc000, 0x04000, mem_read_bios,   mem_read_biosw,   mem_read_biosl,   mem_write_null, mem_write_nullw, mem_write_nulll, rom + (0x1c000 & biosmask), MEM_MAPPING_ROM, 0);
}

int mem_a20_key = 0, mem_a20_alt = 0;
int mem_a20_state = 0;

void mem_a20_init(void)
{
	if (AT) {
                rammask = cpu_16bitbus ? 0xefffff : 0xffefffff;
		flushmmucache();
        	mem_a20_state = mem_a20_key | mem_a20_alt;
	} else {
		rammask = 0xfffff;
		flushmmucache();
        	mem_a20_key = mem_a20_alt = mem_a20_state = 0;
	}
}


void mem_init(void)
{
        int c;

	split_mapping_enabled = 0;

	/* Always allocate the full 16 MB memory space if memory size is smaller,
	   we'll need this for stupid things like the PS/2 split mapping. */
	if (mem_size < 16384) {
        	ram = malloc(16384 * 1024);
        	memset(ram, 0, 16384 * 1024);
	} else {
        	ram = malloc((mem_size + 384) * 1024);	/* 386 extra kB for top remapping */
        	memset(ram, 0, (mem_size + 384) * 1024);
	}

        readlookup2  = malloc(1024 * 1024 * sizeof(uintptr_t));
        writelookup2 = malloc(1024 * 1024 * sizeof(uintptr_t));
	rom = NULL;
        biosmask = 0xffff;
        pages = malloc((1 << 20) * sizeof(page_t));
        page_lookup = malloc((1 << 20) * sizeof(page_t *));

        memset(pages, 0, (1 << 20) * sizeof(page_t));
        
        memset(page_lookup, 0, (1 << 20) * sizeof(page_t *));

	memset(ram_mapped_addr, 0, 64 * sizeof(uint32_t));
        
        for (c = 0; c < (1 << 20); c++)
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
                if ((c >= 0xa && c <= 0xf) || (cpu_16bitbus && c >= 0xfe && c <= 0xff))
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

        mem_mapping_add(&ram_low_mapping, 0x00000, (mem_size > 640) ? 0xa0000 : mem_size * 1024, mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram,  MEM_MAPPING_INTERNAL, NULL);
        if (mem_size > 1024) {
                if(cpu_16bitbus && mem_size > 16256)
                {
                        mem_set_mem_state(0x100000, (16256 - 1024) * 1024, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                        mem_mapping_add(&ram_high_mapping, 0x100000, ((16256 - 1024) * 1024), mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram + 0x100000, MEM_MAPPING_INTERNAL, NULL);
                }
                else
                {
                        mem_set_mem_state(0x100000, (mem_size - 1024) * 1024, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                        mem_mapping_add(&ram_high_mapping, 0x100000, ((mem_size - 1024) * 1024), mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram + 0x100000, MEM_MAPPING_INTERNAL, NULL);
                }
	}
	if (mem_size > 768)
		mem_mapping_add(&ram_mid_mapping,   0xc0000, 0x40000, mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram + 0xc0000,  MEM_MAPPING_INTERNAL, NULL);
 
        if (romset == ROM_IBMPS1_2011)
                mem_mapping_add(&romext_mapping,  0xc8000, 0x08000, mem_read_romext, mem_read_romextw, mem_read_romextl, NULL, NULL, NULL,   romext, 0, NULL);

	mem_mapping_add(&ram_remapped_mapping,  mem_size * 1024, 384 * 1024, mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram + (1 << 20),  MEM_MAPPING_INTERNAL, NULL);
	mem_mapping_disable(&ram_remapped_mapping);

	mem_mapping_add(&ram_split_mapping,  mem_size * 1024, 384 * 1024, mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram + (1 << 20),  MEM_MAPPING_INTERNAL, NULL);
	mem_mapping_disable(&ram_split_mapping);

        mem_a20_init();
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

       		for (c = (start / 64); c < ((start + size - 1) / 64); c++)
	        {
               		isram[c] = 1;
	        }

                mem_set_mem_state(start * 1024, size * 1024, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
		mem_mapping_set_addr(&ram_remapped_mapping, start * 1024, size * 1024);
		mem_mapping_set_exec(&ram_split_mapping, ram + (start * 1024));

		flushmmucache();
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

void mem_split_enable(int max_size, uint32_t addr)
{
	int c;

	uint8_t *mem_split_buffer = &ram[0x80000];

	if (split_mapping_enabled)
		return;

#if 0
	pclog("Split mapping enable at %08X\n", addr);
#endif

	mem_set_mem_state(addr, max_size * 1024, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
	mem_mapping_set_addr(&ram_split_mapping, addr, max_size * 1024);
	mem_mapping_set_exec(&ram_split_mapping, &ram[addr]);

	if (max_size == 384)
		memcpy(&ram[addr], mem_split_buffer, max_size);
	else
		memcpy(&ram[addr], &mem_split_buffer[128 * 1024], max_size);

	for (c = ((addr / 1024) / 64); c < (((addr / 1024) + max_size - 1) / 64); c++)
        {
		isram[c] = 1;
        }

	flushmmucache();

	split_mapping_enabled = 1;
}

void mem_split_disable(int max_size, uint32_t addr)
{
	int c;

	uint8_t *mem_split_buffer = &ram[0x80000];

	if (!split_mapping_enabled)
		return;

#if 0
	pclog("Split mapping disable at %08X\n", addr);
#endif

	if (max_size == 384)
		memcpy(mem_split_buffer, &ram[addr], max_size);
	else
		memcpy(&mem_split_buffer[128 * 1024], &ram[addr], max_size);

	mem_mapping_disable(&ram_split_mapping);
	mem_set_mem_state(addr, max_size * 1024, 0);
	mem_mapping_set_exec(&ram_split_mapping, NULL);

	for (c = ((addr / 1024) / 64); c < (((addr / 1024) + max_size - 1) / 64); c++)
        {
		isram[c] = 0;
        }

	flushmmucache();

	split_mapping_enabled = 0;
}

void mem_resize()
{
        int c;

	split_mapping_enabled = 0;
        
        free(ram);

	/* Always allocate the full 16 MB memory space if memory size is smaller,
	   we'll need this for stupid things like the PS/2 split mapping. */
	if (mem_size < 16384) {
        	ram = malloc(16384 * 1024);
        	memset(ram, 0, 16384 * 1024);
	} else {
        	ram = malloc((mem_size + 384) * 1024);	/* 386 extra kB for top remapping */
        	memset(ram, 0, (mem_size + 384) * 1024);
	}
        
        memset(pages, 0, (1 << 20) * sizeof(page_t));
        for (c = 0; c < (1 << 20); c++)
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
                if ((c >= 0xa && c <= 0xf) || (cpu_16bitbus && c >= 0xfe && c <= 0xff))
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
        
        mem_mapping_add(&ram_low_mapping, 0x00000, (mem_size > 640) ? 0xa0000 : mem_size * 1024, mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram,  MEM_MAPPING_INTERNAL, NULL);
        if (mem_size > 1024) {
                if(cpu_16bitbus && mem_size > 16256)
                {
                        mem_set_mem_state(0x100000, (16256 - 1024) * 1024, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                        mem_mapping_add(&ram_high_mapping, 0x100000, ((16256 - 1024) * 1024), mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram + 0x100000, MEM_MAPPING_INTERNAL, NULL);
                }
                else
                {
                        mem_set_mem_state(0x100000, (mem_size - 1024) * 1024, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                        mem_mapping_add(&ram_high_mapping, 0x100000, ((mem_size - 1024) * 1024), mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram + 0x100000, MEM_MAPPING_INTERNAL, NULL);
                }
	}
	if (mem_size > 768)
        	mem_mapping_add(&ram_mid_mapping,   0xc0000, 0x40000, mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram + 0xc0000,  MEM_MAPPING_INTERNAL, NULL);
 
        if (romset == ROM_IBMPS1_2011)
                mem_mapping_add(&romext_mapping,  0xc8000, 0x08000, mem_read_romext, mem_read_romextw, mem_read_romextl, NULL, NULL, NULL,   romext, 0, NULL);

	mem_mapping_add(&ram_remapped_mapping,  mem_size * 1024, 384 * 1024, mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram + (1 << 20),  MEM_MAPPING_INTERNAL, NULL);
	mem_mapping_disable(&ram_remapped_mapping);

	mem_mapping_add(&ram_split_mapping,  mem_size * 1024, 384 * 1024, mem_read_ram,    mem_read_ramw,    mem_read_raml,    mem_write_ram, mem_write_ramw, mem_write_raml,   ram + (1 << 20),  MEM_MAPPING_INTERNAL, NULL);
	mem_mapping_disable(&ram_split_mapping);

        mem_a20_init();
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

void mem_a20_recalc(void)
{
	int state;

	if (!AT) {
		rammask = 0xfffff;
		flushmmucache();
        	mem_a20_key = mem_a20_alt = mem_a20_state = 0;
		return;
	}

        state = mem_a20_key | mem_a20_alt;
        if (state && !mem_a20_state)
        {
                rammask = (AT && cpu_16bitbus) ? 0xffffff : 0xffffffff;
                flushmmucache();
        }
        else if (!state && mem_a20_state)
        {
                rammask = (AT && cpu_16bitbus) ? 0xefffff : 0xffefffff;
		flushmmucache();
        }
        mem_a20_state = state;
}


uint8_t port_92_read(uint16_t port, void *priv)
{
	return port_92_reg;
}


void port_92_write(uint16_t port, uint8_t val, void *priv)
{
	if ((mem_a20_alt ^ val) & 2)
	{
		mem_a20_alt = (val & 2);
		mem_a20_recalc();
	}

	if ((~port_92_reg & val) & 1)
	{
		softresetx86();
		cpu_set_edx();
	}

	port_92_reg = val;
}


void port_92_clear_reset(void)
{
	port_92_reg &= 2;
}

void port_92_add(void)
{
	io_sethandler(0x0092, 0x0001, port_92_read, NULL, NULL, port_92_write, NULL, NULL, NULL);
}

void port_92_remove(void)
{
	io_removehandler(0x0092, 0x0001, port_92_read, NULL, NULL, port_92_write, NULL, NULL, NULL);
}

void port_92_reset(void)
{
	port_92_reg = 0;
	mem_a20_alt = 0;
	mem_a20_recalc();
	flushmmucache();
}

uint32_t get_phys_virt,get_phys_phys;
