/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IBM PS/2 Speech Adapter (ISA). The ROM listing calls this card
 *          the "TALKER II".
 *
 *          The ISA version of the card uses a 4 MHz oscillator for its
 *          timers rather than the PCjr version's 4.77 MHz bus clock, and
 *          interrupts on IRQ 7 rather than IRQ 1.
 *
 *          Y1   - 4 MHz oscillator
 *          ZM8  - TMS5220CNL
 *          ZM9  - 8254 PIT
 *          ZM10 - 32Kx8 mask ROM
 *          ZM22 - 8255A PPI
 *          MC3418 CVSD decoder
 *
 *          Ported from MAME's ibm_speech.cpp.
 *
 * Authors: Katherine Rohl,
 *
 *          Copyright Katherine Rohl.
 *          Distributed under the BSD-3-Clause license.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/pit_fast.h>
#include <86box/ppi.h>
#include <86box/sound.h>
#include <86box/snd_tms5220.h>
#include <86box/snd_cvsd.h>
#include <86box/video.h>
#include <86box/plat_unused.h>

#define IBM_SPEECH_ROM   "roms/sound/ibm_speech/68x2838.zm10"
#define IBM_SPEECH_IRQ   7
#define IBM_SPEECH_XTAL  4000000.0
#define IBM_SPEECH_LPC   640000 /* TODO: confirm the TMS5220 clock */

#ifdef ENABLE_IBM_SPEECH_LOG
int ibm_speech_do_log = ENABLE_IBM_SPEECH_LOG;

static void
ibm_speech_log(const char *fmt, ...)
{
    va_list ap;

    if (ibm_speech_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ibm_speech_log(fmt, ...)
#endif

typedef struct ibm_speech_t {
    i8255_t   *ppi;
    pitf_t    *pit;
    tms5220_t *lpc;
    mc3417_t   cvsd;

    mem_mapping_t rom_mapping;
    uint8_t       rom[0x8000];
    uint8_t       rom_page;

    int lpc_interrupt; /* /INT from the TMS5220 */
    int lpc_running;

    int cvsd_clock;
    int cvsd_frame;
    int cvsd_ed;

    int sr_clk;
    int beeper_level;

    int acl_int_ena;
    int acl_chan_ena;

    uint8_t channel_mux;
    uint8_t cvsd_sr_bits_remaining;
    uint8_t cvsd_sr_parallel;
    uint8_t cvsd_sr_serial;

    int16_t lpc_sample;

    int16_t buffer[SOUNDBUFLEN];
    int     pos;
} ibm_speech_t;

/******************************************************************************
    Audio output

    The channel mux selects one of four sources; only one is heard at a time.
******************************************************************************/
static void
ibm_speech_update(ibm_speech_t *dev)
{
    for (; dev->pos < sound_pos_global; dev->pos++) {
        /* Run the CVSD output every sample so its idle timeout keeps time. */
        const int16_t cvsd = mc3417_get_sample(&dev->cvsd);

        switch (dev->channel_mux) {
            case 0: /* LPC */
                dev->buffer[dev->pos] = dev->lpc_sample;
                break;
            case 1: /* CVSD */
                dev->buffer[dev->pos] = cvsd;
                break;
            case 2: /* 8254 channel 2 */
                dev->buffer[dev->pos] = dev->beeper_level ? 8192 : -8192;
                break;
            default: /* test signal, TODO: how is this generated? */
                dev->buffer[dev->pos] = 0;
                break;
        }
    }
}

static void
ibm_speech_get_buffer(int32_t *buffer, uint16_t len, void *priv)
{
    ibm_speech_t *dev = (ibm_speech_t *) priv;

    ibm_speech_update(dev);

    for (uint16_t c = 0; c < len; c++) {
        buffer[c << 1] += dev->buffer[c];
        buffer[(c << 1) + 1] += dev->buffer[c];
    }

    dev->pos = 0;
}

static void
ibm_speech_lpc_sample(int16_t sample, void *priv)
{
    ibm_speech_t *dev = (ibm_speech_t *) priv;

    ibm_speech_update(dev);
    dev->lpc_sample = sample;
}

/******************************************************************************
    ROM bankswitching

    The option ROM is 32Kx8 and divided into four 8K pages at CE000-CFFFF.
    Bits 2-3 of PPI port A select the visible page. All pages contain the
    option ROM signature.
******************************************************************************/
static uint8_t
ibm_speech_rom_read(uint32_t addr, void *priv)
{
    const ibm_speech_t *dev = (ibm_speech_t *) priv;

    return dev->rom[(dev->rom_page << 13) | (addr & 0x1fff)];
}

static uint16_t
ibm_speech_rom_readw(uint32_t addr, void *priv)
{
    return ibm_speech_rom_read(addr, priv) | (ibm_speech_rom_read(addr + 1, priv) << 8);
}

static uint32_t
ibm_speech_rom_readl(uint32_t addr, void *priv)
{
    return ibm_speech_rom_readw(addr, priv) | (ibm_speech_rom_readw(addr + 2, priv) << 16);
}

/******************************************************************************
    Channel mux
******************************************************************************/
static void
ibm_speech_channel_mux_w(ibm_speech_t *dev, uint8_t val)
{
    val &= 3;
    if (val != dev->channel_mux) {
        ibm_speech_update(dev);
        ibm_speech_log("IBM Speech: channel mux now %d\n", val);
        dev->channel_mux = val;
    }
}

/******************************************************************************
    TMS5220 interrupt

    In interrupt mode (INT_ENA set), a falling edge of the 5220's /INT
    raises IRQ 7 and opens the PIT channel 2 gate, triggering its one-shot.
******************************************************************************/
static void
ibm_speech_lpc_irq(int state, void *priv)
{
    ibm_speech_t *dev = (ibm_speech_t *) priv;

    if (dev->acl_int_ena) {
        if (dev->lpc_interrupt && !state) {
            ibm_speech_log("IBM Speech: raising IRQ %i\n", IBM_SPEECH_IRQ);
            picint(1 << IBM_SPEECH_IRQ);
            pitf_ctr_set_gate(dev->pit, 2, 1);
        } else if (state) {
            picintc(1 << IBM_SPEECH_IRQ);
            pitf_ctr_set_gate(dev->pit, 2, 0);
        }
    }

    dev->lpc_interrupt = state;
}

/******************************************************************************
    CVSD shift register

    The host sees the parallel side, the CVSD the serial side.
******************************************************************************/
static void
ibm_speech_cvsd_frame_w(ibm_speech_t *dev, int state)
{
    if (state && (dev->cvsd_sr_bits_remaining == 0))
        dev->cvsd_sr_serial = dev->cvsd_sr_parallel;

    dev->cvsd_frame = state;
}

static void
ibm_speech_cvsd_shiftreg_clk_w(ibm_speech_t *dev, int state)
{
    /* On the rising edge of the inverted CVSD clock, shift a bit out. */
    if (!dev->sr_clk && state && (dev->cvsd_sr_bits_remaining > 0)) {
        mc3417_digin_w(&dev->cvsd, dev->cvsd_sr_serial & 1);
        dev->cvsd_sr_serial >>= 1;
        dev->cvsd_sr_bits_remaining--;
    }

    dev->sr_clk = state;
}

static uint8_t
ibm_speech_shift_register_r(UNUSED(uint16_t port), void *priv)
{
    const ibm_speech_t *dev = (ibm_speech_t *) priv;

    return dev->cvsd_sr_parallel;
}

static void
ibm_speech_shift_register_w(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    ibm_speech_t *dev = (ibm_speech_t *) priv;

    if (dev->cvsd_sr_bits_remaining == 0) {
        dev->cvsd_sr_parallel       = val;
        dev->cvsd_sr_bits_remaining = 8;
        ibm_speech_cvsd_frame_w(dev, 1);
    } else
        ibm_speech_log("IBM Speech: CVSD shift register write %02X while busy\n", val);
}

/******************************************************************************
    PIT outputs

    Channel 0 is CVSD CLOCK: straight to the MC3418 clock, and inverted to
    the shift register clock and PPI PC3. Channel 1 is CVSD FRAME. Channel 2
    is INT CLOCK: a one-shot in interrupt mode, a beeper otherwise.
******************************************************************************/
static void
ibm_speech_pit_out0(int new_out, UNUSED(int old_out), void *priv)
{
    ibm_speech_t *dev = (ibm_speech_t *) ((pitf_t *) priv)->dev_priv;

    dev->cvsd_clock = !!new_out;

    ibm_speech_update(dev);
    mc3417_clock_w(&dev->cvsd, dev->cvsd_clock);
    ibm_speech_cvsd_shiftreg_clk_w(dev, !dev->cvsd_clock);
}

static void
ibm_speech_pit_out1(int new_out, UNUSED(int old_out), void *priv)
{
    ibm_speech_t *dev = (ibm_speech_t *) ((pitf_t *) priv)->dev_priv;

    ibm_speech_cvsd_frame_w(dev, !!new_out);
}

static void
ibm_speech_pit_out2(int new_out, UNUSED(int old_out), void *priv)
{
    ibm_speech_t *dev = (ibm_speech_t *) ((pitf_t *) priv)->dev_priv;

    if (!dev->acl_int_ena && (dev->beeper_level != !!new_out)) {
        ibm_speech_update(dev);
        dev->beeper_level = !!new_out;
    }
}

static void
ibm_speech_set_pit_clock(ibm_speech_t *dev)
{
    pitf_set_pit_const(dev->pit, (uint64_t) ((cpuclock / IBM_SPEECH_XTAL) * (double) (1ULL << 32)));
}

/******************************************************************************
    Audio Control Latch
******************************************************************************/
static uint8_t
ibm_speech_acl_r(UNUSED(uint16_t port), void *priv)
{
    const ibm_speech_t *dev = (ibm_speech_t *) priv;

    /* The bits are inverted when reading. */
    return (dev->acl_int_ena ? 0x00 : 0x02) | (dev->acl_chan_ena ? 0x00 : 0x01);
}

static void
ibm_speech_acl_w(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    ibm_speech_t *dev = (ibm_speech_t *) priv;

    dev->acl_int_ena  = !!(val & 0x02);
    dev->acl_chan_ena = !!(val & 0x01);

    /* With interrupts enabled, channel 2 is a one-shot whose gate the 5220
       opens; otherwise it is a beeper with its gate held open. */
    pitf_ctr_set_gate(dev->pit, 2, !dev->acl_int_ena);
}

/******************************************************************************
    PPI port A

    PA0-1: channel mux       (O)
    PA2-3: ROM page          (O)
    PA7:   LPC in progress   (I/O)
******************************************************************************/
static uint8_t
ibm_speech_ppi_in_pa(void *priv)
{
    const ibm_speech_t *dev = (ibm_speech_t *) priv;

    return dev->lpc_running << 7;
}

static void
ibm_speech_ppi_out_pa(uint8_t val, void *priv)
{
    ibm_speech_t *dev = (ibm_speech_t *) priv;

    ibm_speech_channel_mux_w(dev, val & 3);
    dev->rom_page    = (val >> 2) & 3;
    dev->lpc_running = !!(val & 0x80);
}

/* PPI port B is wired straight to the TMS5220 data bus. */
static uint8_t
ibm_speech_ppi_in_pb(void *priv)
{
    const ibm_speech_t *dev = (ibm_speech_t *) priv;

    return tms5220_status_r(dev->lpc);
}

static void
ibm_speech_ppi_out_pb(uint8_t val, void *priv)
{
    const ibm_speech_t *dev = (ibm_speech_t *) priv;

    /* The 5220 only latches the bus when port B drives it; a switch of port
       B to input (to read status) is not a write. */
    if (!(dev->ppi->control & 0x02))
        tms5220_data_w(dev->lpc, val);
}

static uint8_t
ibm_speech_ppi_tri(UNUSED(void *priv))
{
    return 0x00;
}

/******************************************************************************
    PPI port C

    PC0: -LPC READY   (I)
    PC1: -LPC INT     (I)
    PC2: CVSD FRAME   (I)
    PC3: -CVSD CLOCK  (I)
    PC4: LPC RS       (O) (5220 /RS, inverted)
    PC5: LPC WS       (O) (5220 /WS, inverted)
    PC6: CVSD E/D     (I)
******************************************************************************/
static uint8_t
ibm_speech_ppi_in_pc(void *priv)
{
    const ibm_speech_t *dev = (ibm_speech_t *) priv;
    uint8_t             ret = 0x00;

    ret |= tms5220_readyq_r(dev->lpc) << 0;
    ret |= tms5220_intq_r(dev->lpc) << 1;
    ret |= dev->cvsd_frame << 2;

    /* The clock is held until the shift register needs a new byte. */
    if (dev->cvsd_sr_bits_remaining == 0)
        ret |= (!dev->cvsd_clock) << 3;

    ret |= dev->cvsd_ed << 6; /* 0: CVSD playback, 1: CVSD record */

    return ret;
}

static void
ibm_speech_ppi_out_pc(uint8_t val, void *priv)
{
    const ibm_speech_t *dev = (ibm_speech_t *) priv;

    tms5220_rsq_w(dev->lpc, !(val & 0x10));
    tms5220_wsq_w(dev->lpc, !(val & 0x20));
}

static uint8_t
ibm_speech_ppi_read(uint16_t port, void *priv)
{
    ibm_speech_t *dev = (ibm_speech_t *) priv;

    return i8255_read(dev->ppi, port & 3);
}

static void
ibm_speech_ppi_write(uint16_t port, uint8_t val, void *priv)
{
    ibm_speech_t *dev = (ibm_speech_t *) priv;

    i8255_write(dev->ppi, port & 3, val);
}

/******************************************************************************
    Device
******************************************************************************/
static void
ibm_speech_reset(void *priv)
{
    ibm_speech_t *dev = (ibm_speech_t *) priv;

    ibm_speech_update(dev);

    picintc(1 << IBM_SPEECH_IRQ);

    i8255_reset(dev->ppi);
    tms5220_reset(dev->lpc);
    mc3417_reset(&dev->cvsd);

    dev->rom_page               = 0;
    dev->channel_mux            = 0;
    dev->lpc_interrupt          = 1;
    dev->lpc_running            = 0;
    dev->acl_int_ena            = 0;
    dev->acl_chan_ena           = 0;
    dev->sr_clk                 = 0;
    dev->cvsd_ed                = 0;
    dev->beeper_level           = 0;
    dev->cvsd_sr_bits_remaining = 0;

    /* Gates 0 and 1 are pulled high; gate 2 is open in beeper mode. */
    for (int i = 0; i < 3; i++)
        pitf_ctr_set_gate(dev->pit, i, 1);
}

static void *
ibm_speech_init(UNUSED(const device_t *info))
{
    ibm_speech_t *dev = (ibm_speech_t *) calloc(1, sizeof(ibm_speech_t));

    rom_load_linear(IBM_SPEECH_ROM, 0x00000, 0x8000, 0, dev->rom);
    mem_mapping_add(&dev->rom_mapping, 0xce000, 0x2000,
                    ibm_speech_rom_read, ibm_speech_rom_readw, ibm_speech_rom_readl,
                    NULL, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, dev);

    dev->lpc = tms5220_init(TMS5220_TYPE_5220C, IBM_SPEECH_LPC,
                            ibm_speech_lpc_sample, ibm_speech_lpc_irq, dev);
    mc3417_init(&dev->cvsd, CVSD_MC3418);

    dev->ppi = device_add(&i8255_device);
    i8255_set_priv(dev->ppi, dev);
    i8255_set_in(dev->ppi, I8255_PORT_A, ibm_speech_ppi_in_pa);
    i8255_set_in(dev->ppi, I8255_PORT_B, ibm_speech_ppi_in_pb);
    i8255_set_in(dev->ppi, I8255_PORT_C, ibm_speech_ppi_in_pc);
    i8255_set_out(dev->ppi, I8255_PORT_A, ibm_speech_ppi_out_pa);
    i8255_set_out(dev->ppi, I8255_PORT_B, ibm_speech_ppi_out_pb);
    i8255_set_out(dev->ppi, I8255_PORT_C, ibm_speech_ppi_out_pc);
    /* Undriven port A and C pins read low, so a reset leaves ROM page 0,
       the LPC channel, and /RS and /WS inactive. */
    i8255_set_tri(dev->ppi, I8255_PORT_A, ibm_speech_ppi_tri);
    i8255_set_tri(dev->ppi, I8255_PORT_C, ibm_speech_ppi_tri);

    /* All three timer channels are clocked by the 4 MHz crystal. */
    dev->pit           = device_add(&i8254_ext_io_fast_device);
    dev->pit->dev_priv = dev;
    ibm_speech_set_pit_clock(dev);
    pitf_ctr_set_out_func(dev->pit, 0, ibm_speech_pit_out0);
    pitf_ctr_set_out_func(dev->pit, 1, ibm_speech_pit_out1);
    pitf_ctr_set_out_func(dev->pit, 2, ibm_speech_pit_out2);
    for (int i = 0; i < 3; i++)
        pitf_ctr_set_using_timer(dev->pit, i, 1);

    /* None of the addresses are configurable. */
    io_sethandler(0xfb98, 0x0004,
                  ibm_speech_ppi_read, NULL, NULL, ibm_speech_ppi_write, NULL, NULL, dev);
    pitf_handler(1, 0xfb9c, 0x0004, dev->pit);
    io_sethandler(0xff98, 0x0001,
                  ibm_speech_shift_register_r, NULL, NULL, ibm_speech_shift_register_w, NULL, NULL, dev);
    io_sethandler(0xff9f, 0x0001,
                  ibm_speech_acl_r, NULL, NULL, ibm_speech_acl_w, NULL, NULL, dev);

    sound_add_handler(ibm_speech_get_buffer, dev);

    ibm_speech_reset(dev);

    return dev;
}

static void
ibm_speech_close(void *priv)
{
    ibm_speech_t *dev = (ibm_speech_t *) priv;

    tms5220_close(dev->lpc);
    free(dev);
}

static void
ibm_speech_speed_changed(void *priv)
{
    ibm_speech_t *dev = (ibm_speech_t *) priv;

    ibm_speech_set_pit_clock(dev);
}

static int
ibm_speech_available(void)
{
    return rom_present(IBM_SPEECH_ROM);
}

const device_t ibm_speech_device = {
    .name          = "IBM PS/2 Speech Adapter",
    .internal_name = "ibm_speech",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = ibm_speech_init,
    .close         = ibm_speech_close,
    .reset         = ibm_speech_reset,
    .available     = ibm_speech_available,
    .speed_changed = ibm_speech_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};
