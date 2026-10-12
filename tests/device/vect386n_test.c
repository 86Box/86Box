/* HP 386N indexed controller: model selection and firmware register accesses. */
#undef NDEBUG
#include <assert.h>
#include <string.h>
#include "../../src/machine/m_at_vect386n.c"

CPU            *cpu_s;
uint8_t        *rom;
int             bios_only, gfxcard[GFXCARD_MAX];
const device_t  nmc93cxx_device = { 0 }, pc87310_device = { 0 }, tact83000_device = { 0 }, v7_vga_vect386n_device = { 0 };
static unsigned indexed_handlers;
static unsigned bios_loads, board_inits;
static int missing_rom;
void *
device_add(const device_t *d)
{
    (void) d;
    return NULL;
}
void *
device_add_params(const device_t *d, void *p)
{
    (void) d;
    (void) p;
    return NULL;
}
bool
nmc93cxx_eeprom_read(nmc93cxx_eeprom_t *d)
{
    (void) d;
    return true;
}
void
nmc93cxx_eeprom_write(nmc93cxx_eeprom_t *d, bool chip_select, bool clock_pin, bool data_pin)
{
    (void) d;
    (void) chip_select;
    (void) clock_pin;
    (void) data_pin;
}
void
machine_at_ps2_init(const machine_t *m)
{
    (void) m;
    board_inits++;
}
int
bios_load(const char *a, const char *b, uint32_t c, int d, int e, int f)
{
    assert(b == NULL && e == 0);
    if (bios_loads++ == 0) {
        assert(strcmp(a, "roms/machines/vect386n/l0305.bin") == 0);
        assert(c == 0xe8000 && d == 0x20000 && f == 0);
        return missing_rom != 1;
    }
    assert(bios_loads == 2);
    assert(strcmp(a, "roms/machines/vect386n/vga.bin") == 0);
    assert(c == 0xe0000 && d == 0x8000 && f == FLAG_AUX);
    return missing_rom != 2;
}
void
mem_mapping_add(mem_mapping_t *m, uint32_t b, uint32_t sz,
                uint8_t (*rb)(uint32_t, void *), uint16_t (*rw)(uint32_t, void *), uint32_t (*rl)(uint32_t, void *),
                void (*wb)(uint32_t, uint8_t, void *), void (*ww)(uint32_t, uint16_t, void *), void (*wl)(uint32_t, uint32_t, void *),
                uint8_t *exec, uint32_t flags, void *p)
{
    m->base    = b;
    m->size    = sz;
    m->read_b  = rb;
    m->read_w  = rw;
    m->read_l  = rl;
    m->write_b = wb;
    m->write_w = ww;
    m->write_l = wl;
    m->exec    = exec;
    m->flags   = flags;
    m->priv    = p;
}
void
io_sethandler(uint16_t b, uint16_t sz,
              uint8_t (*rb)(uint16_t, void *), uint16_t (*rw)(uint16_t, void *), uint32_t (*rl)(uint16_t, void *),
              void (*wb)(uint16_t, uint8_t, void *), void (*ww)(uint16_t, uint16_t, void *), void (*wl)(uint16_t, uint32_t, void *), void *p)
{
    if (b == 0x2c) {
        indexed_handlers++;
        assert(sz == 4 && rw && ww && !rb && !rl && !wb && !wl);
    }
    (void) sz;
    (void) rb;
    (void) rw;
    (void) rl;
    (void) wb;
    (void) ww;
    (void) wl;
    (void) p;
}

static void
reg(vect386n_board_t *d, uint16_t i, uint16_t v)
{
    vect386n_indexed_write(0x2c, i, d);
    vect386n_indexed_write(0x2e, v, d);
}
static uint16_t
readreg(vect386n_board_t *d, uint16_t i)
{
    vect386n_indexed_write(0x2c, i, d);
    assert(vect386n_indexed_read(0x2c, d) == i);
    return vect386n_indexed_read(0x2e, d);
}
int
main(void)
{
    CPU cpu = { 0 };
    cpu_s   = &cpu;
    /* Both split images are required for detection and normal startup. */
    for (bios_only = 0; bios_only <= 1; bios_only++) {
        for (missing_rom = 0; missing_rom <= 2; missing_rom++) {
            bios_loads = board_inits = 0;
            assert(machine_at_vect386n_init(NULL) == (missing_rom == 0));
            assert(bios_loads == 2);
            assert(board_inits == (!bios_only && !missing_rom));
        }
    }
    bios_only = 0;
    for (unsigned mhz = 16; mhz <= 20; mhz += 4) {
        cpu.rspeed          = mhz * 1000000;
        indexed_handlers    = 0;
        vect386n_board_t *d = vect386n_board_init(NULL);
        assert(indexed_handlers == (mhz == 20));
        if (mhz == 20) {
            assert((readreg(d, 1) >> 8) == 0xcc);
            reg(d, 1, 0);
            assert(readreg(d, 1) == 0xcc00);
            reg(d, 6, 0);
            assert(readreg(d, 6) & 1);
            /* HP tests every tag entry with alternating bit patterns. */
            for (unsigned i = 0; i < 8192; i++) {
                reg(d, 0xa, i);
                reg(d, 0xc, (i ^ 0xa5a5) & 0xffe3);
            }
            for (unsigned i = 0; i < 8192; i++) {
                reg(d, 0xa, i);
                assert(readreg(d, 0xc) == ((i ^ 0xa5a5) & 0xffe3));
            }
            /* Map writes are active high, while readback is active low. */
            for (unsigned i = 0; i < 1024; i++) {
                reg(d, 3, i);
                reg(d, 4, i & 3);
            }
            for (unsigned i = 0; i < 1024; i++) {
                reg(d, 3, i);
                assert(((~readreg(d, 4)) & 3) == (i & 3));
            }
            reg(d, 5, 0x2100);
            reg(d, 6, 0);
            assert(readreg(d, 5) == 0x2100);
            reg(d, 0xffff, 0);
            assert(readreg(d, 0xffff) == 0xffff);
            assert(readreg(d, 1) == 0xcc00);
        }
        vect386n_board_close(d);
    }
    puts("HP 386N 16/20 MHz decode, model ID, tag RAM and inverted map readback passed");
    return 0;
}
