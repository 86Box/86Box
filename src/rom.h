FILE *romfopen(char *fn, char *mode);
int rom_present(char *fn);

typedef struct rom_t
{
        uint8_t *rom;
        uint32_t mask;
        mem_mapping_t mapping;
} rom_t;

int rom_init(rom_t *rom, char *fn, uint32_t address, int size, int mask, int file_offset, uint32_t flags);
int rom_init_interleaved(rom_t *rom, char *fn_low, char *fn_high, uint32_t address, int size, int mask, int file_offset, uint32_t flags);
