/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the memory interface.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2017-2020 Fred N. van Kempen.
 *          Copyright 2016-2020 Miran Grca.
 */

#ifndef EMU_MEM_H
#define EMU_MEM_H

#define MEM_MAP_TO_SHADOW_RAM_MASK 1
#define MEM_MAP_TO_RAM_ADDR_MASK   2

#define STATE_CPU                  0
#define STATE_BUS                  2

#define ACCESS_CPU                 1  /* Update CPU non-SMM access. */
#define ACCESS_CPU_SMM             2  /* Update CPU SMM access. */
#define ACCESS_BUS                 4  /* Update bus access. */
#define ACCESS_BUS_SMM             8  /* Update bus SMM access. */
#define ACCESS_NORMAL              5  /* Update CPU and bus non-SMM accesses. */
#define ACCESS_SMM                 10 /* Update CPU and bus SMM accesses. */
#define ACCESS_CPU_BOTH            3  /* Update CPU non-SMM and SMM accesses. */
#define ACCESS_BUS_BOTH            12 /* Update bus non-SMM and SMM accesses. */
#define ACCESS_ALL                 15 /* Update all accesses. */

#define ACCESS_INTERNAL            1
#define ACCESS_ROMCS               2
#define ACCESS_SMRAM               4
#define ACCESS_CACHE               8
#define ACCESS_DISABLED            16

#define ACCESS_X_INTERNAL          1
#define ACCESS_X_ROMCS             2
#define ACCESS_X_SMRAM             4
#define ACCESS_X_CACHE             8
#define ACCESS_X_DISABLED          16
#define ACCESS_W_INTERNAL          32
#define ACCESS_W_ROMCS             64
#define ACCESS_W_SMRAM             128
#define ACCESS_W_CACHE             256
#define ACCESS_W_DISABLED          512
#define ACCESS_R_INTERNAL          1024
#define ACCESS_R_ROMCS             2048
#define ACCESS_R_SMRAM             4096
#define ACCESS_R_CACHE             8192
#define ACCESS_R_DISABLED          16384

#define ACCESS_EXECUTE             0
#define ACCESS_READ                1
#define ACCESS_WRITE               2

#define ACCESS_SMRAM_OFF           0
#define ACCESS_SMRAM_X             1
#define ACCESS_SMRAM_W             2
#define ACCESS_SMRAM_WX            3
#define ACCESS_SMRAM_R             4
#define ACCESS_SMRAM_RX            5
#define ACCESS_SMRAM_RW            6
#define ACCESS_SMRAM_RWX           7

/* Conversion #define's - we need these to seamlessly convert the old mem_set_mem_state() calls to
   the new stuff in order to make this a drop in replacement.

   Read here includes execute access since the old code also used read access for execute access,
   with some exceptions. */

#define MEM_READ_DISABLED (ACCESS_X_DISABLED | ACCESS_R_DISABLED)
#define MEM_READ_INTERNAL (ACCESS_X_INTERNAL | ACCESS_R_INTERNAL)
#define MEM_READ_EXTERNAL 0
/* These two are going to be identical - on real hardware, chips that don't care about ROMCS#,
   are not magically disabled. */
#define MEM_READ_ROMCS  (ACCESS_X_ROMCS | ACCESS_R_ROMCS)
#define MEM_READ_EXTANY MEM_READ_ROMCS
/* Internal execute access, external read access. */
#define MEM_READ_EXTERNAL_EX 0
#define MEM_READ_SMRAM       (ACCESS_X_SMRAM | ACCESS_R_SMRAM)
#define MEM_READ_CACHE       (ACCESS_X_CACHE | ACCESS_R_CACHE)
#define MEM_READ_SMRAM_EX    (ACCESS_X_SMRAM)
#define MEM_EXEC_SMRAM       MEM_READ_SMRAM_EX
#define MEM_READ_SMRAM_2     (ACCESS_R_SMRAM)
/* Theese two are going to be identical. */
#define MEM_READ_DISABLED_EX MEM_READ_DISABLED
#define MEM_READ_MASK        0x7c1f

#define MEM_WRITE_DISABLED   (ACCESS_W_DISABLED)
#define MEM_WRITE_INTERNAL   (ACCESS_W_INTERNAL)
#define MEM_WRITE_EXTERNAL   0
/* These two are going to be identical - on real hardware, chips that don't care about ROMCS#,
   are not magically disabled. */
#define MEM_WRITE_ROMCS  (ACCESS_W_ROMCS)
#define MEM_WRITE_EXTANY (ACCESS_W_ROMCS)
#define MEM_WRITE_SMRAM  (ACCESS_W_SMRAM)
#define MEM_WRITE_CACHE  (ACCESS_W_CACHE)
/* Theese two are going to be identical. */
#define MEM_WRITE_DISABLED_EX MEM_READ_DISABLED
#define MEM_WRITE_MASK        0x03e0

#define MEM_MAPPING_EXTERNAL  1 /* On external bus (ISA/PCI). */
#define MEM_MAPPING_INTERNAL  2 /* On internal bus (RAM). */
#define MEM_MAPPING_ROM_WS    4 /* Executing from ROM may involve additional wait states. */
#define MEM_MAPPING_IS_ROM    8 /* Responds to ROMCS#. */
#define MEM_MAPPING_ROM       (MEM_MAPPING_ROM_WS | MEM_MAPPING_IS_ROM)
#define MEM_MAPPING_ROMCS     16 /* If it responds to ROMCS#, it requires ROMCS# asserted. */
#define MEM_MAPPING_SMRAM     32 /* On internal bus (RAM) but SMRAM. */
#define MEM_MAPPING_CACHE     64 /* Cache or MTRR - please avoid such mappings unless \
                                    stricly necessary (eg. for CoreBoot). */

/* #define's for memory granularity, currently 4k, less does
   not work because of internal 4k pages. */
#define MEM_GRANULARITY_BITS   12
#define MEM_GRANULARITY_SIZE   (1 << MEM_GRANULARITY_BITS)
#define MEM_GRANULARITY_HBOUND (MEM_GRANULARITY_SIZE - 2)
#define MEM_GRANULARITY_QBOUND (MEM_GRANULARITY_SIZE - 4)
#define MEM_GRANULARITY_MASK   (MEM_GRANULARITY_SIZE - 1)
#define MEM_GRANULARITY_HMASK  ((1 << (MEM_GRANULARITY_BITS - 1)) - 1)
#define MEM_GRANULARITY_QMASK  ((1 << (MEM_GRANULARITY_BITS - 2)) - 1)
#define MEM_GRANULARITY_PMASK  ((1 << (MEM_GRANULARITY_BITS - 3)) - 1)
#define MEM_MAPPINGS_NO        ((0x100000 >> MEM_GRANULARITY_BITS) << 12)
#define MEM_GRANULARITY_PAGE   (MEM_GRANULARITY_MASK & ~0xfff)
#define MEM_GRANULARITY_BASE   (~MEM_GRANULARITY_MASK)

/* Compatibility #defines. */
#define mem_set_state(smm, mode, base, size, access) \
    mem_set_access((smm ? ACCESS_SMM : ACCESS_NORMAL), mode, base, size, access)
#define mem_set_mem_state_common(smm, base, size, access) \
    mem_set_access((smm ? ACCESS_SMM : ACCESS_NORMAL), 0, base, size, access)
#define mem_set_mem_state(base, size, access) \
    mem_set_access(ACCESS_NORMAL, 0, base, size, access)
#define mem_set_mem_state_smm(base, size, access) \
    mem_set_access(ACCESS_SMM, 0, base, size, access)
#define mem_set_mem_state_both(base, size, access) \
    mem_set_access(ACCESS_ALL, 0, base, size, access)
#define mem_set_mem_state_cpu_both(base, size, access) \
    mem_set_access(ACCESS_CPU_BOTH, 0, base, size, access)
#define mem_set_mem_state_bus_both(base, size, access) \
    mem_set_access(ACCESS_BUS_BOTH, 0, base, size, access)
#define mem_set_mem_state_smram(smm, base, size, is_smram) \
    mem_set_access((smm ? ACCESS_SMM : ACCESS_NORMAL), 1, base, size, is_smram)
#define mem_set_mem_state_smram_ex(smm, base, size, is_smram) \
    mem_set_access((smm ? ACCESS_SMM : ACCESS_NORMAL), 2, base, size, is_smram)
#define mem_set_access_smram_cpu(smm, base, size, is_smram) \
    mem_set_access((smm ? ACCESS_CPU_SMM : ACCESS_CPU), 1, base, size, is_smram)
#define mem_set_access_smram_bus(smm, base, size, is_smram) \
    mem_set_access((smm ? ACCESS_BUS_SMM : ACCESS_BUS), 1, base, size, is_smram)

typedef struct state_t {
    uint16_t x : 5;
    uint16_t w : 5;
    uint16_t r : 5;
    uint16_t pad : 1;
} state_t;

typedef union mem_state_t {
    uint16_t vals[4];
    state_t  states[4];
} mem_state_t;

typedef struct _mem_mapping_ {
    struct _mem_mapping_ *prev;
    struct _mem_mapping_ *next;

    int enable;

    uint32_t base;
    uint32_t size;

    uint32_t base_ignore;
    uint32_t mask;

    uint8_t (*read_b)(uint32_t addr, void *priv);
    uint16_t (*read_w)(uint32_t addr, void *priv);
    uint32_t (*read_l)(uint32_t addr, void *priv);
    void (*write_b)(uint32_t addr, uint8_t val, void *priv);
    void (*write_w)(uint32_t addr, uint16_t val, void *priv);
    void (*write_l)(uint32_t addr, uint32_t val, void *priv);

    uint8_t *exec;

    uint32_t flags;

    /* There is never a needed to pass a pointer to the mapping itself, it is much preferable to
       prepare a structure with the requires data (usually, the base address and mask) instead. */
    void *priv; /* backpointer to device */
} mem_mapping_t;

#ifdef USE_NEW_DYNAREC
extern uint64_t *byte_dirty_mask;
extern uint64_t *byte_code_present_mask;

#    define PAGE_BYTE_MASK_SHIFT       6
#    define PAGE_BYTE_MASK_OFFSET_MASK 63
#    define PAGE_BYTE_MASK_MASK        63

#    define EVICT_NOT_IN_LIST          ((uint32_t) -1)
typedef struct page_t {
    void (*write_b)(uint32_t addr, uint8_t val, struct page_t *page);
    void (*write_w)(uint32_t addr, uint16_t val, struct page_t *page);
    void (*write_l)(uint32_t addr, uint32_t val, struct page_t *page);

    uint8_t *mem;

    uint16_t block, block_2;

    /*Head of codeblock tree associated with this page*/
    uint16_t head;

    uint64_t code_present_mask;
    uint64_t dirty_mask;

    uint32_t evict_prev;
    uint32_t evict_next;

    uint64_t *byte_dirty_mask;
    uint64_t *byte_code_present_mask;
} page_t;

extern uint32_t purgable_page_list_head;
__attribute__((always_inline)) static inline int
page_in_evict_list(page_t *page)
{
    return (page->evict_prev != EVICT_NOT_IN_LIST);
}
void page_remove_from_evict_list(page_t *page);
void page_add_to_evict_list(page_t *page);
#else
typedef struct _page_ {
    void (*write_b)(uint32_t addr, uint8_t val, struct _page_ *page);
    void (*write_w)(uint32_t addr, uint16_t val, struct _page_ *page);
    void (*write_l)(uint32_t addr, uint32_t val, struct _page_ *page);

    uint8_t *mem;

    uint64_t code_present_mask[4];
    uint64_t dirty_mask[4];

    struct codeblock_t *block[4];
    struct codeblock_t *block_2[4];

    /*Head of codeblock tree associated with this page*/
    struct codeblock_t *head;
} page_t;
#endif

extern uint8_t *ram;
extern uint8_t *ram2;
extern uint32_t rammask;

extern uint8_t *rom;
extern uint32_t biosmask;
extern uint32_t biosaddr;

extern int        readlookup[256];
extern uintptr_t *readlookup2;
extern uintptr_t  old_rl2;
extern uint8_t    uncached;
extern int        readlnext;
extern int        writelookup[256];
extern uintptr_t *writelookup2;
extern int        writelnext;
extern uint32_t   ram_mapped_addr[64];
extern uint8_t    page_ff[4096];

extern mem_mapping_t ram_low_mapping;
#if 1
extern mem_mapping_t ram_mid_mapping;
#endif
extern mem_mapping_t ram_remapped_mapping;
extern mem_mapping_t ram_high_mapping;
extern mem_mapping_t ram_2gb_mapping;
extern mem_mapping_t bios_mapping;
extern mem_mapping_t bios_high_mapping;

extern uint32_t mem_logical_addr;

extern page_t  *pages;
extern page_t **page_lookup;

extern uint32_t get_phys_virt;
extern uint32_t get_phys_phys;

extern int shadowbios;
extern int shadowbios_write;
extern int readlnum;
extern int writelnum;

extern int memspeed[11];

extern int     mmu_perm;
extern uint8_t high_page; /* if a high (> 4 gb) page was detected */

extern uint8_t *_mem_exec[MEM_MAPPINGS_NO];

extern uint32_t pages_sz; /* #pages in table */
extern int      read_type;

extern int mem_a20_state;
extern int mem_a20_alt;
extern int mem_a20_key;

extern uint8_t  read_mem_b(uint32_t addr);
extern uint16_t read_mem_w(uint32_t addr);
extern void     write_mem_b(uint32_t addr, uint8_t val);
extern void     write_mem_w(uint32_t addr, uint16_t val);

extern uint8_t  readmembl(uint32_t addr);
extern void     writemembl(uint32_t addr, uint8_t val);
extern uint16_t readmemwl(uint32_t addr);
extern void     writememwl(uint32_t addr, uint16_t val);
extern uint32_t readmemll(uint32_t addr);
extern void     writememll(uint32_t addr, uint32_t val);
extern uint64_t readmemql(uint32_t addr);
extern void     writememql(uint32_t addr, uint64_t val);

extern uint8_t  readmembl_no_mmut(uint32_t addr, uint32_t a64);
extern void     writemembl_no_mmut(uint32_t addr, uint32_t a64, uint8_t val);
extern uint16_t readmemwl_no_mmut(uint32_t addr, uint32_t *a64);
extern void     writememwl_no_mmut(uint32_t addr, uint32_t *a64, uint16_t val);
extern uint32_t readmemll_no_mmut(uint32_t addr, uint32_t *a64);
extern void     writememll_no_mmut(uint32_t addr, uint32_t *a64, uint32_t val);

extern void do_mmutranslate(uint32_t addr, uint32_t *a64, int num, int write);

extern uint8_t  readmembl_2386(uint32_t addr);
extern void     writemembl_2386(uint32_t addr, uint8_t val);
extern uint16_t readmemwl_2386(uint32_t addr);
extern void     writememwl_2386(uint32_t addr, uint16_t val);
extern uint32_t readmemll_2386(uint32_t addr);
extern void     writememll_2386(uint32_t addr, uint32_t val);
extern uint64_t readmemql_2386(uint32_t addr);
extern void     writememql_2386(uint32_t addr, uint64_t val);

extern uint8_t  readmembl_no_mmut_2386(uint32_t addr, uint32_t a64);
extern void     writemembl_no_mmut_2386(uint32_t addr, uint32_t a64, uint8_t val);
extern uint16_t readmemwl_no_mmut_2386(uint32_t addr, uint32_t *a64);
extern void     writememwl_no_mmut_2386(uint32_t addr, uint32_t *a64, uint16_t val);
extern uint32_t readmemll_no_mmut_2386(uint32_t addr, uint32_t *a64);
extern void     writememll_no_mmut_2386(uint32_t addr, uint32_t *a64, uint32_t val);

extern void     do_mmutranslate_2386(uint32_t addr, uint32_t *a64, int num, int write);

extern uint8_t *getpccache(uint32_t a);
extern uint64_t mmutranslatereal(uint32_t addr, int rw);
extern uint32_t mmutranslatereal32(uint32_t addr, int rw);
extern void     addreadlookup(uint32_t virt, uint32_t phys);
extern void     addwritelookup(uint32_t virt, uint32_t phys);

extern void mem_mapping_set(mem_mapping_t *,
                            uint32_t base,
                            uint32_t size,
                            uint8_t (*read_b)(uint32_t addr, void *priv),
                            uint16_t (*read_w)(uint32_t addr, void *priv),
                            uint32_t (*read_l)(uint32_t addr, void *priv),
                            void (*write_b)(uint32_t addr, uint8_t val, void *priv),
                            void (*write_w)(uint32_t addr, uint16_t val, void *priv),
                            void (*write_l)(uint32_t addr, uint32_t val, void *priv),
                            uint8_t *exec,
                            uint32_t flags,
                            void    *priv);
extern void mem_mapping_add(mem_mapping_t *,
                            uint32_t base,
                            uint32_t size,
                            uint8_t (*read_b)(uint32_t addr, void *priv),
                            uint16_t (*read_w)(uint32_t addr, void *priv),
                            uint32_t (*read_l)(uint32_t addr, void *priv),
                            void (*write_b)(uint32_t addr, uint8_t val, void *priv),
                            void (*write_w)(uint32_t addr, uint16_t val, void *priv),
                            void (*write_l)(uint32_t addr, uint32_t val, void *priv),
                            uint8_t *exec,
                            uint32_t flags,
                            void    *priv);

extern void mem_mapping_set_handler(mem_mapping_t *,
                                    uint8_t (*read_b)(uint32_t addr, void *priv),
                                    uint16_t (*read_w)(uint32_t addr, void *priv),
                                    uint32_t (*read_l)(uint32_t addr, void *priv),
                                    void (*write_b)(uint32_t addr, uint8_t val, void *priv),
                                    void (*write_w)(uint32_t addr, uint16_t val, void *priv),
                                    void (*write_l)(uint32_t addr, uint32_t val, void *priv));

extern void mem_mapping_set_write_handler(mem_mapping_t *,
                                          void (*write_b)(uint32_t addr, uint8_t val, void *priv),
                                          void (*write_w)(uint32_t addr, uint16_t val, void *priv),
                                          void (*write_l)(uint32_t addr, uint32_t val, void *priv));

extern void mem_mapping_set_p(mem_mapping_t *, void *priv);

extern void mem_mapping_set_addr(mem_mapping_t *,
                                 uint32_t base, uint32_t size);
extern void mem_mapping_set_base_ignore(mem_mapping_t *, uint32_t base_ignore);
extern void mem_mapping_set_exec(mem_mapping_t *, uint8_t *exec);
extern void mem_mapping_set_mask(mem_mapping_t *, uint32_t mask);
extern void mem_mapping_disable(mem_mapping_t *);
extern void mem_mapping_enable(mem_mapping_t *);
extern void mem_mapping_recalc(uint64_t base, uint64_t size);

extern void mem_set_wp(uint64_t base, uint64_t size, uint8_t flags, uint8_t wp);
extern void mem_set_access(uint8_t bitmap, int mode, uint32_t base, uint32_t size, uint16_t access);

extern uint8_t  mem_readb_phys(uint32_t addr);
extern uint16_t mem_readw_phys(uint32_t addr);
extern uint32_t mem_readl_phys(uint32_t addr);
extern void     mem_read_phys(void *dest, uint32_t addr, int tranfer_size);
extern void     mem_writeb_phys(uint32_t addr, uint8_t val);
extern void     mem_writew_phys(uint32_t addr, uint16_t val);
extern void     mem_writel_phys(uint32_t addr, uint32_t val);
extern void     mem_write_phys(void *src, uint32_t addr, int tranfer_size);

extern uint8_t  mem_read_ram(uint32_t addr, void *priv);
extern uint16_t mem_read_ramw(uint32_t addr, void *priv);
extern uint32_t mem_read_raml(uint32_t addr, void *priv);
extern void     mem_write_ram(uint32_t addr, uint8_t val, void *priv);
extern void     mem_write_ramw(uint32_t addr, uint16_t val, void *priv);
extern void     mem_write_raml(uint32_t addr, uint32_t val, void *priv);

extern uint8_t  mem_read_ram_2gb(uint32_t addr, void *priv);
extern uint16_t mem_read_ram_2gbw(uint32_t addr, void *priv);
extern uint32_t mem_read_ram_2gbl(uint32_t addr, void *priv);
extern void     mem_write_ram_2gb(uint32_t addr, uint8_t val, void *priv);
extern void     mem_write_ram_2gbw(uint32_t addr, uint16_t val, void *priv);
extern void     mem_write_ram_2gbl(uint32_t addr, uint32_t val, void *priv);

extern int mem_addr_is_ram(uint32_t addr);

extern uint64_t mmutranslate_noabrt(uint32_t addr, int rw);

extern void mem_invalidate_range(uint32_t start_addr, uint32_t end_addr);

extern void mem_write_ramb_page(uint32_t addr, uint8_t val, page_t *page);
extern void mem_write_ramw_page(uint32_t addr, uint16_t val, page_t *page);
extern void mem_write_raml_page(uint32_t addr, uint32_t val, page_t *page);
extern void mem_flush_write_page(uint32_t addr, uint32_t virt);

extern void mem_reset_page_blocks(void);

extern void flushmmucache(void);
extern void flushmmucache_pc(void);
extern void flushmmucache_nopc(void);

extern void mem_debug_check_addr(uint32_t addr, int write);

extern void mem_a20_init(void);
extern void mem_a20_recalc(void);

extern void mem_init(void);
extern void mem_close(void);
extern void mem_reset(void);
extern void mem_remap_top_ex(int kb, uint32_t start);
extern void mem_remap_top_ex_nomid(int kb, uint32_t start);
extern void mem_remap_top(int kb);
extern void mem_remap_top_nomid(int kb);

extern void umc_smram_recalc(uint32_t start, int set);

extern mem_mapping_t *read_mapping[MEM_MAPPINGS_NO];
extern mem_mapping_t *write_mapping[MEM_MAPPINGS_NO];

#ifdef EMU_CPU_H
static __inline uint32_t
get_phys(uint32_t addr)
{
    uint64_t pa64;

    if (!((addr ^ get_phys_virt) & ~0xfff))
        return get_phys_phys | (addr & 0xfff);

    get_phys_virt = addr;

    if (!(cr0 >> 31)) {
        get_phys_phys = (addr & rammask) & ~0xfff;
        return addr & rammask;
    }

    if (((int) (readlookup2[addr >> 12])) != -1)
        get_phys_phys = ((uintptr_t) readlookup2[addr >> 12] + (addr & ~0xfff)) - (uintptr_t) ram;
    else {
        pa64 = mmutranslatereal(addr, 0);
        if (pa64 > 0xffffffffULL)
            get_phys_phys = 0xffffffff;
        else
            get_phys_phys = (uint32_t) pa64;
        get_phys_phys = (get_phys_phys & rammask) & ~0xfff;
        if (!cpu_state.abrt && mem_addr_is_ram(get_phys_phys))
            addreadlookup(get_phys_virt, get_phys_phys);
    }

    return get_phys_phys | (addr & 0xfff);
}

static __inline uint32_t
get_phys_noabrt(uint32_t addr)
{
    uint64_t phys_addr;
    uint32_t phys_addr32;

    if (!(cr0 >> 31))
        return addr & rammask;

    if (((int) (readlookup2[addr >> 12])) != -1)
        return ((uintptr_t) readlookup2[addr >> 12] + addr) - (uintptr_t) ram;

    phys_addr   = mmutranslate_noabrt(addr, 0);
    phys_addr32 = (uint32_t) phys_addr;
    if ((phys_addr != 0xffffffffffffffffULL) && (phys_addr <= 0xffffffffULL) && mem_addr_is_ram(phys_addr32 & rammask))
        addreadlookup(addr, phys_addr32 & rammask);

    if (phys_addr > 0xffffffffULL)
        phys_addr32 = 0xffffffff;

    return phys_addr32;
}
#endif

#endif /*EMU_MEM_H*/
