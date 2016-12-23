/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#ifndef _MEM_H_
#define _MEM_H_

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

extern uint8_t *ram,*rom;
extern uint8_t romext[32768];
extern int readlnum,writelnum;
extern int memspeed[11];
extern int nopageerrors;
extern uint32_t biosmask;

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

extern int mem_a20_alt;
extern int mem_a20_key;
void mem_a20_recalc();

uint8_t mem_readb_phys(uint32_t addr);
void mem_writeb_phys(uint32_t addr, uint8_t val);

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

FILE *romfopen(char *fn, char *mode);

mem_mapping_t bios_mapping[8];
mem_mapping_t bios_high_mapping[8];


typedef struct page_t
{
        void (*write_b)(uint32_t addr, uint8_t val, struct page_t *p);
        void (*write_w)(uint32_t addr, uint16_t val, struct page_t *p);
        void (*write_l)(uint32_t addr, uint32_t val, struct page_t *p);
        
        uint8_t *mem;
        
        struct codeblock_t *block, *block_2;

        /*Head of codeblock tree associated with this page*/
        struct codeblock_t *head;
        
        uint64_t code_present_mask, dirty_mask;
} page_t;

extern page_t *pages;

extern page_t **page_lookup;

uint32_t mmutranslate_noabrt(uint32_t addr, int rw);

extern uint32_t get_phys_virt,get_phys_phys;
static inline uint32_t get_phys(uint32_t addr)
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
//        return mmutranslatereal(addr, 0) & rammask;
}

static inline uint32_t get_phys_noabrt(uint32_t addr)
{
        if (!(cr0 >> 31))
                return addr & rammask;
        
        return mmutranslate_noabrt(addr, 0) & rammask;
}

void mem_invalidate_range(uint32_t start_addr, uint32_t end_addr);

extern uint32_t mem_logical_addr;

void mem_write_ramb_page(uint32_t addr, uint8_t val, page_t *p);
void mem_write_ramw_page(uint32_t addr, uint16_t val, page_t *p);
void mem_write_raml_page(uint32_t addr, uint32_t val, page_t *p);

void mem_reset_page_blocks();

extern mem_mapping_t ram_low_mapping;
 
void mem_remap_top_384k();

#endif
