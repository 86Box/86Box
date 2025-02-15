/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Memory handling and MMU.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2017-2020 Fred N. van Kempen.
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
#include <86box/version.h>
#include "cpu.h"
#include "x86_ops.h"
#include "x86.h"
#include "x86seg_common.h"
#include <86box/machine.h>
#include <86box/m_xt_xi8088.h>
#include <86box/config.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/plat.h>
#include <86box/rom.h>
#include <86box/gdbstub.h>
#ifdef USE_DYNAREC
#    include "codegen_public.h"
#else
#    ifdef USE_NEW_DYNAREC
#        define PAGE_MASK_SHIFT 6
#    else
#        define PAGE_MASK_INDEX_MASK  3
#        define PAGE_MASK_INDEX_SHIFT 10
#        define PAGE_MASK_SHIFT       4
#    endif
#    define PAGE_MASK_MASK 63
#endif
#if (!defined(USE_DYNAREC) && defined(USE_NEW_DYNAREC))
#    define BLOCK_PC_INVALID 0xffffffff
#    define BLOCK_INVALID    0
#endif

mem_mapping_t ram_low_mapping;       /* 0..640K mapping */
mem_mapping_t ram_mid_mapping;       /* 640..1024K mapping */
mem_mapping_t ram_mid_mapping2;      /* 640..1024K mapping, second part, for SiS 471 in relocate mode  */
mem_mapping_t ram_remapped_mapping;  /* 640..1024K mapping */
mem_mapping_t ram_remapped_mapping2; /* 640..1024K second mapping, for SiS 471 mode */
mem_mapping_t ram_high_mapping;      /* 1024K+ mapping */
mem_mapping_t ram_2gb_mapping;       /* 1024M+ mapping */
mem_mapping_t ram_split_mapping;
mem_mapping_t bios_mapping;
mem_mapping_t bios_high_mapping;

page_t  *pages;       /* RAM page table */
page_t **page_lookup; /* pagetable lookup */
uint32_t pages_sz;    /* #pages in table */

uint8_t *ram;  /* the virtual RAM */
uint8_t *ram2; /* the virtual RAM */
uint8_t  page_ff[4096];
uint32_t rammask;
uint32_t addr_space_size;

uint8_t *rom; /* the virtual ROM */
uint32_t biosmask;
uint32_t biosaddr;

uint32_t pccache;
uint8_t *pccache2;

int        readlnext;
int        readlookup[256];
uintptr_t *readlookup2;
uintptr_t  old_rl2;
uint8_t    uncached = 0;
int        writelnext;
int        writelookup[256];
uintptr_t *writelookup2;

uint32_t mem_logical_addr;

int shadowbios = 0;
int shadowbios_write;
int readlnum  = 0;
int writelnum = 0;
int cachesize = 256;

uint32_t get_phys_virt;
uint32_t get_phys_phys;

int mem_a20_key   = 0;
int mem_a20_alt   = 0;
int mem_a20_state = 0;

int mmuflush = 0;
int mmu_perm = 4;

#ifdef USE_NEW_DYNAREC
uint64_t *byte_dirty_mask;
uint64_t *byte_code_present_mask;

uint32_t purgable_page_list_head = 0;
int      purgeable_page_count    = 0;
#endif

uint8_t high_page = 0; /* if a high (> 4 gb) page was detected */

mem_mapping_t        *read_mapping[MEM_MAPPINGS_NO];
mem_mapping_t        *write_mapping[MEM_MAPPINGS_NO];

uint8_t              *_mem_exec[MEM_MAPPINGS_NO];

/* FIXME: re-do this with a 'mem_ops' struct. */
static uint8_t       *page_lookupp; /* pagetable mmu_perm lookup */
static uint8_t       *readlookupp;
static uint8_t       *writelookupp;
static mem_mapping_t *base_mapping;
static mem_mapping_t *last_mapping;
static mem_mapping_t *read_mapping_bus[MEM_MAPPINGS_NO];
static mem_mapping_t *write_mapping_bus[MEM_MAPPINGS_NO];
static uint8_t       _mem_wp[MEM_MAPPINGS_NO];
static uint8_t       _mem_wp_bus[MEM_MAPPINGS_NO];
static uint8_t        ff_pccache[4] = { 0xff, 0xff, 0xff, 0xff };
static mem_state_t    _mem_state[MEM_MAPPINGS_NO];
static uint32_t       remap_start_addr;
static uint32_t       remap_start_addr2;
#if (!(defined __amd64__ || defined _M_X64 || defined __aarch64__ || defined _M_ARM64))
static size_t ram_size = 0;
static size_t ram2_size = 0;
#else
static size_t ram_size = 0;
#endif

#ifdef ENABLE_MEM_LOG
int mem_do_log = ENABLE_MEM_LOG;

static void
mem_log(const char *fmt, ...)
{
    va_list ap;

    if (mem_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define mem_log(fmt, ...)
#endif

int
mem_addr_is_ram(uint32_t addr)
{
    const mem_mapping_t *mapping = read_mapping[addr >> MEM_GRANULARITY_BITS];

    return (mapping == &ram_low_mapping) || (mapping == &ram_high_mapping) || (mapping == &ram_mid_mapping) ||
           (mapping == &ram_mid_mapping2) || (mapping == &ram_remapped_mapping);
}

void
resetreadlookup(void)
{
    /* Initialize the page lookup table. */
    memset(page_lookup, 0x00, (1 << 20) * sizeof(page_t *));

    /* Initialize the tables for lower (<= 1024K) RAM. */
    for (uint16_t c = 0; c < 256; c++) {
        readlookup[c]  = 0xffffffff;
        writelookup[c] = 0xffffffff;
    }

    /* Initialize the tables for high (> 1024K) RAM. */
    memset(readlookup2, 0xff, (1 << 20) * sizeof(uintptr_t));
    memset(readlookupp, 0x04, (1 << 20) * sizeof(uint8_t));

    memset(writelookup2, 0xff, (1 << 20) * sizeof(uintptr_t));
    memset(writelookupp, 0x04, (1 << 20) * sizeof(uint8_t));

    readlnext  = 0;
    writelnext = 0;
    pccache    = 0xffffffff;
    high_page  = 0;
}

void
flushmmucache(void)
{
    for (uint16_t c = 0; c < 256; c++) {
        if (readlookup[c] != (int) 0xffffffff) {
            readlookup2[readlookup[c]] = LOOKUP_INV;
            readlookupp[readlookup[c]] = 4;
            readlookup[c]              = 0xffffffff;
        }
        if (writelookup[c] != (int) 0xffffffff) {
            page_lookup[writelookup[c]]  = NULL;
            page_lookupp[writelookup[c]] = 4;
            writelookup2[writelookup[c]] = LOOKUP_INV;
            writelookupp[writelookup[c]] = 4;
            writelookup[c]               = 0xffffffff;
        }
    }
    mmuflush++;

    pccache  = (uint32_t) 0xffffffff;
    pccache2 = (uint8_t *) 0xffffffff;

#ifdef USE_DYNAREC
    codegen_flush();
#endif
}

void
flushmmucache_pc(void)
{
    mmuflush++;

    pccache  = (uint32_t) 0xffffffff;
    pccache2 = (uint8_t *) 0xffffffff;

#ifdef USE_DYNAREC
    codegen_flush();
#endif
}

void
flushmmucache_nopc(void)
{
    for (uint16_t c = 0; c < 256; c++) {
        if (readlookup[c] != (int) 0xffffffff) {
            readlookup2[readlookup[c]] = LOOKUP_INV;
            readlookupp[readlookup[c]] = 4;
            readlookup[c]              = 0xffffffff;
        }
        if (writelookup[c] != (int) 0xffffffff) {
            page_lookup[writelookup[c]]  = NULL;
            page_lookupp[writelookup[c]] = 4;
            writelookup2[writelookup[c]] = LOOKUP_INV;
            writelookupp[writelookup[c]] = 4;
            writelookup[c]               = 0xffffffff;
        }
    }
}

void
mem_flush_write_page(uint32_t addr, uint32_t virt)
{
    const page_t *page_target = &pages[addr >> 12];
#if (!(defined __amd64__ || defined _M_X64 || defined __aarch64__ || defined _M_ARM64))
    uint32_t a;
#endif

    for (uint16_t c = 0; c < 256; c++) {
        if (writelookup[c] != (int) 0xffffffff) {
#if (defined __amd64__ || defined _M_X64 || defined __aarch64__ || defined _M_ARM64)
            uintptr_t target = (uintptr_t) &ram[(uintptr_t) (addr & ~0xfff) - (virt & ~0xfff)];
#else
            a = (uintptr_t) (addr & ~0xfff) - (virt & ~0xfff);
            uintptr_t target;

            if ((addr & ~0xfff) >= (1 << 30))
                target = (uintptr_t) &ram2[a - (1 << 30)];
            else
                target = (uintptr_t) &ram[a];
#endif

            if (writelookup2[writelookup[c]] == target || page_lookup[writelookup[c]] == page_target) {
                writelookup2[writelookup[c]] = LOOKUP_INV;
                page_lookup[writelookup[c]]  = NULL;
                writelookup[c]               = 0xffffffff;
            }
        }
    }
}

#define mmutranslate_read(addr)  mmutranslatereal(addr, 0)
#define mmutranslate_write(addr) mmutranslatereal(addr, 1)
#define rammap(x)                ((uint32_t *) (_mem_exec[(x) >> MEM_GRANULARITY_BITS]))[((x) >> 2) & MEM_GRANULARITY_QMASK]
#define rammap64(x)              ((uint64_t *) (_mem_exec[(x) >> MEM_GRANULARITY_BITS]))[((x) >> 3) & MEM_GRANULARITY_PMASK]

static __inline uint64_t
mmutranslatereal_normal(uint32_t addr, int rw)
{
    uint32_t temp;
    uint32_t temp2;
    uint32_t temp3;
    uint32_t addr2;

    if (cpu_state.abrt)
        return 0xffffffffffffffffULL;

    addr2 = ((cr3 & ~0xfff) + ((addr >> 20) & 0xffc));
    temp = temp2 = rammap(addr2);
    if (!(temp & 1)) {
        cr2 = addr;
        temp &= 1;
        if (CPL == 3)
            temp |= 4;
        if (rw)
            temp |= 2;
        cpu_state.abrt = ABRT_PF;
        abrt_error     = temp;
        return 0xffffffffffffffffULL;
    }

    if ((temp & 0x80) && (cr4 & CR4_PSE)) {
        /*4MB page*/
        if (((CPL == 3) && !(temp & 4) && !cpl_override) || (rw && !cpl_override && !(temp & 2) && (((CPL == 3) && !cpl_override) || ((is486 || isibm486) && (cr0 & WP_FLAG))))) {
            cr2 = addr;
            temp &= 1;
            if (CPL == 3)
                temp |= 4;
            if (rw)
                temp |= 2;
            cpu_state.abrt = ABRT_PF;
            abrt_error     = temp;

            return 0xffffffffffffffffULL;
        }

        mmu_perm = temp & 4;
        rammap(addr2) |= (rw ? 0x60 : 0x20);

        uint64_t page = temp & ~0x3fffff;
        if (cpu_features & CPU_FEATURE_PSE36)
            page |= (uint64_t) (temp & 0x1e000) << 19;
        return page + (addr & 0x3fffff);
    }

    temp  = rammap((temp & ~0xfff) + ((addr >> 10) & 0xffc));
    temp3 = temp & temp2;
    if (!(temp & 1) || ((CPL == 3) && !(temp3 & 4) && !cpl_override) || (rw && !cpl_override && !(temp3 & 2) && (((CPL == 3) && !cpl_override) || ((is486 || isibm486) && (cr0 & WP_FLAG))))) {
        cr2 = addr;
        temp &= 1;
        if (CPL == 3)
            temp |= 4;
        if (rw)
            temp |= 2;
        cpu_state.abrt = ABRT_PF;
        abrt_error     = temp;
        return 0xffffffffffffffffULL;
    }

    mmu_perm = temp & 4;
    rammap(addr2) |= 0x20;
    rammap((temp2 & ~0xfff) + ((addr >> 10) & 0xffc)) |= (rw ? 0x60 : 0x20);

    return (uint64_t) ((temp & ~0xfff) + (addr & 0xfff));
}

static __inline uint64_t
mmutranslatereal_pae(uint32_t addr, int rw)
{
    uint64_t temp;
    uint64_t temp2;
    uint64_t temp3;
    uint64_t temp4;
    uint64_t addr2;
    uint64_t addr3;
    uint64_t addr4;

    if (cpu_state.abrt)
        return 0xffffffffffffffffULL;

    addr2 = (cr3 & ~0x1f) + ((addr >> 27) & 0x18);
    temp = temp2 = rammap64(addr2) & 0x000000ffffffffffULL;
    if (!(temp & 1)) {
        cr2 = addr;
        temp &= 1;
        if (CPL == 3)
            temp |= 4;
        if (rw)
            temp |= 2;
        cpu_state.abrt = ABRT_PF;
        abrt_error     = temp;
        return 0xffffffffffffffffULL;
    }

    addr3 = (temp & ~0xfffULL) + ((addr >> 18) & 0xff8);
    temp = temp4 = rammap64(addr3) & 0x000000ffffffffffULL;
    temp3        = temp & temp2;
    if (!(temp & 1)) {
        cr2 = addr;
        temp &= 1;
        if (CPL == 3)
            temp |= 4;
        if (rw)
            temp |= 2;
        cpu_state.abrt = ABRT_PF;
        abrt_error     = temp;
        return 0xffffffffffffffffULL;
    }

    if (temp & 0x80) {
        /*2MB page*/
        if (((CPL == 3) && !(temp & 4) && !cpl_override) || (rw && !cpl_override && !(temp & 2) && (((CPL == 3) && !cpl_override) || (cr0 & WP_FLAG)))) {
            cr2 = addr;
            temp &= 1;
            if (CPL == 3)
                temp |= 4;
            if (rw)
                temp |= 2;
            cpu_state.abrt = ABRT_PF;
            abrt_error     = temp;

            return 0xffffffffffffffffULL;
        }
        mmu_perm = temp & 4;
        rammap64(addr3) |= (rw ? 0x60 : 0x20);

        return ((temp & ~0x1fffffULL) + (addr & 0x1fffffULL)) & 0x000000ffffffffffULL;
    }

    addr4 = (temp & ~0xfffULL) + ((addr >> 9) & 0xff8);
    temp  = rammap64(addr4) & 0x000000ffffffffffULL;
    temp3 = temp & temp4;
    if (!(temp & 1) || ((CPL == 3) && !(temp3 & 4) && !cpl_override) || (rw && !cpl_override && !(temp3 & 2) && (((CPL == 3) && !cpl_override) || (cr0 & WP_FLAG)))) {
        cr2 = addr;
        temp &= 1;
        if (CPL == 3)
            temp |= 4;
        if (rw)
            temp |= 2;
        cpu_state.abrt = ABRT_PF;
        abrt_error     = temp;
        return 0xffffffffffffffffULL;
    }

    mmu_perm = temp & 4;
    rammap64(addr3) |= 0x20;
    rammap64(addr4) |= (rw ? 0x60 : 0x20);

    return ((temp & ~0xfffULL) + ((uint64_t) (addr & 0xfff))) & 0x000000ffffffffffULL;
}

uint64_t
mmutranslatereal(uint32_t addr, int rw)
{
    /* Fast path to return invalid without any call if an exception has occurred beforehand. */
    if (cpu_state.abrt)
        return 0xffffffffffffffffULL;

    if (cr4 & CR4_PAE)
        return mmutranslatereal_pae(addr, rw);
    else
        return mmutranslatereal_normal(addr, rw);
}

/* This is needed because the old recompiler calls this to check for page fault. */
uint32_t
mmutranslatereal32(uint32_t addr, int rw)
{
    /* Fast path to return invalid without any call if an exception has occurred beforehand. */
    if (cpu_state.abrt)
        return (uint32_t) 0xffffffffffffffffULL;

    return (uint32_t) mmutranslatereal(addr, rw);
}

static __inline uint64_t
mmutranslate_noabrt_normal(uint32_t addr, int rw)
{
    uint32_t temp;
    uint32_t temp2;
    uint32_t temp3;
    uint32_t addr2;

    if (cpu_state.abrt)
        return 0xffffffffffffffffULL;

    addr2 = ((cr3 & ~0xfff) + ((addr >> 20) & 0xffc));
    temp = temp2 = rammap(addr2);

    if (!(temp & 1))
        return 0xffffffffffffffffULL;

    if ((temp & 0x80) && (cr4 & CR4_PSE)) {
        /*4MB page*/
        if (((CPL == 3) && !(temp & 4) && !cpl_override) || (rw && !cpl_override && !(temp & 2) && ((CPL == 3) || (cr0 & WP_FLAG))))
            return 0xffffffffffffffffULL;

        uint64_t page = temp & ~0x3fffff;
        if (cpu_features & CPU_FEATURE_PSE36)
            page |= (uint64_t) (temp & 0x1e000) << 19;
        return page + (addr & 0x3fffff);
    }

    temp  = rammap((temp & ~0xfff) + ((addr >> 10) & 0xffc));
    temp3 = temp & temp2;

    if (!(temp & 1) || ((CPL == 3) && !(temp3 & 4) && !cpl_override) || (rw && !cpl_override && !(temp3 & 2) && ((CPL == 3) || (cr0 & WP_FLAG))))
        return 0xffffffffffffffffULL;

    return (uint64_t) ((temp & ~0xfff) + (addr & 0xfff));
}

static __inline uint64_t
mmutranslate_noabrt_pae(uint32_t addr, int rw)
{
    uint64_t temp;
    uint64_t temp2;
    uint64_t temp3;
    uint64_t temp4;
    uint64_t addr2;
    uint64_t addr3;
    uint64_t addr4;

    if (cpu_state.abrt)
        return 0xffffffffffffffffULL;

    addr2 = (cr3 & ~0x1f) + ((addr >> 27) & 0x18);
    temp = temp2 = rammap64(addr2) & 0x000000ffffffffffULL;

    if (!(temp & 1))
        return 0xffffffffffffffffULL;

    addr3 = (temp & ~0xfffULL) + ((addr >> 18) & 0xff8);
    temp = temp4 = rammap64(addr3) & 0x000000ffffffffffULL;
    temp3        = temp & temp2;

    if (!(temp & 1))
        return 0xffffffffffffffffULL;

    if (temp & 0x80) {
        /*2MB page*/
        if (((CPL == 3) && !(temp & 4) && !cpl_override) || (rw && !cpl_override && !(temp & 2) && ((CPL == 3) || (cr0 & WP_FLAG))))
            return 0xffffffffffffffffULL;

        return ((temp & ~0x1fffffULL) + (addr & 0x1fffff)) & 0x000000ffffffffffULL;
    }

    addr4 = (temp & ~0xfffULL) + ((addr >> 9) & 0xff8);
    temp  = rammap64(addr4) & 0x000000ffffffffffULL;

    temp3 = temp & temp4;

    if (!(temp & 1) || ((CPL == 3) && !(temp3 & 4) && !cpl_override) || (rw && !cpl_override && !(temp3 & 2) && ((CPL == 3) || (cr0 & WP_FLAG))))
        return 0xffffffffffffffffULL;

    return ((temp & ~0xfffULL) + ((uint64_t) (addr & 0xfff))) & 0x000000ffffffffffULL;
}

uint64_t
mmutranslate_noabrt(uint32_t addr, int rw)
{
    /* Fast path to return invalid without any call if an exception has occurred beforehand. */
    if (cpu_state.abrt)
        return 0xffffffffffffffffULL;

    if (cr4 & CR4_PAE)
        return mmutranslate_noabrt_pae(addr, rw);
    else
        return mmutranslate_noabrt_normal(addr, rw);
}

uint8_t
mem_addr_range_match(uint32_t addr, uint32_t start, uint32_t len)
{
    if (addr < start)
        return 0;
    else if (addr >= (start + len))
        return 0;
    else
        return 1;
}

uint32_t
mem_addr_translate(uint32_t addr, uint32_t chunk_start, uint32_t len)
{
    uint32_t mask = len - 1;

    return chunk_start + (addr & mask);
}

void
addreadlookup(uint32_t virt, uint32_t phys)
{
#if (!(defined __amd64__ || defined _M_X64 || defined __aarch64__ || defined _M_ARM64))
    uint32_t a;
#endif

    if (virt == 0xffffffff)
        return;

    if (readlookup2[virt >> 12] != (uintptr_t) LOOKUP_INV)
        return;

    if (readlookup[readlnext] != (int) 0xffffffff) {
        if ((readlookup[readlnext] == ((es + DI) >> 12)) || (readlookup[readlnext] == ((es + EDI) >> 12)))
            uncached = 1;
        readlookup2[readlookup[readlnext]] = LOOKUP_INV;
    }

#if (defined __amd64__ || defined _M_X64 || defined __aarch64__ || defined _M_ARM64)
    readlookup2[virt >> 12] = (uintptr_t) &ram[(uintptr_t) (phys & ~0xFFF) - (uintptr_t) (virt & ~0xfff)];
#else
    a = ((uint32_t) (phys & ~0xfff) - (uint32_t) (virt & ~0xfff));

    if ((phys & ~0xfff) >= (1 << 30))
        readlookup2[virt >> 12] = (uintptr_t) &ram2[a - (1 << 30)];
    else
        readlookup2[virt >> 12] = (uintptr_t) &ram[a];
#endif
    readlookupp[virt >> 12] = mmu_perm;

    readlookup[readlnext++] = virt >> 12;
    readlnext &= (cachesize - 1);

    cycles -= 9;
}

void
addwritelookup(uint32_t virt, uint32_t phys)
{
#if (!(defined __amd64__ || defined _M_X64 || defined __aarch64__ || defined _M_ARM64))
    uint32_t a;
#endif

    if (virt == 0xffffffff)
        return;

    if (page_lookup[virt >> 12])
        return;

    if (writelookup[writelnext] != -1) {
        page_lookup[writelookup[writelnext]]  = NULL;
        writelookup2[writelookup[writelnext]] = LOOKUP_INV;
    }

#ifdef USE_NEW_DYNAREC
#    ifdef USE_DYNAREC
    if (pages[phys >> 12].block || (phys & ~0xfff) == recomp_page) {
#    else
    if (pages[phys >> 12].block) {
#    endif
#else
#    ifdef USE_DYNAREC
    if (pages[phys >> 12].block[0] || pages[phys >> 12].block[1] || pages[phys >> 12].block[2] || pages[phys >> 12].block[3] || (phys & ~0xfff) == recomp_page) {
#    else
    if (pages[phys >> 12].block[0] || pages[phys >> 12].block[1] || pages[phys >> 12].block[2] || pages[phys >> 12].block[3]) {
#    endif
#endif
        page_lookup[virt >> 12]  = &pages[phys >> 12];
        page_lookupp[virt >> 12] = mmu_perm;
    } else {
#if (defined __amd64__ || defined _M_X64 || defined __aarch64__ || defined _M_ARM64)
        writelookup2[virt >> 12] = (uintptr_t) &ram[(uintptr_t) (phys & ~0xFFF) - (uintptr_t) (virt & ~0xfff)];
#else
        a = ((uint32_t) (phys & ~0xfff) - (uint32_t) (virt & ~0xfff));

        if ((phys & ~0xfff) >= (1 << 30))
            writelookup2[virt >> 12] = (uintptr_t) &ram2[a - (1 << 30)];
        else
            writelookup2[virt >> 12] = (uintptr_t) &ram[a];
#endif
    }
    writelookupp[virt >> 12] = mmu_perm;

    writelookup[writelnext++] = virt >> 12;
    writelnext &= (cachesize - 1);

    cycles -= 9;
}

uint8_t *
getpccache(uint32_t a)
{
    uint64_t a64 = (uint64_t) a;
#if (defined __amd64__ || defined _M_X64 || defined __aarch64__ || defined _M_ARM64)
    uint8_t *p;
#endif
    uint32_t a2;

    a2 = a;

    if (cr0 >> 31) {
        a64 = mmutranslate_read(a64);

        if (a64 == 0xffffffffffffffffULL)
            return ram;
    }
    a64 &= rammask;

    if (_mem_exec[a64 >> MEM_GRANULARITY_BITS]) {
        if (is286) {
            if (read_mapping[a64 >> MEM_GRANULARITY_BITS] && (read_mapping[a64 >> MEM_GRANULARITY_BITS]->flags & MEM_MAPPING_ROM_WS))
                cpu_prefetch_cycles = cpu_rom_prefetch_cycles;
            else
                cpu_prefetch_cycles = cpu_mem_prefetch_cycles;
        }

#if (defined __amd64__ || defined _M_X64 || defined __aarch64__ || defined _M_ARM64)
        p = &_mem_exec[a64 >> MEM_GRANULARITY_BITS][(uintptr_t) (a64 & MEM_GRANULARITY_PAGE) - (uintptr_t) (a2 & ~0xfff)];
        return (uint8_t *) (((uintptr_t) p & 0x00000000ffffffffULL) | ((uintptr_t) &_mem_exec[a64 >> MEM_GRANULARITY_BITS][0] & 0xffffffff00000000ULL));
#else
        return &_mem_exec[a64 >> MEM_GRANULARITY_BITS][(uintptr_t) (a64 & MEM_GRANULARITY_PAGE) - (uintptr_t) (a2 & ~0xfff)];
#endif
    }

    mem_log("Bad getpccache %08X%08X\n", (uint32_t) (a64 >> 32), (uint32_t) (a64 & 0xffffffffULL));

    return (uint8_t *) &ff_pccache;
}

uint8_t
read_mem_b(uint32_t addr)
{
    mem_mapping_t *map;
    uint8_t        ret        = 0xff;
    int            old_cycles = cycles;

    mem_logical_addr = addr;
    addr &= rammask;

    map = read_mapping[addr >> MEM_GRANULARITY_BITS];
    if (map && map->read_b)
        ret = map->read_b(addr, map->priv);

    resub_cycles(old_cycles);

    return ret;
}

uint16_t
read_mem_w(uint32_t addr)
{
    mem_mapping_t *map;
    uint16_t       ret        = 0xffff;
    int            old_cycles = cycles;

    mem_logical_addr = addr;
    addr &= rammask;

    if (addr & 1)
        ret = read_mem_b(addr) | (read_mem_b(addr + 1) << 8);
    else {
        map = read_mapping[addr >> MEM_GRANULARITY_BITS];

        if (map && map->read_w)
            ret = map->read_w(addr, map->priv);
        else if (map && map->read_b)
            ret = map->read_b(addr, map->priv) | (map->read_b(addr + 1, map->priv) << 8);
    }

    resub_cycles(old_cycles);

    return ret;
}

void
write_mem_b(uint32_t addr, uint8_t val)
{
    mem_mapping_t *map;
    int            old_cycles = cycles;

    mem_logical_addr = addr;
    addr &= rammask;

    map = write_mapping[addr >> MEM_GRANULARITY_BITS];
    if (map && map->write_b)
        map->write_b(addr, val, map->priv);

    resub_cycles(old_cycles);
}

void
write_mem_w(uint32_t addr, uint16_t val)
{
    mem_mapping_t *map;
    int            old_cycles = cycles;

    mem_logical_addr = addr;
    addr &= rammask;

    if (addr & 1) {
        write_mem_b(addr, val);
        write_mem_b(addr + 1, val >> 8);
    } else {
        map = write_mapping[addr >> MEM_GRANULARITY_BITS];
        if (map) {
            if (map->write_w)
                map->write_w(addr, val, map->priv);
            else if (map->write_b) {
                map->write_b(addr, val, map->priv);
                map->write_b(addr + 1, val >> 8, map->priv);
            }
        }
    }

    resub_cycles(old_cycles);
}

uint8_t
readmembl(uint32_t addr)
{
    mem_mapping_t *map;
    uint64_t       a;

    GDBSTUB_MEM_ACCESS(addr, GDBSTUB_MEM_READ, 1);
#ifdef USE_DEBUG_REGS_486
    mem_debug_check_addr(addr, read_type);
#endif

    addr64           = (uint64_t) addr;
    mem_logical_addr = addr;

    high_page = 0;

    if (cr0 >> 31) {
        a      = mmutranslate_read(addr);
        addr64 = (uint32_t) a;

        if (a > 0xffffffffULL)
            return 0xff;
    }
    addr = (uint32_t) (addr64 & rammask);

    map = read_mapping[addr >> MEM_GRANULARITY_BITS];
    if (map && map->read_b)
        return map->read_b(addr, map->priv);

    return 0xff;
}

void
writemembl(uint32_t addr, uint8_t val)
{
    mem_mapping_t *map;
    uint64_t       a;

    GDBSTUB_MEM_ACCESS(addr, GDBSTUB_MEM_WRITE, 1);
#ifdef USE_DEBUG_REGS_486
    mem_debug_check_addr(addr, 2);
#endif

    addr64           = (uint64_t) addr;
    mem_logical_addr = addr;

    high_page = 0;

    if (page_lookup[addr >> 12] && page_lookup[addr >> 12]->write_b) {
        page_lookup[addr >> 12]->write_b(addr, val, page_lookup[addr >> 12]);
        return;
    }

    if (cr0 >> 31) {
        a      = mmutranslate_write(addr);
        addr64 = (uint32_t) a;

        if (a > 0xffffffffULL)
            return;
    }
    addr = (uint32_t) (addr64 & rammask);

    map = write_mapping[addr >> MEM_GRANULARITY_BITS];
    if (map && map->write_b)
        map->write_b(addr, val, map->priv);
}

/* Read a byte from memory without MMU translation - result of previous MMU translation passed as value. */
uint8_t
readmembl_no_mmut(uint32_t addr, uint32_t a64)
{
    mem_mapping_t *map;

    GDBSTUB_MEM_ACCESS(addr, GDBSTUB_MEM_READ, 1);

    mem_logical_addr = addr;

    if (cr0 >> 31) {
        if (cpu_state.abrt || high_page)
            return 0xff;

        addr = a64 & rammask;
    } else
        addr &= rammask;

    map = read_mapping[addr >> MEM_GRANULARITY_BITS];
    if (map && map->read_b)
        return map->read_b(addr, map->priv);

    return 0xff;
}

/* Write a byte to memory without MMU translation - result of previous MMU translation passed as value. */
void
writemembl_no_mmut(uint32_t addr, uint32_t a64, uint8_t val)
{
    mem_mapping_t *map;

    GDBSTUB_MEM_ACCESS(addr, GDBSTUB_MEM_WRITE, 1);

    mem_logical_addr = addr;

    if (page_lookup[addr >> 12] && page_lookup[addr >> 12]->write_b) {
        page_lookup[addr >> 12]->write_b(addr, val, page_lookup[addr >> 12]);
        return;
    }

    if (cr0 >> 31) {
        if (cpu_state.abrt || high_page)
            return;

        addr = a64 & rammask;
    } else
        addr &= rammask;

    map = write_mapping[addr >> MEM_GRANULARITY_BITS];
    if (map && map->write_b)
        map->write_b(addr, val, map->priv);
}

uint16_t
readmemwl(uint32_t addr)
{
    mem_mapping_t *map;
    uint64_t       a;

    addr64a[0] = addr;
    addr64a[1] = addr + 1;
#ifdef USE_DEBUG_REGS_486
    mem_debug_check_addr(addr, read_type);
    mem_debug_check_addr(addr + 1, read_type);
#endif
    GDBSTUB_MEM_ACCESS_FAST(addr64a, GDBSTUB_MEM_READ, 2);

    mem_logical_addr = addr;

    high_page = 0;

    if (addr & 1) {
        if (!cpu_cyrix_alignment || (addr & 7) == 7)
            cycles -= timing_misaligned;
        if ((addr & 0xfff) > 0xffe) {
            if (cr0 >> 31) {
                for (uint8_t i = 0; i < 2; i++) {
                    a          = mmutranslate_read(addr + i);
                    addr64a[i] = (uint32_t) a;

                    if (a > 0xffffffffULL)
                        return 0xffff;
                }
            }

            return readmembl_no_mmut(addr, addr64a[0]) | (((uint16_t) readmembl_no_mmut(addr + 1, addr64a[1])) << 8);
        } else if (readlookup2[addr >> 12] != (uintptr_t) LOOKUP_INV) {
            mmu_perm = readlookupp[addr >> 12];
            return *(uint16_t *) (readlookup2[addr >> 12] + addr);
        }
    }

    if (cr0 >> 31) {
        a          = mmutranslate_read(addr);
        addr64a[0] = (uint32_t) a;

        if (a > 0xffffffffULL)
            return 0xffff;
    } else
        addr64a[0] = (uint64_t) addr;

    addr = addr64a[0] & rammask;

    map = read_mapping[addr >> MEM_GRANULARITY_BITS];

    if (map && map->read_w)
        return map->read_w(addr, map->priv);

    if (map && map->read_b) {
        return map->read_b(addr, map->priv) | ((uint16_t) (map->read_b(addr + 1, map->priv)) << 8);
    }

    return 0xffff;
}

void
writememwl(uint32_t addr, uint16_t val)
{
    mem_mapping_t *map;
    uint64_t       a;

    addr64a[0] = addr;
    addr64a[1] = addr + 1;
#ifdef USE_DEBUG_REGS_486
    mem_debug_check_addr(addr, 2);
    mem_debug_check_addr(addr + 1, 2);
#endif
    GDBSTUB_MEM_ACCESS_FAST(addr64a, GDBSTUB_MEM_WRITE, 2);

    mem_logical_addr = addr;

    high_page = 0;

    if (addr & 1) {
        if (!cpu_cyrix_alignment || (addr & 7) == 7)
            cycles -= timing_misaligned;
        if ((addr & 0xfff) > 0xffe) {
            if (cr0 >> 31) {
                for (uint8_t i = 0; i < 2; i++) {
                    /* Do not translate a page that has a valid lookup, as that is by definition valid
                       and the whole purpose of the lookup is to avoid repeat identical translations. */
                    if (!page_lookup[(addr + i) >> 12] || !page_lookup[(addr + i) >> 12]->write_b) {
                        a          = mmutranslate_write(addr + i);
                        addr64a[i] = (uint32_t) a;

                        if (a > 0xffffffffULL)
                            return;
                    }
                }
            }

            /* No need to waste precious CPU host cycles on mmutranslate's that were already done, just pass
               their result as a parameter to be used if needed. */
            writemembl_no_mmut(addr, addr64a[0], val);
            writemembl_no_mmut(addr + 1, addr64a[1], val >> 8);
            return;
        } else if (writelookup2[addr >> 12] != (uintptr_t) LOOKUP_INV) {
            mmu_perm                                        = writelookupp[addr >> 12];
            *(uint16_t *) (writelookup2[addr >> 12] + addr) = val;
            return;
        }
    }

    if (page_lookup[addr >> 12] && page_lookup[addr >> 12]->write_w) {
        page_lookup[addr >> 12]->write_w(addr, val, page_lookup[addr >> 12]);
        mmu_perm = page_lookupp[addr >> 12];
        return;
    }

    if (cr0 >> 31) {
        a          = mmutranslate_write(addr);
        addr64a[0] = (uint32_t) a;

        if (a > 0xffffffffULL)
            return;
    }

    addr = addr64a[0] & rammask;

    map = write_mapping[addr >> MEM_GRANULARITY_BITS];

    if (map && map->write_w) {
        map->write_w(addr, val, map->priv);
        return;
    }

    if (map && map->write_b) {
        map->write_b(addr, val, map->priv);
        map->write_b(addr + 1, val >> 8, map->priv);
        return;
    }
}

/* Read a word from memory without MMU translation - results of previous MMU translation passed as array. */
uint16_t
readmemwl_no_mmut(uint32_t addr, uint32_t *a64)
{
    mem_mapping_t *map;

    GDBSTUB_MEM_ACCESS(addr, GDBSTUB_MEM_READ, 2);

    mem_logical_addr = addr;

    if (addr & 1) {
        if (!cpu_cyrix_alignment || (addr & 7) == 7)
            cycles -= timing_misaligned;
        if ((addr & 0xfff) > 0xffe) {
            if (cr0 >> 31) {
                if (cpu_state.abrt || high_page)
                    return 0xffff;
            }

            return readmembl_no_mmut(addr, a64[0]) | (((uint16_t) readmembl_no_mmut(addr + 1, a64[1])) << 8);
        } else if (readlookup2[addr >> 12] != (uintptr_t) LOOKUP_INV) {
            mmu_perm = readlookupp[addr >> 12];
            return *(uint16_t *) (readlookup2[addr >> 12] + addr);
        }
    }

    if (cr0 >> 31) {
        if (cpu_state.abrt || high_page)
            return 0xffff;

        addr = (uint32_t) (a64[0] & rammask);
    } else
        addr &= rammask;

    map = read_mapping[addr >> MEM_GRANULARITY_BITS];

    if (map && map->read_w)
        return map->read_w(addr, map->priv);

    if (map && map->read_b) {
        return map->read_b(addr, map->priv) | ((uint16_t) (map->read_b(addr + 1, map->priv)) << 8);
    }

    return 0xffff;
}

/* Write a word to memory without MMU translation - results of previous MMU translation passed as array. */
void
writememwl_no_mmut(uint32_t addr, uint32_t *a64, uint16_t val)
{
    mem_mapping_t *map;

    GDBSTUB_MEM_ACCESS(addr, GDBSTUB_MEM_WRITE, 2);

    mem_logical_addr = addr;

    if (addr & 1) {
        if (!cpu_cyrix_alignment || (addr & 7) == 7)
            cycles -= timing_misaligned;
        if ((addr & 0xfff) > 0xffe) {
            if (cr0 >> 31) {
                if (cpu_state.abrt || high_page)
                    return;
            }

            writemembl_no_mmut(addr, a64[0], val);
            writemembl_no_mmut(addr + 1, a64[1], val >> 8);
            return;
        } else if (writelookup2[addr >> 12] != (uintptr_t) LOOKUP_INV) {
            mmu_perm                                        = writelookupp[addr >> 12];
            *(uint16_t *) (writelookup2[addr >> 12] + addr) = val;
            return;
        }
    }

    if (page_lookup[addr >> 12] && page_lookup[addr >> 12]->write_w) {
        mmu_perm = page_lookupp[addr >> 12];
        page_lookup[addr >> 12]->write_w(addr, val, page_lookup[addr >> 12]);
        return;
    }

    if (cr0 >> 31) {
        if (cpu_state.abrt || high_page)
            return;

        addr = (uint32_t) (a64[0] & rammask);
    } else
        addr &= rammask;

    map = write_mapping[addr >> MEM_GRANULARITY_BITS];

    if (map && map->write_w) {
        map->write_w(addr, val, map->priv);
        return;
    }

    if (map && map->write_b) {
        map->write_b(addr, val, map->priv);
        map->write_b(addr + 1, val >> 8, map->priv);
        return;
    }
}

uint32_t
readmemll(uint32_t addr)
{
    mem_mapping_t *map;
    int            i;
    uint64_t       a = 0x0000000000000000ULL;

    for (i = 0; i < 4; i++) {
        addr64a[i] = (uint64_t) (addr + i);
#ifdef USE_DEBUG_REGS_486
        mem_debug_check_addr(addr + i, read_type);
#endif
    }
    GDBSTUB_MEM_ACCESS_FAST(addr64a, GDBSTUB_MEM_READ, 4);

    mem_logical_addr = addr;

    high_page = 0;

    if (addr & 3) {
        if (!cpu_cyrix_alignment || (addr & 7) > 4)
            cycles -= timing_misaligned;
        if ((addr & 0xfff) > 0xffc) {
            if (cr0 >> 31) {
                for (i = 0; i < 4; i++) {
                    if (i == 0) {
                        a          = mmutranslate_read(addr + i);
                        addr64a[i] = (uint32_t) a;
                    } else if (!((addr + i) & 0xfff)) {
                        a          = mmutranslate_read(addr + 3);
                        addr64a[i] = (uint32_t) a;
                        if (!cpu_state.abrt) {
                            a          = (a & ~0xfffLL) | ((uint64_t) ((addr + i) & 0xfff));
                            addr64a[i] = (uint32_t) a;
                        }
                    } else {
                        a          = (a & ~0xfffLL) | ((uint64_t) ((addr + i) & 0xfff));
                        addr64a[i] = (uint32_t) a;
                    }

                    if (a > 0xffffffffULL)
                        return 0xffff;
                }
            }

            /* No need to waste precious CPU host cycles on mmutranslate's that were already done, just pass
               their result as a parameter to be used if needed. */
            return readmemwl_no_mmut(addr, addr64a) | (((uint32_t) readmemwl_no_mmut(addr + 2, &(addr64a[2]))) << 16);
        } else if (readlookup2[addr >> 12] != (uintptr_t) LOOKUP_INV) {
            mmu_perm = readlookupp[addr >> 12];
            return *(uint32_t *) (readlookup2[addr >> 12] + addr);
        }
    }

    if (cr0 >> 31) {
        a          = mmutranslate_read(addr);
        addr64a[0] = (uint32_t) a;

        if (a > 0xffffffffULL)
            return 0xffffffff;
    }

    addr = addr64a[0] & rammask;

    map = read_mapping[addr >> MEM_GRANULARITY_BITS];

    if (map && map->read_l)
        return map->read_l(addr, map->priv);

    if (map && map->read_w)
        return map->read_w(addr, map->priv) | ((uint32_t) (map->read_w(addr + 2, map->priv)) << 16);

    if (map && map->read_b)
        return map->read_b(addr, map->priv) | ((uint32_t) (map->read_b(addr + 1, map->priv)) << 8) | ((uint32_t) (map->read_b(addr + 2, map->priv)) << 16) | ((uint32_t) (map->read_b(addr + 3, map->priv)) << 24);

    return 0xffffffff;
}

void
writememll(uint32_t addr, uint32_t val)
{
    mem_mapping_t *map;
    int            i;
    uint64_t       a = 0x0000000000000000ULL;

    for (i = 0; i < 4; i++) {
        addr64a[i] = (uint64_t) (addr + i);
#ifdef USE_DEBUG_REGS_486
        mem_debug_check_addr(addr + i, 2);
#endif
    }
    GDBSTUB_MEM_ACCESS_FAST(addr64a, GDBSTUB_MEM_WRITE, 4);

    mem_logical_addr = addr;

    high_page = 0;

    if (addr & 3) {
        if (!cpu_cyrix_alignment || (addr & 7) > 4)
            cycles -= timing_misaligned;
        if ((addr & 0xfff) > 0xffc) {
            if (cr0 >> 31) {
                for (i = 0; i < 4; i++) {
                    /* Do not translate a page that has a valid lookup, as that is by definition valid
                       and the whole purpose of the lookup is to avoid repeat identical translations. */
                    if (!page_lookup[(addr + i) >> 12] || !page_lookup[(addr + i) >> 12]->write_b) {
                        if (i == 0) {
                            a          = mmutranslate_write(addr + i);
                            addr64a[i] = (uint32_t) a;
                        } else if (!((addr + i) & 0xfff)) {
                            a          = mmutranslate_write(addr + 3);
                            addr64a[i] = (uint32_t) a;
                            if (!cpu_state.abrt) {
                                a          = (a & ~0xfffLL) | ((uint64_t) ((addr + i) & 0xfff));
                                addr64a[i] = (uint32_t) a;
                            }
                        } else {
                            a          = (a & ~0xfffLL) | ((uint64_t) ((addr + i) & 0xfff));
                            addr64a[i] = (uint32_t) a;
                        }

                        if (a > 0xffffffffULL)
                            return;
                    }
                }
            }

            /* No need to waste precious CPU host cycles on mmutranslate's that were already done, just pass
               their result as a parameter to be used if needed. */
            writememwl_no_mmut(addr, &(addr64a[0]), val);
            writememwl_no_mmut(addr + 2, &(addr64a[2]), val >> 16);
            return;
        } else if (writelookup2[addr >> 12] != (uintptr_t) LOOKUP_INV) {
            mmu_perm                                        = writelookupp[addr >> 12];
            *(uint32_t *) (writelookup2[addr >> 12] + addr) = val;
            return;
        }
    }

    if (page_lookup[addr >> 12] && page_lookup[addr >> 12]->write_l) {
        mmu_perm = page_lookupp[addr >> 12];
        page_lookup[addr >> 12]->write_l(addr, val, page_lookup[addr >> 12]);
        return;
    }

    if (cr0 >> 31) {
        a          = mmutranslate_write(addr);
        addr64a[0] = (uint32_t) a;

        if (a > 0xffffffffULL)
            return;
    }

    addr = addr64a[0] & rammask;

    map = write_mapping[addr >> MEM_GRANULARITY_BITS];

    if (map && map->write_l) {
        map->write_l(addr, val, map->priv);
        return;
    }
    if (map && map->write_w) {
        map->write_w(addr, val, map->priv);
        map->write_w(addr + 2, val >> 16, map->priv);
        return;
    }
    if (map && map->write_b) {
        map->write_b(addr, val, map->priv);
        map->write_b(addr + 1, val >> 8, map->priv);
        map->write_b(addr + 2, val >> 16, map->priv);
        map->write_b(addr + 3, val >> 24, map->priv);
        return;
    }
}

/* Read a long from memory without MMU translation - results of previous MMU translation passed as array. */
uint32_t
readmemll_no_mmut(uint32_t addr, uint32_t *a64)
{
    mem_mapping_t *map;

    GDBSTUB_MEM_ACCESS(addr, GDBSTUB_MEM_READ, 4);

    mem_logical_addr = addr;

    if (addr & 3) {
        if (!cpu_cyrix_alignment || (addr & 7) > 4)
            cycles -= timing_misaligned;
        if ((addr & 0xfff) > 0xffc) {
            if (cr0 >> 31) {
                if (cpu_state.abrt || high_page)
                    return 0xffffffff;
            }

            return readmemwl_no_mmut(addr, a64) | ((uint32_t) (readmemwl_no_mmut(addr + 2, &(a64[2]))) << 16);
        } else if (readlookup2[addr >> 12] != (uintptr_t) LOOKUP_INV) {
            mmu_perm = readlookupp[addr >> 12];
            return *(uint32_t *) (readlookup2[addr >> 12] + addr);
        }
    }

    if (cr0 >> 31) {
        if (cpu_state.abrt || high_page)
            return 0xffffffff;

        addr = (uint32_t) (a64[0] & rammask);
    } else
        addr &= rammask;

    map = read_mapping[addr >> MEM_GRANULARITY_BITS];

    if (map && map->read_l)
        return map->read_l(addr, map->priv);

    if (map && map->read_w)
        return map->read_w(addr, map->priv) | ((uint32_t) (map->read_w(addr + 2, map->priv)) << 16);

    if (map && map->read_b)
        return map->read_b(addr, map->priv) | ((uint32_t) (map->read_b(addr + 1, map->priv)) << 8) | ((uint32_t) (map->read_b(addr + 2, map->priv)) << 16) | ((uint32_t) (map->read_b(addr + 3, map->priv)) << 24);

    return 0xffffffff;
}

/* Write a long to memory without MMU translation - results of previous MMU translation passed as array. */
void
writememll_no_mmut(uint32_t addr, uint32_t *a64, uint32_t val)
{
    mem_mapping_t *map;

    GDBSTUB_MEM_ACCESS(addr, GDBSTUB_MEM_WRITE, 4);

    mem_logical_addr = addr;

    if (addr & 3) {
        if (!cpu_cyrix_alignment || (addr & 7) > 4)
            cycles -= timing_misaligned;
        if ((addr & 0xfff) > 0xffc) {
            if (cr0 >> 31) {
                if (cpu_state.abrt || high_page)
                    return;
            }

            writememwl_no_mmut(addr, &(a64[0]), val);
            writememwl_no_mmut(addr + 2, &(a64[2]), val >> 16);
            return;
        } else if (writelookup2[addr >> 12] != (uintptr_t) LOOKUP_INV) {
            mmu_perm                                        = writelookupp[addr >> 12];
            *(uint32_t *) (writelookup2[addr >> 12] + addr) = val;
            return;
        }
    }

    if (page_lookup[addr >> 12] && page_lookup[addr >> 12]->write_l) {
        mmu_perm = page_lookupp[addr >> 12];
        page_lookup[addr >> 12]->write_l(addr, val, page_lookup[addr >> 12]);
        return;
    }

    if (cr0 >> 31) {
        if (cpu_state.abrt || high_page)
            return;

        addr = (uint32_t) (a64[0] & rammask);
    } else
        addr &= rammask;

    map = write_mapping[addr >> MEM_GRANULARITY_BITS];

    if (map && map->write_l) {
        map->write_l(addr, val, map->priv);
        return;
    }
    if (map && map->write_w) {
        map->write_w(addr, val, map->priv);
        map->write_w(addr + 2, val >> 16, map->priv);
        return;
    }
    if (map && map->write_b) {
        map->write_b(addr, val, map->priv);
        map->write_b(addr + 1, val >> 8, map->priv);
        map->write_b(addr + 2, val >> 16, map->priv);
        map->write_b(addr + 3, val >> 24, map->priv);
        return;
    }
}

uint64_t
readmemql(uint32_t addr)
{
    mem_mapping_t *map;
    int            i;
    uint64_t       a = 0x0000000000000000ULL;

    for (i = 0; i < 8; i++) {
        addr64a[i] = (uint64_t) (addr + i);
#ifdef USE_DEBUG_REGS_486
        mem_debug_check_addr(addr + i, read_type);
#endif
    }
    GDBSTUB_MEM_ACCESS_FAST(addr64a, GDBSTUB_MEM_READ, 8);

    mem_logical_addr = addr;

    high_page = 0;

    if (addr & 7) {
        cycles -= timing_misaligned;
        if ((addr & 0xfff) > 0xff8) {
            if (cr0 >> 31) {
                for (i = 0; i < 8; i++) {
                    if (i == 0) {
                        a          = mmutranslate_read(addr + i);
                        addr64a[i] = (uint32_t) a;
                    } else if (!((addr + i) & 0xfff)) {
                        a          = mmutranslate_read(addr + 7);
                        addr64a[i] = (uint32_t) a;
                        if (!cpu_state.abrt) {
                            a          = (a & ~0xfffLL) | ((uint64_t) ((addr + i) & 0xfff));
                            addr64a[i] = (uint32_t) a;
                        }
                    } else {
                        a          = (a & ~0xfffLL) | ((uint64_t) ((addr + i) & 0xfff));
                        addr64a[i] = (uint32_t) a;
                    }

                    if (a > 0xffffffffULL)
                        return 0xffff;
                }
            }

            /* No need to waste precious CPU host cycles on mmutranslate's that were already done, just pass
               their result as a parameter to be used if needed. */
            return readmemll_no_mmut(addr, addr64a) | (((uint64_t) readmemll_no_mmut(addr + 4, &(addr64a[4]))) << 32);
        } else if (readlookup2[addr >> 12] != (uintptr_t) LOOKUP_INV) {
            mmu_perm = readlookupp[addr >> 12];
            return *(uint64_t *) (readlookup2[addr >> 12] + addr);
        }
    }

    if (cr0 >> 31) {
        a          = mmutranslate_read(addr);
        addr64a[0] = (uint32_t) a;

        if (a > 0xffffffffULL)
            return 0xffffffffffffffffULL;
    }

    addr = addr64a[0] & rammask;

    map = read_mapping[addr >> MEM_GRANULARITY_BITS];
    if (map && map->read_l)
        return map->read_l(addr, map->priv) | ((uint64_t) map->read_l(addr + 4, map->priv) << 32);

    return readmemll(addr) | ((uint64_t) readmemll(addr + 4) << 32);
}

void
writememql(uint32_t addr, uint64_t val)
{
    mem_mapping_t *map;
    int            i;
    uint64_t       a = 0x0000000000000000ULL;

    for (i = 0; i < 8; i++) {
        addr64a[i] = (uint64_t) (addr + i);
#ifdef USE_DEBUG_REGS_486
        mem_debug_check_addr(addr + i, 2);
#endif
    }
    GDBSTUB_MEM_ACCESS_FAST(addr64a, GDBSTUB_MEM_WRITE, 8);

    mem_logical_addr = addr;

    high_page = 0;

    if (addr & 7) {
        cycles -= timing_misaligned;
        if ((addr & 0xfff) > 0xff8) {
            if (cr0 >> 31) {
                for (i = 0; i < 8; i++) {
                    /* Do not translate a page that has a valid lookup, as that is by definition valid
                       and the whole purpose of the lookup is to avoid repeat identical translations. */
                    if (!page_lookup[(addr + i) >> 12] || !page_lookup[(addr + i) >> 12]->write_b) {
                        if (i == 0) {
                            a          = mmutranslate_write(addr + i);
                            addr64a[i] = (uint32_t) a;
                        } else if (!((addr + i) & 0xfff)) {
                            a          = mmutranslate_write(addr + 7);
                            addr64a[i] = (uint32_t) a;
                            if (!cpu_state.abrt) {
                                a          = (a & ~0xfffLL) | ((uint64_t) ((addr + i) & 0xfff));
                                addr64a[i] = (uint32_t) a;
                            }
                        } else {
                            a          = (a & ~0xfffLL) | ((uint64_t) ((addr + i) & 0xfff));
                            addr64a[i] = (uint32_t) a;
                        }

                        if (addr64a[i] > 0xffffffffULL)
                            return;
                    }
                }
            }

            /* No need to waste precious CPU host cycles on mmutranslate's that were already done, just pass
               their result as a parameter to be used if needed. */
            writememll_no_mmut(addr, addr64a, val);
            writememll_no_mmut(addr + 4, &(addr64a[4]), val >> 32);
            return;
        } else if (writelookup2[addr >> 12] != (uintptr_t) LOOKUP_INV) {
            mmu_perm                                        = writelookupp[addr >> 12];
            *(uint64_t *) (writelookup2[addr >> 12] + addr) = val;
            return;
        }
    }

    if (page_lookup[addr >> 12] && page_lookup[addr >> 12]->write_l) {
        mmu_perm = page_lookupp[addr >> 12];
        page_lookup[addr >> 12]->write_l(addr, val, page_lookup[addr >> 12]);
        page_lookup[addr >> 12]->write_l(addr + 4, val >> 32, page_lookup[addr >> 12]);
        return;
    }

    if (cr0 >> 31) {
        addr64a[0] = mmutranslate_write(addr);
        if (addr64a[0] > 0xffffffffULL)
            return;
    }

    addr = addr64a[0] & rammask;

    map = write_mapping[addr >> MEM_GRANULARITY_BITS];

    if (map && map->write_l) {
        map->write_l(addr, val, map->priv);
        map->write_l(addr + 4, val >> 32, map->priv);
        return;
    }
    if (map && map->write_w) {
        map->write_w(addr, val, map->priv);
        map->write_w(addr + 2, val >> 16, map->priv);
        map->write_w(addr + 4, val >> 32, map->priv);
        map->write_w(addr + 6, val >> 48, map->priv);
        return;
    }
    if (map && map->write_b) {
        map->write_b(addr, val, map->priv);
        map->write_b(addr + 1, val >> 8, map->priv);
        map->write_b(addr + 2, val >> 16, map->priv);
        map->write_b(addr + 3, val >> 24, map->priv);
        map->write_b(addr + 4, val >> 32, map->priv);
        map->write_b(addr + 5, val >> 40, map->priv);
        map->write_b(addr + 6, val >> 48, map->priv);
        map->write_b(addr + 7, val >> 56, map->priv);
        return;
    }
}

void
do_mmutranslate(uint32_t addr, uint32_t *a64, int num, int write)
{
    int      i;
    int      cond = 1;
    uint32_t last_addr = addr + (num - 1);
    uint64_t a         = 0x0000000000000000ULL;
#ifdef USE_DEBUG_REGS_486
    mem_debug_check_addr(addr, write ? 2 : read_type);
#endif
    for (i = 0; i < num; i++)
        a64[i] = (uint64_t) addr;

    if (cr0 >> 31)  for (i = 0; i < num; i++) {
        if (write && ((i == 0) || !(addr & 0xfff)))
            cond = (!page_lookup[addr >> 12] || !page_lookup[addr >> 12]->write_b);

        if (cond) {
            /* If we have encountered at least one page fault, mark all subsequent addresses as
               having page faulted, prevents false negatives in readmem*l_no_mmut. */
            if ((i > 0) && cpu_state.abrt && !high_page)
                a64[i] = a64[i - 1];
            /* If we are on the same page, there is no need to translate again, as we can just
               reuse the previous result. */
            else if (i == 0) {
                a      = mmutranslatereal(addr, write);
                a64[i] = (uint32_t) a;

                high_page = high_page || (!cpu_state.abrt && (a > 0xffffffffULL));
            } else if (!(addr & 0xfff)) {
                a      = mmutranslatereal(last_addr, write);
                a64[i] = (uint32_t) a;

                high_page = high_page || (!cpu_state.abrt && (a64[i] > 0xffffffffULL));

                if (!cpu_state.abrt) {
                    a      = (a & 0xfffffffffffff000ULL) | ((uint64_t) (addr & 0xfff));
                    a64[i] = (uint32_t) a;
                }
            } else {
                a      = (a & 0xfffffffffffff000ULL) | ((uint64_t) (addr & 0xfff));
                a64[i] = (uint32_t) a;
            }
        } else
            mmu_perm = page_lookupp[addr >> 12];

        addr++;
    }
}

uint8_t
mem_readb_phys(uint32_t addr)
{
    mem_mapping_t *map = read_mapping_bus[addr >> MEM_GRANULARITY_BITS];
    uint8_t        ret = 0xff;

    mem_logical_addr = 0xffffffff;

    if (map) {
        if (cpu_use_exec && map->exec)
            ret = map->exec[(addr - map->base) & map->mask];
        else if (map->read_b)
            ret = map->read_b(addr, map->priv);
    }

    return ret;
}

uint16_t
mem_readw_phys(uint32_t addr)
{
    mem_mapping_t  *map = read_mapping_bus[addr >> MEM_GRANULARITY_BITS];
    uint16_t        ret;
    const uint16_t *p;

    mem_logical_addr = 0xffffffff;

    if (cpu_use_exec && ((addr & MEM_GRANULARITY_MASK) <= MEM_GRANULARITY_HBOUND) && (map && map->exec)) {
        p   = (uint16_t *) &(map->exec[(addr - map->base) & map->mask]);
        ret = *p;
    } else if (((addr & MEM_GRANULARITY_MASK) <= MEM_GRANULARITY_HBOUND) && (map && map->read_w))
        ret = map->read_w(addr, map->priv);
    else {
        ret = mem_readb_phys(addr + 1) << 8;
        ret |= mem_readb_phys(addr);
    }

    return ret;
}

uint32_t
mem_readl_phys(uint32_t addr)
{
    mem_mapping_t  *map = read_mapping_bus[addr >> MEM_GRANULARITY_BITS];
    uint32_t        ret;
    const uint32_t *p;

    mem_logical_addr = 0xffffffff;

    if (cpu_use_exec && ((addr & MEM_GRANULARITY_MASK) <= MEM_GRANULARITY_QBOUND) && (map && map->exec)) {
        p   = (uint32_t *) &(map->exec[(addr - map->base) & map->mask]);
        ret = *p;
    } else if (((addr & MEM_GRANULARITY_MASK) <= MEM_GRANULARITY_QBOUND) && (map && map->read_l))
        ret = map->read_l(addr, map->priv);
    else {
        ret = mem_readw_phys(addr + 2) << 16;
        ret |= mem_readw_phys(addr);
    }

    return ret;
}

void
mem_read_phys(void *dest, uint32_t addr, int transfer_size)
{
    uint8_t  *pb;
    uint16_t *pw;
    uint32_t *pl;

    if (transfer_size == 4) {
        pl  = (uint32_t *) dest;
        *pl = mem_readl_phys(addr);
    } else if (transfer_size == 2) {
        pw  = (uint16_t *) dest;
        *pw = mem_readw_phys(addr);
    } else if (transfer_size == 1) {
        pb  = (uint8_t *) dest;
        *pb = mem_readb_phys(addr);
    }
}

void
mem_writeb_phys(uint32_t addr, uint8_t val)
{
    mem_mapping_t *map = write_mapping_bus[addr >> MEM_GRANULARITY_BITS];

    mem_logical_addr = 0xffffffff;

    if (map) {
        if (cpu_use_exec && map->exec)
            map->exec[(addr - map->base) & map->mask] = val;
        else if (map->write_b)
            map->write_b(addr, val, map->priv);
    }
}

void
mem_writew_phys(uint32_t addr, uint16_t val)
{
    mem_mapping_t *map = write_mapping_bus[addr >> MEM_GRANULARITY_BITS];
    uint16_t      *p;

    mem_logical_addr = 0xffffffff;

    if (cpu_use_exec && ((addr & MEM_GRANULARITY_MASK) <= MEM_GRANULARITY_HBOUND) && (map && map->exec)) {
        p  = (uint16_t *) &(map->exec[(addr - map->base) & map->mask]);
        *p = val;
    } else if (((addr & MEM_GRANULARITY_MASK) <= MEM_GRANULARITY_HBOUND) && (map && map->write_w))
        map->write_w(addr, val, map->priv);
    else {
        mem_writeb_phys(addr, val & 0xff);
        mem_writeb_phys(addr + 1, (val >> 8) & 0xff);
    }
}

void
mem_writel_phys(uint32_t addr, uint32_t val)
{
    mem_mapping_t *map = write_mapping_bus[addr >> MEM_GRANULARITY_BITS];
    uint32_t      *p;

    mem_logical_addr = 0xffffffff;

    if (cpu_use_exec && ((addr & MEM_GRANULARITY_MASK) <= MEM_GRANULARITY_QBOUND) && (map && map->exec)) {
        p  = (uint32_t *) &(map->exec[(addr - map->base) & map->mask]);
        *p = val;
    } else if (((addr & MEM_GRANULARITY_MASK) <= MEM_GRANULARITY_QBOUND) && (map && map->write_l))
        map->write_l(addr, val, map->priv);
    else {
        mem_writew_phys(addr, val & 0xffff);
        mem_writew_phys(addr + 2, (val >> 16) & 0xffff);
    }
}

void
mem_write_phys(void *src, uint32_t addr, int transfer_size)
{
    const uint8_t  *pb;
    const uint16_t *pw;
    const uint32_t *pl;

    if (transfer_size == 4) {
        pl = (uint32_t *) src;
        mem_writel_phys(addr, *pl);
    } else if (transfer_size == 2) {
        pw = (uint16_t *) src;
        mem_writew_phys(addr, *pw);
    } else if (transfer_size == 1) {
        pb = (uint8_t *) src;
        mem_writeb_phys(addr, *pb);
    }
}

uint8_t
mem_read_ram(uint32_t addr, UNUSED(void *priv))
{
#ifdef ENABLE_MEM_LOG
    if ((addr >= 0xa0000) && (addr <= 0xbffff))
        mem_log("Read  B       %02X from %08X\n", ram[addr], addr);
#endif

    if (cpu_use_exec)
        addreadlookup(mem_logical_addr, addr);

    return ram[addr];
}

uint16_t
mem_read_ramw(uint32_t addr, UNUSED(void *priv))
{
#ifdef ENABLE_MEM_LOG
    if ((addr >= 0xa0000) && (addr <= 0xbffff))
        mem_log("Read  W     %04X from %08X\n", *(uint16_t *) &ram[addr], addr);
#endif

    if (cpu_use_exec)
        addreadlookup(mem_logical_addr, addr);

    return *(uint16_t *) &ram[addr];
}

uint32_t
mem_read_raml(uint32_t addr, UNUSED(void *priv))
{
#ifdef ENABLE_MEM_LOG
    if ((addr >= 0xa0000) && (addr <= 0xbffff))
        mem_log("Read  L %08X from %08X\n", *(uint32_t *) &ram[addr], addr);
#endif

    if (cpu_use_exec)
        addreadlookup(mem_logical_addr, addr);

    return *(uint32_t *) &ram[addr];
}

uint8_t
mem_read_ram_2gb(uint32_t addr, UNUSED(void *priv))
{
#ifdef ENABLE_MEM_LOG
    if ((addr >= 0xa0000) && (addr <= 0xbffff))
        mem_log("Read  B       %02X from %08X\n", ram[addr], addr);
#endif

    addreadlookup(mem_logical_addr, addr);

    return ram2[addr - (1 << 30)];
}

uint16_t
mem_read_ram_2gbw(uint32_t addr, UNUSED(void *priv))
{
#ifdef ENABLE_MEM_LOG
    if ((addr >= 0xa0000) && (addr <= 0xbffff))
        mem_log("Read  W     %04X from %08X\n", *(uint16_t *) &ram[addr], addr);
#endif

    addreadlookup(mem_logical_addr, addr);

    return *(uint16_t *) &ram2[addr - (1 << 30)];
}

uint32_t
mem_read_ram_2gbl(uint32_t addr, UNUSED(void *priv))
{
#ifdef ENABLE_MEM_LOG
    if ((addr >= 0xa0000) && (addr <= 0xbffff))
        mem_log("Read  L %08X from %08X\n", *(uint32_t *) &ram[addr], addr);
#endif

    addreadlookup(mem_logical_addr, addr);

    return *(uint32_t *) &ram2[addr - (1 << 30)];
}

#ifdef USE_NEW_DYNAREC
static inline int
page_index(page_t *page)
{
    return ((uintptr_t) page - (uintptr_t) pages) / sizeof(page_t);
}

void
page_add_to_evict_list(page_t *page)
{
    pages[purgable_page_list_head].evict_prev = page_index(page);
    page->evict_next                          = purgable_page_list_head;
    page->evict_prev                          = 0;
    purgable_page_list_head                   = pages[purgable_page_list_head].evict_prev;
    purgeable_page_count++;
}

void
page_remove_from_evict_list(page_t *page)
{
    if (!page_in_evict_list(page))
        fatal("page_remove_from_evict_list: not in evict list!\n");
    if (page->evict_prev)
        pages[page->evict_prev].evict_next = page->evict_next;
    else
        purgable_page_list_head = page->evict_next;
    if (page->evict_next)
        pages[page->evict_next].evict_prev = page->evict_prev;
    page->evict_prev = EVICT_NOT_IN_LIST;
    purgeable_page_count--;
}

void
mem_write_ramb_page(uint32_t addr, uint8_t val, page_t *page)
{
    if (page == NULL)
        return;

#    ifdef USE_DYNAREC
    if ((page->mem == NULL) || (page->mem == page_ff) || (val != page->mem[addr & 0xfff]) || codegen_in_recompile) {
#    else
    if ((page->mem == NULL) || (page->mem == page_ff) || (val != page->mem[addr & 0xfff])) {
#    endif
        uint64_t mask        = (uint64_t) 1 << ((addr >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK);
        int      byte_offset = (addr >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK;
        uint64_t byte_mask   = (uint64_t) 1 << (addr & PAGE_BYTE_MASK_MASK);

        page->mem[addr & 0xfff] = val;
        page->dirty_mask |= mask;
        if ((page->code_present_mask & mask) && !page_in_evict_list(page))
            page_add_to_evict_list(page);
        page->byte_dirty_mask[byte_offset] |= byte_mask;
        if ((page->byte_code_present_mask[byte_offset] & byte_mask) && !page_in_evict_list(page))
            page_add_to_evict_list(page);
    }
}

void
mem_write_ramw_page(uint32_t addr, uint16_t val, page_t *page)
{
    if (page == NULL)
        return;

#    ifdef USE_DYNAREC
    if ((page->mem == NULL) || (page->mem == page_ff) || (val != *(uint16_t *) &page->mem[addr & 0xfff]) || codegen_in_recompile) {
#    else
    if ((page->mem == NULL) || (page->mem == page_ff) || (val != *(uint16_t *) &page->mem[addr & 0xfff])) {
#    endif
        uint64_t mask        = (uint64_t) 1 << ((addr >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK);
        int      byte_offset = (addr >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK;
        uint64_t byte_mask   = (uint64_t) 1 << (addr & PAGE_BYTE_MASK_MASK);

        if ((addr & 0xf) == 0xf)
            mask |= (mask << 1);
        *(uint16_t *) &page->mem[addr & 0xfff] = val;
        page->dirty_mask |= mask;
        if ((page->code_present_mask & mask) && !page_in_evict_list(page))
            page_add_to_evict_list(page);
        if ((addr & PAGE_BYTE_MASK_MASK) == PAGE_BYTE_MASK_MASK) {
            page->byte_dirty_mask[byte_offset + 1] |= 1;
            if ((page->byte_code_present_mask[byte_offset + 1] & 1) && !page_in_evict_list(page))
                page_add_to_evict_list(page);
        } else
            byte_mask |= (byte_mask << 1);

        page->byte_dirty_mask[byte_offset] |= byte_mask;

        if ((page->byte_code_present_mask[byte_offset] & byte_mask) && !page_in_evict_list(page))
            page_add_to_evict_list(page);
    }
}

void
mem_write_raml_page(uint32_t addr, uint32_t val, page_t *page)
{
    if (page == NULL)
        return;

#    ifdef USE_DYNAREC
    if ((page->mem == NULL) || (page->mem == page_ff) || (val != *(uint32_t *) &page->mem[addr & 0xfff]) || codegen_in_recompile) {
#    else
    if ((page->mem == NULL) || (page->mem == page_ff) || (val != *(uint32_t *) &page->mem[addr & 0xfff])) {
#    endif
        uint64_t mask        = (uint64_t) 1 << ((addr >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK);
        int      byte_offset = (addr >> PAGE_BYTE_MASK_SHIFT) & PAGE_BYTE_MASK_OFFSET_MASK;
        uint64_t byte_mask   = (uint64_t) 0xf << (addr & PAGE_BYTE_MASK_MASK);

        if ((addr & 0xf) >= 0xd)
            mask |= (mask << 1);
        *(uint32_t *) &page->mem[addr & 0xfff] = val;
        page->dirty_mask |= mask;
        page->byte_dirty_mask[byte_offset] |= byte_mask;
        if (!page_in_evict_list(page) && ((page->code_present_mask & mask) || (page->byte_code_present_mask[byte_offset] & byte_mask)))
            page_add_to_evict_list(page);
        if ((addr & PAGE_BYTE_MASK_MASK) > (PAGE_BYTE_MASK_MASK - 3)) {
            uint32_t byte_mask_2 = 0xf >> (4 - (addr & 3));

            page->byte_dirty_mask[byte_offset + 1] |= byte_mask_2;
            if ((page->byte_code_present_mask[byte_offset + 1] & byte_mask_2) && !page_in_evict_list(page))
                page_add_to_evict_list(page);
        }
    }
}
#else
void
mem_write_ramb_page(uint32_t addr, uint8_t val, page_t *page)
{
    if (page == NULL)
        return;

#    ifdef USE_DYNAREC
    if ((page->mem == NULL) || (page->mem == page_ff) || (val != page->mem[addr & 0xfff]) || codegen_in_recompile) {
#    else
    if ((page->mem == NULL) || (page->mem == page_ff) || (val != page->mem[addr & 0xfff])) {
#    endif
        uint64_t mask = (uint64_t) 1 << ((addr >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK);
        page->dirty_mask[(addr >> PAGE_MASK_INDEX_SHIFT) & PAGE_MASK_INDEX_MASK] |= mask;
        page->mem[addr & 0xfff] = val;
    }
}

void
mem_write_ramw_page(uint32_t addr, uint16_t val, page_t *page)
{
    if (page == NULL)
        return;

#    ifdef USE_DYNAREC
    if ((page->mem == NULL) || (page->mem == page_ff) || (val != *(uint16_t *) &page->mem[addr & 0xfff]) || codegen_in_recompile) {
#    else
    if ((page->mem == NULL) || (page->mem == page_ff) || (val != *(uint16_t *) &page->mem[addr & 0xfff])) {
#    endif
        uint64_t mask = (uint64_t) 1 << ((addr >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK);
        if ((addr & 0xf) == 0xf)
            mask |= (mask << 1);
        page->dirty_mask[(addr >> PAGE_MASK_INDEX_SHIFT) & PAGE_MASK_INDEX_MASK] |= mask;
        *(uint16_t *) &page->mem[addr & 0xfff] = val;
    }
}

void
mem_write_raml_page(uint32_t addr, uint32_t val, page_t *page)
{
    if (page == NULL)
        return;

#    ifdef USE_DYNAREC
    if ((page->mem == NULL) || (page->mem == page_ff) || (val != *(uint32_t *) &page->mem[addr & 0xfff]) || codegen_in_recompile) {
#    else
    if ((page->mem == NULL) || (page->mem == page_ff) || (val != *(uint32_t *) &page->mem[addr & 0xfff])) {
#    endif
        uint64_t mask = (uint64_t) 1 << ((addr >> PAGE_MASK_SHIFT) & PAGE_MASK_MASK);
        if ((addr & 0xf) >= 0xd)
            mask |= (mask << 1);
        page->dirty_mask[(addr >> PAGE_MASK_INDEX_SHIFT) & PAGE_MASK_INDEX_MASK] |= mask;
        *(uint32_t *) &page->mem[addr & 0xfff] = val;
    }
}
#endif

void
mem_write_ram(uint32_t addr, uint8_t val, UNUSED(void *priv))
{
#ifdef ENABLE_MEM_LOG
    if ((addr >= 0xa0000) && (addr <= 0xbffff))
        mem_log("Write B       %02X to   %08X\n", val, addr);
#endif
    if (cpu_use_exec) {
        addwritelookup(mem_logical_addr, addr);
        mem_write_ramb_page(addr, val, &pages[addr >> 12]);
    } else
        ram[addr] = val;
}

void
mem_write_ramw(uint32_t addr, uint16_t val, UNUSED(void *priv))
{
#ifdef ENABLE_MEM_LOG
    if ((addr >= 0xa0000) && (addr <= 0xbffff))
        mem_log("Write W     %04X to   %08X\n", val, addr);
#endif
    if (cpu_use_exec) {
        addwritelookup(mem_logical_addr, addr);
        mem_write_ramw_page(addr, val, &pages[addr >> 12]);
    } else
        *(uint16_t *) &ram[addr] = val;
}

void
mem_write_raml(uint32_t addr, uint32_t val, UNUSED(void *priv))
{
#ifdef ENABLE_MEM_LOG
    if ((addr >= 0xa0000) && (addr <= 0xbffff))
        mem_log("Write L %08X to   %08X\n", val, addr);
#endif
    if (cpu_use_exec) {
        addwritelookup(mem_logical_addr, addr);
        mem_write_raml_page(addr, val, &pages[addr >> 12]);
    } else
        *(uint32_t *) &ram[addr] = val;
}

static uint8_t
mem_read_remapped(uint32_t addr, UNUSED(void *priv))
{
    addr = 0xA0000 + (addr - remap_start_addr);
    if (cpu_use_exec)
        addreadlookup(mem_logical_addr, addr);
    return ram[addr];
}

static uint16_t
mem_read_remappedw(uint32_t addr, UNUSED(void *priv))
{
    addr = 0xA0000 + (addr - remap_start_addr);
    if (cpu_use_exec)
        addreadlookup(mem_logical_addr, addr);
    return *(uint16_t *) &ram[addr];
}

static uint32_t
mem_read_remappedl(uint32_t addr, UNUSED(void *priv))
{
    addr = 0xA0000 + (addr - remap_start_addr);
    if (cpu_use_exec)
        addreadlookup(mem_logical_addr, addr);
    return *(uint32_t *) &ram[addr];
}

static uint8_t
mem_read_remapped2(uint32_t addr, UNUSED(void *priv))
{
    addr = 0xD0000 + (addr - remap_start_addr2);
    if (cpu_use_exec)
        addreadlookup(mem_logical_addr, addr);
    return ram[addr];
}

static uint16_t
mem_read_remappedw2(uint32_t addr, UNUSED(void *priv))
{
    addr = 0xD0000 + (addr - remap_start_addr2);
    if (cpu_use_exec)
        addreadlookup(mem_logical_addr, addr);
    return *(uint16_t *) &ram[addr];
}

static uint32_t
mem_read_remappedl2(uint32_t addr, UNUSED(void *priv))
{
    addr = 0xD0000 + (addr - remap_start_addr2);
    if (cpu_use_exec)
        addreadlookup(mem_logical_addr, addr);
    return *(uint32_t *) &ram[addr];
}

static void
mem_write_remapped(uint32_t addr, uint8_t val, UNUSED(void *priv))
{
    uint32_t oldaddr = addr;
    addr             = 0xA0000 + (addr - remap_start_addr);
    if (cpu_use_exec) {
        addwritelookup(mem_logical_addr, addr);
        mem_write_ramb_page(addr, val, &pages[oldaddr >> 12]);
    } else
        ram[addr] = val;
}

static void
mem_write_remappedw(uint32_t addr, uint16_t val, UNUSED(void *priv))
{
    uint32_t oldaddr = addr;
    addr             = 0xA0000 + (addr - remap_start_addr);
    if (cpu_use_exec) {
        addwritelookup(mem_logical_addr, addr);
        mem_write_ramw_page(addr, val, &pages[oldaddr >> 12]);
    } else
        *(uint16_t *) &ram[addr] = val;
}

static void
mem_write_remappedl(uint32_t addr, uint32_t val, UNUSED(void *priv))
{
    uint32_t oldaddr = addr;
    addr             = 0xA0000 + (addr - remap_start_addr);
    if (cpu_use_exec) {
        addwritelookup(mem_logical_addr, addr);
        mem_write_raml_page(addr, val, &pages[oldaddr >> 12]);
    } else
        *(uint32_t *) &ram[addr] = val;
}

static void
mem_write_remapped2(uint32_t addr, uint8_t val, UNUSED(void *priv))
{
    uint32_t oldaddr = addr;
    addr             = 0xD0000 + (addr - remap_start_addr2);
    if (cpu_use_exec) {
        addwritelookup(mem_logical_addr, addr);
        mem_write_ramb_page(addr, val, &pages[oldaddr >> 12]);
    } else
        ram[addr] = val;
}

static void
mem_write_remappedw2(uint32_t addr, uint16_t val, UNUSED(void *priv))
{
    uint32_t oldaddr = addr;
    addr             = 0xD0000 + (addr - remap_start_addr2);
    if (cpu_use_exec) {
        addwritelookup(mem_logical_addr, addr);
        mem_write_ramw_page(addr, val, &pages[oldaddr >> 12]);
    } else
        *(uint16_t *) &ram[addr] = val;
}

static void
mem_write_remappedl2(uint32_t addr, uint32_t val, UNUSED(void *priv))
{
    uint32_t oldaddr = addr;
    addr             = 0xD0000 + (addr - remap_start_addr2);
    if (cpu_use_exec) {
        addwritelookup(mem_logical_addr, addr);
        mem_write_raml_page(addr, val, &pages[oldaddr >> 12]);
    } else
        *(uint32_t *) &ram[addr] = val;
}

void
mem_invalidate_range(uint32_t start_addr, uint32_t end_addr)
{
#ifdef USE_NEW_DYNAREC
    page_t *page;

    start_addr &= ~PAGE_MASK_MASK;
    end_addr = (end_addr + PAGE_MASK_MASK) & ~PAGE_MASK_MASK;

    for (; start_addr <= end_addr; start_addr += 0x1000) {
        if ((start_addr >> 12) >= pages_sz)
            continue;

        page = &pages[start_addr >> 12];
        if (page) {
            page->dirty_mask = 0xffffffffffffffffULL;

            if ((page->mem != page_ff) && page->byte_dirty_mask)
                memset(page->byte_dirty_mask, 0xff, 64 * sizeof(uint64_t));

            if (!page_in_evict_list(page))
                page_add_to_evict_list(page);
        }
    }
#else
    uint32_t cur_addr;
    start_addr &= ~PAGE_MASK_MASK;
    end_addr = (end_addr + PAGE_MASK_MASK) & ~PAGE_MASK_MASK;

    for (; start_addr <= end_addr; start_addr += 0x1000) {
        /* Do nothing if the pages array is empty or DMA reads/writes to/from PCI device memory addresses
           may crash the emulator. */
        cur_addr = (start_addr >> 12);
        if (cur_addr < pages_sz)
            memset(pages[cur_addr].dirty_mask, 0xff, sizeof(pages[cur_addr].dirty_mask));
    }
#endif
}

static __inline int
mem_mapping_access_allowed(uint32_t flags, uint16_t access)
{
    int ret = 0;

    if (!(access & ACCESS_DISABLED)) {
        if (access & ACCESS_CACHE)
            ret = (flags & MEM_MAPPING_CACHE);
        else if (access & ACCESS_SMRAM)
            ret = (flags & MEM_MAPPING_SMRAM);
        else if (!(access & ACCESS_INTERNAL)) {
            if (flags & MEM_MAPPING_IS_ROM) {
                if (access & ACCESS_ROMCS)
                    ret = (flags & MEM_MAPPING_ROMCS);
                else
                    ret = !(flags & MEM_MAPPING_ROMCS);
            } else
                ret = 1;

            ret = ret && !(flags & MEM_MAPPING_INTERNAL) && !(flags & MEM_MAPPING_SMRAM);
        } else
            ret = !(flags & MEM_MAPPING_EXTERNAL) && !(flags & MEM_MAPPING_SMRAM);
    } else {
        /* Still allow SMRAM if access is DISABLED but also has CACHE and/or SMRAM flags set. */
        if (access & ACCESS_CACHE)
            ret = (flags & MEM_MAPPING_CACHE);
        else if (access & ACCESS_SMRAM)
            ret = (flags & MEM_MAPPING_SMRAM);
    }

    return ret;
}

void
mem_mapping_recalc(uint64_t base, uint64_t size)
{
    mem_mapping_t *map;
    int            n;
    uint64_t       c;
    uint8_t        wp;

    if (!size || (base_mapping == NULL))
        return;

    map = base_mapping;

    /* Clear out old mappings. */
    for (c = base; c < base + size; c += MEM_GRANULARITY_SIZE) {
        _mem_exec[c >> MEM_GRANULARITY_BITS]         = NULL;
        write_mapping[c >> MEM_GRANULARITY_BITS]     = NULL;
        read_mapping[c >> MEM_GRANULARITY_BITS]      = NULL;
        write_mapping_bus[c >> MEM_GRANULARITY_BITS] = NULL;
        read_mapping_bus[c >> MEM_GRANULARITY_BITS]  = NULL;
    }

    /* Walk mapping list. */
    while (map != NULL) {
        /* In range? */
        if (map->enable && (uint64_t) map->base < ((uint64_t) base + (uint64_t) size) &&
            ((uint64_t) map->base + (uint64_t) map->size) > (uint64_t) base) {
            uint64_t i_a   = ((~map->base_ignore) & 0xffffffffULL) + 0x00000001ULL;
            uint64_t i_s   = 0x00000000ULL;
            uint64_t i_e   = map->base_ignore;
            uint64_t i_c   = 0x00000000ULL;
            uint64_t start = (map->base < base) ? map->base : base;
            uint64_t end   = (((uint64_t) map->base + (uint64_t) map->size) < (base + size)) ?
                             ((uint64_t) map->base + (uint64_t) map->size) : (base + size);
            if (start < map->base)
                start = map->base;

            for (i_c = i_s; i_c <= i_e; i_c += i_a) {
                for (c = (start + i_c); c < (end + i_c); c += MEM_GRANULARITY_SIZE) {
                    /* CPU */
                    n = !!in_smm;
                    wp = _mem_wp[c >> MEM_GRANULARITY_BITS];

                    if (map->exec && mem_mapping_access_allowed(map->flags,
                                                                _mem_state[c >> MEM_GRANULARITY_BITS].states[n].x))
                        _mem_exec[c >> MEM_GRANULARITY_BITS] = map->exec + (c - map->base);
                    if (!wp && (map->write_b || map->write_w || map->write_l) &&
                        mem_mapping_access_allowed(map->flags,
                                                   _mem_state[c >> MEM_GRANULARITY_BITS].states[n].w))
                        write_mapping[c >> MEM_GRANULARITY_BITS] = map;
                    if ((map->read_b || map->read_w || map->read_l) &&
                        mem_mapping_access_allowed(map->flags,
                                                   _mem_state[c >> MEM_GRANULARITY_BITS].states[n].r))
                        read_mapping[c >> MEM_GRANULARITY_BITS] = map;

                    /* Bus */
                    n |= STATE_BUS;
                    wp = _mem_wp_bus[c >> MEM_GRANULARITY_BITS];

                    if (!wp && (map->write_b || map->write_w || map->write_l) &&
                        mem_mapping_access_allowed(map->flags,
                                                   _mem_state[c >> MEM_GRANULARITY_BITS].states[n].w))
                        write_mapping_bus[c >> MEM_GRANULARITY_BITS] = map;
                    if ((map->read_b || map->read_w || map->read_l) &&
                        mem_mapping_access_allowed(map->flags,
                                                   _mem_state[c >> MEM_GRANULARITY_BITS].states[n].r))
                        read_mapping_bus[c >> MEM_GRANULARITY_BITS] = map;
                }
            }
        }
        map = map->next;
    }

    flushmmucache_nopc();

#ifdef ENABLE_MEM_LOG
    pclog("\nMemory map:\n");
    mem_mapping_t *write = (mem_mapping_t *) -1, *read = (mem_mapping_t *) -1, *write_bus = (mem_mapping_t *) -1, *read_bus = (mem_mapping_t *) -1;
    for (c = 0; c < (sizeof(write_mapping) / sizeof(write_mapping[0])); c++) {
        if ((write_mapping[c] == write) && (read_mapping[c] == read) && (write_mapping_bus[c] == write_bus) && (read_mapping_bus[c] == read_bus))
            continue;
        write = write_mapping[c];
        read = read_mapping[c];
        write_bus = write_mapping_bus[c];
        read_bus = read_mapping_bus[c];

        pclog("%08X | ", c << MEM_GRANULARITY_BITS);
        if (read) {
            pclog("R%c%c%c %08X+% 8X",
                read->read_b ? 'b' : ' ', read->read_w ? 'w' : ' ', read->read_l ? 'l' : ' ',
                read->base, read->size);
        } else {
            pclog("                      ");
        }
        if (write) {
            pclog(" | W%c%c%c %08X+% 8X",
                write->write_b ? 'b' : ' ', write->write_w ? 'w' : ' ', write->write_l ? 'l' : ' ',
                write->base, write->size);
        } else {
            pclog(" |                       ");
        }
        pclog(" | %c\n", _mem_exec[c] ? 'X' : ' ');

        if ((write != write_bus) || (read != read_bus)) {
            pclog("   ^ bus | ");
            if (read_bus) {
                pclog("R%c%c%c %08X+% 8X",
                    read_bus->read_b ? 'b' : ' ', read_bus->read_w ? 'w' : ' ', read_bus->read_l ? 'l' : ' ',
                    read_bus->base, read_bus->size);
            } else {
                pclog("                      ");
            }
            if (write_bus) {
                pclog(" | W%c%c%c %08X+% 8X",
                    write_bus->write_b ? 'b' : ' ', write_bus->write_w ? 'w' : ' ', write_bus->write_l ? 'l' : ' ',
                    write_bus->base, write_bus->size);
            } else {
                pclog(" |                       ");
            }
            pclog(" |\n");
        }
    }
    pclog("\n");
#endif
}

void
mem_set_wp(uint64_t base, uint64_t size, uint8_t flags, uint8_t wp)
{
    uint64_t       c;
    uint64_t       end = base + size;

    for (c = base; c < end; c += MEM_GRANULARITY_SIZE) {
        if (flags & ACCESS_BUS)
            _mem_wp_bus[c >> MEM_GRANULARITY_BITS] = wp;
        if (flags & ACCESS_CPU)
            _mem_wp[c >> MEM_GRANULARITY_BITS] = wp;
    }

    mem_mapping_recalc(base, size);
}

void
mem_mapping_set(mem_mapping_t *map,
                uint32_t       base,
                uint32_t       size,
                uint8_t (*read_b)(uint32_t addr, void *priv),
                uint16_t (*read_w)(uint32_t addr, void *priv),
                uint32_t (*read_l)(uint32_t addr, void *priv),
                void (*write_b)(uint32_t addr, uint8_t val, void *priv),
                void (*write_w)(uint32_t addr, uint16_t val, void *priv),
                void (*write_l)(uint32_t addr, uint32_t val, void *priv),
                uint8_t *exec,
                uint32_t fl,
                void    *priv)
{
    if (size != 0x00000000)
        map->enable = 1;
    else
        map->enable = 0;
    map->base    = base;
    map->size    = size;
    map->mask    = (map->size ? 0xffffffff : 0x00000000);
    map->read_b  = read_b;
    map->read_w  = read_w;
    map->read_l  = read_l;
    map->write_b = write_b;
    map->write_w = write_w;
    map->write_l = write_l;
    map->exec    = exec;
    map->flags   = fl;
    map->priv    = priv;
    map->next    = NULL;
    mem_log("mem_mapping_add(): Linked list structure: %08X -> %08X -> %08X\n", map->prev, map, map->next);

    /* If the mapping is disabled, there is no need to recalc anything. */
    if (size != 0x00000000)
        mem_mapping_recalc(map->base, map->size);
}

void
mem_mapping_add(mem_mapping_t *map,
                uint32_t       base,
                uint32_t       size,
                uint8_t (*read_b)(uint32_t addr, void *priv),
                uint16_t (*read_w)(uint32_t addr, void *priv),
                uint32_t (*read_l)(uint32_t addr, void *priv),
                void (*write_b)(uint32_t addr, uint8_t val, void *priv),
                void (*write_w)(uint32_t addr, uint16_t val, void *priv),
                void (*write_l)(uint32_t addr, uint32_t val, void *priv),
                uint8_t *exec,
                uint32_t fl,
                void    *priv)
{
    /* Do a sanity check */
    if ((base_mapping == NULL) && (last_mapping != NULL)) {
        fatal("mem_mapping_add(): NULL base mapping with non-NULL last mapping\n");
        return;
    } else if ((base_mapping != NULL) && (last_mapping == NULL)) {
        fatal("mem_mapping_add(): Non-NULL base mapping with NULL last mapping\n");
        return;
    } else if ((base_mapping != NULL) && (base_mapping->prev != NULL)) {
        fatal("mem_mapping_add(): Base mapping with a preceding mapping\n");
        return;
    } else if ((last_mapping != NULL) && (last_mapping->next != NULL)) {
        fatal("mem_mapping_add(): Last mapping with a following mapping\n");
        return;
    }

    /* Add mapping to the beginning of the list if necessary.*/
    if (base_mapping == NULL)
        base_mapping = map;

    /* Add mapping to the end of the list.*/
    if (last_mapping == NULL)
        map->prev = NULL;
    else {
        map->prev          = last_mapping;
        last_mapping->next = map;
    }
    last_mapping = map;

    mem_mapping_set(map, base, size, read_b, read_w, read_l,
                    write_b, write_w, write_l, exec, fl, priv);
}

void
mem_mapping_do_recalc(mem_mapping_t *map)
{
    mem_mapping_recalc(map->base, map->size);
}

void
mem_mapping_set_handler(mem_mapping_t *map,
                        uint8_t (*read_b)(uint32_t addr, void *priv),
                        uint16_t (*read_w)(uint32_t addr, void *priv),
                        uint32_t (*read_l)(uint32_t addr, void *priv),
                        void (*write_b)(uint32_t addr, uint8_t val, void *priv),
                        void (*write_w)(uint32_t addr, uint16_t val, void *priv),
                        void (*write_l)(uint32_t addr, uint32_t val, void *priv))
{
    map->read_b  = read_b;
    map->read_w  = read_w;
    map->read_l  = read_l;
    map->write_b = write_b;
    map->write_w = write_w;
    map->write_l = write_l;

    mem_mapping_recalc(map->base, map->size);
}

void
mem_mapping_set_write_handler(mem_mapping_t *map,
                              void (*write_b)(uint32_t addr, uint8_t val, void *priv),
                              void (*write_w)(uint32_t addr, uint16_t val, void *priv),
                              void (*write_l)(uint32_t addr, uint32_t val, void *priv))
{
    map->write_b = write_b;
    map->write_w = write_w;
    map->write_l = write_l;

    mem_mapping_recalc(map->base, map->size);
}

void
mem_mapping_set_addr(mem_mapping_t *map, uint32_t base, uint32_t size)
{
    /* Remove old mapping. */
    map->enable = 0;
    mem_mapping_recalc(map->base, map->size);

    /* Set new mapping. */
    map->enable = 1;
    map->base   = base;
    map->size   = size;

    mem_mapping_recalc(map->base, map->size);
}

void
mem_mapping_set_base_ignore(mem_mapping_t *map, uint32_t base_ignore)
{
    /* Remove old mapping. */
    map->enable      = 0;
    mem_mapping_recalc(map->base, map->size);

    /* Set new mapping. */
    map->enable      = 1;
    map->base_ignore = base_ignore;

    mem_mapping_recalc(map->base, map->size);
}

void
mem_mapping_set_exec(mem_mapping_t *map, uint8_t *exec)
{
    map->exec = exec;

    mem_mapping_recalc(map->base, map->size);
}

void
mem_mapping_set_mask(mem_mapping_t *map, uint32_t mask)
{
    map->mask = mask;

    mem_mapping_recalc(map->base, map->size);
}

void
mem_mapping_set_p(mem_mapping_t *map, void *priv)
{
    map->priv = priv;
}

void
mem_mapping_disable(mem_mapping_t *map)
{
    map->enable = 0;

    mem_mapping_recalc(map->base, map->size);
}

void
mem_mapping_enable(mem_mapping_t *map)
{
    map->enable = 1;

    mem_mapping_recalc(map->base, map->size);
}

void
mem_set_access(uint8_t bitmap, int mode, uint32_t base, uint32_t size, uint16_t access)
{
    uint16_t       mask;
    uint16_t       smstate = 0x0000;
    const uint16_t smstates[4] = { 0x0000, (MEM_READ_SMRAM | MEM_WRITE_SMRAM),
                                   MEM_READ_SMRAM_EX, (MEM_READ_DISABLED_EX | MEM_WRITE_DISABLED_EX) };

    if (mode)
        mask = 0x2d6b;
    else
        mask = 0x1084;

    if (mode) {
        if (mode == 1)
            access = !!access;

        if (mode == 3) {
            if (access & ACCESS_SMRAM_X)
                smstate |= MEM_EXEC_SMRAM;
            if (access & ACCESS_SMRAM_R)
                smstate |= MEM_READ_SMRAM_2;
            if (access & ACCESS_SMRAM_W)
                smstate |= MEM_WRITE_SMRAM;
        } else
            smstate = smstates[access & 0x07];
    } else
        smstate = access & 0x6f7b;

    for (uint32_t c = 0; c < size; c += MEM_GRANULARITY_SIZE) {
        for (uint8_t i = 0; i < 4; i++) {
            if (bitmap & (1 << i)) {
                _mem_state[(c + base) >> MEM_GRANULARITY_BITS].vals[i] = (_mem_state[(c + base) >> MEM_GRANULARITY_BITS].vals[i] & mask) | smstate;
            }
        }

#ifdef ENABLE_MEM_LOG
        if (((c + base) >= 0xa0000) && ((c + base) <= 0xbffff)) {
            mem_log("Set mem state for block at %08X to %04X with bitmap %02X\n",
                    c + base, smstate, bitmap);
        }
#endif
    }

    mem_mapping_recalc(base, size);
}

void
mem_a20_init(void)
{
    if (is286) {
        mem_a20_key = mem_a20_alt = mem_a20_state = 0;
        rammask = cpu_16bitbus ? 0xffffff : 0xffffffff;
        if (is6117)
            rammask |= 0x03000000;
        flushmmucache();
#if 0
        mem_a20_state = mem_a20_key | mem_a20_alt;
#endif
    } else {
        rammask = 0xfffff;
        flushmmucache();
        mem_a20_key = mem_a20_alt = mem_a20_state = 0;
    }
}

/* Close all the memory mappings. */
void
mem_close(void)
{
    mem_mapping_t *map = base_mapping;
    mem_mapping_t *next;

    while (map != NULL) {
        next      = map->next;
        map->prev = map->next = NULL;
        map                   = next;
    }

    base_mapping = last_mapping = 0;
}

static void
mem_add_ram_mapping(mem_mapping_t *mapping, uint32_t base, uint32_t size)
{
    mem_mapping_add(mapping, base, size,
                    mem_read_ram, mem_read_ramw, mem_read_raml,
                    mem_write_ram, mem_write_ramw, mem_write_raml,
                    ram + base, MEM_MAPPING_INTERNAL, NULL);
}

static void
mem_init_ram_mapping(mem_mapping_t *mapping, uint32_t base, uint32_t size)
{
    mem_set_mem_state_both(base, size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
    mem_add_ram_mapping(mapping, base, size);
}

/* Reset the memory state. */
void
mem_reset(void)
{
    size_t m;

    memset(page_ff, 0xff, sizeof(page_ff));

#ifdef USE_NEW_DYNAREC
    if (byte_dirty_mask) {
        free(byte_dirty_mask);
        byte_dirty_mask = NULL;
    }

    if (byte_code_present_mask) {
        free(byte_code_present_mask);
        byte_code_present_mask = NULL;
    }
#endif

    /* Free the old pages array, if necessary. */
    if (pages) {
        free(pages);
        pages = NULL;
    }

    if (ram != NULL) {
        plat_munmap(ram, ram_size);
        ram      = NULL;
        ram_size = 0;
    }
#if (!(defined __amd64__ || defined _M_X64 || defined __aarch64__ || defined _M_ARM64))
    if (ram2 != NULL) {
        plat_munmap(ram2, ram2_size);
        ram2      = NULL;
        ram2_size = 0;
    }

    if (mem_size > 2097152)
        mem_size = 2097152;
#endif

    m = 1024UL * (size_t) mem_size;

#if (!(defined __amd64__ || defined _M_X64 || defined __aarch64__ || defined _M_ARM64))
    if (mem_size > 1048576) {
        ram_size = 1 << 30;
        ram      = (uint8_t *) plat_mmap(ram_size, 0); /* allocate and clear the RAM block of the first 1 GB */
        if (ram == NULL) {
            fatal("Failed to allocate primary RAM block. Make sure you have enough RAM available.\n");
            return;
        }
        memset(ram, 0x00, ram_size);
        ram2_size = m - (1 << 30);
        /* Allocate 16 extra bytes of RAM to mitigate some dynarec recompiler memory access quirks. */
        ram2      = (uint8_t *) plat_mmap(ram2_size + 16, 0); /* allocate and clear the RAM block above 1 GB */
        if (ram2 == NULL) {
            if (config_changed == 2)
                fatal(EMU_NAME " must be restarted for the memory amount change to be applied.\n");
            else
                fatal("Failed to allocate secondary RAM block. Make sure you have enough RAM available.\n");
            return;
        }
        memset(ram2, 0x00, ram2_size + 16);
    } else
#endif
    {
        ram_size = m;
        /* Allocate 16 extra bytes of RAM to mitigate some dynarec recompiler memory access quirks. */
        ram      = (uint8_t *) plat_mmap(ram_size + 16, 0); /* allocate and clear the RAM block */
        if (ram == NULL) {
            fatal("Failed to allocate RAM block. Make sure you have enough RAM available.\n");
            return;
        }
        memset(ram, 0x00, ram_size + 16);
        if (mem_size > 1048576)
            ram2 = &(ram[1 << 30]);
    }

    /*
     * Allocate the page table based on how much RAM we have.
     * We re-allocate the table on each (hard) reset, as the
     * memory amount could have changed.
     */
    if (is286) {
        if (cpu_16bitbus) {
            /* 80286/386SX; maximum address space is 16MB + 16 MB for EMS. */
            m = 8192;
            /* ALi M6117; maximum address space is 64MB. */
            if (is6117)
                m <<= 2;
        } else {
            /* 80386DX+; maximum address space is 4GB. */
            m = 1048576;
        }
    } else {
        /* 8088/86; maximum address space is 1MB. */
        m = 256;
    }

    addr_space_size = m;

    /*
     * Allocate and initialize the (new) page table.
     */
    pages_sz = m;
    pages    = (page_t *) malloc(m * sizeof(page_t));

    memset(page_lookup, 0x00, (1 << 20) * sizeof(page_t *));
    memset(page_lookupp, 0x04, (1 << 20) * sizeof(uint8_t));

    memset(pages, 0x00, pages_sz * sizeof(page_t));

#ifdef USE_NEW_DYNAREC
    byte_dirty_mask = malloc((mem_size * 1024) / 8);
    memset(byte_dirty_mask, 0, (mem_size * 1024) / 8);

    byte_code_present_mask = malloc((mem_size * 1024) / 8);
    memset(byte_code_present_mask, 0, (mem_size * 1024) / 8);
#endif

    for (uint32_t c = 0; c < pages_sz; c++) {
        if ((c << 12) >= (mem_size << 10))
            pages[c].mem = page_ff;
        else {
#if (!(defined __amd64__ || defined _M_X64 || defined __aarch64__ || defined _M_ARM64))
            if (mem_size > 1048576) {
                if ((c << 12) < (1 << 30))
                    pages[c].mem = &ram[c << 12];
                else
                    pages[c].mem = &ram2[(c << 12) - (1 << 30)];
            } else
                pages[c].mem = &ram[c << 12];
#else
            pages[c].mem = &ram[c << 12];
#endif
        }
        if (c < m) {
            pages[c].write_b = mem_write_ramb_page;
            pages[c].write_w = mem_write_ramw_page;
            pages[c].write_l = mem_write_raml_page;
        }
#ifdef USE_NEW_DYNAREC
        pages[c].evict_prev             = EVICT_NOT_IN_LIST;
        pages[c].byte_dirty_mask        = &byte_dirty_mask[c * 64];
        pages[c].byte_code_present_mask = &byte_code_present_mask[c * 64];
#endif
    }

    memset(_mem_exec, 0x00, sizeof(_mem_exec));
    memset(_mem_wp, 0x00, sizeof(_mem_wp));
    memset(_mem_wp_bus, 0x00, sizeof(_mem_wp_bus));
    memset(write_mapping, 0x00, sizeof(write_mapping));
    memset(read_mapping, 0x00, sizeof(read_mapping));
    memset(write_mapping_bus, 0x00, sizeof(write_mapping_bus));
    memset(read_mapping_bus, 0x00, sizeof(read_mapping_bus));

    base_mapping = last_mapping = NULL;

    /* Set the entire memory space as external. */
    memset(_mem_state, 0x00, sizeof(_mem_state));

    /* Set the low RAM space as internal. */
    mem_init_ram_mapping(&ram_low_mapping, 0x000000, (mem_size > 640) ? 0xa0000 : mem_size * 1024);

    if (mem_size > 1024) {
        if (cpu_16bitbus && !is6117 && mem_size > 16256)
            mem_init_ram_mapping(&ram_high_mapping, 0x100000, (16256 - 1024) * 1024);
        else if (cpu_16bitbus && is6117 && mem_size > 65408)
            mem_init_ram_mapping(&ram_high_mapping, 0x100000, (65408 - 1024) * 1024);
        else {
            if (mem_size > 1048576) {
                mem_init_ram_mapping(&ram_high_mapping, 0x100000, (1048576 - 1024) * 1024);

                mem_set_mem_state_both((1 << 30), (mem_size - 1048576) * 1024,
                                       MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                mem_mapping_add(&ram_2gb_mapping, (1 << 30),
                                ((mem_size - 1048576) * 1024),
                                mem_read_ram_2gb, mem_read_ram_2gbw, mem_read_ram_2gbl,
                                mem_write_ram, mem_write_ramw, mem_write_raml,
                                ram2, MEM_MAPPING_INTERNAL, NULL);
            } else
                mem_init_ram_mapping(&ram_high_mapping, 0x100000, (mem_size - 1024) * 1024);
        }
    }

    if (mem_size > 768) {
        mem_add_ram_mapping(&ram_mid_mapping, 0xa0000, 0x60000);

        mem_add_ram_mapping(&ram_mid_mapping2, 0xa0000, 0x60000);
        mem_mapping_disable(&ram_mid_mapping2);
    }

    mem_mapping_add(&ram_remapped_mapping, mem_size * 1024, 256 * 1024,
                    mem_read_remapped, mem_read_remappedw, mem_read_remappedl,
                    mem_write_remapped, mem_write_remappedw, mem_write_remappedl,
                    ram + 0xa0000, MEM_MAPPING_INTERNAL, NULL);
    mem_mapping_disable(&ram_remapped_mapping);

    /* Mapping for SiS 471 relocation which relocates A0000-BFFFF, D0000-EFFFF, which is non-contiguous. */
    mem_mapping_add(&ram_remapped_mapping2, mem_size * 1024, 256 * 1024,
                    mem_read_remapped2, mem_read_remappedw2, mem_read_remappedl2,
                    mem_write_remapped2, mem_write_remappedw2, mem_write_remappedl2,
                    ram + 0xd0000, MEM_MAPPING_INTERNAL, NULL);
    mem_mapping_disable(&ram_remapped_mapping2);

    mem_a20_init();

#ifdef USE_NEW_DYNAREC
    purgable_page_list_head = 0;
    purgeable_page_count    = 0;
#endif
}

void
mem_init(void)
{
    /* Perform a one-time init. */
    ram = rom = NULL;
    ram2      = NULL;
    pages     = NULL;

    /* Allocate the lookup tables. */
    page_lookup  = (page_t **) malloc((1 << 20) * sizeof(page_t *));
    page_lookupp = (uint8_t *) malloc((1 << 20) * sizeof(uint8_t));
    readlookup2  = malloc((1 << 20) * sizeof(uintptr_t));
    readlookupp  = malloc((1 << 20) * sizeof(uint8_t));
    writelookup2 = malloc((1 << 20) * sizeof(uintptr_t));
    writelookupp = malloc((1 << 20) * sizeof(uint8_t));
}

static void
umc_page_recalc(uint32_t c, int set)
{
    if (set) {
        pages[c].mem = &ram[(c & 0xff) << 12];
        pages[c].write_b = mem_write_ramb_page;
        pages[c].write_w = mem_write_ramw_page;
        pages[c].write_l = mem_write_raml_page;
    } else {
        pages[c].mem = page_ff;
        pages[c].write_b = NULL;
        pages[c].write_w = NULL;
        pages[c].write_l = NULL;
    }

#ifdef USE_NEW_DYNAREC
    pages[c].evict_prev             = EVICT_NOT_IN_LIST;
    pages[c].byte_dirty_mask        = &byte_dirty_mask[(c & 0xff) * 64];
    pages[c].byte_code_present_mask = &byte_code_present_mask[(c & 0xff) * 64];
#endif
}

void
umc_smram_recalc(uint32_t start, int set)
{
    for (uint32_t c = start; c < (start + 0x0020); c++)
        umc_page_recalc(c, set);
}

static void
mem_remap_top_ex_common(int kb, uint32_t start, int mid)
{
    uint32_t   c;
    int        offset;
    int        size = mem_size - 640;
    int        set        = 1;
    static int old_kb     = 0;
    int        sis_mode   = 0;
    uint32_t   start_addr = 0;
    uint32_t   addr = 0;

    mem_log("MEM: remapping top %iKB (mem=%i)\n", kb, mem_size);
    if (mem_size <= 640)
        return;

    /* SiS 471 special mode. */
    if (kb == -256) {
        kb       = 256;
        sis_mode = 1;
    }

    /* Do not remap if we're have more than (16 MB - RAM) memory. */
    if ((kb != 0) && (mem_size >= (16384 - kb)))
        return;

    if (kb == 0) {
        kb  = old_kb;
        set = 0;
    } else
        old_kb = kb;

    if (size > kb)
        size = kb;

    remap_start_addr  = start << 10;
    remap_start_addr2 = (start << 10) + 0x00020000;

    for (c = ((start * 1024) >> 12); c < (((start + size) * 1024) >> 12); c++) {
        offset = c - ((start * 1024) >> 12);
        /* Use A0000-BFFFF, D0000-EFFFF instead of C0000-DFFFF, E0000-FFFFF. */
        addr = 0xa0000 + (offset << 12);
        if (sis_mode) {
            /* A0000-DFFFF -> A0000-BFFFF, D0000-EFFFF */
            if (addr >= 0x000c0000)
                addr += 0x00010000;
        }
        if (start_addr == 0)
            start_addr = addr;
        pages[c].mem     = set ? &ram[addr] : page_ff;
        pages[c].write_b = set ? mem_write_ramb_page : NULL;
        pages[c].write_w = set ? mem_write_ramw_page : NULL;
        pages[c].write_l = set ? mem_write_raml_page : NULL;
#ifdef USE_NEW_DYNAREC
        pages[c].evict_prev             = EVICT_NOT_IN_LIST;
        pages[c].byte_dirty_mask        = &byte_dirty_mask[(addr >> 12) * 64];
        pages[c].byte_code_present_mask = &byte_code_present_mask[(addr >> 12) * 64];
#endif
    }

    mem_set_mem_state_both(start * 1024, size * 1024, set ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL));

    for (c = 0xa0; c < 0xf0; c++) {
        if ((c >= 0xc0) && (c <= 0xcf))
            continue;

        if (sis_mode || ((c << 12) >= (mem_size << 10)))
            pages[c].mem = page_ff;
        else {
#if (!(defined __amd64__ || defined _M_X64 || defined __aarch64__ || defined _M_ARM64))
            if (mem_size > 1048576) {
                if ((c << 12) < (1 << 30))
                    pages[c].mem = &ram[c << 12];
                else
                    pages[c].mem = &ram2[(c << 12) - (1 << 30)];
            } else
                pages[c].mem = &ram[c << 12];
#else
            pages[c].mem = &ram[c << 12];
#endif
        }
        if (!sis_mode && (c < addr_space_size)) {
            pages[c].write_b = mem_write_ramb_page;
            pages[c].write_w = mem_write_ramw_page;
            pages[c].write_l = mem_write_raml_page;
        } else {
            pages[c].write_b = NULL;
            pages[c].write_w = NULL;
            pages[c].write_l = NULL;
        }
#ifdef USE_NEW_DYNAREC
        pages[c].evict_prev             = EVICT_NOT_IN_LIST;
        pages[c].byte_dirty_mask        = &byte_dirty_mask[c * 64];
        pages[c].byte_code_present_mask = &byte_code_present_mask[c * 64];
#endif
    }

    if (set) {
        if (sis_mode) {
            mem_mapping_set_addr(&ram_remapped_mapping, start * 1024, 0x00020000);
            mem_mapping_set_exec(&ram_remapped_mapping, ram + 0x000a0000);
            mem_mapping_set_addr(&ram_remapped_mapping2, (start * 1024) + 0x00020000, 0x00020000);
            mem_mapping_set_exec(&ram_remapped_mapping2, ram + 0x000d0000);

            if (mid) {
                mem_mapping_set_addr(&ram_mid_mapping, 0x000c0000, 0x00010000);
                mem_mapping_set_exec(&ram_mid_mapping, ram + 0x000c0000);
                mem_mapping_set_addr(&ram_mid_mapping2, 0x000f0000, 0x00010000);
                mem_mapping_set_exec(&ram_mid_mapping2, ram + 0x000f0000);
            }
        } else {
            mem_mapping_set_addr(&ram_remapped_mapping, start * 1024, size * 1024);
            mem_mapping_set_exec(&ram_remapped_mapping, ram + start_addr);
            mem_mapping_disable(&ram_remapped_mapping2);

            if (mid) {
                mem_mapping_set_addr(&ram_mid_mapping, 0x000a0000, 0x00060000);
                mem_mapping_set_exec(&ram_mid_mapping, ram + 0x000a0000);
                mem_mapping_disable(&ram_mid_mapping2);
            }
        }
    } else {
        mem_mapping_disable(&ram_remapped_mapping);
        mem_mapping_disable(&ram_remapped_mapping2);

        if (mid) {
            mem_mapping_set_addr(&ram_mid_mapping, 0x000a0000, 0x00060000);
            mem_mapping_set_exec(&ram_mid_mapping, ram + 0x000a0000);
            mem_mapping_disable(&ram_mid_mapping2);
        }
    }

    flushmmucache();
}

void
mem_remap_top_ex(int kb, uint32_t start)
{
    mem_remap_top_ex_common(kb, start, 1);
}

void
mem_remap_top_ex_nomid(int kb, uint32_t start)
{
    mem_remap_top_ex_common(kb, start, 0);
}

void
mem_remap_top(int kb)
{
    mem_remap_top_ex(kb, (mem_size >= 1024) ? mem_size : 1024);
}

void
mem_remap_top_nomid(int kb)
{
    mem_remap_top_ex_nomid(kb, (mem_size >= 1024) ? mem_size : 1024);
}

void
mem_reset_page_blocks(void)
{
    if (pages == NULL)
        return;

    for (uint32_t c = 0; c < pages_sz; c++) {
        pages[c].write_b = mem_write_ramb_page;
        pages[c].write_w = mem_write_ramw_page;
        pages[c].write_l = mem_write_raml_page;
#ifdef USE_NEW_DYNAREC
        pages[c].block   = BLOCK_INVALID;
        pages[c].block_2 = BLOCK_INVALID;
        pages[c].head    = BLOCK_INVALID;
#else
        pages[c].block[0] = pages[c].block[1] = pages[c].block[2] = pages[c].block[3] = NULL;
        pages[c].block_2[0] = pages[c].block_2[1] = pages[c].block_2[2] = pages[c].block_2[3] = NULL;
        pages[c].head                                                                         = NULL;
#endif
    }
}

void
mem_a20_recalc(void)
{
    int state;

    if (!is286) {
        rammask = 0xfffff;
        flushmmucache();
        mem_a20_key = mem_a20_alt = mem_a20_state = 0;

        return;
    }

    state = mem_a20_key | mem_a20_alt;
    if (state && !mem_a20_state) {
        rammask = cpu_16bitbus ? 0xffffff : 0xffffffff;
        if (is6117)
            rammask |= 0x03000000;
        flushmmucache();
    } else if (!state && mem_a20_state) {
        rammask = cpu_16bitbus ? 0xefffff : 0xffefffff;
        if (is6117)
            rammask |= 0x03000000;
        flushmmucache();
    }

    mem_a20_state = state;
}
