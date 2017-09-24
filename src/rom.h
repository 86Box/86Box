/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
extern FILE *romfopen(wchar_t *fn, wchar_t *mode);
extern int rom_getfile(wchar_t *fn, wchar_t *s, int size);
extern int rom_present(wchar_t *fn);

typedef struct rom_t
{
        uint8_t *rom;
        uint32_t mask;
        mem_mapping_t mapping;
} rom_t;

extern int rom_init(rom_t *rom, wchar_t *fn, uint32_t address, int size, int mask, int file_offset, uint32_t flags);
extern int rom_init_interleaved(rom_t *rom, wchar_t *fn_low, wchar_t *fn_high, uint32_t address, int size, int mask, int file_offset, uint32_t flags);

extern uint8_t rom_read(uint32_t addr, void *p);
extern uint16_t rom_readw(uint32_t addr, void *p);
extern uint32_t rom_readl(uint32_t addr, void *p);
