/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#ifndef EMU_MEM_H
# define EMU_MEM_H


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

extern int	shadowbios,shadowbios_write;
extern int	readlnum,writelnum;


typedef struct mem_mapping_t
{
        struct mem_mapping_t *prev, *next;

        int enable;
                
        uint32_t base;
        uint32_t size;

        uint8_t  (*read_b)(uint32_t addr, void *priv);
        uint16_t (*read_w)(uint32_t addr, void *priv);
        uint32_t (*read_l)(uint32_t addr, void *priv);
        void (*write_b)(uint32_t addr, uint8_t  val, void *priv);
        void (*write_w)(uint32_t addr, uint16_t val, void *priv);
        void (*write_l)(uint32_t addr, uint32_t val, void *priv);
        
        uint8_t *exec;
        
        uint32_t flags;
        
        void *p;
} mem_mapping_t;

/*Only present on external bus (ISA/PCI)*/
#define MEM_MAPPING_EXTERNAL 1
/*Only present on internal bus (RAM)*/
#define MEM_MAPPING_INTERNAL 2
/*Executing from ROM may involve additional wait states*/
#define MEM_MAPPING_ROM      4

extern uint8_t *ram,*rom;
extern uint8_t romext[32768];
extern int readlnum,writelnum;
extern int memspeed[11];
extern int nopageerrors;
extern uint32_t biosmask;
extern unsigned char isram[0x10000];

#define MEM_MAP_TO_SHADOW_RAM_MASK 1
#define MEM_MAP_TO_RAM_ADDR_MASK   2

extern uint32_t ram_mapped_addr[64];

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
                    void *p);
void mem_mapping_set_handler(mem_mapping_t *mapping,
                    uint8_t  (*read_b)(uint32_t addr, void *p),
                    uint16_t (*read_w)(uint32_t addr, void *p),
                    uint32_t (*read_l)(uint32_t addr, void *p),
                    void (*write_b)(uint32_t addr, uint8_t  val, void *p),
                    void (*write_w)(uint32_t addr, uint16_t val, void *p),
                    void (*write_l)(uint32_t addr, uint32_t val, void *p));
void mem_mapping_set_p(mem_mapping_t *mapping, void *p);
void mem_mapping_set_addr(mem_mapping_t *mapping, uint32_t base, uint32_t size);
void mem_mapping_set_exec(mem_mapping_t *mapping, uint8_t *exec);
void mem_mapping_disable(mem_mapping_t *mapping);
void mem_mapping_enable(mem_mapping_t *mapping);

void mem_set_mem_state(uint32_t base, uint32_t size, int state);

#define MEM_READ_ANY       0x00
#define MEM_READ_INTERNAL  0x10
#define MEM_READ_EXTERNAL  0x20
#define MEM_READ_MASK      0xf0

#define MEM_WRITE_ANY      0x00
#define MEM_WRITE_INTERNAL 0x01
#define MEM_WRITE_EXTERNAL 0x02
#define MEM_WRITE_DISABLED 0x03
#define MEM_WRITE_MASK     0x0f

extern int mem_a20_state;
extern int mem_a20_alt;
extern int mem_a20_key;
void mem_a20_recalc();

uint8_t mem_readb_phys(uint32_t addr);
uint8_t mem_readb_phys_dma(uint32_t addr);
uint16_t mem_readw_phys(uint32_t addr);
void mem_writeb_phys(uint32_t addr, uint8_t val);
void mem_writeb_phys_dma(uint32_t addr, uint8_t val);
void mem_writew_phys(uint32_t addr, uint16_t val);

uint8_t  mem_read_ram(uint32_t addr, void *priv);
uint16_t mem_read_ramw(uint32_t addr, void *priv);
uint32_t mem_read_raml(uint32_t addr, void *priv);

void mem_write_ram(uint32_t addr, uint8_t val, void *priv);
void mem_write_ramw(uint32_t addr, uint16_t val, void *priv);
void mem_write_raml(uint32_t addr, uint32_t val, void *priv);

uint8_t  mem_read_bios(uint32_t addr, void *priv);
uint16_t mem_read_biosw(uint32_t addr, void *priv);
uint32_t mem_read_biosl(uint32_t addr, void *priv);

void mem_write_null(uint32_t addr, uint8_t val, void *p);
void mem_write_nullw(uint32_t addr, uint16_t val, void *p);
void mem_write_nulll(uint32_t addr, uint32_t val, void *p);

mem_mapping_t bios_mapping[8];
mem_mapping_t bios_high_mapping[8];
mem_mapping_t romext_mapping;

extern mem_mapping_t ram_high_mapping;


typedef struct page_t
{
        void (*write_b)(uint32_t addr, uint8_t val, struct page_t *p);
        void (*write_w)(uint32_t addr, uint16_t val, struct page_t *p);
        void (*write_l)(uint32_t addr, uint32_t val, struct page_t *p);
        
        uint8_t *mem;
        
        struct codeblock_t *block[4], *block_2[4];

        /*Head of codeblock tree associated with this page*/
        struct codeblock_t *head;
        
        uint64_t code_present_mask[4], dirty_mask[4];
} page_t;

extern page_t *pages;

extern page_t **page_lookup;

uint32_t mmutranslate_noabrt(uint32_t addr, int rw);

extern uint32_t get_phys_virt,get_phys_phys;

#ifdef EMU_CPU_H
static __inline uint32_t get_phys(uint32_t addr)
{
        if (!((addr ^ get_phys_virt) & ~0xfff))
                return get_phys_phys | (addr & 0xfff);

        get_phys_virt = addr;

        if (!(cr0 >> 31))
        {
                get_phys_phys = (addr & rammask) & ~0xfff;
                return addr & rammask;
       }      

        get_phys_phys = (mmutranslatereal(addr, 0) & rammask) & ~0xfff;
        return get_phys_phys | (addr & 0xfff);
        /* return mmutranslatereal(addr, 0) & rammask; */
}

static __inline uint32_t get_phys_noabrt(uint32_t addr)
{
        if (!(cr0 >> 31))
                return addr & rammask;
        
        return mmutranslate_noabrt(addr, 0) & rammask;
}
#endif

void mem_invalidate_range(uint32_t start_addr, uint32_t end_addr);

extern uint32_t mem_logical_addr;

void mem_write_ramb_page(uint32_t addr, uint8_t val, page_t *p);
void mem_write_ramw_page(uint32_t addr, uint16_t val, page_t *p);
void mem_write_raml_page(uint32_t addr, uint32_t val, page_t *p);
void mem_flush_write_page(uint32_t addr, uint32_t virt);

extern void	mem_reset_page_blocks(void);

extern mem_mapping_t ram_low_mapping;
extern mem_mapping_t ram_mid_mapping;
 
extern void	mem_remap_top_256k(void);
extern void	mem_remap_top_384k(void);

extern void     flushmmucache(void);
extern void     flushmmucache_cr3(void);
extern void	flushmmucache_nopc(void);
extern void     mmu_invalidate(uint32_t addr);

extern void	mem_split_enable(int max_size, uint32_t addr);
extern void	mem_split_disable(int max_size, uint32_t addr);


extern void	mem_add_bios(void);

extern void	mem_init(void);
extern void	mem_resize(void);

extern uint8_t	port_92_read(uint16_t port, void *priv);
extern void	port_92_write(uint16_t port, uint8_t val, void *priv);
extern void     port_92_clear_reset(void);
extern void	port_92_add(void);
extern void	port_92_remove(void);
extern void	port_92_reset(void);


#endif	/*EMU_MEM_H*/
