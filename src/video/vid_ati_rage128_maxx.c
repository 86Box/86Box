/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage Fury MAXX -- two Rage 128 Pro chips on one AGP board,
 *          alternate-frame rendering.
 *
 *          The board is two chip instances plus the logic between them.
 *          On AGP a chip's idsel# strap picks AD16 or AD17 as its IDSEL,
 *          that is device 0 or 1 on the AGP bus (GCS: External Straps,
 *          p. 4-32 / PDF 80). The board is modeled with chip A as device 0
 *          and chip B as device 1. Chip A is the system VGA.
 *          Chip B is modeled with the vga_disable strap, which makes a chip
 *          "only an extended mode controller" (same page), so only chip A
 *          decodes the legacy VGA ranges. One video BIOS image sits behind
 *          chip A's expansion ROM BAR.
 *
 *          The two chips share one monitor connector. The chip documents
 *          do not describe the board logic that picks which chip's CRTC
 *          drives it; this file models it from what the ATI Windows 98
 *          driver (driver CD 1.10) does with the AMCGPIO pads of both
 *          chips. The DirectDraw flip strobes a pad to hand the connector
 *          to a chip (RE: ATI3DRAU.DLL @b00bf810), and the inter-driver
 *          module locks chip B's raster to chip A's (RE: ATI2I9AU.DLL
 *          @0002:702a, segment 2 offset 702a of that 16-bit DLL). The pin
 *          table is below.
 *
 *          The board also carries a Philips PCF8591, an 8-bit A/D and D/A
 *          converter with an I2C interface, which the driver programs
 *          through chip B's hardware I2C master. It is modeled here as a
 *          device on chip B's multimedia I2C bus; chip A's bus, like a
 *          single card's, has nothing on it.
 *
 *          Outside this file the board needs pci_register_sibling_slot in
 *          pci.c and its vid_table.c entry. The chip side gives it the
 *          AMCGPIO registers, the amcgpio_in / amcgpio_notify hooks and
 *          rage128_chip_init. On a single card nothing is attached to the
 *          pads: amcgpio_in stays 0 and amcgpio_notify is not set.
 *
 *          Relevant literature:
 *
 *          [1] ATI Technologies, "RAGE 128 PRO Register Reference Guide",
 *              RRG-G04500-C Rev 1.01, January 2000. Cited below as
 *              "RRG: <register>, p. <printed page> / PDF <viewer page>".
 *
 *          [2] ATI Technologies, "RAGE 128 VR / RAGE 128 GL Graphics
 *              Controller Specifications", GCS-C04100 Rev 0.05, November
 *              1998 (the board straps). Cited as "GCS: ...".
 *
 *          [3] NXP Semiconductors, "PCF8591 8-bit A/D and D/A converter",
 *              product data sheet, Rev. 7, 27 June 2013. Cited as
 *              "PCF8591 data sheet, <section, table or figure>".
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/i2c.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/plat.h>
#include <86box/rom.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include "cpu.h"
#include <86box/vid_ati_rage128.h>
#include <86box/vid_ati_rage128_regs.h>

#define MAXX_ROM_PATH "roms/video/ati_rage128/FuryMAXX_zerostate.VBI"

/* PCI subsystem ids. A chip loads its subsystem vendor and id from ROM
   bytes 0x70-0x73 after PCI reset and, with no ROM, defaults them to
   1002h and its own device id (GCS: ROM Based Straps, p. 4-35 / PDF 83).
   Chip A reads 1002:2000 from the board ROM. Chip B has no ROM here, so
   the board supplies 1002:2001 through subsys_strap: the driver INF
   (ATII9XAA.INF) binds that id as its "Secondary" device. */
#define MAXX_SUBSYS_SECONDARY (0x20010000u | RAGE128_PCI_VENDOR)

/* Board pins on the AMCGPIO pads. AMCGPIO_MASK makes the same bit of
   AMCGPIO_A and AMCGPIO_EN take effect (RRG: AMCGPIO_MASK, p. 3-209 /
   PDF 227). AMCGPIO_A sets the level a pad drives and AMCGPIO_Y reads
   the level on the pin (RRG: AMCGPIO_A_REG and AMCGPIO_Y_REG, p. 3-211
   / PDF 229), and AMCGPIO_EN makes the pad an output (RRG:
   AMCGPIO_EN_REG, p. 3-212 / PDF 230). The guide lists pads 12-19 only
   as "DVS data in", and the chip documents do not say what the MAXX
   wires to any pad; the meanings below are how the driver uses each
   pin. It sets MASK to 0x000ff0ff and EN to 0xff on both chips, so pads
   0-7 are outputs and 12-19 inputs (RE: ATI3DRAU.DLL @b00b55ae).

   Outputs. Pad 0 of either chip asks for the connector: the flip queues
   AMCGPIO_A = V and then V & ~1 in the command stream of the chip that
   rendered the frame, with V = 1 on chip B and 0xd on chip A, or 9 on
   chip A for a flip that does not wait for vsync (RE: ATI3DRAU.DLL
   @b00bf810). Pad 2 of chip A is therefore read as "switch at vsync".
   Pad 3 of chip A stays high once alternate-frame rendering is set up.

   Inputs on chip A's AMCGPIO_Y. Bit 12: the driver's CRTC-restart lock
   method retries while it is 1, so it reads as "out of sync". Bit 14:
   the PLL-slip lock method, when bits 14 and 15 are both 1, sets a
   phase slip on chip B's PLL and waits for bit 14 to clear, so 14
   reads as "phase mismatch" and 15 as "sync from the other chip
   present" (RE: ATI2I9AU.DLL @0002:6e30 and @0002:6ec9). Bit 16 picks
   which chip the driver hands the connector to first (RE: ATI3DRAU.DLL
   @b00b55ae; ATI2I9AU.DLL @0002:5c1a).

   Input on chip B's AMCGPIO_Y: bit 14 is 1 while the connector shows
   chip B; the flip reads it to know which chip is on screen. */
#define MAXX_PIN_SHOW        (1u << 0)
#define MAXX_PIN_AT_VSYNC    (1u << 2)
#define MAXX_A_Y_OUT_OF_SYNC (1u << 12)
#define MAXX_A_Y_MISMATCH    (1u << 14)
#define MAXX_A_Y_SIBLING     (1u << 15)
#define MAXX_A_Y_STRAP       (1u << 16)
#define MAXX_B_Y_SHOWN       (1u << 14)

enum { MAXX_A = 0,
       MAXX_B = 1 };

/* The PCF8591 on the board, an 8-bit D/A converter with four A/D
   inputs. Its address byte is 1 0 0 1 A2 A1 A0 R/W, with A2 to A0 set
   by three pins (PCF8591 data sheet, Table 5 I2C slave address byte).
   The ATI Windows 98 driver (driver CD 1.10) sends it address byte 0x98
   through chip B's I2C master, so it is modeled on chip B's bus at
   7-bit address 0x4C, the pins strapped 1 0 0. The chip documents do
   not describe this part of the board; the bus and the strap are taken
   from what the driver sends.

   The driver uses the D/A half only. Each of its writes is the address,
   the control byte 0x40, which turns the analog output on (data sheet,
   Fig 4 Control byte), and one D/A value. What the output voltage feeds
   on the board is not documented, and nothing in the emulator takes it:
   the model keeps the part's registers and answers the bus the way the
   data sheet describes, and no pixel depends on either register. */
#define MAXX_PCF8591_ADDR     0x4c
#define MAXX_PCF8591_AUTO_INC (1u << 2)

typedef struct maxx_pcf8591_t {
    uint8_t control;     /* control register: the last control byte */
    uint8_t dac;         /* DAC data register: the last D/A value */
    uint8_t adc;         /* ADC data register: the result of the last
                            A/D conversion */
    uint8_t tx;          /* the result being sent in a read: the ADC data
                            register as it stood before the conversion
                            now running */
    uint8_t tx_first;    /* a read has started and none of its bytes
                            has been sent yet */
    uint8_t channel;     /* the A/D channel the next conversion takes */
    uint8_t got_control; /* this write has stored its control byte, so
                            the bytes that follow are D/A values */
} maxx_pcf8591_t;

typedef struct r128_maxx_t {
    rage128_t *chip[2];
    void (*chip_vsync)(svga_t *svga); /* the chips' own vsync callback */

    int shown;        /* which chip's CRTC feeds the connector */
    int want;         /* the chip the show pads ask for; switched in
                         at that chip's next vsync */
    uint32_t pins[2]; /* pad levels at the last latch write, per chip */
    int      locked;  /* chip B's raster is locked to chip A's */
    int      log;     /* R128_MAXX_LOG set: log pad writes, switches,
                         lock changes and BIOS_1_SCRATCH */

    uint32_t scratch1[2]; /* BIOS_1_SCRATCH last logged, per chip */

    maxx_pcf8591_t pcf8591; /* the converter on chip B's I2C bus */
} r128_maxx_t;

/* The state the part comes up in when the board is powered. The
   control register is all zeros after a power-on reset, which leaves
   the analog output off and selects channel 0 (data sheet, 8.2 Control
   byte), and the first byte read after power-on is 80h (data sheet, 8.4
   A/D conversion). The data sheet does not give the power-on value of
   the DAC data register; 0x00 is modeled. */
static void
maxx_pcf8591_power_on(maxx_pcf8591_t *part)
{
    part->control     = 0x00;
    part->dac         = 0x00;
    part->adc         = 0x80;
    part->tx          = 0x80;
    part->tx_first    = 0;
    part->channel     = 0;
    part->got_control = 0;
}

/* The highest A/D channel the control register's input programming has:
   four single-ended inputs give channels 0 to 3, three differential
   inputs and the mixed arrangement give 0 to 2, and two differential
   inputs give 0 and 1 (data sheet, Fig 4 Control byte, bits 5:4). */
static uint8_t
maxx_pcf8591_top_channel(const maxx_pcf8591_t *part)
{
    static const uint8_t top[4] = { 3, 2, 2, 1 };

    return top[(part->control >> 4) & 3];
}

/* One A/D conversion of the selected channel. The four analog inputs
   are modeled at 0 V, the level of the analog ground pin, so every
   channel converts to 0x00, single-ended or differential (data sheet,
   Fig 9 and Fig 10, the A/D conversion characteristics). What the board
   wires to the inputs is not documented, and the driver never reads the
   part. With the auto-increment flag set the channel number then goes
   up by one, and from the highest channel of the input programming back
   to 0 (data sheet, 8.2 Control byte). */
static void
maxx_pcf8591_convert(maxx_pcf8591_t *part)
{
    part->adc = 0x00;
    if (part->control & MAXX_PCF8591_AUTO_INC)
        part->channel = (part->channel >= maxx_pcf8591_top_channel(part)) ? 0 : part->channel + 1;
}

/* The three bus callbacks: start, write and read. The part needs none
   for the stop condition. The chip's I2C master calls them for 7-bit
   address 0x4C only, with its own mutex held, from the CPU thread or,
   for a register write that arrives in a command packet, from that
   chip's command thread. They touch nothing but the part's own state,
   so the master's mutex is the only lock they need.

   The part acknowledges its address in both directions. A start opens
   a new transfer, so a write begins again at the control byte. A read
   address starts an A/D conversion at its acknowledge, and the
   conversion runs while the result of the one before it is sent (data
   sheet, 8.4 A/D conversion, and Fig 8 A/D conversion sequence): the
   ADC data register as it stands is taken as the first byte to send,
   then the conversion replaces it. This happens whether or not the
   master goes on to read a byte. */
static uint8_t
maxx_pcf8591_start(UNUSED(void *bus), UNUSED(uint8_t addr), uint8_t read, void *priv)
{
    maxx_pcf8591_t *part = (maxx_pcf8591_t *) priv;

    if (read) {
        part->tx       = part->adc;
        part->tx_first = 1;
        maxx_pcf8591_convert(part);
    } else
        part->got_control = 0;
    return 1;
}

/* A write is the address, the control byte, then any number of data
   bytes, each acknowledged (data sheet, Fig 15 Bus protocol for write
   mode, D/A conversion). The first byte after the address goes to the
   control register (8.2 Control byte). Its low two bits pick the A/D
   channel; a channel number the input programming does not have picks
   the highest one it has. Every later byte goes to the DAC data
   register (8.3 D/A conversion), so the last one sent is the value the
   output holds. Bits 7 and 3 of the control byte must be written 0;
   what a 1 does is not documented, and both are kept as written with no
   effect. */
static uint8_t
maxx_pcf8591_write(UNUSED(void *bus), UNUSED(uint8_t addr), uint8_t data, void *priv)
{
    maxx_pcf8591_t *part = (maxx_pcf8591_t *) priv;

    if (!part->got_control) {
        uint8_t top;

        part->control     = data;
        part->got_control = 1;
        top               = maxx_pcf8591_top_channel(part);
        part->channel     = ((data & 3) > top) ? top : (data & 3);
    } else
        part->dac = data;
    return 1;
}

/* The bytes of a read (data sheet, Fig 16 Bus protocol for read mode,
   A/D conversion). The first byte is the result that was waiting when
   the read address arrived: what the last read left behind, or 80h
   after power-on (8.4 A/D conversion). The master's acknowledge of a
   byte starts the next conversion, and the byte that follows is the
   result of the conversion before that one (Fig 8 A/D conversion
   sequence); a master that asks for another byte has acknowledged the
   last, so the step is taken here, before the byte is sent. A read of
   one or more bytes, n of them, therefore makes n conversions, one at
   the address and one per byte acknowledged, and sends the result from
   before the read followed by the first n - 1 of its own. A read
   address with no byte after it still makes the one conversion at the
   address. */
static uint8_t
maxx_pcf8591_read(UNUSED(void *bus), UNUSED(uint8_t addr), void *priv)
{
    maxx_pcf8591_t *part = (maxx_pcf8591_t *) priv;

    if (!part->tx_first) {
        part->tx = part->adc;
        maxx_pcf8591_convert(part);
    }
    part->tx_first = 0;
    return part->tx;
}

/* Put the part on chip B's bus, or take it off. The bus keeps its
   device list without a lock of its own and the master walks that list
   with its mutex held, so the list changes under the same mutex: a
   transfer that chip B's command thread is running finishes first. */
static void
maxx_pcf8591_attach(r128_maxx_t *maxx, int attach)
{
    rage128_t *b = maxx->chip[MAXX_B];

    thread_wait_mutex(b->i2c_mtx);
    i2c_handler(attach, b->mm_i2c, MAXX_PCF8591_ADDR, 1, maxx_pcf8591_start, maxx_pcf8591_read,
                maxx_pcf8591_write, NULL, &maxx->pcf8591);
    thread_release_mutex(b->i2c_mtx);
}

static int
maxx_chip_index(const r128_maxx_t *maxx, const rage128_t *dev)
{
    return (dev == maxx->chip[MAXX_B]) ? MAXX_B : MAXX_A;
}

/* Board inputs on the pads: what a chip's AMCGPIO_Y shows for pins it
   does not drive. Modeled values: chip A's bit 16 is 1, so the driver
   gives chip A the connector first; bit 15 is always 1; bit 14 is 1
   until the lock is made; bit 12 (MAXX_A_Y_OUT_OF_SYNC) is never set.
   Chip B's bit 14 follows the connector. */
static void
maxx_update_inputs(r128_maxx_t *maxx)
{
    maxx->chip[MAXX_A]->amcgpio_in = MAXX_A_Y_STRAP | MAXX_A_Y_SIBLING
        | (maxx->locked ? 0 : MAXX_A_Y_MISMATCH);
    maxx->chip[MAXX_B]->amcgpio_in = (maxx->shown == MAXX_B) ? MAXX_B_Y_SHOWN : 0;
}

/* Route the connector: the shown chip's CRTC renders and blits. The
   other chip runs with the svga override on, which keeps its raster
   timing (vblank, current scanline, frame counter) and draws
   nothing. */
static void
maxx_route_output(r128_maxx_t *maxx)
{
    svga_set_override(&maxx->chip[MAXX_A]->svga, maxx->shown != MAXX_A);
    svga_set_override(&maxx->chip[MAXX_B]->svga, maxx->shown != MAXX_B);
    maxx_update_inputs(maxx);
}

/* A chip wrote one of its AMCGPIO latches: re-read its pad levels and
   decide which chip the show pads ask for. The request is modeled as a
   level with chip A first: A's pad 0 high asks for A, else B's pad 0
   high asks for B, and neither high keeps the last request. The
   driver's strobe raises pad 0 and lowers it again, so the request is
   taken at the first write and kept after the second. Chip A comes
   first because, in the emulator, the video BIOS POST leaves pads 0-3
   driven high, so during the Windows 98 re-POST of chip B both show
   pads are high at once; a rising-edge latch would hand the connector
   to chip B, whose CRTC output is blank at that point.

   The switch itself is made in maxx_vsync at the wanted chip's next
   vsync, so every frame on the connector is whole. The driver's "switch
   at vsync" pad and its raster lock point to a switch at chip A's
   vsync with both rasters in phase; the rasters here are not
   phase-locked, so the new chip starts at its own frame boundary, at
   most one of its frames after the request. */
static void
maxx_pins_changed(rage128_t *dev, void *priv)
{
    r128_maxx_t *maxx = (r128_maxx_t *) priv;
    int          n    = maxx_chip_index(maxx, dev);
    int          want = maxx->want;

    maxx->pins[n] = rage128_amcgpio_pins(dev);
    if (maxx->log)
        rage128_log("MAXX: chip %c latch MASK %08x EN %08x A %08x -> pins %08x, CRTC_OFFSET %08x\n",
                    'A' + n, dev->amcgpio_mask, dev->amcgpio_en, dev->amcgpio_a,
                    maxx->pins[n], dev->crtc_offset);
    if (maxx->pins[MAXX_A] & MAXX_PIN_SHOW)
        want = MAXX_A;
    else if (maxx->pins[MAXX_B] & MAXX_PIN_SHOW)
        want = MAXX_B;
    if (want != maxx->want) {
        maxx->want = want;
        if (maxx->log)
            rage128_log("MAXX: chip %c asks for the connector (A.a=%08x B.a=%08x) CRTC_OFFSET %08x, shown %c%s\n",
                        'A' + want, maxx->chip[MAXX_A]->amcgpio_a, maxx->chip[MAXX_B]->amcgpio_a,
                        maxx->chip[want]->crtc_offset, 'A' + maxx->shown,
                        (maxx->chip[MAXX_A]->amcgpio_a & MAXX_PIN_AT_VSYNC) ? " at-vsync" : "");
    }
}

/* Raster lock between the chips, modeled from the driver's two lock
   methods (RE: ATI2I9AU.DLL @0002:6e30 and @0002:6ec9). Both turn
   CRTC_EN (RRG: CRTC_GEN_CNTL, p. 3-62 / PDF 80) off and back on on
   both chips; the PLL-slip method then writes 0x00010000 to chip B's
   HTOTAL_CNTL, which sets HTOT_PPLL_SLIP to one VCO phase slip per
   HSYNC (RRG: HTOTAL_CNTL, p. 3-97 / PDF 115). So the lock is lost
   while either chip's CRTC_EN is 0 and made when chip B's HTOTAL_CNTL
   has bit 16 set with both CRTCs on; chip A's AMCGPIO_Y bit 14 reads 1
   while there is no lock. Sampled once per chip A frame, so a CRTC_EN
   gap that falls between two samples is not seen. */
static void
maxx_poll_genlock(r128_maxx_t *maxx)
{
    const rage128_t *a      = maxx->chip[MAXX_A];
    const rage128_t *b      = maxx->chip[MAXX_B];
    int              locked = maxx->locked;

    if (!(a->crtc_gen_cntl & RAGE128_CRTC_EN) || !(b->crtc_gen_cntl & RAGE128_CRTC_EN))
        locked = 0;
    else if (b->pll_regs[RAGE128_PLL_HTOTAL_CNTL] & (1u << 16))
        locked = 1;
    if (locked != maxx->locked) {
        maxx->locked = locked;
        maxx_update_inputs(maxx);
        if (maxx->log)
            rage128_log("MAXX: genlock %s (A CRTC_EN %d, B CRTC_EN %d, B HTOTAL_CNTL %08x)\n",
                        locked ? "locked" : "lost",
                        !!(a->crtc_gen_cntl & RAGE128_CRTC_EN), !!(b->crtc_gen_cntl & RAGE128_CRTC_EN),
                        b->pll_regs[RAGE128_PLL_HTOTAL_CNTL]);
    }
}

/* Log BIOS_1_SCRATCH when it changes. The guide reserves the register
   for the video BIOS (RRG: BIOS_1_SCRATCH, p. 3-85 / PDF 103). The MAXX
   BIOS keeps the ROM segment it runs from in the low word and the
   chip's own PCI bus/devfn in the high word, and checks the high word
   against its stored bus/devfn before it uses a chip's I/O base (RE:
   FuryMAXX_zerostate.VBI @c000:0308). The log line shows which chip
   was POSTed from which segment. */
static void
maxx_poll_scratch(r128_maxx_t *maxx)
{
    for (int n = 0; n < 2; n++) {
        uint32_t v = maxx->chip[n]->bios_scratch[1];

        if (v != maxx->scratch1[n]) {
            maxx->scratch1[n] = v;
            if (maxx->log)
                pclog("MAXX: chip %c BIOS_1_SCRATCH %08x (ROM seg %04x, bus/devfn %04x)\n",
                      'A' + n, v, v & 0xffff, v >> 16);
        }
    }
}

static void
maxx_vsync(svga_t *svga)
{
    rage128_t   *dev  = (rage128_t *) svga->priv;
    r128_maxx_t *maxx = (r128_maxx_t *) dev->amcgpio_priv;
    int          n    = maxx_chip_index(maxx, dev);

    maxx->chip_vsync(svga);

    if (maxx->want == n && maxx->shown != n) {
        maxx->shown = n;
        maxx_route_output(maxx);
        if (maxx->log)
            rage128_log("MAXX: connector -> chip %c (frame %u)\n", 'A' + n, dev->frame_count);
    }
    if (n == MAXX_A) {
        maxx_poll_genlock(maxx);
        maxx_poll_scratch(maxx);
    }
}

/* The bus reset, which resets both chips and the logic between them.
   The PCF8591 has no reset pin (data sheet, Table 4 Pin description);
   the one reset it has is its own power-on reset (8.2 Control byte).
   So its registers are left as they are. */
static void
maxx_reset(void *priv)
{
    r128_maxx_t *maxx = (r128_maxx_t *) priv;

    rage128_chip_reset(maxx->chip[MAXX_A]);
    rage128_chip_reset(maxx->chip[MAXX_B]);
    maxx->shown   = MAXX_A;
    maxx->want    = MAXX_A;
    maxx->pins[0] = maxx->pins[1] = 0;
    maxx->locked                  = 0;
    maxx_route_output(maxx);
}

static void *
maxx_init(const device_t *info)
{
    r128_maxx_t *maxx = calloc(1, sizeof(r128_maxx_t));
    const char  *lg   = getenv("R128_MAXX_LOG");

    const rage128_chip_cfg_t cfg_a = {
        .vga_disabled = 0,
        .subsys_strap = 0,
        .rom_path     = MAXX_ROM_PATH,
        .cap_tag      = "A",
    };
    /* Modeled with one flash, wired to chip A. Chip B advertises no
       expansion ROM, so the system BIOS POSTs only chip A. That matters
       because the video BIOS init installs its INT 10h and INT 6Dh
       vectors with no test of any strap (RE: FuryMAXX_zerostate.VBI
       @c000:0378): whichever chip's copy runs last owns INT 10h. The
       driver's inter-driver module asks for the pair with INT 10h
       AX=A005 and uses the I/O base in the answer, which is the
       answering chip's own, for adapter 1, its primary (RE: ATI2I9AU.DLL
       @0002:0821; BIOS side RE: FuryMAXX_zerostate.VBI @c000:55b4). So
       INT 10h has to stay with chip A, the VGA chip. In the emulator,
       Windows 98 then re-POSTs chip B from the same image. */
    const rage128_chip_cfg_t cfg_b = {
        .vga_disabled = 1,
        .subsys_strap = MAXX_SUBSYS_SECONDARY,
        .rom_path     = NULL,
        .cap_tag      = "B",
    };

    maxx->log = lg && lg[0] && strcmp(lg, "0") != 0;

    /* The machine registers one AGP slot, and the board puts two
       devices on it. Register a second slot at the next device number
       on the same bus before either chip queues for a slot; chip A
       queues first and takes the AGP slot, chip B the one after it. */
    if (!pci_register_sibling_slot(PCI_CARD_AGP))
        pclog("MAXX: no AGP slot to pair with; the secondary chip stays off the bus\n");

    maxx->chip[MAXX_A] = rage128_chip_init(info, &cfg_a);
    maxx->chip[MAXX_B] = rage128_chip_init(info, &cfg_b);

    /* Wire the pads and take over the vsync hooks. rage128_chip_init
       gives both chips the same vsync callback, so chip A's pointer
       serves both; maxx_vsync runs it first and does the board work
       after it. */
    maxx->chip_vsync = maxx->chip[MAXX_A]->svga.vsync_callback;
    for (int n = 0; n < 2; n++) {
        maxx->chip[n]->amcgpio_notify      = maxx_pins_changed;
        maxx->chip[n]->amcgpio_priv        = maxx;
        maxx->chip[n]->svga.vsync_callback = maxx_vsync;
        maxx->scratch1[n]                  = 0;
    }

    /* The emulator creates the board when the machine is powered on,
       and again on a hard reset, so the converter starts in its
       power-on state here. */
    maxx_pcf8591_power_on(&maxx->pcf8591);
    maxx_pcf8591_attach(maxx, 1);

    maxx->shown  = MAXX_A;
    maxx->want   = MAXX_A;
    maxx->locked = 0;
    maxx_route_output(maxx);

    rage128_log("MAXX: two chips, A subsys %04x (VGA), B subsys %04x (VGA_DISABLE)\n",
                maxx->chip[MAXX_A]->pci_subsys_id, maxx->chip[MAXX_B]->pci_subsys_id);
    return maxx;
}

static void
maxx_close(void *priv)
{
    r128_maxx_t *maxx = (r128_maxx_t *) priv;

    /* Chip B's close removes its I2C bus, so the part comes off first. */
    maxx_pcf8591_attach(maxx, 0);
    rage128_chip_close(maxx->chip[MAXX_B]);
    rage128_chip_close(maxx->chip[MAXX_A]);
    free(maxx);
}

static int
maxx_available(void)
{
    return rom_present(MAXX_ROM_PATH);
}

static void
maxx_speed_changed(void *priv)
{
    r128_maxx_t *maxx = (r128_maxx_t *) priv;

    rage128_chip_speed_changed(maxx->chip[MAXX_A]);
    rage128_chip_speed_changed(maxx->chip[MAXX_B]);
}

static void
maxx_force_redraw(void *priv)
{
    r128_maxx_t *maxx = (r128_maxx_t *) priv;

    rage128_chip_force_redraw(maxx->chip[maxx->shown]);
}

const device_t ati_rage128_maxx_device = {
    .name          = "ATI Rage Fury MAXX AGP",
    .internal_name = "ati_rage128_maxx",
    .flags         = DEVICE_AGP,
    .local         = 0,
    .init          = maxx_init,
    .close         = maxx_close,
    .reset         = maxx_reset,
    .available     = maxx_available,
    .speed_changed = maxx_speed_changed,
    .force_redraw  = maxx_force_redraw,
    .config        = rage128_config
};
