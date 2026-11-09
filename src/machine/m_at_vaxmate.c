/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of DEC VAXmate Workstation.
 *
 *          Hardware per AA-HD92A-TK TRM Vol.1 chapter 2:
 *            80286 @ 8 MHz, 1 MB on board, up to 3 MB with PC50X-AA
 *            2 x 8259A, 8254-2, MC146818, 8237A, 8042 (DEC firmware)
 *            uPD765A floppy controller 03F0-03F7 (IRQ6, DMA ch2)
 *            DIGITAL video controller  6845 + translation ROM + VLT
 *            System CSR 1              0C00-0C1F, special register 0C80
 *            Am7990 LANCE              0C60/0C62/0C68
 *
 * Authors: WNT50
 *
 *          Copyright 2026 WNT50.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/serial.h>
#include <86box/mouse.h>
#include <86box/fdc.h>
#include <86box/hdc.h>
#include <86box/rom.h>
#include <86box/nvr.h>
#include <86box/video.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/net_lance.h>

typedef struct {
    uint8_t  mem[32];
    uint8_t  ptr;
    uint8_t  csr; /* last value written to CSR1, read back by the BIOS */
    uint8_t  sel, clk, wen;
    uint8_t  bitcnt;
    uint8_t  frame[36];
    uint8_t  valid;
} vaxmate_nvm_t;

static vaxmate_nvm_t vaxmate_nvm;

/* System CSR 1 (0x0C00) is a bit-serial port to the 256-bit NVM (TRM p7-23):
   0 = 8254 counter 2 gate, 1 = write enable, 2 = diagnostic LED, 3 = NVR valid
   (read only), 4 = DI, 5 = CLK, 6 = /CS, 7 = DO. Only the SETUP waveform is
   decoded: start bit, 7-step low nibble, separator, 7-step high nibble,
   20 clocks; the write pointer is sequential. */
static const uint8_t nvm_rec[16] = {
    0x7e, 0x06, 0x6d, 0x4f, 0x17, 0x5b, 0x7b, 0x0e,
    0x7f, 0x1f, 0x3f, 0x73, 0x78, 0x67, 0x79, 0x39
};

#ifdef ENABLE_VAXMATE_LOG
int vaxmate_do_log = ENABLE_VAXMATE_LOG;

static void
vaxmate_log(const char *fmt, ...)
{
    va_list ap;

    if (vaxmate_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define vaxmate_log(fmt, ...)
#endif

static uint8_t
vaxmate_nvm_decode(const uint8_t *bits)
{
    uint8_t v = 0, i, n;

    for (i = 0; i < 7; i++)
        v = (uint8_t) ((v << 1) | bits[i]);

    for (n = 0; n < 16; n++) {
        if (nvm_rec[n] == v)
            return n;
    }

    return 0xff;
}

static void
vaxmate_nvm_endframe(void)
{
    uint8_t lo, hi;

    vaxmate_nvm.bitcnt = 0;

    if (vaxmate_nvm.frame[0] != 1)
        return;

    lo = vaxmate_nvm_decode(&vaxmate_nvm.frame[1]);
    hi = vaxmate_nvm_decode(&vaxmate_nvm.frame[9]);

    if ((lo == 0xff) || (hi == 0xff)) {
        vaxmate_log("NVM: invalid frame (lo=%02X hi=%02X)\n", lo, hi);
        return;
    }

    vaxmate_log("NVM: byte %02X -> offset %02X\n", (uint8_t) ((hi << 4) | lo), vaxmate_nvm.ptr & 0x1f);
    vaxmate_nvm.mem[vaxmate_nvm.ptr++ & 0x1f] = (uint8_t) ((hi << 4) | lo);
}

static uint8_t
vaxmate_csr_in(uint16_t addr, void *priv)
{
    if (addr != 0x0c00)
        return 0xff;

    /* The remaining bits read back the last value written: the ROM at F409A
       tests bit 2 to pick the "system disk / boot device" branch. Bit 3 is
       the read-only NVR valid bit. */
    return (uint8_t) (vaxmate_nvm.csr | (vaxmate_nvm.valid ? 0x08 : 0x00));
}

static void
vaxmate_csr_out(uint16_t addr, uint8_t val, void *priv)
{
    uint8_t sel, clk, di, we;

    if (addr != 0x0c00)
        return;

    sel = (val >> 6) & 1;
    clk = (val >> 5) & 1;
    di  = (val >> 4) & 1;
    we  = (val >> 1) & 1;

    vaxmate_nvm.wen = we;
    vaxmate_nvm.csr = val;

    if (sel) {
        /* The frame is over, /CS released. */
        if (vaxmate_nvm.bitcnt >= 36)
            vaxmate_nvm_endframe();
        vaxmate_nvm.bitcnt = 0;
        vaxmate_nvm.sel    = sel;
        vaxmate_nvm.clk    = clk;
        return;
    }

    if (clk && !vaxmate_nvm.clk) {
        if (vaxmate_nvm.bitcnt < 36)
            vaxmate_nvm.frame[vaxmate_nvm.bitcnt] = di;
        vaxmate_nvm.bitcnt++;

        if (vaxmate_nvm.bitcnt == 36)
            vaxmate_nvm_endframe();
    }

    vaxmate_nvm.sel = sel;
    vaxmate_nvm.clk = clk;
}

/* Ethernet ROM window (0x0C20-0x0C27): 6-byte station address followed by its checksum,
   high byte first; the POST reports error 0x0D when the checksum does not match. */
static uint8_t vaxmate_ethrom[8] = { 0x08, 0x00, 0x2b, 0x12, 0x34, 0x56, 0x00, 0x00 };

static void
vaxmate_ethrom_build(void)
{
    uint32_t bx = 0;
    int      i;

    for (i = 0; i < 6; i += 2) {
        bx = ((bx << 1) | (bx >> 15)) & 0xffff;
        bx += (uint32_t) ((vaxmate_ethrom[i] << 8) | vaxmate_ethrom[i + 1]);
        if (bx > 0xffff)
            bx = (bx & 0xffff) + 1;
    }

    if (bx == 0xffff)
        bx = 0;
    else if (bx == 0)
        bx = 0xffff;

    vaxmate_ethrom[6] = (uint8_t) (bx >> 8);
    vaxmate_ethrom[7] = (uint8_t) (bx & 0xff);
}

static uint8_t
vaxmate_ethrom_in(uint16_t addr, void *priv)
{
    return vaxmate_ethrom[addr - 0x0c20];
}

/* The 1 MB board RAM appears in three non-overlapping windows (TRM 2.5.1,
   Figure 2-2): 640 KB at 000000, the 128 KB DEC private area at 0D0000 and
   the 256 KB RAM disk at F20000. The last two are backed by the 1 MB array;
   the offsets are the contiguous split that adds up to 1 MB. */
typedef struct {
    uint32_t base;
    uint32_t off;
} vaxmate_ramwin_t;

static const vaxmate_ramwin_t vaxmate_priv_win = { 0x000d0000, 0x000a0000 };
static const vaxmate_ramwin_t vaxmate_hi_win   = { 0x00f20000, 0x000c0000 };

static uint8_t
vaxmate_win_rb(uint32_t addr, void *priv)
{
    const vaxmate_ramwin_t *w = (const vaxmate_ramwin_t *) priv;

    return ram[(w->off + (addr - w->base)) & rammask];
}

static uint16_t
vaxmate_win_rw(uint32_t addr, void *priv)
{
    const vaxmate_ramwin_t *w = (const vaxmate_ramwin_t *) priv;

    return *(uint16_t *) &ram[(w->off + (addr - w->base)) & rammask];
}

static uint32_t
vaxmate_win_rl(uint32_t addr, void *priv)
{
    const vaxmate_ramwin_t *w = (const vaxmate_ramwin_t *) priv;

    return *(uint32_t *) &ram[(w->off + (addr - w->base)) & rammask];
}

static void
vaxmate_win_wb(uint32_t addr, uint8_t val, void *priv)
{
    const vaxmate_ramwin_t *w = (const vaxmate_ramwin_t *) priv;

    ram[(w->off + (addr - w->base)) & rammask] = val;
}

static void
vaxmate_win_ww(uint32_t addr, uint16_t val, void *priv)
{
    const vaxmate_ramwin_t *w = (const vaxmate_ramwin_t *) priv;

    *(uint16_t *) &ram[(w->off + (addr - w->base)) & rammask] = val;
}

static void
vaxmate_win_wl(uint32_t addr, uint32_t val, void *priv)
{
    const vaxmate_ramwin_t *w = (const vaxmate_ramwin_t *) priv;

    *(uint32_t *) &ram[(w->off + (addr - w->base)) & rammask] = val;
}

static void     *vaxmate_vid;
static serial_t *vaxmate_com1;
static uint8_t   vaxmate_spr_val;

/* The special purpose register at 0C80 (TRM page 7-23) is a system register:
   bit 3 hands the video block to the bus, bit 2 selects split baud rates
   (receive fixed at 1200, transmit programmable), and the diskette
   status bits are read-only and driven by the FDC. */
static uint8_t
vaxmate_spr_in(uint16_t addr, void *priv)
{
    (void) addr;
    (void) priv;

    return vaxmate_spr_val;
}

static void
vaxmate_spr_out(uint16_t addr, uint8_t val, void *priv)
{
    (void) addr;
    (void) priv;

    vaxmate_spr_val = val;
    vaxmate_video_spr_set(vaxmate_vid, val);
    serial_set_split_rx(vaxmate_com1, (val & 0x04) ? 1200 : 0);
}

/* Some callers use an OUT DX,AX (16-bit) access for the special register. */
static void
vaxmate_spr_outw(uint16_t addr, uint16_t val, void *priv)
{
    vaxmate_spr_out(addr, (uint8_t) val, priv);
}

static const device_config_t vaxmate_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "vaxmate_073186",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "DEC VAXmate BIOS 07/31/86",
                .internal_name = "vaxmate_073186",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/vaxmate/23-200e6.bin",
                                   "roms/machines/vaxmate/23-199e6.bin", "" }
            },
            {
                .name          = "DEC VAXmate BIOS 05/18/87",
                .internal_name = "vaxmate_051887",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/vaxmate/310E6ROM.BIN",
                                   "roms/machines/vaxmate/311E6ROM.BIN", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t vaxmate_device = {
    .name          = "DEC VAXmate",
    .internal_name = "vaxmate",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = vaxmate_config
};

static mem_mapping_t vaxmate_hi_mapping;
static mem_mapping_t vaxmate_priv_mapping;

int
machine_at_vaxmate_init(const machine_t *model)
{
    const char *bios_even;
    const char *bios_odd;
    int         macval;
    int         ret = 0;

    vaxmate_vid     = NULL;
    vaxmate_com1    = NULL;
    vaxmate_spr_val = 0x08; /* video enabled at power-on */

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    /* The 64 KB BIOS is two 32 KB EPROMs: even bytes first, then odd bytes. */
    device_context(model->device);
    bios_even = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    bios_odd  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 1);
    ret       = bios_load_interleaved(bios_even, bios_odd, 0x000f0000, 65536, 0);
    device_context_restore();

    vaxmate_log("BIOS: %s + %s -> %d\n", bios_even, bios_odd, ret);

    if (bios_only || !ret)
        return ret;

    machine_at_init(model);

    /* TRM memory map: 0D0000-0EFFFF is DIGITAL private RAM. The BIOS builds
       its extended data block at EFC0:0000 and AH=15h uses it to bank DS. */
    mem_set_mem_state_both(0x000d0000, 0x00020000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
    mem_mapping_add(&vaxmate_priv_mapping, 0x000d0000, 0x00020000,
                    vaxmate_win_rb, vaxmate_win_rw, vaxmate_win_rl,
                    vaxmate_win_wb, vaxmate_win_ww, vaxmate_win_wl,
                    NULL, MEM_MAPPING_INTERNAL, (void *) &vaxmate_priv_win);

    /* F20000-F5FFFF holds the remaining 256 KB of the 1 MB array (Figure 2-2,
       "RAM DISK"). Only protected mode can form this address, so no gating. */
    mem_set_mem_state_both(0x00f20000, 0x00040000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
    mem_mapping_add(&vaxmate_hi_mapping, 0x00f20000, 0x00040000,
                    vaxmate_win_rb, vaxmate_win_rw, vaxmate_win_rl,
                    vaxmate_win_wb, vaxmate_win_ww, vaxmate_win_wl,
                    NULL, MEM_MAPPING_INTERNAL, (void *) &vaxmate_hi_win);

    device_add(&nvr_at_device);

    device_add(&fdc_at_device);

    if (hdc_current[0] == HDC_INTERNAL)
        device_add(&st506_wd2010_device);

    /* COM1 is a WD8250 ACE (EK-PC500-TD 3.7.2); the ns8250 model has no
       scratch register and no FIFO, which matches the part. */
    vaxmate_com1 = device_add_inst(&ns8250_device, 1);

    /* Serial printer port (EK-PC500-TD 3.6.2): a second WD8250 at 0CA0/IRQ11.
       It takes COM2's slot so the settings page can attach a backend, and
       serial_setup() then moves the port from 2F8 to its real address. */
    serial_t *vaxmate_printer = device_add_inst(&ns8250_device, 2);
    serial_setup(vaxmate_printer, 0x0ca0, 11);
    /* On-board UART: it must answer POST even with COM2 off (an
       unmapped port reads 0xFF and stalls the XON/XOFF loop), so map it here;
       serial_setup() skips registration then. Only the backend follows COM2. */
    if ((vaxmate_printer != NULL) && (vaxmate_printer->base_address == 0))
        io_sethandler(0x0ca0, 0x0008,
                      serial_read, NULL, NULL,
                      serial_write, NULL, NULL,
                      vaxmate_printer);

    if (gfxcard[0] == VID_INTERNAL)
        vaxmate_vid = device_add(machine_get_vid_device(machine));

    /* On-board mouse at 0x0C40/IRQ12: standalone, takes no COM slot and bypasses com_ports[]. */
    void *vaxmate_mouse = device_add_params(&scn2661c_device, (void *) 0xff);
    io_sethandler(0x0c40, 0x0004,
                  scn26x1_read, NULL, NULL,
                  scn26x1_write, NULL, NULL,
                  vaxmate_mouse);
    scn26x1_set_irq(vaxmate_mouse, 0x1000);

    if (mouse_type == MOUSE_TYPE_INTERNAL) {
        serial_endpoint_t *vaxmate_mouse_ep;

        /* The DEC mouse supplies the endpoint's backend; be and on_attach are
           public fields, so the board wires them directly. */
        vaxmate_log("Mouse: DEC mouse attached to SCN2661C backend\n");
        device_add(&vaxmate_mouse_device);
        vaxmate_mouse_ep = scn26x1_endpoint(vaxmate_mouse);
        if (vaxmate_mouse_ep != NULL) {
            vaxmate_mouse_ep->be = vaxmate_mouse_backend();
            if (vaxmate_mouse_ep->on_attach != NULL)
                vaxmate_mouse_ep->on_attach(vaxmate_mouse_ep->chip);
        }
    }
    /* The on-board NIC uses the 86Box "Internal" convention: .net_device in
       the machine table, instantiated only when Internal is selected. Its MAC
       is normalised inside the LANCE init (randomised on first use), so add it
       first and then mirror the low three bytes into the station address ROM. */
    if ((net_cards_conf[0].device_num == NET_INTERNAL) && machine_get_net_device(machine)) {
        device_add(machine_get_net_device(machine));

        device_context(machine_get_net_device(machine));
        macval = device_get_config_mac("mac", 0);
        device_context_restore();

        vaxmate_ethrom[3] = (uint8_t) ((macval >> 16) & 0xff);
        vaxmate_ethrom[4] = (uint8_t) ((macval >> 8) & 0xff);
        vaxmate_ethrom[5] = (uint8_t) (macval & 0xff);
        vaxmate_log("NIC: station address %02X:%02X:%02X:%02X:%02X:%02X\n",
                    vaxmate_ethrom[0], vaxmate_ethrom[1], vaxmate_ethrom[2],
                    vaxmate_ethrom[3], vaxmate_ethrom[4], vaxmate_ethrom[5]);
    }

    io_sethandler(0x0c00, 0x0020,
                  vaxmate_csr_in, NULL, NULL,
                  vaxmate_csr_out, NULL, NULL,
                  NULL);

    io_sethandler(0x0c80, 0x0001,
                  vaxmate_spr_in, NULL, NULL,
                  vaxmate_spr_out, vaxmate_spr_outw, NULL,
                  NULL);

    vaxmate_ethrom_build();

    /* NVM image: station address, its checksum, then the DIGITAL
       configuration word (video = 010b, LK250 present, two RX33 drives). */
    memcpy(&vaxmate_nvm.mem[0], vaxmate_ethrom, 6);
    vaxmate_nvm.mem[6]  = vaxmate_ethrom[6];
    vaxmate_nvm.mem[7]  = vaxmate_ethrom[7];
    vaxmate_nvm.mem[8]  = 0x5a;
    vaxmate_nvm.mem[9]  = 0x00;
    memset(&vaxmate_nvm.mem[10], 0xff, 22);
    vaxmate_nvm.valid = 1;
    vaxmate_nvm.wen   = 1; /* bit1 high at power-on: F0034 picks the boot path with it */
    vaxmate_nvm.csr   = 0x02;
    io_sethandler(0x0c20, 0x0008,
                  vaxmate_ethrom_in, NULL, NULL,
                  NULL, NULL, NULL,
                  NULL);

    return ret;
}
