/* Descriptor DMA used by Rage IIC and Rage Pro. Exercise actual guest
   tables, partial register writes, enable gates and register packets. */
#include "../../src/video/vid_ati_mach64_ragepro.c"

static uint8_t  test_ram[0x20000];
static unsigned failures, eols, writes, waits;
static uint32_t write_addr[16], write_val[16];

#define CHECK(c)                                       \
    do {                                               \
        if (!(c)) {                                    \
            fprintf(stderr, "%d: %s\n", __LINE__, #c); \
            failures++;                                \
        }                                              \
    } while (0)

void
pclog(const char *fmt, ...)
{
    (void) fmt;
}
void
mach64_wait_fifo_idle(mach64_t *m)
{
    (void) m;
    waits++;
}
void
mach64_ragepro_bm_eol(mach64_t *m)
{
    m->crtc_int_cntl |= 1u << 25;
    eols++;
}
void
mach64_ext_writel(uint32_t addr, uint32_t val, void *priv)
{
    (void) priv;
    CHECK(writes < 16);
    if (writes < 16) {
        write_addr[writes]  = addr;
        write_val[writes++] = val;
    }
}
void
mach64_queue(mach64_t *m, uint32_t addr, uint32_t val, uint32_t type)
{
    (void) m;
    (void) addr;
    (void) val;
    (void) type;
    CHECK(0);
}
int
mach64_3d_setup_read(mach64_t *m, int reg, uint32_t *val)
{
    (void) m;
    (void) reg;
    (void) val;
    CHECK(0);
    return 0;
}
/* Unused host services of the Pro's I2C and diagnostic paths. */
void *
i2c_gpio_get_bus(void *p)
{
    return p;
}
uint8_t
i2c_start(void *p, uint8_t a, uint8_t r)
{
    (void) p;
    (void) a;
    (void) r;
    return 0;
}
uint8_t
i2c_read(void *p, uint8_t a)
{
    (void) p;
    (void) a;
    return 0xff;
}
uint8_t
i2c_write(void *p, uint8_t a, uint8_t v)
{
    (void) p;
    (void) a;
    (void) v;
    return 0;
}
void
i2c_stop(void *p, uint8_t a)
{
    (void) p;
    (void) a;
}
void
mach64_timing_dump(mach64_t *m)
{
    (void) m;
}
void
mach64_3d_ragepro_dump(mach64_t *m)
{
    (void) m;
}
uint32_t
mach64_ext_readl(uint32_t addr, void *p)
{
    (void) addr;
    (void) p;
    return 0;
}
void
dma_bm_read(uint32_t addr, uint8_t *buf, uint32_t len, int width)
{
    (void) width;
    CHECK(addr <= sizeof(test_ram) && len <= sizeof(test_ram) - addr);
    if (addr <= sizeof(test_ram) && len <= sizeof(test_ram) - addr)
        memcpy(buf, test_ram + addr, len);
}
void
dma_bm_write(uint32_t addr, const uint8_t *buf, uint32_t len, int width)
{
    (void) width;
    CHECK(addr <= sizeof(test_ram) && len <= sizeof(test_ram) - addr);
    if (addr <= sizeof(test_ram) && len <= sizeof(test_ram) - addr)
        memcpy(test_ram + addr, buf, len);
}

static void
descriptor(uint32_t addr, uint32_t fb, uint32_t sys, uint32_t cmd)
{
    uint32_t d[4] = { fb, sys, cmd, 0 };
    memcpy(test_ram + addr, d, sizeof(d));
}

static void
system_transfers(mach64_t *m)
{
    uint32_t value;
    for (unsigned i = 0; i < 32; i++)
        test_ram[0x1000 + i] = i ^ 0x5a;
    descriptor(0x100, 0x2000, 0x1000, BM_CMD_EOL | 32);

    /* PCI command and BUS_MASTER_DIS must independently block DMA. */
    m->pci_regs[PCI_REG_COMMAND] = 0;
    mach64_ragepro_blk1_write(m, RP1_BM_SYSTEM_TABLE, 0x100, 4);
    CHECK(eols == 0 && m->svga.vram[0x2000] == 0);
    m->pci_regs[PCI_REG_COMMAND] = PCI_COMMAND_L_BM;
    m->bus_cntl                  = BUS_MASTER_DIS;
    mach64_ragepro_blk1_write(m, RP1_BM_SYSTEM_TABLE, 0x100, 4);
    CHECK(eols == 0);
    m->bus_cntl = 0;

    /* The trigger is the completed high byte, not its low-byte writes. */
    mach64_ragepro_blk1_write(m, RP1_BM_SYSTEM_TABLE, 0x100, 2);
    CHECK(eols == 0);
    mach64_ragepro_blk1_write(m, RP1_BM_SYSTEM_TABLE + 2, 0, 2);
    CHECK(eols == 1 && waits == 1);
    CHECK(!memcmp(m->svga.vram + 0x2000, test_ram + 0x1000, 32));
    CHECK(m->svga.changedvram[2] == 17);
    CHECK(m->crtc_int_cntl & (1u << 25));
    CHECK(mach64_ragepro_blk1_read(m, RP1_BM_SYSTEM_MEM_ADDR, &value) && value == 0x1000);

    /* The reverse direction copies back from the framebuffer. */
    memset(test_ram + 0x1000, 0, 32);
    mach64_ragepro_blk1_write(m, RP1_BM_SYSTEM_TABLE, 0x101, 4);
    CHECK(eols == 2 && !memcmp(m->svga.vram + 0x2000, test_ram + 0x1000, 32));

    /* Descriptor chains can cross the end of physical video memory. */
    descriptor(0x100, 0xfffc, 0x1000, 4);
    descriptor(0x110, 0, 0x1004, BM_CMD_EOL | 4);
    mach64_ragepro_blk1_write(m, RP1_BM_SYSTEM_TABLE, 0x100, 4);
    CHECK(!memcmp(m->svga.vram + 0xfffc, test_ram + 0x1000, 4));
    CHECK(!memcmp(m->svga.vram, test_ram + 0x1004, 4));
}

static void
gui_transfers(mach64_t *m)
{
    const uint32_t packet[] = { (1u << 16) | 0xb0, 0x12345678, 0xabcdef01 };
    uint32_t       value;
    writes = 0;
    memcpy(test_ram + 0x1000, packet, sizeof(packet));
    /* Split a packet across descriptors; BM_ADDR state must survive. */
    descriptor(0x4000, 0x7ffe48, 0x1000, BM_CMD_HOLD_OFFSET | 8);
    descriptor(0x4010, 0x7ffe48, 0x1008, BM_CMD_HOLD_OFFSET | BM_CMD_EOL | 4);
    CHECK(mach64_ragepro_gui_write(m, RP0_BM_GUI_TABLE_CMD, 0x4000, FIFO_WRITE_DWORD));
    CHECK(!mach64_ragepro_gui_write(m, RP0_SRC_CNTL, SRC_BM_ENABLE | (3u << SRC_BM_OP_SHIFT), FIFO_WRITE_DWORD));
    CHECK(mach64_ragepro_gui_write(m, RP0_DST_HEIGHT_WIDTH, 1, FIFO_WRITE_DWORD));
    CHECK(writes == 2 && write_addr[0] == 0x6c0 && write_addr[1] == 0x6c4);
    CHECK(write_val[0] == packet[1] && write_val[1] == packet[2]);
    CHECK(mach64_ragepro_blk1_read(m, RP1_BM_GUI_TABLE, &value) && value == 0x4020);
    CHECK(!m->rp.running && !m->rp.pkt_count);
}

int
main(void)
{
    mach64_t *m       = calloc(1, sizeof(*m));
    monitor_t monitor = { 0 };
    uint32_t  value;
    if (!m)
        return 1;
    m->svga.vram        = calloc(1, 0x10000);
    m->svga.changedvram = calloc(16, sizeof(*m->svga.changedvram));
    if (!m->svga.vram || !m->svga.changedvram)
        return 1;
    m->svga.monitor              = &monitor;
    monitor.mon_changeframecount = 17;
    m->vram_mask                 = 0xffff;
    m->rage2c                    = 1;
    CHECK(mach64_has_busmaster(m));
    CHECK(!mach64_ragepro_blk1_write(m, RP1_SETUP_FIRST, 0, 4));
    CHECK(!mach64_ragepro_blk1_read(m, RP1_SETUP_FIRST, &value));
    CHECK(!mach64_ragepro_blk1_write(m, RP1_AGP_BASE, 0, 4));
    CHECK(!mach64_ragepro_blk1_read(m, RP1_GUI_CNTL, &value));
    system_transfers(m);
    gui_transfers(m);
    /* The same transfer engine must continue to work for the Pro. */
    m->rage2c  = 0;
    m->ragepro = 1;
    mach64_ragepro_reset(m);
    CHECK(mach64_ragepro_blk1_write(m, RP1_AGP_BASE, 0x12300000, 4));
    CHECK(mach64_ragepro_blk1_read(m, RP1_AGP_BASE, &value) && value == 0x12300000);
    eols = waits = writes = 0;
    memset(m->svga.vram, 0, 0x10000);
    system_transfers(m);
    gui_transfers(m);
    free(m->svga.vram);
    free(m->svga.changedvram);
    free(m);
    printf("Mach64 IIC/Pro bus master: %u failed checks\n", failures);
    return failures != 0;
}
