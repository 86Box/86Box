/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the memory interface.
 *
 *
 *
 * Authors:	Sarah Walker, <tommowalker@tommowalker.co.uk>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2017-2020 Fred N. van Kempen.
 *		Copyright 2016-2020 Miran Grca.
 */
#ifndef EMU_MEM_H
# define EMU_MEM_H


#define MEM_MAPPING_EXTERNAL	1	/* on external bus (ISA/PCI) */
#define MEM_MAPPING_INTERNAL	2	/* on internal bus (RAM) */
#define MEM_MAPPING_ROM		4	/* Executing from ROM may involve
					 * additional wait states. */
#define MEM_MAPPING_ROMCS	8	/* respond to ROMCS* */
#define MEM_MAPPING_SMRAM	16	/* on internal bus (RAM) but SMRAM */

#define MEM_MAP_TO_SHADOW_RAM_MASK 1
#define MEM_MAP_TO_RAM_ADDR_MASK   2

/* _mem_state layout:
	Bits  0 - 7:	Normal write
	Bits  8 -15:	Normal read
	Bits 16 -23:	SMM write
	Bits 24 -31:	SMM read
*/

#define MEM_READ_ANY		0x0000
#define MEM_READ_INTERNAL	0x0100
#define MEM_READ_EXTERNAL	0x0200
#define MEM_READ_DISABLED	0x0300
#define MEM_READ_NORMAL		0x0400	/* SMM only - means use the non-SMM state */
#define MEM_READ_EXTERNAL_EX	0x0500	/* External but with internal exec - needed by the VIA Apollo Pro */
#define MEM_READ_ROMCS		0x0600	/* EXTERNAL type + ROMC flag */
#define MEM_READ_EXTANY		0x0700	/* Any EXTERNAL type */
#define MEM_READ_SMRAM		0x1000
#define MEM_READ_SMRAM_EX	0x2000
#define MEM_READ_DISABLED_EX	0x4000
#define MEM_READ_MASK		0xff00

#define MEM_WRITE_ANY		0x0000
#define MEM_WRITE_INTERNAL	0x0001
#define MEM_WRITE_EXTERNAL	0x0002
#define MEM_WRITE_DISABLED	0x0003
#define MEM_WRITE_NORMAL	0x0004	/* SMM only - means use the non-SMM state */
#define MEM_WRITE_EXTERNAL_EX	0x0005
#define MEM_WRITE_ROMCS		0x0006	/* EXTERNAL type + ROMC flag */
#define MEM_WRITE_EXTANY	0x0007	/* Any EXTERNAL type */
#define MEM_WRITE_SMRAM		0x0010
#define MEM_WRITE_SMRAM_EX	0x0020
#define MEM_WRITE_DISABLED_EX	0x0040
#define MEM_WRITE_MASK		0x00ff

#define MEM_STATE_SMM_SHIFT	16

/* #define's for memory granularity, currently 16k, but may
   change in the future - 4k works, less does not because of
   internal 4k pages. */
#ifdef DEFAULT_GRANULARITY
#define MEM_GRANULARITY_BITS	14
#define MEM_GRANULARITY_SIZE	(1 << MEM_GRANULARITY_BITS)
#define MEM_GRANULARITY_HBOUND	(MEM_GRANULARITY_SIZE - 2)
#define MEM_GRANULARITY_QBOUND	(MEM_GRANULARITY_SIZE - 4)
#define MEM_GRANULARITY_MASK	(MEM_GRANULARITY_SIZE - 1)
#define MEM_GRANULARITY_HMASK	((1 << (MEM_GRANULARITY_BITS - 1)) - 1)
#define MEM_GRANULARITY_QMASK	((1 << (MEM_GRANULARITY_BITS - 2)) - 1)
#define MEM_GRANULARITY_PMASK	((1 << (MEM_GRANULARITY_BITS - 3)) - 1)
#define MEM_MAPPINGS_NO		((0x100000 >> MEM_GRANULARITY_BITS) << 12)
#define MEM_GRANULARITY_PAGE	(MEM_GRANULARITY_MASK & ~0xfff)
#else
#define MEM_GRANULARITY_BITS	12
#define MEM_GRANULARITY_SIZE	(1 << MEM_GRANULARITY_BITS)
#define MEM_GRANULARITY_HBOUND	(MEM_GRANULARITY_SIZE - 2)
#define MEM_GRANULARITY_QBOUND	(MEM_GRANULARITY_SIZE - 4)
#define MEM_GRANULARITY_MASK	(MEM_GRANULARITY_SIZE - 1)
#define MEM_GRANULARITY_HMASK	((1 << (MEM_GRANULARITY_BITS - 1)) - 1)
#define MEM_GRANULARITY_QMASK	((1 << (MEM_GRANULARITY_BITS - 2)) - 1)
#define MEM_GRANULARITY_PMASK	((1 << (MEM_GRANULARITY_BITS - 3)) - 1)
#define MEM_MAPPINGS_NO		((0x100000 >> MEM_GRANULARITY_BITS) << 12)
#define MEM_GRANULARITY_PAGE	(MEM_GRANULARITY_MASK & ~0xfff)
#endif


typedef struct _mem_mapping_ {
    struct _mem_mapping_ *prev, *next;

    int		enable;

    uint32_t	base;
    uint32_t	size;

    uint8_t	(*read_b)(uint32_t addr, void *priv);
    uint16_t	(*read_w)(uint32_t addr, void *priv);
    uint32_t	(*read_l)(uint32_t addr, void *priv);
    void	(*write_b)(uint32_t addr, uint8_t  val, void *priv);
    void	(*write_w)(uint32_t addr, uint16_t val, void *priv);
    void	(*write_l)(uint32_t addr, uint32_t val, void *priv);

    uint8_t	*exec;

    uint32_t	flags;

    void	*p;		/* backpointer to mapping or device */

    void	*dev;		/* backpointer to memory device */
} mem_mapping_t;

#ifdef USE_NEW_DYNAREC
extern uint64_t *byte_dirty_mask;
extern uint64_t *byte_code_present_mask;

#define PAGE_BYTE_MASK_SHIFT 6
#define PAGE_BYTE_MASK_OFFSET_MASK 63
#define PAGE_BYTE_MASK_MASK  63

#define EVICT_NOT_IN_LIST ((uint32_t)-1)
typedef struct page_t
{
    void	(*write_b)(uint32_t addr, uint8_t val, struct page_t *p);
    void	(*write_w)(uint32_t addr, uint16_t val, struct page_t *p);
    void	(*write_l)(uint32_t addr, uint32_t val, struct page_t *p);

    uint8_t	*mem;

    uint16_t	block, block_2;

    /*Head of codeblock tree associated with this page*/
    uint16_t head;

    uint64_t code_present_mask, dirty_mask;

    uint32_t evict_prev, evict_next;

    uint64_t *byte_dirty_mask;
    uint64_t *byte_code_present_mask;
} page_t;

extern uint32_t purgable_page_list_head;
static inline int
page_in_evict_list(page_t *p)
{
    return (p->evict_prev != EVICT_NOT_IN_LIST);
}
void page_remove_from_evict_list(page_t *p);
void page_add_to_evict_list(page_t *p);
#else
typedef struct _page_ {
    void	(*write_b)(uint32_t addr, uint8_t val, struct _page_ *p);
    void	(*write_w)(uint32_t addr, uint16_t val, struct _page_ *p);
    void	(*write_l)(uint32_t addr, uint32_t val, struct _page_ *p);

    uint8_t	*mem;

    uint64_t	code_present_mask[4],
		dirty_mask[4];

    struct codeblock_t *block[4], *block_2[4];

    /*Head of codeblock tree associated with this page*/
    struct codeblock_t *head;
} page_t;
#endif


typedef struct
{
    uint32_t	host_base,
		ram_base;
} smram_t;


extern uint8_t		*ram, *ram2;
extern uint32_t		rammask;

extern uint8_t		*rom;
extern uint32_t		biosmask, biosaddr;

extern int		readlookup[256],
			readlookupp[256];
extern uintptr_t *	readlookup2;
extern int		readlnext;
extern int		writelookup[256],
			writelookupp[256];
extern uintptr_t	*writelookup2;
extern int		writelnext;
extern uint32_t		ram_mapped_addr[64];

extern mem_mapping_t	base_mapping,
			ram_low_mapping,
#if 1
			ram_mid_mapping,
#endif
			ram_remapped_mapping,
			ram_high_mapping,
			ram_2gb_mapping,
			ram_smram_mapping[2],
			bios_mapping,
			bios_high_mapping;

extern uint32_t		mem_logical_addr;

extern page_t		*pages,
			**page_lookup;

extern uint32_t		get_phys_virt, get_phys_phys;
extern smram_t		smram[2];

extern int		shadowbios,
			shadowbios_write;
extern int		readlnum,
			writelnum;

extern int		memspeed[11];

extern int		mmu_perm,
			use_phys_exec;

extern int		mem_a20_state,
			mem_a20_alt,
			mem_a20_key;


#ifndef USE_NEW_DYNAREC
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
#else
uint8_t readmembl(uint32_t addr);
void writemembl(uint32_t addr, uint8_t val);
uint16_t readmemwl(uint32_t addr);
void writememwl(uint32_t addr, uint16_t val);
uint32_t readmemll(uint32_t addr);
void writememll(uint32_t addr, uint32_t val);
uint64_t readmemql(uint32_t addr);
void writememql(uint32_t addr, uint64_t val);
#endif

extern uint8_t	*getpccache(uint32_t a);
extern uint64_t	mmutranslatereal(uint32_t addr, int rw);
extern uint32_t	mmutranslatereal32(uint32_t addr, int rw);
extern void	addreadlookup(uint32_t virt, uint32_t phys);
extern void	addwritelookup(uint32_t virt, uint32_t phys);

extern void	mem_mapping_del(mem_mapping_t *);

extern void	mem_mapping_add(mem_mapping_t *,
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
                    void *p);

extern void	mem_mapping_set_handler(mem_mapping_t *,
                    uint8_t  (*read_b)(uint32_t addr, void *p),
                    uint16_t (*read_w)(uint32_t addr, void *p),
                    uint32_t (*read_l)(uint32_t addr, void *p),
                    void (*write_b)(uint32_t addr, uint8_t  val, void *p),
                    void (*write_w)(uint32_t addr, uint16_t val, void *p),
                    void (*write_l)(uint32_t addr, uint32_t val, void *p));

extern void	mem_mapping_set_p(mem_mapping_t *, void *p);

extern void	mem_mapping_set_dev(mem_mapping_t *, void *dev);

extern void	mem_mapping_set_addr(mem_mapping_t *,
				     uint32_t base, uint32_t size);
extern void	mem_mapping_set_exec(mem_mapping_t *, uint8_t *exec);
extern void	mem_mapping_disable(mem_mapping_t *);
extern void	mem_mapping_enable(mem_mapping_t *);
extern void	mem_mapping_recalc(uint64_t base, uint64_t size);

extern void	mem_set_mem_state_common(int smm, uint32_t base, uint32_t size, int state);

extern void	mem_set_mem_state_smram(int smm, uint32_t base, uint32_t size, int is_smram);
extern void	mem_set_mem_state_smram_ex(int smm, uint32_t base, uint32_t size, int is_smram);

extern void	mem_set_mem_state(uint32_t base, uint32_t size, int state);
extern void	mem_set_mem_state_smm(uint32_t base, uint32_t size, int state);

extern void	mem_set_mem_state_both(uint32_t base, uint32_t size, int state);

extern uint8_t	mem_readb_phys(uint32_t addr);
extern uint16_t	mem_readw_phys(uint32_t addr);
extern uint32_t	mem_readl_phys(uint32_t addr);
extern void	mem_read_phys(void *dest, uint32_t addr, int tranfer_size);
extern void	mem_writeb_phys(uint32_t addr, uint8_t val);
extern void	mem_writew_phys(uint32_t addr, uint16_t val);
extern void	mem_writel_phys(uint32_t addr, uint32_t val);
extern void	mem_write_phys(void *src, uint32_t addr, int tranfer_size);

extern uint8_t	mem_read_ram(uint32_t addr, void *priv);
extern uint16_t	mem_read_ramw(uint32_t addr, void *priv);
extern uint32_t	mem_read_raml(uint32_t addr, void *priv);
extern void	mem_write_ram(uint32_t addr, uint8_t val, void *priv);
extern void	mem_write_ramw(uint32_t addr, uint16_t val, void *priv);
extern void	mem_write_raml(uint32_t addr, uint32_t val, void *priv);

extern uint8_t	mem_read_smram(uint32_t addr, void *priv);
extern uint16_t	mem_read_smramw(uint32_t addr, void *priv);
extern uint32_t	mem_read_smraml(uint32_t addr, void *priv);
extern void	mem_write_smram(uint32_t addr, uint8_t val, void *priv);
extern void	mem_write_smramw(uint32_t addr, uint16_t val, void *priv);
extern void	mem_write_smraml(uint32_t addr, uint32_t val, void *priv);

extern uint8_t	mem_read_bios(uint32_t addr, void *priv);
extern uint16_t	mem_read_biosw(uint32_t addr, void *priv);
extern uint32_t	mem_read_biosl(uint32_t addr, void *priv);

extern void	mem_write_null(uint32_t addr, uint8_t val, void *p);
extern void	mem_write_nullw(uint32_t addr, uint16_t val, void *p);
extern void	mem_write_nulll(uint32_t addr, uint32_t val, void *p);

extern int	mem_addr_is_ram(uint32_t addr);

extern uint64_t	mmutranslate_noabrt(uint32_t addr, int rw);

extern void	mem_invalidate_range(uint32_t start_addr, uint32_t end_addr);

extern void	mem_write_ramb_page(uint32_t addr, uint8_t val, page_t *p);
extern void	mem_write_ramw_page(uint32_t addr, uint16_t val, page_t *p);
extern void	mem_write_raml_page(uint32_t addr, uint32_t val, page_t *p);
extern void	mem_flush_write_page(uint32_t addr, uint32_t virt);

extern void	mem_reset_page_blocks(void);

extern void     flushmmucache(void);
extern void     flushmmucache_cr3(void);
extern void	flushmmucache_nopc(void);
extern void     mmu_invalidate(uint32_t addr);

extern void	mem_a20_init(void);
extern void	mem_a20_recalc(void);

extern void	mem_add_upper_bios(void);
extern void	mem_add_bios(void);

extern void	mem_init(void);

extern void	mem_reset(void);
extern void	mem_remap_top(int kb);


#ifdef EMU_CPU_H
static __inline uint32_t get_phys(uint32_t addr)
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
	get_phys_phys = ((uintptr_t)readlookup2[addr >> 12] + (addr & ~0xfff)) - (uintptr_t)ram;
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


static __inline uint32_t get_phys_noabrt(uint32_t addr)
{
    uint64_t phys_addr;
    uint32_t phys_addr32;

    if (!(cr0 >> 31))
	return addr & rammask;

    if (((int) (readlookup2[addr >> 12])) != -1)
	return ((uintptr_t)readlookup2[addr >> 12] + addr) - (uintptr_t)ram;

    phys_addr = mmutranslate_noabrt(addr, 0);
    phys_addr32 = (uint32_t) phys_addr;
    if ((phys_addr != 0xffffffffffffffffULL) && (phys_addr <= 0xffffffffULL) &&
	mem_addr_is_ram(phys_addr32 & rammask))
	addreadlookup(addr, phys_addr32 & rammask);

    if (phys_addr > 0xffffffffULL)
	phys_addr32 = 0xffffffff;

    return phys_addr32;
}
#endif


#endif	/*EMU_MEM_H*/
