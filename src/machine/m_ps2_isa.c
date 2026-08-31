#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"

#include <86box/timer.h>
#include <86box/io.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/isartc.h>
#include <86box/nmi.h>
#include <86box/nvr.h>
#include <86box/keyboard.h>
#include <86box/lpt.h>
#include <86box/ppi.h>
#include <86box/port_6x.h>
#include <86box/port_92.h>
#include <86box/serial.h>
#include <86box/snd_speaker.h>
#include <86box/hdc.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/video.h>
#include <86box/vid_mcga.h>
#include <86box/machine.h>

typedef struct {
    uint8_t port_61;
    uint8_t port_65;
    uint8_t ram_control;
    uint8_t port_a1;

    void     *kbc;
    void     *mcga;
    void     *hdc;
    void     *rtc;
    fdc_t    *fdc;
    serial_t *uart;
    lpt_t    *lpt;

    mem_mapping_t ram_mapping;
} ps2_m25_t;

typedef struct {
    int model;
    int cpu_type;

    uint8_t ps2_91,
        ps2_92,
        ps2_94,
        ps2_102,
        ps2_103,
        ps2_104,
        ps2_105,
        ps2_190;

    serial_t *uart;
    lpt_t    *lpt;
} ps2_isa_t;

static int
ps2_m25_ram_offset(const ps2_m25_t *dev, uint32_t addr, uint32_t *offset)
{
    if (addr >= 0x000a0000)
        return 0;

    if (addr >= 0x00040000) {
        const unsigned enable_bit = (addr >> 16) - 3;
        if (dev->ram_control & (1 << enable_bit))
            return 0;
    }

    if (dev->ram_control & 0x01) {
        if (addr >= 0x00080000)
            return 0;
        *offset = addr + ((mem_size == 640) ? 0x00020000 : 0);
    } else if (mem_size == 512) {
        if (addr < 0x00020000)
            return 0;
        *offset = addr - 0x00020000;
    } else
        *offset = addr;

    return *offset < ((uint32_t) mem_size << 10);
}

static uint8_t
ps2_m25_ram_read(uint32_t addr, void *priv)
{
    const ps2_m25_t *dev = (ps2_m25_t *) priv;
    uint32_t offset;

    return ps2_m25_ram_offset(dev, addr, &offset) ? ram[offset] : 0xff;
}

static uint16_t
ps2_m25_ram_readw(uint32_t addr, void *priv)
{
    return ps2_m25_ram_read(addr, priv) |
           (ps2_m25_ram_read(addr + 1, priv) << 8);
}

static uint32_t
ps2_m25_ram_readl(uint32_t addr, void *priv)
{
    return ps2_m25_ram_readw(addr, priv) |
           ((uint32_t) ps2_m25_ram_readw(addr + 2, priv) << 16);
}

static void
ps2_m25_ram_write(uint32_t addr, uint8_t val, void *priv)
{
    const ps2_m25_t *dev = (ps2_m25_t *) priv;
    uint32_t offset;

    if (ps2_m25_ram_offset(dev, addr, &offset)) {
        ram[offset] = val;
        mem_invalidate_range(addr, addr);
    }
}

static void
ps2_m25_ram_writew(uint32_t addr, uint16_t val, void *priv)
{
    ps2_m25_ram_write(addr, val, priv);
    ps2_m25_ram_write(addr + 1, val >> 8, priv);
}

static void
ps2_m25_ram_writel(uint32_t addr, uint32_t val, void *priv)
{
    ps2_m25_ram_writew(addr, val, priv);
    ps2_m25_ram_writew(addr + 2, val >> 16, priv);
}

static void
ps2_m25_update_ram(ps2_m25_t *dev)
{
    for (unsigned segment = 0; segment < 10; segment++) {
        uint32_t offset;
        const uint32_t addr = segment << 16;
        const int present = ps2_m25_ram_offset(dev, addr, &offset);

        mem_set_mem_state(addr, 0x00010000,
                          present ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) :
                                    (MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL));
    }

    flushmmucache();
}

static void
ps2_m25_set_control(ps2_m25_t *dev, uint8_t val)
{
    const uint8_t old = dev->port_65;

    /*
       Bit 0 = Fixed disk;
       Bit 1 = LPT;
       Bit 2 = MCGA;
       Bit 3 = FDC;
       Bit 4 = UART;
       Bit 7 = LPT directionality.
     */
    dev->port_65 = val & 0x9f;

    if ((dev->hdc != NULL) && ((old ^ dev->port_65) & 0x01))
        ps1_hdc_handler(dev->hdc, val & 0x01);

    if ((old ^ dev->port_65) & 0x10) {
        if (dev->port_65 & 0x10)
            serial_setup(dev->uart, COM1_ADDR, 4);
        else
            serial_remove(dev->uart);
    }

    if (((old ^ dev->port_65) & 0x08) && dev->fdc) {
        if (dev->port_65 & 0x08)
            fdc_set_base(dev->fdc, FDC_PRIMARY_ADDR);
        else
            fdc_remove(dev->fdc);
    }

    if (((old ^ dev->port_65) & 0x04) && dev->mcga)
        mcga_set_enabled(dev->mcga, dev->port_65 & 0x04);

    if ((old ^ dev->port_65) & 0x02) {
        if (dev->port_65 & 0x02)
            lpt_port_setup(dev->lpt, LPT1_ADDR);
        else
            lpt_port_remove(dev->lpt);
    }

    if ((old ^ dev->port_65) & 0x80)
        lpt_set_output_enabled(dev->lpt, dev->port_65 & 0x80);
}

static void
ps2_m25_write(uint16_t port, uint8_t val, void *priv)
{
    ps2_m25_t *dev = (ps2_m25_t *) priv;

    switch (port) {
        case 0x0061:
            dev->port_61 = val & 0x33;
            ppi.pb = dev->port_61;

            speaker_update();
            speaker_gated  = val & 0x01;
            speaker_enable = val & 0x02;
            if (speaker_enable)
                was_speaker_enable = 1;
            pit_devs[0].set_gate(pit_devs[0].data, 2, val & 0x01);
            break;

        case 0x0065:
            ps2_m25_set_control(dev, val);
            break;

        case 0x006b:
            /* Bit 7 is the read-only parity-check bank pointer. */
            dev->ram_control = val & 0x7f;
            ps2_m25_update_ram(dev);
            break;

        default:
            break;
    }
}

static uint8_t
ps2_m25_read(uint16_t port, void *priv)
{
    const ps2_m25_t *dev = (ps2_m25_t *) priv;

    switch (port) {
        case 0x0061:
            return dev->port_61;

        case 0x0062:
            return (ppispeakon ? 0x20 : 0x00) |
                   ((dev->hdc == NULL) ? 0x04 : 0x00) |
                   (hasfpu ? 0x02 : 0x00) | 0x01;

        case 0x0065:
            return dev->port_65;

        case 0x006b:
            return dev->ram_control;

        default:
            return 0xff;
    }
}

static void
ps2_m30_a1_write(uint16_t port, uint8_t val, void *priv)
{
    ps2_m25_t *dev = (ps2_m25_t *) priv;

    /* The PS/2 Model 30 has a feature where its chipset (called the
     * gate array in Mod. 25/30 parlance) can mask IRQ1 from the RTC;
     * if bit 0 of port 0A1h is 1, the IRQ is masked. The Mod. 25 doesn't
     * have an RTC this feature effectively is irrelevant. */
    dev->port_a1 = val & 0x01;
}

static uint8_t
ps2_m30_a1_read(uint16_t port, void *priv)
{
    ps2_m25_t *dev = (ps2_m25_t *) priv;

    /* Bit 0 mirrors the RTC IRQ1 mask written via ps2_m30_a1_write.
     * We kept this out of ps2_m25_read since it doesn't exist on the
     * Model 25, although we haven't actually check this on real hardware;
     * it might have this port writeable and readable with no actual
     * function. Who knows. */
    return dev->port_a1 | 0xfe;
}

static void
ps2_write(uint16_t port, uint8_t val, void *priv)
{
    ps2_isa_t *ps2 = (ps2_isa_t *) priv;

    switch (port) {
        case 0x0094:
            ps2->ps2_94 = val;
            break;

        case 0x0102:
            if (!(ps2->ps2_94 & 0x80)) {
                lpt_port_remove(ps2->lpt);
                serial_remove(ps2->uart);
                if (val & 0x04) {
                    if (val & 0x08)
                        serial_setup(ps2->uart, COM1_ADDR, COM1_IRQ);
                    else
                        serial_setup(ps2->uart, COM2_ADDR, COM2_IRQ);
                }
                if (val & 0x10) {
                    switch ((val >> 5) & 3) {
                        case 0:
                            lpt_port_setup(ps2->lpt, LPT_MDA_ADDR);
                            break;
                        case 1:
                            lpt_port_setup(ps2->lpt, LPT1_ADDR);
                            break;
                        case 2:
                            lpt_port_setup(ps2->lpt, LPT2_ADDR);
                            break;

                        default:
                            break;
                    }
                }
                ps2->ps2_102 = val;
            }
            break;

        case 0x0103:
            ps2->ps2_103 = val;
            break;

        case 0x0104:
            ps2->ps2_104 = val;
            break;

        case 0x0105:
            ps2->ps2_105 = val;
            break;

        case 0x0190:
            ps2->ps2_190 = val;
            break;

        default:
            break;
    }
}

static uint8_t
ps2_read(uint16_t port, void *priv)
{
    ps2_isa_t *ps2  = (ps2_isa_t *) priv;
    uint8_t    temp = 0xff;

    switch (port) {
        case 0x0091:
            temp        = ps2->ps2_91;
            ps2->ps2_91 = 0;
            break;

        case 0x0094:
            temp = ps2->ps2_94;
            break;

        case 0x0102:
            temp = ps2->ps2_102 | 0x08;
            break;

        case 0x0103:
            temp = ps2->ps2_103;
            break;

        case 0x0104:
            temp = ps2->ps2_104;
            break;

        case 0x0105:
            temp = ps2->ps2_105;
            break;

        case 0x0190:
            temp = ps2->ps2_190;
            break;

        default:
            break;
    }

    return temp;
}

static const device_config_t ps2_m25_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "ibmps2_m25_type1",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Planar Type 1 (00F2092/00F2093, 06/26/87)",
                .internal_name = "ibmps2_m25_type1",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/ibmps2_m25/00F2092.BIN",
                                   "roms/machines/ibmps2_m25/00F2093.BIN", "" }
            },
            {
                .name          = "Planar Type 2 (00F2122/00F2123, 06/26/87)",
                .internal_name = "ibmps2_m25_type2",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/ibmps2_m25/00F2122.BIN",
                                   "roms/machines/ibmps2_m25/00F2123.BIN", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ps2_m25_device = {
    .name          = "IBM PS/2 model 25 (8086, color)",
    .internal_name = "ibmps2_m25",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ps2_m25_config
};

static const device_config_t ps2_m30_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "ibmps2_m30_rev0",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Revision 0 (68X1687/68X1627, 09/02/86; slashed 0 font)",
                .internal_name = "ibmps2_m30_rev0",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/ibmps2_m30/68X1687.BIN",
                                   "roms/machines/ibmps2_m30/68X1627.BIN", "" }
            },
            {
                .name          = "Revision 1 (61X8938/61X8937, 12/12/86)",
                .internal_name = "ibmps2_m30_rev1",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/ibmps2_m30/61X8938.BIN",
                                   "roms/machines/ibmps2_m30/61X8937.BIN", "" }
            },
            {
                .name          = "Revision 2 (61X8940/61X8939, 02/05/87)",
                .internal_name = "ibmps2_m30_rev2",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/ibmps2_m30/61X8940.BIN",
                                   "roms/machines/ibmps2_m30/61X8939.BIN", "" }
            },
            {
                .name          = "Revision 4 (33F4498/33F4499, 01/31/89)",
                .internal_name = "ibmps2_m30_rev4",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/ibmps2_m30/33F4498.BIN",
                                   "roms/machines/ibmps2_m30/33F4499.BIN", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ps2_m30_device = {
    .name          = "IBM PS/2 model 30 (8086)",
    .internal_name = "ibmps2_m30",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ps2_m30_config
};

static const device_config_t ps2_m30_286_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "ibmps2_m30_286",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Model 30-286 rev. 0 BIOS",
                .internal_name = "ibmps2_m30_286_rev0",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ibmps2_m30_286/27F4092.BIN", "" }
            },
            {
                .name          = "Model 30-286 rev. 2 BIOS",
                .internal_name = "ibmps2_m30_286",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/ibmps2_m30_286/33f5381a.bin", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ps2_m30_286_device = {
    .name          = "IBM PS/2 model 30-286",
    .internal_name = "ibmps2_m30_286",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ps2_m30_286_config
};


static void
ps2_isa_setup(int model, int cpu_type)
{
    ps2_isa_t *ps2;
    void      *priv;

    ps2           = (ps2_isa_t *) calloc(1, sizeof(ps2_isa_t));
    ps2->model    = model;
    ps2->cpu_type = cpu_type;

    io_sethandler(0x0091, 1,
                  ps2_read, NULL, NULL, ps2_write, NULL, NULL, ps2);
    io_sethandler(0x0094, 1,
                  ps2_read, NULL, NULL, ps2_write, NULL, NULL, ps2);
    io_sethandler(0x0102, 4,
                  ps2_read, NULL, NULL, ps2_write, NULL, NULL, ps2);
    io_sethandler(0x0190, 1,
                  ps2_read, NULL, NULL, ps2_write, NULL, NULL, ps2);

    ps2->uart = device_add_inst(&ns16450_device, 1);

    ps2->lpt = device_add_inst(&lpt_port_device, 1);
    lpt_set_ext(ps2->lpt, 1);

    lpt_port_remove(ps2->lpt);
    lpt_port_setup(ps2->lpt, LPT_MDA_ADDR);

    device_add(&port_92_device);

    mem_remap_top(384);

    device_add(&fdc_ps2_device);

    /* Enable the builtin HDC. */
    if (hdc_current[0] == HDC_INTERNAL) {
        priv = device_add(&ps1_hdc_device);
        ps1_hdc_inform(priv, &ps2->ps2_91);
    }

    device_add(&ps1vga_device);
}

static void
ps2_isa_common_init(const machine_t *model)
{
    machine_common_init(model);

    refresh_at_enable = 1;
    pit_devs[0].set_out_func(pit_devs[0].data, 1, pit_refresh_timer_at);

    dma16_init();
    pic2_init();

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&port_6x_ps2_device);
}

uint8_t
machine_ps2_isa_p1_handler(void)
{
    uint8_t mem_p1;

    switch (mem_size / 1024) {
        case 0: /*256Kx2*/
            mem_p1 = 0xb0;
            break;       
        case 1: /*256Kx4*/
            mem_p1 = 0xa0;
            break;
        case 2: /*1Mx2*/
        case 3: 
            mem_p1 = 0x90;
            break;
        case 4: /*1Mx4*/
        default:
            mem_p1 = 0x80;
            break;
    }

    return mem_p1;
}

int
machine_ps2_8086_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn[2];
    ps2_m25_t  *dev;

    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn[0] = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
    fn[1] = device_get_bios_file(model->device, device_get_config_bios("bios"), 1);
    ret = bios_load_interleaved(fn[0], fn[1], 0x000f0000, 65536, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    machine_common_init(model);
    /*
     * Port A0h bit 7 is the global NMI enable. The latch powers up enabled;
     * IBM's system-board diagnostic relies on that reset state when it
     * exercises the diagnostic NMI path through ports 69h and 63h.
     */
    nmi_mask = 0x80;

    dev = (ps2_m25_t *) calloc(1, sizeof(ps2_m25_t));

    /*
     * A 512K Model 25 physically has RAM from 20000h through 9FFFFh.
     * Port 6Bh remaps that bank down when the optional low 128K is absent.
     */
    mem_mapping_disable(&ram_low_mapping);
    mem_mapping_add(&dev->ram_mapping, 0x00000000, 0x000a0000,
                    ps2_m25_ram_read, ps2_m25_ram_readw, ps2_m25_ram_readl,
                    ps2_m25_ram_write, ps2_m25_ram_writew, ps2_m25_ram_writel,
                    NULL, MEM_MAPPING_INTERNAL, dev);
    dev->ram_control = 0x00;
    ps2_m25_update_ram(dev);

    io_sethandler(0x0061, 2,
                  ps2_m25_read, NULL, NULL,
                  ps2_m25_write, NULL, NULL, dev);
    io_sethandler(0x0065, 1,
                  ps2_m25_read, NULL, NULL,
                  ps2_m25_write, NULL, NULL, dev);
    io_sethandler(0x006b, 1,
                  ps2_m25_read, NULL, NULL,
                  ps2_m25_write, NULL, NULL, dev);

    dev->kbc = device_add_params(machine_get_kbc_device(machine),
                                 (void *) model->kbc_params);
    /*
     * The gate-array interrupt controller vectors internal IRQ1 sources to
     * 71h. BIOS then dispatches keyboard data to the programmable PIC's
     * normal IRQ1 vector and pointing-device data to interrupt 73h.
     */
    pic_set_vector_override(1, 0x71);

    dev->uart = device_add_inst(&ns8250_device, 1);
    dev->lpt  = device_add_inst(&lpt_port_device, 1);
    lpt_set_ext(dev->lpt, 1);

    if (fdc_current[0] == FDC_INTERNAL)
        dev->fdc = device_add(&fdc_ps2_device);

    /* Enable the builtin HDC. */
    if (hdc_current[0] == HDC_INTERNAL)
        dev->hdc = device_add(&ps2_m25_hdc_device);

    if ((gfxcard[0] == VID_INTERNAL) ||
        ((gfxcard[0] >= VID_INTERNAL) &&
         (strcmp(video_get_internal_name(gfxcard[0]), "vga") == 0)))
        dev->mcga = device_add(&mcga_device);

    /* All integrated chip selects and the parallel output drivers power up on. */
    dev->port_65 = 0x9f;

    if (strcmp(machine_get_internal_name(), "ibmps2_m30") == 0) {
        /* PS/2 Mod. 30 has an MM58167 RTC with some quirks. Its alarm
         * is wired to IRQ1 and can be masked off by the gate away (the
         * chipset). */
        dev->rtc = device_add(&ibmps2m30_rtc_device);
        ibmps2m30_rtc_inform(dev->rtc, &dev->port_a1);
        io_sethandler(0x00a1, 1,
                      ps2_m30_a1_read, NULL, NULL,
                      ps2_m30_a1_write, NULL, NULL, dev);
    }

    return ret;
}

int
machine_ps2_m30_286_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000e0000, 131072, 0);
    device_context_restore();

    ps2_isa_common_init(model);

    ps2_isa_setup(30, 286);

    return ret;
}
