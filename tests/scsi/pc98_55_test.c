/* Exercise the real adapter's registered I/O and ROM handlers without
 * redistributing firmware. Synthetic banks have distinct executable bytes. */
#include "../../src/scsi/scsi_pc9801_55.c"

static unsigned checks, failures, irq_mask, target_commands, target_completions;
static unsigned irq_assertions, dma_accept, dma_position, dma_drq;
static uint8_t dma_memory[36];
static unsigned rom_variants = 1, next_bus;
static unsigned configured_irq = 5, configured_id = 7;
static uint8_t (*io_read)(uint16_t, void *);
static void (*io_write)(uint16_t, uint8_t, void *);
static mem_mapping_t *rom_mapping;
static uint8_t target_buffer[36];
static scsi_common_t target_common;
scsi_device_t scsi_devices[SCSI_BUS_MAX][SCSI_ID_MAX];
#define CHECK(expr) do { checks++; if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); failures++; } } while (0)

void (pclog)(const char *fmt, ...) { (void)fmt; }
void (pclog_ex)(const char *fmt, va_list ap) { (void)fmt; (void)ap; }
void picint_common(uint16_t mask, int level, int set, uint8_t *p)
{ (void)level; (void)p; if (set) { irq_mask |= mask; irq_assertions++; } else irq_mask &= ~mask; }
void dma_set_drq(int ch, int set) { CHECK(ch == 0); dma_drq = !!set; }
int dma_channel_read(int ch) { (void)ch; return DMA_NODATA; }
int dma_channel_write(int ch, uint16_t value)
{
    CHECK(ch == 0 && dma_drq);
    if (!dma_accept || dma_position >= sizeof(dma_memory)) return DMA_NODATA;
    dma_memory[dma_position++] = (uint8_t)value;
    return dma_position == sizeof(dma_memory) ? DMA_OVER : 0;
}
int device_get_config_int(const char *name)
{
    if (!strcmp(name, "base")) return 0xcc0;
    if (!strcmp(name, "irq")) return configured_irq;
    if (!strcmp(name, "scsi_id")) return configured_id;
    return 0;
}
int device_get_config_hex20(const char *name) { (void)name; return 0xdc000; }
const char *device_get_config_bios(const char *name) { (void)name; return "pc9801_55l"; }
const char *device_get_bios_file(const device_t *d, const char *name, unsigned int n)
{ (void)d; (void)name; return n ? PC98_55L_ROM_HIGH : PC98_55L_ROM_LOW; }
int rom_present(const char *path)
{
    if (strstr(path, "/pc9801_55l/")) return (rom_variants & 1) != 0;
    if (strstr(path, "/pc9801_55u/")) return (rom_variants & 2) != 0;
    return 0;
}
int rom_init_interleaved(rom_t *r, const char *low, const char *high,
                        uint32_t addr, int size, int mask, int offset, uint32_t flags)
{
    CHECK(rom_present(low) && rom_present(high));
    CHECK(size == 0x2000 && mask == 0x1fff && offset == 0);
    CHECK(flags == MEM_MAPPING_EXTERNAL);
    r->rom = calloc(1, size);
    if (!r->rom) abort();
    /* First bank starts with JMP; the second starts with CALL, as on 55L. */
    memset(r->rom, 0xe9, 0x1000);
    memset(r->rom + 0x1000, 0xe8, 0x1000);
    r->mapping.base = addr;
    r->mapping.size = size;
    r->mapping.exec = r->rom;
    rom_mapping = &r->mapping;
    return 0;
}
void mem_mapping_set_addr(mem_mapping_t *m, uint32_t addr, uint32_t size)
{ m->base = addr; m->size = size; }
void mem_mapping_set_handler(mem_mapping_t *m,
    uint8_t (*rb)(uint32_t, void *), uint16_t (*rw)(uint32_t, void *), uint32_t (*rl)(uint32_t, void *),
    void (*wb)(uint32_t, uint8_t, void *), void (*ww)(uint32_t, uint16_t, void *), void (*wl)(uint32_t, uint32_t, void *))
{ m->read_b = rb; m->read_w = rw; m->read_l = rl; m->write_b = wb; m->write_w = ww; m->write_l = wl; }
void mem_mapping_set_p(mem_mapping_t *m, void *p) { m->priv = p; }
void mem_mapping_set_exec(mem_mapping_t *m, uint8_t *exec) { m->exec = exec; }
void mem_mapping_disable(mem_mapping_t *m) { m->enable = 0; }
void io_sethandler(uint16_t base, uint16_t size,
    uint8_t (*rb)(uint16_t, void *), uint16_t (*rw)(uint16_t, void *), uint32_t (*rl)(uint16_t, void *),
    void (*wb)(uint16_t, uint8_t, void *), void (*ww)(uint16_t, uint16_t, void *), void (*wl)(uint16_t, uint32_t, void *), void *p)
{
    CHECK(base == 0xcc0 && size == 6);
    (void)rw; (void)rl; (void)ww; (void)wl; (void)p;
    io_read = rb; io_write = wb;
}
void io_removehandler(uint16_t base, uint16_t size,
    uint8_t (*rb)(uint16_t, void *), uint16_t (*rw)(uint16_t, void *), uint32_t (*rl)(uint16_t, void *),
    void (*wb)(uint16_t, uint8_t, void *), void (*ww)(uint16_t, uint16_t, void *), void (*wl)(uint16_t, uint32_t, void *), void *p)
{ (void)base; (void)size; (void)rb; (void)rw; (void)rl; (void)wb; (void)ww; (void)wl; (void)p; }
uint8_t scsi_get_bus(void) { return next_bus++; }
void scsi_bus_set_speed(uint8_t bus, double speed) { CHECK(bus == 0 && speed == 5000000.0); }
void scsi_device_reset(scsi_device_t *sd) { (void)sd; }
int scsi_device_present(scsi_device_t *sd) { return sd->type != SCSI_NONE; }
void scsi_device_identify(scsi_device_t *sd, uint8_t lun) { if (sd->sc) sd->sc->cur_lun = lun; }
void scsi_device_command_stop(scsi_device_t *sd) { (void)sd; }
void scsi_device_command_phase0(scsi_device_t *sd, uint8_t *cdb)
{
    CHECK(cdb[0] == 0x12 && cdb[4] == sizeof(target_buffer));
    CHECK(sd->buffer_length == -1);
    target_commands++;
    sd->status = 0;
    sd->phase = SCSI_PHASE_DATA_IN;
    sd->buffer_length = sizeof(target_buffer);
}
void scsi_device_command_phase1(scsi_device_t *sd)
{ target_completions++; sd->phase = SCSI_PHASE_STATUS; }

static void write_reg(void *dev, uint8_t reg, uint8_t value)
{ io_write(0xcc0, reg, dev); io_write(0xcc2, value, dev); }
static uint8_t read_reg(void *dev, uint8_t reg)
{ io_write(0xcc0, reg, dev); return io_read(0xcc2, dev); }
static void set_count(void *dev, unsigned n)
{ write_reg(dev, 0x12, (uint8_t)(n >> 16)); io_write(0xcc2, (uint8_t)(n >> 8), dev); io_write(0xcc2, (uint8_t)n, dev); }
static void close_adapter(void *dev)
{
    /* The production memory map remains linked until the core tears down;
     * this isolated harness has no linked list, so release its node too. */
    rom_t *bios = ((pc98_55_t *)dev)->bios;
    scsi_pc9801_55_device.close(dev);
    free(bios);
}
static void test_bank_and_reset(void *dev)
{
    CHECK(rom_mapping->base == 0xdc000 && rom_mapping->size == 0x1000);
    CHECK(rom_mapping->read_b(0xdc000, rom_mapping->priv) == 0xe9);
    /* The firmware's RAM trampoline sets bit 6, then jumps to CS:0000. */
    write_reg(dev, 0x30, 0x40);
    CHECK(read_reg(dev, 0x30) == 0x40);
    CHECK(rom_mapping->read_b(0xdc000, rom_mapping->priv) == 0xe8);
    CHECK(rom_mapping->exec[0] == 0xe8);
    CHECK(rom_mapping->read_w(0xdc010, rom_mapping->priv) == 0xe8e8);
    CHECK(rom_mapping->read_l(0xdc010, rom_mapping->priv) == 0xe8e8e8e8u);
    write_reg(dev, 0x30, 0x41); /* bit 0 does not select the bank */
    CHECK(rom_mapping->exec[0] == 0xe8);
    write_reg(dev, 0x30, 0x01);
    CHECK(rom_mapping->exec[0] == 0xe9);
    write_reg(dev, 0x30, 0x44);
    scsi_pc9801_55_device.reset(dev);
    CHECK(read_reg(dev, 0x30) == 0);
    CHECK(rom_mapping->exec[0] == 0xe9);
    CHECK(io_read(0xcc0, dev) == 0 && irq_mask == 0);
    write_reg(dev, 0x30, 0x04);
    write_reg(dev, 0x18, 0x00);
    CHECK(io_read(0xcc0, dev) == 0x80);
    CHECK(irq_mask == (1u << 5));
    CHECK(read_reg(dev, 0x17) == 0);
    CHECK(io_read(0xcc0, dev) == 0 && irq_mask == 0);
    CHECK(read_reg(dev, 0x33) == 0x0f); /* IRQ5, host ID7 */
}
static void test_bios_transfer(void *dev, int use_dma)
{
    const uint8_t inquiry[] = { 0x12, 0, 0, 0, sizeof(target_buffer), 0 };
    target_commands = target_completions = dma_position = 0;
    dma_accept = use_dma;
    write_reg(dev, 0x30, 0x04);
    write_reg(dev, 0x15, 2);
    write_reg(dev, 0x18, 0x07);
    CHECK(read_reg(dev, 0x17) == 0x11);
    CHECK(io_read(0xcc0, dev) == 0x80);
    CHECK(irq_mask == (1u << 5));
    CHECK(read_reg(dev, 0x17) == 0x8a);
    CHECK(io_read(0xcc0, dev) == 0 && irq_mask == 0);
    set_count(dev, sizeof(inquiry));
    write_reg(dev, 0x18, 0x20);
    io_write(0xcc0, 0x19, dev);
    for (unsigned n = 0; n < sizeof(inquiry); n++) {
        CHECK(io_read(0xcc0, dev) == 0x21); /* exact 55L polling condition */
        io_write(0xcc2, inquiry[n], dev);
    }
    CHECK(target_commands == 1);
    CHECK(read_reg(dev, 0x17) == 0x19);
    set_count(dev, sizeof(target_buffer));
    if (use_dma) {
        write_reg(dev, 0x01, 0x88);
        io_write(0xcc4, 0x01, dev);
    }
    const unsigned before_completion = irq_assertions;
    write_reg(dev, 0x18, 0x20);
    if (use_dma) {
        CHECK(dma_position == sizeof(target_buffer));
        CHECK(!memcmp(dma_memory, target_buffer, sizeof(target_buffer)));
        CHECK(dma_drq == 0);
        io_write(0xcc4, 0x02, dev);
    } else {
        io_write(0xcc0, 0x19, dev);
        for (unsigned n = 0; n < sizeof(target_buffer); n++) {
            CHECK(io_read(0xcc0, dev) == 0x21);
            CHECK(io_read(0xcc2, dev) == target_buffer[n]);
        }
    }
    CHECK(target_completions == 1);
    CHECK(io_read(0xcc0, dev) == 0x80); /* no stale BSY/DBR */
    CHECK(read_reg(dev, 0x17) == 0x1b);
    CHECK(irq_assertions == before_completion + 1); /* one completion, new phase */
    CHECK(io_read(0xcc0, dev) == 0 && irq_mask == 0);
    /* Consume status and COMMAND COMPLETE using the BIOS's single-byte INFO. */
    write_reg(dev, 0x18, 0xa0);
    CHECK(io_read(0xcc0, dev) == 0x21);
    CHECK(read_reg(dev, 0x19) == 0);
    CHECK(read_reg(dev, 0x17) == 0x1f);
    CHECK(io_read(0xcc0, dev) == 0 && irq_mask == 0);
    write_reg(dev, 0x18, 0xa0);
    CHECK(read_reg(dev, 0x19) == 0);
    CHECK(read_reg(dev, 0x17) == 0x20);
    write_reg(dev, 0x18, 0x03);
    CHECK(read_reg(dev, 0x17) == 0x85);
    CHECK(io_read(0xcc0, dev) == 0 && irq_mask == 0);
    CHECK(read_reg(dev, 0x13) == 0 && read_reg(dev, 0x14) == 0);
}
static void test_irq_jumper_readback(void)
{
    const unsigned jumpers[] = {3, 5, 6, 9, 12, 13};
    configured_id = 0;
    /* A target with the host's own ID must never be selected. */
    scsi_devices[0][0].type = SCSI_FIXED_DISK;
    for (unsigned n = 0; n < sizeof(jumpers) / sizeof(jumpers[0]); n++) {
        configured_irq = jumpers[n];
        next_bus = 0;
        void *dev = scsi_pc9801_55_device.init(&scsi_pc9801_55_device);
        CHECK(dev != NULL);
        if (!dev) continue;
        /* Firmware uses bits 3-5 to select its vector and PIC mask table. */
        CHECK(read_reg(dev, 0x33) == n * 8);
        write_reg(dev, 0x30, 0x04);
        write_reg(dev, 0x18, 0x00);
        CHECK(irq_mask == (1u << configured_irq));
        CHECK(read_reg(dev, 0x17) == 0 && irq_mask == 0);
        write_reg(dev, 0x15, 0);
        write_reg(dev, 0x18, 0x07);
        CHECK(read_reg(dev, 0x17) == 0x42 && irq_mask == 0);
        CHECK(!((pc98_55_t *)dev)->selected);
        close_adapter(dev);
    }
    scsi_devices[0][0].type = SCSI_NONE;
    configured_irq = 5;
    configured_id = 7;
}
int main(void)
{
    for (unsigned b = 0; b < SCSI_BUS_MAX; b++)
        for (unsigned id = 0; id < SCSI_ID_MAX; id++) scsi_devices[b][id].type = SCSI_NONE;
    for (unsigned n = 0; n < sizeof(target_buffer); n++) target_buffer[n] = (uint8_t)(n ^ 0x5a);
    target_common.temp_buffer = target_buffer;
    scsi_devices[0][2].type = SCSI_FIXED_DISK;
    scsi_devices[0][2].sc = &target_common;
    CHECK(scsi_pc9801_55_device.available());
    void *dev = scsi_pc9801_55_device.init(&scsi_pc9801_55_device);
    CHECK(dev != NULL);
    if (dev) {
        test_bank_and_reset(dev);
        test_bios_transfer(dev, 0);
        scsi_pc9801_55_device.reset(dev);
        test_bios_transfer(dev, 1);
        close_adapter(dev);
    }
    test_irq_jumper_readback();
    rom_variants = 0;
    CHECK(!scsi_pc9801_55_device.available());
    CHECK(scsi_pc9801_55_device.init(&scsi_pc9801_55_device) == NULL);
    rom_variants = 2; next_bus = 0;
    CHECK(scsi_pc9801_55_device.available());
    dev = scsi_pc9801_55_device.init(&scsi_pc9801_55_device);
    CHECK(dev != NULL); /* saved missing 55L selection falls back to 55U */
    if (dev) close_adapter(dev);
    printf("PC-9801-55: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
